#include "blurry.h"

namespace xhqm
{
	namespace fantasy
	{
		void object::alive(fantasy::object* object_ptr)
		{
			if (!object_ptr) return;
			if (object_ptr->m_used)
			{
				for (auto* child_ptr : (*object_ptr)) //广度遍历
				{
					child_ptr->life();
				}
			}
		}
	}
}
