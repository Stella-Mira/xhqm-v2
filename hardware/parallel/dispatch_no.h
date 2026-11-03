#ifndef XHQM_PARALLEL
#define XHQM_PARALLEL
#include "../../head.h"

#include <atomic>
#include <queue>
#include <mutex>
#include <thread>
#include <optional>
#include <functional>
#include <shared_mutex>

namespace xhqm
{
	namespace parallel
	{

		enum equipment//并行设备
		{
			no = 0, cpu, gpu, net
		};//不并行，cpu并行，gpu并行，分布式并行

		enum state//并行状态
		{
			init = 0, exec, over
		};//初始化，运行，结束

		struct _dispatch//调度器
		{
			std::thread m_thread;//调度器线程
			std::atomic<bool> m_wait = true;//阻塞运行
		};

		struct _execute//执行器
		{
			xhqm::id m_id = 0;//当前执行器ID
		};

		struct _share//共享数据
		{

		};

		struct _array//独占数据
		{
			xhqm::index m_index = 0;//当前数据下标
		};

		struct _task//并行任务
		{
			xhqm::id m_id = 0;
			xhqm::index m_index = 0;
		};



		template<typename type, xhqm::parallel::equipment equip = xhqm::parallel::equipment::no>
		struct share : public _array, public std::vector<type>
		{
			share(const xhqm::size& size) : std::vector<type>(size) {};
			share(const xhqm::size& size, const type& value) : std::vector<type>(size, value) {};
			share(const std::vector<type>& list) : std::vector<type>(list) {};
			share(std::vector<type>&& list) : std::vector<type>(std::forward<std::vector<type>>(list)) {};

			share(const share&) = delete;
			share(share&&) = delete;

			inline bool over() const
			{
				return (m_index == std::vector<type>::size());
			}
		};

		template<typename type, xhqm::parallel::equipment equip = xhqm::parallel::equipment::no>
		struct array : public _array, public std::vector<type>
		{
			array(const xhqm::size& size) : std::vector<type>(size) {};
			array(const xhqm::size& size, const type& value) : std::vector<type>(size, value) {};
			array(const std::vector<type>& list) : std::vector<type>(list) {};
			array(std::vector<type>&& list) : std::vector<type>(std::forward<std::vector<type>>(list)) {};

			array(const xhqm::parallel::array<type, equip>&) = delete;
			array(xhqm::parallel::array<type, equip>&&) = delete;;

			inline bool over() const
			{
				return (m_index == std::vector<type>::size());
			}
		};

		template<typename type, xhqm::parallel::equipment equip = xhqm::parallel::equipment::no>
		struct task : public _task
		{
			virtual ~task() = default;	// 虚析构：task_function 派生自 task，防止经 task* 释放时跳过派生析构
			virtual void call(xhqm::parallel::array<type, equip>&) = 0;
		};

		template<typename type, xhqm::parallel::equipment equip = xhqm::parallel::equipment::no>
		struct execute : public _execute
		{
			template<typename task_type>
			void exec(task_type taskobj, xhqm::parallel::array<type, equip>& values)
			{
				while(!values.over())
				{
					taskobj.m_id = this->m_id;
					taskobj.m_index = values.m_index;
					values.m_index += 1;
					taskobj.call(values);
				}
			}
		};

		template<typename type>
		struct task_function : public xhqm::parallel::task<type, xhqm::parallel::equipment::no>
		{
			std::function<void(type&)> m_function;
			virtual void call(xhqm::parallel::array<type, xhqm::parallel::equipment::no>& values) override
			{
				if(m_function)
					m_function(values[xhqm::parallel::task<type, xhqm::parallel::equipment::no>::m_index]);
			}
		};

		template<typename type, xhqm::parallel::equipment equip = xhqm::parallel::equipment::no>
		class dispatch : public _dispatch
		{
			xhqm::parallel::execute<type, equip> m_execute;
		public:
			dispatch(dispatch&&) = delete;
			dispatch(const dispatch&) = delete;
			constexpr dispatch() { m_wait = true; }
			virtual ~dispatch() { if (m_thread.joinable()) m_thread.join(); }
			template<typename task_type>
			static void static_execute(xhqm::parallel::execute<type, equip>& execute, const task_type& taskobj, xhqm::parallel::array<type, equip>& values)
			{
				execute.exec(taskobj, values);
			}
		public:
			template<typename task_type>
			inline void compute_task(const task_type& taskobj, xhqm::parallel::array<type, equip>& values, bool wait = true)
			{
				m_wait = wait;
				if (m_thread.joinable()) m_thread.join();
				if (m_wait) dispatch::static_execute<task_type>(m_execute, taskobj, values);
				else m_thread = std::thread(&dispatch::static_execute<task_type>, std::ref(m_execute), taskobj, std::ref(values));
			}

			inline void compute(std::function<void(type&)> function, xhqm::parallel::array<type, equip>& values, bool wait = true)
			{
				task_function<type> taskobj;
				taskobj.m_function = function;
				compute_task(taskobj, values, wait);
			}

			inline void wite()
			{
				if (m_thread.joinable()) m_thread.join();
			}
		};
	}
}


#endif