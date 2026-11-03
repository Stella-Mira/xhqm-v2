#ifndef XHQM_ANALYSIS_MATH
#define XHQM_ANALYSIS_MATH

// 逐元素数学（MATLAB 风格）：对 matrix / vector 每个元素应用标量函数。
// f32/f64 走 tensor 后端 unary kernel（SIMD/CUDA/Metal），其余类型回退 xhqm 泛型函数。
// 组合函数（cot/sec/deg2rad 等）不占新算子，用 unary + elemwise/标量乘组合。
#include "../tensor/matrix.h"
#include "../tensor/vector.h"
#include "../tensor/backend/tensor_backend.h"
#include <cmath>
#include <type_traits>

namespace xhqm
{
	namespace analysis
	{
		namespace detail
		{
			// 逐元素单参映射（matrix，标量回退）
			template<class type, class F>
			matrix<type> map(const matrix<type>& m, F&& f)
			{
				matrix<type> r(m.rows(), m.cols());
				const matrix<type>& a = m;
				xhqm::tensor_base::tensor_backend_active().parallel_for(0, m.size(), [&](xhqm::size i)
				{
					r.data()[i] = f(a.data()[i]);
				});
				return r;
			}
			// 逐元素单参映射（vector，标量回退）
			template<class type, class F>
			vector<type> mapv(const vector<type>& v, F&& f)
			{
				vector<type> r(v.size());
				const vector<type>& a = v;
				xhqm::tensor_base::tensor_backend_active().parallel_for(0, v.size(), [&](xhqm::size i)
				{
					r.data()[i] = f(a.data()[i]);
				});
				return r;
			}
			// 一元算子：优先 backend.unary kernel（支持与否由后端决定），回退标量函数
			template<class type, class F>
			matrix<type> unary(const matrix<type>& m, int op, F&& scalar)
			{
				matrix<type> r(m.rows(), m.cols());
				if (xhqm::tensor_base::tensor_backend_active().unary(
					r.data(), m.data(), m.size(), xhqm::tensor_base::dtype_of<type>::value, op))
					return r;
				return map(m, std::forward<F>(scalar));
			}
			template<class type, class F>
			vector<type> unaryv(const vector<type>& v, int op, F&& scalar)
			{
				vector<type> r(v.size());
				if (xhqm::tensor_base::tensor_backend_active().unary(
					r.data(), v.data(), v.size(), xhqm::tensor_base::dtype_of<type>::value, op))
					return r;
				return mapv(v, std::forward<F>(scalar));
			}
			// 求倒数 1/x：unary_recip kernel，回退标量
			template<class type>
			matrix<type> reciprocal(const matrix<type>& m)
			{
				return unary(m, xhqm::tensor_base::unary_recip, [](const type& v) { return xhqm::div(type(1), v); });
			}
			// 一元判断算子：backend.unary 返回 0/1，再转 bool 矩阵/向量
			template<class type, class F>
			matrix<bool> unary_bool(const matrix<type>& m, int op, F&& scalar)
			{
				matrix<type> t = unary(m, op, std::forward<F>(scalar));
				matrix<bool> r(m.rows(), m.cols());
				for (xhqm::size i = 0; i < m.size(); ++i) r.data()[i] = (t.data()[i] != type(0));
				return r;
			}
			template<class type, class F>
			vector<bool> unaryv_bool(const vector<type>& v, int op, F&& scalar)
			{
				vector<type> t = unaryv(v, op, std::forward<F>(scalar));
				vector<bool> r(v.size());
				for (xhqm::size i = 0; i < v.size(); ++i) r[i] = (t[i] != type(0));
				return r;
			}
			// 二元逐元素：优先 backend.elemwise kernel（op 见 elemwise_op），回退标量
			template<class type, class F>
			matrix<type> elemwise_binary(const matrix<type>& a, const matrix<type>& b, int op, F&& scalar)
			{
				if (a.rows() != b.rows() || a.cols() != b.cols()) return matrix<type>();
				matrix<type> r(a.rows(), a.cols());
				if (xhqm::tensor_base::tensor_backend_active().elemwise(
					r.data(), a.data(), b.data(), a.size(), xhqm::tensor_base::dtype_of<type>::value, op))
					return r;
				const matrix<type>& aa = a; const matrix<type>& bb = b;
				xhqm::tensor_base::tensor_backend_active().parallel_for(0, a.size(), [&](xhqm::size i)
				{
					r.data()[i] = scalar(aa.data()[i], bb.data()[i]);
				});
				return r;
			}
		}

		// —— 三角（unary kernel）——
		template<class type> matrix<type> sin(const matrix<type>& m) { return detail::unary(m, xhqm::tensor_base::unary_sin, [](const type& v) { return xhqm::sin(v); }); }
		template<class type> matrix<type> cos(const matrix<type>& m) { return detail::unary(m, xhqm::tensor_base::unary_cos, [](const type& v) { return xhqm::cos(v); }); }
		template<class type> matrix<type> tan(const matrix<type>& m) { return detail::unary(m, xhqm::tensor_base::unary_tan, [](const type& v) { return xhqm::tan(v); }); }
		template<class type> matrix<type> asin(const matrix<type>& m) { return detail::unary(m, xhqm::tensor_base::unary_asin, [](const type& v) { return xhqm::asin(v); }); }
		template<class type> matrix<type> acos(const matrix<type>& m) { return detail::unary(m, xhqm::tensor_base::unary_acos, [](const type& v) { return xhqm::acos(v); }); }
		template<class type> matrix<type> atan(const matrix<type>& m) { return detail::unary(m, xhqm::tensor_base::unary_atan, [](const type& v) { return xhqm::atan(v); }); }
		template<class type> matrix<type> sinh(const matrix<type>& m) { return detail::unary(m, xhqm::tensor_base::unary_sinh, [](const type& v) { return ::std::sinh(v); }); }
		template<class type> matrix<type> cosh(const matrix<type>& m) { return detail::unary(m, xhqm::tensor_base::unary_cosh, [](const type& v) { return ::std::cosh(v); }); }
		template<class type> matrix<type> tanh(const matrix<type>& m) { return detail::unary(m, xhqm::tensor_base::unary_tanh, [](const type& v) { return ::std::tanh(v); }); }

		// —— 指数对数（unary kernel）——
		template<class type> matrix<type> exp(const matrix<type>& m) { return detail::unary(m, xhqm::tensor_base::unary_exp, [](const type& v) { return ::std::exp(v); }); }
		template<class type> matrix<type> log(const matrix<type>& m) { return detail::unary(m, xhqm::tensor_base::unary_log, [](const type& v) { return ::std::log(v); }); }
		template<class type> matrix<type> log10(const matrix<type>& m) { return detail::unary(m, xhqm::tensor_base::unary_log10, [](const type& v) { return ::std::log10(v); }); }
		template<class type> matrix<type> log2(const matrix<type>& m) { return detail::unary(m, xhqm::tensor_base::unary_log2, [](const type& v) { return ::std::log2(v); }); }
		template<class type> matrix<type> sqrt(const matrix<type>& m) { return detail::unary(m, xhqm::tensor_base::unary_sqrt, [](const type& v) { return xhqm::sqrt(v); }); }

		// 逐元素幂 power(A, B)（MATLAB .^），形状须一致
		template<class type>
		matrix<type> power(const matrix<type>& a, const matrix<type>& b)
		{
			return detail::elemwise_binary(a, b, xhqm::tensor_base::elem_pow,
				[](const type& x, const type& y) { return ::std::pow(x, y); });
		}
		// 逐元素幂 power(A, s)（标量指数）
		template<class type>
		matrix<type> power(const matrix<type>& a, const type& s)
		{
			return detail::map(a, [&](const type& v) { return ::std::pow(v, s); });
		}

		// —— 取整（unary kernel，整型无意义拦截）——
		template<class type> matrix<type> floor(const matrix<type>& m) { static_assert(!std::is_integral_v<type>, "analysis::floor 对整型无意义"); return detail::unary(m, xhqm::tensor_base::unary_floor, [](const type& v) { return static_cast<type>(::std::floor(v)); }); }
		template<class type> matrix<type> ceil(const matrix<type>& m) { static_assert(!std::is_integral_v<type>, "analysis::ceil 对整型无意义"); return detail::unary(m, xhqm::tensor_base::unary_ceil, [](const type& v) { return static_cast<type>(::std::ceil(v)); }); }
		template<class type> matrix<type> round(const matrix<type>& m) { static_assert(!std::is_integral_v<type>, "analysis::round 对整型无意义"); return detail::unary(m, xhqm::tensor_base::unary_round, [](const type& v) { return static_cast<type>(::std::round(v)); }); }
		template<class type> matrix<type> trunc(const matrix<type>& m) { static_assert(!std::is_integral_v<type>, "analysis::trunc 对整型无意义"); return detail::unary(m, xhqm::tensor_base::unary_trunc, [](const type& v) { return static_cast<type>(::std::trunc(v)); }); }
		template<class type> matrix<type> fix(const matrix<type>& m) { return trunc(m); }  // MATLAB 别名，指向 trunc

		// 取模 mod(x,y)（结果符号同 y，MATLAB 语义）
		template<class type>
		matrix<type> mod(const matrix<type>& a, const type& y)
		{
			static_assert(!std::is_integral_v<type>, "analysis::mod 对整型无意义");
			return detail::map(a, [&](const type& v) { return static_cast<type>(v - ::std::floor(v / y) * y); });
		}
		// 余数 rem(x,y)（结果符号同 x，即 C++ fmod）
		template<class type>
		matrix<type> rem(const matrix<type>& a, const type& y)
		{
			static_assert(!std::is_integral_v<type>, "analysis::rem 对整型无意义");
			return detail::map(a, [&](const type& v) { return static_cast<type>(::std::fmod(v, y)); });
		}

		// —— 绝对值 / 符号（unary kernel）——
		template<class type> matrix<type> abs(const matrix<type>& m) { return detail::unary(m, xhqm::tensor_base::unary_abs, [](const type& v) { return xhqm::abs(v); }); }
		template<class type> matrix<type> sign(const matrix<type>& m) { return detail::unary(m, xhqm::tensor_base::unary_sign, [](const type& v) { return type((v > type(0)) ? 1 : ((v < type(0)) ? -1 : 0)); }); }

		// —— vector 重载（unary kernel）——
		template<class type> vector<type> sin(const vector<type>& v) { return detail::unaryv(v, xhqm::tensor_base::unary_sin, [](const type& x) { return xhqm::sin(x); }); }
		template<class type> vector<type> cos(const vector<type>& v) { return detail::unaryv(v, xhqm::tensor_base::unary_cos, [](const type& x) { return xhqm::cos(x); }); }
		template<class type> vector<type> exp(const vector<type>& v) { return detail::unaryv(v, xhqm::tensor_base::unary_exp, [](const type& x) { return ::std::exp(x); }); }
		template<class type> vector<type> log(const vector<type>& v) { return detail::unaryv(v, xhqm::tensor_base::unary_log, [](const type& x) { return ::std::log(x); }); }
		template<class type> vector<type> sqrt(const vector<type>& v) { return detail::unaryv(v, xhqm::tensor_base::unary_sqrt, [](const type& x) { return xhqm::sqrt(x); }); }
		template<class type> vector<type> abs(const vector<type>& v) { return detail::unaryv(v, xhqm::tensor_base::unary_abs, [](const type& x) { return xhqm::abs(x); }); }

		// —— 反双曲 / 补充数学（unary kernel，整型无意义拦截）——
		template<class type> matrix<type> asinh(const matrix<type>& m) { static_assert(!std::is_integral_v<type>, "analysis::asinh 对整型无意义"); return detail::unary(m, xhqm::tensor_base::unary_asinh, [](const type& v) { return static_cast<type>(::std::asinh(v)); }); }
		template<class type> matrix<type> acosh(const matrix<type>& m) { static_assert(!std::is_integral_v<type>, "analysis::acosh 对整型无意义"); return detail::unary(m, xhqm::tensor_base::unary_acosh, [](const type& v) { return static_cast<type>(::std::acosh(v)); }); }
		template<class type> matrix<type> atanh(const matrix<type>& m) { static_assert(!std::is_integral_v<type>, "analysis::atanh 对整型无意义"); return detail::unary(m, xhqm::tensor_base::unary_atanh, [](const type& v) { return static_cast<type>(::std::atanh(v)); }); }
		template<class type> matrix<type> expm1(const matrix<type>& m) { static_assert(!std::is_integral_v<type>, "analysis::expm1 对整型无意义"); return detail::unary(m, xhqm::tensor_base::unary_expm1, [](const type& v) { return static_cast<type>(::std::expm1(v)); }); }
		template<class type> matrix<type> log1p(const matrix<type>& m) { static_assert(!std::is_integral_v<type>, "analysis::log1p 对整型无意义"); return detail::unary(m, xhqm::tensor_base::unary_log1p, [](const type& v) { return static_cast<type>(::std::log1p(v)); }); }

		// 二元：atan2(y, x)（结果象限由 y、x 符号决定）
		template<class type>
		matrix<type> atan2(const matrix<type>& y, const matrix<type>& x)
		{
			return detail::elemwise_binary(y, x, xhqm::tensor_base::elem_atan2,
				[](const type& yy, const type& xx) { return static_cast<type>(::std::atan2(yy, xx)); });
		}
		// 二元：hypot(a, b) = sqrt(a² + b²)
		template<class type>
		matrix<type> hypot(const matrix<type>& a, const matrix<type>& b)
		{
			return detail::elemwise_binary(a, b, xhqm::tensor_base::elem_hypot,
				[](const type& x, const type& y) { return static_cast<type>(::std::hypot(x, y)); });
		}

		// —— 组合函数：unary + elemwise/标量乘，不占新算子 ——
		template<class type> matrix<type> cot(const matrix<type>& m) { return detail::reciprocal(tan(m)); }
		template<class type> matrix<type> sec(const matrix<type>& m) { return detail::reciprocal(cos(m)); }
		template<class type> matrix<type> csc(const matrix<type>& m) { return detail::reciprocal(sin(m)); }
		template<class type> matrix<type> acot(const matrix<type>& m) { return atan(detail::reciprocal(m)); }
		template<class type> matrix<type> asec(const matrix<type>& m) { return acos(detail::reciprocal(m)); }
		template<class type> matrix<type> acsc(const matrix<type>& m) { return asin(detail::reciprocal(m)); }
		template<class type> matrix<type> coth(const matrix<type>& m) { return detail::reciprocal(tanh(m)); }
		template<class type> matrix<type> sech(const matrix<type>& m) { return detail::reciprocal(cosh(m)); }
		template<class type> matrix<type> csch(const matrix<type>& m) { return detail::reciprocal(sinh(m)); }

		// —— 误差函数（unary kernel，整型拦截）——
		template<class type> matrix<type> erf(const matrix<type>& m) { static_assert(!std::is_integral_v<type>, "analysis::erf 对整型无意义"); return detail::unary(m, xhqm::tensor_base::unary_erf, [](const type& v) { return static_cast<type>(::std::erf(v)); }); }
		template<class type> matrix<type> erfc(const matrix<type>& m) { static_assert(!std::is_integral_v<type>, "analysis::erfc 对整型无意义"); return detail::unary(m, xhqm::tensor_base::unary_erfc, [](const type& v) { return static_cast<type>(::std::erfc(v)); }); }

		// —— Gamma 函数（unary kernel，整型拦截）——
		template<class type> matrix<type> gamma(const matrix<type>& m) { static_assert(!std::is_integral_v<type>, "analysis::gamma 对整型无意义"); return detail::unary(m, xhqm::tensor_base::unary_tgamma, [](const type& v) { return static_cast<type>(::std::tgamma(v)); }); }
		template<class type> matrix<type> gammaln(const matrix<type>& m) { static_assert(!std::is_integral_v<type>, "analysis::gammaln 对整型无意义"); return detail::unary(m, xhqm::tensor_base::unary_lgamma, [](const type& v) { return static_cast<type>(::std::lgamma(v)); }); }

		// —— 幂根 / 平方（unary kernel，整型拦截）——
		template<class type> matrix<type> square(const matrix<type>& m) { return detail::unary(m, xhqm::tensor_base::unary_square, [](const type& v) { return xhqm::mul(v, v); }); }
		template<class type> matrix<type> cbrt(const matrix<type>& m) { static_assert(!std::is_integral_v<type>, "analysis::cbrt 对整型无意义"); return detail::unary(m, xhqm::tensor_base::unary_cbrt, [](const type& v) { return static_cast<type>(::std::cbrt(v)); }); }
		template<class type> matrix<type> rsqrt(const matrix<type>& m) { static_assert(!std::is_integral_v<type>, "analysis::rsqrt 对整型无意义"); return detail::unary(m, xhqm::tensor_base::unary_rsqrt, [](const type& v) { return static_cast<type>(xhqm::div(type(1), ::std::sqrt(v))); }); }
		template<class type> matrix<type> exp2(const matrix<type>& m) { static_assert(!std::is_integral_v<type>, "analysis::exp2 对整型无意义"); return detail::unary(m, xhqm::tensor_base::unary_exp2, [](const type& v) { return static_cast<type>(::std::exp2(v)); }); }

		// —— 角度转换（标量乘）——
		template<class type> matrix<type> deg2rad(const matrix<type>& m) { return m * type(3.14159265358979323846 / 180.0); }
		template<class type> matrix<type> rad2deg(const matrix<type>& m) { return m * type(180.0 / 3.14159265358979323846); }

		// —— 元素判断（unary kernel，返回 0/1 再转 bool 矩阵）——
		template<class type>
		matrix<bool> isnan(const matrix<type>& m)
		{
			return detail::unary_bool(m, xhqm::tensor_base::unary_isnan, [](const type& v) { return type(std::isnan(v) ? 1 : 0); });
		}
		template<class type>
		matrix<bool> isinf(const matrix<type>& m)
		{
			return detail::unary_bool(m, xhqm::tensor_base::unary_isinf, [](const type& v) { return type(std::isinf(v) ? 1 : 0); });
		}
		template<class type>
		matrix<bool> isfinite(const matrix<type>& m)
		{
			return detail::unary_bool(m, xhqm::tensor_base::unary_isfinite, [](const type& v) { return type(std::isfinite(v) ? 1 : 0); });
		}

		// —— 元素判断（vector 版，unary kernel + bool 向量）——
		template<class type>
		vector<bool> isnan(const vector<type>& v)
		{
			return detail::unaryv_bool(v, xhqm::tensor_base::unary_isnan, [](const type& x) { return type(std::isnan(x) ? 1 : 0); });
		}
		template<class type>
		vector<bool> isinf(const vector<type>& v)
		{
			return detail::unaryv_bool(v, xhqm::tensor_base::unary_isinf, [](const type& x) { return type(std::isinf(x) ? 1 : 0); });
		}
		template<class type>
		vector<bool> isfinite(const vector<type>& v)
		{
			return detail::unaryv_bool(v, xhqm::tensor_base::unary_isfinite, [](const type& x) { return type(std::isfinite(x) ? 1 : 0); });
		}
	}
}

#endif
