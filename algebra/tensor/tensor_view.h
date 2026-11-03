#ifndef XHQM_TENSOR_VIEW
#define XHQM_TENSOR_VIEW

// tensor_view：基于 stride 的非拥有视图。切片 / 转置 / 广播均为零拷贝，
// 只改 shape 与 strides；索引时用 offset = Σ coord[k] * stride[k]。
#include "tensor_base.h"
#include <array>
#include <utility>

namespace xhqm {

	template<xhqm::size dim, class type>
	class tensor_view {
	public:
		using shape_t = std::array<xhqm::size, dim>;
		using strides_t = std::array<xhqm::size, dim>;
	private:
		type* m_data;
		shape_t m_shape;
		strides_t m_strides;
	public:
		// 完整视图（要求 tensor 非空）
		tensor_view(tensor<dim, type>& t)
			: m_data(t.data()), m_shape(t.m_counts)
		{
			xhqm::size stride = 1;
			for (xhqm::size k = dim; k != 0; --k)
			{
				const xhqm::size j = k - 1;
				m_strides[j] = stride;
				stride *= m_shape[j];
			}
		}
		// 原始构造（切片/转置/广播内部用）
		tensor_view(type* data, const shape_t& shape, const strides_t& strides)
			: m_data(data), m_shape(shape), m_strides(strides) {
		}

		xhqm::size dimension(xhqm::size pos) const { return m_shape[pos]; }
		const shape_t& shape() const { return m_shape; }

		// 高维坐标转偏移（stride 累计）
		xhqm::size offset(const std::array<xhqm::size, dim>& coord) const
		{
			xhqm::size flat = 0;
			for (xhqm::size k = 0; k < dim; ++k)
				flat += coord[k] * m_strides[k];
			return flat;
		}

		type& at(const std::array<xhqm::size, dim>& coord) const
		{
			return m_data[offset(coord)];
		}
		template<class ...argvs>
		type& operator()(argvs&&... indexs) const
		{
			return m_data[offset(shape_t{ static_cast<xhqm::size>(std::forward<argvs>(indexs))... })];
		}

		// 转置：交换 a,b 两维（零拷贝）
		tensor_view transpose(xhqm::size a, xhqm::size b) const
		{
			if (a >= dim || b >= dim) throw xhqm::error("tensor_view::transpose 轴越界");
			shape_t s = m_shape;
			strides_t st = m_strides;
			std::swap(s[a], s[b]);
			std::swap(st[a], st[b]);
			return tensor_view(m_data, s, st);
		}
		// 广播：size==1 的维 stride 置 0，扩展到目标形状（零拷贝）
		tensor_view broadcast(const shape_t& dims) const
		{
			for (xhqm::size k = 0; k < dim; ++k)
				if (dims[k] != m_shape[k] && m_shape[k] != 1)
					throw xhqm::error("tensor_view::broadcast 形状不可广播");
			strides_t st = m_strides;
			for (xhqm::size k = 0; k < dim; ++k)
				if (m_shape[k] == 1 && dims[k] > 1)
					st[k] = 0;
			return tensor_view(m_data, dims, st);
		}
		// 切片：固定第 pos 维为 index，降一维（零拷贝）
		template<xhqm::size pos>
		tensor_view<dim - 1, type> slice(xhqm::size index) const
		{
			static_assert(dim > 1 && pos < dim, "slice 需要 dim>1 且 pos<dim");
			if (index >= m_shape[pos]) throw xhqm::error("tensor_view::slice 索引越界");
			std::array<xhqm::size, dim - 1> s{};
			std::array<xhqm::size, dim - 1> st{};
			xhqm::size oi = 0;
			for (xhqm::size k = 0; k < dim; ++k)
			{
				if (k == pos) continue;
				s[oi] = m_shape[k];
				st[oi] = m_strides[k];
				++oi;
			}
			return tensor_view<dim - 1, type>(m_data + index * m_strides[pos], s, st);
		}

		// 是否标准行优先连续（reshape 视图的前置条件）
		bool is_contiguous() const
		{
			xhqm::size stride = 1;
			for (xhqm::size k = dim; k != 0; --k)
			{
				const xhqm::size j = k - 1;
				if (m_strides[j] != stride) return false;
				stride *= m_shape[j];
			}
			return true;
		}

		// reshape 视图（同 dim，零拷贝）：要求当前视图连续（未转置/广播/切片打断 stride）
		tensor_view reshape(const shape_t& new_shape) const
		{
			if (!is_contiguous()) throw xhqm::error("tensor_view::reshape 仅支持连续视图");
			xhqm::size total = 1;
			for (xhqm::size k = 0; k < dim; ++k) total *= m_shape[k];
			xhqm::size new_total = 1;
			for (xhqm::size k = 0; k < dim; ++k) new_total *= new_shape[k];
			if (total != new_total) throw xhqm::error("tensor_view::reshape 元素数须一致");
			strides_t st{};
			xhqm::size stride = 1;
			for (xhqm::size k = dim; k != 0; --k)
			{
				const xhqm::size j = k - 1;
				st[j] = stride;
				stride *= new_shape[j];
			}
			return tensor_view(m_data, new_shape, st);
		}
	};

}

#endif
