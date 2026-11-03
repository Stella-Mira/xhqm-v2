#ifndef XHQM_NEBULA_LIFE
#define XHQM_NEBULA_LIFE

#include "nebula.h"
#include "data.h"

namespace xhqm
{
	namespace nebula
	{
		class xhqm_io life : public data
		{
		protected:
			// 0<=x<m 发邀约 //m<=x 停发邀约
			// 0<=x<n 收邀约 //n<=x 停收邀约
			xhqm::size m_m = 0;	//连接数量下限
			xhqm::size m_n = 0;	//连接数量上限
			mutable std::mutex m_life_mutex; //全局锁
			xhqm::parallel::threads m_life_thread;  //发事件执行池
		public:
			life(const life&) = delete;
			life& operator=(const life&) = delete;
			explicit life(const xhqm::text& path, xhqm::size m, xhqm::size n, xhqm::size c, xhqm::size a, xhqm::size limt, xhqm::size time)
				: nebula::data(path), m_life_thread(c, a, limt, std::chrono::milliseconds(time)) //析构时自动死亡
			{
				n = (n > m ? n : m);
				m_m = m;
				m_n = n;
			}

			inline void join() { m_life_thread.join(); }
			inline void dath() { m_life_thread.dath(); }
			inline void start() { m_life_thread.life(); }    // 启动池（恢复派发）
			inline void sleep() { m_life_thread.stop(); }    // 停派发（暂停但不收摊）
			virtual text exec_event(const json& event);
			virtual void life_event(const json& event);
			inline xhqm::size bound_m() const { return m_m; }
			inline xhqm::size bound_n() const { return m_n; }
		};
	}
}

#endif
