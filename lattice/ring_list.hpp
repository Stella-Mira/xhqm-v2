/*
*作者：星辉清梦
*时间：龙—甲辰丙子己亥甲戌
*用途：双向链表
* 性质：
* 1、for循环遍历时自curr节点向后遍历
* 2、初始化时，curr最终会指在最后一项
* 3、不可添加首项或末项数据
* 4、可添加前一项或后一项数据
* 5、当前项可以前移可以后移
*/

#pragma once
#ifndef XHQM_META_RING_LIST
#define XHQM_META_RING_LIST

#include "./struct.h"
#include "./list.hpp"

namespace xhqm
{
	namespace _graph_
	{
		//环形链表节点
		template<typename _node_data, typename _edge_data = void>
		struct ring_node : public list_node_<ring_node<_node_data, _edge_data>, _node_data, _edge_data, list_dir::list_bind>
		{
			friend struct list_node_<ring_node<_node_data, _edge_data>, _node_data, _edge_data, list_dir::list_bind>;
			using node_base = list_node_<ring_node<_node_data, _edge_data>, _node_data, _edge_data, list_dir::list_bind>;
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
			inline bool _set_prev_ptr(node_type* prev_ptr_)
			{
				if (nullptr == prev_ptr_) return false;
				if (this->prev_ptr() == prev_ptr_) return true;

				loca_type* this_prev_edge_ptr = this->prev_loca;
				loca_type* prev_next_edge_ptr = prev_ptr_->next_loca;
				prev_next_edge_ptr->from_loca = this->prev_ptr();
				this->prev_ptr()->next_loca = prev_next_edge_ptr;
				this_prev_edge_ptr->from_loca = prev_ptr_;
				prev_ptr_->next_loca = this_prev_edge_ptr;
				return true;
			}
			inline bool _set_next_ptr(node_type* next_ptr_)
			{
				if (nullptr == next_ptr_) return false;
				if (this->next_ptr() == next_ptr_) return true;

				loca_type* this_next_edge_ptr = this->next_loca;
				loca_type* next_prev_edge_ptr = next_ptr_->prev_loca;
				next_prev_edge_ptr->goto_loca = this->next_ptr();
				this->next_ptr()->prev_loca = next_prev_edge_ptr;
				this_next_edge_ptr->goto_loca = next_ptr_;
				next_ptr_->prev_loca = this_next_edge_ptr;
				return true;
			}
		public:
			ring_node()
			{
				this->prev_loca = new loca_type();
				this->next_loca = this->prev_loca;
				this->next_loca->from_loca = this;
				this->next_loca->goto_loca = this;
			};
			~ring_node() { _set_next_ptr(this); delete this->prev_loca; };

			template<typename __data_type> ring_node(__data_type&& data_) noexcept
				: node_base(xhqm::move(data_))
			{
				this->prev_loca = new loca_type();
				this->next_loca = this->prev_loca;
				this->next_loca->from_loca = this;
				this->next_loca->goto_loca = this;
			}
			template<typename __data_type> ring_node(const __data_type& data_) noexcept
				: node_base(xhqm::copy(data_))
			{
				this->prev_loca = new loca_type();
				this->next_loca = this->prev_loca;
				this->next_loca->from_loca = this;
				this->next_loca->goto_loca = this;
			}
		};
	}

	//环形链表迭代器
	template<typename _list_type>
	class ring_iterator : public _graph_::list_iterator_<ring_iterator<_list_type>, _list_type>
	{
		using list_iterator = _graph_::list_iterator_<ring_iterator<_list_type>, _list_type>;
	public:
		using list_type = typename list_iterator::list_type;
		using node_type = typename list_iterator::node_type;
		using node_data = typename list_iterator::node_data;
		using reference = typename list_iterator::reference;
		using pointer = typename list_iterator::pointer;
		using list_ptr = typename list_iterator::list_ptr;
		using node_ptr = typename list_iterator::node_ptr;
	protected: node_ptr star_ptr = nullptr;
	public:
		constexpr ring_iterator(node_ptr curr_ptr_ = nullptr) : list_iterator(curr_ptr_) { };
		constexpr ring_iterator& next()
		{
			if (this->star_ptr == nullptr) this->star_ptr = this->curr_ptr;
			if (this->curr_ptr == nullptr) this->star_ptr = this->curr_ptr;
			if (this->curr_ptr) this->curr_ptr = this->curr_ptr->next_ptr();
			if (this->star_ptr == this->curr_ptr)
			{
				this->curr_ptr = nullptr;
				this->star_ptr = nullptr;
			}
			return *this;
		}
		constexpr ring_iterator& prev()
		{
			if (this->star_ptr == nullptr) this->star_ptr = this->curr_ptr;
			if (this->curr_ptr == nullptr) this->star_ptr = this->curr_ptr;
			if (this->curr_ptr) this->curr_ptr = this->curr_ptr->prev_ptr();
			if (this->star_ptr == this->curr_ptr)
			{
				this->curr_ptr = nullptr;
				this->star_ptr = nullptr;
			}
			return *this;
		}

		constexpr ring_iterator& operator ++() { return this->next(); };
		constexpr ring_iterator& operator --() { return this->prev(); };
		constexpr ring_iterator operator ++(int) { ring_iterator tmp(*this); ++(*this); return tmp; };
		constexpr ring_iterator operator --(int) { ring_iterator tmp(*this); --(*this); return tmp; };

		constexpr bool operator ==(const ring_iterator& list_iter_) const { return (this->star_ptr == list_iter_.star_ptr && this->curr_ptr == list_iter_.curr_ptr); };		constexpr bool operator !=(const ring_iterator& list_iter_) const { return (this->star_ptr != list_iter_.star_ptr || this->curr_ptr != list_iter_.curr_ptr); };
	};

	template<typename _node_data, typename _edge_data = void>
	class ring_list
	{
	public:
		using list_node = xhqm::_graph_::ring_node<_node_data, _edge_data>;
		using iterator = xhqm::ring_iterator<ring_list>;
		using node_data_ref = typename list_node::node_data_ref;
		using edge_data_ref = typename list_node::edge_data_ref;
		using node_data_ptr = typename list_node::node_data_ptr;
		using edge_data_ptr = typename list_node::edge_data_ptr;
		using node_data = typename list_node::node_data;
		using edge_data = typename list_node::edge_data;
		using node_ptr = list_node*;
		using node_ref = list_node&;
	protected:
		//stop向前指向star
		xhqm::count m_count = 0;
		node_ptr curr_ptr = nullptr;
	public:
		const edge_data_ref edge_of_prev() const
		{
			if constexpr (!xhqm::is_same<void, edge_data>)
			{
				return curr_ptr->prev_edge()->data;
			}
		};
		edge_data_ref edge_of_prev()
		{
			if constexpr (!xhqm::is_same<void, edge_data>)
			{
				return curr_ptr->prev_edge()->data;
			}
		};
		const edge_data_ref edge_of_next() const
		{
			if constexpr (!xhqm::is_same<void, edge_data>)
			{
				return curr_ptr->next_edge()->data;
			}
		};
		edge_data_ref edge_of_next()
		{
			if constexpr (!xhqm::is_same<void, edge_data>)
			{
				return curr_ptr->next_edge()->data;
			}
		};

		inline bool empty() const noexcept { return curr_ptr == nullptr; };
		inline xhqm::size size() const noexcept { return m_count; };
		inline void clear() 
		{ 
			while (curr_ptr) this->pop_curr();
			curr_ptr = nullptr;
			m_count = 0;
		}

		//begin是首部一项
		inline iterator begin() const noexcept { return iterator(curr_ptr); };
		//begin是首部一项
		inline iterator begin() noexcept { return iterator(curr_ptr); };
		//end是nullptr
		inline iterator end() const noexcept { return iterator(); };
		//end是nullptr
		inline iterator end() noexcept { return iterator(); };

		inline node_data_ref curr() { return curr_ptr->data; };
		inline const node_data_ref curr() const { return curr_ptr->data; };

		//添加前项
		void push_prev(const node_data& data)
		{
			if (curr_ptr == nullptr)
			{
				curr_ptr = new list_node(xhqm::copy(data));
			}
			else
			{
				curr_ptr->set_prev_ptr(new list_node(xhqm::copy(data)));
				curr_ptr = curr_ptr->prev_ptr();
			}
			++m_count;
		};
		//添加后项
		void push_next(const node_data& data)
		{
			if (curr_ptr == nullptr)
			{
				curr_ptr = new list_node(xhqm::copy(data));
			}
			else
			{
				curr_ptr->set_next_ptr(new list_node(xhqm::copy(data)));
				curr_ptr = curr_ptr->next_ptr();
			}
			++m_count;
		};
		//添加前项
		void push_prev(node_data&& data)
		{
			if (curr_ptr == nullptr)
			{
				curr_ptr = new list_node(xhqm::move(data));
			}
			else
			{
				curr_ptr->set_prev_ptr(new list_node(xhqm::move(data)));
				curr_ptr = curr_ptr->prev_ptr();
			}
			++m_count;
		};
		//添加后项
		void push_next(node_data&& data)
		{
			if (curr_ptr == nullptr)
			{
				curr_ptr = new list_node(xhqm::move(data));
			}
			else
			{
				curr_ptr->set_next_ptr(new list_node(xhqm::move(data)));
				curr_ptr = curr_ptr->next_ptr();
			}
			++m_count;
		};

		//抛出上一项
		bool pop_prev()
		{
			if (curr_ptr == nullptr) return false;
			auto prev_ptr = curr_ptr->prev_ptr();
			if (prev_ptr == curr_ptr) curr_ptr = nullptr;
			delete prev_ptr; --m_count; return true;
		};
		//抛出当前项
		bool pop_curr()
		{
			if (curr_ptr == nullptr) return false;
			auto prev_ptr = curr_ptr->prev_ptr();
			auto next_ptr = curr_ptr->next_ptr();
			auto temp_ptr = curr_ptr;
			if (prev_ptr == curr_ptr && curr_ptr == next_ptr)
			{
				curr_ptr = nullptr;
			}
			else
			{
				curr_ptr = next_ptr;
			}
			delete temp_ptr; --m_count;
			return true;
		};
		//抛出下一项
		bool pop_next()
		{
			if (curr_ptr == nullptr) return false;
			auto next_ptr = curr_ptr->next_ptr();
			if (curr_ptr == next_ptr) curr_ptr = nullptr;
			delete next_ptr; --m_count; return true;
		};

		//前移多项
		void prev(xhqm::count size)
		{
			for (; size != 0; --size)
			{
				if (!this->prev()) return;
			}
		}
		//后移多项
		void next(xhqm::count size)
		{
			for (; size != 0; --size)
			{
				if (!this->next()) return;
			}
		}
		//前移一项
		bool prev()
		{
			if (curr_ptr == nullptr) return false;
			curr_ptr = curr_ptr->prev_ptr();
			return true;
		};
		//后移一项
		bool next()
		{
			if (curr_ptr == nullptr) return false;
			curr_ptr = curr_ptr->next_ptr();
			return true;
		};

	public:
		inline ring_list& operator << (const node_data& data_) noexcept
		{
			this->push_prev(data_); return *this;
		};
		inline ring_list& operator << (node_data&& data_) noexcept
		{
			this->push_prev(xhqm::move(data_)); return *this;
		};
		inline ring_list& operator >> (const node_data& data_) noexcept
		{
			this->push_next(data_); return *this;
		};
		inline ring_list& operator >> (node_data&& data_) noexcept
		{
			this->push_next(xhqm::move(data_)); return *this;
		};
		inline ring_list& operator < (const node_data& data_) noexcept
		{
			curr_ptr->data = data_; return *this;
		};
		inline ring_list& operator > (node_data& data_) const noexcept
		{
			data_ = curr_ptr->data; return *this;
		};
	public:
		constexpr ring_list& operator =(ring_list&& move_) noexcept
		{
			this->clear();
			this->curr_ptr = move_.curr_ptr;
			this->m_count = move_.m_count;
			move_.curr_ptr = nullptr;
			move_.m_count = 0;
			return (*this);
		};

		constexpr inline void swap(ring_list& list_) noexcept
		{
			auto temp_curr_ptr = this->curr_ptr;
			auto temp_count = this->m_count;
			this->curr_ptr = list_.curr_ptr;
			this->m_count = list_.m_count;
			list_.curr_ptr = temp_curr_ptr;
			list_.m_count = temp_count;
		}

		template<typename _iterator> ring_list(_iterator first_, _iterator last_) noexcept
		{
			for (; first_ != last_; ++first_) this->push_next(*first_);
		};
		ring_list(std::initializer_list<_node_data>&& data_list_) noexcept
		{
			for (auto& value : data_list_) this->push_next(value);
		};
		ring_list() noexcept {};

		ring_list(const ring_list& copy_) noexcept
			: ring_list(copy_.begin(), copy_.end())
		{

		};
		ring_list(ring_list&& move_) noexcept
		{
			this->curr_ptr = move_.curr_ptr;
			this->m_count = move_.m_count;
			move_.curr_ptr = nullptr;
			move_.m_count = 0;
		};
		~ring_list() noexcept
		{
			this->clear();
		};
	};

	//推导指引
	template <class _node_data> ring_list(std::initializer_list<_node_data>) -> ring_list<_node_data, void>;
}

#endif