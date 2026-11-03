#ifndef XHQM_TENSOR_CONV
#define XHQM_TENSOR_CONV

// 卷积（channels-first，支持 groups）。
// 约定：input (N, Cin, *L) × weight (Cout, Cin/groups, *K) → output (N, Cout, *Lout)。
// 串行/并行桩：parallel_for 覆盖输出元素做直接卷积（no 后端串行、cpu 后端多线程）；
// 高性能后端（im2col+matmul / SIMD / CUDA）经 conv_backend 分派，后端未实现时回退本桩。
#include "tensor_base.h"
#include <array>
#include <cstdint>

namespace xhqm {
namespace tensor_base {

	// 卷积输出尺寸（沿单一空间维）
	inline xhqm::size conv_out_size(xhqm::size in, xhqm::size k, xhqm::size stride, xhqm::size padding, xhqm::size dilation)
	{
		return (in + 2 * padding - dilation * (k - 1) - 1) / stride + 1;
	}

	// S 维卷积核心（S = 空间维数，如 conv1d→1、conv2d→2）
	template<xhqm::size S, class type>
	tensor<S + 2, type> conv_nd(
		const tensor<S + 2, type>& input,
		const tensor<S + 2, type>& weight,
		const tensor<1, type>* bias,
		const xhqm::shape<S>& stride,
		const xhqm::shape<S>& padding,
		const xhqm::shape<S>& dilation,
		xhqm::size groups = 1)
	{
		const xhqm::size N = input.dimension(0);
		const xhqm::size Cin = input.dimension(1);
		const xhqm::size Cout = weight.dimension(0);
		if (groups == 0 || Cin % groups != 0 || Cout % groups != 0)
			throw xhqm::error("conv_nd 通道数不能被 groups 整除");
		const xhqm::size Cin_g = Cin / groups;
		const xhqm::size Cout_g = Cout / groups;

		xhqm::shape<S> L{}, K{}, Lout{};
		for (xhqm::size s = 0; s < S; ++s)
		{
			L[s] = input.dimension(2 + s);
			K[s] = weight.dimension(2 + s);
			Lout[s] = conv_out_size(L[s], K[s], stride[s], padding[s], dilation[s]);
		}

		std::array<xhqm::size, S + 2> out_dims{};
		out_dims[0] = N; out_dims[1] = Cout;
		for (xhqm::size s = 0; s < S; ++s) out_dims[2 + s] = Lout[s];
		tensor<S + 2, type> out(out_dims, type(0));

		// 空间步长（row-major，最后一维 stride=1）
		xhqm::shape<S> L_stride{}, K_stride{};
		xhqm::size acc = 1;
		for (xhqm::size s = S; s-- > 0;) { L_stride[s] = acc; acc *= L[s]; }
		acc = 1;
		for (xhqm::size s = S; s-- > 0;) { K_stride[s] = acc; acc *= K[s]; }
		const xhqm::size L_elems = L_stride[0] * L[0];    // 每通道输入空间元素数
		const xhqm::size K_elems = K_stride[0] * K[0];    // 每通道核元素数

		const type* ip = input.data();
		const type* wp = weight.data();
		const type* bp = bias ? bias->data() : nullptr;

		tensor_base::tensor_backend_active().parallel_for(0, out.size(), [&](xhqm::index flat) {
			// 解码 flat → (n, co, o_0..o_{S-1})
			xhqm::size r = flat;
			xhqm::shape<S> o{};
			for (xhqm::size s = S; s-- > 0;) { o[s] = r % Lout[s]; r /= Lout[s]; }
			const xhqm::size co = r % Cout;
			const xhqm::size n = r / Cout;

			const xhqm::size g = co / Cout_g;
			const xhqm::size cin_base = g * Cin_g;
			type acc = bp ? bp[co] : type(0);

			for (xhqm::size ci = 0; ci < Cin_g; ++ci)
			{
				const xhqm::size c = cin_base + ci;
				const xhqm::size in_c_base = (n * Cin + c) * L_elems;
				const xhqm::size w_base = (co * Cin_g + ci) * K_elems;
				for (xhqm::size kf = 0; kf < K_elems; ++kf)
				{
					// 解码 kf → 核坐标，并算输入空间偏移（有符号坐标，避免 padding 下溢）
					xhqm::size rr = kf;
					xhqm::size in_off = 0;
					bool valid = true;
					for (xhqm::size s = S; s-- > 0;)
					{
						const xhqm::size kk = rr % K[s]; rr /= K[s];
						const std::int64_t ii = (std::int64_t)o[s] * (std::int64_t)stride[s]
							- (std::int64_t)padding[s] + (std::int64_t)dilation[s] * (std::int64_t)kk;
						if (ii < 0 || ii >= (std::int64_t)L[s]) { valid = false; break; }
						in_off += (xhqm::size)ii * L_stride[s];
					}
					if (valid) acc = xhqm::sum(acc, xhqm::mul(ip[in_c_base + in_off], wp[w_base + kf]));
				}
			}
			out[flat] = acc;
		});
		return out;
	}

	// 前向声明：conv1d 便捷封装在 conv_backend 定义之前调用它。
	// GCC 按声明顺序做名称查找，必须先行声明；MSVC permissive 模式允许后置定义，此前依赖该行为。
	template<xhqm::size S, class type>
	tensor<S + 2, type> conv_backend(
		const tensor<S + 2, type>& input, const tensor<S + 2, type>& weight,
		const tensor<1, type>* bias,
		const xhqm::shape<S>& stride,
		const xhqm::shape<S>& padding,
		const xhqm::shape<S>& dilation,
		xhqm::size groups);

	// —— 便捷封装：标量 stride/padding/dilation（全空间维一致），无 bias / 有 bias 两个重载 ——
	template<class type>
	tensor<3, type> conv1d(const tensor<3, type>& input, const tensor<3, type>& weight,
		xhqm::size stride = 1, xhqm::size padding = 0, xhqm::size dilation = 1, xhqm::size groups = 1)
	{
		return conv_backend<1, type>(input, weight, nullptr, xhqm::shape<1>{ stride }, xhqm::shape<1>{ padding }, xhqm::shape<1>{ dilation }, groups);
	}
	template<class type>
	tensor<3, type> conv1d(const tensor<3, type>& input, const tensor<3, type>& weight,
		const tensor<1, type>& bias, xhqm::size stride = 1, xhqm::size padding = 0,
		xhqm::size dilation = 1, xhqm::size groups = 1)
	{
		return conv_backend<1, type>(input, weight, &bias, xhqm::shape<1>{ stride }, xhqm::shape<1>{ padding }, xhqm::shape<1>{ dilation }, groups);
	}

	// 通用卷积后端分派：S=1/2/3 补齐 size-1 维到 3D，复用 backend.conv3d；S=4 走 backend.conv4d。
	// 直接走后端（dtype 支持与否由后端决定），后端未实现时回退 conv_nd。
	template<xhqm::size S, class type>
	tensor<S + 2, type> conv_backend(
		const tensor<S + 2, type>& input, const tensor<S + 2, type>& weight,
		const tensor<1, type>* bias,
		const xhqm::shape<S>& stride,
		const xhqm::shape<S>& padding,
		const xhqm::shape<S>& dilation,
		xhqm::size groups)
	{
		if constexpr (S >= 1 && S <= 3)
		{
			const xhqm::size n = input.dimension(0);
			const xhqm::size cin = input.dimension(1);
			const xhqm::size cout = weight.dimension(0);
			std::array<xhqm::size, 3> L{ 1, 1, 1 }, K{ 1, 1, 1 }, st{ 1, 1, 1 }, pd{ 0, 0, 0 }, dl{ 1, 1, 1 };
			for (xhqm::size s = 0; s < S; ++s)
			{
				L[s] = input.dimension(2 + s);
				K[s] = weight.dimension(2 + s);
				st[s] = stride[s];
				pd[s] = padding[s];
				dl[s] = dilation[s];
			}
			std::array<xhqm::size, S + 2> out_dims{};
			out_dims[0] = n; out_dims[1] = cout;
			for (xhqm::size s = 0; s < S; ++s)
				out_dims[2 + s] = conv_out_size(L[s], K[s], stride[s], padding[s], dilation[s]);
			tensor<S + 2, type> out(out_dims, type(0));
			if (tensor_backend_active().conv3d(out.data(), input.data(), weight.data(),
				bias ? bias->data() : nullptr,
				n, cin, cout, L[0], L[1], L[2], K[0], K[1], K[2],
				st[0], st[1], st[2], pd[0], pd[1], pd[2], dl[0], dl[1], dl[2],
				groups, dtype_of<type>::value))
				return out;
		}
		else if constexpr (S == 4)
		{
			const xhqm::size n = input.dimension(0);
			const xhqm::size cin = input.dimension(1);
			const xhqm::size cout = weight.dimension(0);
			std::array<xhqm::size, 6> out_dims{};
			out_dims[0] = n; out_dims[1] = cout;
			for (xhqm::size s = 0; s < 4; ++s)
				out_dims[2 + s] = conv_out_size(input.dimension(2 + s), weight.dimension(2 + s), stride[s], padding[s], dilation[s]);
			tensor<6, type> out(out_dims, type(0));
			if (tensor_backend_active().conv4d(out.data(), input.data(), weight.data(),
				bias ? bias->data() : nullptr,
				n, cin, cout,
				input.dimension(2), input.dimension(3), input.dimension(4), input.dimension(5),
				weight.dimension(2), weight.dimension(3), weight.dimension(4), weight.dimension(5),
				stride[0], stride[1], stride[2], stride[3],
				padding[0], padding[1], padding[2], padding[3],
				dilation[0], dilation[1], dilation[2], dilation[3],
				groups, dtype_of<type>::value))
				return out;
		}
		return conv_nd<S, type>(input, weight, bias, stride, padding, dilation, groups);
	}

	template<class type>
	tensor<4, type> conv2d(const tensor<4, type>& input, const tensor<4, type>& weight,
		xhqm::size stride = 1, xhqm::size padding = 0, xhqm::size dilation = 1, xhqm::size groups = 1)
	{
		return conv_backend<2, type>(input, weight, nullptr, xhqm::shape<2>{ stride, stride }, xhqm::shape<2>{ padding, padding }, xhqm::shape<2>{ dilation, dilation }, groups);
	}
	template<class type>
	tensor<4, type> conv2d(const tensor<4, type>& input, const tensor<4, type>& weight,
		const tensor<1, type>& bias, xhqm::size stride = 1, xhqm::size padding = 0,
		xhqm::size dilation = 1, xhqm::size groups = 1)
	{
		return conv_backend<2, type>(input, weight, &bias, xhqm::shape<2>{ stride, stride }, xhqm::shape<2>{ padding, padding }, xhqm::shape<2>{ dilation, dilation }, groups);
	}

	template<class type>
	tensor<5, type> conv3d(const tensor<5, type>& input, const tensor<5, type>& weight,
		xhqm::size stride = 1, xhqm::size padding = 0, xhqm::size dilation = 1, xhqm::size groups = 1)
	{
		return conv_backend<3, type>(input, weight, nullptr,
			xhqm::shape<3>{ stride, stride, stride }, xhqm::shape<3>{ padding, padding, padding }, xhqm::shape<3>{ dilation, dilation, dilation }, groups);
	}
	template<class type>
	tensor<5, type> conv3d(const tensor<5, type>& input, const tensor<5, type>& weight,
		const tensor<1, type>& bias, xhqm::size stride = 1, xhqm::size padding = 0,
		xhqm::size dilation = 1, xhqm::size groups = 1)
	{
		return conv_backend<3, type>(input, weight, &bias,
			xhqm::shape<3>{ stride, stride, stride }, xhqm::shape<3>{ padding, padding, padding }, xhqm::shape<3>{ dilation, dilation, dilation }, groups);
	}

	template<class type>
	tensor<6, type> conv4d(const tensor<6, type>& input, const tensor<6, type>& weight,
		xhqm::size stride = 1, xhqm::size padding = 0, xhqm::size dilation = 1, xhqm::size groups = 1)
	{
		return conv_backend<4, type>(input, weight, nullptr,
			xhqm::shape<4>{ stride, stride, stride, stride }, xhqm::shape<4>{ padding, padding, padding, padding },
			xhqm::shape<4>{ dilation, dilation, dilation, dilation }, groups);
	}
	template<class type>
	tensor<6, type> conv4d(const tensor<6, type>& input, const tensor<6, type>& weight,
		const tensor<1, type>& bias, xhqm::size stride = 1, xhqm::size padding = 0,
		xhqm::size dilation = 1, xhqm::size groups = 1)
	{
		return conv_backend<4, type>(input, weight, &bias,
			xhqm::shape<4>{ stride, stride, stride, stride }, xhqm::shape<4>{ padding, padding, padding, padding },
			xhqm::shape<4>{ dilation, dilation, dilation, dilation }, groups);
	}

}
}

#endif
