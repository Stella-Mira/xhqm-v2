// structure 库全量测试：链 / 树 / 网 + 方向变体 + 图算法。
#include "../lattice/list.hpp"
#include "../lattice/tree.hpp"
#include "../lattice/mesh.hpp"
#include "../lattice/arrow_mesh.hpp"
#include "../lattice/bind_mesh.hpp"
#include "../lattice/matrix_mesh.hpp"
#include "../lattice/next_list.hpp"
#include "../lattice/prev_list.hpp"
#include "../lattice/bind_list.hpp"
#include "../lattice/ring_list.hpp"
#include "../lattice/level_tree.hpp"
#include "test.h"
#include <vector>
#include <algorithm>
#include <iterator>
#include <type_traits>

using namespace xhqm;

TEST_CASE("test_list_basic")
{
	// derive_list 双向链：a <-> b <-> c
	class node : public derive_list<node> { public: int id = 0; node(int i = 0) : id(i) {} };
	node a(0), b(1), c(2);
	a.set_next_ptr(&b);
	b.set_next_ptr(&c);
	CHECK(a.next_ptr() == &b);
	CHECK(b.prev_ptr() == &a);
	CHECK(c.prev_ptr() == &b);
	CHECK(b.next_ptr() == &c);
	CHECK(a.first_ptr() == &a);
	CHECK(a.last_ptr() == &c);
	CHECK(c.in_list(&a));
}

TEST_CASE("test_tree_basic")
{
	// derive_tree：root -> (child1, child2)
	class node : public derive_tree<node> { public: int id = 0; node(int i = 0) : id(i) {} };
	node root(0), c1(1), c2(2);
	root.add_child_ptr(&c1);
	root.add_child_ptr(&c2);
	CHECK(root.degree() == 2);
	CHECK(c1.parent_ptr() == &root);
	CHECK(c2.parent_ptr() == &root);
	CHECK(root.is_root());
	CHECK(c1.is_leaf());
	CHECK(c1.is_relatives(&c2));
	CHECK(c1.is_sibling(&c2));
	int cnt = 0;
	root.iterate_bfs([&](node*) { ++cnt; });
	CHECK(cnt == 3);
}

TEST_CASE("test_mesh_basic")
{
	// derive_mesh 无向：a-b-c 连通，d 孤立（自动双向，重复 add 被拒）
	class node : public derive_mesh<node> { public: int id = 0; node(int i = 0) : id(i) {} };
	node a(0), b(1), c(2), d(3);
	a.add_adjacent_ptr(&b); b.add_adjacent_ptr(&a);
	b.add_adjacent_ptr(&c); c.add_adjacent_ptr(&b);

	CHECK(a.degree() == 1);
	CHECK(b.degree() == 2);
	CHECK(d.is_isolated());
	CHECK(a.is_reachable(&c));
	CHECK(!a.is_reachable(&d));
	CHECK(a.connected_size() == 3);
	CHECK(a.shortest_path(&c) == 2);
	CHECK(a.shortest_path(&d) == xhqm::npos);

	// 拓扑排序（DAG x->y->z，用有向网 arrow_node）
	using anode = _graph_::arrow_node<int>;
	anode x(0), y(1), z(2);
	x.add_adjacent_ptr(&y);
	y.add_adjacent_ptr(&z);
	std::vector<anode*> dag = { &z, &x, &y };
	auto order = xhqm::topological_sort(dag);
	CHECK((order.size() == 3 && order[0] == &x && order[1] == &y && order[2] == &z));

	// 强连通分量（p<->q 环 + r 孤立）
	node p(0), q(1), r(2);
	p.add_adjacent_ptr(&q); q.add_adjacent_ptr(&p);
	std::vector<node*> all = { &p, &q, &r };
	auto sccs = xhqm::strongly_connected_components(all);
	bool ok = (sccs.size() == 2);
	for (auto& comp : sccs)
	{
		bool hp = false, hq = false;
		for (auto* n : comp) { if (n == &p) hp = true; if (n == &q) hq = true; }
		if (hp) ok = ok && hq;
	}
	CHECK(ok);
}

TEST_CASE("test_mesh_variants")
{
	// 单向网：a->b，仅 a 邻接 b
	using onode = _graph_::arrow_node<int>;
	onode a(0), b(1);
	a.add_adjacent_ptr(&b);
	CHECK(a.adjacent_index(&b) != xhqm::npos);
	CHECK(b.adjacent_index(&a) == xhqm::npos);
	CHECK((a.is_reachable(&b) && !b.is_reachable(&a)));

	// 双向网：a<->b 自动双向维护
	using tnode = _graph_::bind_mesh_node<int>;
	tnode ta(0), tb(1);
	ta.add_adjacent_ptr(&tb);
	CHECK(ta.adjacent_index(&tb) != xhqm::npos);
	CHECK(tb.adjacent_index(&ta) != xhqm::npos);
	ta.sub_adjacent_ptr(&tb);
	CHECK(ta.adjacent_index(&tb) == xhqm::npos);
	CHECK(tb.adjacent_index(&ta) == xhqm::npos);

	// 双向网析构自动断开邻接
	{
		auto* pa = new tnode(0);
		auto* pb = new tnode(1);
		pa->add_adjacent_ptr(pb);
		CHECK(pb->adjacent_index(pa) != xhqm::npos);
		delete pa;  // pa 析构断开 pb 的反向 edge
		CHECK(pb->adjacent_index(pa) == xhqm::npos);  // pb 不再邻接 pa
		delete pb;
	}
}

TEST_CASE("test_mesh_containers")
{
	// arrow_mesh 带游标（curr）
	xhqm::arrow_mesh<int> om;
	auto* oa = om.add_node(0);       // 首个节点自动成为游标
	auto* ob = om.add_node(1);       // to_curr=false，游标不动
	CHECK(om.size() == 2);
	CHECK(om.curr() == 0);           // 游标在 oa
	oa->add_adjacent_ptr(ob);        // oa -> ob
	CHECK(om.curr_adjacent(0));      // 游标自 oa 移到 ob（第 0 个邻接）
	CHECK(om.curr() == 1);           // 游标在 ob

	// bind_mesh 带游标
	xhqm::bind_mesh<int> tm;
	auto* ta = tm.add_node(0);
	auto* tb = tm.add_node(1, true); // to_curr=true 移动游标
	ta->add_adjacent_ptr(tb);
	CHECK(tb->adjacent_index(ta) != xhqm::npos);
	CHECK(tm.curr() == 1);           // 游标在 tb
}

TEST_CASE("test_matrix_mesh")
{
	// 矩阵网：节点连续 + 邻接矩阵连续
	xhqm::matrix_mesh<int, bool> m;
	m.add_node(10);
	m.add_node(20);
	m.add_node(30);
	m.add_edge(0, 1);
	m.add_edge(1, 2);

	CHECK(m.size() == 3);
	CHECK((m.adjacent(0, 1) && !m.adjacent(1, 0)));
	CHECK(m.adjacent(1, 2));
	CHECK(m.out_degree(1) == 1);
	CHECK(m.in_degree(1) == 1);
	auto l = m.adjacent_list(0);
	CHECK((l.size() == 1 && l[0] == 1));
	CHECK((m.node(0) == 10 && m.nodes()[1] == 20));

	// 扩展后旧边保留
	m.add_node(40);
	CHECK((m.size() == 4 && m.adjacent(0, 1) && m.adjacent(1, 2)));

	// 权重边
	xhqm::matrix_mesh<int, float> w;
	w.add_node(0); w.add_node(1); w.add_node(2);
	w.add_edge(0, 1, 0.5f);
	CHECK((w.edge(0, 1) == 0.5f && w.adjacent(0, 1) && !w.adjacent(1, 2)));
	CHECK(w.matrix()[1] == 0.5f);  // 矩阵连续，i*N+j = 0*3+1 = 1
}

TEST_CASE("test_iterator_traits")
{
	//—— traits 静态断言：五件套类型必须正确 ——
	using lit = decltype(xhqm::next_list<int>().begin());
	static_assert(std::is_same<std::iterator_traits<lit>::value_type, int>::value, "数据族 value_type 应为节点数据");
	static_assert(std::is_same<std::iterator_traits<lit>::difference_type, std::ptrdiff_t>::value, "difference_type 须有符号");
	static_assert(std::is_same<std::iterator_traits<lit>::iterator_category, std::forward_iterator_tag>::value, "category 应为 forward");

	//—— 链族：distance / count / find / advance / vector 范围构造 ——
	xhqm::next_list<int> nl = { 1, 2, 3, 4 };
	CHECK(std::distance(nl.begin(), nl.end()) == 4);
	CHECK(std::count(nl.begin(), nl.end(), 3) == 1);
	CHECK(std::find(nl.begin(), nl.end(), 2) != nl.end());
	auto nit = nl.begin();
	std::advance(nit, 2);
	CHECK(*nit == 3);
	std::vector<int> nv(nl.begin(), nl.end());
	CHECK((nv.size() == 4 && nv[0] == 1 && nv[3] == 4));

	xhqm::prev_list<int> pl = { 1, 2, 3 };
	CHECK(std::distance(pl.begin(), pl.end()) == 3);
	xhqm::bind_list<int> bl = { 1, 2, 3 };
	CHECK(std::distance(bl.begin(), bl.end()) == 3);
	xhqm::ring_list<int> rl = { 1, 2, 3 };
	CHECK(std::distance(rl.begin(), rl.end()) == 3);

	//—— 树族：BFS（默认）/DFS/pre/mid/bak 迭代器 ——
	class tnode : public derive_tree<tnode> { public: int id = 0; tnode(int i = 0) : id(i) {} };
	tnode root(0), c1(1), c2(2);
	root.add_child_ptr(&c1);
	root.add_child_ptr(&c2);
	static_assert(std::is_same<std::iterator_traits<decltype(root.begin())>::value_type, tnode*>::value, "void 数据族 value_type 应为 node*");
	CHECK(std::distance(root.begin(), root.end()) == 3);   // tree_iterator = BFS
	CHECK(std::distance(xhqm::tree_dfs_iterator<tnode>(&root), xhqm::tree_dfs_iterator<tnode>(nullptr)) == 3);
	CHECK(std::distance(xhqm::tree_pre_iterator<tnode>(&root), xhqm::tree_pre_iterator<tnode>(nullptr)) == 3);
	CHECK(std::distance(xhqm::tree_mid_iterator<tnode, 1>(&root), xhqm::tree_mid_iterator<tnode, 1>(nullptr)) == 3);
	CHECK(std::distance(xhqm::tree_bak_iterator<tnode>(&root), xhqm::tree_bak_iterator<tnode>(nullptr)) == 3);
	std::vector<tnode*> nodes(root.begin(), root.end());
	CHECK((nodes.size() == 3 && nodes[0] == &root));

	//—— 网族：DFS（默认）/BFS 迭代器（带 visited，环安全） ——
	class mnode : public derive_mesh<mnode> { public: int id = 0; mnode(int i = 0) : id(i) {} };
	mnode ma(0), mb(1), mc(2), md(3);
	ma.add_adjacent_ptr(&mb);
	mb.add_adjacent_ptr(&mc);
	mb.add_adjacent_ptr(&ma);   // 成环，visited 应防死循环
	CHECK(std::distance(ma.begin(), ma.end()) == 3);
	CHECK(std::distance(xhqm::mesh_bfs_iterator<mnode>(&ma), xhqm::mesh_bfs_iterator<mnode>(nullptr)) == 3);

	//—— 连续族：level_tree / matrix_mesh 内嵌迭代器 ——
	xhqm::level_tree<int> lt;
	lt.emplace_root(0);
	lt.emplace_child(0, 1);
	lt.emplace_child(0, 2);
	CHECK(std::distance(lt.begin(), lt.end()) == 3);
	int lsum = 0;
	for (auto it = lt.begin(); it != lt.end(); ++it) lsum += *it;
	CHECK(lsum == 3);

	xhqm::matrix_mesh<int, bool> mm;
	mm.add_node(10);
	mm.add_node(20);
	CHECK(std::distance(mm.begin(), mm.end()) == 2);
	std::vector<int> mv(mm.begin(), mm.end());
	CHECK((mv.size() == 2 && mv[0] == 10 && mv[1] == 20));
}
