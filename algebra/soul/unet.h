#ifndef XHQM_SOUL_UNET
#define XHQM_SOUL_UNET

// UNet 去噪器（AD 可训练模块）+ 2D 图像扩散训练/采样闭环。
// 结构：编码(conv2d+group_norm+silu → avg_pool 2×) ×2 → 瓶颈 → 解码(upsample → conv2d+gn+silu) ×2 → 输出 conv。
// 时间条件：把正弦时间嵌入广播成额外通道，concat 到输入通道（由调用方在纯张量层完成），UNet 内部不感知时间。
#include "../tensor/tensor.h"
#include "random.h"
#include "ad.h"
#include "diffusion.h"
#include <array>
#include <cmath>

namespace xhqm {
namespace soul {
namespace diffusion {

	// —— 纯张量工具：时间嵌入广播成 (N, td, H, W) ——
	template<class type>
	tensor<4, type> broadcast_time4d(const tensor<2, type>& t_emb, xhqm::size H, xhqm::size W)
	{
		const xhqm::size N = t_emb.dimension(0), td = t_emb.dimension(1);
		tensor<4, type> r(std::array<xhqm::size, 4>{ N, td, H, W }, type(0));
		for (xhqm::size n = 0; n < N; ++n)
			for (xhqm::size c = 0; c < td; ++c)
				for (xhqm::size p = 0; p < H * W; ++p)
					r(n, c, p / W, p % W) = t_emb(n, c);
		return r;
	}

	// —— 4D 通道拼接：a (N,Ca,H,W) + b (N,Cb,H,W) → (N,Ca+Cb,H,W) ——
	template<class type>
	tensor<4, type> concat_channels4d(const tensor<4, type>& a, const tensor<4, type>& b)
	{
		const xhqm::size N = a.dimension(0), Ca = a.dimension(1), Cb = b.dimension(1), H = a.dimension(2), W = a.dimension(3);
		tensor<4, type> r(std::array<xhqm::size, 4>{ N, Ca + Cb, H, W }, type(0));
		for (xhqm::size n = 0; n < N; ++n)
			for (xhqm::size p = 0; p < H * W; ++p)
			{
				const xhqm::size h = p / W, w = p % W;
				for (xhqm::size c = 0; c < Ca; ++c) r(n, c, h, w) = a(n, c, h, w);
				for (xhqm::size c = 0; c < Cb; ++c) r(n, Ca + c, h, w) = b(n, c, h, w);
			}
		return r;
	}

	// 4D 卷积权重 xavier 初始化（fan_in = Cin·KH·KW）
	template<class type>
	void xavier_conv4d(tensor<4, type>& w, uint32_t& seed)
	{
		const xhqm::size fan_in = w.dimension(1) * w.dimension(2) * w.dimension(3);
		const type bound = type(std::sqrt(6.0f / float(fan_in)));
		fill_uniform(w, -bound, bound, seed);
	}

	// —— UNet 去噪器：输入 (N, C_in+time_dim, H, W) → 预测噪声 (N, C_out, H, W) ——
	template<class type = f32>
	struct unet_denoiser : ad::module<type>
	{
		xhqm::size in_ch = 0, base_ch = 0, time_dim = 0, out_ch = 0, gn_groups = 4;

		void init(xhqm::size in_ch_, xhqm::size base_ch_, xhqm::size time_dim_, xhqm::size out_ch_, uint32_t& seed)
		{
			in_ch = in_ch_; base_ch = base_ch_; time_dim = time_dim_; out_ch = out_ch_;
			gn_groups = 4;
			this->params.clear();
			const xhqm::size din = in_ch + time_dim;
			// 3×3 卷积权重 (out, in, 3, 3) + bias (out)
			auto add_conv = [&](const std::string& nm, xhqm::size cin, xhqm::size cout, uint32_t& sd) {
				tensor<4, type> w(std::array<xhqm::size, 4>{ cout, cin, 3, 3 }, type(0)); xavier_conv4d(w, sd); this->add_param(nm + "w", w);
				tensor<1, type> b(std::array<xhqm::size, 1>{ cout }, type(0)); this->add_param(nm + "b", b);
			};
			auto add_gn = [&](const std::string& nm, xhqm::size c) {
				tensor<1, type> g(std::array<xhqm::size, 1>{ c }, type(1)); this->add_param(nm + "g", g);
				tensor<1, type> b(std::array<xhqm::size, 1>{ c }, type(0)); this->add_param(nm + "bb", b);
			};
			add_conv("d1", din, base_ch, seed);      add_gn("d1", base_ch);
			add_conv("d2", base_ch, 2 * base_ch, seed); add_gn("d2", 2 * base_ch);
			add_conv("md", 2 * base_ch, 2 * base_ch, seed); add_gn("md", 2 * base_ch);
			add_conv("u1", 2 * base_ch, base_ch, seed); add_gn("u1", base_ch);
			add_conv("u2", base_ch, base_ch, seed);      add_gn("u2", base_ch);
			add_conv("oc", base_ch, out_ch, seed);
		}

		// 前向构图：x (N, C_in+time_dim, H, W) → 噪声预测
		ad::value<type>* forward(ad::tape<type>& t, ad::value<type>* x)
		{
			this->register_params(t);
			auto block = [&](ad::value<type>* h, const std::string& nm, xhqm::size cg) {
				auto* c = ad::conv2d(t, h, this->node(nm + "w"), this->node(nm + "b"), 1, 1, 1);
				return ad::silu(t, ad::group_norm(t, c, this->node(nm + "g"), this->node(nm + "bb"), cg));
			};
			ad::value<type>* h = block(x, "d1", gn_groups);
			h = ad::avg_pool2d(t, h, 2, 2, 0);
			h = block(h, "d2", gn_groups);
			h = ad::avg_pool2d(t, h, 2, 2, 0);
			h = block(h, "md", gn_groups);
			h = ad::upsample_nearest2d(t, h, 2, 2);
			h = block(h, "u1", gn_groups);
			h = ad::upsample_nearest2d(t, h, 2, 2);
			h = block(h, "u2", gn_groups);
			return ad::conv2d(t, h, this->node("ocw"), this->node("ocb"), 1, 1, 1);
		}

		// 推理（纯张量，不建图）：采样用
		tensor<4, type> infer(const tensor<4, type>& x) const
		{
			auto w = [&](const std::string& nm) -> const tensor<4, type>& { return as_payload<4, type>(this->payload(nm))->value; };
			auto v = [&](const std::string& nm) -> const tensor<1, type>& { return as_payload<1, type>(this->payload(nm))->value; };
			auto block = [&](const tensor<4, type>& h, const std::string& nm, xhqm::size cg) {
				tensor<4, type> c = tensor_base::conv2d(h, w(nm + "w"), v(nm + "b"), 1, 1, 1);
				return silu(tensor_base::group_norm(c, v(nm + "g"), v(nm + "bb"), cg));
			};
			tensor<4, type> h = block(x, "d1", gn_groups);
			h = tensor_base::avg_pool2d(h, 2, 2, 0);
			h = block(h, "d2", gn_groups);
			h = tensor_base::avg_pool2d(h, 2, 2, 0);
			h = block(h, "md", gn_groups);
			h = tensor_base::upsample_nearest2d(h, 2, 2);
			h = block(h, "u1", gn_groups);
			h = tensor_base::upsample_nearest2d(h, 2, 2);
			h = block(h, "u2", gn_groups);
			return tensor_base::conv2d(h, w("ocw"), v("ocb"), 1, 1, 1);
		}
	};

	// —— 2D 图像训练一步：采样 t/ε，加噪，concat 时间通道，构图，MSE，Adam；返回 loss ——
	template<class type>
	type train_step_image(unet_denoiser<type>& model, ad::adam_optimizer<type>& opt,
		const linear_schedule<type>& s, const tensor<4, type>& x0_batch, uint32_t& seed)
	{
		const xhqm::size N = x0_batch.dimension(0), H = x0_batch.dimension(2), W = x0_batch.dimension(3);
		const xhqm::size t = xhqm::size(uniform01(seed) * float(s.T)) % s.T;
		tensor<4, type> eps(std::array<xhqm::size, 4>{ N, model.out_ch, H, W }, type(0));
		fill_normal(eps, type(0), type(1), seed);
		tensor<4, type> xt = q_sample<4, type>(x0_batch, eps, t, s);
		// 时间嵌入（每样本一个 t，这里整批共享 t）
		tensor<1, type> te = time_embedding<type>(t, model.time_dim, s.T);
		tensor<2, type> te_batch = tile_rows(te, N);
		tensor<4, type> te4d = broadcast_time4d(te_batch, H, W);
		tensor<4, type> xcond = concat_channels4d(xt, te4d);
		// 建图 + 反向
		ad::tape<type> ta;
		auto* xv = ad::make_value<4, type>(ta, xcond);
		auto* ev = ad::make_value<4, type>(ta, eps);
		auto* pred = model.forward(ta, xv);
		auto* loss = ad::mse(ta, pred, ev);
		ta.backward(loss);
		const type loss_val = static_cast<const type*>(loss->holder()->data())[0];
		opt.step(model);
		return loss_val;
	}

	// —— 2D 图像反向采样（DDPM）——
	template<class type>
	tensor<4, type> sample_image(unet_denoiser<type>& model, const linear_schedule<type>& s,
		xhqm::size n, xhqm::size ch, xhqm::size H, xhqm::size W, uint32_t& seed)
	{
		tensor<4, type> x(std::array<xhqm::size, 4>{ n, ch, H, W }, type(0));
		fill_normal(x, type(0), type(1), seed);
		for (xhqm::size t = s.T; t-- > 0;)
		{
			tensor<1, type> te = time_embedding<type>(t, model.time_dim, s.T);
			tensor<2, type> te_batch = tile_rows(te, n);
			tensor<4, type> te4d = broadcast_time4d(te_batch, H, W);
			tensor<4, type> xcond = concat_channels4d(x, te4d);
			tensor<4, type> eps_pred = model.infer(xcond);
			tensor<4, type> z(std::array<xhqm::size, 4>{ n, ch, H, W }, type(0));
			if (t > 0) fill_normal(z, type(0), type(1), seed);
			const type alpha = s.alphas[t], beta = s.betas[t], ab = s.alpha_bars[t];
			const type c1 = type(1) / type(std::sqrt(double(alpha)));
			const type c2 = (type(1) - alpha) / type(std::sqrt(double(type(1) - ab)));
			x = (x - eps_pred * c2) * c1 + z * type(std::sqrt(double(beta)));
		}
		return x;
	}

}
}
}

#endif
