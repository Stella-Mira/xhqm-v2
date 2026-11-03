#include "command_recorder.h"

namespace xhqm
{
	namespace fantasy
	{
		void command_recorder::worker()
		{
			while (true)
			{
				std::function<void()> task;
				{
					std::unique_lock<std::mutex> lock(m_mutex);
					m_cv.wait(lock, [this] { return m_stop || !m_tasks.empty(); });
					if (m_stop && m_tasks.empty()) return;
					task = std::move(m_tasks.front());
					m_tasks.pop_front();
				}
				// 并行录制任务
				task();
				// 完成计数
				{
					std::lock_guard<std::mutex> lock(m_mutex);
					if (m_remaining > 0 && --m_remaining == 0)
					{
						m_done_cv.notify_all();
					}
				}
			}
		}

		command_recorder::~command_recorder()
		{
			stop();
		}

		void command_recorder::start(uint n)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (m_running) return;
			if (n == 0) n = std::thread::hardware_concurrency();
			if (n == 0) n = 1;
			m_stop = false;
			m_running = true;
			m_workers.reserve(n);
			for (uint i = 0; i < n; ++i) m_workers.emplace_back(&command_recorder::worker, this);
		}

		void command_recorder::stop()
		{
			std::unique_lock<std::mutex> lock(m_mutex);
			if (!m_running) return;
			m_stop = true;
			m_running = false;
			lock.unlock();
			m_cv.notify_all();
			for (auto& t : m_workers) { if (t.joinable()) t.join(); }
			m_workers.clear();
		}

		void command_recorder::record(std::vector<std::function<void()>> tasks)
		{
			std::unique_lock<std::mutex> lock(m_mutex);
			if (!m_running || tasks.empty()) return;
			// 累计而非整批重置：并发 record() 不再互相覆盖计数（#24）
			m_remaining += tasks.size();
			for (auto& t : tasks) m_tasks.push_back(std::move(t));
			lock.unlock();
			m_cv.notify_all();
			// 阻塞等待全部完成（含本批及之后并发追加的任务）
			std::unique_lock<std::mutex> lock2(m_mutex);
			m_done_cv.wait(lock2, [this] { return m_remaining == 0; });
		}
	}
}
