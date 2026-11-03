#pragma once
#ifndef XHQM_FANTASY_TAA
#define XHQM_FANTASY_TAA

// 时间抗锯齿（M8）：把当前帧与历史帧按指数滑动平均混合，并对当前颜色做「历史邻域钳制」
// （3x3 min/max clamp）抑制移动物体的鬼影。jitter 由 taa_jitter 提供亚像素偏移（Halton 序列）。
#include "fantasy.h"
#include "backend.h"
#include "rhi_no.h"
#include <array>

namespace xhqm
{
	namespace fantasy
	{
		// 亚像素 jitter 偏移（Halton 2,3），返回 [x,y] ∈ [-0.5, 0.5]
		xhqm_io std::array<value, 2> taa_jitter(uint frame_index);

		class xhqm_io taa_effect
		{
			render_backend* m_rhi = nullptr;
			uint m_quad_vb = 0;
			uint m_quad_ib = 0;
			uint m_target = 0;
			uint m_shader = 0;
			bool m_ready = false;
			uint m_w = 0, m_h = 0;
		public:
			taa_effect() = default;
			~taa_effect();

			bool setup(render_backend& rhi, uint w, uint h, std::string& error);
			void reset();
			// 混合 current 与 history 到内部离屏，返回输出纹理；alpha ∈ [0,1]（历史权重 = 1-alpha）
			uint apply(uint current_tex, uint history_tex, value alpha);
			uint target() const { return m_target; }
		};
	}
}

#endif
