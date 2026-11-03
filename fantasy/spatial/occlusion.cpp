#include "occlusion.h"
#include "shader_preprocess.h"
#include "shader_sources.h"

namespace xhqm
{
	namespace fantasy
	{
		// 每个候选对象一个槽：查询与包围盒跨帧复用
		struct occlusion_culler::slot
		{
			object* obj = nullptr;
			aabb world_aabb;      // 上次构建包围盒使用的世界 AABB
			uint vb = 0;          // 世界空间包围盒顶点缓冲（12 三角形 = 36 顶点）
			uint query = 0;       // 遮挡查询 id
			uint visible = 0;     // 上一帧读回的样本数
			bool queried = false; // 是否已记录过至少一次查询
			bool has_box = false; // 是否已构建包围盒
		};

		// 从世界 AABB 生成 12 三角形（36 顶点）的世界空间包围盒
		static void push_box(std::vector<value>& out, const aabb& box)
		{
			value x0 = box.min.x(), x1 = box.max.x();
			value y0 = box.min.y(), y1 = box.max.y();
			value z0 = box.min.z(), z1 = box.max.z();
			point c[8] = {
				point(x0, y0, z0), point(x1, y0, z0), point(x1, y1, z0), point(x0, y1, z0),
				point(x0, y0, z1), point(x1, y0, z1), point(x1, y1, z1), point(x0, y1, z1),
			};
			static const uint idx[36] = {
				0,2,1, 0,3,2,   // -z
				4,5,6, 4,6,7,   // +z
				0,1,5, 0,5,4,   // -y
				3,7,6, 3,6,2,   // +y
				0,4,7, 0,7,3,   // -x
				1,2,6, 1,6,5,   // +x
			};
			for (uint i = 0; i < 36; ++i)
			{
				const point& p = c[idx[i]];
				out.push_back(p.x());
				out.push_back(p.y());
				out.push_back(p.z());
			}
		}

		static bool same_point(const point& a, const point& b)
		{
			return a.x() == b.x() && a.y() == b.y() && a.z() == b.z();
		}

		static bool same_aabb(const aabb& a, const aabb& b)
		{
			return same_point(a.min, b.min) && same_point(a.max, b.max);
		}

		occlusion_culler::~occlusion_culler()
		{
			reset();
		}

		bool occlusion_culler::setup(render_backend& rhi, std::string& error)
		{
			m_rhi = &rhi;
			const bool vulkan = (rhi.name()[0] == 'v');
			m_program = rhi.shader_create(
				shader_preprocess(shader_sources::occlusion_vert(), vulkan),
				shader_preprocess(shader_sources::occlusion_frag(), vulkan), error);
			m_ready = (m_program != 0);
			return m_ready;
		}

		void occlusion_culler::reset()
		{
			if (m_rhi)
			{
				for (auto* s : m_slots)
				{
					if (s->vb) m_rhi->vertex_delete(s->vb);
					if (s->query) m_rhi->query_delete(s->query);
					delete s;
				}
				if (m_program) { m_rhi->shader_delete(m_program); m_program = 0; }
			}
			m_slots.clear();
			m_has_frame = false;
			m_ready = false;
			m_rhi = nullptr;
		}

		occlusion_culler::slot* occlusion_culler::slot_of(object* obj)
		{
			for (auto* s : m_slots) if (s->obj == obj) return s;
			slot* s = new slot();
			s->obj = obj;
			m_slots.push_back(s);
			return s;
		}

		void occlusion_culler::rebuild_box(slot& s, const aabb& box)
		{
			if (s.vb) { m_rhi->vertex_delete(s.vb); s.vb = 0; }
			std::vector<value> verts;
			push_box(verts, box);
			std::vector<uint> locations = { 3 };
			std::string err;
			s.vb = m_rhi->vertex_create(verts, locations, err);
		}

		std::vector<object*> occlusion_culler::cull_and_record(
			const matrix& clip_from_world, const std::vector<std::pair<aabb, object*>>& candidates)
		{
			std::vector<object*> visible;
			if (!m_ready) return visible;

			// 1) 读上一帧查询结果（首帧跳过：无上一帧）
			if (m_has_frame)
			{
				for (auto* s : m_slots)
				{
					if (s->query) s->visible = m_rhi->query_result(s->query);
				}
			}

			// 2) 决定可见集合：首帧全可见；之后用上一帧样本数裁剪
			for (auto& c : candidates)
			{
				slot* s = slot_of(c.second);
				bool vis = true;
				if (m_has_frame) vis = (!s->queried) || (s->visible > m_threshold);
				if (vis) visible.push_back(c.second);
			}

			// 3) 记录本帧查询 pass：颜色写关、只写深度（构建遮挡）
			m_rhi->color_write(false);
			m_rhi->depth_test(true, false);
			m_rhi->use_program(m_program);
			m_rhi->uniform_matrix(m_program, "tranMatrix", clip_from_world.tramat().data(), 1);
			for (auto& c : candidates)
			{
				slot* s = slot_of(c.second);
				if (!s->has_box || !same_aabb(s->world_aabb, c.first))
				{
					rebuild_box(*s, c.first);
					s->world_aabb = c.first;
					s->has_box = true;
				}
				if (!s->query)
				{
					std::string err;
					s->query = m_rhi->query_create(err);
				}
				if (!s->query) continue;
				m_rhi->query_begin(s->query);
				m_rhi->draw(s->vb, rhi_primitive::triangles, 0, 36);
				m_rhi->query_end(s->query);
				s->queried = true;
			}
			m_rhi->color_write(true);

			m_has_frame = true;
			return visible;
		}
	}
}
