#include "element.h"
#include "rhi_no.h"
#include "shader_preprocess.h"
#include "shader_sources.h"

namespace xhqm
{
	namespace fantasy
	{
		void poits::shader()
		{
			if (poits::_shader == 0)
			{
				const bool vulkan = (render_backend_active().name()[0] == 'v');
				poits::_shader = fantasy::shader(
					shader_preprocess(shader_sources::point_line_ploy_vert(), vulkan),
					shader_preprocess(shader_sources::point_line_ploy_frag(), vulkan));
			}
			if (shader_id() == 0) { _shader_id = poits::_shader; };
		}
		void poits::updata()
		{
			if (shader_id() == 0) { shader(); }
			if (m_texture == 0) { m_texture.reset(_colour); }

			for (const auto& poit : m_poits)
			{
				m_attrib.push_back(std::get<0>(poit).x());
				m_attrib.push_back(std::get<0>(poit).y());
				m_attrib.push_back(std::get<0>(poit).z());
				m_attrib.push_back(std::get<1>(poit));
			}

			std::vector<fantasy::uint> locations{ 3, 1 };
			_vertex_id.reset(m_attrib, locations);
			drawer::_aabb_(4);
			m_attrib.clear();
		}
		void poits::render()
		{
			if (!_shader_id || !_vertex_id || !m_system.data()) return;

			render_backend& rhi = render_backend_active();
			fantasy::uint program = static_cast<fantasy::uint>(_shader_id);
			rhi.use_program(program);
			{
				rhi.uniform_matrix(program, "tranMatrix", m_system.tramat().data(), 1);
				std::array<xhqm::real, 4> sel = { 1,1,1,1 };
				if (_is_select) { sel = xhqm::color::rgba2array(_select); }
				rhi.uniform_vec4(program, "modelSelect", sel[0], sel[1], sel[2], sel[3]);
			}
			rhi.point_size(_poit_size);
			rhi.line_size(_line_size);
			rhi.bind_texture(0, static_cast<uint>(m_texture));
			rhi.draw(_vertex_id, rhi_primitive::points, 0, static_cast<uint>(m_poits.size()));
			rhi.bind_texture(0, 0);
			rhi.use_program(0);
		}
		void poits::_render_id_()
		{
			render_backend_active().draw(_vertex_id, rhi_primitive::points, 0, static_cast<uint>(m_poits.size()));
		}

		void lines::shader()
		{
			if (lines::_shader == 0)
			{
				const bool vulkan = (render_backend_active().name()[0] == 'v');
				lines::_shader = fantasy::shader(
					shader_preprocess(shader_sources::point_line_ploy_vert(), vulkan),
					shader_preprocess(shader_sources::point_line_ploy_frag(), vulkan));
			}
			if (shader_id() == 0) { _shader_id = lines::_shader; };
		}
		void lines::updata()
		{
			if (shader_id() == 0) { shader(); }
			if (m_texture == 0) { m_texture.reset(_colour); }

			for (const auto& line : m_lines)
			{
				m_attrib.push_back(std::get<0>(line).point_A().x());
				m_attrib.push_back(std::get<0>(line).point_A().y());
				m_attrib.push_back(std::get<0>(line).point_A().z());
				m_attrib.push_back(std::get<1>(line));
				m_attrib.push_back(std::get<0>(line).point_B().x());
				m_attrib.push_back(std::get<0>(line).point_B().y());
				m_attrib.push_back(std::get<0>(line).point_B().z());
				m_attrib.push_back(std::get<1>(line));
			}

			std::vector<fantasy::uint> locations{ 3, 1 };
			_vertex_id.reset(m_attrib, locations);
			drawer::_aabb_(4);
			m_attrib.clear();
		}
		void lines::render()
		{
			if (!_shader_id || !_vertex_id || !m_system.data()) return;

			render_backend& rhi = render_backend_active();
			fantasy::uint program = static_cast<fantasy::uint>(_shader_id);
			rhi.use_program(program);
			{
				rhi.uniform_matrix(program, "tranMatrix", m_system.tramat().data(), 1);
				std::array<xhqm::real, 4> sel = { 1,1,1,1 };
				if (_is_select) { sel = xhqm::color::rgba2array(_select); }
				rhi.uniform_vec4(program, "modelSelect", sel[0], sel[1], sel[2], sel[3]);
			}
			rhi.point_size(_poit_size);
			rhi.line_size(_line_size);
			rhi.bind_texture(0, static_cast<uint>(m_texture));
			rhi.draw(_vertex_id, rhi_primitive::lines, 0, static_cast<uint>(2 * m_lines.size()));
			rhi.bind_texture(0, 0);
			rhi.use_program(0);
		}
		void lines::_render_id_()
		{
			render_backend_active().draw(_vertex_id, rhi_primitive::lines, 0, static_cast<uint>(2 * m_lines.size()));
		}

		void ploys::shader()
		{
			if (ploys::_shader == 0)
			{
				const bool vulkan = (render_backend_active().name()[0] == 'v');
				ploys::_shader = fantasy::shader(
					shader_preprocess(shader_sources::point_line_ploy_vert(), vulkan),
					shader_preprocess(shader_sources::point_line_ploy_frag(), vulkan));
			}
			if (shader_id() == 0) { _shader_id = ploys::_shader; };
		}
		void ploys::updata()
		{
			if (shader_id() == 0) { shader(); }
			if (m_texture == 0) { m_texture.reset(_colour); }

			for (const auto& topol : m_topols)
			{
				for (const auto& index : topol)
				{
					m_attrib.push_back(m_points[index].x());
					m_attrib.push_back(m_points[index].y());
					m_attrib.push_back(m_points[index].z());
					m_attrib.push_back(m_values[index]);
				}
			}

			std::vector<fantasy::uint> locations{ 3, 1 };
			_vertex_id.reset(m_attrib, locations);
			drawer::_aabb_(4);
			m_attrib.clear();
		}

		static rhi_primitive topol_primitive(uint num_vertices)
		{
			switch (num_vertices)
			{
			case 1: return rhi_primitive::points;
			case 2: return rhi_primitive::lines;
			case 3: return rhi_primitive::triangles;
			case 4: return rhi_primitive::quads;
			default: return rhi_primitive::triangle_fan;
			}
		}

		// 把「连续且顶点数相同」的 topol 合并成一次 draw。
		// 动机：一个 topol 一次 draw 时，一个坐标 gizmo（coord = 1 个 poits + 3 个 arrow，
		// 每个 arrow 是 2 个 11 段 frustum 网格）就能发出 199 次 draw；而 Vulkan 后端每次
		// draw 有固定开销（把 uniform 固化进本次绘制独占的 UBO slot），4 个 drawer 于是
		// 吃掉 39ms/pass、两个 pass 78ms/帧 —— 比果蝇本体的 85 个 mesh 还贵。
		// 语义等价的条件：
		//   顶点在顶点缓冲里本就连续（startIndex 累加）；同顶点数 ⇒ 同 topol_primitive。
		//   1/2/3/4 顶点 = points/lines/triangles/quads，都是「独立图元」，相邻两段拼起来
		//   仍是各自独立（GL_QUADS 拼 2 个 quad、VK TRIANGLE_LIST 拼 2 个三角形，都对）。
		//   >4 顶点会落到 triangle_fan —— 扇形依赖首顶点，拼起来会把两个扇形连成一个，
		//   所以这一档不合并（当前几何只产出 3/4 顶点面，但不赌这个前提）。
		void ploys::_draw_runs_()
		{
			render_backend& rhi = render_backend_active();
			uint startIndex = 0;
			const std::size_t n = m_topols.size();
			std::size_t i = 0;
			while (i < n)
			{
				const uint s = static_cast<uint>(m_topols[i].size());
				std::size_t j = i + 1;
				uint total = s;
				if (s <= 4)
				{
					while (j < n && static_cast<uint>(m_topols[j].size()) == s) { total += s; ++j; }
				}
				// total == 0 不能下发：backend 把 count==0 解释为「整个顶点缓冲」
				if (total > 0) rhi.draw(_vertex_id, topol_primitive(s), startIndex, total);
				startIndex += total;
				i = j;
			}
		}

		void ploys::render()
		{
			if (!_shader_id || !_vertex_id || !m_system.data()) return;

			render_backend& rhi = render_backend_active();
			fantasy::uint program = static_cast<fantasy::uint>(_shader_id);
			rhi.use_program(program);
			{
				rhi.uniform_matrix(program, "tranMatrix", m_system.tramat().data(), 1);
				std::array<xhqm::real, 4> sel = { 1,1,1,1 };
				if (_is_select) { sel = xhqm::color::rgba2array(_select); }
				rhi.uniform_vec4(program, "modelSelect", sel[0], sel[1], sel[2], sel[3]);
			}
			rhi.point_size(_poit_size);
			rhi.line_size(_line_size);
			rhi.bind_texture(0, static_cast<uint>(m_texture));
			ploys::_draw_runs_();
			rhi.bind_texture(0, 0);
			rhi.use_program(0);
		}
		void ploys::_render_id_()
		{
			ploys::_draw_runs_();
		}

		void curve::shader()
		{
			if (curve::_shader == 0)
			{
				const bool vulkan = (render_backend_active().name()[0] == 'v');
				curve::_shader = fantasy::shader(
					shader_preprocess(shader_sources::curve_vert(), vulkan),
					shader_preprocess(shader_sources::curve_frag(), vulkan));
			}
			if (shader_id() == 0) { _shader_id = curve::_shader; };
		}
		void curve::updata()
		{
			if (shader_id() == 0) { shader(); }
			if (m_texture == 0) { m_texture.reset(_colour); }
			if (m_nurbs.nurbs_p().empty()) return;   // 空点列防护（防 .front() 越界）

			aabb box;
			box.min = m_nurbs.nurbs_p().front();
			box.max = m_nurbs.nurbs_p().front();
			for (const auto& point : m_nurbs.nurbs_p())
			{
				box.min.x() = std::min(box.min.x(), point.x());
				box.min.y() = std::min(box.min.y(), point.y());
				box.min.z() = std::min(box.min.z(), point.z());
				box.max.x() = std::max(box.max.x(), point.x());
				box.max.y() = std::max(box.max.y(), point.y());
				box.max.z() = std::max(box.max.z(), point.z());
			}
			uint size = static_cast<uint>((box.max - box.min).length() / m_ratio);
			if (size < 10) size = 10;

			m_shape = 0;
			m_count = 0;
			fantasy::value value = 0;
			fantasy::index index = 0;
			fantasy::values attribs;
			fantasy::values values = geometry::nurbs::linspace(size, 0, 1);
			std::vector<point> points_vec = m_nurbs.creat_points(values);
			fantasy::count count_1 = points_vec.size();
			for (auto& point : points_vec)
			{
				if (m_nurbs.nurbs_k() != xhqm::npos)
				{
					value = values[index++];
				}
				attribs.push_back(point.x());
				attribs.push_back(point.y());
				attribs.push_back(point.z());
				attribs.push_back(value);
			}
			if (m_nurbs.nurbs_k() == xhqm::npos)
			{
				for (fantasy::index i = 0; i < count_1; ++i)
				{
					m_shape = 1;
					++m_count;
					m_attrib.push_back(attribs[i * 4 + 0]);
					m_attrib.push_back(attribs[i * 4 + 1]);
					m_attrib.push_back(attribs[i * 4 + 2]);
					m_attrib.push_back(attribs[i * 4 + 3]);
				}
			}
			else
			{
				for (fantasy::index i = 1; i < count_1; ++i)
				{
					m_shape = 2;
					++m_count;
					m_attrib.push_back(attribs[(i - 1) * 4 + 0]);
					m_attrib.push_back(attribs[(i - 1) * 4 + 1]);
					m_attrib.push_back(attribs[(i - 1) * 4 + 2]);
					m_attrib.push_back(attribs[(i - 1) * 4 + 3]);
					++m_count;
					m_attrib.push_back(attribs[(i - 0) * 4 + 0]);
					m_attrib.push_back(attribs[(i - 0) * 4 + 1]);
					m_attrib.push_back(attribs[(i - 0) * 4 + 2]);
					m_attrib.push_back(attribs[(i - 0) * 4 + 3]);
				}
			}

			std::vector<fantasy::uint> locations{ 3, 1 };
			_vertex_id.reset(m_attrib, locations);
			drawer::_aabb_(4);
			m_attrib.clear();
		}
		void curve::render()
		{
			if (!_shader_id || !_vertex_id || !m_system.data()) return;

			render_backend& rhi = render_backend_active();
			fantasy::uint program = static_cast<fantasy::uint>(_shader_id);
			rhi.use_program(program);
			{
				rhi.uniform_matrix(program, "tranMatrix", m_system.tramat().data(), 1);
				std::array<xhqm::real, 4> sel = { 1,1,1,1 };
				if (_is_select) { sel = xhqm::color::rgba2array(_select); }
				rhi.uniform_vec4(program, "modelSelect", sel[0], sel[1], sel[2], sel[3]);
			}
			rhi.point_size(_poit_size);
			rhi.line_size(_line_size);
			rhi.bind_texture(0, static_cast<uint>(m_texture));

			if (m_shape == 1) { rhi.draw(_vertex_id, rhi_primitive::points, 0, static_cast<uint>(m_count)); }
			else if (m_shape == 2) { rhi.draw(_vertex_id, rhi_primitive::lines, 0, static_cast<uint>(m_count)); }

			rhi.bind_texture(0, 0);
			rhi.use_program(0);
		}
		void curve::_render_id_()
		{
			render_backend& rhi = render_backend_active();
			if (m_shape == 1) { rhi.draw(_vertex_id, rhi_primitive::points, 0, static_cast<uint>(m_count)); }
			else if (m_shape == 2) { rhi.draw(_vertex_id, rhi_primitive::lines, 0, static_cast<uint>(m_count)); }
		}

		void nurbs::shader()
		{
			if (nurbs::_shader == 0)
			{
				const bool vulkan = (render_backend_active().name()[0] == 'v');
				nurbs::_shader = fantasy::shader(
					shader_preprocess(shader_sources::nurbs_vert(), vulkan),
					shader_preprocess(shader_sources::nurbs_frag(), vulkan));
			}
			if (shader_id() == 0) { _shader_id = nurbs::_shader; };
		}
		void nurbs::updata()
		{
			if (shader_id() == 0) { shader(); }
			if (m_nurbs.nurbs_p().empty()) return;
			if (m_nurbs.nurbs_p().front().empty()) return;

			aabb box;
			box.min = m_nurbs.nurbs_p()[0].front();
			box.max = m_nurbs.nurbs_p()[0].front();
			for (const auto& points : m_nurbs.nurbs_p())
			{
				for (auto& point : points)
				{
					box.min.x() = std::min(box.min.x(), point.x());
					box.min.y() = std::min(box.min.y(), point.y());
					box.min.z() = std::min(box.min.z(), point.z());
					box.max.x() = std::max(box.max.x(), point.x());
					box.max.y() = std::max(box.max.y(), point.y());
					box.max.z() = std::max(box.max.z(), point.z());
				}
			}
			uint size = static_cast<uint>((box.max - box.min).length() / m_ratio);
			if (size < 10) size = 10;

			fantasy::value value_u = 0;
			fantasy::value value_v = 0;
			fantasy::index index_u = 0;
			fantasy::index index_v = 0;
			fantasy::values attribs;
			fantasy::values values1 = geometry::nurbs::linspace(size, 0, 1);
			fantasy::values values2 = geometry::nurbs::linspace(size, 0, 1);
			std::vector<std::vector<point>> points_mat = m_nurbs.creat_points(values1, values2);

			index_u = 0;
			value_u = 0;
			m_shape = 0;
			m_count = 0;
			for (auto& points_vec : points_mat)
			{
				index_v = 0;
				value_v = 0;
				if (m_nurbs.nurbs_s() != xhqm::npos)
				{
					value_u = values1[index_u++];
				}
				for (auto& point : points_vec)
				{
					if (m_nurbs.nurbs_t() != xhqm::npos)
					{
						value_v = values2[index_v++];
					}
					attribs.push_back(point.x());
					attribs.push_back(point.y());
					attribs.push_back(point.z());
					attribs.push_back(value_u);
					attribs.push_back(value_v);
				}
			}

			uint count_1 = static_cast<uint>(points_mat.size());
			uint count_2 = static_cast<uint>(points_mat.empty() ? 0 : points_mat[0].size());
			if (m_nurbs.nurbs_s() == xhqm::npos && m_nurbs.nurbs_t() == xhqm::npos)
			{
				for (uint i = 0; i < count_1; ++i)
				{
					for (uint j = 0; j < count_2; ++j)
					{
						m_shape = 1;
						xhqm::topol<> topol{ i * count_2 + j };
						for (auto& index : topol)
						{
							++m_count;
							m_attrib.push_back(attribs[index * 5 + 0]);
							m_attrib.push_back(attribs[index * 5 + 1]);
							m_attrib.push_back(attribs[index * 5 + 2]);
							m_attrib.push_back(attribs[index * 5 + 3]);
							m_attrib.push_back(attribs[index * 5 + 4]);
						}
					}
				}
			}
			else if (m_nurbs.nurbs_s() != xhqm::npos && m_nurbs.nurbs_t() == xhqm::npos)
			{
				for (uint i = 1; i < count_1; ++i)
				{
					for (uint j = 0; j < count_2; ++j)
					{
						m_shape = 2;
						xhqm::topol<> topol{ (i - 1) * count_2 + j, (i - 0) * count_2 + j };
						for (auto& index : topol)
						{
							++m_count;
							m_attrib.push_back(attribs[index * 5 + 0]);
							m_attrib.push_back(attribs[index * 5 + 1]);
							m_attrib.push_back(attribs[index * 5 + 2]);
							m_attrib.push_back(attribs[index * 5 + 3]);
							m_attrib.push_back(attribs[index * 5 + 4]);
						}
					}
				}
			}
			else if (m_nurbs.nurbs_s() == xhqm::npos && m_nurbs.nurbs_t() != xhqm::npos)
			{
				for (uint i = 0; i < count_1; ++i)
				{
					for (uint j = 1; j < count_2; ++j)
					{
						m_shape = 2;
						xhqm::topol<> topol{ i * count_2 + (j - 1), i * count_2 + (j - 0) };
						for (auto& index : topol)
						{
							++m_count;
							m_attrib.push_back(attribs[index * 5 + 0]);
							m_attrib.push_back(attribs[index * 5 + 1]);
							m_attrib.push_back(attribs[index * 5 + 2]);
							m_attrib.push_back(attribs[index * 5 + 3]);
							m_attrib.push_back(attribs[index * 5 + 4]);
						}
					}
				}
			}
			else if (m_nurbs.nurbs_s() != xhqm::npos && m_nurbs.nurbs_t() != xhqm::npos)
			{
				for (uint i = 1; i < count_1; ++i)
				{
					for (uint j = 1; j < count_2; ++j)
					{
						m_shape = 4;
						xhqm::topol<> topol{
							((i - 1) * count_2 + (j - 1)),
							((i - 1) * count_2 + (j - 0)),
							((i - 0) * count_2 + (j - 1)),
							((i - 1) * count_2 + (j - 0)),
							((i - 0) * count_2 + (j - 0)),
							((i - 0) * count_2 + (j - 1))
						};
						for (auto& index : topol)
						{
							++m_count;
							m_attrib.push_back(attribs[index * 5 + 0]);
							m_attrib.push_back(attribs[index * 5 + 1]);
							m_attrib.push_back(attribs[index * 5 + 2]);
							m_attrib.push_back(attribs[index * 5 + 3]);
							m_attrib.push_back(attribs[index * 5 + 4]);
						}
					}
				}
			}

			std::vector<fantasy::uint> locations{ 3, 2 };
			_vertex_id.reset(m_attrib, locations);
			drawer::_aabb_(5);
			m_attrib.clear();
		}
		void nurbs::render()
		{
			if (!_shader_id || !_vertex_id || !m_system.data()) return;

			render_backend& rhi = render_backend_active();
			fantasy::uint program = static_cast<fantasy::uint>(_shader_id);
			rhi.use_program(program);
			{
				rhi.uniform_matrix(program, "tranMatrix", m_system.tramat().data(), 1);
				std::array<xhqm::real, 4> sel = { 1,1,1,1 };
				if (_is_select) { sel = xhqm::color::rgba2array(_select); }
				rhi.uniform_vec4(program, "modelSelect", sel[0], sel[1], sel[2], sel[3]);
			}
			rhi.point_size(_poit_size);
			rhi.line_size(_line_size);
			rhi.bind_texture(0, static_cast<uint>(m_texture));

			if (m_shape == 1) { rhi.draw(_vertex_id, rhi_primitive::points, 0, static_cast<uint>(m_count)); }
			else if (m_shape == 2) { rhi.draw(_vertex_id, rhi_primitive::lines, 0, static_cast<uint>(m_count)); }
			else if (m_shape == 4) { rhi.draw(_vertex_id, rhi_primitive::triangles, 0, static_cast<uint>(m_count)); }

			rhi.bind_texture(0, 0);
			rhi.use_program(0);
		}
		void nurbs::_render_id_()
		{
			render_backend& rhi = render_backend_active();
			if (m_shape == 1) { rhi.draw(_vertex_id, rhi_primitive::points, 0, static_cast<uint>(m_count)); }
			else if (m_shape == 2) { rhi.draw(_vertex_id, rhi_primitive::lines, 0, static_cast<uint>(m_count)); }
			else if (m_shape == 4) { rhi.draw(_vertex_id, rhi_primitive::triangles, 0, static_cast<uint>(m_count)); }
		}
	}

	namespace fantasy
	{
		void arrow::updata()
		{
			geometry::vect vecx(0, 0, 1);
			geometry::vect pose = m_arrow;
			geometry::vect axis = geometry::vect::mul(vecx, pose);
			geometry::grid grid = arrow::make(m_first, m_r1, m_r2);

			xhqm::matx<fantasy::value> matrix = xhqm::scale(geometry::value(1), geometry::value(1), pose.length());
			if (axis.length() > geometry::value(0.1))
			{
				geometry::value cons_v = geometry::vect::angle(vecx, pose);
				matrix = xhqm::rotate_v(axis, cons_v) * matrix;
			}
			for (auto& poit : grid.poits())
			{
				poit = poit * matrix;
			}

			show_grid(grid);

			fantasy::ploys::updata();
		}

		geometry::grid arrow::make(fantasy::value first, fantasy::value r1, fantasy::value r2)
		{
			first = std::max(first, fantasy::value(0.1));
			first = std::min(first, fantasy::value(0.9));
			geometry::frustum frustum_1(xhqm::poit<fantasy::value>(0, 0, 0), xhqm::poit<fantasy::value>(0, 0, first), xhqm::vect<fantasy::value>(0, 0, 1));
			geometry::frustum frustum_2(xhqm::poit<fantasy::value>(0, 0, first), xhqm::poit<fantasy::value>(0, 0, 1), xhqm::vect<fantasy::value>(0, 0, 1));
			frustum_1.count(11); frustum_1.radius_D(r1); frustum_1.radius_T(r1); frustum_2.count(11); frustum_2.radius_D(r2); frustum_2.radius_T(0);
			return frustum_1.frustum_grid() + frustum_2.frustum_grid();
		}
	}
}
