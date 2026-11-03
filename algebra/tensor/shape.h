#ifndef XHQM_TENSOR_SHAPE
#define XHQM_TENSOR_SHAPE

// 形状变换（header-only）：squeeze / unsqueeze（改变维数，拷贝）与 reshape_view（零拷贝视图）。
// 命名空间 xhqm::tensor_base。数据线性顺序在增删 size-1 维时不变，故逐元素拷贝即可。
#include "tensor_view.h"
#include <array>

namespace xhqm {
namespace tensor_base {

	// —— unsqueeze：在 axis 位置插入一个 size-1 维（axis ∈ [0, dim]）——
	template<class type, xhqm::size dim>
	xhqm::tensor<dim + 1, type> unsqueeze(const xhqm::tensor<dim, type>& x, xhqm::size axis)
	{
		if (axis > dim) throw xhqm::error("unsqueeze axis 越界");
		std::array<xhqm::size, dim + 1> new_dims{};
		for (xhqm::size k = 0; k < axis; ++k) new_dims[k] = x.dimension(k);
		new_dims[axis] = 1;
		for (xhqm::size k = axis; k < dim; ++k) new_dims[k + 1] = x.dimension(k);
		xhqm::tensor<dim + 1, type> out(new_dims, type(0));
		for (xhqm::index i = 0; i < x.size(); ++i) out[i] = x[i];
		return out;
	}

	// —— squeeze：删除 axis 位置大小为 1 的维（axis ∈ [0, dim)）——
	template<class type, xhqm::size dim>
	xhqm::tensor<dim - 1, type> squeeze(const xhqm::tensor<dim, type>& x, xhqm::size axis)
	{
		static_assert(dim >= 1, "squeeze 需要 dim>=1");
		if (axis >= dim) throw xhqm::error("squeeze axis 越界");
		if (x.dimension(axis) != 1) throw xhqm::error("squeeze 目标轴大小必须为 1");
		std::array<xhqm::size, dim - 1> new_dims{};
		xhqm::size oi = 0;
		for (xhqm::size k = 0; k < dim; ++k) if (k != axis) new_dims[oi++] = x.dimension(k);
		xhqm::tensor<dim - 1, type> out(new_dims, type(0));
		for (xhqm::index i = 0; i < x.size(); ++i) out[i] = x[i];
		return out;
	}

	// —— reshape 视图（零拷贝，同 dim，要求元素数一致且原张量连续）——
	template<class type, xhqm::size dim>
	xhqm::tensor_view<dim, type> reshape_view(xhqm::tensor<dim, type>& t, const std::array<xhqm::size, dim>& dims)
	{
		return xhqm::tensor_view<dim, type>(t).reshape(dims);
	}

}
}

#endif
