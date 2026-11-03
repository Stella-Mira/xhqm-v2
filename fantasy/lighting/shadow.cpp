#include "shadow.h"

namespace xhqm
{
	namespace fantasy
	{
		shadow_map::~shadow_map()
		{
			reset();
		}

		bool shadow_map::setup(render_backend& rhi, uint w, uint h, std::string& error)
		{
			reset();
			m_rhi = &rhi;
			m_target = rhi.create_offscreen(w, h, 0, error);   // 深度专用
			if (!m_target) return false;
			m_w = w; m_h = h;
			m_ready = true;
			return true;
		}

		void shadow_map::reset()
		{
			if (m_rhi && m_target)
			{
				m_rhi->delete_offscreen(m_target);
				m_target = 0;
			}
			m_depth_tex = 0;
			m_ready = false;
			m_rhi = nullptr;
		}

		matrix shadow_map::compute_light_view_proj(const point& direction, const aabb& world)
		{
			// 世界 AABB 中心与包围球半径
			vect<value> cmin(world.min.x(), world.min.y(), world.min.z());
			vect<value> cmax(world.max.x(), world.max.y(), world.max.z());
			vect<value> center = (cmin + cmax) * value(0.5);
			value radius = (cmax - center).modulus();
			if (radius < value(1e-4)) radius = value(1.0);

			// 光方向（光 → 场景，单位化）
			vect<value> f(direction.x(), direction.y(), direction.z());
			f = f.normal();
			// 光位置：中心沿反方向后退 radius
			vect<value> eye = center - f * radius;

			// 正交基（lookAt）
			vect<value> up(value(0), value(0), value(1));
			if (std::abs(vect<value>::dot(f, up)) > value(0.99)) up = vect<value>(value(0), value(1), value(0));
			vect<value> s = vect<value>::mul(f, up).normal();   // 右
			vect<value> u = vect<value>::mul(s, f);             // 上

			// 视图矩阵（列主序，世界 → 光视图）
			matrix view(4, 4);
			view(0, 0) = s[0]; view(0, 1) = s[1]; view(0, 2) = s[2]; view(0, 3) = -vect<value>::dot(s, eye);
			view(1, 0) = u[0]; view(1, 1) = u[1]; view(1, 2) = u[2]; view(1, 3) = -vect<value>::dot(u, eye);
			view(2, 0) = -f[0]; view(2, 1) = -f[1]; view(2, 2) = -f[2]; view(2, 3) = vect<value>::dot(f, eye);
			view(3, 3) = value(1);

			// 正交投影：覆盖光空间 XY 包围球，深度 [0, 2*radius]
			matrix proj = xhqm::ortho(-radius, radius, -radius, radius, value(0), value(2) * radius);
			return proj * view;
		}

		void shadow_map::begin()
		{
			if (!m_rhi || !m_target) return;
			m_rhi->begin_offscreen(m_target);
			m_rhi->color_write(false);   // 深度专用：关闭颜色写，只写深度
		}

		void shadow_map::end()
		{
			if (!m_rhi) return;
			m_rhi->color_write(true);
			m_rhi->end_offscreen();
		}

		uint shadow_map::depth_texture()
		{
			if (!m_rhi || !m_target) return 0;
			if (!m_depth_tex) m_depth_tex = m_rhi->offscreen_depth_texture(m_target);
			return m_depth_tex;
		}
	}
}
