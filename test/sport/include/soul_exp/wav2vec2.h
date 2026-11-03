#ifndef SOUL_EXP_WAV2VEC2
#define SOUL_EXP_WAV2VEC2

// wav2vec2-base 前向（冻结推理）：复刻 HuggingFace `Wav2Vec2Model` 的
//   feature_extractor（7× conv1d + GroupNorm(1) + GELU）
// → feature_projection（LayerNorm + Linear 512→768）
// → encoder（pos_conv_embed grouped conv1d + 12× 非因果多头自注意力 + FFN）。
//
// 权重由 weight_loader.h 从 manifest.json + weights.bin 载入（PyTorch state_dict
// 原布局、行优先 f32），卷积/线性权重布局与 xhqm tensor_base 天然对齐，线性层在载入时
// 预转置缓存 Wᵀ 免去每次前向转置。
//
// 对齐目标：与 PyTorch `Wav2Vec2Model(...).extract_features` 输出 ≤ 1e-5 相对误差。

#include "weight_loader.h"
#include "nn.h"   // gelu / layer_norm / linear / attention（共享原语）

#include <array>
#include <string>
#include <vector>

namespace soul_exp {

	// —— GroupNorm(num_groups=num_channels)：每通道独立归一化（即 InstanceNorm），biased var=Σ/N，逐通道仿射 ——
	// 对应 HF 的 nn.GroupNorm(num_groups=out_conv_dim, num_channels=out_conv_dim)。
	template<class type>
	inline xhqm::tensor<3, type> group_norm(const xhqm::tensor<3, type>& x,
		const xhqm::tensor<1, type>& weight, const xhqm::tensor<1, type>& bias, type eps)
	{
		const xhqm::size N = x.dimension(0), C = x.dimension(1), L = x.dimension(2);
		xhqm::tensor<3, type> y(std::array<xhqm::size, 3>{ N, C, L }, type(0));
		for (xhqm::size n = 0; n < N; ++n)
			for (xhqm::size c = 0; c < C; ++c)
			{
				type mean = type(0);
				for (xhqm::size l = 0; l < L; ++l) mean += x(n, c, l);
				mean /= type(L);
				type var = type(0);
				for (xhqm::size l = 0; l < L; ++l) { const type d = x(n, c, l) - mean; var += d * d; }
				var /= type(L);                       // biased（nn.GroupNorm）
				const type rstd = type(1) / type(std::sqrt(var + eps));
				for (xhqm::size l = 0; l < L; ++l)
					y(n, c, l) = (x(n, c, l) - mean) * rstd * weight[c] + bias[c];
			}
		return y;
	}

	// —— (1, C, L) → (L, C)（batch=1 去批 + 转置）——
	template<class type>
	inline xhqm::tensor<2, type> to_seq_features(const xhqm::tensor<3, type>& x)
	{
		const xhqm::size C = x.dimension(1), L = x.dimension(2);
		xhqm::tensor<2, type> y(std::array<xhqm::size, 2>{ L, C }, type(0));
		for (xhqm::size l = 0; l < L; ++l)
			for (xhqm::size c = 0; c < C; ++c)
				y(l, c) = x(0, c, l);
		return y;
	}
	// —— (L, C) → (1, C, L) ——
	template<class type>
	inline xhqm::tensor<3, type> to_channels_first(const xhqm::tensor<2, type>& x)
	{
		const xhqm::size L = x.dimension(0), C = x.dimension(1);
		xhqm::tensor<3, type> y(std::array<xhqm::size, 3>{ 1, C, L }, type(0));
		for (xhqm::size l = 0; l < L; ++l)
			for (xhqm::size c = 0; c < C; ++c)
				y(0, c, l) = x(l, c);
		return y;
	}

	// —— wav2vec2-base：冻结推理 ——
	template<class type = xhqm::f32>
	struct wav2vec2_base
	{
		// 超参（facebook/wav2vec2-base）
		static constexpr xhqm::size feat_layers = 7;
		static constexpr xhqm::size conv_dim     = 512;
		static constexpr xhqm::size hidden       = 768;
		static constexpr xhqm::size ffn          = 3072;
		static constexpr xhqm::size heads        = 12;
		static constexpr xhqm::size enc_layers   = 12;
		type eps = type(1e-5);

		// feature extractor
		xhqm::size feat_stride[feat_layers] = { 5, 2, 2, 2, 2, 2, 2 };
		std::vector<xhqm::tensor<3, type>> feat_conv_w;                 // (Cout, Cin, K)
		xhqm::tensor<1, type> feat_gn_w, feat_gn_b;                     // (512)，仅 layer 0 有 GroupNorm

		// feature projection
		xhqm::tensor<1, type> proj_ln_w, proj_ln_b;                     // (512)
		xhqm::tensor<2, type> proj_w_t;                                 // (512, 768)
		xhqm::tensor<1, type> proj_b;                                   // (768)

		// encoder pos_conv_embed（grouped conv1d）
		xhqm::tensor<3, type> pos_conv_w;                               // (768, 768, 128)
		xhqm::tensor<1, type> pos_conv_b;                               // (768)

		// encoder 输入 layer norm
		xhqm::tensor<1, type> enc_ln_w, enc_ln_b;                       // (768)

		// encoder layer
		struct enc_layer
		{
			xhqm::tensor<2, type> wq_t, wk_t, wv_t, wo_t;               // (768, 768)
			xhqm::tensor<1, type> bq, bk, bv, bo;                       // (768)
			xhqm::tensor<1, type> ln1_w, ln1_b;                         // layer_norm
			xhqm::tensor<2, type> ff1_t, ff2_t;                         // (768, 3072), (3072, 768)
			xhqm::tensor<1, type> ff1_b, ff2_b;
			xhqm::tensor<1, type> ln2_w, ln2_b;                         // final_layer_norm
		};
		std::vector<enc_layer> layers;

		void load(const std::string& dir)
		{
			const weight_table wt = load_weights(dir);

			feat_conv_w.resize(feat_layers);
			for (xhqm::size i = 0; i < feat_layers; ++i)
			{
				const std::string p = "feature_extractor.conv_layers." + std::to_string(i);
				feat_conv_w[i] = load_tensor<3, type>(wt, p + ".conv.weight");
			}
			// 仅 layer 0 有 GroupNorm（HF：首层 Wav2Vec2GroupNormConvLayer，其余为 NoLayerNormConvLayer）
			feat_gn_w = load_tensor<1, type>(wt, "feature_extractor.conv_layers.0.layer_norm.weight");
			feat_gn_b = load_tensor<1, type>(wt, "feature_extractor.conv_layers.0.layer_norm.bias");

			proj_ln_w = load_tensor<1, type>(wt, "feature_projection.layer_norm.weight");
			proj_ln_b = load_tensor<1, type>(wt, "feature_projection.layer_norm.bias");
			const xhqm::tensor<2, type> proj_w = load_tensor<2, type>(wt, "feature_projection.projection.weight");
			proj_w_t = xhqm::tensor_base::transpose(proj_w, 0, 1);
			proj_b = load_tensor<1, type>(wt, "feature_projection.projection.bias");

			// pos_conv_embed 的 conv 用了 weight_norm 重参数化：state_dict 里是
			// parametrizations.weight.original0(g) + original1(v)，有效权重 = v·(g/‖v‖)，
			// 其中范数是 norm_except_dim(v,2,dim=2) —— 对除 dim=2 外的所有维求 L2 范数
			// （即 ‖v[:,:,k]‖₂，形状 (1,1,K)），不是沿 dim=2 的每 (i,j) 切片范数。
			const xhqm::tensor<3, type> pos_g = load_tensor<3, type>(wt, "encoder.pos_conv_embed.conv.parametrizations.weight.original0"); // (1,1,128)
			const xhqm::tensor<3, type> pos_v = load_tensor<3, type>(wt, "encoder.pos_conv_embed.conv.parametrizations.weight.original1"); // (768,48,128)
			const xhqm::size K = pos_v.dimension(2);
			std::vector<type> norm_except(K, type(0));
			for (xhqm::size i = 0; i < pos_v.dimension(0); ++i)
				for (xhqm::size j = 0; j < pos_v.dimension(1); ++j)
					for (xhqm::size k = 0; k < K; ++k)
						norm_except[k] += pos_v(i, j, k) * pos_v(i, j, k);
			for (xhqm::size k = 0; k < K; ++k) norm_except[k] = type(std::sqrt(norm_except[k]));
			pos_conv_w = pos_v;
			for (xhqm::size i = 0; i < pos_v.dimension(0); ++i)
				for (xhqm::size j = 0; j < pos_v.dimension(1); ++j)
					for (xhqm::size k = 0; k < K; ++k)
						pos_conv_w(i, j, k) = pos_v(i, j, k) * (pos_g(0, 0, k) / norm_except[k]);
			pos_conv_b = load_tensor<1, type>(wt, "encoder.pos_conv_embed.conv.bias");

			enc_ln_w = load_tensor<1, type>(wt, "encoder.layer_norm.weight");
			enc_ln_b = load_tensor<1, type>(wt, "encoder.layer_norm.bias");

			layers.resize(enc_layers);
			for (xhqm::size i = 0; i < enc_layers; ++i)
			{
				const std::string p = "encoder.layers." + std::to_string(i);
				enc_layer& L = layers[i];
				L.wq_t = xhqm::tensor_base::transpose(load_tensor<2, type>(wt, p + ".attention.q_proj.weight"), 0, 1);
				L.wk_t = xhqm::tensor_base::transpose(load_tensor<2, type>(wt, p + ".attention.k_proj.weight"), 0, 1);
				L.wv_t = xhqm::tensor_base::transpose(load_tensor<2, type>(wt, p + ".attention.v_proj.weight"), 0, 1);
				L.wo_t = xhqm::tensor_base::transpose(load_tensor<2, type>(wt, p + ".attention.out_proj.weight"), 0, 1);
				L.bq = load_tensor<1, type>(wt, p + ".attention.q_proj.bias");
				L.bk = load_tensor<1, type>(wt, p + ".attention.k_proj.bias");
				L.bv = load_tensor<1, type>(wt, p + ".attention.v_proj.bias");
				L.bo = load_tensor<1, type>(wt, p + ".attention.out_proj.bias");
				L.ln1_w = load_tensor<1, type>(wt, p + ".layer_norm.weight");
				L.ln1_b = load_tensor<1, type>(wt, p + ".layer_norm.bias");
				L.ff1_t = xhqm::tensor_base::transpose(load_tensor<2, type>(wt, p + ".feed_forward.intermediate_dense.weight"), 0, 1);
				L.ff2_t = xhqm::tensor_base::transpose(load_tensor<2, type>(wt, p + ".feed_forward.output_dense.weight"), 0, 1);
				L.ff1_b = load_tensor<1, type>(wt, p + ".feed_forward.intermediate_dense.bias");
				L.ff2_b = load_tensor<1, type>(wt, p + ".feed_forward.output_dense.bias");
				L.ln2_w = load_tensor<1, type>(wt, p + ".final_layer_norm.weight");
				L.ln2_b = load_tensor<1, type>(wt, p + ".final_layer_norm.bias");
			}
		}

		struct output
		{
			xhqm::tensor<2, type> last_hidden_state;   // (seq, hidden)
			xhqm::tensor<3, type> extract_features;    // (1, conv_dim, feat_seq)
		};

		output forward(const xhqm::tensor<1, type>& input_values) const
		{
			const xhqm::size raw = input_values.dimension(0);

			// (1, 1, raw)
			xhqm::tensor<3, type> x(std::array<xhqm::size, 3>{ 1, 1, raw }, type(0));
			for (xhqm::size t = 0; t < raw; ++t) x(0, 0, t) = input_values[t];

			// feature extractor
			for (xhqm::size i = 0; i < feat_layers; ++i)
			{
				x = xhqm::tensor_base::conv1d(x, feat_conv_w[i], feat_stride[i], 0, 1, 1);
				if (i == 0) x = group_norm(x, feat_gn_w, feat_gn_b, eps);   // 仅 layer 0 归一化
				x = gelu(x);
			}
			const xhqm::tensor<3, type> extract = x;   // (1, 512, feat_seq)

			// (1, C, L) → (L, C)
			xhqm::tensor<2, type> h = to_seq_features(x);   // (feat_seq, 512)

			// feature projection
			h = layer_norm(h, proj_ln_w, proj_ln_b, eps);
			h = linear(h, proj_w_t, proj_b);                 // (feat_seq, 768)

			// encoder：pos_conv_embed（grouped conv1d，k=128 pad=64）
			// 偶数核 + 对称 pad 使卷积输出为 L+1（PyTorch 公式），对齐 residual 时取前 L 个时间步。
			const xhqm::size L = h.dimension(0);
			xhqm::tensor<3, type> pe = xhqm::tensor_base::conv1d(
				to_channels_first(h), pos_conv_w, pos_conv_b, 1, 64, 1, 16);   // (1, 768, L+1)
			xhqm::tensor<2, type> pos(std::array<xhqm::size, 2>{ L, hidden }, type(0));
			for (xhqm::size l = 0; l < L; ++l)
				for (xhqm::size c = 0; c < hidden; ++c)
					pos(l, c) = pe(0, c, l);
			pos = gelu(pos);
			h = h + pos;
			h = layer_norm(h, enc_ln_w, enc_ln_b, eps);

			// 12 encoder layers（post-norm：attention 残差 → layer_norm，FFN 残差 → final_layer_norm）
			for (const enc_layer& Ln : layers)
			{
				h = layer_norm(
					h + attention(h, Ln.wq_t, Ln.wk_t, Ln.wv_t, Ln.wo_t, Ln.bq, Ln.bk, Ln.bv, Ln.bo, heads),
					Ln.ln1_w, Ln.ln1_b, eps);
				h = layer_norm(
					h + linear(gelu(linear(h, Ln.ff1_t, Ln.ff1_b)), Ln.ff2_t, Ln.ff2_b),
					Ln.ln2_w, Ln.ln2_b, eps);
			}

			return output{ h, extract };
		}
	};

}

#endif
