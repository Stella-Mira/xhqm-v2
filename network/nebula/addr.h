#ifndef XHQM_NEBULA_ADDR
#define XHQM_NEBULA_ADDR

#include "./addr/addr_b.h"
#include "./addr/addr_uuid.h"
#include "./addr/addr_ipv4.h"
#include "./addr/addr_serial.h"
#include "./addr/addr_ipv6.h"

namespace xhqm
{
	namespace nebula
	{
		using addr = std::shared_ptr<addr_b>;// 地址 = 具体 addr 的多态句柄

		// xhqm_io：动态构建（XHQM_DYNAMIC）时导出这两个自由函数——消费方不定义 XHQM_DYNAMIC，
		// 宏展开为空，于是他们按普通外部符号从导入库解析（与类上的 xhqm_io 同一套机制）。
		xhqm_io nebula::addr addr_from(const nebula::json& j);
		xhqm_io nebula::json from_addr(const std::vector<addr>& list);

		struct addr_less
		{
			inline bool operator()(const addr& left, const addr& right) const
			{
				return (nebula::uuid_less()(left->uuid(), right->uuid()));
			}
		};
	}
}

#endif
