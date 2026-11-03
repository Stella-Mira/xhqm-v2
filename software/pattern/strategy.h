#ifndef XHQM_STRATEGY
#define XHQM_STRATEGY

// 策略：封装可替换算法。func_type = 算法签名 return_type(args...)
// 单线程：无锁 std::function；多线程：加锁，execute 拷贝快照后调用
#include <mutex>
#include <functional>
#include "thread_mode.h"

namespace xhqm
{
	// 主模板：只声明，实现见单/多线程特化
	template<class func_type, class thread = xhqm::single_thread>
	class strategy;

	// 单线程策略（默认）
	template<class func_type>
	class strategy<func_type, xhqm::single_thread>
	{
		std::function<func_type> m_strategy;
	public:
		strategy() = default;
		strategy(std::function<func_type> fn) : m_strategy(std::move(fn)) {}

		void set(std::function<func_type> fn) { m_strategy = std::move(fn); }
		void clear() { m_strategy = nullptr; }
		explicit operator bool() const { return (bool)m_strategy; }

		template<class... args_type>
		auto execute(args_type&&... args) const
		{
			return m_strategy(std::forward<args_type>(args)...);
		}
	};

	// 多线程策略
	template<class func_type>
	class strategy<func_type, xhqm::multi_thread>
	{
		mutable std::mutex m_mutex;
		std::function<func_type> m_strategy;
	public:
		strategy() = default;
		strategy(std::function<func_type> fn) : m_strategy(std::move(fn)) {}

		void set(std::function<func_type> fn)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_strategy = std::move(fn);
		}

		void clear()
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_strategy = nullptr;
		}

		explicit operator bool() const
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			return (bool)m_strategy;
		}

		template<class... args_type>
		auto execute(args_type&&... args) const
		{
			std::function<func_type> snapshot;
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				snapshot = m_strategy;
			}
			return snapshot(std::forward<args_type>(args)...);
		}
	};
}

#endif
