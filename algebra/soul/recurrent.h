#ifndef XHQM_SOUL_RECURRENT
#define XHQM_SOUL_RECURRENT

// 循环网络（scan 展开）：AD 引擎是 DAG，循环通过「时间步展开 + 权重节点共享」实现——
// 每个时间步新建 hidden 节点、复用同一组权重节点，反向时权重梯度自动跨时间步累加。
// 先提供最简 RNN（bias-free），LSTM/GRU 同构扩展（多门 + 逐元素 sigmoid/tanh + 逐元素乘）。
#include "../tensor/tensor.h"
#include "random.h"
#include "ad.h"
#include <vector>
#include <array>

namespace xhqm {
namespace soul {

	// —— 简单 RNN（tanh，无 bias）：h_t = tanh(x_t @ W_ih + h_{t-1} @ W_hh) ——
	template<class type = f32>
	struct rnn : ad::module<type>
	{
		xhqm::size input_size = 0, hidden_size = 0;

		void init(xhqm::size in_, xhqm::size hid_, uint32_t& seed)
		{
			input_size = in_; hidden_size = hid_;
			this->params.clear();
			tensor<2, type> wih(std::array<xhqm::size, 2>{ in_, hid_ }, type(0)); xavier_uniform(wih, seed); this->add_param("wih", wih);
			tensor<2, type> whh(std::array<xhqm::size, 2>{ hid_, hid_ }, type(0)); xavier_uniform(whh, seed); this->add_param("whh", whh);
		}

		// 前向：xs 为 seq 个 (batch, input) 节点，h0 为 (batch, hidden)；返回最后 hidden
		ad::value<type>* forward(ad::tape<type>& t, const std::vector<ad::value<type>*>& xs, ad::value<type>* h0)
		{
			this->register_params(t);
			auto* wih = this->node("wih");
			auto* whh = this->node("whh");
			ad::value<type>* h = h0;
			for (auto* x : xs)
			{
				auto* xt = ad::matmul(t, x, wih);
				auto* ht = ad::matmul(t, h, whh);
				h = ad::tanh(t, ad::add(t, xt, ht));
			}
			return h;
		}

		// 推理（纯张量）：xs (seq, batch, input) 逐行展开，返回 (batch, hidden)
		tensor<2, type> infer(const tensor<3, type>& xs, const tensor<2, type>& h0) const
		{
			const auto& wih = as_payload<2, type>(this->payload("wih"))->value;
			const auto& whh = as_payload<2, type>(this->payload("whh"))->value;
			const xhqm::size seq = xs.dimension(0), batch = xs.dimension(1);
			tensor<2, type> h = h0;
			for (xhqm::size t = 0; t < seq; ++t)
			{
				tensor<2, type> xt(std::array<xhqm::size, 2>{ batch, input_size }, type(0));
				for (xhqm::size b = 0; b < batch; ++b)
					for (xhqm::size i = 0; i < input_size; ++i)
						xt(b, i) = xs(t, b, i);
				h = tanh_act(tensor_base::matmul(xt, wih) + tensor_base::matmul(h, whh));
			}
			return h;
		}
	};

	// —— LSTM（无 bias，合并门权重）：i/f/g/o = sigmoid/tanh(x@W_ih + h@W_hh 的四段) ——
	// c_t = f⊙c + i⊙g，h_t = o⊙tanh(c_t)
	template<class type = f32>
	struct lstm : ad::module<type>
	{
		xhqm::size input_size = 0, hidden_size = 0;

		void init(xhqm::size in_, xhqm::size hid_, uint32_t& seed)
		{
			input_size = in_; hidden_size = hid_;
			this->params.clear();
			tensor<2, type> wih(std::array<xhqm::size, 2>{ in_, 4 * hid_ }, type(0)); xavier_uniform(wih, seed); this->add_param("wih", wih);
			tensor<2, type> whh(std::array<xhqm::size, 2>{ hid_, 4 * hid_ }, type(0)); xavier_uniform(whh, seed); this->add_param("whh", whh);
		}

		struct state { ad::value<type>* h; ad::value<type>* c; };

		state forward(ad::tape<type>& t, const std::vector<ad::value<type>*>& xs, ad::value<type>* h0, ad::value<type>* c0)
		{
			this->register_params(t);
			auto* wih = this->node("wih");
			auto* whh = this->node("whh");
			ad::value<type>* h = h0; ad::value<type>* c = c0;
			const xhqm::size hid = hidden_size;
			for (auto* x : xs)
			{
				auto* gates = ad::add(t, ad::matmul(t, x, wih), ad::matmul(t, h, whh));  // (batch, 4*hidden)
				auto* i = ad::sigmoid(t, ad::slice_columns(t, gates, 0, hid));
				auto* f = ad::sigmoid(t, ad::slice_columns(t, gates, hid, 2 * hid));
				auto* g = ad::tanh(t, ad::slice_columns(t, gates, 2 * hid, 3 * hid));
				auto* o = ad::sigmoid(t, ad::slice_columns(t, gates, 3 * hid, 4 * hid));
				c = ad::add(t, ad::mul(t, f, c), ad::mul(t, i, g));
				h = ad::mul(t, o, ad::tanh(t, c));
			}
			return { h, c };
		}
	};

	// —— GRU（无 bias）：z/r 合并门，n = tanh(x@W_in + (r⊙h)@W_hn)，h = (1-z)⊙n + z⊙h ——
	template<class type = f32>
	struct gru : ad::module<type>
	{
		xhqm::size input_size = 0, hidden_size = 0;

		void init(xhqm::size in_, xhqm::size hid_, uint32_t& seed)
		{
			input_size = in_; hidden_size = hid_;
			this->params.clear();
			tensor<2, type> wzr(std::array<xhqm::size, 2>{ in_, 2 * hid_ }, type(0)); xavier_uniform(wzr, seed); this->add_param("wzr", wzr);
			tensor<2, type> uzr(std::array<xhqm::size, 2>{ hid_, 2 * hid_ }, type(0)); xavier_uniform(uzr, seed); this->add_param("uzr", uzr);
			tensor<2, type> win(std::array<xhqm::size, 2>{ in_, hid_ }, type(0)); xavier_uniform(win, seed); this->add_param("win", win);
			tensor<2, type> uhn(std::array<xhqm::size, 2>{ hid_, hid_ }, type(0)); xavier_uniform(uhn, seed); this->add_param("uhn", uhn);
		}

		ad::value<type>* forward(ad::tape<type>& t, const std::vector<ad::value<type>*>& xs, ad::value<type>* h0)
		{
			this->register_params(t);
			auto* wzr = this->node("wzr"); auto* uzr = this->node("uzr");
			auto* win = this->node("win"); auto* uhn = this->node("uhn");
			ad::value<type>* h = h0;
			const xhqm::size hid = hidden_size;
			for (auto* x : xs)
			{
				auto* gates = ad::add(t, ad::matmul(t, x, wzr), ad::matmul(t, h, uzr));  // (batch, 2*hidden)
				auto* z = ad::sigmoid(t, ad::slice_columns(t, gates, 0, hid));
				auto* r = ad::sigmoid(t, ad::slice_columns(t, gates, hid, 2 * hid));
				auto* n = ad::tanh(t, ad::add(t, ad::matmul(t, x, win), ad::matmul(t, ad::mul(t, r, h), uhn)));
				auto* one = ad::make_value<0, type>(t, tensor<0, type>(std::array<xhqm::size, 0>{}, type(1)));
				auto* neg = ad::make_value<0, type>(t, tensor<0, type>(std::array<xhqm::size, 0>{}, type(-1)));
				ad::value<type>* one_mz = ad::add_scalar(t, ad::mul_scalar(t, z, neg), one);  // 1 - z
				h = ad::add(t, ad::mul(t, one_mz, n), ad::mul(t, z, h));
			}
			return h;
		}
	};

}
}

#endif
