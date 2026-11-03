#ifndef XHQM_SOUL_RANDOM
#define XHQM_SOUL_RANDOM

// 权重初始化与采样用的确定性随机源：xorshift32 + Box-Muller。
// 填充一律串行（RNG 有状态，并行会数据竞争且破坏可复现性）。
#include "../tensor/tensor.h"
#include <cstdint>
#include <cmath>

namespace xhqm {
namespace soul {

	// xorshift32：可复现的整数伪随机
	inline uint32_t xorshift32(uint32_t& seed)
	{
		uint32_t x = seed;
		x ^= x << 13;
		x ^= x >> 17;
		x ^= x << 5;
		seed = x;
		return x;
	}

	// [0,1) 均匀
	inline float uniform01(uint32_t& seed)
	{
		return float(xorshift32(seed) >> 8) / float(uint32_t(1) << 24);
	}

	// 标准正态 N(0,1)（Box-Muller）
	inline float normal01(uint32_t& seed)
	{
		float u1 = uniform01(seed);
		float u2 = uniform01(seed);
		if (u1 < 1e-7f) u1 = 1e-7f; // 避免 log(0)
		return std::sqrt(-2.0f * std::log(u1)) * std::cos(2.0f * 3.14159265358979f * u2);
	}

	// 串行填充 [lo, hi) 均匀
	template<xhqm::size dim, class type>
	void fill_uniform(tensor<dim, type>& t, type lo, type hi, uint32_t& seed)
	{
		for (xhqm::index i = 0; i < t.size(); ++i)
			t[i] = lo + type(uniform01(seed)) * (hi - lo);
	}

	// 串行填充 N(mean, stddev)
	template<xhqm::size dim, class type>
	void fill_normal(tensor<dim, type>& t, type mean, type stddev, uint32_t& seed)
	{
		for (xhqm::index i = 0; i < t.size(); ++i)
			t[i] = mean + type(normal01(seed)) * stddev;
	}

	// Xavier 均匀：bound = sqrt(6 / (fan_in + fan_out))
	template<class type>
	void xavier_uniform(tensor<2, type>& w, uint32_t& seed)
	{
		const xhqm::size fan_in = w.dimension(1);
		const xhqm::size fan_out = w.dimension(0);
		type bound = type(std::sqrt(6.0f / float(fan_in + fan_out)));
		fill_uniform(w, -bound, bound, seed);
	}

}
}

#endif
