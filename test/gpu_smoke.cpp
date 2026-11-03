// GPU 卷积冒烟测试：链接 xhqm.dll，验证 conv1d/2d/3d/conv1d_full/xcorr/conv2d_full
// 在 GPU（CUDA）上的结果与 CPU（no 后端，走 conv_nd 串行桩）一致。
#include "../algebra/tensor/tensor.h"
#include "../algebra/tensor/signal.h"
#include <cstdio>
#include <cmath>
#include <array>
using namespace xhqm;

static int failures = 0;
static void check(const char* name, double cpu, double gpu)
{
	if (std::fabs(cpu - gpu) > 1e-3)
	{
		++failures;
		printf("FAIL %s: cpu=%f gpu=%f\n", name, cpu, gpu);
	}
}

int main()
{
	// conv2d：3x3 输入，2x2 全 1 核
	tensor<4, f32> x2(std::array<xhqm::size, 4>{ 1, 1, 3, 3 }, 0.0f);
	for (xhqm::size i = 0; i < 3; ++i) for (xhqm::size j = 0; j < 3; ++j) x2(0, 0, i, j) = f32(i * 3 + j + 1);
	tensor<4, f32> w2(std::array<xhqm::size, 4>{ 1, 1, 2, 2 }, 1.0f);
	tensor_base::tensor_backend_select(tensor_base::device::no);
	auto y2_cpu = tensor_base::conv2d(x2, w2);
	tensor_base::tensor_backend_select(tensor_base::device::gpu);
	printf("backend = %s\n", tensor_base::tensor_backend_active().name());
	auto y2_gpu = tensor_base::conv2d(x2, w2);
	for (xhqm::size i = 0; i < 2; ++i) for (xhqm::size j = 0; j < 2; ++j) check("conv2d", y2_cpu(0, 0, i, j), y2_gpu(0, 0, i, j));

	// conv1d
	tensor<3, f32> x1(std::array<xhqm::size, 3>{ 1, 1, 4 }, 0.0f);
	for (xhqm::size i = 0; i < 4; ++i) x1(0, 0, i) = f32(i + 1);
	tensor<3, f32> w1(std::array<xhqm::size, 3>{ 1, 1, 3 }, 1.0f);
	tensor_base::tensor_backend_select(tensor_base::device::no);
	auto y1_cpu = tensor_base::conv1d(x1, w1);
	tensor_base::tensor_backend_select(tensor_base::device::gpu);
	auto y1_gpu = tensor_base::conv1d(x1, w1);
	check("conv1d[0]", y1_cpu(0, 0, 0), y1_gpu(0, 0, 0));
	check("conv1d[1]", y1_cpu(0, 0, 1), y1_gpu(0, 0, 1));

	// conv3d
	tensor<5, f32> x3(std::array<xhqm::size, 5>{ 1, 1, 2, 2, 2 }, 0.0f);
	for (xhqm::size i = 0; i < 8; ++i) x3[i] = f32(i);
	tensor<5, f32> w3(std::array<xhqm::size, 5>{ 1, 1, 1, 1, 1 }, 2.0f);
	tensor_base::tensor_backend_select(tensor_base::device::no);
	auto y3_cpu = tensor_base::conv3d(x3, w3);
	tensor_base::tensor_backend_select(tensor_base::device::gpu);
	auto y3_gpu = tensor_base::conv3d(x3, w3);
	for (xhqm::size i = 0; i < 8; ++i) check("conv3d", y3_cpu[i], y3_gpu[i]);

	// conv1d_full（翻转核）
	tensor<1, f32> a1(std::array<xhqm::size, 1>{ 2 }, 0.0f); a1[0] = 1; a1[1] = 2;
	tensor<1, f32> b1(std::array<xhqm::size, 1>{ 2 }, 0.0f); b1[0] = 3; b1[1] = 4;
	tensor_base::tensor_backend_select(tensor_base::device::no);
	auto cf_cpu = tensor_base::conv1d_full(a1, b1);
	tensor_base::tensor_backend_select(tensor_base::device::gpu);
	auto cf_gpu = tensor_base::conv1d_full(a1, b1);
	for (xhqm::size i = 0; i < 3; ++i) check("conv1d_full", cf_cpu[i], cf_gpu[i]);

	// xcorr（不翻核）
	tensor<1, f32> xa(std::array<xhqm::size, 1>{ 3 }, 0.0f); xa[0] = 1; xa[1] = 2; xa[2] = 3;
	tensor<1, f32> xb(std::array<xhqm::size, 1>{ 3 }, 0.0f); xb[0] = 1; xb[2] = 1;
	tensor_base::tensor_backend_select(tensor_base::device::no);
	auto xc_cpu = tensor_base::xcorr(xa, xb);
	tensor_base::tensor_backend_select(tensor_base::device::gpu);
	auto xc_gpu = tensor_base::xcorr(xa, xb);
	for (xhqm::size i = 0; i < 5; ++i) check("xcorr", xc_cpu[i], xc_gpu[i]);

	// conv2d_full（二维翻转核）
	tensor<2, f32> m1(std::array<xhqm::size, 2>{ 2, 2 }, 0.0f); m1(0, 0) = 1; m1(0, 1) = 2; m1(1, 0) = 3; m1(1, 1) = 4;
	tensor<2, f32> m2(std::array<xhqm::size, 2>{ 2, 2 }, 1.0f);
	tensor_base::tensor_backend_select(tensor_base::device::no);
	auto c2f_cpu = tensor_base::conv2d_full(m1, m2);
	tensor_base::tensor_backend_select(tensor_base::device::gpu);
	auto c2f_gpu = tensor_base::conv2d_full(m1, m2);
	for (xhqm::size i = 0; i < 3; ++i) for (xhqm::size j = 0; j < 3; ++j) check("conv2d_full", c2f_cpu(i, j), c2f_gpu(i, j));

	// conv4d：2x2x2x2 输入，2x2x2x2 全 1 核 -> 1x1x1x1（块和 1..16 = 136）
	tensor<6, f32> x4(std::array<xhqm::size, 6>{ 1, 1, 2, 2, 2, 2 }, 0.0f);
	for (xhqm::size i = 0; i < 16; ++i) x4[i] = f32(i + 1);
	tensor<6, f32> w4(std::array<xhqm::size, 6>{ 1, 1, 2, 2, 2, 2 }, 1.0f);
	tensor_base::tensor_backend_select(tensor_base::device::no);
	auto y4_cpu = tensor_base::conv4d(x4, w4);
	tensor_base::tensor_backend_select(tensor_base::device::gpu);
	auto y4_gpu = tensor_base::conv4d(x4, w4);
	check("conv4d", y4_cpu(0, 0, 0, 0, 0, 0), y4_gpu(0, 0, 0, 0, 0, 0));

	// trsv_lower（非单位对角）：L=[[1,0],[2,1]]，b=[1,5] -> x=[1,3]
	tensor<2, f32> L(std::array<xhqm::size, 2>{ 2, 2 }, 0.0f);
	L(0, 0) = 1; L(1, 0) = 2; L(1, 1) = 1;
	tensor<1, f32> b(std::array<xhqm::size, 1>{ 2 }, 0.0f); b[0] = 1; b[1] = 5;
	tensor_base::tensor_backend_select(tensor_base::device::no);
	auto tl_cpu = tensor_base::trsv_lower(L, b, false);
	tensor_base::tensor_backend_select(tensor_base::device::gpu);
	auto tl_gpu = tensor_base::trsv_lower(L, b, false);
	check("trsv_lower[0]", tl_cpu[0], tl_gpu[0]);
	check("trsv_lower[1]", tl_cpu[1], tl_gpu[1]);

	// trsv_upper（非单位对角）：U=[[2,1],[0,1]]，b=[5,3] -> x=[1,3]
	tensor<2, f32> U(std::array<xhqm::size, 2>{ 2, 2 }, 0.0f);
	U(0, 0) = 2; U(0, 1) = 1; U(1, 1) = 1;
	tensor<1, f32> bu(std::array<xhqm::size, 1>{ 2 }, 0.0f); bu[0] = 5; bu[1] = 3;
	tensor_base::tensor_backend_select(tensor_base::device::no);
	auto tu_cpu = tensor_base::trsv_upper(U, bu, false);
	tensor_base::tensor_backend_select(tensor_base::device::gpu);
	auto tu_gpu = tensor_base::trsv_upper(U, bu, false);
	check("trsv_upper[0]", tu_cpu[0], tu_gpu[0]);
	check("trsv_upper[1]", tu_cpu[1], tu_gpu[1]);

	// chol：A=[[4,2],[2,3]] -> L=[[2,0],[1,√2]]
	tensor<2, f32> A(std::array<xhqm::size, 2>{ 2, 2 }, 0.0f);
	A(0, 0) = 4; A(0, 1) = 2; A(1, 0) = 2; A(1, 1) = 3;
	tensor_base::tensor_backend_select(tensor_base::device::no);
	auto ch_cpu = tensor_base::chol(A);
	tensor_base::tensor_backend_select(tensor_base::device::gpu);
	auto ch_gpu = tensor_base::chol(A);
	check("chol[0][0]", ch_cpu(0, 0), ch_gpu(0, 0));
	check("chol[1][0]", ch_cpu(1, 0), ch_gpu(1, 0));
	check("chol[1][1]", ch_cpu(1, 1), ch_gpu(1, 1));

	// f16 conv2d：3x3 × 2x2 全 1 -> 2x2 块和（小整数在 f16 中精确）
	tensor<4, f16> xh(std::array<xhqm::size, 4>{ 1, 1, 3, 3 }, f16(0.0f));
	for (xhqm::size i = 0; i < 3; ++i)
		for (xhqm::size j = 0; j < 3; ++j)
			xh(0, 0, i, j) = f16((float)(i * 3 + j + 1));
	tensor<4, f16> wh(std::array<xhqm::size, 4>{ 1, 1, 2, 2 }, f16(1.0f));
	tensor_base::tensor_backend_select(tensor_base::device::no);
	auto yh_cpu = tensor_base::conv2d(xh, wh);
	tensor_base::tensor_backend_select(tensor_base::device::gpu);
	auto yh_gpu = tensor_base::conv2d(xh, wh);
	check("conv2d_f16[0][0]", (float)yh_cpu(0, 0, 0, 0), (float)yh_gpu(0, 0, 0, 0));
	check("conv2d_f16[1][1]", (float)yh_cpu(0, 0, 1, 1), (float)yh_gpu(0, 0, 1, 1));

	// i32 matmul：[[1,2],[3,4]] × [[5,6],[7,8]] = [[19,22],[43,50]]
	tensor<2, i32> Ai(std::array<xhqm::size, 2>{ 2, 2 }, 0);
	Ai(0, 0) = 1; Ai(0, 1) = 2; Ai(1, 0) = 3; Ai(1, 1) = 4;
	tensor<2, i32> Bi(std::array<xhqm::size, 2>{ 2, 2 }, 0);
	Bi(0, 0) = 5; Bi(0, 1) = 6; Bi(1, 0) = 7; Bi(1, 1) = 8;
	tensor_base::tensor_backend_select(tensor_base::device::no);
	auto Ci_cpu = tensor_base::matmul(Ai, Bi);
	tensor_base::tensor_backend_select(tensor_base::device::gpu);
	auto Ci_gpu = tensor_base::matmul(Ai, Bi);
	check("matmul_i32[0][0]", Ci_cpu(0, 0), Ci_gpu(0, 0));
	check("matmul_i32[1][1]", Ci_cpu(1, 1), Ci_gpu(1, 1));

	// i32 elemwise add
	tensor<1, i32> ea(std::array<xhqm::size, 1>{ 8 }, 0);
	for (xhqm::size i = 0; i < 8; ++i) ea[i] = (i32)i;
	tensor_base::tensor_backend_select(tensor_base::device::no);
	auto ea_cpu = ea + ea;
	tensor_base::tensor_backend_select(tensor_base::device::gpu);
	auto ea_gpu = ea + ea;
	check("elemwise_i32[3]", ea_cpu[3], ea_gpu[3]);
	check("elemwise_i32[7]", ea_cpu[7], ea_gpu[7]);

	// i32 conv2d：3x3 × 2x2 全 1 -> 2x2 块和
	tensor<4, i32> xi(std::array<xhqm::size, 4>{ 1, 1, 3, 3 }, 0);
	for (xhqm::size i = 0; i < 3; ++i)
		for (xhqm::size j = 0; j < 3; ++j)
			xi(0, 0, i, j) = (i32)(i * 3 + j + 1);
	tensor<4, i32> wi(std::array<xhqm::size, 4>{ 1, 1, 2, 2 }, 1);
	tensor_base::tensor_backend_select(tensor_base::device::no);
	auto yi_cpu = tensor_base::conv2d(xi, wi);
	tensor_base::tensor_backend_select(tensor_base::device::gpu);
	auto yi_gpu = tensor_base::conv2d(xi, wi);
	check("conv2d_i32[0][0]", yi_cpu(0, 0, 0, 0), yi_gpu(0, 0, 0, 0));
	check("conv2d_i32[1][1]", yi_cpu(0, 0, 1, 1), yi_gpu(0, 0, 1, 1));

	// i16 matmul：[[1,2],[3,4]] × [[5,6],[7,8]] = [[19,22],[43,50]]
	tensor<2, i16> A16(std::array<xhqm::size, 2>{ 2, 2 }, 0);
	A16(0, 0) = 1; A16(0, 1) = 2; A16(1, 0) = 3; A16(1, 1) = 4;
	tensor<2, i16> B16(std::array<xhqm::size, 2>{ 2, 2 }, 0);
	B16(0, 0) = 5; B16(0, 1) = 6; B16(1, 0) = 7; B16(1, 1) = 8;
	tensor_base::tensor_backend_select(tensor_base::device::no);
	auto C16_cpu = tensor_base::matmul(A16, B16);
	tensor_base::tensor_backend_select(tensor_base::device::gpu);
	auto C16_gpu = tensor_base::matmul(A16, B16);
	check("matmul_i16[0][0]", C16_cpu(0, 0), C16_gpu(0, 0));
	check("matmul_i16[1][1]", C16_cpu(1, 1), C16_gpu(1, 1));

	// i8 elemwise add
	tensor<1, i8> e8(std::array<xhqm::size, 1>{ 16 }, 0);
	for (xhqm::size i = 0; i < 16; ++i) e8[i] = (i8)(i % 60);
	tensor_base::tensor_backend_select(tensor_base::device::no);
	auto e8_cpu = e8 + e8;
	tensor_base::tensor_backend_select(tensor_base::device::gpu);
	auto e8_gpu = e8 + e8;
	check("elemwise_i8[7]", e8_cpu[7], e8_gpu[7]);
	check("elemwise_i8[15]", e8_cpu[15], e8_gpu[15]);

	// i16 conv2d：3x3 × 2x2 全 1 -> 2x2 块和
	tensor<4, i16> x16(std::array<xhqm::size, 4>{ 1, 1, 3, 3 }, 0);
	for (xhqm::size i = 0; i < 3; ++i)
		for (xhqm::size j = 0; j < 3; ++j)
			x16(0, 0, i, j) = (i16)(i * 3 + j + 1);
	tensor<4, i16> w16(std::array<xhqm::size, 4>{ 1, 1, 2, 2 }, 1);
	tensor_base::tensor_backend_select(tensor_base::device::no);
	auto y16_cpu = tensor_base::conv2d(x16, w16);
	tensor_base::tensor_backend_select(tensor_base::device::gpu);
	auto y16_gpu = tensor_base::conv2d(x16, w16);
	check("conv2d_i16[0][0]", y16_cpu(0, 0, 0, 0), y16_gpu(0, 0, 0, 0));
	check("conv2d_i16[1][1]", y16_cpu(0, 0, 1, 1), y16_gpu(0, 0, 1, 1));

	// i16 spmv：稀疏 × 稠密（int32 临时缓冲累加 + 窄化）
	matrix<i16> S16(2, 2, (i16)0);
	S16(0, 0) = 2; S16(0, 1) = 0; S16(1, 0) = 0; S16(1, 1) = 3;
	tensor_base::sparse_matrix<i16> sp16(S16);
	vector<i16> v16(2, (i16)0); v16[0] = 1; v16[1] = 2;
	tensor_base::tensor_backend_select(tensor_base::device::no);
	auto r16_cpu = sp16 * v16;
	tensor_base::tensor_backend_select(tensor_base::device::gpu);
	auto r16_gpu = sp16 * v16;
	check("spmv_i16[0]", r16_cpu[0], r16_gpu[0]);
	check("spmv_i16[1]", r16_cpu[1], r16_gpu[1]);

	// i32 trsv_lower（非单位）：L=[[1,0],[2,1]]，b=[1,5] -> x=[1,3]
	tensor<2, i32> Li(std::array<xhqm::size, 2>{ 2, 2 }, 0);
	Li(0, 0) = 1; Li(1, 0) = 2; Li(1, 1) = 1;
	tensor<1, i32> bi(std::array<xhqm::size, 1>{ 2 }, 0); bi[0] = 1; bi[1] = 5;
	tensor_base::tensor_backend_select(tensor_base::device::no);
	auto xi_cpu = tensor_base::trsv_lower(Li, bi, false);
	tensor_base::tensor_backend_select(tensor_base::device::gpu);
	auto xi_gpu = tensor_base::trsv_lower(Li, bi, false);
	check("trsv_i32[0]", xi_cpu[0], xi_gpu[0]);
	check("trsv_i32[1]", xi_cpu[1], xi_gpu[1]);

	if (failures == 0) printf("GPU conv smoke: ALL PASS\n");
	else printf("GPU conv smoke: %d FAILURES\n", failures);
	return failures == 0 ? 0 : 1;
}
