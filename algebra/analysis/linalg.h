#ifndef XHQM_ANALYSIS_LINALG
#define XHQM_ANALYSIS_LINALG

// 线性代数（MATLAB 风格）：mtimes / inv / trace / norm / rank / cond / lu / qr / chol。
#include "axis.h"
#include "../tensor/matrix.h"
#include "../tensor/vector.h"
#include "../tensor/linalg.h"   // tensor_base::svd / eig_sym / matmul
#include "construct.h"
#include <array>

namespace xhqm
{
	namespace analysis
	{
		// —— 内部：matrix → tensor<type,2>（零拷贝 values()，供 svd/eig 等使用） ——
		template<class type>
		const xhqm::tensor<2, type>& to_tensor(const matrix<type>& m) { return m.values(); }

		// —— 内部：tensor<type,2> → matrix（显式包装构造，svd/eig 等结果回写） ——
		template<class type>
		matrix<type> from_tensor(const xhqm::tensor<2, type>& t) { return matrix<type>(t); }

		// 矩阵乘（MATLAB *）
		template<class type>
		matrix<type> mtimes(const matrix<type>& a, const matrix<type>& b)
		{
			return a * b;
		}

		// 迹：主对角元素之和（走 trace 桩）
		template<class type>
		type trace(const matrix<type>& m)
		{
			return xhqm::tensor_base::trace(to_tensor(m));
		}

		// 逆矩阵（走 inv 桩，奇异返回空）
		template<class type>
		matrix<type> inv(const matrix<type>& m)
		{
			return from_tensor(xhqm::tensor_base::inv(to_tensor(m)));
		}

		// 范数：norm_type::spectral（默认，最大奇异值）/ one（最大列和）/ inf（最大行和）/ fro（Frobenius，走桩）
		template<class type>
		type norm(const matrix<type>& m, norm_type p = norm_type::spectral)
		{
			const auto& t = to_tensor(m);
			if (p == norm_type::fro) return xhqm::tensor_base::norm_fro(t);
			if (p == norm_type::spectral) return xhqm::tensor_base::norm_spectral(t);
			if (p == norm_type::one) return xhqm::tensor_base::norm_one(t);
			return xhqm::tensor_base::norm_inf(t);
		}

		// 秩：经 SVD 数非零奇异值（阈值 = 最大奇异值 × 尺寸 × eps）
		template<class type>
		xhqm::size rank(const matrix<type>& m)
		{
			auto sv = xhqm::tensor_base::svd(to_tensor(m));
			const xhqm::size k = sv.S.dimension(0);
			const type tol = sv.S[0] * type(xhqm::preci<type>) * type((m.rows() > m.cols()) ? m.rows() : m.cols());
			xhqm::size r = 0;
			for (xhqm::size i = 0; i < k; ++i)
				if (sv.S[i] > tol) ++r;
			return r;
		}

		// 条件数：最大奇异值 / 最小奇异值
		template<class type>
		type cond(const matrix<type>& m)
		{
			auto sv = xhqm::tensor_base::svd(to_tensor(m));
			const xhqm::size k = sv.S.dimension(0);
			if (k == 0) return type(0);
			const type smax = sv.S[0];
			const type smin = sv.S[k - 1];
			return (smin > type(0)) ? smax / smin : std::numeric_limits<type>::infinity();
		}

		// LU 分解（部分主元，复用 tensor_base::lu）：P·A = L·U
		template<class type>
		struct lu_result { matrix<type> L; matrix<type> U; matrix<type> P; };
		template<class type>
		lu_result<type> lu(const matrix<type>& m)
		{
			lu_result<type> r;
			auto lr = xhqm::tensor_base::lu(to_tensor(m));
			if (lr.U.dimension(0) == 0) return r;  // 奇异/非方阵
			r.L = from_tensor(lr.L);
			r.U = from_tensor(lr.U);
			r.P = from_tensor(lr.P);
			return r;
		}

		// QR 分解（Householder，复用 tensor_base::qr）：A = Q·R，Q 正交（m×m），R 上三角（m×n）
		template<class type>
		struct qr_result { matrix<type> Q; matrix<type> R; };
		template<class type>
		qr_result<type> qr(const matrix<type>& m)
		{
			qr_result<type> r;
			auto q = xhqm::tensor_base::qr(to_tensor(m));
			r.Q = from_tensor(q.Q);
			r.R = from_tensor(q.R);
			return r;
		}

		// Cholesky 分解（对称正定）：A = L·Lᵀ，返回下三角 L（薄封装 tensor_base::chol）
		template<class type>
		matrix<type> chol(const matrix<type>& m)
		{
			return from_tensor(xhqm::tensor_base::chol(to_tensor(m)));
		}

		// 行列式（薄封装 matrix::det）
		template<class type>
		type det(const matrix<type>& m)
		{
			return m.det();
		}

		// 左除 mldivide(a,b) = a\b：解 a·x = b（方阵，LU + 三角求解）
		template<class type>
		matrix<type> mldivide(const matrix<type>& a, const matrix<type>& b)
		{
			const xhqm::size n = a.rows();
			if (n == 0 || n != a.cols() || b.rows() != n) return matrix<type>();
			auto lr = xhqm::tensor_base::lu(to_tensor(a));
			if (lr.U.dimension(0) == 0) return matrix<type>();  // 奇异
			for (xhqm::size i = 0; i < n; ++i)
				if (xhqm::abs(lr.U(i, i)) < xhqm::preci<type>) return matrix<type>();
			auto Pb = xhqm::tensor_base::matmul(lr.P, to_tensor(b));  // P·b（一次算所有列）
			matrix<type> x(n, b.cols(), type(0));
			for (xhqm::size col = 0; col < b.cols(); ++col)
			{
				xhqm::tensor<1, type> bb(std::array<xhqm::size, 1>{ n }, type(0));
				for (xhqm::size i = 0; i < n; ++i) bb[i] = Pb(i, col);
				auto y  = xhqm::tensor_base::trsv_lower(lr.L, bb, true);   // 前代 L·y = P·b
				auto xx = xhqm::tensor_base::trsv_upper(lr.U, y, false);   // 回代 U·x = y
				for (xhqm::size i = 0; i < n; ++i) x(i, col) = xx[i];
			}
			return x;
		}

		// 右除 mrdivide(a,b) = a/b = a·b⁻¹
		template<class type>
		matrix<type> mrdivide(const matrix<type>& a, const matrix<type>& b)
		{
			return mtimes(a, inv(b));
		}

		// 行最简形（走 rref 桩）
		template<class type>
		matrix<type> rref(const matrix<type>& m)
		{
			return from_tensor(xhqm::tensor_base::rref(to_tensor(m)));
		}

		// 值域正交基（U 的前 rank 列）
		template<class type>
		matrix<type> orth(const matrix<type>& m)
		{
			auto sv = xhqm::tensor_base::svd(to_tensor(m));
			const xhqm::size r = rank(m);
			if (r == 0) return matrix<type>();
			matrix<type> out(m.rows(), r);
			for (xhqm::size j = 0; j < r; ++j)
				for (xhqm::size i = 0; i < m.rows(); ++i)
					out(i, j) = sv.U(i, j);
			return out;
		}

		// 零空间（V 的后 n-rank 列）
		template<class type>
		matrix<type> null(const matrix<type>& m)
		{
			auto sv = xhqm::tensor_base::svd(to_tensor(m));
			const xhqm::size r = rank(m);
			const xhqm::size n = m.cols();
			if (r >= n) return matrix<type>();
			matrix<type> out(n, n - r);
			for (xhqm::size j = r; j < n; ++j)
				for (xhqm::size i = 0; i < n; ++i)
					out(i, j - r) = sv.V(i, j);
			return out;
		}

		// 条件数倒数：1 / (‖A‖₁ · ‖A⁻¹‖₁)
		template<class type>
		type rcond(const matrix<type>& m)
		{
			const type n1 = norm(m, 1);
			matrix<type> mi = inv(m);
			if (mi.rows() == 0) return type(0);
			const type ni1 = norm(mi, 1);
			return (n1 > type(0) && ni1 > type(0)) ? type(1) / (n1 * ni1) : type(0);
		}

		// —— SVD（matrix/vector 接口，复用 tensor_base::svd）——
		template<class type>
		struct svd_result { matrix<type> U; vector<type> S; matrix<type> V; };
		template<class type>
		svd_result<type> svd(const matrix<type>& m)
		{
			auto sv = xhqm::tensor_base::svd(to_tensor(m));
			const xhqm::size rows = m.rows(), cols = m.cols(), k = sv.S.dimension(0);
			svd_result<type> r;
			r.U = matrix<type>(rows, rows, type(0));
			r.S = vector<type>(k, type(0));
			r.V = matrix<type>(cols, cols, type(0));
			for (xhqm::size i = 0; i < rows; ++i)
				for (xhqm::size j = 0; j < rows; ++j) r.U(i, j) = sv.U(i, j);
			for (xhqm::size i = 0; i < k; ++i) r.S[i] = sv.S[i];
			for (xhqm::size i = 0; i < cols; ++i)
				for (xhqm::size j = 0; j < cols; ++j) r.V(i, j) = sv.V(i, j);
			return r;
		}

		// —— 一般特征值（复用 tensor_base::eig，返回复特征值向量）——
		template<class type>
		xhqm::vector<xhqm::complex<type>> eig(const matrix<type>& m)
		{
			auto ev = xhqm::tensor_base::eig(to_tensor(m));
			const xhqm::size n = ev.dimension(0);
			xhqm::vector<xhqm::complex<type>> r(n);
			for (xhqm::size i = 0; i < n; ++i) r[i] = ev[i];
			return r;
		}

		// —— 伪逆（复用 tensor_base::pinv）——
		template<class type>
		matrix<type> pinv(const matrix<type>& m, const type& tol = type(0))
		{
			return from_tensor(xhqm::tensor_base::pinv(to_tensor(m), tol));
		}

		// —— 实 Schur 分解（复用 tensor_base::schur）——
		template<class type>
		struct schur_result { matrix<type> T; matrix<type> Q; };
		template<class type>
		schur_result<type> schur(const matrix<type>& m)
		{
			auto sc = xhqm::tensor_base::schur(to_tensor(m));
			schur_result<type> r;
			r.T = from_tensor(sc.T);
			r.Q = from_tensor(sc.Q);
			return r;
		}

		// —— 多项式求根：p 降幂系数，返回复根（companion 矩阵 + eig）——
		template<class type>
		xhqm::vector<xhqm::complex<type>> roots(const xhqm::vector<type>& p)
		{
			const xhqm::size n = p.size();
			xhqm::size start = 0;
			while (start < n && p[start] == type(0)) ++start;
			const xhqm::size deg = (n > start + 1) ? (n - start - 1) : 0;
			xhqm::vector<xhqm::complex<type>> r(deg);
			if (deg == 0) return r;
			const type lead = p[start];
			xhqm::tensor<2, type> C(std::array<xhqm::size, 2>{ deg, deg }, type(0));
			for (xhqm::size j = 0; j < deg; ++j)
				C(0, j) = -p[start + 1 + j] / lead;
			for (xhqm::size i = 1; i < deg; ++i)
				C(i, i - 1) = type(1);
			auto ev = xhqm::tensor_base::eig(C);
			for (xhqm::size i = 0; i < deg; ++i) r[i] = ev[i];
			return r;
		}

		// —— 矩阵指数（走 expm 桩）——
		template<class type>
		matrix<type> expm(const matrix<type>& m)
		{
			return from_tensor(xhqm::tensor_base::expm(to_tensor(m)));
		}

		// —— 矩阵平方根（走 sqrtm 桩）——
		template<class type>
		matrix<type> sqrtm(const matrix<type>& m, xhqm::size max_iter = 64, const type& tol = type(1e-10))
		{
			return from_tensor(xhqm::tensor_base::sqrtm(to_tensor(m), max_iter, tol));
		}

		// —— 矩阵对数（走 logm 桩）——
		template<class type>
		matrix<type> logm(const matrix<type>& m, xhqm::size max_sqrt = 40, xhqm::size taylor_terms = 20)
		{
			return from_tensor(xhqm::tensor_base::logm(to_tensor(m), max_sqrt, taylor_terms));
		}
	}
}

#endif
