// tensor_backend_metal.mm: Apple Silicon Metal GPU backend.
//
// 与 CUDA 后端（tensor_backend_gpu.cu）对应：逐元素/归约/矩阵乘/卷积等全部算子
// 由 Metal compute kernel 在 GPU 执行。支持 f16(half)/f32(float)/i8(char)/
// i16(short)/i32(int)；f64(double) 在 Apple Silicon Metal 上不受支持，返回 false
// 由调用方回退 CPU。
//
// 统一内存下 buffer 为 MTLResourceStorageModeShared，host 可直接读写 contents。
// 卷积/池化/上采样的参数以全 uint32 结构体传递，与 shader 内的 struct 布局一一对应。
//
// 注意：xhqm/head.h 在 namespace xhqm 内有 `using id = std::size_t`，会遮蔽
// Objective-C 的 id 类型；因此本文件把 Metal 相关实现全部放到文件顶层（匿名
// 命名空间），避开该遮蔽，仅 create_metal_backend() 挂在 xhqm::tensor_base 下。
//
// 编译为 Objective-C++（.mm）并开启 ARC。
//
// ── 接口约定（mac 节点扩展时看这里）────────────────────────────────────
// tensor_backend 的 15 个计算虚函数全部 override，统一挂到 run_* 助手：
//   elemwise/matmul/reduce/unary/transpose/grad_accum/spmv/conv3d（真实 kernel）
//   select/pool2d/upsample2d/dropout/conv4d/trsv/chol（kernel 源已内联，pipeline
//   按需构建）。run_* 统一模式：metal_dtype(dt) 拿索引 → 查 pipeline 成员 →
//   未构建（nil）或 dtype 不支持即 return false 回退 CPU。新增算子只需：
//   1) 写 MSL kernel（内联字符串或宏 XHQM_*），2) 在 build() 里 build(lib, 名字)，
//   3) 在 run_* 里查 pipeline 并 launch。无需改 tensor_backend.h 接口。
// dtype 覆盖现状（Apple 上待补）：f16/f32 全算子；f64 仅 elemwise/matmul/reduce；
// i8/i16/i32 仅 unary(neg/abs/sign)、conv3d、trsv、spmv；bool 无内核回退 CPU。
// 本次已加 unary_silu（op=42，见 XHQM_UNARY 宏 case 42）。

#import <Metal/Metal.h>

#include "tensor_backend_metal.h"
#include "tensor_backend.h"

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>

namespace
{
	// ---- Metal compute kernel 源码（运行时编译）----
	// 元素类型对应 tensor_backend.h 的 dtype：f16=1 f32=2 i8=5 i16=6 i32=7
	static const char* kShaderSrc = R"XHQM(
#include <metal_stdlib>
using namespace metal;

// ============================================================
// xhqm tensor Metal compute kernels (runtime-compiled).
// Element types: f16(half) f32(float) i8(char) i16(short) i32(int).
// f64/double is NOT supported by Metal on Apple Silicon -> CPU fallback.
// Reference semantics: tensor_backend_gpu.cu (CUDA backend).
// ============================================================

// ------------------------------------------------------------
// scalar helpers (compute in float, matching CUDA's f16/f32 kernels)
// ------------------------------------------------------------
constant float XHQM_PI = 3.14159265358979323846f;

inline float xhqm_hypot(float a, float b) { return sqrt(a * a + b * b); }
inline float xhqm_expm1(float x) { return exp(x) - 1.0f; }
inline float xhqm_log1p(float x) { return log(1.0f + x); }
inline float xhqm_cbrt(float x) { return copysign(pow(fabs(x), 1.0f / 3.0f), x); }

// erf (Abramowitz-Stegun 7.1.26, |err| ~ 1.5e-7)
inline float xhqm_erf(float x) {
    float s = (x < 0.0f) ? -1.0f : 1.0f;
    x = fabs(x);
    float t = 1.0f / (1.0f + 0.3275911f * x);
    float y = 1.0f - (((((1.061405429f * t - 1.453152027f) * t) + 1.421413741f) * t
        - 0.284496736f) * t + 0.254829592f) * t * exp(-x * x);
    return s * y;
}
inline float xhqm_erfc(float x) { return 1.0f - xhqm_erf(x); }

// log-gamma (Lanczos g=7 n=9; core requires x >= 0.5)
inline float xhqm_lgamma_core(float x) {
    const float cof[9] = {
        0.99999999999980993f, 676.5203681218851f, -1259.1392167224028f,
        771.32342877765313f, -176.61502916214059f, 12.507343278686905f,
        -0.13857109526572012f, 9.9843695780195716e-6f, 1.5056327351493116e-7f
    };
    float xx = x - 1.0f;
    float a = cof[0];
    float t = xx + 7.5f;
    for (int i = 1; i < 9; ++i) a += cof[i] / (xx + (float)i);
    return 0.9189385332046727f + (xx + 0.5f) * log(t) - t + log(a);
}
inline float xhqm_lgamma(float x) {
    if (x < 0.5f) {
        return 1.1447298858494002f - log(sin(XHQM_PI * x)) - xhqm_lgamma_core(1.0f - x);
    }
    return xhqm_lgamma_core(x);
}
inline float xhqm_tgamma(float x) { return exp(xhqm_lgamma(x)); }

// xorshift32 RNG (matches CPU rand_seed_at / xorshift32, reproducible)
inline uint xhqm_xorshift32(thread uint& s) {
    uint x = s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    s = x; return x;
}
inline uint xhqm_seed_at(uint seed, ulong i) {
    ulong h = (ulong)seed * 0x9E3779B97F4A7C15ull ^ (i + 0x9E3779B97F4A7C15ull);
    h ^= h >> 30; h *= 0xBF58476D1CE4E5B9ull;
    h ^= h >> 27; h *= 0x94D049BB133111EBull;
    h ^= h >> 31;
    uint s = (uint)h;
    return (s == 0) ? 0x9E3779B9u : s;
}
inline float xhqm_uniform01(thread uint& s) {
    return (float)(xhqm_xorshift32(s) >> 8) / (float)(1u << 24);
}

// float atomic add via CAS loop (Metal has no native float atomic)
inline float xhqm_atomic_add_f(device atomic_uint* addr, float v) {
    uint expected = atomic_load_explicit(addr, memory_order_relaxed);
    uint desired;
    do {
        desired = as_type<uint>(as_type<float>(expected) + v);
    } while (!atomic_compare_exchange_weak_explicit(addr, &expected, desired,
        memory_order_relaxed, memory_order_relaxed));
    return as_type<float>(expected);
}

// ------------------------------------------------------------
// parameter structs (all-uint layout, mirrors host structs)
// ------------------------------------------------------------
struct Pool2dParams { uint n, c, h, w, oh, ow, k, stride, pad; };
struct Upsample2dParams { uint n, c, h, w, oh, ow; };
struct Conv3dParams {
    uint n, cin, cout, id, ih, iw, kd, kh, kw, od, oh, ow;
    uint stride_d, stride_h, stride_w, pad_d, pad_h, pad_w, dil_d, dil_h, dil_w, groups;
};
struct Conv4dParams {
    uint n, cin, cout, i0, i1, i2, i3, k0, k1, k2, k3, o0, o1, o2, o3;
    uint s0, s1, s2, s3, p0, p1, p2, p3, d0, d1, d2, d3, groups;
};

// ------------------------------------------------------------
// float-kernel load/store adapters
// ------------------------------------------------------------
#define XHQM_LOAD_F16(v) ((float)(v))
#define XHQM_STORE_F16(v) ((half)(v))
#define XHQM_LOAD_F32(v) (v)
#define XHQM_STORE_F32(v) (v)
#define XHQM_LOAD_I(v) (v)
#define XHQM_STORE_I(v) (v)

// ------------------------------------------------------------
// elemwise: dst[i] = a[i] OP b[i]
// ------------------------------------------------------------
#define XHQM_ELEMWISE(STOR, COMP, SUFFIX, LOAD, STORE) \
kernel void xhqm_elemwise_##SUFFIX( \
    device STOR* dst [[buffer(0)]], \
    const device STOR* a [[buffer(1)]], \
    const device STOR* b [[buffer(2)]], \
    constant uint& n [[buffer(3)]], \
    constant int& op [[buffer(4)]], \
    uint idx [[thread_position_in_grid]]) { \
    if (idx >= n) return; \
    COMP x = LOAD(a[idx]); \
    COMP y = LOAD(b[idx]); \
    COMP r = x + y; \
    if (op == 1) r = x - y; \
    else if (op == 2) r = x * y; \
    else if (op == 3) r = x / y; \
    else if (op == 4) r = atan2(x, y); \
    else if (op == 5) r = xhqm_hypot(x, y); \
    else if (op == 6) r = pow(x, y); \
    else if (op == 7) r = x - floor(x / y) * y; \
    else if (op == 8) r = fmod(x, y); \
    dst[idx] = STORE(r); \
}
XHQM_ELEMWISE(half, float, f16, XHQM_LOAD_F16, XHQM_STORE_F16)
XHQM_ELEMWISE(float, float, f32, XHQM_LOAD_F32, XHQM_STORE_F32)

// integer elemwise: only + - * / (op 0..3)
#define XHQM_ELEMWISE_INT(STOR, SUFFIX) \
kernel void xhqm_elemwise_##SUFFIX( \
    device STOR* dst [[buffer(0)]], \
    const device STOR* a [[buffer(1)]], \
    const device STOR* b [[buffer(2)]], \
    constant uint& n [[buffer(3)]], \
    constant int& op [[buffer(4)]], \
    uint idx [[thread_position_in_grid]]) { \
    if (idx >= n) return; \
    if (op == 0) dst[idx] = a[idx] + b[idx]; \
    else if (op == 1) dst[idx] = a[idx] - b[idx]; \
    else if (op == 2) dst[idx] = a[idx] * b[idx]; \
    else dst[idx] = a[idx] / b[idx]; \
}
XHQM_ELEMWISE_INT(char, i8)
XHQM_ELEMWISE_INT(short, i16)
XHQM_ELEMWISE_INT(int, i32)

// ------------------------------------------------------------
// select: dst[i] = cond[i] ? a[i] : b[i]
// ------------------------------------------------------------
#define XHQM_SELECT(STOR, SUFFIX) \
kernel void xhqm_select_##SUFFIX( \
    device STOR* dst [[buffer(0)]], \
    const device uchar* cond [[buffer(1)]], \
    const device STOR* a [[buffer(2)]], \
    const device STOR* b [[buffer(3)]], \
    constant uint& n [[buffer(4)]], \
    uint idx [[thread_position_in_grid]]) { \
    if (idx >= n) return; \
    dst[idx] = cond[idx] ? a[idx] : b[idx]; \
}
XHQM_SELECT(half, f16)
XHQM_SELECT(float, f32)

// ------------------------------------------------------------
// pool2d (NCHW): op 0=max 1=avg
// ------------------------------------------------------------
#define XHQM_POOL2D(STOR, COMP, SUFFIX, LOAD, STORE) \
kernel void xhqm_pool2d_##SUFFIX( \
    device STOR* dst [[buffer(0)]], \
    const device STOR* src [[buffer(1)]], \
    constant Pool2dParams& p [[buffer(2)]], \
    constant int& op [[buffer(3)]], \
    uint idx [[thread_position_in_grid]]) { \
    uint total = p.n * p.c * p.oh * p.ow; \
    if (idx >= total) return; \
    uint oww = idx % p.ow; \
    uint ohh = (idx / p.ow) % p.oh; \
    uint cc = (idx / (p.ow * p.oh)) % p.c; \
    uint nn = idx / (p.ow * p.oh * p.c); \
    int h0 = (int)(ohh * p.stride) - (int)p.pad; \
    int w0 = (int)(oww * p.stride) - (int)p.pad; \
    if (op == 0) { \
        COMP acc = 0; \
        bool first = true; \
        for (uint kh = 0; kh < p.k; ++kh) { \
            for (uint kw = 0; kw < p.k; ++kw) { \
                int hh = h0 + (int)kh; \
                int ww = w0 + (int)kw; \
                if (hh < 0 || hh >= (int)p.h || ww < 0 || ww >= (int)p.w) continue; \
                COMP v = LOAD(src[((nn * p.c + cc) * p.h + (uint)hh) * p.w + (uint)ww]); \
                if (first) { acc = v; first = false; } \
                else if (v > acc) acc = v; \
            } \
        } \
        dst[idx] = STORE(acc); \
    } else { \
        COMP sum = 0; \
        uint cnt = 0; \
        for (uint kh = 0; kh < p.k; ++kh) { \
            for (uint kw = 0; kw < p.k; ++kw) { \
                int hh = h0 + (int)kh; \
                int ww = w0 + (int)kw; \
                if (hh < 0 || hh >= (int)p.h || ww < 0 || ww >= (int)p.w) continue; \
                sum += LOAD(src[((nn * p.c + cc) * p.h + (uint)hh) * p.w + (uint)ww]); \
                ++cnt; \
            } \
        } \
        dst[idx] = (cnt > 0) ? STORE(sum / (COMP)cnt) : STORE((COMP)0); \
    } \
}
XHQM_POOL2D(half, float, f16, XHQM_LOAD_F16, XHQM_STORE_F16)
XHQM_POOL2D(float, float, f32, XHQM_LOAD_F32, XHQM_STORE_F32)

// ------------------------------------------------------------
// upsample2d (NCHW): op 0=nearest 1=bilinear(align_corners=false)
// ------------------------------------------------------------
#define XHQM_UPSAMPLE2D(STOR, COMP, SUFFIX, LOAD, STORE) \
kernel void xhqm_upsample2d_##SUFFIX( \
    device STOR* dst [[buffer(0)]], \
    const device STOR* src [[buffer(1)]], \
    constant Upsample2dParams& p [[buffer(2)]], \
    constant int& op [[buffer(3)]], \
    uint idx [[thread_position_in_grid]]) { \
    uint total = p.n * p.c * p.oh * p.ow; \
    if (idx >= total) return; \
    uint oww = idx % p.ow; \
    uint ohh = (idx / p.ow) % p.oh; \
    uint cc = (idx / (p.ow * p.oh)) % p.c; \
    uint nn = idx / (p.ow * p.oh * p.c); \
    const device STOR* src_nc = src + (nn * p.c + cc) * p.h * p.w; \
    if (op == 0) { \
        uint ih = ohh * p.h / p.oh; \
        uint iw = oww * p.w / p.ow; \
        dst[idx] = src_nc[ih * p.w + iw]; \
    } else { \
        float scale_h = (float)p.h / (float)p.oh; \
        float scale_w = (float)p.w / (float)p.ow; \
        float sh = ((float)ohh + 0.5f) * scale_h - 0.5f; \
        float sw = ((float)oww + 0.5f) * scale_w - 0.5f; \
        sh = clamp(sh, 0.0f, (float)(p.h - 1)); \
        sw = clamp(sw, 0.0f, (float)(p.w - 1)); \
        uint h0 = (uint)sh; \
        uint w0 = (uint)sw; \
        uint h1 = (h0 + 1 < p.h) ? (h0 + 1) : h0; \
        uint w1 = (w0 + 1 < p.w) ? (w0 + 1) : w0; \
        float hw = sh - (float)h0; \
        float ww = sw - (float)w0; \
        float v = (1.0f - hw) * (1.0f - ww) * LOAD(src_nc[h0 * p.w + w0]) \
            + (1.0f - hw) * ww * LOAD(src_nc[h0 * p.w + w1]) \
            + hw * (1.0f - ww) * LOAD(src_nc[h1 * p.w + w0]) \
            + hw * ww * LOAD(src_nc[h1 * p.w + w1]); \
        dst[idx] = STORE(v); \
    } \
}
XHQM_UPSAMPLE2D(half, float, f16, XHQM_LOAD_F16, XHQM_STORE_F16)
XHQM_UPSAMPLE2D(float, float, f32, XHQM_LOAD_F32, XHQM_STORE_F32)

// ------------------------------------------------------------
// dropout (inverted): dst[i] = src[i] * scale or 0
// ------------------------------------------------------------
#define XHQM_DROPOUT(STOR, COMP, SUFFIX, LOAD, STORE) \
kernel void xhqm_dropout_##SUFFIX( \
    device STOR* dst [[buffer(0)]], \
    const device STOR* src [[buffer(1)]], \
    constant uint& n [[buffer(2)]], \
    constant uint& seed [[buffer(3)]], \
    constant float& p [[buffer(4)]], \
    uint idx [[thread_position_in_grid]]) { \
    if (idx >= n) return; \
    uint s = xhqm_seed_at(seed, (ulong)idx); \
    COMP keep = 1.0f - p; \
    COMP scale = 1.0f / keep; \
    if (xhqm_uniform01(s) >= p) \
        dst[idx] = STORE(LOAD(src[idx]) * scale); \
    else \
        dst[idx] = STORE((COMP)0); \
}
XHQM_DROPOUT(half, float, f16, XHQM_LOAD_F16, XHQM_STORE_F16)
XHQM_DROPOUT(float, float, f32, XHQM_LOAD_F32, XHQM_STORE_F32)

// ------------------------------------------------------------
// matmul: C[m x n] = A[m x k] x B[k x n] (row-major)
// ------------------------------------------------------------
#define XHQM_MATMUL(STOR, COMP, SUFFIX, LOAD, STORE) \
kernel void xhqm_matmul_##SUFFIX( \
    device STOR* C [[buffer(0)]], \
    const device STOR* A [[buffer(1)]], \
    const device STOR* B [[buffer(2)]], \
    constant uint& m [[buffer(3)]], \
    constant uint& n [[buffer(4)]], \
    constant uint& k [[buffer(5)]], \
    uint2 gid [[thread_position_in_grid]]) { \
    uint row = gid.y; \
    uint col = gid.x; \
    if (row >= m || col >= n) return; \
    COMP acc = 0; \
    for (uint p = 0; p < k; ++p) \
        acc += LOAD(A[row * k + p]) * LOAD(B[p * n + col]); \
    C[row * n + col] = STORE(acc); \
}
XHQM_MATMUL(half, float, f16, XHQM_LOAD_F16, XHQM_STORE_F16)
XHQM_MATMUL(float, float, f32, XHQM_LOAD_F32, XHQM_STORE_F32)
XHQM_MATMUL(char, char, i8, XHQM_LOAD_I, XHQM_STORE_I)
XHQM_MATMUL(short, short, i16, XHQM_LOAD_I, XHQM_STORE_I)
XHQM_MATMUL(int, int, i32, XHQM_LOAD_I, XHQM_STORE_I)

// ------------------------------------------------------------
// reduce along last dim: src[outer x inner] -> dst[outer]
// one threadgroup per row, threadgroup-shared reduction
// ------------------------------------------------------------
#define XHQM_REDUCE(STOR, COMP, SUFFIX, LOAD, STORE) \
kernel void xhqm_reduce_##SUFFIX( \
    device STOR* dst [[buffer(0)]], \
    const device STOR* src [[buffer(1)]], \
    constant uint& inner [[buffer(2)]], \
    constant int& op [[buffer(3)]], \
    uint row [[threadgroup_position_in_grid]], \
    uint tid [[thread_position_in_threadgroup]], \
    uint bsz [[threads_per_threadgroup]]) { \
    threadgroup COMP sdata[256]; \
    const device STOR* s = src + row * inner; \
    COMP val = (op == 0) ? (COMP)0 : ((op == 3) ? (COMP)1 : LOAD(s[0])); \
    for (uint i = tid; i < inner; i += bsz) { \
        COMP si = LOAD(s[i]); \
        if (op == 0) val += si; \
        else if (op == 1) val = (si > val) ? si : val; \
        else if (op == 2) val = (si < val) ? si : val; \
        else val *= si; \
    } \
    sdata[tid] = val; \
    threadgroup_barrier(mem_flags::mem_threadgroup); \
    for (uint stride = bsz / 2; stride > 0; stride >>= 1) { \
        if (tid < stride) { \
            if (op == 0) sdata[tid] += sdata[tid + stride]; \
            else if (op == 1) sdata[tid] = (sdata[tid + stride] > sdata[tid]) ? sdata[tid + stride] : sdata[tid]; \
            else if (op == 2) sdata[tid] = (sdata[tid + stride] < sdata[tid]) ? sdata[tid + stride] : sdata[tid]; \
            else sdata[tid] *= sdata[tid + stride]; \
        } \
        threadgroup_barrier(mem_flags::mem_threadgroup); \
    } \
    if (tid == 0) dst[row] = STORE(sdata[0]); \
}
XHQM_REDUCE(half, float, f16, XHQM_LOAD_F16, XHQM_STORE_F16)
XHQM_REDUCE(float, float, f32, XHQM_LOAD_F32, XHQM_STORE_F32)
XHQM_REDUCE(char, char, i8, XHQM_LOAD_I, XHQM_STORE_I)
XHQM_REDUCE(short, short, i16, XHQM_LOAD_I, XHQM_STORE_I)
XHQM_REDUCE(int, int, i32, XHQM_LOAD_I, XHQM_STORE_I)

// ------------------------------------------------------------
// unary: dst[i] = f(a[i]) (op 0..42，含 silu=42)
// ------------------------------------------------------------
#define XHQM_UNARY(STOR, COMP, SUFFIX, LOAD, STORE) \
kernel void xhqm_unary_##SUFFIX( \
    device STOR* dst [[buffer(0)]], \
    const device STOR* a [[buffer(1)]], \
    constant uint& n [[buffer(2)]], \
    constant int& op [[buffer(3)]], \
    uint idx [[thread_position_in_grid]]) { \
    if (idx >= n) return; \
    COMP v = LOAD(a[idx]); \
    COMP r = v; \
    if (op == 0) r = (v > 0) ? v : (COMP)0; \
    else if (op == 1) r = (v > 0) ? v : 0.01f * v; \
    else if (op == 2) r = 1.0f / (1.0f + exp(-v)); \
    else if (op == 3) { COMP c = 0.7978845608028654f; COMP t = tanh(c * (v + 0.044715f * v * v * v)); r = 0.5f * v * (1.0f + t); } \
    else if (op == 4) r = exp(v); \
    else if (op == 5) r = log(v); \
    else if (op == 6) r = tanh(v); \
    else if (op == 7) r = sqrt(v); \
    else if (op == 8) r = sin(v); \
    else if (op == 9) r = cos(v); \
    else if (op == 10) r = tan(v); \
    else if (op == 11) r = asin(v); \
    else if (op == 12) r = acos(v); \
    else if (op == 13) r = atan(v); \
    else if (op == 14) r = sinh(v); \
    else if (op == 15) r = cosh(v); \
    else if (op == 16) r = asinh(v); \
    else if (op == 17) r = acosh(v); \
    else if (op == 18) r = atanh(v); \
    else if (op == 19) r = log10(v); \
    else if (op == 20) r = log2(v); \
    else if (op == 21) r = floor(v); \
    else if (op == 22) r = ceil(v); \
    else if (op == 23) r = round(v); \
    else if (op == 24) r = trunc(v); \
    else if (op == 25) r = xhqm_expm1(v); \
    else if (op == 26) r = xhqm_log1p(v); \
    else if (op == 27) r = xhqm_erf(v); \
    else if (op == 28) r = xhqm_erfc(v); \
    else if (op == 29) r = xhqm_tgamma(v); \
    else if (op == 30) r = xhqm_lgamma(v); \
    else if (op == 31) r = fabs(v); \
    else if (op == 32) r = (v > 0) ? (COMP)1 : ((v < 0) ? (COMP)-1 : (COMP)0); \
    else if (op == 33) r = exp2(v); \
    else if (op == 34) r = xhqm_cbrt(v); \
    else if (op == 35) r = rsqrt(v); \
    else if (op == 36) r = 1.0f / v; \
    else if (op == 37) r = -v; \
    else if (op == 38) r = v * v; \
    else if (op == 39) r = isnan(v) ? (COMP)1 : (COMP)0; \
    else if (op == 40) r = isinf(v) ? (COMP)1 : (COMP)0; \
    else if (op == 41) r = isfinite(v) ? (COMP)1 : (COMP)0; \
    else if (op == 42) r = v / ((COMP)1 + exp(-v)); \
    dst[idx] = STORE(r); \
}
XHQM_UNARY(half, float, f16, XHQM_LOAD_F16, XHQM_STORE_F16)
XHQM_UNARY(float, float, f32, XHQM_LOAD_F32, XHQM_STORE_F32)

// integer unary: only neg(37) abs(31) sign(32)
#define XHQM_UNARY_INT(STOR, SUFFIX) \
kernel void xhqm_unary_##SUFFIX( \
    device STOR* dst [[buffer(0)]], \
    const device STOR* a [[buffer(1)]], \
    constant uint& n [[buffer(2)]], \
    constant int& op [[buffer(3)]], \
    uint idx [[thread_position_in_grid]]) { \
    if (idx >= n) return; \
    STOR v = a[idx]; \
    if (op == 37) dst[idx] = (STOR)-v; \
    else if (op == 31) dst[idx] = (v < 0) ? (STOR)-v : v; \
    else if (op == 32) dst[idx] = (v > 0) ? (STOR)1 : ((v < 0) ? (STOR)-1 : (STOR)0); \
    else dst[idx] = v; \
}
XHQM_UNARY_INT(char, i8)
XHQM_UNARY_INT(short, i16)
XHQM_UNARY_INT(int, i32)

// ------------------------------------------------------------
// grad_accum: dst[i] += f(a[i], b[i])
// ------------------------------------------------------------
#define XHQM_GRAD(STOR, COMP, SUFFIX, LOAD, STORE) \
kernel void xhqm_grad_accum_##SUFFIX( \
    device STOR* dst [[buffer(0)]], \
    const device STOR* a [[buffer(1)]], \
    const device STOR* b [[buffer(2)]], \
    constant uint& n [[buffer(3)]], \
    constant int& op [[buffer(4)]], \
    uint idx [[thread_position_in_grid]]) { \
    if (idx >= n) return; \
    COMP ai = LOAD(a[idx]); \
    COMP bi = LOAD(b[idx]); \
    COMP g = 0; \
    if (op == 0) g = (bi > 0) ? ai : (COMP)0; \
    else if (op == 1) g = (bi > 0) ? ai : 0.01f * ai; \
    else g = ai * bi * (1.0f - bi); \
    dst[idx] = STORE(LOAD(dst[idx]) + g); \
}
XHQM_GRAD(half, float, f16, XHQM_LOAD_F16, XHQM_STORE_F16)
XHQM_GRAD(float, float, f32, XHQM_LOAD_F32, XHQM_STORE_F32)

// ------------------------------------------------------------
// transpose: dst[cols x rows] = src[rows x cols]^T
// ------------------------------------------------------------
#define XHQM_TRANSPOSE(STOR, SUFFIX) \
kernel void xhqm_transpose_##SUFFIX( \
    device STOR* dst [[buffer(0)]], \
    const device STOR* src [[buffer(1)]], \
    constant uint& rows [[buffer(2)]], \
    constant uint& cols [[buffer(3)]], \
    uint idx [[thread_position_in_grid]]) { \
    if (idx >= rows * cols) return; \
    uint i = idx / cols; \
    uint j = idx % cols; \
    dst[j * rows + i] = src[i * cols + j]; \
}
XHQM_TRANSPOSE(half, f16)
XHQM_TRANSPOSE(float, f32)
XHQM_TRANSPOSE(char, i8)
XHQM_TRANSPOSE(short, i16)
XHQM_TRANSPOSE(int, i32)

// ------------------------------------------------------------
// spmv (COO): y[row[k]] += val[k] * x[col[k]]
// ------------------------------------------------------------
kernel void xhqm_spmv_f32( \
    device float* y [[buffer(0)]], \
    const device float* x [[buffer(1)]], \
    const device ulong* row [[buffer(2)]], \
    const device ulong* col [[buffer(3)]], \
    const device float* val [[buffer(4)]], \
    constant uint& nnz [[buffer(5)]], \
    uint k [[thread_position_in_grid]]) { \
    if (k >= nnz) return; \
    device atomic_uint* ay = (device atomic_uint*)y; \
    xhqm_atomic_add_f(ay + row[k], val[k] * x[col[k]]); \
}

kernel void xhqm_spmv_f16_acc( \
    device float* yacc [[buffer(0)]], \
    const device half* x [[buffer(1)]], \
    const device ulong* row [[buffer(2)]], \
    const device ulong* col [[buffer(3)]], \
    const device half* val [[buffer(4)]], \
    constant uint& nnz [[buffer(5)]], \
    uint k [[thread_position_in_grid]]) { \
    if (k >= nnz) return; \
    device atomic_uint* ay = (device atomic_uint*)yacc; \
    xhqm_atomic_add_f(ay + row[k], (float)val[k] * (float)x[col[k]]); \
}

kernel void xhqm_spmv_narrow_f16( \
    device half* y [[buffer(0)]], \
    const device float* yacc [[buffer(1)]], \
    constant uint& n [[buffer(2)]], \
    uint i [[thread_position_in_grid]]) { \
    if (i >= n) return; \
    y[i] = (half)yacc[i]; \
}

kernel void xhqm_spmv_i32( \
    device int* y [[buffer(0)]], \
    const device int* x [[buffer(1)]], \
    const device ulong* row [[buffer(2)]], \
    const device ulong* col [[buffer(3)]], \
    const device int* val [[buffer(4)]], \
    constant uint& nnz [[buffer(5)]], \
    uint k [[thread_position_in_grid]]) { \
    if (k >= nnz) return; \
    atomic_fetch_add_explicit((device atomic_int*)y + row[k], val[k] * x[col[k]], memory_order_relaxed); \
}

#define XHQM_SPMV_SMALL(STOR, SUFFIX) \
kernel void xhqm_spmv_small_##SUFFIX( \
    device int* yacc [[buffer(0)]], \
    const device STOR* x [[buffer(1)]], \
    const device ulong* row [[buffer(2)]], \
    const device ulong* col [[buffer(3)]], \
    const device STOR* val [[buffer(4)]], \
    constant uint& nnz [[buffer(5)]], \
    uint k [[thread_position_in_grid]]) { \
    if (k >= nnz) return; \
    atomic_fetch_add_explicit((device atomic_int*)yacc + row[k], (int)(val[k] * x[col[k]]), memory_order_relaxed); \
}
#define XHQM_SPMV_NARROW(STOR, SUFFIX) \
kernel void xhqm_spmv_narrow_##SUFFIX( \
    device STOR* y [[buffer(0)]], \
    const device int* yacc [[buffer(1)]], \
    constant uint& n [[buffer(2)]], \
    uint i [[thread_position_in_grid]]) { \
    if (i >= n) return; \
    y[i] = (STOR)yacc[i]; \
}
XHQM_SPMV_SMALL(char, i8)
XHQM_SPMV_SMALL(short, i16)
XHQM_SPMV_NARROW(char, i8)
XHQM_SPMV_NARROW(short, i16)

// ------------------------------------------------------------
// conv3d (NCDHW, groups/bias/stride/padding/dilation)
// ------------------------------------------------------------
#define XHQM_CONV3D(STOR, COMP, SUFFIX, LOAD, STORE) \
kernel void xhqm_conv3d_##SUFFIX( \
    device STOR* dst [[buffer(0)]], \
    const device STOR* src [[buffer(1)]], \
    const device STOR* weight [[buffer(2)]], \
    const device STOR* bias [[buffer(3)]], \
    constant Conv3dParams& p [[buffer(4)]], \
    uint idx [[thread_position_in_grid]]) { \
    uint total = p.n * p.cout * p.od * p.oh * p.ow; \
    if (idx >= total) return; \
    uint oww = idx % p.ow; \
    uint ohh = (idx / p.ow) % p.oh; \
    uint odd = (idx / (p.ow * p.oh)) % p.od; \
    uint co = (idx / (p.ow * p.oh * p.od)) % p.cout; \
    uint nn = idx / (p.ow * p.oh * p.od * p.cout); \
    uint cin_g = p.cin / p.groups; \
    uint cout_g = p.cout / p.groups; \
    uint g = co / cout_g; \
    uint cin_base = g * cin_g; \
    COMP acc = LOAD(bias[co]); \
    for (uint ci = 0; ci < cin_g; ++ci) { \
        uint c = cin_base + ci; \
        const device STOR* src_c = src + (nn * p.cin + c) * p.id * p.ih * p.iw; \
        const device STOR* w_c = weight + (co * cin_g + ci) * p.kd * p.kh * p.kw; \
        for (uint k0 = 0; k0 < p.kd; ++k0) { \
            int ii = (int)(odd * p.stride_d) - (int)p.pad_d + (int)p.dil_d * (int)k0; \
            if (ii < 0 || ii >= (int)p.id) continue; \
            for (uint k1 = 0; k1 < p.kh; ++k1) { \
                int jj = (int)(ohh * p.stride_h) - (int)p.pad_h + (int)p.dil_h * (int)k1; \
                if (jj < 0 || jj >= (int)p.ih) continue; \
                for (uint k2 = 0; k2 < p.kw; ++k2) { \
                    int kk = (int)(oww * p.stride_w) - (int)p.pad_w + (int)p.dil_w * (int)k2; \
                    if (kk < 0 || kk >= (int)p.iw) continue; \
                    acc += LOAD(src_c[(ii * (int)p.ih + jj) * (int)p.iw + kk]) * LOAD(w_c[(k0 * p.kh + k1) * p.kw + k2]); \
                } \
            } \
        } \
    } \
    dst[idx] = STORE(acc); \
}
XHQM_CONV3D(half, float, f16, XHQM_LOAD_F16, XHQM_STORE_F16)
XHQM_CONV3D(float, float, f32, XHQM_LOAD_F32, XHQM_STORE_F32)
XHQM_CONV3D(char, char, i8, XHQM_LOAD_I, XHQM_STORE_I)
XHQM_CONV3D(short, short, i16, XHQM_LOAD_I, XHQM_STORE_I)
XHQM_CONV3D(int, int, i32, XHQM_LOAD_I, XHQM_STORE_I)

// ------------------------------------------------------------
// conv4d (N,C,D0,D1,D2,D3, groups/bias)
// ------------------------------------------------------------
#define XHQM_CONV4D(STOR, COMP, SUFFIX, LOAD, STORE) \
kernel void xhqm_conv4d_##SUFFIX( \
    device STOR* dst [[buffer(0)]], \
    const device STOR* src [[buffer(1)]], \
    const device STOR* weight [[buffer(2)]], \
    const device STOR* bias [[buffer(3)]], \
    constant Conv4dParams& p [[buffer(4)]], \
    uint idx [[thread_position_in_grid]]) { \
    uint total = p.n * p.cout * p.o0 * p.o1 * p.o2 * p.o3; \
    if (idx >= total) return; \
    uint o3i = idx % p.o3; \
    uint o2i = (idx / p.o3) % p.o2; \
    uint o1i = (idx / (p.o3 * p.o2)) % p.o1; \
    uint o0i = (idx / (p.o3 * p.o2 * p.o1)) % p.o0; \
    uint co = (idx / (p.o3 * p.o2 * p.o1 * p.o0)) % p.cout; \
    uint nn = idx / (p.o3 * p.o2 * p.o1 * p.o0 * p.cout); \
    uint cin_g = p.cin / p.groups; \
    uint cout_g = p.cout / p.groups; \
    uint g = co / cout_g; \
    uint cin_base = g * cin_g; \
    COMP acc = LOAD(bias[co]); \
    for (uint ci = 0; ci < cin_g; ++ci) { \
        uint c = cin_base + ci; \
        const device STOR* src_c = src + (nn * p.cin + c) * p.i0 * p.i1 * p.i2 * p.i3; \
        const device STOR* w_c = weight + (co * cin_g + ci) * p.k0 * p.k1 * p.k2 * p.k3; \
        for (uint k0i = 0; k0i < p.k0; ++k0i) { \
            int ii0 = (int)(o0i * p.s0) - (int)p.p0 + (int)p.d0 * (int)k0i; \
            if (ii0 < 0 || ii0 >= (int)p.i0) continue; \
            for (uint k1i = 0; k1i < p.k1; ++k1i) { \
                int ii1 = (int)(o1i * p.s1) - (int)p.p1 + (int)p.d1 * (int)k1i; \
                if (ii1 < 0 || ii1 >= (int)p.i1) continue; \
                for (uint k2i = 0; k2i < p.k2; ++k2i) { \
                    int ii2 = (int)(o2i * p.s2) - (int)p.p2 + (int)p.d2 * (int)k2i; \
                    if (ii2 < 0 || ii2 >= (int)p.i2) continue; \
                    for (uint k3i = 0; k3i < p.k3; ++k3i) { \
                        int ii3 = (int)(o3i * p.s3) - (int)p.p3 + (int)p.d3 * (int)k3i; \
                        if (ii3 < 0 || ii3 >= (int)p.i3) continue; \
                        acc += LOAD(src_c[((ii0 * (int)p.i1 + ii1) * (int)p.i2 + ii2) * (int)p.i3 + ii3]) \
                            * LOAD(w_c[((k0i * p.k1 + k1i) * p.k2 + k2i) * p.k3 + k3i]); \
                    } \
                } \
            } \
        } \
    } \
    dst[idx] = STORE(acc); \
}
XHQM_CONV4D(half, float, f16, XHQM_LOAD_F16, XHQM_STORE_F16)
XHQM_CONV4D(float, float, f32, XHQM_LOAD_F32, XHQM_STORE_F32)
XHQM_CONV4D(char, char, i8, XHQM_LOAD_I, XHQM_STORE_I)
XHQM_CONV4D(short, short, i16, XHQM_LOAD_I, XHQM_STORE_I)
XHQM_CONV4D(int, int, i32, XHQM_LOAD_I, XHQM_STORE_I)

// ------------------------------------------------------------
// trsv (single-threaded; triangular solve is inherently sequential)
// x = solve(tri, b); lower ? lower-tri : upper-tri; unit_diag ? diag==1
// ------------------------------------------------------------
#define XHQM_TRSV(STOR, COMP, SUFFIX, LOAD, STORE) \
kernel void xhqm_trsv_##SUFFIX( \
    device STOR* x [[buffer(0)]], \
    const device STOR* tri [[buffer(1)]], \
    constant uint& n [[buffer(2)]], \
    constant int& lower [[buffer(3)]], \
    constant int& unit_diag [[buffer(4)]], \
    uint tid [[thread_position_in_grid]]) { \
    if (tid != 0) return; \
    if (lower) { \
        for (uint k = 0; k < n; ++k) { \
            if (!unit_diag) x[k] = STORE(LOAD(x[k]) / LOAD(tri[k * n + k])); \
            COMP xk = LOAD(x[k]); \
            for (uint i = k + 1; i < n; ++i) \
                x[i] = STORE(LOAD(x[i]) - LOAD(tri[i * n + k]) * xk); \
        } \
    } else { \
        for (uint k = n; k-- > 0;) { \
            if (!unit_diag) x[k] = STORE(LOAD(x[k]) / LOAD(tri[k * n + k])); \
            COMP xk = LOAD(x[k]); \
            for (uint i = 0; i < k; ++i) \
                x[i] = STORE(LOAD(x[i]) - LOAD(tri[i * n + k]) * xk); \
        } \
    } \
}
XHQM_TRSV(float, float, f32, XHQM_LOAD_F32, XHQM_STORE_F32)
XHQM_TRSV(char, char, i8, XHQM_LOAD_I, XHQM_STORE_I)
XHQM_TRSV(short, short, i16, XHQM_LOAD_I, XHQM_STORE_I)
XHQM_TRSV(int, int, i32, XHQM_LOAD_I, XHQM_STORE_I)

// ------------------------------------------------------------
// chol (single-threaded Cholesky; f32 only): A (n x n, row-major) -> L (lower, row-major)
// host pre-copies A into L buffer; kernel factorizes in place; host zeroes upper triangle.
// ------------------------------------------------------------
kernel void xhqm_chol_f32( \
    device float* L [[buffer(0)]], \
    constant uint& n [[buffer(1)]], \
    uint tid [[thread_position_in_grid]]) { \
    if (tid != 0) return; \
    for (uint j = 0; j < n; ++j) { \
        float s = L[j * n + j]; \
        for (uint k = 0; k < j; ++k) { \
            float ljk = L[j * n + k]; \
            s -= ljk * ljk; \
        } \
        L[j * n + j] = sqrt(s); \
        for (uint i = j + 1; i < n; ++i) { \
            float t = L[i * n + j]; \
            for (uint k = 0; k < j; ++k) \
                t -= L[i * n + k] * L[j * n + k]; \
            L[i * n + j] = t / L[j * n + j]; \
        } \
    } \
}
)XHQM";

	// ---- 与 shader 内 struct 布局一致（全 uint32，无填充）----
	struct pool2d_params { std::uint32_t n, c, h, w, oh, ow, k, stride, pad; };
	struct upsample2d_params { std::uint32_t n, c, h, w, oh, ow; };
	struct conv3d_params {
		std::uint32_t n, cin, cout, id, ih, iw, kd, kh, kw, od, oh, ow;
		std::uint32_t stride_d, stride_h, stride_w, pad_d, pad_h, pad_w, dil_d, dil_h, dil_w, groups;
	};
	struct conv4d_params {
		std::uint32_t n, cin, cout, i0, i1, i2, i3, k0, k1, k2, k3, o0, o1, o2, o3;
		std::uint32_t s0, s1, s2, s3, p0, p1, p2, p3, d0, d1, d2, d3, groups;
	};

	// ---- Metal 上下文单例：设备/队列/全部 pipeline ----
	struct metal_ctx
	{
		id<MTLDevice> device = nil;
		id<MTLCommandQueue> queue = nil;

		// pipeline 数组，下标即 metal dtype index：0=f16 1=f32 2=i8 3=i16 4=i32
		id<MTLComputePipelineState> p_elem[5] = {};
		id<MTLComputePipelineState> p_select[2] = {};
		id<MTLComputePipelineState> p_pool[2] = {};
		id<MTLComputePipelineState> p_ups[2] = {};
		id<MTLComputePipelineState> p_drop[2] = {};
		id<MTLComputePipelineState> p_mat[5] = {};
		id<MTLComputePipelineState> p_red[5] = {};
		id<MTLComputePipelineState> p_un[5] = {};
		id<MTLComputePipelineState> p_gr[2] = {};
		id<MTLComputePipelineState> p_tr[5] = {};
		id<MTLComputePipelineState> p_conv3d[5] = {};
		id<MTLComputePipelineState> p_conv4d[5] = {};
		id<MTLComputePipelineState> p_trsv[4] = {};   // f32/i8/i16/i32
		id<MTLComputePipelineState> p_chol = nil;     // f32

		// spmv 专用
		id<MTLComputePipelineState> p_spmv_f32 = nil;
		id<MTLComputePipelineState> p_spmv_f16_acc = nil;
		id<MTLComputePipelineState> p_spmv_f16_narrow = nil;
		id<MTLComputePipelineState> p_spmv_i32 = nil;
		id<MTLComputePipelineState> p_spmv_small[2] = {};   // i8/i16 累加
		id<MTLComputePipelineState> p_spmv_narrow[2] = {};  // i8/i16 窄化

		bool ok = false;

		static constexpr const char* kElem[5] = { "xhqm_elemwise_f16", "xhqm_elemwise_f32", "xhqm_elemwise_i8", "xhqm_elemwise_i16", "xhqm_elemwise_i32" };
		static constexpr const char* kSel[2] = { "xhqm_select_f16", "xhqm_select_f32" };
		static constexpr const char* kPool[2] = { "xhqm_pool2d_f16", "xhqm_pool2d_f32" };
		static constexpr const char* kUps[2] = { "xhqm_upsample2d_f16", "xhqm_upsample2d_f32" };
		static constexpr const char* kDrop[2] = { "xhqm_dropout_f16", "xhqm_dropout_f32" };
		static constexpr const char* kMat[5] = { "xhqm_matmul_f16", "xhqm_matmul_f32", "xhqm_matmul_i8", "xhqm_matmul_i16", "xhqm_matmul_i32" };
		static constexpr const char* kRed[5] = { "xhqm_reduce_f16", "xhqm_reduce_f32", "xhqm_reduce_i8", "xhqm_reduce_i16", "xhqm_reduce_i32" };
		static constexpr const char* kUn[5] = { "xhqm_unary_f16", "xhqm_unary_f32", "xhqm_unary_i8", "xhqm_unary_i16", "xhqm_unary_i32" };
		static constexpr const char* kGr[2] = { "xhqm_grad_accum_f16", "xhqm_grad_accum_f32" };
		static constexpr const char* kTr[5] = { "xhqm_transpose_f16", "xhqm_transpose_f32", "xhqm_transpose_i8", "xhqm_transpose_i16", "xhqm_transpose_i32" };
		static constexpr const char* kC3[5] = { "xhqm_conv3d_f16", "xhqm_conv3d_f32", "xhqm_conv3d_i8", "xhqm_conv3d_i16", "xhqm_conv3d_i32" };
		static constexpr const char* kC4[5] = { "xhqm_conv4d_f16", "xhqm_conv4d_f32", "xhqm_conv4d_i8", "xhqm_conv4d_i16", "xhqm_conv4d_i32" };
		static constexpr const char* kTrsv[4] = { "xhqm_trsv_f32", "xhqm_trsv_i8", "xhqm_trsv_i16", "xhqm_trsv_i32" };
		static constexpr const char* kChol = "xhqm_chol_f32";
		static constexpr const char* kSmall[2] = { "xhqm_spmv_small_i8", "xhqm_spmv_small_i16" };
		static constexpr const char* kNarrow[2] = { "xhqm_spmv_narrow_i8", "xhqm_spmv_narrow_i16" };

		id<MTLComputePipelineState> build(id<MTLLibrary> lib, const char* name)
		{
			id<MTLFunction> fn = [lib newFunctionWithName:[NSString stringWithUTF8String:name]];
			if (!fn) { std::fprintf(stderr, "[metal] missing kernel: %s\n", name); return nil; }
			NSError* err = nil;
			id<MTLComputePipelineState> p = [device newComputePipelineStateWithFunction:fn error:&err];
			if (!p) std::fprintf(stderr, "[metal] pipeline '%s' failed: %s\n", name, [err.localizedDescription UTF8String]);
			return p;
		}

		metal_ctx()
		{
			device = MTLCreateSystemDefaultDevice();
			if (!device) return;
			queue = [device newCommandQueue];
			if (!queue) return;
			NSError* err = nil;
			NSString* src = [NSString stringWithUTF8String:kShaderSrc];
			id<MTLLibrary> lib = [device newLibraryWithSource:src options:nil error:&err];
			if (!lib) { std::fprintf(stderr, "[metal] shader compile failed: %s\n", [err.localizedDescription UTF8String]); return; }

			for (int i = 0; i < 5; ++i)
			{
				p_elem[i] = build(lib, kElem[i]);
				p_mat[i] = build(lib, kMat[i]);
				p_red[i] = build(lib, kRed[i]);
				p_un[i] = build(lib, kUn[i]);
				p_tr[i] = build(lib, kTr[i]);
				p_conv3d[i] = build(lib, kC3[i]);
				p_conv4d[i] = build(lib, kC4[i]);
			}
			for (int i = 0; i < 2; ++i)
			{
				p_select[i] = build(lib, kSel[i]);
				p_pool[i] = build(lib, kPool[i]);
				p_ups[i] = build(lib, kUps[i]);
				p_drop[i] = build(lib, kDrop[i]);
				p_gr[i] = build(lib, kGr[i]);
				p_spmv_small[i] = build(lib, kSmall[i]);
				p_spmv_narrow[i] = build(lib, kNarrow[i]);
			}
			for (int i = 0; i < 4; ++i) p_trsv[i] = build(lib, kTrsv[i]);
			p_chol = build(lib, kChol);

			p_spmv_f32 = build(lib, "xhqm_spmv_f32");
			p_spmv_f16_acc = build(lib, "xhqm_spmv_f16_acc");
			p_spmv_f16_narrow = build(lib, "xhqm_spmv_narrow_f16");
			p_spmv_i32 = build(lib, "xhqm_spmv_i32");

			ok = true;
		}
	};

	metal_ctx& g()
	{
		static metal_ctx c;
		return c;
	}

	// dtype 枚举 -> metal dtype index（0=f16 1=f32 2=i8 3=i16 4=i32；-1 不支持）
	int metal_dtype(xhqm::tensor_base::dtype dt)
	{
		using xhqm::tensor_base::dtype;
		switch (dt)
		{
		case dtype::f16: return 0;
		case dtype::f32: return 1;
		case dtype::i8: return 2;
		case dtype::i16: return 3;
		case dtype::i32: return 4;
		default: return -1;
		}
	}
	std::size_t metal_esize(int idx)
	{
		switch (idx)
		{
		case 0: return 2;  // f16
		case 1: return 4;  // f32
		case 2: return 1;  // i8
		case 3: return 2;  // i16
		case 4: return 4;  // i32
		default: return 0;
		}
	}

	// 通用单次 compute 分发：设 buffers、dispatch、commit、wait
	void launch(id<MTLCommandQueue> q, id<MTLComputePipelineState> p,
		const std::vector<id<MTLBuffer>>& bufs, MTLSize grid, MTLSize group)
	{
		id<MTLCommandBuffer> cb = [q commandBuffer];
		id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
		[enc setComputePipelineState:p];
		for (std::size_t i = 0; i < bufs.size(); ++i)
			[enc setBuffer:bufs[i] offset:0 atIndex:(int)i];
		[enc dispatchThreads:grid threadsPerThreadgroup:group];
		[enc endEncoding];
		[cb commit];
		[cb waitUntilCompleted];
	}

	// 一元逐元素（f16/f32 全算子；i8/i16/i32 仅 neg/abs/sign）
	bool run_unary(void* dst, const void* a, std::size_t count, int idx, int op)
	{
		metal_ctx& c = g();
		if (!c.ok || idx < 0 || !c.p_un[idx]) return false;
		if (count == 0) return true;
		// 整型仅支持 op 31/32/37，其余回退 CPU（与 CUDA 一致）
		if (idx >= 2 && op != 31 && op != 32 && op != 37) return false;
		const std::size_t bytes = count * metal_esize(idx);
		uint n = (uint)count;
		std::vector<id<MTLBuffer>> bufs = {
			[c.device newBufferWithLength:bytes options:MTLResourceStorageModeShared],
			[c.device newBufferWithBytes:a length:bytes options:MTLResourceStorageModeShared],
			[c.device newBufferWithBytes:&n length:sizeof(uint) options:MTLResourceStorageModeShared],
			[c.device newBufferWithBytes:&op length:sizeof(int) options:MTLResourceStorageModeShared],
		};
		launch(c.queue, c.p_un[idx], bufs, MTLSizeMake(count, 1, 1), MTLSizeMake(256, 1, 1));
		std::memcpy(dst, [bufs[0] contents], bytes);
		return true;
	}

	// 逐元素二元（f16/f32 全算子；i8/i16/i32 仅 + - * /）
	bool run_elemwise(void* dst, const void* a, const void* b, std::size_t count, int idx, int op)
	{
		metal_ctx& c = g();
		if (!c.ok || idx < 0 || !c.p_elem[idx]) return false;
		if (count == 0) return true;
		if (idx >= 2 && (op < 0 || op > 3)) return false;
		const std::size_t bytes = count * metal_esize(idx);
		uint n = (uint)count;
		std::vector<id<MTLBuffer>> bufs = {
			[c.device newBufferWithLength:bytes options:MTLResourceStorageModeShared],
			[c.device newBufferWithBytes:a length:bytes options:MTLResourceStorageModeShared],
			[c.device newBufferWithBytes:b length:bytes options:MTLResourceStorageModeShared],
			[c.device newBufferWithBytes:&n length:sizeof(uint) options:MTLResourceStorageModeShared],
			[c.device newBufferWithBytes:&op length:sizeof(int) options:MTLResourceStorageModeShared],
		};
		launch(c.queue, c.p_elem[idx], bufs, MTLSizeMake(count, 1, 1), MTLSizeMake(256, 1, 1));
		std::memcpy(dst, [bufs[0] contents], bytes);
		return true;
	}

	// 条件选择（仅 f16/f32，与 CUDA 一致）
	bool run_select(void* dst, const void* cond, const void* a, const void* b, std::size_t count, int idx)
	{
		metal_ctx& c = g();
		if (idx < 0 || idx > 1) return false;
		if (!c.ok || !c.p_select[idx]) return false;
		if (count == 0) return true;
		const std::size_t bytes = count * metal_esize(idx);
		uint n = (uint)count;
		std::vector<id<MTLBuffer>> bufs = {
			[c.device newBufferWithLength:bytes options:MTLResourceStorageModeShared],
			[c.device newBufferWithBytes:cond length:count options:MTLResourceStorageModeShared],
			[c.device newBufferWithBytes:a length:bytes options:MTLResourceStorageModeShared],
			[c.device newBufferWithBytes:b length:bytes options:MTLResourceStorageModeShared],
			[c.device newBufferWithBytes:&n length:sizeof(uint) options:MTLResourceStorageModeShared],
		};
		launch(c.queue, c.p_select[idx], bufs, MTLSizeMake(count, 1, 1), MTLSizeMake(256, 1, 1));
		std::memcpy(dst, [bufs[0] contents], bytes);
		return true;
	}

	// 矩阵乘 C[m×n]=A[m×k]×B[k×n]
	bool run_matmul(void* dst, const void* a, const void* b, std::size_t m, std::size_t n, std::size_t k, int idx)
	{
		metal_ctx& c = g();
		if (!c.ok || idx < 0 || !c.p_mat[idx]) return false;
		if (m == 0 || n == 0 || k == 0) return true;
		const std::size_t esz = metal_esize(idx);
		const std::size_t abytes = m * k * esz, bbytes = k * n * esz, cbytes = m * n * esz;
		uint um = (uint)m, un = (uint)n, uk = (uint)k;
		std::vector<id<MTLBuffer>> bufs = {
			[c.device newBufferWithLength:cbytes options:MTLResourceStorageModeShared],
			[c.device newBufferWithBytes:a length:abytes options:MTLResourceStorageModeShared],
			[c.device newBufferWithBytes:b length:bbytes options:MTLResourceStorageModeShared],
			[c.device newBufferWithBytes:&um length:sizeof(uint) options:MTLResourceStorageModeShared],
			[c.device newBufferWithBytes:&un length:sizeof(uint) options:MTLResourceStorageModeShared],
			[c.device newBufferWithBytes:&uk length:sizeof(uint) options:MTLResourceStorageModeShared],
		};
		launch(c.queue, c.p_mat[idx], bufs, MTLSizeMake(n, m, 1), MTLSizeMake(16, 16, 1));
		std::memcpy(dst, [bufs[0] contents], cbytes);
		return true;
	}

	// 沿最后一维归约 src[outer×inner]→dst[outer]
	bool run_reduce(void* dst, const void* src, std::size_t outer, std::size_t inner, int idx, int op)
	{
		metal_ctx& c = g();
		if (!c.ok || idx < 0 || !c.p_red[idx]) return false;
		if (outer == 0 || inner == 0) return true;
		const std::size_t esz = metal_esize(idx);
		const std::size_t in_bytes = outer * inner * esz, out_bytes = outer * esz;
		uint uin = (uint)inner;
		std::vector<id<MTLBuffer>> bufs = {
			[c.device newBufferWithLength:out_bytes options:MTLResourceStorageModeShared],
			[c.device newBufferWithBytes:src length:in_bytes options:MTLResourceStorageModeShared],
			[c.device newBufferWithBytes:&uin length:sizeof(uint) options:MTLResourceStorageModeShared],
			[c.device newBufferWithBytes:&op length:sizeof(int) options:MTLResourceStorageModeShared],
		};
		// 每行一个 threadgroup（256 线程），行号即 threadgroup index
		launch(c.queue, c.p_red[idx], bufs, MTLSizeMake(outer * 256, 1, 1), MTLSizeMake(256, 1, 1));
		std::memcpy(dst, [bufs[0] contents], out_bytes);
		return true;
	}

	// 转置 dst[cols×rows] = src[rows×cols]^T
	bool run_transpose(void* dst, const void* src, std::size_t rows, std::size_t cols, int idx)
	{
		metal_ctx& c = g();
		if (!c.ok || idx < 0 || !c.p_tr[idx]) return false;
		if (rows == 0 || cols == 0) return true;
		const std::size_t bytes = rows * cols * metal_esize(idx);
		uint ur = (uint)rows, uc = (uint)cols;
		std::vector<id<MTLBuffer>> bufs = {
			[c.device newBufferWithLength:bytes options:MTLResourceStorageModeShared],
			[c.device newBufferWithBytes:src length:bytes options:MTLResourceStorageModeShared],
			[c.device newBufferWithBytes:&ur length:sizeof(uint) options:MTLResourceStorageModeShared],
			[c.device newBufferWithBytes:&uc length:sizeof(uint) options:MTLResourceStorageModeShared],
		};
		launch(c.queue, c.p_tr[idx], bufs, MTLSizeMake(rows * cols, 1, 1), MTLSizeMake(256, 1, 1));
		std::memcpy(dst, [bufs[0] contents], bytes);
		return true;
	}

	// 反向逐元素累加（仅 f16/f32）
	bool run_grad_accum(void* dst, const void* a, const void* b, std::size_t count, int idx, int op)
	{
		metal_ctx& c = g();
		if (idx < 0 || idx > 1) return false;
		if (!c.ok || !c.p_gr[idx]) return false;
		if (count == 0) return true;
		const std::size_t bytes = count * metal_esize(idx);
		uint n = (uint)count;
		std::vector<id<MTLBuffer>> bufs = {
			[c.device newBufferWithBytes:dst length:bytes options:MTLResourceStorageModeShared],
			[c.device newBufferWithBytes:a length:bytes options:MTLResourceStorageModeShared],
			[c.device newBufferWithBytes:b length:bytes options:MTLResourceStorageModeShared],
			[c.device newBufferWithBytes:&n length:sizeof(uint) options:MTLResourceStorageModeShared],
			[c.device newBufferWithBytes:&op length:sizeof(int) options:MTLResourceStorageModeShared],
		};
		launch(c.queue, c.p_gr[idx], bufs, MTLSizeMake(count, 1, 1), MTLSizeMake(256, 1, 1));
		std::memcpy(dst, [bufs[0] contents], bytes);
		return true;
	}

	// 2D 池化（NCHW）
	bool run_pool2d(void* dst, const void* src, std::size_t n, std::size_t c, std::size_t h, std::size_t w,
		std::size_t oh, std::size_t ow, std::size_t k, std::size_t stride, std::size_t pad, int idx, int op)
	{
		metal_ctx& m = g();
		if (idx < 0 || idx > 1) return false;
		if (!m.ok || !m.p_pool[idx]) return false;
		const std::size_t total = n * c * oh * ow;
		if (total == 0) return true;
		const std::size_t esz = metal_esize(idx);
		const std::size_t src_bytes = n * c * h * w * esz, dst_bytes = total * esz;
		pool2d_params p = { (std::uint32_t)n, (std::uint32_t)c, (std::uint32_t)h, (std::uint32_t)w,
			(std::uint32_t)oh, (std::uint32_t)ow, (std::uint32_t)k, (std::uint32_t)stride, (std::uint32_t)pad };
		std::vector<id<MTLBuffer>> bufs = {
			[m.device newBufferWithLength:dst_bytes options:MTLResourceStorageModeShared],
			[m.device newBufferWithBytes:src length:src_bytes options:MTLResourceStorageModeShared],
			[m.device newBufferWithBytes:&p length:sizeof(pool2d_params) options:MTLResourceStorageModeShared],
			[m.device newBufferWithBytes:&op length:sizeof(int) options:MTLResourceStorageModeShared],
		};
		launch(m.queue, m.p_pool[idx], bufs, MTLSizeMake(total, 1, 1), MTLSizeMake(256, 1, 1));
		std::memcpy(dst, [bufs[0] contents], dst_bytes);
		return true;
	}

	// 2D 上采样（NCHW）
	bool run_upsample2d(void* dst, const void* src, std::size_t n, std::size_t c, std::size_t h, std::size_t w,
		std::size_t oh, std::size_t ow, int idx, int op)
	{
		metal_ctx& m = g();
		if (idx < 0 || idx > 1) return false;
		if (!m.ok || !m.p_ups[idx]) return false;
		const std::size_t total = n * c * oh * ow;
		if (total == 0) return true;
		const std::size_t esz = metal_esize(idx);
		const std::size_t src_bytes = n * c * h * w * esz, dst_bytes = total * esz;
		upsample2d_params p = { (std::uint32_t)n, (std::uint32_t)c, (std::uint32_t)h, (std::uint32_t)w,
			(std::uint32_t)oh, (std::uint32_t)ow };
		std::vector<id<MTLBuffer>> bufs = {
			[m.device newBufferWithLength:dst_bytes options:MTLResourceStorageModeShared],
			[m.device newBufferWithBytes:src length:src_bytes options:MTLResourceStorageModeShared],
			[m.device newBufferWithBytes:&p length:sizeof(upsample2d_params) options:MTLResourceStorageModeShared],
			[m.device newBufferWithBytes:&op length:sizeof(int) options:MTLResourceStorageModeShared],
		};
		launch(m.queue, m.p_ups[idx], bufs, MTLSizeMake(total, 1, 1), MTLSizeMake(256, 1, 1));
		std::memcpy(dst, [bufs[0] contents], dst_bytes);
		return true;
	}

	// dropout（inverted）
	bool run_dropout(void* dst, const void* src, std::size_t count, float p, unsigned int seed, int idx)
	{
		metal_ctx& m = g();
		if (idx < 0 || idx > 1) return false;
		if (!m.ok || !m.p_drop[idx]) return false;
		if (count == 0) return true;
		const std::size_t bytes = count * metal_esize(idx);
		uint n = (uint)count;
		std::vector<id<MTLBuffer>> bufs = {
			[m.device newBufferWithLength:bytes options:MTLResourceStorageModeShared],
			[m.device newBufferWithBytes:src length:bytes options:MTLResourceStorageModeShared],
			[m.device newBufferWithBytes:&n length:sizeof(uint) options:MTLResourceStorageModeShared],
			[m.device newBufferWithBytes:&seed length:sizeof(unsigned int) options:MTLResourceStorageModeShared],
			[m.device newBufferWithBytes:&p length:sizeof(float) options:MTLResourceStorageModeShared],
		};
		launch(m.queue, m.p_drop[idx], bufs, MTLSizeMake(count, 1, 1), MTLSizeMake(256, 1, 1));
		std::memcpy(dst, [bufs[0] contents], bytes);
		return true;
	}

	// 稀疏矩阵 × 稠密向量（COO）
	bool run_spmv(void* y, const void* x, const xhqm::index* row, const xhqm::index* col,
		const void* val, std::size_t nnz, std::size_t rows, std::size_t cols, int idx)
	{
		metal_ctx& m = g();
		if (!m.ok || idx < 0) return false;
		if (nnz == 0) return true;
		const std::size_t esz = metal_esize(idx);
		const std::size_t idx_bytes = nnz * sizeof(xhqm::index);
		const std::size_t x_bytes = cols * esz, val_bytes = nnz * esz;
		uint un = (uint)nnz;

		if (idx == 1)  // f32：单 kernel 原子累加
		{
			if (!m.p_spmv_f32) return false;
			const std::size_t y_bytes = rows * 4;
			id<MTLBuffer> by = [m.device newBufferWithLength:y_bytes options:MTLResourceStorageModeShared];
			std::memset([by contents], 0, y_bytes);
			std::vector<id<MTLBuffer>> bufs = {
				by,
				[m.device newBufferWithBytes:x length:x_bytes options:MTLResourceStorageModeShared],
				[m.device newBufferWithBytes:row length:idx_bytes options:MTLResourceStorageModeShared],
				[m.device newBufferWithBytes:col length:idx_bytes options:MTLResourceStorageModeShared],
				[m.device newBufferWithBytes:val length:val_bytes options:MTLResourceStorageModeShared],
				[m.device newBufferWithBytes:&un length:sizeof(uint) options:MTLResourceStorageModeShared],
			};
			launch(m.queue, m.p_spmv_f32, bufs, MTLSizeMake(nnz, 1, 1), MTLSizeMake(256, 1, 1));
			std::memcpy(y, [by contents], y_bytes);
			return true;
		}
		if (idx == 0)  // f16：float 累加 + 窄化
		{
			if (!m.p_spmv_f16_acc || !m.p_spmv_f16_narrow) return false;
			const std::size_t yacc_bytes = rows * 4, y_bytes = rows * 2;
			id<MTLBuffer> byacc = [m.device newBufferWithLength:yacc_bytes options:MTLResourceStorageModeShared];
			std::memset([byacc contents], 0, yacc_bytes);
			std::vector<id<MTLBuffer>> bufs = {
				byacc,
				[m.device newBufferWithBytes:x length:x_bytes options:MTLResourceStorageModeShared],
				[m.device newBufferWithBytes:row length:idx_bytes options:MTLResourceStorageModeShared],
				[m.device newBufferWithBytes:col length:idx_bytes options:MTLResourceStorageModeShared],
				[m.device newBufferWithBytes:val length:val_bytes options:MTLResourceStorageModeShared],
				[m.device newBufferWithBytes:&un length:sizeof(uint) options:MTLResourceStorageModeShared],
			};
			launch(m.queue, m.p_spmv_f16_acc, bufs, MTLSizeMake(nnz, 1, 1), MTLSizeMake(256, 1, 1));
			uint unrow = (uint)rows;
			std::vector<id<MTLBuffer>> bufs2 = {
				[m.device newBufferWithLength:y_bytes options:MTLResourceStorageModeShared],
				byacc,
				[m.device newBufferWithBytes:&unrow length:sizeof(uint) options:MTLResourceStorageModeShared],
			};
			launch(m.queue, m.p_spmv_f16_narrow, bufs2, MTLSizeMake(rows, 1, 1), MTLSizeMake(256, 1, 1));
			std::memcpy(y, [bufs2[0] contents], y_bytes);
			return true;
		}
		if (idx == 4)  // i32：单 kernel 整型原子累加
		{
			if (!m.p_spmv_i32) return false;
			const std::size_t y_bytes = rows * 4;
			id<MTLBuffer> by = [m.device newBufferWithLength:y_bytes options:MTLResourceStorageModeShared];
			std::memset([by contents], 0, y_bytes);
			std::vector<id<MTLBuffer>> bufs = {
				by,
				[m.device newBufferWithBytes:x length:x_bytes options:MTLResourceStorageModeShared],
				[m.device newBufferWithBytes:row length:idx_bytes options:MTLResourceStorageModeShared],
				[m.device newBufferWithBytes:col length:idx_bytes options:MTLResourceStorageModeShared],
				[m.device newBufferWithBytes:val length:val_bytes options:MTLResourceStorageModeShared],
				[m.device newBufferWithBytes:&un length:sizeof(uint) options:MTLResourceStorageModeShared],
			};
			launch(m.queue, m.p_spmv_i32, bufs, MTLSizeMake(nnz, 1, 1), MTLSizeMake(256, 1, 1));
			std::memcpy(y, [by contents], y_bytes);
			return true;
		}
		if (idx == 2 || idx == 3)  // i8/i16：int32 累加 + 窄化
		{
			int si = idx - 2;
			if (!m.p_spmv_small[si] || !m.p_spmv_narrow[si]) return false;
			const std::size_t yacc_bytes = rows * 4, y_bytes = rows * esz;
			id<MTLBuffer> byacc = [m.device newBufferWithLength:yacc_bytes options:MTLResourceStorageModeShared];
			std::memset([byacc contents], 0, yacc_bytes);
			std::vector<id<MTLBuffer>> bufs = {
				byacc,
				[m.device newBufferWithBytes:x length:x_bytes options:MTLResourceStorageModeShared],
				[m.device newBufferWithBytes:row length:idx_bytes options:MTLResourceStorageModeShared],
				[m.device newBufferWithBytes:col length:idx_bytes options:MTLResourceStorageModeShared],
				[m.device newBufferWithBytes:val length:val_bytes options:MTLResourceStorageModeShared],
				[m.device newBufferWithBytes:&un length:sizeof(uint) options:MTLResourceStorageModeShared],
			};
			launch(m.queue, m.p_spmv_small[si], bufs, MTLSizeMake(nnz, 1, 1), MTLSizeMake(256, 1, 1));
			uint unrow = (uint)rows;
			std::vector<id<MTLBuffer>> bufs2 = {
				[m.device newBufferWithLength:y_bytes options:MTLResourceStorageModeShared],
				byacc,
				[m.device newBufferWithBytes:&unrow length:sizeof(uint) options:MTLResourceStorageModeShared],
			};
			launch(m.queue, m.p_spmv_narrow[si], bufs2, MTLSizeMake(rows, 1, 1), MTLSizeMake(256, 1, 1));
			std::memcpy(y, [bufs2[0] contents], y_bytes);
			return true;
		}
		return false;
	}

	// 3D 卷积（NCDHW）
	bool run_conv3d(void* dst, const void* src, const void* weight, const void* bias,
		std::size_t n, std::size_t cin, std::size_t cout,
		std::size_t idim, std::size_t ih, std::size_t iw, std::size_t kd, std::size_t kh, std::size_t kw,
		std::size_t stride_d, std::size_t stride_h, std::size_t stride_w, std::size_t pad_d, std::size_t pad_h, std::size_t pad_w,
		std::size_t dil_d, std::size_t dil_h, std::size_t dil_w, std::size_t groups, int idx)
	{
		metal_ctx& m = g();
		if (!m.ok || idx < 0 || !m.p_conv3d[idx]) return false;
		const std::size_t od = (idim + 2 * pad_d - dil_d * (kd - 1) - 1) / stride_d + 1;
		const std::size_t oh = (ih + 2 * pad_h - dil_h * (kh - 1) - 1) / stride_h + 1;
		const std::size_t ow = (iw + 2 * pad_w - dil_w * (kw - 1) - 1) / stride_w + 1;
		const std::size_t total = n * cout * od * oh * ow;
		if (total == 0) return true;
		const std::size_t esz = metal_esize(idx);
		const std::size_t src_bytes = n * cin * idim * ih * iw * esz;
		const std::size_t w_bytes = cout * (cin / groups) * kd * kh * kw * esz;
		const std::size_t b_bytes = cout * esz;
		const std::size_t dst_bytes = total * esz;
		conv3d_params p = {
			(std::uint32_t)n, (std::uint32_t)cin, (std::uint32_t)cout,
			(std::uint32_t)idim, (std::uint32_t)ih, (std::uint32_t)iw,
			(std::uint32_t)kd, (std::uint32_t)kh, (std::uint32_t)kw,
			(std::uint32_t)od, (std::uint32_t)oh, (std::uint32_t)ow,
			(std::uint32_t)stride_d, (std::uint32_t)stride_h, (std::uint32_t)stride_w,
			(std::uint32_t)pad_d, (std::uint32_t)pad_h, (std::uint32_t)pad_w,
			(std::uint32_t)dil_d, (std::uint32_t)dil_h, (std::uint32_t)dil_w,
			(std::uint32_t)groups
		};
		std::vector<id<MTLBuffer>> bufs;
		bufs.push_back([m.device newBufferWithLength:dst_bytes options:MTLResourceStorageModeShared]);
		bufs.push_back([m.device newBufferWithBytes:src length:src_bytes options:MTLResourceStorageModeShared]);
		bufs.push_back([m.device newBufferWithBytes:weight length:w_bytes options:MTLResourceStorageModeShared]);
		if (bias)
			bufs.push_back([m.device newBufferWithBytes:bias length:b_bytes options:MTLResourceStorageModeShared]);
		else
		{
			id<MTLBuffer> zb = [m.device newBufferWithLength:b_bytes options:MTLResourceStorageModeShared];
			std::memset([zb contents], 0, b_bytes);
			bufs.push_back(zb);
		}
		bufs.push_back([m.device newBufferWithBytes:&p length:sizeof(conv3d_params) options:MTLResourceStorageModeShared]);
		launch(m.queue, m.p_conv3d[idx], bufs, MTLSizeMake(total, 1, 1), MTLSizeMake(256, 1, 1));
		std::memcpy(dst, [bufs[0] contents], dst_bytes);
		return true;
	}

	// 4D 卷积（N,C,D0,D1,D2,D3）
	bool run_conv4d(void* dst, const void* src, const void* weight, const void* bias,
		std::size_t n, std::size_t cin, std::size_t cout,
		std::size_t i0, std::size_t i1, std::size_t i2, std::size_t i3,
		std::size_t k0, std::size_t k1, std::size_t k2, std::size_t k3,
		std::size_t s0, std::size_t s1, std::size_t s2, std::size_t s3,
		std::size_t p0, std::size_t p1, std::size_t p2, std::size_t p3,
		std::size_t d0, std::size_t d1, std::size_t d2, std::size_t d3, std::size_t groups, int idx)
	{
		metal_ctx& m = g();
		if (!m.ok || idx < 0 || !m.p_conv4d[idx]) return false;
		const std::size_t o0 = (i0 + 2 * p0 - d0 * (k0 - 1) - 1) / s0 + 1;
		const std::size_t o1 = (i1 + 2 * p1 - d1 * (k1 - 1) - 1) / s1 + 1;
		const std::size_t o2 = (i2 + 2 * p2 - d2 * (k2 - 1) - 1) / s2 + 1;
		const std::size_t o3 = (i3 + 2 * p3 - d3 * (k3 - 1) - 1) / s3 + 1;
		const std::size_t total = n * cout * o0 * o1 * o2 * o3;
		if (total == 0) return true;
		const std::size_t esz = metal_esize(idx);
		const std::size_t src_bytes = n * cin * i0 * i1 * i2 * i3 * esz;
		const std::size_t w_bytes = cout * (cin / groups) * k0 * k1 * k2 * k3 * esz;
		const std::size_t b_bytes = cout * esz;
		const std::size_t dst_bytes = total * esz;
		conv4d_params p = {
			(std::uint32_t)n, (std::uint32_t)cin, (std::uint32_t)cout,
			(std::uint32_t)i0, (std::uint32_t)i1, (std::uint32_t)i2, (std::uint32_t)i3,
			(std::uint32_t)k0, (std::uint32_t)k1, (std::uint32_t)k2, (std::uint32_t)k3,
			(std::uint32_t)o0, (std::uint32_t)o1, (std::uint32_t)o2, (std::uint32_t)o3,
			(std::uint32_t)s0, (std::uint32_t)s1, (std::uint32_t)s2, (std::uint32_t)s3,
			(std::uint32_t)p0, (std::uint32_t)p1, (std::uint32_t)p2, (std::uint32_t)p3,
			(std::uint32_t)d0, (std::uint32_t)d1, (std::uint32_t)d2, (std::uint32_t)d3,
			(std::uint32_t)groups
		};
		std::vector<id<MTLBuffer>> bufs;
		bufs.push_back([m.device newBufferWithLength:dst_bytes options:MTLResourceStorageModeShared]);
		bufs.push_back([m.device newBufferWithBytes:src length:src_bytes options:MTLResourceStorageModeShared]);
		bufs.push_back([m.device newBufferWithBytes:weight length:w_bytes options:MTLResourceStorageModeShared]);
		if (bias)
			bufs.push_back([m.device newBufferWithBytes:bias length:b_bytes options:MTLResourceStorageModeShared]);
		else
		{
			id<MTLBuffer> zb = [m.device newBufferWithLength:b_bytes options:MTLResourceStorageModeShared];
			std::memset([zb contents], 0, b_bytes);
			bufs.push_back(zb);
		}
		bufs.push_back([m.device newBufferWithBytes:&p length:sizeof(conv4d_params) options:MTLResourceStorageModeShared]);
		launch(m.queue, m.p_conv4d[idx], bufs, MTLSizeMake(total, 1, 1), MTLSizeMake(256, 1, 1));
		std::memcpy(dst, [bufs[0] contents], dst_bytes);
		return true;
	}

	// 三角求解（单线程；f32/i8/i16/i32）
	bool run_trsv(void* x, const void* tri, const void* b, std::size_t n, bool lower, bool unit_diag, int idx)
	{
		metal_ctx& m = g();
		if (!m.ok || idx < 0) return false;
		int ti = (idx == 1) ? 0 : (idx - 1);   // f32->0, i8->1, i16->2, i32->3
		if (ti < 0 || ti > 3 || !m.p_trsv[ti]) return false;
		if (n == 0) return true;
		const std::size_t esz = metal_esize(idx);
		const std::size_t tri_bytes = n * n * esz, x_bytes = n * esz;
		uint un = (uint)n;
		int il = lower ? 1 : 0, iu = unit_diag ? 1 : 0;
		std::vector<id<MTLBuffer>> bufs = {
			[m.device newBufferWithBytes:b length:x_bytes options:MTLResourceStorageModeShared],
			[m.device newBufferWithBytes:tri length:tri_bytes options:MTLResourceStorageModeShared],
			[m.device newBufferWithBytes:&un length:sizeof(uint) options:MTLResourceStorageModeShared],
			[m.device newBufferWithBytes:&il length:sizeof(int) options:MTLResourceStorageModeShared],
			[m.device newBufferWithBytes:&iu length:sizeof(int) options:MTLResourceStorageModeShared],
		};
		launch(m.queue, m.p_trsv[ti], bufs, MTLSizeMake(1, 1, 1), MTLSizeMake(1, 1, 1));
		std::memcpy(x, [bufs[0] contents], x_bytes);
		return true;
	}

	// Cholesky（单线程；仅 f32）
	bool run_chol(void* L, const void* A, std::size_t n, int idx)
	{
		metal_ctx& m = g();
		if (idx != 1) return false;   // 仅 f32
		if (!m.ok || !m.p_chol) return false;
		if (n == 0) return true;
		const std::size_t bytes = n * n * 4;
		uint un = (uint)n;
		std::vector<id<MTLBuffer>> bufs = {
			[m.device newBufferWithBytes:A length:bytes options:MTLResourceStorageModeShared],
			[m.device newBufferWithBytes:&un length:sizeof(uint) options:MTLResourceStorageModeShared],
		};
		launch(m.queue, m.p_chol, bufs, MTLSizeMake(1, 1, 1), MTLSizeMake(1, 1, 1));
		std::memcpy(L, [bufs[0] contents], bytes);
		// 清上三角（行优先）
		float* Lp = (float*)L;
		for (std::size_t i = 0; i < n; ++i)
			for (std::size_t j = i + 1; j < n; ++j)
				Lp[i * n + j] = 0.0f;
		return true;
	}

	// ---- 后端实现（挂 xhqm::tensor_base::tensor_backend）----
	class metal_backend : public xhqm::tensor_base::tensor_backend
	{
	public:
		const char* name() const noexcept override { return "metal"; }
		xhqm::tensor_base::device kind() const noexcept override { return xhqm::tensor_base::device::gpu; }
		bool supports_memory_transfer() const noexcept override { return true; }

		void parallel_for(std::size_t begin, std::size_t end,
			const std::function<void(std::size_t)>& body) override
		{
			for (std::size_t i = begin; i < end; ++i)
				body(i);
		}

		bool elemwise(void* dst, const void* a, const void* b,
			std::size_t count, xhqm::tensor_base::dtype dt, int op) noexcept override
		{
			return run_elemwise(dst, a, b, count, metal_dtype(dt), op);
		}

		bool select(void* dst, const void* cond, const void* a, const void* b,
			std::size_t count, xhqm::tensor_base::dtype dt) noexcept override
		{
			return run_select(dst, cond, a, b, count, metal_dtype(dt));
		}

		bool matmul(void* c, const void* a, const void* b,
			std::size_t m, std::size_t n, std::size_t k, xhqm::tensor_base::dtype dt) noexcept override
		{
			return run_matmul(c, a, b, m, n, k, metal_dtype(dt));
		}

		bool reduce(void* dst, const void* src,
			std::size_t outer, std::size_t inner, xhqm::tensor_base::dtype dt, int op) noexcept override
		{
			return run_reduce(dst, src, outer, inner, metal_dtype(dt), op);
		}

		bool unary(void* dst, const void* a,
			std::size_t count, xhqm::tensor_base::dtype dt, int op) noexcept override
		{
			return run_unary(dst, a, count, metal_dtype(dt), op);
		}

		bool transpose(void* dst, const void* src,
			std::size_t rows, std::size_t cols, xhqm::tensor_base::dtype dt) noexcept override
		{
			return run_transpose(dst, src, rows, cols, metal_dtype(dt));
		}

		bool grad_accum(void* dst, const void* a, const void* b,
			std::size_t count, xhqm::tensor_base::dtype dt, int op) noexcept override
		{
			return run_grad_accum(dst, a, b, count, metal_dtype(dt), op);
		}

		bool spmv(void* y, const void* x, const xhqm::index* row, const xhqm::index* col,
			const void* val, std::size_t nnz, std::size_t rows, std::size_t cols, xhqm::tensor_base::dtype dt) noexcept override
		{
			return run_spmv(y, x, row, col, val, nnz, rows, cols, metal_dtype(dt));
		}

		bool pool2d(void* dst, const void* src,
			std::size_t n, std::size_t c, std::size_t h, std::size_t w,
			std::size_t oh, std::size_t ow, std::size_t k, std::size_t stride, std::size_t pad,
			xhqm::tensor_base::dtype dt, int op) noexcept override
		{
			return run_pool2d(dst, src, n, c, h, w, oh, ow, k, stride, pad, metal_dtype(dt), op);
		}

		bool upsample2d(void* dst, const void* src,
			std::size_t n, std::size_t c, std::size_t h, std::size_t w,
			std::size_t oh, std::size_t ow, xhqm::tensor_base::dtype dt, int op) noexcept override
		{
			return run_upsample2d(dst, src, n, c, h, w, oh, ow, metal_dtype(dt), op);
		}

		bool dropout(void* dst, const void* src, std::size_t count,
			float p, unsigned int seed, xhqm::tensor_base::dtype dt) noexcept override
		{
			return run_dropout(dst, src, count, p, seed, metal_dtype(dt));
		}

		bool conv3d(void* dst, const void* src, const void* weight, const void* bias,
			std::size_t n, std::size_t cin, std::size_t cout,
			std::size_t id, std::size_t ih, std::size_t iw,
			std::size_t kd, std::size_t kh, std::size_t kw,
			std::size_t stride_d, std::size_t stride_h, std::size_t stride_w,
			std::size_t pad_d, std::size_t pad_h, std::size_t pad_w,
			std::size_t dil_d, std::size_t dil_h, std::size_t dil_w,
			std::size_t groups, xhqm::tensor_base::dtype dt) noexcept override
		{
			return run_conv3d(dst, src, weight, bias, n, cin, cout, id, ih, iw, kd, kh, kw,
				stride_d, stride_h, stride_w, pad_d, pad_h, pad_w, dil_d, dil_h, dil_w, groups, metal_dtype(dt));
		}

		bool conv4d(void* dst, const void* src, const void* weight, const void* bias,
			std::size_t n, std::size_t cin, std::size_t cout,
			std::size_t i0, std::size_t i1, std::size_t i2, std::size_t i3,
			std::size_t k0, std::size_t k1, std::size_t k2, std::size_t k3,
			std::size_t s0, std::size_t s1, std::size_t s2, std::size_t s3,
			std::size_t p0, std::size_t p1, std::size_t p2, std::size_t p3,
			std::size_t d0, std::size_t d1, std::size_t d2, std::size_t d3,
			std::size_t groups, xhqm::tensor_base::dtype dt) noexcept override
		{
			return run_conv4d(dst, src, weight, bias, n, cin, cout, i0, i1, i2, i3, k0, k1, k2, k3,
				s0, s1, s2, s3, p0, p1, p2, p3, d0, d1, d2, d3, groups, metal_dtype(dt));
		}

		bool trsv(void* x, const void* tri, const void* b,
			std::size_t n, bool lower, bool unit_diag, xhqm::tensor_base::dtype dt) noexcept override
		{
			return run_trsv(x, tri, b, n, lower, unit_diag, metal_dtype(dt));
		}

		bool chol(void* L, const void* A, std::size_t n, xhqm::tensor_base::dtype dt) noexcept override
		{
			return run_chol(L, A, n, metal_dtype(dt));
		}

		void upload(void*, std::size_t) override {}
		void download(void*, std::size_t) override {}
		void synchronize() override {}
	};
}

namespace xhqm
{
	namespace tensor_base
	{
		tensor_backend* create_metal_backend()
		{
			if (!g().ok) return nullptr;
			static metal_backend instance;
			return &instance;
		}
	}
}
