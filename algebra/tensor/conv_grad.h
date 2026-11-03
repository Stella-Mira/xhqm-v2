#ifndef XHQM_TENSOR_CONV_GRAD
#define XHQM_TENSOR_CONV_GRAD

// conv2d 反向（串行直连，正确性优先，groups=1）：dL/dw、dL/dx、dL/db。
// 前向 conv2d（tensor_base/conv.h）走后端；反向先用直连循环，后续可下沉 im2col/col2im 加速。
#include "tensor_base.h"
#include <cstdint>
#include <array>

namespace xhqm {
namespace tensor_base {

	// —— im2col2d：把 (N,C,H,W) 展开成 (M, K) 补丁矩阵，M=N·OH·OW, K=C·KH·KW（越界零填充）——
	template<class type>
	tensor<2, type> im2col2d(const tensor<4, type>& x,
		xhqm::size kh, xhqm::size kw, xhqm::size stride, xhqm::size padding, xhqm::size dilation)
	{
		const xhqm::size N = x.dimension(0), C = x.dimension(1), H = x.dimension(2), W = x.dimension(3);
		const xhqm::size OH = (H + 2 * padding - dilation * (kh - 1) - 1) / stride + 1;
		const xhqm::size OW = (W + 2 * padding - dilation * (kw - 1) - 1) / stride + 1;
		const xhqm::size M = N * OH * OW, K = C * kh * kw;
		tensor<2, type> col(std::array<xhqm::size, 2>{ M, K }, type(0));
		for (xhqm::size n = 0; n < N; ++n)
		for (xhqm::size oh = 0; oh < OH; ++oh)
		for (xhqm::size ow = 0; ow < OW; ++ow)
		for (xhqm::size c = 0; c < C; ++c)
		for (xhqm::size fh = 0; fh < kh; ++fh)
		for (xhqm::size fw = 0; fw < kw; ++fw)
		{
			const std::int64_t ih = (std::int64_t)oh * stride - (std::int64_t)padding + (std::int64_t)fh * dilation;
			const std::int64_t iw = (std::int64_t)ow * stride - (std::int64_t)padding + (std::int64_t)fw * dilation;
			if (ih >= 0 && ih < (std::int64_t)H && iw >= 0 && iw < (std::int64_t)W)
				col((n * OH + oh) * OW + ow, (c * kh + fh) * kw + fw) = x(n, c, (xhqm::size)ih, (xhqm::size)iw);
		}
		return col;
	}

	// —— col2im2d：把 (M, K) 补丁矩阵散射累加回 (N,C,H,W) ——
	template<class type>
	tensor<4, type> col2im2d(const tensor<2, type>& col,
		xhqm::size N, xhqm::size C, xhqm::size H, xhqm::size W,
		xhqm::size kh, xhqm::size kw, xhqm::size stride, xhqm::size padding, xhqm::size dilation)
	{
		const xhqm::size OH = (H + 2 * padding - dilation * (kh - 1) - 1) / stride + 1;
		const xhqm::size OW = (W + 2 * padding - dilation * (kw - 1) - 1) / stride + 1;
		tensor<4, type> out(std::array<xhqm::size, 4>{ N, C, H, W }, type(0));
		for (xhqm::size n = 0; n < N; ++n)
		for (xhqm::size oh = 0; oh < OH; ++oh)
		for (xhqm::size ow = 0; ow < OW; ++ow)
		for (xhqm::size c = 0; c < C; ++c)
		for (xhqm::size fh = 0; fh < kh; ++fh)
		for (xhqm::size fw = 0; fw < kw; ++fw)
		{
			const std::int64_t ih = (std::int64_t)oh * stride - (std::int64_t)padding + (std::int64_t)fh * dilation;
			const std::int64_t iw = (std::int64_t)ow * stride - (std::int64_t)padding + (std::int64_t)fw * dilation;
			if (ih >= 0 && ih < (std::int64_t)H && iw >= 0 && iw < (std::int64_t)W)
				out(n, c, (xhqm::size)ih, (xhqm::size)iw) = xhqm::sum(out(n, c, (xhqm::size)ih, (xhqm::size)iw),
					col((n * OH + oh) * OW + ow, (c * kh + fh) * kw + fw));
		}
		return out;
	}

	// dL/dw = col2im 的转置：gw_flat = im2col(x)^T @ dy_flat，再 reshape 成 (Cout,Cin,KH,KW)
	template<class type>
	tensor<4, type> conv2d_grad_weight(const tensor<4, type>& x, const tensor<4, type>& dy,
		xhqm::size kh, xhqm::size kw, xhqm::size stride, xhqm::size padding, xhqm::size dilation)
	{
		const xhqm::size N = x.dimension(0), Cin = x.dimension(1);
		const xhqm::size Cout = dy.dimension(1), OH = dy.dimension(2), OW = dy.dimension(3);
		const xhqm::size M = N * OH * OW, K = Cin * kh * kw;
		tensor<2, type> col = im2col2d<type>(x, kh, kw, stride, padding, dilation);
		tensor<2, type> dyf(std::array<xhqm::size, 2>{ M, Cout }, type(0));
		for (xhqm::size n = 0; n < N; ++n)
		for (xhqm::size co = 0; co < Cout; ++co)
		for (xhqm::size oh = 0; oh < OH; ++oh)
		for (xhqm::size ow = 0; ow < OW; ++ow)
			dyf((n * OH + oh) * OW + ow, co) = dy(n, co, oh, ow);
		tensor<2, type> gwf = matmul(transpose(col, 0, 1), dyf);  // (K, Cout)，走后端 GEMM
		tensor<4, type> gw(std::array<xhqm::size, 4>{ Cout, Cin, kh, kw }, type(0));
		for (xhqm::size co = 0; co < Cout; ++co)
		for (xhqm::size ci = 0; ci < Cin; ++ci)
		for (xhqm::size fh = 0; fh < kh; ++fh)
		for (xhqm::size fw = 0; fw < kw; ++fw)
			gw(co, ci, fh, fw) = gwf((ci * kh + fh) * kw + fw, co);
		return gw;
	}

	// dL/dx = col2im(dy_flat @ w_flat)：先 GEMM 再散射累加
	template<class type>
	tensor<4, type> conv2d_grad_input(const tensor<4, type>& w, const tensor<4, type>& dy,
		xhqm::size H, xhqm::size W, xhqm::size stride, xhqm::size padding, xhqm::size dilation)
	{
		const xhqm::size Cout = w.dimension(0), Cin = w.dimension(1), kh = w.dimension(2), kw = w.dimension(3);
		const xhqm::size N = dy.dimension(0), OH = dy.dimension(2), OW = dy.dimension(3);
		const xhqm::size M = N * OH * OW, K = Cin * kh * kw;
		tensor<2, type> wf(std::array<xhqm::size, 2>{ Cout, K }, type(0));
		for (xhqm::size co = 0; co < Cout; ++co)
		for (xhqm::size ci = 0; ci < Cin; ++ci)
		for (xhqm::size fh = 0; fh < kh; ++fh)
		for (xhqm::size fw = 0; fw < kw; ++fw)
			wf(co, (ci * kh + fh) * kw + fw) = w(co, ci, fh, fw);
		tensor<2, type> dyf(std::array<xhqm::size, 2>{ M, Cout }, type(0));
		for (xhqm::size n = 0; n < N; ++n)
		for (xhqm::size co = 0; co < Cout; ++co)
		for (xhqm::size oh = 0; oh < OH; ++oh)
		for (xhqm::size ow = 0; ow < OW; ++ow)
			dyf((n * OH + oh) * OW + ow, co) = dy(n, co, oh, ow);
		tensor<2, type> gxc = matmul(dyf, wf);  // (M, K)，走后端 GEMM
		return col2im2d<type>(gxc, N, Cin, H, W, kh, kw, stride, padding, dilation);
	}

	// dL/db[co] = Σ_{n,oh,ow} dy[n,co,oh,ow]
	template<class type>
	tensor<1, type> conv2d_grad_bias(const tensor<4, type>& dy)
	{
		const xhqm::size N = dy.dimension(0), Cout = dy.dimension(1), OH = dy.dimension(2), OW = dy.dimension(3);
		tensor<1, type> gb(std::array<xhqm::size, 1>{ Cout }, type(0));
		for (xhqm::size n = 0; n < N; ++n)
		for (xhqm::size co = 0; co < Cout; ++co)
		for (xhqm::size oh = 0; oh < OH; ++oh)
		for (xhqm::size ow = 0; ow < OW; ++ow)
			gb[co] = xhqm::sum(gb[co], dy(n, co, oh, ow));
		return gb;
	}

	// —— conv1d（N,C,L）反向 ——
	template<class type>
	tensor<3, type> conv1d_grad_weight(const tensor<3, type>& x, const tensor<3, type>& dy,
		xhqm::size k, xhqm::size stride, xhqm::size padding, xhqm::size dilation)
	{
		const xhqm::size N = x.dimension(0), Cin = x.dimension(1), L = x.dimension(2);
		const xhqm::size Cout = dy.dimension(1), OL = dy.dimension(2);
		tensor<3, type> gw(std::array<xhqm::size, 3>{ Cout, Cin, k }, type(0));
		for (xhqm::size n = 0; n < N; ++n)
		for (xhqm::size co = 0; co < Cout; ++co)
		for (xhqm::size fk = 0; fk < k; ++fk)
		for (xhqm::size ol = 0; ol < OL; ++ol)
		{
			const std::int64_t il = (std::int64_t)ol * stride - (std::int64_t)padding + (std::int64_t)fk * dilation;
			if (il < 0 || il >= (std::int64_t)L) continue;
			for (xhqm::size ci = 0; ci < Cin; ++ci)
				gw(co, ci, fk) = xhqm::sum(gw(co, ci, fk), xhqm::mul(dy(n, co, ol), x(n, ci, (xhqm::size)il)));
		}
		return gw;
	}
	template<class type>
	tensor<3, type> conv1d_grad_input(const tensor<3, type>& w, const tensor<3, type>& dy,
		xhqm::size L, xhqm::size stride, xhqm::size padding, xhqm::size dilation)
	{
		const xhqm::size Cout = w.dimension(0), Cin = w.dimension(1), k = w.dimension(2);
		const xhqm::size N = dy.dimension(0), OL = dy.dimension(2);
		tensor<3, type> gx(std::array<xhqm::size, 3>{ N, Cin, L }, type(0));
		for (xhqm::size n = 0; n < N; ++n)
		for (xhqm::size ci = 0; ci < Cin; ++ci)
		for (xhqm::size l = 0; l < L; ++l)
		for (xhqm::size co = 0; co < Cout; ++co)
		for (xhqm::size fk = 0; fk < k; ++fk)
		{
			const std::int64_t num = (std::int64_t)l + (std::int64_t)padding - (std::int64_t)fk * dilation;
			if (num < 0 || num % (std::int64_t)stride != 0) continue;
			const std::int64_t ol = num / (std::int64_t)stride;
			if (ol >= (std::int64_t)OL) continue;
			gx(n, ci, l) = xhqm::sum(gx(n, ci, l), xhqm::mul(dy(n, co, (xhqm::size)ol), w(co, ci, fk)));
		}
		return gx;
	}
	template<class type>
	tensor<1, type> conv1d_grad_bias(const tensor<3, type>& dy)
	{
		const xhqm::size N = dy.dimension(0), Cout = dy.dimension(1), OL = dy.dimension(2);
		tensor<1, type> gb(std::array<xhqm::size, 1>{ Cout }, type(0));
		for (xhqm::size n = 0; n < N; ++n)
		for (xhqm::size co = 0; co < Cout; ++co)
		for (xhqm::size ol = 0; ol < OL; ++ol)
			gb[co] = xhqm::sum(gb[co], dy(n, co, ol));
		return gb;
	}

	// —— conv3d（N,C,D,H,W）反向 ——
	template<class type>
	tensor<5, type> conv3d_grad_weight(const tensor<5, type>& x, const tensor<5, type>& dy,
		xhqm::size kd, xhqm::size kh, xhqm::size kw,
		xhqm::size sd, xhqm::size sh, xhqm::size sw,
		xhqm::size pd, xhqm::size ph, xhqm::size pw,
		xhqm::size dd, xhqm::size dh, xhqm::size dw)
	{
		const xhqm::size N = x.dimension(0), Cin = x.dimension(1), D = x.dimension(2), H = x.dimension(3), W = x.dimension(4);
		const xhqm::size Cout = dy.dimension(1), OD = dy.dimension(2), OH = dy.dimension(3), OW = dy.dimension(4);
		tensor<5, type> gw(std::array<xhqm::size, 5>{ Cout, Cin, kd, kh, kw }, type(0));
		for (xhqm::size n = 0; n < N; ++n)
		for (xhqm::size co = 0; co < Cout; ++co)
		for (xhqm::size fkd = 0; fkd < kd; ++fkd)
		for (xhqm::size fkh = 0; fkh < kh; ++fkh)
		for (xhqm::size fkw = 0; fkw < kw; ++fkw)
		for (xhqm::size od = 0; od < OD; ++od)
		for (xhqm::size oh = 0; oh < OH; ++oh)
		for (xhqm::size ow = 0; ow < OW; ++ow)
		{
			const std::int64_t id = (std::int64_t)od * sd - (std::int64_t)pd + (std::int64_t)fkd * dd;
			const std::int64_t ih = (std::int64_t)oh * sh - (std::int64_t)ph + (std::int64_t)fkh * dh;
			const std::int64_t iw = (std::int64_t)ow * sw - (std::int64_t)pw + (std::int64_t)fkw * dw;
			if (id < 0 || id >= (std::int64_t)D || ih < 0 || ih >= (std::int64_t)H || iw < 0 || iw >= (std::int64_t)W) continue;
			for (xhqm::size ci = 0; ci < Cin; ++ci)
				gw(co, ci, fkd, fkh, fkw) = xhqm::sum(gw(co, ci, fkd, fkh, fkw),
					xhqm::mul(dy(n, co, od, oh, ow), x(n, ci, (xhqm::size)id, (xhqm::size)ih, (xhqm::size)iw)));
		}
		return gw;
	}
	template<class type>
	tensor<5, type> conv3d_grad_input(const tensor<5, type>& w, const tensor<5, type>& dy,
		xhqm::size D, xhqm::size H, xhqm::size W,
		xhqm::size sd, xhqm::size sh, xhqm::size sw,
		xhqm::size pd, xhqm::size ph, xhqm::size pw,
		xhqm::size dd, xhqm::size dh, xhqm::size dw)
	{
		const xhqm::size Cout = w.dimension(0), Cin = w.dimension(1), kd = w.dimension(2), kh = w.dimension(3), kw = w.dimension(4);
		const xhqm::size N = dy.dimension(0), OD = dy.dimension(2), OH = dy.dimension(3), OW = dy.dimension(4);
		tensor<5, type> gx(std::array<xhqm::size, 5>{ N, Cin, D, H, W }, type(0));
		for (xhqm::size n = 0; n < N; ++n)
		for (xhqm::size ci = 0; ci < Cin; ++ci)
		for (xhqm::size id = 0; id < D; ++id)
		for (xhqm::size ih = 0; ih < H; ++ih)
		for (xhqm::size iw = 0; iw < W; ++iw)
		for (xhqm::size co = 0; co < Cout; ++co)
		for (xhqm::size fkd = 0; fkd < kd; ++fkd)
		for (xhqm::size fkh = 0; fkh < kh; ++fkh)
		for (xhqm::size fkw = 0; fkw < kw; ++fkw)
		{
			const std::int64_t numd = (std::int64_t)id + (std::int64_t)pd - (std::int64_t)fkd * dd;
			const std::int64_t numh = (std::int64_t)ih + (std::int64_t)ph - (std::int64_t)fkh * dh;
			const std::int64_t numw = (std::int64_t)iw + (std::int64_t)pw - (std::int64_t)fkw * dw;
			if (numd < 0 || numh < 0 || numw < 0) continue;
			if (numd % (std::int64_t)sd != 0 || numh % (std::int64_t)sh != 0 || numw % (std::int64_t)sw != 0) continue;
			const std::int64_t od = numd / (std::int64_t)sd, oh = numh / (std::int64_t)sh, ow = numw / (std::int64_t)sw;
			if (od >= (std::int64_t)OD || oh >= (std::int64_t)OH || ow >= (std::int64_t)OW) continue;
			gx(n, ci, id, ih, iw) = xhqm::sum(gx(n, ci, id, ih, iw),
				xhqm::mul(dy(n, co, (xhqm::size)od, (xhqm::size)oh, (xhqm::size)ow), w(co, ci, fkd, fkh, fkw)));
		}
		return gx;
	}
	template<class type>
	tensor<1, type> conv3d_grad_bias(const tensor<5, type>& dy)
	{
		const xhqm::size N = dy.dimension(0), Cout = dy.dimension(1), OD = dy.dimension(2), OH = dy.dimension(3), OW = dy.dimension(4);
		tensor<1, type> gb(std::array<xhqm::size, 1>{ Cout }, type(0));
		for (xhqm::size n = 0; n < N; ++n)
		for (xhqm::size co = 0; co < Cout; ++co)
		for (xhqm::size od = 0; od < OD; ++od)
		for (xhqm::size oh = 0; oh < OH; ++oh)
		for (xhqm::size ow = 0; ow < OW; ++ow)
			gb[co] = xhqm::sum(gb[co], dy(n, co, od, oh, ow));
		return gb;
	}

	// —— 转置卷积 conv_transpose2d：x (N,Cin,H,W)，w (Cin,Cout,KH,KW) → (N,Cout,OH,OW) ——
	// OH = (H-1)·stride - 2·pad + dil·(KH-1) + 1；上采样（UNet 解码器）
	template<class type>
	tensor<4, type> conv_transpose2d(const tensor<4, type>& x, const tensor<4, type>& w, const tensor<1, type>& bias,
		xhqm::size stride, xhqm::size padding, xhqm::size dilation)
	{
		const xhqm::size N = x.dimension(0), Cin = x.dimension(1), H = x.dimension(2), W = x.dimension(3);
		const xhqm::size Cout = w.dimension(1), KH = w.dimension(2), KW = w.dimension(3);
		const xhqm::size OH = (H - 1) * stride - 2 * padding + dilation * (KH - 1) + 1;
		const xhqm::size OW = (W - 1) * stride - 2 * padding + dilation * (KW - 1) + 1;
		tensor<4, type> out(std::array<xhqm::size, 4>{ N, Cout, OH, OW }, type(0));
		tensor_backend_active().parallel_for(0, N * Cout * OH * OW, [&](xhqm::size idx)
		{
			const xhqm::size ow = idx % OW; idx /= OW;
			const xhqm::size oh = idx % OH; idx /= OH;
			const xhqm::size co = idx % Cout; const xhqm::size n = idx / Cout;
			type acc = bias[co];
			for (xhqm::size ci = 0; ci < Cin; ++ci)
			for (xhqm::size kh = 0; kh < KH; ++kh)
			for (xhqm::size kw = 0; kw < KW; ++kw)
			{
				const std::int64_t num_h = (std::int64_t)oh + (std::int64_t)padding - (std::int64_t)kh * dilation;
				const std::int64_t num_w = (std::int64_t)ow + (std::int64_t)padding - (std::int64_t)kw * dilation;
				if (num_h < 0 || num_w < 0) continue;
				if (num_h % (std::int64_t)stride != 0 || num_w % (std::int64_t)stride != 0) continue;
				const std::int64_t ih = num_h / (std::int64_t)stride, iw = num_w / (std::int64_t)stride;
				if (ih >= (std::int64_t)H || iw >= (std::int64_t)W) continue;
				acc = xhqm::sum(acc, xhqm::mul(x(n, ci, (xhqm::size)ih, (xhqm::size)iw), w(ci, co, kh, kw)));
			}
			out(n, co, oh, ow) = acc;
		});
		return out;
	}
	template<class type>
	tensor<4, type> conv_transpose2d_grad_input(const tensor<4, type>& w, const tensor<4, type>& dy,
		xhqm::size H, xhqm::size W, xhqm::size stride, xhqm::size padding, xhqm::size dilation)
	{
		const xhqm::size Cin = w.dimension(0), Cout = w.dimension(1), KH = w.dimension(2), KW = w.dimension(3);
		const xhqm::size N = dy.dimension(0), OH = dy.dimension(2), OW = dy.dimension(3);
		tensor<4, type> gx(std::array<xhqm::size, 4>{ N, Cin, H, W }, type(0));
		for (xhqm::size n = 0; n < N; ++n)
		for (xhqm::size ci = 0; ci < Cin; ++ci)
		for (xhqm::size ih = 0; ih < H; ++ih)
		for (xhqm::size iw = 0; iw < W; ++iw)
		for (xhqm::size co = 0; co < Cout; ++co)
		for (xhqm::size kh = 0; kh < KH; ++kh)
		for (xhqm::size kw = 0; kw < KW; ++kw)
		{
			const std::int64_t oh = (std::int64_t)ih * stride - (std::int64_t)padding + (std::int64_t)kh * dilation;
			const std::int64_t ow = (std::int64_t)iw * stride - (std::int64_t)padding + (std::int64_t)kw * dilation;
			if (oh < 0 || oh >= (std::int64_t)OH || ow < 0 || ow >= (std::int64_t)OW) continue;
			gx(n, ci, ih, iw) = xhqm::sum(gx(n, ci, ih, iw),
				xhqm::mul(dy(n, co, (xhqm::size)oh, (xhqm::size)ow), w(ci, co, kh, kw)));
		}
		return gx;
	}
	template<class type>
	tensor<4, type> conv_transpose2d_grad_weight(const tensor<4, type>& x, const tensor<4, type>& dy,
		xhqm::size KH, xhqm::size KW, xhqm::size stride, xhqm::size padding, xhqm::size dilation)
	{
		const xhqm::size N = x.dimension(0), Cin = x.dimension(1), H = x.dimension(2), W = x.dimension(3);
		const xhqm::size Cout = dy.dimension(1), OH = dy.dimension(2), OW = dy.dimension(3);
		tensor<4, type> gw(std::array<xhqm::size, 4>{ Cin, Cout, KH, KW }, type(0));
		for (xhqm::size n = 0; n < N; ++n)
		for (xhqm::size co = 0; co < Cout; ++co)
		for (xhqm::size oh = 0; oh < OH; ++oh)
		for (xhqm::size ow = 0; ow < OW; ++ow)
		for (xhqm::size ci = 0; ci < Cin; ++ci)
		for (xhqm::size kh = 0; kh < KH; ++kh)
		for (xhqm::size kw = 0; kw < KW; ++kw)
		{
			const std::int64_t num_h = (std::int64_t)oh + (std::int64_t)padding - (std::int64_t)kh * dilation;
			const std::int64_t num_w = (std::int64_t)ow + (std::int64_t)padding - (std::int64_t)kw * dilation;
			if (num_h < 0 || num_w < 0) continue;
			if (num_h % (std::int64_t)stride != 0 || num_w % (std::int64_t)stride != 0) continue;
			const std::int64_t ih = num_h / (std::int64_t)stride, iw = num_w / (std::int64_t)stride;
			if (ih >= (std::int64_t)H || iw >= (std::int64_t)W) continue;
			gw(ci, co, kh, kw) = xhqm::sum(gw(ci, co, kh, kw),
				xhqm::mul(dy(n, co, oh, ow), x(n, ci, (xhqm::size)ih, (xhqm::size)iw)));
		}
		return gw;
	}
	template<class type>
	tensor<1, type> conv_transpose2d_grad_bias(const tensor<4, type>& dy)
	{
		const xhqm::size N = dy.dimension(0), Cout = dy.dimension(1), OH = dy.dimension(2), OW = dy.dimension(3);
		tensor<1, type> gb(std::array<xhqm::size, 1>{ Cout }, type(0));
		for (xhqm::size n = 0; n < N; ++n)
		for (xhqm::size co = 0; co < Cout; ++co)
		for (xhqm::size oh = 0; oh < OH; ++oh)
		for (xhqm::size ow = 0; ow < OW; ++ow)
			gb[co] = xhqm::sum(gb[co], dy(n, co, oh, ow));
		return gb;
	}

	// —— grouped conv2d 反向（groups>1）：权重视为 (Cout, Cin/groups, KH, KW) ——
	template<class type>
	tensor<4, type> conv2d_grad_weight_groups(const tensor<4, type>& x, const tensor<4, type>& dy,
		xhqm::size kh, xhqm::size kw, xhqm::size stride, xhqm::size padding, xhqm::size dilation, xhqm::size groups)
	{
		const xhqm::size N = x.dimension(0), Cin = x.dimension(1), H = x.dimension(2), W = x.dimension(3);
		const xhqm::size Cout = dy.dimension(1), OH = dy.dimension(2), OW = dy.dimension(3);
		const xhqm::size cin_g = Cin / groups, cout_g = Cout / groups;
		tensor<4, type> gw(std::array<xhqm::size, 4>{ Cout, cin_g, kh, kw }, type(0));
		for (xhqm::size n = 0; n < N; ++n)
		for (xhqm::size co = 0; co < Cout; ++co)
		for (xhqm::size fh = 0; fh < kh; ++fh)
		for (xhqm::size fw = 0; fw < kw; ++fw)
		for (xhqm::size oh = 0; oh < OH; ++oh)
		for (xhqm::size ow = 0; ow < OW; ++ow)
		{
			const std::int64_t ih = (std::int64_t)oh * stride - (std::int64_t)padding + (std::int64_t)fh * dilation;
			const std::int64_t iw = (std::int64_t)ow * stride - (std::int64_t)padding + (std::int64_t)fw * dilation;
			if (ih < 0 || ih >= (std::int64_t)H || iw < 0 || iw >= (std::int64_t)W) continue;
			const xhqm::size cin_base = (co / cout_g) * cin_g;
			for (xhqm::size ci = 0; ci < cin_g; ++ci)
				gw(co, ci, fh, fw) = xhqm::sum(gw(co, ci, fh, fw),
					xhqm::mul(dy(n, co, oh, ow), x(n, cin_base + ci, (xhqm::size)ih, (xhqm::size)iw)));
		}
		return gw;
	}
	template<class type>
	tensor<4, type> conv2d_grad_input_groups(const tensor<4, type>& w, const tensor<4, type>& dy,
		xhqm::size H, xhqm::size W, xhqm::size stride, xhqm::size padding, xhqm::size dilation, xhqm::size groups)
	{
		const xhqm::size Cout = w.dimension(0), Cin = w.dimension(1) * groups, kh = w.dimension(2), kw = w.dimension(3);
		const xhqm::size cin_g = w.dimension(1), cout_g = Cout / groups;
		const xhqm::size N = dy.dimension(0), OH = dy.dimension(2), OW = dy.dimension(3);
		tensor<4, type> gx(std::array<xhqm::size, 4>{ N, Cin, H, W }, type(0));
		for (xhqm::size n = 0; n < N; ++n)
		for (xhqm::size ci = 0; ci < Cin; ++ci)
		for (xhqm::size ih = 0; ih < H; ++ih)
		for (xhqm::size iw = 0; iw < W; ++iw)
		{
			const xhqm::size g = ci / cin_g, ci_local = ci % cin_g, co0 = g * cout_g;
			for (xhqm::size co = co0; co < co0 + cout_g; ++co)
			for (xhqm::size fh = 0; fh < kh; ++fh)
			for (xhqm::size fw = 0; fw < kw; ++fw)
			{
				const std::int64_t num_h = (std::int64_t)ih + (std::int64_t)padding - (std::int64_t)fh * dilation;
				const std::int64_t num_w = (std::int64_t)iw + (std::int64_t)padding - (std::int64_t)fw * dilation;
				if (num_h < 0 || num_w < 0) continue;
				if (num_h % (std::int64_t)stride != 0 || num_w % (std::int64_t)stride != 0) continue;
				const std::int64_t oh = num_h / (std::int64_t)stride, ow = num_w / (std::int64_t)stride;
				if (oh >= (std::int64_t)OH || ow >= (std::int64_t)OW) continue;
				gx(n, ci, ih, iw) = xhqm::sum(gx(n, ci, ih, iw),
					xhqm::mul(dy(n, co, (xhqm::size)oh, (xhqm::size)ow), w(co, ci_local, fh, fw)));
			}
		}
		return gx;
	}

}
}
#endif
