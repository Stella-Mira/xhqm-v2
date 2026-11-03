#ifndef XHQM_SIGNAL
#define XHQM_SIGNAL

// 本文件位于 -I 搜索路径（software/）内且名为 signal.h，会把系统 <signal.h>
// 遮蔽（含 <csignal> 内部的转发）。凡本工程 TU 引用 <signal.h>/<csignal>，
// 都只会看到本文件。因此在非 Windows 编译器上先通过 include_next 引入真正的
// 系统 <signal.h>（SIGTERM/SIGKILL/SIGPIPE/signal()/kill() 等），再定义信号槽。
// MSVC 无 include_next，且其 <signal.h> 不会被本文件遮蔽（不靠 -I 同名），无需处理。
#if !defined(_WIN32) && !defined(_MSC_VER) && (defined(__GNUC__) || defined(__clang__))
#include_next <signal.h>
#include "pattern/instance.h"
#endif
namespace xhqm { template <typename T> class instance; }   // 前置声明：pool_life 的 friend 依据

// 信号槽（对标 Qt，依赖 threads_pool 派发异线程槽）。
// 连接类型对齐 Qt：signal_direct=Direct / signal_queued=Queued / signal_auto=Auto / signal_waited=BlockingQueued。
// 异线程槽经 xhqm::parallel::threads 执行；receiver 继承 signal_receiver 可在销毁时自动断开。
#include "../head.h"
#include "event.h"
#include "system/threads/threads_pool.h"

#include <mutex>
#include <tuple>
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <type_traits>
#include <functional>
#include <algorithm>

namespace xhqm
{
	namespace software
	{
		// 接收者基类：继承它即可在对象销毁时自动断开其连接的所有槽
		class signal_receiver
		{
			std::shared_ptr<std::atomic_bool> m_alive = std::make_shared<std::atomic_bool>(true);
		public:
			virtual ~signal_receiver() { m_alive->store(false); }
			std::shared_ptr<std::atomic_bool> alive_flag() const { return m_alive; }
		};
	}

	namespace signals
	{
		xhqm_io xhqm::parallel::threads& pool();
		xhqm_io void pool_life();   // main 首行调用：用单例模板安装进程级信号线程池（见 signal.cpp）
	}

	namespace software
	{
		template<class... values_type>
		class signal
		{
			struct slot
			{
				std::function<void(values_type...)> invoke;	// 已绑定参数的槽
				std::thread::id thread;							// 注册时所属线程
				xhqm::signal_type type;							// 连接类型
				std::shared_ptr<std::atomic_bool> alive;		// 自动断开标志（空 = 永不自动断开）
			};

			mutable std::mutex m_mutex;
			std::vector<slot> m_slots;
			xhqm::parallel::threads* m_pool = nullptr;

			xhqm::parallel::threads& pool() const
			{
				return m_pool ? *m_pool : xhqm::signals::pool();
			}

			void add(std::function<void(values_type...)> fn, xhqm::signal_type type,
				std::shared_ptr<std::atomic_bool> alive)
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_slots.push_back(slot{ std::move(fn), std::this_thread::get_id(), type, std::move(alive) });
			}

		public:
			signal() = default;
			explicit signal(xhqm::parallel::threads* pool) : m_pool(pool) {}
			signal(signal&&) = delete;
			signal(const signal&) = delete;

			// ---- connect（对标 Qt） ----
			// lambda / std::function
			void connect(std::function<void(values_type...)> fn,
				xhqm::signal_type type = xhqm::signal_type::signal_auto)
			{
				add(std::move(fn), type, nullptr);
			}
			// 普通函数
			template<class return_type>
			void connect(return_type(*fn)(values_type...),
				xhqm::signal_type type = xhqm::signal_type::signal_auto)
			{
				add(std::function<void(values_type...)>(fn), type, nullptr);
			}
			// 成员函数（object 继承 signal_receiver 时自动断开）
			template<class object_type, class return_type>
			void connect(object_type* object, return_type(object_type::* fn)(values_type...),
				xhqm::signal_type type = xhqm::signal_type::signal_auto)
			{
				std::shared_ptr<std::atomic_bool> alive;
				if constexpr (std::is_base_of_v<signal_receiver, object_type>)
					alive = object->alive_flag();
				add([object, fn](values_type... v) { (object->*fn)(v...); }, type, std::move(alive));
			}
			template<class object_type, class return_type>
			void connect(object_type* object, return_type(object_type::* fn)(values_type...) const,
				xhqm::signal_type type = xhqm::signal_type::signal_auto)
			{
				std::shared_ptr<std::atomic_bool> alive;
				if constexpr (std::is_base_of_v<signal_receiver, object_type>)
					alive = object->alive_flag();
				add([object, fn](values_type... v) { (object->*fn)(v...); }, type, std::move(alive));
			}

			// ---- disconnect ----
			void disconnect_all()
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_slots.clear();
			}
			void disconnect(signal_receiver* object)
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				auto flag = object->alive_flag();
				m_slots.erase(std::remove_if(m_slots.begin(), m_slots.end(),
					[&flag](const slot& s) { return s.alive == flag; }),
					m_slots.end());
			}

			// ---- emit ----
			void emit(values_type... values) const
			{
				std::vector<slot> snapshot;
				{
					std::lock_guard<std::mutex> lock(m_mutex);
					snapshot = m_slots;
				}
				for (const slot& s : snapshot)
				{
					if (s.alive && !s.alive->load()) continue;	// 接收者已销毁

					xhqm::signal_type t = s.type;
					if (t == xhqm::signal_type::signal_auto)
						t = (s.thread == std::this_thread::get_id())
							? xhqm::signal_type::signal_direct
							: xhqm::signal_type::signal_queued;

					if (t == xhqm::signal_type::signal_direct)
					{
						s.invoke(values...);
					}
					else if (t == xhqm::signal_type::signal_queued)
					{
						pool().submit(0, [invoke = s.invoke, args = std::make_tuple(values...)]() mutable
						{
							std::apply(invoke, std::move(args));
						});
					}
					else if (t == xhqm::signal_type::signal_waited)
					{
						if (s.thread == std::this_thread::get_id())
						{
							s.invoke(values...);	// 同线程直接（Qt BlockingQueued 同线程语义）
						}
						else
						{
							auto holder = pool().submit(0, [invoke = s.invoke, args = std::make_tuple(values...)]() mutable
							{
								std::apply(invoke, std::move(args));
							});
							holder.future_ptr()->wait();	// 阻塞等待，非忙等
						}
					}
				}
			}

			void operator()(values_type... values) const { emit(values...); }
		};
	}
}

#endif
