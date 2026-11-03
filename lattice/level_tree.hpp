/*
*作者：星辉澪
*时间：乙巳
*用途：层级树（连续存储的紧凑树，层序布局）
* 性质：
* 1、节点数据层序连续存储（std::vector），O(1) 下标访问；
* 2、_child_size == 0：任意叉数，CSR 变长布局（child_first 前缀 + child_list 序列）；
*    _child_size == N：N 叉树，固定步长槽位（child_slots[i*N+k]，npos=空槽）；
* 3、两模式对外 API 统一（if constexpr 分流），与 tree 族 _child_size 语义对齐；
* 4、连续指针接口（nodes/parents/child_first/child_slots）支持序列化与外部消费；
* 5、结构稳定场景设计：emplace 建树 O(N)（中间插入），无中间删除（clear 全清）；
*    频繁增删中间节点请用 child_tree/bind_tree 指针树
*/

#pragma once
#ifndef XHQM_META_LEVEL_TREE
#define XHQM_META_LEVEL_TREE

#include "./struct.h"
#include <iterator>
#include <vector>
#include <stack>
#include <queue>
#include <utility>
#include <type_traits>
#include <stdexcept>

namespace xhqm
{
	//边数据存储辅助：void 边数据以 char 占位（std::conditional 两分支均实例化，vector<void> 非法）
	template<typename _edge_data, typename _enable = void>
	struct level_tree_edge_store { using type = std::vector<_edge_data>; };
	template<>
	struct level_tree_edge_store<void> { using type = char; };
	//边数据引用辅助（void 时 char& 占位，conditional 两分支均实例化，void& 非法）
	template<typename _edge_data, typename _enable = void>
	struct level_tree_edge_ref { using type = _edge_data&; };
	template<>
	struct level_tree_edge_ref<void> { using type = char&; };
	template<typename _edge_data, typename _enable = void>
	struct level_tree_edge_cref { using type = const _edge_data&; };
	template<>
	struct level_tree_edge_cref<void> { using type = const char&; };

	template<typename _node_data, typename _edge_data = void, xhqm::size _child_size = 0>
	class level_tree
	{
	public:
		using node_data = _node_data;
		using edge_data = _edge_data;
		static constexpr bool has_edge = !xhqm::is_same<void, _edge_data>;
	protected:
		std::vector<node_data> m_nodes;        // 节点数据（层序连续，下标即节点号）
		std::vector<xhqm::size> m_parent;      // 父下标（根 = npos）
		// _child_size == 0：任意叉数，CSR 变长
		std::vector<xhqm::size> m_child_first; // 前缀数组（长度 count+1）：i 的孩子区间 [first[i], first[i+1])
		std::vector<xhqm::size> m_child_list;  // 孩子下标序列（与 m_edges 平行）
		// _child_size > 0：N 叉树，固定步长
		std::vector<xhqm::size> m_child_slots; // N*count 槽：i 的第 k 个孩子 = slots[i*N+k]，npos=空槽
		// 边数据（has_edge 时与孩子序列/槽平行；void 时以 char 占位不使用）
		typename level_tree_edge_store<_edge_data>::type m_edges;
		typename level_tree_edge_store<_edge_data>::type m_edge_slots;
	public:
		//—— 容量 ——
		inline xhqm::size size() const noexcept { return m_nodes.size(); };
		inline bool empty() const noexcept { return m_nodes.empty(); };
		inline void reserve(const xhqm::size& n)
		{
			m_nodes.reserve(n);
			m_parent.reserve(n);
			if constexpr (_child_size == 0)
			{
				m_child_first.reserve(n + 1);
			}
			else
			{
				m_child_slots.reserve(n * _child_size);
			}
		};
		void clear() noexcept
		{
			m_nodes.clear();
			m_parent.clear();
			m_child_first.clear();
			m_child_list.clear();
			m_child_slots.clear();
			if constexpr (has_edge)
			{
				m_edges.clear();
				m_edge_slots.clear();
			}
		};

		//删除叶子节点 i（连续布局：下标重映射 O(N)；仅叶子可删，删子树请后序逐个删）
		bool remove_leaf(const xhqm::size& i)
		{
			if (i >= m_nodes.size() || !is_leaf(i)) return false;
			const xhqm::size p = m_parent[i];
			if constexpr (_child_size == 0)
			{
				// 从父的孩子序列中移除 i（与边数据平行）
				if (p != xhqm::npos)
				{
					const xhqm::size b = m_child_first[p];
					const xhqm::size e = m_child_first[p + 1];
					for (xhqm::size pos = b; pos < e; ++pos)
					{
						if (m_child_list[pos] == i)
						{
							m_child_list.erase(m_child_list.begin() + pos);
							if constexpr (has_edge)
								m_edges.erase(m_edges.begin() + pos);
							break;
						}
					}
				}
				// 下标重映射：> i 的引用全部 -1
				for (auto& g : m_child_list) if (g > i) --g;
			}
			else
			{
				// 槽位：父区间置 npos（i 是叶子，自身槽全空）
				if (p != xhqm::npos)
				{
					const xhqm::size base = p * _child_size;
					for (xhqm::size k = 0; k < _child_size; ++k)
						if (m_child_slots[base + k] == i) { m_child_slots[base + k] = xhqm::npos; break; }
				}
				// 收缩：删除 i 的整块槽
				m_child_slots.erase(m_child_slots.begin() + i * _child_size,
					m_child_slots.begin() + (i + 1) * _child_size);
				if constexpr (has_edge)
					m_edge_slots.erase(m_edge_slots.begin() + i * _child_size,
						m_edge_slots.begin() + (i + 1) * _child_size);
				// 下标重映射
				for (auto& g : m_child_slots) if (g != xhqm::npos && g > i) --g;
			}
			// 父下标重映射 + 删除节点
			for (auto& v : m_parent)
				if (v != xhqm::npos && v > i) --v;
			m_parent.erase(m_parent.begin() + i);
			m_nodes.erase(m_nodes.begin() + i);
			if constexpr (_child_size == 0)
			{
				// 前缀重建
				std::vector<xhqm::size> deg(m_nodes.size(), 0);
				for (auto g : m_child_list) ++deg[m_parent[g]];
				m_child_first.assign(m_nodes.size() + 1, 0);
				for (xhqm::size n = 0; n < m_nodes.size(); ++n)
					m_child_first[n + 1] = m_child_first[n] + deg[n];
			}
			return true;
		};

		//—— 建树 ——
		//根节点（仅空树可建，根恒为下标 0）
		template<typename... _args>
		xhqm::size emplace_root(_args&&... args)
		{
			if (!m_nodes.empty()) return xhqm::npos;
			m_nodes.emplace_back(std::forward<_args>(args)...);
			m_parent.push_back(xhqm::npos);
			if constexpr (_child_size == 0)
			{
				m_child_first.assign(2, 0);
			}
			else
			{
				m_child_slots.assign(_child_size, xhqm::npos);
			}
			return 0;
		};
		//在 parent 下追加孩子，返回新节点下标
		template<typename... _args>
		xhqm::size emplace_child(const xhqm::size& parent, _args&&... args)
		{
			if (parent >= m_nodes.size()) return xhqm::npos;
			m_nodes.emplace_back(std::forward<_args>(args)...);
			const xhqm::size idx = m_nodes.size() - 1;
			m_parent.push_back(parent);
			if constexpr (_child_size == 0)
			{
				// 孩子插入父区间末尾（中间插入 O(N)：孩子序列搬移 + 前缀重建）
				const xhqm::size pos = m_child_first[parent + 1];
				m_child_list.insert(m_child_list.begin() + pos, idx);
				if constexpr (has_edge)
				{
					m_edges.insert(m_edges.begin() + pos, _edge_data());
				}
				// 前缀重建
				std::vector<xhqm::size> deg(m_nodes.size(), 0);
				for (auto g : m_child_list) ++deg[m_parent[g]];
				m_child_first.assign(m_nodes.size() + 1, 0);
				for (xhqm::size i = 0; i < m_nodes.size(); ++i)
					m_child_first[i + 1] = m_child_first[i] + deg[i];
			}
			else
			{
				// 固定步长：找父的第一个空槽
				if (m_child_slots.size() < (idx + 1) * _child_size)
					m_child_slots.resize((idx + 1) * _child_size, xhqm::npos);
				if constexpr (has_edge)
				{
					if (m_edge_slots.size() < (idx + 1) * _child_size)
						m_edge_slots.resize((idx + 1) * _child_size);
				}
				const xhqm::size base = parent * _child_size;
				for (xhqm::size k = 0; k < _child_size; ++k)
				{
					if (m_child_slots[base + k] == xhqm::npos)
					{
						m_child_slots[base + k] = idx;
						break;
					}
				}
			}
			return idx;
		};

		//—— 访问 ——
		inline _node_data& node(const xhqm::size& i) { return m_nodes[i]; };
		inline const _node_data& node(const xhqm::size& i) const { return m_nodes[i]; };
		inline xhqm::size parent(const xhqm::size& i) const { return m_parent[i]; };
		//—— 边界检查访问（标准库风格：[] 不查、_at 查，越界抛 std::out_of_range）——
		inline _node_data& node_at(const xhqm::size& i)
		{
			if (i >= m_nodes.size()) throw std::out_of_range("level_tree::node_at");
			return m_nodes[i];
		};
		inline const _node_data& node_at(const xhqm::size& i) const
		{
			if (i >= m_nodes.size()) throw std::out_of_range("level_tree::node_at");
			return m_nodes[i];
		};
		inline xhqm::size parent_at(const xhqm::size& i) const
		{
			if (i >= m_nodes.size()) throw std::out_of_range("level_tree::parent_at");
			return m_parent[i];
		};
		inline bool is_root(const xhqm::size& i) const { return m_parent[i] == xhqm::npos; };
		inline bool is_leaf(const xhqm::size& i) const { return degree(i) == 0; };

		//节点 i 的度（孩子数）
		xhqm::size degree(const xhqm::size& i) const
		{
			if constexpr (_child_size == 0)
			{
				return m_child_first[i + 1] - m_child_first[i];
			}
			else
			{
				xhqm::size d = 0;
				const xhqm::size base = i * _child_size;
				for (xhqm::size k = 0; k < _child_size; ++k)
					if (m_child_slots[base + k] != xhqm::npos) ++d;
				return d;
			}
		};
		//节点 i 的第 k 个孩子（越界/空槽返回 npos）
		xhqm::size child(const xhqm::size& i, const xhqm::size& k) const
		{
			if constexpr (_child_size == 0)
			{
				const xhqm::size b = m_child_first[i];
				const xhqm::size e = m_child_first[i + 1];
				if (b + k >= e) return xhqm::npos;
				return m_child_list[b + k];
			}
			else
			{
				const xhqm::size s = i * _child_size + k;
				if (s >= m_child_slots.size()) return xhqm::npos;
				return m_child_slots[s];
			}
		};
		//节点 i 的全部孩子
		std::vector<xhqm::size> childs(const xhqm::size& i) const
		{
			std::vector<xhqm::size> list;
			const xhqm::size d = degree(i);
			list.reserve(d);
			for (xhqm::size k = 0; k < d; ++k)
				list.push_back(child(i, k));
			return list;
		};
		//节点 i 到第 k 个孩子的边数据（无路径数据时返回 char& 占位，调用被 static_assert 拦截）
		typename level_tree_edge_ref<_edge_data>::type edge(const xhqm::size& i, const xhqm::size& k)
		{
			static_assert(has_edge, "level_tree 无路径数据");
			if constexpr (_child_size == 0)
			{
				return m_edges[m_child_first[i] + k];
			}
			else
			{
				return m_edge_slots[i * _child_size + k];
			}
		};
		typename level_tree_edge_cref<_edge_data>::type edge(const xhqm::size& i, const xhqm::size& k) const
		{
			static_assert(has_edge, "level_tree 无路径数据");
			if constexpr (_child_size == 0)
			{
				return m_edges[m_child_first[i] + k];
			}
			else
			{
				return m_edge_slots[i * _child_size + k];
			}
		};

		//—— 连续指针（序列化 / 外部消费）——
		inline _node_data* nodes() noexcept { return m_nodes.data(); };
		inline const _node_data* nodes() const noexcept { return m_nodes.data(); };
		inline const xhqm::size* parents() const noexcept { return m_parent.data(); };
		// _child_size == 0 模式
		inline const xhqm::size* child_first() const noexcept { return m_child_first.data(); };
		inline const xhqm::size* child_list() const noexcept { return m_child_list.data(); };
		// _child_size > 0 模式
		inline const xhqm::size* child_slots() const noexcept { return m_child_slots.data(); };

		//—— 遍历（下标迭代，深树不爆栈；function_(下标, argv...)）——
		//先序
		template<typename _func_type, typename... _argv_type>
		void iterate_pre(const xhqm::size& start, _func_type&& function_, _argv_type&& ... argv_data_) const
		{
			std::stack<xhqm::size> stack;
			stack.push(start);
			while (!stack.empty())
			{
				const xhqm::size i = stack.top(); stack.pop();
				function_(i, std::forward<_argv_type>(argv_data_)...);
				std::vector<xhqm::size> ch = childs(i);
				for (auto it = ch.rbegin(); it != ch.rend(); ++it)
					stack.push(*it);
			}
		};
		//后序
		template<typename _func_type, typename... _argv_type>
		void iterate_bak(const xhqm::size& start, _func_type&& function_, _argv_type&& ... argv_data_) const
		{
			std::stack<std::pair<xhqm::size, bool>> stack;
			stack.push({ start, false });
			while (!stack.empty())
			{
				auto frame = stack.top(); stack.pop();
				if (frame.second)
				{
					function_(frame.first, std::forward<_argv_type>(argv_data_)...);
					continue;
				}
				stack.push({ frame.first, true });
				std::vector<xhqm::size> ch = childs(frame.first);
				for (auto it = ch.rbegin(); it != ch.rend(); ++it)
					stack.push({ *it, false });
			}
		};
		//中序（叶子直接调；非叶 = mid(首子) → 自身 → mid(其余子)，对齐树族语义）
		template<typename _func_type, typename... _argv_type>
		void iterate_mid(const xhqm::size& start, _func_type&& function_, _argv_type&& ... argv_data_) const
		{
			std::stack<std::pair<xhqm::size, bool>> stack;
			stack.push({ start, false });
			while (!stack.empty())
			{
				auto frame = stack.top(); stack.pop();
				if (frame.second)
				{
					function_(frame.first, std::forward<_argv_type>(argv_data_)...);
					continue;
				}
				std::vector<xhqm::size> ch = childs(frame.first);
				if (ch.empty())
				{
					function_(frame.first, std::forward<_argv_type>(argv_data_)...);
					continue;
				}
				for (xhqm::size k = ch.size() - 1; k >= 1; --k)
					stack.push({ ch[k], false });
				stack.push({ frame.first, true });
				stack.push({ ch[0], false });
			}
		};
		//层序遍历：返回 level 层的节点个数
		template<typename _func_type, typename... _argv_type>
		xhqm::count iterate_level(const xhqm::size& start, const xhqm::index& level, _func_type&& function_, _argv_type&& ... argv_data_) const
		{
			xhqm::count count = 0;
			std::queue<std::pair<xhqm::size, xhqm::index>> queue;
			queue.push({ start, 0 });
			while (!queue.empty())
			{
				auto frame = queue.front(); queue.pop();
				if (frame.second == level)
				{
					function_(frame.first, std::forward<_argv_type>(argv_data_)...);
					++count;
				}
				else if (frame.second > level) break;
				for (auto g : childs(frame.first))
					queue.push({ g, frame.second + 1 });
			}
			return count;
		};
		//逐层遍历整树：返回层数（对齐树族 iterate_bfs 语义）
		template<typename _func_type, typename... _argv_type>
		xhqm::count iterate_bfs(const xhqm::size& start, _func_type&& function_, _argv_type&& ... argv_data_) const
		{
			xhqm::index level = 0;
			while (iterate_level(start, level, function_, std::forward<_argv_type>(argv_data_)...))
			{
				++level;
			}
			return level;
		};

		level_tree() noexcept = default;
		level_tree(level_tree&&) = default;
		level_tree& operator=(level_tree&&) = default;
		level_tree(const level_tree&) = default;
		level_tree& operator=(const level_tree&) = default;

		//—— 迭代器（层序下标序列 0..N-1，解引用节点数据；range-for / xhqm::for_each 可用）——
		class iterator
		{
			level_tree* m_tree;
			xhqm::size m_pos;
		public:
			//迭代器 traits（std::distance/count 等泛型算法可用）
			using value_type = _node_data;
			using reference = _node_data&;
			using pointer = _node_data*;
			using difference_type = std::ptrdiff_t;
			using iterator_category = std::forward_iterator_tag;
			iterator(level_tree* t, xhqm::size p) : m_tree(t), m_pos(p) {}
			_node_data& operator*() { return m_tree->m_nodes[m_pos]; }
			iterator& operator++() { ++m_pos; return *this; }
			iterator operator++(int) { iterator tmp(*this); ++*this; return tmp; }
			bool operator==(const iterator& o) const { return m_pos == o.m_pos; }
			bool operator!=(const iterator& o) const { return m_pos != o.m_pos; }
		};
		class const_iterator
		{
			const level_tree* m_tree;
			xhqm::size m_pos;
		public:
			using value_type = _node_data;
			using reference = const _node_data&;
			using pointer = const _node_data*;
			using difference_type = std::ptrdiff_t;
			using iterator_category = std::forward_iterator_tag;
			const_iterator(const level_tree* t, xhqm::size p) : m_tree(t), m_pos(p) {}
			const _node_data& operator*() const { return m_tree->m_nodes[m_pos]; }
			const_iterator& operator++() { ++m_pos; return *this; }
			const_iterator operator++(int) { const_iterator tmp(*this); ++*this; return tmp; }
			bool operator==(const const_iterator& o) const { return m_pos == o.m_pos; }
			bool operator!=(const const_iterator& o) const { return m_pos != o.m_pos; }
		};
		inline iterator begin() noexcept { return iterator(this, 0); }
		inline iterator end() noexcept { return iterator(this, m_nodes.size()); }
		inline const_iterator begin() const noexcept { return const_iterator(this, 0); }
		inline const_iterator end() const noexcept { return const_iterator(this, m_nodes.size()); }
	};
}

#endif
