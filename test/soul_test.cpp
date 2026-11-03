// soul 模块测试：激活/softmax、LayerNorm、Linear、多头因果注意力、嵌入与位置编码、
// Transformer 前向与生成（确定性）。
#include "../algebra/soul/soul.h"
#include "test.h"
#include <array>
#include <vector>
#include <cmath>

using namespace xhqm;
using namespace xhqm::soul;

static bool finite2(const tensor<2, f32>& t)
{
	for (xhqm::size i = 0; i < t.size(); ++i)
		if (!std::isfinite((double)t[i])) return false;
	return true;
}

TEST_CASE("test_activation")
{
	tensor<1, f32> x(std::array<xhqm::size, 1>{ 4 }, 0.0f);
	x[0] = -2.0f; x[1] = -1.0f; x[2] = 1.0f; x[3] = 2.0f;

	auto r = relu(x);
	CHECK_NEAR(r[0], 0.0f, 1e-6);
	CHECK_NEAR(r[3], 2.0f, 1e-6);

	auto s = sigmoid(x);
	CHECK_NEAR(s[2], 0.7310586f, 1e-4);

	auto g = gelu(x);
	CHECK(g[2] > 0.0f);
	CHECK((g[0] < 0.0f && g[0] > -0.2f));

	// softmax 行和 = 1，且平移不变（数值稳定）
	tensor<2, f32> m(std::array<xhqm::size, 2>{ 2, 3 }, 0.0f);
	m(0, 0) = 1; m(0, 1) = 2; m(0, 2) = 3;
	m(1, 0) = 3; m(1, 1) = 2; m(1, 2) = 1;
	auto p = softmax(m);
	for (xhqm::size i = 0; i < 2; ++i)
	{
		f32 sum = 0;
		for (xhqm::size j = 0; j < 3; ++j) sum += p(i, j);
		CHECK_NEAR(sum, 1.0f, 1e-5);
	}
	CHECK(p(0, 2) > p(0, 0));

	tensor<2, f32> shifted = m + 100.0f;
	auto ps = softmax(shifted);
	CHECK_NEAR(p(0, 1), ps(0, 1), 1e-5);
}

TEST_CASE("test_layer_norm")
{
	tensor<2, f32> x(std::array<xhqm::size, 2>{ 2, 8 }, 0.0f);
	for (xhqm::size i = 0; i < 2; ++i)
		for (xhqm::size j = 0; j < 8; ++j)
			x(i, j) = float(i * 8 + j) * 0.5f + 1.0f;

	layer_norm<f32> ln;
	ln.init(8);
	auto y = ln.forward(x);
	for (xhqm::size i = 0; i < 2; ++i)
	{
		f32 mean = 0, var = 0;
		for (xhqm::size j = 0; j < 8; ++j) mean += y(i, j);
		mean /= 8.0f;
		for (xhqm::size j = 0; j < 8; ++j) { f32 d = y(i, j) - mean; var += d * d; }
		var /= 8.0f;
		CHECK_NEAR(mean, 0.0f, 1e-4);
		CHECK_NEAR(var, 1.0f, 1e-3);
	}
}

TEST_CASE("test_linear")
{
	uint32_t seed = 42;
	linear<f32> lin;
	lin.init(3, 2, seed); // in=3, out=2

	tensor<2, f32> x(std::array<xhqm::size, 2>{ 2, 3 }, 0.0f);
	for (xhqm::size i = 0; i < 2; ++i)
		for (xhqm::size j = 0; j < 3; ++j)
			x(i, j) = float(i + j);

	auto y = lin.forward(x);
	CHECK((y.dimension(0) == 2 && y.dimension(1) == 2));

	// 手算核对：y[i][o] = sum_j x[i][j]*W[o][j] + bias[o]
	for (xhqm::size i = 0; i < 2; ++i)
		for (xhqm::size o = 0; o < 2; ++o)
		{
			f32 acc = lin.bias[o];
			for (xhqm::size j = 0; j < 3; ++j)
				acc += x(i, j) * lin.weight(o, j);
			CHECK_NEAR(y(i, o), acc, 1e-4);
		}
}

TEST_CASE("test_attention_causal")
{
	uint32_t seed = 7;
	attention<f32> attn;
	attn.init(8, 2, seed); // hidden=8, heads=2

	const xhqm::size seq = 4;
	tensor<2, f32> x(std::array<xhqm::size, 2>{ seq, 8 }, 0.0f);
	for (xhqm::size i = 0; i < seq; ++i)
		for (xhqm::size j = 0; j < 8; ++j)
			x(i, j) = float((i + 1) * (j + 1)) * 0.1f;

	auto y1 = attn.forward(x);
	CHECK((y1.dimension(0) == seq && y1.dimension(1) == 8));

	// 因果性：修改最后一行的输入，不应影响第 0 行的输出（第 0 行只能看到自己）
	tensor<2, f32> x2 = x;
	for (xhqm::size j = 0; j < 8; ++j) x2(seq - 1, j) += 100.0f;
	auto y2 = attn.forward(x2);
	for (xhqm::size j = 0; j < 8; ++j)
		CHECK_NEAR(y1(0, j), y2(0, j), 1e-4);
}

TEST_CASE("test_embedding_position")
{
	uint32_t seed = 1;
	embedding<f32> emb;
	emb.init(16, 8, seed);

	std::vector<xhqm::size> ids = { 3, 0, 15 };
	auto x = emb.forward(ids);
	CHECK((x.dimension(0) == 3 && x.dimension(1) == 8));
	for (xhqm::size j = 0; j < 8; ++j)
		CHECK_NEAR(x(1, j), emb.weight(0, j), 1e-6);

	auto pe = sinusoidal_position<f32>(8, 8);
	CHECK((pe.dimension(0) == 8 && pe.dimension(1) == 8));
	CHECK_NEAR(pe(0, 0), 0.0f, 1e-6);
	CHECK_NEAR(pe(0, 1), 1.0f, 1e-6);
}

TEST_CASE("test_transformer")
{
	const uint32_t seed = 123;
	transformer<f32> gpt;
	gpt.init(16, 16, 2, 2, 32, 32, seed); // vocab=16, hidden=16, layers=2, heads=2

	std::vector<xhqm::size> ids = { 1, 2, 3, 4 };
	auto logits = gpt.forward(ids);
	CHECK((logits.dimension(0) == 4 && logits.dimension(1) == 16));
	CHECK(finite2(logits));

	// 确定性：相同种子重初始化，输出一致
	transformer<f32> gpt2;
	gpt2.init(16, 16, 2, 2, 32, 32, seed);
	auto logits2 = gpt2.forward(ids);
	for (xhqm::size i = 0; i < logits.size(); ++i)
		CHECK_NEAR(logits[i], logits2[i], 1e-5);

	// 自回归生成：贪心，可复现
	uint32_t gseed = 999;
	auto gen1 = gpt.generate({ 1, 2 }, 8, 0.0f, gseed);
	CHECK(gen1.size() == 10);
	uint32_t gseed2 = 999;
	auto gen2 = gpt.generate({ 1, 2 }, 8, 0.0f, gseed2);
	CHECK(gen1 == gen2);
	CHECK(finite2(gpt.forward(gen1)));
}

TEST_CASE("test_sampling_topk_topp")
{
	const uint32_t seed = 42;
	transformer<f32> gpt;
	gpt.init(16, 16, 2, 2, 32, 32, seed);

	tensor<1, f32> logits(std::array<xhqm::size, 1>{ 16 }, 0.0f);
	for (xhqm::size i = 0; i < 16; ++i) logits[i] = float(i); // 最大为索引 15

	// temperature<=0 → 贪心 = argmax
	uint32_t s0 = 1;
	CHECK(gpt.sample_nucleus(logits, 0.0f, 0, 1.0f, s0) == 15);

	// top_k=3：采样结果必落在最大 3 个索引 {13,14,15} 内
	uint32_t s1 = 7;
	for (int t = 0; t < 200; ++t)
	{
		xhqm::size tok = gpt.sample_nucleus(logits, 1.0f, 3, 1.0f, s1);
		CHECK((tok == 13 || tok == 14 || tok == 15));
	}

	// top_p 很小（0.05）：软最大分布极峰，只保留概率最大的 token（15）
	uint32_t s2 = 5;
	for (int t = 0; t < 50; ++t)
		CHECK(gpt.sample_nucleus(logits, 1.0f, 0, 0.05f, s2) == 15);

	// 确定性：相同种子 → 相同结果
	uint32_t a = 99, b = 99;
	CHECK(gpt.sample_nucleus(logits, 1.0f, 3, 0.9f, a) == gpt.sample_nucleus(logits, 1.0f, 3, 0.9f, b));
}

TEST_CASE("test_generate_beam")
{
	const uint32_t seed = 123;
	transformer<f32> gpt;
	gpt.init(16, 16, 2, 2, 32, 32, seed);

	// beam_width=1 且 temperature<=0：等价贪心，与全量 generate 一致
	uint32_t s1 = 1, s2 = 1;
	auto b1 = gpt.generate_beam({ 1, 2 }, 6, 1, 0.0f, s1);
	auto g1 = gpt.generate({ 1, 2 }, 6, 0.0f, s2);
	CHECK(b1.size() == 8);
	CHECK(b1 == g1);

	// 多束：长度正确、前缀保留、确定性
	uint32_t s3 = 3, s4 = 3;
	auto b3 = gpt.generate_beam({ 1, 2 }, 6, 3, 0.5f, s3);
	auto b4 = gpt.generate_beam({ 1, 2 }, 6, 3, 0.5f, s4);
	CHECK(b3.size() == 8);
	CHECK(b3 == b4);
	CHECK((b3[0] == 1 && b3[1] == 2));
	CHECK(finite2(gpt.forward(b3)));
}

TEST_CASE("test_generate_kv_consistency")
{
	const uint32_t seed = 123;
	transformer<f32> gpt;
	gpt.init(16, 16, 2, 2, 32, 32, seed);

	std::vector<xhqm::size> ids = { 1, 2, 3, 4, 5 };
	auto full = gpt.forward(ids); // (5, 16)

	// 逐 token 增量前向（KV cache），与全量 forward 逐行对比 logits
	kv_cache<f32> cache;
	cache.init(gpt.layers);
	for (xhqm::size i = 0; i < ids.size(); ++i)
	{
		tensor<2, f32> xin = gpt.wte.forward(std::vector<xhqm::size>{ ids[i] });
		for (xhqm::size j = 0; j < gpt.hidden; ++j)
			xin(0, j) += gpt.wpe(i, j);
		for (xhqm::size l = 0; l < gpt.layers; ++l)
		{
			auto r = gpt.blocks[l].forward_inc(xin, cache.K[l], cache.V[l]);
			xin = r.h;
			cache.K[l] = r.k;
			cache.V[l] = r.v;
		}
		tensor<2, f32> logits = gpt.lm_head.forward(gpt.ln_f.forward(xin));
		for (xhqm::size j = 0; j < gpt.vocab; ++j)
			CHECK_NEAR(logits(0, j), full(i, j), 1e-4f);
	}

	// KV cache 贪心生成 == 全量贪心生成（端到端）
	uint32_t s1 = 42, s2 = 42;
	auto kv = gpt.generate_kv({ 1, 2 }, 6, 0.0f, 1.0f, 0, s1);
	auto gg = gpt.generate({ 1, 2 }, 6, 0.0f, s2);
	CHECK(kv.size() == 8);
	CHECK(kv == gg);
}
