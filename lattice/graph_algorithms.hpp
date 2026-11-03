/*
*作者：星辉澪
*时间：乙巳
*用途：图算法层（自由函数，与结构族分离）
* 契约：全部算法以「全节点集合 std::vector<node*>」为输入，
* 节点需提供 adjacent_list()（有向邻接，非零边=有边）。
* 带权算法：weight(u, k) 回调返回 u 第 k 条出边的权重（默认单位权=BFS 语义）。
* 现有：拓扑排序 / 强连通分量 / 弱连通分量 / Dijkstra / Floyd-Warshall；
* MST、最大流等未来扩展
*/

#pragma once
#ifndef XHQM_GRAPH_ALGORITHMS
#define XHQM_GRAPH_ALGORITHMS

#include "./struct.h"
#include <map>
#include <queue>
#include <stack>
#include <utility>
#include <vector>
#include <functional>
#include <limits>
#include <algorithm>
#include <set>

namespace xhqm
{
	//拓扑排序（Kahn，有向无环图；有环返回空）
	template<typename _node_type>
	std::vector<_node_type*> topological_sort(const std::vector<_node_type*>& nodes)
	{
		std::map<_node_type*, xhqm::index> indegree;
		for (auto* u : nodes) indegree[u] = 0;
		for (auto* u : nodes)
			for (auto* v : u->adjacent_list())
				if (indegree.find(v) != indegree.end()) ++indegree[v];
		std::queue<_node_type*> q;
		for (auto* u : nodes) if (indegree[u] == 0) q.push(u);
		std::vector<_node_type*> order;
		while (!q.empty())
		{
			auto* u = q.front(); q.pop();
			order.push_back(u);
			for (auto* v : u->adjacent_list())
				if (indegree.find(v) != indegree.end() && --indegree[v] == 0) q.push(v);
		}
		if (order.size() != nodes.size()) return {};
		return order;
	}

	//强连通分量（Kosaraju，有向图；DFS 改迭代避免深图栈溢出）
	//第一次 DFS：迭代后序，记录完成顺序
	template<typename _node_type>
	void _scc_dfs1_(_node_type* start, std::map<_node_type*, bool>& visited, std::vector<_node_type*>& finish)
	{
		std::stack<std::pair<_node_type*, bool>> st;
		st.push({ start, false });
		visited[start] = true;
		while (!st.empty())
		{
			auto top = st.top(); st.pop();
			_node_type* u = top.first;
			if (top.second)
			{
				finish.push_back(u);  // 子节点已全部访问
			}
			else
			{
				st.push({ u, true });
				auto adj = u->adjacent_list();
				for (auto rit = adj.rbegin(); rit != adj.rend(); ++rit)
				{
					_node_type* v = *rit;
					if (!visited[v])
					{
						visited[v] = true;
						st.push({ v, false });
					}
				}
			}
		}
	}
	//第二次 DFS：迭代，反向图收集分量
	template<typename _node_type>
	void _scc_dfs2_(_node_type* start, std::map<_node_type*, bool>& visited,
		const std::map<_node_type*, std::vector<_node_type*>>& rev, std::vector<_node_type*>& comp)
	{
		std::stack<_node_type*> st;
		st.push(start);
		visited[start] = true;
		while (!st.empty())
		{
			_node_type* u = st.top(); st.pop();
			comp.push_back(u);
			auto it = rev.find(u);
			if (it == rev.end()) continue;
			for (auto rit = it->second.rbegin(); rit != it->second.rend(); ++rit)
			{
				_node_type* v = *rit;
				if (!visited[v])
				{
					visited[v] = true;
					st.push(v);
				}
			}
		}
	}
	template<typename _node_type>
	std::vector<std::vector<_node_type*>> strongly_connected_components(const std::vector<_node_type*>& nodes)
	{
		std::map<_node_type*, bool> visited;
		for (auto* u : nodes) visited[u] = false;
		std::vector<_node_type*> finish;
		for (auto* u : nodes)
			if (!visited[u]) _scc_dfs1_(u, visited, finish);
		std::map<_node_type*, std::vector<_node_type*>> rev;
		for (auto* u : nodes)
			for (auto* v : u->adjacent_list())
				rev[v].push_back(u);
		for (auto* u : nodes) visited[u] = false;
		std::vector<std::vector<_node_type*>> sccs;
		for (auto it = finish.rbegin(); it != finish.rend(); ++it)
		{
			auto* u = *it;
			if (!visited[u])
			{
				std::vector<_node_type*> comp;
				_scc_dfs2_(u, visited, rev, comp);
				sccs.push_back(comp);
			}
		}
		return sccs;
	}

	//弱连通分量（有向图忽略边方向；并查集 + 路径压缩）
	template<typename _node_type>
	std::vector<std::vector<_node_type*>> weakly_connected_components(const std::vector<_node_type*>& nodes)
	{
		std::map<_node_type*, _node_type*> parent;
		for (auto* u : nodes) parent[u] = u;
		auto find = [&](_node_type* x) -> _node_type* {
			while (parent[x] != x)
			{
				parent[x] = parent[parent[x]];   // 路径压缩
				x = parent[x];
			}
			return x;
		};
		for (auto* u : nodes)
			for (auto* v : u->adjacent_list())
				if (parent.find(v) != parent.end())
					parent[find(u)] = find(v);
		std::map<_node_type*, std::vector<_node_type*>> comps;
		for (auto* u : nodes) comps[find(u)].push_back(u);
		std::vector<std::vector<_node_type*>> result;
		for (auto& kv : comps) result.push_back(std::move(kv.second));
		return result;
	}

	//—— 带权最短路（距离 double；不可达 = infinity）——

	//Dijkstra（非负权单源最短路）。weight(u, k) 返回 u 第 k 条出边的权重。
	//返回 {dist, prev}：dist 不含不可达节点（可达者才有条目）；
	//prev[v] = 前驱节点（起点无前驱），供路径重建
	template<typename _node_type, typename _weight_func>
	std::pair<std::map<_node_type*, double>, std::map<_node_type*, _node_type*>>
	dijkstra(const std::vector<_node_type*>& nodes, _node_type* start, _weight_func&& weight)
	{
		std::map<_node_type*, double> dist;
		std::map<_node_type*, _node_type*> prev;
		using item = std::pair<double, _node_type*>;
		std::priority_queue<item, std::vector<item>, std::greater<item>> pq;
		dist[start] = 0.0;
		pq.push({ 0.0, start });
		while (!pq.empty())
		{
			auto top = pq.top(); pq.pop();
			const double d = top.first;
			_node_type* u = top.second;
			if (d != dist[u]) continue;   // 过期条目
			auto adj = u->adjacent_list();
			for (xhqm::size k = 0; k < adj.size(); ++k)
			{
				_node_type* v = adj[k];
				const double nd = d + static_cast<double>(weight(u, k));
				auto it = dist.find(v);
				if (it == dist.end() || nd < it->second)
				{
					dist[v] = nd;
					prev[v] = u;
					pq.push({ nd, v });
				}
			}
		}
		return { std::move(dist), std::move(prev) };
	}
	//单位权重重载（无权图，等价 BFS 层数距离）
	template<typename _node_type>
	std::pair<std::map<_node_type*, double>, std::map<_node_type*, _node_type*>>
	dijkstra(const std::vector<_node_type*>& nodes, _node_type* start)
	{
		return dijkstra(nodes, start, [](_node_type*, xhqm::size) { return 1.0; });
	}

	//Floyd-Warshall（全源最短路，可负权、无负环）。weight(u, k) 同上。
	//返回 dist[u][v]：dist[u][u]=0，不可达=infinity
	template<typename _node_type, typename _weight_func>
	std::map<_node_type*, std::map<_node_type*, double>>
	floyd_warshall(const std::vector<_node_type*>& nodes, _weight_func&& weight)
	{
		const double INF = std::numeric_limits<double>::infinity();
		std::map<_node_type*, std::map<_node_type*, double>> dist;
		for (auto* u : nodes)
			for (auto* v : nodes)
				dist[u][v] = (u == v) ? 0.0 : INF;
		for (auto* u : nodes)
		{
			auto adj = u->adjacent_list();
			for (xhqm::size k = 0; k < adj.size(); ++k)
			{
				const double w = static_cast<double>(weight(u, k));
				if (w < dist[u][adj[k]]) dist[u][adj[k]] = w;
			}
		}
		for (auto* k : nodes)
			for (auto* i : nodes)
				for (auto* j : nodes)
				{
					const double via = dist[i][k] + dist[k][j];
					if (via < dist[i][j]) dist[i][j] = via;
				}
		return dist;
	}
	//单位权重重载（传递闭包：dist[u][v] = 最少边数）
	template<typename _node_type>
	std::map<_node_type*, std::map<_node_type*, double>>
	floyd_warshall(const std::vector<_node_type*>& nodes)
	{
		return floyd_warshall(nodes, [](_node_type*, xhqm::size) { return 1.0; });
	}

	//—— 最小生成树 ——

	//Kruskal（无向带权 MST，边排序 + 并查集）。weight(u, k) 同上。
	//双向网（自动双向）中每条无向边出现两次，并查集天然去重。
	//返回 MST 边集（连通图 N-1 条；不连通则少于 N-1）
	template<typename _node_type, typename _weight_func>
	std::vector<std::pair<_node_type*, _node_type*>> kruskal(const std::vector<_node_type*>& nodes, _weight_func&& weight)
	{
		struct edge { double w; _node_type* u; _node_type* v; };
		std::set<_node_type*> in(nodes.begin(), nodes.end());
		std::vector<edge> edges;
		for (auto* u : nodes)
		{
			auto adj = u->adjacent_list();
			for (xhqm::size k = 0; k < adj.size(); ++k)
			{
				_node_type* v = adj[k];
				if (in.count(v)) edges.push_back({ static_cast<double>(weight(u, k)), u, v });
			}
		}
		std::sort(edges.begin(), edges.end(), [](const edge& a, const edge& b) { return a.w < b.w; });
		std::map<_node_type*, _node_type*> parent;
		for (auto* u : nodes) parent[u] = u;
		auto find = [&](_node_type* x) -> _node_type* {
			while (parent[x] != x)
			{
				parent[x] = parent[parent[x]];
				x = parent[x];
			}
			return x;
		};
		std::vector<std::pair<_node_type*, _node_type*>> tree;
		for (auto& e : edges)
		{
			if (find(e.u) != find(e.v))
			{
				parent[find(e.u)] = find(e.v);
				tree.push_back({ e.u, e.v });
			}
		}
		return tree;
	}
	//单位权重重载
	template<typename _node_type>
	std::vector<std::pair<_node_type*, _node_type*>> kruskal(const std::vector<_node_type*>& nodes)
	{
		return kruskal(nodes, [](_node_type*, xhqm::size) { return 1.0; });
	}

	//Prim（无向带权 MST，堆优化）。返回 MST 边集（同 Kruskal 语义）
	template<typename _node_type, typename _weight_func>
	std::vector<std::pair<_node_type*, _node_type*>> prim(const std::vector<_node_type*>& nodes, _weight_func&& weight)
	{
		if (nodes.empty()) return {};
		const double INF = std::numeric_limits<double>::infinity();
		std::map<_node_type*, double> key;
		std::map<_node_type*, _node_type*> prev;
		std::set<_node_type*> done;
		for (auto* u : nodes) key[u] = INF;
		key[nodes[0]] = 0.0;
		using item = std::pair<double, _node_type*>;
		std::priority_queue<item, std::vector<item>, std::greater<item>> pq;
		pq.push({ 0.0, nodes[0] });
		while (!pq.empty())
		{
			auto top = pq.top(); pq.pop();
			_node_type* u = top.second;
			if (done.count(u)) continue;
			done.insert(u);
			auto adj = u->adjacent_list();
			for (xhqm::size k = 0; k < adj.size(); ++k)
			{
				_node_type* v = adj[k];
				if (done.count(v)) continue;
				const double w = static_cast<double>(weight(u, k));
				if (w < key[v])
				{
					key[v] = w;
					prev[v] = u;
					pq.push({ w, v });
				}
			}
		}
		std::vector<std::pair<_node_type*, _node_type*>> tree;
		for (auto& kv : prev) tree.push_back({ kv.second, kv.first });
		return tree;
	}
	//单位权重重载
	template<typename _node_type>
	std::vector<std::pair<_node_type*, _node_type*>> prim(const std::vector<_node_type*>& nodes)
	{
		return prim(nodes, [](_node_type*, xhqm::size) { return 1.0; });
	}

	//—— 最大流 ——

	//Edmonds-Karp（最大流）。capacity(u, k) 返回 u 第 k 条出边的容量（非负）。
	//残量以 (u,v) 对存储（平行边累加），BFS 增广。返回 s→t 的最大流值
	template<typename _node_type, typename _cap_func>
	double max_flow(const std::vector<_node_type*>& nodes, _node_type* s, _node_type* t, _cap_func&& capacity)
	{
		const double INF = std::numeric_limits<double>::infinity();
		std::map<std::pair<_node_type*, _node_type*>, double> resid;
		for (auto* u : nodes)
		{
			auto adj = u->adjacent_list();
			for (xhqm::size k = 0; k < adj.size(); ++k)
			{
				const double c = static_cast<double>(capacity(u, k));
				if (c > 0) resid[{ u, adj[k] }] += c;
			}
		}
		double flow = 0.0;
		while (true)
		{
			// BFS 找增广路
			std::map<_node_type*, _node_type*> prev;
			std::queue<_node_type*> q;
			q.push(s);
			prev[s] = nullptr;
			while (!q.empty() && prev.find(t) == prev.end())
			{
				_node_type* u = q.front(); q.pop();
				for (auto* v : nodes)
				{
					if (prev.find(v) != prev.end()) continue;
					auto it = resid.find({ u, v });
					if (it != resid.end() && it->second > 0)
					{
						prev[v] = u;
						q.push(v);
					}
				}
			}
			if (prev.find(t) == prev.end()) break;   // 无增广路
			double aug = INF;
			for (auto* v = t; v != s; v = prev[v])
				aug = std::min(aug, resid[{ prev[v], v }]);
			for (auto* v = t; v != s; v = prev[v])
			{
				resid[{ prev[v], v }] -= aug;
				resid[{ v, prev[v] }] += aug;
			}
			flow += aug;
		}
		return flow;
	}

	//—— 二分图最大匹配 ——

	//匈牙利算法（增广路）。left/right 为同一节点类型的两个不相交集合，
	//边自 left 指向 right（有向邻接）。返回匹配边对（left, right）
	namespace
	{
		template<typename _node_type>
		bool _bpm_augment_(_node_type* u, std::map<_node_type*, _node_type*>& match_r,
			std::set<_node_type*>& visited)
		{
			for (auto* v : u->adjacent_list())
			{
				if (visited.count(v)) continue;
				visited.insert(v);
				auto it = match_r.find(v);
				if (it == match_r.end() || _bpm_augment_(it->second, match_r, visited))
				{
					match_r[v] = u;
					return true;
				}
			}
			return false;
		}
	}
	template<typename _node_type>
	std::vector<std::pair<_node_type*, _node_type*>> bipartite_matching(
		const std::vector<_node_type*>& left, const std::vector<_node_type*>& right)
	{
		std::map<_node_type*, _node_type*> match_r;
		for (auto* u : left)
		{
			std::set<_node_type*> visited;
			_bpm_augment_(u, match_r, visited);
		}
		std::vector<std::pair<_node_type*, _node_type*>> result;
		for (auto& kv : match_r) result.push_back({ kv.second, kv.first });
		return result;
	}

	//—— A* ——

	//A*（启发式最短路）。weight(u, k) 同上；heuristic(u) 估计 u 到 goal 的代价
	//（须可采纳：不高于真实代价，保证 dist[goal] 最优）。
	//返回 {dist, prev}：dist[goal] 最优；其余节点为探索期部分 g 值
	template<typename _node_type, typename _weight_func, typename _heur_func>
	std::pair<std::map<_node_type*, double>, std::map<_node_type*, _node_type*>>
	a_star(const std::vector<_node_type*>& nodes, _node_type* start, _node_type* goal,
		_weight_func&& weight, _heur_func&& heuristic)
	{
		std::map<_node_type*, double> g;
		std::map<_node_type*, _node_type*> prev;
		std::set<_node_type*> closed;
		using item = std::pair<double, _node_type*>;
		std::priority_queue<item, std::vector<item>, std::greater<item>> open;
		g[start] = 0.0;
		open.push({ heuristic(start), start });
		while (!open.empty())
		{
			auto top = open.top(); open.pop();
			_node_type* u = top.second;
			if (closed.count(u)) continue;
			closed.insert(u);
			if (u == goal) break;   // 可采纳启发式下首次弹出即最优
			auto adj = u->adjacent_list();
			for (xhqm::size k = 0; k < adj.size(); ++k)
			{
				_node_type* v = adj[k];
				if (closed.count(v)) continue;
				const double ng = g[u] + static_cast<double>(weight(u, k));
				auto it = g.find(v);
				if (it == g.end() || ng < it->second)
				{
					g[v] = ng;
					prev[v] = u;
					open.push({ ng + heuristic(v), v });
				}
			}
		}
		return { std::move(g), std::move(prev) };
	}

	//—— 欧拉路径 ——

	//Hierholzer（有向图欧拉路径/回路）。入度出度判定：
	//回路（全部入=出）或路径（恰一个出-入=1 的起点、一个入-出=1 的终点）。
	//返回途经节点序列（长度=边数+1）；不存在（不连通/度数不符）返回空
	template<typename _node_type>
	std::vector<_node_type*> euler_path(const std::vector<_node_type*>& nodes)
	{
		std::map<_node_type*, std::vector<_node_type*>> out;
		std::map<_node_type*, xhqm::index> in_deg;
		xhqm::size total_edges = 0;
		for (auto* u : nodes)
		{
			out[u] = u->adjacent_list();
			total_edges += out[u].size();
		}
		for (auto& kv : out)
			for (auto* v : kv.second)
				++in_deg[v];
		// 度数判定
		_node_type* begin = nullptr;
		xhqm::size pos_deg = 0, neg_deg = 0;
		for (auto* u : nodes)
		{
			xhqm::index od = out[u].size();
			xhqm::index id = in_deg[u];
			if (od == id + 1) { begin = u; ++pos_deg; }
			else if (id == od + 1) { ++neg_deg; }
			else if (od != id) return {};
		}
		if (pos_deg > 1 || neg_deg > 1 || (pos_deg + neg_deg) == 1) return {};
		if (!begin)
			for (auto* u : nodes)   // 回路：任选有出边的节点
				if (!out[u].empty()) { begin = u; break; }
		if (!begin) return {};
		// Hierholzer
		std::vector<_node_type*> stack, path;
		stack.push_back(begin);
		while (!stack.empty())
		{
			_node_type* u = stack.back();
			if (!out[u].empty())
			{
				_node_type* v = out[u].back();
				out[u].pop_back();
				stack.push_back(v);
			}
			else
			{
				path.push_back(u);
				stack.pop_back();
			}
		}
		if (path.size() != total_edges + 1) return {};   // 不连通
		std::reverse(path.begin(), path.end());
		return path;
	}
}

#endif
