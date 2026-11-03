#ifndef XHQM_ANALYSIS_CONSTRUCT
#define XHQM_ANALYSIS_CONSTRUCT

// 数组构造（MATLAB 风格）：zeros / ones / eye / diag / linspace / logspace。
#include "../tensor/matrix.h"
#include "../tensor/vector.h"
#include <random>

namespace xhqm
{
	namespace analysis
	{
		// 全 0 矩阵 m×n
		template<class type>
		matrix<type> zeros(const xhqm::size& m, const xhqm::size& n)
		{
			return matrix<type>(m, n, type(0));
		}
		// 全 0 方阵 n×n
		template<class type>
		matrix<type> zeros(const xhqm::size& n)
		{
			return zeros<type>(n, n);
		}

		// 全 1 矩阵 m×n
		template<class type>
		matrix<type> ones(const xhqm::size& m, const xhqm::size& n)
		{
			return matrix<type>(m, n, type(1));
		}
		// 全 1 方阵 n×n
		template<class type>
		matrix<type> ones(const xhqm::size& n)
		{
			return ones<type>(n, n);
		}

		// 单位阵 n×n
		template<class type>
		matrix<type> eye(const xhqm::size& n)
		{
			matrix<type> r(n, n, type(0));
			for (xhqm::size i = 0; i < n; ++i)
				r(i, i) = type(1);
			return r;
		}

		// 由向量构造对角方阵
		template<class type>
		matrix<type> diag(const vector<type>& v)
		{
			const xhqm::size n = v.size();
			matrix<type> r(n, n, type(0));
			for (xhqm::size i = 0; i < n; ++i)
				r(i, i) = v[i];
			return r;
		}
		// 提取矩阵主对角线为向量
		template<class type>
		vector<type> diag(const matrix<type>& m)
		{
			const xhqm::size n = (m.rows() < m.cols()) ? m.rows() : m.cols();
			vector<type> r(n);
			for (xhqm::size i = 0; i < n; ++i)
				r[i] = m(i, i);
			return r;
		}

		// 等距向量：linspace(a, b, n)，默认 n=100
		template<class type>
		vector<type> linspace(const type& a, const type& b, const xhqm::size& n = 100)
		{
			vector<type> r(n);
			if (n == 1) { r[0] = b; return r; }
			const type step = xhqm::div(xhqm::sub(b, a), type(n - 1));
			for (xhqm::size i = 0; i < n; ++i)
				r[i] = xhqm::sum(a, xhqm::mul(step, type(i)));
			return r;
		}

		// 对数等距向量：logspace(a, b, n) = 10^linspace(a, b, n)
		template<class type>
		vector<type> logspace(const type& a, const type& b, const xhqm::size& n = 50)
		{
			vector<type> r(n);
			const type step = xhqm::div(xhqm::sub(b, a), type(n - 1));
			for (xhqm::size i = 0; i < n; ++i)
				r[i] = xhqm::pow(type(10), xhqm::sum(a, xhqm::mul(step, type(i))));
			return r;
		}

		// 随机矩阵 [0,1)（固定种子，序列可复现）
		template<class type>
		matrix<type> rand(const xhqm::size& m, const xhqm::size& n)
		{
			static std::mt19937 gen(5489u);
			std::uniform_real_distribution<double> dist(0.0, 1.0);
			matrix<type> r(m, n);
			for (xhqm::size i = 0; i < m * n; ++i) r.data()[i] = type(dist(gen));
			return r;
		}
		template<class type>
		matrix<type> rand(const xhqm::size& n) { return rand<type>(n, n); }

		// 标准正态随机矩阵（固定种子）
		template<class type>
		matrix<type> randn(const xhqm::size& m, const xhqm::size& n)
		{
			static std::mt19937 gen(5489u);
			std::normal_distribution<double> dist(0.0, 1.0);
			matrix<type> r(m, n);
			for (xhqm::size i = 0; i < m * n; ++i) r.data()[i] = type(dist(gen));
			return r;
		}
		template<class type>
		matrix<type> randn(const xhqm::size& n) { return randn<type>(n, n); }

		// 块对角 [a 0; 0 b]
		template<class type>
		matrix<type> blkdiag(const matrix<type>& a, const matrix<type>& b)
		{
			matrix<type> r(a.rows() + b.rows(), a.cols() + b.cols(), type(0));
			for (xhqm::size i = 0; i < a.rows(); ++i)
				for (xhqm::size j = 0; j < a.cols(); ++j)
					r(i, j) = a(i, j);
			for (xhqm::size i = 0; i < b.rows(); ++i)
				for (xhqm::size j = 0; j < b.cols(); ++j)
					r(a.rows() + i, a.cols() + j) = b(i, j);
			return r;
		}

		// —— 网格生成：[X, Y] = meshgrid(x, y)，X 每行重复 x，Y 每列重复 y ——
		template<class type>
		struct meshgrid_result { matrix<type> X; matrix<type> Y; };
		template<class type>
		meshgrid_result<type> meshgrid(const vector<type>& x, const vector<type>& y)
		{
			meshgrid_result<type> r;
			r.X = matrix<type>(y.size(), x.size());
			r.Y = matrix<type>(y.size(), x.size());
			for (xhqm::size i = 0; i < y.size(); ++i)
				for (xhqm::size j = 0; j < x.size(); ++j)
				{
					r.X(i, j) = x[j];
					r.Y(i, j) = y[i];
				}
			return r;
		}

		// —— Vandermonde 矩阵：V(i,j) = v[i]^(n-1-j) ——
		template<class type>
		matrix<type> vander(const vector<type>& v)
		{
			const xhqm::size n = v.size();
			matrix<type> r(n, n);
			for (xhqm::size i = 0; i < n; ++i)
				for (xhqm::size j = 0; j < n; ++j)
					r(i, j) = ::std::pow(v[i], type(n - 1 - j));
			return r;
		}

		// —— 友矩阵（降幂系数 p，首一化）——
		template<class type>
		matrix<type> compan(const vector<type>& p)
		{
			const xhqm::size n = p.size();
			const xhqm::size deg = (n > 1) ? (n - 1) : 0;
			matrix<type> r(deg, deg, type(0));
			if (deg == 0 || p[0] == type(0)) return r;
			for (xhqm::size j = 0; j < deg; ++j)
				r(0, j) = -p[j + 1] / p[0];
			for (xhqm::size i = 1; i < deg; ++i)
				r(i, i - 1) = type(1);
			return r;
		}

		// —— Hilbert 矩阵：H(i,j) = 1/(i+j+1) ——
		template<class type>
		matrix<type> hilb(const xhqm::size& n)
		{
			static_assert(!std::is_integral_v<type>, "analysis::hilb 对整型无意义");
			matrix<type> r(n, n);
			for (xhqm::size i = 0; i < n; ++i)
				for (xhqm::size j = 0; j < n; ++j)
					r(i, j) = xhqm::div(type(1), type(i + j + 1));
			return r;
		}

		// —— Toeplitz：T(i,j) = c[i-j]（i≥j）或 r[j-i]（j>i）——
		template<class type>
		matrix<type> toeplitz(const vector<type>& c, const vector<type>& r)
		{
			const xhqm::size m = c.size(), n = r.size();
			matrix<type> res(m, n);
			for (xhqm::size i = 0; i < m; ++i)
				for (xhqm::size j = 0; j < n; ++j)
					res(i, j) = (i >= j) ? c[i - j] : r[j - i];
			return res;
		}

		// —— Hankel：H(i,j) 沿反对角为常数，第一列 c、最后一行 r ——
		template<class type>
		matrix<type> hankel(const vector<type>& c, const vector<type>& r)
		{
			const xhqm::size m = c.size(), n = r.size();
			matrix<type> res(m, n);
			for (xhqm::size i = 0; i < m; ++i)
				for (xhqm::size j = 0; j < n; ++j)
				{
					const xhqm::size k = i + j;
					res(i, j) = (k < m) ? c[k] : r[k - m + 1];
				}
			return res;
		}
	}
}

#endif
