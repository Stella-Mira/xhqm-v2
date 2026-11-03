/*
*作者：星辉清梦
*时间：龙—甲辰乙亥甲申乙亥
*用途：链、树、网 路径类与节点类
* 
* * 路径
* 无数据路径、有数据路径
* 无连通、来连通、去连同、双连通
* 
* 无连通			出现此类路径将会静态报错
* 双连通			出现此类路径将会静态转换为路径指针
* 无数据来连通		出现此类路径将会静态转换为节点指针
* 无数据去连通		出现此类路径将会静态转换为节点指针
* 无数据双连通		此类路径将会记录节点的来源节点以及目标节点
* 有数据来连通		此类路径将会记录来源节点以及路径数据
* 有数据去连通		此类路径将会记录目标节点以及路径数据
* 有数据双连通		此类路径将会记录节点的来源节点、目标节点以及路径数据
* 
* 
* * 节点
* 禁止移动、禁止复制
* 无数据节点、有数据节点
* 空邻接、有限邻接、无限邻接偏增删、无限邻接偏读写
* 
* 空邻接					出现此类节点将会静态报错
* 有数据无限邻接			此类节点用于树节点、网节点
* 有数据有限邻接			此类节点用链表节点、N叉树节点、有限网节点
* 无数据有限邻接			此类节点用于继承类型基类节点，std::array
* 无数据无限邻接偏增删		此类节点用于继承类型基类节点，std::list
* 无数据无限邻接偏读写		此类节点用于继承类型基类节点，std::vector
* 
* 
* 链表类
*	普通链：		继承用链表 来向链表 去向链表 双向链表 环形链表
* 树形类
*	N叉树：			双向无路径树
*	增删树：		父向树 子向树 双向树
*	读写树：		父向树 子向树 双向树
* 网状类
*	有限邻接网：双向无路径网 双向有路径网
*	增删邻接网：来向网 去向网  双向网
*	读写邻接网：来向网 去向网  双向网
*/

#pragma once
#ifndef XHQM_META_STRUCT
#define XHQM_META_STRUCT

#include "../head.h"
#include "../algebra/value.h"
#include "../metaclass/compute.h"
#include "../metaclass/template.h"
#include "../metaclass/function/class_type.h"

namespace xhqm
{
	namespace _graph_
	{
		//数据类型
		template<typename _data_type> struct base_data_
		{
			using data_type = _data_type;
			using data_ref = _data_type&;
			using data_ptr = _data_type*;
			_data_type data;

			base_data_() noexcept 
				: data() { }
			base_data_(data_type&& data_) noexcept
				: data(xhqm::move(data_)) { }
			base_data_(const data_type& data_) noexcept
				: data(xhqm::copy(data_)) { }
		};
		template<> struct base_data_<void>
		{
			using data_type = void;
			using data_ref = void;
			using data_ptr = void;
		};


		//路径类型
		enum class edge_dir : ::xhqm::id
		{
			edge_null = 0,//无链接
			edge_from = 1,//记录来源 单数据
			edge_goto = 2,//记录目标 单数据
			edge_bind = 4,//双向链接 单数据
		};

		template<typename _node_loca, typename _edge_data, edge_dir _edge_dir>
		struct edge_type_ : public base_data_<_edge_data>
		{
			static_assert(!(edge_dir::edge_null == _edge_dir && !::xhqm::is_same<void, _edge_data>), "无路径时无法存储数据");

			using data_ref = typename base_data_<_edge_data>::data_ref;
			using data_ptr = typename base_data_<_edge_data>::data_ptr;
			using node_loca = _node_loca;
			using edge_data = _edge_data;

			const edge_dir type = _edge_dir;
			bool from_loca = false;
			bool goto_loca = false;

			edge_type_() noexcept
				: base_data_<_edge_data>() { }

			template<typename __data_type> edge_type_(__data_type&& data_) noexcept
				: base_data_<_edge_data>(xhqm::move(data_)) { }

			template<typename __data_type> edge_type_(const __data_type& data_) noexcept
				: base_data_<_edge_data>(xhqm::copy(data_)) { }

			constexpr inline void swap(edge_type_& value_) noexcept
			{
				if constexpr (!::xhqm::is_same<edge_data, void>) { std::swap(this->data, value_.data); }
			}
		};
		template<typename _node_loca, typename _edge_data>
		struct edge_type_<_node_loca, _edge_data, edge_dir::edge_from>
			: public base_data_<_edge_data>
		{
			using data_ref = typename base_data_<_edge_data>::data_ref;
			using data_ptr = typename base_data_<_edge_data>::data_ptr;
			using node_loca = _node_loca;
			using edge_data = _edge_data;

			const edge_dir type = edge_dir::edge_from;
			node_loca from_loca = {};
			bool goto_loca = false;

			edge_type_() noexcept
				: base_data_<_edge_data>() { }

			template<typename __data_type> edge_type_(__data_type&& data_) noexcept
				: base_data_<_edge_data>(xhqm::move(data_)) { }

			template<typename __data_type> edge_type_(const __data_type& data_) noexcept
				: base_data_<_edge_data>(xhqm::copy(data_)) { }

			constexpr inline void swap(edge_type_& value_) noexcept
			{
				if constexpr (!::xhqm::is_same<edge_data, void>) { std::swap(this->data, value_.data); }
			}
		};
		template<typename _node_loca, typename _edge_data>
		struct edge_type_<_node_loca, _edge_data, edge_dir::edge_goto>
			: public base_data_<_edge_data>
		{
			using data_ref = typename base_data_<_edge_data>::data_ref;
			using data_ptr = typename base_data_<_edge_data>::data_ptr;
			using node_loca = _node_loca;
			using edge_data = _edge_data;

			const edge_dir type = edge_dir::edge_goto;
			bool from_loca = false;
			node_loca goto_loca = {};

			edge_type_() noexcept
				: base_data_<_edge_data>() { }

			template<typename __data_type> edge_type_(__data_type&& data_) noexcept
				: base_data_<_edge_data>(xhqm::move(data_)) { }

			template<typename __data_type> edge_type_(const __data_type& data_) noexcept
				: base_data_<_edge_data>(xhqm::copy(data_)) { }

			constexpr inline void swap(edge_type_& value_) noexcept
			{
				if constexpr (!::xhqm::is_same<edge_data, void>) { std::swap(this->data, value_.data); }
			}
		};
		template<typename _node_loca, typename _edge_data>
		struct edge_type_<_node_loca, _edge_data, edge_dir::edge_bind>
			: public base_data_<_edge_data>
		{
			using data_ref = typename base_data_<_edge_data>::data_ref;
			using data_ptr = typename base_data_<_edge_data>::data_ptr;
			using node_loca = _node_loca;
			using edge_data = _edge_data;

			const edge_dir type = edge_dir::edge_bind;
			node_loca from_loca = {};
			node_loca goto_loca = {};

			edge_type_() noexcept
				: base_data_<_edge_data>() { }

			template<typename __data_type> edge_type_(__data_type&& data_) noexcept
				: base_data_<_edge_data>(xhqm::move(data_)) { }

			template<typename __data_type> edge_type_(const __data_type& data_) noexcept
				: base_data_<_edge_data>(xhqm::copy(data_)) { }

			constexpr inline void swap(edge_type_& value_) noexcept
			{
				if constexpr (!::xhqm::is_same<edge_data, void>) { std::swap(this->data, value_.data); }
			}
		};


		//类型计算器
		// 节点类型，节点数据，路径数据，路径方向
		template<typename _node_type, typename _node_data, typename _edge_data, edge_dir _edge_dir>
		struct node_s
		{
			using node_type = _node_type;
			using edge_type = edge_type_<_node_type*, _edge_data, _edge_dir>;
			using loca_type = typename ::xhqm::select_if<(edge_dir::edge_null == _edge_dir), node_type, edge_type>::type;
			using node_data = typename base_data_<_node_data>::data_type;
			using edge_data = typename base_data_<_edge_data>::data_type;
			using node_data_ref = typename base_data_<_node_data>::data_ref;
			using edge_data_ref = typename base_data_<_edge_data>::data_ref;
			using node_data_ptr = typename base_data_<_node_data>::data_ptr;
			using edge_data_ptr = typename base_data_<_edge_data>::data_ptr;
			using loca_ptr = loca_type*;
			using loca_ref = loca_type&;
		};
	};

	template <typename _range_type> constexpr inline auto curr(const _range_type& range_) noexcept { return range_.curr(); }
	//template <typename _type> constexpr inline void swap(_type& left_, _type& right) noexcept { left_.swap(right); }

	template <typename _range_type, typename _iterator = typename _range_type::iterator>
	constexpr inline _iterator begin(_range_type& range_) noexcept { _iterator iter = range_.begin(); return iter; }
	template <typename _range_type, typename _iterator = typename _range_type::iterator>
	constexpr inline _iterator end(_range_type& range_) noexcept { _iterator iter = range_.end(); return iter; }

	template<typename _iterator, typename _docker, typename _function, typename ..._argvs>
	inline void for_each(_docker& objects_, _function&& function_, _argvs&& ...argvs_)
	{
		_iterator iterator = objects_.begin();
		_iterator iterator_end = objects_.end();
		for (; iterator != iterator_end; ++iterator)
		{
			function_(*iterator, std::forward<_argvs>(argvs_)...);
		}
	}

	template<typename _docker, typename _function, typename ..._argvs>
	inline void for_each(_docker& objects_, _function&& function_, _argvs&& ...argvs_)
	{
		for (auto& value : objects_)
		{
			function_(value, std::forward<_argvs>(argvs_)...);
		}
	}
}

#endif
