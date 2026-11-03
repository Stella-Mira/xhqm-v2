#ifndef XHQM_ASTRAL
#define XHQM_ASTRAL

#include "../../head.h"
#include "../../algebra/itype.h"
#include "../../algebra/ftype.h"
#include "../mmap.h"
#include "../uuid.h"
#include "../sha.h"

#include <shared_mutex>
#include <functional>
#include <map>
#include <set>


namespace xhqm
{
	namespace astral
	{
		// 基础类型别名
		using text = std::string;			// 字符串
		using json = nlohmann::json;		// json 数据
		using uuid = xhqm::cipher::uuid;	// 节点标识：复用 xhqm::cipher::uuid（16 字节）
		using uuid_less = xhqm::cipher::uuid_less;
		static constexpr xhqm::u64 version = 20260901;


		// 全零 uuid
		inline uuid uuid_none()
		{
			return cipher::uuid{};
		}

		// uuid 等价判断
		inline bool uuid_equal(const uuid& left, const uuid& right)
		{
			return cipher::uuid::uuid_equal(left, right);
		}

		// 一切皆节点：通用格式 {uuid, info, data[{time, form, json}, ...], links[{uuid,info}, ...]}
		//   uuid  —— 节点标识（未生成 = 全零，由 kernel 补 v4）
		//   info  —— 描述备注
		//   datas —— 真实数据
		//   links —— 关联节点
		struct node
		{
			xhqm::astral::uuid uuid = {};                        // 节点标识
			xhqm::astral::text info = {};                        // 描述备注
			xhqm::astral::json datas = json::array();             // 真实数据 [...]
			std::map<xhqm::astral::uuid, text, uuid_less> links; // 关联节点 {uuid → info}

			inline json datas_json() const
			{
				return datas.is_array() ? datas : astral::json::array();   // 损坏数据段防护：非数组按空数组输出
			}
			inline json links_uuid() const
			{
				astral::json link = astral::json::array();
				for (const auto& [link_uuid, link_info] : links)
				{
					link.push_back(astral::json{
						{"uuid", astral::uuid::to_text(link_uuid)},
						});
				}
				return link;
			}
			inline json links_info() const
			{
				astral::json link = astral::json::array();
				for (const auto& [link_uuid, link_info] : links)
				{
					link.push_back(astral::json{
						{"info", link_info},
						{"uuid", astral::uuid::to_text(link_uuid)},
						});
				}
				return link;
			}
			json to_json(bool full_data = true, bool with_link = true) const
			{
				return astral::json{
					{"uuid",  xhqm::cipher::uuid::to_text(uuid)},
					{"info",  info},
					{"datas", full_data ? datas_json() : astral::json::array()},
					{"links", with_link ? links_info() : astral::json::array()},
				};
			}
		};
	}
}

#endif
