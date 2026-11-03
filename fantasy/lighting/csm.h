#pragma once
#ifndef XHQM_FANTASY_CSM
#define XHQM_FANTASY_CSM

// 级联阴影映射（CSM）：多级深度目标，近处高精度 / 远处低精度
// 每级以相机位置为球心、按分割距离取覆盖半径，光方向一致，逐级放大覆盖范围
#include "fantasy.h"
#include "drawer.h"
#include "rhi_no.h"
#include "shadow.h"

namespace xhqm
{
	namespace fantasy
	{
		class cascaded_shadow_map
		{
			render_backend* m_rhi = nullptr;
			std::vector<uint> m_targets;      // 每级深度离屏 id
			std::vector<uint> m_depth_tex;    // 每级深度纹理采样句柄（延迟创建）
			std::vector<matrix> m_light_vp;   // 每级光空间 view-proj
			std::vector<value> m_split;       // 每级 far 分割距离（供 shader 选级）
			uint m_w = 0, m_h = 0;
			bool m_ready = false;
		public:
			cascaded_shadow_map() = default;
			~cascaded_shadow_map();

			// 创建 count 级深度目标
			bool setup(render_backend& rhi, uint count, uint w, uint h, std::string& error);
			void reset();

			// 计算级联：以相机为球心，按对数/线性混合分割 near..far，逐级算光空间 VP
			void compute(const point& camera_pos, const point& direction, value near, value far, value lambda);

			void begin(uint i);   // 渲染第 i 级深度（关颜色写）
			void end();

			uint count() const { return (uint)m_targets.size(); }
			uint depth_texture(uint i);
			const matrix& light_view_proj(uint i) const { return m_light_vp[i]; }
			value split_far(uint i) const { return (i < m_split.size()) ? m_split[i] : value(0); }
			bool ready() const { return m_ready; }
		};
	}
}

#endif
