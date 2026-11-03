/*
*作者：星辉清梦
*时间：龙—甲辰丙子丙午戊戌
*用途：父向树
* 性质：
* 1、无法遍历，仅能自一个叶节点向上遍历到根节点
* 2、可添加当前数据的父节点数据
* 3、可添加当前数据的子节点数据
* 4、当前项仅可向父节点移动
* 5、叶子节点是无序的，插入顺序非访问顺序
*/

#pragma once
#ifndef XHQM_META_PARENT_TREE
#define XHQM_META_PARENT_TREE
#include "./ring_list.hpp"
#include "./tree.hpp"
#include <set>
namespace xhqm
{
	namespace _graph_
	{
		//记录父节点
		template<typename _node_data, typename _edge_data = void>
		struct parent_node : public tree_node_<parent_node<_node_data, _edge_data>, _node_data, _edge_data, tree_dir::tree_parent, 0>
		{
			friend struct tree_node_<parent_node<_node_data, _edge_data>, _node_data, _edge_data, tree_dir::tree_parent, 0>;
			using node_base = tree_node_<parent_node<_node_data, _edge_data>, _node_data, _edge_data, tree_dir::tree_parent, 0>;
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

				// 断开旧节点的原有链接
				node_type* old_parent_ptr = this->parent_ptr();
				if (old_parent_ptr == new_parent_ptr) return true;

				if (old_parent_ptr)
				{
					old_parent_ptr->childs_loca.pop_back();
				}
				if (new_parent_ptr)
				{
					new_parent_ptr->childs_loca.push_back(true);
				}
				// 写入父链接（新父为空时同样清空，防止残留旧父）
				if constexpr (xhqm::is_same<void, edge_data>)
				{
					this->parent_loca = new_parent_ptr;
				}
				else
				{
					this->parent_loca->from_loca = new_parent_ptr;
				};
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
			parent_node()
			{
				if constexpr (!xhqm::is_same<void, edge_data>)
				{
					this->parent_loca = new loca_type();
					this->parent_loca->goto_loca = true;
				}
			};
			~parent_node()
			{
				_set_parent_ptr(nullptr);
				if constexpr (!xhqm::is_same<void, edge_data>)
				{
					delete this->parent_loca;
				}
			};

			template<typename __data_type> parent_node(__data_type&& data_) noexcept
				: node_base(xhqm::move(data_))
			{
				if constexpr (!xhqm::is_same<void, edge_data>)
				{
					this->parent_loca = new loca_type();
					this->parent_loca->goto_loca = true;
				}
			}
			template<typename __data_type> parent_node(const __data_type& data_) noexcept
				: node_base(xhqm::copy(data_))
			{
				if constexpr (!xhqm::is_same<void, edge_data>)
				{
					this->parent_loca = new loca_type();
					this->parent_loca->goto_loca = true;
				}
			}
		};
	}

	template<typename _node_data, typename _edge_data = void>
	class parent_tree
	{
	public:
		using tree_node = xhqm::_graph_::parent_node<_node_data, _edge_data>;
		using node_data_ref = typename tree_node::node_data_ref;
		using edge_data_ref = typename tree_node::edge_data_ref;
		using node_data_ptr = typename tree_node::node_data_ptr;
		using edge_data_ptr = typename tree_node::edge_data_ptr;
		using node_data = typename tree_node::node_data;
		using edge_data = typename tree_node::edge_data;
		using node_ptr = tree_node*;
		using node_ref = tree_node&;
	protected:
		//leafs向上指向root
		xhqm::count m_count = 0;
		node_ptr root_ptr = nullptr;
		node_ptr curr_ptr = nullptr;
		std::set<node_ptr> leaf_set;
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

		inline bool empty() const noexcept { return root_ptr == nullptr; };
		inline xhqm::size size() const noexcept { return m_count; };
		inline xhqm::size leaf_size() const noexcept { return leaf_set.size(); };
		void clear() noexcept
		{
			std::set<node_ptr> delete_set = leaf_set;
			while (leaf_set.size())
			{
				std::set<node_ptr> parent_set;
				for (auto& leaf_ptr : leaf_set)
				{
					node_ptr parent_ptr = leaf_ptr->parent_ptr();
					delete_set.insert(parent_ptr);
					parent_set.insert(parent_ptr);
				}
				parent_set.erase(nullptr);
				leaf_set = parent_set;
			}
			delete_set.erase(nullptr);
			xhqm::ring_list<node_ptr> delete_list(delete_set.begin(), delete_set.end());
			while (delete_list.size())
			{
				if (delete_list.curr()->is_leaf())
				{
					delete delete_list.curr();
					delete_list.pop_curr();
				}
				else
				{
					delete_list.next();
				}
			}

			this->root_ptr = nullptr;
			this->curr_ptr = nullptr;
			this->m_count = 0;
		}

		inline node_data_ref curr() { return curr_ptr->data; };
		inline const node_data_ref curr() const { return curr_ptr->data; };
		inline node_data_ref root() { return root_ptr->data; };
		inline const node_data_ref root() const { return root_ptr->data; };


		//添加当前项的父节点
		void push_parent(const node_data& data)
		{
			if (!root_ptr)
			{
				root_ptr = new tree_node(data);
				leaf_set.insert(root_ptr);
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
		//添加当前项的子节点
		void push_child(const node_data& data, bool to_child = false)
		{
			leaf_set.erase(curr_ptr);
			if (!root_ptr)
			{
				root_ptr = new tree_node(data);
				leaf_set.insert(root_ptr);
				curr_ptr = root_ptr;
			}
			else
			{
				auto new_child_ptr = new tree_node(data);
				new_child_ptr->set_parent_ptr(curr_ptr);
				leaf_set.insert(new_child_ptr);
				if (to_child) curr_ptr = new_child_ptr;
			}
			++m_count;
		};
		//添加根节点
		void push_root(const node_data& data, bool to_root = false)
		{
			if (!root_ptr)
			{
				root_ptr = new tree_node(data);
				leaf_set.insert(root_ptr);
				curr_ptr = root_ptr;
			}
			else
			{
				auto new_root_ptr = new tree_node(data);
				root_ptr->set_parent_ptr(new_root_ptr);
				if (to_root) curr_ptr = new_root_ptr;
				root_ptr = new_root_ptr;
			}
			++m_count;
		};
		//添加当前项的父节点
		void push_parent(node_data&& data)
		{
			if (!root_ptr)
			{
				root_ptr = new tree_node(xhqm::move(data));
				leaf_set.insert(root_ptr);
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
		//添加当前项的子节点
		void push_child(node_data&& data, bool to_child = false)
		{
			leaf_set.erase(curr_ptr);
			if (!root_ptr)
			{
				root_ptr = new tree_node(xhqm::move(data));
				leaf_set.insert(root_ptr);
				curr_ptr = root_ptr;
			}
			else
			{
				auto new_child_ptr = new tree_node(xhqm::move(data));
				new_child_ptr->set_parent_ptr(curr_ptr);
				leaf_set.insert(new_child_ptr);
				if (to_child) curr_ptr = new_child_ptr;
			}
			++m_count;
		};
		//添加根节点
		void push_root(node_data&& data, bool to_root = false)
		{
			if (!root_ptr)
			{
				root_ptr = new tree_node(xhqm::move(data));
				leaf_set.insert(root_ptr);
				curr_ptr = root_ptr;
			}
			else
			{
				auto new_root_ptr = new tree_node(xhqm::move(data));
				root_ptr->set_parent_ptr(new_root_ptr);
				if (to_root) curr_ptr = new_root_ptr;
				root_ptr = new_root_ptr;
			}
			++m_count;
		};

		//抛出当前节点
		bool pop_curr()
		{
			if (!curr_ptr) return false;
			if (curr_ptr == root_ptr)
			{
				this->clear();
				return true;
			}

			//获取相关的叶子节点汇总至curr_leaf_set
			std::set<node_ptr> curr_leaf_set;
			for (auto& leaf_ptr : leaf_set)
			{
				if (leaf_ptr->is_ancestor(curr_ptr))
				{
					curr_leaf_set.insert(leaf_ptr);
				}
			}
			//区分相关的叶子节点和不相关的叶子节点
			for (auto& leaf_ptr : curr_leaf_set)
				leaf_set.erase(leaf_ptr);

			//将需要删除的节点汇总至delete_set
			auto curr_parent_ptr = curr_ptr->parent_ptr();
			std::set<node_ptr> delete_set = curr_leaf_set;
			while (curr_leaf_set.size())
			{
				std::set<node_ptr> parent_set;
				for (auto& leaf_ptr : curr_leaf_set)
				{
					node_ptr parent_ptr = leaf_ptr->parent_ptr();
					delete_set.insert(parent_ptr);
					parent_set.insert(parent_ptr);
				}
				parent_set.erase(nullptr);
				parent_set.erase(curr_ptr);
				curr_leaf_set = parent_set;
			}

			//删除当前汇总的节点
			delete_set.erase(nullptr);
			this->m_count = this->m_count - delete_set.size();
			xhqm::ring_list<node_ptr> delete_list(delete_set.begin(), delete_set.end());
			while (delete_list.size())
			{
				if (delete_list.curr()->is_leaf())
				{
					delete delete_list.curr();
					delete_list.pop_curr();
				}
				else
				{
					delete_list.next();
				}
			}

			//移动当前节点至其父节点
			curr_ptr = curr_parent_ptr;

			//判断当前节点是否为叶子节点
			if (curr_ptr->degree() == 0)
				leaf_set.insert(curr_ptr);

			return true;
		};

		//向父节点移动
		inline bool to_parent() 
		{ 
			auto parent_ptr = curr_ptr->parent_ptr();
			if (parent_ptr) curr_ptr = parent_ptr;
			return parent_ptr;
		};
		//移向某个叶子节点
		inline bool to_leaf(xhqm::index index)
		{ 
			if (index >= leaf_set.size()) return false;
			auto begin = leaf_set.begin();
			while (index)
			{
				++begin;
				--index;
			}
			curr_ptr = *begin;
			return true;
		};

		inline bool is_root() const { return (curr_ptr == root_ptr); };

		inline void curr_root() { curr_ptr = root_ptr; };

		//无序遍历所有节点（回调收 node*，与树族其余 traverse 一致）
		template<typename _func_type, typename... _argv_type>
		void traverse(_func_type&& function_, _argv_type&& ... argv_data_)
		{
			std::set<node_ptr> temp_set = leaf_set;
			std::set<node_ptr> traverse_set = temp_set;
			while (temp_set.size())
			{
				std::set<node_ptr> parent_set;
				for (auto& leaf_ptr : temp_set)
				{
					node_ptr parent_ptr = leaf_ptr->parent_ptr();
					traverse_set.insert(parent_ptr);
					parent_set.insert(parent_ptr);
				}
				parent_set.erase(nullptr);
				temp_set = parent_set;
			}
			traverse_set.erase(nullptr);
			std::vector<node_ptr> traverse_list(traverse_set.begin(), traverse_set.end());
			for (auto& traverse_ptr : traverse_list)
			{
				function_(traverse_ptr, std::forward<_argv_type>(argv_data_)...);
			}
		}

	public:
		inline parent_tree& operator < (const node_data& data_) noexcept
		{
			curr_ptr->data = data_; return *this;
		};
		inline parent_tree& operator > (node_data& data_) const noexcept
		{
			data_ = curr_ptr->data; return *this;
		};

	public:
		constexpr parent_tree& operator =(parent_tree&& move_) noexcept
		{
			this->clear();
			this->m_count = move_.m_count;
			this->root_ptr = move_.root_ptr;
			this->curr_ptr = move_.curr_ptr;
			this->leaf_set = std::move(move_.leaf_set);
			move_.m_count = 0;
			move_.root_ptr = nullptr;
			move_.curr_ptr = nullptr;
			return (*this);
		};

		constexpr inline void swap(parent_tree& tree_) noexcept
		{
			auto temp_count = this->m_count;
			auto temp_root_ptr = this->root_ptr;
			auto temp_curr_ptr = this->curr_ptr;
			auto temp_leaf_set = this->leaf_set;
			this->m_count = tree_.m_count;
			this->root_ptr = tree_.root_ptr;
			this->curr_ptr = tree_.curr_ptr;
			this->leaf_set = tree_.leaf_set;
			tree_.m_count = temp_count;
			tree_.root_ptr = temp_root_ptr;
			tree_.curr_ptr = temp_curr_ptr;
			tree_.leaf_set = temp_leaf_set;
		}

		//禁止拷贝构造
		parent_tree(const parent_tree& copy_) noexcept = delete;

		parent_tree(parent_tree&& move_) noexcept
		{
			this->m_count = move_.m_count;
			this->root_ptr = move_.root_ptr;
			this->curr_ptr = move_.curr_ptr;
			this->leaf_set = std::move(move_.leaf_set);
			move_.m_count = 0;
			move_.root_ptr = nullptr;
			move_.curr_ptr = nullptr;
		};
		parent_tree() noexcept
		{
			this->root_ptr = nullptr;
			this->curr_ptr = nullptr;
			this->m_count = 0;
		};

		~parent_tree() noexcept
		{
			this->clear();
		};
	};
}

#endif
