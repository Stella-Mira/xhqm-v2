#include "signal.h"
#include "pattern/instance.h"

namespace xhqm
{
	namespace
	{
		// 只存指针：静态指针自身的析构无副作用（池子本体 new 出来、随进程回收，退出期不做任何等待）
		xhqm::parallel::threads* g_pool = nullptr;
	}

	namespace signals
	{
		xhqm::parallel::threads& pool()
		{
		// 只存指针：静态指针自身的析构无副作用（池子本体 new 出来、随进程回收）
			if (g_pool == nullptr)
				throw xhqm::error("signals::pool 未安装：main 首行请调用 xhqm::getInstance<signals::pool_life>();");
			return *g_pool;
		}

		class _pool_life_
		{
			template <typename T> friend class xhqm::instance;
			_pool_life_(xhqm::size core = 2, xhqm::size max = 4)
			{
				g_pool = new xhqm::parallel::threads(core, max);   // 进程级：随进程回收
			}
			~_pool_life_()
			{
				g_pool = nullptr;                                  // 只摘除安装
			}
		};

		void pool_life()
		{
			xhqm::getInstance<_pool_life_>();   // 单例模板构造 pool_life ⇒ 安装线程池
		}
	}
}
