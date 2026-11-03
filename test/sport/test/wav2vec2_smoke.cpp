// wav2vec2 冒烟测试：编译期实例化整模型 + 各算子小尺寸数值自检（不依赖真实权重）。
// 真实权重的全前向与 PyTorch 对照见 loader_test（路线第 3 步）。
#include "soul_exp/wav2vec2.h"
#include <cassert>
#include <cmath>
#include <cstdio>

using namespace xhqm;

int main()
{
	// 1) 编译期实例化整模型（不调用 load/forward）
	soul_exp::wav2vec2_base<f32> model;
	(void)model;

	// 2) 精确 GELU
	{
		const f32 g0 = soul_exp::gelu_exact(0.0f);
		const f32 g1 = soul_exp::gelu_exact(1.0f);
		assert(std::fabs(g0) < 1e-6f);
		assert(std::fabs(g1 - 0.84134474f) < 1e-4f);
	}

	// 3) layer_norm（biased var，输出每行均值≈0、方差≈1）
	{
		tensor<2, f32> x(std::array<xhqm::size, 2>{ 2, 4 }, 0.0f);
		for (xhqm::size i = 0; i < 2; ++i) for (xhqm::size j = 0; j < 4; ++j) x(i, j) = f32(i + j) + 1.0f;
		tensor<1, f32> w(std::array<xhqm::size, 1>{ 4 }, 1.0f);
		tensor<1, f32> b(std::array<xhqm::size, 1>{ 4 }, 0.0f);
		auto y = soul_exp::layer_norm(x, w, b, 1e-5f);
		for (xhqm::size i = 0; i < 2; ++i)
		{
			f32 mean = 0, var = 0;
			for (xhqm::size j = 0; j < 4; ++j) mean += y(i, j);
			mean /= 4;
			for (xhqm::size j = 0; j < 4; ++j) { f32 d = y(i, j) - mean; var += d * d; }
			var /= 4;
			assert(std::fabs(mean) < 1e-4f);
			assert(std::fabs(var - 1.0f) < 1e-3f);
		}
	}

	// 4) group_norm（每通道独立归一化 = InstanceNorm，biased var）
	{
		tensor<3, f32> x(std::array<xhqm::size, 3>{ 1, 2, 3 }, 0.0f);
		for (xhqm::size c = 0; c < 2; ++c) for (xhqm::size l = 0; l < 3; ++l) x(0, c, l) = f32(c + l) + 0.5f;
		tensor<1, f32> w(std::array<xhqm::size, 1>{ 2 }, 1.0f);
		tensor<1, f32> b(std::array<xhqm::size, 1>{ 2 }, 0.0f);
		auto y = soul_exp::group_norm(x, w, b, 1e-5f);
		for (xhqm::size c = 0; c < 2; ++c)
		{
			f32 mean = 0;
			for (xhqm::size l = 0; l < 3; ++l) mean += y(0, c, l);
			mean /= 3;
			f32 var = 0;
			for (xhqm::size l = 0; l < 3; ++l) { f32 d = y(0, c, l) - mean; var += d * d; }
			var /= 3;   // biased
			assert(std::fabs(mean) < 1e-4f);
			assert(std::fabs(var - 1.0f) < 1e-3f);
		}
	}

	// 5) 非因果多头注意力（单位投影，输出形状与有限性）
	{
		const xhqm::size seq = 5, hidden = 8, heads = 2;
		tensor<2, f32> x(std::array<xhqm::size, 2>{ seq, hidden }, 0.0f);
		for (xhqm::size i = 0; i < seq; ++i) for (xhqm::size j = 0; j < hidden; ++j) x(i, j) = f32((i * hidden + j) % 7) - 3.0f;
		tensor<2, f32> w(std::array<xhqm::size, 2>{ hidden, hidden }, 0.0f);
		for (xhqm::size i = 0; i < hidden; ++i) w(i, i) = 1.0f;
		auto w_t = tensor_base::transpose(w, 0, 1);
		tensor<1, f32> b0(std::array<xhqm::size, 1>{ hidden }, 0.0f);
		auto y = soul_exp::attention(x, w_t, w_t, w_t, w_t, b0, b0, b0, b0, heads);
		assert(y.dimension(0) == seq && y.dimension(1) == hidden);
		for (xhqm::size i = 0; i < seq; ++i)
			for (xhqm::size j = 0; j < hidden; ++j)
				assert(std::isfinite(y(i, j)));
	}

	// 6) 通道-序列转置往返
	{
		tensor<3, f32> x(std::array<xhqm::size, 3>{ 1, 3, 4 }, 0.0f);
		for (xhqm::size c = 0; c < 3; ++c) for (xhqm::size l = 0; l < 4; ++l) x(0, c, l) = f32(c * 4 + l);
		auto s = soul_exp::to_seq_features(x);
		auto r = soul_exp::to_channels_first(s);
		for (xhqm::size c = 0; c < 3; ++c) for (xhqm::size l = 0; l < 4; ++l) assert(r(0, c, l) == x(0, c, l));
	}

	std::printf("wav2vec2_smoke OK\n");
	return 0;
}
