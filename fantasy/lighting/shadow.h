#pragma once
#ifndef XHQM_FANTASY_SHADOW
#define XHQM_FANTASY_SHADOW

// 阴影映射（方向光）：深度专用离屏目标 + 光空间 view-proj + 深度纹理采样句柄
#include "fantasy.h"
#include "drawer.h"
#include "rhi_no.h"

namespace xhqm
{
	namespace fantasy
	{
		class shadow_map
		{
			render_backend* m_rhi = nullptr;
			uint m_target = 0;       // 深度专用离屏 id（create_offscreen(w,h,0)）
			uint m_depth_tex = 0;    // 深度纹理采样句柄（延迟创建）
			uint m_w = 0, m_h = 0;
			matrix m_light_vp = matrix(4, 4);   // 光空间 view-proj（世界 → 光裁剪空间；默认 4x4 保证可默认构造）
			bool m_ready = false;
		public:
			shadow_map() = default;
			~shadow_map();

			// 创建深度目标（color_count==0 的深度专用离屏）
			bool setup(render_backend& rhi, uint w, uint h, std::string& error);
			void reset();

			// 计算方向光 view-proj：正交覆盖世界 AABB（中心 + 半径），视线沿 direction
			static matrix compute_light_view_proj(const point& direction, const aabb& world);

			// 绑定/解绑深度目标（阴影 pass 内渲染场景深度）
			void begin();
			void end();

			const matrix& light_view_proj() const { return m_light_vp; }
			void light_view_proj(const matrix& vp) { m_light_vp = vp; }
			uint depth_texture();   // 深度纹理采样句柄（供 bind_texture）
			bool ready() const { return m_ready; }
			uint width() const { return m_w; }
			uint height() const { return m_h; }
		};
	}
}

#endif
