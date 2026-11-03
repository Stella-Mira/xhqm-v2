// 张量模块串行算子全量测试（no 后端 + header-only 回退路径）。
// 覆盖：tensor 逐元素/广播/视图、linalg 归约/分解/矩阵函数、sparse/signal/stats/poly 串行桩，
// 以及本批修复的回归用例（matmul 内维校验、空张量归约、eig_sym 前置校验、
// 就地广播 +=、视图参数校验、sparse 维度校验、prctile 钳位、deconv 零首项等）。
#include "../algebra/tensor/tensor.h"
#include "../algebra/tensor/signal.h"
#include "../algebra/tensor/stats.h"
#include "../algebra/tensor/poly.h"
#include "test.h"
#include <limits>
#include <array>
#include <cmath>

using namespace xhqm;

// ============================================================
// 修复回归：就地广播（B1）
// ============================================================
TEST_CASE("test_serial_broadcast_inplace")
{
	tensor<2, f64> a(std::array<xhqm::size, 2>{ 2, 3 }, 1.0);
	tensor<2, f64> b(std::array<xhqm::size, 2>{ 1, 3 }, 0.0);
	b(0, 0) = 10; b(0, 1) = 20; b(0, 2) = 30;
	a += b;
	CHECK_NEAR(a(0, 0), 11.0, 1e-12);
	CHECK_NEAR(a(1, 2), 31.0, 1e-12);
	// 减/乘/除就地广播
	tensor<2, f64> c(std::array<xhqm::size, 2>{ 2, 2 }, 10.0);
	tensor<2, f64> d(std::array<xhqm::size, 2>{ 1, 2 }, 0.0);
	d(0, 0) = 2; d(0, 1) = 5;
	c -= d;
	CHECK_NEAR(c(0, 0), 8.0, 1e-12);
	CHECK_NEAR(c(1, 1), 5.0, 1e-12);
	c *= d;
	CHECK_NEAR(c(0, 0), 16.0, 1e-12);
	c /= d;
	CHECK_NEAR(c(0, 0), 8.0, 1e-12);
	// 结果形状无法容纳时保持原样（不可广播到自身）
	tensor<2, f64> e(std::array<xhqm::size, 2>{ 1, 3 }, 1.0);
	tensor<2, f64> f(std::array<xhqm::size, 2>{ 2, 3 }, 5.0);
	e += f;
	CHECK_NEAR(e(0, 0), 1.0, 1e-12);
}

// ============================================================
// 修复回归：tensor_view 参数校验 + 零拷贝语义
// ============================================================
TEST_CASE("test_serial_tensor_view")
{
	tensor<2, f64> t(std::array<xhqm::size, 2>{ 2, 3 }, 0.0);
	for (xhqm::size i = 0; i < 2; ++i)
		for (xhqm::size j = 0; j < 3; ++j)
			t(i, j) = f64(i * 3 + j + 1);
	tensor_view<2, f64> tv(t);
	auto tt = tv.transpose(0, 1);
	CHECK_NEAR(tt.at({ 1, 0 }), t(0, 1), 1e-12);
	auto sv = tv.slice<1>(2);
	CHECK_NEAR(sv.at({ 1 }), t(1, 2), 1e-12);
	tensor<2, f64> bc(std::array<xhqm::size, 2>{ 2, 1 }, 7.0);
	tensor_view<2, f64> bcv(bc);
	auto bv = bcv.broadcast({ 2, 3 });
	CHECK_NEAR(bv.at({ 1, 2 }), 7.0, 1e-12);
	// 参数越界抛错
	CHECK_THROWS_AS(tv.transpose(0, 5), xhqm::error);
	CHECK_THROWS_AS(tv.broadcast({ 3, 4 }), xhqm::error);
	CHECK_THROWS_AS(tv.slice<0>(9), xhqm::error);
}

// ============================================================
// 修复回归：各算子前置校验
// ============================================================
TEST_CASE("test_serial_guards")
{
	// matmul 内维不匹配返回空
	tensor<2, f64> A(std::array<xhqm::size, 2>{ 2, 2 }, 1.0);
	tensor<2, f64> B(std::array<xhqm::size, 2>{ 3, 2 }, 1.0);
	CHECK(tensor_base::matmul(A, B).size() == 0);

	// reduce_last 空张量不除零（1D 末轴）
	tensor<1, f64> empty1(std::array<xhqm::size, 1>{ 0 }, 0.0);
	auto rs1 = tensor_base::reduce_sum(empty1, 0);
	CHECK(rs1.size() == 1);
	CHECK_NEAR(rs1.value(0), 0.0, 1e-12);
	// 2D 末轴空
	tensor<2, f64> empty2(std::array<xhqm::size, 2>{ 2, 0 }, 0.0);
	auto rs2 = tensor_base::reduce_sum(empty2, 1);
	CHECK(rs2.size() == 2);

	// eig_sym 空 / 非方阵 / 1×1
	CHECK(tensor_base::eig_sym(tensor<2, f64>()).values.size() == 0);
	tensor<2, f64> non_sq(std::array<xhqm::size, 2>{ 2, 3 }, 0.0);
	CHECK(tensor_base::eig_sym(non_sq).values.size() == 0);
	tensor<2, f64> one(std::array<xhqm::size, 2>{ 1, 1 }, 5.0);
	auto e1 = tensor_base::eig_sym(one);
	CHECK_NEAR(e1.values[0], 5.0, 1e-12);
	CHECK_NEAR(e1.vectors(0, 0), 1.0, 1e-12);

	// sparse 维度不匹配
	matrix<f64> sd({ { 1.0, 0.0 }, { 0.0, 1.0 } });
	tensor_base::sparse_matrix<f64> sp(sd);
	CHECK((sp * vector<f64>({ 1.0 })).size() == 0);
	CHECK((sp * matrix<f64>(1, 2, 1.0)).size() == 0);

	// prctile 越界 p 钳位
	tensor<1, f64> pt(std::array<xhqm::size, 1>{ 3 }, 0.0);
	pt[0] = 1; pt[1] = 2; pt[2] = 3;
	CHECK_NEAR(tensor_base::prctile(pt, -10.0), 1.0, 1e-12);
	CHECK_NEAR(tensor_base::prctile(pt, 200.0), 3.0, 1e-12);

	// deconv b[0]==0 不除零
	tensor<1, f64> pa(std::array<xhqm::size, 1>{ 2 }, 0.0); pa[0] = 1; pa[1] = 1;
	tensor<1, f64> pb(std::array<xhqm::size, 1>{ 2 }, 0.0);
	auto dr = tensor_base::deconv(pa, pb);
	CHECK(dr.q.size() == 1);
	CHECK_NEAR(dr.q[0], 0.0, 1e-12);

	// complex fill 系列无未初始化读
	complex<f64> cmn(0.0, 0.0); xhqm::min(cmn);
	complex<f64> cmx(0.0, 0.0); xhqm::max(cmx);
	CHECK(cmn.real() <= cmx.real());
	CHECK(xhqm::ismin(cmn));
	CHECK(xhqm::ismax(cmx));
}

// ============================================================
// linalg 归约 / 扫描 / 范数 / 矩阵函数（串行桩）
// ============================================================
TEST_CASE("test_serial_linalg_reduce")
{
	tensor<2, f64> M(std::array<xhqm::size, 2>{ 2, 3 }, 0.0);
	M(0, 0) = 1; M(0, 1) = 2; M(0, 2) = 3; M(1, 0) = 4; M(1, 1) = 5; M(1, 2) = 6;

	auto rsum0 = tensor_base::reduce_sum(M, 0);
	CHECK_NEAR(rsum0[0], 5.0, 1e-12);
	CHECK_NEAR(rsum0[2], 9.0, 1e-12);
	auto rsum1 = tensor_base::reduce_sum(M, 1);
	CHECK_NEAR(rsum1[0], 6.0, 1e-12);
	CHECK_NEAR(rsum1[1], 15.0, 1e-12);

	tensor<2, f64> N(std::array<xhqm::size, 2>{ 2, 2 }, 0.0);
	N(0, 0) = 1; N(0, 1) = 5; N(1, 0) = 3; N(1, 1) = 2;
	auto rmax1 = tensor_base::reduce_max(N, 1);
	CHECK_NEAR(rmax1[0], 5.0, 1e-12);
	CHECK_NEAR(rmax1[1], 3.0, 1e-12);
	auto rmin0 = tensor_base::reduce_min(N, 0);
	CHECK_NEAR(rmin0[0], 1.0, 1e-12);
	CHECK_NEAR(rmin0[1], 2.0, 1e-12);

	tensor<2, f64> P(std::array<xhqm::size, 2>{ 2, 2 }, 0.0);
	P(0, 0) = 2; P(0, 1) = 3; P(1, 0) = 4; P(1, 1) = 5;
	auto rprod1 = tensor_base::reduce_prod(P, 1);
	CHECK_NEAR(rprod1[0], 6.0, 1e-12);
	CHECK_NEAR(rprod1[1], 20.0, 1e-12);

	// 全元素归约
	CHECK_NEAR(tensor_base::sum_all(P), 14.0, 1e-12);
	CHECK_NEAR(tensor_base::prod_all(P), 120.0, 1e-12);
	CHECK_NEAR(tensor_base::max_all(P), 5.0, 1e-12);
	CHECK_NEAR(tensor_base::min_all(P), 2.0, 1e-12);
	CHECK(tensor_base::argmax_all(P) == 3);
	CHECK(tensor_base::argmin_all(P) == 0);

	// 沿轴 argmax/argmin
	auto amx1 = tensor_base::argmax(N, 1);
	CHECK((amx1[0] == 1 && amx1[1] == 0));
	auto amn0 = tensor_base::argmin(N, 0);
	CHECK((amn0[0] == 0 && amn0[1] == 1));

	// 前缀扫描
	auto cs0 = tensor_base::cumsum(M, 0);
	CHECK_NEAR(cs0(1, 0), 5.0, 1e-12);
	CHECK_NEAR(cs0(1, 2), 9.0, 1e-12);
	auto cs1 = tensor_base::cumsum(M, 1);
	CHECK_NEAR(cs1(0, 2), 6.0, 1e-12);
	CHECK_NEAR(cs1(1, 2), 15.0, 1e-12);
	auto cp1 = tensor_base::cumprod(M, 1);
	CHECK_NEAR(cp1(0, 2), 6.0, 1e-12);
	CHECK_NEAR(cp1(1, 2), 120.0, 1e-12);

	tensor<2, f64> Q(std::array<xhqm::size, 2>{ 2, 3 }, 0.0);
	Q(0, 0) = 1; Q(0, 1) = 3; Q(0, 2) = 2; Q(1, 0) = 4; Q(1, 1) = 1; Q(1, 2) = 5;
	auto cmx = tensor_base::cummax_last(Q);
	CHECK_NEAR(cmx(0, 2), 3.0, 1e-12);
	CHECK_NEAR(cmx(1, 2), 5.0, 1e-12);
	auto cmn = tensor_base::cummin_last(Q);
	CHECK_NEAR(cmn(0, 1), 1.0, 1e-12);
	CHECK_NEAR(cmn(1, 2), 1.0, 1e-12);
}

TEST_CASE("test_serial_linalg_matrix_funcs")
{
	// identity / trace
	auto I = tensor_base::identity<f64>(3);
	CHECK_NEAR(tensor_base::trace(I), 3.0, 1e-12);
	tensor<2, f64> T(std::array<xhqm::size, 2>{ 2, 2 }, 0.0);
	T(0, 0) = 1; T(0, 1) = 2; T(1, 0) = 3; T(1, 1) = 4;
	CHECK_NEAR(tensor_base::trace(T), 5.0, 1e-12);

	// 范数
	tensor<2, f64> R(std::array<xhqm::size, 2>{ 1, 2 }, 0.0);
	R(0, 0) = 3; R(0, 1) = 4;
	CHECK_NEAR(tensor_base::norm_fro(R), 5.0, 1e-12);
	tensor<2, f64> N(std::array<xhqm::size, 2>{ 2, 2 }, 0.0);
	N(0, 0) = 1; N(0, 1) = -2; N(1, 0) = 3; N(1, 1) = 4;
	CHECK_NEAR(tensor_base::norm_one(N), 6.0, 1e-12);
	CHECK_NEAR(tensor_base::norm_inf(N), 7.0, 1e-12);
	tensor<2, f64> D(std::array<xhqm::size, 2>{ 2, 2 }, 0.0);
	D(0, 0) = 3; D(1, 1) = 2;
	CHECK_NEAR(tensor_base::norm_spectral(D), 3.0, 1e-9);

	// inv
	tensor<2, f64> A(std::array<xhqm::size, 2>{ 2, 2 }, 0.0);
	A(0, 0) = 4; A(0, 1) = 7; A(1, 0) = 2; A(1, 1) = 6;
	auto Ai = tensor_base::inv(A);
	CHECK_NEAR(Ai(0, 0), 0.6, 1e-12);
	CHECK_NEAR(Ai(0, 1), -0.7, 1e-12);
	CHECK_NEAR(Ai(1, 1), 0.4, 1e-12);

	// rref
	tensor<2, f64> G(std::array<xhqm::size, 2>{ 2, 3 }, 0.0);
	G(0, 0) = 1; G(0, 1) = 2; G(0, 2) = 3; G(1, 0) = 4; G(1, 1) = 5; G(1, 2) = 6;
	auto Gg = tensor_base::rref(G);
	CHECK_NEAR(Gg(0, 2), -1.0, 1e-9);
	CHECK_NEAR(Gg(1, 2), 2.0, 1e-9);

	// expm / sqrtm / logm
	tensor<2, f64> Z(std::array<xhqm::size, 2>{ 2, 2 }, 0.0);
	auto eZ = tensor_base::expm(Z);
	CHECK_NEAR(eZ(0, 0), 1.0, 1e-9);
	CHECK_NEAR(eZ(0, 1), 0.0, 1e-9);
	tensor<2, f64> S(std::array<xhqm::size, 2>{ 2, 2 }, 0.0);
	S(0, 0) = 4; S(1, 1) = 9;
	auto sS = tensor_base::sqrtm(S);
	CHECK_NEAR(sS(0, 0), 2.0, 1e-9);
	CHECK_NEAR(sS(1, 1), 3.0, 1e-9);
	auto lI = tensor_base::logm(tensor_base::identity<f64>(2));
	CHECK_NEAR(lI(0, 0), 0.0, 1e-9);
	CHECK_NEAR(lI(0, 1), 0.0, 1e-9);
}

// ============================================================
// vector / matrix 串行代数
// ============================================================
TEST_CASE("test_serial_matrix_vector")
{
	matrix<f64> A({ { 1, 2 }, { 3, 4 } });
	vector<f64> v({ 1, 1 });
	auto r = A * v;	// 矩阵乘向量
	CHECK_NEAR(r[0], 3.0, 1e-12);
	CHECK_NEAR(r[1], 7.0, 1e-12);
	auto At = A.transpose();
	CHECK_NEAR(At(0, 1), 3.0, 1e-12);
	CHECK_NEAR(A.det(), -2.0, 1e-12);
}

// ============================================================
// sparse 串行桩（Sp×稠密 / Sp×Sp 未在 algebra_test 覆盖）
// ============================================================
TEST_CASE("test_serial_sparse_ops")
{
	matrix<f64> S1({ { 1.0, 0.0 }, { 0.0, 2.0 } });
	tensor_base::sparse_matrix<f64> sp1(S1);
	matrix<f64> M({ { 1.0, 2.0 }, { 3.0, 4.0 } });
	auto sm = sp1 * M;	// 稀疏 × 稠密矩阵
	CHECK_NEAR(sm(0, 0), 1.0, 1e-12);
	CHECK_NEAR(sm(1, 1), 8.0, 1e-12);

	matrix<f64> S2({ { 4.0, 0.0 }, { 0.0, 5.0 } });
	tensor_base::sparse_matrix<f64> sp2(S2);
	auto ss = sp1 * sp2;	// 稀疏 × 稀疏
	CHECK(ss.nnz() == 2);
	auto ssd = ss.dense();
	CHECK_NEAR(ssd(0, 0), 4.0, 1e-12);
	CHECK_NEAR(ssd(1, 1), 10.0, 1e-12);
}

// ============================================================
// signal 串行桩
// ============================================================
TEST_CASE("test_serial_signal")
{
	// FFT / IFFT 往返（radix-2）
	tensor<1, xhqm::complex<f64>> x(std::array<xhqm::size, 1>{ 4 }, xhqm::complex<f64>(0, 0));
	x[0] = xhqm::complex<f64>(1, 0);	// 冲激
	auto X = tensor_base::fft(x);
	for (xhqm::size i = 0; i < 4; ++i)
		CHECK_NEAR(X[i].real(), 1.0, 1e-9);
	auto back = tensor_base::ifft(X);
	CHECK_NEAR(back[0].real(), 1.0, 1e-9);
	CHECK_NEAR(back[1].real(), 0.0, 1e-9);

	// fftshift / ifftshift
	tensor<1, xhqm::complex<f64>> xs(std::array<xhqm::size, 1>{ 4 }, xhqm::complex<f64>(0, 0));
	for (xhqm::size i = 0; i < 4; ++i) xs[i] = xhqm::complex<f64>(f64(i + 1), 0);
	auto sf = tensor_base::fftshift(xs);
	CHECK_NEAR(sf[0].real(), 3.0, 1e-12);
	CHECK_NEAR(sf[1].real(), 4.0, 1e-12);
	auto ib = tensor_base::ifftshift(sf);
	CHECK_NEAR(ib[0].real(), 1.0, 1e-12);
	CHECK_NEAR(ib[3].real(), 4.0, 1e-12);

	// interp1 线性插值
	tensor<1, f64> ix(std::array<xhqm::size, 1>{ 3 }, 0.0); ix[0] = 0; ix[1] = 1; ix[2] = 2;
	tensor<1, f64> iy(std::array<xhqm::size, 1>{ 3 }, 0.0); iy[0] = 0; iy[1] = 10; iy[2] = 20;
	tensor<1, f64> ixi(std::array<xhqm::size, 1>{ 2 }, 0.0); ixi[0] = 0.5; ixi[1] = 1.5;
	auto iyi = tensor_base::interp1(ix, iy, ixi);
	CHECK_NEAR(iyi[0], 5.0, 1e-12);
	CHECK_NEAR(iyi[1], 15.0, 1e-12);

	// trapz / cumtrapz
	tensor<1, f64> ty(std::array<xhqm::size, 1>{ 3 }, 0.0); ty[0] = 1; ty[1] = 2; ty[2] = 3;
	CHECK_NEAR(tensor_base::trapz(ty), 4.0, 1e-12);
	tensor<1, f64> tx(std::array<xhqm::size, 1>{ 3 }, 0.0); tx[0] = 0; tx[1] = 1; tx[2] = 2;
	auto ct = tensor_base::cumtrapz(ty, tx);
	CHECK_NEAR(ct[1], 1.5, 1e-12);
	CHECK_NEAR(ct[2], 4.0, 1e-12);

	// filter（IIR）
	tensor<1, f64> fb(std::array<xhqm::size, 1>{ 1 }, 0.0); fb[0] = 1;
	tensor<1, f64> fa(std::array<xhqm::size, 1>{ 2 }, 0.0); fa[0] = 1; fa[1] = -0.5;
	tensor<1, f64> fx(std::array<xhqm::size, 1>{ 4 }, 0.0); fx[0] = 1;
	auto fy = tensor_base::filter(fb, fa, fx);
	CHECK_NEAR(fy[0], 1.0, 1e-12);
	CHECK_NEAR(fy[3], 0.125, 1e-12);

	// conv1d_full / xcorr / conv2d_full
	tensor<1, f64> a1(std::array<xhqm::size, 1>{ 2 }, 0.0); a1[0] = 1; a1[1] = 2;
	tensor<1, f64> b1(std::array<xhqm::size, 1>{ 2 }, 0.0); b1[0] = 3; b1[1] = 4;
	auto c1 = tensor_base::conv1d_full(a1, b1);
	CHECK(c1.size() == 3);
	CHECK_NEAR(c1[1], 10.0, 1e-12);
	CHECK_NEAR(c1[2], 8.0, 1e-12);

	tensor<1, f64> xa(std::array<xhqm::size, 1>{ 3 }, 0.0); xa[0] = 1; xa[1] = 2; xa[2] = 3;
	tensor<1, f64> xb(std::array<xhqm::size, 1>{ 3 }, 0.0); xb[0] = 1; xb[2] = 1;
	auto xc = tensor_base::xcorr(xa, xb);
	CHECK(xc.size() == 5);
	CHECK_NEAR(xc[2], 4.0, 1e-12);
	CHECK_NEAR(xc[4], 3.0, 1e-12);

	tensor<2, f64> m1(std::array<xhqm::size, 2>{ 2, 2 }, 0.0);
	m1(0, 0) = 1; m1(0, 1) = 2; m1(1, 0) = 3; m1(1, 1) = 4;
	tensor<2, f64> m2(std::array<xhqm::size, 2>{ 2, 2 }, 1.0);
	auto m3 = tensor_base::conv2d_full(m1, m2);
	CHECK_NEAR(m3(0, 0), 1.0, 1e-12);
	CHECK_NEAR(m3(1, 1), 10.0, 1e-12);
}

// ============================================================
// stats 串行桩
// ============================================================
TEST_CASE("test_serial_stats")
{
	// var / stddev（全元素）
	tensor<1, f64> v(std::array<xhqm::size, 1>{ 4 }, 0.0);
	v[0] = 1; v[1] = 2; v[2] = 3; v[3] = 4;
	CHECK_NEAR(tensor_base::var(v), 5.0 / 3.0, 1e-12);
	CHECK_NEAR(tensor_base::stddev(v), std::sqrt(5.0 / 3.0), 1e-12);
	CHECK_NEAR(tensor_base::var(v, false), 1.25, 1e-12);

	// var（沿轴）
	tensor<2, f64> M(std::array<xhqm::size, 2>{ 3, 2 }, 0.0);
	M(0, 0) = 1; M(0, 1) = 2; M(1, 0) = 3; M(1, 1) = 4; M(2, 0) = 5; M(2, 1) = 6;
	auto va = tensor_base::var(M, xhqm::size(0));
	CHECK_NEAR(va[0], 4.0, 1e-12);
	CHECK_NEAR(va[1], 4.0, 1e-12);

	// cov（n 观测 × p 变量）
	auto cv = tensor_base::cov(M);
	CHECK_NEAR(cv(0, 0), 4.0, 1e-12);
	CHECK_NEAR(cv(0, 1), 4.0, 1e-12);

	// diff / gradient
	tensor<1, f64> d1(std::array<xhqm::size, 1>{ 3 }, 0.0); d1[0] = 1; d1[1] = 3; d1[2] = 6;
	auto df = tensor_base::diff(d1);
	CHECK_NEAR(df[0], 2.0, 1e-12);
	CHECK_NEAR(df[1], 3.0, 1e-12);
	tensor<1, f64> g1(std::array<xhqm::size, 1>{ 3 }, 0.0); g1[0] = 1; g1[1] = 2; g1[2] = 4;
	auto gd = tensor_base::gradient(g1);
	CHECK_NEAR(gd[0], 1.0, 1e-12);
	CHECK_NEAR(gd[1], 1.5, 1e-12);
	CHECK_NEAR(gd[2], 2.0, 1e-12);

	// median / mode / prctile / quantile / iqr
	tensor<1, f64> s1(std::array<xhqm::size, 1>{ 3 }, 0.0); s1[0] = 3; s1[1] = 1; s1[2] = 2;
	CHECK_NEAR(tensor_base::median(s1), 2.0, 1e-12);
	tensor<1, f64> s2(std::array<xhqm::size, 1>{ 4 }, 0.0); s2[0] = 1; s2[1] = 2; s2[2] = 3; s2[3] = 4;
	CHECK_NEAR(tensor_base::median(s2), 2.5, 1e-12);
	tensor<1, f64> md(std::array<xhqm::size, 1>{ 6 }, 0.0); md[0] = 1; md[1] = 2; md[2] = 2; md[3] = 3; md[4] = 3; md[5] = 3;
	CHECK_NEAR(tensor_base::mode(md), 3.0, 1e-12);
	tensor<1, f64> p(std::array<xhqm::size, 1>{ 5 }, 0.0);
	for (xhqm::size i = 0; i < 5; ++i) p[i] = f64(i * 10);
	CHECK_NEAR(tensor_base::prctile(p, 50.0), 20.0, 1e-12);
	CHECK_NEAR(tensor_base::prctile(p, 25.0), 10.0, 1e-12);
	CHECK_NEAR(tensor_base::prctile(p, 100.0), 40.0, 1e-12);
	CHECK_NEAR(tensor_base::quantile(p, 0.5), 20.0, 1e-12);
	CHECK_NEAR(tensor_base::iqr(p), 20.0, 1e-12);

	// NaN 鲁棒归约
	tensor<1, f64> nv(std::array<xhqm::size, 1>{ 3 }, 0.0);
	nv[0] = 1; nv[1] = std::nan(""); nv[2] = 2;
	CHECK_NEAR(tensor_base::nansum(nv), 3.0, 1e-12);
	CHECK_NEAR(tensor_base::nanmean(nv), 1.5, 1e-12);
	CHECK_NEAR(tensor_base::nanmax(nv), 2.0, 1e-12);
	CHECK_NEAR(tensor_base::nanmin(nv), 1.0, 1e-12);

	// unique / sortrows / 集合
	tensor<1, f64> u(std::array<xhqm::size, 1>{ 4 }, 0.0); u[0] = 3; u[1] = 1; u[2] = 2; u[3] = 1;
	auto un = tensor_base::unique(u);
	CHECK(un.size() == 3);
	CHECK_NEAR(un[0], 1.0, 1e-12);
	CHECK_NEAR(un[2], 3.0, 1e-12);

	tensor<2, f64> sr(std::array<xhqm::size, 2>{ 3, 2 }, 0.0);
	sr(0, 0) = 2; sr(0, 1) = 1; sr(1, 0) = 1; sr(1, 1) = 3; sr(2, 0) = 1; sr(2, 1) = 2;
	auto srs = tensor_base::sortrows(sr);
	CHECK_NEAR(srs(0, 0), 1.0, 1e-12);
	CHECK_NEAR(srs(0, 1), 2.0, 1e-12);
	CHECK_NEAR(srs(2, 0), 2.0, 1e-12);

	tensor<1, f64> seta(std::array<xhqm::size, 1>{ 3 }, 0.0); seta[0] = 1; seta[1] = 2; seta[2] = 3;
	tensor<1, f64> setb(std::array<xhqm::size, 1>{ 3 }, 0.0); setb[0] = 2; setb[1] = 3; setb[2] = 4;
	auto its = tensor_base::intersect(seta, setb);
	CHECK(its.size() == 2);
	CHECK_NEAR(its[0], 2.0, 1e-12);
	auto uni = tensor_base::setunion(seta, setb);
	CHECK(uni.size() == 4);
	auto sdf = tensor_base::setdiff(seta, tensor<1, f64>(std::array<xhqm::size, 1>{ 1 }, 2.0));
	CHECK(sdf.size() == 2);
	CHECK_NEAR(sdf[0], 1.0, 1e-12);
	CHECK(tensor_base::ismember(2.0, seta));
	CHECK(!tensor_base::ismember(9.0, seta));
}

// ============================================================
// poly 串行桩
// ============================================================
TEST_CASE("test_serial_poly")
{
	tensor<1, f64> p(std::array<xhqm::size, 1>{ 3 }, 0.0); p[0] = 2; p[1] = 3; p[2] = 4;	// 2x²+3x+4
	CHECK_NEAR(tensor_base::polyval(p, 1.0), 9.0, 1e-12);
	tensor<1, f64> px(std::array<xhqm::size, 1>{ 2 }, 0.0); px[0] = 0; px[1] = 1;
	auto pv = tensor_base::polyval(p, px);
	CHECK_NEAR(pv[0], 4.0, 1e-12);
	CHECK_NEAR(pv[1], 9.0, 1e-12);

	// deconv：x²+3x+2 ÷ (x+1) = x+2
	tensor<1, f64> a(std::array<xhqm::size, 1>{ 3 }, 0.0); a[0] = 1; a[1] = 3; a[2] = 2;
	tensor<1, f64> b(std::array<xhqm::size, 1>{ 2 }, 0.0); b[0] = 1; b[1] = 1;
	auto d = tensor_base::deconv(a, b);
	CHECK(d.q.size() == 2);
	CHECK_NEAR(d.q[0], 1.0, 1e-12);
	CHECK_NEAR(d.q[1], 2.0, 1e-12);
	CHECK_NEAR(d.r[0], 0.0, 1e-12);

	// polyder / polyint
	tensor<1, f64> pd(std::array<xhqm::size, 1>{ 4 }, 0.0); pd[0] = 1; pd[1] = 0; pd[2] = -4; pd[3] = 1;
	auto der = tensor_base::polyder(pd);
	CHECK(der.size() == 3);
	CHECK_NEAR(der[0], 3.0, 1e-12);
	CHECK_NEAR(der[2], -4.0, 1e-12);
	tensor<1, f64> pi(std::array<xhqm::size, 1>{ 3 }, 0.0); pi[0] = 3; pi[1] = 2; pi[2] = 1;
	auto itg = tensor_base::polyint(pi);
	CHECK(itg.size() == 4);
	CHECK_NEAR(itg[0], 1.0, 1e-12);
	CHECK_NEAR(itg[1], 1.0, 1e-12);
	CHECK_NEAR(itg[3], 0.0, 1e-12);
}

// ============================================================
// 整型（i8/i16/i32/i64）：非 f32/f64 走串行回退，整数除法向零截断
// ============================================================
template<class T>
static void check_int_arith()
{
	tensor<1, T> a(std::array<xhqm::size, 1>{ 4 }, T(0)); a[0] = T(7); a[1] = T(-7); a[2] = T(6); a[3] = T(-6);
	tensor<1, T> b(std::array<xhqm::size, 1>{ 4 }, T(2));
	auto q = a / b;	// 整数除法向零截断
	CHECK((int)q[0] == 3);
	CHECK((int)q[1] == -3);
	CHECK((int)q[2] == 3);
	CHECK((int)q[3] == -3);
	auto s = a + b;
	CHECK((int)s[0] == 9);
	CHECK((int)s[1] == -5);
	auto m = a * b;
	CHECK((int)m[0] == 14);
	CHECK((int)m[1] == -14);
	auto d = a - b;
	CHECK((int)d[1] == -9);
	auto rs = tensor_base::reduce_sum(a, 0);
	CHECK((int)rs.value(0) == 0);	// 7 + (-7) + 6 + (-6) = 0
}

TEST_CASE("test_serial_integer_types")
{
	check_int_arith<i8>();
	check_int_arith<i16>();
	check_int_arith<i32>();
	check_int_arith<i64>();

	// 矩阵乘 / 点积（i32 代表）
	matrix<i32> A({ { 1, 2 }, { 3, 4 } }), B({ { 5, 6 }, { 7, 8 } });
	auto C = A * B;
	CHECK(C(0, 0) == 19);
	CHECK(C(1, 1) == 50);
	vector<i32> v({ 1, 2, 3 }), w({ 4, 5, 6 });
	CHECK(v * w == 32);
	CHECK(v.dot(w) == 32);

	// 归约 max/min（numeric_limits 初值路径，i32 代表）
	tensor<2, i32> M(std::array<xhqm::size, 2>{ 2, 2 }, 0);
	M(0, 0) = 1; M(0, 1) = 5; M(1, 0) = 3; M(1, 1) = 2;
	auto rmax = tensor_base::reduce_max(M, 1);
	CHECK(rmax[0] == 5);
	CHECK(rmax[1] == 3);
	auto rmin = tensor_base::reduce_min(M, 0);
	CHECK(rmin[0] == 1);
	CHECK(rmin[1] == 2);
}

// ============================================================
// bool：GF(2) 布尔环（+/-=XOR，*//=AND）
// ============================================================
TEST_CASE("test_serial_bool_types")
{
	tensor<1, bool> a(std::array<xhqm::size, 1>{ 4 }, false);
	a[0] = true; a[1] = true; a[2] = false; a[3] = false;
	tensor<1, bool> b(std::array<xhqm::size, 1>{ 4 }, false);
	b[0] = true; b[1] = false; b[2] = true; b[3] = false;
	auto s = a + b;	// XOR
	CHECK(s[0] == false);
	CHECK(s[1] == true);
	CHECK(s[2] == true);
	CHECK(s[3] == false);
	auto d = a - b;	// 减法 = XOR
	CHECK(d[0] == false);
	CHECK(d[1] == true);
	auto m = a * b;	// AND
	CHECK(m[0] == true);
	CHECK(m[1] == false);
	CHECK(m[2] == false);
	CHECK(m[3] == false);
	auto dv = a / b;	// 除法 = AND
	CHECK(dv[0] == true);
	CHECK(dv[1] == false);

	// 归约：reduce_sum = 奇偶校验（XOR 折叠）
	tensor<1, bool> p(std::array<xhqm::size, 1>{ 3 }, true);
	CHECK(tensor_base::reduce_sum(p, 0).value(0) == true);	// T XOR T XOR T = T
	tensor<1, bool> q2(std::array<xhqm::size, 1>{ 2 }, true);
	CHECK(tensor_base::reduce_sum(q2, 0).value(0) == false);	// T XOR T = F

	// 矩阵乘（GF(2)）
	matrix<bool> mb({ { true, true }, { true, true } });
	CHECK((mb * mb)(0, 0) == false);

	// 向量点积 = XOR(逐位 AND)
	vector<bool> bv({ true, true, true }), bw({ true, false, true });
	CHECK(bv * bw == false);

	// 就地广播 +=（XOR）
	tensor<2, bool> x(std::array<xhqm::size, 2>{ 1, 2 }, true);
	tensor<2, bool> y(std::array<xhqm::size, 2>{ 2, 2 }, false);
	y += x;
	CHECK(y(0, 0) == true);
	CHECK(y(1, 1) == true);
}
