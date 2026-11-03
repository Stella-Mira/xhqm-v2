/*
*作者：星辉澪
*时间：乙巳
*用途：有向图节点（区分「来连接」与「去连接」）
* 性质：
* 1、边方向 = 数据流：from → goto（来源指向目标）
* 2、建边双向登记：from 的去连接记 goto，goto 的来连接记 from
* 3、正向沿「去连接」下游遍历，反向沿「来连接」上游遍历（计算图正反向）
* 4、对齐 lattice 约定：禁止拷贝/移动，指针即身份
* 5、adjacent_list() 返回去连接（出边），兼容 graph_algorithms 的拓扑排序
*/

#pragma once
#ifndef XHQM_META_DIRECTED_MESH
#define XHQM_META_DIRECTED_MESH

#include "./struct.h"
#include <vector>
#include <utility>

namespace xhqm
{
	namespace _graph_
	{
		// 有向图节点：区分「来连接」（from，谁指向我 = 输入/上游）与「去连接」（goto，我指向谁 = 输出/下游）。
		// 边无数据（直存节点指针）；如需边数据可仿 arrow_node 用 edge 对象扩展。
		template<typename _node_data>
		struct directed_node : public base_data_<_node_data>
		{
			using node_type = directed_node<_node_data>;
			using node_data = _node_data;
			using node_data_ref = typename base_data_<_node_data>::data_ref;
			using node_data_ptr = typename base_data_<_node_data>::data_ptr;
			using node_ptr = node_type*;
			using node_cptr = const node_type*;

		protected:
			std::vector<node_ptr> from_loca;  // 来连接：谁指向我（输入/上游）
			std::vector<node_ptr> goto_loca;  // 去连接：我指向谁（输出/下游）

		public:
			directed_node() = default;
			template<typename _dt> explicit directed_node(_dt&& d)
				: base_data_<_node_data>(std::forward<_dt>(d)) {}
			directed_node(const directed_node&) = delete;
			directed_node(directed_node&&) = delete;
			directed_node& operator=(const directed_node&) = delete;
			directed_node& operator=(directed_node&&) = delete;

			// 建边 this → to（to 的来连接登记 this）
			bool add_goto(node_ptr to)
			{
				if (!to || to == this) return false;
				if (this->goto_index(to) != xhqm::npos) return false;
				this->goto_loca.push_back(to);
				if (to->from_index(this) == xhqm::npos)
					to->from_loca.push_back(this);
				return true;
			}

			// 建边 from → this（from 的去连接登记 this）
			bool add_from(node_ptr from)
			{
				if (!from || from == this) return false;
				if (this->from_index(from) != xhqm::npos) return false;
				this->from_loca.push_back(from);
				if (from->goto_index(this) == xhqm::npos)
					from->goto_loca.push_back(this);
				return true;
			}

			// 拆边 this → to
			bool sub_goto(node_ptr to)
			{
				if (!to) return false;
				auto i = this->goto_index(to);
				if (i == xhqm::npos) return false;
				this->goto_loca.erase(this->goto_loca.begin() + i);
				auto j = to->from_index(this);
				if (j != xhqm::npos)
					to->from_loca.erase(to->from_loca.begin() + j);
				return true;
			}

			// 拆边 from → this
			bool sub_from(node_ptr from)
			{
				if (!from) return false;
				auto i = this->from_index(from);
				if (i == xhqm::npos) return false;
				this->from_loca.erase(this->from_loca.begin() + i);
				auto j = from->goto_index(this);
				if (j != xhqm::npos)
					from->goto_loca.erase(from->goto_loca.begin() + j);
				return true;
			}

			// 索引（越界返回 npos）
			xhqm::index from_index(node_cptr from) const
			{
				for (xhqm::index i = 0; i < from_loca.size(); ++i)
					if (from_loca[i] == from) return i;
				return xhqm::npos;
			}
			xhqm::index goto_index(node_cptr to) const
			{
				for (xhqm::index i = 0; i < goto_loca.size(); ++i)
					if (goto_loca[i] == to) return i;
				return xhqm::npos;
			}

			// 枚举
			std::vector<node_ptr> from_list() const { return from_loca; }  // 谁指向我
			std::vector<node_ptr> goto_list() const { return goto_loca; }  // 我指向谁
			std::vector<node_ptr> adjacent_list() const { return goto_loca; }  // 兼容拓扑排序（出边）

			// 度数
			xhqm::count in_degree() const { return from_loca.size(); }
			xhqm::count out_degree() const { return goto_loca.size(); }
			xhqm::count degree() const { return from_loca.size() + goto_loca.size(); }

			// 判定
			bool in_from(node_cptr from) const { return from_index(from) != xhqm::npos; }
			bool in_goto(node_cptr to) const { return goto_index(to) != xhqm::npos; }
			bool is_source() const { return from_loca.empty(); }  // 无输入（叶子/参数）
			bool is_sink() const { return goto_loca.empty(); }    // 无输出（结果）
		};
	}
}

#endif
