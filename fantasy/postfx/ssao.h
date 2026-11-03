#pragma once
#ifndef XHQM_FANTASY_SSAO
#define XHQM_FANTASY_SSAO

// 屏幕空间环境光遮蔽（M8）：片元着色器由「线性视图深度」纹理重建视图空间位置，
// 用 dFdx/dFdy 重建法线，在半球核内采样深度做遮挡累积，输出 AO 灰度纹理（0=遮蔽 1=无遮蔽）。
// 输入 depth_tex 约定为浮点纹理（RGBA32F），R 通道存线性视图深度（正数，0=近）。
#include "fantasy.h"
#include "backend.h"
#include "rhi_no.h"

namespace xhqm
{
	namespace fantasy
	{
		class xhqm_io ssao_effect
		{
			render_backend* m_rhi = nullptr;
			uint m_quad_vb = 0;
			uint m_quad_ib = 0;
			uint m_target = 0;      // AO 输出离屏（RGBA8）
			uint m_shader = 0;
			bool m_ready = false;
			uint m_w = 0, m_h = 0;
		public:
			ssao_effect() = default;
			~ssao_effect();

			bool setup(render_backend& rhi, uint w, uint h, std::string& error);
			void reset();
			// 采样 depth_tex 计算 AO，输出到内部离屏，返回 AO 纹理；params = aspect/tanHalfFov/radius/bias
			uint apply(uint depth_tex, value aspect, value tan_half_fov, value radius, value bias);
			uint target() const { return m_target; }   // 供验证读回
		};
	}
}

#endif
