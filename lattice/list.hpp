/*
*作者：星辉清梦
*时间：龙—甲辰乙亥丙申戊子
*用途：链节点类
*顺序：自前往后
* 链表类
*	普通链：		继承用链表 来向链表 去向链表 双向链表 环形链表
* 注意：迭代器的移动不影响链表的当前项
*/

#pragma once
#ifndef XHQM_META_LIST
#define XHQM_META_LIST

#include "./struct.h"
#include <iterator>

namespace xhqm
{
	//enum class list_type : ::xhqm::id
	//{
	//	list_prev = 1,//来向链
	//	list_next = 2,//去向链
	//	list_bind = 4,//双向链
	//	list_ring = 8,//环形链
	//};

	namespace _graph_
	{
		enum class list_dir : ::xhqm::id
		{
			list_null = 0,//无上一项无下一项 
			list_prev = 1,//有上一项无下一项
			list_next = 2,//无上一项有下一项
			list_bind = 4,//有上一项有下一项
		};

		template<list_dir _list_dir> struct list_dir_value { constexpr static edge_dir _edge_dir = edge_dir::edge_null; };
		template<> struct list_dir_value<list_dir::list_prev> { constexpr static edge_dir _edge_dir = edge_dir::edge_from; };
		template<> struct list_dir_value<list_dir::list_next> { constexpr static edge_dir _edge_dir = edge_dir::edge_goto; };
		template<> struct list_dir_value<list_dir::list_bind> { constexpr static edge_dir _edge_dir = edge_dir::edge_bind; };

		//链基础类型 
		template<typename _node_type, typename _node_data, typename _edge_data, list_dir _list_dir>
		struct list_node_ : public base_data_<_node_data>
		{
			constexpr static edge_dir void_value = ::xhqm::compute<edge_dir>::select_if<::xhqm::is_same<void, _edge_data>, edge_dir::edge_null, list_dir_value<_list_dir>::_edge_dir>::value;
			constexpr static edge_dir bind_value = ::xhqm::compute<edge_dir>::select_if<list_dir::list_bind == _list_dir, edge_dir::edge_bind, list_dir_value<_list_dir>::_edge_dir>::value;
			constexpr static edge_dir edge_value = ::xhqm::compute<edge_dir>::select_if<edge_dir::edge_bind == bind_value, bind_value, void_value>::value;
			using list_node_s = node_s<_node_type, _node_data, _edge_data, edge_value>;
			using node_base = base_data_<_node_data>;
			using loca_type = typename list_node_s::loca_type;
			using node_type = typename list_node_s::node_type;
			using edge_type = typename list_node_s::edge_type;
			using node_data = typename list_node_s::node_data;
			using edge_data = typename list_node_s::edge_data;
			using node_data_ref = typename list_node_s::node_data_ref;
			using edge_data_ref = typename list_node_s::edge_data_ref;
			using node_data_ptr = typename list_node_s::node_data_ptr;
			using edge_data_ptr = typename list_node_s::edge_data_ptr;
			using from_loca_type = typename ::xhqm::select_if<(_list_dir == list_dir::list_null || _list_dir == list_dir::list_prev || _list_dir == list_dir::list_bind), typename list_node_s::loca_ptr, bool>::type;
			using goto_loca_type = typename ::xhqm::select_if<(_list_dir == list_dir::list_null || _list_dir == list_dir::list_next || _list_dir == list_dir::list_bind), typename list_node_s::loca_ptr, bool>::type;
		protected: from_loca_type prev_loca = {};
		protected: goto_loca_type next_loca = {};
		public:	inline bool has_prev() const 
		{ 
			if constexpr (_list_dir == list_dir::list_bind) { return prev_loca && prev_loca->from_loca; };												//edge_ptr
			if constexpr (_list_dir == list_dir::list_prev && xhqm::is_same<void, edge_data>) { return prev_loca; };						//node_ptr
			if constexpr (_list_dir == list_dir::list_prev && !xhqm::is_same<void, edge_data>) { return prev_loca->from_loca; };			//edge_ptr
			if constexpr (_list_dir == list_dir::list_null || _list_dir == list_dir::list_next) { return prev_loca; };						//node_ptr||bool
		};
		public:	inline bool has_next() const 
		{
			if constexpr (_list_dir == list_dir::list_bind) { return next_loca && next_loca->goto_loca; };												//edge_ptr
			if constexpr (_list_dir == list_dir::list_next && xhqm::is_same<void, edge_data>) { return next_loca; };						//node_ptr
			if constexpr (_list_dir == list_dir::list_next && !xhqm::is_same<void, edge_data>) { return next_loca->goto_loca; };			//edge_ptr
			if constexpr (_list_dir == list_dir::list_null || _list_dir == list_dir::list_prev) { return next_loca; };						//node_ptr||bool
		};
		public:	inline void has_prev(const bool& has) { if constexpr (::xhqm::is_same<from_loca_type, bool>) { prev_loca = has; }; };
		public:	inline void has_next(const bool& has) { if constexpr (::xhqm::is_same<goto_loca_type, bool>) { next_loca = has; }; };

			constexpr list_node_() noexcept : node_base() {};
			template<typename __data_type> list_node_(__data_type&& data_) noexcept
				: node_base(xhqm::move(data_)) { }
			template<typename __data_type> list_node_(const __data_type& data_) noexcept
				: node_base(xhqm::copy(data_)) { }

			constexpr inline void swap(_node_type& value_) noexcept 
			{ 
				if constexpr (!::xhqm::is_same<_node_data, void>) { std::swap(this->data, value_.data); }
			}
		protected:
			//constexpr bool _set_prev_ptr(node_type* prev_ptr_);
			//constexpr bool _set_next_ptr(node_type* next_ptr_);
		public:
			template<typename _data_type = _edge_data>
			bool set_prev_ptr(node_type* prev_ptr_, const _data_type& edge_data_)
			{
				static_assert(!::xhqm::is_same<_data_type, void>, "无路径数据");

				if (static_cast<node_type*>(this)->_set_prev_ptr(prev_ptr_) == false) return false;

				if constexpr (list_dir::list_prev == _list_dir)
				{
					if (!this->prev_loca) return false;
					this->prev_loca->data = edge_data_;
				};
				if constexpr (list_dir::list_next == _list_dir)
				{
					if (!prev_ptr_) return false;
					if (!prev_ptr_->next_loca) return false;
					prev_ptr_->next_loca->data = edge_data_;
				};
				if constexpr (list_dir::list_bind == _list_dir)
				{
					if (!this->prev_loca) return false;
					this->prev_loca->data = edge_data_;
				};

				return true;
			}
			template<typename _data_type = _edge_data>
			bool set_next_ptr(node_type* next_ptr_, const _data_type& edge_data_)
			{
				static_assert(!::xhqm::is_same<_data_type, void>, "无路径数据");

				if (static_cast<node_type*>(this)->_set_next_ptr(next_ptr_) == false) return false;

				if constexpr (list_dir::list_prev == _list_dir)
				{
					if (!next_ptr_) return false;
					if (!next_ptr_->prev_loca) return false;
					next_ptr_->prev_loca->data = edge_data_;
				};
				if constexpr (list_dir::list_next == _list_dir)
				{
					if (!this->next_loca) return false;
					this->next_loca->data = edge_data_;
				};
				if constexpr (list_dir::list_bind == _list_dir)
				{
					if (!this->next_loca) return false;
					this->next_loca->data = edge_data_;
				};

				return true;
			}

			template<typename _data_type = _edge_data>
			bool set_prev_ptr(node_type* prev_ptr_, _data_type&& edge_data_)
			{
				static_assert(!::xhqm::is_same<_data_type, void>, "无路径数据");

				if (static_cast<node_type*>(this)->_set_prev_ptr(prev_ptr_) == false) return false;

				if constexpr (list_dir::list_prev == _list_dir)
				{
					if (!this->prev_loca) return false;
					this->prev_loca->data = xhqm::move(edge_data_);
				};
				if constexpr (list_dir::list_next == _list_dir)
				{
					if (!prev_ptr_) return false;
					if (!prev_ptr_->next_loca) return false;
					prev_ptr_->next_loca->data = xhqm::move(edge_data_);
				};
				if constexpr (list_dir::list_bind == _list_dir)
				{
					if (!this->prev_loca) return false;
					this->prev_loca->data = xhqm::move(edge_data_);
				};

				return true;
			}
			template<typename _data_type = _edge_data>
			bool set_next_ptr(node_type* next_ptr_, _data_type&& edge_data_)
			{
				static_assert(!::xhqm::is_same<_data_type, void>, "无路径数据");

				if (static_cast<node_type*>(this)->_set_next_ptr(next_ptr_) == false) return false;

				if constexpr (list_dir::list_prev == _list_dir)
				{
					if (!next_ptr_) return false;
					if (!next_ptr_->prev_loca) return false;
					next_ptr_->prev_loca->data = xhqm::move(edge_data_);
				};
				if constexpr (list_dir::list_next == _list_dir)
				{
					if (!this->next_loca) return false;
					this->next_loca->data = xhqm::move(edge_data_);
				};
				if constexpr (list_dir::list_bind == _list_dir)
				{
					if (!this->next_loca) return false;
					this->next_loca->data = xhqm::move(edge_data_);
				};

				return true;
			}

			constexpr bool set_prev_ptr(node_type* prev_ptr_)
			{
				return static_cast<node_type*>(this)->_set_prev_ptr(prev_ptr_);
			}
			constexpr bool set_next_ptr(node_type* next_ptr_)
			{
				return static_cast<node_type*>(this)->_set_next_ptr(next_ptr_);
			}
			constexpr node_type* prev_ptr() const
			{
				if constexpr (list_dir::list_prev == _list_dir)
				{
					if constexpr (xhqm::is_same<void, edge_data>)
					{
						return this->prev_loca;
					}
					else
					{
						if (!this->prev_loca) return nullptr;
						return this->prev_loca->from_loca;
					};
				};
				if constexpr (list_dir::list_next == _list_dir)
				{
					return nullptr;
				};
				if constexpr (list_dir::list_bind == _list_dir)
				{
					if (!this->prev_loca) return nullptr;
					return this->prev_loca->from_loca;
				};
				if constexpr (list_dir::list_null == _list_dir)
				{
					return this->prev_loca;
				};
			}
			constexpr node_type* next_ptr() const
			{
				if constexpr (list_dir::list_prev == _list_dir)
				{
					return nullptr;
				};
				if constexpr (list_dir::list_next == _list_dir)
				{
					if constexpr (xhqm::is_same<void, edge_data>)
					{
						return this->next_loca;
					}
					else
					{
						if (!this->next_loca) return nullptr;
						return this->next_loca->goto_loca;
					};
				};
				if constexpr (list_dir::list_bind == _list_dir)
				{
					if (!this->next_loca) return nullptr;
					return this->next_loca->goto_loca;
				};
				if constexpr (list_dir::list_null == _list_dir)
				{
					return this->next_loca;
				};
			}

			constexpr from_loca_type prev_edge() const { return prev_loca; };
			constexpr goto_loca_type next_edge() const { return next_loca; };
		public:
			//nullptr 将会返回false
			bool in_list(const node_type* ptr) const
			{
				if (ptr == nullptr) return false;
				node_type* prev_curr_ptr = const_cast<node_type*>(static_cast<const node_type*>(this));
				node_type* next_curr_ptr = const_cast<node_type*>(static_cast<const node_type*>(this));
				while (prev_curr_ptr)
				{
					if (prev_curr_ptr == ptr) return true;
					prev_curr_ptr = prev_curr_ptr->prev_ptr();
					if (prev_curr_ptr == this) return false;
				}
				while (next_curr_ptr)
				{
					if (next_curr_ptr == ptr) return true;
					next_curr_ptr = next_curr_ptr->next_ptr();
					if (next_curr_ptr == this) return false;
				}
				return false;
			}
			//环上无链头：绕圈回到起点时返回自身
			constexpr node_type* first_ptr() const
			{
				node_type* begin = const_cast<node_type*>(static_cast<const node_type*>(this));
				node_type* currptr = begin;
				while (currptr->prev_ptr())
				{
					currptr = currptr->prev_ptr();
					if (currptr == begin) return begin;
				}
				return currptr;
			}
			//环上无链尾：绕圈回到起点时返回自身
			constexpr node_type* last_ptr() const
			{
				node_type* begin = const_cast<node_type*>(static_cast<const node_type*>(this));
				node_type* currptr = begin;
				while (currptr->next_ptr())
				{
					currptr = currptr->next_ptr();
					if (currptr == begin) return begin;
				}
				return currptr;
			}
			//数前驱总数；环上数到回到自身（环长）。局部游标推进，不动用户游标
			xhqm::count prev_count() const
			{
				xhqm::count count = 0;
				node_type* curr = prev_ptr();
				const node_type* begin = static_cast<const node_type*>(this);
				while (curr && curr != begin) { ++count; curr = curr->prev_ptr(); }
				return count;
			}
			//数后继总数；环上数到回到自身（环长）。局部游标推进，不动用户游标
			xhqm::count next_count() const
			{
				xhqm::count count = 0;
				node_type* curr = next_ptr();
				const node_type* begin = static_cast<const node_type*>(this);
				while (curr && curr != begin) { ++count; curr = curr->next_ptr(); }
				return count;
			}

			xhqm::index prev_index(const node_type* ptr) const
			{
				xhqm::index index = 0;
				node_type* curr = prev_ptr();
				const node_type* begin = static_cast<const node_type*>(this);
				if (begin == ptr) return index;
				while (curr && curr != begin)
				{
					++index;
					if (curr == ptr) return index;
					curr = curr->prev_ptr();
				}
				return xhqm::npos;   // 未找到返回 npos（库内哨兵约定）
			}
			xhqm::index next_index(const node_type* ptr) const
			{
				xhqm::index index = 0;
				node_type* curr = next_ptr();
				const node_type* begin = static_cast<const node_type*>(this);
				if (begin == ptr) return index;
				while (curr && curr != begin)
				{
					++index;
					if (curr == ptr) return index;
					curr = curr->next_ptr();
				}
				return xhqm::npos;   // 未找到返回 npos（库内哨兵约定）
			}
		};

		//继承用链基础类型
		template<typename _node_type>
		class derive_list_ : public list_node_<_node_type, void, void, list_dir::list_null>
		{
		public:
			friend struct list_node_<_node_type, void, void, list_dir::list_null>;
			using node_base = list_node_<_node_type, void, void, list_dir::list_null>;
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
			virtual ~derive_list_() {};
			constexpr explicit derive_list_() {};
			constexpr explicit derive_list_(derive_list_&&) = delete;
			constexpr explicit derive_list_(const derive_list_&) = delete;
		protected:
			virtual bool _set_prev_ptr(node_type* prev_ptr_) = 0;
			virtual bool _set_next_ptr(node_type* next_ptr_) = 0;
		};

		//队queue广度 栈stack深度
		template<typename _list_type>
		struct list_iterator_restraint
		{
			using list_type = _list_type;
			using node_type = typename list_type::list_node;
			using node_data = typename list_type::node_data;
			using edge_data = typename list_type::edge_data;
			using edge_data_ref = typename list_type::edge_data_ref;
			using reference		= typename list_type::node_data_ref;
			using pointer		= typename list_type::node_data_ptr;
			using list_ptr = list_type*;
			using node_ptr = node_type*;
			//迭代器 traits（std::distance/count/find 等泛型算法可用）
			//void 节点数据时解引用得节点指针，否则得节点数据引用
			using value_type = typename ::xhqm::select_if<::xhqm::is_same<void, node_data>, node_type*, node_data>::type;
			using difference_type = std::ptrdiff_t;
			using iterator_category = std::forward_iterator_tag;
		};

		template<typename _iter_type, typename _list_type>
		class list_iterator_ : public list_iterator_restraint<_list_type>
		{
		public:
			using list_iterator = _iter_type;
			using list_struct = list_iterator_restraint<_list_type>;
			using list_type = typename list_struct::list_type;
			using node_type = typename list_struct::node_type;
			using node_data = typename list_struct::node_data;
			using edge_data = typename list_struct::edge_data;
			using edge_data_ref = typename list_struct::edge_data_ref;
			using reference		= typename list_struct::reference;
			using pointer		= typename list_struct::pointer;
			using list_ptr = typename list_struct::list_ptr;
			using node_ptr = typename list_struct::node_ptr;
		protected: node_ptr curr_ptr = nullptr;
			constexpr list_iterator_(node_ptr curr_ptr_ = nullptr) : curr_ptr(curr_ptr_) { };
		public:
			template<typename __iter_type> constexpr operator __iter_type() { return __iter_type(curr_ptr); };

			//constexpr list_iterator operator =(list_iterator&& move) { this->curr_ptr = xhqm::move(move.curr_ptr); return (*this); };
			//constexpr list_iterator operator =(const list_iterator& copy) { this->curr_ptr = copy.curr_ptr; return (*this); };

			constexpr const reference operator *() const noexcept
			{
				if constexpr (xhqm::is_same<void, node_data>) { return this->curr_ptr; };
				if constexpr (!xhqm::is_same<void, node_data>) { return this->curr_ptr->data; };
			};
			constexpr reference operator *() noexcept
			{
				if constexpr (xhqm::is_same<void, node_data>) { return this->curr_ptr; };
				if constexpr (!xhqm::is_same<void, node_data>) { return this->curr_ptr->data; };
			};
			constexpr pointer operator->() const noexcept
			{
				if constexpr (xhqm::is_same<void, node_data>)
				{
					return this->curr_ptr;
				};
				if constexpr (!xhqm::is_same<void, node_data>)
				{
					if (this->curr_ptr) return &(this->curr_ptr->data);
				};
				return nullptr;
			}

			const edge_data_ref edge_of_prev() const
			{
				if constexpr (!xhqm::is_same<void, edge_data>)
				{
					return this->curr_ptr->prev_edge()->data;
				}
			};
			edge_data_ref edge_of_prev()
			{
				if constexpr (!xhqm::is_same<void, edge_data>)
				{
					return this->curr_ptr->prev_edge()->data;
				}
			};
			const edge_data_ref edge_of_next() const
			{
				if constexpr (!xhqm::is_same<void, edge_data>)
				{
					return this->curr_ptr->next_edge()->data;
				}
			};
			edge_data_ref edge_of_next()
			{
				if constexpr (!xhqm::is_same<void, edge_data>)
				{
					return this->curr_ptr->next_edge()->data;
				}
			};
		};
	}

	//向上遍历
	template<typename _list_type>
	class prev_iterator : public _graph_::list_iterator_<prev_iterator<_list_type>, _list_type>
	{
		using list_iterator = _graph_::list_iterator_<prev_iterator<_list_type>, _list_type>;
	public:
		using list_type = typename list_iterator::list_type;
		using node_type = typename list_iterator::node_type;
		using node_data = typename list_iterator::node_data;
		using reference = typename list_iterator::reference;
		using pointer   = typename list_iterator::pointer;
		using list_ptr = typename list_iterator::list_ptr;
		using node_ptr = typename list_iterator::node_ptr;
	public:
		constexpr bool operator ==(const prev_iterator& list_iter_) const { return (this->curr_ptr == list_iter_.curr_ptr); };
		constexpr bool operator !=(const prev_iterator& list_iter_) const { return (this->curr_ptr != list_iter_.curr_ptr); };
		constexpr prev_iterator(node_ptr curr_ptr_ = nullptr) : list_iterator(curr_ptr_) { };
		constexpr prev_iterator& next()
		{
			if (this->curr_ptr) this->curr_ptr = this->curr_ptr->prev_ptr(); return *this;
		}
		constexpr prev_iterator& prev()
		{
			if (this->curr_ptr) this->curr_ptr = this->curr_ptr->next_ptr(); return *this;
		}

		constexpr prev_iterator& operator ++() { return this->next(); };
		constexpr prev_iterator& operator --() { return this->prev(); };
		constexpr prev_iterator operator ++(int) { prev_iterator iter(this->curr_ptr); this->operator++();; return iter; };
		constexpr prev_iterator operator --(int) { prev_iterator iter(this->curr_ptr); this->operator--();; return iter; };
		constexpr prev_iterator& operator +=(xhqm::count count) { while (this->curr_ptr && count) { --count; this->operator++(); }; return *this; };
		constexpr prev_iterator& operator -=(xhqm::count count) { while (this->curr_ptr && count) { --count; this->operator--(); }; return *this; };
		constexpr prev_iterator operator +(const xhqm::count& count) { prev_iterator iter(this->curr_ptr); iter += count; return iter; };
		constexpr prev_iterator operator -(const xhqm::count& count) { prev_iterator iter(this->curr_ptr); iter -= count; return iter; };
	};
	//向下遍历
	template<typename _list_type>
	class next_iterator : public _graph_::list_iterator_<next_iterator<_list_type>, _list_type>
	{
		using list_iterator = _graph_::list_iterator_<next_iterator<_list_type>, _list_type>;
	public:
		using list_type = typename list_iterator::list_type;
		using node_type = typename list_iterator::node_type;
		using node_data = typename list_iterator::node_data;
		using reference = typename list_iterator::reference;
		using pointer = typename list_iterator::pointer;
		using list_ptr = typename list_iterator::list_ptr;
		using node_ptr = typename list_iterator::node_ptr;
	public:
		constexpr bool operator ==(const next_iterator& list_iter_) const { return (this->curr_ptr == list_iter_.curr_ptr); };
		constexpr bool operator !=(const next_iterator& list_iter_) const { return (this->curr_ptr != list_iter_.curr_ptr); };
		constexpr next_iterator(node_ptr curr_ptr_ = nullptr) : list_iterator(curr_ptr_) { };
		constexpr next_iterator& next()
		{
			if (this->curr_ptr) this->curr_ptr = this->curr_ptr->next_ptr(); return *this;
		}
		constexpr next_iterator& prev()
		{
			if (this->curr_ptr) this->curr_ptr = this->curr_ptr->prev_ptr(); return *this;
		}

		constexpr next_iterator& operator ++() { return this->next(); };
		constexpr next_iterator& operator --() { return this->prev(); };
		constexpr next_iterator operator ++(int) { next_iterator iter(this->curr_ptr); this->operator++();; return iter; };
		constexpr next_iterator operator --(int) { next_iterator iter(this->curr_ptr); this->operator--();; return iter; };
		constexpr next_iterator& operator +=(xhqm::count count) { while (this->curr_ptr && count) { --count; this->operator++(); }; return *this; };
		constexpr next_iterator& operator -=(xhqm::count count) { while (this->curr_ptr && count) { --count; this->operator--(); }; return *this; };
		constexpr next_iterator operator +(const xhqm::count& count) { next_iterator iter(this->curr_ptr); iter += count; return iter; };
		constexpr next_iterator operator -(const xhqm::count& count) { next_iterator iter(this->curr_ptr); iter -= count; return iter; };
	};

	//通用链表迭代器
	template<typename _list_type> using list_iterator = next_iterator<_list_type>;

	//通用继承用链表类型
	template<typename _node_type>
	class derive_list : public xhqm::_graph_::derive_list_<_node_type>
	{
	public:
		using iterator = xhqm::next_iterator<_node_type>;
		using list_base = xhqm::_graph_::derive_list_<_node_type>;
		using list_node = typename list_base::node_type;
		using node_type = typename list_base::node_type;
		using edge_type = typename list_base::edge_type;
		using node_data_ref = node_type*;
		using edge_data_ref = void;
		using node_data_ptr = node_type*;
		using edge_data_ptr = void;
		using node_data = void;
		using edge_data = void;
		using node_ptr = node_type*;
		using node_ref = node_type&;
	public:
		virtual bool _set_prev_ptr(node_type* prev_ptr_) override final
		{
			if (prev_ptr_)
			{
				//断开新节点的原有链接
				if (prev_ptr_->next_ptr()) prev_ptr_->next_ptr()->prev_loca = nullptr;
				prev_ptr_->next_loca = static_cast<node_type*>(this);
			}
			// 断开旧节点的原有链接
			if (this->prev_ptr()) this->prev_ptr()->next_loca = nullptr;
			this->prev_loca = prev_ptr_;
			return true;
		}
		virtual bool _set_next_ptr(node_type* next_ptr_) override final
		{
			if (next_ptr_)
			{
				//断开新节点的原有链接
				if (next_ptr_->prev_ptr()) next_ptr_->prev_ptr()->next_loca = nullptr;
				next_ptr_->prev_loca = static_cast<node_type*>(this);
			}
			// 断开旧节点的原有链接
			if (this->next_ptr()) this->next_ptr()->prev_loca = nullptr;
			this->next_loca = next_ptr_;
			return true;
		}

		//begin是自身
		inline iterator begin() const noexcept { return iterator((node_type*)this); };
		//begin是自身
		inline iterator begin() noexcept { return iterator((node_type*)this); };
		//end是nullptr
		inline iterator end() const noexcept { return iterator(nullptr); };
		//end是nullptr
		inline iterator end() noexcept { return iterator(nullptr); };
	};

	class test_list : public derive_list<test_list>
	{
	public:
		char value = 0;
		test_list(char v = 0) :value(v) {};
	};

	//test_list list;
}

#endif
