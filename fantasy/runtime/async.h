#pragma once
#ifndef XHQM_FANTASY_ASYNC
#define XHQM_FANTASY_ASYNC

// 异步资源加载：后台线程跑 CPU 加载（文件 IO/解码/解析），主线程 poll() 执行 GPU 上传回调
#include "fantasy.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <functional>

namespace xhqm
{
	namespace fantasy
	{
		class async_loader
		{
			struct job
			{
				std::function<void()> task;        // 后台线程执行
				std::function<void()> completion;  // 主线程 poll 时执行
			};
			std::thread m_thread;
			mutable std::mutex m_mutex;
			std::condition_variable m_cv;
			std::deque<job> m_queue;
			std::deque<std::function<void()>> m_completed;
			bool m_running = false;
			bool m_stop = false;

			void worker();
		public:
			async_loader() = default;
			~async_loader();

			void start();   // 启动后台线程
			void stop();    // 停止并 join（等待队列清空）

			// 提交任务：task 后台执行（CPU 加载），completion 在主线程 poll 时执行（GPU 上传）
			void submit(std::function<void()> task, std::function<void()> completion);

			// 主线程每帧调用：执行所有已完成的 completion 回调
			void poll();

			size_t pending() const;   // 尚未执行的 CPU 任务数
			size_t completed() const; // 待主线程处理的完成回调数
			bool running() const { return m_running; }
		};
	}
}

#endif
