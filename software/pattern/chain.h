#ifndef XHQM_CHAIN
#define XHQM_CHAIN

// 责任链：请求沿处理器链传递，处理器返回 true 表示已处理（停止），false 继续
// 单线程：无锁；多线程：加锁，handle 拷贝快照后调用
#include <vector>
#include <mutex>
#include <functional>
#include "thread_mode.h"

namespace xhqm
{
	// 主模板：只声明，实现见单/多线程特化
	template<class request_type, class thread = xhqm::single_thread>
	class chain;

	// 单线程责任链（默认）
	template<class request_type>
	class chain<request_type, xhqm::single_thread>
	{
		std::vector<std::function<bool(request_type&)>> m_handlers;
	public:
		// 添加处理器：bool(request_type&)，返回 true 表示已处理（停止传递）
		void add(std::function<bool(request_type&)> handler)
		{
			m_handlers.push_back(std::move(handler));
		}

		// 传递请求；返回是否被某个处理器处理
		bool handle(request_type& request)
		{
			for (auto& handler : m_handlers)
			{
				if (handler(request)) return true;
			}
			return false;
		}

		bool empty() const { return m_handlers.empty(); }
	};

	// 多线程责任链
	template<class request_type>
	class chain<request_type, xhqm::multi_thread>
	{
		mutable std::mutex m_mutex;
		std::vector<std::function<bool(request_type&)>> m_handlers;
	public:
		void add(std::function<bool(request_type&)> handler)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_handlers.push_back(std::move(handler));
		}

		bool handle(request_type& request)
		{
			std::vector<std::function<bool(request_type&)>> snapshot;
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				snapshot = m_handlers;
			}
			for (auto& handler : snapshot)
			{
				if (handler(request)) return true;
			}
			return false;
		}

		bool empty() const
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			return m_handlers.empty();
		}
	};
}

#endif
