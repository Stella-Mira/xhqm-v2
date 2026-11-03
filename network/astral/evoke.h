#ifndef ASTRAL_MEMORY_EVOKE
#define ASTRAL_MEMORY_EVOKE

#include "kernel.h"

namespace xhqm
{
	namespace astral
	{
		//data_node 仅连 link_node //link_node 仅连 data_node 
		using link_node = node; //{ uuid, info, ~, links[{uuid, info}] }
		using data_node = node; //{ uuid, info, data[{time, form, json}], links[{uuid, info}] }
		class xhqm_io evoke : public kernel
		{
			name_map m_name_uuid;			      // name → uuid （一对一）
		public:
			uuid create(const text& form, const text& info, const json& data, const std::vector<text>& links = {});
			uuid create(const text& form, const text& info, const json& data, const text& link);

			// upsert：按 info 精确查重（服务端原子）。
			// 命中 → 追加 data + 补 links，返回既有 uuid（upserted=true）；
			// 未命中 → 新建（upserted=false）。out_upserted 可为 nullptr。
			uuid upsert(const text& form, const text& info, const json& data,
				const std::vector<text>& links, bool* out_upserted = nullptr);

			// remove：id + time（time=0 删整个节点并级联清关联；time>0 删 time 之前的 data 时间点，节点保留）
			bool remove(const text& form, const uuid& id, xhqm::u64 time);

			// submit：id + links（关联标签）
			bool submit(const text& form, const uuid& id, const std::vector<text>& links);
			// submit：id + link（追加标签）
			bool submit(const text& form, const uuid& id, const text& link);
			// update：id + data（追加数据）
			bool submit(const text& form, const uuid& id, const json& data);
			// update：id + info（更新描述）
			bool update(const text& form, const uuid& id, const text& info);

			// select_link：link 模糊匹配（查询对冷节点自动加热，非 const）
			json select_links(const std::vector<text>& links);
			// select_word：word 模糊匹配
			json select_words(const std::vector<text>& words);
			// select_data：data 内容关键字模糊匹配（最新一条 data 的 JSON 序列化子串）
			json select_datas(const std::vector<text>& words);
			// select_node：uuid 精准匹配
			json select_nodes(const std::vector<uuid>& uuids);

			// 重建 name→uuid 表：从库中识别标签节点（被引用 + data 为空的节点），
			// 供 create 复用既有标签（服务重启后必须调用，否则标签重复创建）
			void rebuild_names();
		};
	}

}

#endif
