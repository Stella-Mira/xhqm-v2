#ifndef XHQM_THING
#define XHQM_THING

// thing：认知记忆的实体层（astral 之上的扩展）。node 分为两类：
//
//   events —— info = "event:<事件名>"；links 含「父事件」（有向图，子→父，value="parent"）；
//             data = 事件流（时间序列），流里可含子事件引用 {type:"child", info, uuid}
//             （子事件引用留在 data 流而非 links，价值在于「时间」：它标记子事件在父事件
//               时间轴上的发生时刻）。events 无 provenance。
//
//   object —— info = "object:<物品名>"；links = 关联物品（双向图）；
//             data = 属性变化流，每帧是「某次变化后的完整属性快照 + 溯源 prov」：
//               { time, form, json: { prov, name, color, ... } }
//             读取 = 取最后一帧（最新属性）。
//
// thing::kernel 继承 astral::kernel，在其上提供两类 node 的语义操作。

#include "kernel.h"

#include <vector>

namespace xhqm
{
	namespace astral
	{
		// object 的最新属性视图（= 最后一帧属性快照）。
		struct snapshot
		{
			astral::uuid   id;
			astral::text   name;      // 物品名（从 info 提取）
			astral::json   attrs;     // 最新属性（扁平 json 对象）
			astral::json to_json() const;
		};

		// thing 内核：继承 astral::kernel，提供 events / object 两类 node 的语义操作。
		class thing : public astral::kernel
		{
			astral::text m_form = "thing";
		public:
			// ================= events（有向图 + 事件流，无 provenance） =================
			astral::uuid event_create(const astral::text& name, const astral::json& content,
				const astral::uuid& parent = astral::uuid{});
			bool event_append(const astral::uuid& id, const astral::json& content);
			// 产生子事件：在父流里记 {info,uuid} 引用，并建子节点、设父边
			astral::uuid event_spawn(const astral::uuid& parent, const astral::text& child_name,
				const astral::json& content);
			bool event_set_parent(const astral::uuid& child, const astral::uuid& parent);
			astral::uuid event_parent(const astral::uuid& id) const;
			std::vector<astral::uuid> event_children(const astral::uuid& id) const;

			// ================= object（双向图 + 属性快照流） =================
			// attrs：初始属性对象（扁平 json，如 {name, color, ...}）
			astral::uuid object_create(const astral::text& name, const astral::json& attrs = astral::json::object());
			// 追加一帧属性快照：prov 描述这次变化（如 "事件{uuid}里将 name 改为桌子"），attrs 为完整最新属性
			bool object_set(const astral::uuid& id, const astral::json& prov, const astral::json& attrs);
			bool object_link(const astral::uuid& a, const astral::uuid& b);   // 双向关联
			bool object_get(const astral::uuid& id, snapshot& out) const;     // 取最后一帧

			// ================= 合并（身份消解） =================
			bool merge(const astral::uuid& a, const astral::uuid& b,
				const astral::text& reason, double confidence, astral::uuid& primary_out);

			// ================= 检索 =================
			std::vector<snapshot> query_object(const astral::text& word);
			std::vector<astral::uuid> query_event(const astral::text& word);
		};
	}
}

#endif
