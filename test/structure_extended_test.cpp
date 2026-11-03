// structure 扩展测试：bind/next/prev/ring_list、child/parent_tree、queues、stacks
#include "../lattice/bind_list.hpp"
#include "../lattice/next_list.hpp"
#include "../lattice/prev_list.hpp"
#include "../lattice/ring_list.hpp"
#include "../lattice/child_tree.hpp"
#include "../lattice/parent_tree.hpp"
#include "../lattice/bind_tree.hpp"
#include "../lattice/queues.h"
#include "../lattice/stacks.h"
#include "test.h"
#include <string>
#include <vector>

using namespace xhqm;

TEST_CASE("test_bind_list")
{
	bind_list<int> l;
	CHECK(l.empty() == true);          // 空时应为 true（可能反了）
	l.push_back(1);
	l.push_back(2);
	l.push_back(3);
	CHECK(l.size() == 3);
	CHECK(l.empty() == false);         // 非空应为 false
	CHECK(l.front() == 1);
	CHECK(l.back() == 3);

	int sum = 0;
	for (auto& v : l) sum += v;
	CHECK(sum == 6);
}

TEST_CASE("test_next_list")
{
	next_list<int> l;
	CHECK(l.empty() == true);
	l.push_back(1);
	l.push_back(2);
	l.push_back(3);
	CHECK(l.size() == 3);
	CHECK(l.front() == 1);
	CHECK(l.back() == 3);

	// 从头遍历到尾
	int sum = 0;
	for (auto& v : l) sum += v;
	CHECK(sum == 6);

	// next() 移动
	l.curr_star();
	CHECK(l.curr() == 1);
	bool moved = l.next();
	CHECK(moved);                        // 应能后移（可能失败）
	if (moved) CHECK(l.curr() == 2);
}

TEST_CASE("test_prev_list")
{
	prev_list<int> l;
	CHECK(l.empty() == true);
	l.push_front(1);
	l.push_front(2);
	l.push_front(3);
	CHECK(l.size() == 3);
	CHECK(l.front() == 3);   // front 是 star
	CHECK(l.back() == 1);    // back 是 stop

	int sum = 0;
	for (auto& v : l) sum += v;
	CHECK(sum == 6);
}

TEST_CASE("test_ring_list")
{
	ring_list<int> l;
	CHECK(l.empty() == true);
	l.push_next(1);
	l.push_next(2);
	l.push_next(3);
	CHECK(l.size() == 3);
	// 注：ring_iterator 缺 operator++，无法 range-for（记录为缺陷）
}

TEST_CASE("test_child_tree")
{
	child_tree<int> t;
	CHECK(t.empty() == true);
	t.push_child(1);         // root
	t.push_child(2, true);   // root 的子节点，移动到子
	t.push_child(3, false);  // 当前(2)的子节点
	CHECK(t.size() == 3);
	CHECK(t.root() == 1);

	int cnt = 0;
	t.curr_root();                      // traverse 自 curr 出发，先回根
	t.traverse([&](auto) { ++cnt; });   // child_tree 的 traverse 传节点指针（非 data，与 parent_tree 不一致）
	CHECK(cnt == 3);
}

TEST_CASE("test_parent_tree")
{
	parent_tree<int> t;
	CHECK(t.empty() == true);
	t.push_child(1);         // root
	t.push_child(2, true);   // root 的子节点，移动到子
	t.push_child(3, false);  // 当前(2)的子节点
	CHECK(t.size() == 3);
	CHECK(t.root() == 1);
	CHECK(t.leaf_size() == 1);

	int cnt = 0;
	t.traverse([&](auto) { ++cnt; });   // 树族 traverse 统一传节点指针
	CHECK(cnt == 3);
}

TEST_CASE("test_bind_tree")
{
	bind_tree<int> t;
	CHECK(t.empty() == true);
	t.push_child(1);         // root
	t.push_child(2, true);   // root 的子节点，移动到子
	t.push_child(3, false);  // 当前(2)的子节点
	CHECK(t.size() == 3);
	CHECK(t.root() == 1);

	// 双向移动：自 2 上移到 1，再下移到 2
	CHECK(t.to_parent());    // curr: 2 -> 1
	CHECK(t.curr() == 1);
	CHECK(t.to_child(0));    // curr: 1 -> 2
	CHECK(t.curr() == 2);

	// 插入父节点（1 <- 4 <- 2），curr 仍指向 2
	t.push_parent(4);
	CHECK(t.size() == 4);
	CHECK(t.root() == 1);    // 1 仍是根
	CHECK(t.to_parent());    // 2 -> 4
	CHECK(t.curr() == 4);
	CHECK(t.to_parent());    // 4 -> 1
	CHECK(t.curr() == 1);
	CHECK(t.is_root());

	// 深度遍历计数（自根出发应到全部 4 个节点）
	int cnt = 0;
	t.curr_root();
	t.traverse([&](auto) { ++cnt; });
	CHECK(cnt == 4);

	// 广度迭代器 range-for
	int sum = 0;
	t.curr_root();
	for (auto& v : t) sum += v;
	CHECK(sum == 1 + 2 + 3 + 4);
}

TEST_CASE("test_bind_tree_edge")
{
	// 带边数据的双向树：父子共享同一条边对象
	bind_tree<int, int> t;
	t.push_child(1);        // root
	t.push_child(2, true);  // 1 -> 2，curr=2
	t.edge_of_parent() = 777;   // 写入 (1,2) 这条共享边
	CHECK(t.to_parent());   // curr: 2 -> 1
	CHECK(t.edge_of_child(0) == 777);  // 父节点读同一条边

	// pop_child：弹出 1 的子节点子树
	t.push_child(3, false); // curr=1，添加子节点 3（不移动）
	CHECK(t.size() == 3);
	CHECK(t.pop_child(1));  // 弹出序号 1 的子节点（3）
	CHECK(t.size() == 2);
}

TEST_CASE("test_queues")
{
	queues<int> q;
	int v = 0;
	int a1 = 1, a2 = 2, a3 = 3;
	q.set(a1); q.set(a2); q.set(a3);
	CHECK((q.get(v) && v == 1));   // 先进先出
	CHECK((q.get(v) && v == 2));
	CHECK((q.get(v) && v == 3));
	CHECK(!q.get(v));            // 空
}

TEST_CASE("test_stacks")
{
	stacks<int> s;
	int v = 0;
	int a1 = 1, a2 = 2, a3 = 3;
	s.set(a1); s.set(a2); s.set(a3);
	CHECK((s.get(v) && v == 3));   // 先进后出
	CHECK((s.get(v) && v == 2));
	CHECK((s.get(v) && v == 1));
	CHECK(!s.get(v));
}
