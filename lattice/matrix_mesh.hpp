/*
*作者：星辉澪
*时间：乙巳
*用途：矩阵网（邻接矩阵表示的图）
* 性质：
* 1、节点数据连续存储（std::vector），邻接矩阵由 xhqm::matrix 承载（行优先）
* 2、m_matrix(i,j) 表示节点 i 到节点 j 的边（_edge_data，默认 bool：true 有边）
* 3、O(1) 查询任意两节点邻接/边；add_node 扩展矩阵 O(N²)
* 4、邻接矩阵经 xhqm::matrix 获得代数能力（矩阵乘=路径数等），
*    运算走 tensor 后端；关 TENSOR 模块时为 no 串行桩（等价 no 模式）
*/

#pragma once
#ifndef XHQM_META_MATRIX_MESH
#define XHQM_META_MATRIX_MESH

#include "./struct.h"
#include "../algebra/tensor/matrix.h"
#include <iterator>
#include <vector>
#include <utility>
#include <stdexcept>

namespace xhqm
{
	//矩阵网：邻接矩阵表示，节点数据与关系矩阵均连续存储
	template<typename _node_data, typename _edge_data = bool>
	class matrix_mesh
	{
	public:
		using node_data = _node_data;
		using node_data_ref = _node_data&;
		using edge_data = _edge_data;
		using edge_data_ref = _edge_data&;
	protected:
		std::vector<_node_data> m_nodes;     // 节点数据（连续）
		xhqm::matrix<_edge_data> m_matrix;   // 邻接矩阵（N×N 行优先，operator()(i,j) 索引）
		xhqm::size m_count = 0;
	public:
		//—— 节点 ——
		inline xhqm::size size() const noexcept { return m_count; };
		inline bool empty() const noexcept { return m_count == 0; };

		//删除节点 i（矩阵收缩 O(N²)；i 之后节点下标前移）
		bool remove_node(const xhqm::size& i)
		{
			if (i >= m_count) return false;
			const xhqm::size new_n = m_count - 1;
			xhqm::matrix<_edge_data> new_matrix(new_n, new_n);
			for (xhqm::size r = 0, nr = 0; r < m_count; ++r)
			{
				if (r == i) continue;
				for (xhqm::size c = 0, nc = 0; c < m_count; ++c)
				{
					if (c == i) continue;
					new_matrix(nr, nc) = std::move(m_matrix(r, c));
					++nc;
				}
				++nr;
			}
			m_matrix = std::move(new_matrix);
			m_nodes.erase(m_nodes.begin() + i);
			m_count = new_n;
			return true;
		};

		//添加节点，返回节点编号
		xhqm::size add_node(const _node_data& data)
		{
			return emplace_node(data);
		};
		xhqm::size add_node(_node_data&& data)
		{
			return emplace_node(std::move(data));
		};
		template<typename... _args>
		xhqm::size emplace_node(_args&&... args)
		{
			m_nodes.emplace_back(std::forward<_args>(args)...);
			const xhqm::size old_n = m_count;
			const xhqm::size new_n = old_n + 1;
			//扩展邻接矩阵：旧 N×N → 新 (N+1)×(N+1)
			xhqm::matrix<_edge_data> new_matrix(new_n, new_n);
			for (xhqm::size i = 0; i < old_n; ++i)
				for (xhqm::size j = 0; j < old_n; ++j)
					new_matrix(i, j) = std::move(m_matrix(i, j));
			m_matrix = std::move(new_matrix);
			m_count = new_n;
			return old_n;
		};

		//节点数据
		inline _node_data& node(const xhqm::size& i) { return m_nodes[i]; };
		inline const _node_data& node(const xhqm::size& i) const { return m_nodes[i]; };
		//—— 边界检查访问（标准库风格：[] 不查、_at 查，越界抛 std::out_of_range）——
		inline _node_data& node_at(const xhqm::size& i)
		{
			if (i >= m_count) throw std::out_of_range("matrix_mesh::node_at");
			return m_nodes[i];
		};
		inline const _node_data& node_at(const xhqm::size& i) const
		{
			if (i >= m_count) throw std::out_of_range("matrix_mesh::node_at");
			return m_nodes[i];
		};
		inline bool adjacent_at(const xhqm::size& i, const xhqm::size& j) const
		{
			if (i >= m_count || j >= m_count) throw std::out_of_range("matrix_mesh::adjacent_at");
			return static_cast<bool>(m_matrix(i, j));
		};
		inline _edge_data& edge_at(const xhqm::size& i, const xhqm::size& j)
		{
			if (i >= m_count || j >= m_count) throw std::out_of_range("matrix_mesh::edge_at");
			return m_matrix(i, j);
		};
		inline const _edge_data& edge_at(const xhqm::size& i, const xhqm::size& j) const
		{
			if (i >= m_count || j >= m_count) throw std::out_of_range("matrix_mesh::edge_at");
			return m_matrix(i, j);
		};

		//—— 边 ——
		//添加/设置边 i→j（bool 时 set 为 true）
		void add_edge(const xhqm::size& i, const xhqm::size& j)
		{
			m_matrix(i, j) = _edge_data(true);
		};
		//添加/设置边 i→j（带边数据）
		void add_edge(const xhqm::size& i, const xhqm::size& j, const _edge_data& e)
		{
			m_matrix(i, j) = e;
		};
		void add_edge(const xhqm::size& i, const xhqm::size& j, _edge_data&& e)
		{
			m_matrix(i, j) = std::move(e);
		};
		//移除边 i→j
		void remove_edge(const xhqm::size& i, const xhqm::size& j)
		{
			m_matrix(i, j) = _edge_data();
		};

		//是否邻接（边数据转 bool：非零为真）
		inline bool adjacent(const xhqm::size& i, const xhqm::size& j) const
		{
			return static_cast<bool>(m_matrix(i, j));
		};
		//边数据
		inline _edge_data& edge(const xhqm::size& i, const xhqm::size& j) { return m_matrix(i, j); };
		inline const _edge_data& edge(const xhqm::size& i, const xhqm::size& j) const { return m_matrix(i, j); };

		//—— 连续数据 ——
		inline _node_data* nodes() noexcept { return m_nodes.data(); };
		inline const _node_data* nodes() const noexcept { return m_nodes.data(); };
		// 邻接矩阵裸指针：bool 边数据无连续内存，返回 nullptr（store<bool> 约定）
		inline _edge_data* matrix() noexcept { return m_matrix.data(); };
		inline const _edge_data* matrix() const noexcept { return m_matrix.data(); };
		// 邻接矩阵引用（供代数运算：矩阵乘=路径数等；需 tensor 模块或 no 后端）
		inline xhqm::matrix<_edge_data>& matrix_ref() noexcept { return m_matrix; };
		inline const xhqm::matrix<_edge_data>& matrix_ref() const noexcept { return m_matrix; };

		//—— 遍历 ——
		//节点 i 的邻接节点编号列表
		std::vector<xhqm::size> adjacent_list(const xhqm::size& i) const
		{
			std::vector<xhqm::size> list;
			for (xhqm::size j = 0; j < m_count; ++j)
				if (adjacent(i, j)) list.push_back(j);
			return list;
		};

		//节点 i 的出度
		xhqm::size out_degree(const xhqm::size& i) const
		{
			xhqm::size d = 0;
			for (xhqm::size j = 0; j < m_count; ++j)
				if (adjacent(i, j)) ++d;
			return d;
		};
		//节点 i 的入度
		xhqm::size in_degree(const xhqm::size& i) const
		{
			xhqm::size d = 0;
			for (xhqm::size k = 0; k < m_count; ++k)
				if (adjacent(k, i)) ++d;
			return d;
		};

		//—— 可达性 ——
		//BFS 可达：从 i 能否经有向边到达 j（非零边=有边；自身恒可达）
		bool reachable(const xhqm::size& i, const xhqm::size& j) const
		{
			if (i == j) return true;
			std::vector<bool> visited(m_count, false);
			std::vector<xhqm::size> queue;
			queue.push_back(i);
			visited[i] = true;
			xhqm::size head = 0;
			while (head < queue.size())
			{
				xhqm::size u = queue[head++];
				for (xhqm::size v = 0; v < m_count; ++v)
				{
					if (!visited[v] && adjacent(u, v))
					{
						if (v == j) return true;
						visited[v] = true;
						queue.push_back(v);
					}
				}
			}
			return false;
		};
		//i 出发可达的节点集合（BFS 分量，含 i 自身）
		std::vector<xhqm::size> reachable_component(const xhqm::size& i) const
		{
			std::vector<bool> visited(m_count, false);
			std::vector<xhqm::size> queue;
			std::vector<xhqm::size> comp;
			queue.push_back(i);
			visited[i] = true;
			xhqm::size head = 0;
			while (head < queue.size())
			{
				xhqm::size u = queue[head++];
				comp.push_back(u);
				for (xhqm::size v = 0; v < m_count; ++v)
				{
					if (!visited[v] && adjacent(u, v))
					{
						visited[v] = true;
						queue.push_back(v);
					}
				}
			}
			return comp;
		};

		matrix_mesh() noexcept = default;
		matrix_mesh(matrix_mesh&& other) noexcept
			: m_nodes(std::move(other.m_nodes)), m_matrix(std::move(other.m_matrix)), m_count(other.m_count)
		{
			other.m_count = 0;   // 源计数清零，避免 size() 与数据不一致的危险状态
		}
		matrix_mesh& operator=(matrix_mesh&& other) noexcept
		{
			if (this != &other)
			{
				m_nodes = std::move(other.m_nodes);
				m_matrix = std::move(other.m_matrix);
				m_count = other.m_count;
				other.m_count = 0;
			}
			return *this;
		}
		matrix_mesh(const matrix_mesh&) = default;
		matrix_mesh& operator=(const matrix_mesh&) = default;

		//—— 迭代器（节点下标序列 0..N-1，解引用节点数据；range-for / xhqm::for_each 可用）——
		class iterator
		{
			matrix_mesh* m_mesh;
			xhqm::size m_pos;
		public:
			//迭代器 traits（std::distance/count 等泛型算法可用）
			using value_type = _node_data;
			using reference = _node_data&;
			using pointer = _node_data*;
			using difference_type = std::ptrdiff_t;
			using iterator_category = std::forward_iterator_tag;
			iterator(matrix_mesh* m, xhqm::size p) : m_mesh(m), m_pos(p) {}
			_node_data& operator*() { return m_mesh->m_nodes[m_pos]; }
			iterator& operator++() { ++m_pos; return *this; }
			iterator operator++(int) { iterator tmp(*this); ++*this; return tmp; }
			bool operator==(const iterator& o) const { return m_pos == o.m_pos; }
			bool operator!=(const iterator& o) const { return m_pos != o.m_pos; }
		};
		class const_iterator
		{
			const matrix_mesh* m_mesh;
			xhqm::size m_pos;
		public:
			using value_type = _node_data;
			using reference = const _node_data&;
			using pointer = const _node_data*;
			using difference_type = std::ptrdiff_t;
			using iterator_category = std::forward_iterator_tag;
			const_iterator(const matrix_mesh* m, xhqm::size p) : m_mesh(m), m_pos(p) {}
			const _node_data& operator*() const { return m_mesh->m_nodes[m_pos]; }
			const_iterator& operator++() { ++m_pos; return *this; }
			const_iterator operator++(int) { const_iterator tmp(*this); ++*this; return tmp; }
			bool operator==(const const_iterator& o) const { return m_pos == o.m_pos; }
			bool operator!=(const const_iterator& o) const { return m_pos != o.m_pos; }
		};
		inline iterator begin() noexcept { return iterator(this, 0); }
		inline iterator end() noexcept { return iterator(this, m_count); }
		inline const_iterator begin() const noexcept { return const_iterator(this, 0); }
		inline const_iterator end() const noexcept { return const_iterator(this, m_count); }
	};
}

#endif
