#ifndef XHQM_THREAD_MODE
#define XHQM_THREAD_MODE

namespace xhqm
{
	// 线程策略标签：观察者/通知者的线程模型
	struct single_thread {};   // 单线程：无锁顺序执行（默认）
	struct multi_thread {};    // 多线程：加锁线程安全
}

#endif
