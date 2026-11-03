#ifndef XHQM_SOUL_DIFFUSION
#define XHQM_SOUL_DIFFUSION

// 简单扩散模型（DDPM）：线性噪声调度 + 前向加噪 + MLP 去噪器（AD 训练）+ 反向采样。
// 约定：数据为 2D 张量 (batch, in_dim)（玩具数据即 (n, 2) 的 2D 点）；时间 t ∈ [0, T)。
// 调度数组 betas/alphas/alpha_bars 下标 t 对应 DDPM 的第 t+1 步（alpha_bars[t] = ∏_{s≤t} α_s）。
#include "../tensor/tensor.h"
#include "random.h"
#include "ad.h"
#include <array>
#include <cmath>

namespace xhqm {
namespace soul {
namespace diffusion {

	// —— 纯张量工具 ——

	// 行平铺：把 (cols,) 向量 v 平铺成 (rows, cols)
	template<class type>
	tensor<2, type> tile_rows(const tensor<1, type>& v, xhqm::size rows)
	{
		const xhqm::size cols = v.dimension(0);
		tensor<2, type> r(std::array<xhqm::size, 2>{ rows, cols }, type(0));
		for (xhqm::size i = 0; i < rows; ++i)
			for (xhqm::size j = 0; j < cols; ++j)
				r(i, j) = v[j];
		return r;
	}

	// 沿特征维（列）拼接：a (B, A) + b (B, B) -> (B, A+B)
	template<class type>
	tensor<2, type> concat_features(const tensor<2, type>& a, const tensor<2, type>& b)
	{
		const xhqm::size rows = a.dimension(0), ca = a.dimension(1), cb = b.dimension(1);
		tensor<2, type> r(std::array<xhqm::size, 2>{ rows, ca + cb }, type(0));
		for (xhqm::size i = 0; i < rows; ++i)
			for (xhqm::size j = 0; j < ca + cb; ++j)
				r(i, j) = (j < ca) ? a(i, j) : b(i, j - ca);
		return r;
	}

	// —— 1. 线性噪声调度 ——
	template<class type = f32>
	struct linear_schedule
	{
		xhqm::size T = 0;
		tensor<1, type> betas;       // β_t
		tensor<1, type> alphas;      // α_t = 1 - β_t
		tensor<1, type> alpha_bars;  // ᾱ_t = ∏ α_s

		void init(xhqm::size T_, type beta_start = type(1e-4), type beta_end = type(0.02))
		{
			T = T_;
			betas = tensor<1, type>(std::array<xhqm::size, 1>{ T }, type(0));
			alphas = tensor<1, type>(std::array<xhqm::size, 1>{ T }, type(0));
			alpha_bars = tensor<1, type>(std::array<xhqm::size, 1>{ T }, type(0));
			type ab = type(1);
			for (xhqm::size t = 0; t < T; ++t)
			{
				const type frac = (T <= 1) ? type(0) : type(t) / type(T - 1);
				betas[t] = beta_start + frac * (beta_end - beta_start);
				alphas[t] = type(1) - betas[t];
				ab *= alphas[t];
				alpha_bars[t] = ab;
			}
		}
	};

	// —— 2. 前向加噪（纯张量，不进 AD）：x_t = √ᾱ_t · x0 + √(1-ᾱ_t) · ε ——
	template<xhqm::size dim, class type>
	tensor<dim, type> q_sample(const tensor<dim, type>& x0, const tensor<dim, type>& eps,
		xhqm::size t, const linear_schedule<type>& s)
	{
		const type ab = s.alpha_bars[t];
		const type c1 = type(std::sqrt(double(ab)));
		const type c2 = type(std::sqrt(double(type(1) - ab)));
		return x0 * c1 + eps * c2;
	}

	// —— 3. 正弦时间嵌入：t -> [sin(t·ω_k), cos(t·ω_k)]，ω_k = 10000^(-2k/D) ——
	template<class type>
	tensor<1, type> time_embedding(xhqm::size t, xhqm::size dim, xhqm::size T)
	{
		tensor<1, type> e(std::array<xhqm::size, 1>{ dim }, type(0));
		const type tf = type(t) / type(T);  // 归一化到 [0,1)
		for (xhqm::size k = 0; k < dim / 2; ++k)
		{
			const type omega = type(std::exp(-std::log(10000.0) * (2.0 * double(k) / double(dim))));
			const type phase = tf * type(100.0) * omega;  // 放大到 t 尺度，增强区分度
			e[2 * k] = type(std::sin(double(phase)));
			e[2 * k + 1] = type(std::cos(double(phase)));
		}
		if (dim % 2 == 1) e[dim - 1] = tf;
		return e;
	}

	// —— 4. MLP 去噪器（AD 可训练模块）：concat(x, t_emb) -> ε 预测 ——
	template<class type = f32>
	struct mlp_denoiser : ad::module<type>
	{
		xhqm::size in_dim = 0;    // 数据维（玩具 = 2）
		xhqm::size hidden = 0;    // 隐层宽
		xhqm::size time_dim = 0;  // 时间嵌入维

		void init(xhqm::size in_dim_, xhqm::size hidden_, xhqm::size time_dim_, uint32_t& seed)
		{
			in_dim = in_dim_; hidden = hidden_; time_dim = time_dim_;
			this->params.clear();
			const xhqm::size din = in_dim + time_dim;
			tensor<2, type> w1(std::array<xhqm::size, 2>{ din, hidden }, type(0)); xavier_uniform(w1, seed); this->add_param("w1", w1);
			tensor<2, type> w2(std::array<xhqm::size, 2>{ hidden, hidden }, type(0)); xavier_uniform(w2, seed); this->add_param("w2", w2);
			tensor<2, type> w3(std::array<xhqm::size, 2>{ hidden, in_dim }, type(0)); xavier_uniform(w3, seed); this->add_param("w3", w3);
		}

		// 前向构图：x (B, in_dim)、t_emb (B, time_dim) -> ε 预测 (B, in_dim)
		ad::value<type>* forward(ad::tape<type>& t, ad::value<type>* x, ad::value<type>* t_emb)
		{
			this->register_params(t);
			auto* w1 = this->node("w1");
			auto* w2 = this->node("w2");
			auto* w3 = this->node("w3");
			auto* h = ad::concat_columns(t, { x, t_emb });   // (B, in_dim + time_dim)
			h = ad::relu(t, ad::matmul(t, h, w1));          // (B, hidden)
			h = ad::relu(t, ad::matmul(t, h, w2));          // (B, hidden)
			return ad::matmul(t, h, w3);                     // (B, in_dim)
		}

		// 推理（纯张量，不建图）：采样时用
		tensor<2, type> infer(const tensor<2, type>& x, const tensor<2, type>& t_emb) const
		{
			const auto* pw1 = as_payload<2, type>(this->payload("w1"));
			const auto* pw2 = as_payload<2, type>(this->payload("w2"));
			const auto* pw3 = as_payload<2, type>(this->payload("w3"));
			tensor<2, type> h = concat_features(x, t_emb);
			h = relu(tensor_base::matmul(h, pw1->value));
			h = relu(tensor_base::matmul(h, pw2->value));
			return tensor_base::matmul(h, pw3->value);
		}
	};

	// —— 5. 训练一步：采样 t/ε，前向加噪，构图，MSE，backward，优化器更新；返回 loss ——
	template<class type>
	type train_step(mlp_denoiser<type>& model, ad::adam_optimizer<type>& opt,
		const linear_schedule<type>& s, const tensor<2, type>& x0_batch, uint32_t& seed)
	{
		const xhqm::size B = x0_batch.dimension(0);
		// 采样时间步（整批共享一个 t，玩具模型足够）
		const xhqm::size t = xhqm::size(uniform01(seed) * float(s.T)) % s.T;
		// 采样噪声 ε ~ N(0,1)
		tensor<2, type> eps(std::array<xhqm::size, 2>{ B, model.in_dim }, type(0));
		fill_normal(eps, type(0), type(1), seed);
		// 前向加噪
		tensor<2, type> xt = q_sample<2, type>(x0_batch, eps, t, s);
		// 时间嵌入 + 平铺
		tensor<1, type> te = time_embedding<type>(t, model.time_dim, s.T);
		tensor<2, type> te_batch = tile_rows(te, B);
		// 建图 + 反向
		ad::tape<type> ta;
		auto* xv = ad::make_value<2, type>(ta, xt);
		auto* tv = ad::make_value<2, type>(ta, te_batch);
		auto* ev = ad::make_value<2, type>(ta, eps);
		auto* pred = model.forward(ta, xv, tv);
		auto* loss = ad::mse(ta, pred, ev);
		ta.backward(loss);
		const type loss_val = static_cast<const type*>(loss->holder()->data())[0];
		opt.step(model);
		return loss_val;
	}

	// —— 6. 反向采样（DDPM）：x_T ~ N(0,I)，逐步去噪到 x_0 ——
	template<class type>
	tensor<2, type> sample(mlp_denoiser<type>& model, const linear_schedule<type>& s,
		xhqm::size n, uint32_t& seed)
	{
		tensor<2, type> x(std::array<xhqm::size, 2>{ n, model.in_dim }, type(0));
		fill_normal(x, type(0), type(1), seed);
		for (xhqm::size t = s.T; t-- > 0;)
		{
			tensor<1, type> te = time_embedding<type>(t, model.time_dim, s.T);
			tensor<2, type> te_batch = tile_rows(te, n);
			tensor<2, type> eps_pred = model.infer(x, te_batch);
			// 采样 z（t=0 时为零，避免最后一步加噪）
			tensor<2, type> z(std::array<xhqm::size, 2>{ n, model.in_dim }, type(0));
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
