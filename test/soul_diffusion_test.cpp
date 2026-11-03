// soul diffusion 测试：DDPM 线性调度 + 前向加噪 + MLP 去噪器训练收敛 + 反向采样。
// 玩具数据：2D 高斯 N(mean=(1.0, -0.5), std=0.5)，验证噪声预测 loss 下降且采样均值收敛到目标。
#include "../algebra/soul/diffusion.h"
#include "test.h"
#include <array>
#include <cmath>
#include <cstdio>

using namespace xhqm;
using namespace xhqm::soul;

// 从目标高斯采样一批 (batch, 2) 点
static tensor<2, f32> sample_target(xhqm::size batch, uint32_t& seed)
{
	tensor<2, f32> x(std::array<xhqm::size, 2>{ batch, 2 }, 0.0f);
	fill_normal(x, 0.0f, 0.5f, seed);
	for (xhqm::size i = 0; i < batch; ++i) { x(i, 0) += 1.0f; x(i, 1) -= 0.5f; }
	return x;
}

TEST_CASE("test_diffusion_schedule")
{
	diffusion::linear_schedule<f32> s;
	s.init(100);

	CHECK(s.T == 100);
	// β 线性递增，α = 1 - β
	CHECK(s.betas[0] < s.betas[s.T - 1]);
	CHECK_NEAR(s.alphas[0] + s.betas[0], 1.0f, 1e-6);
	// ᾱ 严格递减，首步接近 1
	CHECK_NEAR(s.alpha_bars[0], 1.0f, 1e-3);
	CHECK(s.alpha_bars[0] > s.alpha_bars[s.T - 1]);
	CHECK(s.alpha_bars[s.T - 1] < 0.8f);
}

TEST_CASE("test_diffusion_q_sample")
{
	diffusion::linear_schedule<f32> s;
	s.init(100);

	tensor<2, f32> x0(std::array<xhqm::size, 2>{ 1, 2 }, 0.0f);
	x0(0, 0) = 1.0f; x0(0, 1) = -0.5f;
	tensor<2, f32> eps(std::array<xhqm::size, 2>{ 1, 2 }, 0.0f);
	eps(0, 0) = 2.0f; eps(0, 1) = 1.0f;

	// t=0：ᾱ≈1，按公式 x_t = √ᾱ·x0 + √(1-ᾱ)·ε 精确核对
	auto x0t = diffusion::q_sample<2, f32>(x0, eps, 0, s);
	const f32 ab0 = s.alpha_bars[0];
	const f32 expected0 = std::sqrt(ab0) * 1.0f + std::sqrt(1.0f - ab0) * 2.0f;
	CHECK_NEAR(x0t(0, 0), expected0, 1e-4);

	// t=T-1：按公式 x_t = √ᾱ·x0 + √(1-ᾱ)·ε 精确核对
	auto xT = diffusion::q_sample<2, f32>(x0, eps, s.T - 1, s);
	const f32 ab = s.alpha_bars[s.T - 1];
	const f32 expected = std::sqrt(ab) * 1.0f + std::sqrt(1.0f - ab) * 2.0f;
	CHECK_NEAR(xT(0, 0), expected, 1e-4);
}

TEST_CASE("test_diffusion_train_and_sample")
{
	uint32_t seed = 2024;
	const xhqm::size T = 100, in_dim = 2, hidden = 64, time_dim = 16;
	const xhqm::size batch = 64, steps = 3000;

	diffusion::linear_schedule<f32> s; s.init(T);
	diffusion::mlp_denoiser<f32> model; model.init(in_dim, hidden, time_dim, seed);
	ad::adam_optimizer<f32> opt(1e-3f);

	f32 first_loss = 0.0f, last_loss = 0.0f;
	for (xhqm::size step = 0; step < steps; ++step)
	{
		auto x0 = sample_target(batch, seed);
		const f32 loss = diffusion::train_step(model, opt, s, x0, seed);
		if (step == 0) first_loss = loss;
		if (step == steps - 1) last_loss = loss;
	}
	printf("[diffusion] first_loss=%.4f last_loss=%.4f\n", first_loss, last_loss);

	// 收敛：噪声预测 MSE 从 ~1.0 明显下降
	CHECK(first_loss > 0.3f);
	CHECK(last_loss < 0.5f);
	CHECK(last_loss < first_loss * 0.5f);

	// 反向采样，统计均值/方差
	const xhqm::size M = 256;
	auto samples = diffusion::sample(model, s, M, seed);
	f32 mean0 = 0.0f, mean1 = 0.0f;
	for (xhqm::size i = 0; i < M; ++i) { mean0 += samples(i, 0); mean1 += samples(i, 1); }
	mean0 /= f32(M); mean1 /= f32(M);
	f32 var0 = 0.0f;
	for (xhqm::size i = 0; i < M; ++i) { const f32 d = samples(i, 0) - mean0; var0 += d * d; }
	var0 /= f32(M);
	printf("[diffusion] sampled mean=(%.3f, %.3f) var0=%.3f\n", mean0, mean1, var0);

	CHECK_NEAR(mean0, 1.0f, 0.5f);
	CHECK_NEAR(mean1, -0.5f, 0.5f);
	CHECK(var0 > 0.02f);   // 有散布，未坍缩成点
	CHECK(var0 < 1.5f);    // 未发散
}
