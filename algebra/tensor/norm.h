#ifndef XHQM_TENSOR_NORM
#define XHQM_TENSOR_NORM

// 归一化与 softmax（header-only，下沉自 soul）：softmax / layer_norm / rms_norm。
// 均为「归约 + 逐元素」组合，经 tensor_backend_active() 并行调度；
// 命名空间 xhqm::tensor_base（对齐 linalg/reduce 等后端算子约定）。
#include "linalg.h"
#include <cmath>
#include <array>

namespace xhqm {
namespace tensor_base {

	// —— softmax：沿 axis 数值稳定（先减该轴最大值）——
	// reduce_max 末维走后端 reduce kernel，其余轴走通用归约；exp 与归一化为 parallel_for。
	template<class type, xhqm::size dim>
	xhqm::tensor<dim, type> softmax(const xhqm::tensor<dim, type>& x, xhqm::size axis)
	{
		static_assert(dim >= 1, "softmax 需要 dim>=1");
		if (x.size() == 0 || x.dimension(axis) == 0)
			return xhqm::tensor<dim, type>();

		// 1) 沿 axis 求 max
		auto mx = reduce_max(x, axis);
		// 2) exp(x - max)，把 max 广播回原形状
		xhqm::tensor<dim, type> e(x.shape(), type(0));
		tensor_backend_active().parallel_for(0, x.size(), [&](xhqm::index flat) {
			std::array<xhqm::index, dim> c = x.index(flat);
			std::array<xhqm::index, dim - 1> rc{};
			xhqm::size oi = 0;
			for (xhqm::size k = 0; k < dim; ++k) if (k != axis) rc[oi++] = c[k];
			type v = xhqm::sub(x.value(flat), mx.value(mx.index(rc)));
			e.value(flat) = type(std::exp(v));
		});
		// 3) 沿 axis 求和
		auto s = reduce_sum(e, axis);
		// 4) 归一化
		xhqm::tensor<dim, type> out(x.shape(), type(0));
		tensor_backend_active().parallel_for(0, out.size(), [&](xhqm::index flat) {
			std::array<xhqm::index, dim> c = out.index(flat);
			std::array<xhqm::index, dim - 1> rc{};
			xhqm::size oi = 0;
			for (xhqm::size k = 0; k < dim; ++k) if (k != axis) rc[oi++] = c[k];
			out.value(flat) = xhqm::div(e.value(flat), s.value(s.index(rc)));
		});
		return out;
	}

	// —— layer_norm：沿 axis 归一化 + affine，y = (x - mean)/sqrt(var+eps) * gamma + beta ——
	// gamma/beta 长度 = x.dimension(axis)。var 用 E[x²]-E[x]²（单趟两归约，x*x 为 Hadamard 平方）。
	template<class type, xhqm::size dim>
	xhqm::tensor<dim, type> layer_norm(
		const xhqm::tensor<dim, type>& x,
		const xhqm::tensor<1, type>& gamma,
		const xhqm::tensor<1, type>& beta,
		type eps = type(1e-5),
		xhqm::size axis = dim - 1)
	{
		static_assert(dim >= 1, "layer_norm 需要 dim>=1");
		const xhqm::size n = x.dimension(axis);
		if (x.size() == 0 || n == 0)
			return xhqm::tensor<dim, type>();
		if (gamma.dimension(0) != n || beta.dimension(0) != n)
			throw xhqm::error("layer_norm gamma/beta 长度须等于归一化轴长度");

		auto sumx = reduce_sum(x, axis);
		auto sumx2 = reduce_sum(x * x, axis);

		xhqm::tensor<dim, type> y(x.shape(), type(0));
		tensor_backend_active().parallel_for(0, x.size(), [&](xhqm::index flat) {
			std::array<xhqm::index, dim> c = x.index(flat);
			std::array<xhqm::index, dim - 1> rc{};
			xhqm::size oi = 0;
			for (xhqm::size k = 0; k < dim; ++k) if (k != axis) rc[oi++] = c[k];
			type mean = xhqm::div(sumx.value(sumx.index(rc)), type(n));
			type var = xhqm::sub(xhqm::div(sumx2.value(sumx2.index(rc)), type(n)), xhqm::mul(mean, mean));
			type rstd = xhqm::div(type(1), type(std::sqrt(xhqm::sum(var, eps))));
			type xh = xhqm::mul(xhqm::sub(x.value(flat), mean), rstd);
			y.value(flat) = xhqm::sum(xhqm::mul(xh, gamma[c[axis]]), beta[c[axis]]);
		});
		return y;
	}

	// —— rms_norm：沿 axis 归一化（无去均值、无 bias），y = x / sqrt(mean(x²)+eps) * gamma ——
	template<class type, xhqm::size dim>
	xhqm::tensor<dim, type> rms_norm(
		const xhqm::tensor<dim, type>& x,
		const xhqm::tensor<1, type>& gamma,
		type eps = type(1e-5),
		xhqm::size axis = dim - 1)
	{
		static_assert(dim >= 1, "rms_norm 需要 dim>=1");
		const xhqm::size n = x.dimension(axis);
		if (x.size() == 0 || n == 0)
			return xhqm::tensor<dim, type>();
		if (gamma.dimension(0) != n)
			throw xhqm::error("rms_norm gamma 长度须等于归一化轴长度");

		auto ms = reduce_sum(x * x, axis);

		xhqm::tensor<dim, type> y(x.shape(), type(0));
		tensor_backend_active().parallel_for(0, x.size(), [&](xhqm::index flat) {
			std::array<xhqm::index, dim> c = x.index(flat);
			std::array<xhqm::index, dim - 1> rc{};
			xhqm::size oi = 0;
			for (xhqm::size k = 0; k < dim; ++k) if (k != axis) rc[oi++] = c[k];
			type mean_sq = xhqm::div(ms.value(ms.index(rc)), type(n));
			type r = xhqm::div(type(1), type(std::sqrt(xhqm::sum(mean_sq, eps))));
			y.value(flat) = xhqm::mul(xhqm::mul(x.value(flat), r), gamma[c[axis]]);
		});
		return y;
	}

	// —— group_norm：x (N,C,H,W) 按 G 组归一化（每组 C/G 通道），逐通道 affine ——
	template<class type>
	tensor<4, type> group_norm(const tensor<4, type>& x, const tensor<1, type>& gamma, const tensor<1, type>& beta,
		xhqm::size groups, type eps = type(1e-5))
	{
		const xhqm::size N = x.dimension(0), C = x.dimension(1), H = x.dimension(2), W = x.dimension(3);
		if (groups == 0 || C % groups != 0) throw xhqm::error("group_norm groups 须整除通道数");
		if (gamma.dimension(0) != C || beta.dimension(0) != C) throw xhqm::error("group_norm gamma/beta 长度须等于通道数");
		const xhqm::size cg = C / groups, sp = H * W, gsz = cg * sp;
		tensor<4, type> y(x.shape(), type(0));
		tensor_backend_active().parallel_for(0, N * groups, [&](xhqm::size ng)
		{
			const xhqm::size n = ng / groups, g = ng % groups;
			const xhqm::size c0 = g * cg;
			type sum = type(0), sum2 = type(0);
			for (xhqm::size c = c0; c < c0 + cg; ++c)
				for (xhqm::size p = 0; p < sp; ++p)
				{
					const type v = x(n, c, p / W, p % W);
					sum = xhqm::sum(sum, v); sum2 = xhqm::sum(sum2, xhqm::mul(v, v));
				}
			const type mean = xhqm::div(sum, type(gsz));
			const type var = xhqm::sub(xhqm::div(sum2, type(gsz)), xhqm::mul(mean, mean));
			const type rstd = xhqm::div(type(1), type(std::sqrt(xhqm::sum(var, eps))));
			for (xhqm::size c = c0; c < c0 + cg; ++c)
				for (xhqm::size p = 0; p < sp; ++p)
				{
					const xhqm::size h = p / W, w = p % W;
					y(n, c, h, w) = xhqm::sum(xhqm::mul(xhqm::mul(xhqm::sub(x(n, c, h, w), mean), rstd), gamma[c]), beta[c]);
				}
		});
		return y;
	}

	// —— batch_norm：x (N,C,H,W) 按通道归一化（统计量跨 batch，即 (N,H,W)），逐通道 affine ——
	template<class type>
	tensor<4, type> batch_norm(const tensor<4, type>& x, const tensor<1, type>& gamma, const tensor<1, type>& beta,
		type eps = type(1e-5))
	{
		const xhqm::size N = x.dimension(0), C = x.dimension(1), H = x.dimension(2), W = x.dimension(3);
		if (gamma.dimension(0) != C || beta.dimension(0) != C) throw xhqm::error("batch_norm gamma/beta 长度须等于通道数");
		const xhqm::size M = N * H * W;
		tensor<4, type> y(x.shape(), type(0));
		tensor_backend_active().parallel_for(0, C, [&](xhqm::size c)
		{
			type sum = type(0), sum2 = type(0);
			for (xhqm::size n = 0; n < N; ++n)
				for (xhqm::size p = 0; p < H * W; ++p)
				{
					const type v = x(n, c, p / W, p % W);
					sum = xhqm::sum(sum, v); sum2 = xhqm::sum(sum2, xhqm::mul(v, v));
				}
			const type mean = xhqm::div(sum, type(M));
			const type var = xhqm::sub(xhqm::div(sum2, type(M)), xhqm::mul(mean, mean));
			const type rstd = xhqm::div(type(1), type(std::sqrt(xhqm::sum(var, eps))));
			for (xhqm::size n = 0; n < N; ++n)
				for (xhqm::size p = 0; p < H * W; ++p)
				{
					const xhqm::size h = p / W, w = p % W;
					y(n, c, h, w) = xhqm::sum(xhqm::mul(xhqm::mul(xhqm::sub(x(n, c, h, w), mean), rstd), gamma[c]), beta[c]);
				}
		});
		return y;
	}

	// —— batch_norm 推理态：用 running mean/var（训练期 EMA 维护），不做 batch 统计 ——
	template<class type>
	tensor<4, type> batch_norm_eval(const tensor<4, type>& x, const tensor<1, type>& gamma, const tensor<1, type>& beta,
		const tensor<1, type>& running_mean, const tensor<1, type>& running_var, type eps = type(1e-5))
	{
		const xhqm::size N = x.dimension(0), C = x.dimension(1), H = x.dimension(2), W = x.dimension(3);
		if (gamma.dimension(0) != C || beta.dimension(0) != C || running_mean.dimension(0) != C || running_var.dimension(0) != C)
			throw xhqm::error("batch_norm_eval 参数长度须等于通道数");
		tensor<4, type> y(x.shape(), type(0));
		tensor_backend_active().parallel_for(0, C, [&](xhqm::size c)
		{
			const type rstd = xhqm::div(type(1), type(std::sqrt(xhqm::sum(running_var[c], eps))));
			for (xhqm::size n = 0; n < N; ++n)
				for (xhqm::size p = 0; p < H * W; ++p)
				{
					const xhqm::size h = p / W, w = p % W;
					y(n, c, h, w) = xhqm::sum(xhqm::mul(xhqm::mul(xhqm::sub(x(n, c, h, w), running_mean[c]), rstd), gamma[c]), beta[c]);
				}
		});
		return y;
	}

	// —— 更新 running mean/var（训练期 EMA，momentum 通常 0.1）——
	template<class type>
	void update_running_stats(tensor<1, type>& running_mean, tensor<1, type>& running_var,
		const tensor<4, type>& x, type momentum)
	{
		const xhqm::size N = x.dimension(0), C = x.dimension(1), H = x.dimension(2), W = x.dimension(3);
		const xhqm::size M = N * H * W;
		for (xhqm::size c = 0; c < C; ++c)
		{
			type sum = type(0), sum2 = type(0);
			for (xhqm::size n = 0; n < N; ++n)
				for (xhqm::size p = 0; p < H * W; ++p)
				{
					const type v = x(n, c, p / W, p % W);
					sum = xhqm::sum(sum, v); sum2 = xhqm::sum(sum2, xhqm::mul(v, v));
				}
			const type bm = xhqm::div(sum, type(M));
			const type bv = xhqm::sub(xhqm::div(sum2, type(M)), xhqm::mul(bm, bm));
			running_mean[c] = xhqm::sum(xhqm::mul(momentum, running_mean[c]), xhqm::mul(xhqm::sub(type(1), momentum), bm));
			running_var[c] = xhqm::sum(xhqm::mul(momentum, running_var[c]), xhqm::mul(xhqm::sub(type(1), momentum), bv));
		}
	}

}
}

#endif
