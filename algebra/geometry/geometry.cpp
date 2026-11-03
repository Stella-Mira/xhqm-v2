#include "geometry.h"
#include <algorithm>
#include <random>

namespace xhqm
{
	namespace geometry
	{
		grid operator+(const grid& a, const grid& b)
		{
			geometry::grid grid_res;
			grid_res.m_poits.insert(grid_res.m_poits.end(), a.m_poits.begin(), a.m_poits.end());
			grid_res.m_poits.insert(grid_res.m_poits.end(), b.m_poits.begin(), b.m_poits.end());
			grid_res.m_faces.insert(grid_res.m_faces.end(), a.m_faces.begin(), a.m_faces.end());
			grid_res.m_faces.insert(grid_res.m_faces.end(), b.m_faces.begin(), b.m_faces.end());
			for (xhqm::size index = a.m_faces.size(), count = grid_res.m_faces.size(); index < count; ++index)
			{
				for (auto& pos : grid_res.m_faces[index])
				{
					pos += a.m_poits.size();
				}
			}
			return grid_res;
		}

		mesh operator+(const mesh& a, const mesh& b)
		{
			geometry::mesh mesh_res;
			mesh_res.m_poits.insert(mesh_res.m_poits.end(), a.m_poits.begin(), a.m_poits.end());
			mesh_res.m_poits.insert(mesh_res.m_poits.end(), b.m_poits.begin(), b.m_poits.end());
			mesh_res.m_faces.insert(mesh_res.m_faces.end(), a.m_faces.begin(), a.m_faces.end());
			mesh_res.m_faces.insert(mesh_res.m_faces.end(), b.m_faces.begin(), b.m_faces.end());
			mesh_res.m_cubes.insert(mesh_res.m_cubes.end(), a.m_cubes.begin(), a.m_cubes.end());
			mesh_res.m_cubes.insert(mesh_res.m_cubes.end(), b.m_cubes.begin(), b.m_cubes.end());
			for (xhqm::size index = a.m_faces.size(), count = mesh_res.m_faces.size(); index < count; ++index)
			{
				for (auto& pos : mesh_res.m_faces[index])
				{
					pos += a.m_poits.size();
				}
			}
			for (xhqm::size index = a.m_cubes.size(), count = mesh_res.m_cubes.size(); index < count; ++index)
			{
				for (auto& pos : mesh_res.m_cubes[index])
				{
					pos += a.m_faces.size();
				}
			}
			return mesh_res;
		}


		geometry::grid circle::circle_grid(const geometry::count& count) const
		{
			geometry::grid circle;

			circle.poits().push_back(m_poit);
			auto points = this->circle_points(count);
			circle.poits().insert(circle.poits().end(), points.begin(), points.end());
			for (xhqm::index index = 1; index < count; ++index)
			{
				circle.faces().push_back({ 0, index, index + 1 });
			}
			circle.faces().push_back({ 0, count, 1 });

			return circle;
		}

		geometry::poit circle::circle_point(const geometry::value& angle) const
		{
			geometry::vect vecx(0, 0, 1);
			geometry::vect pose = m_pose;
			geometry::vect axis = geometry::vect::mul(vecx, pose);
			if (axis.length() > geometry::value(0.1))
			{
				geometry::value cos_v = geometry::vect::dot(vecx, pose) / (vecx.length() * pose.length());
				cos_v = std::min(geometry::value(1), std::max(geometry::value(-1), cos_v));
				geometry::poit point(m_radius * std::cos(angle), m_radius * std::sin(angle), 0);
				return (point * xhqm::rotate_v(axis, std::acos(cos_v))) + m_poit;
			}
			geometry::poit point(m_radius * std::cos(angle), m_radius * std::sin(angle), 0);
			if (geometry::vect::dot(vecx, pose) < geometry::value(0))
				point = point * xhqm::rotate_x(geometry::value(xhqm::pi));
			return point + m_poit;
		}

		std::vector<geometry::poit> circle::circle_points(const geometry::count& count) const
		{
			geometry::vect vecx(0, 0, 1);
			geometry::vect pose = m_pose;
			std::vector<geometry::poit> points(count);
			geometry::vect axis = geometry::vect::mul(vecx, pose);
			if (axis.length() > geometry::value(0.1))
			{
				geometry::value cos_v = geometry::vect::dot(vecx, pose) / (vecx.length() * pose.length());
				cos_v = std::min(geometry::value(1), std::max(geometry::value(-1), cos_v));
				const geometry::value theta = std::acos(cos_v);

				const geometry::value Vc = static_cast<geometry::value>(2 * xhqm::pi / count);
				for (xhqm::index i = 0; i < count; ++i)
				{
					geometry::value angle = Vc * i;
					geometry::poit point(m_radius * std::cos(angle), m_radius * std::sin(angle), 0);
					points[i] = (point * xhqm::rotate_v(axis, theta)) + m_poit;
				}
				return points;
			}
			const geometry::value Vc = static_cast<geometry::value>(2 * xhqm::pi / count);
			for (xhqm::index i = 0; i < count; ++i)
			{
				geometry::value angle = Vc * i;
				geometry::poit point(m_radius * std::cos(angle), m_radius * std::sin(angle), 0);
				if (geometry::vect::dot(vecx, pose) < geometry::value(0))
					point = point * xhqm::rotate_x(geometry::value(xhqm::pi));
				points[i] = point + m_poit;
			}
			return points;
		}


		geometry::grid sphere::sphere_grid(geometry::value& resolution) const
		{
			geometry::grid sphere_grid;
			if (resolution < 0) resolution = m_radius / 10;
			std::vector<geometry::face> triangles = geometry::create_sphere(m_radius, resolution);
			std::unordered_map<geometry::poit, std::size_t, geometry::poit_hash> TriVertexIndexMap; // 全局顶点到索引的映射，用于去重

			for (geometry::face& triangle : triangles)
			{
				if (TriVertexIndexMap.find(triangle.point_A()) == TriVertexIndexMap.end())// 如果点未被记录
				{
					sphere_grid.poits().push_back(triangle.point_A()); TriVertexIndexMap[triangle.point_A()] = sphere_grid.poits().size() - 1;
				}
				if (TriVertexIndexMap.find(triangle.point_B()) == TriVertexIndexMap.end())// 如果点未被记录
				{
					sphere_grid.poits().push_back(triangle.point_B()); TriVertexIndexMap[triangle.point_B()] = sphere_grid.poits().size() - 1;
				}
				if (TriVertexIndexMap.find(triangle.point_C()) == TriVertexIndexMap.end())// 如果点未被记录
				{
					sphere_grid.poits().push_back(triangle.point_C()); TriVertexIndexMap[triangle.point_C()] = sphere_grid.poits().size() - 1;
				}
				sphere_grid.faces().push_back(geometry::topol{ TriVertexIndexMap[triangle.point_A()], TriVertexIndexMap[triangle.point_B()], TriVertexIndexMap[triangle.point_C()] });
			}
			for (geometry::poit& poit : sphere_grid.poits())
			{
				poit = poit + m_poit;
			}
			return sphere_grid;
		}

		geometry::poit sphere::sphere_point(const geometry::vect& pose) const
		{
			return m_poit + pose.normal() * m_radius;
		}


		geometry::grid frustum::frustum_grid() const
		{
			geometry::grid sphere_grid;

			geometry::circle RoundT; RoundT.radius(m_radiusT); RoundT.poit(m_centerT); RoundT.pose(m_vectorA);
			geometry::circle RoundD; RoundD.radius(m_radiusD); RoundD.poit(m_centerD); RoundD.pose(m_vectorA);
			std::vector<geometry::poit> RoundPointsT = RoundT.circle_points(m_count);
			std::vector<geometry::poit> RoundPointsD = RoundD.circle_points(m_count);

			sphere_grid.poits().push_back(m_centerT);
			sphere_grid.poits().insert(sphere_grid.poits().end(), RoundPointsT.begin(), RoundPointsT.end());
			sphere_grid.poits().push_back(m_centerD);
			sphere_grid.poits().insert(sphere_grid.poits().end(), RoundPointsD.begin(), RoundPointsD.end());

			xhqm::index IndexT = 0;
			xhqm::index IndexD = m_count + 1;
			for (xhqm::index Index = 1; Index < m_count; ++Index)
			{
				sphere_grid.faces().push_back({ Index + IndexT + 1, Index + IndexT, IndexT });
				sphere_grid.faces().push_back({ Index + IndexT, Index + IndexT + 1, Index + IndexD + 1, Index + IndexD });
				sphere_grid.faces().push_back({ IndexD, Index + IndexD, Index + IndexD + 1 });
			}
			sphere_grid.faces().push_back({ IndexT + 1, IndexT + m_count, IndexT });
			sphere_grid.faces().push_back({ IndexT + m_count, IndexT + 1, IndexD + 1, IndexD + m_count });
			sphere_grid.faces().push_back({ IndexD, IndexD + m_count, IndexD + 1 });

			return sphere_grid;
		}


		index nurbs::find_span(const values& knots, count p, value u)
		{
			if (p == xhqm::npos) return 0;
			count n = knots.size() - p - 1;
			if (u >= knots[n]) return n - 1;
			if (u <= knots[p]) return p;
			count low = p;
			count high = n;
			count mid = (low + high) / 2;
			while (u < knots[mid] || u >= knots[mid + 1]) {
				if (u < knots[mid])
					high = mid;
				else
					low = mid;
				mid = (low + high) / 2;
			}
			return mid;
		}
		values nurbs::base_funs(const values& knots, count p, value u, count span)
		{
			if (p == xhqm::npos) return {};
			const value preci_v = xhqm::preci<value>;
			std::vector<value> left(p + 1);
			std::vector<value> right(p + 1);
			std::vector<value> N(p + 1, 0);

			N[0] = 1;
			for (index j = 1; j <= p; ++j)
			{
				left[j] = u - knots[span + 1 - j];
				right[j] = knots[span + j] - u;
				value saved = 0;
				for (index r = 0; r < j; ++r)
				{
					value denom = right[r + 1] + left[j - r];
					//value temp = N[r] / (denom > preci_v ? denom : preci_v);
					value temp = (denom == 0 ? 0 : N[r] / denom);
					N[r] = saved + right[r + 1] * temp;
					saved = left[j - r] * temp;
				}
				N[j] = saved;
			}
			return N;
		}
		values nurbs::linspace(const count& size, const value& star, const value& stop)
		{
			values values(size);
			value step = (stop - star) / (size - 1);
			for (index i = 0; i != size; ++i)
				values[i] = star + step * i;
			values.back() = stop;
			return values;
		}

		geometry::poit curve::creat_point(const geometry::real& v) const
		{
			geometry::poit S;

			// 转换参数到节点范围
			value real = v * (m_nurbs_d.back() - m_nurbs_d.front()) + m_nurbs_d.front();
			// 查找参数跨度
			count span = nurbs::find_span(m_nurbs_d, m_nurbs_k, real);
			// 计算基函数
			auto N = nurbs::base_funs(m_nurbs_d, m_nurbs_k, real, span);
			// 计算曲面点 (双线性加权和)
			value W = 0;
			count start = span - m_nurbs_k;
			count size_p = m_nurbs_p.size();
			if (size_p != 0)
			{
				for (count i = 0; i <= m_nurbs_k; ++i)
				{
					count idx = start + i;
					if (idx < size_p)
					{
						value weight = N[i] * m_nurbs_w[idx];
						S.x() += weight * m_nurbs_p[idx].x();
						S.y() += weight * m_nurbs_p[idx].y();
						S.z() += weight * m_nurbs_p[idx].z();
						W += weight;
					}
				}
			}

			// 归一化处理
			if (W != 0)
			{
				S.x() /= W;
				S.y() /= W;
				S.z() /= W;
			}
			return S;
		}

		std::vector<geometry::poit> curve::creat_points(const std::vector<value>& v) const
		{
			count size_p = m_nurbs_p.size();
			std::vector<geometry::poit> points;
			if (m_nurbs_k == xhqm::npos)
			{
				points = m_nurbs_p;
			}
			else if (m_nurbs_k != xhqm::npos)
			{
				points.resize(v.size());
				for (size_t i = 0; i < v.size(); ++i)
				{
					points[i] = creat_point(v[i]);
				}
			}
			return points;
		}

		geometry::curve surface::index_curve(const index& ind, bool is_h) const
		{
			geometry::curve curve;
			if (is_h)
			{
				curve.nurbs_k() = m_nurbs_k[0];
				std::vector<poit> nurbs_p;
				std::vector<real> nurbs_w;
				nurbs_p = m_nurbs_p[ind];
				nurbs_w = m_nurbs_w[ind];
				curve.nurbs_v(m_nurbs_d[0]);
				curve.nurbs_p(nurbs_p);
				curve.nurbs_w(nurbs_w);
			}
			else
			{
				curve.nurbs_k() = m_nurbs_k[1];
				std::vector<poit> nurbs_p;
				std::vector<real> nurbs_w;
				for (const auto& vector : m_nurbs_p)
				{
					nurbs_p.push_back(vector[ind]);
				}
				for (const auto& vector : m_nurbs_w)
				{
					nurbs_w.push_back(vector[ind]);
				}
				curve.nurbs_v(m_nurbs_d[1]);
				curve.nurbs_p(nurbs_p);
				curve.nurbs_w(nurbs_w);
			}
			return curve;
		}

		geometry::poit surface::creat_point(const geometry::real& v1, index i2) const
		{
			// 初始化累加变量
			geometry::poit S;
			// 转换参数到节点范围
			value u_real = v1 * (m_nurbs_d[0].back() - m_nurbs_d[0].front()) + m_nurbs_d[0].front();
			// 查找参数跨度
			count span_u = nurbs::find_span(m_nurbs_d[0], m_nurbs_k[0], u_real);
			// 计算基函数
			auto N_u = nurbs::base_funs(m_nurbs_d[0], m_nurbs_k[0], u_real, span_u);
			// 计算曲面点 (双线性加权和)
			count start_u = span_u - m_nurbs_k[0];

			value W = 0;
			count size_p_u = m_nurbs_p.size();
			count size_p_v = m_nurbs_p.empty() ? 0 : m_nurbs_p[0].size();
			if (size_p_u != 0 && size_p_v != 0)
			{
				for (index i = 0; i <= m_nurbs_k[0]; ++i) {
					index idx = start_u + i;
					if (idx < size_p_u && i2 < size_p_v) {
						value weight = N_u[i] * m_nurbs_w[idx][i2];
						S.x() += weight * m_nurbs_p[idx][i2].x();
						S.y() += weight * m_nurbs_p[idx][i2].y();
						S.z() += weight * m_nurbs_p[idx][i2].z();
						W += weight;
					}
				}
			}

			// 归一化处理
			if (W != 0)
			{
				S.x() /= W;
				S.y() /= W;
				S.z() /= W;
			}
			return S;
		}

		geometry::poit surface::creat_point(index i1, const geometry::real& v2) const
		{
			// 初始化累加变量
			geometry::poit S;
			// 转换参数到节点范围
			value v_real = v2 * (m_nurbs_d[1].back() - m_nurbs_d[1].front()) + m_nurbs_d[1].front();
			// 查找参数跨度
			count span_v = nurbs::find_span(m_nurbs_d[1], m_nurbs_k[1], v_real);
			// 计算基函数
			auto N_v = nurbs::base_funs(m_nurbs_d[1], m_nurbs_k[1], v_real, span_v);
			// 计算曲面点 (双线性加权和)
			count start_v = span_v - m_nurbs_k[1];

			value W = 0;
			count size_p_u = m_nurbs_p.size();
			count size_p_v = m_nurbs_p.empty() ? 0 : m_nurbs_p[0].size();
			if (size_p_u != 0 && size_p_v != 0)
			{
				for (index j = 0; j <= m_nurbs_k[1]; ++j) {
					index jdx = start_v + j;
					if (i1 < size_p_u && jdx < size_p_v) {
						value weight = N_v[j] * m_nurbs_w[i1][jdx];
						S.x() += weight * m_nurbs_p[i1][jdx].x();
						S.y() += weight * m_nurbs_p[i1][jdx].y();
						S.z() += weight * m_nurbs_p[i1][jdx].z();
						W += weight;
					}
				}
			}

			// 归一化处理
			if (W != 0)
			{
				S.x() /= W;
				S.y() /= W;
				S.z() /= W;
			}
			return S;
		}

		geometry::poit surface::creat_point(const geometry::real& v1, const geometry::real& v2) const
		{
			geometry::poit S;

			// 转换参数到节点范围
			value u_real = v1 * (m_nurbs_d[0].back() - m_nurbs_d[0].front()) + m_nurbs_d[0].front();
			value v_real = v2 * (m_nurbs_d[1].back() - m_nurbs_d[1].front()) + m_nurbs_d[1].front();
			// 查找参数跨度
			count span_u = nurbs::find_span(m_nurbs_d[0], m_nurbs_k[0], u_real);
			count span_v = nurbs::find_span(m_nurbs_d[1], m_nurbs_k[1], v_real);
			// 计算基函数
			auto N_u = nurbs::base_funs(m_nurbs_d[0], m_nurbs_k[0], u_real, span_u);
			auto N_v = nurbs::base_funs(m_nurbs_d[1], m_nurbs_k[1], v_real, span_v);
			// 计算曲面点 (双线性加权和)
			count start_u = span_u - m_nurbs_k[0];
			count start_v = span_v - m_nurbs_k[1];

			count size_p_u = m_nurbs_p.size();
			count size_p_v = m_nurbs_p.empty() ? 0 : m_nurbs_p[0].size();

			value W = 0;
			if (size_p_u != 0 && size_p_v != 0)
			{
				for (count i = 0; i <= m_nurbs_k[0]; ++i) {
					for (count j = 0; j <= m_nurbs_k[1]; ++j) {
						count idx_u = start_u + i;
						count jdx_v = start_v + j;

						// 检查索引边界
						if (idx_u < size_p_u && jdx_v < size_p_v) {
							value weight = N_u[i] * N_v[j] * m_nurbs_w[idx_u][jdx_v];
							S.x() += weight * m_nurbs_p[idx_u][jdx_v].x();
							S.y() += weight * m_nurbs_p[idx_u][jdx_v].y();
							S.z() += weight * m_nurbs_p[idx_u][jdx_v].z();
							W += weight;
						}
					}
				}
			}

			// 归一化处理
			if (W != 0)
			{
				S.x() /= W;
				S.y() /= W;
				S.z() /= W;
			}
			return S;
		}

		std::vector<std::vector<geometry::poit>> surface::creat_points(const std::vector<value>& v1, const std::vector<value>& v2) const
		{
			count size_p_u = m_nurbs_p.size();
			count size_p_v = m_nurbs_p.empty() ? 0 : m_nurbs_p[0].size();

			std::vector<std::vector<geometry::poit>> points;
			if (m_nurbs_k[0] == xhqm::npos && m_nurbs_k[1] == xhqm::npos)
			{
				points = m_nurbs_p;
			}
			else if (m_nurbs_k[0] != xhqm::npos && m_nurbs_k[1] == xhqm::npos)
			{
				points.resize(v1.size(), std::vector<geometry::poit>(size_p_v));
				for (size_t i = 0; i < v1.size(); ++i)
				{
					for (size_t j = 0; j < size_p_v; ++j)
					{
						points[i][j] = creat_point(v1[i], j);
					}
				}
			}
			else if (m_nurbs_k[0] == xhqm::npos && m_nurbs_k[1] != xhqm::npos)
			{
				points.resize(size_p_u, std::vector<geometry::poit>(v2.size()));
				for (size_t i = 0; i < size_p_u; ++i)
				{
					for (size_t j = 0; j < v2.size(); ++j)
					{
						points[i][j] = creat_point(i, v2[j]);
					}
				}
			}
			else if (m_nurbs_k[0] != xhqm::npos && m_nurbs_k[1] != xhqm::npos)
			{
				points.resize(v1.size(), std::vector<geometry::poit>(v2.size()));
				for (size_t i = 0; i < v1.size(); ++i)
				{
					for (size_t j = 0; j < v2.size(); ++j)
					{
						points[i][j] = creat_point(v1[i], v2[j]);
					}
				}
			}

			return points;
		}


		std::vector<geometry::face> create_sphere(geometry::value Radius, geometry::value resolution)
		{
			// 防护：resolution ≤ 0 时细分条件"边长 > resolution"恒成立，会死循环
			if (resolution <= geometry::value(0))
				resolution = std::max(geometry::value(0.001), Radius / 10);
			std::vector<geometry::face> SphereTriangle;
			std::queue<geometry::face> Triangles;

			Triangles.push({ geometry::poit{ Radius, 0, 0 }, geometry::poit{ 0, Radius, 0 }, geometry::poit{ 0, 0, Radius } });
			while (!Triangles.empty()) {
				geometry::face& triangle = Triangles.front();

				// 当三角形各边长度都不大于resolution时就不再进一步细分
				if ((triangle.point_A() - triangle.point_B()).length() > resolution ||
					(triangle.point_B() - triangle.point_C()).length() > resolution ||
					(triangle.point_C() - triangle.point_A()).length() > resolution)
				{
					geometry::vect vA = (triangle.point_A() + triangle.point_B());
					geometry::vect vB = (triangle.point_B() + triangle.point_C());
					geometry::vect vC = (triangle.point_C() + triangle.point_A());
					geometry::poit A(vA.normal() * Radius);
					geometry::poit B(vB.normal() * Radius);
					geometry::poit C(vC.normal() * Radius);
					Triangles.push({ triangle.point_A() ,C, A });
					Triangles.push({ triangle.point_B(), A, B });
					Triangles.push({ triangle.point_C(), B, C });
					Triangles.push({ A, B, C });
				}
				else
				{
					// 第一象限
					SphereTriangle.push_back({ triangle.point_A(), triangle.point_B(), triangle.point_C() });
					// 第二象限
					triangle.point_A().x() = -triangle.point_A().x();
					triangle.point_B().x() = -triangle.point_B().x();
					triangle.point_C().x() = -triangle.point_C().x();
					SphereTriangle.push_back({ triangle.point_A(), triangle.point_B(), triangle.point_C() });
					// 第三象限
					triangle.point_A().y() = -triangle.point_A().y();
					triangle.point_B().y() = -triangle.point_B().y();
					triangle.point_C().y() = -triangle.point_C().y();
					SphereTriangle.push_back({ triangle.point_A(), triangle.point_B(), triangle.point_C() });
					// 第四象限
					triangle.point_A().x() = -triangle.point_A().x();
					triangle.point_B().x() = -triangle.point_B().x();
					triangle.point_C().x() = -triangle.point_C().x();
					SphereTriangle.push_back({ triangle.point_A(), triangle.point_B(), triangle.point_C() });
					// 第五象限
					triangle.point_A().z() = -triangle.point_A().z();
					triangle.point_B().z() = -triangle.point_B().z();
					triangle.point_C().z() = -triangle.point_C().z();
					SphereTriangle.push_back({ triangle.point_A(), triangle.point_B(), triangle.point_C() });
					// 第六象限
					triangle.point_A().x() = -triangle.point_A().x();
					triangle.point_B().x() = -triangle.point_B().x();
					triangle.point_C().x() = -triangle.point_C().x();
					SphereTriangle.push_back({ triangle.point_A(), triangle.point_B(), triangle.point_C() });
					// 第七象限
					triangle.point_A().y() = -triangle.point_A().y();
					triangle.point_B().y() = -triangle.point_B().y();
					triangle.point_C().y() = -triangle.point_C().y();
					SphereTriangle.push_back({ triangle.point_A(), triangle.point_B(), triangle.point_C() });
					// 第八象限
					triangle.point_A().x() = -triangle.point_A().x();
					triangle.point_B().x() = -triangle.point_B().x();
					triangle.point_C().x() = -triangle.point_C().x();
					SphereTriangle.push_back({ triangle.point_A(), triangle.point_B(), triangle.point_C() });
				}
				Triangles.pop();
			}
			return SphereTriangle;
		}

		xhqm::geometry::surface create_sphere(geometry::value Radius)
		{
			return xhqm::geometry::create_sphere_1(Radius);
		}

		xhqm::geometry::surface create_sphere_1(geometry::value radius)
		{
			geometry::surface octant;
			const geometry::count num_s = 3;
			const geometry::count num_t = 3;
			const geometry::count num_u = 50;  // 纬度控制点数
			const geometry::count num_v = 50;  // 经度控制点数
			const geometry::value pi = static_cast<geometry::value>(xhqm::pi);

			// 曲面阶数
			octant.nurbs_s() = num_s;
			octant.nurbs_t() = num_t;

			// 节点向量
			auto values_u = geometry::nurbs::linspace(num_u - num_s + 1, 0, 1);
			auto values_v = geometry::nurbs::linspace(num_v - num_t + 1, 0, 1);
			octant.nurbs_u().insert(octant.nurbs_u().end(), num_s, 0.0);
			octant.nurbs_u().insert(octant.nurbs_u().end(), values_u.begin(), values_u.end());
			octant.nurbs_u().insert(octant.nurbs_u().end(), num_s, 1.0);
			octant.nurbs_v().insert(octant.nurbs_v().end(), num_t, 0.0);
			octant.nurbs_v().insert(octant.nurbs_v().end(), values_v.begin(), values_v.end());
			octant.nurbs_v().insert(octant.nurbs_v().end(), num_t, 1.0);

			// 控制点网格和权重
			octant.nurbs_p().resize(num_u, std::vector<geometry::poit>(num_v));
			octant.nurbs_w().resize(num_u, std::vector<geometry::real>(num_v));
			std::vector<geometry::value> values_lat = geometry::nurbs::linspace(num_u, -pi / 2, +pi / 2);
			std::vector<geometry::value> values_lon = geometry::nurbs::linspace(num_v, -pi, +pi);
			for (xhqm::index i = 0; i < num_u; ++i)
			{
				for (xhqm::index j = 0; j < num_v; ++j)
				{
					//geometry::value x = radius * std::sin(values_lat[i]) * std::cos(values_lon[j]);
					//geometry::value y = radius * std::sin(values_lat[i]) * std::sin(values_lon[j]);
					//geometry::value z = radius * std::cos(values_lat[i]);
					geometry::value x = radius * std::cos(values_lat[i]) * std::sin(values_lon[j]);
					geometry::value y = radius * std::cos(values_lat[i]) * std::cos(values_lon[j]);
					geometry::value z = radius * std::sin(values_lat[i]);
					octant.nurbs_p()[i][j] = geometry::poit(x, y, z);
					octant.nurbs_w()[i][j] = 1;
				}
			}

			return octant;
		}

		xhqm::geometry::surface create_sphere_2(geometry::value radius)
		{
			geometry::surface octant;
			const geometry::count num_s = 3;
			const geometry::count num_t = 3;
			const geometry::count num_u = 11;  // 纬度控制点数
			const geometry::count num_v = 11;  // 经度控制点数
			const geometry::value pi = static_cast<geometry::value>(xhqm::pi);

			// 曲面阶数
			octant.nurbs_s() = num_s;
			octant.nurbs_t() = num_t;

			// 节点向量
			auto values_u = geometry::nurbs::linspace(num_u - num_s + 1, 0, 1);
			auto values_v = geometry::nurbs::linspace(num_v - num_t + 1, 0, 1);
			octant.nurbs_u().insert(octant.nurbs_u().end(), num_s, 0.0);
			octant.nurbs_u().insert(octant.nurbs_u().end(), values_u.begin(), values_u.end());
			octant.nurbs_u().insert(octant.nurbs_u().end(), num_s, 1.0);
			octant.nurbs_v().insert(octant.nurbs_v().end(), num_t, 0.0);
			octant.nurbs_v().insert(octant.nurbs_v().end(), values_v.begin(), values_v.end());
			octant.nurbs_v().insert(octant.nurbs_v().end(), num_t, 1.0);

			// 控制点网格和权重
			octant.nurbs_p().resize(num_u, std::vector<geometry::poit>(num_v));
			octant.nurbs_w().resize(num_u, std::vector<geometry::real>(num_v));
			std::vector<geometry::value> values_lat = geometry::nurbs::linspace(num_u, -pi / 2, pi / 2);
			std::vector<geometry::value> values_lon = geometry::nurbs::linspace(num_v, 0, pi);
			for (xhqm::index i = 0; i < num_u; ++i)
			{
				for (xhqm::index j = 0; j < num_v; ++j)
				{
					geometry::value x = radius * std::sin(values_lat[i]) * std::cos(values_lon[j]);
					geometry::value y = radius * std::sin(values_lat[i]) * std::sin(values_lon[j]);
					geometry::value z = radius * std::cos(values_lat[i]);
					octant.nurbs_p()[i][j] = geometry::poit(x, y, z);
					octant.nurbs_w()[i][j] = 1;
				}
			}

			return octant;
		}

		xhqm::geometry::surface create_sphere_4(geometry::value radius)
		{
			geometry::surface octant;
			const geometry::count num_s = 3;
			const geometry::count num_t = 3;
			const geometry::count num_u = 11;  // 纬度控制点数
			const geometry::count num_v = 11;  // 经度控制点数
			const geometry::value pi = static_cast<geometry::value>(xhqm::pi);

			// 曲面阶数
			octant.nurbs_s() = num_s;
			octant.nurbs_t() = num_t;

			// 节点向量
			auto values_u = geometry::nurbs::linspace(num_u - num_s + 1, 0, 1);
			auto values_v = geometry::nurbs::linspace(num_v - num_t + 1, 0, 1);
			octant.nurbs_u().insert(octant.nurbs_u().end(), num_s, 0.0);
			octant.nurbs_u().insert(octant.nurbs_u().end(), values_u.begin(), values_u.end());
			octant.nurbs_u().insert(octant.nurbs_u().end(), num_s, 1.0);
			octant.nurbs_v().insert(octant.nurbs_v().end(), num_t, 0.0);
			octant.nurbs_v().insert(octant.nurbs_v().end(), values_v.begin(), values_v.end());
			octant.nurbs_v().insert(octant.nurbs_v().end(), num_t, 1.0);

			// 控制点网格和权重
			octant.nurbs_p().resize(num_u, std::vector<geometry::poit>(num_v));
			octant.nurbs_w().resize(num_u, std::vector<geometry::real>(num_v));
			std::vector<geometry::value> values_lat = geometry::nurbs::linspace(num_u, 0, pi / 2);
			std::vector<geometry::value> values_lon = geometry::nurbs::linspace(num_v, 0, pi);
			for (xhqm::index i = 0; i < num_u; ++i)
			{
				for (xhqm::index j = 0; j < num_v; ++j)
				{
					geometry::value x = radius * std::sin(values_lat[i]) * std::cos(values_lon[j]);
					geometry::value y = radius * std::sin(values_lat[i]) * std::sin(values_lon[j]);
					geometry::value z = radius * std::cos(values_lat[i]);
					octant.nurbs_p()[i][j] = geometry::poit(x, y, z);
					octant.nurbs_w()[i][j] = 1;
				}
			}

			return octant;
		}

		xhqm::geometry::surface create_sphere_8(geometry::value radius)
		{
			geometry::surface octant;
			const geometry::count num_s = 3;
			const geometry::count num_t = 3;
			const geometry::count num_u = 11;  // 纬度控制点数
			const geometry::count num_v = 11;  // 经度控制点数
			const geometry::value pi = static_cast<geometry::value>(xhqm::pi);

			// 曲面阶数
			octant.nurbs_s() = num_s;
			octant.nurbs_t() = num_t;

			// 节点向量
			auto values_u = geometry::nurbs::linspace(num_u - num_s + 1, 0, 1);
			auto values_v = geometry::nurbs::linspace(num_v - num_t + 1, 0, 1);
			octant.nurbs_u().insert(octant.nurbs_u().end(), num_s, 0.0);
			octant.nurbs_u().insert(octant.nurbs_u().end(), values_u.begin(), values_u.end());
			octant.nurbs_u().insert(octant.nurbs_u().end(), num_s, 1.0);
			octant.nurbs_v().insert(octant.nurbs_v().end(), num_t, 0.0);
			octant.nurbs_v().insert(octant.nurbs_v().end(), values_v.begin(), values_v.end());
			octant.nurbs_v().insert(octant.nurbs_v().end(), num_t, 1.0);

			// 控制点网格和权重
			octant.nurbs_p().resize(num_u, std::vector<geometry::poit>(num_v));
			octant.nurbs_w().resize(num_u, std::vector<geometry::real>(num_v));
			std::vector<geometry::value> values_lat = geometry::nurbs::linspace(num_u, 0, pi / 2);
			std::vector<geometry::value> values_lon = geometry::nurbs::linspace(num_v, 0, pi / 2);
			for (xhqm::index i = 0; i < num_u; ++i)
			{
				for (xhqm::index j = 0; j < num_v; ++j)
				{
					geometry::value x = radius * std::sin(values_lat[i]) * std::cos(values_lon[j]);
					geometry::value y = radius * std::sin(values_lat[i]) * std::sin(values_lon[j]);
					geometry::value z = radius * std::cos(values_lat[i]);
					octant.nurbs_p()[i][j] = geometry::poit(x, y, z);
					octant.nurbs_w()[i][j] = 1;
				}
			}

			return octant;
		}

		namespace hyper //超几何
		{
			xhqm::index dimension(const point& p)
			{
				return static_cast<xhqm::index>(p.size());
			}

			object hypercube(xhqm::count dim, value half)
			{
				object obj;
				if (dim == 0) return obj;
				const xhqm::count nverts = xhqm::count(1) << dim;
				obj.first.resize(nverts);
				for (xhqm::count i = 0; i < nverts; ++i)
				{
					obj.first[i].resize(dim);
					for (xhqm::count k = 0; k < dim; ++k)
						obj.first[i][k] = ((i >> k) & 1) ? half : -half;
				}
				obj.second.resize(1);
				obj.second[0].resize(2 * dim);
				for (xhqm::count axis = 0; axis < dim; ++axis)
				{
					for (xhqm::count side = 0; side < 2; ++side)
					{
						topol& face = obj.second[0][axis * 2 + side];
						for (xhqm::count i = 0; i < nverts; ++i)
							if (((i >> axis) & 1) == side)
								face.push_back(i);
					}
				}
				return obj;
			}

			object simplex(xhqm::count dim)
			{
				object obj;
				obj.first.resize(dim + 1);
				for (xhqm::count i = 0; i <= dim; ++i)
				{
					obj.first[i].assign(dim, value(0));
					if (i > 0) obj.first[i][i - 1] = value(1);
				}
				topol all;
				all.reserve(dim + 1);
				for (xhqm::count i = 0; i <= dim; ++i) all.push_back(i);
				obj.second.resize(1);
				obj.second[0].push_back(all);
				return obj;
			}

			points hypersphere(xhqm::count dim, xhqm::count samples, value radius)
			{
				points pts(samples);
				if (dim == 0) return pts;
				static std::mt19937 gen(5489u);
				std::normal_distribution<double> dist(0.0, 1.0);
				for (xhqm::count s = 0; s < samples; ++s)
				{
					point p(dim);
					double norm2 = 0.0;
					for (xhqm::count k = 0; k < dim; ++k)
					{
						const double v = dist(gen);
						p[k] = static_cast<value>(v);
						norm2 += v * v;
					}
					const double inv = (norm2 > 0.0) ? (static_cast<double>(radius) / std::sqrt(norm2)) : 0.0;
					for (xhqm::count k = 0; k < dim; ++k)
						p[k] = static_cast<value>(static_cast<double>(p[k]) * inv);
					pts[s] = p;
				}
				return pts;
			}
		}

		//—— face 度量 ——
		geometry::vect face::normal(void) const
		{
			geometry::vect n = geometry::vect::mul(m_pB - m_pA, m_pC - m_pA);
			geometry::value len = n.length();
			if (len < xhqm::preci<geometry::value>)
				return geometry::vect(0, 0, 1);
			return n / len;
		}
		geometry::value face::area(void) const
		{
			return geometry::vect::mul(m_pB - m_pA, m_pC - m_pA).length() / geometry::value(2);
		}
		geometry::poit face::centroid(void) const
		{
			return (m_pA + m_pB + m_pC) / geometry::value(3);
		}

		//—— 图元网格化 ——
		geometry::grid plane::plane_grid(const geometry::value& half, const geometry::count& div) const
		{
			geometry::grid grd;
			geometry::vect n = m_norm.normal();
			geometry::vect ref = (std::fabs(n.x()) < 0.9f) ? geometry::vect(1, 0, 0) : geometry::vect(0, 1, 0);
			geometry::vect u = geometry::vect::mul(ref, n).normal();
			geometry::vect v = geometry::vect::mul(n, u).normal();
			const geometry::count nv = div + 1;
			grd.poits().resize(nv * nv);
			for (geometry::count i = 0; i < nv; ++i)
				for (geometry::count j = 0; j < nv; ++j)
				{
					geometry::value fi = geometry::value(i) / geometry::value(div);
					geometry::value fj = geometry::value(j) / geometry::value(div);
					geometry::value su = -half + geometry::value(2) * half * fi;
					geometry::value sv = -half + geometry::value(2) * half * fj;
					grd.poits()[i * nv + j] = m_poit + u * su + v * sv;
				}
			for (geometry::count i = 0; i < div; ++i)
				for (geometry::count j = 0; j < div; ++j)
				{
					geometry::index a = i * nv + j;
					geometry::index b = (i + 1) * nv + j;
					geometry::index c = (i + 1) * nv + j + 1;
					geometry::index d = i * nv + j + 1;
					grd.faces().push_back({ a, b, c });
					grd.faces().push_back({ a, c, d });
				}
			return grd;
		}

		geometry::grid box::box_grid(void) const
		{
			geometry::grid grd;
			geometry::poit mn = m_pMin, mx = m_pMax;
			grd.poits().resize(8);
			grd.poits()[0] = geometry::poit(mn.x(), mn.y(), mn.z());
			grd.poits()[1] = geometry::poit(mx.x(), mn.y(), mn.z());
			grd.poits()[2] = geometry::poit(mx.x(), mx.y(), mn.z());
			grd.poits()[3] = geometry::poit(mn.x(), mx.y(), mn.z());
			grd.poits()[4] = geometry::poit(mn.x(), mn.y(), mx.z());
			grd.poits()[5] = geometry::poit(mx.x(), mn.y(), mx.z());
			grd.poits()[6] = geometry::poit(mx.x(), mx.y(), mx.z());
			grd.poits()[7] = geometry::poit(mn.x(), mx.y(), mx.z());
			// 6 面 12 三角形
			grd.faces().push_back({ 0, 2, 1 });
			grd.faces().push_back({ 0, 3, 2 });
			grd.faces().push_back({ 4, 5, 6 });
			grd.faces().push_back({ 4, 6, 7 });
			grd.faces().push_back({ 0, 1, 5 });
			grd.faces().push_back({ 0, 5, 4 });
			grd.faces().push_back({ 2, 6, 7 });
			grd.faces().push_back({ 2, 7, 3 });
			grd.faces().push_back({ 0, 4, 7 });
			grd.faces().push_back({ 0, 7, 3 });
			grd.faces().push_back({ 1, 5, 6 });
			grd.faces().push_back({ 1, 6, 2 });
			return grd;
		}
		bool box::contain(const geometry::poit& p) const
		{
			return p.x() >= m_pMin.x() && p.x() <= m_pMax.x()
				&& p.y() >= m_pMin.y() && p.y() <= m_pMax.y()
				&& p.z() >= m_pMin.z() && p.z() <= m_pMax.z();
		}

		geometry::grid cylinder::cylinder_grid(void) const
		{
			geometry::grid grd;
			geometry::count n = m_count;
			geometry::value r = m_radius, h = m_height;
			grd.poits().resize(2 * n + 2);
			grd.poits()[0] = m_poit;
			grd.poits()[n + 1] = m_poit + geometry::vect(0, 0, h);
			const geometry::value Vc = static_cast<geometry::value>(2 * xhqm::pi / n);
			for (geometry::count i = 0; i < n; ++i)
			{
				geometry::value a = Vc * i;
				geometry::poit p(r * std::cos(a), r * std::sin(a), 0);
				grd.poits()[1 + i] = m_poit + p;
				grd.poits()[n + 2 + i] = m_poit + geometry::vect(0, 0, h) + p;
			}
			for (geometry::count i = 0; i < n; ++i)
			{
				geometry::index j = (i + 1) % n;
				grd.faces().push_back({ 0, 1 + i, 1 + j });
				grd.faces().push_back({ n + 1, n + 2 + j, n + 2 + i });
				grd.faces().push_back({ 1 + i, 1 + j, n + 2 + j });
				grd.faces().push_back({ 1 + i, n + 2 + j, n + 2 + i });
			}
			return grd;
		}

		geometry::grid cone::cone_grid(void) const
		{
			geometry::grid grd;
			geometry::count n = m_count;
			geometry::value r = m_radius, h = m_height;
			grd.poits().resize(n + 2);
			grd.poits()[0] = m_poit;
			grd.poits()[n + 1] = m_poit + geometry::vect(0, 0, h);
			const geometry::value Vc = static_cast<geometry::value>(2 * xhqm::pi / n);
			for (geometry::count i = 0; i < n; ++i)
			{
				geometry::value a = Vc * i;
				grd.poits()[1 + i] = m_poit + geometry::poit(r * std::cos(a), r * std::sin(a), 0);
			}
			for (geometry::count i = 0; i < n; ++i)
			{
				geometry::index j = (i + 1) % n;
				grd.faces().push_back({ 0, 1 + i, 1 + j });
				grd.faces().push_back({ 1 + i, 1 + j, n + 1 });
			}
			return grd;
		}

		geometry::grid torus::torus_grid(void) const
		{
			geometry::grid grd;
			geometry::count nu = m_countU, nv = m_countV;
			geometry::value R = m_radiusM, r = m_radiusm;
			grd.poits().resize(nu * nv);
			for (geometry::count u = 0; u < nu; ++u)
			{
				geometry::value th = static_cast<geometry::value>(2 * xhqm::pi * u / nu);
				for (geometry::count v = 0; v < nv; ++v)
				{
					geometry::value ph = static_cast<geometry::value>(2 * xhqm::pi * v / nv);
					geometry::poit p(
						(R + r * std::cos(ph)) * std::cos(th),
						(R + r * std::cos(ph)) * std::sin(th),
						r * std::sin(ph));
					grd.poits()[u * nv + v] = m_poit + p;
				}
			}
			for (geometry::count u = 0; u < nu; ++u)
			{
				geometry::count u1 = (u + 1) % nu;
				for (geometry::count v = 0; v < nv; ++v)
				{
					geometry::count v1 = (v + 1) % nv;
					geometry::index a = u * nv + v;
					geometry::index b = u1 * nv + v;
					geometry::index c = u1 * nv + v1;
					geometry::index d = u * nv + v1;
					grd.faces().push_back({ a, b, c });
					grd.faces().push_back({ a, c, d });
				}
			}
			return grd;
		}

		geometry::grid capsule::capsule_grid(void) const
		{
			geometry::grid grd;
			geometry::count n = m_count;
			geometry::value r = m_radius, h = m_height;
			geometry::value hz = h / 2;
			grd.poits().resize(2 * n + 2);
			grd.poits()[0] = m_poit + geometry::vect(0, 0, hz + r);
			grd.poits()[1] = m_poit + geometry::vect(0, 0, -hz - r);
			const geometry::value Vc = static_cast<geometry::value>(2 * xhqm::pi / n);
			for (geometry::count i = 0; i < n; ++i)
			{
				geometry::value a = Vc * i;
				geometry::poit p(r * std::cos(a), r * std::sin(a), 0);
				grd.poits()[2 + i] = m_poit + geometry::vect(0, 0, hz) + p;
				grd.poits()[n + 2 + i] = m_poit + geometry::vect(0, 0, -hz) + p;
			}
			for (geometry::count i = 0; i < n; ++i)
			{
				geometry::index j = (i + 1) % n;
				geometry::index ti = 2 + i, tj = 2 + j;
				geometry::index bi = n + 2 + i, bj = n + 2 + j;
				grd.faces().push_back({ 0, tj, ti });
				grd.faces().push_back({ 1, bi, bj });
				grd.faces().push_back({ ti, tj, bj });
				grd.faces().push_back({ ti, bj, bi });
			}
			return grd;
		}

		geometry::grid quad::quad_grid(void) const
		{
			geometry::grid grd;
			grd.poits().push_back(m_pA);
			grd.poits().push_back(m_pB);
			grd.poits().push_back(m_pC);
			grd.poits().push_back(m_pD);
			grd.faces().push_back({ 0, 1, 2 });
			grd.faces().push_back({ 0, 2, 3 });
			return grd;
		}

		//—— 求交与度量 ——
		bool ray_triangle(const geometry::poit& ro, const geometry::vect& rd,
			const geometry::poit& pA, const geometry::poit& pB, const geometry::poit& pC, geometry::value& t)
		{
			geometry::vect e1 = pB - pA;
			geometry::vect e2 = pC - pA;
			geometry::vect p = geometry::vect::mul(rd, e2);
			geometry::value det = geometry::vect::dot(e1, p);
			if (std::fabs(det) < xhqm::preci<geometry::value>)
				return false;
			geometry::value inv = 1 / det;
			geometry::vect s = ro - pA;
			geometry::value u = inv * geometry::vect::dot(s, p);
			if (u < 0 || u > 1) return false;
			geometry::vect q = geometry::vect::mul(s, e1);
			geometry::value v = inv * geometry::vect::dot(rd, q);
			if (v < 0 || u + v > 1) return false;
			t = inv * geometry::vect::dot(e2, q);
			return t >= 0;
		}

		bool ray_sphere(const geometry::poit& ro, const geometry::vect& rd,
			const geometry::poit& center, geometry::value radius, geometry::value& t)
		{
			geometry::vect oc = ro - center;
			geometry::value a = geometry::vect::dot(rd, rd);
			geometry::value b = geometry::value(2) * geometry::vect::dot(oc, rd);
			geometry::value c = geometry::vect::dot(oc, oc) - radius * radius;
			geometry::value disc = b * b - geometry::value(4) * a * c;
			if (disc < 0) return false;
			geometry::value sq = std::sqrt(disc);
			geometry::value t0 = (-b - sq) / (geometry::value(2) * a);
			geometry::value t1 = (-b + sq) / (geometry::value(2) * a);
			if (t0 < 0 && t1 < 0) return false;
			t = (t0 >= 0) ? t0 : t1;
			return true;
		}

		geometry::value point_plane_distance(const geometry::poit& p,
			const geometry::poit& plane_poit, const geometry::vect& plane_norm)
		{
			geometry::vect n = plane_norm.normal();
			return geometry::vect::dot(p - plane_poit, n);
		}

		geometry::value signed_volume(const geometry::poit& pA, const geometry::poit& pB,
			const geometry::poit& pC, const geometry::poit& pD)
		{
			geometry::vect ab = pB - pA;
			geometry::vect ac = pC - pA;
			geometry::vect ad = pD - pA;
			return geometry::vect::dot(ab, geometry::vect::mul(ac, ad)) / geometry::value(6);
		}

		//—— NURBS 导数 ——
		std::vector<geometry::values> nurbs::base_funs_deriv(const values& knots, count p, value u, count span, count order)
		{
			if (p == xhqm::npos) return {};
			if (order > p) order = p;
			std::vector<geometry::values> ndu(p + 1, geometry::values(p + 1, 0));
			std::vector<geometry::values> ders(order + 1, geometry::values(p + 1, 0));
			std::vector<value> left(p + 1, 0), right(p + 1, 0);

			ndu[0][0] = 1;
			for (count j = 1; j <= p; ++j)
			{
				left[j] = u - knots[span + 1 - j];
				right[j] = knots[span + j] - u;
				value saved = 0;
				for (count r = 0; r < j; ++r)
				{
					ndu[j][r] = right[r + 1] + left[j - r];
					value temp = ndu[r][j - 1] / ndu[j][r];
					ndu[r][j] = saved + right[r + 1] * temp;
					saved = left[j - r] * temp;
				}
				ndu[j][j] = saved;
			}
			for (count j = 0; j <= p; ++j)
				ders[0][j] = ndu[j][p];

			std::vector<geometry::values> a(2, geometry::values(p + 1, 0));
			for (count r = 0; r <= p; ++r)
			{
				count s1 = 0, s2 = 1;
				a[0][0] = 1;
				for (count k = 1; k <= order; ++k)
				{
					value d = 0;
					int rk = (int)r - (int)k;
					int pk = (int)p - (int)k;
					if (rk >= 0)
					{
						a[s2][0] = a[s1][0] / ndu[(count)pk + 1][(count)rk];
						d = a[s2][0] * ndu[(count)rk][(count)pk];
					}
					int j1 = (rk >= -1) ? 1 : -rk;
					int j2 = ((int)r - 1 <= pk) ? (int)k - 1 : (int)p - (int)r;
					for (int j = j1; j <= j2; ++j)
					{
						a[s2][j] = (a[s1][j] - a[s1][j - 1]) / ndu[(count)pk + 1][(count)(rk + j)];
						d += a[s2][j] * ndu[(count)(rk + j)][(count)pk];
					}
					if ((int)r <= pk)
					{
						a[s2][k] = -a[s1][k - 1] / ndu[(count)pk + 1][r];
						d += a[s2][k] * ndu[r][(count)pk];
					}
					ders[k][r] = d;
					count tmp = s1; s1 = s2; s2 = tmp;
				}
			}

			count rfac = p;
			for (count k = 1; k <= order; ++k)
			{
				for (count j = 0; j <= p; ++j)
					ders[k][j] *= (value)rfac;
				rfac *= (p - k);
			}
			return ders;
		}

		geometry::vect curve::creat_derivative(const geometry::real& v, count order) const
		{
			geometry::vect result;
			if (m_nurbs_k == xhqm::npos || m_nurbs_p.empty()) return result;
			if (order == 0) return (geometry::vect)creat_point(v);

			value real = v * (m_nurbs_d.back() - m_nurbs_d.front()) + m_nurbs_d.front();
			count span = nurbs::find_span(m_nurbs_d, m_nurbs_k, real);
			auto ders = nurbs::base_funs_deriv(m_nurbs_d, m_nurbs_k, real, span, order);

			count start = span - m_nurbs_k;
			count size_p = m_nurbs_p.size();

			std::vector<geometry::vect> A(order + 1);
			std::vector<value> W(order + 1, 0);
			for (count k = 0; k <= order; ++k)
				for (count i = 0; i <= m_nurbs_k; ++i)
				{
					count idx = start + i;
					if (idx < size_p)
					{
						value w = ders[k][i] * m_nurbs_w[idx];
						A[k].x() += w * m_nurbs_p[idx].x();
						A[k].y() += w * m_nurbs_p[idx].y();
						A[k].z() += w * m_nurbs_p[idx].z();
						W[k] += w;
					}
				}

			if (W[0] == 0) return result;

			auto binom = [](count n, count k) -> value
			{
				if (k > n) return value(0);
				if (k == 0 || k == n) return value(1);
				value r = 1;
				for (count i = 1; i <= k; ++i)
					r = r * value(n - k + i) / value(i);
				return r;
			};

			std::vector<geometry::vect> C(order + 1);
			C[0] = A[0] / W[0];
			for (count k = 1; k <= order; ++k)
			{
				geometry::vect sum = A[k];
				for (count j = 1; j <= k; ++j)
					sum = sum - (C[k - j] * W[j]) * binom(k, j);
				C[k] = sum / W[0];
			}
			return C[order];
		}

		geometry::vect surface::creat_derivative(const geometry::real& v1, const geometry::real& v2, bool is_u) const
		{
			geometry::vect result;
			count ku = m_nurbs_k[0], kv = m_nurbs_k[1];
			if (ku == xhqm::npos || kv == xhqm::npos) return result;
			if (m_nurbs_p.empty() || m_nurbs_p[0].empty()) return result;

			value u_real = v1 * (m_nurbs_d[0].back() - m_nurbs_d[0].front()) + m_nurbs_d[0].front();
			value v_real = v2 * (m_nurbs_d[1].back() - m_nurbs_d[1].front()) + m_nurbs_d[1].front();
			count span_u = nurbs::find_span(m_nurbs_d[0], ku, u_real);
			count span_v = nurbs::find_span(m_nurbs_d[1], kv, v_real);
			count start_u = span_u - ku;
			count start_v = span_v - kv;
			count size_u = m_nurbs_p.size();
			count size_v = m_nurbs_p[0].size();

			geometry::vect S;
			geometry::vect dA;
			value W = 0, dW = 0;

			if (is_u)
			{
				auto du = nurbs::base_funs_deriv(m_nurbs_d[0], ku, u_real, span_u, 1);
				auto Nv = nurbs::base_funs(m_nurbs_d[1], kv, v_real, span_v);
				for (count j = 0; j <= kv; ++j)
				{
					count idxv = start_v + j;
					if (idxv >= size_v) continue;
					for (count i = 0; i <= ku; ++i)
					{
						count idxu = start_u + i;
						if (idxu >= size_u) continue;
						value w = m_nurbs_w[idxu][idxv];
						value n0 = du[0][i] * Nv[j];
						value n1 = du[1][i] * Nv[j];
						S.x() += n0 * w * m_nurbs_p[idxu][idxv].x();
						S.y() += n0 * w * m_nurbs_p[idxu][idxv].y();
						S.z() += n0 * w * m_nurbs_p[idxu][idxv].z();
						W += n0 * w;
						dA.x() += n1 * w * m_nurbs_p[idxu][idxv].x();
						dA.y() += n1 * w * m_nurbs_p[idxu][idxv].y();
						dA.z() += n1 * w * m_nurbs_p[idxu][idxv].z();
						dW += n1 * w;
					}
				}
			}
			else
			{
				auto Nu = nurbs::base_funs(m_nurbs_d[0], ku, u_real, span_u);
				auto dv = nurbs::base_funs_deriv(m_nurbs_d[1], kv, v_real, span_v, 1);
				for (count j = 0; j <= kv; ++j)
				{
					count idxv = start_v + j;
					if (idxv >= size_v) continue;
					for (count i = 0; i <= ku; ++i)
					{
						count idxu = start_u + i;
						if (idxu >= size_u) continue;
						value w = m_nurbs_w[idxu][idxv];
						value n0 = Nu[i] * dv[0][j];
						value n1 = Nu[i] * dv[1][j];
						S.x() += n0 * w * m_nurbs_p[idxu][idxv].x();
						S.y() += n0 * w * m_nurbs_p[idxu][idxv].y();
						S.z() += n0 * w * m_nurbs_p[idxu][idxv].z();
						W += n0 * w;
						dA.x() += n1 * w * m_nurbs_p[idxu][idxv].x();
						dA.y() += n1 * w * m_nurbs_p[idxu][idxv].y();
						dA.z() += n1 * w * m_nurbs_p[idxu][idxv].z();
						dW += n1 * w;
					}
				}
			}

			if (W == 0) return result;
			return (dA - S * dW) / W;
		}

		geometry::vect surface::creat_normal(const geometry::real& v1, const geometry::real& v2) const
		{
			geometry::vect du = creat_derivative(v1, v2, true);
			geometry::vect dv = creat_derivative(v1, v2, false);
			geometry::vect n = geometry::vect::mul(du, dv);
			geometry::value len = n.length();
			if (len < xhqm::preci<geometry::value>)
				return geometry::vect(0, 0, 1);
			return n / len;
		}

		geometry::grid surface::surface_grid(count u_div, count v_div) const
		{
			geometry::grid grd;
			count nu = u_div + 1, nv = v_div + 1;
			grd.poits().resize(nu * nv);
			for (count i = 0; i < nu; ++i)
			{
				value u = (u_div == 0) ? value(0) : value(i) / value(u_div);
				for (count j = 0; j < nv; ++j)
				{
					value v = (v_div == 0) ? value(0) : value(j) / value(v_div);
					grd.poits()[i * nv + j] = creat_point(u, v);
				}
			}
			for (count i = 0; i < u_div; ++i)
				for (count j = 0; j < v_div; ++j)
				{
					index a = i * nv + j;
					index b = (i + 1) * nv + j;
					index c = (i + 1) * nv + j + 1;
					index d = i * nv + j + 1;
					grd.faces().push_back({ a, b, c });
					grd.faces().push_back({ a, c, d });
				}
			return grd;
		}
	}
}
