#ifndef XHQM_OBSERVER
#define XHQM_OBSERVER
#include "../../head.h"
#include <functional>
#include <mutex>
#include <vector>
#include <cstring>
#include "thread_mode.h"

namespace xhqm 
{
	template<class subject_type, class thread = xhqm::single_thread> class subject;  //目标
	template<class subject_type, class thread = xhqm::single_thread> class observer; //观察者

	// ---- 单线程（默认）：无锁，顺序执行 ----

	template<class subject_type>
	class subject<subject_type, xhqm::single_thread> //目标
	{
	public:
		using observer = xhqm::observer<subject_type, xhqm::single_thread>;
		using changed_type = void*;
	protected:
		friend class xhqm::observer<subject_type, xhqm::single_thread>;
		std::list<observer*> m_observers;
	public:
		template<class func_type>
		void change(func_type function)//目标改变触发观察者反应
		{
			//signal 以函数指针地址作为变化标识（函数指针→void* 按位复制，规避 reinterpret_cast 的条件支持问题）
			changed_type signal = nullptr;
			std::memcpy(&signal, &function, sizeof(changed_type));
			for (auto& server : m_observers)
			{
				server->response(static_cast<subject_type*>(this), signal);
			}
		}
	};

	template<class subject_type>
	class observer<subject_type, xhqm::single_thread> //观察者
	{
	public:
		using subject = subject_type;
		using changed_type = void*;
	public:
		observer() = default;
		observer(observer&&) = delete;   //观察者注册后持有 this 指针，禁止移动
		observer(const observer&) = delete;

		inline void regist(subject* object)
		{
			if (object) object->m_observers.push_back(this);
		}

		inline void remove(subject* object)
		{
			if (object)
			{
				object->m_observers.remove_if([this](const auto& select) { return this == select; });
			}
		}

		virtual void response(subject* object, changed_type signal) = 0; //反应
	};

	// ---- 多线程：加锁，change 拷贝快照后调用（避免重入死锁与长持锁） ----

	template<class subject_type>
	class subject<subject_type, xhqm::multi_thread> //目标
	{
	public:
		using observer = xhqm::observer<subject_type, xhqm::multi_thread>;
		using changed_type = void*;
	protected:
		friend class xhqm::observer<subject_type, xhqm::multi_thread>;
		std::mutex m_mutex;
		std::list<observer*> m_observers;
	public:
		subject() = default;
		subject(subject&&) = delete;
		subject(const subject&) = delete;

		template<class func_type>
		void change(func_type function)//目标改变触发观察者反应
		{
			changed_type signal = nullptr;
			std::memcpy(&signal, &function, sizeof(changed_type));
			std::vector<observer*> snapshot;
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				snapshot.assign(m_observers.begin(), m_observers.end());
			}
			for (auto& server : snapshot)
			{
				server->response(static_cast<subject_type*>(this), signal);
			}
		}
	};

	template<class subject_type>
	class observer<subject_type, xhqm::multi_thread> //观察者
	{
	public:
		using subject = subject_type;
		using changed_type = void*;
	public:
		observer() = default;
		observer(observer&&) = delete;
		observer(const observer&) = delete;

		inline void regist(subject* object)
		{
			if (object)
			{
				std::lock_guard<std::mutex> lock(object->m_mutex);
				object->m_observers.push_back(this);
			}
		}

		inline void remove(subject* object)
		{
			if (object)
			{
				std::lock_guard<std::mutex> lock(object->m_mutex);
				object->m_observers.remove_if([this](const auto& select) { return this == select; });
			}
		}

		virtual void response(subject* object, changed_type signal) = 0; //反应
	};
}
#endif
