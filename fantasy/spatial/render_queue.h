#pragma once
#ifndef XHQM_FANTASY_RENDERQUEUE
#define XHQM_FANTASY_RENDERQUEUE

// 渲染队列：不透明对象 front-to-back（减少 overdraw），透明对象 back-to-front（正确混合）
#include "fantasy.h"
#include "blurry.h"

namespace xhqm
{
	namespace fantasy
	{
		class render_queue
		{
			struct item { object* obj = nullptr; value depth = 0; bool transparent = false; };
			std::vector<item> m_items;
		public:
			render_queue() = default;

			void clear() { m_items.clear(); }

			// 添加对象；depth 为到相机距离（越大越远），transparent 是否透明
			void add(object* obj, value depth, bool transparent = false);

			// 排序并返回渲染顺序：不透明（近→远）在前，透明（远→近）在后
			std::vector<object*> order() const;
		};
	}
}

#endif
