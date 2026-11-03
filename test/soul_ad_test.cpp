// reverse-mode AD 测试：数值梯度检查（matmul/relu/sum/mse/add）、线性回归收敛、XOR 收敛。
#include "../algebra/soul/ad.h"
#include "../algebra/soul/activation.h"
#include "../algebra/soul/random.h"
#include "../algebra/soul/recurrent.h"
#include "test.h"
#include <array>
#include <vector>
#include <map>
#include <string>
#include <cmath>

using namespace xhqm;
using namespace xhqm::soul;
using namespace xhqm::soul::ad;

// f = sum(relu(a @ b))，数值梯度核对 a.grad / b.grad
TEST_CASE("test_grad_matmul_relu_sum")
{
	tape<f32> t;
	tensor<2, f32> A(std::array<xhqm::size, 2>{ 2, 2 }, 0.0f);
	A(0, 0) = 1; A(0, 1) = 2; A(1, 0) = 3; A(1, 1) = 4;
	tensor<2, f32> B(std::array<xhqm::size, 2>{ 2, 2 }, 0.0f);
	B(0, 0) = 0.5f; B(0, 1) = -1; B(1, 0) = 2; B(1, 1) = 1;

	auto* va = make_value<2, f32>(t, A);
	auto* vb = make_value<2, f32>(t, B);
	auto* loss = sum_all(t, relu(t, matmul(t, va, vb)));
	t.backward(loss);

	auto fwd = [](const tensor<2, f32>& A, const tensor<2, f32>& B) -> f32 {
		auto C = tensor_base::matmul(A, B);
		auto R = soul::relu(C);
		f32 s = 0; for (xhqm::size i = 0; i < R.size(); ++i) s += R[i];
		return s;
	};
	const f32 eps = 1e-3f;
	auto* ga = as_payload<2, f32>(va->holder());
	auto* gb = as_payload<2, f32>(vb->holder());
	for (xhqm::size i = 0; i < 2; ++i)
		for (xhqm::size j = 0; j < 2; ++j)
		{
			tensor<2, f32> Ap = A, Am = A;
			Ap(i, j) += eps; Am(i, j) -= eps;
			f32 ng = (fwd(Ap, B) - fwd(Am, B)) / (2 * eps);
			CHECK_NEAR(ga->grad(i, j), ng, 1e-2);
			tensor<2, f32> Bp = B, Bm = B;
			Bp(i, j) += eps; Bm(i, j) -= eps;
			f32 ng2 = (fwd(A, Bp) - fwd(A, Bm)) / (2 * eps);
			CHECK_NEAR(gb->grad(i, j), ng2, 1e-2);
		}
}

// f = mse(a @ b, target)，核对 a.grad
TEST_CASE("test_grad_mse")
{
	tape<f32> t;
	tensor<2, f32> A(std::array<xhqm::size, 2>{ 2, 2 }, 0.0f);
	A(0, 0) = 1; A(0, 1) = 0; A(1, 0) = 0; A(1, 1) = 2;
	tensor<2, f32> B(std::array<xhqm::size, 2>{ 2, 2 }, 0.0f);
	B(0, 0) = 1; B(0, 1) = 2; B(1, 0) = 3; B(1, 1) = 4;
	tensor<2, f32> T(std::array<xhqm::size, 2>{ 2, 2 }, 0.0f);
	for (xhqm::size i = 0; i < 4; ++i) T[i] = 0.5f;

	auto* va = make_value<2, f32>(t, A);
	auto* vb = make_value<2, f32>(t, B);
	auto* vt = make_value<2, f32>(t, T);
	auto* loss = mse(t, matmul(t, va, vb), vt);
	t.backward(loss);

	auto fwd = [&](const tensor<2, f32>& A) -> f32 {
		auto C = tensor_base::matmul(A, B);
		f32 s = 0; for (xhqm::size i = 0; i < 4; ++i) { f32 d = C[i] - T[i]; s += d * d; }
		return s / 4.0f;
	};
	const f32 eps = 1e-3f;
	auto* ga = as_payload<2, f32>(va->holder());
	for (xhqm::size i = 0; i < 2; ++i)
		for (xhqm::size j = 0; j < 2; ++j)
		{
			tensor<2, f32> Ap = A, Am = A;
			Ap(i, j) += eps; Am(i, j) -= eps;
			f32 ng = (fwd(Ap) - fwd(Am)) / (2 * eps);
			CHECK_NEAR(ga->grad(i, j), ng, 1e-2);
		}
}

// f = sum(sigmoid(x + b))，核对 x.grad 与标量 bias b.grad
TEST_CASE("test_grad_sigmoid_addscalar")
{
	tape<f32> t;
	tensor<2, f32> X(std::array<xhqm::size, 2>{ 2, 2 }, 0.0f);
	X(0, 0) = 0.3f; X(0, 1) = -0.7f; X(1, 0) = 1.2f; X(1, 1) = 0.5f;
	tensor<0, f32> B(std::array<xhqm::size, 0>{}, 0.4f);

	auto* x = make_value<2, f32>(t, X);
	auto* b = make_value<0, f32>(t, B);
	auto* loss = sum_all(t, sigmoid(t, add_scalar(t, x, b)));
	t.backward(loss);

	auto fwd = [&](const tensor<2, f32>& X, f32 b) -> f32 {
		f32 s = 0;
		for (xhqm::size i = 0; i < 4; ++i)
		{
			f32 z = X[i] + b;
			s += 1.0f / (1.0f + std::exp(-z));
		}
		return s;
	};
	const f32 eps = 1e-3f;
	auto* gx = as_payload<2, f32>(x->holder());
	auto* gb = as_payload<0, f32>(b->holder());
	for (xhqm::size i = 0; i < 4; ++i)
	{
		tensor<2, f32> Xp = X, Xm = X;
		Xp[i] += eps; Xm[i] -= eps;
		f32 ng = (fwd(Xp, B[0]) - fwd(Xm, B[0])) / (2 * eps);
		CHECK_NEAR(gx->grad[i], ng, 1e-2);
	}
	f32 ngb = (fwd(X, B[0] + eps) - fwd(X, B[0] - eps)) / (2 * eps);
	CHECK_NEAR(gb->grad[0], ngb, 1e-2);
}

// 线性回归：y = 2x，无 bias，验证 matmul + mse + SGD 端到端收敛
TEST_CASE("test_linear_regression")
{
	const xhqm::size N = 4;
	tensor<2, f32> X(std::array<xhqm::size, 2>{ N, 1 }, 0.0f);
	tensor<2, f32> Y(std::array<xhqm::size, 2>{ N, 1 }, 0.0f);
	for (xhqm::size i = 0; i < N; ++i) { X(i, 0) = float(i + 1); Y(i, 0) = 2.0f * X(i, 0); }

	tensor<2, f32> W(std::array<xhqm::size, 2>{ 1, 1 }, 0.0f);
	W(0, 0) = 0.3f;

	f32 lr = 0.1f;
	for (int epoch = 0; epoch < 60; ++epoch)
	{
		tape<f32> t;
		auto* x = make_value<2, f32>(t, X);
		auto* y = make_value<2, f32>(t, Y);
		auto* w = make_value<2, f32>(t, W);
		auto* loss = mse(t, matmul(t, x, w), y);
		t.backward(loss);
		auto* gw = as_payload<2, f32>(w->holder());
		W = W - gw->grad * lr;
	}
	CHECK_NEAR(W(0, 0), 2.0f, 1e-2);
}

// XOR：2 层（relu + sigmoid + bias），验证非线性反向与训练收敛
TEST_CASE("test_xor")
{
	tensor<2, f32> X(std::array<xhqm::size, 2>{ 4, 2 }, 0.0f);
	tensor<2, f32> Y(std::array<xhqm::size, 2>{ 4, 1 }, 0.0f);
	const float xs[4][2] = { {0,0},{0,1},{1,0},{1,1} };
	const float ys[4] = { 0,1,1,0 };
	for (xhqm::size i = 0; i < 4; ++i)
		for (xhqm::size j = 0; j < 2; ++j) X(i, j) = xs[i][j];
	for (xhqm::size i = 0; i < 4; ++i) Y(i, 0) = ys[i];

	uint32_t seed = 7;
	tensor<2, f32> W1(std::array<xhqm::size, 2>{ 2, 4 }, 0.0f);
	xavier_uniform(W1, seed);
	tensor<0, f32> B1(std::array<xhqm::size, 0>{}, 0.0f);
	tensor<2, f32> W2(std::array<xhqm::size, 2>{ 4, 1 }, 0.0f);
	xavier_uniform(W2, seed);
	tensor<0, f32> B2(std::array<xhqm::size, 0>{}, 0.0f);

	f32 lr = 0.5f;
	f32 final_loss = 0.0f;
	for (int epoch = 0; epoch < 800; ++epoch)
	{
		tape<f32> t;
		auto* x = make_value<2, f32>(t, X);
		auto* y = make_value<2, f32>(t, Y);
		auto* w1 = make_value<2, f32>(t, W1);
		auto* b1 = make_value<0, f32>(t, B1);
		auto* w2 = make_value<2, f32>(t, W2);
		auto* b2 = make_value<0, f32>(t, B2);

		auto* h = leaky_relu(t, add_scalar(t, matmul(t, x, w1), b1));  // (4,4)
		auto* o = add_scalar(t, matmul(t, h, w2), b2);                 // (4,1) 线性输出
		auto* loss = mse(t, o, y);
		t.backward(loss);

		W1 = W1 - as_payload<2, f32>(w1->holder())->grad * lr;
		B1 = B1 - as_payload<0, f32>(b1->holder())->grad * lr;
		W2 = W2 - as_payload<2, f32>(w2->holder())->grad * lr;
		B2 = B2 - as_payload<0, f32>(b2->holder())->grad * lr;
		final_loss = static_cast<const f32*>(loss->holder()->data())[0];
	}

	// 前向核对最终预测
	auto C1 = tensor_base::matmul(X, W1);
	tensor<2, f32> C1b(std::array<xhqm::size, 2>{ 4, 4 }, 0.0f);
	for (xhqm::size i = 0; i < 4; ++i)
		for (xhqm::size j = 0; j < 4; ++j)
		{
			f32 z = C1(i, j) + B1[0];
			C1b(i, j) = z > 0.0f ? z : 0.01f * z;
		}
	auto C2 = tensor_base::matmul(C1b, W2);
	int correct = 0;
	for (xhqm::size i = 0; i < 4; ++i)
	{
		f32 z = C2(i, 0) + B2[0];
		int pred = z > 0.5f ? 1 : 0;
		if (pred == int(ys[i])) ++correct;
	}
	CHECK(final_loss < 0.05f);
	CHECK(correct == 4);
}

// f = softmax(x)[0]，数值梯度核对 softmax 反向
TEST_CASE("test_grad_softmax")
{
	tape<f32> t;
	tensor<2, f32> X(std::array<xhqm::size, 2>{ 1, 4 }, 0.0f);
	X(0, 0) = 1; X(0, 1) = 2; X(0, 2) = 3; X(0, 3) = 4;
	auto* x = make_value<2, f32>(t, X);
	auto* s = softmax(t, x);
	tensor<2, f32> W(std::array<xhqm::size, 2>{ 1, 4 }, 0.0f); W(0, 0) = 1.0f;
	auto* w = make_value<2, f32>(t, W);
	auto* f = sum_all(t, mul(t, s, w));  // = softmax(x)[0]
	t.backward(f);

	auto fwd = [](const tensor<2, f32>& X) -> f32 {
		return soul::softmax(X)(0, 0);
	};
	const f32 eps = 1e-3f;
	auto* gx = as_payload<2, f32>(x->holder());
	for (xhqm::size j = 0; j < 4; ++j)
	{
		tensor<2, f32> Xp = X, Xm = X;
		Xp(0, j) += eps; Xm(0, j) -= eps;
		f32 ng = (fwd(Xp) - fwd(Xm)) / (2 * eps);
		CHECK_NEAR(gx->grad(0, j), ng, 1e-3);
	}
}

// softmax_cross_entropy 数值梯度核对
TEST_CASE("test_grad_softmax_ce")
{
	tape<f32> t;
	tensor<2, f32> L(std::array<xhqm::size, 2>{ 2, 4 }, 0.0f);
	L(0, 0) = 1; L(0, 1) = 2; L(0, 2) = 3; L(0, 3) = 4;
	L(1, 0) = 0; L(1, 1) = -1; L(1, 2) = 2; L(1, 3) = 1;
	std::vector<xhqm::size> target = { 2, 1 };
	auto* l = make_value<2, f32>(t, L);
	auto* loss = softmax_cross_entropy(t, l, target);
	t.backward(loss);

	auto fwd = [&](const tensor<2, f32>& L) -> f32 {
		f32 r = 0;
		for (xhqm::size i = 0; i < 2; ++i)
		{
			f32 m = L(i, 0);
			for (xhqm::size j = 1; j < 4; ++j) if (L(i, j) > m) m = L(i, j);
			f32 sum = 0;
			for (xhqm::size j = 0; j < 4; ++j) sum += std::exp(L(i, j) - m);
			r += std::log(sum) - (L(i, target[i]) - m);
		}
		return r / 2.0f;
	};
	const f32 eps = 1e-3f;
	auto* gl = as_payload<2, f32>(l->holder());
	for (xhqm::size i = 0; i < 2; ++i)
		for (xhqm::size j = 0; j < 4; ++j)
		{
			tensor<2, f32> Lp = L, Lm = L;
			Lp(i, j) += eps; Lm(i, j) -= eps;
			f32 ng = (fwd(Lp) - fwd(Lm)) / (2 * eps);
			CHECK_NEAR(gl->grad(i, j), ng, 1e-3);
		}
}

// layer_norm 数值梯度核对
TEST_CASE("test_grad_layer_norm")
{
	tape<f32> t;
	tensor<2, f32> X(std::array<xhqm::size, 2>{ 2, 4 }, 0.0f);
	X(0, 0) = 1; X(0, 1) = 2; X(0, 2) = 3; X(0, 3) = 4;
	X(1, 0) = 0; X(1, 1) = -1; X(1, 2) = 2; X(1, 3) = 1;
	tensor<1, f32> G(std::array<xhqm::size, 1>{ 4 }, 1.0f);
	tensor<1, f32> B(std::array<xhqm::size, 1>{ 4 }, 0.5f);
	auto* x = make_value<2, f32>(t, X);
	auto* g = make_value<1, f32>(t, G);
	auto* b = make_value<1, f32>(t, B);
	auto* f = sum_all(t, layer_norm(t, x, g, b));
	t.backward(f);

	auto fwd = [&](const tensor<2, f32>& X, const tensor<1, f32>& G, const tensor<1, f32>& B) -> f32 {
		f32 r = 0;
		for (xhqm::size i = 0; i < 2; ++i)
		{
			f32 mean = 0; for (xhqm::size j = 0; j < 4; ++j) mean += X(i, j); mean /= 4.0f;
			f32 var = 0; for (xhqm::size j = 0; j < 4; ++j) { f32 d = X(i, j) - mean; var += d * d; } var /= 4.0f;
			f32 rstd = 1.0f / std::sqrt(var + 1e-5f);
			for (xhqm::size j = 0; j < 4; ++j) r += (X(i, j) - mean) * rstd * G[j] + B[j];
		}
		return r;
	};
	const f32 eps = 1e-3f;
	auto* gx = as_payload<2, f32>(x->holder());
	auto* gg = as_payload<1, f32>(g->holder());
	auto* gb = as_payload<1, f32>(b->holder());
	for (xhqm::size i = 0; i < 2; ++i)
		for (xhqm::size j = 0; j < 4; ++j)
		{
			tensor<2, f32> Xp = X, Xm = X;
			Xp(i, j) += eps; Xm(i, j) -= eps;
			f32 ng = (fwd(Xp, G, B) - fwd(Xm, G, B)) / (2 * eps);
			CHECK_NEAR(gx->grad(i, j), ng, 2e-3);
		}
	for (xhqm::size j = 0; j < 4; ++j)
	{
		tensor<1, f32> Gp = G, Gm = G;
		Gp[j] += eps; Gm[j] -= eps;
		f32 ng = (fwd(X, Gp, B) - fwd(X, Gm, B)) / (2 * eps);
		CHECK_NEAR(gg->grad[j], ng, 2e-3);
		tensor<1, f32> Bp = B, Bm = B;
		Bp[j] += eps; Bm[j] -= eps;
		f32 ngb = (fwd(X, G, Bp) - fwd(X, G, Bm)) / (2 * eps);
		CHECK_NEAR(gb->grad[j], ngb, 2e-3);
	}
}

// embedding 查表梯度（解析核对：梯度按出现次数 scatter-add）
TEST_CASE("test_grad_embedding")
{
	tape<f32> t;
	tensor<2, f32> W(std::array<xhqm::size, 2>{ 4, 3 }, 0.0f);
	for (xhqm::size i = 0; i < 4; ++i)
		for (xhqm::size j = 0; j < 3; ++j)
			W(i, j) = float((i + 1) * (j + 1)) * 0.1f;
	auto* w = make_value<2, f32>(t, W);
	std::vector<xhqm::size> ids = { 0, 2, 0 };
	auto* f = sum_all(t, embedding_lookup(t, w, ids));
	t.backward(f);

	auto* gw = as_payload<2, f32>(w->holder());
	for (xhqm::size j = 0; j < 3; ++j)
	{
		CHECK_NEAR(gw->grad(0, j), 2.0f, 1e-5);  // id 0 出现 2 次
		CHECK_NEAR(gw->grad(2, j), 1.0f, 1e-5);  // id 2 出现 1 次
		CHECK_NEAR(gw->grad(1, j), 0.0f, 1e-5);
		CHECK_NEAR(gw->grad(3, j), 0.0f, 1e-5);
	}
}

// attention 前向手算（供数值梯度）
static f32 attention_fwd(const tensor<2, f32>& X, const tensor<2, f32>& Wq, const tensor<2, f32>& Wk,
	const tensor<2, f32>& Wv, const tensor<2, f32>& Wo, const tensor<2, f32>& mask, f32 scale)
{
	const xhqm::size seq = X.dimension(0);
	auto Q = tensor_base::matmul(X, Wq);
	auto K = tensor_base::matmul(X, Wk);
	auto V = tensor_base::matmul(X, Wv);
	auto scores = tensor_base::matmul(Q, tensor_base::transpose(K, 0, 1));
	for (xhqm::size i = 0; i < seq; ++i)
		for (xhqm::size j = 0; j < seq; ++j)
			scores(i, j) = scores(i, j) * scale + mask(i, j);
	auto probs = soul::softmax(scores);
	auto out = tensor_base::matmul(tensor_base::matmul(probs, V), Wo);
	f32 r = 0; for (xhqm::size i = 0; i < out.size(); ++i) r += out[i];
	return r;
}

// attention 数值梯度核对（查 wq/wk/wv/wo）
TEST_CASE("test_grad_attention")
{
	tape<f32> t;
	const xhqm::size seq = 3, h = 2;
	tensor<2, f32> X(std::array<xhqm::size, 2>{ seq, h }, 0.0f);
	X(0, 0) = 1; X(0, 1) = 0; X(1, 0) = 0.5f; X(1, 1) = 1; X(2, 0) = 0; X(2, 1) = -1;
	tensor<2, f32> Wq(std::array<xhqm::size, 2>{ h, h }, 0.0f);
	Wq(0, 0) = 0.5f; Wq(0, 1) = -0.3f; Wq(1, 0) = 0.2f; Wq(1, 1) = 0.7f;
	tensor<2, f32> Wk(std::array<xhqm::size, 2>{ h, h }, 0.0f);
	Wk(0, 0) = -0.4f; Wk(0, 1) = 0.6f; Wk(1, 0) = 0.3f; Wk(1, 1) = 0.1f;
	tensor<2, f32> Wv(std::array<xhqm::size, 2>{ h, h }, 0.0f);
	Wv(0, 0) = 0.8f; Wv(0, 1) = -0.2f; Wv(1, 0) = 0.4f; Wv(1, 1) = 0.5f;
	tensor<2, f32> Wo(std::array<xhqm::size, 2>{ h, h }, 0.0f);
	Wo(0, 0) = 0.3f; Wo(0, 1) = 0.9f; Wo(1, 0) = -0.5f; Wo(1, 1) = 0.2f;

	auto* x = make_value<2, f32>(t, X);
	auto* wq = make_value<2, f32>(t, Wq);
	auto* wk = make_value<2, f32>(t, Wk);
	auto* wv = make_value<2, f32>(t, Wv);
	auto* wo = make_value<2, f32>(t, Wo);
	auto mask = causal_mask<f32>(seq);
	f32 scale = 1.0f / std::sqrt(float(h));
	auto* f = sum_all(t, attention(t, x, wq, wk, wv, wo, mask, scale));
	t.backward(f);

	const f32 eps = 1e-3f;

	// 直接逐参数数值梯度（wq）
	auto* gq = as_payload<2, f32>(wq->holder());
	for (xhqm::size i = 0; i < h; ++i)
		for (xhqm::size j = 0; j < h; ++j)
		{
			tensor<2, f32> Wp = Wq, Wm = Wq;
			Wp(i, j) += eps; Wm(i, j) -= eps;
			f32 ng = (attention_fwd(X, Wp, Wk, Wv, Wo, mask, scale) - attention_fwd(X, Wm, Wk, Wv, Wo, mask, scale)) / (2 * eps);
			CHECK_NEAR(gq->grad(i, j), ng, 2e-3);
		}
	auto* gk = as_payload<2, f32>(wk->holder());
	for (xhqm::size i = 0; i < h; ++i)
		for (xhqm::size j = 0; j < h; ++j)
		{
			tensor<2, f32> Wp = Wk, Wm = Wk;
			Wp(i, j) += eps; Wm(i, j) -= eps;
			f32 ng = (attention_fwd(X, Wq, Wp, Wv, Wo, mask, scale) - attention_fwd(X, Wq, Wm, Wv, Wo, mask, scale)) / (2 * eps);
			CHECK_NEAR(gk->grad(i, j), ng, 2e-3);
		}
	auto* gv = as_payload<2, f32>(wv->holder());
	for (xhqm::size i = 0; i < h; ++i)
		for (xhqm::size j = 0; j < h; ++j)
		{
			tensor<2, f32> Wp = Wv, Wm = Wv;
			Wp(i, j) += eps; Wm(i, j) -= eps;
			f32 ng = (attention_fwd(X, Wq, Wk, Wp, Wo, mask, scale) - attention_fwd(X, Wq, Wk, Wm, Wo, mask, scale)) / (2 * eps);
			CHECK_NEAR(gv->grad(i, j), ng, 2e-3);
		}
	auto* go = as_payload<2, f32>(wo->holder());
	for (xhqm::size i = 0; i < h; ++i)
		for (xhqm::size j = 0; j < h; ++j)
		{
			tensor<2, f32> Wp = Wo, Wm = Wo;
			Wp(i, j) += eps; Wm(i, j) -= eps;
			f32 ng = (attention_fwd(X, Wq, Wk, Wv, Wp, mask, scale) - attention_fwd(X, Wq, Wk, Wv, Wm, mask, scale)) / (2 * eps);
			CHECK_NEAR(go->grad(i, j), ng, 2e-3);
		}
}

// 最小 GPT 端到端训练：embedding + 单头注意力 + 残差 + layer_norm + FFN + lm_head，
// 字符级预测下一个 token，验证 loss 下降。
TEST_CASE("test_gpt_train")
{
	const xhqm::size vocab = 8, hidden = 16, ffn = 32, seq = 8;
	uint32_t seed = 42;

	// 参数（跨 epoch 持久）
	tensor<2, f32> Wte(std::array<xhqm::size, 2>{ vocab, hidden }, 0.0f); fill_normal(Wte, 0.0f, 0.02f, seed);
	tensor<2, f32> Wq(std::array<xhqm::size, 2>{ hidden, hidden }, 0.0f); xavier_uniform(Wq, seed);
	tensor<2, f32> Wk(std::array<xhqm::size, 2>{ hidden, hidden }, 0.0f); xavier_uniform(Wk, seed);
	tensor<2, f32> Wv(std::array<xhqm::size, 2>{ hidden, hidden }, 0.0f); xavier_uniform(Wv, seed);
	tensor<2, f32> Wo(std::array<xhqm::size, 2>{ hidden, hidden }, 0.0f); xavier_uniform(Wo, seed);
	tensor<2, f32> W1(std::array<xhqm::size, 2>{ hidden, ffn }, 0.0f); xavier_uniform(W1, seed);
	tensor<2, f32> W2(std::array<xhqm::size, 2>{ ffn, hidden }, 0.0f); xavier_uniform(W2, seed);
	tensor<1, f32> G(std::array<xhqm::size, 1>{ hidden }, 1.0f);
	tensor<1, f32> B(std::array<xhqm::size, 1>{ hidden }, 0.0f);
	tensor<2, f32> Wlm(std::array<xhqm::size, 2>{ hidden, vocab }, 0.0f); xavier_uniform(Wlm, seed);

	// 数据：0..7 循环，预测下一个 token
	std::vector<xhqm::size> input = { 0, 1, 2, 3, 4, 5, 6 };
	std::vector<xhqm::size> target = { 1, 2, 3, 4, 5, 6, 7 };
	auto mask = causal_mask<f32>(seq - 1);
	const f32 scale = 1.0f / std::sqrt(float(hidden));
	const f32 lr = 0.05f;

	f32 init_loss = 0.0f, final_loss = 0.0f;
	for (int epoch = 0; epoch < 300; ++epoch)
	{
		tape<f32> t;
		auto* wte = make_value<2, f32>(t, Wte);
		auto* wq = make_value<2, f32>(t, Wq);
		auto* wk = make_value<2, f32>(t, Wk);
		auto* wv = make_value<2, f32>(t, Wv);
		auto* wo = make_value<2, f32>(t, Wo);
		auto* w1 = make_value<2, f32>(t, W1);
		auto* w2 = make_value<2, f32>(t, W2);
		auto* g = make_value<1, f32>(t, G);
		auto* b = make_value<1, f32>(t, B);
		auto* wlm = make_value<2, f32>(t, Wlm);

		auto* x = embedding_lookup(t, wte, input);           // (7, hidden)
		auto* a = attention(t, x, wq, wk, wv, wo, mask, scale);
		auto* h = add(t, x, a);                              // 残差
		h = layer_norm(t, h, g, b);
		auto* f = matmul(t, relu(t, matmul(t, h, w1)), w2);  // FFN
		h = add(t, h, f);                                    // 残差
		auto* logits = matmul(t, h, wlm);                    // (7, vocab)
		auto* loss = softmax_cross_entropy(t, logits, target);
		t.backward(loss);

		final_loss = static_cast<const f32*>(loss->holder()->data())[0];
		if (epoch == 0) init_loss = final_loss;

		Wte = Wte - as_payload<2, f32>(wte->holder())->grad * lr;
		Wq = Wq - as_payload<2, f32>(wq->holder())->grad * lr;
		Wk = Wk - as_payload<2, f32>(wk->holder())->grad * lr;
		Wv = Wv - as_payload<2, f32>(wv->holder())->grad * lr;
		Wo = Wo - as_payload<2, f32>(wo->holder())->grad * lr;
		W1 = W1 - as_payload<2, f32>(w1->holder())->grad * lr;
		W2 = W2 - as_payload<2, f32>(w2->holder())->grad * lr;
		G = G - as_payload<1, f32>(g->holder())->grad * lr;
		B = B - as_payload<1, f32>(b->holder())->grad * lr;
		Wlm = Wlm - as_payload<2, f32>(wlm->holder())->grad * lr;
	}
	CHECK(init_loss > 1.0f);
	CHECK(final_loss < 0.3f);   // loss 显著下降
}

// unary 标量语义（unary_eval：串行/后端共用的单一份回退）
TEST_CASE("test_unary_trig")
{
	CHECK_NEAR(tensor_base::unary_eval(0.0f, tensor_base::unary_sin), 0.0f, 1e-6);
	CHECK_NEAR(tensor_base::unary_eval(1.0f, tensor_base::unary_sin), std::sin(1.0f), 1e-6);
	CHECK_NEAR(tensor_base::unary_eval(-0.3f, tensor_base::unary_sin), std::sin(-0.3f), 1e-6);
	CHECK_NEAR(tensor_base::unary_eval(0.0f, tensor_base::unary_cos), 1.0f, 1e-6);
	CHECK_NEAR(tensor_base::unary_eval(1.0f, tensor_base::unary_cos), std::cos(1.0f), 1e-6);
	CHECK_NEAR(tensor_base::unary_eval(0.5f, tensor_base::unary_tan), std::tan(0.5f), 1e-6);
	CHECK_NEAR(tensor_base::unary_eval(0.5f, tensor_base::unary_asin), std::asin(0.5f), 1e-6);
	CHECK_NEAR(tensor_base::unary_eval(1.0f, tensor_base::unary_acos), std::acos(1.0f), 1e-6);
	CHECK_NEAR(tensor_base::unary_eval(0.5f, tensor_base::unary_atan), std::atan(0.5f), 1e-6);
}

// unary 扩展算子标量语义（双曲/对数/取整/误差/gamma/符号）
TEST_CASE("test_unary_extended")
{
	CHECK_NEAR(tensor_base::unary_eval(0.5f, tensor_base::unary_sinh), std::sinh(0.5f), 1e-6);
	CHECK_NEAR(tensor_base::unary_eval(0.5f, tensor_base::unary_cosh), std::cosh(0.5f), 1e-6);
	CHECK_NEAR(tensor_base::unary_eval(0.5f, tensor_base::unary_asinh), std::asinh(0.5f), 1e-6);
	CHECK_NEAR(tensor_base::unary_eval(1.5f, tensor_base::unary_acosh), std::acosh(1.5f), 1e-6);
	CHECK_NEAR(tensor_base::unary_eval(0.3f, tensor_base::unary_atanh), std::atanh(0.3f), 1e-6);
	CHECK_NEAR(tensor_base::unary_eval(2.0f, tensor_base::unary_log10), std::log10(2.0f), 1e-6);
	CHECK_NEAR(tensor_base::unary_eval(2.0f, tensor_base::unary_log2), std::log2(2.0f), 1e-6);
	CHECK_NEAR(tensor_base::unary_eval(1.5f, tensor_base::unary_floor), std::floor(1.5f), 1e-6);
	CHECK_NEAR(tensor_base::unary_eval(1.5f, tensor_base::unary_ceil), std::ceil(1.5f), 1e-6);
	CHECK_NEAR(tensor_base::unary_eval(1.5f, tensor_base::unary_round), std::round(1.5f), 1e-6);
	CHECK_NEAR(tensor_base::unary_eval(1.5f, tensor_base::unary_trunc), std::trunc(1.5f), 1e-6);
	CHECK_NEAR(tensor_base::unary_eval(0.5f, tensor_base::unary_expm1), std::expm1(0.5f), 1e-6);
	CHECK_NEAR(tensor_base::unary_eval(0.5f, tensor_base::unary_log1p), std::log1p(0.5f), 1e-6);
	CHECK_NEAR(tensor_base::unary_eval(0.5f, tensor_base::unary_erf), std::erf(0.5f), 1e-6);
	CHECK_NEAR(tensor_base::unary_eval(0.5f, tensor_base::unary_erfc), std::erfc(0.5f), 1e-6);
	CHECK_NEAR(tensor_base::unary_eval(2.0f, tensor_base::unary_tgamma), std::tgamma(2.0f), 1e-6);
	CHECK_NEAR(tensor_base::unary_eval(2.0f, tensor_base::unary_lgamma), std::lgamma(2.0f), 1e-6);
	CHECK_NEAR(tensor_base::unary_eval(1.5f, tensor_base::unary_abs), std::fabs(1.5f), 1e-6);
	CHECK_NEAR(tensor_base::unary_eval(1.5f, tensor_base::unary_sign), 1.0f, 1e-6);
	CHECK_NEAR(tensor_base::unary_eval(0.3f, tensor_base::unary_sign), 1.0f, 1e-6);
}

// elemwise 二元标量语义（atan2/hypot/pow/mod/rem，elemwise_eval）
TEST_CASE("test_elemwise_extended")
{
	CHECK_NEAR(tensor_base::elemwise_eval(1.0f, 2.0f, tensor_base::elem_atan2), std::atan2(1.0f, 2.0f), 1e-6);
	CHECK_NEAR(tensor_base::elemwise_eval(1.0f, 2.0f, tensor_base::elem_hypot), std::hypot(1.0f, 2.0f), 1e-6);
	CHECK_NEAR(tensor_base::elemwise_eval(2.0f, 4.0f, tensor_base::elem_pow), std::pow(2.0f, 4.0f), 1e-5);
	CHECK_NEAR(tensor_base::elemwise_eval(3.0f, 4.0f, tensor_base::elem_mod), 3.0f, 1e-6);   // mod(3,4)=3
	CHECK_NEAR(tensor_base::elemwise_eval(5.0f, 2.0f, tensor_base::elem_mod), 1.0f, 1e-6);   // mod(5,2)=1
	CHECK_NEAR(tensor_base::elemwise_eval(5.0f, 2.0f, tensor_base::elem_rem), std::fmod(5.0f, 2.0f), 1e-6);  // rem(5,2)=1
	CHECK_NEAR(tensor_base::elemwise_eval(2.0f, 4.0f, tensor_base::elem_rem), std::fmod(2.0f, 4.0f), 1e-6);  // rem(2,4)=2
}

// unary 幂/根/倒数/取负/平方标量语义
TEST_CASE("test_unary_pow_root")
{
	CHECK_NEAR(tensor_base::unary_eval(2.0f, tensor_base::unary_exp2), std::exp2(2.0f), 1e-6);
	CHECK_NEAR(tensor_base::unary_eval(2.0f, tensor_base::unary_cbrt), std::cbrt(2.0f), 1e-6);
	CHECK_NEAR(tensor_base::unary_eval(4.0f, tensor_base::unary_rsqrt), 1.0f / std::sqrt(4.0f), 1e-6);
	CHECK_NEAR(tensor_base::unary_eval(0.5f, tensor_base::unary_recip), 1.0f / 0.5f, 1e-6);
	CHECK_NEAR(tensor_base::unary_eval(1.5f, tensor_base::unary_neg), -1.5f, 1e-6);
	CHECK_NEAR(tensor_base::unary_eval(1.5f, tensor_base::unary_square), 1.5f * 1.5f, 1e-6);
}

// reduce_prod：沿最后一维与任意维归约（no 后端走通用回退路径）
TEST_CASE("test_reduce_prod")
{
	tensor<2, f32> x(std::array<xhqm::size, 2>{ 2, 3 }, 0.0f);
	x(0, 0) = 1; x(0, 1) = 2; x(0, 2) = 3;
	x(1, 0) = 4; x(1, 1) = 5; x(1, 2) = 6;

	tensor<1, f32> r1 = tensor_base::reduce_prod(x, 1);  // [6, 120]
	CHECK_NEAR(r1[0], 6.0f, 1e-6);
	CHECK_NEAR(r1[1], 120.0f, 1e-6);

	tensor<1, f32> r0 = tensor_base::reduce_prod(x, 0);  // [4, 10, 18]
	CHECK_NEAR(r0[0], 4.0f, 1e-6);
	CHECK_NEAR(r0[1], 10.0f, 1e-6);
	CHECK_NEAR(r0[2], 18.0f, 1e-6);
}

// —— 整图序列化：save_tape / load_tape 往返（值一致 + 梯度一致）——
namespace
{
	// 取节点前向值 / 梯度为 vector（f32 视图）
	std::vector<f32> node_value_vec(value<f32>* v)
	{
		const f32* p = static_cast<const f32*>(v->holder()->data());
		return std::vector<f32>(p, p + v->holder()->size());
	}
	std::vector<f32> node_grad_vec(value<f32>* v)
	{
		const f32* p = static_cast<const f32*>(v->grad());
		return std::vector<f32>(p, p + v->holder()->size());
	}

	// 找到唯一汇节点（无消费者的结果节点）
	value<f32>* find_sink(tape<f32>& t)
	{
		value<f32>* sink = nullptr;
		for (auto* v : t.all())
			if (v->consumers().empty()) { sink = v; break; }
		return sink;
	}

	// 保存→加载→校验全部节点值一致→重算梯度与命名叶子一致
	void roundtrip_check(tape<f32>& t, value<f32>* loss)
	{
		t.backward(loss);
		std::vector<std::vector<f32>> vals_before;
		for (auto* v : t.all()) vals_before.push_back(node_value_vec(v));
		std::map<std::string, std::vector<f32>> grads_before;
		for (auto* v : t.all()) if (!v->name().empty()) grads_before[v->name()] = node_grad_vec(v);

		auto bytes = json_to_cbor(save_tape(t));
		tape<f32> t2 = load_tape<f32>(cbor_to_json(bytes));

		auto a = t.all(), b = t2.all();
		REQUIRE(a.size() == b.size());
		for (xhqm::size i = 0; i < a.size(); ++i)
		{
			REQUIRE(a[i]->holder()->size() == b[i]->holder()->size());
			const f32* p = static_cast<const f32*>(a[i]->holder()->data());
			const f32* q = static_cast<const f32*>(b[i]->holder()->data());
			for (xhqm::size k = 0; k < a[i]->holder()->size(); ++k)
				CHECK_NEAR(p[k], q[k], 1e-6);
		}

		value<f32>* loss2 = find_sink(t2);
		REQUIRE(loss2 != nullptr);
		t2.backward(loss2);
		std::map<std::string, std::vector<f32>> grads_after;
		for (auto* v : t2.all()) if (!v->name().empty()) grads_after[v->name()] = node_grad_vec(v);

		REQUIRE(grads_before.size() == grads_after.size());
		for (auto& kv : grads_before)
		{
			REQUIRE(grads_after.count(kv.first) == 1);
			auto& g = grads_after[kv.first];
			REQUIRE(g.size() == kv.second.size());
			for (xhqm::size i = 0; i < g.size(); ++i)
				CHECK_NEAR(g[i], kv.second[i], 1e-5);
		}
	}
}

// 小 GPT block：覆盖 embedding/attention(含 matmul/transpose/mul_scalar/add/softmax)/layer_norm/relu/softmax_ce
TEST_CASE("test_graph_roundtrip")
{
	const xhqm::size vocab = 6, hidden = 8;
	uint32_t seed = 7;
	tensor<2, f32> Wte(std::array<xhqm::size, 2>{ vocab, hidden }, 0.0f); fill_normal(Wte, 0.0f, 0.05f, seed);
	tensor<2, f32> Wq(std::array<xhqm::size, 2>{ hidden, hidden }, 0.0f); xavier_uniform(Wq, seed);
	tensor<2, f32> Wk(std::array<xhqm::size, 2>{ hidden, hidden }, 0.0f); xavier_uniform(Wk, seed);
	tensor<2, f32> Wv(std::array<xhqm::size, 2>{ hidden, hidden }, 0.0f); xavier_uniform(Wv, seed);
	tensor<2, f32> Wo(std::array<xhqm::size, 2>{ hidden, hidden }, 0.0f); xavier_uniform(Wo, seed);
	tensor<2, f32> W1(std::array<xhqm::size, 2>{ hidden, hidden * 2 }, 0.0f); xavier_uniform(W1, seed);
	tensor<2, f32> W2(std::array<xhqm::size, 2>{ hidden * 2, hidden }, 0.0f); xavier_uniform(W2, seed);
	tensor<1, f32> G(std::array<xhqm::size, 1>{ hidden }, 1.0f);
	tensor<1, f32> B(std::array<xhqm::size, 1>{ hidden }, 0.0f);
	tensor<2, f32> Wlm(std::array<xhqm::size, 2>{ hidden, vocab }, 0.0f); xavier_uniform(Wlm, seed);

	tape<f32> t;
	auto* wte = make_value<2, f32>(t, Wte, "leaf", "Wte");
	auto* wq  = make_value<2, f32>(t, Wq,  "leaf", "Wq");
	auto* wk  = make_value<2, f32>(t, Wk,  "leaf", "Wk");
	auto* wv  = make_value<2, f32>(t, Wv,  "leaf", "Wv");
	auto* wo  = make_value<2, f32>(t, Wo,  "leaf", "Wo");
	auto* w1  = make_value<2, f32>(t, W1,  "leaf", "W1");
	auto* w2  = make_value<2, f32>(t, W2,  "leaf", "W2");
	auto* g   = make_value<1, f32>(t, G,   "leaf", "G");
	auto* b   = make_value<1, f32>(t, B,   "leaf", "B");
	auto* wlm = make_value<2, f32>(t, Wlm, "leaf", "Wlm");

	std::vector<xhqm::size> input_ids = { 0, 1, 2 };
	std::vector<xhqm::size> target_ids = { 1, 2, 3 };
	auto* x = embedding_lookup(t, wte, input_ids);
	auto* a = attention(t, x, wq, wk, wv, wo, causal_mask<f32>(input_ids.size()), 1.0f / std::sqrt(float(hidden)));
	auto* h = add(t, x, a);
	h = layer_norm(t, h, g, b);
	auto* f = matmul(t, relu(t, matmul(t, h, w1)), w2);
	h = add(t, h, f);
	auto* logits = matmul(t, h, wlm);
	auto* loss = softmax_cross_entropy(t, logits, target_ids);

	roundtrip_check(t, loss);
}

// 逐元素/标量/损失：mul/add_scalar/leaky_relu/sigmoid/sum_all + mse/mul_scalar/transpose
TEST_CASE("test_graph_roundtrip_elemwise")
{
	tape<f32> t;
	auto* x = make_value<1, f32>(t, tensor<1, f32>(std::array<xhqm::size, 1>{ 4 }, 0.0f), "leaf", "x");
	auto* y = make_value<1, f32>(t, tensor<1, f32>(std::array<xhqm::size, 1>{ 4 }, 0.0f), "leaf", "y");
	auto* s = make_value<0, f32>(t, tensor<0, f32>(std::array<xhqm::size, 0>{}, 0.0f), "leaf", "s");
	for (xhqm::size i = 0; i < 4; ++i) { static_cast<f32*>(x->raw_data())[i] = f32(i) * 0.25f + 0.1f; static_cast<f32*>(y->raw_data())[i] = 0.5f; }
	static_cast<f32*>(s->raw_data())[0] = 1.5f;

	auto* m = mul(t, x, y);
	auto* as_ = add_scalar(t, m, s);
	auto* lr = leaky_relu(t, as_, 0.1f);
	auto* sg = sigmoid(t, lr);
	auto* loss = sum_all(t, sg);

	roundtrip_check(t, loss);

	// 第二个小图：mse（双输入）+ transpose + mul_scalar
	{
		tape<f32> t2;
		auto* pred = make_value<2, f32>(t2, tensor<2, f32>(std::array<xhqm::size, 2>{ 2, 2 }, 0.0f), "leaf", "pred");
		auto* tgt  = make_value<2, f32>(t2, tensor<2, f32>(std::array<xhqm::size, 2>{ 2, 2 }, 0.0f), "leaf", "tgt");
		auto* s2   = make_value<0, f32>(t2, tensor<0, f32>(std::array<xhqm::size, 0>{}, 0.0f), "leaf", "s2");
		for (xhqm::size i = 0; i < 4; ++i) { static_cast<f32*>(pred->raw_data())[i] = f32(i) * 0.3f; static_cast<f32*>(tgt->raw_data())[i] = 1.0f; }
		static_cast<f32*>(s2->raw_data())[0] = 2.0f;

		auto* tr = transpose(t2, pred);
		auto* ms = mul_scalar(t2, tr, s2);
		auto* l2 = mse(t2, ms, tgt);
		roundtrip_check(t2, l2);
	}
}

// slice_columns / concat_columns 梯度：线性算子，梯度 = 散射/切片（恒等）的索引正确性
TEST_CASE("test_grad_slice_concat")
{
	// slice：loss = sum(slice(x, 1, 3)) → x 的梯度在列 [1,3) 为 1，其余 0
	{
		tape<f32> t;
		tensor<2, f32> X(std::array<xhqm::size, 2>{ 2, 4 }, 0.0f);
		for (xhqm::size i = 0; i < 8; ++i) X[i] = f32(i);
		auto* x = make_value<2, f32>(t, X);
		auto* loss = sum_all(t, slice_columns(t, x, 1, 3));
		t.backward(loss);
		auto* gx = as_payload<2, f32>(x->holder());
		for (xhqm::size r = 0; r < 2; ++r)
			for (xhqm::size c = 0; c < 4; ++c)
				CHECK_NEAR(gx->grad(r, c), (c >= 1 && c < 3) ? 1.0f : 0.0f, 1e-6);
	}
	// concat：loss = sum(concat([x1, x2])) → 各输入梯度全 1
	{
		tape<f32> t;
		tensor<2, f32> X1(std::array<xhqm::size, 2>{ 2, 2 }, 0.0f);
		tensor<2, f32> X2(std::array<xhqm::size, 2>{ 2, 3 }, 0.0f);
		for (xhqm::size i = 0; i < 4; ++i) X1[i] = f32(i);
		for (xhqm::size i = 0; i < 6; ++i) X2[i] = f32(i + 10);
		auto* x1 = make_value<2, f32>(t, X1);
		auto* x2 = make_value<2, f32>(t, X2);
		auto* loss = sum_all(t, concat_columns(t, std::vector<value<f32>*>{ x1, x2 }));
		t.backward(loss);
		auto* g1 = as_payload<2, f32>(x1->holder());
		auto* g2 = as_payload<2, f32>(x2->holder());
		for (xhqm::size i = 0; i < g1->grad.size(); ++i) CHECK_NEAR(g1->grad[i], 1.0f, 1e-6);
		for (xhqm::size i = 0; i < g2->grad.size(); ++i) CHECK_NEAR(g2->grad[i], 1.0f, 1e-6);
	}
}

// 多头注意力：前向形状 + 整图往返（覆盖 slice/concat 的反向重建）
TEST_CASE("test_multi_head_attention_roundtrip")
{
	const xhqm::size seq = 3, hidden = 6, heads = 2;
	uint32_t seed = 11;
	tensor<2, f32> X(std::array<xhqm::size, 2>{ seq, hidden }, 0.0f); fill_normal(X, 0.0f, 0.2f, seed);
	tensor<2, f32> Wq(std::array<xhqm::size, 2>{ hidden, hidden }, 0.0f); xavier_uniform(Wq, seed);
	tensor<2, f32> Wk(std::array<xhqm::size, 2>{ hidden, hidden }, 0.0f); xavier_uniform(Wk, seed);
	tensor<2, f32> Wv(std::array<xhqm::size, 2>{ hidden, hidden }, 0.0f); xavier_uniform(Wv, seed);
	tensor<2, f32> Wo(std::array<xhqm::size, 2>{ hidden, hidden }, 0.0f); xavier_uniform(Wo, seed);

	tape<f32> t;
	auto* x  = make_value<2, f32>(t, X,  "leaf", "x");
	auto* wq = make_value<2, f32>(t, Wq, "leaf", "Wq");
	auto* wk = make_value<2, f32>(t, Wk, "leaf", "Wk");
	auto* wv = make_value<2, f32>(t, Wv, "leaf", "Wv");
	auto* wo = make_value<2, f32>(t, Wo, "leaf", "Wo");

	const f32 scale = 1.0f / std::sqrt(float(hidden / heads));
	auto* out = multi_head_attention(t, x, wq, wk, wv, wo, heads, causal_mask<f32>(seq), scale);
	REQUIRE(out->shape()[0] == seq);
	REQUIRE(out->shape()[1] == hidden);
	auto* loss = sum_all(t, out);
	roundtrip_check(t, loss);
}

// Adam：线性回归收敛（含偏置修正，moment 跨 step 持久）
TEST_CASE("test_adam")
{
	const xhqm::size n = 8, d = 3;
	uint32_t seed = 5;
	tensor<2, f32> X(std::array<xhqm::size, 2>{ n, d }, 0.0f); fill_normal(X, 0.0f, 1.0f, seed);
	tensor<2, f32> Y(std::array<xhqm::size, 2>{ n, 1 }, 0.0f);
	for (xhqm::size i = 0; i < n; ++i)
		Y(i, 0) = 1.5f * X(i, 0) - 2.0f * X(i, 1) + 0.5f * X(i, 2);
	tensor<2, f32> W(std::array<xhqm::size, 2>{ d, 1 }, 0.0f); fill_normal(W, 0.0f, 0.1f, seed);
	tensor<2, f32> mW(std::array<xhqm::size, 2>{ d, 1 }, 0.0f);
	tensor<2, f32> vW(std::array<xhqm::size, 2>{ d, 1 }, 0.0f);
	const f32 lr = 0.05f;

	f32 init_loss = 0.0f, final_loss = 0.0f;
	for (int e = 0; e < 300; ++e)
	{
		tape<f32> tp;
		auto* w = make_value<2, f32>(tp, W, "leaf", "W");
		auto* x = make_value<2, f32>(tp, X);
		auto* y = make_value<2, f32>(tp, Y);
		auto* loss = mse(tp, matmul(tp, x, w), y);
		tp.backward(loss);
		final_loss = static_cast<const f32*>(loss->holder()->data())[0];
		if (e == 0) init_loss = final_loss;
		adam_step<2, f32>(W, as_payload<2, f32>(w->holder())->grad, lr, xhqm::size(e + 1), mW, vW);
	}
	CHECK(init_loss > final_loss);
	CHECK(final_loss < 0.05f);
}

// GPT 训练闭环（module + adam）：embedding + 多头注意力 + layer_norm + FFN 端到端收敛
TEST_CASE("test_gpt_module")
{
	const xhqm::size vocab = 8, hidden = 16, ffn = 32, heads = 2;
	uint32_t seed = 42;
	gpt<f32> model;
	model.init(vocab, hidden, ffn, heads, seed);
	adam_optimizer<f32> opt(0.01f);

	std::vector<xhqm::size> input = { 0, 1, 2, 3, 4, 5, 6 };
	std::vector<xhqm::size> target = { 1, 2, 3, 4, 5, 6, 7 };

	f32 init_loss = 0.0f, final_loss = 0.0f;
	for (int e = 0; e < 300; ++e)
	{
		tape<f32> t;
		auto* loss = model.forward(t, input, target);
		t.backward(loss);
		final_loss = static_cast<const f32*>(loss->holder()->data())[0];
		if (e == 0) init_loss = final_loss;
		opt.step(model);
	}
	CHECK(init_loss > 1.0f);
	CHECK(final_loss < 0.3f);
}

// module 参数快照（state_dict）：save_json / read_json 往返
TEST_CASE("test_module_state_dict")
{
	uint32_t seed = 9;
	gpt<f32> model;
	model.init(6, 8, 16, 2, seed);

	auto json = model.save_json();
	gpt<f32> model2;
	model2.init(6, 8, 16, 2, seed);   // 重新初始化后读回覆盖
	model2.read_json(json);

	REQUIRE(model.params.size() == model2.params.size());
	for (xhqm::size i = 0; i < model.params.size(); ++i)
	{
		REQUIRE(model.params[i].name == model2.params[i].name);
		tensor_holder* a = model.params[i].payload.get();
		tensor_holder* b = model2.params[i].payload.get();
		REQUIRE(a->size() == b->size());
		const f32* pa = static_cast<const f32*>(a->data());
		const f32* pb = static_cast<const f32*>(b->data());
		for (xhqm::size k = 0; k < a->size(); ++k)
			CHECK_NEAR(pa[k], pb[k], 1e-6);
	}
}

// silu 数值梯度：d/dx [Σ x·σ(x)]
TEST_CASE("test_grad_silu")
{
	tape<f32> t;
	tensor<2, f32> X(std::array<xhqm::size, 2>{ 2, 2 }, 0.0f);
	X(0, 0) = 0.5f; X(0, 1) = -1.2f; X(1, 0) = 1.8f; X(1, 1) = -0.4f;
	auto* x = make_value<2, f32>(t, X);
	auto* loss = sum_all(t, silu(t, x));
	t.backward(loss);

	auto fwd = [&](const tensor<2, f32>& Xv) -> f32 {
		auto Y = soul::silu(Xv); f32 s = 0; for (xhqm::size i = 0; i < Y.size(); ++i) s += Y[i]; return s;
	};
	const f32 eps = 1e-3f;
	auto* gx = as_payload<2, f32>(x->holder());
	for (xhqm::size i = 0; i < X.size(); ++i)
	{
		tensor<2, f32> Xp = X, Xm = X; Xp[i] += eps; Xm[i] -= eps;
		f32 ng = (fwd(Xp) - fwd(Xm)) / (2 * eps);
		CHECK_NEAR(gx->grad[i], ng, 1e-2);
	}
}

// gelu 数值梯度（tanh 近似）
TEST_CASE("test_grad_gelu")
{
	tape<f32> t;
	tensor<2, f32> X(std::array<xhqm::size, 2>{ 2, 2 }, 0.0f);
	X(0, 0) = 0.6f; X(0, 1) = -1.4f; X(1, 0) = 1.1f; X(1, 1) = -0.2f;
	auto* x = make_value<2, f32>(t, X);
	auto* loss = sum_all(t, gelu(t, x));
	t.backward(loss);

	auto fwd = [&](const tensor<2, f32>& Xv) -> f32 {
		auto Y = soul::gelu(Xv); f32 s = 0; for (xhqm::size i = 0; i < Y.size(); ++i) s += Y[i]; return s;
	};
	const f32 eps = 1e-3f;
	auto* gx = as_payload<2, f32>(x->holder());
	for (xhqm::size i = 0; i < X.size(); ++i)
	{
		tensor<2, f32> Xp = X, Xm = X; Xp[i] += eps; Xm[i] -= eps;
		f32 ng = (fwd(Xp) - fwd(Xm)) / (2 * eps);
		CHECK_NEAR(gx->grad[i], ng, 1e-2);
	}
}

// group_norm 数值梯度（N=1,C=4,H=2,W=2，groups=2）
TEST_CASE("test_grad_group_norm")
{
	tape<f32> t;
	tensor<4, f32> X(std::array<xhqm::size, 4>{ 1, 4, 2, 2 }, 0.0f);
	for (xhqm::size i = 0; i < X.size(); ++i) X[i] = float(int(i % 7) - 3) * 0.4f;
	tensor<1, f32> G(std::array<xhqm::size, 1>{ 4 }, 0.0f);
	G[0] = 1.0f; G[1] = 0.5f; G[2] = 1.5f; G[3] = -0.5f;
	tensor<1, f32> B(std::array<xhqm::size, 1>{ 4 }, 0.0f);
	B[0] = 0.2f; B[1] = -0.1f; B[2] = 0.0f; B[3] = 0.3f;

	auto* x = make_value<4, f32>(t, X);
	auto* g = make_value<1, f32>(t, G);
	auto* b = make_value<1, f32>(t, B);
	auto* loss = sum_all(t, group_norm(t, x, g, b, 2));
	t.backward(loss);

	auto fwd = [&](const tensor<4, f32>& Xv, const tensor<1, f32>& Gv, const tensor<1, f32>& Bv) -> f32 {
		auto Y = tensor_base::group_norm(Xv, Gv, Bv, 2); f32 s = 0; for (xhqm::size i = 0; i < Y.size(); ++i) s += Y[i]; return s;
	};
	const f32 eps = 1e-3f;
	auto* gx = as_payload<4, f32>(x->holder());
	auto* gg = as_payload<1, f32>(g->holder());
	auto* gb = as_payload<1, f32>(b->holder());
	for (xhqm::size i = 0; i < X.size(); ++i)
	{
		tensor<4, f32> Xp = X, Xm = X; Xp[i] += eps; Xm[i] -= eps;
		f32 ng = (fwd(Xp, G, B) - fwd(Xm, G, B)) / (2 * eps);
		CHECK_NEAR(gx->grad[i], ng, 1e-2);
	}
	for (xhqm::size c = 0; c < 4; ++c)
	{
		tensor<1, f32> Gp = G, Gm = G; Gp[c] += eps; Gm[c] -= eps;
		f32 ng = (fwd(X, Gp, B) - fwd(X, Gm, B)) / (2 * eps);
		CHECK_NEAR(gg->grad[c], ng, 1e-2);
		tensor<1, f32> Bp = B, Bm = B; Bp[c] += eps; Bm[c] -= eps;
		ng = (fwd(X, G, Bp) - fwd(X, G, Bm)) / (2 * eps);
		CHECK_NEAR(gb->grad[c], ng, 1e-2);
	}
}

// conv2d 数值梯度（N=1,C=1,3x3，w 2x2，stride=1,pad=0）
TEST_CASE("test_grad_conv2d")
{
	tape<f32> t;
	tensor<4, f32> X(std::array<xhqm::size, 4>{ 1, 1, 3, 3 }, 0.0f);
	for (xhqm::size i = 0; i < X.size(); ++i) X[i] = float(i) * 0.3f - 1.0f;
	tensor<4, f32> W(std::array<xhqm::size, 4>{ 1, 1, 2, 2 }, 0.0f);
	W(0, 0, 0, 0) = 0.5f; W(0, 0, 0, 1) = -0.3f; W(0, 0, 1, 0) = 0.8f; W(0, 0, 1, 1) = 0.1f;
	tensor<1, f32> B(std::array<xhqm::size, 1>{ 1 }, 0.2f);

	auto* x = make_value<4, f32>(t, X);
	auto* w = make_value<4, f32>(t, W);
	auto* b = make_value<1, f32>(t, B);
	auto* y = conv2d(t, x, w, b, 1, 0, 1);
	auto* loss = sum_all(t, y);
	t.backward(loss);

	auto fwd = [&](const tensor<4, f32>& Xv, const tensor<4, f32>& Wv, const tensor<1, f32>& Bv) -> f32 {
		auto Y = tensor_base::conv2d(Xv, Wv, Bv, 1, 0, 1); f32 s = 0; for (xhqm::size i = 0; i < Y.size(); ++i) s += Y[i]; return s;
	};
	const f32 eps = 1e-3f;
	auto* gx = as_payload<4, f32>(x->holder());
	auto* gw = as_payload<4, f32>(w->holder());
	auto* gb = as_payload<1, f32>(b->holder());
	for (xhqm::size i = 0; i < X.size(); ++i)
	{
		tensor<4, f32> Xp = X, Xm = X; Xp[i] += eps; Xm[i] -= eps;
		f32 ng = (fwd(Xp, W, B) - fwd(Xm, W, B)) / (2 * eps);
		CHECK_NEAR(gx->grad[i], ng, 1e-2);
	}
	for (xhqm::size i = 0; i < W.size(); ++i)
	{
		tensor<4, f32> Wp = W, Wm = W; Wp[i] += eps; Wm[i] -= eps;
		f32 ng = (fwd(X, Wp, B) - fwd(X, Wm, B)) / (2 * eps);
		CHECK_NEAR(gw->grad[i], ng, 1e-2);
	}
	{
		tensor<1, f32> Bp = B, Bm = B; Bp[0] += eps; Bm[0] -= eps;
		f32 ng = (fwd(X, W, Bp) - fwd(X, W, Bm)) / (2 * eps);
		CHECK_NEAR(gb->grad[0], ng, 1e-2);
	}
}

// BCE / NLL / KL 数值梯度
TEST_CASE("test_grad_losses")
{
	// BCE
	{
		tape<f32> t;
		tensor<2, f32> P(std::array<xhqm::size, 2>{ 2, 2 }, 0.0f);
		P(0,0)=0.2f; P(0,1)=0.7f; P(1,0)=0.4f; P(1,1)=0.85f;
		tensor<2, f32> T(std::array<xhqm::size, 2>{ 2, 2 }, 0.0f);
		T(0,0)=1; T(0,1)=0; T(1,0)=1; T(1,1)=1;
		auto* p = make_value<2, f32>(t, P);
		auto* tg = make_value<2, f32>(t, T);
		auto* loss = bce(t, p, tg);
		t.backward(loss);
		auto fwd = [&](const tensor<2,f32>& Pv) -> f32 {
			f32 s = 0; for (xhqm::size i=0;i<4;++i) s += T[i]*std::log(Pv[i]) + (1-T[i])*std::log(1-Pv[i]); return -s/4;
		};
		const f32 eps = 1e-3f; auto* gp = as_payload<2,f32>(p->holder());
		for (xhqm::size i=0;i<4;++i){ tensor<2,f32> Pp=P,Pm=P; Pp[i]+=eps; Pm[i]-=eps; f32 ng=(fwd(Pp)-fwd(Pm))/(2*eps); CHECK_NEAR(gp->grad[i], ng, 1e-2); }
	}
	// NLL
	{
		tape<f32> t;
		tensor<2, f32> L(std::array<xhqm::size, 2>{ 2, 3 }, 0.0f);
		L(0,0)=-1.0f; L(0,1)=0.2f; L(0,2)=0.5f; L(1,0)=0.1f; L(1,1)=-0.3f; L(1,2)=0.8f;
		std::vector<xhqm::size> target = { 2, 0 };
		auto* lp = make_value<2, f32>(t, L);
		auto* loss = nll(t, lp, target);
		t.backward(loss);
		auto fwd = [&](const tensor<2,f32>& Lv) -> f32 { return -(Lv(0,2)+Lv(1,0))/2.0f; };
		const f32 eps = 1e-3f; auto* gl = as_payload<2,f32>(lp->holder());
		for (xhqm::size i=0;i<6;++i){ tensor<2,f32> Lp=L,Lm=L; Lp[i]+=eps; Lm[i]-=eps; f32 ng=(fwd(Lp)-fwd(Lm))/(2*eps); CHECK_NEAR(gl->grad[i], ng, 1e-2); }
	}
	// KL（高斯）
	{
		tape<f32> t;
		tensor<1, f32> M(std::array<xhqm::size,1>{3}, 0.0f); M[0]=0.5f; M[1]=-0.2f; M[2]=1.0f;
		tensor<1, f32> V(std::array<xhqm::size,1>{3}, 0.0f); V[0]=0.1f; V[1]=-0.5f; V[2]=0.3f;
		auto* mu = make_value<1, f32>(t, M);
		auto* lv = make_value<1, f32>(t, V);
		auto* loss = kl_div_gaussian(t, mu, lv);
		t.backward(loss);
		auto fwd = [&](const tensor<1,f32>& Mv, const tensor<1,f32>& Vv) -> f32 {
			f32 s = 0; for (xhqm::size i=0;i<3;++i) s += 1.0f + Vv[i] - Mv[i]*Mv[i] - std::exp(Vv[i]); return -0.5f*s/3.0f;
		};
		const f32 eps = 1e-3f; auto* gm = as_payload<1,f32>(mu->holder()); auto* gv = as_payload<1,f32>(lv->holder());
		for (xhqm::size i=0;i<3;++i){
			tensor<1,f32> Mp=M,Mm=M; Mp[i]+=eps; Mm[i]-=eps; f32 ng=(fwd(Mp,V)-fwd(Mm,V))/(2*eps); CHECK_NEAR(gm->grad[i], ng, 1e-2);
			tensor<1,f32> Vp=V,Vm=V; Vp[i]+=eps; Vm[i]-=eps; ng=(fwd(M,Vp)-fwd(M,Vm))/(2*eps); CHECK_NEAR(gv->grad[i], ng, 1e-2);
		}
	}
}

// tanh 数值梯度
TEST_CASE("test_grad_tanh")
{
	tape<f32> t;
	tensor<2, f32> X(std::array<xhqm::size, 2>{ 2, 2 }, 0.0f);
	X(0,0)=0.4f; X(0,1)=-0.8f; X(1,0)=1.2f; X(1,1)=-0.1f;
	auto* x = make_value<2, f32>(t, X);
	auto* loss = sum_all(t, tanh(t, x));
	t.backward(loss);
	auto fwd = [&](const tensor<2,f32>& Xv) -> f32 { auto Y = soul::tanh_act(Xv); f32 s=0; for (xhqm::size i=0;i<4;++i) s += Y[i]; return s; };
	const f32 eps = 1e-3f; auto* gx = as_payload<2,f32>(x->holder());
	for (xhqm::size i=0;i<4;++i){ tensor<2,f32> Xp=X,Xm=X; Xp[i]+=eps; Xm[i]-=eps; f32 ng=(fwd(Xp)-fwd(Xm))/(2*eps); CHECK_NEAR(gx->grad[i], ng, 1e-2); }
}

// conv1d 数值梯度（N=1,C=1,L=4，w k=3，stride=1,pad=0）
TEST_CASE("test_grad_conv1d")
{
	tape<f32> t;
	tensor<3, f32> X(std::array<xhqm::size, 3>{ 1, 1, 4 }, 0.0f);
	for (xhqm::size i=0;i<4;++i) X(0,0,i) = float(i)*0.5f - 1.0f;
	tensor<3, f32> W(std::array<xhqm::size, 3>{ 1, 1, 3 }, 0.0f);
	W(0,0,0)=0.3f; W(0,0,1)=-0.5f; W(0,0,2)=0.7f;
	tensor<1, f32> B(std::array<xhqm::size,1>{1}, 0.1f);
	auto* x = make_value<3, f32>(t, X); auto* w = make_value<3, f32>(t, W); auto* b = make_value<1, f32>(t, B);
	auto* y = conv1d(t, x, w, b, 1, 0, 1);
	auto* loss = sum_all(t, y);
	t.backward(loss);
	auto fwd = [&](const tensor<3,f32>& Xv, const tensor<3,f32>& Wv, const tensor<1,f32>& Bv) -> f32 { auto Y = tensor_base::conv1d(Xv, Wv, Bv, 1, 0, 1); f32 s=0; for (xhqm::size i=0;i<Y.size();++i) s+=Y[i]; return s; };
	const f32 eps = 1e-3f; auto* gx = as_payload<3,f32>(x->holder()); auto* gw = as_payload<3,f32>(w->holder()); auto* gb = as_payload<1,f32>(b->holder());
	for (xhqm::size i=0;i<X.size();++i){ tensor<3,f32> Xp=X,Xm=X; Xp[i]+=eps; Xm[i]-=eps; f32 ng=(fwd(Xp,W,B)-fwd(Xm,W,B))/(2*eps); CHECK_NEAR(gx->grad[i], ng, 1e-2); }
	for (xhqm::size i=0;i<W.size();++i){ tensor<3,f32> Wp=W,Wm=W; Wp[i]+=eps; Wm[i]-=eps; f32 ng=(fwd(X,Wp,B)-fwd(X,Wm,B))/(2*eps); CHECK_NEAR(gw->grad[i], ng, 1e-2); }
	{ tensor<1,f32> Bp=B,Bm=B; Bp[0]+=eps; Bm[0]-=eps; f32 ng=(fwd(X,W,Bp)-fwd(X,W,Bm))/(2*eps); CHECK_NEAR(gb->grad[0], ng, 1e-2); }
}

// conv3d 数值梯度（N=1,C=1,2x2x2，w 1x1x1，stride=1）
TEST_CASE("test_grad_conv3d")
{
	tape<f32> t;
	tensor<5, f32> X(std::array<xhqm::size, 5>{ 1, 1, 2, 2, 2 }, 0.0f);
	for (xhqm::size i=0;i<8;++i) X[i] = float(i)*0.2f - 0.5f;
	tensor<5, f32> W(std::array<xhqm::size, 5>{ 1, 1, 1, 1, 1 }, 0.0f);
	W[0] = 1.5f;
	tensor<1, f32> B(std::array<xhqm::size,1>{1}, 0.2f);
	auto* x = make_value<5, f32>(t, X); auto* w = make_value<5, f32>(t, W); auto* b = make_value<1, f32>(t, B);
	auto* y = conv3d(t, x, w, b, 1, 0, 1);
	auto* loss = sum_all(t, y);
	t.backward(loss);
	auto fwd = [&](const tensor<5,f32>& Xv, const tensor<5,f32>& Wv, const tensor<1,f32>& Bv) -> f32 { auto Y = tensor_base::conv3d(Xv, Wv, Bv, 1, 0, 1); f32 s=0; for (xhqm::size i=0;i<Y.size();++i) s+=Y[i]; return s; };
	const f32 eps = 1e-3f; auto* gx = as_payload<5,f32>(x->holder()); auto* gw = as_payload<5,f32>(w->holder()); auto* gb = as_payload<1,f32>(b->holder());
	for (xhqm::size i=0;i<X.size();++i){ tensor<5,f32> Xp=X,Xm=X; Xp[i]+=eps; Xm[i]-=eps; f32 ng=(fwd(Xp,W,B)-fwd(Xm,W,B))/(2*eps); CHECK_NEAR(gx->grad[i], ng, 1e-2); }
	for (xhqm::size i=0;i<W.size();++i){ tensor<5,f32> Wp=W,Wm=W; Wp[i]+=eps; Wm[i]-=eps; f32 ng=(fwd(X,Wp,B)-fwd(X,Wm,B))/(2*eps); CHECK_NEAR(gw->grad[i], ng, 1e-2); }
	{ tensor<1,f32> Bp=B,Bm=B; Bp[0]+=eps; Bm[0]-=eps; f32 ng=(fwd(X,W,Bp)-fwd(X,W,Bm))/(2*eps); CHECK_NEAR(gb->grad[0], ng, 1e-2); }
}

// batch_norm 数值梯度（N=2,C=2,2x2）
TEST_CASE("test_grad_batch_norm")
{
	tape<f32> t;
	tensor<4, f32> X(std::array<xhqm::size, 4>{ 2, 2, 2, 2 }, 0.0f);
	for (xhqm::size i=0;i<X.size();++i) X[i] = float(int(i%5)-2)*0.5f;
	tensor<1, f32> G(std::array<xhqm::size,1>{2}, 0.0f); G[0]=1.0f; G[1]=0.6f;
	tensor<1, f32> B(std::array<xhqm::size,1>{2}, 0.0f); B[0]=0.1f; B[1]=-0.2f;
	auto* x = make_value<4, f32>(t, X); auto* g = make_value<1, f32>(t, G); auto* b = make_value<1, f32>(t, B);
	auto* loss = sum_all(t, batch_norm(t, x, g, b));
	t.backward(loss);
	auto fwd = [&](const tensor<4,f32>& Xv, const tensor<1,f32>& Gv, const tensor<1,f32>& Bv) -> f32 { auto Y = tensor_base::batch_norm(Xv, Gv, Bv); f32 s=0; for (xhqm::size i=0;i<Y.size();++i) s+=Y[i]; return s; };
	const f32 eps = 1e-3f; auto* gx = as_payload<4,f32>(x->holder()); auto* gg = as_payload<1,f32>(g->holder()); auto* gb = as_payload<1,f32>(b->holder());
	for (xhqm::size i=0;i<X.size();++i){ tensor<4,f32> Xp=X,Xm=X; Xp[i]+=eps; Xm[i]-=eps; f32 ng=(fwd(Xp,G,B)-fwd(Xm,G,B))/(2*eps); CHECK_NEAR(gx->grad[i], ng, 1e-2); }
	for (xhqm::size c=0;c<2;++c){
		tensor<1,f32> Gp=G,Gm=G; Gp[c]+=eps; Gm[c]-=eps; f32 ng=(fwd(X,Gp,B)-fwd(X,Gm,B))/(2*eps); CHECK_NEAR(gg->grad[c], ng, 1e-2);
		tensor<1,f32> Bp=B,Bm=B; Bp[c]+=eps; Bm[c]-=eps; ng=(fwd(X,G,Bp)-fwd(X,G,Bm))/(2*eps); CHECK_NEAR(gb->grad[c], ng, 1e-2);
	}
}

// conv_transpose2d 数值梯度（N=1,C=1,2x2，w 2x2，stride=2）
TEST_CASE("test_grad_conv_transpose2d")
{
	tape<f32> t;
	tensor<4, f32> X(std::array<xhqm::size, 4>{ 1, 1, 2, 2 }, 0.0f);
	for (xhqm::size i=0;i<4;++i) X[i] = float(i)*0.5f - 0.5f;
	tensor<4, f32> W(std::array<xhqm::size, 4>{ 1, 1, 2, 2 }, 0.0f);
	W(0,0,0,0)=0.4f; W(0,0,0,1)=-0.2f; W(0,0,1,0)=0.6f; W(0,0,1,1)=0.3f;
	tensor<1, f32> B(std::array<xhqm::size,1>{1}, 0.1f);
	auto* x = make_value<4, f32>(t, X); auto* w = make_value<4, f32>(t, W); auto* b = make_value<1, f32>(t, B);
	auto* y = conv_transpose2d(t, x, w, b, 2, 0, 1);
	auto* loss = sum_all(t, y);
	t.backward(loss);
	auto fwd = [&](const tensor<4,f32>& Xv, const tensor<4,f32>& Wv, const tensor<1,f32>& Bv) -> f32 { auto Y = tensor_base::conv_transpose2d(Xv, Wv, Bv, 2, 0, 1); f32 s=0; for (xhqm::size i=0;i<Y.size();++i) s+=Y[i]; return s; };
	const f32 eps = 1e-3f; auto* gx = as_payload<4,f32>(x->holder()); auto* gw = as_payload<4,f32>(w->holder()); auto* gb = as_payload<1,f32>(b->holder());
	for (xhqm::size i=0;i<X.size();++i){ tensor<4,f32> Xp=X,Xm=X; Xp[i]+=eps; Xm[i]-=eps; f32 ng=(fwd(Xp,W,B)-fwd(Xm,W,B))/(2*eps); CHECK_NEAR(gx->grad[i], ng, 1e-2); }
	for (xhqm::size i=0;i<W.size();++i){ tensor<4,f32> Wp=W,Wm=W; Wp[i]+=eps; Wm[i]-=eps; f32 ng=(fwd(X,Wp,B)-fwd(X,Wm,B))/(2*eps); CHECK_NEAR(gw->grad[i], ng, 1e-2); }
	{ tensor<1,f32> Bp=B,Bm=B; Bp[0]+=eps; Bm[0]-=eps; f32 ng=(fwd(X,W,Bp)-fwd(X,W,Bm))/(2*eps); CHECK_NEAR(gb->grad[0], ng, 1e-2); }
}

// grouped conv2d 数值梯度（N=1,C=2,3x3，w 2x(1,2,2)，groups=2）
TEST_CASE("test_grad_conv2d_grouped")
{
	tape<f32> t;
	tensor<4, f32> X(std::array<xhqm::size, 4>{ 1, 2, 3, 3 }, 0.0f);
	for (xhqm::size i=0;i<X.size();++i) X[i] = float(int(i%5)-2)*0.3f;
	tensor<4, f32> W(std::array<xhqm::size, 4>{ 2, 1, 2, 2 }, 0.0f);
	for (xhqm::size i=0;i<W.size();++i) W[i] = float(int(i%3)-1)*0.4f + 0.1f;
	tensor<1, f32> B(std::array<xhqm::size,1>{2}, 0.0f); B[0]=0.1f; B[1]=-0.1f;
	auto* x = make_value<4, f32>(t, X); auto* w = make_value<4, f32>(t, W); auto* b = make_value<1, f32>(t, B);
	auto* y = conv2d_grouped(t, x, w, b, 1, 0, 1, 2);
	auto* loss = sum_all(t, y);
	t.backward(loss);
	auto fwd = [&](const tensor<4,f32>& Xv, const tensor<4,f32>& Wv, const tensor<1,f32>& Bv) -> f32 { auto Y = tensor_base::conv2d(Xv, Wv, Bv, 1, 0, 1, 2); f32 s=0; for (xhqm::size i=0;i<Y.size();++i) s+=Y[i]; return s; };
	const f32 eps = 1e-3f; auto* gx = as_payload<4,f32>(x->holder()); auto* gw = as_payload<4,f32>(w->holder()); auto* gb = as_payload<1,f32>(b->holder());
	for (xhqm::size i=0;i<X.size();++i){ tensor<4,f32> Xp=X,Xm=X; Xp[i]+=eps; Xm[i]-=eps; f32 ng=(fwd(Xp,W,B)-fwd(Xm,W,B))/(2*eps); CHECK_NEAR(gx->grad[i], ng, 1e-2); }
	for (xhqm::size i=0;i<W.size();++i){ tensor<4,f32> Wp=W,Wm=W; Wp[i]+=eps; Wm[i]-=eps; f32 ng=(fwd(X,Wp,B)-fwd(X,Wm,B))/(2*eps); CHECK_NEAR(gw->grad[i], ng, 1e-2); }
	for (xhqm::size c=0;c<2;++c){ tensor<1,f32> Bp=B,Bm=B; Bp[c]+=eps; Bm[c]-=eps; f32 ng=(fwd(X,W,Bp)-fwd(X,W,Bm))/(2*eps); CHECK_NEAR(gb->grad[c], ng, 1e-2); }
}

// hinge / focal 数值梯度
TEST_CASE("test_grad_hinge_focal")
{
	// hinge
	{
		tape<f32> t;
		tensor<1, f32> P(std::array<xhqm::size,1>{4}, 0.0f); P[0]=0.5f; P[1]=1.5f; P[2]=-0.3f; P[3]=2.0f;
		tensor<1, f32> T(std::array<xhqm::size,1>{4}, 0.0f); T[0]=1; T[1]=1; T[2]=-1; T[3]=-1;
		auto* p = make_value<1, f32>(t, P); auto* tg = make_value<1, f32>(t, T);
		auto* loss = hinge_loss(t, p, tg);
		t.backward(loss);
		auto fwd = [&](const tensor<1,f32>& Pv) -> f32 { f32 s=0; for (xhqm::size i=0;i<4;++i){ f32 m=1-Pv[i]*T[i]; if(m>0) s+=m; } return s/4; };
		const f32 eps = 1e-3f; auto* gp = as_payload<1,f32>(p->holder());
		for (xhqm::size i=0;i<4;++i){ tensor<1,f32> Pp=P,Pm=P; Pp[i]+=eps; Pm[i]-=eps; f32 ng=(fwd(Pp)-fwd(Pm))/(2*eps); CHECK_NEAR(gp->grad[i], ng, 1e-2); }
	}
	// focal（γ=2）
	{
		tape<f32> t;
		tensor<1, f32> P(std::array<xhqm::size,1>{4}, 0.0f); P[0]=0.2f; P[1]=0.7f; P[2]=0.4f; P[3]=0.85f;
		tensor<1, f32> T(std::array<xhqm::size,1>{4}, 0.0f); T[0]=1; T[1]=0; T[2]=1; T[3]=1;
		auto* p = make_value<1, f32>(t, P); auto* tg = make_value<1, f32>(t, T);
		auto* loss = focal_bce(t, p, tg, 2.0f);
		t.backward(loss);
		auto fwd = [&](const tensor<1,f32>& Pv) -> f32 {
			f32 s=0; for (xhqm::size i=0;i<4;++i){ f32 q=1-Pv[i]; s += T[i]*q*q*std::log(Pv[i]) + (1-T[i])*Pv[i]*Pv[i]*std::log(q); } return -s/4;
		};
		const f32 eps = 1e-3f; auto* gp = as_payload<1,f32>(p->holder());
		for (xhqm::size i=0;i<4;++i){ tensor<1,f32> Pp=P,Pm=P; Pp[i]+=eps; Pm[i]-=eps; f32 ng=(fwd(Pp)-fwd(Pm))/(2*eps); CHECK_NEAR(gp->grad[i], ng, 1e-2); }
	}
}

// RNN（时间步展开 + 权重共享）数值梯度
TEST_CASE("test_grad_rnn")
{
	uint32_t seed = 77;
	rnn<f32> r; r.init(2, 2, seed);
	tensor<2, f32> X0(std::array<xhqm::size, 2>{ 1, 2 }, 0.0f); X0(0,0)=0.3f; X0(0,1)=-0.5f;
	tensor<2, f32> X1(std::array<xhqm::size, 2>{ 1, 2 }, 0.0f); X1(0,0)=1.2f; X1(0,1)=0.7f;
	tensor<2, f32> H0(std::array<xhqm::size, 2>{ 1, 2 }, 0.0f);

	tape<f32> t;
	auto* x0 = make_value<2, f32>(t, X0);
	auto* x1 = make_value<2, f32>(t, X1);
	auto* h0 = make_value<2, f32>(t, H0);
	auto* hf = r.forward(t, { x0, x1 }, h0);
	auto* loss = sum_all(t, hf);
	t.backward(loss);

	// 数值梯度核对 wih 的一个元素（h0 固定）
	auto fwd = [&](const tensor<2,f32>& W) -> f32 {
		tensor<2,f32> wih = W;
		tensor<2,f32> whh = as_payload<2,f32>(r.payload("whh"))->value;
		auto hh = H0;
		for (int t=0;t<2;++t) {
			tensor<2,f32> X = (t==0)?X0:X1;
			hh = soul::tanh_act(tensor_base::matmul(X, wih) + tensor_base::matmul(hh, whh));
		}
		f32 s=0; for (xhqm::size i=0;i<hh.size();++i) s += hh[i]; return s;
	};
	const f32 eps = 1e-3f;
	auto* gw = as_payload<2,f32>(r.payload("wih"));
	tensor<2,f32> W = gw->value;
	for (xhqm::size i=0;i<W.size();++i){
		tensor<2,f32> Wp=W,Wm=W; Wp[i]+=eps; Wm[i]-=eps;
		f32 ng=(fwd(Wp)-fwd(Wm))/(2*eps);
		CHECK_NEAR(gw->grad[i], ng, 1e-2);
	}
}

// LSTM（合并门 + 展开）数值梯度
TEST_CASE("test_grad_lstm")
{
	uint32_t seed = 88;
	lstm<f32> m; m.init(2, 2, seed);
	tensor<2, f32> X0(std::array<xhqm::size, 2>{ 1, 2 }, 0.0f); X0(0,0)=0.4f; X0(0,1)=-0.2f;
	tensor<2, f32> X1(std::array<xhqm::size, 2>{ 1, 2 }, 0.0f); X1(0,0)=-0.7f; X1(0,1)=0.9f;
	tensor<2, f32> H0(std::array<xhqm::size, 2>{ 1, 2 }, 0.0f);
	tensor<2, f32> C0(std::array<xhqm::size, 2>{ 1, 2 }, 0.0f);

	tape<f32> t;
	auto* x0 = make_value<2, f32>(t, X0);
	auto* x1 = make_value<2, f32>(t, X1);
	auto* h0 = make_value<2, f32>(t, H0);
	auto* c0 = make_value<2, f32>(t, C0);
	auto st = m.forward(t, { x0, x1 }, h0, c0);
	auto* loss = sum_all(t, st.h);
	t.backward(loss);

	auto fwd = [&](const tensor<2,f32>& Wih) -> f32 {
		tensor<2,f32> wih = Wih;
		tensor<2,f32> whh = as_payload<2,f32>(m.payload("whh"))->value;
		tensor<2,f32> h = H0, c = C0;
		for (int t_=0;t_<2;++t_) {
			tensor<2,f32> X = (t_==0)?X0:X1;
			tensor<2,f32> gates = tensor_base::matmul(X, wih) + tensor_base::matmul(h, whh);
			// i/f/g/o = 4 段 sigmoid/tanh
			auto sl = [&](xhqm::size c0_) { tensor<2,f32> r(std::array<xhqm::size,2>{1,2},0.0f); for (xhqm::size j=0;j<2;++j) r(0,j)=gates(0,c0_+j); return r; };
			tensor<2,f32> i = soul::sigmoid(sl(0)), f = soul::sigmoid(sl(2)), g = soul::tanh_act(sl(4)), o = soul::sigmoid(sl(6));
			c = f * c + i * g;
			h = o * soul::tanh_act(c);
		}
		f32 s=0; for (xhqm::size i=0;i<h.size();++i) s += h[i]; return s;
	};
	const f32 eps = 1e-3f;
	auto* gw = as_payload<2,f32>(m.payload("wih"));
	tensor<2,f32> W = gw->value;
	for (xhqm::size i=0;i<W.size();++i){
		tensor<2,f32> Wp=W,Wm=W; Wp[i]+=eps; Wm[i]-=eps;
		f32 ng=(fwd(Wp)-fwd(Wm))/(2*eps);
		CHECK_NEAR(gw->grad[i], ng, 1e-2);
	}
}

// InfoNCE 冒烟：构图 + 反向，loss 有限、q/k 梯度非零
TEST_CASE("test_info_nce")
{
	uint32_t seed = 99;
	tensor<2, f32> Q(std::array<xhqm::size, 2>{ 3, 4 }, 0.0f);
	tensor<2, f32> K(std::array<xhqm::size, 2>{ 3, 4 }, 0.0f);
	fill_normal(Q, 0.0f, 1.0f, seed); fill_normal(K, 0.0f, 1.0f, seed);
	tape<f32> t;
	auto* q = make_value<2, f32>(t, Q);
	auto* k = make_value<2, f32>(t, K);
	auto* loss = info_nce(t, q, k, 0.5f);
	t.backward(loss);
	const f32 lv = static_cast<const f32*>(loss->holder()->data())[0];
	CHECK(std::isfinite((double)lv));
	auto* gq = as_payload<2, f32>(q->holder());
	f32 gn = 0; for (xhqm::size i=0;i<gq->grad.size();++i) gn += gq->grad[i]*gq->grad[i];
	CHECK(gn > 0.0f);
}
