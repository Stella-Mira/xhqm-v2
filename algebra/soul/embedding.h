#ifndef XHQM_SOUL_EMBEDDING
#define XHQM_SOUL_EMBEDDING

// 词嵌入表 + 正弦位置编码（绝对）。
#include "../tensor/tensor.h"
#include "random.h"
#include <cmath>
#include <array>
#include <vector>

namespace xhqm {
namespace soul {

	template<class type>
	struct embedding
	{
		xhqm::size vocab = 0;
		xhqm::size hidden = 0;
		tensor<2, type> weight; // (vocab, hidden)

		void init(xhqm::size vocab_, xhqm::size hidden_, uint32_t& seed)
		{
			vocab = vocab_;
			hidden = hidden_;
			weight = tensor<2, type>(std::array<xhqm::size, 2>{ vocab_, hidden_ }, type(0));
			fill_normal(weight, type(0), type(0.02), seed);
		}

		// 查表：ids(seq) -> (seq, hidden)
		tensor<2, type> forward(const std::vector<xhqm::size>& ids) const
		{
			const xhqm::size seq = ids.size();
			tensor<2, type> x(std::array<xhqm::size, 2>{ seq, hidden }, type(0));
			for (xhqm::size i = 0; i < seq; ++i)
				for (xhqm::size j = 0; j < hidden; ++j)
					x(i, j) = weight(ids[i], j);
			return x;
		}
	};

	// 正弦位置编码：PE(p, 2i) = sin(p / 10000^(2i/d))，PE(p, 2i+1) = cos(...)
	template<class type>
	inline tensor<2, type> sinusoidal_position(xhqm::size seq, xhqm::size hidden)
	{
		tensor<2, type> pe(std::array<xhqm::size, 2>{ seq, hidden }, type(0));
		for (xhqm::size p = 0; p < seq; ++p)
			for (xhqm::size i = 0; i < hidden; ++i)
			{
				const double div = std::pow(10000.0, (2.0 * double(i / 2)) / double(hidden));
				const double angle = double(p) / div;
				if ((i & 1) == 0)
					pe(p, i) = type(std::sin(angle));
				else
					pe(p, i) = type(std::cos(angle));
			}
		return pe;
	}

	// RoPE 旋转位置编码：对 (seq, hidden) 的 q/k 逐位置、逐「成对维度」旋转，
	// 偶数/奇数维组成复平面旋转角 θ = p / 10000^(2i/d)。start_pos 支持 KV cache 续写。
	template<class type>
	inline tensor<2, type> rope(const tensor<2, type>& x, xhqm::size start_pos = 0)
	{
		const xhqm::size seq = x.dimension(0), d = x.dimension(1);
		tensor<2, type> out(x.shape(), type(0));
		for (xhqm::size p = 0; p < seq; ++p)
			for (xhqm::size i = 0; i < d / 2; ++i)
			{
				const double theta = 1.0 / std::pow(10000.0, (2.0 * double(i)) / double(d));
				const double angle = double(start_pos + p) * theta;
				const double c = std::cos(angle), sn = std::sin(angle);
				const type x0 = x(p, 2 * i), x1 = x(p, 2 * i + 1);
				out(p, 2 * i) = type(double(x0) * c - double(x1) * sn);
				out(p, 2 * i + 1) = type(double(x0) * sn + double(x1) * c);
			}
		return out;
	}

}
}

#endif
