/*
*作者：星辉澪
*时间：乙巳
*用途：稀疏网（COO 三元组 + 惰性 CSR 行索引的图）
* 性质：
* 1、节点数据连续存储（std::vector），边为 COO 三元组：行=源（pre）、列=目标（post）、值
* 2、add_edge O(1) 追加（不查重）；index() 按 (row,col) 排序、合并重复边、建 CSR 行偏移与入度
* 3、重复边合并规则：bool 取或（存在即真），其余类型累加（xhqm::sum）——契合「多突触聚合」语义
* 4、出度/入度 O(1)（index 后）；邻接遍历走 CSR O(出度)；单边定位二分 O(log 出度)
* 5、to_sparse_matrix() 导出 tensor_base::sparse_matrix（COO），复用其 SpMV 后端加速
* 6、与 matrix_mesh 的差异：邻接矩阵是稠密 O(N²)，本类是稀疏 O(nnz)——连接组等大稀疏图专用
* 7、需建索引的查询（度数/邻接表/可达/边引用）为非 const（惰性触发 index）；纯只读（邻接判定/遍历）为 const
*/

#pragma once
#ifndef XHQM_META_SPARSE_MESH
#define XHQM_META_SPARSE_MESH

#include "./struct.h"
#include "../algebra/tensor/sparse.h"
#include "../algebra/mathf.h"
#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace xhqm
{
	//稀疏网：COO 三元组表示的图，节点数据连续，边稀疏存储
	template<typename _node_data, typename _edge_data = bool>
	class sparse_mesh
	{
	public:
		using node_data = _node_data;
		using node_data_ref = _node_data&;
		using edge_data = _edge_data;
		using edge_data_ref = _edge_data&;
	protected:
		std::vector<_node_data> m_nodes;     // 节点数据（连续）
		std::vector<xhqm::index> m_row;      // 非零边：源节点（pre）
		std::vector<xhqm::index> m_col;      // 非零边：目标节点（post）
		std::vector<_edge_data> m_val;       // 非零边：值
		std::vector<xhqm::size> m_row_ptr;   // CSR 行偏移（N+1），index 后有效
		std::vector<xhqm::size> m_in_deg;    // 入度（index 后有效）
		bool m_indexed = false;              // CSR 索引是否最新
		xhqm::size m_count = 0;              // 节点数

		//—— 建索引：排序 + 合并重复边 + CSR 行偏移 + 入度（非 const，原地整理）——
		inline void index()
		{
			if (m_indexed) return;
			const xhqm::size e = m_row.size();

			// 1. 按 (row, col) 排序（索引排序，再重排三个并行数组）
			std::vector<xhqm::size> perm(e);
			for (xhqm::size k = 0; k < e; ++k) perm[k] = k;
			std::sort(perm.begin(), perm.end(), [&](const xhqm::size& a, const xhqm::size& b)
			{
				return m_row[a] < m_row[b] || (m_row[a] == m_row[b] && m_col[a] < m_col[b]);
			});
			std::vector<xhqm::index> nr(e), nc(e);
			std::vector<_edge_data> nv(e);
			for (xhqm::size k = 0; k < e; ++k)
			{
				nr[k] = m_row[perm[k]];
				nc[k] = m_col[perm[k]];
				nv[k] = m_val[perm[k]];
			}

			// 2. 合并重复 (row, col)：bool 取或，其余累加
			xhqm::size w = 0;
			for (xhqm::size k = 0; k < e; ++k)
			{
				if (w > 0 && nr[w - 1] == nr[k] && nc[w - 1] == nc[k])
				{
					if constexpr (std::is_same_v<_edge_data, bool>)
						nv[w - 1] = nv[w - 1] || nv[k];
					else
						nv[w - 1] = xhqm::sum(nv[w - 1], nv[k]);
				}
				else
				{
					nr[w] = nr[k];
					nc[w] = nc[k];
					nv[w] = nv[k];
					++w;
				}
			}
			nr.resize(w); nc.resize(w); nv.resize(w);
			m_row = std::move(nr);
			m_col = std::move(nc);
			m_val = std::move(nv);

			// 3. CSR 行偏移（前缀和）
			m_row_ptr.assign(m_count + 1, 0);
			for (xhqm::size k = 0; k < w; ++k)
				++m_row_ptr[m_row[k] + 1];
			for (xhqm::size i = 1; i <= m_count; ++i)
				m_row_ptr[i] += m_row_ptr[i - 1];

			// 4. 入度（合并后唯一邻居计数）
			m_in_deg.assign(m_count, 0);
			for (xhqm::size k = 0; k < w; ++k)
				++m_in_deg[m_col[k]];

			m_indexed = true;
		};

		//—— 单边定位（index 后二分 O(log 出度)，未 index 线性 O(nnz)；纯只读，可 const）——
		inline xhqm::size find_edge(const xhqm::size& i, const xhqm::size& j) const
		{
			if (i >= m_count || j >= m_count) return xhqm::npos;
			if (m_indexed)
			{
				xhqm::size lo = m_row_ptr[i], hi = m_row_ptr[i + 1];
				while (lo < hi)
				{
					const xhqm::size mid = lo + (hi - lo) / 2;
					if (m_col[mid] < j) lo = mid + 1;
					else hi = mid;
				}
				if (lo < m_row_ptr[i + 1] && m_col[lo] == j) return lo;
				return xhqm::npos;
			}
			for (xhqm::size k = 0; k < m_row.size(); ++k)
				if (m_row[k] == i && m_col[k] == j) return k;
			return xhqm::npos;
		};
	public:
		//—— 节点 ——
		inline xhqm::size size() const noexcept { return m_count; };
		inline bool empty() const noexcept { return m_count == 0; };

		xhqm::size add_node(const _node_data& data) { return emplace_node(data); };
		xhqm::size add_node(_node_data&& data) { return emplace_node(std::move(data)); };
		template<typename... _args>
		xhqm::size emplace_node(_args&&... args)
		{
			m_nodes.emplace_back(std::forward<_args>(args)...);
			m_indexed = false;
			return m_count++;
		};

		//删除节点 i（O(nnz)：删除涉及 i 的边，i 之后下标前移）
		bool remove_node(const xhqm::size& i)
		{
			if (i >= m_count) return false;
			xhqm::size w = 0;
			for (xhqm::size k = 0; k < m_row.size(); ++k)
			{
				if (m_row[k] == i || m_col[k] == i) continue;      // 丢弃涉及 i 的边
				m_row[w] = m_row[k] > i ? m_row[k] - 1 : m_row[k]; // 重编号
				m_col[w] = m_col[k] > i ? m_col[k] - 1 : m_col[k];
				m_val[w] = m_val[k];
				++w;
			}
			m_row.resize(w); m_col.resize(w); m_val.resize(w);
			m_nodes.erase(m_nodes.begin() + i);
			--m_count;
			m_indexed = false;
			return true;
		};

		//节点数据
		inline _node_data& node(const xhqm::size& i) { return m_nodes[i]; };
		inline const _node_data& node(const xhqm::size& i) const { return m_nodes[i]; };
		inline _node_data& node_at(const xhqm::size& i)
		{
			if (i >= m_count) throw std::out_of_range("sparse_mesh::node_at");
			return m_nodes[i];
		};
		inline const _node_data& node_at(const xhqm::size& i) const
		{
			if (i >= m_count) throw std::out_of_range("sparse_mesh::node_at");
			return m_nodes[i];
		};

		//—— 边 ——
		inline xhqm::size nnz() const noexcept { return m_row.size(); };

		//追加边 i→j（bool 时值 true）；重复边在 index() 时合并
		void add_edge(const xhqm::size& i, const xhqm::size& j)
		{
			add_edge(i, j, _edge_data(true));
		};
		//追加边 i→j（带边数据）
		void add_edge(const xhqm::size& i, const xhqm::size& j, const _edge_data& e)
		{
			if (i >= m_count || j >= m_count) throw std::out_of_range("sparse_mesh::add_edge");
			m_row.push_back(i);
			m_col.push_back(j);
			m_val.push_back(e);
			m_indexed = false;
		};
		void add_edge(const xhqm::size& i, const xhqm::size& j, _edge_data&& e)
		{
			if (i >= m_count || j >= m_count) throw std::out_of_range("sparse_mesh::add_edge");
			m_row.push_back(i);
			m_col.push_back(j);
			m_val.push_back(std::move(e));
			m_indexed = false;
		};

		//设置边值（存在则覆盖，不存在则追加）
		void set_edge(const xhqm::size& i, const xhqm::size& j, const _edge_data& e)
		{
			if (i >= m_count || j >= m_count) throw std::out_of_range("sparse_mesh::set_edge");
			index();
			const xhqm::size k = find_edge(i, j);
			if (k == xhqm::npos) add_edge(i, j, e);
			else m_val[k] = e;
		};

		//删除边 i→j（O(log 出度) 定位 + O(1) 尾交换删除，删除后索引失效）
		bool remove_edge(const xhqm::size& i, const xhqm::size& j)
		{
			if (i >= m_count || j >= m_count) return false;
			index();
			const xhqm::size k = find_edge(i, j);
			if (k == xhqm::npos) return false;
			const xhqm::size last = m_row.size() - 1;
			m_row[k] = m_row[last];
			m_col[k] = m_col[last];
			m_val[k] = m_val[last];
			m_row.pop_back();
			m_col.pop_back();
			m_val.pop_back();
			m_indexed = false;
			return true;
		};

		//是否邻接（非零值 = 有边；纯只读，index 后二分 / 未 index 线性）
		bool adjacent(const xhqm::size& i, const xhqm::size& j) const
		{
			if (i >= m_count || j >= m_count) return false;
			return find_edge(i, j) != xhqm::npos;
		};
		bool adjacent_at(const xhqm::size& i, const xhqm::size& j) const
		{
			if (i >= m_count || j >= m_count) throw std::out_of_range("sparse_mesh::adjacent_at");
			return find_edge(i, j) != xhqm::npos;
		};

		//边数据（假定边存在；不存在时行为未定义；触发 index 合并）
		_edge_data& edge(const xhqm::size& i, const xhqm::size& j)
		{
			index();
			return m_val[find_edge(i, j)];
		};
		_edge_data& edge_at(const xhqm::size& i, const xhqm::size& j)
		{
			index();
			const xhqm::size k = find_edge(i, j);
			if (k == xhqm::npos) throw std::out_of_range("sparse_mesh::edge_at");
			return m_val[k];
		};

		//—— 度数（index 后 O(1)；未 index 触发惰性建索引，非 const）——
		xhqm::size out_degree(const xhqm::size& i)
		{
			index();
			if (i >= m_count) return 0;
			return m_row_ptr[i + 1] - m_row_ptr[i];
		};
		xhqm::size in_degree(const xhqm::size& i)
		{
			index();
			if (i >= m_count) return 0;
			return m_in_deg[i];
		};

		//—— 连续数据 ——
		inline _node_data* nodes() noexcept { return m_nodes.data(); };
		inline const _node_data* nodes() const noexcept { return m_nodes.data(); };
		//COO 三元组（index 后为排序+合并后的非零边）
		inline const std::vector<xhqm::index>& row() const noexcept { return m_row; };
		inline const std::vector<xhqm::index>& col() const noexcept { return m_col; };
		inline const std::vector<_edge_data>& val() const noexcept { return m_val; };
		//CSR 行偏移（N+1，index 后有效；触发惰性建索引；与 col()/val() 配合作 CSR 顺序访问 SpMV）
		inline const std::vector<xhqm::size>& row_ptr() { index(); return m_row_ptr; };

		//—— 遍历 ——
		//节点 i 的邻接节点编号列表（出边，O(出度)；触发 index）
		std::vector<xhqm::size> adjacent_list(const xhqm::size& i)
		{
			index();
			std::vector<xhqm::size> list;
			if (i >= m_count) return list;
			list.reserve(m_row_ptr[i + 1] - m_row_ptr[i]);
			for (xhqm::size k = m_row_ptr[i]; k < m_row_ptr[i + 1]; ++k)
				list.push_back(m_col[k]);
			return list;
		};

		//遍历所有非零边（O(nnz)，不触发索引）：fn(source, target, value)
		template<typename _fn>
		void for_each_edge(_fn&& fn) const
		{
			for (xhqm::size k = 0; k < m_row.size(); ++k)
				fn(m_row[k], m_col[k], m_val[k]);
		};

		//—— 可达性（BFS，依赖 CSR 邻接遍历；触发 index）——
		bool reachable(const xhqm::size& i, const xhqm::size& j)
		{
			if (i == j) return true;
			index();
			if (i >= m_count || j >= m_count) return false;
			std::vector<bool> visited(m_count, false);
			std::vector<xhqm::size> queue;
			queue.push_back(i);
			visited[i] = true;
			xhqm::size head = 0;
			while (head < queue.size())
			{
				const xhqm::size u = queue[head++];
				for (xhqm::size k = m_row_ptr[u]; k < m_row_ptr[u + 1]; ++k)
				{
					const xhqm::size v = m_col[k];
					if (!visited[v])
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
		std::vector<xhqm::size> reachable_component(const xhqm::size& i)
		{
			index();
			std::vector<xhqm::size> comp;
			if (i >= m_count) return comp;
			std::vector<bool> visited(m_count, false);
			std::vector<xhqm::size> queue;
			queue.push_back(i);
			visited[i] = true;
			xhqm::size head = 0;
			while (head < queue.size())
			{
				const xhqm::size u = queue[head++];
				comp.push_back(u);
				for (xhqm::size k = m_row_ptr[u]; k < m_row_ptr[u + 1]; ++k)
				{
					const xhqm::size v = m_col[k];
					if (!visited[v])
					{
						visited[v] = true;
						queue.push_back(v);
					}
				}
			}
			return comp;
		};

		//—— 索引与代数 ——
		inline void rebuild_index() { m_indexed = false; index(); };
		inline bool indexed() const noexcept { return m_indexed; };

		//导出稀疏矩阵（COO），复用 tensor_base::sparse_matrix 的 SpMV 后端加速
		tensor_base::sparse_matrix<_edge_data> to_sparse_matrix()
		{
			index();
			tensor_base::store<_edge_data> sv(m_val);
			return tensor_base::sparse_matrix<_edge_data>(m_count, m_count, m_row, m_col, sv);
		};

		//—— 构造 / 移动 ——
		sparse_mesh() noexcept = default;
		sparse_mesh(sparse_mesh&& other) noexcept
			: m_nodes(std::move(other.m_nodes)), m_row(std::move(other.m_row)),
			m_col(std::move(other.m_col)), m_val(std::move(other.m_val)),
			m_row_ptr(std::move(other.m_row_ptr)), m_in_deg(std::move(other.m_in_deg)),
			m_indexed(other.m_indexed), m_count(other.m_count)
		{
			other.m_count = 0;
			other.m_indexed = false;
		}
		sparse_mesh& operator=(sparse_mesh&& other) noexcept
		{
			if (this != &other)
			{
				m_nodes = std::move(other.m_nodes);
				m_row = std::move(other.m_row);
				m_col = std::move(other.m_col);
				m_val = std::move(other.m_val);
				m_row_ptr = std::move(other.m_row_ptr);
				m_in_deg = std::move(other.m_in_deg);
				m_indexed = other.m_indexed;
				m_count = other.m_count;
				other.m_count = 0;
				other.m_indexed = false;
			}
			return *this;
		}
		sparse_mesh(const sparse_mesh&) = default;
		sparse_mesh& operator=(const sparse_mesh&) = default;

		//—— 迭代器（节点下标序列 0..N-1，解引用节点数据）——
		class iterator
		{
			sparse_mesh* m_mesh;
			xhqm::size m_pos;
		public:
			using value_type = _node_data;
			using reference = _node_data&;
			using pointer = _node_data*;
			using difference_type = std::ptrdiff_t;
			using iterator_category = std::forward_iterator_tag;
			iterator(sparse_mesh* m, xhqm::size p) : m_mesh(m), m_pos(p) {}
			_node_data& operator*() { return m_mesh->m_nodes[m_pos]; }
			iterator& operator++() { ++m_pos; return *this; }
			iterator operator++(int) { iterator tmp(*this); ++*this; return tmp; }
			bool operator==(const iterator& o) const { return m_pos == o.m_pos; }
			bool operator!=(const iterator& o) const { return m_pos != o.m_pos; }
		};
		class const_iterator
		{
			const sparse_mesh* m_mesh;
			xhqm::size m_pos;
		public:
			using value_type = _node_data;
			using reference = const _node_data&;
			using pointer = const _node_data*;
			using difference_type = std::ptrdiff_t;
			using iterator_category = std::forward_iterator_tag;
			const_iterator(const sparse_mesh* m, xhqm::size p) : m_mesh(m), m_pos(p) {}
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
