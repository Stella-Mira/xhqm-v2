/*
*作者：星辉清梦
*时间：龙—甲辰己巳
*用途：单向网（有向图）
* 性质：
* 1、仅可添加当前节点的目标节点（a.add(b) 仅 a→b）
* 2、for循环遍历时自当前节点广度/深度遍历
*/

#pragma once
#ifndef XHQM_META_ARROW_MESH
#define XHQM_META_ARROW_MESH

#include "./struct.h"
#include "./mesh.hpp"

namespace xhqm
{
	namespace _graph_
	{
		//单向网节点：仅记录目标（mesh_oneway）
		template<typename _node_data, typename _edge_data = void>
		struct arrow_node : public mesh_node_<arrow_node<_node_data, _edge_data>, _node_data, _edge_data, mesh_dir::mesh_oneway, 0>
		{
			friend struct mesh_node_<arrow_node<_node_data, _edge_data>, _node_data, _edge_data, mesh_dir::mesh_oneway, 0>;
			using node_base = mesh_node_<arrow_node<_node_data, _edge_data>, _node_data, _edge_data, mesh_dir::mesh_oneway, 0>;
			using node_data_ref = typename node_base::node_data_ref;
			using edge_data_ref = typename node_base::edge_data_ref;
			using node_data_ptr = typename node_base::node_data_ptr;
			using edge_data_ptr = typename node_base::edge_data_ptr;
			using loca_type = typename node_base::loca_type;
			using node_type = typename node_base::node_type;
			using edge_type = typename node_base::edge_type;
			using node_data = typename node_base::node_data;
			using edge_data = typename node_base::edge_data;
			using mesh_node = node_type;
			using iterator = xhqm::mesh_iterator<arrow_node>;
			//friend struct node_base;
		protected:
			inline bool _add_adjacent_ptr(node_type* adjacent_ptr_)
			{
				if (!adjacent_ptr_) return false;
				if (this->adjacent_index(adjacent_ptr_) != xhqm::npos) return false;

				if constexpr (xhqm::is_same<void, edge_data>)
				{
					//无路径：直接存节点指针
					this->adjacent_loca.push_back(adjacent_ptr_);
				}
				else
				{
					//有路径：goto edge 记录目标
					loca_type* e = new loca_type();
					e->goto_loca = adjacent_ptr_;
					this->adjacent_loca.push_back(e);
				}
				return true;
			}
			inline bool _sub_adjacent_ptr(node_type* adjacent_ptr_)
			{
				if (!adjacent_ptr_) return false;
				auto index = this->adjacent_index(adjacent_ptr_);
				if (index == xhqm::npos) return false;

				if constexpr (!xhqm::is_same<void, edge_data>) delete this->adjacent_loca[index];
				this->adjacent_loca.erase(this->adjacent_loca.begin() + index);
				return true;
			}
		public:
			arrow_node() {};
			~arrow_node()
			{
				if constexpr (!xhqm::is_same<void, edge_data>)
				{
					for (auto* e : this->adjacent_loca) delete e;
				}
			};

			template<typename __data_type> arrow_node(__data_type&& data_) noexcept
				: node_base(xhqm::move(data_)) { }
			template<typename __data_type> arrow_node(const __data_type& data_) noexcept
				: node_base(xhqm::copy(data_)) { }

			//begin是自身
			inline iterator begin() const noexcept { return iterator(const_cast<node_type*>(static_cast<const node_type*>(this))); };
			//begin是自身
			inline iterator begin() noexcept { return iterator(static_cast<node_type*>(this)); };
			//end是nullptr
			inline iterator end() const noexcept { return iterator(nullptr); };
			//end是nullptr
			inline iterator end() noexcept { return iterator(nullptr); };
		};
	}

	//单向网容器（游标容器：入口 head + curr，对齐 next_list）
	template<typename _node_data, typename _edge_data = void>
	class arrow_mesh
	{
	public:
		using node = xhqm::_graph_::arrow_node<_node_data, _edge_data>;
		using mesh_node = node;
		using node_ptr = node*;
		using node_data = typename node::node_data;
		using node_data_ref = typename node::node_data_ref;
		using edge_data = typename node::edge_data;
		using edge_data_ref = typename node::edge_data_ref;
		using node_data_ptr = typename node::node_data_ptr;
		using iterator = xhqm::mesh_iterator<arrow_mesh>;
	protected:
		xhqm::count m_count = 0;
		node_ptr head_ptr = nullptr;  // 入口
		node_ptr curr_ptr = nullptr;  // 游标
	public:
		inline bool empty() const noexcept { return head_ptr == nullptr; };
		inline xhqm::size size() const noexcept { return m_count; };

		inline node_data_ref curr() { return curr_ptr->data; };
		inline const node_data_ref curr() const { return curr_ptr->data; };

		//添加节点（返回指针，to_curr 控制游标，首个节点自动成为游标）
		node_ptr add_node(const node_data& data, bool to_curr = false)
		{
			node_ptr n = new node(xhqm::copy(data));
			if (!head_ptr) head_ptr = n;
			if (to_curr || !curr_ptr) curr_ptr = n;
			++m_count;
			return n;
		};
		node_ptr add_node(node_data&& data, bool to_curr = false)
		{
			node_ptr n = new node(xhqm::move(data));
			if (!head_ptr) head_ptr = n;
			if (to_curr || !curr_ptr) curr_ptr = n;
			++m_count;
			return n;
		};

		//移动游标到第 pos 个邻接
		bool curr_adjacent(const xhqm::index& pos)
		{
			if (!curr_ptr) return false;
			node_ptr adj = curr_ptr->adjacent_ptr(pos);
			if (!adj) return false;
			curr_ptr = adj;
			return true;
		};

		//遍历（自入口 head）
		inline iterator begin() const noexcept { return iterator(head_ptr); };
		inline iterator end() const noexcept { return iterator(nullptr); };

		//重置（不释放 node，由 graph 负责释放）
		void reset() { head_ptr = nullptr; curr_ptr = nullptr; m_count = 0; };
	};
}

#endif
