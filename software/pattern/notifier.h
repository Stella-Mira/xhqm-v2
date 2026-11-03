#ifndef XHQM_NOTIFIER
#define XHQM_NOTIFIER

#include <list>
#include <mutex>
#include <vector>
#include "thread_mode.h"
#include "../../metaclass/function/function_type.h"

namespace xhqm 
{
	// 主模板：只声明，实现见单/多线程特化
	template<class func_type, class thread = xhqm::single_thread>
	class notifier;

	// 单线程通知者（默认）：无锁，顺序执行
	template<class func_type>
	class notifier<func_type, xhqm::single_thread>
	{
		using function_ptr = typename xhqm::function<func_type>::function_ptr;
		using receiver = typename xhqm::function<func_type>::function_std;
	protected:
		std::list<std::pair<receiver, function_ptr>> m_receivers;
	public:
		notifier() = default;
		notifier(notifier&&) = default;
		notifier(const notifier&) = default;

		inline void regist(function_ptr function)
		{
			m_receivers.emplace_back(receiver(function), function);
		}

		inline void remove(function_ptr function)
		{
			m_receivers.remove_if([function](const auto& select)
				{
					return (select.second == function);
				});
		}

		template<class ...values_type>
		inline void notify(values_type&& ...values) const
		{
			for (auto& hander : m_receivers)
			{
				hander.first(std::forward<values_type>(values)...);
			}
		}
	};

	// 多线程通知者：加锁，notify 拷贝快照后调用（避免重入死锁与长持锁）
	template<class func_type>
	class notifier<func_type, xhqm::multi_thread>
	{
		using function_ptr = typename xhqm::function<func_type>::function_ptr;
		using receiver = typename xhqm::function<func_type>::function_std;
	protected:
		mutable std::mutex m_mutex;
		std::list<std::pair<receiver, function_ptr>> m_receivers;
	public:
		notifier() = default;
		notifier(notifier&&) = delete;
		notifier(const notifier&) = delete;

		inline void regist(function_ptr function)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_receivers.emplace_back(receiver(function), function);
		}

		inline void remove(function_ptr function)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_receivers.remove_if([function](const auto& select)
				{
					return (select.second == function);
				});
		}

		template<class ...values_type>
		inline void notify(values_type&& ...values) const
		{
			std::vector<std::pair<receiver, function_ptr>> snapshot;
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				snapshot.assign(m_receivers.begin(), m_receivers.end());
			}
			for (auto& hander : snapshot)
			{
				hander.first(std::forward<values_type>(values)...);
			}
		}
	};

}

#endif
