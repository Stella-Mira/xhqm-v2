#ifndef XHQM_SOUL_ATTENTION
#define XHQM_SOUL_ATTENTION

// 多头因果自注意力：Q/K/V 各一线性投影 + 缩放点积 + 因果掩码 + 输出投影。
// 输入输出均为 (seq, hidden) 的 2D 张量（单序列，batch=1）。
#include "../tensor/tensor.h"
#include "linear.h"
#include "activation.h"
#include <cmath>
#include <array>
#include <vector>

namespace xhqm {
namespace soul {

	template<class type>
	struct attention
	{
		xhqm::size heads = 1;
		xhqm::size head_dim = 0;
		xhqm::size hidden = 0;
		linear<type> wq, wk, wv, wo;

		void init(xhqm::size hidden_, xhqm::size heads_, uint32_t& seed)
		{
			hidden = hidden_;
			heads = heads_;
			head_dim = hidden / heads; // 要求 hidden 能被 heads 整除
			wq.init(hidden, hidden, seed);
			wk.init(hidden, hidden, seed);
			wv.init(hidden, hidden, seed);
			wo.init(hidden, hidden, seed);
		}

		tensor<2, type> forward(const tensor<2, type>& x) const
		{
			const xhqm::size seq = x.dimension(0);
			tensor<2, type> q = wq.forward(x);
			tensor<2, type> k = wk.forward(x);
			tensor<2, type> v = wv.forward(x);
			tensor<2, type> out(std::array<xhqm::size, 2>{ seq, hidden }, type(0));

			const type scale = type(1) / type(std::sqrt(type(head_dim)));
			for (xhqm::size h = 0; h < heads; ++h)
			{
				tensor<2, type> qh(std::array<xhqm::size, 2>{ seq, head_dim }, type(0));
				tensor<2, type> kh(std::array<xhqm::size, 2>{ seq, head_dim }, type(0));
				tensor<2, type> vh(std::array<xhqm::size, 2>{ seq, head_dim }, type(0));
				for (xhqm::size r = 0; r < seq; ++r)
					for (xhqm::size c = 0; c < head_dim; ++c)
					{
						qh(r, c) = q(r, h * head_dim + c);
						kh(r, c) = k(r, h * head_dim + c);
						vh(r, c) = v(r, h * head_dim + c);
					}

				tensor<2, type> scores = tensor_base::matmul(qh, tensor_base::transpose(kh, 0, 1));
				for (xhqm::size i = 0; i < seq; ++i)
					for (xhqm::size j = 0; j < seq; ++j)
					{
						scores(i, j) *= scale;
						if (j > i) scores(i, j) = type(-1e9); // 因果掩码：禁止看到未来
					}

				tensor<2, type> probs = softmax(scores);
				tensor<2, type> oh = tensor_base::matmul(probs, vh);
				for (xhqm::size r = 0; r < seq; ++r)
					for (xhqm::size c = 0; c < head_dim; ++c)
						out(r, h * head_dim + c) = oh(r, c);
			}
			return wo.forward(out);
		}

		// 只投影 K/V（供 KV cache 增量：新 token 无需重算历史 K/V）
		void project_kv(const tensor<2, type>& x, tensor<2, type>& k, tensor<2, type>& v) const
		{
			k = wk.forward(x);
			v = wv.forward(x);
		}

		// 增量注意力：q(1,hidden) 对缓存 K/V(seq_kv,hidden) 做缩放点积（q 看全部缓存，无因果掩码）
		tensor<2, type> attend_cached(const tensor<2, type>& q, const tensor<2, type>& k, const tensor<2, type>& v) const
		{
			const xhqm::size seq_kv = k.dimension(0);
			tensor<2, type> out(std::array<xhqm::size, 2>{ 1, hidden }, type(0));
			const type scale = type(1) / type(std::sqrt(type(head_dim)));
			for (xhqm::size h = 0; h < heads; ++h)
			{
				tensor<2, type> qh(std::array<xhqm::size, 2>{ 1, head_dim }, type(0));
				tensor<2, type> kh(std::array<xhqm::size, 2>{ seq_kv, head_dim }, type(0));
				tensor<2, type> vh(std::array<xhqm::size, 2>{ seq_kv, head_dim }, type(0));
				for (xhqm::size c = 0; c < head_dim; ++c)
				{
					qh(0, c) = q(0, h * head_dim + c);
					for (xhqm::size r = 0; r < seq_kv; ++r)
					{
						kh(r, c) = k(r, h * head_dim + c);
						vh(r, c) = v(r, h * head_dim + c);
					}
				}
				tensor<2, type> scores = tensor_base::matmul(qh, tensor_base::transpose(kh, 0, 1)); // (1, seq_kv)
				for (xhqm::size j = 0; j < seq_kv; ++j)
					scores(0, j) *= scale;
				tensor<2, type> probs = softmax(scores);
				tensor<2, type> oh = tensor_base::matmul(probs, vh); // (1, head_dim)
				for (xhqm::size c = 0; c < head_dim; ++c)
					out(0, h * head_dim + c) = oh(0, c);
			}
			return wo.forward(out);
		}
	};

	// KV cache：自回归生成时缓存每层注意力已计算的 K/V，避免每步重算历史 token。
	// 用法：每生成一个新 token，把各层的 K/V（(1, hidden)）append 进来；取 k(layer)/v(layer)
	// 得完整序列 K/V 供 sdpa 用。reset() 清空重新生成。
	template<class type>
	struct kv_cache
	{
		std::vector<tensor<2, type>> K;  // 每层 (seq_so_far, hidden)
		std::vector<tensor<2, type>> V;

		void reset() { K.clear(); V.clear(); }

		// 首次：按层数初始化空缓存
		void init(xhqm::size layers) { K.assign(layers, tensor<2, type>()); V.assign(layers, tensor<2, type>()); }

		// 追加一个新 token 的 K/V（每层一个 (1, hidden)）
		void append(const std::vector<tensor<2, type>>& k_new, const std::vector<tensor<2, type>>& v_new)
		{
			for (xhqm::size l = 0; l < k_new.size(); ++l)
			{
				const xhqm::size seq = K[l].dimension(0), d = K[l].dimension(1);
				tensor<2, type> nk(std::array<xhqm::size, 2>{ seq + 1, d }, type(0));
				for (xhqm::size i = 0; i < seq; ++i) for (xhqm::size j = 0; j < d; ++j) nk(i, j) = K[l](i, j);
				for (xhqm::size j = 0; j < d; ++j) nk(seq, j) = k_new[l](0, j);
				K[l] = nk;
				tensor<2, type> nv(std::array<xhqm::size, 2>{ seq + 1, d }, type(0));
				for (xhqm::size i = 0; i < seq; ++i) for (xhqm::size j = 0; j < d; ++j) nv(i, j) = V[l](i, j);
				for (xhqm::size j = 0; j < d; ++j) nv(seq, j) = v_new[l](0, j);
				V[l] = nv;
			}
		}

		const tensor<2, type>& k(xhqm::size layer) const { return K[layer]; }
		const tensor<2, type>& v(xhqm::size layer) const { return V[layer]; }
	};

}
}

#endif
