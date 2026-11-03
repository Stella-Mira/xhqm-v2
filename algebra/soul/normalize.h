#ifndef XHQM_SOUL_NORMALIZE
#define XHQM_SOUL_NORMALIZE

// LayerNorm / RMSNorm：沿特征维（最后一维）归一化，作用于 (rows, features) 的 2D 张量。
// 计算已下沉到 tensor_base::layer_norm / rms_norm，本层仅保留参数与薄封装。
#include "../tensor/tensor.h"
#include <cmath>
#include <array>

namespace xhqm {
namespace soul {

	// LayerNorm：y = (x - mean) / sqrt(var + eps) * weight + bias
	template<class type>
	struct layer_norm
	{
		tensor<1, type> weight; // (features,)
		tensor<1, type> bias;   // (features,)
		type eps = type(1e-5);

		void init(xhqm::size features, type eps_ = type(1e-5))
		{
			eps = eps_;
			weight = tensor<1, type>(std::array<xhqm::size, 1>{ features }, type(1));
			bias = tensor<1, type>(std::array<xhqm::size, 1>{ features }, type(0));
		}

		tensor<2, type> forward(const tensor<2, type>& x) const
		{
			return tensor_base::layer_norm(x, weight, bias, eps);
		}
	};

	// RMSNorm：y = x / sqrt(mean(x^2) + eps) * weight（无去均值、无 bias）
	template<class type>
	struct rms_norm
	{
		tensor<1, type> weight;
		type eps = type(1e-5);

		void init(xhqm::size features, type eps_ = type(1e-5))
		{
			eps = eps_;
			weight = tensor<1, type>(std::array<xhqm::size, 1>{ features }, type(1));
		}

		tensor<2, type> forward(const tensor<2, type>& x) const
		{
			return tensor_base::rms_norm(x, weight, eps);
		}
	};

}
}

#endif
