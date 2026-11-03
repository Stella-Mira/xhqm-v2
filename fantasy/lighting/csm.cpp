#include "csm.h"

namespace xhqm
{
	namespace fantasy
	{
		cascaded_shadow_map::~cascaded_shadow_map()
		{
			reset();
		}

		bool cascaded_shadow_map::setup(render_backend& rhi, uint count, uint w, uint h, std::string& error)
		{
			reset();
			if (count == 0 || count > 4) { error = "illegal cascade count"; return false; }
			m_rhi = &rhi;
			m_targets.resize(count, 0);
			m_depth_tex.resize(count, 0);
			m_light_vp.resize(count, matrix(4, 4));
			m_split.resize(count, value(0));
			for (uint i = 0; i < count; ++i)
			{
				m_targets[i] = rhi.create_offscreen(w, h, 0, error);   // 深度专用
				if (!m_targets[i]) { reset(); return false; }
			}
			m_w = w; m_h = h;
			m_ready = true;
			return true;
		}

		void cascaded_shadow_map::reset()
		{
			if (m_rhi)
			{
				for (uint t : m_targets) { if (t) m_rhi->delete_offscreen(t); }
			}
			m_targets.clear();
			m_depth_tex.clear();
			m_light_vp.clear();
			m_split.clear();
			m_ready = false;
			m_rhi = nullptr;
		}

		void cascaded_shadow_map::compute(const point& camera_pos, const point& direction, value near, value far, value lambda)
		{
			uint n = (uint)m_targets.size();
			if (n == 0 || far <= near) return;

			// 对数/线性混合分割
			std::vector<value> split(n + 1);
			for (uint i = 0; i <= n; ++i)
			{
				value t = value(i) / value(n);
				value log_split = near * std::pow(far / near, t);
				value lin_split = near + (far - near) * t;
				split[i] = lambda * log_split + (value(1) - lambda) * lin_split;
			}
			// 逐级：以相机为球心，覆盖半径 = 该级 far 分割距离
			for (uint i = 0; i < n; ++i)
			{
				value r = split[i + 1];
				if (r < value(1e-3)) r = value(1e-3);
				aabb box;
				box.min = point(camera_pos.x() - r, camera_pos.y() - r, camera_pos.z() - r);
				box.max = point(camera_pos.x() + r, camera_pos.y() + r, camera_pos.z() + r);
				m_light_vp[i] = shadow_map::compute_light_view_proj(direction, box);
				m_split[i] = split[i + 1];
			}
		}

		void cascaded_shadow_map::begin(uint i)
		{
			if (!m_rhi || i >= m_targets.size() || !m_targets[i]) return;
			m_rhi->begin_offscreen(m_targets[i]);
			m_rhi->color_write(false);   // 深度专用
		}

		void cascaded_shadow_map::end()
		{
			if (!m_rhi) return;
			m_rhi->color_write(true);
			m_rhi->end_offscreen();
		}

		uint cascaded_shadow_map::depth_texture(uint i)
		{
			if (!m_rhi || i >= m_targets.size() || !m_targets[i]) return 0;
			if (!m_depth_tex[i]) m_depth_tex[i] = m_rhi->offscreen_depth_texture(m_targets[i]);
			return m_depth_tex[i];
		}
	}
}
