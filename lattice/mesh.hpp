/*
*作者：星辉清梦
*时间：龙—甲辰丙子戊申壬子
*用途：图节点类
* 网状类
*	普通网：		继承用网 双向网 单向网 矩阵网
* 注意：迭代器的移动不影响树的当前项
*/

#pragma once
#ifndef XHQM_META_MESH
#define XHQM_META_MESH
#include "./struct.h"
#include "./graph_algorithms.hpp"   // 图算法层（拓扑/SCC 等；兼容旧 include 路径）
#include <iterator>
#include <stack>
#include <queue>
#include <set>
#include <map>
#include <utility>
namespace xhqm
{
	//enum class mesh_type : ::xhqm::id
	//{
	//	mesh_oneway = 1,	//单向网
	//	mesh_twoway = 2,	//双向网
	//	mesh_matrix = 4,	//矩阵网
	//};

	namespace _graph_
	{
		enum class mesh_dir : ::xhqm::id
		{
			mesh_nullptr = 0,	//无向
			mesh_oneway = 1,	//单向
			mesh_twoway = 2,	//双向
		};

		template<mesh_dir _mesh_dir> struct mesh_dir_value { constexpr static edge_dir _edge_dir = edge_dir::edge_null; };
		template<> struct mesh_dir_value<mesh_dir::mesh_oneway> { constexpr static edge_dir _edge_dir = edge_dir::edge_goto; };
		template<> struct mesh_dir_value<mesh_dir::mesh_twoway> { constexpr static edge_dir _edge_dir = edge_dir::edge_bind; };


		//网基础类型 
		template<typename _node_type, typename _node_data, typename _edge_data, mesh_dir _mesh_dir, ::xhqm::size _size>
		struct mesh_node_ : public base_data_<_node_data>
		{
			constexpr static edge_dir void_value = ::xhqm::compute<edge_dir>::select_if<::xhqm::is_same<void, _edge_data>, edge_dir::edge_null, mesh_dir_value<_mesh_dir>::_edge_dir>::value;
			constexpr static edge_dir bind_value = ::xhqm::compute<edge_dir>::select_if<mesh_dir::mesh_twoway == _mesh_dir, edge_dir::edge_bind, mesh_dir_value<_mesh_dir>::_edge_dir>::value;
			constexpr static edge_dir edge_value = ::xhqm::compute<edge_dir>::select_if<edge_dir::edge_bind == bind_value, bind_value, void_value>::value;
			using mesh_node_s = node_s<_node_type, _node_data, _edge_data, edge_value>;
			using node_base = base_data_<_node_data>;
			using loca_type = typename mesh_node_s::loca_type;
			using node_type = typename mesh_node_s::node_type;
			using edge_type = typename mesh_node_s::edge_type;
			using node_data = typename mesh_node_s::node_data;
			using edge_data = typename mesh_node_s::edge_data;
			using node_data_ref = typename mesh_node_s::node_data_ref;
			using edge_data_ref = typename mesh_node_s::edge_data_ref;
			using node_data_ptr = typename mesh_node_s::node_data_ptr;
			using edge_data_ptr = typename mesh_node_s::edge_data_ptr;
			using node_loca_type = typename mesh_node_s::loca_ptr;
			using adjacent_loca_type = typename ::xhqm::select_if<(_size == 0), std::vector<node_loca_type>, std::array<node_loca_type, _size>>::type;

		protected: adjacent_loca_type adjacent_loca = {};
		public:	
			  constexpr mesh_node_() noexcept : node_base() {};
			  template<typename __data_type> mesh_node_(__data_type&& data_) noexcept
				  : node_base(xhqm::move(data_)) { }
			  template<typename __data_type> mesh_node_(const __data_type& data_) noexcept
				  : node_base(xhqm::copy(data_)) { }
			  constexpr inline void swap(_node_type& value_) noexcept
			  {
				  if constexpr (!::xhqm::is_same<_node_data, void>) { std::swap(this->data, value_.data); }
			  }
		protected:
			//constexpr loca_type* _add_adjacent_ptr(node_type* adjacent_ptr_);
			//constexpr loca_type* _sub_adjacent_ptr(node_type* adjacent_ptr_);
		public:
			constexpr bool add_adjacent_ptr(node_type* adjacent_ptr_)
			{
				return static_cast<node_type*>(this)->_add_adjacent_ptr(adjacent_ptr_);
			}
			constexpr bool sub_adjacent_ptr(node_type* adjacent_ptr_)
			{
				return static_cast<node_type*>(this)->_sub_adjacent_ptr(adjacent_ptr_);
			}

			//空间大小
			constexpr inline xhqm::count size() const { return this->adjacent_loca.size(); };
			//邻接节点
			constexpr node_type* adjacent_ptr(const ::xhqm::index& pos_) const
			{
				if (this->adjacent_loca.size() <= pos_) return nullptr;

				if constexpr (edge_dir::edge_null == edge_value)
				{
					return this->adjacent_loca[pos_];  // 无路径：直接存节点指针
				}
				else
				{
					return this->adjacent_loca[pos_]->goto_loca;  // 有路径：edge_ptr -> 目标节点
				};
			};
			//nullptr时，返回 npos
			constexpr xhqm::index adjacent_index(const node_type* ptr_) const
			{
				if (ptr_)
				{
					xhqm::count adjacent_size = this->adjacent_loca.size();
					for (xhqm::index ind = 0; ind < adjacent_size; ++ind)
					{
						if (this->adjacent_ptr(ind) == ptr_) return ind;
					}
				}
				return xhqm::npos;
			}
			//邻接节点的个数
			constexpr xhqm::count adjacent_size() const
			{
				if constexpr (_size == 0) { return this->adjacent_loca.size(); }
				if constexpr (_size != 0)
				{
					xhqm::count childs_size = 0;
					for (xhqm::index ind = 0; ind < _size; ++ind)
					{
						if (this->adjacent_ptr(ind)) ++childs_size;
					}
					return childs_size;
				}
			};
			//邻接节点列表
			std::vector<node_type*> adjacent_list() const
			{
				std::vector<node_type*> this_adjacent_list = {};
				xhqm::count adjacent_size = this->adjacent_loca.size();
				for (xhqm::index ind = 0; ind < adjacent_size; ++ind)
				{
					node_type* this_adjacent = this->adjacent_ptr(ind);
					if (this_adjacent) this_adjacent_list.push_back(this_adjacent);
				}
				return this_adjacent_list;
			}

			//constexpr inline node_loca_type adjacent_edge(const ::xhqm::index& pos) const { if (this->adjacent_loca.size() <= pos) return nullptr; return this->adjacent_loca[pos]; };   // 越界归用户管理
			constexpr inline node_loca_type adjacent_edge(const ::xhqm::index& pos) const { return this->adjacent_loca[pos]; };

		public:
			// 广度遍历
			template<typename _func_type, typename... _argv_type>
			void iterate_bfs(_func_type&& function_, _argv_type&& ... argv_data_)
			{
				std::queue<node_type*> node_queue;
				std::set<node_type*> visited_nodes;

				node_queue.push(static_cast<node_type*>(this));
				visited_nodes.insert(static_cast<node_type*>(this));

				while (!node_queue.empty()) {
					node_type* current_node = node_queue.front();
					node_queue.pop();

					// 调用处理函数
					function_(current_node, std::forward<_argv_type>(argv_data_)...);

					// 遍历所有邻接节点
					xhqm::count adjacent_size = current_node->adjacent_size();
					for (xhqm::index i = 0; i < adjacent_size; ++i) {
						node_type* adjacent_node = current_node->adjacent_ptr(i);
						if (adjacent_node && visited_nodes.find(adjacent_node) == visited_nodes.end()) {
							node_queue.push(adjacent_node);
							visited_nodes.insert(adjacent_node);
						}
					}
				}
			}

			// 深度遍历
			template<xhqm::index index = 0, typename _func_type, typename... _argv_type>
			void iterate_dfs(_func_type&& function_, _argv_type&& ... argv_data_)
			{
				if (!this) return;

				std::stack<node_type*> node_stack;
				std::set<node_type*> visited_nodes;

				node_stack.push(static_cast<node_type*>(this));
				visited_nodes.insert(static_cast<node_type*>(this));

				while (!node_stack.empty()) {
					node_type* current_node = node_stack.top();
					node_stack.pop();

					// 调用处理函数
					function_(current_node, std::forward<_argv_type>(argv_data_)...);

					// 遍历所有邻接节点（逆序入栈以保证正向遍历）
					std::vector<node_type*> adjacent_list = current_node->adjacent_list();
					for (auto it = adjacent_list.rbegin(); it != adjacent_list.rend(); ++it) {
						node_type* adjacent_node = *it;
						if (adjacent_node && visited_nodes.find(adjacent_node) == visited_nodes.end()) {
							node_stack.push(adjacent_node);
							visited_nodes.insert(adjacent_node);
						}
					}
				}
			}

			//是否邻接
			constexpr inline bool in_adjacent(const node_type* ptr) const
			{
				return (this->adjacent_index(ptr) != xhqm::npos);
			}

			//度（邻接节点数）
			constexpr inline xhqm::count degree() const { return this->adjacent_size(); }
			//出度（单向图：从 this 出发的边数，即邻接数）
			constexpr inline xhqm::count out_degree() const { return this->adjacent_size(); }
			//是否孤立（度 0）
			constexpr inline bool is_isolated() const { return this->adjacent_size() == 0; }

			//是否可达（从 this 能否经邻接到达 ptr，广度优先）
			bool is_reachable(const node_type* ptr) const
			{
				if (!ptr) return false;
				if (static_cast<const node_type*>(this) == ptr) return true;
				bool found = false;
				const_cast<node_type*>(static_cast<const node_type*>(this))->iterate_bfs(
					[&](node_type* n) { if (n == ptr) found = true; });
				return found;
			}
			//是否连通（双向：this 与 ptr 互相可达）
			bool is_connected(const node_type* ptr) const
			{
				if (!ptr) return false;
				return is_reachable(ptr) && ptr->is_reachable(static_cast<const node_type*>(this));
			}
			//连通分量大小（从 this 出发可达的节点数）
			xhqm::count connected_size() const
			{
				xhqm::count count = 0;
				const_cast<node_type*>(static_cast<const node_type*>(this))->iterate_bfs(
					[&](node_type*) { ++count; });
				return count;
			}
			//最短路径长度（无权，BFS；不可达返回 npos）
			xhqm::index shortest_path(const node_type* target) const
			{
				if (!target) return xhqm::npos;
				if (static_cast<const node_type*>(this) == target) return 0;
				std::queue<node_type*> q;
				std::map<node_type*, xhqm::index> dist;
				node_type* self = const_cast<node_type*>(static_cast<const node_type*>(this));
				q.push(self);
				dist[self] = 0;
				while (!q.empty())
				{
					node_type* u = q.front(); q.pop();
					for (node_type* v : u->adjacent_list())
					{
						if (dist.find(v) == dist.end())
						{
							dist[v] = dist[u] + 1;
							if (v == target) return dist[v];
							q.push(v);
						}
					}
				}
				return xhqm::npos;  // 不可达
			}
		};

		//继承用图基础类型（固定无向；单向/双向由 mesh_node_ 的 mesh_dir 参数组合）
		template<typename _node_type>
		class derive_mesh_ : public mesh_node_<_node_type, void, void, mesh_dir::mesh_nullptr, 0>
		{
		public:
			friend struct mesh_node_<_node_type, void, void, mesh_dir::mesh_nullptr, 0>;
			using node_base = mesh_node_<_node_type, void, void, mesh_dir::mesh_nullptr, 0>;
			using node_data_ref = typename node_base::node_data_ref;
			using edge_data_ref = typename node_base::edge_data_ref;
			using node_data_ptr = typename node_base::node_data_ptr;
			using edge_data_ptr = typename node_base::edge_data_ptr;
			using loca_type = typename node_base::loca_type;
			using node_type = typename node_base::node_type;
			using edge_type = typename node_base::edge_type;
			using node_data = typename node_base::node_data;
			using edge_data = typename node_base::edge_data;
			using node_ptr = _node_type*;
			using node_ref = _node_type&;
			//friend struct node_base;
		public:
			virtual ~derive_mesh_() {};
			constexpr explicit derive_mesh_() {};
			constexpr explicit derive_mesh_(derive_mesh_&&) = delete;
			constexpr explicit derive_mesh_(const derive_mesh_&) = delete;
		protected:
			virtual bool _add_adjacent_ptr(node_type* adjacent_ptr_) = 0;
			virtual bool _sub_adjacent_ptr(node_type* adjacent_ptr_) = 0;
		};


		//队queue广度 栈stack深度
		template<typename _mesh_type, template<typename> typename _dock_type>
		struct mesh_iterator_restraint
		{
			using mesh_type = _mesh_type;
			using node_type = typename mesh_type::mesh_node;
			using node_data = typename mesh_type::node_data;
			using edge_data = typename mesh_type::edge_data;
			using edge_data_ref = typename mesh_type::edge_data_ref;
			using reference = typename mesh_type::node_data_ref;
			using pointer = typename mesh_type::node_data_ptr;
			using dock_stack = std::stack<node_type*>;
			using dock_queue = std::queue<node_type*>;
			using dock_type = _dock_type<node_type*>;
			using mesh_ptr = mesh_type*;
			using node_ptr = node_type*;
			//迭代器 traits（std::distance/count/find 等泛型算法可用）
			//void 节点数据时解引用得节点指针，否则得节点数据引用
			using value_type = typename ::xhqm::select_if<::xhqm::is_same<void, node_data>, node_type*, node_data>::type;
			using difference_type = std::ptrdiff_t;
			using iterator_category = std::forward_iterator_tag;

			constexpr static bool is_stack = xhqm::is_same<dock_type, dock_stack>;
			constexpr static bool is_queue = xhqm::is_same<dock_type, dock_queue>;
			static_assert((is_stack || is_queue), "未知类型的迭代器");
		};

		template<typename _iter_type, typename _mesh_type, template<typename> typename _dock_type>
		class mesh_iterator_ : public mesh_iterator_restraint<_mesh_type, _dock_type>
		{
		public:
			using mesh_iterator = _iter_type;
			using mesh_struct = mesh_iterator_restraint<_mesh_type, _dock_type>;
			using mesh_type = typename mesh_struct::mesh_type;
			using node_type = typename mesh_struct::node_type;
			using node_data = typename mesh_struct::node_data;
			using dock_type = typename mesh_struct::dock_type;
			using reference = typename mesh_struct::reference;
			using pointer = typename mesh_struct::pointer;
			using mesh_ptr = typename mesh_struct::mesh_ptr;
			using node_ptr = typename mesh_struct::node_ptr;

		protected:
			dock_type node_list;
			std::set<node_ptr> visited_nodes;

			constexpr inline node_ptr& first_ptr()
			{
				if constexpr (mesh_struct::is_stack) { return this->node_list.top(); };
				if constexpr (mesh_struct::is_queue) { return this->node_list.front(); };
			}

			constexpr inline const node_ptr& first_ptr() const
			{
				if constexpr (mesh_struct::is_stack) { return this->node_list.top(); };
				if constexpr (mesh_struct::is_queue) { return this->node_list.front(); };
			}

		public:
			constexpr mesh_iterator_() = default;
			mesh_iterator_(const mesh_iterator_&) = default;   // 用户移动赋值会删除隐式拷贝构造，补回（std::distance 按值传参需要）

			constexpr mesh_iterator_(node_ptr curr_ptr_)
			{
				if (curr_ptr_)
				{
					node_list.push(curr_ptr_);
					visited_nodes.insert(curr_ptr_);
				}
			};

			template<typename __iter_type>
			constexpr operator __iter_type()
			{
				if (this->node_list.empty()) return __iter_type(nullptr);
				return __iter_type(this->first_ptr());
			};

			constexpr mesh_iterator_& operator=(mesh_iterator_&& move)
			{
				this->node_list = std::move(move.node_list);
				this->visited_nodes = std::move(move.visited_nodes);
				return *this;
			};

			constexpr mesh_iterator_& operator=(const mesh_iterator_& copy)
			{
				this->node_list = copy.node_list;
				this->visited_nodes = copy.visited_nodes;
				return *this;
			};

			constexpr bool operator==(const mesh_iterator_& mesh_iter_) const
			{
				//if (this->node_list.empty() && mesh_iter_.node_list.empty()) return true;
				if (this->node_list.empty() && mesh_iter_.node_list.empty()) return true;
				else if (!this->node_list.empty() && mesh_iter_.node_list.empty()) return false;
				else if (this->node_list.empty() && !mesh_iter_.node_list.empty()) return false;
				return (this->first_ptr() == mesh_iter_.first_ptr());
			};

			constexpr bool operator!=(const mesh_iterator_& mesh_iter_) const
			{
				return !(*this == mesh_iter_);
			};

			constexpr const reference operator*() const noexcept
			{
				if constexpr (xhqm::is_same<void, node_data>)
				{
					return this->first_ptr();
				}
				else
				{
					return this->first_ptr()->data;
				}
			};

			constexpr reference operator*() noexcept
			{
				if constexpr (xhqm::is_same<void, node_data>)
				{
					return this->first_ptr();
				}
				else
				{
					return this->first_ptr()->data;
				}
			};

			constexpr pointer operator->() const noexcept
			{
				if constexpr (xhqm::is_same<void, node_data>)
				{
					if (!this->node_list.empty()) return this->first_ptr();
				}
				else
				{
					if (!this->node_list.empty()) return &(this->first_ptr()->data);
				}
				return nullptr;
			}

			constexpr bool empty() const { return node_list.empty(); }
			constexpr size_t size() const { return node_list.size(); }
		};
	}

	// 广度遍历
	template<typename _mesh_type>
	class mesh_bfs_iterator : public _graph_::mesh_iterator_<mesh_bfs_iterator<_mesh_type>, _mesh_type, std::queue>
	{
		using mesh_iterator = _graph_::mesh_iterator_<mesh_bfs_iterator<_mesh_type>, _mesh_type, std::queue>;
	public:
		using mesh_type = typename mesh_iterator::mesh_type;
		using node_type = typename mesh_iterator::node_type;
		using node_data = typename mesh_iterator::node_data;
		using dock_type = typename mesh_iterator::dock_type;
		using reference = typename mesh_iterator::reference;
		using pointer = typename mesh_iterator::pointer;
		using mesh_ptr = typename mesh_iterator::mesh_ptr;
		using node_ptr = typename mesh_iterator::node_ptr;

	public:
		constexpr mesh_bfs_iterator(node_ptr curr_ptr_ = nullptr)
			: mesh_iterator(curr_ptr_)
		{

		}

		constexpr mesh_bfs_iterator& next()
		{
			if (!this->node_list.empty())
			{
				auto curr_ptr = this->first_ptr();
				this->node_list.pop();

				// 将当前节点的所有未访问邻接节点加入队列
				auto adjacent_list = curr_ptr->adjacent_list();
				for (auto& adjacent_ptr : adjacent_list)
				{
					if (adjacent_ptr && this->visited_nodes.find(adjacent_ptr) == this->visited_nodes.end())
					{
						this->node_list.push(adjacent_ptr);
						this->visited_nodes.insert(adjacent_ptr);
					}
				}
			}
			return *this;
		};

		constexpr mesh_bfs_iterator& operator++() { return this->next(); };
	};

	// 深度遍历
	template<typename _mesh_type>
	class mesh_dfs_iterator : public _graph_::mesh_iterator_<mesh_dfs_iterator<_mesh_type>, _mesh_type, std::stack>
	{
		using mesh_iterator = _graph_::mesh_iterator_<mesh_dfs_iterator<_mesh_type>, _mesh_type, std::stack>;
	public:
		using mesh_type = typename mesh_iterator::mesh_type;
		using node_type = typename mesh_iterator::node_type;
		using node_data = typename mesh_iterator::node_data;
		using dock_type = typename mesh_iterator::dock_type;
		using reference = typename mesh_iterator::reference;
		using pointer = typename mesh_iterator::pointer;
		using mesh_ptr = typename mesh_iterator::mesh_ptr;
		using node_ptr = typename mesh_iterator::node_ptr;

	public:
		constexpr mesh_dfs_iterator(node_ptr curr_ptr_ = nullptr)
			: mesh_iterator(curr_ptr_)
		{

		}

		constexpr mesh_dfs_iterator& next()
		{
			if (!this->node_list.empty())
			{
				auto curr_ptr = this->first_ptr();
				this->node_list.pop();

				// 将当前节点的所有未访问邻接节点逆序入栈
				auto adjacent_list = curr_ptr->adjacent_list();
				for (auto it = adjacent_list.rbegin(); it != adjacent_list.rend(); ++it)
				{
					auto adjacent_ptr = *it;
					if (adjacent_ptr && this->visited_nodes.find(adjacent_ptr) == this->visited_nodes.end())
					{
						this->node_list.push(adjacent_ptr);
						this->visited_nodes.insert(adjacent_ptr);
					}
				}
			}
			return *this;
		};

		constexpr mesh_dfs_iterator& operator++() { return this->next(); };
	};

	template<typename _mesh_type> using mesh_iterator = mesh_dfs_iterator<_mesh_type>;

	//通用继承用图类型（固定无向，对齐 list/tree 的 derive 类；单向/双向由 mesh_node_ 的 mesh_dir 参数组合）
	template<typename _node_type>
	class derive_mesh : public xhqm::_graph_::derive_mesh_<_node_type>
	{
	public:
		using iterator = xhqm::mesh_iterator<_node_type>;
		using mesh_base = xhqm::_graph_::derive_mesh_<_node_type>;
		using mesh_node = typename mesh_base::node_type;
		using node_type = typename mesh_base::node_type;
		using edge_type = typename mesh_base::edge_type;
		using node_data_ref = node_type*;
		using edge_data_ref = void;
		using node_data_ptr = node_type*;
		using edge_data_ptr = void;
		using node_data = void;
		using edge_data = void;
		using node_ptr = node_type*;
		using node_ref = node_type&;
	public:
		virtual bool _add_adjacent_ptr(node_type* adjacent_ptr_) override final
		{
			if (!adjacent_ptr_) return false;
			if (adjacent_ptr_ == static_cast<node_type*>(this)) return false;   // 拒绝自环
			if (this->adjacent_index(adjacent_ptr_) != xhqm::npos) return false;

			this->adjacent_loca.push_back(adjacent_ptr_);
			// 无向：自动双向维护（对方表加 this；重复由对方 adjacent_index 守卫）
			if (adjacent_ptr_->adjacent_index(static_cast<node_type*>(this)) == xhqm::npos)
				adjacent_ptr_->adjacent_loca.push_back(static_cast<node_type*>(this));
			return true;
		}
		virtual bool _sub_adjacent_ptr(node_type* adjacent_ptr_) override final
		{
			if (adjacent_ptr_)
			{
				auto index = this->adjacent_index(adjacent_ptr_);
				if (index != xhqm::npos)
				{
					this->adjacent_loca.erase(this->adjacent_loca.begin() + index);
					// 无向：对称删除对方表中的 this
					auto pindex = adjacent_ptr_->adjacent_index(static_cast<node_type*>(this));
					if (pindex != xhqm::npos)
						adjacent_ptr_->adjacent_loca.erase(adjacent_ptr_->adjacent_loca.begin() + pindex);
					return true;
				}
			}
			return false;
		}

		//begin是自身
		inline iterator begin() const noexcept { return iterator((node_type*)(this)); };
		//begin是自身
		inline iterator begin() noexcept { return iterator((node_type*)(this)); };
		//end是nullptr
		inline iterator end() const noexcept { return iterator(nullptr); };
		//end是nullptr
		inline iterator end() noexcept { return iterator(nullptr); };
	};

	class test_mesh : public derive_mesh<test_mesh>
	{
	public:
		char value = 0;
		test_mesh(char v = 0) :value(v) {};
	};

	//test_mesh mesh;
}
#endif