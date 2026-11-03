#pragma once
#ifndef XHQM_FANTASY_SPATIAL
#define XHQM_FANTASY_SPATIAL

// 空间分区（BVH 包围盒层次）：加速射线拾取与视锥剔除
#include "fantasy.h"
#include "blurry.h"
#include "drawer.h"

namespace xhqm
{
	namespace fantasy
	{
		class spatial_bvh
		{
			struct node;
			node* m_root = nullptr;
			std::vector<node*> m_nodes;   // 所有节点（clear 时统一释放）
		public:
			spatial_bvh() = default;
			~spatial_bvh() { clear(); }

			// 用 (包围盒, 对象) 列表构建 BVH
			void build(const std::vector<std::pair<aabb, object*>>& items);
			void clear();

			// 射线拾取：返回与射线相交的所有对象
			void query_ray(const point& origin, const vector& dir, std::vector<object*>& out) const;
			// 视锥剔除：view_proj = proj * view * model，返回视锥内对象
			void query_frustum(const matrix& view_proj, std::vector<object*>& out) const;

			size_t node_count() const { return m_nodes.size(); }
		};
	}
}

#endif
