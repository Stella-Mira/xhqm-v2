#ifndef XHQM_NEBULA
#define XHQM_NEBULA

#include "../../head.h"
#include "../../network/uuid.h"
#include "../../algebra/itype.h"
#include "../../software/system/timesys.h"
#include "../../software/system/filesys.h"
#include "../../software/system/threads.h"
#include "../../software/system/process.h"

#include <queue>
#include <deque>
#include <vector>
#include <memory>
#include <string>
#include <chrono>

namespace xhqm
{
	namespace nebula
	{
		std::string version();
	}
}

namespace xhqm
{
	namespace nebula
	{
		// 基础类型别名
		using sint = signed int;
		using uint = unsigned int;
		using byte = unsigned char;
		using size = std::size_t;
		using text = std::string;
		using json = nlohmann::json;
		using uuid = xhqm::cipher::uuid;
		using uuid_less = xhqm::cipher::uuid_less;    // 比较器

		class xhqm_io node
		{
			xhqm::nebula::text m_name; //节点名称
			xhqm::nebula::json m_info; //相关信息
			xhqm::nebula::uuid m_uuid; //真正地址
		public:
			node(nebula::uuid uuid = {})
				: m_uuid{ uuid }
			{
				if (m_uuid == nebula::uuid{})
				{
					m_uuid = uuid::creat_uuid_v4();
				}
			}

			inline void uuid(const nebula::uuid& value) { m_uuid = value; }
			inline void name(const nebula::text& value) { m_name = value; }
			inline void info(const nebula::json& value) { m_info = value; }

			inline const nebula::uuid& uuid() const { return m_uuid; }
			inline const nebula::text& name() const { return m_name; }
			inline const nebula::json& info() const { return m_info; }
			virtual nebula::json make_json() const; // 序列化
			virtual ~node() = default;		// 虚析构
		};
	};
}

#endif
