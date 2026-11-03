#ifndef XHQM_ANALYSIS_POLY
#define XHQM_ANALYSIS_POLY

// 多项式（MATLAB 风格）：polyval / conv / deconv / polyder / polyint / poly / polyfit。
// 系数向量一律降幂排列：[a_n, a_{n-1}, ..., a_0]。
// 基础算子走 tensor_base 桩，poly / polyfit 为组合。
#include "../tensor/vector.h"
#include "../tensor/matrix.h"
#include "../tensor/poly.h"
#include "../tensor/signal.h"
#include "linalg.h"
#include "convert.h"

namespace xhqm
{
	namespace analysis
	{
		// 多项式求值（Horner，走 polyval 桩）
		template<class type>
		type polyval(const vector<type>& p, const type& x)
		{
			return xhqm::tensor_base::polyval(detail::as_t1(p), x);
		}
		template<class type>
		vector<type> polyval(const vector<type>& p, const vector<type>& x)
		{
			return detail::as_vec(xhqm::tensor_base::polyval(detail::as_t1(p), detail::as_t1(x)));
		}

		// 卷积（多项式乘法，走 conv1d_full 桩）
		template<class type>
		vector<type> conv(const vector<type>& a, const vector<type>& b)
		{
			return detail::as_vec(xhqm::tensor_base::conv1d_full(detail::as_t1(a), detail::as_t1(b)));
		}

		// 反卷积（多项式长除法，走 deconv 桩）
		template<class type>
		struct deconv_result { vector<type> q; vector<type> r; };
		template<class type>
		deconv_result<type> deconv(const vector<type>& a, const vector<type>& b)
		{
			auto d = xhqm::tensor_base::deconv(detail::as_t1(a), detail::as_t1(b));
			deconv_result<type> res;
			res.q = detail::as_vec(d.q);
			res.r = detail::as_vec(d.r);
			return res;
		}

		// 多项式求导（走 polyder 桩）
		template<class type>
		vector<type> polyder(const vector<type>& p)
		{
			return detail::as_vec(xhqm::tensor_base::polyder(detail::as_t1(p)));
		}

		// 多项式积分（常数项取 0，走 polyint 桩）
		template<class type>
		vector<type> polyint(const vector<type>& p)
		{
			return detail::as_vec(xhqm::tensor_base::polyint(detail::as_t1(p)));
		}

		// 由根构造多项式（首一）：p(x) = Π(x - r_i)（基于 conv 组合）
		template<class type>
		vector<type> poly(const vector<type>& roots)
		{
			vector<type> p(1, type(1));
			for (xhqm::size i = 0; i < roots.size(); ++i)
			{
				vector<type> factor(2);
				factor[0] = type(1);
				factor[1] = xhqm::sub(type(0), roots[i]);
				p = conv(p, factor);
			}
			return p;
		}

		// 最小二乘多项式拟合：Vandermonde + 正规方程（经 mldivide，组合）
		template<class type>
		vector<type> polyfit(const vector<type>& x, const vector<type>& y, const xhqm::size& deg)
		{
			const xhqm::size n = x.size();
			if (y.size() != n || deg + 1 > n) return vector<type>();
			matrix<type> V(n, deg + 1);
			for (xhqm::size i = 0; i < n; ++i)
				for (xhqm::size j = 0; j <= deg; ++j)
					V(i, deg - j) = ::std::pow(x[i], type(j));
			matrix<type> Vt = V.transpose();
			matrix<type> A = Vt * V;
			matrix<type> ymat(n, 1);
			for (xhqm::size i = 0; i < n; ++i) ymat(i, 0) = y[i];
			matrix<type> b = Vt * ymat;
			matrix<type> c = mldivide(A, b);
			vector<type> coeff(deg + 1);
			for (xhqm::size i = 0; i <= deg; ++i) coeff[i] = c(i, 0);
			return coeff;
		}
	}
}

#endif
