#ifndef XHQM_THREADS_TASK
#define XHQM_THREADS_TASK

#include "../../../head.h"

#include <functional>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <type_traits>
#include <utility>
#include <cstdint>
#include <future>
#include <vector>
#include <memory>
#include <mutex>
#include <deque>
#include <tuple>

#ifndef XHQM_HEAD
#define xhqm_byte(_data, _ind) (((unsigned char*)(&_data))[_ind])
#define xhqm_uchar(value) static_cast<unsigned char>(value)
#define xhqm_schar(value) static_cast<signed char>(value)
#define xhqm_f64(value) static_cast<double>(value)
#define xhqm_f32(value) static_cast<float>(value)
#define xhqm_io
namespace xhqm
{
	using measure = double;
	using integer = std::intmax_t;
	using natural = std::uintmax_t;

	using id = std::size_t;
	using size = std::size_t;
	using index = std::size_t;
	using count = std::size_t;

	using coord = std::vector<xhqm::index>;
	using topol = std::vector<xhqm::index>;
	using shape = std::vector<xhqm::count>;

	using real = float;
	using sure = double;
	using writ = signed char;
	using sign = signed char;
	using byte = unsigned char;
	using text = std::string;
	using byte_1 = std::uint8_t;
	using byte_2 = std::uint16_t;
	using byte_4 = std::uint32_t;
	using byte_8 = std::uint64_t;

	static constexpr auto en = 2.71828182845904523536028747135266249775724709369996l;
	static constexpr auto pi = 3.14159265358979323846264338327950288419716939937510l;
	static constexpr auto phi = 0.618033988749894848204586834365638117720309179805763;
	static constexpr auto egamma = 0.577215664901532860606512090082402431042159335939924;

	static constexpr unsigned char num_0 = 0x0u;
	static constexpr unsigned char num_1 = 0x1u;
	static constexpr unsigned char num_2 = 0x2u;
	static constexpr unsigned char num_3 = 0x3u;
	static constexpr unsigned char num_4 = 0x4u;
	static constexpr unsigned char num_5 = 0x5u;
	static constexpr unsigned char num_6 = 0x6u;
	static constexpr unsigned char num_7 = 0x7u;
	static constexpr unsigned char num_8 = 0x8u;
	static constexpr unsigned char num_9 = 0x9u;
	static constexpr unsigned char num_10 = 0xAu;
	static constexpr unsigned char num_11 = 0xBu;

	static constexpr auto precif = 0.00001f;
	static constexpr auto precid = 0.00000000001;
	static constexpr auto precil = 0.0000000000000000001l;
	template <typename type = float> constexpr type preci = 0;
	template <> constexpr auto preci<float> = xhqm::precif;
	template <> constexpr auto preci<double> = xhqm::precid;
	template <> constexpr auto preci<long double> = xhqm::precil;
	static constexpr auto npos = static_cast<xhqm::count>(-1);

	class error : public std::runtime_error
	{
	public:
		explicit error(const std::string& msg) : std::runtime_error(msg) {}
	};
}
#else
#include "../../../algebra/value.h"
#endif // XHQM_HEAD

namespace xhqm
{
	namespace parallel
	{
		class threads;

		class target
		{
		public:
			using ptr = std::shared_ptr<parallel::target>;

			enum class state : unsigned char
			{
				create = 0,
				cancel,
				runing,
				failures,
				successes
			};

			// 保留这些公开成员以兼容旧代码。任务状态转换由 CAS 保证只发生一次。
			std::mutex mutex;
			std::function<void()> action;
			std::function<bool()> cancel;
			std::atomic_uchar status{ xhqm_uchar(target::state::create) };

			bool finish() const noexcept
			{
				const auto value = status.load(std::memory_order_acquire);
				return value == xhqm_uchar(target::state::cancel)
					|| value == xhqm_uchar(target::state::failures)
					|| value == xhqm_uchar(target::state::successes);
			}
		};

		template<typename _return_type>
		class holder
		{
			std::shared_ptr<std::promise<_return_type>> m_promise;
			std::shared_ptr<std::future<_return_type>> m_future;
			parallel::target::ptr m_target;
			friend class threads;

		public:
			holder() = delete;

			template<typename _func_type, typename... _argvs_type>
			explicit holder(_func_type&& _func, _argvs_type&&... _argv)
			{
				using function_type = std::decay_t<_func_type>;
				using arguments_type = decltype(std::make_tuple(std::forward<_argvs_type>(_argv)...));

				struct invocation
				{
					function_type function;
					arguments_type arguments;

					invocation(_func_type&& func, _argvs_type&&... args)
						: function(std::forward<_func_type>(func)),
						arguments(std::make_tuple(std::forward<_argvs_type>(args)...))
					{
					}
				};

				m_promise = std::make_shared<std::promise<_return_type>>();
				m_future = std::make_shared<std::future<_return_type>>(m_promise->get_future());
				m_target = std::make_shared<parallel::target>();

				auto promise = m_promise;
				auto target_weak = std::weak_ptr<parallel::target>(m_target);
				auto call = std::make_shared<invocation>(
					std::forward<_func_type>(_func), std::forward<_argvs_type>(_argv)...);

				m_target->cancel = [promise, target_weak]() -> bool
				{
					auto target_ptr = target_weak.lock();
					if (!target_ptr)
					{
						return false;
					}

					auto expected = xhqm_uchar(target::state::create);
					if (!target_ptr->status.compare_exchange_strong(
						expected,
						xhqm_uchar(target::state::cancel),
						std::memory_order_acq_rel,
						std::memory_order_acquire))
					{
						return false;
					}

					try
					{
						throw xhqm::error("Task is canceled");
					}
					catch (...)
					{
						try
						{
							promise->set_exception(std::current_exception());
						}
						catch (const std::future_error&)
						{
							// CAS 已保证正常情况下只会完成一次；这里仅防御外部篡改。
						}
					}
					return true;
				};

				m_target->action = [promise, target_weak, call]() mutable
				{
					auto target_ptr = target_weak.lock();
					if (!target_ptr)
					{
						return;
					}

					auto expected = xhqm_uchar(target::state::create);
					if (!target_ptr->status.compare_exchange_strong(
						expected,
						xhqm_uchar(target::state::runing),
						std::memory_order_acq_rel,
						std::memory_order_acquire))
					{
						return;
					}

					try
					{
						if constexpr (std::is_void_v<_return_type>)
						{
							std::apply(std::move(call->function), std::move(call->arguments));
							promise->set_value();
						}
						else
						{
							promise->set_value(
								std::apply(std::move(call->function), std::move(call->arguments)));
						}
						target_ptr->status.store(
							xhqm_uchar(target::state::successes), std::memory_order_release);
					}
					catch (...)
					{
						target_ptr->status.store(
							xhqm_uchar(target::state::failures), std::memory_order_release);
						try
						{
							promise->set_exception(std::current_exception());
						}
						catch (const std::future_error&)
						{
							// 防御外部代码重复完成 promise。
						}
					}
				};
			}

			holder(const holder&) noexcept = default;
			holder(holder&&) noexcept = default;
			holder& operator=(const holder&) noexcept = default;
			holder& operator=(holder&&) noexcept = default;

			bool set_cancel() const
			{
				return m_target && m_target->cancel ? m_target->cancel() : false;
			}

			auto future_ptr() const noexcept
			{
				return m_future;
			}
		};
	}
}

#endif