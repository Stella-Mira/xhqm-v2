#ifndef XHQM_TENSOR_SIGNAL
#define XHQM_TENSOR_SIGNAL

// 信号处理算子（串行桩）：fft/ifft、fftshift、插值 interp1、积分 trapz/cumtrapz、
// 差分方程滤波 filter、full 卷积 conv1d_full/conv2d_full、互相关 xcorr。
#include "tensor_base.h"
#include "complex.hpp"
#include <array>
#include <cmath>

namespace xhqm {
	namespace tensor_base {

		// 判断 n 是否为 2 的幂
		inline bool is_pow2(xhqm::size n) { return n != 0 && (n & (n - 1)) == 0; }

		// —— FFT（radix-2，长度须 2 的幂，否则返回空）——
		template<class type>
		xhqm::tensor<1, xhqm::complex<type>> fft(const xhqm::tensor<1, xhqm::complex<type>>& x)
		{
			const xhqm::size n = x.dimension(0);
			if (!is_pow2(n)) return xhqm::tensor<1, xhqm::complex<type>>();
			xhqm::size bits = 0;
			for (xhqm::size t = n; t > 1; t >>= 1) ++bits;
			const type pi = type(3.14159265358979323846);

			xhqm::tensor<1, xhqm::complex<type>> a(std::array<xhqm::size, 1>{ n }, xhqm::complex<type>(type(0), type(0)));
			for (xhqm::size i = 0; i < n; ++i)  // 位反转重排
			{
				xhqm::size j = 0;
				for (xhqm::size b = 0; b < bits; ++b)
					if (i & (xhqm::size(1) << b)) j |= (xhqm::size(1) << (bits - 1 - b));
				a[j] = x[i];
			}
			for (xhqm::size len = 2; len <= n; len <<= 1)  // 蝶形
			{
				const xhqm::complex<type> w(::std::cos(-type(2) * pi / type(len)), ::std::sin(-type(2) * pi / type(len)));
				for (xhqm::size i = 0; i < n; i += len)
				{
					xhqm::complex<type> wk(type(1), type(0));
					for (xhqm::size j = 0; j < len / 2; ++j)
					{
						const xhqm::complex<type> u = a[i + j];
						const xhqm::complex<type> v = a[i + j + len / 2] * wk;
						a[i + j] = u + v;
						a[i + j + len / 2] = u - v;
						wk = wk * w;
					}
				}
			}
			return a;
		}

		// —— IFFT（radix-2，最后归一化 /n）——
		template<class type>
		xhqm::tensor<1, xhqm::complex<type>> ifft(const xhqm::tensor<1, xhqm::complex<type>>& x)
		{
			const xhqm::size n = x.dimension(0);
			if (!is_pow2(n)) return xhqm::tensor<1, xhqm::complex<type>>();
			xhqm::size bits = 0;
			for (xhqm::size t = n; t > 1; t >>= 1) ++bits;
			const type pi = type(3.14159265358979323846);

			xhqm::tensor<1, xhqm::complex<type>> a(std::array<xhqm::size, 1>{ n }, xhqm::complex<type>(type(0), type(0)));
			for (xhqm::size i = 0; i < n; ++i)
			{
				xhqm::size j = 0;
				for (xhqm::size b = 0; b < bits; ++b)
					if (i & (xhqm::size(1) << b)) j |= (xhqm::size(1) << (bits - 1 - b));
				a[j] = x[i];
			}
			for (xhqm::size len = 2; len <= n; len <<= 1)
			{
				const xhqm::complex<type> w(::std::cos(type(2) * pi / type(len)), ::std::sin(type(2) * pi / type(len)));
				for (xhqm::size i = 0; i < n; i += len)
				{
					xhqm::complex<type> wk(type(1), type(0));
					for (xhqm::size j = 0; j < len / 2; ++j)
					{
						const xhqm::complex<type> u = a[i + j];
						const xhqm::complex<type> v = a[i + j + len / 2] * wk;
						a[i + j] = u + v;
						a[i + j + len / 2] = u - v;
						wk = wk * w;
					}
				}
			}
			for (xhqm::size i = 0; i < n; ++i) a[i] = a[i] / type(n);
			return a;
		}

		// —— FFT 移位（零频移到中心）——
		template<class type>
		xhqm::tensor<1, xhqm::complex<type>> fftshift(const xhqm::tensor<1, xhqm::complex<type>>& x)
		{
			const xhqm::size n = x.dimension(0);
			xhqm::tensor<1, xhqm::complex<type>> r(std::array<xhqm::size, 1>{ n }, xhqm::complex<type>(type(0), type(0)));
			if (n == 0) return r;
			const xhqm::size shift = n / 2;
			for (xhqm::size i = 0; i < n; ++i) r[i] = x[(i + shift) % n];
			return r;
		}
		template<class type>
		xhqm::tensor<1, xhqm::complex<type>> ifftshift(const xhqm::tensor<1, xhqm::complex<type>>& x)
		{
			const xhqm::size n = x.dimension(0);
			xhqm::tensor<1, xhqm::complex<type>> r(std::array<xhqm::size, 1>{ n }, xhqm::complex<type>(type(0), type(0)));
			if (n == 0) return r;
			const xhqm::size shift = (n + 1) / 2;
			for (xhqm::size i = 0; i < n; ++i) r[i] = x[(i + shift) % n];
			return r;
		}

		// —— 一维线性插值：x 升序采样点，y 采样值，xi 查询点 ——
		template<class type>
		xhqm::tensor<1, type> interp1(const xhqm::tensor<1, type>& x, const xhqm::tensor<1, type>& y, const xhqm::tensor<1, type>& xi)
		{
			const xhqm::size nx = x.dimension(0), ni = xi.dimension(0);
			xhqm::tensor<1, type> yi(std::array<xhqm::size, 1>{ ni }, type(0));
			if (nx == 0 || nx != y.dimension(0)) return yi;
			for (xhqm::size k = 0; k < ni; ++k)
			{
				const type xq = xi[k];
				if (xq <= x[0]) { yi[k] = y[0]; continue; }
				if (xq >= x[nx - 1]) { yi[k] = y[nx - 1]; continue; }
				xhqm::size i = 0;
				while (i + 1 < nx && x[i + 1] < xq) ++i;
				if (x[i + 1] == x[i]) { yi[k] = y[i]; continue; }	// 重复采样点防除零
				const type t = xhqm::div(xhqm::sub(xq, x[i]), xhqm::sub(x[i + 1], x[i]));
				yi[k] = xhqm::sum(y[i], xhqm::mul(t, xhqm::sub(y[i + 1], y[i])));
			}
			return yi;
		}

		// —— 梯形数值积分（默认 dx=1）——
		template<class type>
		type trapz(const xhqm::tensor<1, type>& y)
		{
			if (y.dimension(0) < 2) return type(0);
			type s = type(0);
			for (xhqm::size i = 1; i < y.dimension(0); ++i)
				s = xhqm::sum(s, xhqm::div(xhqm::sum(y[i], y[i - 1]), type(2)));
			return s;
		}
		template<class type>
		type trapz(const xhqm::tensor<1, type>& y, const xhqm::tensor<1, type>& x)
		{
			if (y.dimension(0) < 2 || x.dimension(0) != y.dimension(0)) return type(0);
			type s = type(0);
			for (xhqm::size i = 1; i < y.dimension(0); ++i)
				s = xhqm::sum(s, xhqm::div(xhqm::mul(xhqm::sub(x[i], x[i - 1]), xhqm::sum(y[i], y[i - 1])), type(2)));
			return s;
		}
		template<class type>
		xhqm::tensor<1, type> cumtrapz(const xhqm::tensor<1, type>& y, const xhqm::tensor<1, type>& x)
		{
			const xhqm::size n = y.dimension(0);
			xhqm::tensor<1, type> r(std::array<xhqm::size, 1>{ n }, type(0));
			if (x.dimension(0) != n) return r;
			for (xhqm::size i = 1; i < n; ++i)
				r[i] = xhqm::sum(r[i - 1], xhqm::div(xhqm::mul(xhqm::sub(x[i], x[i - 1]), xhqm::sum(y[i], y[i - 1])), type(2)));
			return r;
		}

		// —— 一维差分方程滤波：a[0]·y[n] = Σ b_k·x[n-k] - Σ_{k≥1} a_k·y[n-k] ——
		template<class type>
		xhqm::tensor<1, type> filter(const xhqm::tensor<1, type>& b, const xhqm::tensor<1, type>& a, const xhqm::tensor<1, type>& x)
		{
			const xhqm::size nx = x.dimension(0);
			xhqm::tensor<1, type> y(std::array<xhqm::size, 1>{ nx }, type(0));
			if (a.dimension(0) == 0 || a[0] == type(0)) return y;
			for (xhqm::size n = 0; n < nx; ++n)
			{
				type acc = type(0);
				for (xhqm::size k = 0; k < b.dimension(0); ++k)
					if (n >= k) acc = xhqm::sum(acc, xhqm::mul(b[k], x[n - k]));
				for (xhqm::size k = 1; k < a.dimension(0); ++k)
					if (n >= k) acc = xhqm::sub(acc, xhqm::mul(a[k], y[n - k]));
				y[n] = xhqm::div(acc, a[0]);
			}
			return y;
		}

		// —— 一维 full 卷积（多项式乘法）——
		template<class type>
		xhqm::tensor<1, type> conv1d_full(const xhqm::tensor<1, type>& a, const xhqm::tensor<1, type>& b)
		{
			const xhqm::size na = a.dimension(0), nb = b.dimension(0);
			xhqm::tensor<1, type> r(std::array<xhqm::size, 1>{ na + nb - 1 }, type(0));
			if (na > 0 && nb > 0)
			{
				// 翻转核（卷积 = 相关翻转核），包装成 (cout=1,cin=1,kd=1,kh=1,kw=nb)
				xhqm::tensor<5, type> w(std::array<xhqm::size, 5>{ 1, 1, 1, 1, nb }, type(0));
				for (xhqm::size i = 0; i < nb; ++i) w(0, 0, 0, 0, i) = b[nb - 1 - i];
				if (tensor_backend_active().conv3d(r.data(), a.data(), w.data(), nullptr,
					1, 1, 1, 1, 1, na, 1, 1, nb,
					1, 1, 1, 0, 0, nb - 1, 1, 1, 1, 1, dtype_of<type>::value))
					return r;
			}
			for (xhqm::size i = 0; i < na; ++i)
				for (xhqm::size j = 0; j < nb; ++j)
					r[i + j] = xhqm::sum(r[i + j], xhqm::mul(a[i], b[j]));
			return r;
		}

		// —— 互相关（full）：r[k] = Σ a[i]·b[i-k] ——
		template<class type>
		xhqm::tensor<1, type> xcorr(const xhqm::tensor<1, type>& a, const xhqm::tensor<1, type>& b)
		{
			const xhqm::size na = a.dimension(0), nb = b.dimension(0);
			xhqm::tensor<1, type> r(std::array<xhqm::size, 1>{ na + nb - 1 }, type(0));
			if (na > 0 && nb > 0)
			{
				// 不翻核（xcorr 本来就是互相关）
				xhqm::tensor<5, type> w(std::array<xhqm::size, 5>{ 1, 1, 1, 1, nb }, type(0));
				for (xhqm::size i = 0; i < nb; ++i) w(0, 0, 0, 0, i) = b[i];
				if (tensor_backend_active().conv3d(r.data(), a.data(), w.data(), nullptr,
					1, 1, 1, 1, 1, na, 1, 1, nb,
					1, 1, 1, 0, 0, nb - 1, 1, 1, 1, 1, dtype_of<type>::value))
					return r;
			}
			for (xhqm::size i = 0; i < na; ++i)
				for (xhqm::size j = 0; j < nb; ++j)
					r[i + nb - 1 - j] = xhqm::sum(r[i + nb - 1 - j], xhqm::mul(a[i], b[j]));
			return r;
		}

		// —— 二维 full 卷积 ——
		template<class type>
		xhqm::tensor<2, type> conv2d_full(const xhqm::tensor<2, type>& a, const xhqm::tensor<2, type>& b)
		{
			const xhqm::size ma = a.dimension(0), na = a.dimension(1);
			const xhqm::size mb = b.dimension(0), nb = b.dimension(1);
			xhqm::tensor<2, type> r(std::array<xhqm::size, 2>{ ma + mb - 1, na + nb - 1 }, type(0));
			if (ma > 0 && na > 0 && mb > 0 && nb > 0)
			{
				// 二维翻转核，包装成 (cout=1,cin=1,kd=1,kh=mb,kw=nb)
				xhqm::tensor<5, type> w(std::array<xhqm::size, 5>{ 1, 1, 1, mb, nb }, type(0));
				for (xhqm::size p = 0; p < mb; ++p)
					for (xhqm::size q = 0; q < nb; ++q)
						w(0, 0, 0, p, q) = b(mb - 1 - p, nb - 1 - q);
				if (tensor_backend_active().conv3d(r.data(), a.data(), w.data(), nullptr,
					1, 1, 1, 1, ma, na, 1, mb, nb,
					1, 1, 1, 0, mb - 1, nb - 1, 1, 1, 1, 1, dtype_of<type>::value))
					return r;
			}
			for (xhqm::size i = 0; i < ma; ++i)
				for (xhqm::size j = 0; j < na; ++j)
					for (xhqm::size p = 0; p < mb; ++p)
						for (xhqm::size q = 0; q < nb; ++q)
							r(i + p, j + q) = xhqm::sum(r(i + p, j + q), xhqm::mul(a(i, j), b(p, q)));
			return r;
		}

	}
}

#endif
