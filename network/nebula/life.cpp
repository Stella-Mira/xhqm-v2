#include "life.h"

namespace xhqm
{
	namespace nebula
	{
		text life::exec_event(const json& event)
		{
			text type = event.value("type", "");
			//if (type == "") { type = ""; }
			return type;
		}

		void life::life_event(const json& event)
		{
			if (!event.is_null() && event.contains("type"))
			{
				m_life_thread.submit<void>(0, [this, event]()
				{
					this->exec_event(event); //虚函数
				});
			}
		}
	}
}
