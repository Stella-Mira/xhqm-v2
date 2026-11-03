/*
*作者：星辉清梦
*时间：龙—甲辰丙子丙午戊戌
*用途：子向树
* 性质：
* 1、无法遍历，仅能自一个叶节点向上遍历到根节点
* 2、可添加当前数据的父节点数据
* 3、可添加当前数据的子节点数据
* 4、当前项仅可向父节点移动
* 5、叶子节点是无序的，插入顺序非访问顺序
*/

#pragma once
#ifndef XHQM_META_CHILD_TREE
#define XHQM_META_CHILD_TREE
#include "./struct.h"
#include "./tree.hpp"

namespace xhqm
{
	namespace _graph_
	{
		//记录父节点
		template<typename _node_data, typename _edge_data = void>
		struct child_node : public tree_node_<child_node<_node_data, _edge_data>, _node_data, _edge_data, tree_dir::tree_childs, 0>
		{
			friend struct tree_node_<child_node<_node_data, _edge_data>, _node_data, _edge_data, tree_dir::tree_childs, 0>;
			using node_base = tree_node_<child_node<_node_data, _edge_data>, _node_data, _edge_data, tree_dir::tree_childs, 0>;
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
			bool _set_parent_ptr(node_type* parent_ptr_)
			{
				if (parent_ptr_)
					return parent_ptr_->_add_child_ptr(static_cast<node_type*>(this));
				return false;
			}
			bool _add_child_ptr(node_type* child_ptr_)
			{
				if (child_ptr_)
				{
					if (child_ptr_->parent_loca == false)
					{
						child_ptr_->parent_loca = true;
						if constexpr (xhqm::is_same<void, edge_data>)
						{
							this->childs_loca.push_back(child_ptr_);
						}
						else
						{
							auto child_loca = new loca_type();
							child_loca->from_loca = true;
							child_loca->goto_loca = child_ptr_;
							this->childs_loca.push_back(child_loca);
						};
						return true;
					}
					else
					{
						for (auto iter = this->childs_loca.begin(); iter != this->childs_loca.end(); ++iter)
						{
							if constexpr (xhqm::is_same<void, edge_data>)
							{
								if (*iter == child_ptr_)
								{
									return true;
								}
							}
							else
							{
								if ((*iter)->goto_loca == child_ptr_)
								{
									return true;
								}
							};
						}
					}
				}
				return false;
			}
			bool _sub_child_ptr(node_type* child_ptr_)
			{
				if (child_ptr_)
				{
					for (auto iter = this->childs_loca.begin(); iter != this->childs_loca.end(); ++iter)
					{
						if constexpr (xhqm::is_same<void, edge_data>)
						{
							if (*iter == child_ptr_)
							{
								child_ptr_->parent_loca = false;
								this->childs_loca.erase(iter);
								return true;
							}
						}
						else
						{
							if (iter->goto_loca == child_ptr_)
							{
								child_ptr_->parent_loca = false;
								auto edge_ptr = *iter; delete edge_ptr;
								this->childs_loca.erase(iter);
								return true;
							}
						};
					}
				}

				return false;
			}

		public:
			child_node()
			{

			};
			~child_node() 
			{
				// 不再 throw：删除前是否断开父链接归用户管理。
				// 释放自身持有的出边（edge_data 版本）；void 版本 childs_loca 存子指针，仅 clear 不 delete
				if constexpr (!xhqm::is_same<void, edge_data>)
				{
					for (auto* e : this->childs_loca) delete e;
				}
				this->childs_loca.clear();
			};

			void clear()
			{
				for (auto iter = this->childs_loca.begin(); iter != this->childs_loca.end(); ++iter)
				{
					if constexpr (xhqm::is_same<void, edge_data>)
					{
						(*iter)->parent_loca = false;
					}
					else
					{
						auto edge_ptr = (*iter);
						edge_ptr->goto_loca->parent_loca = false;
						delete edge_ptr;
					};
				}
				this->childs_loca.clear();
			}

			template<typename __data_type> child_node(__data_type&& data_) noexcept
				: node_base(xhqm::move(data_))
			{

			}
			template<typename __data_type> child_node(const __data_type& data_) noexcept
				: node_base(xhqm::copy(data_))
			{

			}
		};
	}


	template<typename _node_data, typename _edge_data = void>
	class child_tree
	{
	public:
		using tree_node = xhqm::_graph_::child_node<_node_data, _edge_data>;
		using node_data_ref = typename tree_node::node_data_ref;
		using edge_data_ref = typename tree_node::edge_data_ref;
		using node_data_ptr = typename tree_node::node_data_ptr;
		using edge_data_ptr = typename tree_node::edge_data_ptr;
		using node_data = typename tree_node::node_data;
		using edge_data = typename tree_node::edge_data;
		using iterator = xhqm::tree_iterator<child_tree>;
		using node_ptr = tree_node*;
		using node_ref = tree_node&;
	protected:
		xhqm::count m_count = 0;
		node_ptr root_ptr = nullptr;
		node_ptr curr_ptr = nullptr;
	public:
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
				// 后序删除全部节点（子先于父；父的 childs_loca 悬垂随父销毁）
				xhqm::iterate_bak(this->root_ptr, [](node_ptr node_ptr_)
				{
					delete node_ptr_;
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

		//摘除第 index 个子树（不删除，返回子树根，所有权移交调用者；失败返回 nullptr）
		node_ptr sub_child(const xhqm::index& index)
		{
			auto childs_list = curr_ptr->childs();
			if (index >= childs_list.size()) return nullptr;

			// 断开子树根与 curr 的链接（清 parent 标记 + 从 curr 表移除）
			node_ptr subtree_root = childs_list[index];
			if (!curr_ptr->sub_child_ptr(subtree_root)) return nullptr;

			// 统计子树节点数并从 m_count 扣除
			xhqm::count removed = 0;
			xhqm::iterate_bak(subtree_root, [&removed](node_ptr) { ++removed; });
			this->m_count -= removed;
			return subtree_root;
		};

		//抛出一个子节点及其子树（摘除并删除）
		bool pop_child(const xhqm::index& index)
		{
			node_ptr subtree_root = sub_child(index);
			if (!subtree_root) return false;

			// 后序删除整个子树（子先于父；析构自行释放出边）
			xhqm::iterate_bak(subtree_root, [](node_ptr node_ptr_)
			{
				delete node_ptr_;
			});
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

		inline bool is_root() const { return (curr_ptr == root_ptr); };

		inline void curr_root() { curr_ptr = root_ptr; };

		//深度遍历所有节点
		template<typename _func_type, typename... _argv_type>
		void traverse(_func_type&& function_, _argv_type&& ... argv_data_)
		{
			xhqm::iterate_dfs(curr_ptr, std::forward<_func_type>(function_), std::forward<_argv_type>(argv_data_)...);
		}

	public:
		inline child_tree& operator < (const node_data& data_) noexcept
		{
			curr_ptr->data = data_; return *this;
		};
		inline child_tree& operator > (node_data& data_) const noexcept
		{
			data_ = curr_ptr->data; return *this;
		};


	public:
		constexpr child_tree& operator =(child_tree&& move_) noexcept
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

		constexpr inline void swap(child_tree& tree_) noexcept
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
		child_tree(const child_tree& copy_) noexcept = delete;

		child_tree(child_tree&& move_) noexcept
		{
			this->m_count = move_.m_count;
			this->root_ptr = move_.root_ptr;
			this->curr_ptr = move_.curr_ptr;
			move_.m_count = 0;
			move_.root_ptr = nullptr;
			move_.curr_ptr = nullptr;
		};
		child_tree() noexcept
		{
			this->root_ptr = nullptr;
			this->curr_ptr = nullptr;
			this->m_count = 0;
		};
		~child_tree() noexcept
		{
			this->clear();
		};
	};
}

/*
namespace xhqm
{
	//N叉树
	template<xhqm::size array_size_, typename node_data_, typename path_data_ = void>
	class fork_tree : public xhqm::tree_t<typename xhqm::fork_tree<array_size_, node_data_, path_data_>, node_data_, path_data_, xhqm_std::array<typename xhqm::path_s<path_data_, typename xhqm::fork_tree<array_size_, node_data_, path_data_>>::docker_data, array_size_>>
	{
	public:
		using tree_base = typename xhqm::tree_t<typename xhqm::fork_tree<array_size_, node_data_, path_data_>, node_data_, path_data_, xhqm_std::array<typename xhqm::path_s<path_data_, typename xhqm::fork_tree<array_size_, node_data_, path_data_>>::docker_data, array_size_>>;
		using tree_type = typename xhqm::fork_tree<array_size_, node_data_, path_data_>;
		using tree_data = node_data_;
		using path_data = path_data_;
		using tree_ptr = tree_type*;
		using tree_ref = tree_type&;
	public:
		//N叉树 允许移动 禁止拷贝
		constexpr explicit fork_tree(void) : tree_base() {};

		fork_tree(const fork_tree&) noexcept = delete;
		fork_tree(fork_tree&& node) noexcept : tree_base(std::move(node)) {}
		template<typename data_type_ = tree_data> void operator =(const data_type_& data) { this->data = data; };
		template<typename data_type_ = tree_data> constexpr fork_tree(const data_type_& data) : tree_base(data) { };
	public:
		xhqm::count tree_size() const
		{
			xhqm::count count = 0;
			for (auto& child : tree_base::m_nodes)
				if (tree_ptr(child)) ++count;
			return count;
		}

		virtual void set_parent(const tree_ptr& parent) override
		{
			if (this->tree_base::m_parent)
				if (this->tree_base::m_parent->has_tree(static_cast<tree_ptr>(this)))
					this->tree_base::m_parent->tree_base::m_nodes.erase(this->tree_base::m_parent->tree_base::m_nodes.begin() + this->tree_base::m_parent->index(static_cast<tree_ptr>(this)));
			if (parent) parent->tree_base::m_nodes.push_back(static_cast<tree_ptr>(this));
			this->tree_base::m_parent = parent;
		}

		void set_child(const xhqm::index& pos, const tree_ptr& child_ptr)
		{
			if (child_ptr)
			{
				//断开新节点与原父节点链接
				if (child_ptr->m_parent)
				{
					xhqm::index new_child_pos = child_ptr->m_parent->tree_base::index(child_ptr);
					child_ptr->m_parent->tree_base::m_nodes[new_child_pos] = nullptr;
				}
				child_ptr->m_parent = this;
			}
			// 断开旧节点与当前节点链接
			tree_ptr old_child_ptr = tree_ptr(this->tree_base::m_nodes[pos]);
			if (old_child_ptr) old_child_ptr->m_parent = nullptr;
			this->tree_base::m_nodes[pos] = child_ptr;			//新节点与当前节点链接
		}

		
	};
	*/

#endif
