#ifndef XHQM_SOUL_TRANSFORMER
#define XHQM_SOUL_TRANSFORMER

// GPT 风格 decoder-only Transformer：词嵌入 + 位置编码 + 多层 block + LayerNorm + 语言模型头。
// 单序列（batch=1）前向与自回归生成；依赖 tensor 的 matmul/reduce/逐元素算子。
#include "../tensor/tensor.h"
#include "random.h"
#include "activation.h"
#include "normalize.h"
#include "linear.h"
#include "attention.h"
#include "embedding.h"
#include <cmath>
#include <array>
#include <vector>
#include <algorithm>
#include <utility>

namespace xhqm {
namespace soul {

	// 把一行 (1,d) 追加到 (seq,d) 底部，返回 (seq+1,d)；seq==0（空缓存）时按 row 定宽
	template<class type>
	inline tensor<2, type> concat_rows(const tensor<2, type>& a, const tensor<2, type>& row)
	{
		const xhqm::size seq = a.dimension(0);
		const xhqm::size d = (seq == 0) ? row.dimension(1) : a.dimension(1);
		tensor<2, type> r(std::array<xhqm::size, 2>{ seq + 1, d }, type(0));
		for (xhqm::size i = 0; i < seq; ++i)
			for (xhqm::size j = 0; j < d; ++j)
				r(i, j) = a(i, j);
		for (xhqm::size j = 0; j < d; ++j)
			r(seq, j) = row(0, j);
		return r;
	}

	// 一个 Transformer 块：pre-norm + 多头因果注意力 + 残差，pre-norm + MLP + 残差
	template<class type>
	struct transformer_block
	{
		layer_norm<type> ln1;
		layer_norm<type> ln2;
		attention<type> attn;
		linear<type> ffn1; // hidden -> ffn
		linear<type> ffn2; // ffn -> hidden

		void init(xhqm::size hidden, xhqm::size heads, xhqm::size ffn, uint32_t& seed)
		{
			ln1.init(hidden);
			ln2.init(hidden);
			attn.init(hidden, heads, seed);
			ffn1.init(hidden, ffn, seed);
			ffn2.init(ffn, hidden, seed);
		}

		tensor<2, type> forward(const tensor<2, type>& x) const
		{
			tensor<2, type> a = attn.forward(ln1.forward(x));
			tensor<2, type> h = x + a;
			tensor<2, type> f = ffn2.forward(gelu(ffn1.forward(ln2.forward(h))));
			return h + f;
		}

		// 增量前向结果：新隐藏 (1,hidden) + 追加后的完整 K/V (seq+1,hidden)
		struct block_inc
		{
			tensor<2, type> h;
			tensor<2, type> k;
			tensor<2, type> v;
		};

		// KV cache 增量前向：x 为新 token 隐藏 (1,hidden)，k_cache/v_cache 为该层历史 K/V (seq,hidden)
		block_inc forward_inc(const tensor<2, type>& x,
			const tensor<2, type>& k_cache, const tensor<2, type>& v_cache) const
		{
			tensor<2, type> xn = ln1.forward(x);
			tensor<2, type> q = attn.wq.forward(xn);
			tensor<2, type> k, v;
			attn.project_kv(xn, k, v);
			tensor<2, type> kf = concat_rows(k_cache, k);
			tensor<2, type> vf = concat_rows(v_cache, v);
			tensor<2, type> a = attn.attend_cached(q, kf, vf);
			tensor<2, type> h = x + a;
			tensor<2, type> f = ffn2.forward(gelu(ffn1.forward(ln2.forward(h))));
			return { h + f, kf, vf };
		}
	};

	template<class type>
	struct transformer
	{
		xhqm::size vocab = 0;
		xhqm::size hidden = 0;
		xhqm::size layers = 0;
		xhqm::size heads = 0;
		xhqm::size max_seq = 0;
		xhqm::size ffn = 0;

		embedding<type> wte;                 // 词嵌入
		tensor<2, type> wpe;                 // 位置编码 (max_seq, hidden)
		std::vector<transformer_block<type>> blocks;
		layer_norm<type> ln_f;
		linear<type> lm_head;                // hidden -> vocab

		void init(xhqm::size vocab_, xhqm::size hidden_, xhqm::size layers_,
			xhqm::size heads_, xhqm::size max_seq_, xhqm::size ffn_, uint32_t seed)
		{
			vocab = vocab_; hidden = hidden_; layers = layers_;
			heads = heads_; max_seq = max_seq_; ffn = ffn_;

			wte.init(vocab, hidden, seed);
			wpe = sinusoidal_position<type>(max_seq, hidden);
			blocks.clear();
			blocks.reserve(layers);
			for (xhqm::size l = 0; l < layers; ++l)
			{
				transformer_block<type> b;
				b.init(hidden, heads, ffn, seed);
				blocks.push_back(std::move(b));
			}
			ln_f.init(hidden);
			lm_head.init(hidden, vocab, seed);
		}

		// 前向：token ids(seq) -> logits(seq, vocab)
		tensor<2, type> forward(const std::vector<xhqm::size>& ids) const
		{
			const xhqm::size seq = ids.size();
			tensor<2, type> x = wte.forward(ids);
			for (xhqm::size i = 0; i < seq; ++i)
				for (xhqm::size j = 0; j < hidden; ++j)
					x(i, j) += wpe(i, j);
			for (const auto& b : blocks)
				x = b.forward(x);
			x = ln_f.forward(x);
			return lm_head.forward(x);
		}

		// 从 logits 采样：temperature <= 0 贪心，否则 softmax 采样
		xhqm::size sample(const tensor<1, type>& logits, type temperature, uint32_t& seed) const
		{
			const xhqm::size n = logits.size();
			if (temperature <= type(0))
			{
				xhqm::size best = 0;
				for (xhqm::size i = 1; i < n; ++i)
					if (logits[i] > logits[best]) best = i;
				return best;
			}
			type m = logits[0];
			for (xhqm::size i = 1; i < n; ++i)
				if (logits[i] > m) m = logits[i];
			type s = type(0);
			std::vector<type> probs(n);
			for (xhqm::size i = 0; i < n; ++i)
			{
				probs[i] = type(std::exp((logits[i] - m) / temperature));
				s += probs[i];
			}
			for (xhqm::size i = 0; i < n; ++i)
				probs[i] /= s;
			type u = type(uniform01(seed));
			type acc = type(0);
			for (xhqm::size i = 0; i < n; ++i)
			{
				acc += probs[i];
				if (u <= acc) return i;
			}
			return n - 1;
		}

		// top-k + top-p（nucleus）采样：temperature<=0 贪心；否则缩放 → top-k 截断 → softmax → top-p 截断 → 采样
		// top_k==0 或 >= n 表示不截断；top_p>=1 表示不截断
		xhqm::size sample_nucleus(const tensor<1, type>& logits, type temperature,
			xhqm::size top_k, type top_p, uint32_t& seed) const
		{
			const xhqm::size n = logits.size();
			if (temperature <= type(0))
			{
				xhqm::size best = 0;
				for (xhqm::size i = 1; i < n; ++i)
					if (logits[i] > logits[best]) best = i;
				return best;
			}

			std::vector<type> l(n);
			for (xhqm::size i = 0; i < n; ++i)
				l[i] = logits[i] / temperature;

			// top-k：保留最高的 top_k 个，其余置 -inf（exp 后为 0）
			if (top_k > 0 && top_k < n)
			{
				std::vector<std::pair<type, xhqm::size>> ord(n);
				for (xhqm::size i = 0; i < n; ++i) ord[i] = { l[i], i };
				std::sort(ord.begin(), ord.end(),
					[](const std::pair<type, xhqm::size>& a, const std::pair<type, xhqm::size>& b) { return a.first > b.first; });
				const type thresh = ord[top_k - 1].first;
				for (xhqm::size i = 0; i < n; ++i)
					if (l[i] < thresh) l[i] = type(-1e9);
			}

			type m = l[0];
			for (xhqm::size i = 1; i < n; ++i)
				if (l[i] > m) m = l[i];
			std::vector<type> p(n);
			type s = type(0);
			for (xhqm::size i = 0; i < n; ++i)
			{
				p[i] = type(std::exp(l[i] - m));
				s += p[i];
			}
			for (xhqm::size i = 0; i < n; ++i)
				p[i] /= s;

			// top-p（nucleus）：按概率降序累计，只保留累计和首次达到 top_p 的前缀，再归一化
			if (top_p > type(0) && top_p < type(1))
			{
				std::vector<std::pair<type, xhqm::size>> ord(n);
				for (xhqm::size i = 0; i < n; ++i) ord[i] = { p[i], i };
				std::sort(ord.begin(), ord.end(),
					[](const std::pair<type, xhqm::size>& a, const std::pair<type, xhqm::size>& b) { return a.first > b.first; });
				type cum = type(0);
				xhqm::size keep = n;
				for (xhqm::size i = 0; i < n; ++i)
				{
					cum += ord[i].first;
					if (cum >= top_p) { keep = i + 1; break; }
				}
				std::vector<type> q(n, type(0));
				type s2 = type(0);
				for (xhqm::size i = 0; i < keep; ++i)
				{
					q[ord[i].second] = p[ord[i].second];
					s2 += p[ord[i].second];
				}
				for (xhqm::size i = 0; i < n; ++i)
					q[i] /= s2;
				p = std::move(q);
			}

			type u = type(uniform01(seed));
			type acc = type(0);
			for (xhqm::size i = 0; i < n; ++i)
			{
				acc += p[i];
				if (u <= acc) return i;
			}
			return n - 1;
		}

		// 自回归生成：在 ids 之后续写 max_new 个 token
		std::vector<xhqm::size> generate(std::vector<xhqm::size> ids,
			xhqm::size max_new, type temperature, uint32_t& seed) const
		{
			for (xhqm::size step = 0; step < max_new; ++step)
			{
				std::vector<xhqm::size> window = ids;
				if (window.size() > max_seq)
					window = std::vector<xhqm::size>(window.end() - max_seq, window.end());

				tensor<2, type> logits = forward(window);
				const xhqm::size L = logits.dimension(0);
				tensor<1, type> last(std::array<xhqm::size, 1>{ logits.dimension(1) }, type(0));
				for (xhqm::size j = 0; j < logits.dimension(1); ++j)
					last[j] = logits(L - 1, j);
				ids.push_back(sample(last, temperature, seed));
			}
			return ids;
		}

		// 束搜索：维护 beam_width 条候选，每步对每条候选扩展 top-beam_width 个 token，
		// 按累计对数似然（log-softmax，temperature>0 时软化）保留全局最优的 beam_width 条
		std::vector<xhqm::size> generate_beam(std::vector<xhqm::size> ids,
			xhqm::size max_new, xhqm::size beam_width, type temperature, uint32_t& seed) const
		{
			if (beam_width < 1) beam_width = 1;
			struct beam { std::vector<xhqm::size> ids; type score; };
			std::vector<beam> beams(1, beam{ ids, type(0) });

			for (xhqm::size step = 0; step < max_new; ++step)
			{
				std::vector<beam> next;
				for (const auto& b : beams)
				{
					std::vector<xhqm::size> window = b.ids;
					if (window.size() > max_seq)
						window = std::vector<xhqm::size>(window.end() - max_seq, window.end());
					tensor<2, type> logits = forward(window);
					const xhqm::size L = logits.dimension(0);
					const xhqm::size n = logits.dimension(1);
					tensor<1, type> last(std::array<xhqm::size, 1>{ n }, type(0));
					for (xhqm::size j = 0; j < n; ++j)
						last[j] = logits(L - 1, j);

					type m = last[0];
					for (xhqm::size i = 1; i < n; ++i)
						if (last[i] > m) m = last[i];
					if (temperature > type(0))
					{
						for (xhqm::size i = 0; i < n; ++i) last[i] /= temperature;
						m = last[0];
						for (xhqm::size i = 1; i < n; ++i)
							if (last[i] > m) m = last[i];
					}
					type s = type(0);
					for (xhqm::size i = 0; i < n; ++i)
						s += type(std::exp(last[i] - m));
					const type logsumexp = m + type(std::log(s));

					std::vector<std::pair<type, xhqm::size>> ord(n);
					for (xhqm::size i = 0; i < n; ++i) ord[i] = { last[i], i };
					std::sort(ord.begin(), ord.end(),
						[](const std::pair<type, xhqm::size>& a, const std::pair<type, xhqm::size>& b) { return a.first > b.first; });
					const xhqm::size k = beam_width < n ? beam_width : n;
					for (xhqm::size t = 0; t < k; ++t)
					{
						beam nb = b;
						nb.ids.push_back(ord[t].second);
						nb.score = b.score + (ord[t].first - logsumexp);
						next.push_back(std::move(nb));
					}
				}
				std::sort(next.begin(), next.end(),
					[](const beam& a, const beam& b) { return a.score > b.score; });
				if (next.size() > beam_width)
					next.resize(beam_width);
				beams = std::move(next);
			}
			return beams.front().ids;
		}

		// KV cache 自回归生成：逐 token 增量前向并缓存每层 K/V（与全量 forward 结果一致），
		// 避免每步重算历史 token；采样走 sample_nucleus（temperature/top_k/top_p）
		std::vector<xhqm::size> generate_kv(std::vector<xhqm::size> ids, xhqm::size max_new,
			type temperature, type top_p, xhqm::size top_k, uint32_t& seed) const
		{
			kv_cache<type> cache;
			cache.init(layers);

			// 逐 token 跑 prompt，填充每层 K/V 缓存
			tensor<2, type> x;
			for (xhqm::size i = 0; i < ids.size(); ++i)
			{
				tensor<2, type> xin = wte.forward(std::vector<xhqm::size>{ ids[i] });
				for (xhqm::size j = 0; j < hidden; ++j)
					xin(0, j) += wpe(i, j);
				for (xhqm::size l = 0; l < layers; ++l)
				{
					auto r = blocks[l].forward_inc(xin, cache.K[l], cache.V[l]);
					xin = r.h;
					cache.K[l] = r.k;
					cache.V[l] = r.v;
				}
				x = xin;
			}

			for (xhqm::size step = 0; step < max_new; ++step)
			{
				const xhqm::size pos = ids.size(); // 新 token 的绝对位置（0 起）
				if (pos >= max_seq) break;         // 超出位置编码表，停止（KV cache 不做滑动窗口）
				tensor<2, type> logits = lm_head.forward(ln_f.forward(x));
				tensor<1, type> last(std::array<xhqm::size, 1>{ vocab }, type(0));
				for (xhqm::size j = 0; j < vocab; ++j)
					last[j] = logits(0, j);
				const xhqm::size tok = sample_nucleus(last, temperature, top_k, top_p, seed);
				ids.push_back(tok);

				tensor<2, type> xin = wte.forward(std::vector<xhqm::size>{ tok });
				for (xhqm::size j = 0; j < hidden; ++j)
					xin(0, j) += wpe(pos, j);
				for (xhqm::size l = 0; l < layers; ++l)
				{
					auto r = blocks[l].forward_inc(xin, cache.K[l], cache.V[l]);
					xin = r.h;
					cache.K[l] = r.k;
					cache.V[l] = r.v;
				}
				x = xin;
			}
			return ids;
		}
	};

}
}

#endif
