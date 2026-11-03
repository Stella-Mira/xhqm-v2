#ifndef SOUL_EXP_DINOV2
#define SOUL_EXP_DINOV2

// DINOv2 前向（冻结推理）：复刻 HuggingFace `Dinov2Model`。
//   patch embed（Conv2d 3→768, k=14, s=14）→ [CLS] + 可学习位置编码
//   → 12× pre-norm transformer（layer_scale）→ 最终 LayerNorm → [CLS] 即图像向量。
// 序列统一 tensor<2>(seq, hidden)（单 batch=1 去批），复用 nn.h 的 gelu/layer_norm/linear/attention。

#include "weight_loader.h"
#include "nn.h"

#include <array>
#include <string>
#include <vector>

namespace soul_exp {

	template<class type = xhqm::f32>
	struct dinov2_base
	{
		static constexpr xhqm::size hidden = 768;
		static constexpr xhqm::size enc_layers = 12;
		static constexpr xhqm::size heads = 12;
		static constexpr xhqm::size ffn = 3072;
		static constexpr xhqm::size image_size = 518;
		static constexpr xhqm::size patch_size = 14;
		static constexpr xhqm::size grid = image_size / patch_size;      // 37
		static constexpr xhqm::size num_patches = grid * grid;           // 1369
		type eps = type(1e-6);   // DINOv2 layer_norm_eps（比 wav2vec2 的 1e-5 更小）

		// embeddings
		xhqm::tensor<3, type> cls_token;               // (1,1,768)
		xhqm::tensor<3, type> position_embeddings;     // (1,1370,768)
		xhqm::tensor<4, type> patch_proj_w;            // (768,3,14,14)  Conv2d
		xhqm::tensor<1, type> patch_proj_b;            // (768)

		// encoder layer
		struct layer
		{
			xhqm::tensor<1, type> norm1_w, norm1_b;
			xhqm::tensor<2, type> wq_t, wk_t, wv_t, wo_t;   // (768,768)
			xhqm::tensor<1, type> bq, bk, bv, bo;           // (768)
			xhqm::tensor<1, type> ls1;                      // layer_scale1.lambda1
			xhqm::tensor<1, type> norm2_w, norm2_b;
			xhqm::tensor<2, type> fc1_t, fc2_t;             // (768,3072),(3072,768)
			xhqm::tensor<1, type> fc1_b, fc2_b;
			xhqm::tensor<1, type> ls2;                      // layer_scale2.lambda1
		};
		std::vector<layer> layers;

		xhqm::tensor<1, type> final_ln_w, final_ln_b;       // (768)

		void load(const std::string& dir)
		{
			const weight_table wt = load_weights(dir);

			cls_token = load_tensor<3, type>(wt, "embeddings.cls_token");
			position_embeddings = load_tensor<3, type>(wt, "embeddings.position_embeddings");
			patch_proj_w = load_tensor<4, type>(wt, "embeddings.patch_embeddings.projection.weight");
			patch_proj_b = load_tensor<1, type>(wt, "embeddings.patch_embeddings.projection.bias");

			layers.resize(enc_layers);
			for (xhqm::size i = 0; i < enc_layers; ++i)
			{
				const std::string p = "encoder.layer." + std::to_string(i);
				layer& L = layers[i];
				L.norm1_w = load_tensor<1, type>(wt, p + ".norm1.weight");
				L.norm1_b = load_tensor<1, type>(wt, p + ".norm1.bias");
				L.wq_t = xhqm::tensor_base::transpose(load_tensor<2, type>(wt, p + ".attention.attention.query.weight"), 0, 1);
				L.wk_t = xhqm::tensor_base::transpose(load_tensor<2, type>(wt, p + ".attention.attention.key.weight"), 0, 1);
				L.wv_t = xhqm::tensor_base::transpose(load_tensor<2, type>(wt, p + ".attention.attention.value.weight"), 0, 1);
				L.wo_t = xhqm::tensor_base::transpose(load_tensor<2, type>(wt, p + ".attention.output.dense.weight"), 0, 1);
				L.bq = load_tensor<1, type>(wt, p + ".attention.attention.query.bias");
				L.bk = load_tensor<1, type>(wt, p + ".attention.attention.key.bias");
				L.bv = load_tensor<1, type>(wt, p + ".attention.attention.value.bias");
				L.bo = load_tensor<1, type>(wt, p + ".attention.output.dense.bias");
				L.ls1 = load_tensor<1, type>(wt, p + ".layer_scale1.lambda1");
				L.norm2_w = load_tensor<1, type>(wt, p + ".norm2.weight");
				L.norm2_b = load_tensor<1, type>(wt, p + ".norm2.bias");
				L.fc1_t = xhqm::tensor_base::transpose(load_tensor<2, type>(wt, p + ".mlp.fc1.weight"), 0, 1);
				L.fc2_t = xhqm::tensor_base::transpose(load_tensor<2, type>(wt, p + ".mlp.fc2.weight"), 0, 1);
				L.fc1_b = load_tensor<1, type>(wt, p + ".mlp.fc1.bias");
				L.fc2_b = load_tensor<1, type>(wt, p + ".mlp.fc2.bias");
				L.ls2 = load_tensor<1, type>(wt, p + ".layer_scale2.lambda1");
			}

			final_ln_w = load_tensor<1, type>(wt, "layernorm.weight");
			final_ln_b = load_tensor<1, type>(wt, "layernorm.bias");
		}

		struct output
		{
			xhqm::tensor<2, type> last_hidden_state;   // (1370, 768)
			xhqm::tensor<1, type> pooler;              // (768) = [CLS] 图像向量
		};

		output forward(const xhqm::tensor<4, type>& image) const
		{
			// patch embed：conv2d(3→768, k=14, s=14) → (1,768,37,37)
			xhqm::tensor<4, type> patches = xhqm::tensor_base::conv2d(image, patch_proj_w, patch_proj_b, patch_size, 0, 1, 1);

			// 展平 → (1369,768)，行 0 放 [CLS]
			xhqm::tensor<2, type> x(std::array<xhqm::size, 2>{ num_patches + 1, hidden }, type(0));
			for (xhqm::size c = 0; c < hidden; ++c) x(0, c) = cls_token(0, 0, c);
			for (xhqm::size h = 0; h < grid; ++h)
				for (xhqm::size w = 0; w < grid; ++w)
					for (xhqm::size c = 0; c < hidden; ++c)
						x(h * grid + w + 1, c) = patches(0, c, h, w);

			// + 位置编码
			for (xhqm::size p = 0; p < num_patches + 1; ++p)
				for (xhqm::size c = 0; c < hidden; ++c)
					x(p, c) += position_embeddings(0, p, c);

			// encoder（pre-norm + layer_scale）
			for (const layer& L : layers)
			{
				xhqm::tensor<2, type> a = attention(layer_norm(x, L.norm1_w, L.norm1_b, eps),
					L.wq_t, L.wk_t, L.wv_t, L.wo_t, L.bq, L.bk, L.bv, L.bo, heads);
				for (xhqm::size p = 0; p < num_patches + 1; ++p)
					for (xhqm::size c = 0; c < hidden; ++c)
						a(p, c) *= L.ls1[c];   // layer_scale1
				x = x + a;                     // residual 1

				xhqm::tensor<2, type> m = linear(
					gelu(linear(layer_norm(x, L.norm2_w, L.norm2_b, eps), L.fc1_t, L.fc1_b)),
					L.fc2_t, L.fc2_b);
				for (xhqm::size p = 0; p < num_patches + 1; ++p)
					for (xhqm::size c = 0; c < hidden; ++c)
						m(p, c) *= L.ls2[c];   // layer_scale2
				x = x + m;                     // residual 2
			}

			x = layer_norm(x, final_ln_w, final_ln_b, eps);

			xhqm::tensor<1, type> pooler(std::array<xhqm::size, 1>{ hidden }, type(0));
			for (xhqm::size c = 0; c < hidden; ++c) pooler[c] = x(0, c);

			return output{ x, pooler };
		}
	};

}

#endif
