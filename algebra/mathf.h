#ifndef XHQM_MATH_FUNCTION
#define XHQM_MATH_FUNCTION

#include <cmath>
#include <math.h>
#include <string>
#include <cstdlib>
#include <numeric>
//#include <numbers>
#include "value.h"

namespace xhqm
{
	template<typename type> inline type min(const type& left, const type& right) { return std::min(left, right); }
	template<typename type> inline type max(const type& left, const type& right) { return std::max(left, right); }
	template<typename type> inline type sum(const type& left, const type& right) { return left + right; }
	template<typename type> inline type sub(const type& left, const type& right) { return left - right; }
	template<typename type> inline type mul(const type& left, const type& right) { return left * right; }	// 乘法（分量/逐元素）
	template<typename type> inline type dot(const type& left, const type& right) { return left * right; }	// 内积（标量点积=乘法）
	template<typename type> inline type div(const type& left, const type& right) { return left / right; }
	template<typename type> inline bool most(const type& left, const type& right) { return (left > right); }
	template<typename type> inline bool less(const type& left, const type& right) { return (left < right); }
	template<typename type> inline bool equal(const type& left, const type& right) { return (left == right); }
	template<typename type> inline bool equal_most(const type& left, const type& right) { return (left >= right); }
	template<typename type> inline bool equal_less(const type& left, const type& right) { return (left <= right); }
	template<typename type> inline type abs(const type& value) { return std::abs(value); }
	template<typename type> inline type sin(const type& value) { return std::sin(value); }
	template<typename type> inline type cos(const type& value) { return std::cos(value); }
	template<typename type> inline type tan(const type& value) { return std::tan(value); }
	template<typename type> inline type asin(const type& value) { return std::asin(value); }
	template<typename type> inline type acos(const type& value) { return std::acos(value); }
	template<typename type> inline type atan(const type& value) { return std::atan(value); }
	template<typename type> inline type log(const type& value) { return std::log(value); }
	template<typename type> inline type pow(const type& value) { return std::exp(value); }
	template<typename type> inline type log(const type& value, const type& exp) { return std::log(value) / std::log(exp); }
	template<typename type> inline type pow(const type& value, const type& exp) { return std::pow(value, exp); }
	template<typename type> inline type sqrt(const type& value) { return std::sqrt(value); }
	template<typename type> inline type min(type& value) { value = type(); return value; }
	template<typename type> inline type max(type& value) { value = type(); return value; }
	template<typename type> inline type nan(type& value) { value = type(); return value; }
	template<typename type> inline type inf(type& value) { value = type(); return value; }
	template<typename type> inline bool ismin(const type& value) { return false; }
	template<typename type> inline bool ismax(const type& value) { return false; }
	template<typename type> inline bool isnan(const type& value) { return std::isnan(value); }
	template<typename type> inline bool isinf(const type& value) { return std::isinf(value); }

	//—— 布尔环（GF(2)）特化：加法=减法=XOR（⊕），乘法=除法=AND（∧）——
	// {0,1} 构成特征 2 的域：1+1=0，每元素自为负元（-x=x）；除法除以 1 恒等、除以 0 兜底为 0
	template<> inline bool sum(const bool& left, const bool& right) { return left != right; }
	template<> inline bool sub(const bool& left, const bool& right) { return left != right; }
	template<> inline bool mul(const bool& left, const bool& right) { return left && right; }
	template<> inline bool dot(const bool& left, const bool& right) { return left && right; }
	template<> inline bool div(const bool& left, const bool& right) { return left && right; }

	template<typename type> inline type read(const std::string& value) { return type(); }
	template<typename type> inline std::string save(const type& value) { return "type"; }
}

#endif
