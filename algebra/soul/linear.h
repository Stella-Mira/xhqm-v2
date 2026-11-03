#ifndef XHQM_SOUL_LINEAR
#define XHQM_SOUL_LINEAR

// 线性层：y = x · W^T + b。W 存 (out, in)，另缓存 W^T (in, out) 免去每次转置。
#include "../tensor/tensor.h"
#include "random.h"
#include <array>

namespace xhqm {
namespace soul {

	template<class type>
	struct linear
	{
		xhqm::size in_features = 0;
		xhqm::size out_features = 0;
		tensor<2, type> weight;   // (out, in)
		tensor<2, type> weight_t; // (in, out) = weight^T
		tensor<1, type> bias;     // (out,)

		void init(xhqm::size in_f, xhqm::size out_f, uint32_t& seed)
		{
			in_features = in_f;
			out_features = out_f;
			weight = tensor<2, type>(std::array<xhqm::size, 2>{ out_f, in_f }, type(0));
			xavier_uniform(weight, seed);
			weight_t = tensor_base::transpose(weight, 0, 1);
			bias = tensor<1, type>(std::array<xhqm::size, 1>{ out_f }, type(0));
		}

		// x: (batch, in_features) -> (batch, out_features)
		tensor<2, type> forward(const tensor<2, type>& x) const
		{
			tensor<2, type> y = tensor_base::matmul(x, weight_t);
			const xhqm::size rows = y.dimension(0);
			for (xhqm::size i = 0; i < rows; ++i)
				for (xhqm::size j = 0; j < out_features; ++j)
					y(i, j) += bias[j];
			return y;
		}
	};

}
}

#endif
