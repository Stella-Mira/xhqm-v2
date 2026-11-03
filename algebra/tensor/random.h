#ifndef XHQM_TENSOR_RANDOM
#define XHQM_TENSOR_RANDOM

// 随机张量（header-only）：rand / randn，并行 + 每元素独立 RNG 状态（可复现、无数据竞争）。
// 命名空间 xhqm::tensor_base。RNG：splitmix64 派生状态 + xorshift32 + Box-Muller。
#include "tensor_base.h"
#include <cstdint>
#include <cmath>
#include <array>

namespace xhqm {
namespace tensor_base {

	// —— 每元素独立 RNG 状态：从 (seed, 线性索引) 派生，保证并行安全与可复现 ——
	inline uint32_t rand_seed_at(uint32_t seed, xhqm::index i)
	{
		uint64_t h = (uint64_t)seed * 0x9E3779B97F4A7C15ull ^ ((uint64_t)i + 0x9E3779B97F4A7C15ull);
		h ^= h >> 30; h *= 0xBF58476D1CE4E5B9ull;
		h ^= h >> 27; h *= 0x94D049BB133111EBull;
		h ^= h >> 31;
		uint32_t s = (uint32_t)h;
		return s == 0 ? 0x9E3779B9u : s;
	}

	inline uint32_t rand_xorshift32(uint32_t& s)
	{
		uint32_t x = s;
		x ^= x << 13; x ^= x >> 17; x ^= x << 5;
		s = x; return x;
	}

	inline float rand_uniform01(uint32_t& s)
	{
		return float(rand_xorshift32(s) >> 8) / float(uint32_t(1) << 24);
	}

	inline float rand_normal01(uint32_t& s)
	{
		float u1 = rand_uniform01(s);
		float u2 = rand_uniform01(s);
		if (u1 < 1e-7f) u1 = 1e-7f; // 避免 log(0)
		return std::sqrt(-2.0f * std::log(u1)) * std::cos(2.0f * 3.14159265358979323846f * u2);
	}

	// —— 就地填充 [lo, hi) 均匀（并行，每元素独立状态）——
	template<xhqm::size dim, class type>
	void rand_uniform(xhqm::tensor<dim, type>& t, type lo, type hi, uint32_t seed = 5489u)
	{
		tensor_backend_active().parallel_for(0, t.size(), [&](xhqm::index i) {
			uint32_t s = rand_seed_at(seed, i);
			t[i] = xhqm::sum(lo, xhqm::mul(type(rand_uniform01(s)), xhqm::sub(hi, lo)));
		});
	}

	// —— 就地填充 N(mean, stddev)（并行，每元素独立状态）——
	template<xhqm::size dim, class type>
	void rand_normal(xhqm::tensor<dim, type>& t, type mean, type stddev, uint32_t seed = 5489u)
	{
		tensor_backend_active().parallel_for(0, t.size(), [&](xhqm::index i) {
			uint32_t s = rand_seed_at(seed, i);
			t[i] = xhqm::sum(mean, xhqm::mul(type(rand_normal01(s)), stddev));
		});
	}

	// —— 便捷构造：返回 [0,1) 均匀张量 ——
	template<class type, xhqm::size dim>
	xhqm::tensor<dim, type> rand(const std::array<xhqm::size, dim>& dims, uint32_t seed = 5489u)
	{
		xhqm::tensor<dim, type> t(dims, type(0));
		rand_uniform(t, type(0), type(1), seed);
		return t;
	}

	// —— 便捷构造：返回标准正态 N(0,1) 张量 ——
	template<class type, xhqm::size dim>
	xhqm::tensor<dim, type> randn(const std::array<xhqm::size, dim>& dims, uint32_t seed = 5489u)
	{
		xhqm::tensor<dim, type> t(dims, type(0));
		rand_normal(t, type(0), type(1), seed);
		return t;
	}

}
}

#endif
