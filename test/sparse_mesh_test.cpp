// sparse_mesh 语法/接口验证（仅编译，不链接）
#include "../lattice/sparse_mesh.hpp"

#include <cassert>
#include <cstdio>

int main()
{
	using xhqm::sparse_mesh;

	// —— bool 边（默认）——
	{
		sparse_mesh<int> g;                       // _edge_data = bool
		g.add_node(10);
		g.add_node(20);
		g.add_node(30);
		g.add_edge(0, 1);
		g.add_edge(1, 2);
		g.add_edge(0, 1);                          // 重复，index 后合并为 true

		assert(g.size() == 3);
		assert(g.nnz() == 3);
		assert(g.adjacent(0, 1));
		assert(!g.adjacent(2, 0));
		assert(g.out_degree(0) == 1);              // 合并后唯一邻居
		assert(g.in_degree(1) == 1);

		auto l = g.adjacent_list(0);
		assert(l.size() == 1 && l[0] == 1);
		assert(g.reachable(0, 2));
		assert(g.reachable_component(0).size() == 3);

		int sum = 0;
		for (auto& v : g) sum += v;                 // 节点迭代器
		assert(sum == 60);
	}

	// —— float 边（权重，重复累加）——
	{
		sparse_mesh<int, float> g;
		g.add_node(1);
		g.add_node(2);
		g.add_node(3);
		g.add_edge(0, 1, 0.5f);
		g.add_edge(1, 2, 1.0f);
		g.add_edge(0, 1, 0.3f);                    // 与 0.5 合并为 0.8

		assert(g.nnz() == 3);
		assert(g.edge_at(0, 1) > 0.79f && g.edge_at(0, 1) < 0.81f);  // 触发 index + 二分定位
		assert(g.edge(1, 2) == 1.0f);

		g.set_edge(0, 1, 9.0f);                    // 覆盖
		assert(g.edge_at(0, 1) == 9.0f);
		g.set_edge(0, 2, 4.0f);                    // 不存在则追加
		assert(g.adjacent(0, 2));

		g.remove_edge(0, 2);
		assert(!g.adjacent(0, 2));
		assert(g.nnz() == 2);

		// 导出 sparse_matrix 并做 SpMV（不链接，仅编译期验证调用）
		auto sm = g.to_sparse_matrix();
		assert(sm.rows() == 3 && sm.cols() == 3 && sm.nnz() == 2);
		(void)sm;
	}

	// —— 边遍历 + 移动语义 ——
	{
		sparse_mesh<int, unsigned> g;
		g.add_node(0);
		g.add_node(1);
		g.add_edge(0, 1, 5u);
		unsigned total = 0;
		g.for_each_edge([&](xhqm::size s, xhqm::size t, unsigned v) {
			(void)s; (void)t; total += v;
		});
		assert(total == 5u);

		sparse_mesh<int, unsigned> h = std::move(g);
		assert(h.size() == 2 && h.nnz() == 1);
		assert(g.size() == 0 && g.nnz() == 0);     // 移动后源清零
	}

	std::puts("sparse_mesh syntax OK");
	return 0;
}
