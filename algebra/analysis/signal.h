#ifndef XHQM_ANALYSIS_SIGNAL
#define XHQM_ANALYSIS_SIGNAL

// 信号处理（MATLAB 风格）：interp1（线性插值）、fft / ifft（radix-2）、trapz、filter、conv2、xcorr。
// 全部薄封装 tensor_base 的 signal 算子。
#include "../tensor/vector.h"
#include "../tensor/matrix.h"
#include "../tensor/complex.hpp"
#include "../tensor/signal.h"
#include "convert.h"

namespace xhqm
{
	namespace analysis
	{
		// 一维线性插值：x 升序采样点，y 采样值，xi 查询点
		template<class type>
		vector<type> interp1(const vector<type>& x, const vector<type>& y, const vector<type>& xi)
		{
			return detail::as_vec(xhqm::tensor_base::interp1(detail::as_t1(x), detail::as_t1(y), detail::as_t1(xi)));
		}

		// 快速傅里叶变换 / 逆变换（radix-2，长度须 2 的幂，否则返回空）
		template<class type>
		vector<complex<type>> fft(const vector<complex<type>>& x)
		{
			return detail::as_vec(xhqm::tensor_base::fft(detail::as_t1(x)));
		}
		template<class type>
		vector<complex<type>> ifft(const vector<complex<type>>& x)
		{
			return detail::as_vec(xhqm::tensor_base::ifft(detail::as_t1(x)));
		}
		template<class type>
		vector<complex<type>> fftshift(const vector<complex<type>>& x)
		{
			return detail::as_vec(xhqm::tensor_base::fftshift(detail::as_t1(x)));
		}
		template<class type>
		vector<complex<type>> ifftshift(const vector<complex<type>>& x)
		{
			return detail::as_vec(xhqm::tensor_base::ifftshift(detail::as_t1(x)));
		}

		// —— 梯形数值积分（默认 dx=1）——
		template<class type>
		type trapz(const vector<type>& y, const vector<type>& x)
		{
			return xhqm::tensor_base::trapz(detail::as_t1(y), detail::as_t1(x));
		}
		template<class type>
		type trapz(const vector<type>& y)
		{
			return xhqm::tensor_base::trapz(detail::as_t1(y));
		}
		template<class type>
		vector<type> cumtrapz(const vector<type>& y, const vector<type>& x)
		{
			return detail::as_vec(xhqm::tensor_base::cumtrapz(detail::as_t1(y), detail::as_t1(x)));
		}

		// —— 一维差分方程滤波 ——
		template<class type>
		vector<type> filter(const vector<type>& b, const vector<type>& a, const vector<type>& x)
		{
			return detail::as_vec(xhqm::tensor_base::filter(detail::as_t1(b), detail::as_t1(a), detail::as_t1(x)));
		}

		// —— 二维卷积（full）——
		template<class type>
		matrix<type> conv2(const matrix<type>& a, const matrix<type>& b)
		{
			return detail::as_mat(xhqm::tensor_base::conv2d_full(detail::as_t2(a), detail::as_t2(b)));
		}

		// —— 互相关（full）——
		template<class type>
		vector<type> xcorr(const vector<type>& a, const vector<type>& b)
		{
			return detail::as_vec(xhqm::tensor_base::xcorr(detail::as_t1(a), detail::as_t1(b)));
		}
	}
}

#endif
