#ifndef XHQM_FIGURE
#define XHQM_FIGURE

// figure：标量数值封装（class 风格）。
// 主模板覆盖 f32/f64（原生浮点）；f16 为半精度，经 float 转换运算并暴露特殊值。
// 运算语义为标量代数，与 tensor 无关，也不走后端（单元素无需并行）。
#include "../ftype.h"
#include <type_traits>

namespace xhqm
{
	template<class type> class figure;

	// 主模板：通用数值封装（f32/f64 原生浮点）
	template<class type>
	class figure
	{
		type m_value;
	public:
		constexpr figure() : m_value(type(0)) {}
		constexpr figure(const type& v) : m_value(v) {}

		operator type() const { return m_value; }
		type& value() { return m_value; }
		const type& value() const { return m_value; }

		// 四则（figure 间，经 xhqm 泛型函数，bool 走布尔环）
		inline figure operator+(const figure& v) const { return xhqm::sum(m_value, v.m_value); }
		inline figure operator-(const figure& v) const { return xhqm::sub(m_value, v.m_value); }
		inline figure operator*(const figure& v) const { return xhqm::mul(m_value, v.m_value); }
		inline figure operator/(const figure& v) const { return xhqm::div(m_value, v.m_value); }
		inline figure& operator+=(const figure& v) { m_value = xhqm::sum(m_value, v.m_value); return *this; }
		inline figure& operator-=(const figure& v) { m_value = xhqm::sub(m_value, v.m_value); return *this; }
		inline figure& operator*=(const figure& v) { m_value = xhqm::mul(m_value, v.m_value); return *this; }
		inline figure& operator/=(const figure& v) { m_value = xhqm::div(m_value, v.m_value); return *this; }

		// 与底层 type
		inline figure operator+(const type& v) const { return xhqm::sum(m_value, v); }
		inline figure operator-(const type& v) const { return xhqm::sub(m_value, v); }
		inline figure operator*(const type& v) const { return xhqm::mul(m_value, v); }
		inline figure operator/(const type& v) const { return xhqm::div(m_value, v); }

		// 比较
		inline bool operator==(const figure& v) const { return m_value == v.m_value; }
		inline bool operator!=(const figure& v) const { return m_value != v.m_value; }
		inline bool operator<(const figure& v) const { return m_value < v.m_value; }
		inline bool operator<=(const figure& v) const { return m_value <= v.m_value; }
		inline bool operator>(const figure& v) const { return m_value > v.m_value; }
		inline bool operator>=(const figure& v) const { return m_value >= v.m_value; }

		// 数学（经 xhqm 泛型函数；abs 对整型/bool 恒等保留，其余需浮点）
		inline figure abs() const { return xhqm::abs(m_value); }
		inline figure sqrt() const { static_assert(!std::is_integral_v<type>, "figure 数学函数对整型/bool 无意义（double 窄化）"); return xhqm::sqrt(m_value); }
		inline figure sin() const { static_assert(!std::is_integral_v<type>, "figure 数学函数对整型/bool 无意义（double 窄化）"); return xhqm::sin(m_value); }
		inline figure cos() const { static_assert(!std::is_integral_v<type>, "figure 数学函数对整型/bool 无意义（double 窄化）"); return xhqm::cos(m_value); }
		inline figure tan() const { static_assert(!std::is_integral_v<type>, "figure 数学函数对整型/bool 无意义（double 窄化）"); return xhqm::tan(m_value); }
		inline figure log() const { static_assert(!std::is_integral_v<type>, "figure 数学函数对整型/bool 无意义（double 窄化）"); return xhqm::log(m_value); }
		inline figure exp() const { static_assert(!std::is_integral_v<type>, "figure 数学函数对整型/bool 无意义（double 窄化）"); return xhqm::pow(m_value); } // xhqm::pow(type) 单参 = std::exp
		inline figure pow(const figure& v) const { static_assert(!std::is_integral_v<type>, "figure 数学函数对整型/bool 无意义（double 窄化）"); return xhqm::pow(m_value, v.m_value); }
	};

	// f16 特化：半精度（16 位），显式 float 转换运算 + 特殊值
	template<>
	class figure<f16>
	{
		f16 m_value;
	public:
		constexpr figure() : m_value() {}
		constexpr figure(const f16& v) : m_value(v) {}
		figure(const float& v) : m_value(v) {}

		operator float() const { return (float)m_value; }
		f16& value() { return m_value; }
		const f16& value() const { return m_value; }

		// 半精度特殊值（true + / false -）
		inline figure most(bool us = true) { m_value.most(us); return *this; }
		inline figure zero(bool us = true) { m_value.zero(us); return *this; }
		inline figure inf(bool us = true) { m_value.inf(us); return *this; }
		inline figure nan(bool us = true) { m_value.nan(us); return *this; }

		// 四则（float 转换，结果回半精度）
		inline figure operator+(const figure& v) const { return (float)m_value + (float)v.m_value; }
		inline figure operator-(const figure& v) const { return (float)m_value - (float)v.m_value; }
		inline figure operator*(const figure& v) const { return (float)m_value * (float)v.m_value; }
		inline figure operator/(const figure& v) const { return (float)m_value / (float)v.m_value; }
		inline figure& operator+=(const figure& v) { m_value = (float)m_value + (float)v.m_value; return *this; }
		inline figure& operator-=(const figure& v) { m_value = (float)m_value - (float)v.m_value; return *this; }
		inline figure& operator*=(const figure& v) { m_value = (float)m_value * (float)v.m_value; return *this; }
		inline figure& operator/=(const figure& v) { m_value = (float)m_value / (float)v.m_value; return *this; }

		inline figure operator+(const float& v) const { return (float)m_value + v; }
		inline figure operator-(const float& v) const { return (float)m_value - v; }
		inline figure operator*(const float& v) const { return (float)m_value * v; }
		inline figure operator/(const float& v) const { return (float)m_value / v; }

		// 比较
		inline bool operator==(const figure& v) const { return (float)m_value == (float)v.m_value; }
		inline bool operator!=(const figure& v) const { return (float)m_value != (float)v.m_value; }
		inline bool operator<(const figure& v) const { return (float)m_value < (float)v.m_value; }
		inline bool operator<=(const figure& v) const { return (float)m_value <= (float)v.m_value; }
		inline bool operator>(const figure& v) const { return (float)m_value > (float)v.m_value; }
		inline bool operator>=(const figure& v) const { return (float)m_value >= (float)v.m_value; }

		// 数学（xhqm 对 f16 有特化）
		inline figure abs() const { return xhqm::abs(m_value); }
		inline figure sqrt() const { return xhqm::sqrt(m_value); }
		inline figure sin() const { return xhqm::sin(m_value); }
		inline figure cos() const { return xhqm::cos(m_value); }
		inline figure pow(const figure& v) const { return xhqm::pow(m_value, v.m_value); }
	};

	// bool 特化：GF(2) 布尔环值类型（+/- = XOR，*// = AND）。
	// 有 operator bool() 隐式转换；比较走内建 bool 比较（不重复定义成员比较，避免与 operator bool() 二义）。
	template<>
	class figure<bool>
	{
		bool m_value;
	public:
		constexpr figure() : m_value(false) {}
		constexpr figure(const bool& v) : m_value(v) {}

		operator bool() const { return m_value; }
		bool& value() { return m_value; }
		const bool& value() const { return m_value; }

		// GF(2) 四则（figure×figure 与 figure×bool 显式，规避 operator bool() 的二义）
		inline figure operator+(const figure& v) const { return xhqm::sum(m_value, v.m_value); }
		inline figure operator-(const figure& v) const { return xhqm::sub(m_value, v.m_value); }
		inline figure operator*(const figure& v) const { return xhqm::mul(m_value, v.m_value); }
		inline figure operator/(const figure& v) const { return xhqm::div(m_value, v.m_value); }
		inline figure operator+(const bool& v) const { return xhqm::sum(m_value, v); }
		inline figure operator-(const bool& v) const { return xhqm::sub(m_value, v); }
		inline figure operator*(const bool& v) const { return xhqm::mul(m_value, v); }
		inline figure operator/(const bool& v) const { return xhqm::div(m_value, v); }
		inline figure& operator+=(const figure& v) { m_value = xhqm::sum(m_value, v.m_value); return *this; }
		inline figure& operator-=(const figure& v) { m_value = xhqm::sub(m_value, v.m_value); return *this; }
		inline figure& operator*=(const figure& v) { m_value = xhqm::mul(m_value, v.m_value); return *this; }
		inline figure& operator/=(const figure& v) { m_value = xhqm::div(m_value, v.m_value); return *this; }
	};
}

#endif
