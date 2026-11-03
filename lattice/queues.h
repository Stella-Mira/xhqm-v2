#ifndef XHQM_QUEUES
#define XHQM_QUEUES
#include <mutex>
#include <queue>
#include <functional>
#include <utility>

namespace xhqm
{
	//队 先进先出（线程安全，std::mutex）
	template<class type>
	class queues {
		std::mutex m_mutex;
		std::queue<type> m_queues;
	public:
		inline queues() {};

		//查询
		inline xhqm::size size()
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			return m_queues.size();
		}
		inline bool empty()
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			return m_queues.empty();
		}
		inline void clear()
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_queues = std::queue<type>();
		}

		inline void set(const type& dataref)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_queues.emplace(dataref);
		}
		inline void set(type&& dataref)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_queues.emplace(std::move(dataref));
		}

		inline void set_if(const std::function<bool(const type&)>& function, const type& dataref)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if(function(dataref))
				m_queues.emplace(dataref);
		}

		inline bool get(type& dataref)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (m_queues.size())
			{
				dataref = std::move(m_queues.front());
				m_queues.pop();
				return true;
			}
			return false;
		}

		inline bool get_if(const std::function<bool(const type&)>& function, type& dataref)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (m_queues.size())
			{
				if (function(m_queues.front()))
				{
					dataref = std::move(m_queues.front());
					m_queues.pop();
					return true;
				}
			}
			return false;
		}

		inline bool pop()
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (m_queues.size())
			{
				m_queues.pop();
				return true;
			}
			return false;
		}

		inline bool pop_if(const std::function<bool(const type&)>& function)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (m_queues.size())
			{
				if (function(m_queues.front()))
				{
					m_queues.pop();
					return true;
				}
			}
			return false;
		}
	};
}
#endif
