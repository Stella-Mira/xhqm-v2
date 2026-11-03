#ifndef XHQM_TENSOR_POLY
#define XHQM_TENSOR_POLY

// 多项式算子（串行桩）：polyval / deconv / polyder / polyint。
// 系数向量一律降幂排列：[a_n, a_{n-1}, ..., a_0]。
#include "tensor_base.h"
#include <array>

namespace xhqm {
	namespace tensor_base {

		// —— 多项式求值（Horner）：p 降幂系数 ——
		template<class type>
		type polyval(const xhqm::tensor<1, type>& p, const type& x)
		{
			type r = type(0);
			for (xhqm::size i = 0; i < p.dimension(0); ++i)
				r = xhqm::sum(xhqm::mul(r, x), p[i]);
			return r;
		}
		template<class type>
		xhqm::tensor<1, type> polyval(const xhqm::tensor<1, type>& p, const xhqm::tensor<1, type>& x)
		{
			xhqm::tensor<1, type> r(std::array<xhqm::size, 1>{ x.dimension(0) }, type(0));
			for (xhqm::size i = 0; i < x.dimension(0); ++i) r[i] = polyval(p, x[i]);
			return r;
		}

		// —— 反卷积（多项式长除法）：a / b = q 余 r ——
		template<class type>
		struct deconv_result { xhqm::tensor<1, type> q; xhqm::tensor<1, type> r; };
		template<class type>
		deconv_result<type> deconv(const xhqm::tensor<1, type>& a, const xhqm::tensor<1, type>& b)
		{
			deconv_result<type> res;
			const xhqm::size na = a.dimension(0), nb = b.dimension(0);
			if (nb == 0 || na < nb)
			{
				res.q = xhqm::tensor<1, type>(std::array<xhqm::size, 1>{ 1 }, type(0));
				res.r = a;
				return res;
			}
			if (b[0] == type(0))
			{
				res.q = xhqm::tensor<1, type>(std::array<xhqm::size, 1>{ 1 }, type(0));
				res.r = a;
				return res;
			}
			const xhqm::size nq = na - nb + 1;
			xhqm::tensor<1, type> q(std::array<xhqm::size, 1>{ nq }, type(0));
			xhqm::tensor<1, type> rem = a;
			for (xhqm::size i = 0; i < nq; ++i)
			{
				const type coef = xhqm::div(rem[i], b[0]);
				q[i] = coef;
				for (xhqm::size j = 0; j < nb; ++j)
					rem[i + j] = xhqm::sub(rem[i + j], xhqm::mul(coef, b[j]));
			}
			res.q = q;
			const xhqm::size nr = nb - 1;
			res.r = xhqm::tensor<1, type>(std::array<xhqm::size, 1>{ nr }, type(0));
			for (xhqm::size k = 0; k < nr; ++k) res.r[k] = rem[nq + k];
			return res;
		}

		// —— 多项式求导：p = [a_n..a_0] → [n·a_n .. 1·a_1] ——
		template<class type>
		xhqm::tensor<1, type> polyder(const xhqm::tensor<1, type>& p)
		{
			const xhqm::size n = p.dimension(0);
			if (n <= 1) return xhqm::tensor<1, type>(std::array<xhqm::size, 1>{ 1 }, type(0));
			xhqm::tensor<1, type> r(std::array<xhqm::size, 1>{ n - 1 }, type(0));
			for (xhqm::size i = 0; i < n - 1; ++i)
				r[i] = xhqm::mul(p[i], type(n - 1 - i));
			return r;
		}

		// —— 多项式积分（常数项取 0）——
		template<class type>
		xhqm::tensor<1, type> polyint(const xhqm::tensor<1, type>& p)
		{
			const xhqm::size n = p.dimension(0);
			xhqm::tensor<1, type> r(std::array<xhqm::size, 1>{ n + 1 }, type(0));
			for (xhqm::size i = 0; i < n; ++i)
				r[i] = xhqm::div(p[i], type(n - i));
			return r;
		}

	}
}

#endif
