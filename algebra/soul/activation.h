#ifndef XHQM_SOUL_ACTIVATION
#define XHQM_SOUL_ACTIVATION

// 激活函数：逐元素（经后端并行调度）与 softmax（下沉到 tensor_base，数值稳定）。
#include "../tensor/tensor.h"
#include <cmath>
#include <array>

namespace xhqm {
namespace soul {

	// 逐元素映射：y[i] = fn(x[i])（通用回退路径）
	template<xhqm::size dim, class type, class Fn>
	inline tensor<dim, type> apply(const tensor<dim, type>& x, Fn fn)
	{
		tensor<dim, type> y = x;
		tensor_base::tensor_backend_active().parallel_for(0, y.size(), [&](xhqm::index i) {
			y[i] = fn(y[i]);
		});
		return y;
	}

	// 一元算子包装：优先走后端 unary kernel（SIMD/GPU），失败回退 parallel_for
	template<xhqm::size dim, class type>
	inline tensor<dim, type> unary_op(const tensor<dim, type>& x, int op)
	{
		tensor<dim, type> y = x;
		if (tensor_base::tensor_backend_active().unary(y.data(), x.data(), x.size(),
			tensor_base::dtype_of<type>::value, op))
			return y;
		tensor_base::tensor_backend_active().parallel_for(0, y.size(), [&](xhqm::index i) {
			y[i] = tensor_base::unary_eval(y[i], op);
		});
		return y;
	}

	template<xhqm::size dim, class type>
	inline tensor<dim, type> relu(const tensor<dim, type>& x)
	{
		return unary_op(x, tensor_base::unary_relu);
	}

	template<xhqm::size dim, class type>
	inline tensor<dim, type> sigmoid(const tensor<dim, type>& x)
	{
		return unary_op(x, tensor_base::unary_sigmoid);
	}

	template<xhqm::size dim, class type>
	inline tensor<dim, type> tanh_act(const tensor<dim, type>& x)
	{
		return unary_op(x, tensor_base::unary_tanh);
	}

	// GELU（tanh 近似，GPT-2 风格）
	template<xhqm::size dim, class type>
	inline tensor<dim, type> gelu(const tensor<dim, type>& x)
	{
		return unary_op(x, tensor_base::unary_gelu);
	}

	// SiLU / Swish
	template<xhqm::size dim, class type>
	inline tensor<dim, type> silu(const tensor<dim, type>& x)
	{
		return unary_op(x, tensor_base::unary_silu);
	}

	// softmax（下沉到 tensor_base；2D 沿最后一维）
	template<class type>
	inline tensor<2, type> softmax(const tensor<2, type>& x)
	{
		return tensor_base::softmax(x, 1);
	}

	// softmax（1D）
	template<class type>
	inline tensor<1, type> softmax(const tensor<1, type>& x)
	{
		return tensor_base::softmax(x, 0);
	}

}
}

#endif
