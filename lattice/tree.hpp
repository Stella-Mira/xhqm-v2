/*
*作者：星辉清梦
*时间：龙—甲辰丙子戊申壬子
*用途：树节点类
*顺序：自上往下，自前往后
* 树型类
*	普通树：		继承用树 父向树 子向树 双向树 多叉树 层级树
* 注意：迭代器的移动不影响树的当前项
*/

#pragma once
#ifndef XHQM_META_TREE
#define XHQM_META_TREE
#include "./struct.h"
#include <iterator>
#include <stack>
#include <queue>
#include <utility>
namespace xhqm
{
	//enum class tree_type : ::xhqm::id
	//{
	//	tree_parent = 1,	//父向树
	//	tree_childs = 2,	//子向树
	//	tree_link = 4,		//双向树
	//	tree_fork = 8,		//多叉树
	//	tree_ply = 16,		//层级树
	//};

	namespace _graph_
	{
		enum class tree_dir : ::xhqm::id
		{
			tree_nullptr = 0,	//无父节点无子节点
			tree_parent = 1,	//有父节点无子节点
			tree_childs = 2,	//无父节点有子节点
			tree_bind = 4,		//有父节点有子节点
		};

		template<tree_dir _tree_dir> struct tree_dir_value { constexpr static edge_dir _edge_dir = edge_dir::edge_null; };
		template<> struct tree_dir_value<tree_dir::tree_parent> { constexpr static edge_dir _edge_dir = edge_dir::edge_from; };
		template<> struct tree_dir_value<tree_dir::tree_childs> { constexpr static edge_dir _edge_dir = edge_dir::edge_goto; };
		template<> struct tree_dir_value<tree_dir::tree_bind> { constexpr static edge_dir _edge_dir = edge_dir::edge_bind; };

		//树基础类型 
		template<typename _node_type, typename _node_data, typename _edge_data, tree_dir _tree_dir, ::xhqm::size _child_size>
		struct tree_node_ : public base_data_<_node_data>
		{
			constexpr static edge_dir void_value = ::xhqm::compute<edge_dir>::select_if<::xhqm::is_same<void, _edge_data>, edge_dir::edge_null, tree_dir_value<_tree_dir>::_edge_dir>::value;
			constexpr static edge_dir bind_value = ::xhqm::compute<edge_dir>::select_if<tree_dir::tree_bind == _tree_dir, edge_dir::edge_bind, tree_dir_value<_tree_dir>::_edge_dir>::value;
			constexpr static edge_dir edge_value = ::xhqm::compute<edge_dir>::select_if<edge_dir::edge_bind == bind_value, bind_value, void_value>::value;
			using tree_node_s = node_s<_node_type, _node_data, _edge_data, edge_value>;
			using node_base = base_data_<_node_data>;
			using loca_type = typename tree_node_s::loca_type;
			using node_type = typename tree_node_s::node_type;
			using edge_type = typename tree_node_s::edge_type;
			using node_data = typename tree_node_s::node_data;
			using edge_data = typename tree_node_s::edge_data;
			using node_data_ref = typename tree_node_s::node_data_ref;
			using edge_data_ref = typename tree_node_s::edge_data_ref;
			using node_data_ptr = typename tree_node_s::node_data_ptr;
			using edge_data_ptr = typename tree_node_s::edge_data_ptr;
			using from_loca_type = typename ::xhqm::select_if<(_tree_dir == tree_dir::tree_nullptr || _tree_dir == tree_dir::tree_parent || _tree_dir == tree_dir::tree_bind), typename tree_node_s::loca_ptr, bool>::type;
			using goto_loca_type = typename ::xhqm::select_if<(_tree_dir == tree_dir::tree_nullptr || _tree_dir == tree_dir::tree_childs || _tree_dir == tree_dir::tree_bind), typename tree_node_s::loca_ptr, bool>::type;

			using parent_loca_type = from_loca_type;
			using childs_loca_type = typename ::xhqm::select_if<(_child_size == 0), std::vector<goto_loca_type>, std::array<goto_loca_type, _child_size>>::type;
		protected: parent_loca_type parent_loca = {};
		protected: childs_loca_type childs_loca = {};
		public:	inline bool has_parent() const
		{
			if constexpr (_tree_dir == tree_dir::tree_bind) { return (parent_loca ? bool(parent_loca->from_loca) : false); };												//edge_ptr
			if constexpr (_tree_dir == tree_dir::tree_parent && xhqm::is_same<void, edge_data>) { return parent_loca; };													//node_ptr
			if constexpr (_tree_dir == tree_dir::tree_parent && !xhqm::is_same<void, edge_data>) { return (parent_loca ? bool(parent_loca->from_loca) : false); };			//edge_ptr
			if constexpr (_tree_dir == tree_dir::tree_nullptr || _tree_dir == tree_dir::tree_childs) { return parent_loca; };												//node_ptr||bool
		};
		public:	inline void has_parent(const bool& has) { if constexpr (::xhqm::is_same<from_loca_type, bool>) { parent_loca = has; }; };

			  constexpr tree_node_() noexcept : node_base() {};
			  template<typename __data_type> tree_node_(__data_type&& data_) noexcept
				  : node_base(xhqm::move(data_)) {
			  }
			  template<typename __data_type> tree_node_(const __data_type& data_) noexcept
				  : node_base(xhqm::copy(data_)) {
			  }
			  constexpr inline void swap(_node_type& value_) noexcept
			  {
				  if constexpr (!::xhqm::is_same<_node_data, void>) { std::swap(this->data, value_.data); }
			  }
		protected:
			//constexpr loca_type* _set_parent_ptr(node_type* parent_ptr_);
		protected:
			//先序遍历-0 //中序遍历-n //后序遍历-npos
			//显式栈迭代（深树不爆栈）：pre=[0,index) 子节点 → 根 → post=[index,end) 子节点
			template<typename __node_type, typename _func_type, typename... _argv_type>
			static void _iterate_dfs(const xhqm::index& index, __node_type* tree_node_, _func_type& function_, _argv_type&& ... argv_data_)
			{
				std::stack<std::pair<__node_type*, bool>> stack;
				stack.push({ tree_node_, false });
				while (!stack.empty())
				{
					auto frame = stack.top(); stack.pop();
					__node_type* node = frame.first;
					if (frame.second)
					{
						function_(node, std::forward<_argv_type>(argv_data_)...);
						continue;
					}
					std::vector<__node_type*> childs_list = node->childs();
					std::vector<__node_type*> pre_childs_list;
					std::vector<__node_type*> post_childs_list;
					if (childs_list.size() <= index)
					{
						pre_childs_list = childs_list;
					}
					else
					{
						pre_childs_list.insert(pre_childs_list.begin(), childs_list.begin(), childs_list.begin() + index);
						post_childs_list.insert(post_childs_list.begin(), childs_list.begin() + index, childs_list.end());
					}
					// 执行顺序：pre 正序 → 根 → post 正序；压栈取逆序
					for (auto it = post_childs_list.rbegin(); it != post_childs_list.rend(); ++it)
						stack.push({ *it, false });
					stack.push({ node, true });
					for (auto it = pre_childs_list.rbegin(); it != pre_childs_list.rend(); ++it)
						stack.push({ *it, false });
				}
			}
			//层序遍历：返回 index 层的节点个数（队列迭代，深树不爆栈）
			template<typename __node_type, typename _func_type, typename... _argv_type>
			static xhqm::count _iterate_bfs(const xhqm::index& index, __node_type* tree_node_, _func_type& function_, _argv_type&& ... argv_data_)
			{
				xhqm::count count = 0;
				std::queue<std::pair<__node_type*, xhqm::index>> queue;
				queue.push({ tree_node_, 0 });
				while (!queue.empty())
				{
					auto frame = queue.front(); queue.pop();
					__node_type* node = frame.first;
					xhqm::index level = frame.second;
					if (level == index)
					{
						function_(node, std::forward<_argv_type>(argv_data_)...);
						++count;
					}
					else if (level > index) break;   // BFS 按层推进，越过目标层即可停止
					for (auto& child_ptr : node->childs())
						queue.push({ child_ptr, level + 1 });
				}
				return count;
			}
		public:
			constexpr bool set_parent_ptr(node_type* parent_ptr_)
			{
				return static_cast<node_type*>(this)->_set_parent_ptr(parent_ptr_);
			}
			constexpr bool add_child_ptr(node_type* child_ptr_)
			{
				return static_cast<node_type*>(this)->_add_child_ptr(child_ptr_);
			}
			constexpr bool sub_child_ptr(node_type* child_ptr_)
			{
				return static_cast<node_type*>(this)->_sub_child_ptr(child_ptr_);
			}

			//树节点的度
			constexpr xhqm::count degree() const
			{
				if constexpr (_child_size == 0) { return this->childs_loca.size(); }
				if constexpr (_child_size != 0)
				{
					xhqm::count childs_size = 0;
					for (xhqm::index ind = 0; ind < _child_size; ++ind)
					{
						if (this->child_ptr(ind)) ++childs_size;
					}
					return childs_size;
				}
			};
			//子向时，根节点是自身
			constexpr node_type* root_ptr() const
			{
				node_type* root = nullptr;
				node_type* begin = const_cast<node_type*>(static_cast<const node_type*>(this));
				if (node_type* curr = this->parent_ptr())
				{
					while (curr)
					{
						if (curr == begin) return nullptr;   // 环（结构破坏）：无根，返回 nullptr
						root = curr;
						curr = curr->parent_ptr();
					}
				}
				else
				{
					root = begin;   // 根：返回自身
				}
				return root;
			}
			//子向时，父节点是空值
			constexpr node_type* parent_ptr() const
			{
				if constexpr (tree_dir::tree_nullptr == _tree_dir)
				{
					return this->parent_loca;
				};
				if constexpr (tree_dir::tree_parent == _tree_dir)
				{
					if constexpr (xhqm::is_same<void, edge_data>)
					{
						return this->parent_loca;
					}
					else
					{
						if (!this->parent_loca) return nullptr;
						return this->parent_loca->from_loca;
					};
				};
				if constexpr (tree_dir::tree_childs == _tree_dir)
				{
					return nullptr;
				};
				if constexpr (tree_dir::tree_bind == _tree_dir)
				{
					if (!this->parent_loca) return nullptr;
					return this->parent_loca->from_loca;
				};
			}
			//父向时，所有子节点是空值 nullptr
			constexpr node_type* child_ptr(const ::xhqm::index& pos_) const
			{
				if constexpr (tree_dir::tree_nullptr == _tree_dir)
				{
					//if (this->childs_loca.size() <= pos_) return nullptr;   // 越界归用户管理
					return this->childs_loca[pos_];
				};
				if constexpr (tree_dir::tree_parent == _tree_dir)
				{
					return nullptr;
				};
				if constexpr (tree_dir::tree_childs == _tree_dir)
				{
					if constexpr (xhqm::is_same<void, edge_data>)
					{
						//if (this->childs_loca.size() <= pos_) return nullptr;   // 越界归用户管理
						return this->childs_loca[pos_];
					}
					else
					{
						//if (this->childs_loca.size() <= pos_) return nullptr;   // 越界归用户管理
						return this->childs_loca[pos_]->goto_loca;
					};
				};
				if constexpr (tree_dir::tree_bind == _tree_dir)
				{
					//if (this->childs_loca.size() <= pos_) return nullptr;   // 越界归用户管理
					return this->childs_loca[pos_]->goto_loca;
				};
			};
			//父向时，所有返回是无限值 npos
			constexpr xhqm::index child_index(const node_type* ptr_) const
			{
				if constexpr (tree_dir::tree_parent != _tree_dir)
				{
					xhqm::count child_size = this->childs_loca.size();
					if (ptr_)
					{
						for (xhqm::index ind = 0; ind < child_size; ++ind)
						{
							if (this->child_ptr(ind) == ptr_) return ind;
						}
					}
				};
				return xhqm::npos;
			}
			//N叉树时，返回N
			constexpr inline xhqm::count child_size() const { return this->childs_loca.size(); };

			std::vector<node_type*> childs() const
			{
				std::vector<node_type*> this_childs = {};
				xhqm::count child_size = this->childs_loca.size();
				for (xhqm::index ind = 0; ind < child_size; ++ind)
				{
					node_type* this_child = this->child_ptr(ind);
					if (this_child) this_childs.push_back(this_child);
				}
				return this_childs;
			}

			constexpr inline from_loca_type parent_edge() const { return parent_loca; };
			constexpr inline goto_loca_type child_edge(const ::xhqm::index& pos) const { return childs_loca[pos]; };
		public:
			//父向时，无法遍历//层序遍历 返回当前层节点个数
			template<typename _func_type, typename... _argv_type>
			xhqm::count iterate_level(const xhqm::index& select, _func_type&& function_, _argv_type&& ... argv_data_)
			{
				return tree_node_::_iterate_bfs(select, static_cast<node_type*>(this), std::forward<_func_type>(function_), std::forward<_argv_type>(argv_data_)...);
			}
			//父向时，无法遍历//广度遍历 返回层个数
			template<typename _func_type, typename... _argv_type>
			xhqm::count iterate_bfs(_func_type&& function_, _argv_type&& ... argv_data_)
			{
				xhqm::index select = 0;
				while (tree_node_::_iterate_bfs(select, static_cast<node_type*>(this), function_, std::forward<_argv_type>(argv_data_)...))
				{
					++select;
				}
				return select;
			}
			//父向时，无法遍历//深度遍历
			template<xhqm::index index = 0, typename _func_type, typename... _argv_type>
			void iterate_dfs(_func_type&& function_, _argv_type&& ... argv_data_)
			{
				tree_node_::_iterate_dfs(index, static_cast<node_type*>(this), function_, std::forward<_argv_type>(argv_data_)...);
			}

			//子向时，无法判定是否为亲戚节点
			inline bool is_relatives(const node_type* ptr) const
			{
				if (!ptr) return false;
				return (ptr->root_ptr() == this->root_ptr());
			}
			//子向时，无法判定是否为兄弟节点
			bool is_sibling(const node_type* ptr) const
			{
				if (!ptr) return false;
				node_type* parent = this->parent_ptr();
				if (nullptr == parent) return false;
				return (ptr->parent_ptr() == parent);
			}
			//是否为祖先节点
			bool is_ancestor(const node_type* ptr) const
			{
				if (!ptr) return false;
				if (ptr == this) return false;
				const node_type* begin = static_cast<const node_type*>(this);
				node_type* parent = this->parent_ptr();
				while (parent && parent != begin)
				{
					if (parent == ptr) return true;
					parent = parent->parent_ptr();
				}
				//向上未命中：自 ptr 向下找 this（覆盖仅存子链的树）
				bool is_true = false;
				node_type& node_ref = *((node_type*)(ptr));
				node_ref.iterate_dfs([](node_type* _child_ptr_, const node_type* _this_node_, bool& _is_true_)
				{
					if (_child_ptr_ == _this_node_) _is_true_ = true;
				}, static_cast<const node_type*>(this), is_true);
				return is_true;
			}

			//是否为 ptr 的父（语义：我是 ptr 的父）
			bool is_parent_of(const node_type* ptr) const
			{
				if (!ptr) return false;
				if (ptr->parent_ptr() == this) return true;
				std::vector<node_type*> this_childs = this->childs();
				for (auto& child : this_childs)
					if (child == ptr)
						return true;
				return false;
			}
			//是否为 ptr 的子（语义：我是 ptr 的子）
			bool is_child_of(const node_type* ptr) const
			{
				if (!ptr) return false;
				if (this->parent_ptr() == ptr) return true;
				std::vector<node_type*> ptr_childs = ptr->childs();
				for (auto& child : ptr_childs)
					if (child == this)
						return true;
				return false;
			}
			inline bool is_root() const
			{
				return (this->has_parent() == false);
			}
			inline bool is_leaf() const
			{
				return (this->degree() == 0);
			}
		};

		//继承用树基础类型
		template<typename _node_type>
		class derive_tree_ : public tree_node_<_node_type, void, void, tree_dir::tree_nullptr, 0>
		{
		public:
			friend struct tree_node_<_node_type, void, void, tree_dir::tree_nullptr, 0>;
			using node_base = tree_node_<_node_type, void, void, tree_dir::tree_nullptr, 0>;
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
			virtual ~derive_tree_() {};
			constexpr explicit derive_tree_() {};
			constexpr explicit derive_tree_(derive_tree_&&) = delete;
			constexpr explicit derive_tree_(const derive_tree_&) = delete;
		protected:
			virtual bool _set_parent_ptr(node_type* parent_ptr_) = 0;
			virtual bool _add_child_ptr(node_type* child_ptr_) = 0;
			virtual bool _sub_child_ptr(node_type* child_ptr_) = 0;
		public:
			template<class type> type* ancestor_ptr() const
			{
				type* _ancestor_ptr = nullptr;
				auto _parent_ptr = this->parent_ptr();
				while (!_ancestor_ptr && _parent_ptr)
				{
					_ancestor_ptr = dynamic_cast<type*>(_parent_ptr);
					_parent_ptr = _parent_ptr->parent_ptr();
				}
				return _ancestor_ptr;
			}
		};


		//队queue广度 栈stack深度
		template<typename _tree_type, template<typename> typename _dock_type>
		struct tree_iterator_restraint
		{
			using tree_type = _tree_type;
			using node_type = typename tree_type::tree_node;
			using node_data = typename tree_type::node_data;
			using edge_data = typename tree_type::edge_data;
			using edge_data_ref = typename tree_type::edge_data_ref;
			using reference = typename tree_type::node_data_ref;
			using pointer = typename tree_type::node_data_ptr;
			using dock_stack = std::stack<node_type*>;
			using dock_queue = std::queue<node_type*>;
			using dock_type = _dock_type<node_type*>;
			using tree_ptr = tree_type*;
			using node_ptr = node_type*;
			//迭代器 traits（std::distance/count/find 等泛型算法可用）
			//void 节点数据时解引用得节点指针，否则得节点数据引用
			using value_type = typename ::xhqm::select_if<::xhqm::is_same<void, node_data>, node_type*, node_data>::type;
			using difference_type = std::ptrdiff_t;
			using iterator_category = std::forward_iterator_tag;

			constexpr static bool is_stack = xhqm::is_same<dock_type, dock_stack>;
			constexpr static bool is_queue = xhqm::is_same<dock_type, dock_queue>;
			static_assert((is_stack || is_queue), "未知类型的树形迭代器");
		};

		template<typename _iter_type, typename _tree_type, template<typename> typename _dock_type>
		class tree_iterator_ : public tree_iterator_restraint<_tree_type, _dock_type>
		{
		public:
			using tree_iterator = _iter_type;
			using tree_struct = tree_iterator_restraint<_tree_type, _dock_type>;
			using tree_type = typename tree_struct::tree_type;
			using node_type = typename tree_struct::node_type;
			using node_data = typename tree_struct::node_data;
			using dock_type = typename tree_struct::dock_type;
			using reference = typename tree_struct::reference;
			using pointer = typename tree_struct::pointer;
			using tree_ptr = typename tree_struct::tree_ptr;
			using node_ptr = typename tree_struct::node_ptr;
		protected: dock_type node_list;
				 constexpr inline node_ptr& first_ptr()
				 {
					 if constexpr (tree_struct::is_stack) { return this->node_list.top(); };
					 if constexpr (tree_struct::is_queue) { return this->node_list.front(); };
				 }
				 constexpr inline const node_ptr& first_ptr() const
				 {
					 if constexpr (tree_struct::is_stack) { return this->node_list.top(); };
					 if constexpr (tree_struct::is_queue) { return this->node_list.front(); };
				 }
				 constexpr tree_iterator_(node_ptr curr_ptr_)
				 {
					 if (curr_ptr_) { node_list.push(curr_ptr_); }
				 };
		public:
			template<typename __iter_type> constexpr operator __iter_type()
			{
				if (this->node_list.empty()) return __iter_type(nullptr);
				return __iter_type(this->first_ptr());
			};
		public:
			tree_iterator_(const tree_iterator_&) = default;   // 用户移动赋值会删除隐式拷贝构造，补回（std::distance 按值传参需要）

			constexpr tree_iterator operator =(tree_iterator&& move)
			{
				this->node_list = xhqm::move(move.node_list); return (*this);
			};
			constexpr tree_iterator operator =(const tree_iterator& copy)
			{
				this->node_list = copy.node_list; return (*this);
			};

			constexpr bool operator ==(const tree_iterator& tree_iter_) const {

				if (this->node_list.empty() && tree_iter_.node_list.empty()) return true;
				else if (this->node_list.size() && tree_iter_.node_list.empty()) return false;
				else if (this->node_list.empty() && tree_iter_.node_list.size()) return false;
				return (this->first_ptr() == tree_iter_.first_ptr());
			};
			constexpr bool operator !=(const tree_iterator& tree_iter_) const
			{
				if (this->node_list.empty() && tree_iter_.node_list.empty()) return false;
				else if (this->node_list.size() && tree_iter_.node_list.empty()) return true;
				else if (this->node_list.empty() && tree_iter_.node_list.size()) return true;
				return (this->first_ptr() != tree_iter_.first_ptr());
			};
			constexpr const reference operator *() const noexcept
			{
				if constexpr (xhqm::is_same<void, node_data>) { return (this->first_ptr()); };
				if constexpr (!xhqm::is_same<void, node_data>) { return this->first_ptr()->data; };
			};
			constexpr reference operator *() noexcept
			{
				if constexpr (xhqm::is_same<void, node_data>) { return (this->first_ptr()); };
				if constexpr (!xhqm::is_same<void, node_data>) { return this->first_ptr()->data; };
			};
			constexpr pointer operator->() const noexcept
			{
				if constexpr (xhqm::is_same<void, node_data>)
				{
					if (this->node_list.size()) return (this->first_ptr());
				};
				if constexpr (!xhqm::is_same<void, node_data>)
				{
					if (this->node_list.size()) return &(this->first_ptr()->data);
				};
				return nullptr;
			}
		};
	}

	//广度遍历
	template<typename _node_type, typename _func_type, typename... _argv_type>
	inline void iterate_bfs(_node_type* tree_node_, _func_type&& function_, _argv_type&& ... argv_data_)
	{
		tree_node_->iterate_bfs(std::forward<_func_type>(function_), std::forward<_argv_type>(argv_data_)...);
	}
	//深度遍历
	template<typename _node_type, typename _func_type, typename... _argv_type>
	inline void iterate_dfs(_node_type* tree_node_, _func_type&& function_, _argv_type&& ... argv_data_)
	{
		tree_node_->iterate_dfs(std::forward<_func_type>(function_), std::forward<_argv_type>(argv_data_)...);
	}
	//先序遍历（迭代：栈 + 逆序压子，深树不爆栈）
	template<typename _node_type, typename _func_type, typename... _argv_type>
	static void iterate_pre(_node_type* tree_node_, _func_type&& function_, _argv_type&& ... argv_data_)
	{
		std::stack<_node_type*> stack;
		stack.push(tree_node_);
		while (!stack.empty())
		{
			_node_type* node = stack.top(); stack.pop();
			function_(node, std::forward<_argv_type>(argv_data_)...);
			std::vector<_node_type*> childs_list = node->childs();
			for (auto it = childs_list.rbegin(); it != childs_list.rend(); ++it)
				stack.push(*it);
		}
	}
	//中序遍历（迭代：叶子直接调用；非叶 = mid(首子) → 自身 → mid(其余子)）
	template<typename _node_type, typename _func_type, typename... _argv_type>
	static void iterate_mid(_node_type* tree_node_, _func_type&& function_, _argv_type&& ... argv_data_)
	{
		std::stack<std::pair<_node_type*, bool>> stack;
		stack.push({ tree_node_, false });
		while (!stack.empty())
		{
			auto frame = stack.top(); stack.pop();
			_node_type* node = frame.first;
			if (frame.second)
			{
				function_(node, std::forward<_argv_type>(argv_data_)...);
				continue;
			}
			std::vector<_node_type*> childs_list = node->childs();
			if (childs_list.empty())
			{
				function_(node, std::forward<_argv_type>(argv_data_)...);
				continue;
			}
			// 执行顺序：mid(childs[0]) → 自身 → mid(childs[1..])；压栈取逆序
			for (xhqm::size i = childs_list.size() - 1; i >= 1; --i)
				stack.push({ childs_list[i], false });
			stack.push({ node, true });
			stack.push({ childs_list[0], false });
		}
	}
	//后序遍历（迭代：标记栈 enter 展开子 / exit 调用）
	template<typename _node_type, typename _func_type, typename... _argv_type>
	static void iterate_bak(_node_type* tree_node_, _func_type&& function_, _argv_type&& ... argv_data_)
	{
		std::stack<std::pair<_node_type*, bool>> stack;
		stack.push({ tree_node_, false });
		while (!stack.empty())
		{
			auto frame = stack.top(); stack.pop();
			_node_type* node = frame.first;
			if (frame.second)
			{
				function_(node, std::forward<_argv_type>(argv_data_)...);
				continue;
			}
			stack.push({ node, true });
			std::vector<_node_type*> childs_list = node->childs();
			for (auto it = childs_list.rbegin(); it != childs_list.rend(); ++it)
				stack.push({ *it, false });
		}
	}

	//广度遍历//层序遍历
	template<typename _tree_type>
	class tree_bfs_iterator : public _graph_::tree_iterator_<tree_bfs_iterator<_tree_type>, _tree_type, std::queue>
	{
		using tree_iterator = _graph_::tree_iterator_<tree_bfs_iterator<_tree_type>, _tree_type, std::queue>;
	public:
		using tree_type = typename tree_iterator::tree_type;
		using node_type = typename tree_iterator::node_type;
		using node_data = typename tree_iterator::node_data;
		using dock_type = typename tree_iterator::dock_type;
		using reference = typename tree_iterator::reference;
		using pointer = typename tree_iterator::pointer;
		using tree_ptr = typename tree_iterator::tree_ptr;
		using node_ptr = typename tree_iterator::node_ptr;
	public:
		constexpr tree_bfs_iterator(node_ptr curr_ptr_ = nullptr) : tree_iterator(curr_ptr_) {};
		constexpr tree_bfs_iterator& next()
		{
			if (this->node_list.size())
			{
				auto curr_ptr = this->first_ptr();
				auto childs = curr_ptr->childs();
				for (auto& child_ptr : childs)
					this->node_list.push(child_ptr);
				this->node_list.pop();
			}
			return (*this);
		};
		constexpr tree_bfs_iterator& operator ++() { return this->next(); };
	};
	//深度遍历//先序遍历
	template<typename _tree_type>
	class tree_dfs_iterator : public _graph_::tree_iterator_<tree_dfs_iterator<_tree_type>, _tree_type, std::stack>
	{
		using tree_iterator = _graph_::tree_iterator_<tree_dfs_iterator<_tree_type>, _tree_type, std::stack>;
	public:
		using tree_type = typename tree_iterator::tree_type;
		using node_type = typename tree_iterator::node_type;
		using node_data = typename tree_iterator::node_data;
		using dock_type = typename tree_iterator::dock_type;
		using reference = typename tree_iterator::reference;
		using pointer = typename tree_iterator::pointer;
		using tree_ptr = typename tree_iterator::tree_ptr;
		using node_ptr = typename tree_iterator::node_ptr;
	public:
		constexpr tree_dfs_iterator(node_ptr curr_ptr_ = nullptr) : tree_iterator(curr_ptr_) {};
		constexpr tree_dfs_iterator& next()
		{
			if (this->node_list.size())
			{
				auto curr_ptr = this->first_ptr();
				auto childs = curr_ptr->childs();
				::std::reverse(childs.begin(), childs.end());

				this->node_list.pop();
				for (auto& child_ptr : childs)
					this->node_list.push(child_ptr);
			}
			return (*this);
		};
		constexpr tree_dfs_iterator& operator ++() { return this->next(); };
	};

	//统一深度遍历迭代器（标记栈 + 位置参数；range-for 可用）：
	// index=0 先序（根最先）；index=n 中序（根插在第 n 个子树之间）；
	// index=npos 后序（根最后）。与 tree_node_::_iterate_dfs 的 index 语义一致
	template<typename _tree_type, xhqm::index index>
	class tree_dfs_index_iterator
	{
		using node_type = typename _tree_type::tree_node;
	public:
		//迭代器 traits：解引用得 node_type*（与 void 数据族迭代器一致）
		using value_type = node_type*;
		using reference = node_type*;
		using pointer = node_type**;
		using difference_type = std::ptrdiff_t;
		using iterator_category = std::forward_iterator_tag;
	private:
		std::stack<std::pair<node_type*, bool>> m_stack;
		void advance()
		{
			while (!m_stack.empty())
			{
				auto frame = m_stack.top(); m_stack.pop();
				if (frame.second) { m_stack.push(frame); return; }   // 放回：当前节点保持在栈顶
				std::vector<node_type*> ch = frame.first->childs();
				std::vector<node_type*> pre;
				std::vector<node_type*> post;
				if (ch.size() <= index)
				{
					pre = ch;   // 根最后（后序 / 中序越界退化为后序）
				}
				else
				{
					pre.assign(ch.begin(), ch.begin() + index);
					post.assign(ch.begin() + index, ch.end());
				}
				// 执行顺序：pre 正序 → 自身 → post 正序；压栈取逆序
				for (auto it = post.rbegin(); it != post.rend(); ++it)
					m_stack.push({ *it, false });
				m_stack.push({ frame.first, true });
				for (auto it = pre.rbegin(); it != pre.rend(); ++it)
					m_stack.push({ *it, false });
			}
		}
	public:
		constexpr tree_dfs_index_iterator(node_type* curr_ptr_ = nullptr)
		{
			if (curr_ptr_) m_stack.push({ curr_ptr_, false });
			advance();
		};
		node_type* operator*() const noexcept { return m_stack.top().first; }
		constexpr tree_dfs_index_iterator& operator ++() { m_stack.pop(); advance(); return *this; };
		constexpr tree_dfs_index_iterator operator ++(int) { tree_dfs_index_iterator tmp(*this); ++(*this); return tmp; };
		constexpr bool operator ==(const tree_dfs_index_iterator& iter_) const { return m_stack.empty() && iter_.m_stack.empty(); };
		constexpr bool operator !=(const tree_dfs_index_iterator& iter_) const { return !(*this == iter_); };
	};

	//先序遍历迭代器
	template<typename _tree_type>
	using tree_pre_iterator = tree_dfs_index_iterator<_tree_type, 0>;

	//中序遍历迭代器
	template<typename _tree_type, xhqm::index index>
	using tree_mid_iterator = tree_dfs_index_iterator<_tree_type, index>;

	//后序遍历迭代器
	template<typename _tree_type>
	using tree_bak_iterator = tree_dfs_index_iterator<_tree_type, xhqm::npos>;

	//通用树形迭代器
	template<typename _tree_type> using tree_iterator = tree_bfs_iterator<_tree_type>;


	//通用继承用链表类型
	template<typename _node_type>
	class derive_tree : public xhqm::_graph_::derive_tree_<_node_type>
	{
	public:
		using iterator = xhqm::tree_iterator<_node_type>;
		using tree_base = xhqm::_graph_::derive_tree_<_node_type>;
		using tree_node = typename tree_base::node_type;
		using node_type = typename tree_base::node_type;
		using edge_type = typename tree_base::edge_type;
		using node_data_ref = node_type*;
		using edge_data_ref = void;
		using node_data_ptr = node_type*;
		using edge_data_ptr = void;
		using node_data = void;
		using edge_data = void;
		using node_ptr = node_type*;
		using node_ref = node_type&;
	public:
		virtual bool _set_parent_ptr(node_type* new_parent_ptr) override final
		{
			// 环防护：拒绝自身为父、拒绝后代为父（会成环）。
			// 短路：this 无孩子则无后代；new_parent 是根则无祖先——均不可能成环，跳过深查
			// （深树链式建树 O(N²) 的主要来源，短路后摊还 O(1)）
			if (new_parent_ptr == static_cast<node_type*>(this)) return false;
			if (new_parent_ptr && this->degree() != 0 && !new_parent_ptr->is_root()
				&& new_parent_ptr->is_ancestor(static_cast<const node_type*>(this))) return false;

			// 断开旧节点的原有链接
			node_ptr old_parent_ptr = this->parent_ptr();
			if (old_parent_ptr == new_parent_ptr) return true;

			if (old_parent_ptr)
			{
				auto index = old_parent_ptr->child_index(static_cast<node_type*>(this));
				if (index != xhqm::npos)
					old_parent_ptr->childs_loca.erase(old_parent_ptr->childs_loca.begin() + index);
			}
			if (new_parent_ptr)
			{
				new_parent_ptr->childs_loca.push_back(static_cast<node_type*>(this));
			}
			this->parent_loca = new_parent_ptr;
			return true;
		}
		virtual bool _add_child_ptr(node_type* child_ptr_) override final
		{
			if (child_ptr_)
				return child_ptr_->_set_parent_ptr(static_cast<node_type*>(this));
			return false;
		}
		virtual bool _sub_child_ptr(node_type* child_ptr_) override final
		{
			if (child_ptr_)
				return child_ptr_->_set_parent_ptr(nullptr);
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

	class test_tree : public derive_tree<test_tree>
	{
	public:
		char value = 0;
		test_tree(char v = 0) :value(v) {};
	};

	//test_tree tree;
}

#endif
