/*
*作者：星辉清梦
*时间：龙—甲辰己巳
*用途：双向网（无向图）
* 性质：
* 1、添加邻接自动双向维护（a.add(b) 即 a↔b）
* 2、for循环遍历时自当前节点广度/深度遍历
*/

#pragma once
#ifndef XHQM_META_BIND_MESH
#define XHQM_META_BIND_MESH

#include "./struct.h"
#include "./mesh.hpp"

namespace xhqm
{
	namespace _graph_
	{
		//双向网节点：记录来源+目标（mesh_twoway），bind edge 双向维护
		template<typename _node_data, typename _edge_data = void>
		struct bind_mesh_node : public mesh_node_<bind_mesh_node<_node_data, _edge_data>, _node_data, _edge_data, mesh_dir::mesh_twoway, 0>
		{
			friend struct mesh_node_<bind_mesh_node<_node_data, _edge_data>, _node_data, _edge_data, mesh_dir::mesh_twoway, 0>;
			using node_base = mesh_node_<bind_mesh_node<_node_data, _edge_data>, _node_data, _edge_data, mesh_dir::mesh_twoway, 0>;
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
			using iterator = xhqm::mesh_iterator<bind_mesh_node>;
			//friend struct node_base;
		protected:
			inline bool _add_adjacent_ptr(node_type* adjacent_ptr_)
			{
				if (!adjacent_ptr_) return false;
				if (adjacent_ptr_ == static_cast<node_type*>(this)) return false;   // 拒绝自环
				if (this->adjacent_index(adjacent_ptr_) != xhqm::npos) return false;

				//正向 bind edge（归属 this）
				loca_type* e = new loca_type();
				e->from_loca = static_cast<node_type*>(this);
				e->goto_loca = adjacent_ptr_;
				this->adjacent_loca.push_back(e);
				//反向 bind edge（归属 adjacent）
				loca_type* re = new loca_type();
				re->from_loca = adjacent_ptr_;
				re->goto_loca = static_cast<node_type*>(this);
				adjacent_ptr_->adjacent_loca.push_back(re);
				return true;
			}
			inline bool _sub_adjacent_ptr(node_type* adjacent_ptr_)
			{
				if (!adjacent_ptr_) return false;
				auto index = this->adjacent_index(adjacent_ptr_);
				if (index == xhqm::npos) return false;

				delete this->adjacent_loca[index];
				auto pindex = adjacent_ptr_->adjacent_index(static_cast<node_type*>(this));
				if (pindex != xhqm::npos)
				{
					delete adjacent_ptr_->adjacent_loca[pindex];
					adjacent_ptr_->adjacent_loca.erase(adjacent_ptr_->adjacent_loca.begin() + pindex);
				}
				this->adjacent_loca.erase(this->adjacent_loca.begin() + index);
				return true;
			}
		public:
			bind_mesh_node() {};
			~bind_mesh_node()
			{
				//断开所有邻接：删除相邻节点的反向 edge，再释放自己的 edge
				for (auto* e : this->adjacent_loca)
				{
					node_type* adj = e->goto_loca;
					if (adj)
					{
						auto pindex = adj->adjacent_index(static_cast<node_type*>(this));
						if (pindex != xhqm::npos)
						{
							delete adj->adjacent_loca[pindex];
							adj->adjacent_loca.erase(adj->adjacent_loca.begin() + pindex);
						}
					}
					delete e;
				}
				this->adjacent_loca.clear();
			};

			template<typename __data_type> bind_mesh_node(__data_type&& data_) noexcept
				: node_base(xhqm::move(data_)) { }
			template<typename __data_type> bind_mesh_node(const __data_type& data_) noexcept
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

	//双向网容器（游标容器：入口 head + curr，对齐 next_list）
	template<typename _node_data, typename _edge_data = void>
	class bind_mesh
	{
	public:
		using node = xhqm::_graph_::bind_mesh_node<_node_data, _edge_data>;
		using mesh_node = node;
		using node_ptr = node*;
		using node_data = typename node::node_data;
		using node_data_ref = typename node::node_data_ref;
		using edge_data = typename node::edge_data;
		using edge_data_ref = typename node::edge_data_ref;
		using node_data_ptr = typename node::node_data_ptr;
		using iterator = xhqm::mesh_iterator<bind_mesh>;
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
