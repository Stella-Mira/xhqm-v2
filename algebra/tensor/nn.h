#ifndef XHQM_TENSOR_NN
#define XHQM_TENSOR_NN

// 神经网络基础算子（header-only）：where / gather / scatter_add / dropout / softmax_cross_entropy。
// 命名空间 xhqm::tensor_base；逐元素经 tensor_backend_active() 并行调度，随机算子复用 random.h。
#include "random.h"
#include <cmath>
#include <array>
#include <vector>

namespace xhqm {
namespace tensor_base {

	// —— 条件选择：out[i] = cond[i] ? a[i] : b[i]（形状需一致）——
	template<class type, xhqm::size dim>
	xhqm::tensor<dim, type> where(const xhqm::tensor<dim, bool>& cond,
		const xhqm::tensor<dim, type>& a, const xhqm::tensor<dim, type>& b)
	{
		if (cond.shape() != a.shape() || a.shape() != b.shape())
			throw xhqm::error("where cond/a/b 形状须一致");
		xhqm::tensor<dim, type> out(a.shape(), type(0));
		// 后端 select kernel（SIMD blendv）优先，否则回退 parallel_for
		if (tensor_backend_active().select(out.data(), cond.data(), a.data(), b.data(),
			a.size(), dtype_of<type>::value))
			return out;
		tensor_backend_active().parallel_for(0, a.size(), [&](xhqm::index i) {
			out[i] = cond[i] ? a[i] : b[i];
		});
		return out;
	}

	// —— gather：按 ids 取 weight 的行（词嵌入查表），weight(vocab×hidden) → (ids.size()×hidden) ——
	template<class type>
	xhqm::tensor<2, type> gather(const xhqm::tensor<2, type>& weight, const std::vector<xhqm::size>& ids)
	{
		const xhqm::size seq = ids.size();
		const xhqm::size hidden = weight.dimension(1);
		const xhqm::size vocab = weight.dimension(0);
		xhqm::tensor<2, type> out(std::array<xhqm::size, 2>{ seq, hidden }, type(0));
		for (xhqm::size i = 0; i < seq; ++i)
		{
			if (ids[i] >= vocab) throw xhqm::error("gather ids 越界");
			for (xhqm::size j = 0; j < hidden; ++j)
				out(i, j) = weight(ids[i], j);
		}
		return out;
	}

	// —— scatter_add：weight[ids[i], :] += updates[i, :]（embedding 反向的梯度散射累加）——
	template<class type>
	void scatter_add(xhqm::tensor<2, type>& weight, const xhqm::tensor<2, type>& updates,
		const std::vector<xhqm::size>& ids)
	{
		const xhqm::size hidden = weight.dimension(1);
		const xhqm::size vocab = weight.dimension(0);
		if (updates.dimension(0) != ids.size() || updates.dimension(1) != hidden)
			throw xhqm::error("scatter_add updates 形状与 ids/hidden 不匹配");
		for (xhqm::size i = 0; i < ids.size(); ++i)
		{
			if (ids[i] >= vocab) throw xhqm::error("scatter_add ids 越界");
			for (xhqm::size j = 0; j < hidden; ++j)
				weight(ids[i], j) = xhqm::sum(weight(ids[i], j), updates(i, j));
		}
	}

	// —— dropout（inverted）：y = x * mask / (1-p)，mask_i ~ Bernoulli(1-p)（保留概率 1-p）——
	// 每元素独立状态，并行安全、同 seed 可复现。p ∈ [0,1)。
	template<class type, xhqm::size dim>
	xhqm::tensor<dim, type> dropout(const xhqm::tensor<dim, type>& x, type p, uint32_t seed)
	{
		if (p < type(0) || p >= type(1)) throw xhqm::error("dropout p 须 ∈ [0,1)");
		xhqm::tensor<dim, type> out(x.shape(), type(0));
		if (tensor_backend_active().dropout(out.data(), x.data(), x.size(), (float)p, seed, dtype_of<type>::value))
			return out;
		const type keep = xhqm::sub(type(1), p);
		const type scale = xhqm::div(type(1), keep);
		const float pf = (float)p;
		tensor_backend_active().parallel_for(0, x.size(), [&](xhqm::index i) {
			uint32_t s = rand_seed_at(seed, i);
			if (rand_uniform01(s) >= pf)      // 保留
				out[i] = xhqm::mul(x[i], scale);
			else                               // 丢弃
				out[i] = type(0);
		});
		return out;
	}

	// —— softmax + cross entropy（数值稳定，融合）：logits(batch×vocab) + target(类别索引) → 标量 loss ——
	template<class type>
	type softmax_cross_entropy(const xhqm::tensor<2, type>& logits, const std::vector<xhqm::size>& target)
	{
		const xhqm::size batch = logits.dimension(0);
		const xhqm::size vocab = logits.dimension(1);
		if (target.size() != batch) throw xhqm::error("softmax_cross_entropy target 长度须等于 batch");
		type loss = type(0);
		for (xhqm::size i = 0; i < batch; ++i)
		{
			if (target[i] >= vocab) throw xhqm::error("softmax_cross_entropy target 越界");
			type m = logits(i, 0);
			for (xhqm::size j = 1; j < vocab; ++j) if (logits(i, j) > m) m = logits(i, j);
			type sum = type(0);
			for (xhqm::size j = 0; j < vocab; ++j) sum = xhqm::sum(sum, type(std::exp(xhqm::sub(logits(i, j), m))));
			loss = xhqm::sum(loss, xhqm::sub(type(std::log(sum)), xhqm::sub(logits(i, target[i]), m)));
		}
		return xhqm::div(loss, type(batch));
	}

}
}

#endif
