#ifndef XHQM_FTYPE
#define XHQM_FTYPE

#include "mathf.h"
#include "itype.h"
#include <cstring>
//当指数位全为1且尾数位非0时，表示NaN（Not a Number，非数）。
//当指数位全为1且尾数位为0时，表示无穷大（根据符号位区分正无穷和负无穷）。
//当指数位全为0且尾数位为0时，表示数值零（根据符号位可区分正零和负零）。
//当指数位全为0但尾数位非0时，表示非规范化数，用于表示非常接近于0的数值。

namespace xhqm
{
    struct xhqm_io float_16 {
        xhqm::u16 fraction : 10;
        xhqm::u16 exponent : 5;
        xhqm::u16 sign : 1;

        constexpr float_16()
            : fraction(0b0000000000000000u)
            , exponent(0b0000000000000000u)
            , sign(0b0000000000000000u)
        {
            
        }

        float_16(const float& fValue);

        operator float() const;

        //true + //false -
        inline float_16 most(bool us = true)
        {
            xhqm::u16 value = us ? 0b0000001111111111u : 0b1000001111111111u;
            memcpy(this, &value, sizeof(xhqm::float_16));
            return (*this);
        }
        //true + //false -
        inline float_16 zero(bool us = true)
        {
            xhqm::u16 value = us ? 0b0000000000000000u : 0b1000000000000000u;
            memcpy(this, &value, sizeof(xhqm::float_16));
            return (*this);
        }
        //true + //false -
        inline float_16 inf(bool us = true)
        {
            xhqm::u16 value = us ? 0b0111110000000000u : 0b1111110000000000u;
            memcpy(this, &value, sizeof(xhqm::float_16));
            return (*this);
        }
        //true + //false -
        inline float_16 nan(bool us = true)
        {
            xhqm::u16 value = us ? 0b0111111111111111u : 0b1111111111111111u;
            memcpy(this, &value, sizeof(xhqm::float_16));
            return (*this);
        }
    };

	using float_32 = float;
	using float_64 = double;
	using float_ld = long double;

    using f16 = xhqm::float_16;
    using f32 = xhqm::float_32;
    using f64 = xhqm::float_64;
    using fld = xhqm::float_ld;

    static constexpr auto f32min = std::numeric_limits<f32>::min();
    static constexpr auto f64min = std::numeric_limits<f64>::min();
    static constexpr auto fldmin = std::numeric_limits<fld>::min();
    static constexpr auto f32max = std::numeric_limits<f32>::max();
    static constexpr auto f64max = std::numeric_limits<f64>::max();
    static constexpr auto fldmax = std::numeric_limits<fld>::max();

    static constexpr auto f32inf = std::numeric_limits<f32>::infinity();
    static constexpr auto f64inf = std::numeric_limits<f64>::infinity();
    static constexpr auto fldinf = std::numeric_limits<fld>::infinity();
    static constexpr auto f32nan = std::numeric_limits<f32>::quiet_NaN();
    static constexpr auto f64nan = std::numeric_limits<f64>::quiet_NaN();
    static constexpr auto fldnan = std::numeric_limits<fld>::quiet_NaN();

    template<> struct number<f16>
    {
    	inline static f16 en		= static_cast<float>(xhqm::en);
    	inline static f16 pi		= static_cast<float>(xhqm::pi);
    	inline static f16 phi		= static_cast<float>(xhqm::phi);
    	inline static f16 egamma	= static_cast<float>(xhqm::egamma);
    	inline static f16 ln_2		= static_cast<float>(xhqm::ln_2);
    	inline static f16 ln_10		= static_cast<float>(xhqm::ln_10);
    	inline static f16 log_e_2	= static_cast<float>(xhqm::log_e_2);
    	inline static f16 log_e_10	= static_cast<float>(xhqm::log_e_10);
    	inline static f16 log_2_e	= static_cast<float>(xhqm::log_2_e);
    	inline static f16 log_10_e	= static_cast<float>(xhqm::log_10_e);
    	inline static f16 sqrt_2	= static_cast<float>(xhqm::sqrt_2);
    	inline static f16 sqrt_3	= static_cast<float>(xhqm::sqrt_3);
    	inline static f16 sqrt_en	= static_cast<float>(xhqm::sqrt_en);
    	inline static f16 sqrt_pi	= static_cast<float>(xhqm::sqrt_pi);
    	inline static f16 num_0		= f16().zero(1);
    	inline static f16 num_1		= static_cast<float>(xhqm::num_1);
    	inline static f16 num_2		= static_cast<float>(xhqm::num_2);
    	inline static f16 num_3		= static_cast<float>(xhqm::num_3);
    	inline static f16 num_4		= static_cast<float>(xhqm::num_4);
    	inline static f16 num_5		= static_cast<float>(xhqm::num_5);
    	inline static f16 num_6		= static_cast<float>(xhqm::num_6);
    	inline static f16 num_7		= static_cast<float>(xhqm::num_7);
    	inline static f16 num_8		= static_cast<float>(xhqm::num_8);
    	inline static f16 num_9		= static_cast<float>(xhqm::num_9);
    	inline static f16 num_10	= static_cast<float>(xhqm::num_10);	
    	inline static f16 num_11	= static_cast<float>(xhqm::num_11);
    	inline static f16 preci		= 0.001f;
        inline constexpr static f16 zero = f16();
    };

    template<> inline f16 min(const f16& left, const f16& right) { return std::min<float>(left, right); }
    template<> inline f16 max(const f16& left, const f16& right) { return std::max<float>(left, right); }
    template<> inline f16 sum(const f16& left, const f16& right) { return left + right; }
    template<> inline f16 sub(const f16& left, const f16& right) { return left - right; }
    template<> inline f16 mul(const f16& left, const f16& right) { return left * right; }
    template<> inline f16 dot(const f16& left, const f16& right) { return left * right; }
    template<> inline f16 div(const f16& left, const f16& right) { return left / right; }
    template<> inline bool most(const f16& left, const f16& right) { return (left > right); }
    template<> inline bool less(const f16& left, const f16& right) { return (left < right); }
    template<> inline bool equal(const f16& left, const f16& right) { return (left == right); }
    template<> inline bool equal_most(const f16& left, const f16& right) { return (left >= right); }
    template<> inline bool equal_less(const f16& left, const f16& right) { return (left <= right); }
    template<> inline f16 abs(const f16& value) { return std::abs(value); }
    template<> inline f16 sin(const f16& value) { return std::sin(value); }
    template<> inline f16 cos(const f16& value) { return std::cos(value); }
    template<> inline f16 tan(const f16& value) { return std::tan(value); }
    template<> inline f16 asin(const f16& value) { return std::asin(value); }
    template<> inline f16 acos(const f16& value) { return std::acos(value); }
    template<> inline f16 atan(const f16& value) { return std::atan(value); }
    template<> inline f16 log(const f16& value) { return std::log(value); }
    template<> inline f16 pow(const f16& value) { return std::exp(value); }
    template<> inline f16 log(const f16& value, const f16& exp) { return std::log(value) / std::log(exp); }
    template<> inline f16 pow(const f16& value, const f16& exp) { return std::pow(value, exp); }
    template<> inline f16 sqrt(const f16& value) { return std::sqrt(value); }
    template<> inline f16 min(f16& value) { value.most(0); return value; }
    template<> inline f16 max(f16& value) { value.most(1); return value; }
    template<> inline f16 nan(f16& value) { value.nan(1); return value; } 
    template<> inline f16 inf(f16& value) { value.inf(1); return value; } 
    template<> inline bool ismin(const f16& value) { f16 val; return (value == xhqm::min<f16>(val)); }
    template<> inline bool ismax(const f16& value) { f16 val; return (value == xhqm::max<f16>(val)); }
    //template<> inline bool isnan(const f16& value) { return std::isnan<float>(value); }
    //template<> inline bool isinf(const f16& value) { return std::isinf<float>(value); }
    template<> inline bool isnan(const f16& value) { return (value == (f16()).nan(0) || value == (f16()).nan(1)); }
    template<> inline bool isinf(const f16& value) { return (value == (f16()).inf(0) || value == (f16()).inf(1)); }
    template<> inline f16 read(const std::string& value) { return xhqm::float_16(std::stof(value)); }
    template<> inline std::string save(const f16& value) { return std::to_string(value.operator float()); }


    template<> inline f32 min(f32& value) { value = f32min; return value; }
    template<> inline f32 max(f32& value) { value = f32max; return value; }
    template<> inline f32 nan(f32& value) { value = f32nan; return value; }
    template<> inline f32 inf(f32& value) { value = f32inf; return value; }
    template<> inline bool ismin(const f32& value) { return (value == f32min); }
    template<> inline bool ismax(const f32& value) { return (value == f32max); }
    template<> inline f32 read(const std::string& value) { return std::stof(value); }
    template<> inline std::string save(const f32& value) { return std::to_string(value); }

    template<> inline f64 min(f64& value) { value = f64min; return value; }
    template<> inline f64 max(f64& value) { value = f64max; return value; }
    template<> inline f64 nan(f64& value) { value = f64nan; return value; }
    template<> inline f64 inf(f64& value) { value = f64inf; return value; }
    template<> inline bool ismin(const f64& value) { return (value == f64min); }
    template<> inline bool ismax(const f64& value) { return (value == f64max); }
    template<> inline f64 read(const std::string& value) { return std::stod(value); }
    template<> inline std::string save(const f64& value) { return std::to_string(value); }
               
    template<> inline fld min(fld& value) { value = std::numeric_limits<fld>::min();       return value; }
    template<> inline fld max(fld& value) { value = std::numeric_limits<fld>::max();       return value; }
    template<> inline fld nan(fld& value) { value = std::numeric_limits<fld>::quiet_NaN(); return value; }
    template<> inline fld inf(fld& value) { value = std::numeric_limits<fld>::infinity();  return value; }
    template<> inline bool ismin(const fld& value) { return (value == std::numeric_limits<fld>::min()); }
    template<> inline bool ismax(const fld& value) { return (value == std::numeric_limits<fld>::max()); }
    template<> inline fld read(const std::string& value) { return std::stold(value); }
    template<> inline std::string save(const fld& value) { return std::to_string(value); }
}
#endif