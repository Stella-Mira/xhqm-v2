#ifndef XHQM_DUALNUM
#define XHQM_DUALNUM

// dualnum：对偶数（自动微分），a + bε，ε²=0。
// 四则按对偶数代数；数学函数经链式法则求导；save/read 序列化为 "real+duale"。
#include "../mathf.h"
#include "../itype.h"
#include "../ftype.h"
#include <string>
#include <cmath>

namespace xhqm
{
	template<typename type> struct dualnum
	{
		type real;	// 实部
		type dual;	// 对偶部（ε 系数）

		constexpr dualnum() : real(type()), dual(type()) {}
		constexpr dualnum(const type& r, const type& d) : real(r), dual(d) {}
		constexpr dualnum(const type& r) : real(r), dual(type()) {}

		// 四则（对偶数代数：ε²=0）
		inline dualnum operator+(const dualnum& v) const { return dualnum(real + v.real, dual + v.dual); }
		inline dualnum operator-(const dualnum& v) const { return dualnum(real - v.real, dual - v.dual); }
		inline dualnum operator*(const dualnum& v) const { return dualnum(real * v.real, real * v.dual + dual * v.real); }
		inline dualnum operator/(const dualnum& v) const
		{
			type r = real / v.real;
			type d = (dual * v.real - real * v.dual) / (v.real * v.real);
			return dualnum(r, d);
		}
		inline dualnum operator-() const { return dualnum(-real, -dual); }

		// 与标量
		inline dualnum operator+(const type& s) const { return dualnum(real + s, dual); }
		inline dualnum operator-(const type& s) const { return dualnum(real - s, dual); }
		inline dualnum operator*(const type& s) const { return dualnum(real * s, dual * s); }
		inline dualnum operator/(const type& s) const { return dualnum(real / s, dual / s); }

		inline dualnum operator+=(const dualnum& v) { (*this) = (*this) + v; return (*this); }
		inline dualnum operator-=(const dualnum& v) { (*this) = (*this) - v; return (*this); }
		inline dualnum operator*=(const dualnum& v) { (*this) = (*this) * v; return (*this); }
		inline dualnum operator/=(const dualnum& v) { (*this) = (*this) / v; return (*this); }

		inline bool operator==(const dualnum& v) const { return real == v.real && dual == v.dual; }
		inline bool operator!=(const dualnum& v) const { return !((*this) == v); }
	};

	// xhqm 泛型函数特化
	template<typename type> inline dualnum<type> sum(const dualnum<type>& left, const dualnum<type>& right) { return left + right; }
	template<typename type> inline dualnum<type> sub(const dualnum<type>& left, const dualnum<type>& right) { return left - right; }
	template<typename type> inline dualnum<type> mul(const dualnum<type>& left, const dualnum<type>& right) { return left * right; }
	template<typename type> inline dualnum<type> dot(const dualnum<type>& left, const dualnum<type>& right) { return left * right; }
	template<typename type> inline dualnum<type> div(const dualnum<type>& left, const dualnum<type>& right) { return left / right; }
	template<typename type> inline type abs(const dualnum<type>& value) { return std::sqrt(value.real * value.real + value.dual * value.dual); }
	template<typename type> inline bool equal(const dualnum<type>& left, const dualnum<type>& right) { return left == right; }

	// 数学（自动微分，链式法则）
	template<typename type> inline dualnum<type> sin(const dualnum<type>& value) { return dualnum<type>(std::sin(value.real), value.dual * std::cos(value.real)); }
	template<typename type> inline dualnum<type> cos(const dualnum<type>& value) { return dualnum<type>(std::cos(value.real), -value.dual * std::sin(value.real)); }
	template<typename type> inline dualnum<type> tan(const dualnum<type>& value) { type c = std::cos(value.real); return dualnum<type>(std::tan(value.real), value.dual / (c * c)); }
	template<typename type> inline dualnum<type> sqrt(const dualnum<type>& value) { type s = std::sqrt(value.real); return dualnum<type>(s, value.dual / (type(2) * s)); }
	template<typename type> inline dualnum<type> exp(const dualnum<type>& value) { type e = std::exp(value.real); return dualnum<type>(e, value.dual * e); }
	template<typename type> inline dualnum<type> log(const dualnum<type>& value) { return dualnum<type>(std::log(value.real), value.dual / value.real); }
	template<typename type> inline dualnum<type> pow(const dualnum<type>& value, const dualnum<type>& exp)
	{
		type r = std::pow(value.real, exp.real);
		type d = r * (exp.dual * std::log(value.real) + exp.real * value.dual / value.real);
		return dualnum<type>(r, d);
	}

	// 序列化："real+duale"
	template<typename type> inline std::string save(const dualnum<type>& value)
	{
		return std::to_string(value.real) + "+" + std::to_string(value.dual) + "e";
	}
	template<> inline dualnum<f32> read(const std::string& value)
	{
		xhqm::size index_r = value.find('+');
		xhqm::size index_e = value.find('e');
		return dualnum<f32>(
			xhqm::read<f32>(value.substr(0, index_r)),
			xhqm::read<f32>(value.substr(index_r + 1, index_e - index_r - 1)));
	}
	template<> inline dualnum<f64> read(const std::string& value)
	{
		xhqm::size index_r = value.find('+');
		xhqm::size index_e = value.find('e');
		return dualnum<f64>(
			xhqm::read<f64>(value.substr(0, index_r)),
			xhqm::read<f64>(value.substr(index_r + 1, index_e - index_r - 1)));
	}
	template<> inline dualnum<fld> read(const std::string& value)
	{
		xhqm::size index_r = value.find('+');
		xhqm::size index_e = value.find('e');
		return dualnum<fld>(
			xhqm::read<fld>(value.substr(0, index_r)),
			xhqm::read<fld>(value.substr(index_r + 1, index_e - index_r - 1)));
	}
}

#endif
