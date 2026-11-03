/*
*作者：星辉清梦
*时间：龙—甲辰丙子己亥甲子 
*用途：双向链表
* 性质：
* 1、for循环遍历时自star节点向后遍历到stop
* 2、可添加当前数据的前一项数据
* 3、可添加当前数据的后一项数据
* 4、当前项可以前移可以后移
*/

#pragma once
#ifndef XHQM_META_BIND_LIST
#define XHQM_META_BIND_LIST

#include "./struct.h"
#include "./list.hpp"

namespace xhqm
{
	namespace _graph_
	{
		//记录上下两项
		template<typename _node_data, typename _edge_data = void>
		struct bind_list_node : public list_node_<bind_list_node<_node_data, _edge_data>, _node_data, _edge_data, list_dir::list_bind>
		{
			friend struct list_node_<bind_list_node<_node_data, _edge_data>, _node_data, _edge_data, list_dir::list_bind>;
			using node_base = list_node_<bind_list_node<_node_data, _edge_data>, _node_data, _edge_data, list_dir::list_bind>;
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
				if (prev_ptr_ == nullptr)
				{
					if (this->prev_ptr())
					{
						this->prev_ptr()->next_loca = nullptr;
						delete this->prev_loca; this->prev_loca = nullptr;
					}
				}
				else
				{
					loca_type* this_prev_edge_ptr = this->prev_loca;
					loca_type* prev_next_edge_ptr = prev_ptr_->next_loca;
					if (this_prev_edge_ptr == nullptr && prev_next_edge_ptr == nullptr)
					{
						//不存在前一项且新前一项不存在后一项
						loca_type* new_edge_ptr = new loca_type();
						new_edge_ptr->from_loca = prev_ptr_;
						new_edge_ptr->goto_loca = static_cast<node_type*>(this);
						prev_ptr_->next_loca = new_edge_ptr;
						this->prev_loca = new_edge_ptr;
					}
					else if (this_prev_edge_ptr && prev_next_edge_ptr == nullptr)
					{
						//存在前一项且新前一项不存在后一项
						this->prev_ptr()->next_loca = nullptr;
						this_prev_edge_ptr->from_loca = prev_ptr_;
						prev_ptr_->next_loca = this_prev_edge_ptr;
					}
					else if (this_prev_edge_ptr == nullptr && prev_next_edge_ptr)
					{
						//不存在前一项且新前一项存在后一项
						prev_ptr_->next_ptr()->prev_loca = nullptr;
						prev_next_edge_ptr->goto_loca = static_cast<node_type*>(this);
						this->prev_loca = prev_next_edge_ptr;
					}
					else if (this_prev_edge_ptr && prev_next_edge_ptr)
					{
						//存在前一项且新前一项存在后一项
						this->prev_ptr()->next_loca = nullptr;
						prev_ptr_->next_ptr()->prev_loca = nullptr;

						loca_type* new_edge_ptr = new loca_type();
						new_edge_ptr->from_loca = prev_ptr_;
						new_edge_ptr->goto_loca = static_cast<node_type*>(this);
						prev_ptr_->next_loca = new_edge_ptr;
						this->prev_loca = new_edge_ptr;
						delete this_prev_edge_ptr;
						delete prev_next_edge_ptr;
					}
				}

				return true;
			}
			inline bool _set_next_ptr(node_type* next_ptr_)
			{
				if (this->next_ptr() == next_ptr_) return true;
				if (next_ptr_ == nullptr)
				{
					if (this->next_ptr())
					{
						this->next_ptr()->prev_loca = nullptr;
						delete this->next_loca; this->next_loca = nullptr;
					}
				}
				else
				{
					loca_type* this_next_edge_ptr = this->next_loca;
					loca_type* next_prev_edge_ptr = next_ptr_->prev_loca;
					if (this_next_edge_ptr == nullptr && next_prev_edge_ptr == nullptr)
					{
						//不存在后一项且新后一项不存在前一项
						loca_type* new_edge_ptr = new loca_type();
						new_edge_ptr->from_loca = static_cast<node_type*>(this);
						new_edge_ptr->goto_loca = next_ptr_;
						next_ptr_->prev_loca = new_edge_ptr;
						this->next_loca = new_edge_ptr;
					}
					else if (this_next_edge_ptr && next_prev_edge_ptr == nullptr)
					{
						//存在后一项且新后一项不存在前一项
						this->next_ptr()->prev_loca = nullptr;
						this_next_edge_ptr->goto_loca = next_ptr_;
						next_ptr_->prev_loca = this_next_edge_ptr;
					}
					else if (this_next_edge_ptr == nullptr && next_prev_edge_ptr)
					{
						//不存在后一项且新后一项存在前一项
						next_ptr_->prev_ptr()->next_loca = nullptr;
						next_prev_edge_ptr->from_loca = static_cast<node_type*>(this);
						this->next_loca = next_prev_edge_ptr;
					}
					else if (this_next_edge_ptr && next_prev_edge_ptr)
					{
						//存在后一项且新后一项存在前一项
						this->next_ptr()->prev_loca = nullptr;
						next_ptr_->prev_ptr()->next_loca = nullptr;

						loca_type* new_edge_ptr = new loca_type();
						new_edge_ptr->from_loca = static_cast<node_type*>(this);
						new_edge_ptr->goto_loca = next_ptr_;
						next_ptr_->prev_loca = new_edge_ptr;
						this->next_loca = new_edge_ptr;
						delete this_next_edge_ptr;
						delete next_prev_edge_ptr;
					}
				}

				return true;
			}
		public:
			bind_list_node() {};
			~bind_list_node() { _set_prev_ptr(nullptr); _set_next_ptr(nullptr); };

			template<typename __data_type> bind_list_node(__data_type&& data_) noexcept
				: node_base(xhqm::move(data_)) { }
			template<typename __data_type> bind_list_node(const __data_type& data_) noexcept
				: node_base(xhqm::copy(data_)) { }
		};
	}

	template<typename _node_data, typename _edge_data = void>
	class bind_list
	{
	public:
		using list_node = xhqm::_graph_::bind_list_node<_node_data, _edge_data>;
		using iterator = xhqm::list_iterator<bind_list>;
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
		void clear()
		{
			while (star_ptr)
			{
				curr_ptr = star_ptr;
				star_ptr = star_ptr->next_ptr();
				delete curr_ptr;
			}
			star_ptr = nullptr;
			curr_ptr = nullptr;
			stop_ptr = nullptr;
			m_count = 0;
		}

		//begin是首部一项
		inline iterator begin() const noexcept { return iterator(star_ptr); };
		//begin是首部一项
		inline iterator begin() noexcept { return iterator(star_ptr); };
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
				curr_ptr->set_next_ptr(star_ptr);
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
			else
			{
				auto temp_ptr = curr_ptr->prev_ptr();
				curr_ptr->set_prev_ptr(new list_node(xhqm::copy(data)));
				curr_ptr = curr_ptr->prev_ptr();
				curr_ptr->set_prev_ptr(temp_ptr);
			}
			++m_count;
		};
		//添加后项
		void push_next(const node_data& data)
		{
			if (curr_ptr == nullptr)
			{
				curr_ptr = new list_node(xhqm::copy(data));
				star_ptr = curr_ptr;
				stop_ptr = curr_ptr;
			}
			else if (curr_ptr == stop_ptr)
			{
				curr_ptr = new list_node(xhqm::copy(data));
				stop_ptr->set_next_ptr(curr_ptr);
				stop_ptr = curr_ptr;
			}
			else
			{
				auto temp_ptr = curr_ptr->next_ptr();
				curr_ptr->set_next_ptr(new list_node(xhqm::copy(data)));
				curr_ptr = curr_ptr->next_ptr();
				curr_ptr->set_next_ptr(temp_ptr);
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
				curr_ptr->set_next_ptr(star_ptr);
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
			else
			{
				auto temp_ptr = curr_ptr->prev_ptr();
				curr_ptr->set_prev_ptr(new list_node(xhqm::move(data)));
				curr_ptr = curr_ptr->prev_ptr();
				curr_ptr->set_prev_ptr(temp_ptr);
			}
			++m_count;
		};
		//添加后项
		void push_next(node_data&& data)
		{
			if (curr_ptr == nullptr)
			{
				curr_ptr = new list_node(xhqm::move(data));
				star_ptr = curr_ptr;
				stop_ptr = curr_ptr;
			}
			else if (curr_ptr == stop_ptr)
			{
				curr_ptr = new list_node(xhqm::move(data));
				stop_ptr->set_next_ptr(curr_ptr);
				stop_ptr = curr_ptr;
			}
			else
			{
				auto temp_ptr = curr_ptr->next_ptr();
				curr_ptr->set_next_ptr(new list_node(xhqm::move(data)));
				curr_ptr = curr_ptr->next_ptr();
				curr_ptr->set_next_ptr(temp_ptr);
			}
			++m_count;
		};

		//抛出首项
		bool pop_front()
		{
			if (star_ptr == nullptr) return false;
			curr_ptr = star_ptr;
			star_ptr = star_ptr->next_ptr();
			delete curr_ptr; curr_ptr = star_ptr;
			if (star_ptr == nullptr) stop_ptr = nullptr;
			--m_count; return true;
		};
		//抛出当前
		bool pop_curr()
		{
			if (curr_ptr == nullptr) return false;
			else if (curr_ptr == star_ptr)
			{
				star_ptr = star_ptr->next_ptr();
				delete curr_ptr; curr_ptr = star_ptr;
				if (star_ptr == nullptr) stop_ptr = nullptr;
				--m_count; return true;
			}
			else if (curr_ptr == stop_ptr)
			{
				stop_ptr = stop_ptr->prev_ptr();
				delete curr_ptr; curr_ptr = stop_ptr;
				if (stop_ptr == nullptr) star_ptr = nullptr;
				--m_count; return true;
			}
			else
			{
				auto temp_ptr = curr_ptr;
				curr_ptr->prev_ptr()->set_next_ptr(curr_ptr->next_ptr());
				curr_ptr = curr_ptr->next_ptr();
				delete temp_ptr; --m_count; 
				return true;
			}
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
			if (!curr_ptr->prev_ptr()) return false;
			curr_ptr = curr_ptr->prev_ptr();
			return true;
		};
		//后移一项
		bool next()
		{
			if (curr_ptr == nullptr) return false;
			if (!curr_ptr->next_ptr()) return false;
			curr_ptr = curr_ptr->next_ptr();
			return true;
		};

	public:
		inline bind_list& operator << (const node_data& data_) noexcept
		{
			this->push_front(data_); return *this;
		};
		inline bind_list& operator << (node_data&& data_) noexcept
		{
			this->push_front(xhqm::move(data_)); return *this;
		};
		inline bind_list& operator >> (const node_data& data_) noexcept
		{
			this->push_back(data_); return *this;
		};
		inline bind_list& operator >> (node_data&& data_) noexcept
		{
			this->push_back(xhqm::move(data_)); return *this;
		};
		inline bind_list& operator < (node_data& data_) noexcept
		{
			data_ = curr_ptr->data; this->prev(); return *this;
		};
		inline bind_list& operator > (node_data& data_) noexcept
		{
			data_ = curr_ptr->data; this->next(); return *this;
		};

	public:
		constexpr bind_list& operator =(bind_list&& move_) noexcept
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

		constexpr inline void swap(bind_list& list_) noexcept
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

		template<typename _iterator> bind_list(_iterator first_, _iterator last_) noexcept
		{
			for (; first_ != last_; ++first_) this->push_next(*first_);
		};
		bind_list(std::initializer_list<_node_data>&& data_list_) noexcept
		{
			for (auto& value : data_list_) this->push_next(value);
		};
		bind_list() noexcept {};

		bind_list(const bind_list& copy_) noexcept
			: bind_list(copy_.begin(), copy_.end())
		{

		};
		bind_list(bind_list&& move_) noexcept
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
		~bind_list() noexcept
		{
			while (star_ptr)
			{
				curr_ptr = star_ptr;
				star_ptr = star_ptr->next_ptr();
				delete curr_ptr;
			}
		};
	};

	//推导指引
	template <class _node_data> bind_list(std::initializer_list<_node_data>) -> bind_list<_node_data, void>;
}

#endif