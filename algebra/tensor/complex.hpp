#ifndef XHQM_COMPLEX
#define XHQM_COMPLEX

#include "../mathf.h"
#include "../itype.h"
#include "../ftype.h"
#include <complex>

namespace xhqm
{
	template<typename type> using complex = std::complex<type>;


	template<typename type> type abs(const xhqm::complex<type>& value) { return std::abs(value); }
	template<typename type> xhqm::complex<type> sin(const xhqm::complex<type>& value) { return std::sin(value); }
	template<typename type> xhqm::complex<type> cos(const xhqm::complex<type>& value) { return std::cos(value); }
	template<typename type> xhqm::complex<type> tan(const xhqm::complex<type>& value) { return std::tan(value); }
	template<typename type> xhqm::complex<type> asin(const xhqm::complex<type>& value) { return std::asin(value); }
	template<typename type> xhqm::complex<type> acos(const xhqm::complex<type>& value) { return std::acos(value); }
	template<typename type> xhqm::complex<type> atan(const xhqm::complex<type>& value) { return std::atan(value); }
	template<typename type> xhqm::complex<type> min(const xhqm::complex<type>& left, const xhqm::complex<type>& right) { return std::min(std::abs(left), std::abs(right)); }
	template<typename type> xhqm::complex<type> max(const xhqm::complex<type>& left, const xhqm::complex<type>& right) { return std::max(std::abs(left), std::abs(right)); }
	template<typename type> xhqm::complex<type> sum(const xhqm::complex<type>& left, const xhqm::complex<type>& right) { return left + right; }
	template<typename type> xhqm::complex<type> sub(const xhqm::complex<type>& left, const xhqm::complex<type>& right) { return left - right; }
	template<typename type> xhqm::complex<type> mul(const xhqm::complex<type>& left, const xhqm::complex<type>& right) { return left * right; }
	template<typename type> type dot(const xhqm::complex<type>& left, const xhqm::complex<type>& right) { return left.real() * right.real() + left.imag() * right.imag(); }
	template<typename type> xhqm::complex<type> div(const xhqm::complex<type>& left, const xhqm::complex<type>& right) { return left / right; }
	template<typename type> bool most(const xhqm::complex<type>& left, const xhqm::complex<type>& right) { return (std::abs(left) > std::abs(right)); }
	template<typename type> bool less(const xhqm::complex<type>& left, const xhqm::complex<type>& right) { return (std::abs(left) < std::abs(right)); }
	template<typename type> bool equal_most(const xhqm::complex<type>& left, const xhqm::complex<type>& right) { return (std::abs(left) >= std::abs(right)); }
	template<typename type> bool equal_less(const xhqm::complex<type>& left, const xhqm::complex<type>& right) { return (std::abs(left) <= std::abs(right)); }
	template<typename type> bool equal(const xhqm::complex<type>& left, const xhqm::complex<type>& right) { return (left == right); }
	template<typename type> xhqm::complex<type> log(const xhqm::complex<type>& value) { return std::log(value); }
	template<typename type> xhqm::complex<type> pow(const xhqm::complex<type>& value) { return std::exp(value); }
	template<typename type> xhqm::complex<type> log(const xhqm::complex<type>& value, const xhqm::complex<type>& exp) { return std::log(value) / std::log(exp); }
	template<typename type> xhqm::complex<type> log(const xhqm::complex<type>& value, const type& exp) { return std::log(value) / std::log(exp); }
	template<typename type> xhqm::complex<type> log(const type& value, const xhqm::complex<type>& exp) { return std::log(value) / std::log(exp); }
	template<typename type> xhqm::complex<type> pow(const xhqm::complex<type>& value, const xhqm::complex<type>& exp) { return std::pow(value, exp); }
	template<typename type> xhqm::complex<type> pow(const xhqm::complex<type>& value, const type& exp) { return std::pow(value, exp); }
	template<typename type> xhqm::complex<type> pow(const type& value, const xhqm::complex<type>& exp) { return std::pow(value, exp); }
	template<typename type> xhqm::complex<type> sqrt(const xhqm::complex<type>& value) { return std::sqrt(value); }

	template<typename type> xhqm::complex<type> min(xhqm::complex<type>& value) { type val = type(); xhqm::min<type>(val); value = xhqm::complex<type>(val, val); return value; }
	template<typename type> xhqm::complex<type> max(xhqm::complex<type>& value) { type val = type(); xhqm::max<type>(val); value = xhqm::complex<type>(val, val); return value; }
	template<typename type> xhqm::complex<type> nan(xhqm::complex<type>& value) { type val = type(); xhqm::nan<type>(val); value = xhqm::complex<type>(val, val); return value; }
	template<typename type> xhqm::complex<type> inf(xhqm::complex<type>& value) { type val = type(); xhqm::inf<type>(val); value = xhqm::complex<type>(val, val); return value; }
	template<typename type> bool ismin(const xhqm::complex<type>& value) { type val = type(); xhqm::min<type>(val); return (value == xhqm::complex<type>(val, val)); }
	template<typename type> bool ismax(const xhqm::complex<type>& value) { type val = type(); xhqm::max<type>(val); return (value == xhqm::complex<type>(val, val)); }
	template<typename type> bool isnan(const xhqm::complex<type>& value) { return std::isnan(value.real()) || std::isnan(value.imag()); }
	template<typename type> bool isinf(const xhqm::complex<type>& value) { return std::isinf(value.real()) || std::isinf(value.imag()); }

	template<> inline std::string save(const xhqm::complex<xhqm::f32>& value) { return std::to_string(value.real()) + "+" + std::to_string(value.imag()) + "i"; }
	template<> inline std::string save(const xhqm::complex<xhqm::f64>& value) { return std::to_string(value.real()) + "+" + std::to_string(value.imag()) + "i"; }
	template<> inline std::string save(const xhqm::complex<xhqm::fld>& value) { return std::to_string(value.real()) + "+" + std::to_string(value.imag()) + "i"; }

	template<> inline xhqm::complex<xhqm::f32> read(const std::string& value)
	{ 
		xhqm::complex<xhqm::f32> res_value;
		xhqm::index index_r = value.find_first_of('+');
		xhqm::index index_i = value.find_first_of('i');
		xhqm::string value_r = value.substr(0, index_r);
		xhqm::string value_i = value.substr(index_r + 1, index_i);
		res_value.real(xhqm::read<xhqm::f32>(value_r));
		res_value.imag(xhqm::read<xhqm::f32>(value_i));
		return res_value;
	}
	template<> inline xhqm::complex<xhqm::f64> read(const std::string& value)
	{
		xhqm::complex<xhqm::f64> res_value;
		xhqm::index index_r = value.find_first_of('+');
		xhqm::index index_i = value.find_first_of('i');
		xhqm::string value_r = value.substr(0, index_r);
		xhqm::string value_i = value.substr(index_r + 1, index_i);
		res_value.real(xhqm::read<xhqm::f64>(value_r));
		res_value.imag(xhqm::read<xhqm::f64>(value_i));
		return res_value;
	}
	template<> inline xhqm::complex<xhqm::fld> read(const std::string& value)
	{
		xhqm::complex<xhqm::fld> res_value;
		xhqm::index index_r = value.find_first_of('+');
		xhqm::index index_i = value.find_first_of('i');
		xhqm::string value_r = value.substr(0, index_r);
		xhqm::string value_i = value.substr(index_r + 1, index_i);
		res_value.real(xhqm::read<xhqm::fld>(value_r));
		res_value.imag(xhqm::read<xhqm::fld>(value_i));
		return res_value;
	}
}

#endif