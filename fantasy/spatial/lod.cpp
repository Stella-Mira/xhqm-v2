#include "lod.h"

namespace xhqm
{
	namespace fantasy
	{
		void lod_group::add_level(value distance, object* obj)
		{
			if (!obj) return;
			level lv;
			lv.distance = distance;
			lv.obj = obj;
			m_levels.push_back(lv);
		}

		object* lod_group::select(value distance) const
		{
			if (m_levels.empty()) return nullptr;
			object* result = m_levels.front().obj;
			for (const level& lv : m_levels)
			{
				if (distance >= lv.distance) result = lv.obj;
				else break;
			}
			return result;
		}

		lod_blend_result lod_group::select_blend(value distance, value fade_range) const
		{
			lod_blend_result r;
			if (m_levels.empty()) return r;

			object* lower = m_levels.front().obj;
			object* upper = lower;
			value upper_d = m_levels.front().distance;
			for (const level& lv : m_levels)
			{
				if (distance >= lv.distance) { lower = lv.obj; }
				else { upper = lv.obj; upper_d = lv.distance; break; }   // 过渡锚点为 upper 层切换距离
			}
			r.from = lower;
			r.to = upper;

			// cross-fade：距离进入 [切换点 - fade_range, 切换点] 时，to 权重 0→1
			if (upper != lower && fade_range > 0)
			{
				value w = (distance - (upper_d - fade_range)) / fade_range;
				r.weight = w < 0 ? 0 : (w > 1 ? 1 : w);
			}
			return r;
		}
	}
}
