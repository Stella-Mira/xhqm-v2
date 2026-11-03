#ifndef XHQM_VECTOR
#define XHQM_VECTOR

// vector：动态向量封装（1 维），tensor<1,type> 的浅封装。
// 运算语义为代数：operator* 是点积（返回标量），标量缩放另经 operator*(type) 重载；
// 逐元素 + / - / Hadamard 委托 tensor（走后端并行），与 tensor 的逐元素语义一致。
#include "../ftype.h"
#include "tensor_base.h"
#include "linalg.h"
#include <array>
#include <vector>
#include <initializer_list>

namespace xhqm
{
	template<class type> class vector;

	template<class type>
	class vector
	{
		tensor<1, type> m_values;
	public:
		vector() = default;
		explicit vector(const xhqm::size& n, const type& v = type(0))
			: m_values(std::array<xhqm::size, 1>{ n }, v) {}
		vector(const std::initializer_list<type>& list)
			: m_values(std::array<xhqm::size, 1>{ list.size() }, type(0))
		{
			xhqm::size i = 0;
			for (auto& v : list) m_values[i++] = v;
		}
		vector(const std::vector<type>& list)
			: m_values(std::array<xhqm::size, 1>{ list.size() }, type(0))
		{
			for (xhqm::size i = 0; i < list.size(); ++i) m_values[i] = list[i];
		}
		// 内部：从 tensor<1> 包装（算子委托的返回值）
		explicit vector(const tensor<1, type>& t) : m_values(t) {}

		// 大小与访问
		xhqm::size size() const { return m_values.size(); }
		type* data() { return m_values.data(); }
		const type* data() const { return m_values.data(); }
		// 底层 tensor 常量访问（零拷贝委托，供 analysis 适配层直接复用）
		const tensor<1, type>& values() const { return m_values; }
		type& operator[](xhqm::index i) { return m_values[i]; }
		const type& operator[](xhqm::index i) const { return m_values[i]; }

		// 逐元素 + / -（委托 tensor 后端）
		vector operator+(const vector& v) const { return vector(m_values + v.m_values); }
		vector operator-(const vector& v) const { return vector(m_values - v.m_values); }
		// 逐元素乘（Hadamard，mul；operator* 已被点积占用）
		vector mul(const vector& v) const { return vector(m_values * v.m_values); }
		vector hadamard(const vector& v) const { return mul(v); }	// 别名（兼容旧名）

		// 标量缩放 / 除法（委托 tensor）
		vector operator*(const type& s) const { return vector(m_values * s); }
		vector operator/(const type& s) const { return vector(m_values / s); }
		friend vector operator*(const type& s, const vector& v) { return v * s; }

		// 点积：vector * vector -> 标量（Hadamard 乘 + 后端 reduce_sum）
		type operator*(const vector& v) const
		{
			if (m_values.size() != v.m_values.size()) return type(0);
			return tensor_base::reduce_sum(m_values * v.m_values, 0)[0];
		}
		type dot(const vector& v) const { return (*this) * v; }

		// 叉乘（仅 3 维）
		vector cross(const vector& v) const
		{
			if (m_values.size() != 3 || v.m_values.size() != 3) return vector();
			return vector{
				xhqm::sub(xhqm::mul(m_values[1], v.m_values[2]), xhqm::mul(m_values[2], v.m_values[1])),
				xhqm::sub(xhqm::mul(m_values[2], v.m_values[0]), xhqm::mul(m_values[0], v.m_values[2])),
				xhqm::sub(xhqm::mul(m_values[0], v.m_values[1]), xhqm::mul(m_values[1], v.m_values[0]))
			};
		}

		// 模长 / 归一化
		type norm() const
		{
			static_assert(!std::is_integral_v<type>, "vector::norm/normalize 对整型/bool 无意义（sqrt 截断窄化）");
			return xhqm::sqrt(dot(*this));
		}
		type modulus() const { return norm(); }
		vector normalize() const
		{
			static_assert(!std::is_integral_v<type>, "vector::norm/normalize 对整型/bool 无意义（sqrt 截断窄化）");
			type n = norm();
			if (n == type(0)) return *this;
			return (*this) / n;
		}
	};
}

#endif
