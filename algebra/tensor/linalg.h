#ifndef XHQM_TENSOR_LINALG
#define XHQM_TENSOR_LINALG

// 线性代数与归约：matmul / transpose / reduce / broadcast_to。
// 逐元素与矩阵乘均经 tensor_backend_active() 并行调度。
#include "tensor_base.h"
#include "tensor_view.h"
#include "complex.hpp"
#include <utility>
#include <limits>
#include <vector>
#include <algorithm>
#include <execution>

// 并行执行策略的可移植守卫：`std::execution::par` 需要 PSTL。Apple clang 的 libc++ 至今未提供
// `__cpp_lib_parallel_algorithm`，直接调 `std::execution::par` 会在 macOS 上编译失败
// （实测 Apple clang 21 / c++17·20·23 三档均无 `std::execution::par`，且系统无 TBB/PSTL 头）。
// 这里退化为**顺序执行**：排序结果不变，仅失去并行加速；有 PSTL 的平台行为完全不变。
#if defined(__cpp_lib_parallel_algorithm)
#  define XHQM_PAR std::execution::par,   // 连逗号一起带（调用点写法统一）
#  define XHQM_HAS_PAR 1
#else
#  define XHQM_PAR   // 空：退化为顺序执行
#  define XHQM_HAS_PAR 0
#endif
#include <atomic>

namespace xhqm {
	namespace tensor_base {

		// 矩阵乘：a[m×k] · b[k×n] = c[m×n]（b.dimension(0) 需等于 a.dimension(1)）
		template<class type>
		xhqm::tensor<2, type> matmul(const xhqm::tensor<2, type>& a, const xhqm::tensor<2, type>& b)
		{
			const xhqm::size m = a.dimension(0);
			const xhqm::size k = a.dimension(1);
			const xhqm::size n = b.dimension(1);
			// 内维一致性：a.dimension(1) 必须等于 b.dimension(0)，否则返回空（防越界读）
			if (k != b.dimension(0))
				return xhqm::tensor<2, type>();

			xhqm::tensor<2, type> c(std::array<xhqm::size, 2>{ m, n }, type(0));
			// 直接走后端（cuBLAS/SIMD/CUDA 分派由后端决定），dtype 不支持返回 false 回退 CPU 并行
			if (tensor_backend_active().matmul(c.data(), a.data(), b.data(), m, n, k, dtype_of<type>::value))
				return c;
			xhqm::tensor<2, type>& self = c;
			tensor_backend_active().parallel_for(0, m * n, [&](xhqm::size idx)
			{
				const xhqm::size i = idx / n;
				const xhqm::size j = idx % n;
				type acc = type(0);
				for (xhqm::size kk = 0; kk < k; ++kk)
					acc = xhqm::sum(acc, xhqm::mul(a(i, kk), b(kk, j)));
				self(i, j) = acc;
			});
			return c;
		}

		// 转置：交换 a,b 两维，返回拷贝（视图见 tensor_view::transpose）
		template<class type, xhqm::size dim>
		xhqm::tensor<dim, type> transpose(const xhqm::tensor<dim, type>& t, xhqm::size a, xhqm::size b)
		{
			auto dims = t.shape();
			std::swap(dims[a], dims[b]);
			xhqm::tensor<dim, type> out(dims);
			// 2D 矩阵转置：走后端 kernel（SIMD/GPU），其余轴回退通用循环
			if constexpr (dim == 2)
			{
				if (a == 0 && b == 1)
				{
					if (tensor_backend_active().transpose(out.data(), t.data(),
						t.dimension(0), t.dimension(1), dtype_of<type>::value))
						return out;
				}
			}
			for (xhqm::size flat = 0; flat < out.size(); ++flat)
			{
				auto coord = out.index(flat);
				std::swap(coord[a], coord[b]);
				out.value(flat) = t.value(t.index(coord));
			}
			return out;
		}

		// 沿轴归约（通用）。op 为二元归约，init 为初值。
		template<class type, xhqm::size dim, class Op>
		xhqm::tensor<dim - 1, type> reduce(const xhqm::tensor<dim, type>& t, xhqm::size axis, Op&& op, const type& init)
		{
			static_assert(dim >= 1, "reduce 需要 dim>=1");
			std::array<xhqm::size, dim - 1> out_dims{};
			xhqm::size oi = 0;
			for (xhqm::size k = 0; k < dim; ++k)
				if (k != axis) out_dims[oi++] = t.dimension(k);

			xhqm::tensor<dim - 1, type> out(out_dims, init);
			// 各输出元素相互独立，parallel_for 并行（no 后端串行、cpu 后端多线程）。
			tensor_backend_active().parallel_for(0, out.size(), [&](xhqm::size flat)
			{
				auto out_coord = out.index(flat);
				type acc = init;
				std::array<xhqm::size, dim> coord{};
				for (xhqm::size a = 0; a < t.dimension(axis); ++a)
				{
					xhqm::size oi2 = 0;
					for (xhqm::size k = 0; k < dim; ++k)
						coord[k] = (k == axis) ? a : out_coord[oi2++];
					acc = op(acc, t.value(t.index(coord)));
				}
				out.value(flat) = acc;
			});
			return out;
		}

		// 沿最后一维归约：走 backend.reduce（SIMD/GPU kernel，op: 0=sum 1=max 2=min），
		// 不支持（非 f32/f64 或后端未实现）时回退通用 reduce。
		template<class type, xhqm::size dim>
		xhqm::tensor<dim - 1, type> reduce_last(const xhqm::tensor<dim, type>& t, int op)
		{
			const xhqm::size inner = t.dimension(dim - 1);
			std::array<xhqm::size, dim - 1> out_dims{};
			for (xhqm::size k = 0; k < dim - 1; ++k) out_dims[k] = t.dimension(k);
			xhqm::tensor<dim - 1, type> out(out_dims, type(0));
			// 空张量（末维为 0）：直接返回，避免 t.size()/inner 除零
			if (inner == 0)
				return out;
			const xhqm::size outer = t.size() / inner;
			if (tensor_backend_active().reduce(out.data(), t.data(), outer, inner, dtype_of<type>::value, op))
				return out;
			type init = type(0);
			if (op == reduce_op_max) init = std::numeric_limits<type>::lowest();
			else if (op == reduce_op_min) init = std::numeric_limits<type>::max();
			else if (op == reduce_op_prod) init = type(1);
			return reduce(t, dim - 1, [op](const type& a, const type& b) { return reduce_eval(a, b, op); }, init);
		}

		template<class type, xhqm::size dim>
		xhqm::tensor<dim - 1, type> reduce_sum(const xhqm::tensor<dim, type>& t, xhqm::size axis)
		{
			if (axis == dim - 1) return reduce_last(t, reduce_op_sum);
			return reduce(t, axis, [](const type& a, const type& b) { return xhqm::sum(a, b); }, type(0));
		}
		// 要求 type 为算术类型（依赖 numeric_limits 的最值）
		template<class type, xhqm::size dim>
		xhqm::tensor<dim - 1, type> reduce_max(const xhqm::tensor<dim, type>& t, xhqm::size axis)
		{
			if (axis == dim - 1) return reduce_last(t, reduce_op_max);
			return reduce(t, axis, [](const type& a, const type& b) { return xhqm::max(a, b); },
				std::numeric_limits<type>::lowest());
		}
		template<class type, xhqm::size dim>
		xhqm::tensor<dim - 1, type> reduce_min(const xhqm::tensor<dim, type>& t, xhqm::size axis)
		{
			if (axis == dim - 1) return reduce_last(t, reduce_op_min);
			return reduce(t, axis, [](const type& a, const type& b) { return xhqm::min(a, b); },
				std::numeric_limits<type>::max());
		}
		template<class type, xhqm::size dim>
		xhqm::tensor<dim - 1, type> reduce_prod(const xhqm::tensor<dim, type>& t, xhqm::size axis)
		{
			if (axis == dim - 1) return reduce_last(t, reduce_op_prod);
			return reduce(t, axis, [](const type& a, const type& b) { return xhqm::mul(a, b); }, type(1));
		}

		// —— 全元素归约（串行桩，返回标量）——
		// 注：CPU 并行 = CPU 串行（可并行归约，暂未回补）。
		template<class type, xhqm::size dim>
		type sum_all(const xhqm::tensor<dim, type>& t)
		{
			type acc = type(0);
			for (xhqm::size i = 0; i < t.size(); ++i) acc = xhqm::sum(acc, t.value(i));
			return acc;
		}
		template<class type, xhqm::size dim>
		type prod_all(const xhqm::tensor<dim, type>& t)
		{
			type acc = type(1);
			for (xhqm::size i = 0; i < t.size(); ++i) acc = xhqm::mul(acc, t.value(i));
			return acc;
		}
		template<class type, xhqm::size dim>
		type max_all(const xhqm::tensor<dim, type>& t)
		{
			if (t.size() == 0) return type(0);
			type acc = t.value(0);
			for (xhqm::size i = 1; i < t.size(); ++i) acc = xhqm::max(acc, t.value(i));
			return acc;
		}
		template<class type, xhqm::size dim>
		type min_all(const xhqm::tensor<dim, type>& t)
		{
			if (t.size() == 0) return type(0);
			type acc = t.value(0);
			for (xhqm::size i = 1; i < t.size(); ++i) acc = xhqm::min(acc, t.value(i));
			return acc;
		}

		// —— 全元素 argmax / argmin（串行桩，返回行优先线性索引）——
		// 注：CPU 并行 = CPU 串行（可并行归约，暂未回补）。
		template<class type, xhqm::size dim>
		xhqm::index argmax_all(const xhqm::tensor<dim, type>& t)
		{
			if (t.size() == 0) return 0;
			xhqm::index best = 0;
			for (xhqm::size i = 1; i < t.size(); ++i) if (t.value(i) > t.value(best)) best = i;
			return best;
		}
		template<class type, xhqm::size dim>
		xhqm::index argmin_all(const xhqm::tensor<dim, type>& t)
		{
			if (t.size() == 0) return 0;
			xhqm::index best = 0;
			for (xhqm::size i = 1; i < t.size(); ++i) if (t.value(i) < t.value(best)) best = i;
			return best;
		}

		// —— 沿任意轴的 argmax / argmin（串行桩，返回索引）——
		// 注：CPU 并行 = CPU 串行（可并行归约，暂未回补）。
		template<class type, xhqm::size dim>
		xhqm::tensor<dim - 1, xhqm::index> argmax(const xhqm::tensor<dim, type>& t, xhqm::size axis)
		{
			std::array<xhqm::size, dim - 1> out_dims{};
			xhqm::size oi = 0;
			for (xhqm::size k = 0; k < dim; ++k)
				if (k != axis) out_dims[oi++] = t.dimension(k);
			xhqm::tensor<dim - 1, xhqm::index> out(out_dims, xhqm::index(0));
			if (t.size() == 0) return out;
			const xhqm::size n = t.dimension(axis);
			for (xhqm::size flat = 0; flat < out.size(); ++flat)
			{
				auto out_coord = out.index(flat);
				std::array<xhqm::size, dim> cb{};
				xhqm::size oi2 = 0;
				for (xhqm::size k = 0; k < dim; ++k) cb[k] = (k == axis) ? xhqm::size(0) : out_coord[oi2++];
				xhqm::index best = 0;
				for (xhqm::size a = 1; a < n; ++a)
				{
					std::array<xhqm::size, dim> c{};
					xhqm::size oi3 = 0;
					for (xhqm::size k = 0; k < dim; ++k) c[k] = (k == axis) ? a : out_coord[oi3++];
					if (t.value(t.index(c)) > t.value(t.index(cb))) { best = a; cb = c; }
				}
				out.value(flat) = best;
			}
			return out;
		}
		template<class type, xhqm::size dim>
		xhqm::tensor<dim - 1, xhqm::index> argmin(const xhqm::tensor<dim, type>& t, xhqm::size axis)
		{
			std::array<xhqm::size, dim - 1> out_dims{};
			xhqm::size oi = 0;
			for (xhqm::size k = 0; k < dim; ++k)
				if (k != axis) out_dims[oi++] = t.dimension(k);
			xhqm::tensor<dim - 1, xhqm::index> out(out_dims, xhqm::index(0));
			if (t.size() == 0) return out;
			const xhqm::size n = t.dimension(axis);
			for (xhqm::size flat = 0; flat < out.size(); ++flat)
			{
				auto out_coord = out.index(flat);
				std::array<xhqm::size, dim> cb{};
				xhqm::size oi2 = 0;
				for (xhqm::size k = 0; k < dim; ++k) cb[k] = (k == axis) ? xhqm::size(0) : out_coord[oi2++];
				xhqm::index best = 0;
				for (xhqm::size a = 1; a < n; ++a)
				{
					std::array<xhqm::size, dim> c{};
					xhqm::size oi3 = 0;
					for (xhqm::size k = 0; k < dim; ++k) c[k] = (k == axis) ? a : out_coord[oi3++];
					if (t.value(t.index(c)) < t.value(t.index(cb))) { best = a; cb = c; }
				}
				out.value(flat) = best;
			}
			return out;
		}

		// 广播视图（零拷贝，要求 size==1 的维可扩展到目标形状）
		template<class type, xhqm::size dim>
		xhqm::tensor_view<dim, type> broadcast_to(xhqm::tensor<dim, type>& t, const std::array<xhqm::size, dim>& dims)
		{
			return xhqm::tensor_view<dim, type>(t).broadcast(dims);
		}

		// —— 特征值 / 特征向量 / SVD ——
		// 对称矩阵特征值分解（Jacobi 迭代）：A 为 n×n 对称，返回特征值（升序）与特征向量（列）
		// 注：CPU 并行 = CPU 串行（Jacobi 旋转逐次依赖，确实不适合 CPU 并行）。
		template<class type>
		struct eig_result { xhqm::tensor<1, type> values; xhqm::tensor<2, type> vectors; };

		template<class type>
		eig_result<type> eig_sym(const xhqm::tensor<2, type>& A, xhqm::size max_iter = 100)
		{
			const xhqm::size n = A.dimension(0);
			// 空矩阵或非方阵：无法做对称特征分解，返回空
			if (n == 0 || n != A.dimension(1))
				return eig_result<type>();
			// 1×1：特征值 = 唯一元素，特征向量 = [1]
			if (n == 1)
			{
				eig_result<type> single;
				single.values = xhqm::tensor<1, type>(std::array<xhqm::size, 1>{ 1 }, A(0, 0));
				single.vectors = xhqm::tensor<2, type>(std::array<xhqm::size, 2>{ 1, 1 }, type(0));
				single.vectors(0, 0) = type(1);
				return single;
			}
			const type eps = xhqm::preci<type> *type(100);
			xhqm::tensor<2, type> a = A;
			xhqm::tensor<2, type> v(std::array<xhqm::size, 2>{ n, n }, type(0));
			for (xhqm::size i = 0; i < n; ++i) v(i, i) = type(1);

			for (xhqm::size iter = 0; iter < max_iter; ++iter)
			{
				xhqm::size p = 0, q = 1; type max_off = std::abs(a(0, 1));
				for (xhqm::size i = 0; i < n; ++i)
					for (xhqm::size j = i + 1; j < n; ++j)
						if (std::abs(a(i, j)) > max_off) { max_off = std::abs(a(i, j)); p = i; q = j; }
				if (max_off < eps) break;

				const type theta = type(0.5) * std::atan2(type(2) * a(p, q), a(q, q) - a(p, p));
				const type c = std::cos(theta), s = std::sin(theta);

				for (xhqm::size k = 0; k < n; ++k)
				{
					if (k == p || k == q) continue;
					const type akp = a(k, p), akq = a(k, q);
					a(k, p) = a(p, k) = c * akp - s * akq;
					a(k, q) = a(q, k) = s * akp + c * akq;
				}
				const type app = a(p, p), aqq = a(q, q), apq = a(p, q);
				a(p, p) = c * c * app - type(2) * s * c * apq + s * s * aqq;
				a(q, q) = s * s * app + type(2) * s * c * apq + c * c * aqq;
				a(p, q) = a(q, p) = type(0);

				for (xhqm::size k = 0; k < n; ++k)
				{
					const type vkp = v(k, p), vkq = v(k, q);
					v(k, p) = c * vkp - s * vkq;
					v(k, q) = s * vkp + c * vkq;
				}
			}

			eig_result<type> r;
			r.values = xhqm::tensor<1, type>(std::array<xhqm::size, 1>{ n }, type(0));
			r.vectors = v;
			for (xhqm::size i = 0; i < n; ++i) r.values[i] = a(i, i);
			// 升序排序（选择排序，同步列）
			for (xhqm::size i = 0; i < n; ++i)
				for (xhqm::size j = i + 1; j < n; ++j)
					if (r.values[i] > r.values[j])
					{
						std::swap(r.values[i], r.values[j]);
						for (xhqm::size k = 0; k < n; ++k) std::swap(r.vectors(k, i), r.vectors(k, j));
					}
			return r;
		}

		// SVD：A(m×n) = U(m×m) · S(min(m,n)) · V^T(n×n)，经 A^T A 的 Jacobi 特征分解
		// 注：CPU 并行 = CPU 串行（经 Jacobi 特征分解，顺序依赖，确实不适合 CPU 并行）。
		template<class type>
		struct svd_result { xhqm::tensor<2, type> U; xhqm::tensor<1, type> S; xhqm::tensor<2, type> V; };

		template<class type>
		svd_result<type> svd(const xhqm::tensor<2, type>& A)
		{
			const xhqm::size m = A.dimension(0), n = A.dimension(1);
			const xhqm::size k = (m < n) ? m : n;
			xhqm::tensor<2, type> At = transpose(A, 0, 1);
			xhqm::tensor<2, type> B = matmul(At, A);  // n×n 对称半正定
			auto eig = eig_sym(B);

			svd_result<type> r;
			r.V = xhqm::tensor<2, type>(std::array<xhqm::size, 2>{ n, n }, type(0));
			for (xhqm::size i = 0; i < n; ++i)
				for (xhqm::size row = 0; row < n; ++row)
					r.V(row, i) = eig.vectors(row, n - 1 - i);  // 按奇异值降序重排右奇异向量
			r.S = xhqm::tensor<1, type>(std::array<xhqm::size, 1>{ k }, type(0));
			r.U = xhqm::tensor<2, type>(std::array<xhqm::size, 2>{ m, m }, type(0));

			for (xhqm::size i = 0; i < k; ++i)
			{
				const type ev = eig.values[n - 1 - i];  // 特征值从大到小
				const type sigma = (ev > type(0)) ? std::sqrt(ev) : type(0);
				r.S[i] = sigma;
				if (sigma > xhqm::preci<type>)
				{
					for (xhqm::size row = 0; row < m; ++row)
					{
						type acc = type(0);
						for (xhqm::size col = 0; col < n; ++col)
							acc += A(row, col) * eig.vectors(col, n - 1 - i);
						r.U(row, i) = acc / sigma;
					}
				}
			}
			return r;
		}

		// —— LU 分解（部分主元）：P·A = L·U，L 单位下三角，U 上三角，P 置换 ——
		// 注：CPU 并行 = CPU 串行（部分主元逐列依赖，确实不适合 CPU 并行）。
		template<class type>
		struct lu_result { xhqm::tensor<2, type> L; xhqm::tensor<2, type> U; xhqm::tensor<2, type> P; };
		template<class type>
		lu_result<type> lu(const xhqm::tensor<2, type>& A)
		{
			const xhqm::size n = A.dimension(0);
			lu_result<type> r;
			if (n != A.dimension(1) || n == 0) return r;
			xhqm::tensor<2, type> a = A;   // 就地：下三角存 L（不含对角），上三角存 U
			r.L = xhqm::tensor<2, type>(std::array<xhqm::size, 2>{ n, n }, type(0));
			r.U = xhqm::tensor<2, type>(std::array<xhqm::size, 2>{ n, n }, type(0));
			r.P = xhqm::tensor<2, type>(std::array<xhqm::size, 2>{ n, n }, type(0));
			for (xhqm::size i = 0; i < n; ++i) { r.L(i, i) = type(1); r.P(i, i) = type(1); }
			for (xhqm::size k = 0; k < n; ++k)
			{
				// 部分主元：a(k:n, k) 已被前 k 步 Schur 补更新
				xhqm::size pivot = k;
				type maxv = xhqm::abs(a(k, k));
				for (xhqm::size i = k + 1; i < n; ++i)
					if (xhqm::abs(a(i, k)) > maxv) { maxv = xhqm::abs(a(i, k)); pivot = i; }
				if (maxv < xhqm::preci<type>) return lu_result<type>();
				if (pivot != k)
					for (xhqm::size j = 0; j < n; ++j)
					{
						std::swap(a(k, j), a(pivot, j));
						std::swap(r.P(k, j), r.P(pivot, j));
					}
				// L 列：a(i,k) /= a(k,k)（i>k）
				for (xhqm::size i = k + 1; i < n; ++i) a(i, k) = xhqm::div(a(i, k), a(k, k));
				// Schur 补：a(i,j) -= a(i,k)·a(k,j)（i>k, j>k）
				for (xhqm::size i = k + 1; i < n; ++i)
					for (xhqm::size j = k + 1; j < n; ++j)
						a(i, j) = xhqm::sub(a(i, j), xhqm::mul(a(i, k), a(k, j)));
			}
			// 提取 L（下三角 + 单位对角）与 U（上三角）
			for (xhqm::size i = 0; i < n; ++i)
				for (xhqm::size j = 0; j < n; ++j)
					if (j < i) r.L(i, j) = a(i, j);
					else r.U(i, j) = a(i, j);
			return r;
		}

		// —— QR 分解（Householder）：A = Q·R，Q 正交（m×m），R 上三角（m×n） ——
		// 注：CPU 并行 = CPU 串行（Householder 反射逐次依赖，确实不适合 CPU 并行）。
		template<class type>
		struct qr_result { xhqm::tensor<2, type> Q; xhqm::tensor<2, type> R; };
		template<class type>
		qr_result<type> qr(const xhqm::tensor<2, type>& A)
		{
			const xhqm::size m = A.dimension(0), n = A.dimension(1);
			qr_result<type> r;
			r.R = A;
			r.Q = xhqm::tensor<2, type>(std::array<xhqm::size, 2>{ m, m }, type(0));
			for (xhqm::size i = 0; i < m; ++i) r.Q(i, i) = type(1);
			const xhqm::size k = (m < n) ? m : n;
			std::vector<type> v(m);
			for (xhqm::size j = 0; j < k; ++j)
			{
				type normx = type(0);
				for (xhqm::size i = j; i < m; ++i) normx = normx + r.R(i, j) * r.R(i, j);
				normx = xhqm::sqrt(normx);
				if (normx < xhqm::preci<type>) continue;
				type alpha = (r.R(j, j) > type(0)) ? -normx : normx;
				v[0] = r.R(j, j) - alpha;
				for (xhqm::size i = j + 1; i < m; ++i) v[i - j] = r.R(i, j);
				type vn2 = type(0);
				for (xhqm::size i = 0; i < m - j; ++i) vn2 = vn2 + v[i] * v[i];
				if (vn2 < xhqm::preci<type>) continue;
				for (xhqm::size c = j; c < n; ++c)
				{
					type dotv = type(0);
					for (xhqm::size i = 0; i < m - j; ++i) dotv = dotv + v[i] * r.R(j + i, c);
					type beta = type(2) * dotv / vn2;
					for (xhqm::size i = 0; i < m - j; ++i) r.R(j + i, c) = r.R(j + i, c) - beta * v[i];
				}
				for (xhqm::size row = 0; row < m; ++row)
				{
					type dotv = type(0);
					for (xhqm::size i = 0; i < m - j; ++i) dotv = dotv + r.Q(row, j + i) * v[i];
					type beta = type(2) * dotv / vn2;
					for (xhqm::size i = 0; i < m - j; ++i) r.Q(row, j + i) = r.Q(row, j + i) - beta * v[i];
				}
			}
			return r;
		}

		// —— 伪逆（Moore-Penrose）：A⁺ = V·S⁺·Uᵀ，奇异值阈值截断 ——
		// 注：CPU 并行 = CPU 串行（经 SVD，顺序依赖，确实不适合 CPU 并行）。
		template<class type>
		xhqm::tensor<2, type> pinv(const xhqm::tensor<2, type>& A, const type& tol = type(0))
		{
			const xhqm::size m = A.dimension(0), n = A.dimension(1);
			auto sv = svd(A);
			const xhqm::size k = sv.S.dimension(0);
			type thr = tol;
			if (thr <= type(0))
				thr = sv.S[0] * type(xhqm::preci<type>) * type((m > n) ? m : n);
			xhqm::tensor<2, type> Sp(std::array<xhqm::size, 2>{ n, m }, type(0));
			for (xhqm::size i = 0; i < k; ++i)
				if (sv.S[i] > thr) Sp(i, i) = type(1) / sv.S[i];
			return matmul(matmul(sv.V, Sp), transpose(sv.U, 0, 1));
		}

		// —— 实 Schur 分解：A = Q·T·Qᵀ（Hessenberg 归约 + 带移位 QR 迭代）——
		// 注：CPU 并行 = CPU 串行（Hessenberg/QR 迭代逐次依赖，确实不适合 CPU 并行）。
		template<class type>
		struct schur_result { xhqm::tensor<2, type> T; xhqm::tensor<2, type> Q; };

		template<class type>
		schur_result<type> schur(const xhqm::tensor<2, type>& A, xhqm::size max_iter = 500)
		{
			const xhqm::size n = A.dimension(0);
			schur_result<type> r;
			if (n == 0 || n != A.dimension(1)) return r;
			r.T = A;
			r.Q = xhqm::tensor<2, type>(std::array<xhqm::size, 2>{ n, n }, type(0));
			for (xhqm::size i = 0; i < n; ++i) r.Q(i, i) = type(1);
			if (n == 1) return r;

			// Hessenberg 归约（Householder 反射器）
			std::vector<type> v(n);
			for (xhqm::size k = 0; k + 2 < n; ++k)
			{
				type normx = type(0);
				for (xhqm::size i = k + 1; i < n; ++i) normx = normx + r.T(i, k) * r.T(i, k);
				if (normx < xhqm::preci<type>) continue;
				normx = xhqm::sqrt(normx);
				type alpha = (r.T(k + 1, k) > type(0)) ? -normx : normx;
				v[0] = r.T(k + 1, k) - alpha;
				for (xhqm::size i = k + 2; i < n; ++i) v[i - (k + 1)] = r.T(i, k);
				type vn2 = type(0);
				for (xhqm::size i = 0; i < n - (k + 1); ++i) vn2 = vn2 + v[i] * v[i];
				if (vn2 < xhqm::preci<type>) continue;
				for (xhqm::size j = k; j < n; ++j)
				{
					type dotv = type(0);
					for (xhqm::size i = 0; i < n - (k + 1); ++i) dotv = dotv + v[i] * r.T(k + 1 + i, j);
					type beta = type(2) * dotv / vn2;
					for (xhqm::size i = 0; i < n - (k + 1); ++i) r.T(k + 1 + i, j) = r.T(k + 1 + i, j) - beta * v[i];
				}
				for (xhqm::size i = 0; i < n; ++i)
				{
					type dotv = type(0);
					for (xhqm::size j = 0; j < n - (k + 1); ++j) dotv = dotv + r.T(i, k + 1 + j) * v[j];
					type beta = type(2) * dotv / vn2;
					for (xhqm::size j = 0; j < n - (k + 1); ++j) r.T(i, k + 1 + j) = r.T(i, k + 1 + j) - beta * v[j];
				}
				for (xhqm::size i = 0; i < n; ++i)
				{
					type dotv = type(0);
					for (xhqm::size j = 0; j < n - (k + 1); ++j) dotv = dotv + r.Q(i, k + 1 + j) * v[j];
					type beta = type(2) * dotv / vn2;
					for (xhqm::size j = 0; j < n - (k + 1); ++j) r.Q(i, k + 1 + j) = r.Q(i, k + 1 + j) - beta * v[j];
				}
			}

			// 带移位 QR 迭代（单移位 + 2×2 块 deflation）
			const type eps = xhqm::preci<type> * type(10);
			std::vector<type> cs(n), sn(n);
			xhqm::size m = n;
			xhqm::size iter = 0;
			while (m > 1 && iter < max_iter)
			{
				if (m == 2) break;  // 2×2 块即最小 Schur 块，直接接受（含共轭复对）
				if (xhqm::abs(r.T(m - 1, m - 2)) <= eps * (xhqm::abs(r.T(m - 1, m - 1)) + xhqm::abs(r.T(m - 2, m - 2))))
				{
					r.T(m - 1, m - 2) = type(0);
					--m;
					continue;
				}
				if (m > 2 && xhqm::abs(r.T(m - 2, m - 3)) <= eps * (xhqm::abs(r.T(m - 2, m - 2)) + xhqm::abs(r.T(m - 3, m - 3))))
				{
					r.T(m - 2, m - 3) = type(0);
					m -= 2;
					continue;
				}
				const type a = r.T(m - 2, m - 2), b = r.T(m - 2, m - 1), c = r.T(m - 1, m - 2), d = r.T(m - 1, m - 1);
				const type tr = a + d;
				const type det = a * d - b * c;
				const type disc = tr * tr - type(4) * det;
				type s;
				if (disc >= type(0))
				{
					const type sq = xhqm::sqrt(disc);
					const type l1 = (tr + sq) / type(2), l2 = (tr - sq) / type(2);
					s = (xhqm::abs(l1 - d) < xhqm::abs(l2 - d)) ? l1 : l2;
				}
				else
					s = tr / type(2);

				// H -= sI（活动子块对角）
				for (xhqm::size i = 0; i < m; ++i) r.T(i, i) = r.T(i, i) - s;
				// QR 阶段：左乘 Givensᵀ 消去次对角
				for (xhqm::size i = 0; i + 1 < m; ++i)
				{
					const type x = r.T(i, i), y = r.T(i + 1, i);
					const type rr = xhqm::sqrt(x * x + y * y);
					if (rr < xhqm::preci<type>) { cs[i] = type(1); sn[i] = type(0); continue; }
					const type cc = x / rr, ss = y / rr;
					cs[i] = cc; sn[i] = ss;
					for (xhqm::size j = i; j < m; ++j)
					{
						const type tj = cc * r.T(i, j) + ss * r.T(i + 1, j);
						r.T(i + 1, j) = -ss * r.T(i, j) + cc * r.T(i + 1, j);
						r.T(i, j) = tj;
					}
				}
				// RQ 阶段：右乘 Givens
				for (xhqm::size i = 0; i + 1 < m; ++i)
				{
					const type cc = cs[i], ss = sn[i];
					for (xhqm::size j = 0; j <= i + 1 && j < m; ++j)
					{
						const type tj = cc * r.T(j, i) + ss * r.T(j, i + 1);
						r.T(j, i + 1) = -ss * r.T(j, i) + cc * r.T(j, i + 1);
						r.T(j, i) = tj;
					}
				}
				// 加回移位
				for (xhqm::size i = 0; i < m; ++i) r.T(i, i) = r.T(i, i) + s;
				// Q 累积：右乘 Givens
				for (xhqm::size i = 0; i + 1 < m; ++i)
				{
					const type cc = cs[i], ss = sn[i];
					for (xhqm::size row = 0; row < n; ++row)
					{
						const type t1 = cc * r.Q(row, i) + ss * r.Q(row, i + 1);
						r.Q(row, i + 1) = -ss * r.Q(row, i) + cc * r.Q(row, i + 1);
						r.Q(row, i) = t1;
					}
				}
				++iter;
			}
			return r;
		}

		// —— 一般特征值：实矩阵 A 的特征值（可为共轭复对）——
		// 注：CPU 并行 = CPU 串行（经 Schur 分解，顺序依赖，确实不适合 CPU 并行）。
		template<class type>
		xhqm::tensor<1, xhqm::complex<type>> eig(const xhqm::tensor<2, type>& A)
		{
			const xhqm::size n = A.dimension(0);
			xhqm::tensor<1, xhqm::complex<type>> vals(std::array<xhqm::size, 1>{ n },
				xhqm::complex<type>(type(0), type(0)));
			if (n == 0 || n != A.dimension(1)) return vals;
			auto sc = schur(A);
			const type tol = xhqm::preci<type> * type(10);
			xhqm::size i = 0;
			while (i < n)
			{
				if (i + 1 == n || xhqm::abs(sc.T(i + 1, i)) <= tol * (xhqm::abs(sc.T(i, i)) + xhqm::abs(sc.T(i + 1, i + 1))))
				{
					vals[i] = xhqm::complex<type>(sc.T(i, i), type(0));
					++i;
				}
				else
				{
					const type a = sc.T(i, i), b = sc.T(i, i + 1), c = sc.T(i + 1, i), d = sc.T(i + 1, i + 1);
					const type tr = a + d;
					const type det = a * d - b * c;
					const type disc = tr * tr - type(4) * det;
					if (disc >= type(0))
					{
						const type sq = xhqm::sqrt(disc);
						vals[i] = xhqm::complex<type>((tr + sq) / type(2), type(0));
						vals[i + 1] = xhqm::complex<type>((tr - sq) / type(2), type(0));
					}
					else
					{
						const type re = tr / type(2);
						const type im = xhqm::sqrt(-disc) / type(2);
						vals[i] = xhqm::complex<type>(re, im);
						vals[i + 1] = xhqm::complex<type>(re, -im);
					}
					i += 2;
				}
			}
			return vals;
		}

		// —— 三角求解（BLAS trsv 语义）——
		// 解下三角 L·x = b（前代）；unit_diag=true 表示 L 对角全 1。直接走 backend.trsv
		// （BLAS f32/f64，CUDA 另支持 i8/i16/i32），不支持则回退块化列向前代：对角块串行、尾块按行 parallel_for 并行。
		template<class type>
		xhqm::tensor<1, type> trsv_lower(const xhqm::tensor<2, type>& L, const xhqm::tensor<1, type>& b, bool unit_diag = true)
		{
			const xhqm::size n = b.dimension(0);
			xhqm::tensor<1, type> x(std::array<xhqm::size, 1>{ n }, type(0));
			if (tensor_backend_active().trsv(x.data(), L.data(), b.data(), n, true, unit_diag, dtype_of<type>::value))
				return x;
			for (xhqm::size i = 0; i < n; ++i) x[i] = b[i];
			const xhqm::size B = 256;
			const xhqm::size NB = (n + B - 1) / B;
			for (xhqm::size jb = 0; jb < NB; ++jb)
			{
				const xhqm::size j0 = jb * B, j1 = (j0 + B < n ? j0 + B : n);
				// 对角块内串行前代
				for (xhqm::size k = j0; k < j1; ++k)
				{
					if (!unit_diag) x[k] = xhqm::div(x[k], L(k, k));
					const type xk = x[k];
					for (xhqm::size i = k + 1; i < j1; ++i)
						x[i] = xhqm::sub(x[i], xhqm::mul(L(i, k), xk));
				}
				// 尾块并行：x[i] -= Σ_{k∈[j0,j1)} L(i,k)·x[k]（各行独立）
				if (j1 < n)
					tensor_backend_active().parallel_for(j1, n, [&](xhqm::size i)
					{
						for (xhqm::size k = j0; k < j1; ++k)
							x[i] = xhqm::sub(x[i], xhqm::mul(L(i, k), x[k]));
					});
			}
			return x;
		}
		// 解上三角 U·x = b（回代）；unit_diag=true 表示 U 对角全 1。直接走 backend.trsv
		// （BLAS f32/f64，CUDA 另支持 i8/i16/i32），不支持则回退块化列向回代：对角块串行、头块按行 parallel_for 并行。
		template<class type>
		xhqm::tensor<1, type> trsv_upper(const xhqm::tensor<2, type>& U, const xhqm::tensor<1, type>& b, bool unit_diag = false)
		{
			const xhqm::size n = b.dimension(0);
			xhqm::tensor<1, type> x(std::array<xhqm::size, 1>{ n }, type(0));
			if (tensor_backend_active().trsv(x.data(), U.data(), b.data(), n, false, unit_diag, dtype_of<type>::value))
				return x;
			for (xhqm::size i = 0; i < n; ++i) x[i] = b[i];
			const xhqm::size B = 256;
			const xhqm::size NB = (n + B - 1) / B;
			for (xhqm::size jb = NB; jb-- > 0;)
			{
				const xhqm::size j0 = jb * B, j1 = (j0 + B < n ? j0 + B : n);
				// 对角块内串行回代（k 从高到低）
				for (xhqm::size k = j1; k-- > j0;)
				{
					if (!unit_diag) x[k] = xhqm::div(x[k], U(k, k));
					const type xk = x[k];
					for (xhqm::size i = j0; i < k; ++i)
						x[i] = xhqm::sub(x[i], xhqm::mul(U(i, k), xk));
				}
				// 头块并行：x[i] -= Σ_{k∈[j0,j1)} U(i,k)·x[k]（各行独立）
				if (j0 > 0)
					tensor_backend_active().parallel_for(0, j0, [&](xhqm::size i)
					{
						for (xhqm::size k = j0; k < j1; ++k)
							x[i] = xhqm::sub(x[i], xhqm::mul(U(i, k), x[k]));
					});
			}
			return x;
		}

		// —— Cholesky 分解：A = L·Lᵀ，对称正定，非正定返回空。直接走 backend.chol
		// （LAPACK/cuSOLVER potrf，f32/f64），不支持则回退块化列向 Cholesky：对角块串行、块下行按行 parallel_for 并行。
		template<class type>
		xhqm::tensor<2, type> chol(const xhqm::tensor<2, type>& A)
		{
			const xhqm::size n = A.dimension(0);
			if (n != A.dimension(1) || n == 0) return xhqm::tensor<2, type>();
			xhqm::tensor<2, type> L(std::array<xhqm::size, 2>{ n, n }, type(0));
			if (tensor_backend_active().chol(L.data(), A.data(), n, dtype_of<type>::value))
				return L;
			for (xhqm::size i = 0; i < n; ++i)
				for (xhqm::size j = 0; j <= i; ++j)
					L(i, j) = A(i, j);
			const xhqm::size B = 256;
			const xhqm::size NB = (n + B - 1) / B;
			for (xhqm::size jb = 0; jb < NB; ++jb)
			{
				const xhqm::size j0 = jb * B, j1 = (j0 + B < n ? j0 + B : n);
				// 块内列串行：对角 + 块内下三角
				for (xhqm::size j = j0; j < j1; ++j)
				{
					type s = L(j, j);
					for (xhqm::size k = 0; k < j; ++k) s = xhqm::sub(s, xhqm::mul(L(j, k), L(j, k)));
					if (s <= type(0)) return xhqm::tensor<2, type>();
					L(j, j) = xhqm::sqrt(s);
					const type ljj = L(j, j);
					for (xhqm::size i = j + 1; i < j1; ++i)
					{
						type t = L(i, j);
						for (xhqm::size k = 0; k < j; ++k) t = xhqm::sub(t, xhqm::mul(L(i, k), L(j, k)));
						L(i, j) = xhqm::div(t, ljj);
					}
				}
				// 块下行并行：每行独立、行内列顺序
				if (j1 < n)
					tensor_backend_active().parallel_for(j1, n, [&](xhqm::size i)
					{
						for (xhqm::size j = j0; j < j1; ++j)
						{
							type t = L(i, j);
							for (xhqm::size k = 0; k < j; ++k) t = xhqm::sub(t, xhqm::mul(L(i, k), L(j, k)));
							L(i, j) = xhqm::div(t, L(j, j));
						}
					});
			}
			return L;
		}

		// —— 沿最后一维的 argmax / argmin（行间并行，返回索引）——
		template<class type, xhqm::size dim>
		xhqm::tensor<dim - 1, xhqm::index> argmax_last(const xhqm::tensor<dim, type>& t)
		{
			std::array<xhqm::size, dim - 1> od{};
			for (xhqm::size k = 0; k < dim - 1; ++k) od[k] = t.dimension(k);
			xhqm::tensor<dim - 1, xhqm::index> out(od, xhqm::index(0));
			if (t.size() == 0) return out;
			const xhqm::size inner = t.dimension(dim - 1);
			const xhqm::size outer = t.size() / inner;
			tensor_backend_active().parallel_for(0, outer, [&](xhqm::size o)
			{
				xhqm::index best = 0;
				for (xhqm::size i = 1; i < inner; ++i)
					if (t.value(o * inner + i) > t.value(o * inner + best)) best = i;
				out.value(o) = best;
			});
			return out;
		}
		template<class type, xhqm::size dim>
		xhqm::tensor<dim - 1, xhqm::index> argmin_last(const xhqm::tensor<dim, type>& t)
		{
			std::array<xhqm::size, dim - 1> od{};
			for (xhqm::size k = 0; k < dim - 1; ++k) od[k] = t.dimension(k);
			xhqm::tensor<dim - 1, xhqm::index> out(od, xhqm::index(0));
			if (t.size() == 0) return out;
			const xhqm::size inner = t.dimension(dim - 1);
			const xhqm::size outer = t.size() / inner;
			tensor_backend_active().parallel_for(0, outer, [&](xhqm::size o)
			{
				xhqm::index best = 0;
				for (xhqm::size i = 1; i < inner; ++i)
					if (t.value(o * inner + i) < t.value(o * inner + best)) best = i;
				out.value(o) = best;
			});
			return out;
		}

		// —— 逻辑归约：all（全真）/ any（存在真）；小规模串行早退，大规模并行 + 原子归并 ——
		template<xhqm::size dim>
		bool all(const xhqm::tensor<dim, bool>& t)
		{
			const xhqm::size n = t.size();
			if (n < XHQM_PARALLEL_THRESHOLD)
			{
				for (xhqm::size i = 0; i < n; ++i) if (!t.value(i)) return false;
				return true;
			}
			std::atomic<bool> acc(true);
			tensor_backend_active().parallel_for(0, n, [&](xhqm::size i) { if (!t.value(i)) acc.store(false, std::memory_order_relaxed); });
			return acc.load(std::memory_order_relaxed);
		}
		template<xhqm::size dim>
		bool any(const xhqm::tensor<dim, bool>& t)
		{
			const xhqm::size n = t.size();
			if (n < XHQM_PARALLEL_THRESHOLD)
			{
				for (xhqm::size i = 0; i < n; ++i) if (t.value(i)) return true;
				return false;
			}
			std::atomic<bool> acc(false);
			tensor_backend_active().parallel_for(0, n, [&](xhqm::size i) { if (t.value(i)) acc.store(true, std::memory_order_relaxed); });
			return acc.load(std::memory_order_relaxed);
		}

		// —— 沿最后一维前缀（scan，行间并行）：结果同形状 ——
		template<class type, xhqm::size dim>
		xhqm::tensor<dim, type> cumsum_last(const xhqm::tensor<dim, type>& t)
		{
			xhqm::tensor<dim, type> out = t;
			if (t.size() == 0) return out;
			const xhqm::size inner = t.dimension(dim - 1);
			const xhqm::size outer = t.size() / inner;
			tensor_backend_active().parallel_for(0, outer, [&](xhqm::size o)
			{
				type acc = type(0);
				for (xhqm::size i = 0; i < inner; ++i) { acc = xhqm::sum(acc, t.value(o * inner + i)); out.value(o * inner + i) = acc; }
			});
			return out;
		}
		template<class type, xhqm::size dim>
		xhqm::tensor<dim, type> cumprod_last(const xhqm::tensor<dim, type>& t)
		{
			xhqm::tensor<dim, type> out = t;
			if (t.size() == 0) return out;
			const xhqm::size inner = t.dimension(dim - 1);
			const xhqm::size outer = t.size() / inner;
			tensor_backend_active().parallel_for(0, outer, [&](xhqm::size o)
			{
				type acc = type(1);
				for (xhqm::size i = 0; i < inner; ++i) { acc = xhqm::mul(acc, t.value(o * inner + i)); out.value(o * inner + i) = acc; }
			});
			return out;
		}

		// —— 沿任意轴前缀（scan，非轴切片并行）：每片沿 axis 顺序扫描 ——
		template<class type, xhqm::size dim>
		xhqm::tensor<dim, type> cumsum(const xhqm::tensor<dim, type>& t, xhqm::size axis)
		{
			xhqm::tensor<dim, type> out = t;
			if (t.size() == 0) return out;
			const xhqm::size ax_len = t.dimension(axis);
			if (ax_len <= 1) return out;
			std::array<xhqm::size, dim> stride{};
			xhqm::size s = 1;
			for (xhqm::size k = dim; k-- > 0;) { stride[k] = s; s *= t.dimension(k); }
			const xhqm::size ax_stride = stride[axis];
			const xhqm::size outer = t.size() / ax_len;
			tensor_backend_active().parallel_for(0, outer, [&](xhqm::size o)
			{
				xhqm::size base = 0, r = o;
				for (xhqm::size k = dim; k-- > 0;)
				{
					if (k == axis) continue;
					const xhqm::size dk = t.dimension(k);
					const xhqm::size ck = r % dk;
					r /= dk;
					base += ck * stride[k];
				}
				type acc = type(0);
				for (xhqm::size a = 0; a < ax_len; ++a)
				{
					acc = xhqm::sum(acc, t.value(base + a * ax_stride));
					out.value(base + a * ax_stride) = acc;
				}
			});
			return out;
		}
		template<class type, xhqm::size dim>
		xhqm::tensor<dim, type> cumprod(const xhqm::tensor<dim, type>& t, xhqm::size axis)
		{
			xhqm::tensor<dim, type> out = t;
			if (t.size() == 0) return out;
			const xhqm::size ax_len = t.dimension(axis);
			if (ax_len <= 1) return out;
			std::array<xhqm::size, dim> stride{};
			xhqm::size s = 1;
			for (xhqm::size k = dim; k-- > 0;) { stride[k] = s; s *= t.dimension(k); }
			const xhqm::size ax_stride = stride[axis];
			const xhqm::size outer = t.size() / ax_len;
			tensor_backend_active().parallel_for(0, outer, [&](xhqm::size o)
			{
				xhqm::size base = 0, r = o;
				for (xhqm::size k = dim; k-- > 0;)
				{
					if (k == axis) continue;
					const xhqm::size dk = t.dimension(k);
					const xhqm::size ck = r % dk;
					r /= dk;
					base += ck * stride[k];
				}
				type acc = type(1);
				for (xhqm::size a = 0; a < ax_len; ++a)
				{
					acc = xhqm::mul(acc, t.value(base + a * ax_stride));
					out.value(base + a * ax_stride) = acc;
				}
			});
			return out;
		}
		template<class type, xhqm::size dim>
		xhqm::tensor<dim, type> cummax_last(const xhqm::tensor<dim, type>& t)
		{
			xhqm::tensor<dim, type> out = t;
			if (t.size() == 0) return out;
			const xhqm::size inner = t.dimension(dim - 1);
			const xhqm::size outer = t.size() / inner;
			tensor_backend_active().parallel_for(0, outer, [&](xhqm::size o)
			{
				type acc = t.value(o * inner);
				for (xhqm::size i = 1; i < inner; ++i) { if (t.value(o * inner + i) > acc) acc = t.value(o * inner + i); out.value(o * inner + i) = acc; }
			});
			return out;
		}
		template<class type, xhqm::size dim>
		xhqm::tensor<dim, type> cummin_last(const xhqm::tensor<dim, type>& t)
		{
			xhqm::tensor<dim, type> out = t;
			if (t.size() == 0) return out;
			const xhqm::size inner = t.dimension(dim - 1);
			const xhqm::size outer = t.size() / inner;
			tensor_backend_active().parallel_for(0, outer, [&](xhqm::size o)
			{
				type acc = t.value(o * inner);
				for (xhqm::size i = 1; i < inner; ++i) { if (t.value(o * inner + i) < acc) acc = t.value(o * inner + i); out.value(o * inner + i) = acc; }
			});
			return out;
		}

		// —— 排序（并行）：升序 / 降序 + 索引 ——
		template<class type>
		xhqm::tensor<1, type> sort(const xhqm::tensor<1, type>& t, bool descend = false)
		{
			const xhqm::size n = t.dimension(0);
			std::vector<type> s(n);
			for (xhqm::size i = 0; i < n; ++i) s[i] = t[i];
			if (descend) std::sort(XHQM_PAR s.begin(), s.end(), [](const type& a, const type& b) { return a > b; });
			else std::sort(XHQM_PAR s.begin(), s.end());
			xhqm::tensor<1, type> out(std::array<xhqm::size, 1>{ n }, type(0));
			for (xhqm::size i = 0; i < n; ++i) out[i] = s[i];
			return out;
		}
		template<class type>
		xhqm::tensor<1, xhqm::index> sort_index(const xhqm::tensor<1, type>& t, bool descend = false)
		{
			const xhqm::size n = t.dimension(0);
			std::vector<xhqm::index> idx(n);
			for (xhqm::size i = 0; i < n; ++i) idx[i] = i;
			if (descend) std::sort(XHQM_PAR idx.begin(), idx.end(), [&](xhqm::index a, xhqm::index b) { return t[a] > t[b]; });
			else std::sort(XHQM_PAR idx.begin(), idx.end(), [&](xhqm::index a, xhqm::index b) { return t[a] < t[b]; });
			xhqm::tensor<1, xhqm::index> out(std::array<xhqm::size, 1>{ n }, xhqm::index(0));
			for (xhqm::size i = 0; i < n; ++i) out[i] = idx[i];
			return out;
		}

		// —— 范数（串行桩）：Frobenius / 1-范数 / ∞-范数 / 谱范数 ——
		// 注：CPU 并行 = CPU 串行（可并行归约，暂未回补）。
		template<class type, xhqm::size dim>
		type norm_fro(const xhqm::tensor<dim, type>& t)
		{
			type s = type(0);
			for (xhqm::size i = 0; i < t.size(); ++i) s = xhqm::sum(s, xhqm::mul(t.value(i), t.value(i)));
			return xhqm::sqrt(s);
		}
		template<class type>
		type norm_one(const xhqm::tensor<2, type>& m)
		{
			type r = type(0);
			for (xhqm::size j = 0; j < m.dimension(1); ++j)
			{
				type s = type(0);
				for (xhqm::size i = 0; i < m.dimension(0); ++i) s = xhqm::sum(s, xhqm::abs(m(i, j)));
				if (j == 0 || s > r) r = s;
			}
			return r;
		}
		template<class type>
		type norm_inf(const xhqm::tensor<2, type>& m)
		{
			type r = type(0);
			for (xhqm::size i = 0; i < m.dimension(0); ++i)
			{
				type s = type(0);
				for (xhqm::size j = 0; j < m.dimension(1); ++j) s = xhqm::sum(s, xhqm::abs(m(i, j)));
				if (i == 0 || s > r) r = s;
			}
			return r;
		}
		template<class type>
		type norm_spectral(const xhqm::tensor<2, type>& m)
		{
			auto sv = svd(m);
			return (sv.S.dimension(0) == 0) ? type(0) : sv.S[0];
		}

		// —— 单位矩阵 / 迹 ——
		// 注：trace 为对角线归约，CPU 并行 = CPU 串行（可并行，暂未回补）。
		template<class type>
		xhqm::tensor<2, type> identity(xhqm::size n)
		{
			xhqm::tensor<2, type> I(std::array<xhqm::size, 2>{ n, n }, type(0));
			for (xhqm::size i = 0; i < n; ++i) I(i, i) = type(1);
			return I;
		}
		template<class type>
		type trace(const xhqm::tensor<2, type>& t)
		{
			const xhqm::size n = (t.dimension(0) < t.dimension(1)) ? t.dimension(0) : t.dimension(1);
			type r = type(0);
			for (xhqm::size i = 0; i < n; ++i) r = xhqm::sum(r, t(i, i));
			return r;
		}

		// —— 逆矩阵（LU + 三角求解，奇异返回空）——
		// 注：CPU 并行 = CPU 串行（经 LU 部分主元，顺序依赖，确实不适合 CPU 并行）。
		template<class type>
		xhqm::tensor<2, type> inv(const xhqm::tensor<2, type>& A)
		{
			const xhqm::size n = A.dimension(0);
			if (n != A.dimension(1) || n == 0) return xhqm::tensor<2, type>();
			auto lr = lu(A);
			if (lr.U.dimension(0) == 0) return xhqm::tensor<2, type>();
			for (xhqm::size i = 0; i < n; ++i)
				if (xhqm::abs(lr.U(i, i)) < xhqm::preci<type>) return xhqm::tensor<2, type>();
			xhqm::tensor<2, type> r(std::array<xhqm::size, 2>{ n, n }, type(0));
			for (xhqm::size j = 0; j < n; ++j)
			{
				xhqm::tensor<1, type> b(std::array<xhqm::size, 1>{ n }, type(0));
				for (xhqm::size i = 0; i < n; ++i) b[i] = lr.P(i, j);
				auto y = trsv_lower(lr.L, b, true);
				auto x = trsv_upper(lr.U, y, false);
				for (xhqm::size i = 0; i < n; ++i) r(i, j) = x[i];
			}
			return r;
		}

		// —— 行最简形（高斯-约当 + 主元交换）——
		// 注：CPU 并行 = CPU 串行（逐行主元依赖，确实不适合 CPU 并行）。
		template<class type>
		xhqm::tensor<2, type> rref(const xhqm::tensor<2, type>& m)
		{
			xhqm::tensor<2, type> r = m;
			const xhqm::size rows = r.dimension(0), cols = r.dimension(1);
			xhqm::size lead = 0;
			for (xhqm::size row = 0; row < rows && lead < cols; ++row)
			{
				xhqm::size i = row;
				while (xhqm::abs(r(i, lead)) < xhqm::preci<type>)
				{
					++i;
					if (i == rows) { i = row; ++lead; if (lead == cols) return r; }
				}
				for (xhqm::size j = 0; j < cols; ++j) std::swap(r(i, j), r(row, j));
				const type pv = r(row, lead);
				for (xhqm::size j = 0; j < cols; ++j) r(row, j) = xhqm::div(r(row, j), pv);
				for (xhqm::size k = 0; k < rows; ++k)
				{
					if (k == row) continue;
					const type f = r(k, lead);
					for (xhqm::size j = 0; j < cols; ++j) r(k, j) = xhqm::sub(r(k, j), xhqm::mul(f, r(row, j)));
				}
				++lead;
			}
			return r;
		}

		// —— 矩阵指数（Taylor 级数 + 缩放平方）——
		// 注：CPU 并行 = CPU 串行（迭代逐次依赖，确实不适合 CPU 并行；内部 matmul 已并行）。
		template<class type>
		xhqm::tensor<2, type> expm(const xhqm::tensor<2, type>& m)
		{
			const xhqm::size n = m.dimension(0);
			if (n != m.dimension(1) || n == 0) return xhqm::tensor<2, type>();
			type norm1 = type(0);
			for (xhqm::size j = 0; j < n; ++j)
			{
				type s = type(0);
				for (xhqm::size i = 0; i < n; ++i) s = xhqm::sum(s, xhqm::abs(m(i, j)));
				if (s > norm1) norm1 = s;
			}
			xhqm::size sq = 0;
			xhqm::tensor<2, type> A = m;
			while (norm1 > type(0.5) && sq < 20)
			{
				for (xhqm::size i = 0; i < n; ++i)
					for (xhqm::size j = 0; j < n; ++j)
						A(i, j) = xhqm::mul(A(i, j), type(0.5));
				norm1 = xhqm::mul(norm1, type(0.5));
				++sq;
			}
			xhqm::tensor<2, type> R = identity<type>(n);
			xhqm::tensor<2, type> term = identity<type>(n);
			for (xhqm::size k = 1; k <= 20; ++k)
			{
				term = matmul(term, A);
				for (xhqm::size i = 0; i < n; ++i)
					for (xhqm::size j = 0; j < n; ++j)
						term(i, j) = xhqm::div(term(i, j), type(k));
				for (xhqm::size i = 0; i < n; ++i)
					for (xhqm::size j = 0; j < n; ++j)
						R(i, j) = xhqm::sum(R(i, j), term(i, j));
			}
			for (xhqm::size t = 0; t < sq; ++t) R = matmul(R, R);
			return R;
		}

		// —— 矩阵平方根（牛顿迭代：X_{k+1} = (X_k + A·X_k^{-1}) / 2）——
		// 注：CPU 并行 = CPU 串行（迭代逐次依赖，确实不适合 CPU 并行；内部 matmul/inv 已并行）。
		template<class type>
		xhqm::tensor<2, type> sqrtm(const xhqm::tensor<2, type>& m, xhqm::size max_iter = 64, const type& tol = type(1e-10))
		{
			const xhqm::size n = m.dimension(0);
			if (n != m.dimension(1) || n == 0) return xhqm::tensor<2, type>();
			xhqm::tensor<2, type> X = m;
			for (xhqm::size it = 0; it < max_iter; ++it)
			{
				auto Xi = inv(X);
				if (Xi.dimension(0) == 0) return xhqm::tensor<2, type>();
				auto mXi = matmul(m, Xi);
				xhqm::tensor<2, type> Xn(std::array<xhqm::size, 2>{ n, n }, type(0));
				type d = type(0);
				for (xhqm::size i = 0; i < n; ++i)
					for (xhqm::size j = 0; j < n; ++j)
					{
						Xn(i, j) = xhqm::mul(xhqm::sum(X(i, j), mXi(i, j)), type(0.5));
						type e = xhqm::abs(xhqm::sub(Xn(i, j), X(i, j)));
						if (e > d) d = e;
					}
				X = Xn;
				if (d < tol) break;
			}
			return X;
		}

		// —— 矩阵对数（逆缩放平方 + Taylor 级数 + 乘回 2^k）——
		// 注：CPU 并行 = CPU 串行（迭代逐次依赖，确实不适合 CPU 并行；内部 matmul/sqrtm 已并行）。
		template<class type>
		xhqm::tensor<2, type> logm(const xhqm::tensor<2, type>& m, xhqm::size max_sqrt = 40, xhqm::size taylor_terms = 20)
		{
			const xhqm::size n = m.dimension(0);
			if (n != m.dimension(1) || n == 0) return xhqm::tensor<2, type>();
			xhqm::tensor<2, type> A = m;
			xhqm::size k = 0;
			while (k < max_sqrt)
			{
				type d = type(0);
				for (xhqm::size i = 0; i < n; ++i)
					for (xhqm::size j = 0; j < n; ++j)
					{
						type e = xhqm::sub(A(i, j), ((i == j) ? type(1) : type(0)));
						d = xhqm::sum(d, xhqm::mul(e, e));
					}
				if (d < type(0.25)) break;
				auto As = sqrtm(A);
				if (As.dimension(0) == 0) break;
				A = As;
				++k;
			}
			xhqm::tensor<2, type> X = A;
			for (xhqm::size i = 0; i < n; ++i) X(i, i) = xhqm::sub(X(i, i), type(1));
			xhqm::tensor<2, type> R(std::array<xhqm::size, 2>{ n, n }, type(0));
			xhqm::tensor<2, type> term = X;
			for (xhqm::size j = 1; j <= taylor_terms; ++j)
			{
				xhqm::tensor<2, type> add = term;
				for (xhqm::size i = 0; i < n; ++i)
					for (xhqm::size jj = 0; jj < n; ++jj)
						add(i, jj) = xhqm::div(term(i, jj), type(j));
				for (xhqm::size i = 0; i < n; ++i)
					for (xhqm::size jj = 0; jj < n; ++jj)
						R(i, jj) = (j % 2 == 1) ? xhqm::sum(R(i, jj), add(i, jj)) : xhqm::sub(R(i, jj), add(i, jj));
				term = matmul(term, X);
			}
			for (xhqm::size t = 0; t < k; ++t)
				for (xhqm::size i = 0; i < n; ++i)
					for (xhqm::size j = 0; j < n; ++j)
						R(i, j) = xhqm::mul(R(i, j), type(2));
			return R;
		}

	}
}

#endif
