#pragma once
#ifndef XHQM_FANTASY_CULLER
#define XHQM_FANTASY_CULLER

// 场景剔除器：从场景构建 BVH，视锥剔除 / 遮挡剔除
#include "fantasy.h"
#include "blurry.h"
#include "system.h"
#include "spatial.h"

namespace xhqm
{
	namespace fantasy
	{
		class scene_culler
		{
			spatial_bvh m_bvh;
			std::vector<object*> m_all;
		public:
			scene_culler() = default;

			// 从场景根节点收集所有 models 的世界空间 AABB，构建 BVH
			void build(object* root);

			// 视锥剔除：view_proj = proj * view，返回视锥内（含相交）的 object
			std::vector<object*> cull_frustum(const matrix& view_proj) const;

			size_t node_count() const { return m_bvh.node_count(); }
			size_t object_count() const { return m_all.size(); }
		};
	}
}

#endif
