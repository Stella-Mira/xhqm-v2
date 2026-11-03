#pragma once
#ifndef XHQM_FANTASY_SSR
#define XHQM_FANTASY_SSR

// 屏幕空间反射（M8，可选）：片元由线性视图深度重建位置与法线，反射视线，在屏幕空间沿反射
// 射线步进采样颜色/深度求交，输出反射色。输入 color_tex（RGBA32F）+ depth_tex（RGBA32F，R=视图深度）。
#include "fantasy.h"
#include "backend.h"
#include "rhi_no.h"

namespace xhqm
{
	namespace fantasy
	{
		class xhqm_io ssr_effect
		{
			render_backend* m_rhi = nullptr;
			uint m_quad_vb = 0;
			uint m_quad_ib = 0;
			uint m_target = 0;
			uint m_shader = 0;
			bool m_ready = false;
			uint m_w = 0, m_h = 0;
		public:
			ssr_effect() = default;
			~ssr_effect();

			bool setup(render_backend& rhi, uint w, uint h, std::string& error);
			void reset();
			// params = aspect / tanHalfFov / stepSize / bias
			uint apply(uint color_tex, uint depth_tex, value aspect, value tan_half_fov, value step_size, value bias);
			uint target() const { return m_target; }
		};
	}
}

#endif
