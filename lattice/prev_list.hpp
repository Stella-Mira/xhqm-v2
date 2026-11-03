/*
*作者：星辉清梦
*时间：龙—甲辰乙亥戊戌癸丑 
*用途：来向链表
* 性质：
* 1、仅可添加当前数据的前一项数据
* 2、for循环遍历时自stop节点向前遍历到star
*/

#pragma once
#ifndef XHQM_META_PREV_LIST
#define XHQM_META_PREV_LIST
#include "./struct.h"
#include "./list.hpp"

namespace xhqm
{
	namespace _graph_
	{
		//仅记录上一项
		template<typename _node_data, typename _edge_data = void>
		struct prev_node : public list_node_<prev_node<_node_data, _edge_data>, _node_data, _edge_data, list_dir::list_prev>
		{
			friend struct list_node_<prev_node<_node_data, _edge_data>, _node_data, _edge_data, list_dir::list_prev>;
			using node_base = list_node_<prev_node<_node_data, _edge_data>, _node_data, _edge_data, list_dir::list_prev>;
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
				if (this->prev_ptr() == prev_ptr_) return true;

				if (this->prev_ptr()) this->prev_ptr()->has_next(false);
				if (prev_ptr_) prev_ptr_->has_next(true);

				if constexpr (xhqm::is_same<void, edge_data>)
				{
					this->prev_loca = prev_ptr_;
				}
				else
				{
					this->prev_loca->from_loca = prev_ptr_;
				};

				return true;
			}
			inline bool _set_next_ptr(node_type* next_ptr_)
			{
				if (next_ptr_ == nullptr) return false;
				return next_ptr_->_set_prev_ptr(static_cast<node_type*>(this));
			}
		public:
			prev_node()
			{
				if constexpr (!xhqm::is_same<void, edge_data>)
				{
					this->prev_loca = new loca_type();
					this->prev_loca->goto_loca = true;
				}
			};
			~prev_node()
			{
				_set_prev_ptr(nullptr);
				if constexpr (!xhqm::is_same<void, edge_data>)
				{
					delete this->prev_loca;
				}
			};

			template<typename __data_type> prev_node(__data_type&& data_) noexcept
				: node_base(xhqm::move(data_))
			{
				if constexpr (!xhqm::is_same<void, edge_data>)
				{
					this->prev_loca = new loca_type();
					this->prev_loca->goto_loca = true;
				}
			}
			template<typename __data_type> prev_node(const __data_type& data_) noexcept
				: node_base(xhqm::copy(data_))
			{
				if constexpr (!xhqm::is_same<void, edge_data>)
				{
					this->prev_loca = new loca_type();
					this->prev_loca->goto_loca = true;
				}
			}
		};
	}

	template<typename _node_data, typename _edge_data = void>
	class prev_list
	{
	public:		
		using list_node = xhqm::_graph_::prev_node<_node_data, _edge_data>;
		using iterator = xhqm::prev_iterator<prev_list>;
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
		node_ptr star_ptr = nullptr;
		node_ptr curr_ptr = nullptr;
		node_ptr stop_ptr = nullptr;
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

		inline bool empty() const noexcept { return stop_ptr == nullptr; };
		inline xhqm::size size() const noexcept { return m_count; };
		void clear()
		{
			while (stop_ptr)
			{
				curr_ptr = stop_ptr;
				stop_ptr = stop_ptr->prev_ptr();
				delete curr_ptr; curr_ptr = nullptr;
			}
			star_ptr = nullptr;
			curr_ptr = nullptr;
			stop_ptr = nullptr;
			m_count = 0;
		}

		//begin是末尾一项
		inline iterator begin() const noexcept { return iterator(stop_ptr); };
		//begin是末尾一项
		inline iterator begin() noexcept { return iterator(stop_ptr); };
		//end是nullptr
		inline iterator end() const noexcept { return iterator(); };
		//end是nullptr
		inline iterator end() noexcept { return iterator(); };

		inline node_data_ref front() { return star_ptr->data; };
		inline const node_data_ref front() const { return star_ptr->data; };
		inline node_data_ref back() { return stop_ptr->data; };
		inline const node_data_ref back() const { return stop_ptr->data; };
		inline node_data_ref curr() { return curr_ptr->data; };
		inline const node_data_ref curr() const { return curr_ptr->data; };

		inline bool is_star() const { return (curr_ptr == star_ptr); };
		inline bool is_stop() const { return (curr_ptr == stop_ptr); };
		inline void curr_reset() { curr_ptr = stop_ptr; };
		inline void curr_star() { curr_ptr = star_ptr; };
		inline void curr_stop() { curr_ptr = stop_ptr; };

		//添加首项
		void push_front(const node_data& data)
		{
			if (star_ptr == nullptr)
			{
				star_ptr = new list_node(xhqm::copy(data));
				curr_ptr = star_ptr;
				stop_ptr = curr_ptr;
			}
			else
			{
				curr_ptr = new list_node(xhqm::copy(data));
				star_ptr->set_prev_ptr(curr_ptr);
				star_ptr = curr_ptr;
			}
			++m_count;
		};
		//添加尾项
		void push_back(const node_data& data)
		{
			if (stop_ptr == nullptr)
			{
				stop_ptr = new list_node(xhqm::copy(data));
				curr_ptr = stop_ptr;
				star_ptr = curr_ptr;
			}
			else
			{
				curr_ptr = new list_node(xhqm::copy(data));
				curr_ptr->set_prev_ptr(stop_ptr);
				stop_ptr = curr_ptr;
			}
			++m_count;
		};
		//添加前项
		void push_prev(const node_data& data)
		{
			if (curr_ptr == nullptr)
			{
				curr_ptr = new list_node(xhqm::copy(data));
				star_ptr = curr_ptr;
				stop_ptr = curr_ptr;
			}
			else if (curr_ptr == star_ptr)
			{
				curr_ptr = new list_node(xhqm::copy(data));
				star_ptr->set_prev_ptr(curr_ptr);
				star_ptr = curr_ptr;
			}
			else if (curr_ptr == stop_ptr)
			{
				curr_ptr = new list_node(xhqm::copy(data));
				curr_ptr->set_prev_ptr(stop_ptr->prev_ptr());
				stop_ptr->set_prev_ptr(curr_ptr);
			}
			else if (curr_ptr != star_ptr && curr_ptr != stop_ptr)
			{
				auto temp_ptr = curr_ptr->prev_ptr();
				curr_ptr->set_prev_ptr(new list_node(xhqm::copy(data)));
				curr_ptr = curr_ptr->prev_ptr();
				curr_ptr->set_prev_ptr(temp_ptr);
			}
			++m_count;
		};

		//添加首项
		void push_front(node_data&& data)
		{
			if (star_ptr == nullptr)
			{
				star_ptr = new list_node(xhqm::move(data));
				curr_ptr = star_ptr;
				stop_ptr = curr_ptr;
			}
			else
			{
				curr_ptr = new list_node(xhqm::move(data));
				star_ptr->set_prev_ptr(curr_ptr);
				star_ptr = curr_ptr;
			}
			++m_count;
		};
		//添加尾项
		void push_back(node_data&& data)
		{
			if (stop_ptr == nullptr)
			{
				stop_ptr = new list_node(xhqm::move(data));
				curr_ptr = stop_ptr;
				star_ptr = curr_ptr;
			}
			else
			{
				curr_ptr = new list_node(xhqm::move(data));
				curr_ptr->set_prev_ptr(stop_ptr);
				stop_ptr = curr_ptr;
			}
			++m_count;
		};
		//添加前项
		void push_prev(node_data&& data)
		{
			if (curr_ptr == nullptr)
			{
				curr_ptr = new list_node(xhqm::move(data));
				star_ptr = curr_ptr;
				stop_ptr = curr_ptr;
			}
			else if (curr_ptr == star_ptr)
			{
				curr_ptr = new list_node(xhqm::move(data));
				star_ptr->set_prev_ptr(curr_ptr);
				star_ptr = curr_ptr;
			}
			else if (curr_ptr == stop_ptr)
			{
				curr_ptr = new list_node(xhqm::move(data));
				curr_ptr->set_prev_ptr(stop_ptr->prev_ptr());
				stop_ptr->set_prev_ptr(curr_ptr);
			}
			else if (curr_ptr != star_ptr && curr_ptr != stop_ptr)
			{
				auto temp_ptr = curr_ptr->prev_ptr();
				curr_ptr->set_prev_ptr(new list_node(xhqm::move(data)));
				curr_ptr = curr_ptr->prev_ptr();
				curr_ptr->set_prev_ptr(temp_ptr);
			}
			++m_count;
		};

		//抛出尾项
		bool pop_back()
		{
			if (stop_ptr == nullptr) return false;
			curr_ptr = stop_ptr;
			stop_ptr = stop_ptr->prev_ptr();
			delete curr_ptr; curr_ptr = stop_ptr;
			if (stop_ptr == nullptr) star_ptr = nullptr;
			--m_count; return true;
		};

		//前移多项
		void prev(xhqm::count size)
		{
			for (; size != 0; --size)
			{
				if (!this->prev()) return;
			}
		}
		//前移一项
		bool prev()
		{
			if (curr_ptr == nullptr) return false;
			if (!curr_ptr->prev_ptr()) return false;
			curr_ptr = curr_ptr->prev_ptr();
			return true;
		};

	public:
		inline prev_list& operator << (const node_data& data_) noexcept
		{
			this->push_front(data_); return *this;
		};
		inline prev_list& operator << (node_data&& data_) noexcept
		{
			this->push_front(xhqm::move(data_)); return *this;
		};
		inline prev_list& operator >> (const node_data& data_) noexcept
		{
			this->push_back(data_); return *this;
		};
		inline prev_list& operator >> (node_data&& data_) noexcept
		{
			this->push_back(xhqm::move(data_)); return *this;
		};
		inline prev_list& operator < (const node_data& data_) noexcept
		{
			curr_ptr->data = data_; return *this;
		};
		inline prev_list& operator > (node_data& data_) const noexcept
		{
			data_ = curr_ptr->data; return *this;
		};

	public:
		constexpr prev_list& operator =(prev_list&& move_) noexcept
		{
			this->clear();
			this->star_ptr = move_.star_ptr;
			this->curr_ptr = move_.curr_ptr;
			this->stop_ptr = move_.stop_ptr;
			this->m_count = move_.m_count;
			move_.star_ptr = nullptr;
			move_.curr_ptr = nullptr;
			move_.stop_ptr = nullptr;
			move_.m_count = 0;
			return (*this);
		};

		constexpr inline void swap(prev_list& list_) noexcept
		{
			auto temp_star_ptr = this->star_ptr;
			auto temp_curr_ptr = this->curr_ptr;
			auto temp_stop_ptr = this->stop_ptr;
			auto temp_count = this->m_count;
			this->star_ptr = list_.star_ptr;
			this->curr_ptr = list_.curr_ptr;
			this->stop_ptr = list_.stop_ptr;
			this->m_count = list_.m_count;
			list_.star_ptr = temp_star_ptr;
			list_.curr_ptr = temp_curr_ptr;
			list_.stop_ptr = temp_stop_ptr;
			list_.m_count = temp_count;
		}

		template<typename _iterator> prev_list(_iterator first_, _iterator last_) noexcept
		{
			for (; first_ != last_; ++first_) this->push_front(*first_);
		};
		prev_list(std::initializer_list<_node_data>&& data_list_) noexcept
		{
			for (auto& value : data_list_) this->push_front(value);
		};
		prev_list() noexcept {};

		prev_list(const prev_list& copy_) noexcept
			: prev_list(copy_.begin(), copy_.end())
		{
		};
		prev_list(prev_list&&move_) noexcept
		{
			this->star_ptr = move_.star_ptr;
			this->curr_ptr = move_.curr_ptr;
			this->stop_ptr = move_.stop_ptr;
			this->m_count = move_.m_count;
			move_.star_ptr = nullptr;
			move_.curr_ptr = nullptr;
			move_.stop_ptr = nullptr;
			move_.m_count = 0;
		};
		~prev_list() noexcept
		{
			while (stop_ptr)
			{
				curr_ptr = stop_ptr;
				stop_ptr = stop_ptr->prev_ptr();
				delete curr_ptr; curr_ptr = nullptr;
			}
		};
	};

	//推导指引
	template <class _node_data> prev_list(std::initializer_list<_node_data>) -> prev_list<_node_data, void>;
}

#endif