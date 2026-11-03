#ifndef XHQM_THREADS_POOL
#define XHQM_THREADS_POOL

#include "threads_task.h"
namespace xhqm
{
	namespace parallel
	{
		class dqueue
		{
		public:
			using ptr = std::shared_ptr<parallel::dqueue>;
			std::deque<parallel::target::ptr> queue;
			std::mutex mutex;
		};

		class xhqm_io worker
		{
		public:
			friend class threads;

			enum class state : unsigned char
			{
				close = 0,
				alive = 1,
				sleep = 2,
			};

			std::atomic_uchar m_status{ xhqm_uchar(worker::state::close) };
			std::chrono::milliseconds m_millis;
			std::atomic_bool m_shared{ false };
			std::thread m_thread;

			worker(std::chrono::milliseconds millis = std::chrono::milliseconds::max())
				: m_millis(millis)
			{
			}

			~worker()
			{
				if (m_thread.joinable())
				{
					m_thread.join();
				}
			}

			worker(const worker&) = delete;
			worker& operator=(const worker&) = delete;
			static void _runing_(worker*, parallel::threads*);
		private:
			bool m_core{ false };
		};

		class xhqm_io threads
		{
		public:
			enum class state : unsigned char
			{
				dath = static_cast<unsigned char>(worker::state::close),
				life = static_cast<unsigned char>(worker::state::alive),
				stop = static_cast<unsigned char>(worker::state::sleep),
				join,
			};
		private:
			std::vector<std::unique_ptr<parallel::worker>> m_workers;
			std::vector<parallel::dqueue::ptr> m_channels;	// 通道队列，下标即通道索引
			std::size_t m_cored_threads{ 0 };
			std::size_t m_allow_threads{ 0 };
			std::size_t m_allow_actions{ 0 };
			std::chrono::milliseconds m_keep_alive{ 0 };
			std::atomic_uchar m_status{ xhqm_uchar(threads::state::dath) };	// 池状态
			std::mutex m_wake_mutex;						// 池状态/计数/通道表保护锁
			std::condition_variable m_alive;				// 全局唤醒信号
			std::mutex m_mutex;								// 生命周期互斥
			std::atomic<std::size_t> m_scan_seed{ 0 };		// 轮转扫描起点种子
		private:
			std::size_t m_live_workers{ 0 };
			std::size_t m_idle_workers{ 0 };
			std::size_t m_running_tasks{ 0 };
			std::size_t m_queued_tasks{ 0 };				// 全部通道排队任务数（wake_mutex 保护）
			friend class worker;
		private:
			void _worker_loop(parallel::worker* worker_ptr) noexcept;
			void _spawn_worker_locked(bool core_worker);
			void _ensure_workers_locked() noexcept;
			void _ensure_channel_locked(std::size_t channel);
			void _reap_closed_workers();
			void _join_all_workers();
			static void _cancel_target(const parallel::target::ptr& target_ptr) noexcept;
			threads::state _state() const noexcept
			{
				return static_cast<threads::state>(m_status.load(std::memory_order_acquire));
			}

		protected:
			bool _submit_(std::size_t channel, const parallel::target::ptr& target_ptr);
			void _submit_(std::size_t channel, const std::vector<parallel::target::ptr>& targets);

		public:
			~threads() noexcept;
			threads(threads&&) = delete;
			threads(const threads&) = delete;
			threads& operator=(threads&&) = delete;
			threads& operator=(const threads&) = delete;
			threads(const std::size_t& _cored_threads = 1, const std::size_t& _allow_threads = 1,
				const std::size_t& _allow_actions = xhqm::npos, std::chrono::milliseconds _keep_alive_time = std::chrono::milliseconds(1000));

			void life();
			void stop();
			void join();
			void dath();

			threads::state threads_state() const noexcept
			{
				return static_cast<threads::state>(m_status.load(std::memory_order_acquire));
			}

			template<typename _func_type, typename... _argvs_type>
			parallel::holder<void> submit(std::size_t _channel, _func_type&& _func, _argvs_type&&... _argv)
			{
				parallel::holder<void> value(
					std::forward<_func_type>(_func), std::forward<_argvs_type>(_argv)...);
				_submit_(_channel, value.m_target);
				return value;
			}

			template<typename _return_type, typename _func_type, typename... _argvs_type>
			parallel::holder<_return_type> submit(std::size_t _channel, _func_type&& _func, _argvs_type&&... _argv)
			{
				parallel::holder<_return_type> value(
					std::forward<_func_type>(_func), std::forward<_argvs_type>(_argv)...);
				_submit_(_channel, value.m_target);
				return value;
			}

			template <typename _return_type>
			void submit_list(std::size_t _channel, const std::vector<xhqm::parallel::holder<_return_type>>& holders)
			{
				std::vector<parallel::target::ptr> targets;
				targets.reserve(holders.size());
				for (const auto& value : holders)
				{
					targets.push_back(value.m_target);
				}
				_submit_(_channel, targets);
			}

			template <typename _return_type>
			void submit_task(
				std::size_t _channel,
				const parallel::holder<_return_type>& value,
				std::chrono::milliseconds timeout = std::chrono::milliseconds(0))
			{
				if (!_submit_(_channel, value.m_target))
				{
					return;
				}

				if (timeout == std::chrono::milliseconds(0))
				{
					return;
				}
				if (timeout == std::chrono::milliseconds::max())
				{
					value.future_ptr()->wait();
					return;
				}
				if (value.future_ptr()->wait_for(timeout) == std::future_status::timeout)
				{
					value.set_cancel();
				}
			}
		};
	}
}

#endif
