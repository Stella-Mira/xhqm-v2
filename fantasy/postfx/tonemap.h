#pragma once
#ifndef XHQM_FANTASY_TONEMAP
#define XHQM_FANTASY_TONEMAP

// 独立 tonemap pass（M5）：读 HDR 场景纹理，做 tonemap（Reinhard）+ gamma，输出 LDR 纹理
#include "fantasy.h"
#include "rhi_no.h"

namespace xhqm
{
	namespace fantasy
	{
		class tonemap_effect
		{
			render_backend* m_rhi = nullptr;
			uint m_w = 0, m_h = 0;
			uint m_quad_vb = 0, m_quad_ib = 0;
			uint m_target = 0;      // LDR 结果离屏（RGBA8）
			uint m_shader = 0;      // tonemap shader
			uint m_shader_blit = 0; // blit shader（LDR → 当前目标）
			bool m_ready = false;
		public:
			tonemap_effect() = default;
			~tonemap_effect();

			bool setup(render_backend& rhi, uint w, uint h, std::string& error);
			void reset();

			// 输入 HDR 场景纹理，输出 LDR 纹理（exposure 为曝光）
			uint apply(uint hdr_texture, value exposure = 1.0f);
			// 把 LDR 纹理 blit 到当前目标（swapchain）
			void present(uint texture);

			bool ready() const { return m_ready; }
		};
	}
}

#endif
