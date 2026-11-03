// 代数库全量测试：覆盖 mathf/complex/dualnum/quaternion/tensor/matrix/vector/figure/
// linear/geometry/ctype/linalg(eig/svd)/sparse/dual_quaternion
#include "../algebra/tensor/tensor.h"
#include "../algebra/geometry/geometry.h"
#include "../algebra/geometry/brep/brep.h"
#include "../algebra/ctype.h"
#include "test.h"
#include <limits>
#include <array>

using namespace xhqm;

TEST_CASE("test_mathf")
{
	CHECK_NEAR(sum(1.0, 2.0), 3.0, 1e-12);
	CHECK_NEAR(sub(5.0, 3.0), 2.0, 1e-12);
	CHECK_NEAR(mul(2.0, 3.0), 6.0, 1e-12);
	CHECK_NEAR(dot(2.0, 3.0), 6.0, 1e-12);
	CHECK_NEAR(div(6.0, 2.0), 3.0, 1e-12);
	CHECK_NEAR(min(3.0, 1.0), 1.0, 1e-12);
	CHECK_NEAR(max(3.0, 1.0), 3.0, 1e-12);
	CHECK_NEAR(abs(-2.5), 2.5, 1e-12);
	CHECK_NEAR(sin(0.0), 0.0, 1e-12);
	CHECK_NEAR(cos(0.0), 1.0, 1e-12);
	CHECK_NEAR(sqrt(16.0), 4.0, 1e-12);
	CHECK_NEAR(log(1.0), 0.0, 1e-12);
	CHECK_NEAR(pow(2.0, 3.0), 8.0, 1e-12);
	CHECK(std::isnan(std::nan("")));
	CHECK(std::isinf(std::numeric_limits<double>::infinity()));
	// bool 布尔环 GF(2)
	CHECK(sum(true, true) == false);
	CHECK(mul(true, true) == true);
	CHECK(dot(true, true) == true);
	CHECK(div(true, false) == false);
	// 序列化
	CHECK_NEAR(read<f32>(save(3.5f)), 3.5f, 1e-6);
	CHECK_NEAR(read<f64>("2.5"), 2.5, 1e-12);
	CHECK_NEAR(read<i64>("42"), 42, 1e-12);
	// 常量
	CHECK_NEAR(number<f64>::pi, 3.141592653589793, 1e-12);
	// f16 / 整型极限
	f16 h(0.5f);
	CHECK_NEAR((float)h, 0.5f, 0.01);
	f16 hmax; xhqm::max<f16>(hmax);
	CHECK(ismax(hmax));
	i64 iminv; xhqm::min<i64>(iminv);
	CHECK(ismin(iminv));
}

TEST_CASE("test_complex")
{
	complex<f64> a(1, 2), b(3, 4);
	auto cm = mul(a, b);
	CHECK_NEAR(cm.real(), -5.0, 1e-12);
	CHECK_NEAR(cm.imag(), 10.0, 1e-12);
	CHECK_NEAR(dot(a, b), 11.0, 1e-12);	// R² 内积
	CHECK_NEAR(sum(a, b).real(), 4.0, 1e-12);
	CHECK(equal(complex<f64>(1, 2), complex<f64>(1, 2)));
	CHECK(most(complex<f64>(3, 0), complex<f64>(1, 0)));
	auto back = read<complex<f64>>(save(complex<f64>(3.0, 4.0)));
	CHECK_NEAR(back.imag(), 4.0, 1e-12);
}

TEST_CASE("test_dualnum")
{
	dualnum<f64> x(3.0, 1.0);
	auto y = x * x * x + x * x * 2.0 + x + 1.0;
	CHECK_NEAR(y.real, 49.0, 1e-12);
	CHECK_NEAR(y.dual, 40.0, 1e-12);
	auto g = sin(x * x);
	CHECK_NEAR(g.dual, 6.0 * std::cos(9.0), 1e-12);
	CHECK_NEAR(exp(x).dual, std::exp(3.0), 1e-12);
	CHECK_NEAR(log(x).dual, 1.0 / 3.0, 1e-12);
	CHECK_NEAR(sqrt(x).dual, 1.0 / (2.0 * std::sqrt(3.0)), 1e-12);
	CHECK(equal(dualnum<f64>(1.0, 0.0), dualnum<f64>(1.0, 0.0)));
	auto back = read<dualnum<f64>>(save(dualnum<f64>(3.0, 2.0)));
	CHECK_NEAR(back.dual, 2.0, 1e-12);
}

TEST_CASE("test_quaternion")
{
	quaternion<f64> a(1.0, 2.0, 3.0, 4.0);
	CHECK_NEAR(a.norm2(), 30.0, 1e-12);
	CHECK_NEAR(dot(a, a), 30.0, 1e-12);	// 4D 标量点积
	auto qx = quaternion<f64>(1, 0, 0, 0), qy = quaternion<f64>(0, 1, 0, 0);
	CHECK_NEAR(mul(qx, qy).z, 1.0, 1e-12);	// Hamilton i*j=k
	auto q = quaternion<f64>::from_axis_angle({ 0, 0, 1 }, number<f64>::pi / 2);
	auto v = q.rotate({ 1.0, 0.0, 0.0 });
	CHECK_NEAR(v[1], 1.0, 1e-9);
	CHECK_NEAR(q.to_euler()[2], number<f64>::pi / 2.0, 1e-9);
	auto qm = quaternion<f64>::slerp(quaternion<f64>(), q, 0.5);
	CHECK_NEAR(qm.to_euler()[2], number<f64>::pi / 4.0, 1e-9);
	// so(3) exp/log 往返
	quaternion<f64> w(0, 0, number<f64>::pi / 2, 0);
	quaternion<f64> qe = quaternion<f64>::exp(w);
	CHECK_NEAR(qe.w, std::cos(number<f64>::pi / 4), 1e-9);
	CHECK_NEAR(quaternion<f64>::log(qe).z, number<f64>::pi / 2, 1e-9);
	auto qb = read<quaternion<f64>>(save(quaternion<f64>(1.0, 2.0, 3.0, 4.0)));
	CHECK_NEAR(qb.w, 4.0, 1e-12);
}

TEST_CASE("test_tensor")
{
	tensor<2, f64> t(std::array<xhqm::size, 2>{ 2, 2 }, 0.0);
	t(0, 0) = 1; t(0, 1) = 2; t(1, 0) = 3; t(1, 1) = 4;
	CHECK_NEAR((t + t)(0, 0), 2.0, 1e-12);
	CHECK_NEAR((t * t)(0, 1), 4.0, 1e-12);	// Hadamard
	auto tm = tensor_base::matmul(t, t);
	CHECK_NEAR(tm(0, 0), 7.0, 1e-12);
	auto rs = tensor_base::reduce_sum(t, 1);
	CHECK_NEAR(rs[1], 7.0, 1e-12);
	CHECK_NEAR(tensor_base::transpose(t, 0, 1)(0, 1), 3.0, 1e-12);
	// 自动广播 (2,1)+(1,3)->(2,3)
	tensor<2, f64> ba(std::array<xhqm::size, 2>{ 2, 1 }, 0.0);
	ba(0, 0) = 1; ba(1, 0) = 2;
	tensor<2, f64> bb(std::array<xhqm::size, 2>{ 1, 3 }, 0.0);
	bb(0, 0) = 10; bb(0, 1) = 20; bb(0, 2) = 30;
	auto bc = ba + bb;
	CHECK((bc.dimension(0) == 2 && bc.dimension(1) == 3));
	CHECK_NEAR(bc(0, 0), 11.0, 1e-12);
	CHECK_NEAR(bc(1, 2), 32.0, 1e-12);
	// 切片
	auto sl = t.slice({ 1, 0 }, { 2, 2 });
	CHECK((sl.dimension(0) == 1 && sl.dimension(1) == 2));
	CHECK_NEAR(sl(0, 0), 3.0, 1e-12);
	// bool GF(2)
	tensor<1, bool> tb(std::array<xhqm::size, 1>{ 2 }, true);
	CHECK((tb + tb)[0] == false);
}

TEST_CASE("test_matrix_vector")
{
	matrix<f64> a({ { 1, 2 }, { 3, 4 } }), b({ { 5, 6 }, { 7, 8 } });
	CHECK_NEAR((a + b)(0, 0), 6.0, 1e-12);
	CHECK_NEAR(a.mul(b)(0, 0), 5.0, 1e-12);	// Hadamard
	CHECK_NEAR(a.dot(b)(0, 0), 19.0, 1e-12);	// 矩阵乘
	CHECK_NEAR((a * b)(0, 0), 19.0, 1e-12);	// operator* = 矩阵乘
	CHECK_NEAR(matrix<f64>({ { 4, 7 }, { 2, 6 } }).det(), 10.0, 1e-12);
	matrix<bool> mb({ { true, true }, { true, true } });
	CHECK((mb * mb)(0, 0) == false);
	matrix<bool> mi({ { true, false }, { false, true } });
	CHECK(mi.det() == true);

	vector<f64> v1({ 1, 2, 3 }), v2({ 4, 5, 6 });
	CHECK_NEAR(v1.mul(v2)[0], 4.0, 1e-12);
	CHECK_NEAR(v1.dot(v2), 32.0, 1e-12);
	CHECK_NEAR(v1 * v2, 32.0, 1e-12);
	CHECK_NEAR(v1.cross(v2)[0], -3.0, 1e-12);
	CHECK_NEAR(v1.norm(), std::sqrt(14.0), 1e-12);
	CHECK_NEAR(v1.normalize().norm(), 1.0, 1e-12);
}

TEST_CASE("test_figure")
{
	figure<f64> a(4.0), b(2.0);
	CHECK_NEAR((a * b).value(), 8.0, 1e-12);
	CHECK_NEAR(a.sqrt().value(), 2.0, 1e-12);
	CHECK_NEAR(a.sin().value(), std::sin(4.0), 1e-12);
	CHECK(bool(a > b));
	CHECK(bool(a == figure<f64>(4.0)));
}

TEST_CASE("test_linear_geometry")
{
	vect<f64> v(3.0, 4.0, 0.0);
	CHECK_NEAR(v.length(), 5.0, 1e-12);
	vect<f64> a(1.0, 0.0, 0.0), b(0.0, 1.0, 0.0);
	CHECK_NEAR(vect<f64>::dot(a, b), 0.0, 1e-12);
	CHECK_NEAR(vect<f64>::mul(a, b).z(), 1.0, 1e-12);
	matx<f64> m(2, 2);
	m(0, 0) = 1; m(0, 1) = 2; m(1, 0) = 3; m(1, 1) = 4;
	CHECK_NEAR(m.det(), -2.0, 1e-12);
	CHECK_NEAR(rotate_x(0.0)(0, 0), 1.0, 1e-12);
	CHECK_NEAR(radians(180.0), xhqm::pi, 1e-12);

	using namespace geometry;
	circle c(2.0);
	c.poit(geometry::poit(0, 0, 0));
	c.pose(geometry::vect(0, 0, 1));
	CHECK_NEAR(c.circle_point(0.0).x(), 2.0, 1e-9);
	CHECK(c.circle_grid(4).faces().size() == 4);
	sphere sp(1.0);
	sp.poit(geometry::poit(0, 0, 0));
	CHECK_NEAR(sp.sphere_point(geometry::vect(1, 0, 0)).x(), 1.0, 1e-9);
	values knots = { 0, 0, 0, 0.5f, 1, 1, 1 };
	auto span = nurbs::find_span(knots, 2, 0.5f);
	auto bf = nurbs::base_funs(knots, 2, 0.5f, span);
	CHECK_NEAR(bf[0] + bf[1] + bf[2], 1.0, 1e-6);
	CHECK(create_sphere_1(1.0).nurbs_p().size() > 0);
	CHECK(create_sphere_4(1.0).nurbs_p().size() > 0);
}

TEST_CASE("test_ctype")
{
	CHECK(ctype::get_type("real_32") == ctype::real_32);
	CHECK(ctype::get_text(ctype::bool_1) == "bool");
	CHECK(ctype::get_size(ctype::real_64) == 8);
	xhqm::arr ar("array<2,2,real_32>");
	CHECK(ar.good());
	CHECK(ar.size() == 4);
	xhqm::tup tp("tuple<real_32,uint_8>");
	CHECK(tp.good());
	CHECK(tp.size() == 2);
}

TEST_CASE("test_linalg_sparse")
{
	// 对称特征值：[[2,1],[1,2]] → {1,3}
	tensor<2, f64> A(std::array<xhqm::size, 2>{ 2, 2 }, 0.0);
	A(0, 0) = 2; A(0, 1) = 1; A(1, 0) = 1; A(1, 1) = 2;
	auto eig = tensor_base::eig_sym(A);
	CHECK_NEAR(eig.values[0], 1.0, 1e-9);
	CHECK_NEAR(eig.values[1], 3.0, 1e-9);
	// SVD：diag(3,2)
	tensor<2, f64> M(std::array<xhqm::size, 2>{ 2, 2 }, 0.0);
	M(0, 0) = 3; M(1, 1) = 2;
	auto sv = tensor_base::svd(M);
	CHECK_NEAR(sv.S[0], 3.0, 1e-9);
	CHECK_NEAR(sv.S[1], 2.0, 1e-9);
	tensor<2, f64> US(std::array<xhqm::size, 2>{ 2, 2 }, 0.0);
	US(0, 0) = sv.S[0]; US(1, 1) = sv.S[1];
	auto recon = tensor_base::matmul(tensor_base::matmul(sv.U, US), tensor_base::transpose(sv.V, 0, 1));
	CHECK_NEAR(recon(0, 0), 3.0, 1e-9);
	CHECK_NEAR(recon(1, 1), 2.0, 1e-9);
	// 稀疏
	matrix<f64> dense(3, 3, 0.0);
	dense(0, 0) = 1; dense(0, 2) = 2; dense(1, 1) = 3; dense(2, 0) = 4; dense(2, 2) = 5;
	tensor_base::sparse_matrix<f64> sp(dense);
	CHECK(sp.nnz() == 5);
	vector<f64> x({ 1.0, 2.0, 3.0 });
	vector<f64> sx = sp * x;
	CHECK_NEAR(sx[0], 7.0, 1e-12);
	CHECK_NEAR(sx[1], 6.0, 1e-12);
	CHECK_NEAR(sx[2], 19.0, 1e-12);
	CHECK_NEAR(sp.dense()(2, 2), 5.0, 1e-12);
	CHECK(sp.transpose().nnz() == 5);
}

TEST_CASE("test_dual_quaternion")
{
	quaternion<f64> rot = quaternion<f64>::from_axis_angle({ 0, 0, 1 }, number<f64>::pi / 2);
	dual_quaternion<f64> dq(rot, { 1.0, 2.0, 3.0 });
	auto t = dq.translation();
	CHECK_NEAR(t[0], 1.0, 1e-9);
	CHECK_NEAR(t[2], 3.0, 1e-9);
	auto p0 = dq.transform({ 0.0, 0.0, 0.0 });
	CHECK_NEAR(p0[1], 2.0, 1e-9);
	dual_quaternion<f64> id;
	auto p1 = id.transform({ 5.0, 6.0, 7.0 });
	CHECK_NEAR(p1[0], 5.0, 1e-9);
	dual_quaternion<f64> dq1(quaternion<f64>(), { 1.0, 0.0, 0.0 });
	dual_quaternion<f64> dq2(quaternion<f64>(), { 0.0, 2.0, 0.0 });
	auto tc = (dq1 * dq2).translation();
	CHECK_NEAR(tc[0], 1.0, 1e-9);
	CHECK_NEAR(tc[1], 2.0, 1e-9);
}

TEST_CASE("test_linalg_lu_qr_pinv")
{
	// LU：P·A = L·U
	tensor<2, f64> A(std::array<xhqm::size, 2>{ 3, 3 }, 0.0);
	A(0, 0) = 2; A(0, 1) = 1; A(0, 2) = 1;
	A(1, 0) = 4; A(1, 1) = -6; A(1, 2) = 0;
	A(2, 0) = -2; A(2, 1) = 7; A(2, 2) = 2;
	auto lr = tensor_base::lu(A);
	auto PA = tensor_base::matmul(lr.P, A);
	auto LU = tensor_base::matmul(lr.L, lr.U);
	for (xhqm::size i = 0; i < 3; ++i)
		for (xhqm::size j = 0; j < 3; ++j)
			CHECK_NEAR(PA(i, j), LU(i, j), 1e-9);

	// LU 回归：一般矩阵（旧实现缺 Schur 补，会失配）
	tensor<2, f64> A2(std::array<xhqm::size, 2>{ 3, 3 }, 0.0);
	A2(0, 0) = 1; A2(0, 1) = 2; A2(0, 2) = 3;
	A2(1, 0) = 4; A2(1, 1) = 5; A2(1, 2) = 6;
	A2(2, 0) = 7; A2(2, 1) = 8; A2(2, 2) = 10;
	auto lr2 = tensor_base::lu(A2);
	auto PA2 = tensor_base::matmul(lr2.P, A2);
	auto LU2 = tensor_base::matmul(lr2.L, lr2.U);
	for (xhqm::size i = 0; i < 3; ++i)
		for (xhqm::size j = 0; j < 3; ++j)
			CHECK_NEAR(PA2(i, j), LU2(i, j), 1e-9);

	// QR：A = Q·R，Q 正交，R 上三角
	tensor<2, f64> B(std::array<xhqm::size, 2>{ 3, 2 }, 0.0);
	B(0, 0) = 1; B(0, 1) = 2;
	B(1, 0) = 3; B(1, 1) = 4;
	B(2, 0) = 5; B(2, 1) = 6;
	auto qr = tensor_base::qr(B);
	auto QR = tensor_base::matmul(qr.Q, qr.R);
	for (xhqm::size i = 0; i < 3; ++i)
		for (xhqm::size j = 0; j < 2; ++j)
			CHECK_NEAR(QR(i, j), B(i, j), 1e-9);
	auto QtQ = tensor_base::matmul(tensor_base::transpose(qr.Q, 0, 1), qr.Q);
	for (xhqm::size i = 0; i < 3; ++i)
		for (xhqm::size j = 0; j < 3; ++j)
			CHECK_NEAR(QtQ(i, j), (i == j) ? 1.0 : 0.0, 1e-9);

	// pinv：A·A⁺·A = A
	tensor<2, f64> C(std::array<xhqm::size, 2>{ 2, 3 }, 0.0);
	C(0, 0) = 1; C(0, 1) = 2; C(0, 2) = 3;
	C(1, 0) = 4; C(1, 1) = 5; C(1, 2) = 6;
	auto Cp = tensor_base::pinv(C);
	auto CCp = tensor_base::matmul(C, Cp);
	CHECK((CCp.dimension(0) == 2 && CCp.dimension(1) == 2));
	auto CCpC = tensor_base::matmul(CCp, C);
	for (xhqm::size i = 0; i < 2; ++i)
		for (xhqm::size j = 0; j < 3; ++j)
			CHECK_NEAR(CCpC(i, j), C(i, j), 1e-6);
}

TEST_CASE("test_linalg_schur_eig")
{
	// 对称 [[2,1],[1,2]] → {1,3}
	tensor<2, f64> S(std::array<xhqm::size, 2>{ 2, 2 }, 0.0);
	S(0, 0) = 2; S(0, 1) = 1; S(1, 0) = 1; S(1, 1) = 2;
	auto es = tensor_base::eig(S);
	double mn = es[0].real() < es[1].real() ? es[0].real() : es[1].real();
	double mx = es[0].real() > es[1].real() ? es[0].real() : es[1].real();
	CHECK_NEAR(mn, 1.0, 1e-9);
	CHECK_NEAR(mx, 3.0, 1e-9);
	CHECK_NEAR(es[0].imag(), 0.0, 1e-9);
	CHECK_NEAR(es[1].imag(), 0.0, 1e-9);

	// 反对称 [[0,-1],[1,0]] → ±i
	tensor<2, f64> R(std::array<xhqm::size, 2>{ 2, 2 }, 0.0);
	R(0, 1) = -1; R(1, 0) = 1;
	auto er = tensor_base::eig(R);
	CHECK_NEAR(er[0].real(), 0.0, 1e-9);
	CHECK_NEAR(er[1].real(), 0.0, 1e-9);
	CHECK_NEAR(std::fabs(er[0].imag()), 1.0, 1e-9);
	CHECK_NEAR(std::fabs(er[1].imag()), 1.0, 1e-9);

	// 上三角 [[1,2,3],[0,4,5],[0,0,6]] → {1,4,6}
	tensor<2, f64> U(std::array<xhqm::size, 2>{ 3, 3 }, 0.0);
	U(0, 0) = 1; U(0, 1) = 2; U(0, 2) = 3;
	U(1, 1) = 4; U(1, 2) = 5;
	U(2, 2) = 6;
	auto eu = tensor_base::eig(U);
	double s[3] = { eu[0].real(), eu[1].real(), eu[2].real() };
	double smn = s[0], smx = s[0];
	for (int i = 1; i < 3; ++i) { if (s[i] < smn) smn = s[i]; if (s[i] > smx) smx = s[i]; }
	double smid = s[0] + s[1] + s[2] - smn - smx;
	CHECK_NEAR(smn, 1.0, 1e-9);
	CHECK_NEAR(smid, 4.0, 1e-9);
	CHECK_NEAR(smx, 6.0, 1e-9);

	// schur：Q·T·Qᵀ = A
	tensor<2, f64> A(std::array<xhqm::size, 2>{ 3, 3 }, 0.0);
	A(0, 0) = 4; A(0, 1) = 1; A(0, 2) = 2;
	A(1, 0) = 1; A(1, 1) = 3; A(1, 2) = 0;
	A(2, 0) = 2; A(2, 1) = 0; A(2, 2) = 5;
	auto sc = tensor_base::schur(A);
	auto QT = tensor_base::matmul(sc.Q, sc.T);
	auto QTe = tensor_base::matmul(QT, tensor_base::transpose(sc.Q, 0, 1));
	for (xhqm::size i = 0; i < 3; ++i)
		for (xhqm::size j = 0; j < 3; ++j)
			CHECK_NEAR(QTe(i, j), A(i, j), 1e-8);
}

TEST_CASE("test_circle_orientation")
{
	using namespace geometry;
	circle c(1.0);
	c.poit(geometry::poit(0, 0, 0));

	// pose = +z（默认朝向）：圆在 xy 平面，法线 +z
	c.pose(geometry::vect(0, 0, 1));
	auto p0 = c.circle_point(0.0);
	auto p90 = c.circle_point(static_cast<geometry::value>(xhqm::pi / 2));
	CHECK_NEAR(p0.x(), 1.0, 1e-5);
	CHECK_NEAR(p0.y(), 0.0, 1e-5);
	CHECK_NEAR(p90.x(), 0.0, 1e-5);
	CHECK_NEAR(p90.y(), 1.0, 1e-5);

	// pose = +x：圆转到 yz 平面（x 分量 ≈ 0），半径保持
	c.pose(geometry::vect(1, 0, 0));
	auto px = c.circle_point(0.0);
	CHECK_NEAR(px.x(), 0.0, 1e-5);
	CHECK_NEAR(px.length(), 1.0, 1e-5);

	// pose = +y：圆转到 xz 平面（y 分量 ≈ 0）
	c.pose(geometry::vect(0, 1, 0));
	auto py = c.circle_point(0.0);
	CHECK_NEAR(py.y(), 0.0, 1e-5);
	CHECK_NEAR(py.length(), 1.0, 1e-5);

	// pose = -z（反平行）：圆仍在 xy 平面但绕向翻转
	c.pose(geometry::vect(0, 0, -1));
	auto pn = c.circle_point(static_cast<geometry::value>(xhqm::pi / 2));
	CHECK_NEAR(pn.x(), 0.0, 1e-5);
	CHECK_NEAR(pn.y(), -1.0, 1e-5);
}

TEST_CASE("test_create_sphere_guard")
{
	using namespace geometry;
	// resolution ≤ 0 曾死循环，现应 clamp 并正常返回
	auto s0 = create_sphere(1.0, 0.0);
	auto s1 = create_sphere(1.0, -1.0);
	auto s2 = create_sphere(1.0, 0.5);
	CHECK(!s0.empty());
	CHECK(!s1.empty());
	CHECK(!s2.empty());
}

TEST_CASE("test_poit_hash")
{
	using namespace geometry;
	poit_hash h;
	// XOR 组合可交换导致 (1,2,3) 与 (2,1,3) 同 hash；hash_combine 应区分
	CHECK(h(geometry::poit(1, 2, 3)) != h(geometry::poit(2, 1, 3)));
	CHECK(h(geometry::poit(1, 2, 3)) == h(geometry::poit(1, 2, 3)));
}

TEST_CASE("test_hyper")
{
	using namespace geometry;

	// hypercube(3)：2^3 顶点 + 2*3 个 (dim-1) 维面
	auto cube = geometry::hyper::hypercube(3, 1.0f);
	CHECK(cube.first.size() == 8);
	CHECK(cube.second.size() == 1);
	CHECK(cube.second[0].size() == 6);
	for (auto& f : cube.second[0]) CHECK(f.size() == 4);
	for (auto& p : cube.first)
		for (auto v : p)
			CHECK(std::fabs(v) == 1.0f);

	// simplex(3)：dim+1 顶点，顶点 0 = 原点，顶点 i = 第 i-1 单位轴
	auto sim = geometry::hyper::simplex(3);
	CHECK(sim.first.size() == 4);
	CHECK(sim.second[0].size() == 1);
	CHECK(sim.second[0][0].size() == 4);
	CHECK_NEAR(sim.first[0][0], 0.0, 1e-6);
	CHECK_NEAR(sim.first[0][1], 0.0, 1e-6);
	CHECK_NEAR(sim.first[0][2], 0.0, 1e-6);
	CHECK_NEAR(sim.first[1][0], 1.0, 1e-6);
	CHECK_NEAR(sim.first[2][1], 1.0, 1e-6);
	CHECK_NEAR(sim.first[3][2], 1.0, 1e-6);

	// hypersphere(3)：采样点范数 ≈ radius，维度正确
	auto sph = geometry::hyper::hypersphere(3, 64, 2.0f);
	CHECK(sph.size() == 64);
	for (auto& p : sph)
	{
		CHECK(p.size() == 3);
		double norm2 = 0.0;
		for (auto v : p) norm2 += (double)v * (double)v;
		CHECK_NEAR(std::sqrt(norm2), 2.0, 1e-3);
	}

	// dimension：顶点维度
	CHECK(geometry::hyper::dimension(geometry::hyper::point{ 1.0f, 2.0f, 3.0f }) == 3);
}

TEST_CASE("test_face_measure_intersect")
{
	using namespace geometry;

	// face 度量：单位直角三角形
	face f(geometry::poit(0, 0, 0), geometry::poit(1, 0, 0), geometry::poit(0, 1, 0));
	auto n = f.normal();
	CHECK_NEAR(n.x(), 0.0, 1e-5);
	CHECK_NEAR(n.y(), 0.0, 1e-5);
	CHECK_NEAR(n.z(), 1.0, 1e-5);
	CHECK_NEAR(f.area(), 0.5, 1e-5);
	auto c = f.centroid();
	CHECK_NEAR(c.x(), 1.0 / 3.0, 1e-5);
	CHECK_NEAR(c.y(), 1.0 / 3.0, 1e-5);

	// ray_sphere：从 (0,0,-5) 朝 +z，球心原点半径 1 → t=4
	geometry::value t = 0;
	CHECK(ray_sphere(geometry::poit(0, 0, -5), geometry::vect(0, 0, 1), geometry::poit(0, 0, 0), 1.0f, t));
	CHECK_NEAR(t, 4.0, 1e-4);

	// ray_triangle：从 (0.2,0.2,-1) 朝 +z 命中 z=0 平面三角形 → t=1
	CHECK(ray_triangle(geometry::poit(0.2f, 0.2f, -1.0f), geometry::vect(0, 0, 1),
		geometry::poit(0, 0, 0), geometry::poit(1, 0, 0), geometry::poit(0, 1, 0), t));
	CHECK_NEAR(t, 1.0, 1e-4);

	// point_plane_distance：有符号距离
	CHECK_NEAR(point_plane_distance(geometry::poit(0, 0, 3), geometry::poit(0, 0, 0), geometry::vect(0, 0, 1)), 3.0, 1e-5);
	CHECK_NEAR(point_plane_distance(geometry::poit(0, 0, -3), geometry::poit(0, 0, 0), geometry::vect(0, 0, 1)), -3.0, 1e-5);

	// signed_volume：单位四面体 = 1/6
	CHECK_NEAR(signed_volume(geometry::poit(0, 0, 0), geometry::poit(1, 0, 0), geometry::poit(0, 1, 0), geometry::poit(0, 0, 1)), 1.0 / 6.0, 1e-5);
}

TEST_CASE("test_primitives_grid")
{
	using namespace geometry;

	plane pl(geometry::poit(0, 0, 0), geometry::vect(0, 0, 1));
	auto plg = pl.plane_grid(1.0f, 1);
	CHECK(plg.poits().size() == 4);
	CHECK(plg.faces().size() == 2);

	box b(geometry::poit(0, 0, 0), geometry::poit(1, 1, 1));
	auto bg = b.box_grid();
	CHECK(bg.poits().size() == 8);
	CHECK(bg.faces().size() == 12);
	CHECK(b.contain(geometry::poit(0.5f, 0.5f, 0.5f)));
	CHECK(!b.contain(geometry::poit(2, 0, 0)));

	cylinder cy(1.0f, 2.0f);
	cy.count(8);
	auto cyg = cy.cylinder_grid();
	CHECK(cyg.poits().size() == 18);
	CHECK(cyg.faces().size() == 32);

	cone co(1.0f, 2.0f);
	co.count(8);
	auto cog = co.cone_grid();
	CHECK(cog.poits().size() == 10);
	CHECK(cog.faces().size() == 16);

	torus to(2.0f, 1.0f);
	to.count_U(8);
	to.count_V(6);
	auto tog = to.torus_grid();
	CHECK(tog.poits().size() == 48);
	CHECK(tog.faces().size() == 96);

	capsule ca(1.0f, 2.0f);
	ca.count(8);
	auto cag = ca.capsule_grid();
	CHECK(cag.poits().size() == 18);
	CHECK(cag.faces().size() == 32);

	quad q(geometry::poit(0, 0, 0), geometry::poit(1, 0, 0), geometry::poit(1, 1, 0), geometry::poit(0, 1, 0));
	auto qg = q.quad_grid();
	CHECK(qg.poits().size() == 4);
	CHECK(qg.faces().size() == 2);
}

TEST_CASE("test_nurbs_derivative")
{
	using namespace geometry;

	// 直线曲线（p=1，两控制点）：一阶导 = P1-P0
	curve c;
	c.nurbs_k(1);
	c.nurbs_v(values{ 0.0f, 0.0f, 1.0f, 1.0f });
	c.nurbs_p(std::vector<geometry::poit>{ geometry::poit(0, 0, 0), geometry::poit(2, 0, 0) });
	c.nurbs_w(std::vector<geometry::real>{ 1, 1 });
	auto d1 = c.creat_derivative(0.5f, 1);
	CHECK_NEAR(d1.x(), 2.0, 1e-4);
	CHECK_NEAR(d1.y(), 0.0, 1e-4);
	CHECK_NEAR(d1.z(), 0.0, 1e-4);

	// 球面法线指向径向（球心原点）
	surface sph = create_sphere_1(1.0f);
	geometry::value u0 = 0.5f, v0 = 0.5f;
	auto p = sph.creat_point(u0, v0);
	auto n = sph.creat_normal(u0, v0);
	geometry::value plen = p.length();
	CHECK(plen > 0.9f);
	geometry::vect radial = (geometry::vect)p / plen;
	CHECK_NEAR(std::fabs(geometry::vect::dot(n, radial)), 1.0, 1e-3);
	CHECK_NEAR(n.length(), 1.0, 1e-4);

	// 等参网格化：4×4 → 25 顶点 32 三角形
	auto g = sph.surface_grid(4, 4);
	CHECK(g.poits().size() == 25);
	CHECK(g.faces().size() == 32);
}

// 卷积（channels-first，串行/并行桩）
TEST_CASE("test_conv")
{
	// conv1d 基础：滑动窗口求和
	tensor<3, f64> x1(std::array<xhqm::size, 3>{ 1, 1, 4 }, 0.0);
	x1(0, 0, 0) = 1; x1(0, 0, 1) = 2; x1(0, 0, 2) = 3; x1(0, 0, 3) = 4;
	tensor<3, f64> w1(std::array<xhqm::size, 3>{ 1, 1, 3 }, 1.0);
	auto y1 = tensor_base::conv1d(x1, w1);
	CHECK(y1.dimension(2) == 2);
	CHECK_NEAR(y1(0, 0, 0), 6.0, 1e-12);
	CHECK_NEAR(y1(0, 0, 1), 9.0, 1e-12);

	// conv1d padding=1（零填充）
	auto y1p = tensor_base::conv1d(x1, w1, 1, 1);
	CHECK(y1p.dimension(2) == 4);
	CHECK_NEAR(y1p(0, 0, 0), 3.0, 1e-12);
	CHECK_NEAR(y1p(0, 0, 3), 7.0, 1e-12);

	// conv1d stride=2
	tensor<3, f64> x6(std::array<xhqm::size, 3>{ 1, 1, 6 }, 0.0);
	for (xhqm::size i = 0; i < 6; ++i) x6(0, 0, i) = f64(i + 1);
	auto y1s = tensor_base::conv1d(x6, w1, 2);
	CHECK(y1s.dimension(2) == 2);
	CHECK_NEAR(y1s(0, 0, 0), 6.0, 1e-12);
	CHECK_NEAR(y1s(0, 0, 1), 12.0, 1e-12);

	// conv1d dilation=2
	auto y1d = tensor_base::conv1d(x6, w1, 1, 0, 2);
	CHECK(y1d.dimension(2) == 2);
	CHECK_NEAR(y1d(0, 0, 0), 9.0, 1e-12);   // 1+3+5
	CHECK_NEAR(y1d(0, 0, 1), 12.0, 1e-12);  // 2+4+6

	// conv1d 带 bias
	tensor<1, f64> b1(std::array<xhqm::size, 1>{ 1 }, 10.0);
	auto y1b = tensor_base::conv1d(x1, w1, b1);
	CHECK_NEAR(y1b(0, 0, 0), 16.0, 1e-12);
	CHECK_NEAR(y1b(0, 0, 1), 19.0, 1e-12);

	// conv1d groups=2（每个输出通道只看一组输入通道）
	tensor<3, f64> xg(std::array<xhqm::size, 3>{ 1, 2, 4 }, 0.0);
	for (xhqm::size c = 0; c < 2; ++c)
		for (xhqm::size i = 0; i < 4; ++i)
			xg(0, c, i) = f64(c * 4 + i + 1);
	tensor<3, f64> wg(std::array<xhqm::size, 3>{ 2, 1, 3 }, 0.0);
	for (xhqm::size i = 0; i < 3; ++i) { wg(0, 0, i) = 1.0; wg(1, 0, i) = 2.0; }
	auto yg = tensor_base::conv1d(xg, wg, 1, 0, 1, 2);
	CHECK_NEAR(yg(0, 0, 0), 6.0, 1e-12);    // 1+2+3
	CHECK_NEAR(yg(0, 1, 1), 42.0, 1e-12);   // 2*(6+7+8)

	// conv2d 基础：2×2 求和核
	tensor<4, f64> x2(std::array<xhqm::size, 4>{ 1, 1, 3, 3 }, 0.0);
	for (xhqm::size i = 0; i < 3; ++i)
		for (xhqm::size j = 0; j < 3; ++j)
			x2(0, 0, i, j) = f64(i * 3 + j + 1);
	tensor<4, f64> w2(std::array<xhqm::size, 4>{ 1, 1, 2, 2 }, 1.0);
	auto y2 = tensor_base::conv2d(x2, w2);
	CHECK((y2.dimension(2) == 2 && y2.dimension(3) == 2));
	CHECK_NEAR(y2(0, 0, 0, 0), 12.0, 1e-12);  // 1+2+4+5
	CHECK_NEAR(y2(0, 0, 0, 1), 16.0, 1e-12);  // 2+3+5+6
	CHECK_NEAR(y2(0, 0, 1, 0), 24.0, 1e-12);  // 4+5+7+8
	CHECK_NEAR(y2(0, 0, 1, 1), 28.0, 1e-12);  // 5+6+8+9

	// conv3d 冒烟：1×1×1 核 = 缩放
	tensor<5, f64> x3(std::array<xhqm::size, 5>{ 1, 1, 2, 2, 2 }, 0.0);
	for (xhqm::size i = 0; i < 8; ++i) x3[i] = f64(i);
	tensor<5, f64> w3(std::array<xhqm::size, 5>{ 1, 1, 1, 1, 1 }, 2.0);
	auto y3 = tensor_base::conv3d(x3, w3);
	CHECK(y3.size() == 8);
	for (xhqm::size i = 0; i < 8; ++i) CHECK_NEAR(y3[i], 2.0 * f64(i), 1e-12);

	// conv4d 冒烟：1×1×1×1 核 = 缩放
	tensor<6, f64> x4(std::array<xhqm::size, 6>{ 1, 1, 2, 2, 2, 2 }, 0.0);
	for (xhqm::size i = 0; i < 16; ++i) x4[i] = f64(i);
	tensor<6, f64> w4(std::array<xhqm::size, 6>{ 1, 1, 1, 1, 1, 1 }, 3.0);
	auto y4 = tensor_base::conv4d(x4, w4);
	CHECK(y4.size() == 16);
	for (xhqm::size i = 0; i < 16; ++i) CHECK_NEAR(y4[i], 3.0 * f64(i), 1e-12);
}

// 稀疏矩阵 × 向量（SpMV，no 后端走串行桩）
TEST_CASE("test_spmv")
{
	// A = [[2,0,1],[0,3,0],[0,0,4]]，v=[1,2,3] → r=[5,6,12]
	matrix<f64> A({ { 2.0, 0.0, 1.0 }, { 0.0, 3.0, 0.0 }, { 0.0, 0.0, 4.0 } });
	tensor_base::sparse_matrix<f64> S(A);
	vector<f64> v({ 1.0, 2.0, 3.0 });
	auto r = S * v;
	CHECK_NEAR(r[0], 5.0, 1e-12);
	CHECK_NEAR(r[1], 6.0, 1e-12);
	CHECK_NEAR(r[2], 12.0, 1e-12);

	// 转置后乘：A^T @ v = [2, 6, 13]
	auto St = S.transpose();
	auto r2 = St * v;
	CHECK_NEAR(r2[0], 2.0, 1e-12);
	CHECK_NEAR(r2[1], 6.0, 1e-12);
	CHECK_NEAR(r2[2], 13.0, 1e-12);
}

TEST_CASE("test_brep_wire")
{
	using namespace geometry::brep;

	vertex v0, v1;
	v0.position = geometry::poit(0, 0, 0);
	v1.position = geometry::poit(1, 0, 0);
	edge e;
	e.from = v0.id;
	e.to = v1.id;
	e.t1 = 1;

	// uuid 唯一
	CHECK(v0.id != v1.id);
	CHECK(v0.id != e.id);

	// wire = 边环，push_next 构建
	wire w;
	w.push_next(&e);
	CHECK(w.size() == 1);

	edge e2;
	e2.from = v1.id;
	e2.to = v0.id;
	w.push_next(&e2);
	CHECK(w.size() == 2);

	// 方向在环边上（可读写）
	w.edge_of_next().reversed = true;
	CHECK(w.edge_of_next().reversed == true);

	// 环遍历：回到起点
	int visited = 0;
	for (auto& n : w) { (void)n; ++visited; }
	CHECK(visited == 2);
}

TEST_CASE("test_linalg_stub")
{
	// trsv_lower：L=[[1,0,0],[2,1,0],[3,4,1]]，b=[1,4,15] → x=[1,2,4]
	tensor<2, f64> L(std::array<xhqm::size, 2>{ 3, 3 }, 0.0);
	L(0, 0) = 1; L(1, 0) = 2; L(1, 1) = 1; L(2, 0) = 3; L(2, 1) = 4; L(2, 2) = 1;
	tensor<1, f64> b(std::array<xhqm::size, 1>{ 3 }, 0.0);
	b[0] = 1; b[1] = 4; b[2] = 15;
	auto x = tensor_base::trsv_lower(L, b);
	CHECK_NEAR(x[0], 1.0, 1e-12);
	CHECK_NEAR(x[1], 2.0, 1e-12);
	CHECK_NEAR(x[2], 4.0, 1e-12);

	// trsv_upper：U=[[1,2,3],[0,1,4],[0,0,1]]，x=[1,2,3] → b=[14,14,3]
	tensor<2, f64> U(std::array<xhqm::size, 2>{ 3, 3 }, 0.0);
	U(0, 0) = 1; U(0, 1) = 2; U(0, 2) = 3; U(1, 1) = 1; U(1, 2) = 4; U(2, 2) = 1;
	tensor<1, f64> bu(std::array<xhqm::size, 1>{ 3 }, 0.0);
	bu[0] = 14; bu[1] = 14; bu[2] = 3;
	auto xu = tensor_base::trsv_upper(U, bu);
	CHECK_NEAR(xu[0], 1.0, 1e-12);
	CHECK_NEAR(xu[1], 2.0, 1e-12);
	CHECK_NEAR(xu[2], 3.0, 1e-12);

	// chol：A=[[4,2],[2,3]] → L=[[2,0],[1,sqrt(2)]]
	tensor<2, f64> A(std::array<xhqm::size, 2>{ 2, 2 }, 0.0);
	A(0, 0) = 4; A(0, 1) = 2; A(1, 0) = 2; A(1, 1) = 3;
	auto C = tensor_base::chol(A);
	CHECK_NEAR(C(0, 0), 2.0, 1e-12);
	CHECK_NEAR(C(1, 0), 1.0, 1e-12);
	CHECK_NEAR(C(1, 1), std::sqrt(2.0), 1e-12);

	// cumsum_last / cumprod_last：[[1,2,3],[4,5,6]]
	tensor<2, f64> M(std::array<xhqm::size, 2>{ 2, 3 }, 0.0);
	M(0, 0) = 1; M(0, 1) = 2; M(0, 2) = 3; M(1, 0) = 4; M(1, 1) = 5; M(1, 2) = 6;
	auto cs = tensor_base::cumsum_last(M);
	CHECK_NEAR(cs(0, 2), 6.0, 1e-12);
	CHECK_NEAR(cs(1, 2), 15.0, 1e-12);
	auto cp = tensor_base::cumprod_last(M);
	CHECK_NEAR(cp(0, 2), 6.0, 1e-12);
	CHECK_NEAR(cp(1, 2), 120.0, 1e-12);

	// argmax_last / argmin_last
	auto amx = tensor_base::argmax_last(M);
	CHECK((amx[0] == 2 && amx[1] == 2));
	auto amn = tensor_base::argmin_last(M);
	CHECK((amn[0] == 0 && amn[1] == 0));

	// all / any
	tensor<1, bool> tb(std::array<xhqm::size, 1>{ 2 }, true);
	CHECK(tensor_base::all(tb));
	CHECK(tensor_base::any(tb));
	tensor<1, bool> tb2(std::array<xhqm::size, 1>{ 2 }, false);
	CHECK(!tensor_base::all(tb2));
	CHECK(!tensor_base::any(tb2));

	// sort / sort_index：[3,1,4,2] → [1,2,3,4]，索引 [1,3,0,2]
	tensor<1, f64> sv(std::array<xhqm::size, 1>{ 4 }, 0.0);
	sv[0] = 3; sv[1] = 1; sv[2] = 4; sv[3] = 2;
	auto ss = tensor_base::sort(sv);
	CHECK((ss[0] == 1 && ss[3] == 4));
	auto si = tensor_base::sort_index(sv);
	CHECK((si[0] == 1 && si[3] == 2));
}
