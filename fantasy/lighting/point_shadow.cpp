#include "point_shadow.h"

namespace xhqm
{
	namespace fantasy
	{
		point_shadow_map::~point_shadow_map()
		{
			reset();
		}

		bool point_shadow_map::setup(render_backend& rhi, uint size, std::string& error)
		{
			reset();
			if (size == 0) { error = "illegal cube size"; return false; }
			m_rhi = &rhi;
			m_target = rhi.create_offscreen_cubemap_depth(size, error);
			if (!m_target) { reset(); return false; }
			m_size = size;
			m_face_vp.assign(6, matrix(4, 4));
			m_face_view.assign(6, matrix(4, 4));
			m_ready = true;
			return true;
		}

		void point_shadow_map::reset()
		{
			if (m_rhi && m_target)
			{
				m_rhi->delete_offscreen_cubemap_depth(m_target);
				m_target = 0;
			}
			m_cube_tex = 0;
			m_size = 0;
			m_ready = false;
			m_rhi = nullptr;
		}

		void point_shadow_map::compute(const point& light_pos, value near, value far)
		{
			if (!m_ready || far <= near) return;

			// 6 面方向与朝上（OpenGL cubemap 约定）
			static const value dirs[6][3] = {
				{ 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
			};
			static const value ups[6][3] = {
				{ 0, -1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }, { 0, -1, 0 }, { 0, -1, 0 }
			};

			matrix proj = xhqm::perspective(xhqm::radians(90.0f), value(1), near, far);
			vect<value> eye(light_pos.x(), light_pos.y(), light_pos.z());

			for (int i = 0; i < 6; ++i)
			{
				vect<value> f(dirs[i][0], dirs[i][1], dirs[i][2]);
				vect<value> up(ups[i][0], ups[i][1], ups[i][2]);
				vect<value> s = vect<value>::mul(f, up).normal();   // 右
				vect<value> u = vect<value>::mul(s, f);             // 上（正交化）

				matrix view(4, 4);
				view(0, 0) = s[0]; view(0, 1) = s[1]; view(0, 2) = s[2]; view(0, 3) = -vect<value>::dot(s, eye);
				view(1, 0) = u[0]; view(1, 1) = u[1]; view(1, 2) = u[2]; view(1, 3) = -vect<value>::dot(u, eye);
				view(2, 0) = -f[0]; view(2, 1) = -f[1]; view(2, 2) = -f[2]; view(2, 3) = vect<value>::dot(f, eye);
				view(3, 3) = value(1);

				m_face_vp[i] = proj * view;
				m_face_view[i] = view;
			}
		}

		void point_shadow_map::begin(uint face)
		{
			if (!m_ready || !m_target || face > 5) return;
			m_rhi->begin_offscreen_cubemap(m_target, face);
			m_rhi->color_write(false);   // 深度专用
		}

		void point_shadow_map::end()
		{
			if (!m_rhi) return;
			m_rhi->color_write(true);
			m_rhi->end_offscreen();
		}

		uint point_shadow_map::depth_texture()
		{
			if (!m_ready || !m_target) return 0;
			if (!m_cube_tex) m_cube_tex = m_rhi->offscreen_cubemap_texture(m_target);
			return m_cube_tex;
		}
	}
}
