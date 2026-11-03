#ifndef XHQM_STATE_MACHINE
#define XHQM_STATE_MACHINE

// 状态机：状态 + 事件 → 状态转换表；转换可带动作回调
// 单线程：无锁；多线程：加锁，handle 拷贝动作快照后调用
#include <map>
#include <utility>
#include <mutex>
#include <functional>
#include "thread_mode.h"

namespace xhqm
{
	// 主模板：只声明，实现见单/多线程特化
	template<class state_type, class event_type, class thread = xhqm::single_thread>
	class state_machine;

	// 单线程状态机（默认）
	template<class state_type, class event_type>
	class state_machine<state_type, event_type, xhqm::single_thread>
	{
		std::map<std::pair<state_type, event_type>, std::pair<state_type, std::function<void()>>> m_transitions;
		state_type m_current{};
	public:
		void start(state_type initial) { m_current = initial; }
		state_type current() const { return m_current; }

		// 注册转换：from --event--> to，可选动作
		void add_transition(state_type from, event_type event, state_type to, std::function<void()> action = nullptr)
		{
			m_transitions[{ from, event }] = { to, std::move(action) };
		}

		// 处理事件；返回是否发生转换
		bool handle(event_type event)
		{
			auto it = m_transitions.find({ m_current, event });
			if (it == m_transitions.end()) return false;
			m_current = it->second.first;
			if (it->second.second) it->second.second();
			return true;
		}
	};

	// 多线程状态机
	template<class state_type, class event_type>
	class state_machine<state_type, event_type, xhqm::multi_thread>
	{
		mutable std::mutex m_mutex;
		std::map<std::pair<state_type, event_type>, std::pair<state_type, std::function<void()>>> m_transitions;
		state_type m_current{};
	public:
		state_machine() = default;
		state_machine(state_machine&&) = delete;
		state_machine(const state_machine&) = delete;

		void start(state_type initial)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_current = initial;
		}

		state_type current() const
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			return m_current;
		}

		void add_transition(state_type from, event_type event, state_type to, std::function<void()> action = nullptr)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_transitions[{ from, event }] = { to, std::move(action) };
		}

		bool handle(event_type event)
		{
			std::pair<state_type, std::function<void()>> transition;
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				auto it = m_transitions.find({ m_current, event });
				if (it == m_transitions.end()) return false;
				m_current = it->second.first;
				transition = it->second;
			}
			if (transition.second) transition.second();
			return true;
		}
	};
}

#endif
