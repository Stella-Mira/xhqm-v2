// pattern 测试：notifier（通知者）/ observer（观察者）单线程与多线程特化
#include "../software/pattern/notifier.h"
#include "../software/pattern/observer.h"
#include "test.h"
#include <atomic>
#include <thread>
#include <vector>

using namespace xhqm;

// notifier 回调为函数指针，需全局状态
static int g_sum = 0;
static void add1(int v) { g_sum += v; }
static void add2(int v) { g_sum += v * 2; }
static std::atomic<int> g_atomic_sum{ 0 };
static void add_atomic(int v) { g_atomic_sum += v; }

// 单线程观察者（observer 模板参数 = subject 类型）
struct single_subject : xhqm::subject<single_subject> {};
struct single_observer : xhqm::observer<single_subject>
{
	int count = 0;
	void response(single_subject*, void*) override { ++count; }
};

// 多线程观察者
struct multi_subject : xhqm::subject<multi_subject, xhqm::multi_thread> {};
struct multi_observer : xhqm::observer<multi_subject, xhqm::multi_thread>
{
	std::atomic<int> count{ 0 };
	void response(multi_subject*, void*) override { ++count; }
};

TEST_CASE("test_single_notifier")
{
	g_sum = 0;
	xhqm::notifier<void(int)> n;
	n.regist(&add1);
	n.regist(&add2);
	n.notify(5);                    // g_sum += 5 + 10
	CHECK(g_sum == 15);
	n.remove(&add1);
	n.notify(5);                    // g_sum += 10
	CHECK(g_sum == 25);
	n.remove(&add2);
	n.notify(5);                    // 无回调
	CHECK(g_sum == 25);
}

TEST_CASE("test_multi_notifier")
{
	g_sum = 0;
	xhqm::notifier<void(int), xhqm::multi_thread> n;
	n.regist(&add1);
	n.regist(&add2);
	n.notify(5);
	CHECK(g_sum == 15);
	n.remove(&add1);
	n.notify(5);
	CHECK(g_sum == 25);
}

TEST_CASE("test_multi_notifier_concurrent")
{
	g_atomic_sum = 0;
	xhqm::notifier<void(int), xhqm::multi_thread> n;
	n.regist(&add_atomic);
	std::vector<std::thread> threads;
	for (int t = 0; t < 4; ++t)
		threads.emplace_back([&n]() {
		for (int i = 0; i < 100; ++i) n.notify(1);
			});
	for (auto& th : threads) th.join();
	CHECK(g_atomic_sum == 400);
}

TEST_CASE("test_single_observer")
{
	single_subject s;
	single_observer o1, o2;
	o1.regist(&s);
	o2.regist(&s);
	s.change(&add1);
	CHECK((o1.count == 1 && o2.count == 1));
	o1.remove(&s);
	s.change(&add1);
	CHECK((o1.count == 1 && o2.count == 2));
}

TEST_CASE("test_multi_observer")
{
	multi_subject s;
	multi_observer o1, o2;
	o1.regist(&s);
	o2.regist(&s);
	s.change(&add1);
	CHECK((o1.count == 1 && o2.count == 1));
	o1.remove(&s);
	s.change(&add1);
	CHECK((o1.count == 1 && o2.count == 2));
}

TEST_CASE("test_multi_observer_concurrent")
{
	multi_subject s;
	multi_observer o;
	o.regist(&s);
	std::vector<std::thread> threads;
	for (int t = 0; t < 4; ++t)
		threads.emplace_back([&s]() {
		for (int i = 0; i < 100; ++i) s.change(&add1);
			});
	for (auto& th : threads) th.join();
	CHECK(o.count == 400);
}
