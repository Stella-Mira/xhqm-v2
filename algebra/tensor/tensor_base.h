#ifndef XHQM_TENSOR_BASE
#define XHQM_TENSOR_BASE

#include "../../head.h"
#include "backend/tensor_backend.h"
#include "store.h"
#include <numeric>
#include <algorithm>
#include <array>

namespace xhqm {

	template<xhqm::size dim, class type> class tensor_view;
	template<xhqm::size dim> using dimensions = std::array<xhqm::count, dim>;
	template<xhqm::size dim> using coordinate = std::array<xhqm::index, dim>;

	template<xhqm::size dim, class type = xhqm::measure>
	class tensor {
	public:
		using count_array = std::array<xhqm::size, dim>;
		using value_array = tensor_base::store<type>;
		using index_type = xhqm::size;
		using value_type = type;
	private:
		friend class tensor_view<dim, type>;
		value_array m_values;
		count_array m_counts;
	protected:
		inline static xhqm::size sum(const count_array& indexs, const xhqm::size& star = 0)
		{
			return std::accumulate(indexs.begin(), indexs.end(), star, [](const xhqm::size& that, const xhqm::size& next) { return that + next; });
		}
		inline static xhqm::size dot(const count_array& indexs, const xhqm::size& star = 1)
		{
			return std::accumulate(indexs.begin(), indexs.end(), star, [](const xhqm::size& that, const xhqm::size& next) { return that * next; });
		}
		// 逐元素二元求值（op: 0=+ 1=- 2=* 3=/），供就地/广播路径统一复用
		template<int op>
		inline static type apply_op(const type& a, const type& b)
		{
			if constexpr (op == 0) return xhqm::sum(a, b);
			else if constexpr (op == 1) return xhqm::sub(a, b);
			else if constexpr (op == 2) return xhqm::mul(a, b);
			else return xhqm::div(a, b);
		}
	public:
		//高维坐标转一维下标（行优先 row-major：最后一维 stride=1）
		inline xhqm::index index(const coordinate<dim>& coord) const
		{
			xhqm::index flat = 0;
			xhqm::size stride = 1;
			for (xhqm::size k = dim; k != 0; --k)
			{
				const xhqm::size j = k - 1;
				flat += coord[j] * stride;
				stride *= m_counts[j];
			}
			return flat;
		}
		//一维下标转高维坐标（行优先逆运算）
		inline coordinate<dim> index(xhqm::index flat) const
		{
			coordinate<dim> coord{};
			for (xhqm::size k = 0; k != dim; ++k)
			{
				const xhqm::size j = dim - k - 1;
				coord[j] = flat % m_counts[j];
				flat /= m_counts[j];
			}
			return coord;
		}
		// 广播：形状是否可广播（每维相等或其一为 1）
		inline static bool broadcastable(const count_array& a, const count_array& b)
		{
			for (xhqm::size i = 0; i < dim; ++i)
				if (a[i] != b[i] && a[i] != 1 && b[i] != 1) return false;
			return true;
		}
		// 广播结果形状（每维取较大者）
		inline static count_array broadcast_shape(const count_array& a, const count_array& b)
		{
			count_array r{};
			for (xhqm::size i = 0; i < dim; ++i)
				r[i] = (a[i] > b[i]) ? a[i] : b[i];
			return r;
		}
		// 广播逐元素二元运算（形状可广播时，结果形状取 broadcast_shape）
		template<class op>
		inline tensor broadcast_binary(const tensor& value, op&& f) const
		{
			if (!broadcastable(m_counts, value.m_counts)) return tensor();
			const count_array out_shape = broadcast_shape(m_counts, value.m_counts);
			tensor res(out_shape, type(0));
			const tensor& a = *this; const tensor& b = value; tensor& c = res;
			const count_array shape_a = m_counts, shape_b = value.m_counts;
			tensor_base::tensor_backend_active().parallel_for(0, res.size(), [&](xhqm::index idx)
			{
				coordinate<dim> coord = res.index(idx);
				coordinate<dim> ca = coord, cb = coord;
				for (xhqm::size i = 0; i < dim; ++i)
				{
					if (shape_a[i] == 1) ca[i] = 0;
					if (shape_b[i] == 1) cb[i] = 0;
				}
				c[idx] = f(a[ca], b[cb]);
			});
			return res;
		}
		// 切片：每维取 [begin[i], end[i])，返回子张量（拷贝）
		inline tensor slice(const coordinate<dim>& begin, const coordinate<dim>& end) const
		{
			count_array out_shape{};
			for (xhqm::size i = 0; i < dim; ++i)
			{
				const xhqm::size e = (end[i] > m_counts[i]) ? m_counts[i] : end[i];
				out_shape[i] = (e > begin[i]) ? (e - begin[i]) : 0;
			}
			tensor res(out_shape, type(0));
			for (xhqm::index flat = 0; flat < res.size(); ++flat)
			{
				coordinate<dim> out_coord = res.index(flat);
				coordinate<dim> in_coord{};
				for (xhqm::size i = 0; i < dim; ++i)
					in_coord[i] = begin[i] + out_coord[i];
				res[flat] = (*this)[in_coord];
			}
			return res;
		}
		constexpr tensor() : m_values{}, m_counts{} {};

		tensor(const std::array<xhqm::size, dim>& dims, const type& value = 0)
			: m_values{}, m_counts{ dims }
		{
			m_values.resize(tensor::dot(m_counts), value);
		}
		//大小
		constexpr xhqm::size size() const
		{
			return m_values.size();
		}
		//形状
		constexpr dimensions<dim> shape() const
		{
			return m_counts;
		}
		//维度
		constexpr xhqm::size dimension() const
		{
			return dim;
		}
		//维度
		constexpr xhqm::size dimension(const xhqm::index& pos) const
		{
			return m_counts[pos];
		}
		//重置形状 需等大小
		constexpr bool reshape(const dimensions<dim>& dims)
		{
			if (m_values.size() == tensor::dot(dims))
			{
				m_counts = dims; return true;
			}
			return false;
		}
		//重置维度 改变数量
		constexpr void redimension(const xhqm::index& pos, const xhqm::count& dimension)
		{
			m_counts[pos] = dimension; m_values.resize(tensor::dot(m_counts));
		}
		//重置维度 改变数量
		constexpr void redimension(const dimensions<dim>& dims, const type& value = 0)
		{
			m_counts = dims; m_values.resize(tensor::dot(m_counts), value);
		}


		inline type& value(const xhqm::index& index) noexcept
		{
			return m_values[index];
		}
		inline const type& value(const xhqm::index& index) const
		{
			return m_values[index];
		}
		inline type& value(const coordinate<dim>& indexs) noexcept
		{
			return m_values[index(indexs)];
		}
		inline const type& value(const coordinate<dim>& indexs) const
		{
			return m_values[index(indexs)];
		}

		// 连续内存首指针：供后端 matmul/elemwise/reduce 取 data()；
		// bool 特化 store 返回 reinterpret_cast<bool*>（连续 figure<bool> 内存，见 store.h）
		inline type* data() noexcept { return m_values.data(); }
		inline const type* data() const noexcept { return m_values.data(); }

		//func(m_values, index, argv...) 逐元素写回；保持串行顺序语义（func 可含跨元素依赖）
		template<class function, class...argvs> inline tensor& traverse(function&& func, argvs&&...argv)
		{
			xhqm::count count = this->size();
			for (xhqm::index index = 0; index < count; ++index)
				m_values[index] = func(m_values, index, std::forward<argvs>(argv)...);
			return (*this);
		}

		// ---- 逐元素四则（无跨元素依赖，经后端并行调度） ----
		// 就地二元：形状相同走后端 elemwise；否则按广播语义就地累加
		// （value 每维须为 1 或与 *this 相等，结果形状 = *this 形状；不可广播时无操作）。
		template<int op>
		inline tensor& this_binary(const tensor& value)
		{
			const xhqm::count count = m_values.size();
			if (m_counts == value.m_counts)
			{
				// 后端 kernel（SIMD/GPU）：支持与否由后端决定，不支持回退 parallel_for
				if (tensor_base::tensor_backend_active().elemwise(m_values.data(), m_values.data(), value.m_values.data(),
					count, tensor_base::dtype_of<type>::value, op))
					return (*this);
				tensor_base::tensor_backend_active().parallel_for(0, count, [this, &value](xhqm::index i)
				{
					m_values[i] = apply_op<op>(m_values[i], value.m_values[i]);
				});
				return (*this);
			}
			for (xhqm::size i = 0; i < dim; ++i)
				if (value.m_counts[i] != 1 && value.m_counts[i] != m_counts[i])
					return (*this);
			const count_array shape_b = value.m_counts;
			tensor_base::tensor_backend_active().parallel_for(0, count, [this, &value, &shape_b](xhqm::index idx)
			{
				coordinate<dim> cb = index(idx);
				for (xhqm::size i = 0; i < dim; ++i)
					if (shape_b[i] == 1) cb[i] = 0;
				m_values[idx] = apply_op<op>(m_values[idx], value[cb]);
			});
			return (*this);
		}
		inline tensor& this_sum(const tensor& value) { return this_binary<0>(value); }
		inline tensor& this_sub(const tensor& value) { return this_binary<1>(value); }
		inline tensor& this_mul(const tensor& value) { return this_binary<2>(value); }
		inline tensor& this_div(const tensor& value) { return this_binary<3>(value); }
		inline tensor& this_sum(const type& value)
		{
			const xhqm::count count = m_values.size();
			tensor_base::tensor_backend_active().parallel_for(0, count, [this, &value](xhqm::index i)
			{
				m_values[i] = xhqm::sum(m_values[i], value);
			});
			return (*this);
		}
		inline tensor& this_sub(const type& value)
		{
			const xhqm::count count = m_values.size();
			tensor_base::tensor_backend_active().parallel_for(0, count, [this, &value](xhqm::index i)
			{
				m_values[i] = xhqm::sub(m_values[i], value);
			});
			return (*this);
		}
		inline tensor& this_mul(const type& value)
		{
			const xhqm::count count = m_values.size();
			tensor_base::tensor_backend_active().parallel_for(0, count, [this, &value](xhqm::index i)
			{
				m_values[i] = xhqm::mul(m_values[i], value);
			});
			return (*this);
		}
		inline tensor& this_div(const type& value)
		{
			const xhqm::count count = m_values.size();
			tensor_base::tensor_backend_active().parallel_for(0, count, [this, &value](xhqm::index i)
			{
				m_values[i] = xhqm::div(m_values[i], value);
			});
			return (*this);
		}

		inline tensor copy_sum(const tensor& value) const
		{
			tensor res = (*this);
			res.this_sum(value);
			return res;
		}
		inline tensor copy_sub(const tensor& value) const
		{
			tensor res = (*this);
			res.this_sub(value);
			return res;
		}
		inline tensor copy_mul(const tensor& value) const
		{
			tensor res = (*this);
			res.this_mul(value);
			return res;
		}
		inline tensor copy_div(const tensor& value) const
		{
			tensor res = (*this);
			res.this_div(value);
			return res;
		}
		inline tensor copy_sum(const type& value) const
		{
			tensor res = (*this);
			res.this_sum(value);
			return res;
		}
		inline tensor copy_sub(const type& value) const
		{
			tensor res = (*this);
			res.this_sub(value);
			return res;
		}
		inline tensor copy_mul(const type& value) const
		{
			tensor res = (*this);
			res.this_mul(value);
			return res;
		}
		inline tensor copy_div(const type& value) const
		{
			tensor res = (*this);
			res.this_div(value);
			return res;
		}

		inline const type& operator[](const xhqm::index& index) const
		{
			return m_values[index];
		}
		inline type& operator[](const xhqm::index& index) noexcept
		{
			return m_values[index];
		}
		inline const type& operator[](const coordinate<dim>& indexs) const
		{
			return m_values[index(indexs)];
		}
		inline type& operator[](const coordinate<dim>& indexs) noexcept
		{
			return m_values[index(indexs)];
		}
		template<class ...argvs>
		inline const type& operator()(argvs&&... indexs) const
		{
			return m_values[index(coordinate<dim>{ static_cast<xhqm::index>(std::forward<argvs>(indexs))... })];
		}
		template<class ...argvs>
		inline type& operator()(argvs&&... indexs) noexcept
		{
			return m_values[index(coordinate<dim>{ static_cast<xhqm::index>(std::forward<argvs>(indexs))... })];
		}


		inline tensor& operator+=(const tensor& value)
		{
			return this_sum(value);
		}
		inline tensor& operator-=(const tensor& value)
		{
			return this_sub(value);
		}
		inline tensor& operator*=(const tensor& value)
		{
			return this_mul(value);
		}
		inline tensor& operator/=(const tensor& value)
		{
			return this_div(value);
		}
		inline tensor& operator+=(const type& value)
		{
			return this_sum(value);
		}
		inline tensor& operator-=(const type& value)
		{
			return this_sub(value);
		}
		inline tensor& operator*=(const type& value)
		{
			return this_mul(value);
		}
		inline tensor& operator/=(const type& value)
		{
			return this_div(value);
		}

		inline tensor operator+(const tensor& value) const
		{
			if (m_counts == value.m_counts) return copy_sum(value);
			return broadcast_binary(value, [](const type& x, const type& y) { return xhqm::sum(x, y); });
		}
		inline tensor operator-(const tensor& value) const
		{
			if (m_counts == value.m_counts) return copy_sub(value);
			return broadcast_binary(value, [](const type& x, const type& y) { return xhqm::sub(x, y); });
		}
		inline tensor operator*(const tensor& value) const
		{
			if (m_counts == value.m_counts) return copy_mul(value);
			return broadcast_binary(value, [](const type& x, const type& y) { return xhqm::mul(x, y); });
		}
		inline tensor operator/(const tensor& value) const
		{
			if (m_counts == value.m_counts) return copy_div(value);
			return broadcast_binary(value, [](const type& x, const type& y) { return xhqm::div(x, y); });
		}
		inline tensor operator+(const type& value) const
		{
			return copy_sum(value);
		}
		inline tensor operator-(const type& value) const
		{
			return copy_sub(value);
		}
		inline tensor operator*(const type& value) const
		{
			return copy_mul(value);
		}
		inline tensor operator/(const type& value) const
		{
			return copy_div(value);
		}
	};

}

#endif //XHQM_TENSOR_BASE
