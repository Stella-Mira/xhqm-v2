// UNet diffusion 2D 图像训练闭环测试：玩具图像分布（16×16 高斯光斑，中心随机）上训练 UNet 去噪器，
// 验证 noise-MSE 下降、反向采样产出有限且合理的图像。
#include "../algebra/soul/unet.h"
#include "test.h"
#include <array>
#include <cmath>
#include <cstdio>

using namespace xhqm;
using namespace xhqm::soul;

// 玩具目标分布：16×16 单通道高斯光斑，中心在 [5,11) 内随机、宽度固定
static tensor<4, f32> sample_target_image(xhqm::size n, uint32_t& seed)
{
	const xhqm::size H = 16, W = 16;
	tensor<4, f32> x(std::array<xhqm::size, 4>{ n, 1, H, W }, 0.0f);
	for (xhqm::size i = 0; i < n; ++i)
	{
		const float cx = 8.0f + 3.0f * (uniform01(seed) * 2.0f - 1.0f);
		const float cy = 8.0f + 3.0f * (uniform01(seed) * 2.0f - 1.0f);
		for (xhqm::size h = 0; h < H; ++h)
			for (xhqm::size w = 0; w < W; ++w)
			{
				const float dh = float(h) - cy, dw = float(w) - cx;
				x(i, 0, h, w) = std::exp(-(dh * dh + dw * dw) / (2.0f * 2.0f * 2.0f));
			}
	}
	return x;
}

TEST_CASE("test_unet_diffusion_train")
{
	uint32_t seed = 20240;
	const xhqm::size T = 100, in_ch = 1, base_ch = 16, time_dim = 8, out_ch = 1, H = 16, W = 16;
	const xhqm::size batch = 16, steps = 1500;

	diffusion::linear_schedule<f32> s; s.init(T);
	diffusion::unet_denoiser<f32> model; model.init(in_ch, base_ch, time_dim, out_ch, seed);
	ad::adam_optimizer<f32> opt(1e-3f);

	f32 first_loss = 0.0f, last_loss = 0.0f;
	for (xhqm::size step = 0; step < steps; ++step)
	{
		auto x0 = sample_target_image(batch, seed);
		const f32 loss = diffusion::train_step_image(model, opt, s, x0, seed);
		if (step == 0) first_loss = loss;
		if (step == steps - 1) last_loss = loss;
	}
	printf("[unet] first_loss=%.4f last_loss=%.4f\n", first_loss, last_loss);

	// 收敛：noise-MSE 从 ~1.0 明显下降
	CHECK(first_loss > 0.3f);
	CHECK(last_loss < first_loss);
	CHECK(last_loss < 0.8f);

	// 反向采样：产出有限、均值合理的图像
	auto samples = diffusion::sample_image(model, s, 8, out_ch, H, W, seed);
	CHECK(samples.size() == 8 * out_ch * H * W);
	f32 mean = 0.0f;
	for (xhqm::size i = 0; i < samples.size(); ++i)
	{
		CHECK(std::isfinite((double)samples[i]));
		mean += samples[i];
	}
	mean /= f32(samples.size());
	printf("[unet] sampled mean=%.4f\n", mean);
	CHECK(std::isfinite((double)mean));
	// 采样均值应在 [0,1] 区间附近（高斯光斑目标，非噪声）
	CHECK((mean > 0.0f && mean < 1.0f));
}
