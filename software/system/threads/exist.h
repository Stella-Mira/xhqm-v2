#ifndef XHQM_EXIST
#define XHQM_EXIST
#include "../../../head.h"
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>

namespace xhqm
{
	class xhqm_io exist
	{
		std::mutex m_mutex;
		std::thread m_thread;
		std::atomic<bool> m_star = false;
	public:
		bool is_enter()
		{
			return exist::m_star;
		}
		std::mutex& mutex()
		{
			return exist::m_mutex;
		}

		virtual void enter()
		{
			if (!exist::m_star)
			{
				if (exist::m_thread.joinable())
					exist::m_thread.join();
				exist::m_star = true;
				exist::m_thread = std::thread(&exist::runexist, this);
			}
		}
		virtual void leave()
		{
			if (exist::m_star)
			{
				exist::m_star = false;
				if (std::this_thread::get_id() != exist::m_thread.get_id())
				{
					if (exist::m_thread.joinable())
						exist::m_thread.join();
				}
			}
		}

		virtual ~exist()
		{
			exist::m_star = false;
			if (exist::m_thread.joinable())
				exist::m_thread.join();
		}
	protected:
		virtual void runexist()
		{
			redoexcept();
			while (exist::m_star)
				runexcept();
			undoexcept();
		}//辅线程执行

		virtual void redoexcept() = 0;//辅线程首次执行一次
		virtual void runexcept() = 0;//辅线程循环执行多次
		virtual void undoexcept() = 0;//辅线程末尾执行一次
	};

	template<typename exist_type>
	class exist_this : public exist
	{
		std::function<void(exist_type*)> m_redoexcept;
		std::function<void(exist_type*)> m_runexcept;
		std::function<void(exist_type*)> m_undoexcept;
	public:
		void redoexcept(std::function<void(exist_type*)> fun) { m_redoexcept = fun; };
		void runexcept(std::function<void(exist_type*)> fun) { m_runexcept = fun; };
		void undoexcept(std::function<void(exist_type*)> fun) { m_undoexcept = fun; };
	protected:
		virtual void enter() override { xhqm::exist::enter(); };
		virtual void leave() override { xhqm::exist::leave(); };
		virtual void redoexcept() override { if (m_redoexcept) m_redoexcept(this); };
		virtual void runexcept() override { if (m_runexcept) m_runexcept(this); };
		virtual void undoexcept() override { if (m_undoexcept) m_undoexcept(this); };
	};
}
#endif
