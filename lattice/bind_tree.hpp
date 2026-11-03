/*
*作者：星辉清梦
*时间：龙—甲辰丙子丁未庚子
*用途：双向树
* 性质：
* 1、可遍历，即可自一个节点向上遍历到根，也可向下遍历到叶子
* 2、可添加当前数据的父节点数据
* 3、可添加当前数据的子节点数据
* 4、当前项可以向上移动也可以向下移动
* 5、叶子节点是无序的，插入顺序非访问顺序
* 说明：
*	边为双向边（edge_bind），父子两节点共享同一条边对象：
*	边记录 from_loca=父节点、goto_loca=子节点；
*	子节点 parent_loca 与父节点 childs_loca[index] 指向同一边。
*/

#pragma once
#ifndef XHQM_META_BIND_TREE
#define XHQM_META_BIND_TREE
#include "./struct.h"
#include "./tree.hpp"

namespace xhqm
{
	namespace _graph_
	{
		//记录父节点与子节点
		template<typename _node_data, typename _edge_data = void>
		struct bind_tree_node : public tree_node_<bind_tree_node<_node_data, _edge_data>, _node_data, _edge_data, tree_dir::tree_bind, 0>
		{
			friend struct tree_node_<bind_tree_node<_node_data, _edge_data>, _node_data, _edge_data, tree_dir::tree_bind, 0>;
			using node_base = tree_node_<bind_tree_node<_node_data, _edge_data>, _node_data, _edge_data, tree_dir::tree_bind, 0>;
			using node_data_ref = typename node_base::node_data_ref;
			using edge_data_ref = typename node_base::edge_data_ref;
			using node_data_ptr = typename node_base::node_data_ptr;
			using edge_data_ptr = typename node_base::edge_data_ptr;
			using loca_type = typename node_base::loca_type;
			using node_type = typename node_base::node_type;
			using edge_type = typename node_base::edge_type;
			using node_data = typename node_base::node_data;
			using edge_data = typename node_base::edge_data;
			//friend struct node_base;
		protected:
			bool _set_parent_ptr(node_type* new_parent_ptr)
			{
				// 环防护：拒绝自身为父、拒绝后代为父。
				// 短路：this 无孩子则无后代；new_parent 是根则无祖先——均不可能成环，跳过深查
				if (new_parent_ptr == static_cast<node_type*>(this)) return false;
				if (new_parent_ptr && this->degree() != 0 && !new_parent_ptr->is_root()
					&& new_parent_ptr->is_ancestor(static_cast<const node_type*>(this))) return false;

				node_type* old_parent_ptr = this->parent_ptr();
				if (old_parent_ptr == new_parent_ptr) return true;

				//断开旧父节点的链接（销毁父子共享的边对象）
				if (old_parent_ptr)
				{
					loca_type* this_parent_edge = this->parent_loca;
					for (auto iter = old_parent_ptr->childs_loca.begin(); iter != old_parent_ptr->childs_loca.end(); ++iter)
					{
						if ((*iter)->goto_loca == static_cast<node_type*>(this))
						{
							old_parent_ptr->childs_loca.erase(iter);
							break;
						}
					}
					this->parent_loca = nullptr;
					delete this_parent_edge;
				}
				//链接新父节点（新建父子共享的边对象）
				if (new_parent_ptr)
				{
					loca_type* new_parent_edge = new loca_type();
					new_parent_edge->from_loca = new_parent_ptr;
					new_parent_edge->goto_loca = static_cast<node_type*>(this);
					new_parent_ptr->childs_loca.push_back(new_parent_edge);
					this->parent_loca = new_parent_edge;
				}
				return true;
			}
			bool _add_child_ptr(node_type* child_ptr_)
			{
				if (child_ptr_)
					return child_ptr_->_set_parent_ptr(static_cast<node_type*>(this));
				return false;
			}
			bool _sub_child_ptr(node_type* child_ptr_)
			{
				if (child_ptr_)
					return child_ptr_->_set_parent_ptr(nullptr);
				return false;
			}
		public:
			bind_tree_node() {};
			~bind_tree_node() { _set_parent_ptr(nullptr); };

			template<typename __data_type> bind_tree_node(__data_type&& data_) noexcept
				: node_base(xhqm::move(data_))
			{

			}
			template<typename __data_type> bind_tree_node(const __data_type& data_) noexcept
				: node_base(xhqm::copy(data_))
			{

			}
		};
	}

	template<typename _node_data, typename _edge_data = void>
	class bind_tree
	{
	public:
		using tree_node = xhqm::_graph_::bind_tree_node<_node_data, _edge_data>;
		using node_data_ref = typename tree_node::node_data_ref;
		using edge_data_ref = typename tree_node::edge_data_ref;
		using node_data_ptr = typename tree_node::node_data_ptr;
		using edge_data_ptr = typename tree_node::edge_data_ptr;
		using node_data = typename tree_node::node_data;
		using edge_data = typename tree_node::edge_data;
		using iterator = xhqm::tree_iterator<bind_tree>;
		using node_ptr = tree_node*;
		using node_ref = tree_node&;
	protected:
		xhqm::count m_count = 0;
		node_ptr root_ptr = nullptr;
		node_ptr curr_ptr = nullptr;
	public:
		const edge_data_ref edge_of_parent() const
		{
			if constexpr (!xhqm::is_same<void, edge_data>)
			{
				return curr_ptr->parent_edge()->data;
			}
		};
		edge_data_ref edge_of_parent()
		{
			if constexpr (!xhqm::is_same<void, edge_data>)
			{
				return curr_ptr->parent_edge()->data;
			}
		};
		const edge_data_ref edge_of_child(const xhqm::index& index) const
		{
			if constexpr (!xhqm::is_same<void, edge_data>)
			{
				return curr_ptr->child_edge(index)->data;
			}
		};
		edge_data_ref edge_of_child(const xhqm::index& index)
		{
			if constexpr (!xhqm::is_same<void, edge_data>)
			{
				return curr_ptr->child_edge(index)->data;
			}
		};

		inline bool empty() const noexcept { return root_ptr == nullptr; };
		inline xhqm::size size() const noexcept { return m_count; };

		void clear() noexcept
		{
			if (this->root_ptr)
			{
				xhqm::iterate_bak(this->root_ptr, [](node_ptr node_ptr)
				{
					delete node_ptr;
				});
				this->root_ptr = nullptr;
				this->curr_ptr = nullptr;
				this->m_count = 0;
			}
		}

		inline node_data_ref curr() { return curr_ptr->data; };
		inline const node_data_ref curr() const { return curr_ptr->data; };
		inline node_data_ref root() { return root_ptr->data; };
		inline const node_data_ref root() const { return root_ptr->data; };

		//begin是自身
		inline iterator begin() const noexcept { return iterator(root_ptr); };
		//begin是自身
		inline iterator begin() noexcept { return iterator(root_ptr); };
		//end是nullptr
		inline iterator end() const noexcept { return iterator(nullptr); };
		//end是nullptr
		inline iterator end() noexcept { return iterator(nullptr); };

		//添加当前项的子节点
		void push_child(const node_data& data, bool to_child = false)
		{
			if (!root_ptr)
			{
				root_ptr = new tree_node(data);
				curr_ptr = root_ptr;
			}
			else
			{
				auto new_child_ptr = new tree_node(data);
				curr_ptr->add_child_ptr(new_child_ptr);
				if (to_child) curr_ptr = new_child_ptr;
			}
			++m_count;
		};
		//添加当前项的子节点
		void push_child(node_data&& data, bool to_child = false)
		{
			if (!root_ptr)
			{
				root_ptr = new tree_node(xhqm::move(data));
				curr_ptr = root_ptr;
			}
			else
			{
				auto new_child_ptr = new tree_node(xhqm::move(data));
				curr_ptr->add_child_ptr(new_child_ptr);
				if (to_child) curr_ptr = new_child_ptr;
			}
			++m_count;
		};
		//添加当前项的父节点
		void push_parent(const node_data& data)
		{
			if (!root_ptr)
			{
				root_ptr = new tree_node(data);
				curr_ptr = root_ptr;
			}
			else
			{
				auto new_parent_ptr = new tree_node(data);
				auto old_parent_ptr = curr_ptr->parent_ptr();
				new_parent_ptr->set_parent_ptr(old_parent_ptr);
				curr_ptr->set_parent_ptr(new_parent_ptr);
				if (old_parent_ptr == nullptr) root_ptr = new_parent_ptr;
			}
			++m_count;
		};
		//添加当前项的父节点
		void push_parent(node_data&& data)
		{
			if (!root_ptr)
			{
				root_ptr = new tree_node(xhqm::move(data));
				curr_ptr = root_ptr;
			}
			else
			{
				auto new_parent_ptr = new tree_node(xhqm::move(data));
				auto old_parent_ptr = curr_ptr->parent_ptr();
				new_parent_ptr->set_parent_ptr(old_parent_ptr);
				curr_ptr->set_parent_ptr(new_parent_ptr);
				if (old_parent_ptr == nullptr) root_ptr = new_parent_ptr;
			}
			++m_count;
		};
		//添加根节点
		void push_root(const node_data& data, bool to_root = false)
		{
			if (!root_ptr)
			{
				root_ptr = new tree_node(data);
				curr_ptr = root_ptr;
			}
			else
			{
				auto new_root_ptr = new tree_node(data);
				new_root_ptr->add_child_ptr(root_ptr);
				if (to_root) curr_ptr = new_root_ptr;
				root_ptr = new_root_ptr;
			}
			++m_count;
		};
		//添加根节点
		void push_root(node_data&& data, bool to_root = false)
		{
			if (!root_ptr)
			{
				root_ptr = new tree_node(xhqm::move(data));
				curr_ptr = root_ptr;
			}
			else
			{
				auto new_root_ptr = new tree_node(xhqm::move(data));
				new_root_ptr->add_child_ptr(root_ptr);
				if (to_root) curr_ptr = new_root_ptr;
				root_ptr = new_root_ptr;
			}
			++m_count;
		};

		//抛出一个子节点及其子树
		bool pop_child(const xhqm::index& index)
		{
			auto childs_list = curr_ptr->childs();
			if (index >= childs_list.size()) return false;
			xhqm::count removed = 0;
			xhqm::iterate_bak(childs_list[index], [&removed](node_ptr node_ptr_)
			{
				delete node_ptr_;
				++removed;
			});
			this->m_count -= removed;
			return true;
		};

		//抛出当前节点及其子树
		bool pop_curr()
		{
			if (!curr_ptr) return false;
			node_ptr curr_parent_ptr = curr_ptr->parent_ptr();
			bool was_root = (curr_ptr == root_ptr);
			xhqm::count removed = 0;
			xhqm::iterate_bak(curr_ptr, [&removed](node_ptr node_ptr_)
			{
				delete node_ptr_;
				++removed;
			});
			this->m_count -= removed;
			if (was_root) root_ptr = nullptr;
			curr_ptr = curr_parent_ptr;
			return true;
		};


		//向子节点移动
		inline bool to_child(const xhqm::index& index)
		{
			auto childs_list = curr_ptr->childs();
			if (index >= childs_list.size()) return false;
			curr_ptr = childs_list[index];
			return true;
		};
		//向父节点移动
		inline bool to_parent()
		{
			auto parent_ptr = curr_ptr->parent_ptr();
			if (parent_ptr) curr_ptr = parent_ptr;
			return parent_ptr;
		};

		inline bool is_root() const { return (curr_ptr == root_ptr); };

		inline void curr_root() { curr_ptr = root_ptr; };

		//深度遍历所有节点
		template<typename _func_type, typename... _argv_type>
		void traverse(_func_type&& function_, _argv_type&& ... argv_data_)
		{
			xhqm::iterate_dfs(curr_ptr, std::forward<_func_type>(function_), std::forward<_argv_type>(argv_data_)...);
		}

	public:
		inline bind_tree& operator < (const node_data& data_) noexcept
		{
			curr_ptr->data = data_; return *this;
		};
		inline bind_tree& operator > (node_data& data_) const noexcept
		{
			data_ = curr_ptr->data; return *this;
		};


	public:
		constexpr bind_tree& operator =(bind_tree&& move_) noexcept
		{
			this->clear();
			this->m_count = move_.m_count;
			this->root_ptr = move_.root_ptr;
			this->curr_ptr = move_.curr_ptr;
			move_.m_count = 0;
			move_.root_ptr = nullptr;
			move_.curr_ptr = nullptr;
			return (*this);
		};

		constexpr inline void swap(bind_tree& tree_) noexcept
		{
			auto temp_count = this->m_count;
			auto temp_root_ptr = this->root_ptr;
			auto temp_curr_ptr = this->curr_ptr;
			this->m_count = tree_.m_count;
			this->root_ptr = tree_.root_ptr;
			this->curr_ptr = tree_.curr_ptr;
			tree_.m_count = temp_count;
			tree_.root_ptr = temp_root_ptr;
			tree_.curr_ptr = temp_curr_ptr;
		}

		//禁止拷贝构造
		bind_tree(const bind_tree& copy_) noexcept = delete;

		bind_tree(bind_tree&& move_) noexcept
		{
			this->m_count = move_.m_count;
			this->root_ptr = move_.root_ptr;
			this->curr_ptr = move_.curr_ptr;
			move_.m_count = 0;
			move_.root_ptr = nullptr;
			move_.curr_ptr = nullptr;
		};
		bind_tree() noexcept
		{
			this->root_ptr = nullptr;
			this->curr_ptr = nullptr;
			this->m_count = 0;
		};
		~bind_tree() noexcept
		{
			this->clear();
		};
	};
}

#endif
