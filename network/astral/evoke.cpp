#include "evoke.h"

namespace xhqm
{
	namespace astral
	{
		static xhqm::u64 latest_time(const node& n)
		{
			if (!n.datas.is_array() || n.datas.empty()) return 0;
			return n.datas.back().value("time", xhqm::u64(0));
		}

		static kernel::uuid_list top_latest(std::vector<std::pair<xhqm::u64, uuid>>& cand)
		{
			std::sort(cand.begin(), cand.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
			kernel::uuid_list out;
			out.reserve(cand.size());
			for (const auto& [t, id] : cand) out.push_back(id);
			return out;
		}

		uuid evoke::create(const text& form, const text& info, const json& data, const std::vector<text>& links)
		{
			std::unique_lock<std::shared_mutex> lock(m_mutex);

			data_node n;
			n.info = info;
			astral::json rec;
			rec["time"] = now_stamp();
			rec["form"] = form;
			rec["json"] = data;
			n.datas.push_back(rec);
			uuid node_uuid = put_impl(n);
			for (const text& link : links)
			{
				uuid link_uuid = {};
				if (link.empty()) { continue; }
				auto it = m_name_uuid.find(link);
				if (it != m_name_uuid.end()) //已有标签
				{
					link_uuid = it->second;
				}
				else //新标签
				{
					link_node l;
					l.info = link;
					link_uuid = put_impl(l);
					m_name_uuid[link] = link_uuid;
				}

				kernel::set_connect_impl(node_uuid, link_uuid);
			}
			return node_uuid;
		}

		uuid evoke::create(const text& form, const text& info, const json& data, const text& link)
		{
			return create(form, info, data, std::vector<text>{ link });
		}

		uuid evoke::upsert(const text& form, const text& info, const json& data,
			const std::vector<text>& links, bool* out_upserted)
		{
			std::unique_lock<std::shared_mutex> lock(m_mutex);

			// info 精确索引查重（m_info_index: info → uuid 列表，一对多取首个）
			auto it = m_info_index.find(info);
			if (it != m_info_index.end() && !it->second.empty())
			{
				const uuid target = it->second.front();
				auto nit = m_node_data.find(target);
				if (nit != m_node_data.end())
				{
					fetch_impl(nit->second, target);
					if (!nit->second.datas.is_array())
						nit->second.datas = astral::json::array();   // 损坏数据段防护
					astral::json rec;
					rec["time"] = now_stamp();
					rec["form"] = form;
					rec["json"] = data;
					nit->second.datas.push_back(rec);
					// 补 links（复用/新建标签，与 create 同规则）
					for (const text& link : links)
					{
						if (link.empty()) continue;
						auto lit = m_name_uuid.find(link);
						uuid link_uuid;
						if (lit != m_name_uuid.end())
						{
							link_uuid = lit->second;
						}
						else
						{
							link_node l;
							l.info = link;
							link_uuid = put_impl(l);
							m_name_uuid[link] = link_uuid;
						}
						kernel::set_connect_impl(target, link_uuid);
					}
					if (out_upserted) *out_upserted = true;
					return target;
				}
			}
			// 未命中 → 新建（内联实现：本函数已持锁，调 create 会重入加锁死锁）
			{
				data_node n;
				n.info = info;
				astral::json rec;
				rec["time"] = now_stamp();
				rec["form"] = form;
				rec["json"] = data;
				n.datas.push_back(rec);
				uuid node_uuid = put_impl(n);
				for (const text& link : links)
				{
					if (link.empty()) continue;
					auto lit = m_name_uuid.find(link);
					uuid link_uuid;
					if (lit != m_name_uuid.end())
					{
						link_uuid = lit->second;
					}
					else
					{
						link_node l;
						l.info = link;
						link_uuid = put_impl(l);
						m_name_uuid[link] = link_uuid;
					}
					kernel::set_connect_impl(node_uuid, link_uuid);
				}
				if (out_upserted) *out_upserted = false;
				return node_uuid;
			}
		}

		bool evoke::remove(const text& form, const uuid& id, xhqm::u64 time)
		{
			if (time == 0)
			{
				std::unique_lock<std::shared_mutex> lock(m_mutex);
				auto it = m_node_data.find(id);
				if (it == m_node_data.end()) return false;
				const text info = it->second.info;
				// 捕获被删节点的直接关联：del_impl 会清掉这些节点对 id 的反向引用
				std::vector<uuid> linked;
				linked.reserve(it->second.links.size());
				for (const auto& [lid, linfo] : it->second.links) linked.push_back(lid);

				if (!del_impl(id)) return false;
				m_name_uuid.erase(info);

				// 级联：删除因失去唯一引用而变空的孤儿标签（info 非空 + datas 空 + links 空）
				// 不碰数据节点（datas 非空），避免历史 bug 误删数据节点。
				for (const uuid& lid : linked)
				{
					auto lit = m_node_data.find(lid);
					if (lit == m_node_data.end()) continue;
					if (!lit->second.links.empty()) continue;
					if (!lit->second.datas.is_array() || !lit->second.datas.empty()) continue;
					if (lit->second.info.empty()) continue;
					m_name_uuid.erase(lit->second.info);
					del_impl(lid);
				}
				return true;
			}
			return kernel::remove_time(form, id, time);
		}

		bool evoke::submit(const text& form, const uuid& id, const std::vector<text>& links)
		{
			if (kernel::has(id))
			{
				std::unique_lock<std::shared_mutex> lock(m_mutex);
				for (const text& link : links)
				{
					uuid link_uuid = {};
					if (link.empty()) { continue; }
					auto it = m_name_uuid.find(link);
					if (it != m_name_uuid.end()) //已有标签
					{
						link_uuid = it->second;
					}
					else //新标签
					{
						link_node l;
						l.info = link;
						link_uuid = put_impl(l);
						m_name_uuid[link] = link_uuid;
					}
					kernel::set_connect_impl(id, link_uuid);
				}
				return true;
			}
			return false;
		}

		bool evoke::submit(const text& form, const uuid& id, const text& link)
		{
			return evoke::submit(form, id, std::vector<text>{ link });
		}

		bool evoke::submit(const text& form, const uuid& id, const json& data)
		{
			return kernel::update_data(form, id, data);
		}

		bool evoke::update(const text& form, const uuid& id, const text& info)
		{
			return kernel::update_info(form, id, info);
		}

		json evoke::select_links(const std::vector<text>& links)
		{
			json array = json::array();
			if (links.empty()) return array;
			node_list list = kernel::select("",
				[](const text& form, const astral::node& node, const std::vector<astral::text>& links) -> json {
				astral::json array = astral::json::array();
				for (const auto& link : links)
				{
					bool is_break = false;
					if (link.empty()) { continue; }
					for (const auto& [uuid, text] : node.links)
					{
						if (node.datas.is_array() && !node.datas.empty() && text.find(link) != text::npos)
						{
							array.push_back(node.datas.back());
							is_break = true;
							break;
						}
					}
					if (is_break)
					{
						break;
					}
				}
				return array;
			}, links);
			for (const auto& node : list)
			{
				array.push_back(node.to_json());
			}
			return array;
		}

		json evoke::select_words(const std::vector<text>& words)
		{
			json array = json::array();
			node_list list = kernel::select_info(words);
			for (const auto& node : list)
			{
				array.push_back(node.to_json());
			}
			return array;
		}

		json evoke::select_datas(const std::vector<text>& words)
		{
			json array = json::array();
			node_list list = kernel::select_data(words);
			for (const auto& node : list)
			{
				array.push_back(node.to_json());
			}
			return array;
		}

		json evoke::select_nodes(const std::vector<uuid>& uuids)
		{
			json array = json::array();
			node_list list = kernel::select_uuid(uuids);
			for (const auto& node : list)
			{
				array.push_back(node.to_json());
			}
			return array;
		}

		void evoke::rebuild_names()
		{
			std::unique_lock<std::shared_mutex> lock(m_mutex);
			m_name_uuid.clear();
			// 标签节点特征：info 非空、被其他节点 links 引用、data 为空数组。
			// 先统计引用计数并收集被引用集合，再对其按需加热，datas 为空者入候选；
			// 同名标签取引用计数最大者（历史重复标签按使用度归并）。
			std::map<uuid, std::size_t, uuid_less> refcount;
			for (const auto& [id, n] : m_node_data)
				for (const auto& [lid, linfo] : n.links)
					++refcount[lid];
			std::map<text, std::pair<uuid, std::size_t>> best;
			for (const auto& [rid, rc] : refcount)
			{
				auto it = m_node_data.find(rid);
				if (it == m_node_data.end() || it->second.info.empty()) continue;
				fetch_impl(it->second, rid);
				if (it->second.datas.is_array() && it->second.datas.empty() && !it->second.links.empty())
				{
					auto& slot = best[it->second.info];
					if (rc > slot.second)
						slot = { rid, rc };
				}
			}
			for (const auto& [name, slot] : best)
				m_name_uuid[name] = slot.first;
		}
	}
}
