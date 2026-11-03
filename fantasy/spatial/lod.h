#pragma once
#ifndef XHQM_FANTASY_LOD
#define XHQM_FANTASY_LOD

// LOD（距离分级）：按相机距离选择对象的细节级别
#include "fantasy.h"
#include "blurry.h"

namespace xhqm
{
	namespace fantasy
	{
		// cross-fade 混合结果（M7）：from=当前级别，to=下一级别，weight=to 的权重[0,1]
		struct lod_blend_result
		{
			object* from = nullptr;
			object* to = nullptr;
			value weight = 0;
		};

		// 一个对象的多个 LOD 级别
		class lod_group
		{
			struct level { value distance = 0; object* obj = nullptr; };
			std::vector<level> m_levels;
		public:
			lod_group() = default;

			// 添加 LOD 级别：distance 为切换到该级别的最小距离（升序），obj 为该级别的对象
			void add_level(value distance, object* obj);
			void clear() { m_levels.clear(); }
			size_t level_count() const { return m_levels.size(); }

			// 根据相机距离选择 LOD 级别对象；无级别返回 nullptr
			object* select(value distance) const;

			// cross-fade：在切换点前 fade_range 距离内返回 from/to 两级别与权重，避免硬切跳变
			lod_blend_result select_blend(value distance, value fade_range) const;
		};
	}
}

#endif
