#ifndef XHQM_MATRIX
#define XHQM_MATRIX

// matrix：动态矩阵封装（2 维），tensor<2,type> 的浅封装。
// 运算语义为代数：operator* 是矩阵乘（matrix*matrix）、矩阵乘向量（matrix*vector）
// 与标量缩放（matrix*type），与 tensor 的逐元素 Hadamard 不同。逐元素与矩阵乘委托 tensor 后端。
#include "vector.h"
#include "tensor_base.h"
#include "linalg.h"
#include <vector>
#include <initializer_list>
#include <type_traits>

namespace xhqm
{
	template<class type> class matrix;

	template<class type>
	class matrix
	{
		tensor<2, type> m_values;
	public:
		matrix() = default;
		matrix(const xhqm::size& rows, const xhqm::size& cols, const type& v = type(0))
			: m_values(std::array<xhqm::size, 2>{ rows, cols }, v) {}
		matrix(const std::initializer_list<std::initializer_list<type>>& list)
			: m_values(std::array<xhqm::size, 2>{
				list.size(), (list.size() > 0 ? list.begin()->size() : 0) }, type(0))
		{
			xhqm::index i = 0;
			for (auto& row : list)
			{
				xhqm::index j = 0;
				for (auto& val : row)
				{
					if (j < cols()) (*this)(i, j) = val;
					++j;
				}
				++i;
			}
		}
		// 内部：从 tensor<2> 包装（算子委托的返回值）
		explicit matrix(const tensor<2, type>& t) : m_values(t) {}

		// 尺寸与访问
		xhqm::size rows() const { return m_values.dimension(0); }
		xhqm::size cols() const { return m_values.dimension(1); }
		xhqm::size size() const { return m_values.size(); }
		type* data() { return m_values.data(); }
		const type* data() const { return m_values.data(); }
		// 底层 tensor 常量访问（零拷贝委托，供 analysis 适配层直接复用）
		const tensor<2, type>& values() const { return m_values; }
		type& operator()(xhqm::index i, xhqm::index j) { return m_values(i, j); }
		const type& operator()(xhqm::index i, xhqm::index j) const { return m_values(i, j); }

		// 逐元素 + / -（委托 tensor 后端）
		matrix operator+(const matrix& v) const { return matrix(m_values + v.m_values); }
		matrix operator-(const matrix& v) const { return matrix(m_values - v.m_values); }

		// 逐元素乘（Hadamard）
		matrix mul(const matrix& v) const { return matrix(m_values * v.m_values); }

		// 标量缩放 / 除法（委托 tensor）
		matrix operator*(const type& s) const { return matrix(m_values * s); }
		matrix operator/(const type& s) const { return matrix(m_values / s); }
		friend matrix operator*(const type& s, const matrix& m) { return m * s; }

		// 矩阵乘：matrix * matrix（tensor_base::matmul，后端加速）
		matrix operator*(const matrix& v) const
		{
			return matrix(tensor_base::matmul(m_values, v.m_values));
		}
		// 点积（矩阵乘的显式名）
		matrix dot(const matrix& v) const { return (*this) * v; }

		// 矩阵乘向量：matrix * vector -> vector
		xhqm::vector<type> operator*(const xhqm::vector<type>& v) const
		{
			if (cols() != v.size()) return xhqm::vector<type>();
			xhqm::vector<type> r(rows(), type(0));
			if (xhqm::tensor_base::tensor_backend_active().matmul(r.data(), m_values.data(), v.data(),
				rows(), 1, cols(), xhqm::tensor_base::dtype_of<type>::value))
				return r;
			for (xhqm::index i = 0; i < rows(); ++i)
			{
				type acc = type(0);
				for (xhqm::index k = 0; k < cols(); ++k)
					acc = xhqm::sum(acc, xhqm::mul(m_values(i, k), v[k]));
				r[i] = acc;
			}
			return r;
		}

		// 转置（tensor_base::transpose，后端加速）
		matrix transpose() const
		{
			return matrix(tensor_base::transpose(m_values, 0, 1));
		}

		// 行列式（方阵，高斯消元 + 部分主元）
		type det() const
		{
			static_assert(!std::is_integral_v<type> || std::is_same_v<type, bool>,
				"matrix::det 对整型错误（整数除法破坏高斯消元），需浮点或 GF(2) bool");
			if (rows() != cols() || rows() == 0) return type(0);
			const xhqm::size n = rows();
			tensor<2, type> a = m_values;
			type sign = type(1);
			for (xhqm::size k = 0; k < n; ++k)
			{
				xhqm::size pivot = k;
				for (xhqm::size i = k + 1; i < n; ++i)
					if (xhqm::abs(a(i, k)) > xhqm::abs(a(pivot, k))) pivot = i;
				if (a(pivot, k) == type(0)) return type(0);
				if (pivot != k)
				{
					for (xhqm::size j = 0; j < n; ++j) std::swap(a(k, j), a(pivot, j));
					sign = xhqm::sub(type(0), sign);
				}
				for (xhqm::size i = k + 1; i < n; ++i)
				{
					type factor = xhqm::div(a(i, k), a(k, k));
					for (xhqm::size j = k; j < n; ++j)
						a(i, j) = xhqm::sub(a(i, j), xhqm::mul(factor, a(k, j)));
				}
			}
			type result = sign;
			for (xhqm::size i = 0; i < n; ++i)
				result = xhqm::mul(result, a(i, i));
			return result;
		}
	};
}

#endif
