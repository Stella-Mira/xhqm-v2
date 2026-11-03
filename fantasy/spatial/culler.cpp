#include "culler.h"

namespace xhqm
{
	namespace fantasy
	{
		void scene_culler::build(object* root)
		{
			m_all.clear();
			if (!root) return;

			std::vector<std::pair<aabb, object*>> items;
			for (auto* obj : (*root))   // 广度遍历
			{
				models* m = dynamic_cast<models*>(obj);
				if (!m) continue;
				aabb local = m->updata_aabb();
				if (!local.good()) continue;
				// 世界空间 AABB
				matrix world_mat = system::transform(root, m);
				aabb world = local.access(world_mat);
				items.push_back({ world, m });
				m_all.push_back(m);
			}
			m_bvh.build(items);
		}

		std::vector<object*> scene_culler::cull_frustum(const matrix& view_proj) const
		{
			std::vector<object*> out;
			m_bvh.query_frustum(view_proj, out);
			return out;
		}
	}
}
