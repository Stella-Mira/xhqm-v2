#pragma once
#ifndef XHQM_FANTASY_COMMAND_RECORDER
#define XHQM_FANTASY_COMMAND_RECORDER

// 多线程命令缓冲录制：固定线程池，按 pass 并行执行录制任务，主线程阻塞等待全部完成
#include "fantasy.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <functional>
#include <atomic>

namespace xhqm
{
	namespace fantasy
	{
		class command_recorder
		{
			std::vector<std::thread> m_workers;
			std::mutex m_mutex;
			std::condition_variable m_cv;
			std::condition_variable m_done_cv;
			std::deque<std::function<void()>> m_tasks;
			size_t m_remaining = 0;
			std::atomic<bool> m_running{false};
			std::atomic<bool> m_stop{false};

			void worker();
		public:
			command_recorder() = default;
			~command_recorder();

			// 启动线程池；n==0 取硬件并发数
			void start(uint n = 0);
			void stop();

			// 并行执行 tasks（每 pass 一个录制任务），阻塞直到全部完成
			void record(std::vector<std::function<void()>> tasks);

			bool running() const { return m_running; }
			size_t thread_count() const { return m_workers.size(); }
		};
	}
}

#endif
