
#pragma once
#include "dispatch_no.h"
#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>


namespace xhqm
{
	namespace parallel
	{
		//template<typename type>
		//struct array<type, xhqm::parallel::equipment::cpu> : public _array, public std::vector<type>
		//{
		//	array(const xhqm::size& size) : std::vector<type>(size) {};
		//	array(const xhqm::size& size, const type& value) : std::vector<type>(size, value) {};
		//	array(const std::vector<type>& list) : std::vector<type>(list) {};
		//	array(std::vector<type>&& list) : std::vector<type>(list) {};

		//	inline bool over() const
		//	{
		//		return (m_index == std::vector<type>::size());
		//	}
		//};

		//template<typename type, xhqm::parallel::equipment equip = xhqm::parallel::equipment::cpu>
		//struct task : public _task
		//{
		//	virtual void call(xhqm::parallel::array<type, equip>& values) = 0;
		//};

		//template<typename type, xhqm::parallel::equipment equip = xhqm::parallel::equipment::cpu>
		//struct execute : public _execute
		//{
		//	template<typename task_type>
		//	void exec(task_type taskobj, xhqm::parallel::array<type, equip>& values)
		//	{
		//		while (!values.over())
		//		{
		//			taskobj.m_id = this->m_id;
		//			taskobj.m_index = values.m_index;
		//			values.m_index += 1;
		//			taskobj.call(values);
		//		}
		//	}
		//};

		//template<typename type, xhqm::parallel::equipment equip = xhqm::parallel::equipment::cpu>
		//class dispatch : public _dispatch
		//{
		//	xhqm::parallel::execute<type, equip> m_execute;
		//public:
		//	dispatch(dispatch&&) = delete;
		//	dispatch(const dispatch&) = delete;
		//	constexpr dispatch() { m_wait = true; }
		//	virtual ~dispatch() { if (m_thread.joinable()) m_thread.join(); }
		//	template<typename task_type>
		//	static void static_execute(xhqm::parallel::execute<type, equip>& execute, const task_type& taskobj, xhqm::parallel::array<type, equip>& values)
		//	{
		//		execute.exec(taskobj, values);
		//	}
		//public:
		//	template<typename task_type>
		//	inline void compute_task(const task_type& taskobj, xhqm::parallel::array<type, equip>& values, bool wait = true)
		//	{
		//		m_wait = wait;
		//		if (m_thread.joinable()) m_thread.join();
		//		if (m_wait) dispatch::static_execute<task_type>(m_execute, taskobj, values);
		//		else m_thread = std::thread(&dispatch::static_execute<task_type>, std::ref(m_execute), taskobj, std::ref(values));
		//	}

		//	inline void compute(std::function<void(type&)> function, xhqm::parallel::array<type, equip>& values, bool wait = true)
		//	{
		//		task_function<type> taskobj;
		//		taskobj.m_function = function;
		//		compute_task(taskobj, values, wait);
		//	}

		//	inline void wite()
		//	{
		//		if (m_thread.joinable()) m_thread.join();
		//	}
		//};
	}
}