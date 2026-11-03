#include "tensor_backend_cpu.h"
// SIMD 内核为 x86 (AVX2) 专属，仅当 TENSOR_WITH_SIMD 开启时包含；
// 否则走标量/线程路径，避免在 ARM/macOS 上引入 immintrin.h 而编译失败。
#ifdef TENSOR_WITH_SIMD
#include "tensor_backend_simd.h"
#endif
#ifdef TENSOR_WITH_BLAS
#include <cblas.h>
// scipy-openblas32 的 CBLAS/LAPACKE 符号带 scipy_ 前缀；标准 OpenBLAS/MKL 不带。
// 用宏统一为 cblas_ / LAPACKE_ 前缀调用（换标准 OpenBLAS 时去掉这几行即可）。
// 注：不 include <lapacke.h>（会连带 <lapack.h>，其 Fortran complex 与 C++ std::complex 冲突）；
// 这里只声明 potrf 的 LAPACKE 原型（LP64，尺寸 int）。
#  ifndef OPENBLAS_STANDARD
#    define cblas_sgemm scipy_cblas_sgemm
#    define cblas_dgemm scipy_cblas_dgemm
#    define cblas_strsv scipy_cblas_strsv
#    define cblas_dtrsv scipy_cblas_dtrsv
#  endif
extern "C" {
	int scipy_LAPACKE_spotrf(int matrix_layout, char uplo, int n, float* a, int lda);
	int scipy_LAPACKE_dpotrf(int matrix_layout, char uplo, int n, double* a, int lda);
}
#  ifndef OPENBLAS_STANDARD
#    define LAPACKE_spotrf scipy_LAPACKE_spotrf
#    define LAPACKE_dpotrf scipy_LAPACKE_dpotrf
#  endif
constexpr int LAPACK_ROW_MAJOR = 101;
#endif

#include <thread>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <type_traits>

namespace xhqm
{
	namespace tensor_base
	{
		namespace
		{
#ifdef TENSOR_WITH_BLAS
			// im2col + GEMM 卷积（统一 3D，1D/2D 由调用方补齐 size-1 维复用）。
			// 仅 f32/f64；col 过大（>16M 元素）回退直接卷积（conv_nd 桩）。
			template<class T>
			bool conv3d_im2col(void* dst, const void* src, const void* weight, const void* bias,
				std::size_t n, std::size_t cin, std::size_t cout,
				std::size_t id, std::size_t ih, std::size_t iw,
				std::size_t kd, std::size_t kh, std::size_t kw,
				std::size_t stride_d, std::size_t stride_h, std::size_t stride_w,
				std::size_t pad_d, std::size_t pad_h, std::size_t pad_w,
				std::size_t dil_d, std::size_t dil_h, std::size_t dil_w,
				std::size_t groups)
			{
				if (cin % groups != 0 || cout % groups != 0) return false;
				const std::size_t od = (id + 2 * pad_d - dil_d * (kd - 1) - 1) / stride_d + 1;
				const std::size_t oh = (ih + 2 * pad_h - dil_h * (kh - 1) - 1) / stride_h + 1;
				const std::size_t ow = (iw + 2 * pad_w - dil_w * (kw - 1) - 1) / stride_w + 1;
				const std::size_t M = n * od * oh * ow;
				const std::size_t cin_g = cin / groups, cout_g = cout / groups;
				const std::size_t K = cin_g * kd * kh * kw;
				if (M == 0 || K == 0) return true;
				if (K * M > (std::size_t(1) << 24)) return false;  // >16M 元素回退

				const T* S = (const T*)src; const T* W = (const T*)weight;
				const T* B = bias ? (const T*)bias : nullptr; T* D = (T*)dst;
				std::vector<T> col(K * M), outbuf(cout_g * M);

				for (std::size_t g = 0; g < groups; ++g)
				{
					const T* Sg = S + g * cin_g * id * ih * iw;
					// im2col：col[kk, m]，kk=(ci,kd,kh,kw)，m=(nn,od,oh,ow)
					for (std::size_t m = 0; m < M; ++m)
					{
						std::size_t r = m;
						const std::size_t ow_i = r % ow; r /= ow;
						const std::size_t oh_i = r % oh; r /= oh;
						const std::size_t od_i = r % od; r /= od;
						const std::size_t nn = r;
						for (std::size_t ci = 0; ci < cin_g; ++ci)
							for (std::size_t kd_i = 0; kd_i < kd; ++kd_i)
								for (std::size_t kh_i = 0; kh_i < kh; ++kh_i)
									for (std::size_t kw_i = 0; kw_i < kw; ++kw_i)
									{
										const std::int64_t i_d = (std::int64_t)od_i * stride_d - (std::int64_t)pad_d + (std::int64_t)(kd_i * dil_d);
										const std::int64_t i_h = (std::int64_t)oh_i * stride_h - (std::int64_t)pad_h + (std::int64_t)(kh_i * dil_h);
										const std::int64_t i_w = (std::int64_t)ow_i * stride_w - (std::int64_t)pad_w + (std::int64_t)(kw_i * dil_w);
										const std::size_t kk = ((ci * kd + kd_i) * kh + kh_i) * kw + kw_i;
										T val = T(0);
										if (i_d >= 0 && i_d < (std::int64_t)id && i_h >= 0 && i_h < (std::int64_t)ih && i_w >= 0 && i_w < (std::int64_t)iw)
											val = Sg[((nn * cin_g + ci) * id + (std::size_t)i_d) * ih * iw + (std::size_t)i_h * iw + (std::size_t)i_w];
										col[kk * M + m] = val;
									}
					}
					// GEMM：out(cout_g × M) = W_g(cout_g × K) @ col(K × M)
					if constexpr (std::is_same_v<T, float>)
						cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans, (int)cout_g, (int)M, (int)K,
							1.0f, W + g * cout_g * K, (int)K, col.data(), (int)M, 0.0f, outbuf.data(), (int)M);
					else
						cblas_dgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans, (int)cout_g, (int)M, (int)K,
							1.0, W + g * cout_g * K, (int)K, col.data(), (int)M, 0.0, outbuf.data(), (int)M);
					// 写回（NCDHW 布局，按 m 解码散射 + bias）
					for (std::size_t m = 0; m < M; ++m)
					{
						std::size_t r = m;
						const std::size_t ow_i = r % ow; r /= ow;
						const std::size_t oh_i = r % oh; r /= oh;
						const std::size_t od_i = r % od; r /= od;
						const std::size_t nn = r;
						for (std::size_t co = 0; co < cout_g; ++co)
						{
							const T bv = B ? B[g * cout_g + co] : T(0);
							D[(((nn * cout + (g * cout_g + co)) * od + od_i) * oh + oh_i) * ow + ow_i] = outbuf[co * M + m] + bv;
						}
					}
				}
				return true;
			}
#endif

			// —— 通用 SpMV（f16/f32/f64/i8/i16/i32/i64）：并行部分和 + 归并，经 xhqm::sum/mul 兼容各类型；
			// 每线程维护独立部分和规避 scatter 写同一 y 行的数据竞争 ——
			template<class T>
			void spmv_arith(void* y, const void* x, const xhqm::index* row, const xhqm::index* col,
				const void* val, std::size_t nnz, std::size_t rows)
			{
				T* Y = (T*)y; const T* X = (const T*)x; const T* V = (const T*)val;
				unsigned hw = std::thread::hardware_concurrency();
				if (hw == 0) hw = 1;
				if (nnz < XHQM_PARALLEL_THRESHOLD || hw == 1)
				{
					for (std::size_t k = 0; k < nnz; ++k) Y[row[k]] = xhqm::sum(Y[row[k]], xhqm::mul(V[k], X[col[k]]));
					return;
				}
				const std::size_t nt = (hw > 32) ? 32 : hw;
				std::vector<std::vector<T>> partial(nt, std::vector<T>(rows, T(0)));
				std::vector<std::thread> pool; pool.reserve(nt);
				for (std::size_t t = 0; t < nt; ++t)
				{
					const std::size_t b = t * nnz / nt, e = (t + 1) * nnz / nt;
					pool.emplace_back([&, t, b, e]() {
						T* p = partial[t].data();
						for (std::size_t k = b; k < e; ++k) p[row[k]] = xhqm::sum(p[row[k]], xhqm::mul(V[k], X[col[k]]));
					});
				}
				for (auto& th : pool) th.join();
				for (std::size_t r = 0; r < rows; ++r)
				{
					T acc = T(0);
					for (std::size_t t = 0; t < nt; ++t) acc = xhqm::sum(acc, partial[t][r]);
					Y[r] = xhqm::sum(Y[r], acc);
				}
			}

			// —— bool SpMV（GF(2)：sum=XOR、mul=AND）；unsigned char 字节访问规避 strict-aliasing ——
			inline void spmv_bool(void* y, const void* x, const xhqm::index* row, const xhqm::index* col,
				const void* val, std::size_t nnz, std::size_t rows)
			{
				unsigned char* Y = (unsigned char*)y; const unsigned char* X = (const unsigned char*)x; const unsigned char* V = (const unsigned char*)val;
				unsigned hw = std::thread::hardware_concurrency();
				if (hw == 0) hw = 1;
				if (nnz < XHQM_PARALLEL_THRESHOLD || hw == 1)
				{
					for (std::size_t k = 0; k < nnz; ++k) Y[row[k]] ^= (unsigned char)(V[k] & X[col[k]]);
					return;
				}
				const std::size_t nt = (hw > 32) ? 32 : hw;
				std::vector<std::vector<unsigned char>> partial(nt, std::vector<unsigned char>(rows, 0));
				std::vector<std::thread> pool; pool.reserve(nt);
				for (std::size_t t = 0; t < nt; ++t)
				{
					const std::size_t b = t * nnz / nt, e = (t + 1) * nnz / nt;
					pool.emplace_back([&, t, b, e]() {
						unsigned char* p = partial[t].data();
						for (std::size_t k = b; k < e; ++k) p[row[k]] ^= (unsigned char)(V[k] & X[col[k]]);
					});
				}
				for (auto& th : pool) th.join();
				for (std::size_t r = 0; r < rows; ++r)
				{
					unsigned char acc = 0;
					for (std::size_t t = 0; t < nt; ++t) acc ^= partial[t][r];
					Y[r] ^= acc;
				}
			}

#ifdef TENSOR_WITH_SIMD
			// —— 整型 conv3d：im2col + 整型 GEMM（SIMD 单行内核，i16/i32/bool）——
			// bool 以 unsigned char 承载（GF(2)：XOR/AND）；bias 对 bool 用 XOR 叠加，整型用 +。
			template<class T>
			bool conv3d_im2col_int(void* dst, const void* src, const void* weight, const void* bias,
				std::size_t n, std::size_t cin, std::size_t cout,
				std::size_t id, std::size_t ih, std::size_t iw,
				std::size_t kd, std::size_t kh, std::size_t kw,
				std::size_t stride_d, std::size_t stride_h, std::size_t stride_w,
				std::size_t pad_d, std::size_t pad_h, std::size_t pad_w,
				std::size_t dil_d, std::size_t dil_h, std::size_t dil_w,
				std::size_t groups)
			{
				if (cin % groups != 0 || cout % groups != 0) return false;
				const std::size_t od = (id + 2 * pad_d - dil_d * (kd - 1) - 1) / stride_d + 1;
				const std::size_t oh = (ih + 2 * pad_h - dil_h * (kh - 1) - 1) / stride_h + 1;
				const std::size_t ow = (iw + 2 * pad_w - dil_w * (kw - 1) - 1) / stride_w + 1;
				const std::size_t M = n * od * oh * ow;
				const std::size_t cin_g = cin / groups, cout_g = cout / groups;
				const std::size_t K = cin_g * kd * kh * kw;
				if (M == 0 || K == 0) return true;
				if (K * M > (std::size_t(1) << 24)) return false;

				const T* S = (const T*)src; const T* W = (const T*)weight;
				const T* B = bias ? (const T*)bias : nullptr; T* D = (T*)dst;
				std::vector<T> col(K * M), outbuf(cout_g * M);

				for (std::size_t g = 0; g < groups; ++g)
				{
					const T* Sg = S + g * cin_g * id * ih * iw;
					for (std::size_t m = 0; m < M; ++m)
					{
						std::size_t r = m;
						const std::size_t ow_i = r % ow; r /= ow;
						const std::size_t oh_i = r % oh; r /= oh;
						const std::size_t od_i = r % od; r /= od;
						const std::size_t nn = r;
						for (std::size_t ci = 0; ci < cin_g; ++ci)
							for (std::size_t kd_i = 0; kd_i < kd; ++kd_i)
								for (std::size_t kh_i = 0; kh_i < kh; ++kh_i)
									for (std::size_t kw_i = 0; kw_i < kw; ++kw_i)
									{
										const std::int64_t i_d = (std::int64_t)od_i * stride_d - (std::int64_t)pad_d + (std::int64_t)(kd_i * dil_d);
										const std::int64_t i_h = (std::int64_t)oh_i * stride_h - (std::int64_t)pad_h + (std::int64_t)(kh_i * dil_h);
										const std::int64_t i_w = (std::int64_t)ow_i * stride_w - (std::int64_t)pad_w + (std::int64_t)(kw_i * dil_w);
										const std::size_t kk = ((ci * kd + kd_i) * kh + kh_i) * kw + kw_i;
										T val = T(0);
										if (i_d >= 0 && i_d < (std::int64_t)id && i_h >= 0 && i_h < (std::int64_t)ih && i_w >= 0 && i_w < (std::int64_t)iw)
											val = Sg[((nn * cin_g + ci) * id + (std::size_t)i_d) * ih * iw + (std::size_t)i_h * iw + (std::size_t)i_w];
										col[kk * M + m] = val;
									}
					}
					// 例外（约定外）：conv 整型 GEMM 并行按输出行切（粒度即行），不设 4096 阈值，
					// 仅单核回退；线程数封顶 32 控内存与调度开销。
					const unsigned hwc = std::thread::hardware_concurrency();
					const std::size_t nth = (hwc == 0 || hwc == 1) ? 1 : (hwc > 32 ? 32 : hwc);
					auto gemm_row = [&](std::size_t co) {
						if constexpr (std::is_same_v<T, xhqm::i16>)
							simd::matmul_row_i16(outbuf.data() + co * M, W + g * cout_g * K + co * K, col.data(), M, K);
						else if constexpr (std::is_same_v<T, xhqm::i32>)
							simd::matmul_row_i32(outbuf.data() + co * M, W + g * cout_g * K + co * K, col.data(), M, K);
						else
							simd::matmul_row_bool(outbuf.data() + co * M, W + g * cout_g * K + co * K, col.data(), M, K);
					};
					if (nth == 1)
					{
						for (std::size_t co = 0; co < cout_g; ++co) gemm_row(co);
					}
					else
					{
						std::vector<std::thread> pool; pool.reserve(nth);
						for (std::size_t t = 0; t < nth; ++t)
						{
							const std::size_t b = t * cout_g / nth, e = (t + 1) * cout_g / nth;
							if (b >= e) continue;
							pool.emplace_back([&, b, e]() { for (std::size_t co = b; co < e; ++co) gemm_row(co); });
						}
						for (auto& th : pool) th.join();
					}
					for (std::size_t m = 0; m < M; ++m)
					{
						std::size_t r = m;
						const std::size_t ow_i = r % ow; r /= ow;
						const std::size_t oh_i = r % oh; r /= oh;
						const std::size_t od_i = r % od; r /= od;
						const std::size_t nn = r;
						for (std::size_t co = 0; co < cout_g; ++co)
						{
							const T bv = B ? B[g * cout_g + co] : T(0);
							T ov = outbuf[co * M + m];
							if constexpr (std::is_same_v<T, unsigned char>) ov = (unsigned char)(ov ^ bv);
							else ov = (T)(ov + bv);
							D[(((nn * cout + (g * cout_g + co)) * od + od_i) * oh + oh_i) * ow + ow_i] = ov;
						}
					}
				}
				return true;
			}
#endif

#ifdef TENSOR_WITH_BLAS
			// —— 4D 卷积（N,C,D0,D1,D2,D3）：im2col + GEMM（BLAS，f32/f64）——
			template<class T>
			bool conv4d_im2col(void* dst, const void* src, const void* weight, const void* bias,
				std::size_t n, std::size_t cin, std::size_t cout,
				std::size_t i0, std::size_t i1, std::size_t i2, std::size_t i3,
				std::size_t k0, std::size_t k1, std::size_t k2, std::size_t k3,
				std::size_t s0, std::size_t s1, std::size_t s2, std::size_t s3,
				std::size_t p0, std::size_t p1, std::size_t p2, std::size_t p3,
				std::size_t d0, std::size_t d1, std::size_t d2, std::size_t d3,
				std::size_t groups)
			{
				if (cin % groups != 0 || cout % groups != 0) return false;
				const std::size_t o0 = (i0 + 2 * p0 - d0 * (k0 - 1) - 1) / s0 + 1;
				const std::size_t o1 = (i1 + 2 * p1 - d1 * (k1 - 1) - 1) / s1 + 1;
				const std::size_t o2 = (i2 + 2 * p2 - d2 * (k2 - 1) - 1) / s2 + 1;
				const std::size_t o3 = (i3 + 2 * p3 - d3 * (k3 - 1) - 1) / s3 + 1;
				const std::size_t M = n * o0 * o1 * o2 * o3;
				const std::size_t cin_g = cin / groups, cout_g = cout / groups;
				const std::size_t K = cin_g * k0 * k1 * k2 * k3;
				if (M == 0 || K == 0) return true;
				if (K * M > (std::size_t(1) << 24)) return false;

				const T* S = (const T*)src; const T* W = (const T*)weight;
				const T* B = bias ? (const T*)bias : nullptr; T* D = (T*)dst;
				std::vector<T> col(K * M), outbuf(cout_g * M);

				for (std::size_t g = 0; g < groups; ++g)
				{
					const T* Sg = S + g * cin_g * i0 * i1 * i2 * i3;
					for (std::size_t m = 0; m < M; ++m)
					{
						std::size_t r = m;
						const std::size_t o3_i = r % o3; r /= o3;
						const std::size_t o2_i = r % o2; r /= o2;
						const std::size_t o1_i = r % o1; r /= o1;
						const std::size_t o0_i = r % o0; r /= o0;
						const std::size_t nn = r;
						for (std::size_t ci = 0; ci < cin_g; ++ci)
							for (std::size_t k0_i = 0; k0_i < k0; ++k0_i)
								for (std::size_t k1_i = 0; k1_i < k1; ++k1_i)
									for (std::size_t k2_i = 0; k2_i < k2; ++k2_i)
										for (std::size_t k3_i = 0; k3_i < k3; ++k3_i)
										{
											const std::int64_t i0_in = (std::int64_t)o0_i * s0 - (std::int64_t)p0 + (std::int64_t)(k0_i * d0);
											const std::int64_t i1_in = (std::int64_t)o1_i * s1 - (std::int64_t)p1 + (std::int64_t)(k1_i * d1);
											const std::int64_t i2_in = (std::int64_t)o2_i * s2 - (std::int64_t)p2 + (std::int64_t)(k2_i * d2);
											const std::int64_t i3_in = (std::int64_t)o3_i * s3 - (std::int64_t)p3 + (std::int64_t)(k3_i * d3);
											const std::size_t kk = (((ci * k0 + k0_i) * k1 + k1_i) * k2 + k2_i) * k3 + k3_i;
											T val = T(0);
											if (i0_in >= 0 && i0_in < (std::int64_t)i0 && i1_in >= 0 && i1_in < (std::int64_t)i1
												&& i2_in >= 0 && i2_in < (std::int64_t)i2 && i3_in >= 0 && i3_in < (std::int64_t)i3)
												val = Sg[((((nn * cin_g + ci) * i0 + (std::size_t)i0_in) * i1 + (std::size_t)i1_in) * i2 + (std::size_t)i2_in) * i3 + (std::size_t)i3_in];
											col[kk * M + m] = val;
										}
					}
					if constexpr (std::is_same_v<T, float>)
						cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans, (int)cout_g, (int)M, (int)K,
							1.0f, W + g * cout_g * K, (int)K, col.data(), (int)M, 0.0f, outbuf.data(), (int)M);
					else
						cblas_dgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans, (int)cout_g, (int)M, (int)K,
							1.0, W + g * cout_g * K, (int)K, col.data(), (int)M, 0.0, outbuf.data(), (int)M);
					for (std::size_t m = 0; m < M; ++m)
					{
						std::size_t r = m;
						const std::size_t o3_i = r % o3; r /= o3;
						const std::size_t o2_i = r % o2; r /= o2;
						const std::size_t o1_i = r % o1; r /= o1;
						const std::size_t o0_i = r % o0; r /= o0;
						const std::size_t nn = r;
						for (std::size_t co = 0; co < cout_g; ++co)
						{
							const T bv = B ? B[g * cout_g + co] : T(0);
							D[((((nn * cout + (g * cout_g + co)) * o0 + o0_i) * o1 + o1_i) * o2 + o2_i) * o3 + o3_i] = outbuf[co * M + m] + bv;
						}
					}
				}
				return true;
			}
#endif

#ifdef TENSOR_WITH_SIMD
			// —— 整型 conv4d：im2col + 整型 GEMM（SIMD 单行内核，i16/i32/bool）——
			template<class T>
			bool conv4d_im2col_int(void* dst, const void* src, const void* weight, const void* bias,
				std::size_t n, std::size_t cin, std::size_t cout,
				std::size_t i0, std::size_t i1, std::size_t i2, std::size_t i3,
				std::size_t k0, std::size_t k1, std::size_t k2, std::size_t k3,
				std::size_t s0, std::size_t s1, std::size_t s2, std::size_t s3,
				std::size_t p0, std::size_t p1, std::size_t p2, std::size_t p3,
				std::size_t d0, std::size_t d1, std::size_t d2, std::size_t d3,
				std::size_t groups)
			{
				if (cin % groups != 0 || cout % groups != 0) return false;
				const std::size_t o0 = (i0 + 2 * p0 - d0 * (k0 - 1) - 1) / s0 + 1;
				const std::size_t o1 = (i1 + 2 * p1 - d1 * (k1 - 1) - 1) / s1 + 1;
				const std::size_t o2 = (i2 + 2 * p2 - d2 * (k2 - 1) - 1) / s2 + 1;
				const std::size_t o3 = (i3 + 2 * p3 - d3 * (k3 - 1) - 1) / s3 + 1;
				const std::size_t M = n * o0 * o1 * o2 * o3;
				const std::size_t cin_g = cin / groups, cout_g = cout / groups;
				const std::size_t K = cin_g * k0 * k1 * k2 * k3;
				if (M == 0 || K == 0) return true;
				if (K * M > (std::size_t(1) << 24)) return false;

				const T* S = (const T*)src; const T* W = (const T*)weight;
				const T* B = bias ? (const T*)bias : nullptr; T* D = (T*)dst;
				std::vector<T> col(K * M), outbuf(cout_g * M);

				for (std::size_t g = 0; g < groups; ++g)
				{
					const T* Sg = S + g * cin_g * i0 * i1 * i2 * i3;
					for (std::size_t m = 0; m < M; ++m)
					{
						std::size_t r = m;
						const std::size_t o3_i = r % o3; r /= o3;
						const std::size_t o2_i = r % o2; r /= o2;
						const std::size_t o1_i = r % o1; r /= o1;
						const std::size_t o0_i = r % o0; r /= o0;
						const std::size_t nn = r;
						for (std::size_t ci = 0; ci < cin_g; ++ci)
							for (std::size_t k0_i = 0; k0_i < k0; ++k0_i)
								for (std::size_t k1_i = 0; k1_i < k1; ++k1_i)
									for (std::size_t k2_i = 0; k2_i < k2; ++k2_i)
										for (std::size_t k3_i = 0; k3_i < k3; ++k3_i)
										{
											const std::int64_t i0_in = (std::int64_t)o0_i * s0 - (std::int64_t)p0 + (std::int64_t)(k0_i * d0);
											const std::int64_t i1_in = (std::int64_t)o1_i * s1 - (std::int64_t)p1 + (std::int64_t)(k1_i * d1);
											const std::int64_t i2_in = (std::int64_t)o2_i * s2 - (std::int64_t)p2 + (std::int64_t)(k2_i * d2);
											const std::int64_t i3_in = (std::int64_t)o3_i * s3 - (std::int64_t)p3 + (std::int64_t)(k3_i * d3);
											const std::size_t kk = (((ci * k0 + k0_i) * k1 + k1_i) * k2 + k2_i) * k3 + k3_i;
											T val = T(0);
											if (i0_in >= 0 && i0_in < (std::int64_t)i0 && i1_in >= 0 && i1_in < (std::int64_t)i1
												&& i2_in >= 0 && i2_in < (std::int64_t)i2 && i3_in >= 0 && i3_in < (std::int64_t)i3)
												val = Sg[((((nn * cin_g + ci) * i0 + (std::size_t)i0_in) * i1 + (std::size_t)i1_in) * i2 + (std::size_t)i2_in) * i3 + (std::size_t)i3_in];
											col[kk * M + m] = val;
										}
					}
					// 例外（约定外）：conv 整型 GEMM 并行按输出行切（粒度即行），不设 4096 阈值，
					// 仅单核回退；线程数封顶 32 控内存与调度开销。
					const unsigned hwc = std::thread::hardware_concurrency();
					const std::size_t nth = (hwc == 0 || hwc == 1) ? 1 : (hwc > 32 ? 32 : hwc);
					auto gemm_row = [&](std::size_t co) {
						if constexpr (std::is_same_v<T, xhqm::i16>)
							simd::matmul_row_i16(outbuf.data() + co * M, W + g * cout_g * K + co * K, col.data(), M, K);
						else if constexpr (std::is_same_v<T, xhqm::i32>)
							simd::matmul_row_i32(outbuf.data() + co * M, W + g * cout_g * K + co * K, col.data(), M, K);
						else
							simd::matmul_row_bool(outbuf.data() + co * M, W + g * cout_g * K + co * K, col.data(), M, K);
					};
					if (nth == 1)
					{
						for (std::size_t co = 0; co < cout_g; ++co) gemm_row(co);
					}
					else
					{
						std::vector<std::thread> pool; pool.reserve(nth);
						for (std::size_t t = 0; t < nth; ++t)
						{
							const std::size_t b = t * cout_g / nth, e = (t + 1) * cout_g / nth;
							if (b >= e) continue;
							pool.emplace_back([&, b, e]() { for (std::size_t co = b; co < e; ++co) gemm_row(co); });
						}
						for (auto& th : pool) th.join();
					}
					for (std::size_t m = 0; m < M; ++m)
					{
						std::size_t r = m;
						const std::size_t o3_i = r % o3; r /= o3;
						const std::size_t o2_i = r % o2; r /= o2;
						const std::size_t o1_i = r % o1; r /= o1;
						const std::size_t o0_i = r % o0; r /= o0;
						const std::size_t nn = r;
						for (std::size_t co = 0; co < cout_g; ++co)
						{
							const T bv = B ? B[g * cout_g + co] : T(0);
							T ov = outbuf[co * M + m];
							if constexpr (std::is_same_v<T, unsigned char>) ov = (unsigned char)(ov ^ bv);
							else ov = (T)(ov + bv);
							D[((((nn * cout + (g * cout_g + co)) * o0 + o0_i) * o1 + o1_i) * o2 + o2_i) * o3 + o3_i] = ov;
						}
					}
				}
				return true;
			}
#endif

			class cpu_backend : public tensor_backend
			{
			public:
				const char* name() const noexcept override { return "cpu"; }
				device kind() const noexcept override { return device::cpu; }
				bool supports_memory_transfer() const noexcept override { return false; }

				void parallel_for(std::size_t begin, std::size_t end,
					const std::function<void(std::size_t)>& body) override
				{
					if (end <= begin)
						return;

					const std::size_t count = end - begin;
					unsigned threads = std::thread::hardware_concurrency();
					if (threads == 0)
						threads = 1;

					// 数据量小或单核时直接串行，省去线程创建开销
					if (count < XHQM_PARALLEL_THRESHOLD || threads == 1)
					{
						for (std::size_t i = begin; i < end; ++i)
							body(i);
						return;
					}

					const std::size_t block = (count + threads - 1) / threads;
					std::vector<std::thread> pool;
					pool.reserve(threads);
					for (unsigned t = 0; t < threads; ++t)
					{
						const std::size_t b = begin + t * block;
						const std::size_t e = (std::min)(end, b + block);
						if (b >= e)
							break;
						pool.emplace_back([b, e, &body]()
						{
							for (std::size_t i = b; i < e; ++i)
								body(i);
						});
					}
					for (auto& th : pool)
						th.join();
				}

				// 逐元素：SIMD 优先，否则回退 parallel_for 通用路径
				bool elemwise(void* dst, const void* a, const void* b,
					std::size_t count, dtype dt, int op) noexcept override
				{
#ifdef TENSOR_WITH_SIMD
					if (dt == dtype::f16) { simd::elemwise_f16(dst, a, b, count, op); return true; }
					if (dt == dtype::f32) { simd::elemwise_ps(dst, a, b, count, op); return true; }
					if (dt == dtype::f64) { simd::elemwise_pd(dst, a, b, count, op); return true; }
					if (dt == dtype::i8) { if (simd::elemwise_i8(dst, a, b, count, op)) return true; }
					if (dt == dtype::i16) { if (simd::elemwise_i16(dst, a, b, count, op)) return true; }
					if (dt == dtype::i32) { if (simd::elemwise_i32(dst, a, b, count, op)) return true; }
					if (dt == dtype::i64) { if (simd::elemwise_i64(dst, a, b, count, op)) return true; }
					if (dt == dtype::boolean) { if (simd::elemwise_bool(dst, a, b, count, op)) return true; }
#endif
					return false;
				}

				// 条件选择：SIMD（blendv）优先，否则回退 parallel_for
				bool select(void* dst, const void* cond, const void* a, const void* b,
					std::size_t count, dtype dt) noexcept override
				{
#ifdef TENSOR_WITH_SIMD
					if (dt == dtype::f32) { simd::select_ps(dst, cond, a, b, count); return true; }
					if (dt == dtype::f64) { simd::select_pd(dst, cond, a, b, count); return true; }
#endif
					return false;
				}

				// 矩阵乘：blas → simd → thread（thread 由调用方 parallel_for 回退）
				bool matmul(void* c, const void* a, const void* b,
					std::size_t m, std::size_t n, std::size_t k, dtype dt) noexcept override
				{
#ifdef TENSOR_WITH_BLAS
					if (dt == dtype::f32)
					{
						cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans,
							(int)m, (int)n, (int)k, 1.0f, (const float*)a, (int)k, (const float*)b, (int)n, 0.0f, (float*)c, (int)n);
						return true;
					}
					if (dt == dtype::f64)
					{
						cblas_dgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans,
							(int)m, (int)n, (int)k, 1.0, (const double*)a, (int)k, (const double*)b, (int)n, 0.0, (double*)c, (int)n);
						return true;
					}
#endif
#ifdef TENSOR_WITH_SIMD
					if (dt == dtype::f16) { simd::matmul_f16(c, a, b, m, n, k); return true; }
					if (dt == dtype::f32) { simd::matmul_ps(c, a, b, m, n, k); return true; }
					if (dt == dtype::f64) { simd::matmul_pd(c, a, b, m, n, k); return true; }
					if (dt == dtype::i16)
					{
						xhqm::i16* C = (xhqm::i16*)c; const xhqm::i16* A = (const xhqm::i16*)a; const xhqm::i16* B = (const xhqm::i16*)b;
						parallel_for(0, m, [&](std::size_t i) { simd::matmul_row_i16(C + i * n, A + i * k, B, n, k); });
						return true;
					}
					if (dt == dtype::i32)
					{
						xhqm::i32* C = (xhqm::i32*)c; const xhqm::i32* A = (const xhqm::i32*)a; const xhqm::i32* B = (const xhqm::i32*)b;
						parallel_for(0, m, [&](std::size_t i) { simd::matmul_row_i32(C + i * n, A + i * k, B, n, k); });
						return true;
					}
					if (dt == dtype::boolean)
					{
						unsigned char* C = (unsigned char*)c; const unsigned char* A = (const unsigned char*)a; const unsigned char* B = (const unsigned char*)b;
						parallel_for(0, m, [&](std::size_t i) { simd::matmul_row_bool(C + i * n, A + i * k, B, n, k); });
						return true;
					}
#endif
					return false;
				}

				// 沿最后一维归约：SIMD 优先，否则回退 parallel_for
				bool reduce(void* dst, const void* src,
					std::size_t outer, std::size_t inner, dtype dt, int op) noexcept override
				{
#ifdef TENSOR_WITH_SIMD
					if (dt == dtype::f16) { simd::reduce_f16(dst, src, outer, inner, op); return true; }
					if (dt == dtype::f32) { simd::reduce_ps(dst, src, outer, inner, op); return true; }
					if (dt == dtype::f64) { simd::reduce_pd(dst, src, outer, inner, op); return true; }
					if (dt == dtype::i32) { simd::reduce_i32(dst, src, outer, inner, op); return true; }
					if (dt == dtype::i64) { simd::reduce_i64(dst, src, outer, inner, op); return true; }
					if (dt == dtype::i16) { simd::reduce_i16(dst, src, outer, inner, op); return true; }
					if (dt == dtype::i8) { simd::reduce_i8(dst, src, outer, inner, op); return true; }
					if (dt == dtype::boolean) { simd::reduce_bool(dst, src, outer, inner, op); return true; }
#endif
					return false;
				}

				// 一元逐元素：SIMD（relu/leaky_relu/sqrt）优先，其余回退并行桩
				bool unary(void* dst, const void* a,
					std::size_t count, dtype dt, int op) noexcept override
				{
#ifdef TENSOR_WITH_SIMD
					if (dt == dtype::f16) { if (simd::unary_f16(dst, a, count, op)) return true; }
					if (dt == dtype::f32) { if (simd::unary_ps(dst, a, count, op)) return true; }
					if (dt == dtype::f64) { if (simd::unary_pd(dst, a, count, op)) return true; }
					if (dt == dtype::i8) { if (simd::unary_i8(dst, a, count, op)) return true; }
					if (dt == dtype::i16) { if (simd::unary_i16(dst, a, count, op)) return true; }
					if (dt == dtype::i32) { if (simd::unary_i32(dst, a, count, op)) return true; }
					if (dt == dtype::i64) { if (simd::unary_i64(dst, a, count, op)) return true; }
#endif
					switch (dt)
					{
					case dtype::f32: {
						float* d = static_cast<float*>(dst); const float* s = static_cast<const float*>(a);
						parallel_for(0, count, [&](std::size_t i) { d[i] = unary_eval(s[i], op); });
						return true;
					}
					case dtype::f64: {
						double* d = static_cast<double*>(dst); const double* s = static_cast<const double*>(a);
						parallel_for(0, count, [&](std::size_t i) { d[i] = unary_eval(s[i], op); });
						return true;
					}
					default: return false;
					}
				}

				// 转置：SIMD（4x4 分块）优先，回退并行桩
				bool transpose(void* dst, const void* src,
					std::size_t rows, std::size_t cols, dtype dt) noexcept override
				{
#ifdef TENSOR_WITH_SIMD
					if (dt == dtype::f16) { simd::transpose_f16(dst, src, rows, cols); return true; }
					if (dt == dtype::f32) { simd::transpose_ps(dst, src, rows, cols); return true; }
					if (dt == dtype::f64) { simd::transpose_pd(dst, src, rows, cols); return true; }
					if (dt == dtype::i32) { simd::transpose_i32(dst, src, rows, cols); return true; }
					if (dt == dtype::i16) { simd::transpose_i16(dst, src, rows, cols); return true; }
					if (dt == dtype::i8) { simd::transpose_i8(dst, src, rows, cols); return true; }
#endif
					switch (dt)
					{
					case dtype::f32: {
						float* d = static_cast<float*>(dst); const float* s = static_cast<const float*>(src);
						parallel_for(0, rows * cols, [&](std::size_t idx) {
							std::size_t i = idx / cols, j = idx % cols;
							d[j * rows + i] = s[i * cols + j];
						});
						return true;
					}
					case dtype::f64: {
						double* d = static_cast<double*>(dst); const double* s = static_cast<const double*>(src);
						parallel_for(0, rows * cols, [&](std::size_t idx) {
							std::size_t i = idx / cols, j = idx % cols;
							d[j * rows + i] = s[i * cols + j];
						});
						return true;
					}
					case dtype::f16: case dtype::i8: case dtype::i16: case dtype::i32: case dtype::i64: case dtype::boolean: {
						// 整型/bool/f16 转置：按元素字节数做并行拷贝（无算术，仅重排）
						const std::size_t elem = (dt == dtype::i8 || dt == dtype::boolean) ? 1
							: (dt == dtype::f16 || dt == dtype::i16) ? 2 : (dt == dtype::i32) ? 4 : 8;
						char* d = static_cast<char*>(dst); const char* s = static_cast<const char*>(src);
						parallel_for(0, rows * cols, [&](std::size_t idx) {
							std::size_t i = idx / cols, j = idx % cols;
							std::memcpy(d + (j * rows + i) * elem, s + (i * cols + j) * elem, elem);
						});
						return true;
					}
					default: return false;
					}
				}

				// 反向逐元素累加：SIMD 优先，回退并行桩
				bool grad_accum(void* dst, const void* a, const void* b,
					std::size_t count, dtype dt, int op) noexcept override
				{
#ifdef TENSOR_WITH_SIMD
					if (dt == dtype::f16) { simd::grad_accum_f16(dst, a, b, count, op); return true; }
					if (dt == dtype::f32) { simd::grad_accum_ps(dst, a, b, count, op); return true; }
					if (dt == dtype::f64) { simd::grad_accum_pd(dst, a, b, count, op); return true; }
#endif
					switch (dt)
					{
					case dtype::f32: {
						float* d = static_cast<float*>(dst); const float* pa = static_cast<const float*>(a); const float* pb = static_cast<const float*>(b);
						parallel_for(0, count, [&](std::size_t i) { d[i] += grad_eval(pa[i], pb[i], op); });
						return true;
					}
					case dtype::f64: {
						double* d = static_cast<double*>(dst); const double* pa = static_cast<const double*>(a); const double* pb = static_cast<const double*>(b);
						parallel_for(0, count, [&](std::size_t i) { d[i] += grad_eval(pa[i], pb[i], op); });
						return true;
					}
					default: return false;
					}
				}

				// 稀疏矩阵 × 稠密向量（COO）：y[row[k]] += val[k] * x[col[k]]（y 需预清零）。
				// 并行策略：每线程维护独立的稠密部分和再归并，规避 scatter 写同一 y 行的数据竞争
				// （float 无可用原子累加原语）；小数据量或单核回退串行。
				bool spmv(void* y, const void* x, const xhqm::index* row, const xhqm::index* col,
					const void* val, std::size_t nnz, std::size_t rows, std::size_t cols, dtype dt) noexcept override
				{
					if (nnz == 0 || rows == 0 || cols == 0) return true;
					if (dt == dtype::boolean) { spmv_bool(y, x, row, col, val, nnz, rows); return true; }
					if (dt == dtype::f16) { spmv_arith<xhqm::f16>(y, x, row, col, val, nnz, rows); return true; }
					if (dt == dtype::f32) { spmv_arith<float>(y, x, row, col, val, nnz, rows); return true; }
					if (dt == dtype::f64) { spmv_arith<double>(y, x, row, col, val, nnz, rows); return true; }
					if (dt == dtype::i8)  { spmv_arith<xhqm::i8>(y, x, row, col, val, nnz, rows); return true; }
					if (dt == dtype::i16) { spmv_arith<xhqm::i16>(y, x, row, col, val, nnz, rows); return true; }
					if (dt == dtype::i32) { spmv_arith<xhqm::i32>(y, x, row, col, val, nnz, rows); return true; }
					if (dt == dtype::i64) { spmv_arith<xhqm::i64>(y, x, row, col, val, nnz, rows); return true; }
					return false;
				}

				// 卷积（统一 3D）：im2col + GEMM；f32/f64 走 BLAS，i16/i32/bool 走 SIMD 整型 GEMM；
				// col 过大或未支持类型回退 conv_nd。
				bool conv3d(void* dst, const void* src, const void* weight, const void* bias,
					std::size_t n, std::size_t cin, std::size_t cout,
					std::size_t id, std::size_t ih, std::size_t iw,
					std::size_t kd, std::size_t kh, std::size_t kw,
					std::size_t stride_d, std::size_t stride_h, std::size_t stride_w,
					std::size_t pad_d, std::size_t pad_h, std::size_t pad_w,
					std::size_t dil_d, std::size_t dil_h, std::size_t dil_w,
					std::size_t groups, dtype dt) noexcept override
				{
#ifdef TENSOR_WITH_BLAS
					if (dt == dtype::f32)
						return conv3d_im2col<float>(dst, src, weight, bias, n, cin, cout, id, ih, iw, kd, kh, kw,
							stride_d, stride_h, stride_w, pad_d, pad_h, pad_w, dil_d, dil_h, dil_w, groups);
					if (dt == dtype::f64)
						return conv3d_im2col<double>(dst, src, weight, bias, n, cin, cout, id, ih, iw, kd, kh, kw,
							stride_d, stride_h, stride_w, pad_d, pad_h, pad_w, dil_d, dil_h, dil_w, groups);
#endif
#ifdef TENSOR_WITH_SIMD
					if (dt == dtype::i16)
						return conv3d_im2col_int<xhqm::i16>(dst, src, weight, bias, n, cin, cout, id, ih, iw, kd, kh, kw,
							stride_d, stride_h, stride_w, pad_d, pad_h, pad_w, dil_d, dil_h, dil_w, groups);
					if (dt == dtype::i32)
						return conv3d_im2col_int<xhqm::i32>(dst, src, weight, bias, n, cin, cout, id, ih, iw, kd, kh, kw,
							stride_d, stride_h, stride_w, pad_d, pad_h, pad_w, dil_d, dil_h, dil_w, groups);
					if (dt == dtype::boolean)
						return conv3d_im2col_int<unsigned char>(dst, src, weight, bias, n, cin, cout, id, ih, iw, kd, kh, kw,
							stride_d, stride_h, stride_w, pad_d, pad_h, pad_w, dil_d, dil_h, dil_w, groups);
#endif
					(void)dst; (void)src; (void)weight; (void)bias;
					(void)n; (void)cin; (void)cout; (void)id; (void)ih; (void)iw; (void)kd; (void)kh; (void)kw;
					(void)stride_d; (void)stride_h; (void)stride_w; (void)pad_d; (void)pad_h; (void)pad_w;
					(void)dil_d; (void)dil_h; (void)dil_w; (void)groups; (void)dt;
					return false;
				}

				// 4D 卷积（N,C,D0,D1,D2,D3）：im2col + GEMM；f32/f64 走 BLAS，i16/i32/bool 走 SIMD 整型 GEMM。
				bool conv4d(void* dst, const void* src, const void* weight, const void* bias,
					std::size_t n, std::size_t cin, std::size_t cout,
					std::size_t i0, std::size_t i1, std::size_t i2, std::size_t i3,
					std::size_t k0, std::size_t k1, std::size_t k2, std::size_t k3,
					std::size_t s0, std::size_t s1, std::size_t s2, std::size_t s3,
					std::size_t p0, std::size_t p1, std::size_t p2, std::size_t p3,
					std::size_t d0, std::size_t d1, std::size_t d2, std::size_t d3,
					std::size_t groups, dtype dt) noexcept override
				{
#ifdef TENSOR_WITH_BLAS
					if (dt == dtype::f32)
						return conv4d_im2col<float>(dst, src, weight, bias, n, cin, cout, i0, i1, i2, i3, k0, k1, k2, k3,
							s0, s1, s2, s3, p0, p1, p2, p3, d0, d1, d2, d3, groups);
					if (dt == dtype::f64)
						return conv4d_im2col<double>(dst, src, weight, bias, n, cin, cout, i0, i1, i2, i3, k0, k1, k2, k3,
							s0, s1, s2, s3, p0, p1, p2, p3, d0, d1, d2, d3, groups);
#endif
#ifdef TENSOR_WITH_SIMD
					if (dt == dtype::i16)
						return conv4d_im2col_int<xhqm::i16>(dst, src, weight, bias, n, cin, cout, i0, i1, i2, i3, k0, k1, k2, k3,
							s0, s1, s2, s3, p0, p1, p2, p3, d0, d1, d2, d3, groups);
					if (dt == dtype::i32)
						return conv4d_im2col_int<xhqm::i32>(dst, src, weight, bias, n, cin, cout, i0, i1, i2, i3, k0, k1, k2, k3,
							s0, s1, s2, s3, p0, p1, p2, p3, d0, d1, d2, d3, groups);
					if (dt == dtype::boolean)
						return conv4d_im2col_int<unsigned char>(dst, src, weight, bias, n, cin, cout, i0, i1, i2, i3, k0, k1, k2, k3,
							s0, s1, s2, s3, p0, p1, p2, p3, d0, d1, d2, d3, groups);
#endif
					(void)dst; (void)src; (void)weight; (void)bias;
					(void)n; (void)cin; (void)cout; (void)i0; (void)i1; (void)i2; (void)i3;
					(void)k0; (void)k1; (void)k2; (void)k3; (void)s0; (void)s1; (void)s2; (void)s3;
					(void)p0; (void)p1; (void)p2; (void)p3; (void)d0; (void)d1; (void)d2; (void)d3;
					(void)groups; (void)dt;
					return false;
				}

				// 三角求解：x = solve(tri, b)，cblas 行优先 strsv/dtrsv（原地覆盖 x）。
				bool trsv(void* x, const void* tri, const void* b,
					std::size_t n, bool lower, bool unit_diag, dtype dt) noexcept override
				{
#ifdef TENSOR_WITH_BLAS
					if (dt == dtype::f32)
					{
						float* xp = (float*)x; const float* tp = (const float*)tri; const float* bp = (const float*)b;
						std::memcpy(xp, bp, n * sizeof(float));
						cblas_strsv(CblasRowMajor, lower ? CblasLower : CblasUpper, CblasNoTrans,
							unit_diag ? CblasUnit : CblasNonUnit, (int)n, tp, (int)n, xp, 1);
						return true;
					}
					if (dt == dtype::f64)
					{
						double* xp = (double*)x; const double* tp = (const double*)tri; const double* bp = (const double*)b;
						std::memcpy(xp, bp, n * sizeof(double));
						cblas_dtrsv(CblasRowMajor, lower ? CblasLower : CblasUpper, CblasNoTrans,
							unit_diag ? CblasUnit : CblasNonUnit, (int)n, tp, (int)n, xp, 1);
						return true;
					}
#endif
					(void)x; (void)tri; (void)b; (void)n; (void)lower; (void)unit_diag; (void)dt;
					return false;
				}

				// Cholesky：A → L（下三角，行优先）。LAPACKE potrf（行优先），非正定 info>0 返回 false。
				bool chol(void* L, const void* A, std::size_t n, dtype dt) noexcept override
				{
#ifdef TENSOR_WITH_BLAS
					if (dt == dtype::f32)
					{
						float* Lp = (float*)L;
						std::memcpy(Lp, A, n * n * sizeof(float));
						if (LAPACKE_spotrf(LAPACK_ROW_MAJOR, 'L', (int)n, Lp, (int)n) != 0) return false;
						for (std::size_t i = 0; i < n; ++i)
							for (std::size_t j = i + 1; j < n; ++j) Lp[i * n + j] = 0.0f;  // 清上三角
						return true;
					}
					if (dt == dtype::f64)
					{
						double* Lp = (double*)L;
						std::memcpy(Lp, A, n * n * sizeof(double));
						if (LAPACKE_dpotrf(LAPACK_ROW_MAJOR, 'L', (int)n, Lp, (int)n) != 0) return false;
						for (std::size_t i = 0; i < n; ++i)
							for (std::size_t j = i + 1; j < n; ++j) Lp[i * n + j] = 0.0;   // 清上三角
						return true;
					}
#endif
					(void)L; (void)A; (void)n; (void)dt;
					return false;
				}

				void upload(void*, std::size_t) override {}
				void download(void*, std::size_t) override {}
				void synchronize() override {}
			};
		}

		tensor_backend* create_cpu_backend()
		{
			static cpu_backend instance;
			return &instance;
		}
	}
}
