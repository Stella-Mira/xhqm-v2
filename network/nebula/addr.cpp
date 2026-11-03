#include "addr.h"

namespace xhqm
{
	namespace nebula
	{
		nebula::addr addr_from(const nebula::json& j)
		{
			nebula::addr a = nullptr;
			if (j.contains("kind"))
			{
				switch (text_kind(j.value("kind", "")))
				{
				case nebula::kind::uuid: a = std::make_shared<addr_uuid>(nebula::uuid::to_uuid(j.value("uuid", ""))); break;
				case nebula::kind::ipv4: a = std::make_shared<addr_ipv4>(j.value("host", ""), j.value("port", 0), j.value("broad", 0)); break;
				case nebula::kind::ipv6: a = std::make_shared<addr_ipv6>(j.value("host", ""), j.value("port", 0), j.value("broad", 0)); break;
				// 串口：port 语义 = 波特率（默认 s_baud）；只带 "baud" 的载荷也认
				case nebula::kind::serial: a = std::make_shared<addr_serial>(j.value("host", ""),
					j.contains("port") ? j.value("port", addr_serial::s_baud) : j.value("baud", addr_serial::s_baud)); break;
				default: a = nullptr;
				}

				if (a)
				{
					nebula::uuid target;
					nebula::text uuid = j.value("uuid", "");
					if (bool(target = uuid)) { a->uuid(target); }
					a->name(j.value("name", nebula::text{}));
					a->info(j.value("info", nebula::json{}));
				}
			}
			return a;
		}
		nebula::json from_addr(const std::vector<addr>& list)
		{
			nebula::json arr = nebula::json::array();
			for (const auto& a : list)
			{
				if (a) { arr.push_back(a->make_json()); }
			}
			return arr;
		}
	}
}
