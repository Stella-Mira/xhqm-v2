#ifndef XHQM_MATH_VALUE
#define XHQM_MATH_VALUE
#include "../head.h"
#include <numeric>
//#include <numbers>

namespace xhqm
{
	//常数
	static constexpr auto en       = 2.71828182845904523536028747135266249775724709369996l;//自然底数
	static constexpr auto pi       = 3.14159265358979323846264338327950288419716939937510l;//圆周率π
	static constexpr auto phi      = 0.618033988749894848204586834365638117720309179805763;//黄金比Ф
	static constexpr auto egamma   = 0.577215664901532860606512090082402431042159335939924;//欧拉-马歇罗尼常数γ
	static constexpr auto ln_2     = 0.0l;
	static constexpr auto ln_10    = 0.0l;
	static constexpr auto log_e_2  = 0.0l;
	static constexpr auto log_e_10 = 0.0l;
	static constexpr auto log_2_e  = 0.0l;
	static constexpr auto log_10_e = 0.0l;
	static constexpr auto sqrt_2   = 0.0l;
	static constexpr auto sqrt_3   = 0.0l;
	static constexpr auto sqrt_en  = 0.0l;
	static constexpr auto sqrt_pi  = 0.0l;

	//static constexpr auto zero  = 0x0ui8;		//自然数
	static constexpr unsigned char num_0 = 0x0u;		//自然数
	static constexpr unsigned char num_1 = 0x1u;		//自然数
	static constexpr unsigned char num_2 = 0x2u;		//自然数
	static constexpr unsigned char num_3 = 0x3u;		//自然数
	static constexpr unsigned char num_4 = 0x4u;		//自然数
	static constexpr unsigned char num_5 = 0x5u;		//自然数
	static constexpr unsigned char num_6 = 0x6u;		//自然数
	static constexpr unsigned char num_7 = 0x7u;		//自然数
	static constexpr unsigned char num_8 = 0x8u;		//自然数
	static constexpr unsigned char num_9 = 0x9u;		//自然数
	static constexpr unsigned char num_10 = 0xAu;		//自然数
	static constexpr unsigned char num_11 = 0xBu;		//自然数

	static constexpr auto precif = 0.00001f;		           //单浮点精度
	static constexpr auto precid = 0.00000000001;              //双浮点精度
	static constexpr auto precil = 0.0000000000000000001l;     //四浮点精度
	template <typename type = float> constexpr type preci = 0;
	template <> inline constexpr auto preci<float>       = xhqm::precif;
	template <> inline constexpr auto preci<double>      = xhqm::precid;
	template <> inline constexpr auto preci<long double> = xhqm::precil;
	static constexpr auto npos = static_cast<xhqm::count>(-1); //下标上限

	using utype = std::uintmax_t;
	using itype = std::intmax_t;
	using ftype = double;
	using umax = xhqm::utype;
	using imax = xhqm::itype;
	using fmax = xhqm::ftype;

	//数学体系
	using natural = xhqm::umax;//自然数
	using integer = xhqm::imax;//整数
	using measure = xhqm::fmax;//实数
	using symbols = unsigned char;//符号
	using duality = bool;		  //是否
	using anycast = void*;		  //事物

	template<typename type>
	struct number
	{
		constexpr static auto en		= static_cast<type>(xhqm::en);
		constexpr static auto pi		= static_cast<type>(xhqm::pi);
		constexpr static auto phi		= static_cast<type>(xhqm::phi);
		constexpr static auto egamma	= static_cast<type>(xhqm::egamma);
		constexpr static auto ln_2		= static_cast<type>(xhqm::ln_2);
		constexpr static auto ln_10		= static_cast<type>(xhqm::ln_10);
		constexpr static auto log_e_2	= static_cast<type>(xhqm::log_e_2);
		constexpr static auto log_e_10	= static_cast<type>(xhqm::log_e_10);
		constexpr static auto log_2_e	= static_cast<type>(xhqm::log_2_e);
		constexpr static auto log_10_e	= static_cast<type>(xhqm::log_10_e);
		constexpr static auto sqrt_2	= static_cast<type>(xhqm::sqrt_2);
		constexpr static auto sqrt_3	= static_cast<type>(xhqm::sqrt_3);
		constexpr static auto sqrt_en	= static_cast<type>(xhqm::sqrt_en);
		constexpr static auto sqrt_pi	= static_cast<type>(xhqm::sqrt_pi);
		constexpr static auto zero		= static_cast<type>(xhqm::num_0);
		constexpr static auto num_0		= static_cast<type>(xhqm::num_0);
		constexpr static auto num_1		= static_cast<type>(xhqm::num_1);
		constexpr static auto num_2		= static_cast<type>(xhqm::num_2);
		constexpr static auto num_3		= static_cast<type>(xhqm::num_3);
		constexpr static auto num_4		= static_cast<type>(xhqm::num_4);
		constexpr static auto num_5		= static_cast<type>(xhqm::num_5);
		constexpr static auto num_6		= static_cast<type>(xhqm::num_6);
		constexpr static auto num_7		= static_cast<type>(xhqm::num_7);
		constexpr static auto num_8		= static_cast<type>(xhqm::num_8);
		constexpr static auto num_9		= static_cast<type>(xhqm::num_9);
		constexpr static auto num_10	= static_cast<type>(xhqm::num_10);	
		constexpr static auto num_11	= static_cast<type>(xhqm::num_11);
		constexpr static auto preci		= xhqm::preci<type>;
	};
}

#endif
