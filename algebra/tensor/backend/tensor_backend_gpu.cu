// tensor_backend_gpu.cu: CUDA kernels (nvcc) for tensor_backend_gpu.cpp.
// float(f16)/float(f32)/double(f64) run on GPU; others return -1 (CPU fallback).
// Matmul delegates to cuBLAS (cublasGemmEx for f16, Sgemm/Dgemm for f32/f64);
// elemwise and reduce are hand kernels.
// Device memory is pooled/reused and ops run on a dedicated stream to avoid per-call cudaMalloc/cudaFree.
#include <cuda_runtime.h>
#include <cublas_v2.h>
#include <cusolverDn.h>
#include <cuda_fp16.h>
#include <cstddef>
#include <map>
#include <mutex>

// Values match xhqm::tensor::dtype in tensor_backend.h
constexpr int dtype_f16 = 1;
constexpr int dtype_f32 = 2;
constexpr int dtype_f64 = 3;
constexpr int dtype_fld = 4;
constexpr int dtype_i8 = 5;
constexpr int dtype_i16 = 6;
constexpr int dtype_i32 = 7;

namespace
{
	template<typename T>
	__global__ void elemwise_kernel(T* dst, const T* a, const T* b, std::size_t n, int op)
	{
		std::size_t i = blockIdx.x * blockDim.x + threadIdx.x;
		if (i >= n) return;
		switch (op)
		{
		case 0: dst[i] = a[i] + b[i]; break;
		case 1: dst[i] = a[i] - b[i]; break;
		case 2: dst[i] = a[i] * b[i]; break;
		case 3: dst[i] = a[i] / b[i]; break;
		case 4: dst[i] = (T)atan2((double)a[i], (double)b[i]); break;
		case 5: dst[i] = (T)hypot((double)a[i], (double)b[i]); break;
		case 6: dst[i] = (T)pow((double)a[i], (double)b[i]); break;
		case 7: { double af = (double)a[i], bf = (double)b[i]; dst[i] = (T)(af - floor(af / bf) * bf); break; }
		case 8: dst[i] = (T)fmod((double)a[i], (double)b[i]); break;
		default: dst[i] = a[i]; break;
		}
	}

	template<typename T>
	__global__ void select_kernel(T* dst, const unsigned char* cond, const T* a, const T* b, std::size_t n)
	{
		std::size_t i = blockIdx.x * blockDim.x + threadIdx.x;
		if (i >= n) return;
		dst[i] = cond[i] ? a[i] : b[i];
	}

	template<typename T>
	__global__ void pool2d_kernel(T* dst, const T* src,
		int n, int c, int h, int w, int oh, int ow, int k, int stride, int pad, int op)
	{
		int idx = blockIdx.x * blockDim.x + threadIdx.x;
		int total = n * c * oh * ow;
		if (idx >= total) return;
		int flat = idx;
		int ow_ = flat % ow; flat /= ow;
		int oh_ = flat % oh; flat /= oh;
		int c_ = flat % c; flat /= c;
		int n_ = flat;
		const int h0 = oh_ * stride - pad;
		const int w0 = ow_ * stride - pad;
		if (op == 0) {  // max
			T acc = (T)0;
			bool first = true;
			for (int kh = 0; kh < k; ++kh)
				for (int kw = 0; kw < k; ++kw) {
					const int hh = h0 + kh, ww = w0 + kw;
					if (hh < 0 || hh >= h || ww < 0 || ww >= w) continue;
					T v = src[((n_ * c + c_) * h + hh) * w + ww];
					if (first) { acc = v; first = false; }
					else if (v > acc) acc = v;
				}
			dst[idx] = acc;
		} else {  // avg
			T sum = (T)0;
			int cnt = 0;
			for (int kh = 0; kh < k; ++kh)
				for (int kw = 0; kw < k; ++kw) {
					const int hh = h0 + kh, ww = w0 + kw;
					if (hh < 0 || hh >= h || ww < 0 || ww >= w) continue;
					sum += src[((n_ * c + c_) * h + hh) * w + ww];
					++cnt;
				}
			dst[idx] = cnt > 0 ? sum / (T)cnt : (T)0;
		}
	}

	template<typename T>
	__global__ void upsample2d_kernel(T* dst, const T* src,
		int n, int c, int h, int w, int oh, int ow, int op)
	{
		int idx = blockIdx.x * blockDim.x + threadIdx.x;
		int total = n * c * oh * ow;
		if (idx >= total) return;
		int flat = idx;
		int ow_ = flat % ow; flat /= ow;
		int oh_ = flat % oh; flat /= oh;
		int c_ = flat % c; flat /= c;
		int n_ = flat;
		const T* src_nc = src + (n_ * c + c_) * h * w;
		if (op == 0) {  // nearest（整数倍）
			const int ih = oh_ * h / oh;
			const int iw = ow_ * w / ow;
			dst[idx] = src_nc[ih * w + iw];
		} else {  // bilinear（align_corners=false）
			const double scale_h = (double)h / (double)oh;
			const double scale_w = (double)w / (double)ow;
			double sh = ((double)oh_ + 0.5) * scale_h - 0.5;
			double sw = ((double)ow_ + 0.5) * scale_w - 0.5;
			if (sh < 0.0) sh = 0.0; if (sh > (double)(h - 1)) sh = (double)(h - 1);
			if (sw < 0.0) sw = 0.0; if (sw > (double)(w - 1)) sw = (double)(w - 1);
			const int h0 = (int)sh, w0 = (int)sw;
			const int h1 = (h0 + 1 < h) ? h0 + 1 : h0;
			const int w1 = (w0 + 1 < w) ? w0 + 1 : w0;
			const double hw = sh - (double)h0, ww = sw - (double)w0;
			const double v = (1.0 - hw) * (1.0 - ww) * (double)src_nc[h0 * w + w0]
				+ (1.0 - hw) * ww * (double)src_nc[h0 * w + w1]
				+ hw * (1.0 - ww) * (double)src_nc[h1 * w + w0]
				+ hw * ww * (double)src_nc[h1 * w + w1];
			dst[idx] = (T)v;
		}
	}

	// ---- device 端 RNG（与 CPU rand_seed_at/xorshift32 同算法，可复现）----
	__device__ unsigned int xorshift32_d(unsigned int& s)
	{
		unsigned int x = s;
		x ^= x << 13; x ^= x >> 17; x ^= x << 5;
		s = x; return x;
	}
	__device__ float uniform01_d(unsigned int& s)
	{
		return (float)(xorshift32_d(s) >> 8) / (float)(1u << 24);
	}
	__device__ unsigned int seed_at_d(unsigned int seed, std::size_t i)
	{
		unsigned long long h = (unsigned long long)seed * 0x9E3779B97F4A7C15ull ^ ((unsigned long long)i + 0x9E3779B97F4A7C15ull);
		h ^= h >> 30; h *= 0xBF58476D1CE4E5B9ull;
		h ^= h >> 27; h *= 0x94D049BB133111EBull;
		h ^= h >> 31;
		unsigned int s = (unsigned int)h;
		return s == 0 ? 0x9E3779B9u : s;
	}

	template<typename T>
	__global__ void dropout_kernel(T* dst, const T* src, unsigned int seed, float p, std::size_t n)
	{
		std::size_t i = blockIdx.x * blockDim.x + threadIdx.x;
		if (i >= n) return;
		unsigned int s = seed_at_d(seed, i);
		const T keep = (T)(1.0 - p);
		const T scale = (T)1 / keep;
		if (uniform01_d(s) >= p)
			dst[i] = src[i] * scale;
		else
			dst[i] = (T)0;
	}

	// ---- device memory pool: reuse cudaMalloc buffers, capped at 512 MiB ----
	std::mutex g_pool_mutex;
	std::multimap<std::size_t, void*> g_free_buffers;
	std::size_t g_pool_bytes = 0;
	const std::size_t g_pool_limit = 512ull * 1024ull * 1024ull;

	void* pool_alloc(std::size_t bytes)
	{
		if (bytes == 0) return nullptr;
		std::lock_guard<std::mutex> lock(g_pool_mutex);
		auto it = g_free_buffers.lower_bound(bytes);
		if (it != g_free_buffers.end())
		{
			void* p = it->second;
			g_pool_bytes -= it->first;
			g_free_buffers.erase(it);
			return p;
		}
		void* p = nullptr;
		if (cudaMalloc(&p, bytes) == cudaSuccess) return p;
		return nullptr;
	}

	void pool_free(void* p, std::size_t bytes)
	{
		if (!p || bytes == 0) return;
		std::lock_guard<std::mutex> lock(g_pool_mutex);
		if (g_pool_bytes + bytes > g_pool_limit)
		{
			cudaFree(p);
			return;
		}
		g_free_buffers.emplace(bytes, p);
		g_pool_bytes += bytes;
	}

	// ---- dedicated stream: transfers / kernels / cuBLAS share one stream ----
	cudaStream_t& default_stream()
	{
		static cudaStream_t s = nullptr;
		if (!s) cudaStreamCreate(&s);
		return s;
	}

	// cuBLAS handle singleton (bound to the dedicated stream)
	cublasHandle_t& blas_handle()
	{
		static cublasHandle_t h = nullptr;
		if (!h)
		{
			cublasCreate(&h);
			cublasSetStream(h, default_stream());
		}
		return h;
	}

	// cuSOLVER handle singleton (bound to the dedicated stream)
	cusolverDnHandle_t& solver_handle()
	{
		static cusolverDnHandle_t h = nullptr;
		if (!h)
		{
			cusolverDnCreate(&h);
			cusolverDnSetStream(h, default_stream());
		}
		return h;
	}

	template<typename T>
	int launch_elemwise(void* dst, const void* a, const void* b, std::size_t count, int op)
	{
		if (count == 0) return 0;
		const std::size_t bytes = count * sizeof(T);
		T* d_dst = (T*)pool_alloc(bytes);
		T* d_a = (T*)pool_alloc(bytes);
		T* d_b = (T*)pool_alloc(bytes);
		if (!d_dst || !d_a || !d_b)
		{
			pool_free(d_dst, bytes); pool_free(d_a, bytes); pool_free(d_b, bytes);
			return -1;
		}
		cudaStream_t s = default_stream();
		if (cudaMemcpyAsync(d_a, a, bytes, cudaMemcpyHostToDevice, s) != cudaSuccess ||
			cudaMemcpyAsync(d_b, b, bytes, cudaMemcpyHostToDevice, s) != cudaSuccess)
		{
			cudaStreamSynchronize(s);
			pool_free(d_dst, bytes); pool_free(d_a, bytes); pool_free(d_b, bytes);
			return -1;
		}
		const int block = 256;
		const int grid = (int)((count + block - 1) / block);
		elemwise_kernel<T><<<grid, block, 0, s>>>(d_dst, d_a, d_b, count, op);
		if (cudaGetLastError() != cudaSuccess ||
			cudaMemcpyAsync(dst, d_dst, bytes, cudaMemcpyDeviceToHost, s) != cudaSuccess)
		{
			cudaStreamSynchronize(s);
			pool_free(d_dst, bytes); pool_free(d_a, bytes); pool_free(d_b, bytes);
			return -1;
		}
		cudaStreamSynchronize(s);
		pool_free(d_dst, bytes); pool_free(d_a, bytes); pool_free(d_b, bytes);
		return 0;
	}

	template<typename T>
	int launch_select(void* dst, const void* cond, const void* a, const void* b, std::size_t count)
	{
		if (count == 0) return 0;
		const std::size_t bytes = count * sizeof(T);
		const std::size_t cbytes = count * sizeof(unsigned char);
		T* d_dst = (T*)pool_alloc(bytes);
		unsigned char* d_cond = (unsigned char*)pool_alloc(cbytes);
		T* d_a = (T*)pool_alloc(bytes);
		T* d_b = (T*)pool_alloc(bytes);
		if (!d_dst || !d_cond || !d_a || !d_b)
		{
			pool_free(d_dst, bytes); pool_free(d_cond, cbytes); pool_free(d_a, bytes); pool_free(d_b, bytes);
			return -1;
		}
		cudaStream_t s = default_stream();
		if (cudaMemcpyAsync(d_cond, cond, cbytes, cudaMemcpyHostToDevice, s) != cudaSuccess ||
			cudaMemcpyAsync(d_a, a, bytes, cudaMemcpyHostToDevice, s) != cudaSuccess ||
			cudaMemcpyAsync(d_b, b, bytes, cudaMemcpyHostToDevice, s) != cudaSuccess)
		{
			cudaStreamSynchronize(s);
			pool_free(d_dst, bytes); pool_free(d_cond, cbytes); pool_free(d_a, bytes); pool_free(d_b, bytes);
			return -1;
		}
		const int block = 256;
		const int grid = (int)((count + block - 1) / block);
		select_kernel<T><<<grid, block, 0, s>>>(d_dst, d_cond, d_a, d_b, count);
		if (cudaGetLastError() != cudaSuccess ||
			cudaMemcpyAsync(dst, d_dst, bytes, cudaMemcpyDeviceToHost, s) != cudaSuccess)
		{
			cudaStreamSynchronize(s);
			pool_free(d_dst, bytes); pool_free(d_cond, cbytes); pool_free(d_a, bytes); pool_free(d_b, bytes);
			return -1;
		}
		cudaStreamSynchronize(s);
		pool_free(d_dst, bytes); pool_free(d_cond, cbytes); pool_free(d_a, bytes); pool_free(d_b, bytes);
		return 0;
	}

	template<typename T>
	int launch_pool2d(void* dst, const void* src,
		std::size_t n, std::size_t c, std::size_t h, std::size_t w,
		std::size_t oh, std::size_t ow, std::size_t k, std::size_t stride, std::size_t pad, int op)
	{
		const std::size_t in_count = n * c * h * w;
		const std::size_t out_count = n * c * oh * ow;
		if (in_count == 0 || out_count == 0) return 0;
		const std::size_t in_bytes = in_count * sizeof(T);
		const std::size_t out_bytes = out_count * sizeof(T);
		T* d_src = (T*)pool_alloc(in_bytes);
		T* d_dst = (T*)pool_alloc(out_bytes);
		if (!d_src || !d_dst)
		{
			pool_free(d_src, in_bytes); pool_free(d_dst, out_bytes);
			return -1;
		}
		cudaStream_t s = default_stream();
		if (cudaMemcpyAsync(d_src, src, in_bytes, cudaMemcpyHostToDevice, s) != cudaSuccess)
		{
			cudaStreamSynchronize(s);
			pool_free(d_src, in_bytes); pool_free(d_dst, out_bytes);
			return -1;
		}
		const int block = 256;
		const int grid = (int)((out_count + block - 1) / block);
		pool2d_kernel<T><<<grid, block, 0, s>>>(d_dst, d_src,
			(int)n, (int)c, (int)h, (int)w, (int)oh, (int)ow, (int)k, (int)stride, (int)pad, op);
		if (cudaGetLastError() != cudaSuccess ||
			cudaMemcpyAsync(dst, d_dst, out_bytes, cudaMemcpyDeviceToHost, s) != cudaSuccess)
		{
			cudaStreamSynchronize(s);
			pool_free(d_src, in_bytes); pool_free(d_dst, out_bytes);
			return -1;
		}
		cudaStreamSynchronize(s);
		pool_free(d_src, in_bytes); pool_free(d_dst, out_bytes);
		return 0;
	}

	template<typename T>
	int launch_upsample2d(void* dst, const void* src,
		std::size_t n, std::size_t c, std::size_t h, std::size_t w,
		std::size_t oh, std::size_t ow, int op)
	{
		const std::size_t in_count = n * c * h * w;
		const std::size_t out_count = n * c * oh * ow;
		if (in_count == 0 || out_count == 0) return 0;
		const std::size_t in_bytes = in_count * sizeof(T);
		const std::size_t out_bytes = out_count * sizeof(T);
		T* d_src = (T*)pool_alloc(in_bytes);
		T* d_dst = (T*)pool_alloc(out_bytes);
		if (!d_src || !d_dst)
		{
			pool_free(d_src, in_bytes); pool_free(d_dst, out_bytes);
			return -1;
		}
		cudaStream_t s = default_stream();
		if (cudaMemcpyAsync(d_src, src, in_bytes, cudaMemcpyHostToDevice, s) != cudaSuccess)
		{
			cudaStreamSynchronize(s);
			pool_free(d_src, in_bytes); pool_free(d_dst, out_bytes);
			return -1;
		}
		const int block = 256;
		const int grid = (int)((out_count + block - 1) / block);
		upsample2d_kernel<T><<<grid, block, 0, s>>>(d_dst, d_src, (int)n, (int)c, (int)h, (int)w, (int)oh, (int)ow, op);
		if (cudaGetLastError() != cudaSuccess ||
			cudaMemcpyAsync(dst, d_dst, out_bytes, cudaMemcpyDeviceToHost, s) != cudaSuccess)
		{
			cudaStreamSynchronize(s);
			pool_free(d_src, in_bytes); pool_free(d_dst, out_bytes);
			return -1;
		}
		cudaStreamSynchronize(s);
		pool_free(d_src, in_bytes); pool_free(d_dst, out_bytes);
		return 0;
	}

	template<typename T>
	int launch_dropout(void* dst, const void* src, std::size_t count, float p, unsigned int seed)
	{
		if (count == 0) return 0;
		const std::size_t bytes = count * sizeof(T);
		T* d_src = (T*)pool_alloc(bytes);
		T* d_dst = (T*)pool_alloc(bytes);
		if (!d_src || !d_dst)
		{
			pool_free(d_src, bytes); pool_free(d_dst, bytes);
			return -1;
		}
		cudaStream_t s = default_stream();
		if (cudaMemcpyAsync(d_src, src, bytes, cudaMemcpyHostToDevice, s) != cudaSuccess)
		{
			cudaStreamSynchronize(s);
			pool_free(d_src, bytes); pool_free(d_dst, bytes);
			return -1;
		}
		const int block = 256;
		const int grid = (int)((count + block - 1) / block);
		dropout_kernel<T><<<grid, block, 0, s>>>(d_dst, d_src, seed, p, count);
		if (cudaGetLastError() != cudaSuccess ||
			cudaMemcpyAsync(dst, d_dst, bytes, cudaMemcpyDeviceToHost, s) != cudaSuccess)
		{
			cudaStreamSynchronize(s);
			pool_free(d_src, bytes); pool_free(d_dst, bytes);
			return -1;
		}
		cudaStreamSynchronize(s);
		pool_free(d_src, bytes); pool_free(d_dst, bytes);
		return 0;
	}

	// Row-major C[m x n] = A[m x k] x B[k x n] is column-major C^T = B^T x A^T,
	// so cuBLAS takes B as the left operand and A as the right operand.
	int launch_matmul_sgemm(void* c, const void* a, const void* b, std::size_t m, std::size_t n, std::size_t k)
	{
		if (m == 0 || n == 0 || k == 0) return 0;
		const std::size_t abytes = m * k * sizeof(float);
		const std::size_t bbytes = k * n * sizeof(float);
		const std::size_t cbytes = m * n * sizeof(float);
		float* d_a = (float*)pool_alloc(abytes);
		float* d_b = (float*)pool_alloc(bbytes);
		float* d_c = (float*)pool_alloc(cbytes);
		if (!d_a || !d_b || !d_c)
		{
			pool_free(d_a, abytes); pool_free(d_b, bbytes); pool_free(d_c, cbytes);
			return -1;
		}
		cudaStream_t s = default_stream();
		if (cudaMemcpyAsync(d_a, a, abytes, cudaMemcpyHostToDevice, s) != cudaSuccess ||
			cudaMemcpyAsync(d_b, b, bbytes, cudaMemcpyHostToDevice, s) != cudaSuccess)
		{
			cudaStreamSynchronize(s);
			pool_free(d_a, abytes); pool_free(d_b, bbytes); pool_free(d_c, cbytes);
			return -1;
		}
		const float alpha = 1.0f, beta = 0.0f;
		const cublasStatus_t st = cublasSgemm(blas_handle(), CUBLAS_OP_N, CUBLAS_OP_N,
			(int)n, (int)m, (int)k, &alpha, d_b, (int)n, d_a, (int)k, &beta, d_c, (int)n);
		if (st != CUBLAS_STATUS_SUCCESS ||
			cudaMemcpyAsync(c, d_c, cbytes, cudaMemcpyDeviceToHost, s) != cudaSuccess)
		{
			cudaStreamSynchronize(s);
			pool_free(d_a, abytes); pool_free(d_b, bbytes); pool_free(d_c, cbytes);
			return -1;
		}
		cudaStreamSynchronize(s);
		pool_free(d_a, abytes); pool_free(d_b, bbytes); pool_free(d_c, cbytes);
		return 0;
	}

	int launch_matmul_dgemm(void* c, const void* a, const void* b, std::size_t m, std::size_t n, std::size_t k)
	{
		if (m == 0 || n == 0 || k == 0) return 0;
		const std::size_t abytes = m * k * sizeof(double);
		const std::size_t bbytes = k * n * sizeof(double);
		const std::size_t cbytes = m * n * sizeof(double);
		double* d_a = (double*)pool_alloc(abytes);
		double* d_b = (double*)pool_alloc(bbytes);
		double* d_c = (double*)pool_alloc(cbytes);
		if (!d_a || !d_b || !d_c)
		{
			pool_free(d_a, abytes); pool_free(d_b, bbytes); pool_free(d_c, cbytes);
			return -1;
		}
		cudaStream_t s = default_stream();
		if (cudaMemcpyAsync(d_a, a, abytes, cudaMemcpyHostToDevice, s) != cudaSuccess ||
			cudaMemcpyAsync(d_b, b, bbytes, cudaMemcpyHostToDevice, s) != cudaSuccess)
		{
			cudaStreamSynchronize(s);
			pool_free(d_a, abytes); pool_free(d_b, bbytes); pool_free(d_c, cbytes);
			return -1;
		}
		const double alpha = 1.0, beta = 0.0;
		const cublasStatus_t st = cublasDgemm(blas_handle(), CUBLAS_OP_N, CUBLAS_OP_N,
			(int)n, (int)m, (int)k, &alpha, d_b, (int)n, d_a, (int)k, &beta, d_c, (int)n);
		if (st != CUBLAS_STATUS_SUCCESS ||
			cudaMemcpyAsync(c, d_c, cbytes, cudaMemcpyDeviceToHost, s) != cudaSuccess)
		{
			cudaStreamSynchronize(s);
			pool_free(d_a, abytes); pool_free(d_b, bbytes); pool_free(d_c, cbytes);
			return -1;
		}
		cudaStreamSynchronize(s);
		pool_free(d_a, abytes); pool_free(d_b, bbytes); pool_free(d_c, cbytes);
		return 0;
	}

	// f16 matmul: fp16 inputs/accumulate-in-fp32 output (tensor core on sm_86+).
	// Uses cuBLAS GemmEx so we can pick CUBLAS_COMPUTE_32F for the accumulation,
	// avoiding fp16 accumulate error; memory stays fp16 (CUDA_R_16F).
	int launch_matmul_hgemm(void* c, const void* a, const void* b, std::size_t m, std::size_t n, std::size_t k)
	{
		if (m == 0 || n == 0 || k == 0) return 0;
		const std::size_t abytes = m * k * sizeof(__half);
		const std::size_t bbytes = k * n * sizeof(__half);
		const std::size_t cbytes = m * n * sizeof(__half);
		__half* d_a = (__half*)pool_alloc(abytes);
		__half* d_b = (__half*)pool_alloc(bbytes);
		__half* d_c = (__half*)pool_alloc(cbytes);
		if (!d_a || !d_b || !d_c)
		{
			pool_free(d_a, abytes); pool_free(d_b, bbytes); pool_free(d_c, cbytes);
			return -1;
		}
		cudaStream_t s = default_stream();
		if (cudaMemcpyAsync(d_a, a, abytes, cudaMemcpyHostToDevice, s) != cudaSuccess ||
			cudaMemcpyAsync(d_b, b, bbytes, cudaMemcpyHostToDevice, s) != cudaSuccess)
		{
			cudaStreamSynchronize(s);
			pool_free(d_a, abytes); pool_free(d_b, bbytes); pool_free(d_c, cbytes);
			return -1;
		}
		const float alpha = 1.0f, beta = 0.0f;
		const cublasStatus_t st = cublasGemmEx(blas_handle(), CUBLAS_OP_N, CUBLAS_OP_N,
			(int)n, (int)m, (int)k, &alpha,
			d_b, CUDA_R_16F, (int)n,
			d_a, CUDA_R_16F, (int)k,
			&beta,
			d_c, CUDA_R_16F, (int)n,
			CUBLAS_COMPUTE_32F, CUBLAS_GEMM_DEFAULT);
		if (st != CUBLAS_STATUS_SUCCESS ||
			cudaMemcpyAsync(c, d_c, cbytes, cudaMemcpyDeviceToHost, s) != cudaSuccess)
		{
			cudaStreamSynchronize(s);
			pool_free(d_a, abytes); pool_free(d_b, bbytes); pool_free(d_c, cbytes);
			return -1;
		}
		cudaStreamSynchronize(s);
		pool_free(d_a, abytes); pool_free(d_b, bbytes); pool_free(d_c, cbytes);
		return 0;
	}

	// Reduce along last dim: src[outer x inner] -> dst[outer] (op: 0=sum 1=max 2=min)
	template<typename T>
	__global__ void reduce_kernel(T* dst, const T* src, std::size_t inner, int op)
	{
		__shared__ T sdata[256];
		const std::size_t row = blockIdx.x;
		const T* s = src + row * inner;
		const std::size_t tid = threadIdx.x;

		T val = (op == 0) ? T(0) : ((op == 3) ? T(1) : s[0]);
		for (std::size_t i = tid; i < inner; i += blockDim.x)
		{
			if (op == 0) val += s[i];
			else if (op == 1) val = (s[i] > val) ? s[i] : val;
			else if (op == 2) val = (s[i] < val) ? s[i] : val;
			else val *= s[i];
		}
		sdata[tid] = val;
		__syncthreads();
		for (std::size_t stride = blockDim.x / 2; stride > 0; stride >>= 1)
		{
			if (tid < stride)
			{
				if (op == 0) sdata[tid] += sdata[tid + stride];
				else if (op == 1) sdata[tid] = (sdata[tid + stride] > sdata[tid]) ? sdata[tid + stride] : sdata[tid];
				else if (op == 2) sdata[tid] = (sdata[tid + stride] < sdata[tid]) ? sdata[tid + stride] : sdata[tid];
				else sdata[tid] *= sdata[tid + stride];
			}
			__syncthreads();
		}
		if (tid == 0) dst[row] = sdata[0];
	}

	template<typename T>
	int launch_reduce(void* dst, const void* src, std::size_t outer, std::size_t inner, int op)
	{
		if (outer == 0 || inner == 0) return 0;
		const std::size_t in_bytes = outer * inner * sizeof(T);
		const std::size_t out_bytes = outer * sizeof(T);
		T* d_src = (T*)pool_alloc(in_bytes);
		T* d_dst = (T*)pool_alloc(out_bytes);
		if (!d_src || !d_dst)
		{
			pool_free(d_src, in_bytes); pool_free(d_dst, out_bytes);
			return -1;
		}
		cudaStream_t s = default_stream();
		if (cudaMemcpyAsync(d_src, src, in_bytes, cudaMemcpyHostToDevice, s) != cudaSuccess)
		{
			cudaStreamSynchronize(s);
			pool_free(d_src, in_bytes); pool_free(d_dst, out_bytes);
			return -1;
		}
		const int block = 256;
		const int grid = (int)outer;
		reduce_kernel<T><<<grid, block, 0, s>>>(d_dst, d_src, inner, op);
		if (cudaGetLastError() != cudaSuccess ||
			cudaMemcpyAsync(dst, d_dst, out_bytes, cudaMemcpyDeviceToHost, s) != cudaSuccess)
		{
			cudaStreamSynchronize(s);
			pool_free(d_src, in_bytes); pool_free(d_dst, out_bytes);
			return -1;
		}
		cudaStreamSynchronize(s);
		pool_free(d_src, in_bytes); pool_free(d_dst, out_bytes);
		return 0;
	}

	// ---- unary elementwise: dst[i] = f(a[i]) ----
	template<typename T>
	__global__ void unary_kernel(T* dst, const T* a, std::size_t n, int op)
	{
		std::size_t i = blockIdx.x * blockDim.x + threadIdx.x;
		if (i >= n) return;
		T v = a[i];
		switch (op)
		{
		case 0: dst[i] = v > T(0) ? v : T(0); break;                       // relu
		case 1: dst[i] = v > T(0) ? v : T(0.01) * v; break;               // leaky_relu
		case 2: dst[i] = T(1) / (T(1) + exp(-v)); break;                  // sigmoid
		case 3: { T c = T(0.7978845608028654); T t = tanh(c * (v + T(0.044715) * v * v * v)); dst[i] = T(0.5) * v * (T(1) + t); break; } // gelu
		case 4: dst[i] = exp(v); break;                                   // exp
		case 5: dst[i] = log(v); break;                                   // log
		case 6: dst[i] = tanh(v); break;                                  // tanh
		case 7: dst[i] = sqrt(v); break;                                  // sqrt
		case 8: dst[i] = sin(v); break;                                   // sin
		case 9: dst[i] = cos(v); break;                                   // cos
		case 10: dst[i] = tan(v); break;                                  // tan
		case 11: dst[i] = asin(v); break;                                 // asin
		case 12: dst[i] = acos(v); break;                                 // acos
		case 13: dst[i] = atan(v); break;                                 // atan
		case 14: dst[i] = sinh(v); break;                                 // sinh
		case 15: dst[i] = cosh(v); break;                                 // cosh
		case 16: dst[i] = asinh(v); break;                                // asinh
		case 17: dst[i] = acosh(v); break;                                // acosh
		case 18: dst[i] = atanh(v); break;                                // atanh
		case 19: dst[i] = log10(v); break;                                // log10
		case 20: dst[i] = log2(v); break;                                 // log2
		case 21: dst[i] = floor(v); break;                                // floor
		case 22: dst[i] = ceil(v); break;                                 // ceil
		case 23: dst[i] = round(v); break;                                // round
		case 24: dst[i] = trunc(v); break;                                // trunc
		case 25: dst[i] = expm1(v); break;                                // expm1
		case 26: dst[i] = log1p(v); break;                                // log1p
		case 27: dst[i] = erf(v); break;                                  // erf
		case 28: dst[i] = erfc(v); break;                                 // erfc
		case 29: dst[i] = tgamma(v); break;                               // tgamma
		case 30: dst[i] = lgamma(v); break;                               // lgamma
		case 31: dst[i] = fabs(v); break;                                 // abs
		case 32: dst[i] = (v > T(0)) ? T(1) : ((v < T(0)) ? T(-1) : T(0)); break; // sign
		case 33: dst[i] = exp2(v); break;                                 // exp2
		case 34: dst[i] = cbrt(v); break;                                 // cbrt
		case 35: dst[i] = rsqrt(v); break;                                // rsqrt
		case 36: dst[i] = T(1) / v; break;                                // recip
		case 37: dst[i] = -v; break;                                      // neg
		case 38: dst[i] = v * v; break;                                   // square
		case 39: dst[i] = isnan(v) ? T(1) : T(0); break;                  // isnan
		case 40: dst[i] = isinf(v) ? T(1) : T(0); break;                  // isinf
		case 41: dst[i] = isfinite(v) ? T(1) : T(0); break;               // isfinite
		case 42: dst[i] = v / (T(1) + exp(-v)); break;                   // silu
		default: dst[i] = v; break;
		}
	}

	template<typename T>
	int launch_unary(void* dst, const void* a, std::size_t count, int op)
	{
		if (count == 0) return 0;
		const std::size_t bytes = count * sizeof(T);
		T* d_dst = (T*)pool_alloc(bytes);
		T* d_a = (T*)pool_alloc(bytes);
		if (!d_dst || !d_a) { pool_free(d_dst, bytes); pool_free(d_a, bytes); return -1; }
		cudaStream_t s = default_stream();
		if (cudaMemcpyAsync(d_a, a, bytes, cudaMemcpyHostToDevice, s) != cudaSuccess)
		{
			cudaStreamSynchronize(s); pool_free(d_dst, bytes); pool_free(d_a, bytes); return -1;
		}
		const int block = 256;
		const int grid = (int)((count + block - 1) / block);
		unary_kernel<T><<<grid, block, 0, s>>>(d_dst, d_a, count, op);
		if (cudaGetLastError() != cudaSuccess ||
			cudaMemcpyAsync(dst, d_dst, bytes, cudaMemcpyDeviceToHost, s) != cudaSuccess)
		{
			cudaStreamSynchronize(s); pool_free(d_dst, bytes); pool_free(d_a, bytes); return -1;
		}
		cudaStreamSynchronize(s);
		pool_free(d_dst, bytes); pool_free(d_a, bytes);
		return 0;
	}

	// ---- f16 unary: compute in float then convert back (avoid __half/float operator ambiguity) ----
	__global__ void unary_f16_kernel(__half* dst, const __half* a, std::size_t n, int op)
	{
		std::size_t i = blockIdx.x * blockDim.x + threadIdx.x;
		if (i >= n) return;
		float v = __half2float(a[i]);
		float r = v;
		switch (op)
		{
		case 0: r = v > 0.0f ? v : 0.0f; break;                       // relu
		case 1: r = v > 0.0f ? v : 0.01f * v; break;                 // leaky_relu
		case 2: r = 1.0f / (1.0f + expf(-v)); break;                 // sigmoid
		case 3: { float c = 0.7978845608028654f; float t = tanhf(c * (v + 0.044715f * v * v * v)); r = 0.5f * v * (1.0f + t); break; } // gelu
		case 4: r = expf(v); break;                                   // exp
		case 5: r = logf(v); break;                                   // log
		case 6: r = tanhf(v); break;                                  // tanh
		case 7: r = sqrtf(v); break;                                  // sqrt
		case 8: r = sinf(v); break;                                   // sin
		case 9: r = cosf(v); break;                                   // cos
		case 10: r = tanf(v); break;                                  // tan
		case 11: r = asinf(v); break;                                 // asin
		case 12: r = acosf(v); break;                                 // acos
		case 13: r = atanf(v); break;                                 // atan
		case 14: r = sinhf(v); break;                                 // sinh
		case 15: r = coshf(v); break;                                 // cosh
		case 16: r = asinhf(v); break;                                // asinh
		case 17: r = acoshf(v); break;                                // acosh
		case 18: r = atanhf(v); break;                                // atanh
		case 19: r = log10f(v); break;                                // log10
		case 20: r = log2f(v); break;                                 // log2
		case 21: r = floorf(v); break;                                // floor
		case 22: r = ceilf(v); break;                                 // ceil
		case 23: r = roundf(v); break;                                // round
		case 24: r = truncf(v); break;                                // trunc
		case 25: r = expm1f(v); break;                                // expm1
		case 26: r = log1pf(v); break;                                // log1p
		case 27: r = erff(v); break;                                  // erf
		case 28: r = erfcf(v); break;                                 // erfc
		case 29: r = tgammaf(v); break;                               // tgamma
		case 30: r = lgammaf(v); break;                               // lgamma
		case 31: r = fabsf(v); break;                                 // abs
		case 32: r = (v > 0.0f) ? 1.0f : ((v < 0.0f) ? -1.0f : 0.0f); break; // sign
		case 33: r = exp2f(v); break;                                 // exp2
		case 34: r = cbrtf(v); break;                                 // cbrt
		case 35: r = rsqrtf(v); break;                                // rsqrt
		case 36: r = 1.0f / v; break;                                 // recip
		case 37: r = -v; break;                                       // neg
		case 38: r = v * v; break;                                    // square
		case 42: r = v / (1.0f + expf(-v)); break;                    // silu
		default: r = v; break;
		}
		dst[i] = __float2half(r);
	}

	int launch_unary_f16(void* dst, const void* a, std::size_t count, int op)
	{
		if (count == 0) return 0;
		const std::size_t bytes = count * sizeof(__half);
		__half* d_dst = (__half*)pool_alloc(bytes);
		__half* d_a = (__half*)pool_alloc(bytes);
		if (!d_dst || !d_a) { pool_free(d_dst, bytes); pool_free(d_a, bytes); return -1; }
		cudaStream_t s = default_stream();
		if (cudaMemcpyAsync(d_a, a, bytes, cudaMemcpyHostToDevice, s) != cudaSuccess)
		{
			cudaStreamSynchronize(s); pool_free(d_dst, bytes); pool_free(d_a, bytes); return -1;
		}
		const int block = 256;
		const int grid = (int)((count + block - 1) / block);
		unary_f16_kernel<<<grid, block, 0, s>>>(d_dst, d_a, count, op);
		if (cudaGetLastError() != cudaSuccess ||
			cudaMemcpyAsync(dst, d_dst, bytes, cudaMemcpyDeviceToHost, s) != cudaSuccess)
		{
			cudaStreamSynchronize(s); pool_free(d_dst, bytes); pool_free(d_a, bytes); return -1;
		}
		cudaStreamSynchronize(s);
		pool_free(d_dst, bytes); pool_free(d_a, bytes);
		return 0;
	}

	// ---- gradient accumulate: dst[i] += f(a[i], b[i]) ----
	template<typename T>
	__global__ void grad_accum_kernel(T* dst, const T* a, const T* b, std::size_t n, int op)
	{
		std::size_t i = blockIdx.x * blockDim.x + threadIdx.x;
		if (i >= n) return;
		T g;
		switch (op)
		{
		case 0: g = (b[i] > T(0)) ? a[i] : T(0); break;                    // relu_grad
		case 1: g = (b[i] > T(0)) ? a[i] : T(0.01) * a[i]; break;          // leaky_relu_grad
		default: g = a[i] * b[i] * (T(1) - b[i]); break;                   // sigmoid_grad
		}
		dst[i] += g;
	}

	template<typename T>
	int launch_grad_accum(void* dst, const void* a, const void* b, std::size_t count, int op)
	{
		if (count == 0) return 0;
		const std::size_t bytes = count * sizeof(T);
		T* d_dst = (T*)pool_alloc(bytes);
		T* d_a = (T*)pool_alloc(bytes);
		T* d_b = (T*)pool_alloc(bytes);
		if (!d_dst || !d_a || !d_b) { pool_free(d_dst, bytes); pool_free(d_a, bytes); pool_free(d_b, bytes); return -1; }
		cudaStream_t s = default_stream();
		if (cudaMemcpyAsync(d_dst, dst, bytes, cudaMemcpyHostToDevice, s) != cudaSuccess ||
			cudaMemcpyAsync(d_a, a, bytes, cudaMemcpyHostToDevice, s) != cudaSuccess ||
			cudaMemcpyAsync(d_b, b, bytes, cudaMemcpyHostToDevice, s) != cudaSuccess)
		{
			cudaStreamSynchronize(s); pool_free(d_dst, bytes); pool_free(d_a, bytes); pool_free(d_b, bytes); return -1;
		}
		const int block = 256;
		const int grid = (int)((count + block - 1) / block);
		grad_accum_kernel<T><<<grid, block, 0, s>>>(d_dst, d_a, d_b, count, op);
		if (cudaGetLastError() != cudaSuccess ||
			cudaMemcpyAsync(dst, d_dst, bytes, cudaMemcpyDeviceToHost, s) != cudaSuccess)
		{
			cudaStreamSynchronize(s); pool_free(d_dst, bytes); pool_free(d_a, bytes); pool_free(d_b, bytes); return -1;
		}
		cudaStreamSynchronize(s);
		pool_free(d_dst, bytes); pool_free(d_a, bytes); pool_free(d_b, bytes);
		return 0;
	}

	// ---- transpose: dst[cols x rows] = src[rows x cols]^T ----
	template<typename T>
	__global__ void transpose_kernel(T* dst, const T* src, std::size_t rows, std::size_t cols)
	{
		std::size_t idx = blockIdx.x * blockDim.x + threadIdx.x;
		if (idx >= rows * cols) return;
		std::size_t i = idx / cols, j = idx % cols;
		dst[j * rows + i] = src[i * cols + j];
	}

	template<typename T>
	int launch_transpose(void* dst, const void* src, std::size_t rows, std::size_t cols)
	{
		if (rows == 0 || cols == 0) return 0;
		const std::size_t bytes = rows * cols * sizeof(T);
		T* d_dst = (T*)pool_alloc(bytes);
		T* d_src = (T*)pool_alloc(bytes);
		if (!d_dst || !d_src) { pool_free(d_dst, bytes); pool_free(d_src, bytes); return -1; }
		cudaStream_t s = default_stream();
		if (cudaMemcpyAsync(d_src, src, bytes, cudaMemcpyHostToDevice, s) != cudaSuccess)
		{
			cudaStreamSynchronize(s); pool_free(d_dst, bytes); pool_free(d_src, bytes); return -1;
		}
		const int block = 256;
		const int grid = (int)((rows * cols + block - 1) / block);
		transpose_kernel<T><<<grid, block, 0, s>>>(d_dst, d_src, rows, cols);
		if (cudaGetLastError() != cudaSuccess ||
			cudaMemcpyAsync(dst, d_dst, bytes, cudaMemcpyDeviceToHost, s) != cudaSuccess)
		{
			cudaStreamSynchronize(s); pool_free(d_dst, bytes); pool_free(d_src, bytes); return -1;
		}
		cudaStreamSynchronize(s);
		pool_free(d_dst, bytes); pool_free(d_src, bytes);
		return 0;
	}

	// ---- 稀疏矩阵 × 稠密向量（COO）：y[row[k]] += val[k] * x[col[k]]，原子累加 ----
	template<typename T>
	__global__ void spmv_kernel(T* y, const T* x, const unsigned long long* row, const unsigned long long* col, const T* val, std::size_t nnz)
	{
		std::size_t k = blockIdx.x * blockDim.x + threadIdx.x;
		if (k >= nnz) return;
		atomicAdd(&y[row[k]], val[k] * x[col[k]]);
	}

	template<typename T>
	int launch_spmv(void* y, const void* x, const unsigned long long* row, const unsigned long long* col,
		const void* val, std::size_t nnz, std::size_t rows, std::size_t cols)
	{
		if (nnz == 0) return 0;
		const std::size_t x_bytes = cols * sizeof(T);
		const std::size_t y_bytes = rows * sizeof(T);
		const std::size_t val_bytes = nnz * sizeof(T);
		const std::size_t idx_bytes = nnz * sizeof(unsigned long long);
		T* d_y = (T*)pool_alloc(y_bytes);
		T* d_x = (T*)pool_alloc(x_bytes);
		unsigned long long* d_row = (unsigned long long*)pool_alloc(idx_bytes);
		unsigned long long* d_col = (unsigned long long*)pool_alloc(idx_bytes);
		T* d_val = (T*)pool_alloc(val_bytes);
		if (!d_y || !d_x || !d_row || !d_col || !d_val)
		{
			pool_free(d_y, y_bytes); pool_free(d_x, x_bytes); pool_free(d_row, idx_bytes);
			pool_free(d_col, idx_bytes); pool_free(d_val, val_bytes);
			return -1;
		}
		cudaStream_t s = default_stream();
		if (cudaMemcpyAsync(d_y, y, y_bytes, cudaMemcpyHostToDevice, s) != cudaSuccess ||
			cudaMemcpyAsync(d_x, x, x_bytes, cudaMemcpyHostToDevice, s) != cudaSuccess ||
			cudaMemcpyAsync(d_row, row, idx_bytes, cudaMemcpyHostToDevice, s) != cudaSuccess ||
			cudaMemcpyAsync(d_col, col, idx_bytes, cudaMemcpyHostToDevice, s) != cudaSuccess ||
			cudaMemcpyAsync(d_val, val, val_bytes, cudaMemcpyHostToDevice, s) != cudaSuccess)
		{
			cudaStreamSynchronize(s);
			pool_free(d_y, y_bytes); pool_free(d_x, x_bytes); pool_free(d_row, idx_bytes);
			pool_free(d_col, idx_bytes); pool_free(d_val, val_bytes);
			return -1;
		}
		const int block = 256;
		const int grid = (int)((nnz + block - 1) / block);
		spmv_kernel<T><<<grid, block, 0, s>>>(d_y, d_x, d_row, d_col, d_val, nnz);
		if (cudaGetLastError() != cudaSuccess ||
			cudaMemcpyAsync(y, d_y, y_bytes, cudaMemcpyDeviceToHost, s) != cudaSuccess)
		{
			cudaStreamSynchronize(s);
			pool_free(d_y, y_bytes); pool_free(d_x, x_bytes); pool_free(d_row, idx_bytes);
			pool_free(d_col, idx_bytes); pool_free(d_val, val_bytes);
			return -1;
		}
		cudaStreamSynchronize(s);
		pool_free(d_y, y_bytes); pool_free(d_x, x_bytes); pool_free(d_row, idx_bytes);
		pool_free(d_col, idx_bytes); pool_free(d_val, val_bytes);
		return 0;
	}

	// ---- i8/i16 SpMV：atomicAdd 无 8/16 位重载，先用 int32 临时缓冲累加再窄化 ----
	template<typename T>
	__global__ void spmv_small_kernel(int* y, const T* x, const unsigned long long* row, const unsigned long long* col, const T* val, std::size_t nnz)
	{
		std::size_t k = blockIdx.x * blockDim.x + threadIdx.x;
		if (k >= nnz) return;
		atomicAdd(&y[row[k]], (int)(val[k] * x[col[k]]));
	}

	template<typename T>
	__global__ void spmv_narrow_kernel(T* dst, const int* src, std::size_t n)
	{
		std::size_t i = blockIdx.x * blockDim.x + threadIdx.x;
		if (i >= n) return;
		dst[i] = (T)src[i];
	}

	template<typename T>
	int launch_spmv_small(void* y, const void* x, const unsigned long long* row, const unsigned long long* col,
		const void* val, std::size_t nnz, std::size_t rows, std::size_t cols)
	{
		if (nnz == 0) return 0;
		const std::size_t x_bytes = cols * sizeof(T);
		const std::size_t y_bytes = rows * sizeof(T);
		const std::size_t yi_bytes = rows * sizeof(int);
		const std::size_t val_bytes = nnz * sizeof(T);
		const std::size_t idx_bytes = nnz * sizeof(unsigned long long);
		T* d_x = (T*)pool_alloc(x_bytes);
		unsigned long long* d_row = (unsigned long long*)pool_alloc(idx_bytes);
		unsigned long long* d_col = (unsigned long long*)pool_alloc(idx_bytes);
		T* d_val = (T*)pool_alloc(val_bytes);
		int* d_yi = (int*)pool_alloc(yi_bytes);
		T* d_y = (T*)pool_alloc(y_bytes);
		if (!d_x || !d_row || !d_col || !d_val || !d_yi || !d_y)
		{
			pool_free(d_x, x_bytes); pool_free(d_row, idx_bytes); pool_free(d_col, idx_bytes);
			pool_free(d_val, val_bytes); pool_free(d_yi, yi_bytes); pool_free(d_y, y_bytes);
			return -1;
		}
		cudaStream_t s = default_stream();
		if (cudaMemsetAsync(d_yi, 0, yi_bytes, s) != cudaSuccess ||
			cudaMemcpyAsync(d_x, x, x_bytes, cudaMemcpyHostToDevice, s) != cudaSuccess ||
			cudaMemcpyAsync(d_row, row, idx_bytes, cudaMemcpyHostToDevice, s) != cudaSuccess ||
			cudaMemcpyAsync(d_col, col, idx_bytes, cudaMemcpyHostToDevice, s) != cudaSuccess ||
			cudaMemcpyAsync(d_val, val, val_bytes, cudaMemcpyHostToDevice, s) != cudaSuccess)
		{
			cudaStreamSynchronize(s);
			pool_free(d_x, x_bytes); pool_free(d_row, idx_bytes); pool_free(d_col, idx_bytes);
			pool_free(d_val, val_bytes); pool_free(d_yi, yi_bytes); pool_free(d_y, y_bytes);
			return -1;
		}
		const int block = 256;
		const int grid = (int)((nnz + block - 1) / block);
		spmv_small_kernel<T><<<grid, block, 0, s>>>(d_yi, d_x, d_row, d_col, d_val, nnz);
		const int grid2 = (int)((rows + block - 1) / block);
		spmv_narrow_kernel<T><<<grid2, block, 0, s>>>(d_y, d_yi, rows);
		if (cudaGetLastError() != cudaSuccess ||
			cudaMemcpyAsync(y, d_y, y_bytes, cudaMemcpyDeviceToHost, s) != cudaSuccess)
		{
			cudaStreamSynchronize(s);
			pool_free(d_x, x_bytes); pool_free(d_row, idx_bytes); pool_free(d_col, idx_bytes);
			pool_free(d_val, val_bytes); pool_free(d_yi, yi_bytes); pool_free(d_y, y_bytes);
			return -1;
		}
		cudaStreamSynchronize(s);
		pool_free(d_x, x_bytes); pool_free(d_row, idx_bytes); pool_free(d_col, idx_bytes);
		pool_free(d_val, val_bytes); pool_free(d_yi, yi_bytes); pool_free(d_y, y_bytes);
		return 0;
	}

	// ---- f16 SpMV: atomicAdd has no __half overload, accumulate in float then convert back ----
	__global__ void spmv_f16_kernel(float* y, const __half* x, const unsigned long long* row,
		const unsigned long long* col, const __half* val, std::size_t nnz)
	{
		std::size_t k = blockIdx.x * blockDim.x + threadIdx.x;
		if (k >= nnz) return;
		atomicAdd(&y[row[k]], __half2float(val[k]) * __half2float(x[col[k]]));
	}

	__global__ void f16_store_kernel(__half* dst, const float* src, std::size_t n)
	{
		std::size_t i = blockIdx.x * blockDim.x + threadIdx.x;
		if (i >= n) return;
		dst[i] = __float2half(src[i]);
	}

	int launch_spmv_f16(void* y, const void* x, const unsigned long long* row, const unsigned long long* col,
		const void* val, std::size_t nnz, std::size_t rows, std::size_t cols)
	{
		if (nnz == 0) return 0;
		const std::size_t x_bytes = cols * sizeof(__half);
		const std::size_t val_bytes = nnz * sizeof(__half);
		const std::size_t idx_bytes = nnz * sizeof(unsigned long long);
		const std::size_t yf_bytes = rows * sizeof(float);
		const std::size_t yh_bytes = rows * sizeof(__half);
		float* d_yf = (float*)pool_alloc(yf_bytes);
		__half* d_x = (__half*)pool_alloc(x_bytes);
		unsigned long long* d_row = (unsigned long long*)pool_alloc(idx_bytes);
		unsigned long long* d_col = (unsigned long long*)pool_alloc(idx_bytes);
		__half* d_val = (__half*)pool_alloc(val_bytes);
		__half* d_yh = (__half*)pool_alloc(yh_bytes);
		if (!d_yf || !d_x || !d_row || !d_col || !d_val || !d_yh)
		{
			pool_free(d_yf, yf_bytes); pool_free(d_x, x_bytes); pool_free(d_row, idx_bytes);
			pool_free(d_col, idx_bytes); pool_free(d_val, val_bytes); pool_free(d_yh, yh_bytes);
			return -1;
		}
		cudaStream_t s = default_stream();
		if (cudaMemsetAsync(d_yf, 0, yf_bytes, s) != cudaSuccess ||
			cudaMemcpyAsync(d_x, x, x_bytes, cudaMemcpyHostToDevice, s) != cudaSuccess ||
			cudaMemcpyAsync(d_row, row, idx_bytes, cudaMemcpyHostToDevice, s) != cudaSuccess ||
			cudaMemcpyAsync(d_col, col, idx_bytes, cudaMemcpyHostToDevice, s) != cudaSuccess ||
			cudaMemcpyAsync(d_val, val, val_bytes, cudaMemcpyHostToDevice, s) != cudaSuccess)
		{
			cudaStreamSynchronize(s);
			pool_free(d_yf, yf_bytes); pool_free(d_x, x_bytes); pool_free(d_row, idx_bytes);
			pool_free(d_col, idx_bytes); pool_free(d_val, val_bytes); pool_free(d_yh, yh_bytes);
			return -1;
		}
		const int block = 256;
		const int grid = (int)((nnz + block - 1) / block);
		spmv_f16_kernel<<<grid, block, 0, s>>>(d_yf, d_x, d_row, d_col, d_val, nnz);
		const int grid2 = (int)((rows + block - 1) / block);
		f16_store_kernel<<<grid2, block, 0, s>>>(d_yh, d_yf, rows);
		if (cudaGetLastError() != cudaSuccess ||
			cudaMemcpyAsync(y, d_yh, yh_bytes, cudaMemcpyDeviceToHost, s) != cudaSuccess)
		{
			cudaStreamSynchronize(s);
			pool_free(d_yf, yf_bytes); pool_free(d_x, x_bytes); pool_free(d_row, idx_bytes);
			pool_free(d_col, idx_bytes); pool_free(d_val, val_bytes); pool_free(d_yh, yh_bytes);
			return -1;
		}
		cudaStreamSynchronize(s);
		pool_free(d_yf, yf_bytes); pool_free(d_x, x_bytes); pool_free(d_row, idx_bytes);
		pool_free(d_col, idx_bytes); pool_free(d_val, val_bytes); pool_free(d_yh, yh_bytes);
		return 0;
	}

	// ---- 3D convolution (NCDHW, groups/bias/stride/padding/dilation), one thread per output element ----
	// 1D/2D reuse this kernel by padding size-1 dims at the caller.
	template<typename T>
	__global__ void conv3d_kernel(T* dst, const T* src, const T* weight, const T* bias,
		int n, int cin, int cout, int id, int ih, int iw, int kd, int kh, int kw,
		int od, int oh, int ow, int stride_d, int stride_h, int stride_w,
		int pad_d, int pad_h, int pad_w, int dil_d, int dil_h, int dil_w, int groups)
	{
		int idx = blockIdx.x * blockDim.x + threadIdx.x;
		int total = n * cout * od * oh * ow;
		if (idx >= total) return;
		int oww = idx % ow;
		int ohh = (idx / ow) % oh;
		int odd = (idx / (ow * oh)) % od;
		int co = (idx / (ow * oh * od)) % cout;
		int nn = idx / (ow * oh * od * cout);
		int cin_g = cin / groups;
		int cout_g = cout / groups;
		int g = co / cout_g;
		int cin_base = g * cin_g;
		T acc = (bias != nullptr) ? bias[co] : T(0);
		for (int ci = 0; ci < cin_g; ++ci)
		{
			int c = cin_base + ci;
			const T* src_c = src + (nn * cin + c) * id * ih * iw;
			const T* w_c = weight + (co * cin_g + ci) * kd * kh * kw;
			for (int k0 = 0; k0 < kd; ++k0)
			{
				int ii = odd * stride_d - pad_d + dil_d * k0;
				if (ii < 0 || ii >= id) continue;
				for (int k1 = 0; k1 < kh; ++k1)
				{
					int jj = ohh * stride_h - pad_h + dil_h * k1;
					if (jj < 0 || jj >= ih) continue;
					for (int k2 = 0; k2 < kw; ++k2)
					{
						int kk = oww * stride_w - pad_w + dil_w * k2;
						if (kk < 0 || kk >= iw) continue;
						acc += src_c[(ii * ih + jj) * iw + kk] * w_c[(k0 * kh + k1) * kw + k2];
					}
				}
			}
		}
		dst[idx] = acc;
	}

	template<typename T>
	int launch_conv3d(void* dst, const void* src, const void* weight, const void* bias,
		std::size_t n, std::size_t cin, std::size_t cout,
		std::size_t id, std::size_t ih, std::size_t iw, std::size_t kd, std::size_t kh, std::size_t kw,
		std::size_t stride_d, std::size_t stride_h, std::size_t stride_w, std::size_t pad_d, std::size_t pad_h, std::size_t pad_w,
		std::size_t dil_d, std::size_t dil_h, std::size_t dil_w, std::size_t groups)
	{
		const std::size_t od = (id + 2 * pad_d - dil_d * (kd - 1) - 1) / stride_d + 1;
		const std::size_t oh = (ih + 2 * pad_h - dil_h * (kh - 1) - 1) / stride_h + 1;
		const std::size_t ow = (iw + 2 * pad_w - dil_w * (kw - 1) - 1) / stride_w + 1;
		const std::size_t total = n * cout * od * oh * ow;
		if (total == 0) return 0;
		const std::size_t src_bytes = n * cin * id * ih * iw * sizeof(T);
		const std::size_t w_bytes = cout * (cin / groups) * kd * kh * kw * sizeof(T);
		const std::size_t b_bytes = bias ? cout * sizeof(T) : 0;
		const std::size_t dst_bytes = total * sizeof(T);
		T* d_dst = (T*)pool_alloc(dst_bytes);
		T* d_src = (T*)pool_alloc(src_bytes);
		T* d_w = (T*)pool_alloc(w_bytes);
		T* d_b = bias ? (T*)pool_alloc(b_bytes) : nullptr;
		if (!d_dst || !d_src || !d_w || (bias && !d_b))
		{
			pool_free(d_dst, dst_bytes); pool_free(d_src, src_bytes); pool_free(d_w, w_bytes);
			if (d_b) pool_free(d_b, b_bytes);
			return -1;
		}
		cudaStream_t s = default_stream();
		if (cudaMemcpyAsync(d_src, src, src_bytes, cudaMemcpyHostToDevice, s) != cudaSuccess ||
			cudaMemcpyAsync(d_w, weight, w_bytes, cudaMemcpyHostToDevice, s) != cudaSuccess ||
			(bias && cudaMemcpyAsync(d_b, bias, b_bytes, cudaMemcpyHostToDevice, s) != cudaSuccess))
		{
			cudaStreamSynchronize(s);
			pool_free(d_dst, dst_bytes); pool_free(d_src, src_bytes); pool_free(d_w, w_bytes);
			if (d_b) pool_free(d_b, b_bytes);
			return -1;
		}
		const int block = 256;
		const int grid = (int)((total + block - 1) / block);
		conv3d_kernel<T><<<grid, block, 0, s>>>(d_dst, d_src, d_w, d_b,
			(int)n, (int)cin, (int)cout, (int)id, (int)ih, (int)iw, (int)kd, (int)kh, (int)kw,
			(int)od, (int)oh, (int)ow, (int)stride_d, (int)stride_h, (int)stride_w,
			(int)pad_d, (int)pad_h, (int)pad_w, (int)dil_d, (int)dil_h, (int)dil_w, (int)groups);
		if (cudaGetLastError() != cudaSuccess ||
			cudaMemcpyAsync(dst, d_dst, dst_bytes, cudaMemcpyDeviceToHost, s) != cudaSuccess)
		{
			cudaStreamSynchronize(s);
			pool_free(d_dst, dst_bytes); pool_free(d_src, src_bytes); pool_free(d_w, w_bytes);
			if (d_b) pool_free(d_b, b_bytes);
			return -1;
		}
		cudaStreamSynchronize(s);
		pool_free(d_dst, dst_bytes); pool_free(d_src, src_bytes); pool_free(d_w, w_bytes);
		if (d_b) pool_free(d_b, b_bytes);
		return 0;
	}

	// ---- 4D convolution (N,C,D0,D1,D2,D3), one thread per output element ----
	template<typename T>
	__global__ void conv4d_kernel(T* dst, const T* src, const T* weight, const T* bias,
		int n, int cin, int cout, int i0, int i1, int i2, int i3,
		int k0, int k1, int k2, int k3, int o0, int o1, int o2, int o3,
		int s0, int s1, int s2, int s3, int p0, int p1, int p2, int p3,
		int d0, int d1, int d2, int d3, int groups)
	{
		int idx = blockIdx.x * blockDim.x + threadIdx.x;
		int total = n * cout * o0 * o1 * o2 * o3;
		if (idx >= total) return;
		int o3i = idx % o3;
		int o2i = (idx / o3) % o2;
		int o1i = (idx / (o3 * o2)) % o1;
		int o0i = (idx / (o3 * o2 * o1)) % o0;
		int co = (idx / (o3 * o2 * o1 * o0)) % cout;
		int nn = idx / (o3 * o2 * o1 * o0 * cout);
		int cin_g = cin / groups, cout_g = cout / groups;
		int g = co / cout_g;
		int cin_base = g * cin_g;
		T acc = (bias != nullptr) ? bias[co] : T(0);
		for (int ci = 0; ci < cin_g; ++ci)
		{
			int c = cin_base + ci;
			const T* src_c = src + (nn * cin + c) * i0 * i1 * i2 * i3;
			const T* w_c = weight + (co * cin_g + ci) * k0 * k1 * k2 * k3;
			for (int k0i = 0; k0i < k0; ++k0i)
			{
				int ii0 = o0i * s0 - p0 + d0 * k0i;
				if (ii0 < 0 || ii0 >= i0) continue;
				for (int k1i = 0; k1i < k1; ++k1i)
				{
					int ii1 = o1i * s1 - p1 + d1 * k1i;
					if (ii1 < 0 || ii1 >= i1) continue;
					for (int k2i = 0; k2i < k2; ++k2i)
					{
						int ii2 = o2i * s2 - p2 + d2 * k2i;
						if (ii2 < 0 || ii2 >= i2) continue;
						for (int k3i = 0; k3i < k3; ++k3i)
						{
							int ii3 = o3i * s3 - p3 + d3 * k3i;
							if (ii3 < 0 || ii3 >= i3) continue;
							acc += src_c[((ii0 * i1 + ii1) * i2 + ii2) * i3 + ii3] * w_c[((k0i * k1 + k1i) * k2 + k2i) * k3 + k3i];
						}
					}
				}
			}
		}
		dst[idx] = acc;
	}

	template<typename T>
	int launch_conv4d(void* dst, const void* src, const void* weight, const void* bias,
		std::size_t n, std::size_t cin, std::size_t cout,
		std::size_t i0, std::size_t i1, std::size_t i2, std::size_t i3,
		std::size_t k0, std::size_t k1, std::size_t k2, std::size_t k3,
		std::size_t s0, std::size_t s1, std::size_t s2, std::size_t s3,
		std::size_t p0, std::size_t p1, std::size_t p2, std::size_t p3,
		std::size_t d0, std::size_t d1, std::size_t d2, std::size_t d3, std::size_t groups)
	{
		const std::size_t o0 = (i0 + 2 * p0 - d0 * (k0 - 1) - 1) / s0 + 1;
		const std::size_t o1 = (i1 + 2 * p1 - d1 * (k1 - 1) - 1) / s1 + 1;
		const std::size_t o2 = (i2 + 2 * p2 - d2 * (k2 - 1) - 1) / s2 + 1;
		const std::size_t o3 = (i3 + 2 * p3 - d3 * (k3 - 1) - 1) / s3 + 1;
		const std::size_t total = n * cout * o0 * o1 * o2 * o3;
		if (total == 0) return 0;
		const std::size_t src_bytes = n * cin * i0 * i1 * i2 * i3 * sizeof(T);
		const std::size_t w_bytes = cout * (cin / groups) * k0 * k1 * k2 * k3 * sizeof(T);
		const std::size_t b_bytes = bias ? cout * sizeof(T) : 0;
		const std::size_t dst_bytes = total * sizeof(T);
		T* d_dst = (T*)pool_alloc(dst_bytes);
		T* d_src = (T*)pool_alloc(src_bytes);
		T* d_w = (T*)pool_alloc(w_bytes);
		T* d_b = bias ? (T*)pool_alloc(b_bytes) : nullptr;
		if (!d_dst || !d_src || !d_w || (bias && !d_b))
		{
			pool_free(d_dst, dst_bytes); pool_free(d_src, src_bytes); pool_free(d_w, w_bytes);
			if (d_b) pool_free(d_b, b_bytes);
			return -1;
		}
		cudaStream_t s = default_stream();
		if (cudaMemcpyAsync(d_src, src, src_bytes, cudaMemcpyHostToDevice, s) != cudaSuccess ||
			cudaMemcpyAsync(d_w, weight, w_bytes, cudaMemcpyHostToDevice, s) != cudaSuccess ||
			(bias && cudaMemcpyAsync(d_b, bias, b_bytes, cudaMemcpyHostToDevice, s) != cudaSuccess))
		{
			cudaStreamSynchronize(s);
			pool_free(d_dst, dst_bytes); pool_free(d_src, src_bytes); pool_free(d_w, w_bytes);
			if (d_b) pool_free(d_b, b_bytes);
			return -1;
		}
		const int block = 256;
		const int grid = (int)((total + block - 1) / block);
		conv4d_kernel<T><<<grid, block, 0, s>>>(d_dst, d_src, d_w, d_b,
			(int)n, (int)cin, (int)cout, (int)i0, (int)i1, (int)i2, (int)i3,
			(int)k0, (int)k1, (int)k2, (int)k3, (int)o0, (int)o1, (int)o2, (int)o3,
			(int)s0, (int)s1, (int)s2, (int)s3, (int)p0, (int)p1, (int)p2, (int)p3,
			(int)d0, (int)d1, (int)d2, (int)d3, (int)groups);
		if (cudaGetLastError() != cudaSuccess ||
			cudaMemcpyAsync(dst, d_dst, dst_bytes, cudaMemcpyDeviceToHost, s) != cudaSuccess)
		{
			cudaStreamSynchronize(s);
			pool_free(d_dst, dst_bytes); pool_free(d_src, src_bytes); pool_free(d_w, w_bytes);
			if (d_b) pool_free(d_b, b_bytes);
			return -1;
		}
		cudaStreamSynchronize(s);
		pool_free(d_dst, dst_bytes); pool_free(d_src, src_bytes); pool_free(d_w, w_bytes);
		if (d_b) pool_free(d_b, b_bytes);
		return 0;
	}

	// ---- 三角求解 trsv：cuBLAS（行优先三角阵按列优先转置后经 OP_T 求解）----
	template<typename T>
	int launch_trsv(void* x, const void* tri, const void* b, std::size_t n, bool lower, bool unit_diag)
	{
		if (n == 0) return 0;
		const std::size_t tri_bytes = n * n * sizeof(T);
		const std::size_t x_bytes = n * sizeof(T);
		T* d_tri = (T*)pool_alloc(tri_bytes);
		T* d_x = (T*)pool_alloc(x_bytes);
		if (!d_tri || !d_x) { pool_free(d_tri, tri_bytes); pool_free(d_x, x_bytes); return -1; }
		cudaStream_t s = default_stream();
		if (cudaMemcpyAsync(d_tri, tri, tri_bytes, cudaMemcpyHostToDevice, s) != cudaSuccess ||
			cudaMemcpyAsync(d_x, b, x_bytes, cudaMemcpyHostToDevice, s) != cudaSuccess)
		{
			cudaStreamSynchronize(s);
			pool_free(d_tri, tri_bytes); pool_free(d_x, x_bytes);
			return -1;
		}
		const cublasFillMode_t uplo = lower ? CUBLAS_FILL_MODE_UPPER : CUBLAS_FILL_MODE_LOWER;
		const cublasDiagType_t diag = unit_diag ? CUBLAS_DIAG_UNIT : CUBLAS_DIAG_NON_UNIT;
		if constexpr (sizeof(T) == 4)
			cublasStrsv(blas_handle(), uplo, CUBLAS_OP_T, diag, (int)n, d_tri, (int)n, d_x, 1);
		else
			cublasDtrsv(blas_handle(), uplo, CUBLAS_OP_T, diag, (int)n, d_tri, (int)n, d_x, 1);
		if (cudaMemcpyAsync(x, d_x, x_bytes, cudaMemcpyDeviceToHost, s) != cudaSuccess)
		{
			cudaStreamSynchronize(s);
			pool_free(d_tri, tri_bytes); pool_free(d_x, x_bytes);
			return -1;
		}
		cudaStreamSynchronize(s);
		pool_free(d_tri, tri_bytes); pool_free(d_x, x_bytes);
		return 0;
	}

	// ---- Cholesky：cuSOLVER potrf（UPPER 使行优先下三角 L 直接落位），非正定返回 -1 ----
	template<typename T>
	int launch_potrf(void* L, const void* A, std::size_t n)
	{
		if (n == 0) return 0;
		const std::size_t bytes = n * n * sizeof(T);
		T* d_A = (T*)pool_alloc(bytes);
		if (!d_A) return -1;
		cudaStream_t s = default_stream();
		if (cudaMemcpyAsync(d_A, A, bytes, cudaMemcpyHostToDevice, s) != cudaSuccess)
		{
			cudaStreamSynchronize(s);
			pool_free(d_A, bytes);
			return -1;
		}
		int lwork = 0;
		if constexpr (sizeof(T) == 4)
			cusolverDnSpotrf_bufferSize(solver_handle(), CUBLAS_FILL_MODE_UPPER, (int)n, d_A, (int)n, &lwork);
		else
			cusolverDnDpotrf_bufferSize(solver_handle(), CUBLAS_FILL_MODE_UPPER, (int)n, d_A, (int)n, &lwork);
		void* d_work = pool_alloc((std::size_t)lwork);
		int* d_info = (int*)pool_alloc(sizeof(int));
		if (!d_work || !d_info)
		{
			pool_free(d_A, bytes); pool_free(d_work, (std::size_t)lwork); pool_free(d_info, sizeof(int));
			return -1;
		}
		if constexpr (sizeof(T) == 4)
			cusolverDnSpotrf(solver_handle(), CUBLAS_FILL_MODE_UPPER, (int)n, d_A, (int)n, (T*)d_work, lwork, d_info);
		else
			cusolverDnDpotrf(solver_handle(), CUBLAS_FILL_MODE_UPPER, (int)n, d_A, (int)n, (T*)d_work, lwork, d_info);
		int info = 0;
		cudaMemcpyAsync(&info, d_info, sizeof(int), cudaMemcpyDeviceToHost, s);
		cudaStreamSynchronize(s);
		if (info != 0)
		{
			pool_free(d_A, bytes); pool_free(d_work, (std::size_t)lwork); pool_free(d_info, sizeof(int));
			return -1;
		}
		cudaMemcpyAsync(L, d_A, bytes, cudaMemcpyDeviceToHost, s);
		cudaStreamSynchronize(s);
		pool_free(d_A, bytes); pool_free(d_work, (std::size_t)lwork); pool_free(d_info, sizeof(int));
		// 清上三角（行优先）
		T* Lp = (T*)L;
		for (std::size_t i = 0; i < n; ++i)
			for (std::size_t j = i + 1; j < n; ++j)
				Lp[i * n + j] = T(0);
		return 0;
	}

	// ---- 整型矩阵乘（i8/i16/i32）：手工 GEMM（cuBLAS 无整型接口），一线程一输出元素；T 累加与 CPU 截断语义一致 ----
	template<typename T>
	__global__ void matmul_int_kernel(T* C, const T* A, const T* B, int m, int n, int k)
	{
		int row = blockIdx.y;
		int col = blockIdx.x * blockDim.x + threadIdx.x;
		if (row >= m || col >= n) return;
		T acc = 0;
		for (int p = 0; p < k; ++p) acc += A[row * k + p] * B[p * n + col];
		C[row * n + col] = acc;
	}

	template<typename T>
	int launch_matmul_int(void* c, const void* a, const void* b, std::size_t m, std::size_t n, std::size_t k)
	{
		if (m == 0 || n == 0 || k == 0) return 0;
		const std::size_t abytes = m * k * sizeof(T);
		const std::size_t bbytes = k * n * sizeof(T);
		const std::size_t cbytes = m * n * sizeof(T);
		T* d_a = (T*)pool_alloc(abytes);
		T* d_b = (T*)pool_alloc(bbytes);
		T* d_c = (T*)pool_alloc(cbytes);
		if (!d_a || !d_b || !d_c) { pool_free(d_a, abytes); pool_free(d_b, bbytes); pool_free(d_c, cbytes); return -1; }
		cudaStream_t s = default_stream();
		if (cudaMemcpyAsync(d_a, a, abytes, cudaMemcpyHostToDevice, s) != cudaSuccess ||
			cudaMemcpyAsync(d_b, b, bbytes, cudaMemcpyHostToDevice, s) != cudaSuccess)
		{
			cudaStreamSynchronize(s);
			pool_free(d_a, abytes); pool_free(d_b, bbytes); pool_free(d_c, cbytes);
			return -1;
		}
		const int block = 16;
		dim3 grid((int)((n + block - 1) / block), (int)m);
		matmul_int_kernel<T><<<grid, block, 0, s>>>(d_c, d_a, d_b, (int)m, (int)n, (int)k);
		if (cudaGetLastError() != cudaSuccess ||
			cudaMemcpyAsync(c, d_c, cbytes, cudaMemcpyDeviceToHost, s) != cudaSuccess)
		{
			cudaStreamSynchronize(s);
			pool_free(d_a, abytes); pool_free(d_b, bbytes); pool_free(d_c, cbytes);
			return -1;
		}
		cudaStreamSynchronize(s);
		pool_free(d_a, abytes); pool_free(d_b, bbytes); pool_free(d_c, cbytes);
		return 0;
	}

	// ---- 整型 unary（i8/i16/i32）：仅 neg(37)/abs(31)/sign(32)，其余回退 CPU ----
	template<typename T>
	__global__ void unary_int_kernel(T* dst, const T* a, int n, int op)
	{
		int i = blockIdx.x * blockDim.x + threadIdx.x;
		if (i >= n) return;
		T v = a[i];
		if (op == 37) dst[i] = -v;
		else if (op == 31) dst[i] = (v < 0) ? (T)-v : v;
		else if (op == 32) dst[i] = (v > 0) ? (T)1 : ((v < 0) ? (T)-1 : (T)0);
		else dst[i] = v;
	}

	template<typename T>
	int launch_unary_int(void* dst, const void* a, std::size_t count, int op)
	{
		if (op != 31 && op != 32 && op != 37) return -1;
		if (count == 0) return 0;
		const std::size_t bytes = count * sizeof(T);
		T* d_dst = (T*)pool_alloc(bytes);
		T* d_a = (T*)pool_alloc(bytes);
		if (!d_dst || !d_a) { pool_free(d_dst, bytes); pool_free(d_a, bytes); return -1; }
		cudaStream_t s = default_stream();
		if (cudaMemcpyAsync(d_a, a, bytes, cudaMemcpyHostToDevice, s) != cudaSuccess)
		{
			cudaStreamSynchronize(s); pool_free(d_dst, bytes); pool_free(d_a, bytes); return -1;
		}
		const int block = 256;
		const int grid = (int)((count + block - 1) / block);
		unary_int_kernel<T><<<grid, block, 0, s>>>(d_dst, d_a, (int)count, op);
		if (cudaGetLastError() != cudaSuccess ||
			cudaMemcpyAsync(dst, d_dst, bytes, cudaMemcpyDeviceToHost, s) != cudaSuccess)
		{
			cudaStreamSynchronize(s); pool_free(d_dst, bytes); pool_free(d_a, bytes); return -1;
		}
		cudaStreamSynchronize(s);
		pool_free(d_dst, bytes); pool_free(d_a, bytes);
		return 0;
	}

	// ---- 整型逐元素（i8/i16/i32）：仅 + - * /（op 0-3），其余回退 CPU（避免 float 函数实例化到 int）----
	template<typename T>
	__global__ void elemwise_int_kernel(T* dst, const T* a, const T* b, int n, int op)
	{
		int i = blockIdx.x * blockDim.x + threadIdx.x;
		if (i >= n) return;
		if (op == 0) dst[i] = a[i] + b[i];
		else if (op == 1) dst[i] = a[i] - b[i];
		else if (op == 2) dst[i] = a[i] * b[i];
		else dst[i] = a[i] / b[i];  // op == 3
	}

	template<typename T>
	int launch_elemwise_int(void* dst, const void* a, const void* b, std::size_t count, int op)
	{
		if (op < 0 || op > 3) return -1;
		if (count == 0) return 0;
		const std::size_t bytes = count * sizeof(T);
		T* d_dst = (T*)pool_alloc(bytes);
		T* d_a = (T*)pool_alloc(bytes);
		T* d_b = (T*)pool_alloc(bytes);
		if (!d_dst || !d_a || !d_b) { pool_free(d_dst, bytes); pool_free(d_a, bytes); pool_free(d_b, bytes); return -1; }
		cudaStream_t s = default_stream();
		if (cudaMemcpyAsync(d_a, a, bytes, cudaMemcpyHostToDevice, s) != cudaSuccess ||
			cudaMemcpyAsync(d_b, b, bytes, cudaMemcpyHostToDevice, s) != cudaSuccess)
		{
			cudaStreamSynchronize(s); pool_free(d_dst, bytes); pool_free(d_a, bytes); pool_free(d_b, bytes); return -1;
		}
		const int block = 256;
		const int grid = (int)((count + block - 1) / block);
		elemwise_int_kernel<T><<<grid, block, 0, s>>>(d_dst, d_a, d_b, (int)count, op);
		if (cudaGetLastError() != cudaSuccess ||
			cudaMemcpyAsync(dst, d_dst, bytes, cudaMemcpyDeviceToHost, s) != cudaSuccess)
		{
			cudaStreamSynchronize(s); pool_free(d_dst, bytes); pool_free(d_a, bytes); pool_free(d_b, bytes); return -1;
		}
		cudaStreamSynchronize(s);
		pool_free(d_dst, bytes); pool_free(d_a, bytes); pool_free(d_b, bytes);
		return 0;
	}

	// ---- 整型 trsv（i8/i16/i32）：列向前代/回代，每列一次对角除法 + 并行更新（cuBLAS 无整型接口）----
	template<typename T>
	__global__ void trsv_diag_kernel(T* x, const T* tri, int k, int n)
	{
		x[k] = x[k] / tri[k * n + k];
	}

	template<typename T>
	__global__ void trsv_update_lower_kernel(T* x, const T* tri, int k, int n)
	{
		int i = k + 1 + blockIdx.x * blockDim.x + threadIdx.x;
		if (i >= n) return;
		x[i] = x[i] - tri[i * n + k] * x[k];
	}

	template<typename T>
	__global__ void trsv_update_upper_kernel(T* x, const T* tri, int k, int n)
	{
		int i = blockIdx.x * blockDim.x + threadIdx.x;
		if (i >= k) return;
		x[i] = x[i] - tri[i * n + k] * x[k];
	}

	template<typename T>
	int launch_trsv_int(void* x, const void* tri, const void* b, std::size_t n, bool lower, bool unit_diag)
	{
		if (n == 0) return 0;
		const std::size_t tri_bytes = n * n * sizeof(T);
		const std::size_t x_bytes = n * sizeof(T);
		T* d_tri = (T*)pool_alloc(tri_bytes);
		T* d_x = (T*)pool_alloc(x_bytes);
		if (!d_tri || !d_x) { pool_free(d_tri, tri_bytes); pool_free(d_x, x_bytes); return -1; }
		cudaStream_t s = default_stream();
		if (cudaMemcpyAsync(d_tri, tri, tri_bytes, cudaMemcpyHostToDevice, s) != cudaSuccess ||
			cudaMemcpyAsync(d_x, b, x_bytes, cudaMemcpyHostToDevice, s) != cudaSuccess)
		{
			cudaStreamSynchronize(s);
			pool_free(d_tri, tri_bytes); pool_free(d_x, x_bytes);
			return -1;
		}
		const int block = 256;
		if (lower)
		{
			for (std::size_t k = 0; k < n; ++k)
			{
				if (!unit_diag) trsv_diag_kernel<T><<<1, 1, 0, s>>>(d_x, d_tri, (int)k, (int)n);
				const int grid = (int)((n - k - 1 + block - 1) / block);
				if (grid > 0) trsv_update_lower_kernel<T><<<grid, block, 0, s>>>(d_x, d_tri, (int)k, (int)n);
			}
		}
		else
		{
			for (std::size_t k = n; k-- > 0;)
			{
				if (!unit_diag) trsv_diag_kernel<T><<<1, 1, 0, s>>>(d_x, d_tri, (int)k, (int)n);
				const int grid = (int)((k + block - 1) / block);
				if (grid > 0) trsv_update_upper_kernel<T><<<grid, block, 0, s>>>(d_x, d_tri, (int)k, (int)n);
			}
		}
		if (cudaGetLastError() != cudaSuccess ||
			cudaMemcpyAsync(x, d_x, x_bytes, cudaMemcpyDeviceToHost, s) != cudaSuccess)
		{
			cudaStreamSynchronize(s);
			pool_free(d_tri, tri_bytes); pool_free(d_x, x_bytes);
			return -1;
		}
		cudaStreamSynchronize(s);
		pool_free(d_tri, tri_bytes); pool_free(d_x, x_bytes);
		return 0;
	}
}

extern "C" int xhqm_cuda_elemwise(void* dst, const void* a, const void* b,
	std::size_t count, int dtype, int op)
{
	if (dtype == dtype_f16) return launch_elemwise<__half>(dst, a, b, count, op);
	if (dtype == dtype_f32) return launch_elemwise<float>(dst, a, b, count, op);
	if (dtype == dtype_f64) return launch_elemwise<double>(dst, a, b, count, op);
	if (dtype == dtype_i8) return launch_elemwise_int<signed char>(dst, a, b, count, op);
	if (dtype == dtype_i16) return launch_elemwise_int<short>(dst, a, b, count, op);
	if (dtype == dtype_i32) return launch_elemwise_int<int>(dst, a, b, count, op);
	return -1;
}

extern "C" int xhqm_cuda_select(void* dst, const void* cond, const void* a, const void* b,
	std::size_t count, int dtype)
{
	if (dtype == dtype_f16) return launch_select<__half>(dst, cond, a, b, count);
	if (dtype == dtype_f32) return launch_select<float>(dst, cond, a, b, count);
	if (dtype == dtype_f64) return launch_select<double>(dst, cond, a, b, count);
	return -1;
}

extern "C" int xhqm_cuda_pool2d(void* dst, const void* src,
	std::size_t n, std::size_t c, std::size_t h, std::size_t w,
	std::size_t oh, std::size_t ow, std::size_t k, std::size_t stride, std::size_t pad, int dtype, int op)
{
	if (dtype == dtype_f16) return launch_pool2d<__half>(dst, src, n, c, h, w, oh, ow, k, stride, pad, op);
	if (dtype == dtype_f32) return launch_pool2d<float>(dst, src, n, c, h, w, oh, ow, k, stride, pad, op);
	if (dtype == dtype_f64) return launch_pool2d<double>(dst, src, n, c, h, w, oh, ow, k, stride, pad, op);
	return -1;
}

extern "C" int xhqm_cuda_upsample2d(void* dst, const void* src,
	std::size_t n, std::size_t c, std::size_t h, std::size_t w,
	std::size_t oh, std::size_t ow, int dtype, int op)
{
	if (dtype == dtype_f16) return launch_upsample2d<__half>(dst, src, n, c, h, w, oh, ow, op);
	if (dtype == dtype_f32) return launch_upsample2d<float>(dst, src, n, c, h, w, oh, ow, op);
	if (dtype == dtype_f64) return launch_upsample2d<double>(dst, src, n, c, h, w, oh, ow, op);
	return -1;
}

extern "C" int xhqm_cuda_dropout(void* dst, const void* src, std::size_t count,
	float p, unsigned int seed, int dtype)
{
	if (dtype == dtype_f16) return launch_dropout<__half>(dst, src, count, p, seed);
	if (dtype == dtype_f32) return launch_dropout<float>(dst, src, count, p, seed);
	if (dtype == dtype_f64) return launch_dropout<double>(dst, src, count, p, seed);
	return -1;
}

extern "C" int xhqm_cuda_matmul(void* c, const void* a, const void* b,
	std::size_t m, std::size_t n, std::size_t k, int dtype)
{
	if (dtype == dtype_f16) return launch_matmul_hgemm(c, a, b, m, n, k);
	if (dtype == dtype_f32) return launch_matmul_sgemm(c, a, b, m, n, k);
	if (dtype == dtype_f64) return launch_matmul_dgemm(c, a, b, m, n, k);
	if (dtype == dtype_i8) return launch_matmul_int<signed char>(c, a, b, m, n, k);
	if (dtype == dtype_i16) return launch_matmul_int<short>(c, a, b, m, n, k);
	if (dtype == dtype_i32) return launch_matmul_int<int>(c, a, b, m, n, k);
	return -1;
}

extern "C" int xhqm_cuda_reduce(void* dst, const void* src,
	std::size_t outer, std::size_t inner, int dtype, int op)
{
	if (dtype == dtype_f16) return launch_reduce<__half>(dst, src, outer, inner, op);
	if (dtype == dtype_f32) return launch_reduce<float>(dst, src, outer, inner, op);
	if (dtype == dtype_f64) return launch_reduce<double>(dst, src, outer, inner, op);
	if (dtype == dtype_i8) return launch_reduce<signed char>(dst, src, outer, inner, op);
	if (dtype == dtype_i16) return launch_reduce<short>(dst, src, outer, inner, op);
	if (dtype == dtype_i32) return launch_reduce<int>(dst, src, outer, inner, op);
	return -1;
}

extern "C" int xhqm_cuda_unary(void* dst, const void* a,
	std::size_t count, int dtype, int op)
{
	if (dtype == dtype_f16) return launch_unary_f16(dst, a, count, op);
	if (dtype == dtype_f32) return launch_unary<float>(dst, a, count, op);
	if (dtype == dtype_f64) return launch_unary<double>(dst, a, count, op);
	if (dtype == dtype_i8) return launch_unary_int<signed char>(dst, a, count, op);
	if (dtype == dtype_i16) return launch_unary_int<short>(dst, a, count, op);
	if (dtype == dtype_i32) return launch_unary_int<int>(dst, a, count, op);
	return -1;
}

extern "C" int xhqm_cuda_grad_accum(void* dst, const void* a, const void* b,
	std::size_t count, int dtype, int op)
{
	if (dtype == dtype_f16) return launch_grad_accum<__half>(dst, a, b, count, op);
	if (dtype == dtype_f32) return launch_grad_accum<float>(dst, a, b, count, op);
	if (dtype == dtype_f64) return launch_grad_accum<double>(dst, a, b, count, op);
	return -1;
}

extern "C" int xhqm_cuda_transpose(void* dst, const void* src,
	std::size_t rows, std::size_t cols, int dtype)
{
	if (dtype == dtype_f16) return launch_transpose<__half>(dst, src, rows, cols);
	if (dtype == dtype_f32) return launch_transpose<float>(dst, src, rows, cols);
	if (dtype == dtype_f64) return launch_transpose<double>(dst, src, rows, cols);
	if (dtype == dtype_i8) return launch_transpose<signed char>(dst, src, rows, cols);
	if (dtype == dtype_i16) return launch_transpose<short>(dst, src, rows, cols);
	if (dtype == dtype_i32) return launch_transpose<int>(dst, src, rows, cols);
	return -1;
}

extern "C" int xhqm_cuda_spmv(void* y, const void* x,
	const unsigned long long* row, const unsigned long long* col,
	const void* val, std::size_t nnz, std::size_t rows, std::size_t cols, int dtype)
{
	if (dtype == dtype_f16) return launch_spmv_f16(y, x, row, col, val, nnz, rows, cols);
	if (dtype == dtype_f32) return launch_spmv<float>(y, x, row, col, val, nnz, rows, cols);
	if (dtype == dtype_f64) return launch_spmv<double>(y, x, row, col, val, nnz, rows, cols);
	if (dtype == dtype_i8) return launch_spmv_small<signed char>(y, x, row, col, val, nnz, rows, cols);
	if (dtype == dtype_i16) return launch_spmv_small<short>(y, x, row, col, val, nnz, rows, cols);
	if (dtype == dtype_i32) return launch_spmv<int>(y, x, row, col, val, nnz, rows, cols);
	return -1;
}

extern "C" int xhqm_cuda_conv3d(void* dst, const void* src, const void* weight, const void* bias,
	std::size_t n, std::size_t cin, std::size_t cout,
	std::size_t id, std::size_t ih, std::size_t iw, std::size_t kd, std::size_t kh, std::size_t kw,
	std::size_t stride_d, std::size_t stride_h, std::size_t stride_w, std::size_t pad_d, std::size_t pad_h, std::size_t pad_w,
	std::size_t dil_d, std::size_t dil_h, std::size_t dil_w, std::size_t groups, int dtype)
{
	if (dtype == dtype_f16) return launch_conv3d<__half>(dst, src, weight, bias, n, cin, cout, id, ih, iw, kd, kh, kw, stride_d, stride_h, stride_w, pad_d, pad_h, pad_w, dil_d, dil_h, dil_w, groups);
	if (dtype == dtype_f32) return launch_conv3d<float>(dst, src, weight, bias, n, cin, cout, id, ih, iw, kd, kh, kw, stride_d, stride_h, stride_w, pad_d, pad_h, pad_w, dil_d, dil_h, dil_w, groups);
	if (dtype == dtype_f64) return launch_conv3d<double>(dst, src, weight, bias, n, cin, cout, id, ih, iw, kd, kh, kw, stride_d, stride_h, stride_w, pad_d, pad_h, pad_w, dil_d, dil_h, dil_w, groups);
	if (dtype == dtype_i8) return launch_conv3d<signed char>(dst, src, weight, bias, n, cin, cout, id, ih, iw, kd, kh, kw, stride_d, stride_h, stride_w, pad_d, pad_h, pad_w, dil_d, dil_h, dil_w, groups);
	if (dtype == dtype_i16) return launch_conv3d<short>(dst, src, weight, bias, n, cin, cout, id, ih, iw, kd, kh, kw, stride_d, stride_h, stride_w, pad_d, pad_h, pad_w, dil_d, dil_h, dil_w, groups);
	if (dtype == dtype_i32) return launch_conv3d<int>(dst, src, weight, bias, n, cin, cout, id, ih, iw, kd, kh, kw, stride_d, stride_h, stride_w, pad_d, pad_h, pad_w, dil_d, dil_h, dil_w, groups);
	return -1;
}

extern "C" int xhqm_cuda_conv4d(void* dst, const void* src, const void* weight, const void* bias,
	std::size_t n, std::size_t cin, std::size_t cout,
	std::size_t i0, std::size_t i1, std::size_t i2, std::size_t i3,
	std::size_t k0, std::size_t k1, std::size_t k2, std::size_t k3,
	std::size_t s0, std::size_t s1, std::size_t s2, std::size_t s3,
	std::size_t p0, std::size_t p1, std::size_t p2, std::size_t p3,
	std::size_t d0, std::size_t d1, std::size_t d2, std::size_t d3, std::size_t groups, int dtype)
{
	if (dtype == dtype_f32) return launch_conv4d<float>(dst, src, weight, bias, n, cin, cout, i0, i1, i2, i3, k0, k1, k2, k3, s0, s1, s2, s3, p0, p1, p2, p3, d0, d1, d2, d3, groups);
	if (dtype == dtype_f64) return launch_conv4d<double>(dst, src, weight, bias, n, cin, cout, i0, i1, i2, i3, k0, k1, k2, k3, s0, s1, s2, s3, p0, p1, p2, p3, d0, d1, d2, d3, groups);
	if (dtype == dtype_i8) return launch_conv4d<signed char>(dst, src, weight, bias, n, cin, cout, i0, i1, i2, i3, k0, k1, k2, k3, s0, s1, s2, s3, p0, p1, p2, p3, d0, d1, d2, d3, groups);
	if (dtype == dtype_i16) return launch_conv4d<short>(dst, src, weight, bias, n, cin, cout, i0, i1, i2, i3, k0, k1, k2, k3, s0, s1, s2, s3, p0, p1, p2, p3, d0, d1, d2, d3, groups);
	if (dtype == dtype_i32) return launch_conv4d<int>(dst, src, weight, bias, n, cin, cout, i0, i1, i2, i3, k0, k1, k2, k3, s0, s1, s2, s3, p0, p1, p2, p3, d0, d1, d2, d3, groups);
	return -1;
}

extern "C" int xhqm_cuda_trsv(void* x, const void* tri, const void* b, std::size_t n, int lower, int unit_diag, int dtype)
{
	if (dtype == dtype_f32) return launch_trsv<float>(x, tri, b, n, lower != 0, unit_diag != 0);
	if (dtype == dtype_f64) return launch_trsv<double>(x, tri, b, n, lower != 0, unit_diag != 0);
	if (dtype == dtype_i8) return launch_trsv_int<signed char>(x, tri, b, n, lower != 0, unit_diag != 0);
	if (dtype == dtype_i16) return launch_trsv_int<short>(x, tri, b, n, lower != 0, unit_diag != 0);
	if (dtype == dtype_i32) return launch_trsv_int<int>(x, tri, b, n, lower != 0, unit_diag != 0);
	return -1;
}

extern "C" int xhqm_cuda_chol(void* L, const void* A, std::size_t n, int dtype)
{
	if (dtype == dtype_f32) return launch_potrf<float>(L, A, n);
	if (dtype == dtype_f64) return launch_potrf<double>(L, A, n);
	return -1;
}
