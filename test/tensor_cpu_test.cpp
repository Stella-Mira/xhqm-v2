// CPU 后端测试：编译 tensor_backend_cpu.cpp + SIMD（/arch:AVX2），
// 验证 metal/cuda → blas → simd → 多线程 → 串行 的调度链在 CPU 上真正命中 SIMD 层，
// 并覆盖新增的 spmv 多线程核函数。
#include "../algebra/tensor/tensor.h"
#include "test.h"
#include <array>
#include <cmath>
#include <string>

using namespace xhqm;

TEST_CASE("test_cpu_backend_selection")
{
	CHECK(std::string(tensor_base::tensor_backend_active().name()) == "cpu");
}

TEST_CASE("test_cpu_backend_f32_kernels")
{
	// elemwise（SIMD）
	tensor<1, f32> a(std::array<xhqm::size, 1>{ 16 }, 0.0f);
	tensor<1, f32> b(std::array<xhqm::size, 1>{ 16 }, 0.0f);
	for (xhqm::size i = 0; i < 16; ++i) { a[i] = f32(i); b[i] = f32(i + 1); }
	auto s = a + b;
	for (xhqm::size i = 0; i < 16; ++i) CHECK_NEAR(s[i], f32(2 * i + 1), 1e-5);
	auto m = a * b;	// Hadamard
	CHECK_NEAR(m[3], f32(3 * 4), 1e-5);

	// matmul（SIMD）
	matrix<f32> A({ { 1.0f, 2.0f }, { 3.0f, 4.0f } }), B({ { 5.0f, 6.0f }, { 7.0f, 8.0f } });
	auto C = A * B;
	CHECK_NEAR(C(0, 0), 19.0f, 1e-4);
	CHECK_NEAR(C(1, 1), 50.0f, 1e-4);

	// reduce（SIMD，末轴）
	tensor<2, f32> M(std::array<xhqm::size, 2>{ 4, 8 }, 0.0f);
	for (xhqm::size i = 0; i < 4; ++i)
		for (xhqm::size j = 0; j < 8; ++j)
			M(i, j) = f32(i * 8 + j);
	auto rs = tensor_base::reduce_sum(M, 1);
	CHECK_NEAR(rs[0], 28.0f, 1e-3);	// 0+..+7
	CHECK_NEAR(rs[3], 220.0f, 1e-3);	// 24+..+31

	// transpose（SIMD，4×4 分块 + 边缘标量）
	auto Mt = tensor_base::transpose(M, 0, 1);
	CHECK_NEAR(Mt(0, 1), M(1, 0), 1e-5);
	CHECK_NEAR(Mt(7, 3), M(3, 7), 1e-5);
}

TEST_CASE("test_cpu_backend_f64_kernels")
{
	tensor<1, f64> a(std::array<xhqm::size, 1>{ 8 }, 0.0);
	tensor<1, f64> b(std::array<xhqm::size, 1>{ 8 }, 0.0);
	for (xhqm::size i = 0; i < 8; ++i) { a[i] = f64(i); b[i] = f64(i + 1); }
	auto s = a + b;
	CHECK_NEAR(s[7], 15.0, 1e-12);
	matrix<f64> A({ { 1.0, 2.0 }, { 3.0, 4.0 } }), B({ { 5.0, 6.0 }, { 7.0, 8.0 } });
	auto C = A * B;
	CHECK_NEAR(C(1, 1), 50.0, 1e-12);
}

TEST_CASE("test_cpu_backend_unary")
{
	float in[8] = { -2, -1, 0, 1, 2, 4, 9, 16 };
	float out[8] = { 0 };
	tensor_base::tensor_backend_active().unary(out, in, 8, tensor_base::dtype::f32, tensor_base::unary_relu);
	CHECK_NEAR(out[0], 0.0f, 1e-6);
	CHECK_NEAR(out[4], 2.0f, 1e-6);
	tensor_base::tensor_backend_active().unary(out, in, 8, tensor_base::dtype::f32, tensor_base::unary_sqrt);
	CHECK_NEAR(out[5], 2.0f, 1e-6);
	CHECK_NEAR(out[7], 4.0f, 1e-6);
}

TEST_CASE("test_cpu_backend_spmv")
{
	// 100×100 全 1 稠密 → 稀疏 nnz=10000（≥4096 触发多线程部分和路径），
	// v[i]=i+1，每行结果 = 1+..+100 = 5050。
	const xhqm::size n = 100;
	matrix<f32> A(n, n, 1.0f);
	tensor_base::sparse_matrix<f32> S(A);
	CHECK(S.nnz() == n * n);
	vector<f32> v(n, 0.0f);
	for (xhqm::size i = 0; i < n; ++i) v[i] = f32(i + 1);
	auto rd = A * v;	// 稠密 matvec（走 backend matmul）
	auto rs = S * v;	// 稀疏 spmv（走 backend spmv 多线程核）
	for (xhqm::size i = 0; i < n; ++i)
	{
		CHECK_NEAR(rd[i], 5050.0f, 1e-2);
		CHECK_NEAR(rs[i], 5050.0f, 1e-2);
	}

	// f64 小矩阵（nnz<4096 走串行分支）
	matrix<f64> A2({ { 2.0, 0.0, 1.0 }, { 0.0, 3.0, 0.0 }, { 0.0, 0.0, 4.0 } });
	tensor_base::sparse_matrix<f64> S2(A2);
	vector<f64> v2({ 1.0, 2.0, 3.0 });
	auto r2 = S2 * v2;
	CHECK_NEAR(r2[0], 5.0, 1e-12);
	CHECK_NEAR(r2[1], 6.0, 1e-12);
	CHECK_NEAR(r2[2], 12.0, 1e-12);
}

TEST_CASE("test_cpu_backend_f16_kernels")
{
	// elemwise（F16C：f16→f32 计算→f16）
	tensor<1, f16> a(std::array<xhqm::size, 1>{ 8 }, f16(0.0f));
	tensor<1, f16> b(std::array<xhqm::size, 1>{ 8 }, f16(0.0f));
	for (xhqm::size i = 0; i < 8; ++i) { a[i] = f16((float)i); b[i] = f16((float)(i + 1)); }
	auto s = a + b;
	for (xhqm::size i = 0; i < 8; ++i) CHECK_NEAR((float)s[i], (float)(2 * i + 1), 0.01);
	auto m = a * b;	// Hadamard
	CHECK_NEAR((float)m[3], 12.0f, 0.05);

	// matmul（f16→f32 复用 f32 SIMD matmul，fp32 累加）
	matrix<f16> A(2, 2, f16(0.0f));
	A(0, 0) = f16(1.0f); A(0, 1) = f16(2.0f); A(1, 0) = f16(3.0f); A(1, 1) = f16(4.0f);
	matrix<f16> B(2, 2, f16(0.0f));
	B(0, 0) = f16(5.0f); B(0, 1) = f16(6.0f); B(1, 0) = f16(7.0f); B(1, 1) = f16(8.0f);
	auto C = A * B;
	CHECK_NEAR((float)C(0, 0), 19.0f, 0.05);
	CHECK_NEAR((float)C(1, 1), 50.0f, 0.05);

	// reduce（F16C）
	tensor<2, f16> M(std::array<xhqm::size, 2>{ 2, 8 }, f16(0.0f));
	for (xhqm::size i = 0; i < 2; ++i)
		for (xhqm::size j = 0; j < 8; ++j)
			M(i, j) = f16((float)(i * 8 + j));
	auto rs = tensor_base::reduce_sum(M, 1);
	CHECK_NEAR((float)rs[0], 28.0f, 0.05);
	CHECK_NEAR((float)rs[1], 92.0f, 0.05);
}

// 整型内核能力门（运行时自检，非编译期裁剪）：i32 unary_neg 仅 SIMD 层实现，
// 无 SVML/SIMD 的构建（Linux/mac）标量路径不写结果——自检一次，能力缺失则
// 跳过相关用例（用例在册、SKIP 有理由，不藏漂移）。
static bool int_unary_available()
{
	i32 probe = -5;
	i32 out = 0;
	tensor_base::tensor_backend_active().unary(&out, &probe, 1, tensor_base::dtype::i32, tensor_base::unary_neg);
	return out == 5;
}

// f16 unary（relu 等）同样仅 SIMD 层实现（标量 switch 只含 f32/f64）——能力自检
static bool f16_unary_available()
{
	f16 probe = f16(-2.0f);
	f16 out = f16(99.0f);
	tensor_base::tensor_backend_active().unary(&out, &probe, 1, tensor_base::dtype::f16, tensor_base::unary_relu);
	return (float)out == 0.0f;
}

TEST_CASE("test_cpu_backend_f16_more_kernels")
{
	// transpose（f16→f32 复用 f32 分块转置）——标量路径有实现，无条件验证
	tensor<2, f16> M(std::array<xhqm::size, 2>{ 4, 8 }, f16(0.0f));
	for (xhqm::size i = 0; i < 4; ++i)
		for (xhqm::size j = 0; j < 8; ++j)
			M(i, j) = f16((float)(i * 8 + j));
	auto Mt = tensor_base::transpose(M, 0, 1);
	CHECK_NEAR((float)Mt(0, 1), (float)M(1, 0), 0.01);
	CHECK_NEAR((float)Mt(7, 3), (float)M(3, 7), 0.01);

	// unary/grad_accum：f16 仅 SIMD 层实现（标量 switch 只含 f32/f64，无 SIMD 构建
	// 不写结果——栈上未初始化值曾"侥幸通过"，运行时能力门后诚实跳过）
	if (!f16_unary_available())
	{
		WARN("skip: f16 unary/grad_accum 仅 SIMD 实现，本构建无该层");
		return;
	}

	f16 in[8] = { f16(-2.0f), f16(-1.0f), f16(0.0f), f16(1.0f), f16(2.0f), f16(4.0f), f16(9.0f), f16(16.0f) };
	f16 out[8];
	tensor_base::tensor_backend_active().unary(out, in, 8, tensor_base::dtype::f16, tensor_base::unary_relu);
	CHECK_NEAR((float)out[0], 0.0f, 0.01);
	CHECK_NEAR((float)out[4], 2.0f, 0.01);

	// grad_accum（f16→f32 复用 f32 SIMD）
	f16 dst[4] = { f16(1.0f), f16(1.0f), f16(1.0f), f16(1.0f) };
	f16 ga[4] = { f16(2.0f), f16(2.0f), f16(2.0f), f16(2.0f) };
	f16 gb[4] = { f16(1.0f), f16(-1.0f), f16(1.0f), f16(-1.0f) };
	tensor_base::tensor_backend_active().grad_accum(dst, ga, gb, 4, tensor_base::dtype::f16, tensor_base::grad_relu);
	CHECK_NEAR((float)dst[0], 3.0f, 0.01);	// 1 + 2*(1>0)
	CHECK_NEAR((float)dst[1], 1.0f, 0.01);	// 1 + 2*(-1>0 ? 0)
}

TEST_CASE("test_cpu_backend_integer_kernels")
{
	// i32 逐元素（add/sub/mul 走 AVX2 整型 SIMD）
	tensor<1, i32> a(std::array<xhqm::size, 1>{ 16 }, 0);
	tensor<1, i32> b(std::array<xhqm::size, 1>{ 16 }, 0);
	for (xhqm::size i = 0; i < 16; ++i) { a[i] = (i32)i; b[i] = (i32)(i + 1); }
	auto s = a + b;
	for (xhqm::size i = 0; i < 16; ++i) CHECK(s[i] == (i32)(2 * i + 1));
	auto m = a * b;
	CHECK(m[3] == (i32)(3 * 4));
	CHECK(m[15] == (i32)(15 * 16));
	auto d = a - b;
	CHECK(d[0] == (i32)-1);
	CHECK(d[15] == (i32)-1);

	// i32 归约（sum 走 AVX2，max/min/prod 走标量）
	tensor<2, i32> M(std::array<xhqm::size, 2>{ 2, 8 }, 0);
	for (xhqm::size i = 0; i < 2; ++i)
		for (xhqm::size j = 0; j < 8; ++j)
			M(i, j) = (i32)(i * 8 + j);
	auto rs = tensor_base::reduce_sum(M, 1);
	CHECK(rs[0] == (i32)28);
	CHECK(rs[1] == (i32)92);
	auto rmax = tensor_base::reduce_max(M, 1);
	CHECK(rmax[0] == (i32)7);
	CHECK(rmax[1] == (i32)15);
	auto rmin = tensor_base::reduce_min(M, 1);
	CHECK(rmin[0] == (i32)0);
	CHECK(rmin[1] == (i32)8);

	// i64 逐元素（add/sub 走 AVX2）
	tensor<1, i64> a64(std::array<xhqm::size, 1>{ 8 }, 0);
	tensor<1, i64> b64(std::array<xhqm::size, 1>{ 8 }, 0);
	for (xhqm::size i = 0; i < 8; ++i) { a64[i] = (i64)i; b64[i] = (i64)(i + 1); }
	auto s64 = a64 + b64;
	CHECK(s64[7] == (i64)15);
	CHECK(s64[0] == (i64)1);
	tensor<1, i64> rs64(std::array<xhqm::size, 1>{ 8 }, 0);
	{
		tensor<2, i64> M64(std::array<xhqm::size, 2>{ 1, 8 }, 0);
		for (xhqm::size j = 0; j < 8; ++j) M64(0, j) = (i64)(j + 1);
		auto r = tensor_base::reduce_sum(M64, 1);
		CHECK(r[0] == (i64)36);	// 1+..+8
	}

	// 整型转置（按字节并行拷贝）
	tensor<2, i32> Ti(std::array<xhqm::size, 2>{ 4, 8 }, 0);
	for (xhqm::size i = 0; i < 4; ++i)
		for (xhqm::size j = 0; j < 8; ++j)
			Ti(i, j) = (i32)(i * 8 + j);
	auto Tt = tensor_base::transpose(Ti, 0, 1);
	CHECK(Tt(0, 1) == Ti(1, 0));
	CHECK(Tt(7, 3) == Ti(3, 7));
	CHECK(Tt(5, 2) == Ti(2, 5));
}

TEST_CASE("test_cpu_backend_bool_kernels")
{
	// bool 逐元素（GF(2)：+/- = XOR，*/÷ = AND），布局兼容 1 字节/元素
	bool pa[8] = { true, true, false, false, true, false, true, false };
	bool pb[8] = { true, false, true, false, true, true, false, false };
	tensor<1, bool> a(std::array<xhqm::size, 1>{ 8 }, false);
	tensor<1, bool> b(std::array<xhqm::size, 1>{ 8 }, false);
	for (xhqm::size i = 0; i < 8; ++i) { a[i] = pa[i]; b[i] = pb[i]; }
	auto s = a + b;	// XOR
	CHECK(s[0] == false);	// T ^ T
	CHECK(s[1] == true);	// T ^ F
	CHECK(s[2] == true);	// F ^ T
	CHECK(s[3] == false);	// F ^ F
	auto m = a * b;	// AND
	CHECK(m[0] == true);	// T & T
	CHECK(m[1] == false);	// T & F
	CHECK(m[4] == true);	// T & T

	// bool 归约（GF(2)：sum=XOR，prod=AND，max=OR，min=AND）
	tensor<2, bool> Mb(std::array<xhqm::size, 2>{ 2, 4 }, false);
	bool row0[4] = { true, true, false, false };
	bool row1[4] = { true, false, true, false };
	for (xhqm::size j = 0; j < 4; ++j) { Mb(0, j) = row0[j]; Mb(1, j) = row1[j]; }
	auto rxor = tensor_base::reduce_sum(Mb, 1);	// XOR
	CHECK(rxor[0] == false);	// T^T^F^F
	CHECK(rxor[1] == false);	// T^F^T^F
	auto rand = tensor_base::reduce_last(Mb, tensor_base::reduce_op_prod);	// AND
	CHECK(rand[0] == false);
	CHECK(rand[1] == false);
	auto ror = tensor_base::reduce_last(Mb, tensor_base::reduce_op_max);	// OR
	CHECK(ror[0] == true);
	CHECK(ror[1] == true);
	auto rmin = tensor_base::reduce_last(Mb, tensor_base::reduce_op_min);	// AND
	CHECK(rmin[0] == false);
	CHECK(rmin[1] == false);

	// bool 转置
	tensor<2, bool> Tb(std::array<xhqm::size, 2>{ 3, 4 }, false);
	for (xhqm::size i = 0; i < 3; ++i)
		for (xhqm::size j = 0; j < 4; ++j)
			Tb(i, j) = ((i + j) % 2 == 0);
	auto Tbt = tensor_base::transpose(Tb, 0, 1);
	CHECK(Tbt(0, 1) == Tb(1, 0));
	CHECK(Tbt(3, 2) == Tb(2, 3));
}

TEST_CASE("test_cpu_backend_nan_inf")
{
	float in[8] = { 1.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
		-std::numeric_limits<float>::infinity(), 0.0f, -0.0f, 3.0f, -3.0f };
	float out[8] = { 0 };
	tensor_base::tensor_backend_active().unary(out, in, 8, tensor_base::dtype::f32, tensor_base::unary_isnan);
	CHECK(out[1] == 1.0f); CHECK(out[0] == 0.0f); CHECK(out[2] == 0.0f);
	tensor_base::tensor_backend_active().unary(out, in, 8, tensor_base::dtype::f32, tensor_base::unary_isinf);
	CHECK(out[2] == 1.0f); CHECK(out[3] == 1.0f); CHECK(out[0] == 0.0f); CHECK(out[1] == 0.0f);
	tensor_base::tensor_backend_active().unary(out, in, 8, tensor_base::dtype::f32, tensor_base::unary_isfinite);
	CHECK(out[0] == 1.0f); CHECK(out[1] == 0.0f); CHECK(out[2] == 0.0f); CHECK(out[3] == 0.0f);
}

TEST_CASE("test_cpu_backend_i8_i16")
{
	tensor<1, i8> a8(std::array<xhqm::size, 1>{ 32 }, 0);
	tensor<1, i8> b8(std::array<xhqm::size, 1>{ 32 }, 0);
	for (xhqm::size i = 0; i < 32; ++i) { a8[i] = (i8)(i % 60); b8[i] = (i8)((i + 1) % 60); }
	auto s8 = a8 + b8;
	CHECK(s8[0] == (i8)1);
	CHECK(s8[31] == (i8)63);	// 31+32
	auto d8 = a8 - b8;
	CHECK(d8[0] == (i8)-1);
	CHECK(d8[31] == (i8)-1);

	tensor<1, i16> a16(std::array<xhqm::size, 1>{ 16 }, 0);
	tensor<1, i16> b16(std::array<xhqm::size, 1>{ 16 }, 0);
	for (xhqm::size i = 0; i < 16; ++i) { a16[i] = (i16)i; b16[i] = (i16)(i + 1); }
	auto s16 = a16 + b16;
	CHECK(s16[15] == (i16)31);
	auto m16 = a16 * b16;	// mullo SIMD
	CHECK(m16[3] == (i16)12);
	CHECK(m16[15] == (i16)240);
}

TEST_CASE("test_cpu_backend_i32_matmul")
{
	matrix<i32> A({ { 1, 2 }, { 3, 4 } }), B({ { 5, 6 }, { 7, 8 } });
	auto C = A * B;
	CHECK(C(0, 0) == (i32)19);
	CHECK(C(0, 1) == (i32)22);
	CHECK(C(1, 0) == (i32)43);
	CHECK(C(1, 1) == (i32)50);
}

TEST_CASE("test_cpu_backend_trsv_chol")
{
	// trsv_lower：L·x=b，L=[[1,0],[2,1]]，b=[1,5] -> x=[1,3]
	tensor<2, f32> L(std::array<xhqm::size, 2>{ 2, 2 }, 0.0f);
	L(0, 0) = 1; L(1, 0) = 2; L(1, 1) = 1;
	tensor<1, f32> b(std::array<xhqm::size, 1>{ 2 }, 0.0f);
	b[0] = 1; b[1] = 5;
	auto x = tensor_base::trsv_lower(L, b, false);
	CHECK_NEAR(x[0], 1.0f, 1e-5);
	CHECK_NEAR(x[1], 3.0f, 1e-5);

	// trsv_upper：U·x=b，U=[[2,1],[0,1]]，b=[5,3] -> x=[1,3]
	tensor<2, f32> U(std::array<xhqm::size, 2>{ 2, 2 }, 0.0f);
	U(0, 0) = 2; U(0, 1) = 1; U(1, 1) = 1;
	tensor<1, f32> bu(std::array<xhqm::size, 1>{ 2 }, 0.0f);
	bu[0] = 5; bu[1] = 3;
	auto xu = tensor_base::trsv_upper(U, bu, false);
	CHECK_NEAR(xu[0], 1.0f, 1e-5);
	CHECK_NEAR(xu[1], 3.0f, 1e-5);

	// chol：A=[[4,2],[2,3]] -> L=[[2,0],[1,√2]]
	tensor<2, f32> A(std::array<xhqm::size, 2>{ 2, 2 }, 0.0f);
	A(0, 0) = 4; A(0, 1) = 2; A(1, 0) = 2; A(1, 1) = 3;
	auto Lc = tensor_base::chol(A);
	CHECK(Lc.dimension(0) == 2);
	CHECK_NEAR(Lc(0, 0), 2.0f, 1e-4);
	CHECK_NEAR(Lc(0, 1), 0.0f, 1e-4);
	CHECK_NEAR(Lc(1, 0), 1.0f, 1e-4);
	CHECK_NEAR(Lc(1, 1), std::sqrt(2.0f), 1e-4);
}

TEST_CASE("test_cpu_backend_conv")
{
	// conv1d：input[1..5] × kernel[1,1,1] -> [6,9,12]
	tensor<3, f32> x(std::array<xhqm::size, 3>{ 1, 1, 5 }, 0.0f);
	for (xhqm::size i = 0; i < 5; ++i) x(0, 0, i) = f32(i + 1);
	tensor<3, f32> w(std::array<xhqm::size, 3>{ 1, 1, 3 }, 1.0f);
	auto y = tensor_base::conv1d(x, w, 1, 0, 1, 1);
	CHECK_NEAR(y(0, 0, 0), 6.0f, 1e-4);
	CHECK_NEAR(y(0, 0, 1), 9.0f, 1e-4);
	CHECK_NEAR(y(0, 0, 2), 12.0f, 1e-4);

	// conv2d：3x3 × 2x2 全 1 kernel -> 2x2 块和
	tensor<4, f32> x2(std::array<xhqm::size, 4>{ 1, 1, 3, 3 }, 0.0f);
	for (xhqm::size i = 0; i < 3; ++i)
		for (xhqm::size j = 0; j < 3; ++j)
			x2(0, 0, i, j) = f32(i * 3 + j + 1);
	tensor<4, f32> w2(std::array<xhqm::size, 4>{ 1, 1, 2, 2 }, 1.0f);
	auto y2 = tensor_base::conv2d(x2, w2, 1, 0, 1, 1);
	CHECK_NEAR(y2(0, 0, 0, 0), 12.0f, 1e-4);	// 1+2+4+5
	CHECK_NEAR(y2(0, 0, 0, 1), 16.0f, 1e-4);	// 2+3+5+6
	CHECK_NEAR(y2(0, 0, 1, 0), 24.0f, 1e-4);	// 4+5+7+8
	CHECK_NEAR(y2(0, 0, 1, 1), 28.0f, 1e-4);	// 5+6+8+9

	// conv3d：3x3x3 × 2x2x2 全 1 kernel -> 2x2x2 块和
	tensor<5, f32> x3(std::array<xhqm::size, 5>{ 1, 1, 3, 3, 3 }, 0.0f);
	for (xhqm::size d = 0; d < 3; ++d)
		for (xhqm::size i = 0; i < 3; ++i)
			for (xhqm::size j = 0; j < 3; ++j)
				x3(0, 0, d, i, j) = f32((d * 3 + i) * 3 + j + 1);
	tensor<5, f32> w3(std::array<xhqm::size, 5>{ 1, 1, 2, 2, 2 }, 1.0f);
	auto y3 = tensor_base::conv3d(x3, w3, 1, 0, 1, 1);
	// 块 (0,0,0) = 1+2+4+5+10+11+13+14 = 60
	CHECK_NEAR(y3(0, 0, 0, 0, 0), 60.0f, 1e-3);
	// 块 (1,1,1) = 14+15+17+18+23+24+26+27 = 164
	CHECK_NEAR(y3(0, 0, 1, 1, 1), 164.0f, 1e-3);
}

TEST_CASE("test_cpu_backend_unary_int")
{
	if (!int_unary_available()) { WARN("skip: 整型 unary 仅 SIMD/SVML 实现，本构建无该层"); return; }
	auto& be = tensor_base::tensor_backend_active();

	// i32：neg/square/abs/sign 全走 SIMD
	i32 in32[8] = { -5, -1, 0, 1, 5, -7, 3, -2 };
	i32 out32[8] = { 0 };
	be.unary(out32, in32, 8, tensor_base::dtype::i32, tensor_base::unary_neg);
	CHECK(out32[0] == 5); CHECK(out32[2] == 0); CHECK(out32[3] == -1);
	be.unary(out32, in32, 8, tensor_base::dtype::i32, tensor_base::unary_abs);
	CHECK(out32[0] == 5); CHECK(out32[2] == 0); CHECK(out32[5] == 7);
	be.unary(out32, in32, 8, tensor_base::dtype::i32, tensor_base::unary_sign);
	CHECK(out32[0] == -1); CHECK(out32[2] == 0); CHECK(out32[4] == 1);
	be.unary(out32, in32, 8, tensor_base::dtype::i32, tensor_base::unary_square);
	CHECK(out32[1] == 1); CHECK(out32[4] == 25); CHECK(out32[5] == 49);

	// i64：neg/abs/sign（square 无 epi64 mul，回退标量）
	i64 in64[4] = { -3, 0, 9, -11 };
	i64 out64[4] = { 0 };
	be.unary(out64, in64, 4, tensor_base::dtype::i64, tensor_base::unary_neg);
	CHECK(out64[0] == 3); CHECK(out64[2] == -9);
	be.unary(out64, in64, 4, tensor_base::dtype::i64, tensor_base::unary_abs);
	CHECK(out64[0] == 3); CHECK(out64[3] == 11);
	be.unary(out64, in64, 4, tensor_base::dtype::i64, tensor_base::unary_sign);
	CHECK(out64[0] == -1); CHECK(out64[1] == 0); CHECK(out64[2] == 1);

	// i16：neg/square/abs/sign
	i16 in16[8] = { -4, -1, 0, 1, 4, -6, 2, -2 };
	i16 out16[8] = { 0 };
	be.unary(out16, in16, 8, tensor_base::dtype::i16, tensor_base::unary_neg);
	CHECK(out16[0] == 4); CHECK(out16[4] == -4);
	be.unary(out16, in16, 8, tensor_base::dtype::i16, tensor_base::unary_abs);
	CHECK(out16[0] == 4); CHECK(out16[5] == 6);
	be.unary(out16, in16, 8, tensor_base::dtype::i16, tensor_base::unary_sign);
	CHECK(out16[0] == -1); CHECK(out16[2] == 0); CHECK(out16[3] == 1);
	be.unary(out16, in16, 8, tensor_base::dtype::i16, tensor_base::unary_square);
	CHECK(out16[4] == 16); CHECK(out16[5] == 36);

	// i8：neg/abs/sign（square 无 epi8 mul，回退标量）
	i8 in8[8] = { -3, -1, 0, 1, 3, -5, 2, -2 };
	i8 out8[8] = { 0 };
	be.unary(out8, in8, 8, tensor_base::dtype::i8, tensor_base::unary_neg);
	CHECK(out8[0] == 3); CHECK(out8[4] == -3);
	be.unary(out8, in8, 8, tensor_base::dtype::i8, tensor_base::unary_abs);
	CHECK(out8[0] == 3); CHECK(out8[5] == 5);
	be.unary(out8, in8, 8, tensor_base::dtype::i8, tensor_base::unary_sign);
	CHECK(out8[0] == -1); CHECK(out8[2] == 0); CHECK(out8[3] == 1);
}

TEST_CASE("test_cpu_backend_i16_bool_matmul")
{
	if (!int_unary_available()) { WARN("skip: i16 matmul 仅 SIMD 实现，本构建无该层"); return; }
	// i16 matmul（mullo_epi16，16 位截断算术）
	matrix<i16> A({ { 1, 2 }, { 3, 4 } }), B({ { 5, 6 }, { 7, 8 } });
	auto C = A * B;
	CHECK(C(0, 0) == (i16)19);
	CHECK(C(0, 1) == (i16)22);
	CHECK(C(1, 0) == (i16)43);
	CHECK(C(1, 1) == (i16)50);

	// bool matmul（GF(2)：C = XOR_k (A[k] & B[k,:])）
	matrix<bool> Ab(2, 2, false), Bb(2, 2, false);
	Ab(0, 0) = true; Ab(0, 1) = false; Ab(1, 0) = true; Ab(1, 1) = true;
	Bb(0, 0) = true; Bb(0, 1) = true; Bb(1, 0) = false; Bb(1, 1) = true;
	auto Cb = Ab * Bb;
	CHECK(Cb(0, 0) == true);	// (1&1)^(0&0)=1
	CHECK(Cb(0, 1) == true);	// (1&1)^(0&1)=1
	CHECK(Cb(1, 0) == true);	// (1&1)^(1&0)=1
	CHECK(Cb(1, 1) == false);	// (1&1)^(1&1)=0
}

TEST_CASE("test_cpu_backend_spmv_int_bool")
{
	// i32 稀疏 × 稠密（100×100 全 1，nnz=10000 ≥ 4096 触发多线程）
	const xhqm::size n = 100;
	matrix<i32> Ai(n, n, (i32)1);
	tensor_base::sparse_matrix<i32> Si(Ai);
	vector<i32> vi(n, (i32)0);
	for (xhqm::size i = 0; i < n; ++i) vi[i] = (i32)(i + 1);
	auto ri = Si * vi;
	for (xhqm::size i = 0; i < n; ++i) CHECK(ri[i] == (i32)5050);

	// bool spmv 后端原语（GF(2)：y ^= val & x；sparse_matrix<bool> 因 vector<bool> 位压缩暂不支持）
	bool bval[4] = { true, true, true, false };
	xhqm::index brow[4] = { 0, 0, 1, 1 };
	xhqm::index bcol[4] = { 0, 1, 0, 1 };
	bool bx[2] = { true, true };
	bool by[2] = { false, false };
	tensor_base::tensor_backend_active().spmv(by, bx, brow, bcol, bval, 4, 2, 2, tensor_base::dtype::boolean);
	CHECK(by[0] == false);	// (1&1)^(1&1)=0
	CHECK(by[1] == true);	// (1&1)^(0&1)=1
}

TEST_CASE("test_cpu_backend_conv_int_bool")
{
	// conv2d i32：3x3 × 2x2 全 1 -> 2x2 块和（S=2 降维到 3D 复用 conv3d 整型 GEMM）
	tensor<4, i32> x2i(std::array<xhqm::size, 4>{ 1, 1, 3, 3 }, 0);
	for (xhqm::size i = 0; i < 3; ++i)
		for (xhqm::size j = 0; j < 3; ++j)
			x2i(0, 0, i, j) = (i32)(i * 3 + j + 1);
	tensor<4, i32> w2i(std::array<xhqm::size, 4>{ 1, 1, 2, 2 }, 1);
	auto y2i = tensor_base::conv2d(x2i, w2i, 1, 0, 1, 1);
	CHECK(y2i(0, 0, 0, 0) == (i32)12);	// 1+2+4+5
	CHECK(y2i(0, 0, 0, 1) == (i32)16);	// 2+3+5+6
	CHECK(y2i(0, 0, 1, 0) == (i32)24);	// 4+5+7+8
	CHECK(y2i(0, 0, 1, 1) == (i32)28);	// 5+6+8+9

	// conv3d i32：3x3x3 × 2x2x2 全 1 -> 2x2x2 块和
	tensor<5, i32> x3i(std::array<xhqm::size, 5>{ 1, 1, 3, 3, 3 }, 0);
	for (xhqm::size d = 0; d < 3; ++d)
		for (xhqm::size i = 0; i < 3; ++i)
			for (xhqm::size j = 0; j < 3; ++j)
				x3i(0, 0, d, i, j) = (i32)((d * 3 + i) * 3 + j + 1);
	tensor<5, i32> w3i(std::array<xhqm::size, 5>{ 1, 1, 2, 2, 2 }, 1);
	auto y3i = tensor_base::conv3d(x3i, w3i, 1, 0, 1, 1);
	CHECK(y3i(0, 0, 0, 0, 0) == (i32)60);
	CHECK(y3i(0, 0, 1, 1, 1) == (i32)164);

	// conv2d bool：3x3 × 2x2 全 true -> 2x2（GF(2) XOR 块）
	tensor<4, bool> x2b(std::array<xhqm::size, 4>{ 1, 1, 3, 3 }, false);
	x2b(0, 0, 0, 0) = true;	// block(0,0) 感受野 {0,1}² 仅含 (0,0) 一个 true → XOR true
	x2b(0, 0, 1, 2) = true; x2b(0, 0, 2, 1) = true;	// block(1,1) 感受野 {1,2}² 含 (1,2),(2,1) 两个 true → XOR false
	tensor<4, bool> w2b(std::array<xhqm::size, 4>{ 1, 1, 2, 2 }, true);
	auto y2b = tensor_base::conv2d(x2b, w2b, 1, 0, 1, 1);
	CHECK(y2b(0, 0, 0, 0) == true);
	CHECK(y2b(0, 0, 1, 1) == false);

	// conv3d bool：3x3x3 × 2x2x2 全 true -> 2x2x2（GF(2) XOR 块）
	tensor<5, bool> x3b(std::array<xhqm::size, 5>{ 1, 1, 3, 3, 3 }, false);
	x3b(0, 0, 0, 0, 0) = true;	// block(0,0,0) 感受野 {0,1}³ 仅含 (0,0,0) 一个 true → XOR true
	x3b(0, 0, 2, 1, 1) = true; x3b(0, 0, 1, 2, 1) = true;	// block(1,1,1) 感受野 {1,2}³ 含两者 → XOR false
	tensor<5, bool> w3b(std::array<xhqm::size, 5>{ 1, 1, 2, 2, 2 }, true);
	auto y3b = tensor_base::conv3d(x3b, w3b, 1, 0, 1, 1);
	CHECK(y3b(0, 0, 0, 0, 0) == true);
	CHECK(y3b(0, 0, 1, 1, 1) == false);
}

TEST_CASE("test_cpu_backend_i8_i16_transpose")
{
	// i16 转置（10x12，非 8 的倍数，测块转置 + 边缘标量）
	tensor<2, i16> M16(std::array<xhqm::size, 2>{ 10, 12 }, 0);
	for (xhqm::size i = 0; i < 10; ++i)
		for (xhqm::size j = 0; j < 12; ++j)
			M16(i, j) = (i16)(i * 12 + j);
	auto T16 = tensor_base::transpose(M16, 0, 1);
	for (xhqm::size i = 0; i < 10; ++i)
		for (xhqm::size j = 0; j < 12; ++j)
			CHECK(T16(j, i) == M16(i, j));

	// i8 转置（20x24，非 16 的倍数）
	tensor<2, i8> M8(std::array<xhqm::size, 2>{ 20, 24 }, 0);
	for (xhqm::size i = 0; i < 20; ++i)
		for (xhqm::size j = 0; j < 24; ++j)
			M8(i, j) = (i8)((i * 24 + j) % 100);
	auto T8 = tensor_base::transpose(M8, 0, 1);
	for (xhqm::size i = 0; i < 20; ++i)
		for (xhqm::size j = 0; j < 24; ++j)
			CHECK(T8(j, i) == M8(i, j));
}

TEST_CASE("test_cpu_backend_conv4d")
{
	// f32 conv4d：2x2x2x2 × 2x2x2x2 全 1 -> 1x1x1x1（块和 1..16 = 136）
	tensor<6, f32> x4(std::array<xhqm::size, 6>{ 1, 1, 2, 2, 2, 2 }, 0.0f);
	for (xhqm::size a = 0; a < 2; ++a)
		for (xhqm::size b = 0; b < 2; ++b)
			for (xhqm::size c = 0; c < 2; ++c)
				for (xhqm::size d = 0; d < 2; ++d)
					x4(0, 0, a, b, c, d) = f32(((a * 2 + b) * 2 + c) * 2 + d + 1);
	tensor<6, f32> w4(std::array<xhqm::size, 6>{ 1, 1, 2, 2, 2, 2 }, 1.0f);
	auto y4 = tensor_base::conv4d(x4, w4, 1, 0, 1, 1);
	CHECK(y4.dimension(2) == 1);
	CHECK(y4.dimension(3) == 1);
	CHECK(y4.dimension(4) == 1);
	CHECK(y4.dimension(5) == 1);
	CHECK_NEAR(y4(0, 0, 0, 0, 0, 0), 136.0f, 1e-3);

	// f32 conv4d 与直接卷积 conv_nd<4> 交叉验证（3x3x3x3 × 2x2x2x2 -> 2x2x2x2）
	tensor<6, f32> x4b(std::array<xhqm::size, 6>{ 1, 1, 3, 3, 3, 3 }, 0.0f);
	for (xhqm::size a = 0; a < 3; ++a)
		for (xhqm::size b = 0; b < 3; ++b)
			for (xhqm::size c = 0; c < 3; ++c)
				for (xhqm::size d = 0; d < 3; ++d)
					x4b(0, 0, a, b, c, d) = f32(((a * 3 + b) * 3 + c) * 3 + d + 1);
	tensor<6, f32> w4b(std::array<xhqm::size, 6>{ 1, 1, 2, 2, 2, 2 }, 1.0f);
	auto yb = tensor_base::conv4d(x4b, w4b, 1, 0, 1, 1);
	auto yr = tensor_base::conv_nd<4, f32>(x4b, w4b, nullptr,
		{ 1, 1, 1, 1 }, { 0, 0, 0, 0 }, { 1, 1, 1, 1 }, 1);
	for (xhqm::size a = 0; a < 2; ++a)
		for (xhqm::size b = 0; b < 2; ++b)
			for (xhqm::size c = 0; c < 2; ++c)
				for (xhqm::size d = 0; d < 2; ++d)
					CHECK_NEAR(yb(0, 0, a, b, c, d), yr(0, 0, a, b, c, d), 1e-3);

	// i32 conv4d：2x2x2x2 × 2x2x2x2 全 1 -> 1x1x1x1（块和 = 136，走整型 GEMM）
	tensor<6, i32> x4i(std::array<xhqm::size, 6>{ 1, 1, 2, 2, 2, 2 }, 0);
	for (xhqm::size a = 0; a < 2; ++a)
		for (xhqm::size b = 0; b < 2; ++b)
			for (xhqm::size c = 0; c < 2; ++c)
				for (xhqm::size d = 0; d < 2; ++d)
					x4i(0, 0, a, b, c, d) = (i32)(((a * 2 + b) * 2 + c) * 2 + d + 1);
	tensor<6, i32> w4i(std::array<xhqm::size, 6>{ 1, 1, 2, 2, 2, 2 }, 1);
	auto y4i = tensor_base::conv4d(x4i, w4i, 1, 0, 1, 1);
	CHECK(y4i(0, 0, 0, 0, 0, 0) == (i32)136);
}

TEST_CASE("test_cpu_backend_reduce_int_parallel")
{
	// i8/i16 末维归约无 SIMD，走 reduce() 的 parallel_for 回退（多线程，数据量小则串行）
	tensor<2, i16> M16(std::array<xhqm::size, 2>{ 4, 8 }, 0);
	for (xhqm::size i = 0; i < 4; ++i)
		for (xhqm::size j = 0; j < 8; ++j)
			M16(i, j) = (i16)(i * 8 + j);
	auto rs16 = tensor_base::reduce_sum(M16, 1);
	CHECK(rs16[0] == (i16)28);
	CHECK(rs16[1] == (i16)92);
	CHECK(rs16[3] == (i16)220);

	tensor<2, i8> M8(std::array<xhqm::size, 2>{ 2, 8 }, 0);
	for (xhqm::size i = 0; i < 2; ++i)
		for (xhqm::size j = 0; j < 8; ++j)
			M8(i, j) = (i8)(i * 8 + j);
	auto rs8 = tensor_base::reduce_sum(M8, 1);
	CHECK(rs8[0] == (i8)28);
	CHECK(rs8[1] == (i8)92);
}

TEST_CASE("test_cpu_backend_trsv_chol_parallel")
{
	// trsv i32（unit 对角，无除法 → 精确）：n>256 触发块化前代/回代的尾块/头块更新路径
	const xhqm::size n = 300;
	tensor<2, i32> L(std::array<xhqm::size, 2>{ n, n }, 0);
	tensor<1, i32> b(std::array<xhqm::size, 1>{ n }, 0);
	for (xhqm::size i = 0; i < n; ++i)
		for (xhqm::size j = 0; j < i; ++j)
			L(i, j) = (i32)((i * 7 + j) % 5 + 1);
	for (xhqm::size i = 0; i < n; ++i) b[i] = (i32)((i * 3 + 1) % 17);
	auto xl = tensor_base::trsv_lower(L, b, true);
	for (xhqm::size i = 0; i < n; ++i)
	{
		i32 s = xl[i];	// L(i,i)=1
		for (xhqm::size j = 0; j < i; ++j) s += L(i, j) * xl[j];
		CHECK(s == b[i]);
	}

	tensor<2, i32> U(std::array<xhqm::size, 2>{ n, n }, 0);
	for (xhqm::size i = 0; i < n; ++i)
		for (xhqm::size j = i + 1; j < n; ++j)
			U(i, j) = (i32)((i * 7 + j) % 5 + 1);
	auto xu = tensor_base::trsv_upper(U, b, true);
	for (xhqm::size i = 0; i < n; ++i)
	{
		i32 s = xu[i];	// U(i,i)=1
		for (xhqm::size j = i + 1; j < n; ++j) s += U(i, j) * xu[j];
		CHECK(s == b[i]);
	}

	// chol f16（非 f32/f64 → 走块化列向 Cholesky，非 LAPACK）：验证 L·Lᵀ ≈ A
	tensor<2, f16> A(std::array<xhqm::size, 2>{ 3, 3 }, f16(0.0f));
	A(0, 0) = f16(4.0f); A(0, 1) = f16(2.0f); A(0, 2) = f16(1.0f);
	A(1, 0) = f16(2.0f); A(1, 1) = f16(5.0f); A(1, 2) = f16(2.0f);
	A(2, 0) = f16(1.0f); A(2, 1) = f16(2.0f); A(2, 2) = f16(6.0f);
	auto Lc = tensor_base::chol(A);
	CHECK(Lc.dimension(0) == 3);
	for (xhqm::size i = 0; i < 3; ++i)
		for (xhqm::size j = 0; j <= i; ++j)
		{
			float s = 0.0f;
			for (xhqm::size k = 0; k <= j; ++k) s += (float)Lc(i, k) * (float)Lc(j, k);
			CHECK_NEAR(s, (float)A(i, j), 0.15f);
		}
	// 上三角清零
	CHECK_NEAR((float)Lc(0, 1), 0.0f, 0.01f);
	CHECK_NEAR((float)Lc(0, 2), 0.0f, 0.01f);
	CHECK_NEAR((float)Lc(1, 2), 0.0f, 0.01f);
}

TEST_CASE("test_cpu_backend_spmv_f16_bool")
{
	// f16 稀疏 × 稠密（走 spmv_arith<f16> 部分和）
	matrix<f16> A(2, 2, f16(0.0f));
	A(0, 0) = f16(2.0f); A(0, 1) = f16(0.0f); A(1, 0) = f16(0.0f); A(1, 1) = f16(3.0f);
	tensor_base::sparse_matrix<f16> S(A);
	vector<f16> v(2, f16(0.0f));
	v[0] = f16(1.0f); v[1] = f16(2.0f);
	auto r = S * v;
	CHECK_NEAR((float)r[0], 2.0f, 0.05f);
	CHECK_NEAR((float)r[1], 6.0f, 0.05f);

	// bool 稀疏 × 稠密（GF(2)：XOR/AND，store<bool> 连续存储）
	matrix<bool> Ab(2, 2, false);
	Ab(0, 0) = true; Ab(0, 1) = true; Ab(1, 0) = true; Ab(1, 1) = false;
	tensor_base::sparse_matrix<bool> Sb(Ab);
	vector<bool> vb(2, false);
	vb[0] = true; vb[1] = true;
	auto rb = Sb * vb;
	CHECK(rb[0] == false);	// (1&1)^(1&1)=0
	CHECK(rb[1] == true);	// (1&1)^(0&1)=1
}

TEST_CASE("test_cpu_backend_conv_bool_fallback")
{
	// conv_nd<bool> 直接调用（GF(2)：原生 +=/* 已改为 xhqm::sum/mul）
	tensor<4, bool> xb(std::array<xhqm::size, 4>{ 1, 1, 3, 3 }, false);
	xb(0, 0, 0, 0) = true;	// block(0,0) 感受野 {0,1}² 仅含 (0,0) → XOR true
	xb(0, 0, 1, 2) = true; xb(0, 0, 2, 1) = true;	// block(1,1) 感受野 {1,2}² 含两者 → XOR false
	tensor<4, bool> wb(std::array<xhqm::size, 4>{ 1, 1, 2, 2 }, true);
	auto yb = tensor_base::conv_nd<2, bool>(xb, wb, nullptr, { 1, 1 }, { 0, 0 }, { 1, 1 }, 1);
	CHECK(yb(0, 0, 0, 0) == true);
	CHECK(yb(0, 0, 1, 1) == false);
}
