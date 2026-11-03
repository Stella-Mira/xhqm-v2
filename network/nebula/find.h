#ifndef XHQM_NEBULA_FIND
#define XHQM_NEBULA_FIND

// L2 局域搜索p零号协议原则 + make/exec 句式）：
//   - 收到即合并本地区域图p按 uuid 幂等覆盖；乱序到达结果一致）
//   - l>0 转发除来源外邻居pl 递减；事件 id 去重防环）
//   - 路由 = 合并图上 dijkstrap每次合并/老化后重算）

#include "nebula.h"
#include "link.h"
#include <map>
#include <set>

namespace xhqm
{
	namespace nebula
	{
		class xhqm_io find : public link
		{
		public:
			struct sign
			{
				nebula::uuid from;
				nebula::size size = 0;
				nebula::size time = 0;//ns
			};
			struct node
			{
				nebula::uuid iden;
				std::set<nebula::addr, addr_less> addrs;

				bool operator<(const node& y) const
				{
					return (nebula::uuid_less()(iden, y.iden));
				}
			};
			struct edge
			{
				nebula::uuid a, b;

				static int cmp(const nebula::uuid& l, const nebula::uuid& r)
				{
					for (xhqm::size i = 0; i < 16; ++i)
					{
						if (l(i) != r(i)) return l(i) < r(i) ? -1 : 1;
					}
					return 0;
				}

				bool operator<(const edge& y) const
				{
					int ca = cmp(a, y.a);
					if (ca != 0) return ca < 0;
					int cb = cmp(b, y.b);
					if (cb != 0) return cb < 0;
					return false;
				}
			};

			struct graph
			{
				std::map<nebula::uuid, nebula::find::node, uuid_less> nodes;   // uuid → 事件端点p身份缓存）
				std::map<nebula::find::edge, std::chrono::steady_clock::time_point> edges;// 边 → 本地单调时间戳

				nebula::json make_json() const;
				void with_json(const nebula::json& value);                  // 合并p幂等覆盖/去重插入）
				void clear(std::chrono::steady_clock::duration timeout);				// 清理时长超过timeout的边
				void clear(const nebula::uuid& base, const nebula::size& pathcount);    // 清理距离base的最短路径长度大于pathcount的边
				// 返回 from→to 的节点序列；不可达/端点不在视图 → 空；from==to → {from}。
				std::vector<nebula::uuid> short_path(const nebula::uuid& from, const nebula::uuid& to) const;
			};
		protected:
			// ---- 区域图状态p当前已见视图）----
			mutable std::mutex m_find_mutex;
			nebula::find::graph m_region_graph;
			std::chrono::seconds m_region_aging{ 60 };
			std::map<nebula::uuid, nebula::find::sign> m_back_map;//事件ID和来源节点ID
			std::map<nebula::uuid, xhqm::u64, uuid_less> m_relay_map;//转发去重：事件 id → 过期时刻(ns)
			xhqm::u64 m_relay_aging = 30000000000ull;//转发去重窗口：30s
		protected:
			virtual void exec_dath(const  nebula::json&) override;
		protected:
			// 带 goal 的语义事件pcare/omit/push/...）的转发：
			//   返回 true = 本层已消费p转发了 / 去重吞掉了）；false = 就是给我的，交上层ppost/tool）
			virtual bool exec_relay(const nebula::json&);
			// 到 goal 发得出去吗：直连邻居，或图上路径里存在一个我的直连邻居
			bool routable(const nebula::uuid& goal) const;
			// 选下一跳p发不出去时返回空 uuid）
			nebula::uuid next_hop(const nebula::uuid& goal) const;
			nebula::json make_layer(const nebula::json&);
			nebula::json make_graph(const nebula::uuid&);
			virtual void exec_layer(const nebula::json&);
			virtual void exec_graph(const nebula::json&);
			virtual text exec_event(const  nebula::json&) override;
		public:
			find(const find&) = delete;
			find& operator=(const find&) = delete;
			explicit find(const xhqm::text& path, xhqm::size m, xhqm::size n, xhqm::size c, xhqm::size a, xhqm::size size, xhqm::size time)
				: nebula::link(path, m, n, c, a, size, time)
			{

			}

			void updata_graph(nebula::size deep, xhqm::u64 wait);
			std::vector<nebula::uuid> short_path(const nebula::uuid& uuid) const;
			inline std::chrono::seconds region_aging(void) const { return m_region_aging; };
			inline void region_aging(std::chrono::seconds value) { m_region_aging = value; };
		};
	}
}

#endif
