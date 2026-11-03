#ifndef XHQM_TENSOR_POOL
#define XHQM_TENSOR_POOL

// 池化与上采样（header-only）：max/avg pool1d/2d/3d、upsample_nearest2d、upsample_bilinear2d。
// 约定 channels-first（NCL / NCHW / NCDHW），逐输出元素经 tensor_backend_active() 并行调度。
#include "tensor_base.h"
#include <array>
#include <cstdint>

namespace xhqm {
namespace tensor_base {

	// 池化输出尺寸（沿单一空间维）
	inline xhqm::size pool_out_size(xhqm::size in, xhqm::size k, xhqm::size stride, xhqm::size padding)
	{
		return (in + 2 * padding - k) / stride + 1;
	}

	// —— 1D 最大池化（NCL）：窗口内 max，padding 越界跳过 ——
	template<class type>
	xhqm::tensor<3, type> max_pool1d(const xhqm::tensor<3, type>& x,
		xhqm::size k, xhqm::size stride, xhqm::size padding)
	{
		const xhqm::size N = x.dimension(0), C = x.dimension(1), L = x.dimension(2);
		const xhqm::size Lo = pool_out_size(L, k, stride, padding);
		xhqm::tensor<3, type> out(std::array<xhqm::size, 3>{ N, C, Lo }, type(0));
		tensor_backend_active().parallel_for(0, N * C * Lo, [&](xhqm::index flat) {
			xhqm::size r = flat;
			const xhqm::size lo = r % Lo; r /= Lo;
			const xhqm::size c = r % C; r /= C;
			const xhqm::size n = r;
			const std::int64_t l0 = (std::int64_t)lo * (std::int64_t)stride - (std::int64_t)padding;
			bool first = true;
			type acc = type(0);
			for (xhqm::size kk = 0; kk < k; ++kk)
			{
				const std::int64_t ll = l0 + (std::int64_t)kk;
				if (ll < 0 || ll >= (std::int64_t)L) continue;
				type v = x(n, c, (xhqm::size)ll);
				if (first) { acc = v; first = false; }
				else if (v > acc) acc = v;
			}
			out[flat] = acc;
		});
		return out;
	}
	// —— 1D 平均池化（NCL）：窗口内 sum / 有效元素数 ——
	template<class type>
	xhqm::tensor<3, type> avg_pool1d(const xhqm::tensor<3, type>& x,
		xhqm::size k, xhqm::size stride, xhqm::size padding)
	{
		const xhqm::size N = x.dimension(0), C = x.dimension(1), L = x.dimension(2);
		const xhqm::size Lo = pool_out_size(L, k, stride, padding);
		xhqm::tensor<3, type> out(std::array<xhqm::size, 3>{ N, C, Lo }, type(0));
		tensor_backend_active().parallel_for(0, N * C * Lo, [&](xhqm::index flat) {
			xhqm::size r = flat;
			const xhqm::size lo = r % Lo; r /= Lo;
			const xhqm::size c = r % C; r /= C;
			const xhqm::size n = r;
			const std::int64_t l0 = (std::int64_t)lo * (std::int64_t)stride - (std::int64_t)padding;
			type sum = type(0);
			xhqm::size cnt = 0;
			for (xhqm::size kk = 0; kk < k; ++kk)
			{
				const std::int64_t ll = l0 + (std::int64_t)kk;
				if (ll < 0 || ll >= (std::int64_t)L) continue;
				sum = xhqm::sum(sum, x(n, c, (xhqm::size)ll));
				++cnt;
			}
			out[flat] = cnt > 0 ? xhqm::div(sum, type(cnt)) : type(0);
		});
		return out;
	}

	// —— 2D 最大池化（NCHW）：窗口内 max，padding 越界跳过 ——
	template<class type>
	xhqm::tensor<4, type> max_pool2d(const xhqm::tensor<4, type>& x,
		xhqm::size k, xhqm::size stride, xhqm::size padding)
	{
		const xhqm::size N = x.dimension(0), C = x.dimension(1), H = x.dimension(2), W = x.dimension(3);
		const xhqm::size Oh = pool_out_size(H, k, stride, padding);
		const xhqm::size Ow = pool_out_size(W, k, stride, padding);
		xhqm::tensor<4, type> out(std::array<xhqm::size, 4>{ N, C, Oh, Ow }, type(0));
		if (tensor_backend_active().pool2d(out.data(), x.data(), N, C, H, W, Oh, Ow, k, stride, padding,
			dtype_of<type>::value, 0))  // op=0 max
			return out;
		tensor_backend_active().parallel_for(0, N * C * Oh * Ow, [&](xhqm::index flat) {
			xhqm::size r = flat;
			const xhqm::size ow = r % Ow; r /= Ow;
			const xhqm::size oh = r % Oh; r /= Oh;
			const xhqm::size c = r % C; r /= C;
			const xhqm::size n = r;
			const std::int64_t h0 = (std::int64_t)oh * (std::int64_t)stride - (std::int64_t)padding;
			const std::int64_t w0 = (std::int64_t)ow * (std::int64_t)stride - (std::int64_t)padding;
			bool first = true;
			type acc = type(0);
			for (xhqm::size kh = 0; kh < k; ++kh)
				for (xhqm::size kw = 0; kw < k; ++kw)
				{
					const std::int64_t hh = h0 + (std::int64_t)kh;
					const std::int64_t ww = w0 + (std::int64_t)kw;
					if (hh < 0 || hh >= (std::int64_t)H || ww < 0 || ww >= (std::int64_t)W) continue;
					type v = x(n, c, (xhqm::size)hh, (xhqm::size)ww);
					if (first) { acc = v; first = false; }
					else if (v > acc) acc = v;
				}
			out[flat] = acc;
		});
		return out;
	}
	// —— 2D 平均池化（NCHW）：窗口内 sum / 有效元素数 ——
	template<class type>
	xhqm::tensor<4, type> avg_pool2d(const xhqm::tensor<4, type>& x,
		xhqm::size k, xhqm::size stride, xhqm::size padding)
	{
		const xhqm::size N = x.dimension(0), C = x.dimension(1), H = x.dimension(2), W = x.dimension(3);
		const xhqm::size Oh = pool_out_size(H, k, stride, padding);
		const xhqm::size Ow = pool_out_size(W, k, stride, padding);
		xhqm::tensor<4, type> out(std::array<xhqm::size, 4>{ N, C, Oh, Ow }, type(0));
		if (tensor_backend_active().pool2d(out.data(), x.data(), N, C, H, W, Oh, Ow, k, stride, padding,
			dtype_of<type>::value, 1))  // op=1 avg
			return out;
		tensor_backend_active().parallel_for(0, N * C * Oh * Ow, [&](xhqm::index flat) {
			xhqm::size r = flat;
			const xhqm::size ow = r % Ow; r /= Ow;
			const xhqm::size oh = r % Oh; r /= Oh;
			const xhqm::size c = r % C; r /= C;
			const xhqm::size n = r;
			const std::int64_t h0 = (std::int64_t)oh * (std::int64_t)stride - (std::int64_t)padding;
			const std::int64_t w0 = (std::int64_t)ow * (std::int64_t)stride - (std::int64_t)padding;
			type sum = type(0);
			xhqm::size cnt = 0;
			for (xhqm::size kh = 0; kh < k; ++kh)
				for (xhqm::size kw = 0; kw < k; ++kw)
				{
					const std::int64_t hh = h0 + (std::int64_t)kh;
					const std::int64_t ww = w0 + (std::int64_t)kw;
					if (hh < 0 || hh >= (std::int64_t)H || ww < 0 || ww >= (std::int64_t)W) continue;
					sum = xhqm::sum(sum, x(n, c, (xhqm::size)hh, (xhqm::size)ww));
					++cnt;
				}
			out[flat] = cnt > 0 ? xhqm::div(sum, type(cnt)) : type(0);
		});
		return out;
	}

	// —— 3D 最大池化（NCDHW）：窗口内 max，padding 越界跳过 ——
	template<class type>
	xhqm::tensor<5, type> max_pool3d(const xhqm::tensor<5, type>& x,
		xhqm::size k, xhqm::size stride, xhqm::size padding)
	{
		const xhqm::size N = x.dimension(0), C = x.dimension(1), D = x.dimension(2), H = x.dimension(3), W = x.dimension(4);
		const xhqm::size Do = pool_out_size(D, k, stride, padding);
		const xhqm::size Ho = pool_out_size(H, k, stride, padding);
		const xhqm::size Wo = pool_out_size(W, k, stride, padding);
		xhqm::tensor<5, type> out(std::array<xhqm::size, 5>{ N, C, Do, Ho, Wo }, type(0));
		tensor_backend_active().parallel_for(0, N * C * Do * Ho * Wo, [&](xhqm::index flat) {
			xhqm::size r = flat;
			const xhqm::size wo = r % Wo; r /= Wo;
			const xhqm::size ho = r % Ho; r /= Ho;
			const xhqm::size dout = r % Do; r /= Do;
			const xhqm::size c = r % C; r /= C;
			const xhqm::size n = r;
			const std::int64_t d0 = (std::int64_t)dout * (std::int64_t)stride - (std::int64_t)padding;
			const std::int64_t h0 = (std::int64_t)ho * (std::int64_t)stride - (std::int64_t)padding;
			const std::int64_t w0 = (std::int64_t)wo * (std::int64_t)stride - (std::int64_t)padding;
			bool first = true;
			type acc = type(0);
			for (xhqm::size kd = 0; kd < k; ++kd)
				for (xhqm::size kh = 0; kh < k; ++kh)
					for (xhqm::size kw = 0; kw < k; ++kw)
					{
						const std::int64_t dd = d0 + (std::int64_t)kd;
						const std::int64_t hh = h0 + (std::int64_t)kh;
						const std::int64_t ww = w0 + (std::int64_t)kw;
						if (dd < 0 || dd >= (std::int64_t)D || hh < 0 || hh >= (std::int64_t)H || ww < 0 || ww >= (std::int64_t)W) continue;
						type v = x(n, c, (xhqm::size)dd, (xhqm::size)hh, (xhqm::size)ww);
						if (first) { acc = v; first = false; }
						else if (v > acc) acc = v;
					}
			out[flat] = acc;
		});
		return out;
	}
	// —— 3D 平均池化（NCDHW）：窗口内 sum / 有效元素数 ——
	template<class type>
	xhqm::tensor<5, type> avg_pool3d(const xhqm::tensor<5, type>& x,
		xhqm::size k, xhqm::size stride, xhqm::size padding)
	{
		const xhqm::size N = x.dimension(0), C = x.dimension(1), D = x.dimension(2), H = x.dimension(3), W = x.dimension(4);
		const xhqm::size Do = pool_out_size(D, k, stride, padding);
		const xhqm::size Ho = pool_out_size(H, k, stride, padding);
		const xhqm::size Wo = pool_out_size(W, k, stride, padding);
		xhqm::tensor<5, type> out(std::array<xhqm::size, 5>{ N, C, Do, Ho, Wo }, type(0));
		tensor_backend_active().parallel_for(0, N * C * Do * Ho * Wo, [&](xhqm::index flat) {
			xhqm::size r = flat;
			const xhqm::size wo = r % Wo; r /= Wo;
			const xhqm::size ho = r % Ho; r /= Ho;
			const xhqm::size dout = r % Do; r /= Do;
			const xhqm::size c = r % C; r /= C;
			const xhqm::size n = r;
			const std::int64_t d0 = (std::int64_t)dout * (std::int64_t)stride - (std::int64_t)padding;
			const std::int64_t h0 = (std::int64_t)ho * (std::int64_t)stride - (std::int64_t)padding;
			const std::int64_t w0 = (std::int64_t)wo * (std::int64_t)stride - (std::int64_t)padding;
			type sum = type(0);
			xhqm::size cnt = 0;
			for (xhqm::size kd = 0; kd < k; ++kd)
				for (xhqm::size kh = 0; kh < k; ++kh)
					for (xhqm::size kw = 0; kw < k; ++kw)
					{
						const std::int64_t dd = d0 + (std::int64_t)kd;
						const std::int64_t hh = h0 + (std::int64_t)kh;
						const std::int64_t ww = w0 + (std::int64_t)kw;
						if (dd < 0 || dd >= (std::int64_t)D || hh < 0 || hh >= (std::int64_t)H || ww < 0 || ww >= (std::int64_t)W) continue;
						sum = xhqm::sum(sum, x(n, c, (xhqm::size)dd, (xhqm::size)hh, (xhqm::size)ww));
						++cnt;
					}
			out[flat] = cnt > 0 ? xhqm::div(sum, type(cnt)) : type(0);
		});
		return out;
	}

	// —— 2D 最近邻上采样（NCHW，整数倍 scale）：out[n,c,oh,ow] = x[n,c,oh/scale_h,ow/scale_w] ——
	template<class type>
	xhqm::tensor<4, type> upsample_nearest2d(const xhqm::tensor<4, type>& x,
		xhqm::size scale_h, xhqm::size scale_w)
	{
		if (scale_h == 0 || scale_w == 0) throw xhqm::error("upsample_nearest2d scale 不能为 0");
		const xhqm::size N = x.dimension(0), C = x.dimension(1), H = x.dimension(2), W = x.dimension(3);
		const xhqm::size Oh = H * scale_h, Ow = W * scale_w;
		xhqm::tensor<4, type> out(std::array<xhqm::size, 4>{ N, C, Oh, Ow }, type(0));
		if (tensor_backend_active().upsample2d(out.data(), x.data(), N, C, H, W, Oh, Ow,
			dtype_of<type>::value, 0))  // op=0 nearest
			return out;
		tensor_backend_active().parallel_for(0, N * C * Oh * Ow, [&](xhqm::index flat) {
			xhqm::size r = flat;
			const xhqm::size ow = r % Ow; r /= Ow;
			const xhqm::size oh = r % Oh; r /= Oh;
			const xhqm::size c = r % C; r /= C;
			const xhqm::size n = r;
			out[flat] = x(n, c, oh / scale_h, ow / scale_w);
		});
		return out;
	}

	// —— 2D 双线性上采样（NCHW，任意尺寸，align_corners=false）——
	template<class type>
	xhqm::tensor<4, type> upsample_bilinear2d(const xhqm::tensor<4, type>& x,
		xhqm::size out_h, xhqm::size out_w)
	{
		const xhqm::size N = x.dimension(0), C = x.dimension(1), H = x.dimension(2), W = x.dimension(3);
		if (out_h == 0 || out_w == 0) throw xhqm::error("upsample_bilinear2d 输出尺寸不能为 0");
		xhqm::tensor<4, type> out(std::array<xhqm::size, 4>{ N, C, out_h, out_w }, type(0));
		if (tensor_backend_active().upsample2d(out.data(), x.data(), N, C, H, W, out_h, out_w,
			dtype_of<type>::value, 1))  // op=1 bilinear
			return out;
		const double scale_h = (double)H / (double)out_h;
		const double scale_w = (double)W / (double)out_w;
		tensor_backend_active().parallel_for(0, N * C * out_h * out_w, [&](xhqm::index flat) {
			xhqm::size r = flat;
			const xhqm::size ow = r % out_w; r /= out_w;
			const xhqm::size oh = r % out_h; r /= out_h;
			const xhqm::size c = r % C; r /= C;
			const xhqm::size n = r;
			// 源坐标（align_corners=false），clamp 到 [0, size-1]
			double sh = ((double)oh + 0.5) * scale_h - 0.5;
			double sw = ((double)ow + 0.5) * scale_w - 0.5;
			if (sh < 0.0) sh = 0.0; if (sh > (double)(H - 1)) sh = (double)(H - 1);
			if (sw < 0.0) sw = 0.0; if (sw > (double)(W - 1)) sw = (double)(W - 1);
			const xhqm::size h0 = (xhqm::size)sh;
			const xhqm::size w0 = (xhqm::size)sw;
			const xhqm::size h1 = (h0 + 1 < H) ? h0 + 1 : h0;
			const xhqm::size w1 = (w0 + 1 < W) ? w0 + 1 : w0;
			const type hw = type(sh - (double)h0);
			const type ww = type(sw - (double)w0);
			const type one = type(1);
			const type w00 = xhqm::mul(xhqm::sub(one, hw), xhqm::sub(one, ww));
			const type w01 = xhqm::mul(xhqm::sub(one, hw), ww);
			const type w10 = xhqm::mul(hw, xhqm::sub(one, ww));
			const type w11 = xhqm::mul(hw, ww);
			type v = xhqm::sum(
				xhqm::sum(xhqm::mul(x(n, c, h0, w0), w00), xhqm::mul(x(n, c, h0, w1), w01)),
				xhqm::sum(xhqm::mul(x(n, c, h1, w0), w10), xhqm::mul(x(n, c, h1, w1), w11)));
			out[flat] = v;
		});
		return out;
	}

}
}

#endif
