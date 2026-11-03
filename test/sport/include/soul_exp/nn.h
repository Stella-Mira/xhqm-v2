#ifndef SOUL_EXP_NN
#define SOUL_EXP_NN

// 共享神经网络原语（wav2vec2 / dinov2 等复用）：精确 GELU / LayerNorm / Linear / 非因果多头注意力。
// 序列统一用 tensor<2>(seq, hidden)（单 batch=1，去批），逐元素/归约沿最后一维（特征维）。
#include "../../../../algebra/tensor/tensor.h"
#include "../../../../algebra/soul/activation.h"   // softmax / apply

#include <array>
#include <cmath>

namespace soul_exp {

	// —— 精确 GELU（PyTorch F.gelu，approximate='none'）：0.5·x·(1+erf(x/√2)) ——
	template<class type>
	inline type gelu_exact(type v)
	{
		const double d = double(v);
		return type(0.5 * d * (1.0 + std::erf(d * 0.70710678118654752440)));
	}
	template<xhqm::size dim, class type>
	inline xhqm::tensor<dim, type> gelu(const xhqm::tensor<dim, type>& x)
	{
		return xhqm::soul::apply(x, [](const type& v) -> type { return gelu_exact(v); });
	}

	// —— LayerNorm（沿最后一维；biased var=Σ(x-μ)²/N，同 nn.LayerNorm）——
	template<class type>
	inline xhqm::tensor<2, type> layer_norm(const xhqm::tensor<2, type>& x,
		const xhqm::tensor<1, type>& weight, const xhqm::tensor<1, type>& bias, type eps)
	{
		const xhqm::size rows = x.dimension(0), cols = x.dimension(1);
		xhqm::tensor<2, type> y(std::array<xhqm::size, 2>{ rows, cols }, type(0));
		for (xhqm::size i = 0; i < rows; ++i)
		{
			type mean = type(0);
			for (xhqm::size j = 0; j < cols; ++j) mean += x(i, j);
			mean /= type(cols);
			type var = type(0);
			for (xhqm::size j = 0; j < cols; ++j) { const type d = x(i, j) - mean; var += d * d; }
			var /= type(cols);                       // biased（nn.LayerNorm）
			const type rstd = type(1) / type(std::sqrt(var + eps));
			for (xhqm::size j = 0; j < cols; ++j)
				y(i, j) = (x(i, j) - mean) * rstd * weight[j] + bias[j];
		}
		return y;
	}

	// —— 线性：y = x·Wᵀ + b（Wᵀ 预转置缓存，形状 (in, out)）——
	template<class type>
	inline xhqm::tensor<2, type> linear(const xhqm::tensor<2, type>& x,
		const xhqm::tensor<2, type>& weight_t, const xhqm::tensor<1, type>& bias)
	{
		const xhqm::size rows = x.dimension(0), out = weight_t.dimension(1);
		xhqm::tensor<2, type> y = xhqm::tensor_base::matmul(x, weight_t);
		for (xhqm::size i = 0; i < rows; ++i)
			for (xhqm::size j = 0; j < out; ++j) y(i, j) += bias[j];
		return y;
	}

	// —— 非因果多头自注意力（无掩码、无偏置项，scale=1/√head_dim）——
	template<class type>
	inline xhqm::tensor<2, type> attention(const xhqm::tensor<2, type>& x,
		const xhqm::tensor<2, type>& wq_t, const xhqm::tensor<2, type>& wk_t,
		const xhqm::tensor<2, type>& wv_t, const xhqm::tensor<2, type>& wo_t,
		const xhqm::tensor<1, type>& bq, const xhqm::tensor<1, type>& bk,
		const xhqm::tensor<1, type>& bv, const xhqm::tensor<1, type>& bo,
		xhqm::size heads)
	{
		const xhqm::size seq = x.dimension(0), hidden = x.dimension(1);
		const xhqm::size head_dim = hidden / heads;
		const type scale = type(1) / type(std::sqrt(type(head_dim)));

		xhqm::tensor<2, type> q = linear(x, wq_t, bq);
		xhqm::tensor<2, type> k = linear(x, wk_t, bk);
		xhqm::tensor<2, type> v = linear(x, wv_t, bv);
		xhqm::tensor<2, type> out(std::array<xhqm::size, 2>{ seq, hidden }, type(0));

		for (xhqm::size h = 0; h < heads; ++h)
		{
			xhqm::tensor<2, type> qh(std::array<xhqm::size, 2>{ seq, head_dim }, type(0));
			xhqm::tensor<2, type> kh(std::array<xhqm::size, 2>{ seq, head_dim }, type(0));
			xhqm::tensor<2, type> vh(std::array<xhqm::size, 2>{ seq, head_dim }, type(0));
			for (xhqm::size r = 0; r < seq; ++r)
				for (xhqm::size c = 0; c < head_dim; ++c)
				{
					qh(r, c) = q(r, h * head_dim + c);
					kh(r, c) = k(r, h * head_dim + c);
					vh(r, c) = v(r, h * head_dim + c);
				}
			xhqm::tensor<2, type> scores = xhqm::tensor_base::matmul(qh, xhqm::tensor_base::transpose(kh, 0, 1));
			for (xhqm::size i = 0; i < seq; ++i)
				for (xhqm::size j = 0; j < seq; ++j)
					scores(i, j) *= scale;
			xhqm::tensor<2, type> probs = xhqm::soul::softmax(scores);
			xhqm::tensor<2, type> oh = xhqm::tensor_base::matmul(probs, vh);
			for (xhqm::size r = 0; r < seq; ++r)
				for (xhqm::size c = 0; c < head_dim; ++c)
					out(r, h * head_dim + c) = oh(r, c);
		}
		return linear(out, wo_t, bo);
	}

}

#endif
