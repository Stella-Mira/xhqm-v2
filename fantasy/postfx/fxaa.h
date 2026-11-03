#pragma once
#ifndef XHQM_FANTASY_FXAA
#define XHQM_FANTASY_FXAA

// FXAA 后处理抗锯齿：边缘检测 + 沿边缘混合，输入场景纹理，输出平滑结果纹理
#include "fantasy.h"
#include "rhi_no.h"

namespace xhqm
{
	namespace fantasy
	{
		class fxaa_effect
		{
			render_backend* m_rhi = nullptr;
			uint m_w = 0, m_h = 0;
			uint m_quad_vb = 0;
			uint m_quad_ib = 0;
			uint m_target = 0;      // 结果离屏
			uint m_shader_fxaa = 0;
			uint m_shader_blit = 0;
			bool m_ready = false;
		public:
			fxaa_effect() = default;
			~fxaa_effect();

			bool setup(render_backend& rhi, uint w, uint h, std::string& error);
			void reset();

			uint apply(uint scene_texture);   // 返回 AA 结果纹理（离屏）
			void present(uint texture);       // 全屏直通到 swapchain
			bool ready() const { return m_ready; }
		};
	}
}

#endif
