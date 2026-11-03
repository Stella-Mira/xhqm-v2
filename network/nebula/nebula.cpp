#include "nebula.h"

namespace xhqm
{
	namespace nebula
	{
		std::string version()
		{
			return "nebula 1.0";
		}
	}

	namespace nebula
	{
		nebula::json node::make_json() const
		{
			xhqm::nebula::json json;
			json["uuid"] = m_uuid.to_text();
			json["name"] = name();
			json["info"] = info();
			return json;
		}
	}
}
