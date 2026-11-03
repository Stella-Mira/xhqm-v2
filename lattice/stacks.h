#ifndef XHQM_STACKS
#define XHQM_STACKS
#include <mutex>
#include <stack>
#include <functional>
#include <utility>

namespace xhqm
{
	//栈 先进后出（线程安全，std::mutex）
	template<class type>
	class stacks {
		std::mutex m_mutex;
		std::stack<type> m_stacks;
	public:
		inline stacks() {};

		//查询
		inline xhqm::size size()
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			return m_stacks.size();
		}
		inline bool empty()
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			return m_stacks.empty();
		}
		inline void clear()
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_stacks = std::stack<type>();
		}

		inline void set(const type& dataref)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_stacks.emplace(dataref);
		}
		inline void set(type&& dataref)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_stacks.emplace(std::move(dataref));
		}

		inline void set_if(const std::function<bool(const type&)>& function, const type& dataref)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (function(dataref))
				m_stacks.emplace(dataref);
		}

		inline bool get(type& dataref)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (m_stacks.size())
			{
				dataref = std::move(m_stacks.top());
				m_stacks.pop();
				return true;
			}
			return false;
		}

		inline bool get_if(const std::function<bool(const type&)>& function, type& dataref)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (m_stacks.size())
			{
				if (function(m_stacks.top()))
				{
					dataref = std::move(m_stacks.top());
					m_stacks.pop();
					return true;
				}
			}
			return false;
		}

		inline bool pop()
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (m_stacks.size())
			{
				m_stacks.pop();
				return true;
			}
			return false;
		}

		inline bool pop_if(const std::function<bool(const type&)>& function)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (m_stacks.size())
			{
				if (function(m_stacks.top()))
				{
					m_stacks.pop();
					return true;
				}
			}
			return false;
		}
	};
}
#endif
