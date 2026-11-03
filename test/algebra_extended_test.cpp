// algebra 扩展测试：analysis 统计接口 + matrix/vector 基本操作
#include "../algebra/analysis/analysis.h"
#include "test.h"
#include <vector>

using namespace xhqm;
using namespace xhqm::analysis;

TEST_CASE("test_matrix_basic")
{
	matrix<double> m(2, 3, 0.0);
	CHECK((m.rows() == 2 && m.cols() == 3 && m.size() == 6));
	m(0, 0) = 1.0; m(0, 1) = 2.0; m(0, 2) = 3.0;
	m(1, 0) = 4.0; m(1, 1) = 5.0; m(1, 2) = 6.0;
	CHECK((m(0, 0) == 1.0 && m(1, 2) == 6.0));

	matrix<double> m2 = { {1, 2, 3}, {4, 5, 6} };
	CHECK(m2(1, 1) == 5.0);
}

TEST_CASE("test_vector_basic")
{
	vector<double> v(4);
	v[0] = 1; v[1] = 2; v[2] = 3; v[3] = 4;
	CHECK(v.size() == 4);
	CHECK((v[0] == 1 && v[3] == 4));

	vector<double> v2 = { 1, 2, 3, 4 };
	CHECK(v2[2] == 3);
}

TEST_CASE("test_stats_basic")
{
	vector<double> v = { 1, 2, 3, 4 };
	CHECK_NEAR(analysis::sum(v), 10.0, 1e-12);
	CHECK_NEAR(analysis::prod(v), 24.0, 1e-12);
	CHECK_NEAR(analysis::mean(v), 2.5, 1e-12);
	CHECK_NEAR(analysis::max(v), 4.0, 1e-12);
	CHECK_NEAR(analysis::min(v), 1.0, 1e-12);
	CHECK_NEAR(analysis::range(v), 3.0, 1e-12);
	CHECK_NEAR(analysis::var(v), 5.0 / 3.0, 1e-12);   // 样本方差 N-1
	CHECK_NEAR(analysis::stddev(v), std::sqrt(5.0 / 3.0), 1e-12);

	// cumsum / cumprod / diff
	auto cs = analysis::cumsum(v);
	CHECK((cs.size() == 4 && cs[3] == 10));
	auto cp = analysis::cumprod(v);
	CHECK((cp.size() == 4 && cp[3] == 24));
	auto df = analysis::diff(v);
	CHECK((df.size() == 3 && df[0] == 1 && df[2] == 1));

	// median / mode / sort / unique
	vector<double> v2 = { 1, 2, 3, 4 };
	CHECK_NEAR(analysis::median(v2), 2.5, 1e-12);
	vector<double> v3 = { 1, 1, 2, 3 };
	CHECK_NEAR(analysis::mode(v3), 1.0, 1e-12);
	vector<double> v4 = { 4, 2, 1, 3 };
	auto s = analysis::sort(v4);
	CHECK((s.size() == 4 && s[0] == 1 && s[3] == 4));
	vector<double> v5 = { 1, 2, 2, 3 };
	auto u = analysis::unique(v5);
	CHECK((u.size() == 3 && u[0] == 1 && u[2] == 3));
}

TEST_CASE("test_stats_matrix")
{
	matrix<double> m = { {1, 2, 3}, {4, 5, 6} };
	CHECK_NEAR(analysis::sum(m), 21.0, 1e-12);
	CHECK_NEAR(analysis::mean(m), 3.5, 1e-12);
	CHECK_NEAR(analysis::max(m), 6.0, 1e-12);
	CHECK_NEAR(analysis::min(m), 1.0, 1e-12);
}

TEST_CASE("test_linalg_basic")
{
	// svd 重构：A = U·S·Vᵀ
	matrix<double> A = { {1, 2}, {3, 4}, {5, 6} };
	auto sv = analysis::svd(A);
	matrix<double> S(sv.U.cols(), sv.V.rows(), 0.0);
	for (xhqm::size i = 0; i < sv.S.size(); ++i) S(i, i) = sv.S[i];
	auto recon = (sv.U * S) * sv.V.transpose();
	CHECK((recon.rows() == 3 && recon.cols() == 2));
	for (xhqm::size i = 0; i < 3; ++i)
		for (xhqm::size j = 0; j < 2; ++j)
			CHECK_NEAR(recon(i, j), A(i, j), 1e-6);

	// eig：对称 [[2,1],[1,2]] → {1,3}
	matrix<double> S2 = { {2, 1}, {1, 2} };
	auto e = analysis::eig(S2);
	double mn = e[0].real() < e[1].real() ? e[0].real() : e[1].real();
	double mx = e[0].real() > e[1].real() ? e[0].real() : e[1].real();
	CHECK_NEAR(mn, 1.0, 1e-9);
	CHECK_NEAR(mx, 3.0, 1e-9);

	// pinv：A·A⁺·A = A
	matrix<double> C = { {1, 2, 3}, {4, 5, 6} };
	auto Cp = analysis::pinv(C);
	auto CCp = C * Cp;
	auto CCpC = CCp * C;
	for (xhqm::size i = 0; i < 2; ++i)
		for (xhqm::size j = 0; j < 3; ++j)
			CHECK_NEAR(CCpC(i, j), C(i, j), 1e-6);

	// roots：x²-5x+6 → {2,3}
	vector<double> p = { 1, -5, 6 };
	auto rr = analysis::roots(p);
	CHECK(rr.size() == 2);
	double rmn = rr[0].real() < rr[1].real() ? rr[0].real() : rr[1].real();
	double rmx = rr[0].real() > rr[1].real() ? rr[0].real() : rr[1].real();
	CHECK_NEAR(rmn, 2.0, 1e-9);
	CHECK_NEAR(rmx, 3.0, 1e-9);

	// schur：Q·T·Qᵀ = A
	matrix<double> B = { {4, 1, 2}, {1, 3, 0}, {2, 0, 5} };
	auto sc = analysis::schur(B);
	auto QT = sc.Q * sc.T;
	auto QTe = QT * sc.Q.transpose();
	for (xhqm::size i = 0; i < 3; ++i)
		for (xhqm::size j = 0; j < 3; ++j)
			CHECK_NEAR(QTe(i, j), B(i, j), 1e-8);

	// inv：A·A⁻¹ = I
	matrix<double> D = { {4, 7}, {2, 6} };
	auto Di = analysis::inv(D);
	auto DDi = D * Di;
	for (xhqm::size i = 0; i < 2; ++i)
		for (xhqm::size j = 0; j < 2; ++j)
			CHECK_NEAR(DDi(i, j), (i == j) ? 1.0 : 0.0, 1e-9);

	// lu：P·A = L·U
	matrix<double> E = { {2, 1, 1}, {4, -6, 0}, {-2, 7, 2} };
	auto lr = analysis::lu(E);
	auto PA = lr.P * E;
	auto LU = lr.L * lr.U;
	for (xhqm::size i = 0; i < 3; ++i)
		for (xhqm::size j = 0; j < 3; ++j)
			CHECK_NEAR(PA(i, j), LU(i, j), 1e-9);

	// qr：A = Q·R，Q 正交
	matrix<double> F = { {1, 2}, {3, 4}, {5, 6} };
	auto qr = analysis::qr(F);
	auto QR = qr.Q * qr.R;
	for (xhqm::size i = 0; i < 3; ++i)
		for (xhqm::size j = 0; j < 2; ++j)
			CHECK_NEAR(QR(i, j), F(i, j), 1e-9);
	auto QtQ = qr.Q.transpose() * qr.Q;
	for (xhqm::size i = 0; i < 3; ++i)
		for (xhqm::size j = 0; j < 3; ++j)
			CHECK_NEAR(QtQ(i, j), (i == j) ? 1.0 : 0.0, 1e-9);

	// mldivide：a·x = b
	matrix<double> G = { {3, 1}, {1, 2} };
	matrix<double> H = { {9}, {8} };
	auto X = analysis::mldivide(G, H);
	CHECK_NEAR(X(0, 0), 2.0, 1e-9);
	CHECK_NEAR(X(1, 0), 3.0, 1e-9);

	// norm Frobenius：‖[3,4]‖ = 5
	matrix<double> N = { {3, 4} };
	CHECK_NEAR(analysis::norm(N, norm_type::fro), 5.0, 1e-9);
}

TEST_CASE("test_enhance")
{
	// 沿维归约
	matrix<double> m = { {1, 2, 3}, {4, 5, 6} };
	auto mc = analysis::mean(m, axis::rows);
	CHECK_NEAR(mc[0], 2.5, 1e-12);
	CHECK_NEAR(mc[2], 4.5, 1e-12);
	auto mx = analysis::max(m, axis::cols);
	CHECK_NEAR(mx[0], 3.0, 1e-12);
	CHECK_NEAR(mx[1], 6.0, 1e-12);
	auto mn = analysis::min(m, axis::rows);
	CHECK_NEAR(mn[1], 2.0, 1e-12);
	auto pd = analysis::prod(m, axis::cols);
	CHECK_NEAR(pd[0], 6.0, 1e-12);
	CHECK_NEAR(pd[1], 120.0, 1e-12);

	// var/std 总体方差
	vector<double> v = { 1, 2, 3, 4 };
	CHECK_NEAR(analysis::var(v, variance::population), 1.25, 1e-12);
	CHECK_NEAR(analysis::stddev(v, variance::population), std::sqrt(1.25), 1e-12);

	// argmax/argmin
	CHECK(analysis::argmax(v) == 3);
	CHECK(analysis::argmin(v) == 0);

	// sort 降序 + 索引
	auto sd = analysis::sort(v, sort_order::descend);
	CHECK((sd[0] == 4 && sd[3] == 1));
	auto si = analysis::sort_index(v);
	CHECK((si[0] == 0 && si[3] == 3));

	// flip / circshift
	matrix<double> F = { {1, 2}, {3, 4} };
	auto fl = analysis::flip(F, axis::cols);
	CHECK((fl(0, 0) == 2 && fl(0, 1) == 1));
	auto cs = analysis::circshift(F, 1, axis::rows);
	CHECK((cs(0, 0) == 3 && cs(1, 0) == 1));

	// kron
	auto K = analysis::kron(matrix<double>{ {1, 2} }, matrix<double>{ {10} });
	CHECK((K.rows() == 1 && K.cols() == 2));
	CHECK((K(0, 0) == 10 && K(0, 1) == 20));

	// rot90 × 2
	auto r2 = analysis::rot90(F, 2);
	CHECK((r2(0, 0) == 4 && r2(1, 1) == 1));

	// find2
	matrix<double> NZ = { {0, 1}, {2, 0} };
	auto fz = analysis::find_rc(NZ);
	CHECK((fz.size() == 2 && fz[0].first == 0 && fz[0].second == 1));

	// 数学增强
	matrix<double> one = { {1.0} };
	CHECK_NEAR(analysis::cot(one)(0, 0), 1.0 / std::tan(1.0), 1e-12);
	CHECK_NEAR(analysis::deg2rad(matrix<double>{ {180.0} })(0, 0), 3.141592653589793, 1e-12);
	CHECK_NEAR(analysis::rad2deg(matrix<double>{ {3.141592653589793} })(0, 0), 180.0, 1e-9);
	CHECK_NEAR(analysis::erf(matrix<double>{ {0.0} })(0, 0), 0.0, 1e-12);
	CHECK_NEAR(analysis::gamma(matrix<double>{ {5.0} })(0, 0), 24.0, 1e-9);
}

TEST_CASE("test_p2")
{
	// all/any
	matrix<bool> bm = { {true, true}, {true, true} };
	CHECK(analysis::all(bm));
	CHECK(analysis::any(bm));
	matrix<bool> bm2 = { {true, false} };
	CHECK(!analysis::all(bm2));
	CHECK(analysis::any(bm2));

	// isnan/isfinite
	matrix<double> m = { {1.0, std::nan("")}, {2.0, 3.0} };
	auto nn = analysis::isnan(m);
	CHECK((!nn(0, 0) && nn(0, 1)));
	auto ff = analysis::isfinite(m);
	CHECK((ff(0, 0) && !ff(0, 1)));

	// nan* 系列
	vector<double> v = { 1, std::nan(""), 3, 4 };
	CHECK_NEAR(analysis::nansum(v), 8.0, 1e-12);
	CHECK_NEAR(analysis::nanmean(v), 8.0 / 3.0, 1e-12);
	CHECK_NEAR(analysis::nanmax(v), 4.0, 1e-12);
	CHECK_NEAR(analysis::nanmin(v), 1.0, 1e-12);

	// prctile/quantile/iqr
	vector<double> s = { 1, 2, 3, 4, 5 };
	CHECK_NEAR(analysis::prctile(s, 50.0), 3.0, 1e-12);
	CHECK_NEAR(analysis::quantile(s, 0.5), 3.0, 1e-12);
	CHECK_NEAR(analysis::iqr(s), 2.0, 1e-12);

	// 集合
	vector<double> a = { 1, 2, 3, 4 };
	vector<double> b = { 3, 4, 5 };
	auto inter = analysis::intersect(a, b);
	CHECK((inter.size() == 2 && inter[0] == 3 && inter[1] == 4));
	auto uni = analysis::setunion(a, b);
	CHECK((uni.size() == 5 && uni[0] == 1 && uni[4] == 5));
	auto diff = analysis::setdiff(a, b);
	CHECK((diff.size() == 2 && diff[0] == 1 && diff[1] == 2));
	CHECK(analysis::ismember(3.0, a));
	CHECK(!analysis::ismember(9.0, a));

	// sortrows
	matrix<double> M = { {3, 1}, {1, 2}, {2, 0} };
	auto sr = analysis::sortrows(M);
	CHECK((sr(0, 0) == 1 && sr(1, 0) == 2 && sr(2, 0) == 3));

	// trapz
	vector<double> y = { 1, 2, 3 };
	CHECK_NEAR(analysis::trapz(y), 4.0, 1e-12);

	// fftshift/ifftshift
	vector<complex<double>> c = { {1, 0}, {2, 0}, {3, 0}, {4, 0} };
	auto fs = analysis::fftshift(c);
	CHECK((fs[0].real() == 3 && fs[1].real() == 4 && fs[2].real() == 1 && fs[3].real() == 2));
	auto ifs = analysis::ifftshift(fs);
	CHECK((ifs[0].real() == 1 && ifs[1].real() == 2));

	// filter：一阶 IIR y[n] = x[n] + 0.5·y[n-1]
	vector<double> fb = { 1 };
	vector<double> fa = { 1, -0.5 };
	vector<double> fx = { 1, 0, 0, 0 };
	auto fy = analysis::filter(fb, fa, fx);
	CHECK_NEAR(fy[0], 1.0, 1e-12);
	CHECK_NEAR(fy[1], 0.5, 1e-12);
	CHECK_NEAR(fy[2], 0.25, 1e-12);

	// meshgrid
	auto mg = analysis::meshgrid(vector<double>{1, 2, 3}, vector<double>{4, 5});
	CHECK((mg.X.rows() == 2 && mg.X.cols() == 3));
	CHECK((mg.X(0, 0) == 1 && mg.X(1, 2) == 3));
	CHECK((mg.Y(0, 0) == 4 && mg.Y(1, 2) == 5));
}

TEST_CASE("test_p3")
{
	// vander
	auto V = analysis::vander(vector<double>{1, 2, 3});
	CHECK_NEAR(V(0, 2), 1.0, 1e-12);
	CHECK_NEAR(V(2, 0), 9.0, 1e-12);

	// compan：x²-5x+6 友矩阵 [[5,-6],[1,0]]
	auto C = analysis::compan(vector<double>{1, -5, 6});
	CHECK_NEAR(C(0, 0), 5.0, 1e-12);
	CHECK_NEAR(C(0, 1), -6.0, 1e-12);
	CHECK_NEAR(C(1, 0), 1.0, 1e-12);

	// hilb
	auto H = analysis::hilb<double>(3);
	CHECK_NEAR(H(0, 0), 1.0, 1e-12);
	CHECK_NEAR(H(1, 1), 1.0 / 3.0, 1e-12);

	// toeplitz
	auto T = analysis::toeplitz(vector<double>{1, 2, 3}, vector<double>{1, 4, 5});
	CHECK_NEAR(T(0, 0), 1.0, 1e-12);
	CHECK_NEAR(T(0, 1), 4.0, 1e-12);
	CHECK_NEAR(T(1, 0), 2.0, 1e-12);

	// hankel
	auto HK = analysis::hankel(vector<double>{1, 2, 3}, vector<double>{3, 4, 5});
	CHECK_NEAR(HK(0, 0), 1.0, 1e-12);
	CHECK_NEAR(HK(1, 2), 4.0, 1e-12);

	// expm(0) = I
	auto E = analysis::expm(matrix<double>{ {0, 0}, {0, 0} });
	CHECK_NEAR(E(0, 0), 1.0, 1e-12);
	CHECK_NEAR(E(0, 1), 0.0, 1e-12);

	// conv2
	auto C2 = analysis::conv2(matrix<double>{ {1, 2} }, matrix<double>{ {3}, {4} });
	CHECK((C2.rows() == 2 && C2.cols() == 2));
	CHECK_NEAR(C2(0, 0), 3.0, 1e-12);
	CHECK_NEAR(C2(1, 1), 8.0, 1e-12);

	// xcorr：[1,2] × [3,4] → [4,11,6]
	auto X = analysis::xcorr(vector<double>{1, 2}, vector<double>{3, 4});
	CHECK(X.size() == 3);
	CHECK_NEAR(X[0], 4.0, 1e-12);
	CHECK_NEAR(X[1], 11.0, 1e-12);
	CHECK_NEAR(X[2], 6.0, 1e-12);
}

// 矩阵平方根 / 矩阵对数（手写：牛顿迭代 / 逆缩放平方 + Taylor）
TEST_CASE("test_logm_sqrtm")
{
	// A = [[2,1],[1,2]]（对称正定，特征值 1,3）
	matrix<double> A({ { 2.0, 1.0 }, { 1.0, 2.0 } });

	// sqrtm：X*X ≈ A
	auto X = sqrtm(A);
	auto XX = X * X;
	CHECK_NEAR(XX(0, 0), 2.0, 1e-6);
	CHECK_NEAR(XX(0, 1), 1.0, 1e-6);
	CHECK_NEAR(XX(1, 0), 1.0, 1e-6);
	CHECK_NEAR(XX(1, 1), 2.0, 1e-6);

	// logm：expm(logm(A)) ≈ A
	auto L = logm(A);
	auto E = expm(L);
	CHECK_NEAR(E(0, 0), 2.0, 1e-4);
	CHECK_NEAR(E(0, 1), 1.0, 1e-4);
	CHECK_NEAR(E(1, 0), 1.0, 1e-4);
	CHECK_NEAR(E(1, 1), 2.0, 1e-4);

	// 对角矩阵：sqrtm/logm 逐元素可直接验证
	matrix<double> D({ { 4.0, 0.0 }, { 0.0, 9.0 } });
	auto SD = sqrtm(D);
	CHECK_NEAR(SD(0, 0), 2.0, 1e-6);
	CHECK_NEAR(SD(1, 1), 3.0, 1e-6);
	CHECK_NEAR(SD(0, 1), 0.0, 1e-6);
	auto LD = logm(D);
	CHECK_NEAR(LD(0, 0), std::log(4.0), 1e-4);
	CHECK_NEAR(LD(1, 1), std::log(9.0), 1e-4);
	CHECK_NEAR(LD(0, 1), 0.0, 1e-6);
}
