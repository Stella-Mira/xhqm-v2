#pragma once
#ifndef XHQM_FANTASY_POINT_SHADOW
#define XHQM_FANTASY_POINT_SHADOW

// 点光阴影（M6）：立方体深度阴影图，6 面透视投影。
// 与 cascaded_shadow_map 同构：begin(face)/end 包住场景深度渲染，depth_texture() 供片元采样。
#include "fantasy.h"
#include "rhi_no.h"

namespace xhqm
{
	namespace fantasy
	{
		class xhqm_io point_shadow_map
		{
			render_backend* m_rhi = nullptr;
			uint m_target = 0;      // 立方体深度离屏 id
			uint m_cube_tex = 0;    // 立方体深度纹理（供采样）
			uint m_size = 0;
			std::vector<matrix> m_face_vp;    // 6 面 view-proj（+X/-X/+Y/-Y/+Z/-Z）
			std::vector<matrix> m_face_view; // 6 面 view（距离写入深度着色器用）
			bool m_ready = false;
		public:
			point_shadow_map() = default;
			~point_shadow_map();

			bool setup(render_backend& rhi, uint size, std::string& error);
			void reset();

			// 由点光位置 + 近远平面计算 6 面 view-proj（90° FOV 覆盖整个球面）
			void compute(const point& light_pos, value near, value far);

			void begin(uint face);   // face 0..5（+X/-X/+Y/-Y/+Z/-Z）
			void end();
			uint depth_texture();
			const matrix& face_vp(uint i) const { return m_face_vp[i < m_face_vp.size() ? i : 0]; }
			const matrix& face_view(uint i) const { return m_face_view[i < m_face_view.size() ? i : 0]; }
			bool ready() const { return m_ready; }
		};
	}
}

#endif
