#include "addr_b.h"

namespace xhqm
{
	namespace nebula
	{
		size hardware_allow()
		{
			return xhqm::size(std::max(uint(1), std::thread::hardware_concurrency()));
		}

		text kind_text(const kind& k)
		{
			switch (k)
			{
			case kind::uuid:       return "uuid";
			case kind::ipv4:       return "ipv4";
			case kind::ipv6:       return "ipv6";
			case kind::serial:     return "serial";
			case kind::bluetooth:  return "bluetooth";
			case kind::nearlink:   return "nearlink";
			case kind::quantum:    return "quantum";
			default:            return "no";
			}
		}

		kind text_kind(const text& s)
		{
			if (s == "uuid")       return kind::uuid;
			if (s == "ipv4")       return kind::ipv4;
			if (s == "ipv6")       return kind::ipv6;
			if (s == "serial")     return kind::serial;
			if (s == "bluetooth")  return kind::bluetooth;
			if (s == "nearlink")   return kind::nearlink;
			if (s == "quantum")    return kind::quantum;
			return kind::no;
		}

		nebula::json addr_b::make_json() const
		{
			nebula::json json = node::make_json();
			json["kind"] = nebula::kind_text(m_type);
			return json;
		}
	}
}
