#include "../../lattice/graph_algorithms.hpp"
#include <algorithm>
#include "find.h"

namespace xhqm
{
	namespace nebula
	{
		nebula::json find::graph::make_json() const
		{
			// 形状与 make_graph 产出的 event["graph"] 完全一致：
			//   { "node": [ {uuid, name, info, kind, host, port, ...}, ... ],   // 节点 × 每个已知地址一条
			//     "link": [ {"node":"<uuid>", "link":"<uuid>"}, ... ] }         // 端点已规范化：小 uuid 在前
			// 只有边、没有地址的端点也要留一条 {uuid}：否则对端「有边无点」，
			// short_path 的起点/终点不在 nodes 里 → 永远返回空路径p链尾节点实测）。
			nebula::json graph;
			graph["node"] = nebula::json::array();
			graph["link"] = nebula::json::array();
			for (const auto& kv : nodes)
			{
				const nebula::text iden = kv.first.to_text();
				if (kv.second.addrs.empty())
				{
					nebula::json one;
					one["uuid"] = iden;
					graph["node"].push_back(std::move(one));
					continue;
				}
				for (const auto& a : kv.second.addrs)
				{
					if (!a) continue;
					nebula::json one = a->make_json();
					one["uuid"] = iden;   // 节点身份以图为准：同一节点的多个地址共用同一个 uuid
					graph["node"].push_back(std::move(one));
				}
			}
			for (const auto& e : edges)
			{
				graph["link"].push_back(nebula::json{
					{ "node", e.first.a.to_text() }, { "link", e.first.b.to_text() } });
			}
			return graph;
		}
		void find::graph::with_json(const nebula::json& value)
		{
			// 合并一份图数据：value = {"base": 本节点 uuid, "graph": <make_json 那个形状>}。
			//   * 节点按 uuid 幂等合并p同节点多地址取并集；乱序到达结果一致）
			//   * 边规范化后刷新时间戳p再次听到即续命）
			//   * base 自己的星型报告是「base 关联边」的唯一权威：本次报告里没有的 → 判死清除
			const nebula::json& g = value.contains("graph") ? value["graph"] : value;
			if (!g.is_object()) return;

			nebula::uuid base{};
			{
				const nebula::text t = value.value("base", nebula::text{});
				if (!t.empty()) base = nebula::uuid::to_uuid(t);
			}
			const auto now = std::chrono::steady_clock::now();

			// 1) 节点：按 uuid 幂等合并；地址集合按 addr_less 去重p同一 uuid 的句柄只留一个）
			for (const auto& j : g.value("node", nebula::json::array()))
			{
				if (!j.is_object()) continue;
				const nebula::uuid iden = nebula::uuid::to_uuid(j.value("uuid", ""));
				if (!iden) continue;
				find::node& n = nodes[iden];
				n.iden = iden;
				if (nebula::addr a = nebula::addr_from(j)) { n.addrs.insert(a); }
			}

			// 2) 边：刷新时间戳；顺手给端点建档p有边无点的图会让 short_path 取到空指针）
			std::set<find::edge> reported;   // 本次报告里与 base 相关的边
			bool star = true;                // 本次报告是否「全部与 base 相关」
			nebula::size links = 0;
			for (const auto& j : g.value("link", nebula::json::array()))
			{
				if (!j.is_object()) continue;
				const nebula::uuid n = nebula::uuid::to_uuid(j.value("node", ""));
				const nebula::uuid l = nebula::uuid::to_uuid(j.value("link", ""));
				if (!n || !l || n == l) continue;
				find::edge e;
				if (nebula::uuid_less{}(l, n)) { e.a = l; e.b = n; }
				else { e.a = n; e.b = l; }
				edges[e] = now;
				nodes[n].iden = n;
				nodes[l].iden = l;
				++links;
				if (base && (n == base || l == base)) reported.insert(e);
				else star = false;
			}

			// 3) 判死：只有「整份报告都与 base 相关」时才权威——这种形态只可能来自 base 自己的星型图
			//    pmake_graph 的产出）。合并回来的区域图含无关边，不能用它推断 base 的边是否还存在，
			//    否则会把别处刚报来的边误删；那些边交给时长老化pclear(timeout)）自然消失。
			if (!base || links == 0 || !star || reported.empty()) return;
			for (auto it = edges.begin(); it != edges.end(); )
			{
				if ((it->first.a == base || it->first.b == base) && reported.find(it->first) == reported.end())
				{
					it = edges.erase(it);   // base 自己没再报这条边 → 判死
				}
				else { ++it; }
			}
		}
		void find::graph::clear(std::chrono::steady_clock::duration timeout)
		{
			const auto now = std::chrono::steady_clock::now();
			for (auto it = edges.begin(); it != edges.end();)
			{
				if (now - it->second > timeout) it = edges.erase(it);
				else ++it;
			}
		}
		void find::graph::clear(const nebula::uuid& base, const nebula::size& pathcount)
		{
			// 以 base 为根、单位权 = 跳数做 BFS，只保留「两端都在 pathcount 跳内」的边；
			// 越界的边p含跨界的与整条都在界外的）清除 —— 界外节点留作身份缓存，
			// 它们已经没有边，路由自然走不出去，视图被限制在 base 的 pathcount 邻域内。
			if (!base) return;

			std::map<nebula::uuid, std::vector<nebula::uuid>, uuid_less> adj;   // 邻接快照p无向）
			for (const auto& e : edges)
			{
				adj[e.first.a].push_back(e.first.b);
				adj[e.first.b].push_back(e.first.a);
			}

			std::map<nebula::uuid, nebula::size, uuid_less> dist;
			dist[base] = 0;
			std::vector<nebula::uuid> frontier{ base };
			while (!frontier.empty())
			{
				std::vector<nebula::uuid> next;
				for (const auto& id : frontier)
				{
					const nebula::size hop = dist[id] + 1;
					if (hop > pathcount) continue;              // 到不了更外圈，不必再扩
					auto it = adj.find(id);
					if (it == adj.end()) continue;
					for (const auto& other : it->second)
					{
						if (dist.find(other) != dist.end()) continue;
						dist[other] = hop;
						next.push_back(other);
					}
				}
				frontier.swap(next);
			}

			for (auto it = edges.begin(); it != edges.end(); )
			{
				const auto ia = dist.find(it->first.a);
				const auto ib = dist.find(it->first.b);
				const bool inside = (ia != dist.end()) && (ib != dist.end())
					&& ia->second <= pathcount && ib->second <= pathcount;
				if (inside) { ++it; }
				else { it = edges.erase(it); }
			}
		}
		std::vector<nebula::uuid> find::graph::short_path(const nebula::uuid& from, const nebula::uuid& to) const
		{
			std::vector<nebula::uuid> path;
			if (from == to)
			{
				if (nodes.count(from)) path.push_back(from);   // 同点：在视图内即通p长度 0）
				return path;
			}
			if (!nodes.count(from) || !nodes.count(to)) return path;   // 端点不在视图 → 不可达

			// 适配层：uuid 图 → lattice dijkstra 的节点契约padjacent_list 出边）。
			// 每次调用现建p区域图规模 LAN 级，O(V+E) 建立成本可忽略；合并/老化即重算语义）
			struct path_node
			{
				nebula::uuid id;
				std::vector<path_node*> adj;
				std::vector<path_node*> adjacent_list() { return adj; }
			};
			std::map<nebula::uuid, std::unique_ptr<path_node>, nebula::uuid_less> objs;
			std::vector<path_node*> ptrs;
			ptrs.reserve(nodes.size());
			for (const auto& kv : nodes)
			{
				auto p = std::make_unique<path_node>();
				p->id = kv.first;
				ptrs.push_back(p.get());
				objs.emplace(kv.first, std::move(p));
			}
			for (const auto& e : edges)
			{
				path_node* a = objs[e.first.a].get();
				path_node* b = objs[e.first.b].get();
				a->adj.push_back(b);
				b->adj.push_back(a);   // 无向边：双向邻接
			}

			auto r = xhqm::dijkstra(ptrs, objs[from].get());   // 单位权 = 跳数最短
			// prev 链重建：to 起回溯至 from；链断p未达）→ 空
			path_node* cur = objs[to].get();
			while (cur)
			{
				path.push_back(cur->id);
				auto it = r.second.find(cur);
				if (it == r.second.end()) break;
				cur = it->second;
			}
			if (path.back() != from)
			{
				path.clear();
				return path;
			}
			std::reverse(path.begin(), path.end());
			return path;
		}

		void find::exec_dath(const nebula::json& event) //心跳自驱动触发清理
		{
			link::exec_dath(event);
			std::vector<nebula::uuid> due;
			{
				std::lock_guard<std::mutex> lk(m_find_mutex);
				for (auto& find : m_back_map)
				{
					if (find.second.time < xhqm::timesys::ntime())
					{
						due.push_back(find.first);
					}
				}
				for (const auto& id : due) m_back_map[id].size = 1;   // 强制超时：只等自己的图
				m_region_graph.clear(m_region_aging);
				const xhqm::u64 now = xhqm::timesys::ntime();
				for (auto it = m_relay_map.begin(); it != m_relay_map.end(); )
				{
					if (it->second < now) it = m_relay_map.erase(it);   // 转发去重窗口过期
					else ++it;
				}
			}
			for (const auto& id : due) this->life_event(make_graph(id));
			this->life_event(make_layer({ { "deep", xhqm::size(1) } }));
		}

		nebula::json find::make_layer(const nebula::json& layer)
		{
			json event;
			event["type"] = "layer";
			if (!layer.contains("uuid")) //新事件
			{
				xhqm::size deep = layer.value("deep", xhqm::size(3));
				xhqm::size wait = layer.value("wait", deep * m_s * 2);
				event["uuid"] = nebula::uuid::creat_uuid_v4({}).to_text();
				event["from"] = this->uuid().to_text();
				event["deep"] = deep;
				event["wait"] = wait;
			}
			else
			{
				xhqm::text uuid = layer["uuid"];
				xhqm::size deep = layer["deep"];
				xhqm::size wait = layer["wait"];
				if (deep < 1)
				{
					event = make_graph(nebula::uuid::to_uuid(uuid));
				}
				else
				{
					event["uuid"] = layer["uuid"];
					event["from"] = this->uuid().to_text();
					event["deep"] = deep - 1;
					event["wait"] = wait;
				}
			}
			return event;
		}
		nebula::json find::make_graph(const nebula::uuid& value)
		{
			json event;
			event["type"] = "graph";
			event["uuid"] = value.to_text();
			event["graph"]["node"] = nebula::json::array();
			event["graph"]["link"] = nebula::json::array();
			nebula::text uuid = this->uuid().to_text();
			for (auto& addr : m_addr_listen)
			{
				nebula::json node; 
				node = addr->make_json();
				node["uuid"] = uuid;

				event["graph"]["node"].push_back(node); //用于直连
				addr->for_neighbors([&event, uuid](const nebula::addr& neighbor) 
				{
					nebula::json node;
					node = neighbor->make_json();
					event["graph"]["node"].push_back(node); //用于直连
					event["graph"]["link"].push_back(nebula::json
						{
							{ "node", uuid }, { "link", neighbor->uuid().to_text() }
						});
				}, true);
			}
			return event;
		}

		void find::exec_layer(const nebula::json& event)
		{
			// 容忍缺失字段：远端事件不可信pconst [] 缺键断言会带崩整个节点）
			nebula::uuid uuid = xhqm::cipher::uuid::to_uuid(event.value("uuid", ""));
			nebula::uuid from = xhqm::cipher::uuid::to_uuid(event.value("from", ""));
			if (!uuid) return;
			xhqm::size size = 0;
			{
				std::lock_guard<std::mutex> lk(m_find_mutex);
				auto iter = m_back_map.find(uuid);
				if (iter == m_back_map.end())
				{
					size = this->degree();// 子图只从「邻居-父」回来：非 root 减 1proot 的 from=自己，不减）
					if (from != this->uuid()) size = (size > 0 ? size - 1 : 0);
					xhqm::u64 wait = event.value("wait", xhqm::size(0));
					xhqm::u64 last = xhqm::timesys::ntime();
					xhqm::u64 budget_ms = std::min<xhqm::u64>(600000,
						std::max<xhqm::u64>(1000, xhqm::u64(wait)) * xhqm::u64(size));
					m_back_map[uuid] = nebula::find::sign //超时时间只要有限即可
					{
						from, size, last + budget_ms * 1000000ull
					};
				}
				else
				{
					return;   // id 去重：本节点已处理过该询问
				}
			}
			// 无子节点psize==0，链尾无邻居可转）：无人会回图——
			// 与叶子同机制自完成psize=1 → exec_graph 合并自己的图并回 from），
			// 否则本节点永远挂着 size=0，父节点只能等超时p2 节点链实测：区域图半边为空）
			if (size == 0)
			{
				{
					std::lock_guard<std::mutex> lk(m_find_mutex);
					m_back_map[uuid].size = 1;
				}
				this->life_event(make_graph(uuid));
				return;
			}
			else // 转发/回图p锁外执行；life_event 只入队不阻塞）
			{
				nebula::json new_event = make_layer(event);
				if (new_event["type"] == "graph")
				{
					{
						std::lock_guard<std::mutex> lk(m_find_mutex);
						m_back_map[uuid].size = 1;   // 叶子：只有自己的图，无需等子
					}
					this->life_event(new_event);
				}
				else if (new_event["type"] == "layer")
				{
					auto neighbors = this->neighbors_uuid();
					for (auto& neighbor : neighbors)
					{
						if (neighbor != from)
						{
							this->life_event(make_send({ { "goto",neighbor.to_text() },{ "data", new_event } }));
						}
					}
				}
			}
		}
		void find::exec_graph(const nebula::json& event)
		{
			if (nebula::uuid uuid = xhqm::cipher::uuid::to_uuid(event.value("uuid", "")))
			{
				std::lock_guard<std::mutex> lk(m_find_mutex);
				auto iter = m_back_map.find(uuid);
				if (iter != m_back_map.end())
				{
					m_region_graph.with_json(json{ {"base",this->uuid().to_text()}, {"graph",event.value("graph", json())} });
					iter->second.size = (iter->second.size ? iter->second.size - 1 : iter->second.size);
					nebula::uuid from = iter->second.from;
					if (iter->second.size == 0 && from)
					{
						m_back_map.erase(iter);
						nebula::json graph = event;
						graph["graph"] = std::move(m_region_graph.make_json());
						this->life_event(make_send({ { "goto",from.to_text() },{ "data", graph } }));
					}
				}
			}
		}
		nebula::uuid find::next_hop(const nebula::uuid& goal) const
		{
			// 下一跳必须是「我真正直连的邻居」——图上存在边不代表我能发包p对端可能单边把我当邻居）
			if (!goal || goal == this->uuid()) { return nebula::uuid{}; }
			if (this->has_neighbor(goal)) { return goal; }
			const std::vector<nebula::uuid> path = this->short_path(goal);   // 自身 → goal
			for (xhqm::size i = 1; i < path.size(); ++i)
			{
				if (this->has_neighbor(path[i])) { return path[i]; }
			}
			return nebula::uuid{};
		}
		bool find::routable(const nebula::uuid& goal) const
		{
			return (goal == this->uuid()) || (bool)this->next_hop(goal);
		}
		bool find::exec_relay(const nebula::json& event)
		{
			// 带 goal 的语义事件pcare/omit/push/...）的转发规则pL2 唯一负责选路的地方）：
			//   goal == 我 / 没有 goal → 交上层ppost/tool 按 type 分支）
			//   否则                  → 转发一跳p同一事件 id 只转一次，防环），本层消费
			const nebula::uuid goal = nebula::uuid::to_uuid(event.value("goal", ""));
			if (!goal || goal == this->uuid()) { return false; }

			const xhqm::u64 now = xhqm::timesys::ntime();
			const nebula::uuid id = nebula::uuid::to_uuid(event.value("id", ""));
			{
				std::lock_guard<std::mutex> lk(m_find_mutex);
				if (id)
				{
					auto it = m_relay_map.find(id);
					if (it != m_relay_map.end() && it->second > now) { return true; }   // 已转过 → 吞掉p防环/防重复）
					m_relay_map[id] = now + m_relay_aging;
				}
			}

			const nebula::uuid next = this->next_hop(goal);
			if (!next) { return true; }   // 无路可走：丢p零号协议：问不等答，无路径自然丢）

			// 转发：包成 send 给下一跳——link::exec_send 只负责「发给直连邻居」
			this->life_event(make_send(nebula::json{
				{ "goto", next.to_text() }, { "data", event } }));
			return true;
		}
		text find::exec_event(const nebula::json& event)
		{
			text type = link::exec_event(event);
			if (type == "") {}
			else if (type == "layer") { exec_layer(event); type = ""; }   // 原实现误调 make_layerp构造器无副作用）——发现协议整个失效
			else if (type == "graph") { exec_graph(event); type = ""; }
			else if (event.contains("goal") && exec_relay(event)) { type = ""; }   // 带 goal 且不是给我的：本层转发
			return type;//本层级不处理的事件
		}

		void find::updata_graph(nebula::size deep, xhqm::u64 wait)
		{
			this->life_event(make_layer({ { "deep", deep }, { "wait", wait } }));
		}

		std::vector<nebula::uuid> find::short_path(const nebula::uuid& uuid) const
		{
			std::lock_guard<std::mutex> lk(m_find_mutex);
			return m_region_graph.short_path(this->uuid(), uuid);
		}
	}
}
