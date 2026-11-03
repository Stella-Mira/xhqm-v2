#include "spatial.h"
#include <algorithm>

namespace xhqm
{
	namespace fantasy
	{
		struct spatial_bvh::node
		{
			aabb box;
			std::vector<node*> children;
			std::vector<object*> objects;
			bool leaf = true;
		};

		// 射线与 AABB 相交（slab 法），返回是否命中
		static bool ray_aabb(const point& origin, const vector& dir, const aabb& box)
		{
			value tmin = -std::numeric_limits<value>::infinity();
			value tmax = +std::numeric_limits<value>::infinity();
			value o[3] = { origin.x(), origin.y(), origin.z() };
			value d[3] = { dir.x(), dir.y(), dir.z() };
			value mn[3] = { box.min.x(), box.min.y(), box.min.z() };
			value mx[3] = { box.max.x(), box.max.y(), box.max.z() };
			for (int i = 0; i < 3; ++i)
			{
				if (std::abs(d[i]) < 1e-8f)
				{
					if (o[i] < mn[i] || o[i] > mx[i]) return false;
				}
				else
				{
					value t1 = (mn[i] - o[i]) / d[i];
					value t2 = (mx[i] - o[i]) / d[i];
					if (t1 > t2) std::swap(t1, t2);
					tmin = std::max(tmin, t1);
					tmax = std::min(tmax, t2);
				}
			}
			return tmax >= tmin && tmax >= 0;
		}

		// 从 view_proj 提取 6 个视锥平面（列向量约定）
		static void extract_planes(const matrix& m, value planes[6][4])
		{
			// 行 r0..r3
			for (int i = 0; i < 6; ++i) for (int k = 0; k < 4; ++k) planes[i][k] = 0;
			auto row = [&](int r, int c) { return m(r, c); };
			// left = row3 + row0
			for (int c = 0; c < 4; ++c) planes[0][c] = row(3, c) + row(0, c);
			// right = row3 - row0
			for (int c = 0; c < 4; ++c) planes[1][c] = row(3, c) - row(0, c);
			// bottom = row3 + row1
			for (int c = 0; c < 4; ++c) planes[2][c] = row(3, c) + row(1, c);
			// top = row3 - row1
			for (int c = 0; c < 4; ++c) planes[3][c] = row(3, c) - row(1, c);
			// near = row3 + row2
			for (int c = 0; c < 4; ++c) planes[4][c] = row(3, c) + row(2, c);
			// far = row3 - row2
			for (int c = 0; c < 4; ++c) planes[5][c] = row(3, c) - row(2, c);
		}

		// AABB 与视锥（6 平面）相交测试：任一点在平面正半空间即相交（保守）
		static bool frustum_aabb(const value planes[6][4], const aabb& box)
		{
			value mn[3] = { box.min.x(), box.min.y(), box.min.z() };
			value mx[3] = { box.max.x(), box.max.y(), box.max.z() };
			for (int p = 0; p < 6; ++p)
			{
				bool inside = false;
				for (int c = 0; c < 8 && !inside; ++c)
				{
					value px = (c & 1) ? mx[0] : mn[0];
					value py = (c & 2) ? mx[1] : mn[1];
					value pz = (c & 4) ? mx[2] : mn[2];
					value dist = planes[p][0] * px + planes[p][1] * py + planes[p][2] * pz + planes[p][3];
					if (dist >= 0) inside = true;
				}
				if (!inside) return false;   // 所有顶点都在该平面负半空间 => 整体在视锥外
			}
			return true;
		}

		void spatial_bvh::build(const std::vector<std::pair<aabb, object*>>& items)
		{
			clear();
			if (items.empty()) return;

			struct build_ctx { spatial_bvh* self; };
			// 递归构建
			std::function<node*(const std::vector<std::pair<aabb, object*>>&)> rec =
				[&](const std::vector<std::pair<aabb, object*>>& list) -> node*
			{
				node* n = new node();
				// 合并包围盒
				n->box = list[0].first;
				for (size_t i = 1; i < list.size(); ++i) n->box = n->box.attach(list[i].first);

				if (list.size() <= 4)
				{
					// 叶子
					n->leaf = true;
					for (auto& it : list) n->objects.push_back(it.second);
					m_nodes.push_back(n);
					return n;
				}

				// 选最长轴
				value ex = n->box.max.x() - n->box.min.x();
				value ey = n->box.max.y() - n->box.min.y();
				value ez = n->box.max.z() - n->box.min.z();
				int axis = (ex >= ey && ex >= ez) ? 0 : (ey >= ez ? 1 : 2);

				std::vector<std::pair<aabb, object*>> l = list;
				auto center = [&](const std::pair<aabb, object*>& it) -> value
				{
					value c = 0;
					if (axis == 0) c = (it.first.min.x() + it.first.max.x()) * 0.5f;
					else if (axis == 1) c = (it.first.min.y() + it.first.max.y()) * 0.5f;
					else c = (it.first.min.z() + it.first.max.z()) * 0.5f;
					return c;
				};
				std::sort(l.begin(), l.end(), [&](const auto& a, const auto& b) { return center(a) < center(b); });

				size_t mid = l.size() / 2;
				std::vector<std::pair<aabb, object*>> left(l.begin(), l.begin() + mid);
				std::vector<std::pair<aabb, object*>> right(l.begin() + mid, l.end());

				n->leaf = false;
				n->children.push_back(rec(left));
				n->children.push_back(rec(right));
				m_nodes.push_back(n);
				return n;
			};

			m_root = rec(items);
		}

		void spatial_bvh::clear()
		{
			for (node* n : m_nodes) delete n;
			m_nodes.clear();
			m_root = nullptr;
		}

		void spatial_bvh::query_ray(const point& origin, const vector& dir, std::vector<object*>& out) const
		{
			if (!m_root) return;
			std::function<void(node*)> rec = [&](node* n)
			{
				if (!n || !ray_aabb(origin, dir, n->box)) return;
				if (n->leaf)
				{
					for (object* o : n->objects) out.push_back(o);
				}
				else
				{
					for (node* c : n->children) rec(c);
				}
			};
			rec(m_root);
		}

		void spatial_bvh::query_frustum(const matrix& view_proj, std::vector<object*>& out) const
		{
			if (!m_root) return;
			value planes[6][4];
			extract_planes(view_proj, planes);
			std::function<void(node*)> rec = [&](node* n)
			{
				if (!n || !frustum_aabb(planes, n->box)) return;
				if (n->leaf)
				{
					for (object* o : n->objects) out.push_back(o);
				}
				else
				{
					for (node* c : n->children) rec(c);
				}
			};
			rec(m_root);
		}
	}
}
