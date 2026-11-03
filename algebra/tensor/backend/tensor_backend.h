#pragma once
#ifndef XHQM_TENSOR_BACKEND_IF
#define XHQM_TENSOR_BACKEND_IF

// 张量计算后端抽象：把「并行调度 + 内存搬运 + 同步」收敛到一个虚接口，
// none / cpu / gpu / net 各为独立实现，运行时经 tensor_backend_active() 调度。
#include "backend.h"
#include "../../../head.h"
#include "../../ftype.h"

#include <cstddef>
#include <functional>
#include <cmath>

// CPU 并行的回退阈值：数据量 < 该值时并行路径退回串行，避免线程创建/调度开销。
// CPU 并行（parallel_for / std::thread）统一以此为基准做回退判断。
#ifndef XHQM_PARALLEL_THRESHOLD
#define XHQM_PARALLEL_THRESHOLD 4096
#endif

namespace xhqm
{
	namespace tensor_base
	{
		// 元素类型标识：加速后端只对精确标量类型开 kernel，
		// 其余（复数/对偶数/四元数/自定义）一律回退 CPU，避免按 sizeof 误判
		// （如 complex<float>、dualnum<float> 均为 8 字节，会被 sizeof 当成 double）
		enum class dtype : int
		{
			unknown = 0,
			f16 = 1,
			f32 = 2,
			f64 = 3,
			fld = 4,
			i8 = 5,
			i16 = 6,
			i32 = 7,
			i64 = 8,
			boolean = 9,   // bool 张量：GF(2) 语义（sum/sub=XOR，mul/div=AND）
		};

		// 编译期把元素类型映射到 dtype；未特化的类型一律 unknown（回退 CPU）
		template<class type> struct dtype_of { static constexpr dtype value = dtype::unknown; };
		template<> struct dtype_of<xhqm::f16> { static constexpr dtype value = dtype::f16; };
		template<> struct dtype_of<xhqm::f32> { static constexpr dtype value = dtype::f32; };
		template<> struct dtype_of<xhqm::f64> { static constexpr dtype value = dtype::f64; };
		template<> struct dtype_of<xhqm::fld> { static constexpr dtype value = dtype::fld; };
		template<> struct dtype_of<xhqm::i8>  { static constexpr dtype value = dtype::i8; };
		template<> struct dtype_of<xhqm::i16> { static constexpr dtype value = dtype::i16; };
		template<> struct dtype_of<xhqm::i32> { static constexpr dtype value = dtype::i32; };
		template<> struct dtype_of<xhqm::i64> { static constexpr dtype value = dtype::i64; };
		template<> struct dtype_of<bool>     { static constexpr dtype value = dtype::boolean; };

		// 一元算子编码
		enum unary_op : int
		{
			unary_relu = 0,
			unary_leaky_relu = 1,
			unary_sigmoid = 2,
			unary_gelu = 3,
			unary_exp = 4,
			unary_log = 5,
			unary_tanh = 6,
			unary_sqrt = 7,
			unary_sin = 8,
			unary_cos = 9,
			unary_tan = 10,
			unary_asin = 11,
			unary_acos = 12,
			unary_atan = 13,
			unary_sinh = 14,
			unary_cosh = 15,
			unary_asinh = 16,
			unary_acosh = 17,
			unary_atanh = 18,
			unary_log10 = 19,
			unary_log2 = 20,
			unary_floor = 21,
			unary_ceil = 22,
			unary_round = 23,
			unary_trunc = 24,
			unary_expm1 = 25,
			unary_log1p = 26,
			unary_erf = 27,
			unary_erfc = 28,
			unary_tgamma = 29,
			unary_lgamma = 30,
			unary_abs = 31,
			unary_sign = 32,
			unary_exp2 = 33,
			unary_cbrt = 34,
			unary_rsqrt = 35,
			unary_recip = 36,
			unary_neg = 37,
			unary_square = 38,
			unary_isnan = 39,     // 返回 type(1)/type(0)
			unary_isinf = 40,
			unary_isfinite = 41,
			unary_silu = 42,     // x·σ(x)，Swish
		};

		// 归约算子编码：src[outer × inner] → dst[outer]
		enum reduce_op : int
		{
			reduce_op_sum = 0,
			reduce_op_max = 1,
			reduce_op_min = 2,
			reduce_op_prod = 3,
		};

		// 二元逐元素算子编码：dst[i] = f(a[i], b[i])
		enum elemwise_op : int
		{
			elem_add = 0,
			elem_sub = 1,
			elem_mul = 2,
			elem_div = 3,
			elem_atan2 = 4,   // atan2(a, b)
			elem_hypot = 5,   // hypot(a, b) = sqrt(a²+b²)
			elem_pow = 6,     // pow(a, b)
			elem_mod = 7,     // a - floor(a/b)*b（结果符号同 b，MATLAB mod）
			elem_rem = 8,     // fmod(a, b)（结果符号同 a，MATLAB rem）
		};

		// 二元逐元素求值（模板；供后端桩与回退路径共用，统一用 xhqm:: 泛型以支持各类型）
		template<class type>
		inline type elemwise_eval(type a, type b, int op)
		{
			switch (op)
			{
			case elem_add: return xhqm::sum(a, b);
			case elem_sub: return xhqm::sub(a, b);
			case elem_mul: return xhqm::mul(a, b);
			case elem_div: return xhqm::div(a, b);
			case elem_atan2: return type(std::atan2(a, b));
			case elem_hypot: return type(std::hypot(a, b));
			case elem_pow: return type(std::pow(a, b));
			case elem_mod: return xhqm::sub(a, xhqm::mul(type(std::floor(xhqm::div(a, b))), b));
			case elem_rem: return type(std::fmod(a, b));
			default: return a;
			}
		}

		// 一元逐元素求值（模板；供后端桩与回退路径共用，统一用 xhqm:: 泛型以支持各类型）
		template<class type>
		inline type unary_eval(type v, int op)
		{
			switch (op)
			{
			case unary_relu: return xhqm::most(v, type(0)) ? v : type(0);
			case unary_leaky_relu: return xhqm::most(v, type(0)) ? v : xhqm::mul(type(0.01), v);
			case unary_sigmoid: return xhqm::div(type(1), xhqm::sum(type(1), type(std::exp(-v))));
			case unary_gelu: {
				const type c = type(0.7978845608028654);
				type x3 = xhqm::mul(xhqm::mul(v, v), v);
				type inner = xhqm::sum(v, xhqm::mul(type(0.044715), x3));
				type t = type(std::tanh(xhqm::mul(c, inner)));
				return xhqm::mul(xhqm::mul(type(0.5), v), xhqm::sum(type(1), t));
			}
			case unary_exp: return type(std::exp(v));
			case unary_log: return type(std::log(v));
			case unary_tanh: return type(std::tanh(v));
			case unary_sqrt: return type(std::sqrt(v));
			case unary_sin: return type(std::sin(v));
			case unary_cos: return type(std::cos(v));
			case unary_tan: return type(std::tan(v));
			case unary_asin: return type(std::asin(v));
			case unary_acos: return type(std::acos(v));
			case unary_atan: return type(std::atan(v));
			case unary_sinh: return type(std::sinh(v));
			case unary_cosh: return type(std::cosh(v));
			case unary_asinh: return type(std::asinh(v));
			case unary_acosh: return type(std::acosh(v));
			case unary_atanh: return type(std::atanh(v));
			case unary_log10: return type(std::log10(v));
			case unary_log2: return type(std::log2(v));
			case unary_floor: return type(std::floor(v));
			case unary_ceil: return type(std::ceil(v));
			case unary_round: return type(std::round(v));
			case unary_trunc: return type(std::trunc(v));
			case unary_expm1: return type(std::expm1(v));
			case unary_log1p: return type(std::log1p(v));
			case unary_erf: return type(std::erf(v));
			case unary_erfc: return type(std::erfc(v));
			case unary_tgamma: return type(std::tgamma(v));
			case unary_lgamma: return type(std::lgamma(v));
			case unary_abs: return type(std::fabs(v));
			case unary_sign: return xhqm::most(v, type(0)) ? type(1) : (xhqm::less(v, type(0)) ? type(-1) : type(0));
			case unary_exp2: return type(std::exp2(v));
			case unary_cbrt: return type(std::cbrt(v));
			case unary_rsqrt: return xhqm::div(type(1), type(std::sqrt(v)));
			case unary_recip: return xhqm::div(type(1), v);
			case unary_neg: return xhqm::sub(type(0), v);
			case unary_square: return xhqm::mul(v, v);
			case unary_isnan: return type(std::isnan(v) ? 1 : 0);
			case unary_isinf: return type(std::isinf(v) ? 1 : 0);
			case unary_isfinite: return type(std::isfinite(v) ? 1 : 0);
			case unary_silu: return xhqm::div(v, xhqm::sum(type(1), type(std::exp(-v))));
			default: return v;
			}
		}

		// 反向逐元素累加算子编码：dst[i] += f(a[i], b[i])
		enum grad_op : int
		{
			grad_relu = 0,        // dst += a * (b > 0)
			grad_leaky_relu = 1,  // dst += a * (b > 0 ? 1 : 0.01)
			grad_sigmoid = 2,     // dst += a * b * (1 - b)
		};

		// 反向逐元素求值（模板；统一用 xhqm:: 泛型）
		template<class type>
		inline type grad_eval(type a, type b, int op)
		{
			switch (op)
			{
			case grad_relu: return xhqm::most(b, type(0)) ? a : type(0);
			case grad_leaky_relu: return xhqm::most(b, type(0)) ? a : xhqm::mul(type(0.01), a);
			case grad_sigmoid: return xhqm::mul(xhqm::mul(a, b), xhqm::sub(type(1), b));
			default: return type(0);
			}
		}

		// 归约求值（标量，op 见 reduce_op）：与 linalg.h/simd.h 归约尾段共用单一份语义
		template<class type>
		inline type reduce_eval(type a, type b, int op)
		{
			switch (op)
			{
			case reduce_op_sum: return xhqm::sum(a, b);
			case reduce_op_max: return xhqm::max(a, b);
			case reduce_op_min: return xhqm::min(a, b);
			case reduce_op_prod: return xhqm::mul(a, b);
			default: return a;
			}
		}

		// 张量计算后端抽象接口（仿 fantasy::render_backend）
		class xhqm_io tensor_backend
		{
		public:
			virtual ~tensor_backend() = default;

			virtual const char* name() const noexcept = 0;
			virtual device kind() const noexcept = 0;
			virtual bool supports_memory_transfer() const noexcept = 0;

			// 并行调度：把 [begin, end) 切成若干块并行执行，
			// 块内串行调用 body(i)；body 是类型安全的元素操作，后端只负责调度。
			virtual void parallel_for(std::size_t begin, std::size_t end,
				const std::function<void(std::size_t)>& body) = 0;

			// 逐元素二元运算：dst[i] = a[i] OP b[i]（op: 0=+ 1=- 2=* 3=/）
			// 按 dtype 区分元素类型，支持与否由后端实现决定，其余返回 false，
			// 调用方回退 parallel_for 的 CPU 路径。
			virtual bool elemwise(void* dst, const void* a, const void* b,
				std::size_t count, dtype dt, int op) noexcept { return false; }

			// 条件选择：dst[i] = cond[i] ? a[i] : b[i]（cond 为 bool 字节 0/1，与 a/b 同 count）
			// 支持与否由后端实现决定，其余返回 false，调用方回退 parallel_for。
			virtual bool select(void* dst, const void* cond, const void* a, const void* b,
				std::size_t count, dtype dt) noexcept { return false; }

			// 矩阵乘：C[m×n] = A[m×k] × B[k×n]（行优先）
			// 按 dtype 区分元素类型，支持与否由后端实现决定，其余返回 false，调用方回退 CPU。
			virtual bool matmul(void* c, const void* a, const void* b,
				std::size_t m, std::size_t n, std::size_t k, dtype dt) noexcept { return false; }

			// 沿最后一维归约：src[outer × inner] → dst[outer]（op: 0=sum 1=max 2=min 3=prod）
			// 支持与否由后端实现决定，其余返回 false，调用方回退 CPU 通用路径。
			virtual bool reduce(void* dst, const void* src,
				std::size_t outer, std::size_t inner, dtype dt, int op) noexcept { return false; }

			// 一元逐元素：dst[i] = f(a[i])（op 见 unary_op）
			// 支持与否由后端实现决定，其余返回 false，调用方回退 parallel_for。
			virtual bool unary(void* dst, const void* a,
				std::size_t count, dtype dt, int op) noexcept { return false; }

			// 转置：dst[cols×rows] = src[rows×cols]^T（行优先）
			// 支持与否由后端实现决定，其余返回 false，调用方回退 CPU 串行。
			virtual bool transpose(void* dst, const void* src,
				std::size_t rows, std::size_t cols, dtype dt) noexcept { return false; }

			// 反向逐元素累加：dst[i] += f(a[i], b[i])（op 见 grad_op）
			// 支持与否由后端实现决定，其余返回 false，调用方回退 parallel_for。
			virtual bool grad_accum(void* dst, const void* a, const void* b,
				std::size_t count, dtype dt, int op) noexcept { return false; }

			// 稀疏矩阵 × 稠密向量（COO）：y[row[k]] += val[k] * x[col[k]]，k∈[0,nnz)。
			// y 需预清零（调用方保证）；row/col 为 xhqm::index 数组，val 按 dtype。
			// f16/f32/f64 与 i8/i16/i32/i64/bool 走 kernel（bool 为 GF(2) XOR/AND），其余返回 false 回退串行。
			virtual bool spmv(void* y, const void* x, const xhqm::index* row, const xhqm::index* col,
				const void* val, std::size_t nnz, std::size_t rows, std::size_t cols, dtype dt) noexcept { return false; }

			// 2D 池化（NCHW）：dst[n,c,oh,ow] = pool(src 窗口)，op: 0=max 1=avg，padding 越界跳过（avg 除以有效数）
			// 支持与否由后端实现决定，其余返回 false，调用方回退 parallel_for。
			virtual bool pool2d(void* dst, const void* src,
				std::size_t n, std::size_t c, std::size_t h, std::size_t w,
				std::size_t oh, std::size_t ow, std::size_t k, std::size_t stride, std::size_t pad,
				dtype dt, int op) noexcept { return false; }

			// 2D 上采样（NCHW）：dst[n,c,oh,ow] 由 src 插值，op: 0=nearest 1=bilinear(align_corners=false)
			// 支持与否由后端实现决定，其余返回 false，调用方回退 parallel_for。
			virtual bool upsample2d(void* dst, const void* src,
				std::size_t n, std::size_t c, std::size_t h, std::size_t w,
				std::size_t oh, std::size_t ow, dtype dt, int op) noexcept { return false; }

			// dropout（inverted）：dst[i] = src[i] * mask / (1-p)，mask_i ~ Bernoulli(1-p)（保留概率 1-p）
			// seed 决定每元素 RNG 状态（与 CPU rand_seed_at 同算法，可复现）。
			virtual bool dropout(void* dst, const void* src, std::size_t count,
				float p, unsigned int seed, dtype dt) noexcept { return false; }

			// 3D 卷积（NCDHW，含 groups/bias）：dst[n,co,od,oh,ow] = bias[co] + Σ src[n,ci,id,ih,iw]·w[co,ci,kd,kh,kw]
			// 1D/2D 由调用方补齐 size-1 维复用本槽位；f16/f32/f64 走 BLAS，整型走 GEMM，
			// 不支持返回 false 回退 conv_nd。
			virtual bool conv3d(void* dst, const void* src, const void* weight, const void* bias,
				std::size_t n, std::size_t cin, std::size_t cout,
				std::size_t id, std::size_t ih, std::size_t iw,
				std::size_t kd, std::size_t kh, std::size_t kw,
				std::size_t stride_d, std::size_t stride_h, std::size_t stride_w,
				std::size_t pad_d, std::size_t pad_h, std::size_t pad_w,
				std::size_t dil_d, std::size_t dil_h, std::size_t dil_w,
				std::size_t groups, dtype dt) noexcept { return false; }

			// 4D 卷积（N,C,D0,D1,D2,D3，含 groups/bias）：f32/f64 走 BLAS，整型走 GEMM，
			// 不支持返回 false 回退 conv_nd。
			virtual bool conv4d(void* dst, const void* src, const void* weight, const void* bias,
				std::size_t n, std::size_t cin, std::size_t cout,
				std::size_t i0, std::size_t i1, std::size_t i2, std::size_t i3,
				std::size_t k0, std::size_t k1, std::size_t k2, std::size_t k3,
				std::size_t s0, std::size_t s1, std::size_t s2, std::size_t s3,
				std::size_t p0, std::size_t p1, std::size_t p2, std::size_t p3,
				std::size_t d0, std::size_t d1, std::size_t d2, std::size_t d3,
				std::size_t groups, dtype dt) noexcept { return false; }

			// 三角求解：x = solve(tri, b)。lower=true 解下三角 tri·x=b，否则上三角 tri·x=b。
			// tri n×n 行优先，b/x 各 n 长；x 需预分配（可 == b 原地）。unit_diag 表示对角全 1。
			// 支持与否由后端实现决定（BLAS f32/f64，CUDA 另支持 i8/i16/i32），不支持返回 false 回退串行前代/回代。
			virtual bool trsv(void* x, const void* tri, const void* b,
				std::size_t n, bool lower, bool unit_diag, dtype dt) noexcept { return false; }

			// Cholesky 分解：A（n×n 对称正定，行优先）→ L（n×n 下三角，行优先，含对角）。
			// 仅 f32/f64 走 LAPACK（potrf），非正定返回 false 回退串行。
			virtual bool chol(void* L, const void* A, std::size_t n, dtype dt) noexcept { return false; }

			// 内存搬运（cpu 为 no-op；gpu 桩仅声明能力、数据仍留主机内存）
			virtual void upload(void* data, std::size_t bytes) = 0;
			virtual void download(void* data, std::size_t bytes) = 0;
			virtual void synchronize() = 0;
		};

		// 运行时后端选择与调度
		xhqm_io tensor_backend& tensor_backend_active();
		xhqm_io void tensor_backend_select(device kind);
	}
}

#endif
