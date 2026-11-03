#pragma once
#ifndef XHQM_FANTASY_COMPOSE
#define XHQM_FANTASY_COMPOSE

// 通用合成 pass：把 overlay 纹理按模式合成到 base 纹理。
// mode 0 = multiply（AO：scene × ao）；mode 1 = add（SSR：scene + reflection）。
#include "fantasy.h"
#include "rhi_no.h"

namespace xhqm
{
	namespace fantasy
	{
		class compose_effect
		{
			render_backend* m_rhi = nullptr;
			uint m_w = 0, m_h = 0;
			uint m_quad_vb = 0;
			uint m_quad_ib = 0;
			uint m_target = 0;      // 合成结果离屏（HDR，与场景一致）
			uint m_shader = 0;
			bool m_ready = false;
		public:
			compose_effect() = default;
			~compose_effect();

			bool setup(render_backend& rhi, uint w, uint h, std::string& error);
			void reset();

			// mode: 0 = multiply（AO），1 = add（SSR）
			uint apply(uint base_tex, uint overlay_tex, int mode);
			bool ready() const { return m_ready; }
		};
	}
}

#endif
