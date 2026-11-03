#pragma once
#ifndef XHQM_TENSOR_BACKEND_SIMD
#define XHQM_TENSOR_BACKEND_SIMD

// SIMD 内核（AVX2 + FMA）：逐元素 / 矩阵乘 / 归约，供 cpu 后端调用。
// 仅在 TENSOR_WITH_SIMD 下由 cpu_backend 使用，其余类型由后端回退标量路径。
#include "tensor_backend.h"
#include <immintrin.h>
#include <vector>
#include <cstddef>

namespace xhqm
{
	namespace tensor_base
	{
		namespace simd
		{
			// —— 横向求和 ——
			inline float hsum_ps(__m256 v)
			{
				__m128 lo = _mm256_castps256_ps128(v);
				__m128 hi = _mm256_extractf128_ps(v, 1);
				__m128 s = _mm_add_ps(lo, hi);
				s = _mm_hadd_ps(s, s);
				s = _mm_hadd_ps(s, s);
				return _mm_cvtss_f32(s);
			}
			inline double hsum_pd(__m256d v)
			{
				__m128d lo = _mm256_castpd256_pd128(v);
				__m128d hi = _mm256_extractf128_pd(v, 1);
				__m128d s = _mm_add_pd(lo, hi);
				s = _mm_hadd_pd(s, s);
				return _mm_cvtsd_f64(s);
			}
			// —— 横向连乘 ——
			inline float hprod_ps(__m256 v)
			{
				__m128 lo = _mm256_castps256_ps128(v);
				__m128 hi = _mm256_extractf128_ps(v, 1);
				__m128 p = _mm_mul_ps(lo, hi);
				// 两两相乘：p = [a,b,c,d] -> [a*b, a*b, c*d, c*d] -> [a*b*c*d, ...]
				p = _mm_mul_ps(p, _mm_shuffle_ps(p, p, _MM_SHUFFLE(2, 3, 0, 1)));
				p = _mm_mul_ps(p, _mm_shuffle_ps(p, p, _MM_SHUFFLE(1, 0, 3, 2)));
				return _mm_cvtss_f32(p);
			}
			inline double hprod_pd(__m256d v)
			{
				__m128d lo = _mm256_castpd256_pd128(v);
				__m128d hi = _mm256_extractf128_pd(v, 1);
				__m128d p = _mm_mul_pd(lo, hi);
				p = _mm_mul_pd(p, _mm_shuffle_pd(p, p, 1));
				return _mm_cvtsd_f64(p);
			}

			// —— 逐元素二元运算（op 见 elemwise_op）——
			inline void elemwise_ps(void* dst, const void* a, const void* b, std::size_t count, int op)
			{
				float* d = (float*)dst; const float* x = (const float*)a; const float* y = (const float*)b;
				std::size_t i = 0;
				for (; i + 8 <= count; i += 8)
				{
					__m256 va = _mm256_loadu_ps(x + i);
					__m256 vb = _mm256_loadu_ps(y + i);
					__m256 vr;
					switch (op)
					{
					case elem_add: vr = _mm256_add_ps(va, vb); break;
					case elem_sub: vr = _mm256_sub_ps(va, vb); break;
					case elem_mul: vr = _mm256_mul_ps(va, vb); break;
					case elem_div: vr = _mm256_div_ps(va, vb); break;
					case elem_atan2: vr = _mm256_atan2_ps(va, vb); break;
					case elem_hypot: vr = _mm256_hypot_ps(va, vb); break;
					case elem_pow: vr = _mm256_pow_ps(va, vb); break;
					case elem_mod: vr = _mm256_sub_ps(va, _mm256_mul_ps(_mm256_floor_ps(_mm256_div_ps(va, vb)), vb)); break;
					case elem_rem: {
						__m256 q = _mm256_round_ps(_mm256_div_ps(va, vb), _MM_FROUND_TO_ZERO | _MM_FROUND_NO_EXC);
						vr = _mm256_sub_ps(va, _mm256_mul_ps(q, vb)); break;
					}
					default: vr = va; break;
					}
					_mm256_storeu_ps(d + i, vr);
				}
				for (; i < count; ++i)
					d[i] = elemwise_eval(x[i], y[i], op);
			}
			inline void elemwise_pd(void* dst, const void* a, const void* b, std::size_t count, int op)
			{
				double* d = (double*)dst; const double* x = (const double*)a; const double* y = (const double*)b;
				std::size_t i = 0;
				for (; i + 4 <= count; i += 4)
				{
					__m256d va = _mm256_loadu_pd(x + i);
					__m256d vb = _mm256_loadu_pd(y + i);
					__m256d vr;
					switch (op)
					{
					case elem_add: vr = _mm256_add_pd(va, vb); break;
					case elem_sub: vr = _mm256_sub_pd(va, vb); break;
					case elem_mul: vr = _mm256_mul_pd(va, vb); break;
					case elem_div: vr = _mm256_div_pd(va, vb); break;
					case elem_atan2: vr = _mm256_atan2_pd(va, vb); break;
					case elem_hypot: vr = _mm256_hypot_pd(va, vb); break;
					case elem_pow: vr = _mm256_pow_pd(va, vb); break;
					case elem_mod: vr = _mm256_sub_pd(va, _mm256_mul_pd(_mm256_floor_pd(_mm256_div_pd(va, vb)), vb)); break;
					case elem_rem: {
						__m256d q = _mm256_round_pd(_mm256_div_pd(va, vb), _MM_FROUND_TO_ZERO | _MM_FROUND_NO_EXC);
						vr = _mm256_sub_pd(va, _mm256_mul_pd(q, vb)); break;
					}
					default: vr = va; break;
					}
					_mm256_storeu_pd(d + i, vr);
				}
				for (; i < count; ++i)
					d[i] = elemwise_eval(x[i], y[i], op);
			}

			// —— 条件选择：dst = cond ? a : b（cond 为 bool 字节 0/1，blendv 掩码）——
			inline void select_ps(void* dst, const void* cond, const void* a, const void* b, std::size_t count)
			{
				float* d = (float*)dst; const float* x = (const float*)a; const float* y = (const float*)b;
				const unsigned char* c = (const unsigned char*)cond;
				std::size_t i = 0;
				for (; i + 8 <= count; i += 8)
				{
					__m128i cb = _mm_loadl_epi64((const __m128i*)(c + i));   // 8 个 bool 字节
					__m256i ci = _mm256_cvtepu8_epi32(cb);                   // 8 个 int32（0/1）
					__m256 cf = _mm256_cvtepi32_ps(ci);                      // 8 个 float（0/1）
					__m256 mask = _mm256_cmp_ps(cf, _mm256_set1_ps(0.5f), _CMP_GT_OQ);
					__m256 va = _mm256_loadu_ps(x + i);
					__m256 vb = _mm256_loadu_ps(y + i);
					_mm256_storeu_ps(d + i, _mm256_blendv_ps(vb, va, mask));
				}
				for (; i < count; ++i) d[i] = c[i] ? x[i] : y[i];
			}
			inline void select_pd(void* dst, const void* cond, const void* a, const void* b, std::size_t count)
			{
				double* d = (double*)dst; const double* x = (const double*)a; const double* y = (const double*)b;
				const unsigned char* c = (const unsigned char*)cond;
				std::size_t i = 0;
				for (; i + 4 <= count; i += 4)
				{
					__m128i cb = _mm_loadu_si32((const void*)(c + i));       // 4 个 bool 字节
					__m256i ci = _mm256_cvtepu8_epi32(cb);                   // 8 个 int32（前 4 有效）
					__m256d cf = _mm256_cvtepi32_pd(_mm256_castsi256_si128(ci)); // 前 4 个 → double
					__m256d mask = _mm256_cmp_pd(cf, _mm256_set1_pd(0.5), _CMP_GT_OQ);
					__m256d va = _mm256_loadu_pd(x + i);
					__m256d vb = _mm256_loadu_pd(y + i);
					_mm256_storeu_pd(d + i, _mm256_blendv_pd(vb, va, mask));
				}
				for (; i < count; ++i) d[i] = c[i] ? x[i] : y[i];
			}

			// —— 矩阵乘（行优先 C[m×n] = A[m×k] × B[k×n]）——
			inline void matmul_ps(void* c, const void* a, const void* b, std::size_t m, std::size_t n, std::size_t k)
			{
				float* C = (float*)c; const float* A = (const float*)a; const float* B = (const float*)b;
				std::vector<float> Bt(n * k);	// B 转置（列主序，便于 SIMD 连续访问）
				for (std::size_t i = 0; i < k; ++i)
					for (std::size_t j = 0; j < n; ++j)
						Bt[j * k + i] = B[i * n + j];
				for (std::size_t i = 0; i < m; ++i)
				{
					for (std::size_t j = 0; j < n; ++j)
					{
						__m256 acc = _mm256_setzero_ps();
						std::size_t p = 0;
						for (; p + 8 <= k; p += 8)
						{
							__m256 va = _mm256_loadu_ps(A + i * k + p);
							__m256 vb = _mm256_loadu_ps(Bt.data() + j * k + p);
							acc = _mm256_fmadd_ps(va, vb, acc);
						}
						float sum = hsum_ps(acc);
						for (; p < k; ++p)
							sum += A[i * k + p] * Bt[j * k + p];
						C[i * n + j] = sum;
					}
				}
			}
			inline void matmul_pd(void* c, const void* a, const void* b, std::size_t m, std::size_t n, std::size_t k)
			{
				double* C = (double*)c; const double* A = (const double*)a; const double* B = (const double*)b;
				std::vector<double> Bt(n * k);
				for (std::size_t i = 0; i < k; ++i)
					for (std::size_t j = 0; j < n; ++j)
						Bt[j * k + i] = B[i * n + j];
				for (std::size_t i = 0; i < m; ++i)
				{
					for (std::size_t j = 0; j < n; ++j)
					{
						__m256d acc = _mm256_setzero_pd();
						std::size_t p = 0;
						for (; p + 4 <= k; p += 4)
						{
							__m256d va = _mm256_loadu_pd(A + i * k + p);
							__m256d vb = _mm256_loadu_pd(Bt.data() + j * k + p);
							acc = _mm256_fmadd_pd(va, vb, acc);
						}
						double sum = hsum_pd(acc);
						for (; p < k; ++p)
							sum += A[i * k + p] * Bt[j * k + p];
						C[i * n + j] = sum;
					}
				}
			}

			// —— 沿最后一维归约（op: 0=sum 1=max 2=min）——
			inline void reduce_ps(void* dst, const void* src, std::size_t outer, std::size_t inner, int op)
			{
				float* d = (float*)dst; const float* s = (const float*)src;
				for (std::size_t o = 0; o < outer; ++o)
				{
					const float* row = s + o * inner;
					if (op == 0 || op == 3)	// sum / prod（AVX2 累加/累乘）
					{
						__m256 acc = (op == 0) ? _mm256_setzero_ps() : _mm256_set1_ps(1.0f);
						std::size_t i = 0;
						for (; i + 8 <= inner; i += 8)
							acc = (op == 0) ? _mm256_add_ps(acc, _mm256_loadu_ps(row + i)) : _mm256_mul_ps(acc, _mm256_loadu_ps(row + i));
						float r = (op == 0) ? hsum_ps(acc) : hprod_ps(acc);
						for (; i < inner; ++i) r = reduce_eval(r, row[i], op);
						d[o] = r;
					}
					else	// max / min（标量，规避 SIMD 最值提取的复杂度）
					{
						float r = row[0];
						for (std::size_t i = 1; i < inner; ++i)
							r = reduce_eval(r, row[i], op);
						d[o] = r;
					}
				}
			}
			inline void reduce_pd(void* dst, const void* src, std::size_t outer, std::size_t inner, int op)
			{
				double* d = (double*)dst; const double* s = (const double*)src;
				for (std::size_t o = 0; o < outer; ++o)
				{
					const double* row = s + o * inner;
					if (op == 0 || op == 3)	// sum / prod
					{
						__m256d acc = (op == 0) ? _mm256_setzero_pd() : _mm256_set1_pd(1.0);
						std::size_t i = 0;
						for (; i + 4 <= inner; i += 4)
							acc = (op == 0) ? _mm256_add_pd(acc, _mm256_loadu_pd(row + i)) : _mm256_mul_pd(acc, _mm256_loadu_pd(row + i));
						double r = (op == 0) ? hsum_pd(acc) : hprod_pd(acc);
						for (; i < inner; ++i) r = reduce_eval(r, row[i], op);
						d[o] = r;
					}
					else	// max / min（标量）
					{
						double r = row[0];
						for (std::size_t i = 1; i < inner; ++i)
							r = reduce_eval(r, row[i], op);
						d[o] = r;
					}
				}
			}

			// —— f16 半精度（F16C：f16↔f32 转换，内核内以 f32 计算再转回，fp32 累加）——
			inline __m256 load_f16x8(const xhqm::f16* p)
			{
				return _mm256_cvtph_ps(_mm_loadu_si128((const __m128i*)p));
			}
			inline void store_f16x8(xhqm::f16* p, __m256 v)
			{
				_mm_storeu_si128((__m128i*)p, _mm256_cvtps_ph(v, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC));
			}
			inline void elemwise_f16(void* dst, const void* a, const void* b, std::size_t count, int op)
			{
				xhqm::f16* d = (xhqm::f16*)dst;
				const xhqm::f16* x = (const xhqm::f16*)a;
				const xhqm::f16* y = (const xhqm::f16*)b;
				std::size_t i = 0;
				for (; i + 8 <= count; i += 8)
				{
					__m256 va = load_f16x8(x + i);
					__m256 vb = load_f16x8(y + i);
					__m256 vr;
					switch (op)
					{
					case elem_add: vr = _mm256_add_ps(va, vb); break;
					case elem_sub: vr = _mm256_sub_ps(va, vb); break;
					case elem_mul: vr = _mm256_mul_ps(va, vb); break;
					case elem_div: vr = _mm256_div_ps(va, vb); break;
					case elem_atan2: vr = _mm256_atan2_ps(va, vb); break;
					case elem_hypot: vr = _mm256_hypot_ps(va, vb); break;
					case elem_pow: vr = _mm256_pow_ps(va, vb); break;
					case elem_mod: vr = _mm256_sub_ps(va, _mm256_mul_ps(_mm256_floor_ps(_mm256_div_ps(va, vb)), vb)); break;
					case elem_rem: {
						__m256 q = _mm256_round_ps(_mm256_div_ps(va, vb), _MM_FROUND_TO_ZERO | _MM_FROUND_NO_EXC);
						vr = _mm256_sub_ps(va, _mm256_mul_ps(q, vb)); break;
					}
					default: vr = va; break;
					}
					store_f16x8(d + i, vr);
				}
				for (; i < count; ++i)
					d[i] = elemwise_eval(x[i], y[i], op);
			}
			// f16 矩阵乘：转 f32 复用 f32 SIMD matmul（fp32 累加，规避 fp16 累加误差）
			inline void matmul_f16(void* c, const void* a, const void* b, std::size_t m, std::size_t n, std::size_t k)
			{
				const xhqm::f16* A = (const xhqm::f16*)a;
				const xhqm::f16* B = (const xhqm::f16*)b;
				xhqm::f16* C = (xhqm::f16*)c;
				std::vector<float> Af(m * k), Bf(k * n), Cf(m * n);
				for (std::size_t i = 0; i < m * k; ++i) Af[i] = (float)A[i];
				for (std::size_t i = 0; i < k * n; ++i) Bf[i] = (float)B[i];
				matmul_ps(Cf.data(), Af.data(), Bf.data(), m, n, k);
				for (std::size_t i = 0; i < m * n; ++i) C[i] = (xhqm::f16)Cf[i];
			}
			inline void reduce_f16(void* dst, const void* src, std::size_t outer, std::size_t inner, int op)
			{
				xhqm::f16* d = (xhqm::f16*)dst;
				const xhqm::f16* s = (const xhqm::f16*)src;
				for (std::size_t o = 0; o < outer; ++o)
				{
					const xhqm::f16* row = s + o * inner;
					float r;
					if (op == 0 || op == 3)	// sum / prod
					{
						__m256 acc = (op == 0) ? _mm256_setzero_ps() : _mm256_set1_ps(1.0f);
						std::size_t i = 0;
						for (; i + 8 <= inner; i += 8)
						{
							__m256 v = load_f16x8(row + i);
							acc = (op == 0) ? _mm256_add_ps(acc, v) : _mm256_mul_ps(acc, v);
						}
						r = (op == 0) ? hsum_ps(acc) : hprod_ps(acc);
						for (; i < inner; ++i) r = (float)reduce_eval<float>(r, (float)row[i], op);
					}
					else	// max / min（标量）
					{
						r = (float)row[0];
						for (std::size_t i = 1; i < inner; ++i)
							r = (float)reduce_eval<float>(r, (float)row[i], op);
					}
					d[o] = (xhqm::f16)r;
				}
			}

			// —— 一元逐元素（SIMD；超越函数经 SVML，仅在 MSVC 下编译，其余平台回退标量桩）——
			inline bool unary_ps(void* dst, const void* a, std::size_t count, int op)
			{
				float* d = (float*)dst; const float* s = (const float*)a;
				std::size_t i = 0;
				switch (op)
				{
				case unary_relu: {
					__m256 zero = _mm256_setzero_ps();
					for (; i + 8 <= count; i += 8)
						_mm256_storeu_ps(d + i, _mm256_max_ps(_mm256_loadu_ps(s + i), zero));
					break;
				}
				case unary_leaky_relu: {
					__m256 zero = _mm256_setzero_ps(), slope = _mm256_set1_ps(0.01f);
					for (; i + 8 <= count; i += 8) {
						__m256 v = _mm256_loadu_ps(s + i);
						_mm256_storeu_ps(d + i, _mm256_blendv_ps(_mm256_mul_ps(v, slope), v, _mm256_cmp_ps(v, zero, _CMP_GT_OQ)));
					}
					break;
				}
				case unary_sqrt:
					for (; i + 8 <= count; i += 8)
						_mm256_storeu_ps(d + i, _mm256_sqrt_ps(_mm256_loadu_ps(s + i)));
					break;
				case unary_exp:
					for (; i + 8 <= count; i += 8)
						_mm256_storeu_ps(d + i, _mm256_exp_ps(_mm256_loadu_ps(s + i)));
					break;
				case unary_log:
					for (; i + 8 <= count; i += 8)
						_mm256_storeu_ps(d + i, _mm256_log_ps(_mm256_loadu_ps(s + i)));
					break;
				case unary_tanh:
					for (; i + 8 <= count; i += 8)
						_mm256_storeu_ps(d + i, _mm256_tanh_ps(_mm256_loadu_ps(s + i)));
					break;
				case unary_sigmoid: {
					__m256 one = _mm256_set1_ps(1.0f), zero = _mm256_setzero_ps();
					for (; i + 8 <= count; i += 8) {
						__m256 e = _mm256_exp_ps(_mm256_sub_ps(zero, _mm256_loadu_ps(s + i)));
						_mm256_storeu_ps(d + i, _mm256_div_ps(one, _mm256_add_ps(one, e)));
					}
					break;
				}
				case unary_silu: {
					__m256 one = _mm256_set1_ps(1.0f), zero = _mm256_setzero_ps();
					for (; i + 8 <= count; i += 8) {
						__m256 v = _mm256_loadu_ps(s + i);
						__m256 e = _mm256_exp_ps(_mm256_sub_ps(zero, v));
						_mm256_storeu_ps(d + i, _mm256_mul_ps(v, _mm256_div_ps(one, _mm256_add_ps(one, e))));
					}
					break;
				}
				case unary_gelu: {
					__m256 half = _mm256_set1_ps(0.5f), one = _mm256_set1_ps(1.0f);
					__m256 c = _mm256_set1_ps(0.7978845608028654f), k = _mm256_set1_ps(0.044715f);
					for (; i + 8 <= count; i += 8) {
						__m256 v = _mm256_loadu_ps(s + i);
						__m256 x3 = _mm256_mul_ps(_mm256_mul_ps(v, v), v);
						__m256 t = _mm256_tanh_ps(_mm256_mul_ps(c, _mm256_add_ps(v, _mm256_mul_ps(k, x3))));
						_mm256_storeu_ps(d + i, _mm256_mul_ps(_mm256_mul_ps(half, v), _mm256_add_ps(one, t)));
					}
					break;
				}
				case unary_sin:
					for (; i + 8 <= count; i += 8)
						_mm256_storeu_ps(d + i, _mm256_sin_ps(_mm256_loadu_ps(s + i)));
					break;
				case unary_cos:
					for (; i + 8 <= count; i += 8)
						_mm256_storeu_ps(d + i, _mm256_cos_ps(_mm256_loadu_ps(s + i)));
					break;
				case unary_tan:
					for (; i + 8 <= count; i += 8)
						_mm256_storeu_ps(d + i, _mm256_tan_ps(_mm256_loadu_ps(s + i)));
					break;
				case unary_asin:
					for (; i + 8 <= count; i += 8)
						_mm256_storeu_ps(d + i, _mm256_asin_ps(_mm256_loadu_ps(s + i)));
					break;
				case unary_acos:
					for (; i + 8 <= count; i += 8)
						_mm256_storeu_ps(d + i, _mm256_acos_ps(_mm256_loadu_ps(s + i)));
					break;
				case unary_atan:
					for (; i + 8 <= count; i += 8)
						_mm256_storeu_ps(d + i, _mm256_atan_ps(_mm256_loadu_ps(s + i)));
					break;
				case unary_sinh:
					for (; i + 8 <= count; i += 8)
						_mm256_storeu_ps(d + i, _mm256_sinh_ps(_mm256_loadu_ps(s + i)));
					break;
				case unary_cosh:
					for (; i + 8 <= count; i += 8)
						_mm256_storeu_ps(d + i, _mm256_cosh_ps(_mm256_loadu_ps(s + i)));
					break;
				case unary_asinh:
					for (; i + 8 <= count; i += 8)
						_mm256_storeu_ps(d + i, _mm256_asinh_ps(_mm256_loadu_ps(s + i)));
					break;
				case unary_acosh:
					for (; i + 8 <= count; i += 8)
						_mm256_storeu_ps(d + i, _mm256_acosh_ps(_mm256_loadu_ps(s + i)));
					break;
				case unary_atanh:
					for (; i + 8 <= count; i += 8)
						_mm256_storeu_ps(d + i, _mm256_atanh_ps(_mm256_loadu_ps(s + i)));
					break;
				case unary_log10:
					for (; i + 8 <= count; i += 8)
						_mm256_storeu_ps(d + i, _mm256_log10_ps(_mm256_loadu_ps(s + i)));
					break;
				case unary_log2:
					for (; i + 8 <= count; i += 8)
						_mm256_storeu_ps(d + i, _mm256_log2_ps(_mm256_loadu_ps(s + i)));
					break;
				case unary_floor:
					for (; i + 8 <= count; i += 8)
						_mm256_storeu_ps(d + i, _mm256_floor_ps(_mm256_loadu_ps(s + i)));
					break;
				case unary_ceil:
					for (; i + 8 <= count; i += 8)
						_mm256_storeu_ps(d + i, _mm256_ceil_ps(_mm256_loadu_ps(s + i)));
					break;
				case unary_round:
					for (; i + 8 <= count; i += 8)
						_mm256_storeu_ps(d + i, _mm256_round_ps(_mm256_loadu_ps(s + i), _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC));
					break;
				case unary_trunc:
					for (; i + 8 <= count; i += 8)
						_mm256_storeu_ps(d + i, _mm256_round_ps(_mm256_loadu_ps(s + i), _MM_FROUND_TO_ZERO | _MM_FROUND_NO_EXC));
					break;
				case unary_expm1:
					for (; i + 8 <= count; i += 8)
						_mm256_storeu_ps(d + i, _mm256_expm1_ps(_mm256_loadu_ps(s + i)));
					break;
				case unary_log1p:
					for (; i + 8 <= count; i += 8)
						_mm256_storeu_ps(d + i, _mm256_log1p_ps(_mm256_loadu_ps(s + i)));
					break;
				case unary_erf:
					for (; i + 8 <= count; i += 8)
						_mm256_storeu_ps(d + i, _mm256_erf_ps(_mm256_loadu_ps(s + i)));
					break;
				case unary_erfc:
					for (; i + 8 <= count; i += 8)
						_mm256_storeu_ps(d + i, _mm256_erfc_ps(_mm256_loadu_ps(s + i)));
					break;
				case unary_tgamma:   // SVML 无 tgamma，回退标量桩
					return false;
				case unary_lgamma:   // SVML 无 lgamma，回退标量桩
					return false;
				case unary_abs:
					for (; i + 8 <= count; i += 8)
						_mm256_storeu_ps(d + i, _mm256_andnot_ps(_mm256_set1_ps(-0.0f), _mm256_loadu_ps(s + i)));
					break;
				case unary_sign: {
					__m256 zero = _mm256_setzero_ps(), ones = _mm256_set1_ps(1.0f), mone = _mm256_set1_ps(-1.0f);
					for (; i + 8 <= count; i += 8) {
						__m256 v = _mm256_loadu_ps(s + i);
						__m256 r = _mm256_blendv_ps(zero, mone, _mm256_cmp_ps(v, zero, _CMP_LT_OQ));
						_mm256_storeu_ps(d + i, _mm256_blendv_ps(r, ones, _mm256_cmp_ps(v, zero, _CMP_GT_OQ)));
					}
					break;
				}
				case unary_exp2:
					for (; i + 8 <= count; i += 8)
						_mm256_storeu_ps(d + i, _mm256_exp2_ps(_mm256_loadu_ps(s + i)));
					break;
				case unary_cbrt:
					for (; i + 8 <= count; i += 8)
						_mm256_storeu_ps(d + i, _mm256_cbrt_ps(_mm256_loadu_ps(s + i)));
					break;
				case unary_rsqrt: {
					__m256 one = _mm256_set1_ps(1.0f);
					for (; i + 8 <= count; i += 8)
						_mm256_storeu_ps(d + i, _mm256_div_ps(one, _mm256_sqrt_ps(_mm256_loadu_ps(s + i))));
					break;
				}
				case unary_recip: {
					__m256 one = _mm256_set1_ps(1.0f);
					for (; i + 8 <= count; i += 8)
						_mm256_storeu_ps(d + i, _mm256_div_ps(one, _mm256_loadu_ps(s + i)));
					break;
				}
				case unary_neg: {
					__m256 zero = _mm256_setzero_ps();
					for (; i + 8 <= count; i += 8)
						_mm256_storeu_ps(d + i, _mm256_sub_ps(zero, _mm256_loadu_ps(s + i)));
					break;
				}
				case unary_square:
					for (; i + 8 <= count; i += 8)
						_mm256_storeu_ps(d + i, _mm256_mul_ps(_mm256_loadu_ps(s + i), _mm256_loadu_ps(s + i)));
					break;
				case unary_isnan:
					for (; i + 8 <= count; i += 8)
					{
						__m256 v = _mm256_loadu_ps(s + i);
						_mm256_storeu_ps(d + i, _mm256_and_ps(_mm256_cmp_ps(v, v, _CMP_UNORD_Q), _mm256_set1_ps(1.0f)));
					}
					break;
				case unary_isinf: {
					__m256 inf = _mm256_castsi256_ps(_mm256_set1_epi32(0x7F800000));
					__m256 absm = _mm256_set1_ps(-0.0f);
					for (; i + 8 <= count; i += 8)
					{
						__m256 v = _mm256_loadu_ps(s + i);
						__m256 m = _mm256_and_ps(_mm256_cmp_ps(v, v, _CMP_EQ_OQ),
							_mm256_cmp_ps(_mm256_andnot_ps(absm, v), inf, _CMP_EQ_OQ));
						_mm256_storeu_ps(d + i, _mm256_and_ps(m, _mm256_set1_ps(1.0f)));
					}
					break;
				}
				case unary_isfinite: {
					__m256 inf = _mm256_castsi256_ps(_mm256_set1_epi32(0x7F800000));
					__m256 absm = _mm256_set1_ps(-0.0f);
					for (; i + 8 <= count; i += 8)
					{
						__m256 v = _mm256_loadu_ps(s + i);
						__m256 m = _mm256_and_ps(_mm256_cmp_ps(v, v, _CMP_ORD_Q),
							_mm256_cmp_ps(_mm256_andnot_ps(absm, v), inf, _CMP_NEQ_OQ));
						_mm256_storeu_ps(d + i, _mm256_and_ps(m, _mm256_set1_ps(1.0f)));
					}
					break;
				}
				default: return false;
				}
				for (; i < count; ++i) d[i] = unary_eval(s[i], op);
				return true;
			}
			inline bool unary_pd(void* dst, const void* a, std::size_t count, int op)
			{
				double* d = (double*)dst; const double* s = (const double*)a;
				std::size_t i = 0;
				switch (op)
				{
				case unary_relu: {
					__m256d zero = _mm256_setzero_pd();
					for (; i + 4 <= count; i += 4)
						_mm256_storeu_pd(d + i, _mm256_max_pd(_mm256_loadu_pd(s + i), zero));
					break;
				}
				case unary_leaky_relu: {
					__m256d zero = _mm256_setzero_pd(), slope = _mm256_set1_pd(0.01);
					for (; i + 4 <= count; i += 4) {
						__m256d v = _mm256_loadu_pd(s + i);
						_mm256_storeu_pd(d + i, _mm256_blendv_pd(_mm256_mul_pd(v, slope), v, _mm256_cmp_pd(v, zero, _CMP_GT_OQ)));
					}
					break;
				}
				case unary_sqrt:
					for (; i + 4 <= count; i += 4)
						_mm256_storeu_pd(d + i, _mm256_sqrt_pd(_mm256_loadu_pd(s + i)));
					break;
				case unary_exp:
					for (; i + 4 <= count; i += 4)
						_mm256_storeu_pd(d + i, _mm256_exp_pd(_mm256_loadu_pd(s + i)));
					break;
				case unary_log:
					for (; i + 4 <= count; i += 4)
						_mm256_storeu_pd(d + i, _mm256_log_pd(_mm256_loadu_pd(s + i)));
					break;
				case unary_tanh:
					for (; i + 4 <= count; i += 4)
						_mm256_storeu_pd(d + i, _mm256_tanh_pd(_mm256_loadu_pd(s + i)));
					break;
				case unary_sigmoid: {
					__m256d one = _mm256_set1_pd(1.0), zero = _mm256_setzero_pd();
					for (; i + 4 <= count; i += 4) {
						__m256d e = _mm256_exp_pd(_mm256_sub_pd(zero, _mm256_loadu_pd(s + i)));
						_mm256_storeu_pd(d + i, _mm256_div_pd(one, _mm256_add_pd(one, e)));
					}
					break;
				}
				case unary_silu: {
					__m256d one = _mm256_set1_pd(1.0), zero = _mm256_setzero_pd();
					for (; i + 4 <= count; i += 4) {
						__m256d v = _mm256_loadu_pd(s + i);
						__m256d e = _mm256_exp_pd(_mm256_sub_pd(zero, v));
						_mm256_storeu_pd(d + i, _mm256_mul_pd(v, _mm256_div_pd(one, _mm256_add_pd(one, e))));
					}
					break;
				}
				case unary_gelu: {
					__m256d half = _mm256_set1_pd(0.5), one = _mm256_set1_pd(1.0);
					__m256d c = _mm256_set1_pd(0.7978845608028654), k = _mm256_set1_pd(0.044715);
					for (; i + 4 <= count; i += 4) {
						__m256d v = _mm256_loadu_pd(s + i);
						__m256d x3 = _mm256_mul_pd(_mm256_mul_pd(v, v), v);
						__m256d t = _mm256_tanh_pd(_mm256_mul_pd(c, _mm256_add_pd(v, _mm256_mul_pd(k, x3))));
						_mm256_storeu_pd(d + i, _mm256_mul_pd(_mm256_mul_pd(half, v), _mm256_add_pd(one, t)));
					}
					break;
				}
				case unary_sin:
					for (; i + 4 <= count; i += 4)
						_mm256_storeu_pd(d + i, _mm256_sin_pd(_mm256_loadu_pd(s + i)));
					break;
				case unary_cos:
					for (; i + 4 <= count; i += 4)
						_mm256_storeu_pd(d + i, _mm256_cos_pd(_mm256_loadu_pd(s + i)));
					break;
				case unary_tan:
					for (; i + 4 <= count; i += 4)
						_mm256_storeu_pd(d + i, _mm256_tan_pd(_mm256_loadu_pd(s + i)));
					break;
				case unary_asin:
					for (; i + 4 <= count; i += 4)
						_mm256_storeu_pd(d + i, _mm256_asin_pd(_mm256_loadu_pd(s + i)));
					break;
				case unary_acos:
					for (; i + 4 <= count; i += 4)
						_mm256_storeu_pd(d + i, _mm256_acos_pd(_mm256_loadu_pd(s + i)));
					break;
				case unary_atan:
					for (; i + 4 <= count; i += 4)
						_mm256_storeu_pd(d + i, _mm256_atan_pd(_mm256_loadu_pd(s + i)));
					break;
				case unary_sinh:
					for (; i + 4 <= count; i += 4)
						_mm256_storeu_pd(d + i, _mm256_sinh_pd(_mm256_loadu_pd(s + i)));
					break;
				case unary_cosh:
					for (; i + 4 <= count; i += 4)
						_mm256_storeu_pd(d + i, _mm256_cosh_pd(_mm256_loadu_pd(s + i)));
					break;
				case unary_asinh:
					for (; i + 4 <= count; i += 4)
						_mm256_storeu_pd(d + i, _mm256_asinh_pd(_mm256_loadu_pd(s + i)));
					break;
				case unary_acosh:
					for (; i + 4 <= count; i += 4)
						_mm256_storeu_pd(d + i, _mm256_acosh_pd(_mm256_loadu_pd(s + i)));
					break;
				case unary_atanh:
					for (; i + 4 <= count; i += 4)
						_mm256_storeu_pd(d + i, _mm256_atanh_pd(_mm256_loadu_pd(s + i)));
					break;
				case unary_log10:
					for (; i + 4 <= count; i += 4)
						_mm256_storeu_pd(d + i, _mm256_log10_pd(_mm256_loadu_pd(s + i)));
					break;
				case unary_log2:
					for (; i + 4 <= count; i += 4)
						_mm256_storeu_pd(d + i, _mm256_log2_pd(_mm256_loadu_pd(s + i)));
					break;
				case unary_floor:
					for (; i + 4 <= count; i += 4)
						_mm256_storeu_pd(d + i, _mm256_floor_pd(_mm256_loadu_pd(s + i)));
					break;
				case unary_ceil:
					for (; i + 4 <= count; i += 4)
						_mm256_storeu_pd(d + i, _mm256_ceil_pd(_mm256_loadu_pd(s + i)));
					break;
				case unary_round:
					for (; i + 4 <= count; i += 4)
						_mm256_storeu_pd(d + i, _mm256_round_pd(_mm256_loadu_pd(s + i), _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC));
					break;
				case unary_trunc:
					for (; i + 4 <= count; i += 4)
						_mm256_storeu_pd(d + i, _mm256_round_pd(_mm256_loadu_pd(s + i), _MM_FROUND_TO_ZERO | _MM_FROUND_NO_EXC));
					break;
				case unary_expm1:
					for (; i + 4 <= count; i += 4)
						_mm256_storeu_pd(d + i, _mm256_expm1_pd(_mm256_loadu_pd(s + i)));
					break;
				case unary_log1p:
					for (; i + 4 <= count; i += 4)
						_mm256_storeu_pd(d + i, _mm256_log1p_pd(_mm256_loadu_pd(s + i)));
					break;
				case unary_erf:
					for (; i + 4 <= count; i += 4)
						_mm256_storeu_pd(d + i, _mm256_erf_pd(_mm256_loadu_pd(s + i)));
					break;
				case unary_erfc:
					for (; i + 4 <= count; i += 4)
						_mm256_storeu_pd(d + i, _mm256_erfc_pd(_mm256_loadu_pd(s + i)));
					break;
				case unary_tgamma:   // SVML 无 tgamma，回退标量桩
					return false;
				case unary_lgamma:   // SVML 无 lgamma，回退标量桩
					return false;
				case unary_abs:
					for (; i + 4 <= count; i += 4)
						_mm256_storeu_pd(d + i, _mm256_andnot_pd(_mm256_set1_pd(-0.0), _mm256_loadu_pd(s + i)));
					break;
				case unary_sign: {
					__m256d zero = _mm256_setzero_pd(), ones = _mm256_set1_pd(1.0), mone = _mm256_set1_pd(-1.0);
					for (; i + 4 <= count; i += 4) {
						__m256d v = _mm256_loadu_pd(s + i);
						__m256d r = _mm256_blendv_pd(zero, mone, _mm256_cmp_pd(v, zero, _CMP_LT_OQ));
						_mm256_storeu_pd(d + i, _mm256_blendv_pd(r, ones, _mm256_cmp_pd(v, zero, _CMP_GT_OQ)));
					}
					break;
				}
				case unary_exp2:
					for (; i + 4 <= count; i += 4)
						_mm256_storeu_pd(d + i, _mm256_exp2_pd(_mm256_loadu_pd(s + i)));
					break;
				case unary_cbrt:
					for (; i + 4 <= count; i += 4)
						_mm256_storeu_pd(d + i, _mm256_cbrt_pd(_mm256_loadu_pd(s + i)));
					break;
				case unary_rsqrt: {
					__m256d one = _mm256_set1_pd(1.0);
					for (; i + 4 <= count; i += 4)
						_mm256_storeu_pd(d + i, _mm256_div_pd(one, _mm256_sqrt_pd(_mm256_loadu_pd(s + i))));
					break;
				}
				case unary_recip: {
					__m256d one = _mm256_set1_pd(1.0);
					for (; i + 4 <= count; i += 4)
						_mm256_storeu_pd(d + i, _mm256_div_pd(one, _mm256_loadu_pd(s + i)));
					break;
				}
				case unary_neg: {
					__m256d zero = _mm256_setzero_pd();
					for (; i + 4 <= count; i += 4)
						_mm256_storeu_pd(d + i, _mm256_sub_pd(zero, _mm256_loadu_pd(s + i)));
					break;
				}
				case unary_square:
					for (; i + 4 <= count; i += 4)
						_mm256_storeu_pd(d + i, _mm256_mul_pd(_mm256_loadu_pd(s + i), _mm256_loadu_pd(s + i)));
					break;
				case unary_isnan:
					for (; i + 4 <= count; i += 4)
					{
						__m256d v = _mm256_loadu_pd(s + i);
						_mm256_storeu_pd(d + i, _mm256_and_pd(_mm256_cmp_pd(v, v, _CMP_UNORD_Q), _mm256_set1_pd(1.0)));
					}
					break;
				case unary_isinf: {
					__m256d inf = _mm256_castsi256_pd(_mm256_set1_epi64x(0x7FF0000000000000LL));
					__m256d absm = _mm256_set1_pd(-0.0);
					for (; i + 4 <= count; i += 4)
					{
						__m256d v = _mm256_loadu_pd(s + i);
						__m256d m = _mm256_and_pd(_mm256_cmp_pd(v, v, _CMP_EQ_OQ),
							_mm256_cmp_pd(_mm256_andnot_pd(absm, v), inf, _CMP_EQ_OQ));
						_mm256_storeu_pd(d + i, _mm256_and_pd(m, _mm256_set1_pd(1.0)));
					}
					break;
				}
				case unary_isfinite: {
					__m256d inf = _mm256_castsi256_pd(_mm256_set1_epi64x(0x7FF0000000000000LL));
					__m256d absm = _mm256_set1_pd(-0.0);
					for (; i + 4 <= count; i += 4)
					{
						__m256d v = _mm256_loadu_pd(s + i);
						__m256d m = _mm256_and_pd(_mm256_cmp_pd(v, v, _CMP_ORD_Q),
							_mm256_cmp_pd(_mm256_andnot_pd(absm, v), inf, _CMP_NEQ_OQ));
						_mm256_storeu_pd(d + i, _mm256_and_pd(m, _mm256_set1_pd(1.0)));
					}
					break;
				}
				default: return false;
				}
				for (; i < count; ++i) d[i] = unary_eval(s[i], op);
				return true;
			}

			// —— 反向逐元素累加：dst += f(a,b)（op 见 grad_op）——
			inline void grad_accum_ps(void* dst, const void* a, const void* b, std::size_t count, int op)
			{
				float* d = (float*)dst; const float* pa = (const float*)a; const float* pb = (const float*)b;
				std::size_t i = 0;
				__m256 zero = _mm256_setzero_ps(), one = _mm256_set1_ps(1.0f), slope = _mm256_set1_ps(0.01f);
				for (; i + 8 <= count; i += 8)
				{
					__m256 va = _mm256_loadu_ps(pa + i);
					__m256 vb = _mm256_loadu_ps(pb + i);
					__m256 g;
					switch (op)
					{
					case grad_relu: g = _mm256_blendv_ps(zero, va, _mm256_cmp_ps(vb, zero, _CMP_GT_OQ)); break;
					case grad_leaky_relu: g = _mm256_blendv_ps(_mm256_mul_ps(va, slope), va, _mm256_cmp_ps(vb, zero, _CMP_GT_OQ)); break;
					default: g = _mm256_mul_ps(_mm256_mul_ps(va, vb), _mm256_sub_ps(one, vb)); break;
					}
					_mm256_storeu_ps(d + i, _mm256_add_ps(_mm256_loadu_ps(d + i), g));
				}
				for (; i < count; ++i) d[i] += grad_eval(pa[i], pb[i], op);
			}
			inline void grad_accum_pd(void* dst, const void* a, const void* b, std::size_t count, int op)
			{
				double* d = (double*)dst; const double* pa = (const double*)a; const double* pb = (const double*)b;
				std::size_t i = 0;
				__m256d zero = _mm256_setzero_pd(), one = _mm256_set1_pd(1.0), slope = _mm256_set1_pd(0.01);
				for (; i + 4 <= count; i += 4)
				{
					__m256d va = _mm256_loadu_pd(pa + i);
					__m256d vb = _mm256_loadu_pd(pb + i);
					__m256d g;
					switch (op)
					{
					case grad_relu: g = _mm256_blendv_pd(zero, va, _mm256_cmp_pd(vb, zero, _CMP_GT_OQ)); break;
					case grad_leaky_relu: g = _mm256_blendv_pd(_mm256_mul_pd(va, slope), va, _mm256_cmp_pd(vb, zero, _CMP_GT_OQ)); break;
					default: g = _mm256_mul_pd(_mm256_mul_pd(va, vb), _mm256_sub_pd(one, vb)); break;
					}
					_mm256_storeu_pd(d + i, _mm256_add_pd(_mm256_loadu_pd(d + i), g));
				}
				for (; i < count; ++i) d[i] += grad_eval(pa[i], pb[i], op);
			}

			// —— 转置（4x4 块 SIMD + 边缘标量）——
			inline void transpose_ps(void* dst, const void* src, std::size_t rows, std::size_t cols)
			{
				float* d = (float*)dst; const float* s = (const float*)src;
				const std::size_t bi = (rows / 4) * 4, bj = (cols / 4) * 4;
				for (std::size_t i = 0; i + 4 <= rows; i += 4)
					for (std::size_t j = 0; j + 4 <= cols; j += 4)
					{
						__m128 r0 = _mm_loadu_ps(s + (i + 0) * cols + j);
						__m128 r1 = _mm_loadu_ps(s + (i + 1) * cols + j);
						__m128 r2 = _mm_loadu_ps(s + (i + 2) * cols + j);
						__m128 r3 = _mm_loadu_ps(s + (i + 3) * cols + j);
						_MM_TRANSPOSE4_PS(r0, r1, r2, r3);
						_mm_storeu_ps(d + (j + 0) * rows + i, r0);
						_mm_storeu_ps(d + (j + 1) * rows + i, r1);
						_mm_storeu_ps(d + (j + 2) * rows + i, r2);
						_mm_storeu_ps(d + (j + 3) * rows + i, r3);
					}
				for (std::size_t i = bi; i < rows; ++i)
					for (std::size_t j = 0; j < cols; ++j)
						d[j * rows + i] = s[i * cols + j];
				for (std::size_t i = 0; i < bi; ++i)
					for (std::size_t j = bj; j < cols; ++j)
						d[j * rows + i] = s[i * cols + j];
			}
			inline void transpose_pd(void* dst, const void* src, std::size_t rows, std::size_t cols)
			{
				double* d = (double*)dst; const double* s = (const double*)src;
				const std::size_t bi = (rows / 4) * 4, bj = (cols / 4) * 4;
				for (std::size_t i = 0; i + 4 <= rows; i += 4)
					for (std::size_t j = 0; j + 4 <= cols; j += 4)
					{
						__m256d r0 = _mm256_loadu_pd(s + (i + 0) * cols + j);
						__m256d r1 = _mm256_loadu_pd(s + (i + 1) * cols + j);
						__m256d r2 = _mm256_loadu_pd(s + (i + 2) * cols + j);
						__m256d r3 = _mm256_loadu_pd(s + (i + 3) * cols + j);
						__m256d t0 = _mm256_unpacklo_pd(r0, r1);
						__m256d t1 = _mm256_unpackhi_pd(r0, r1);
						__m256d t2 = _mm256_unpacklo_pd(r2, r3);
						__m256d t3 = _mm256_unpackhi_pd(r2, r3);
						_mm256_storeu_pd(d + (j + 0) * rows + i, _mm256_permute2f128_pd(t0, t2, 0x20));
						_mm256_storeu_pd(d + (j + 1) * rows + i, _mm256_permute2f128_pd(t0, t2, 0x31));
						_mm256_storeu_pd(d + (j + 2) * rows + i, _mm256_permute2f128_pd(t1, t3, 0x20));
						_mm256_storeu_pd(d + (j + 3) * rows + i, _mm256_permute2f128_pd(t1, t3, 0x31));
					}
				for (std::size_t i = bi; i < rows; ++i)
					for (std::size_t j = 0; j < cols; ++j)
						d[j * rows + i] = s[i * cols + j];
				for (std::size_t i = 0; i < bi; ++i)
					for (std::size_t j = bj; j < cols; ++j)
						d[j * rows + i] = s[i * cols + j];
			}
			// i32 转置：4x4 分块 __m128i（unpack）转置 + 边缘标量
			inline void transpose_i32(void* dst, const void* src, std::size_t rows, std::size_t cols)
			{
				std::int32_t* d = (std::int32_t*)dst; const std::int32_t* s = (const std::int32_t*)src;
				const std::size_t bi = (rows / 4) * 4, bj = (cols / 4) * 4;
				for (std::size_t i = 0; i + 4 <= rows; i += 4)
					for (std::size_t j = 0; j + 4 <= cols; j += 4)
					{
						__m128i r0 = _mm_loadu_si128((const __m128i*)(s + (i + 0) * cols + j));
						__m128i r1 = _mm_loadu_si128((const __m128i*)(s + (i + 1) * cols + j));
						__m128i r2 = _mm_loadu_si128((const __m128i*)(s + (i + 2) * cols + j));
						__m128i r3 = _mm_loadu_si128((const __m128i*)(s + (i + 3) * cols + j));
						__m128i t0 = _mm_unpacklo_epi32(r0, r1);
						__m128i t1 = _mm_unpackhi_epi32(r0, r1);
						__m128i t2 = _mm_unpacklo_epi32(r2, r3);
						__m128i t3 = _mm_unpackhi_epi32(r2, r3);
						_mm_storeu_si128((__m128i*)(d + (j + 0) * rows + i), _mm_unpacklo_epi64(t0, t2));
						_mm_storeu_si128((__m128i*)(d + (j + 1) * rows + i), _mm_unpackhi_epi64(t0, t2));
						_mm_storeu_si128((__m128i*)(d + (j + 2) * rows + i), _mm_unpacklo_epi64(t1, t3));
						_mm_storeu_si128((__m128i*)(d + (j + 3) * rows + i), _mm_unpackhi_epi64(t1, t3));
					}
				for (std::size_t i = bi; i < rows; ++i)
					for (std::size_t j = 0; j < cols; ++j)
						d[j * rows + i] = s[i * cols + j];
				for (std::size_t i = 0; i < bi; ++i)
					for (std::size_t j = bj; j < cols; ++j)
						d[j * rows + i] = s[i * cols + j];
			}
			// i16 转置：8x8 分块 __m128i（epi16→epi32→epi64 三级 unpack）+ 边缘标量
			inline void transpose_i16(void* dst, const void* src, std::size_t rows, std::size_t cols)
			{
				std::int16_t* d = (std::int16_t*)dst; const std::int16_t* s = (const std::int16_t*)src;
				const std::size_t bi = (rows / 8) * 8, bj = (cols / 8) * 8;
				for (std::size_t i = 0; i + 8 <= rows; i += 8)
					for (std::size_t j = 0; j + 8 <= cols; j += 8)
					{
						__m128i r0 = _mm_loadu_si128((const __m128i*)(s + (i + 0) * cols + j));
						__m128i r1 = _mm_loadu_si128((const __m128i*)(s + (i + 1) * cols + j));
						__m128i r2 = _mm_loadu_si128((const __m128i*)(s + (i + 2) * cols + j));
						__m128i r3 = _mm_loadu_si128((const __m128i*)(s + (i + 3) * cols + j));
						__m128i r4 = _mm_loadu_si128((const __m128i*)(s + (i + 4) * cols + j));
						__m128i r5 = _mm_loadu_si128((const __m128i*)(s + (i + 5) * cols + j));
						__m128i r6 = _mm_loadu_si128((const __m128i*)(s + (i + 6) * cols + j));
						__m128i r7 = _mm_loadu_si128((const __m128i*)(s + (i + 7) * cols + j));
						__m128i t0 = _mm_unpacklo_epi16(r0, r1), t1 = _mm_unpackhi_epi16(r0, r1);
						__m128i t2 = _mm_unpacklo_epi16(r2, r3), t3 = _mm_unpackhi_epi16(r2, r3);
						__m128i t4 = _mm_unpacklo_epi16(r4, r5), t5 = _mm_unpackhi_epi16(r4, r5);
						__m128i t6 = _mm_unpacklo_epi16(r6, r7), t7 = _mm_unpackhi_epi16(r6, r7);
						__m128i u0 = _mm_unpacklo_epi32(t0, t2), u1 = _mm_unpackhi_epi32(t0, t2);
						__m128i u2 = _mm_unpacklo_epi32(t1, t3), u3 = _mm_unpackhi_epi32(t1, t3);
						__m128i u4 = _mm_unpacklo_epi32(t4, t6), u5 = _mm_unpackhi_epi32(t4, t6);
						__m128i u6 = _mm_unpacklo_epi32(t5, t7), u7 = _mm_unpackhi_epi32(t5, t7);
						_mm_storeu_si128((__m128i*)(d + (j + 0) * rows + i), _mm_unpacklo_epi64(u0, u4));
						_mm_storeu_si128((__m128i*)(d + (j + 1) * rows + i), _mm_unpackhi_epi64(u0, u4));
						_mm_storeu_si128((__m128i*)(d + (j + 2) * rows + i), _mm_unpacklo_epi64(u1, u5));
						_mm_storeu_si128((__m128i*)(d + (j + 3) * rows + i), _mm_unpackhi_epi64(u1, u5));
						_mm_storeu_si128((__m128i*)(d + (j + 4) * rows + i), _mm_unpacklo_epi64(u2, u6));
						_mm_storeu_si128((__m128i*)(d + (j + 5) * rows + i), _mm_unpackhi_epi64(u2, u6));
						_mm_storeu_si128((__m128i*)(d + (j + 6) * rows + i), _mm_unpacklo_epi64(u3, u7));
						_mm_storeu_si128((__m128i*)(d + (j + 7) * rows + i), _mm_unpackhi_epi64(u3, u7));
					}
				for (std::size_t i = bi; i < rows; ++i)
					for (std::size_t j = 0; j < cols; ++j)
						d[j * rows + i] = s[i * cols + j];
				for (std::size_t i = 0; i < bi; ++i)
					for (std::size_t j = bj; j < cols; ++j)
						d[j * rows + i] = s[i * cols + j];
			}
			// i8 转置：16x16 分块 __m128i（epi8→epi16→epi32→epi64 四级 unpack）+ 边缘标量
			inline void transpose_i8(void* dst, const void* src, std::size_t rows, std::size_t cols)
			{
				std::int8_t* d = (std::int8_t*)dst; const std::int8_t* s = (const std::int8_t*)src;
				const std::size_t bi = (rows / 16) * 16, bj = (cols / 16) * 16;
				for (std::size_t i = 0; i + 16 <= rows; i += 16)
					for (std::size_t j = 0; j + 16 <= cols; j += 16)
					{
						__m128i r0 = _mm_loadu_si128((const __m128i*)(s + (i + 0) * cols + j));
						__m128i r1 = _mm_loadu_si128((const __m128i*)(s + (i + 1) * cols + j));
						__m128i r2 = _mm_loadu_si128((const __m128i*)(s + (i + 2) * cols + j));
						__m128i r3 = _mm_loadu_si128((const __m128i*)(s + (i + 3) * cols + j));
						__m128i r4 = _mm_loadu_si128((const __m128i*)(s + (i + 4) * cols + j));
						__m128i r5 = _mm_loadu_si128((const __m128i*)(s + (i + 5) * cols + j));
						__m128i r6 = _mm_loadu_si128((const __m128i*)(s + (i + 6) * cols + j));
						__m128i r7 = _mm_loadu_si128((const __m128i*)(s + (i + 7) * cols + j));
						__m128i r8 = _mm_loadu_si128((const __m128i*)(s + (i + 8) * cols + j));
						__m128i r9 = _mm_loadu_si128((const __m128i*)(s + (i + 9) * cols + j));
						__m128i ra = _mm_loadu_si128((const __m128i*)(s + (i + 10) * cols + j));
						__m128i rb = _mm_loadu_si128((const __m128i*)(s + (i + 11) * cols + j));
						__m128i rc = _mm_loadu_si128((const __m128i*)(s + (i + 12) * cols + j));
						__m128i rd = _mm_loadu_si128((const __m128i*)(s + (i + 13) * cols + j));
						__m128i re = _mm_loadu_si128((const __m128i*)(s + (i + 14) * cols + j));
						__m128i rf = _mm_loadu_si128((const __m128i*)(s + (i + 15) * cols + j));
						__m128i t0 = _mm_unpacklo_epi8(r0, r1), t1 = _mm_unpackhi_epi8(r0, r1);
						__m128i t2 = _mm_unpacklo_epi8(r2, r3), t3 = _mm_unpackhi_epi8(r2, r3);
						__m128i t4 = _mm_unpacklo_epi8(r4, r5), t5 = _mm_unpackhi_epi8(r4, r5);
						__m128i t6 = _mm_unpacklo_epi8(r6, r7), t7 = _mm_unpackhi_epi8(r6, r7);
						__m128i t8 = _mm_unpacklo_epi8(r8, r9), t9 = _mm_unpackhi_epi8(r8, r9);
						__m128i ta = _mm_unpacklo_epi8(ra, rb), tb = _mm_unpackhi_epi8(ra, rb);
						__m128i tc = _mm_unpacklo_epi8(rc, rd), td = _mm_unpackhi_epi8(rc, rd);
						__m128i te = _mm_unpacklo_epi8(re, rf), tf = _mm_unpackhi_epi8(re, rf);
						__m128i u0 = _mm_unpacklo_epi16(t0, t2), u1 = _mm_unpackhi_epi16(t0, t2);
						__m128i u2 = _mm_unpacklo_epi16(t1, t3), u3 = _mm_unpackhi_epi16(t1, t3);
						__m128i u4 = _mm_unpacklo_epi16(t4, t6), u5 = _mm_unpackhi_epi16(t4, t6);
						__m128i u6 = _mm_unpacklo_epi16(t5, t7), u7 = _mm_unpackhi_epi16(t5, t7);
						__m128i u8 = _mm_unpacklo_epi16(t8, ta), u9 = _mm_unpackhi_epi16(t8, ta);
						__m128i ua = _mm_unpacklo_epi16(t9, tb), ub = _mm_unpackhi_epi16(t9, tb);
						__m128i uc = _mm_unpacklo_epi16(tc, te), ud = _mm_unpackhi_epi16(tc, te);
						__m128i ue = _mm_unpacklo_epi16(td, tf), uf = _mm_unpackhi_epi16(td, tf);
						__m128i v0 = _mm_unpacklo_epi32(u0, u4), v1 = _mm_unpackhi_epi32(u0, u4);
						__m128i v2 = _mm_unpacklo_epi32(u1, u5), v3 = _mm_unpackhi_epi32(u1, u5);
						__m128i v4 = _mm_unpacklo_epi32(u2, u6), v5 = _mm_unpackhi_epi32(u2, u6);
						__m128i v6 = _mm_unpacklo_epi32(u3, u7), v7 = _mm_unpackhi_epi32(u3, u7);
						__m128i v8 = _mm_unpacklo_epi32(u8, uc), v9 = _mm_unpackhi_epi32(u8, uc);
						__m128i va = _mm_unpacklo_epi32(u9, ud), vb = _mm_unpackhi_epi32(u9, ud);
						__m128i vc = _mm_unpacklo_epi32(ua, ue), vd = _mm_unpackhi_epi32(ua, ue);
						__m128i ve = _mm_unpacklo_epi32(ub, uf), vf = _mm_unpackhi_epi32(ub, uf);
						_mm_storeu_si128((__m128i*)(d + (j + 0) * rows + i), _mm_unpacklo_epi64(v0, v8));
						_mm_storeu_si128((__m128i*)(d + (j + 1) * rows + i), _mm_unpackhi_epi64(v0, v8));
						_mm_storeu_si128((__m128i*)(d + (j + 2) * rows + i), _mm_unpacklo_epi64(v1, v9));
						_mm_storeu_si128((__m128i*)(d + (j + 3) * rows + i), _mm_unpackhi_epi64(v1, v9));
						_mm_storeu_si128((__m128i*)(d + (j + 4) * rows + i), _mm_unpacklo_epi64(v2, va));
						_mm_storeu_si128((__m128i*)(d + (j + 5) * rows + i), _mm_unpackhi_epi64(v2, va));
						_mm_storeu_si128((__m128i*)(d + (j + 6) * rows + i), _mm_unpacklo_epi64(v3, vb));
						_mm_storeu_si128((__m128i*)(d + (j + 7) * rows + i), _mm_unpackhi_epi64(v3, vb));
						_mm_storeu_si128((__m128i*)(d + (j + 8) * rows + i), _mm_unpacklo_epi64(v4, vc));
						_mm_storeu_si128((__m128i*)(d + (j + 9) * rows + i), _mm_unpackhi_epi64(v4, vc));
						_mm_storeu_si128((__m128i*)(d + (j + 10) * rows + i), _mm_unpacklo_epi64(v5, vd));
						_mm_storeu_si128((__m128i*)(d + (j + 11) * rows + i), _mm_unpackhi_epi64(v5, vd));
						_mm_storeu_si128((__m128i*)(d + (j + 12) * rows + i), _mm_unpacklo_epi64(v6, ve));
						_mm_storeu_si128((__m128i*)(d + (j + 13) * rows + i), _mm_unpackhi_epi64(v6, ve));
						_mm_storeu_si128((__m128i*)(d + (j + 14) * rows + i), _mm_unpacklo_epi64(v7, vf));
						_mm_storeu_si128((__m128i*)(d + (j + 15) * rows + i), _mm_unpackhi_epi64(v7, vf));
					}
				for (std::size_t i = bi; i < rows; ++i)
					for (std::size_t j = 0; j < cols; ++j)
						d[j * rows + i] = s[i * cols + j];
				for (std::size_t i = 0; i < bi; ++i)
					for (std::size_t j = bj; j < cols; ++j)
						d[j * rows + i] = s[i * cols + j];
			}
			// f16 一元：转 f32 复用 f32 SIMD（SVML），再转回
			inline bool unary_f16(void* dst, const void* a, std::size_t count, int op)
			{
				xhqm::f16* d = (xhqm::f16*)dst;
				const xhqm::f16* s = (const xhqm::f16*)a;
				std::vector<float> sf(count), df(count);
				for (std::size_t i = 0; i < count; ++i) sf[i] = (float)s[i];
				if (!unary_ps(df.data(), sf.data(), count, op)) return false;
				for (std::size_t i = 0; i < count; ++i) d[i] = (xhqm::f16)df[i];
				return true;
			}
			// f16 转置：转 f32 复用 f32 4×4 分块转置，再转回
			inline void transpose_f16(void* dst, const void* src, std::size_t rows, std::size_t cols)
			{
				xhqm::f16* d = (xhqm::f16*)dst;
				const xhqm::f16* s = (const xhqm::f16*)src;
				std::vector<float> sf(rows * cols), df(rows * cols);
				for (std::size_t i = 0; i < rows * cols; ++i) sf[i] = (float)s[i];
				transpose_ps(df.data(), sf.data(), rows, cols);
				for (std::size_t i = 0; i < rows * cols; ++i) d[i] = (xhqm::f16)df[i];
			}
			// f16 反向累加：转 f32 复用 f32 SIMD，再转回
			inline void grad_accum_f16(void* dst, const void* a, const void* b, std::size_t count, int op)
			{
				xhqm::f16* d = (xhqm::f16*)dst;
				const xhqm::f16* pa = (const xhqm::f16*)a;
				const xhqm::f16* pb = (const xhqm::f16*)b;
				std::vector<float> df(count), af(count), bf(count);
				for (std::size_t i = 0; i < count; ++i) { df[i] = (float)d[i]; af[i] = (float)pa[i]; bf[i] = (float)pb[i]; }
				grad_accum_ps(df.data(), af.data(), bf.data(), count, op);
				for (std::size_t i = 0; i < count; ++i) d[i] = (xhqm::f16)df[i];
			}

			// ================= 整型 / bool 后端（GF(2)） =================

			// int8 逐元素：add/sub 走 AVX2（epi8，32 通道）；mul/div 等回退标量
			inline bool elemwise_i8(void* dst, const void* a, const void* b, std::size_t count, int op)
			{
				if (op != elem_add && op != elem_sub) return false;
				xhqm::i8* d = (xhqm::i8*)dst; const xhqm::i8* x = (const xhqm::i8*)a; const xhqm::i8* y = (const xhqm::i8*)b;
				std::size_t i = 0;
				for (; i + 32 <= count; i += 32)
				{
					__m256i va = _mm256_loadu_si256((const __m256i*)(x + i));
					__m256i vb = _mm256_loadu_si256((const __m256i*)(y + i));
					__m256i vr = (op == elem_add) ? _mm256_add_epi8(va, vb) : _mm256_sub_epi8(va, vb);
					_mm256_storeu_si256((__m256i*)(d + i), vr);
				}
				for (; i < count; ++i) d[i] = elemwise_eval(x[i], y[i], op);
				return true;
			}
			// int16 逐元素：add/sub/mul 走 AVX2（epi16，16 通道，mullo）；div 等回退标量
			inline bool elemwise_i16(void* dst, const void* a, const void* b, std::size_t count, int op)
			{
				if (op != elem_add && op != elem_sub && op != elem_mul) return false;
				xhqm::i16* d = (xhqm::i16*)dst; const xhqm::i16* x = (const xhqm::i16*)a; const xhqm::i16* y = (const xhqm::i16*)b;
				std::size_t i = 0;
				for (; i + 16 <= count; i += 16)
				{
					__m256i va = _mm256_loadu_si256((const __m256i*)(x + i));
					__m256i vb = _mm256_loadu_si256((const __m256i*)(y + i));
					__m256i vr = (op == elem_add) ? _mm256_add_epi16(va, vb)
						: (op == elem_sub) ? _mm256_sub_epi16(va, vb) : _mm256_mullo_epi16(va, vb);
					_mm256_storeu_si256((__m256i*)(d + i), vr);
				}
				for (; i < count; ++i) d[i] = elemwise_eval(x[i], y[i], op);
				return true;
			}

			// int32 逐元素：add/sub/mul 走 AVX2；div/mod/rem 等回退标量（无整数 SIMD 除法）
			inline bool elemwise_i32(void* dst, const void* a, const void* b, std::size_t count, int op)
			{
				if (op != elem_add && op != elem_sub && op != elem_mul) return false;
				xhqm::i32* d = (xhqm::i32*)dst; const xhqm::i32* x = (const xhqm::i32*)a; const xhqm::i32* y = (const xhqm::i32*)b;
				std::size_t i = 0;
				for (; i + 8 <= count; i += 8)
				{
					__m256i va = _mm256_loadu_si256((const __m256i*)(x + i));
					__m256i vb = _mm256_loadu_si256((const __m256i*)(y + i));
					__m256i vr = (op == elem_add) ? _mm256_add_epi32(va, vb)
						: (op == elem_sub) ? _mm256_sub_epi32(va, vb) : _mm256_mullo_epi32(va, vb);
					_mm256_storeu_si256((__m256i*)(d + i), vr);
				}
				for (; i < count; ++i) d[i] = elemwise_eval(x[i], y[i], op);
				return true;
			}
			// int64 逐元素：add/sub 走 AVX2（无 64 位乘法 SIMD），其余回退标量
			inline bool elemwise_i64(void* dst, const void* a, const void* b, std::size_t count, int op)
			{
				if (op != elem_add && op != elem_sub) return false;
				xhqm::i64* d = (xhqm::i64*)dst; const xhqm::i64* x = (const xhqm::i64*)a; const xhqm::i64* y = (const xhqm::i64*)b;
				std::size_t i = 0;
				for (; i + 4 <= count; i += 4)
				{
					__m256i va = _mm256_loadu_si256((const __m256i*)(x + i));
					__m256i vb = _mm256_loadu_si256((const __m256i*)(y + i));
					__m256i vr = (op == elem_add) ? _mm256_add_epi64(va, vb) : _mm256_sub_epi64(va, vb);
					_mm256_storeu_si256((__m256i*)(d + i), vr);
				}
				for (; i < count; ++i) d[i] = elemwise_eval(x[i], y[i], op);
				return true;
			}
			// bool 逐元素（GF(2)）：add/sub=XOR，mul/div=AND。
			// 用 unsigned char 访问 figure<bool> 底层字节（字符类型别名，规避严格别名 UB）；
			// bool 字节表示 0/1，字节级 XOR/AND 即 GF(2) 语义。
			inline bool elemwise_bool(void* dst, const void* a, const void* b, std::size_t count, int op)
			{
				const bool xorop = (op == elem_add || op == elem_sub);
				const bool andop = (op == elem_mul || op == elem_div);
				if (!xorop && !andop) return false;
				unsigned char* d = (unsigned char*)dst; const unsigned char* x = (const unsigned char*)a; const unsigned char* y = (const unsigned char*)b;
				std::size_t i = 0;
				for (; i + 32 <= count; i += 32)
				{
					__m256i va = _mm256_loadu_si256((const __m256i*)(x + i));
					__m256i vb = _mm256_loadu_si256((const __m256i*)(y + i));
					__m256i vr = xorop ? _mm256_xor_si256(va, vb) : _mm256_and_si256(va, vb);
					_mm256_storeu_si256((__m256i*)(d + i), vr);
				}
				for (; i < count; ++i)
					d[i] = xorop ? (unsigned char)(x[i] ^ y[i]) : (unsigned char)(x[i] & y[i]);
				return true;
			}

			// —— int32 矩阵乘单行（SIMD 点积：8 列一组，mullo+add；供 cpu 后端并行调度各行）——
			inline void matmul_row_i32(xhqm::i32* crow, const xhqm::i32* arow, const xhqm::i32* B, std::size_t n, std::size_t k)
			{
				std::size_t j = 0;
				for (; j + 8 <= n; j += 8)
				{
					__m256i vsum = _mm256_setzero_si256();
					for (std::size_t kk = 0; kk < k; ++kk)
					{
						__m256i va = _mm256_set1_epi32(arow[kk]);
						__m256i vb = _mm256_loadu_si256((const __m256i*)(B + kk * n + j));
						vsum = _mm256_add_epi32(vsum, _mm256_mullo_epi32(va, vb));
					}
					_mm256_storeu_si256((__m256i*)(crow + j), vsum);
				}
				for (; j < n; ++j)
				{
					xhqm::i32 acc = 0;
					for (std::size_t kk = 0; kk < k; ++kk) acc += arow[kk] * B[kk * n + j];
					crow[j] = acc;
				}
			}
			// —— int16 矩阵乘单行（16 位算术：mullo_epi16 + add_epi16，与标量截断语义一致）——
			inline void matmul_row_i16(xhqm::i16* crow, const xhqm::i16* arow, const xhqm::i16* B, std::size_t n, std::size_t k)
			{
				std::size_t j = 0;
				for (; j + 16 <= n; j += 16)
				{
					__m256i vsum = _mm256_setzero_si256();
					for (std::size_t kk = 0; kk < k; ++kk)
					{
						__m256i va = _mm256_set1_epi16(arow[kk]);
						__m256i vb = _mm256_loadu_si256((const __m256i*)(B + kk * n + j));
						vsum = _mm256_add_epi16(vsum, _mm256_mullo_epi16(va, vb));
					}
					_mm256_storeu_si256((__m256i*)(crow + j), vsum);
				}
				for (; j < n; ++j)
				{
					xhqm::i16 acc = 0;
					for (std::size_t kk = 0; kk < k; ++kk) acc += arow[kk] * B[kk * n + j];
					crow[j] = acc;
				}
			}
			// —— bool 矩阵乘单行（GF(2)：C = XOR_k (A[k] & B[k,:])，字节位运算，32 通道）——
			inline void matmul_row_bool(unsigned char* crow, const unsigned char* arow, const unsigned char* B, std::size_t n, std::size_t k)
			{
				std::size_t j = 0;
				for (; j + 32 <= n; j += 32)
				{
					__m256i vsum = _mm256_setzero_si256();
					for (std::size_t kk = 0; kk < k; ++kk)
					{
						__m256i va = _mm256_set1_epi8((int)arow[kk]);
						__m256i vb = _mm256_loadu_si256((const __m256i*)(B + kk * n + j));
						vsum = _mm256_xor_si256(vsum, _mm256_and_si256(va, vb));
					}
					_mm256_storeu_si256((__m256i*)(crow + j), vsum);
				}
				for (; j < n; ++j)
				{
					unsigned char acc = 0;
					for (std::size_t kk = 0; kk < k; ++kk) acc ^= (unsigned char)(arow[kk] & B[kk * n + j]);
					crow[j] = acc;
				}
			}

			// —— 整型 unary：neg(37)/square(38)/abs(31)/sign(32) ——
			inline bool unary_i32(void* dst, const void* a, std::size_t count, int op)
			{
				if (op != unary_neg && op != unary_square && op != unary_abs && op != unary_sign) return false;
				xhqm::i32* d = (xhqm::i32*)dst; const xhqm::i32* s = (const xhqm::i32*)a;
				const __m256i zero = _mm256_setzero_si256(), one = _mm256_set1_epi32(1);
				std::size_t i = 0;
				for (; i + 8 <= count; i += 8)
				{
					__m256i v = _mm256_loadu_si256((const __m256i*)(s + i));
					__m256i r;
					if (op == unary_neg) r = _mm256_sub_epi32(zero, v);
					else if (op == unary_square) r = _mm256_mullo_epi32(v, v);
					else if (op == unary_abs) r = _mm256_abs_epi32(v);
					else r = _mm256_sub_epi32(_mm256_and_si256(_mm256_cmpgt_epi32(v, zero), one),
						_mm256_and_si256(_mm256_cmpgt_epi32(zero, v), one));
					_mm256_storeu_si256((__m256i*)(d + i), r);
				}
				for (; i < count; ++i) d[i] = unary_eval(s[i], op);
				return true;
			}
			inline bool unary_i64(void* dst, const void* a, std::size_t count, int op)
			{
				if (op != unary_neg && op != unary_abs && op != unary_sign) return false;  // square 无 epi64 mul，回退
				xhqm::i64* d = (xhqm::i64*)dst; const xhqm::i64* s = (const xhqm::i64*)a;
				const __m256i zero = _mm256_setzero_si256(), one = _mm256_set1_epi64x(1);
				std::size_t i = 0;
				for (; i + 4 <= count; i += 4)
				{
					__m256i v = _mm256_loadu_si256((const __m256i*)(s + i));
					__m256i r;
					if (op == unary_neg) r = _mm256_sub_epi64(zero, v);
					else if (op == unary_abs) r = _mm256_blendv_epi8(v, _mm256_sub_epi64(zero, v), _mm256_cmpgt_epi64(zero, v));
					else r = _mm256_sub_epi64(_mm256_and_si256(_mm256_cmpgt_epi64(v, zero), one),
						_mm256_and_si256(_mm256_cmpgt_epi64(zero, v), one));
					_mm256_storeu_si256((__m256i*)(d + i), r);
				}
				for (; i < count; ++i) d[i] = unary_eval(s[i], op);
				return true;
			}
			inline bool unary_i16(void* dst, const void* a, std::size_t count, int op)
			{
				if (op != unary_neg && op != unary_square && op != unary_abs && op != unary_sign) return false;
				xhqm::i16* d = (xhqm::i16*)dst; const xhqm::i16* s = (const xhqm::i16*)a;
				const __m256i zero = _mm256_setzero_si256(), one = _mm256_set1_epi16(1);
				std::size_t i = 0;
				for (; i + 16 <= count; i += 16)
				{
					__m256i v = _mm256_loadu_si256((const __m256i*)(s + i));
					__m256i r;
					if (op == unary_neg) r = _mm256_sub_epi16(zero, v);
					else if (op == unary_square) r = _mm256_mullo_epi16(v, v);
					else if (op == unary_abs) r = _mm256_abs_epi16(v);
					else r = _mm256_sub_epi16(_mm256_and_si256(_mm256_cmpgt_epi16(v, zero), one),
						_mm256_and_si256(_mm256_cmpgt_epi16(zero, v), one));
					_mm256_storeu_si256((__m256i*)(d + i), r);
				}
				for (; i < count; ++i) d[i] = unary_eval(s[i], op);
				return true;
			}
			inline bool unary_i8(void* dst, const void* a, std::size_t count, int op)
			{
				if (op != unary_neg && op != unary_abs && op != unary_sign) return false;  // square 无 epi8 mul，回退
				xhqm::i8* d = (xhqm::i8*)dst; const xhqm::i8* s = (const xhqm::i8*)a;
				const __m256i zero = _mm256_setzero_si256(), one = _mm256_set1_epi8(1);
				std::size_t i = 0;
				for (; i + 32 <= count; i += 32)
				{
					__m256i v = _mm256_loadu_si256((const __m256i*)(s + i));
					__m256i r;
					if (op == unary_neg) r = _mm256_sub_epi8(zero, v);
					else if (op == unary_abs) r = _mm256_abs_epi8(v);
					else r = _mm256_sub_epi8(_mm256_and_si256(_mm256_cmpgt_epi8(v, zero), one),
						_mm256_and_si256(_mm256_cmpgt_epi8(zero, v), one));
					_mm256_storeu_si256((__m256i*)(d + i), r);
				}
				for (; i < count; ++i) d[i] = unary_eval(s[i], op);
				return true;
			}

			// —— 整型横向求和 ——
			inline xhqm::i32 hsum_epi32(__m256i v)
			{
				__m128i lo = _mm256_castsi256_si128(v);
				__m128i hi = _mm256_extracti128_si256(v, 1);
				__m128i s = _mm_add_epi32(lo, hi);
				s = _mm_add_epi32(s, _mm_shuffle_epi32(s, _MM_SHUFFLE(2, 3, 0, 1)));
				s = _mm_add_epi32(s, _mm_shuffle_epi32(s, _MM_SHUFFLE(1, 0, 3, 2)));
				return _mm_cvtsi128_si32(s);
			}
			inline xhqm::i64 hsum_epi64(__m256i v)
			{
				__m128i lo = _mm256_castsi256_si128(v);
				__m128i hi = _mm256_extracti128_si256(v, 1);
				__m128i s = _mm_add_epi64(lo, hi);
				s = _mm_add_epi64(s, _mm_unpackhi_epi64(s, s));
				return _mm_cvtsi128_si64(s);
			}
			inline xhqm::i16 hsum_epi16(__m256i v)
			{
				__m128i lo = _mm256_castsi256_si128(v);
				__m128i hi = _mm256_extracti128_si256(v, 1);
				__m128i s = _mm_add_epi16(lo, hi);                 // 8×i16
				__m128i m = _mm_madd_epi16(s, _mm_set1_epi16(1));  // 4×i32 两两求和
				m = _mm_add_epi32(m, _mm_shuffle_epi32(m, _MM_SHUFFLE(2, 3, 0, 1)));
				m = _mm_add_epi32(m, _mm_shuffle_epi32(m, _MM_SHUFFLE(1, 0, 3, 2)));
				return (xhqm::i16)_mm_cvtsi128_si32(m);
			}
			inline xhqm::i8 hsum_epi8(__m256i v)
			{
				__m256i m = _mm256_maddubs_epi16(v, _mm256_set1_epi16(0x0101));  // 16×i16 两两符号求和
				m = _mm256_madd_epi16(m, _mm256_set1_epi16(1));                 // 8×i32
				__m128i lo = _mm256_castsi256_si128(m);
				__m128i hi = _mm256_extracti128_si256(m, 1);
				__m128i s = _mm_add_epi32(lo, hi);
				s = _mm_add_epi32(s, _mm_shuffle_epi32(s, _MM_SHUFFLE(2, 3, 0, 1)));
				s = _mm_add_epi32(s, _mm_shuffle_epi32(s, _MM_SHUFFLE(1, 0, 3, 2)));
				return (xhqm::i8)_mm_cvtsi128_si32(s);
			}

			// int32 归约：sum 走 AVX2，max/min/prod 走标量
			inline bool reduce_i32(void* dst, const void* src, std::size_t outer, std::size_t inner, int op)
			{
				xhqm::i32* d = (xhqm::i32*)dst; const xhqm::i32* s = (const xhqm::i32*)src;
				for (std::size_t o = 0; o < outer; ++o)
				{
					const xhqm::i32* row = s + o * inner;
					xhqm::i32 acc = (op == reduce_op_prod) ? 1 : (op == reduce_op_max) ? xhqm::i32min : (op == reduce_op_min) ? xhqm::i32max : 0;
					if (op == reduce_op_sum)
					{
						__m256i vsum = _mm256_setzero_si256();
						std::size_t i = 0;
						for (; i + 8 <= inner; i += 8) vsum = _mm256_add_epi32(vsum, _mm256_loadu_si256((const __m256i*)(row + i)));
						acc += hsum_epi32(vsum);
						for (; i < inner; ++i) acc += row[i];
					}
					else
					{
						for (std::size_t i = 0; i < inner; ++i) acc = reduce_eval(acc, row[i], op);
					}
					d[o] = acc;
				}
				return true;
			}
			inline bool reduce_i64(void* dst, const void* src, std::size_t outer, std::size_t inner, int op)
			{
				xhqm::i64* d = (xhqm::i64*)dst; const xhqm::i64* s = (const xhqm::i64*)src;
				for (std::size_t o = 0; o < outer; ++o)
				{
					const xhqm::i64* row = s + o * inner;
					xhqm::i64 acc = (op == reduce_op_prod) ? 1 : (op == reduce_op_max) ? xhqm::i64min : (op == reduce_op_min) ? xhqm::i64max : 0;
					if (op == reduce_op_sum)
					{
						__m256i vsum = _mm256_setzero_si256();
						std::size_t i = 0;
						for (; i + 4 <= inner; i += 4) vsum = _mm256_add_epi64(vsum, _mm256_loadu_si256((const __m256i*)(row + i)));
						acc += hsum_epi64(vsum);
						for (; i < inner; ++i) acc += row[i];
					}
					else
					{
						for (std::size_t i = 0; i < inner; ++i) acc = reduce_eval(acc, row[i], op);
					}
					d[o] = acc;
				}
				return true;
			}
			// int16 归约：sum 走 AVX2（epi16 add），max/min/prod 走标量
			inline bool reduce_i16(void* dst, const void* src, std::size_t outer, std::size_t inner, int op)
			{
				xhqm::i16* d = (xhqm::i16*)dst; const xhqm::i16* s = (const xhqm::i16*)src;
				for (std::size_t o = 0; o < outer; ++o)
				{
					const xhqm::i16* row = s + o * inner;
					xhqm::i16 acc = (op == reduce_op_prod) ? 1 : (op == reduce_op_max) ? xhqm::i16min : (op == reduce_op_min) ? xhqm::i16max : 0;
					if (op == reduce_op_sum)
					{
						__m256i vsum = _mm256_setzero_si256();
						std::size_t i = 0;
						for (; i + 16 <= inner; i += 16) vsum = _mm256_add_epi16(vsum, _mm256_loadu_si256((const __m256i*)(row + i)));
						acc += hsum_epi16(vsum);
						for (; i < inner; ++i) acc += row[i];
					}
					else
					{
						for (std::size_t i = 0; i < inner; ++i) acc = reduce_eval(acc, row[i], op);
					}
					d[o] = acc;
				}
				return true;
			}
			// int8 归约：sum 走 AVX2（epi8 add），max/min/prod 走标量
			inline bool reduce_i8(void* dst, const void* src, std::size_t outer, std::size_t inner, int op)
			{
				xhqm::i8* d = (xhqm::i8*)dst; const xhqm::i8* s = (const xhqm::i8*)src;
				for (std::size_t o = 0; o < outer; ++o)
				{
					const xhqm::i8* row = s + o * inner;
					xhqm::i8 acc = (op == reduce_op_prod) ? 1 : (op == reduce_op_max) ? xhqm::i8min : (op == reduce_op_min) ? xhqm::i8max : 0;
					if (op == reduce_op_sum)
					{
						__m256i vsum = _mm256_setzero_si256();
						std::size_t i = 0;
						for (; i + 32 <= inner; i += 32) vsum = _mm256_add_epi8(vsum, _mm256_loadu_si256((const __m256i*)(row + i)));
						acc += hsum_epi8(vsum);
						for (; i < inner; ++i) acc += row[i];
					}
					else
					{
						for (std::size_t i = 0; i < inner; ++i) acc = reduce_eval(acc, row[i], op);
					}
					d[o] = acc;
				}
				return true;
			}
			// bool 归约（GF(2)）：sum=XOR，prod=AND，max=OR，min=AND。用 unsigned char 访问底层字节。
			inline bool reduce_bool(void* dst, const void* src, std::size_t outer, std::size_t inner, int op)
			{
				unsigned char* d = (unsigned char*)dst; const unsigned char* s = (const unsigned char*)src;
				for (std::size_t o = 0; o < outer; ++o)
				{
					const unsigned char* row = s + o * inner;
					unsigned char acc = (op == reduce_op_prod || op == reduce_op_min) ? 1 : 0;
					if (op == reduce_op_sum)
					{
						for (std::size_t i = 0; i < inner; ++i) acc ^= row[i];
					}
					else if (op == reduce_op_max)
					{
						for (std::size_t i = 0; i < inner; ++i) acc |= row[i];
					}
					else // prod / min = AND
					{
						for (std::size_t i = 0; i < inner; ++i) acc &= row[i];
					}
					d[o] = acc;
				}
				return true;
			}
		}
	}
}

#endif
