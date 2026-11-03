#include "threads_pool.h"

namespace xhqm
{
	namespace parallel
	{
		namespace
		{
			thread_local threads* current_pool = nullptr;//当前线程所处的线程池
		}

		void worker::_runing_(worker* worker_ptr, parallel::threads* threads_ptr)
		{
			threads_ptr->_worker_loop(worker_ptr);
		}

		threads::threads(
			const std::size_t& _cored_threads, const std::size_t& _allow_threads,
			const std::size_t& _allow_actions, std::chrono::milliseconds _keep_alive_time)
			: m_cored_threads(_cored_threads), m_allow_threads(_allow_threads), m_allow_actions(_allow_actions),
			m_keep_alive(_keep_alive_time)
		{
			if (m_cored_threads == 0)
			{
				throw xhqm::error("cored_threads must be greater than 0");
			}
			if (m_allow_threads < m_cored_threads)
			{
				throw xhqm::error("allow_threads must be greater than or equal to cored_threads");
			}
			if (_keep_alive_time < std::chrono::milliseconds::zero())
			{
				throw xhqm::error("keep_alive_time must not be negative");
			}

			m_status.store(xhqm_uchar(threads::state::life), std::memory_order_release);
			m_workers.reserve(m_allow_threads);

			std::unique_lock<std::mutex> lock(m_wake_mutex);
			try
			{
				for (std::size_t index = 0; index < m_cored_threads; ++index)
				{
					_spawn_worker_locked(true);
				}
			}
			catch (...)
			{
				m_status.store(xhqm_uchar(threads::state::dath), std::memory_order_release);
				m_alive.notify_all();
				lock.unlock();
				_join_all_workers();
				throw;
			}
		}

		threads::~threads() noexcept
		{
			try
			{
				dath();
			}
			catch (...)
			{
				// 析构函数不得抛出。
			}
		}

		void threads::_ensure_channel_locked(std::size_t channel)
		{
			while (m_channels.size() <= channel)
			{
				m_channels.push_back(std::make_shared<parallel::dqueue>());
			}
		}

		void threads::_spawn_worker_locked(bool core_worker)
		{
			if (m_live_workers >= m_allow_threads)
			{
				return;
			}

			const auto timeout = core_worker ? std::chrono::milliseconds::max() : m_keep_alive;
			auto value = std::make_unique<parallel::worker>(timeout);
			value->m_status.store(xhqm_uchar(worker::state::sleep), std::memory_order_release);
			value->m_core = core_worker;

			try
			{
				++m_live_workers;
				auto* raw = value.get();
				value->m_thread = std::thread(&worker::_runing_, raw, this);
			}
			catch (...)
			{
				value->m_status.store(xhqm_uchar(worker::state::close), std::memory_order_release);
				--m_live_workers;
				throw;
			}

			m_workers.push_back(std::move(value));
		}

		void threads::_ensure_workers_locked() noexcept
		{
			const auto pool_state = _state();
			if (pool_state != threads::state::life && pool_state != threads::state::join)
			{
				return;
			}

			const std::size_t demand = m_running_tasks + m_queued_tasks;
			const std::size_t desired = std::min(
				m_allow_threads, std::max(m_cored_threads, demand));
			if (m_live_workers >= desired)
			{
				return;
			}

			const std::size_t count = desired - m_live_workers;
			for (std::size_t index = 0; index < count; ++index)
			{
				try
				{
					_spawn_worker_locked(false);
				}
				catch (...)
				{
					break;
				}
			}
		}

		void threads::_worker_loop(parallel::worker* worker_ptr) noexcept
		{
			current_pool = this;
			std::unique_lock<std::mutex> lock(m_wake_mutex);

			for (;;)
			{
				worker_ptr->m_status.store(xhqm_uchar(worker::state::sleep), std::memory_order_release);
				++m_idle_workers;

				auto ready = [this]() -> bool
				{
					const auto value = _state();
					return value == threads::state::dath
						|| value == threads::state::join
						|| (value == threads::state::life && m_queued_tasks != 0);
				};

				bool timed_out = false;
				if (worker_ptr->m_core || worker_ptr->m_millis == std::chrono::milliseconds::max())
				{
					m_alive.wait(lock, ready);
				}
				else
				{
					timed_out = !m_alive.wait_for(lock, worker_ptr->m_millis, ready);
				}

				--m_idle_workers;
				const auto pool_state = _state();

				if (pool_state == threads::state::dath)
				{
					break;
				}

				if (timed_out)
				{
					break;// 非核心线程空闲到期后退出；暂停状态下也允许回收。
				}

				if (pool_state == threads::state::stop)
				{
					continue;
				}

				parallel::target::ptr target_ptr;
				const std::size_t channel_count = m_channels.size();
				if (channel_count == 0)
				{
					if (pool_state == threads::state::join)
					{
						break;
					}
					continue;
				}

				// 轮转扫描：各 worker 从不同通道起步，公平分摊取任务
				const std::size_t start = m_scan_seed.fetch_add(1, std::memory_order_relaxed) % channel_count;
				for (std::size_t step = 0; step < channel_count; ++step)
				{
					const std::size_t index = (start + step) % channel_count;
					std::lock_guard<std::mutex> channel_lock(m_channels[index]->mutex);
					if (!m_channels[index]->queue.empty())
					{
						target_ptr = std::move(m_channels[index]->queue.front());
						m_channels[index]->queue.pop_front();
						--m_queued_tasks;
						break;
					}
				}

				if (!target_ptr)
				{
					if (pool_state == threads::state::join)
					{
						break;
					}
					continue;
				}

				++m_running_tasks;
				worker_ptr->m_status.store(xhqm_uchar(worker::state::alive), std::memory_order_release);
				_ensure_workers_locked();

				lock.unlock();
				try
				{
					if (target_ptr && target_ptr->action)
					{
						target_ptr->action();
					}
				}
				catch (...)
				{
					// holder 生成的 action 会自行捕获异常；防御自定义 target 泄漏异常。
				}
				lock.lock();

				--m_running_tasks;
				m_alive.notify_all();
			}

			worker_ptr->m_status.store(xhqm_uchar(worker::state::close), std::memory_order_release);
			if (m_live_workers > 0)
			{
				--m_live_workers;
			}
			m_alive.notify_all();
			lock.unlock();
			current_pool = nullptr;
		}

		void threads::_reap_closed_workers()
		{
			std::vector<std::unique_ptr<parallel::worker>> closed;
			{
				std::lock_guard<std::mutex> lock(m_wake_mutex);
				auto iterator = m_workers.begin();
				while (iterator != m_workers.end())
				{
					if ((*iterator)->m_status.load(std::memory_order_acquire)
						== xhqm_uchar(worker::state::close))
					{
						closed.push_back(std::move(*iterator));
						iterator = m_workers.erase(iterator);
					}
					else
					{
						++iterator;
					}
				}
			}
			// 离开池锁后析构并 join，避免与退出路径互锁。
			closed.clear();
		}

		void threads::_join_all_workers()
		{
			std::vector<std::unique_ptr<parallel::worker>> workers;
			{
				std::lock_guard<std::mutex> lock(m_wake_mutex);
				workers.swap(m_workers);
			}
			workers.clear();
		}

		void threads::_cancel_target(const parallel::target::ptr& target_ptr) noexcept
		{
			try
			{
				if (target_ptr && target_ptr->cancel)
				{
					target_ptr->cancel();
				}
			}
			catch (...)
			{
			}
		}

		void threads::life()
		{
			std::lock_guard<std::mutex> lifecycle_lock(m_mutex);
			_reap_closed_workers();

			std::lock_guard<std::mutex> lock(m_wake_mutex);
			if (_state() != threads::state::stop)
			{
				return;
			}

			m_status.store(xhqm_uchar(threads::state::life), std::memory_order_release);
			while (m_live_workers < m_cored_threads)
			{
				_spawn_worker_locked(true);
			}
			_ensure_workers_locked();
			m_alive.notify_all();
		}

		void threads::stop()
		{
			std::lock_guard<std::mutex> lifecycle_lock(m_mutex);
			std::lock_guard<std::mutex> lock(m_wake_mutex);
			if (_state() == threads::state::life)
			{
				m_status.store(xhqm_uchar(threads::state::stop), std::memory_order_release);
				m_alive.notify_all();
			}
		}

		void threads::join()
		{
			if (current_pool == this)
			{
				throw xhqm::error("threads::join cannot be called from one of its own tasks");
			}

			std::lock_guard<std::mutex> lifecycle_lock(m_mutex);
			_reap_closed_workers();

			std::unique_lock<std::mutex> lock(m_wake_mutex);
			if (_state() == threads::state::dath)
			{
				lock.unlock();
				_join_all_workers();
				return;
			}

			m_status.store(xhqm_uchar(threads::state::join), std::memory_order_release);
			_ensure_workers_locked();
			m_alive.notify_all();
			m_alive.wait(lock, [this]() -> bool
			{
				return m_queued_tasks == 0
					&& m_running_tasks == 0
					&& m_live_workers == 0;
			});
			m_status.store(xhqm_uchar(threads::state::dath), std::memory_order_release);
			lock.unlock();
			_join_all_workers();
		}

		void threads::dath()
		{
			if (current_pool == this)
			{
				throw xhqm::error("threads::dath cannot be called from one of its own tasks");
			}

			std::lock_guard<std::mutex> lifecycle_lock(m_mutex);
			std::deque<parallel::target::ptr> canceled;

			{
				std::unique_lock<std::mutex> lock(m_wake_mutex);
				if (_state() != threads::state::dath)
				{
					m_status.store(xhqm_uchar(threads::state::dath), std::memory_order_release);
					for (const auto& channel : m_channels)
					{
						std::lock_guard<std::mutex> channel_lock(channel->mutex);
						for (const auto& target_ptr : channel->queue)
						{
							canceled.push_back(target_ptr);
						}
						channel->queue.clear();
					}
					m_queued_tasks = 0;
					m_alive.notify_all();
				}
			}

			for (const auto& target_ptr : canceled)
			{
				_cancel_target(target_ptr);
			}

			{
				std::unique_lock<std::mutex> lock(m_wake_mutex);
				m_alive.wait(lock, [this]() -> bool
				{
					return m_live_workers == 0;
				});
			}
			_join_all_workers();
		}

		bool threads::_submit_(std::size_t channel, const parallel::target::ptr& target_ptr)
		{
			if (!target_ptr)
			{
				return false;
			}

			_reap_closed_workers();
			bool accepted = false;
			{
				std::lock_guard<std::mutex> lock(m_wake_mutex);
				const bool running = _state() == threads::state::life;
				const bool has_capacity = m_allow_actions == xhqm::npos
					|| m_queued_tasks < m_allow_actions;
				if (running && has_capacity)
				{
					_ensure_channel_locked(channel);
					{
						std::lock_guard<std::mutex> channel_lock(m_channels[channel]->mutex);
						m_channels[channel]->queue.push_back(target_ptr);
					}
					++m_queued_tasks;
					accepted = true;
					_ensure_workers_locked();
					m_alive.notify_all();
				}
			}

			if (!accepted)
			{
				_cancel_target(target_ptr);
			}
			return accepted;
		}

		void threads::_submit_(std::size_t channel, const std::vector<parallel::target::ptr>& targets)
		{
			if (targets.empty())
			{
				return;
			}

			_reap_closed_workers();
			std::size_t accepted = 0;
			{
				std::lock_guard<std::mutex> lock(m_wake_mutex);
				if (_state() == threads::state::life)
				{
					const std::size_t capacity = m_allow_actions == xhqm::npos
						? targets.size()
						: (m_queued_tasks < m_allow_actions
							? std::min(m_allow_actions - m_queued_tasks, targets.size())
							: 0);
					accepted = std::min(capacity, targets.size());
					if (accepted > 0)
					{
						_ensure_channel_locked(channel);
						{
							std::lock_guard<std::mutex> channel_lock(m_channels[channel]->mutex);
							for (std::size_t index = 0; index < accepted; ++index)
							{
								if (targets[index])
								{
									m_channels[channel]->queue.push_back(targets[index]);
									++m_queued_tasks;
								}
							}
						}
						_ensure_workers_locked();
						m_alive.notify_all();
					}
				}
			}

			for (std::size_t index = accepted; index < targets.size(); ++index)
			{
				_cancel_target(targets[index]);
			}
		}
	}
}
