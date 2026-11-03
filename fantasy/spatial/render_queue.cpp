#include "render_queue.h"
#include <algorithm>

namespace xhqm
{
	namespace fantasy
	{
		void render_queue::add(object* obj, value depth, bool transparent)
		{
			if (!obj) return;
			item it;
			it.obj = obj;
			it.depth = depth;
			it.transparent = transparent;
			m_items.push_back(it);
		}

		std::vector<object*> render_queue::order() const
		{
			std::vector<item> sorted = m_items;
			std::sort(sorted.begin(), sorted.end(), [](const item& a, const item& b)
			{
				if (a.transparent != b.transparent)
				{
					// 不透明在前，透明在后
					return !a.transparent && b.transparent;
				}
				if (a.transparent)
				{
					// 透明：远→近（back-to-front）
					return a.depth > b.depth;
				}
				// 不透明：近→远（front-to-back）
				return a.depth < b.depth;
			});

			std::vector<object*> out;
			out.reserve(sorted.size());
			for (const item& it : sorted) out.push_back(it.obj);
			return out;
		}
	}
}
