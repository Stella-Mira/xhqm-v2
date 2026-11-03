#include "async.h"

namespace xhqm
{
	namespace fantasy
	{
		void async_loader::worker()
		{
			while (true)
			{
				job j;
				{
					std::unique_lock<std::mutex> lock(m_mutex);
					m_cv.wait(lock, [this] { return m_stop || !m_queue.empty(); });
					if (m_stop && m_queue.empty()) return;
					j = std::move(m_queue.front());
					m_queue.pop_front();
				}
				// 后台执行 CPU 加载
				if (j.task) j.task();
				// 完成回调交回主线程
				{
					std::lock_guard<std::mutex> lock(m_mutex);
					m_completed.push_back(std::move(j.completion));
				}
			}
		}

		async_loader::~async_loader()
		{
			stop();
		}

		void async_loader::start()
		{
			if (m_running) return;
			m_stop = false;
			m_running = true;
			m_thread = std::thread(&async_loader::worker, this);
		}

		void async_loader::stop()
		{
			if (!m_running) return;
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_stop = true;
				m_running = false;   // 拒绝新提交
			}
			m_cv.notify_all();
			if (m_thread.joinable()) m_thread.join();
			// 排空未处理的完成回调
			m_completed.clear();
		}

		void async_loader::submit(std::function<void()> task, std::function<void()> completion)
		{
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				if (!m_running) return;
				m_queue.push_back(job{ std::move(task), std::move(completion) });
			}
			m_cv.notify_one();
		}

		void async_loader::poll()
		{
			std::deque<std::function<void()>> done;
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				done.swap(m_completed);
			}
			for (auto& f : done) { if (f) f(); }
		}

		size_t async_loader::pending() const
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			return m_queue.size();
		}

		size_t async_loader::completed() const
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			return m_completed.size();
		}
	}
}
