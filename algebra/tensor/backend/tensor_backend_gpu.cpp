#include "tensor_backend_gpu.h"

#ifdef XHQM_TENSOR_CUDA
#include <cuda_runtime.h>
extern "C" int xhqm_cuda_elemwise(void*, const void*, const void*, std::size_t, int, int);
extern "C" int xhqm_cuda_select(void*, const void*, const void*, const void*, std::size_t, int);
extern "C" int xhqm_cuda_pool2d(void*, const void*, std::size_t, std::size_t, std::size_t, std::size_t, std::size_t, std::size_t, std::size_t, std::size_t, std::size_t, int, int);
extern "C" int xhqm_cuda_upsample2d(void*, const void*, std::size_t, std::size_t, std::size_t, std::size_t, std::size_t, std::size_t, int, int);
extern "C" int xhqm_cuda_dropout(void*, const void*, std::size_t, float, unsigned int, int);
extern "C" int xhqm_cuda_matmul(void*, const void*, const void*, std::size_t, std::size_t, std::size_t, int);
extern "C" int xhqm_cuda_reduce(void*, const void*, std::size_t, std::size_t, int, int);
extern "C" int xhqm_cuda_unary(void*, const void*, std::size_t, int, int);
extern "C" int xhqm_cuda_grad_accum(void*, const void*, const void*, std::size_t, int, int);
extern "C" int xhqm_cuda_transpose(void*, const void*, std::size_t, std::size_t, int);
extern "C" int xhqm_cuda_spmv(void*, const void*, const unsigned long long*, const unsigned long long*, const void*, std::size_t, std::size_t, std::size_t, int);
extern "C" int xhqm_cuda_conv3d(void*, const void*, const void*, const void*,
	std::size_t, std::size_t, std::size_t, std::size_t, std::size_t, std::size_t,
	std::size_t, std::size_t, std::size_t, std::size_t, std::size_t, std::size_t,
	std::size_t, std::size_t, std::size_t, std::size_t, std::size_t, std::size_t,
	std::size_t, int);
extern "C" int xhqm_cuda_conv4d(void*, const void*, const void*, const void*,
	std::size_t, std::size_t, std::size_t, std::size_t, std::size_t, std::size_t, std::size_t,
	std::size_t, std::size_t, std::size_t, std::size_t, std::size_t, std::size_t, std::size_t, std::size_t,
	std::size_t, std::size_t, std::size_t, std::size_t, std::size_t, std::size_t, std::size_t, std::size_t, std::size_t,
	int);
extern "C" int xhqm_cuda_trsv(void*, const void*, const void*, std::size_t, int, int, int);
extern "C" int xhqm_cuda_chol(void*, const void*, std::size_t, int);
#endif

namespace xhqm
{
	namespace tensor_base
	{
		namespace
		{
			class gpu_backend : public tensor_backend
			{
			public:
				const char* name() const noexcept override { return "gpu"; }
				device kind() const noexcept override { return device::gpu; }
				bool supports_memory_transfer() const noexcept override { return true; }

				// GPU 无法执行 std::function 闭包，parallel_for 回退主机串行；
				// 真正的并行算力走 elemwise / matmul 原语。
				void parallel_for(std::size_t begin, std::size_t end,
					const std::function<void(std::size_t)>& body) override
				{
					for (std::size_t i = begin; i < end; ++i)
						body(i);
				}

				bool elemwise(void* dst, const void* a, const void* b,
					std::size_t count, dtype dt, int op) noexcept override
				{
#ifdef XHQM_TENSOR_CUDA
					return xhqm_cuda_elemwise(dst, a, b, count, static_cast<int>(dt), op) == 0;
#else
					(void)dst; (void)a; (void)b; (void)count; (void)dt; (void)op;
					return false; // 未编 CUDA：回退 CPU
#endif
				}

				bool select(void* dst, const void* cond, const void* a, const void* b,
					std::size_t count, dtype dt) noexcept override
				{
#ifdef XHQM_TENSOR_CUDA
					return xhqm_cuda_select(dst, cond, a, b, count, static_cast<int>(dt)) == 0;
#else
					(void)dst; (void)cond; (void)a; (void)b; (void)count; (void)dt;
					return false;
#endif
				}

				bool pool2d(void* dst, const void* src,
					std::size_t n, std::size_t c, std::size_t h, std::size_t w,
					std::size_t oh, std::size_t ow, std::size_t k, std::size_t stride, std::size_t pad,
					dtype dt, int op) noexcept override
				{
#ifdef XHQM_TENSOR_CUDA
					return xhqm_cuda_pool2d(dst, src, n, c, h, w, oh, ow, k, stride, pad, static_cast<int>(dt), op) == 0;
#else
					(void)dst; (void)src; (void)n; (void)c; (void)h; (void)w; (void)oh; (void)ow; (void)k; (void)stride; (void)pad; (void)dt; (void)op;
					return false;
#endif
				}

				bool upsample2d(void* dst, const void* src,
					std::size_t n, std::size_t c, std::size_t h, std::size_t w,
					std::size_t oh, std::size_t ow, dtype dt, int op) noexcept override
				{
#ifdef XHQM_TENSOR_CUDA
					return xhqm_cuda_upsample2d(dst, src, n, c, h, w, oh, ow, static_cast<int>(dt), op) == 0;
#else
					(void)dst; (void)src; (void)n; (void)c; (void)h; (void)w; (void)oh; (void)ow; (void)dt; (void)op;
					return false;
#endif
				}

				bool dropout(void* dst, const void* src, std::size_t count,
					float p, unsigned int seed, dtype dt) noexcept override
				{
#ifdef XHQM_TENSOR_CUDA
					return xhqm_cuda_dropout(dst, src, count, p, seed, static_cast<int>(dt)) == 0;
#else
					(void)dst; (void)src; (void)count; (void)p; (void)seed; (void)dt;
					return false;
#endif
				}

				bool matmul(void* c, const void* a, const void* b,
					std::size_t m, std::size_t n, std::size_t k, dtype dt) noexcept override
				{
#ifdef XHQM_TENSOR_CUDA
					return xhqm_cuda_matmul(c, a, b, m, n, k, static_cast<int>(dt)) == 0;
#else
					(void)c; (void)a; (void)b; (void)m; (void)n; (void)k; (void)dt;
					return false;
#endif
				}

				bool reduce(void* dst, const void* src,
					std::size_t outer, std::size_t inner, dtype dt, int op) noexcept override
				{
#ifdef XHQM_TENSOR_CUDA
					return xhqm_cuda_reduce(dst, src, outer, inner, static_cast<int>(dt), op) == 0;
#else
					(void)dst; (void)src; (void)outer; (void)inner; (void)dt; (void)op;
					return false;
#endif
				}

				bool unary(void* dst, const void* a,
					std::size_t count, dtype dt, int op) noexcept override
				{
#ifdef XHQM_TENSOR_CUDA
					return xhqm_cuda_unary(dst, a, count, static_cast<int>(dt), op) == 0;
#else
					(void)dst; (void)a; (void)count; (void)dt; (void)op;
					return false;
#endif
				}

				bool transpose(void* dst, const void* src,
					std::size_t rows, std::size_t cols, dtype dt) noexcept override
				{
#ifdef XHQM_TENSOR_CUDA
					return xhqm_cuda_transpose(dst, src, rows, cols, static_cast<int>(dt)) == 0;
#else
					(void)dst; (void)src; (void)rows; (void)cols; (void)dt;
					return false;
#endif
				}

				bool grad_accum(void* dst, const void* a, const void* b,
					std::size_t count, dtype dt, int op) noexcept override
				{
#ifdef XHQM_TENSOR_CUDA
					return xhqm_cuda_grad_accum(dst, a, b, count, static_cast<int>(dt), op) == 0;
#else
					(void)dst; (void)a; (void)b; (void)count; (void)dt; (void)op;
					return false;
#endif
				}

				bool spmv(void* y, const void* x, const xhqm::index* row, const xhqm::index* col,
					const void* val, std::size_t nnz, std::size_t rows, std::size_t cols, dtype dt) noexcept override
				{
#ifdef XHQM_TENSOR_CUDA
					return xhqm_cuda_spmv(y, x,
						reinterpret_cast<const unsigned long long*>(row),
						reinterpret_cast<const unsigned long long*>(col),
						val, nnz, rows, cols, static_cast<int>(dt)) == 0;
#else
					(void)y; (void)x; (void)row; (void)col; (void)val; (void)nnz; (void)rows; (void)cols; (void)dt;
					return false;
#endif
				}

				bool conv3d(void* dst, const void* src, const void* weight, const void* bias,
					std::size_t n, std::size_t cin, std::size_t cout,
					std::size_t id, std::size_t ih, std::size_t iw,
					std::size_t kd, std::size_t kh, std::size_t kw,
					std::size_t stride_d, std::size_t stride_h, std::size_t stride_w,
					std::size_t pad_d, std::size_t pad_h, std::size_t pad_w,
					std::size_t dil_d, std::size_t dil_h, std::size_t dil_w,
					std::size_t groups, dtype dt) noexcept override
				{
#ifdef XHQM_TENSOR_CUDA
					return xhqm_cuda_conv3d(dst, src, weight, bias, n, cin, cout, id, ih, iw, kd, kh, kw,
						stride_d, stride_h, stride_w, pad_d, pad_h, pad_w, dil_d, dil_h, dil_w,
						groups, static_cast<int>(dt)) == 0;
#else
					(void)dst; (void)src; (void)weight; (void)bias; (void)n; (void)cin; (void)cout;
					(void)id; (void)ih; (void)iw; (void)kd; (void)kh; (void)kw;
					(void)stride_d; (void)stride_h; (void)stride_w; (void)pad_d; (void)pad_h; (void)pad_w;
					(void)dil_d; (void)dil_h; (void)dil_w; (void)groups; (void)dt;
					return false;
#endif
				}

				// 4D 卷积（N,C,D0,D1,D2,D3）：CUDA 内核，f32/f64。
				bool conv4d(void* dst, const void* src, const void* weight, const void* bias,
					std::size_t n, std::size_t cin, std::size_t cout,
					std::size_t i0, std::size_t i1, std::size_t i2, std::size_t i3,
					std::size_t k0, std::size_t k1, std::size_t k2, std::size_t k3,
					std::size_t s0, std::size_t s1, std::size_t s2, std::size_t s3,
					std::size_t p0, std::size_t p1, std::size_t p2, std::size_t p3,
					std::size_t d0, std::size_t d1, std::size_t d2, std::size_t d3,
					std::size_t groups, dtype dt) noexcept override
				{
#ifdef XHQM_TENSOR_CUDA
					return xhqm_cuda_conv4d(dst, src, weight, bias, n, cin, cout,
						i0, i1, i2, i3, k0, k1, k2, k3,
						s0, s1, s2, s3, p0, p1, p2, p3, d0, d1, d2, d3,
						groups, static_cast<int>(dt)) == 0;
#else
					(void)dst; (void)src; (void)weight; (void)bias; (void)n; (void)cin; (void)cout;
					(void)i0; (void)i1; (void)i2; (void)i3; (void)k0; (void)k1; (void)k2; (void)k3;
					(void)s0; (void)s1; (void)s2; (void)s3; (void)p0; (void)p1; (void)p2; (void)p3;
					(void)d0; (void)d1; (void)d2; (void)d3; (void)groups; (void)dt;
					return false;
#endif
				}

				// 三角求解：cuBLAS trsv（f32/f64）。
				bool trsv(void* x, const void* tri, const void* b,
					std::size_t n, bool lower, bool unit_diag, dtype dt) noexcept override
				{
#ifdef XHQM_TENSOR_CUDA
					return xhqm_cuda_trsv(x, tri, b, n, lower ? 1 : 0, unit_diag ? 1 : 0, static_cast<int>(dt)) == 0;
#else
					(void)x; (void)tri; (void)b; (void)n; (void)lower; (void)unit_diag; (void)dt;
					return false;
#endif
				}

				// Cholesky：cuSOLVER potrf（f32/f64）。
				bool chol(void* L, const void* A, std::size_t n, dtype dt) noexcept override
				{
#ifdef XHQM_TENSOR_CUDA
					return xhqm_cuda_chol(L, A, n, static_cast<int>(dt)) == 0;
#else
					(void)L; (void)A; (void)n; (void)dt;
					return false;
#endif
				}

				void upload(void*, std::size_t) override {}
				void download(void*, std::size_t) override {}
				void synchronize() override
				{
#ifdef XHQM_TENSOR_CUDA
					cudaDeviceSynchronize();
#endif
				}
			};
		}

		tensor_backend* create_gpu_backend()
		{
			static gpu_backend instance;
			return &instance;
		}
	}
}
