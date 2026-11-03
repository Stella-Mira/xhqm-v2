// pattern 扩展测试：factory / strategy / command / chain / state_machine（单线程与多线程特化）
#include "../software/pattern/factory.h"
#include "../software/pattern/strategy.h"
#include "../software/pattern/command.h"
#include "../software/pattern/chain.h"
#include "../software/pattern/state_machine.h"
#include "test.h"
#include <atomic>
#include <thread>
#include <vector>

using namespace xhqm;

// ---- factory ----
struct animal { virtual ~animal() = default; virtual int speak() const = 0; };
struct dog : animal { int speak() const override { return 1; } };
struct cat : animal { int speak() const override { return 2; } };

TEST_CASE("test_factory")
{
	factory<animal, int> f;
	f.regist<dog>(1);
	f.regist<cat>(2);
	auto d = f.create(1);
	auto c = f.create(2);
	CHECK((d && d->speak() == 1));
	CHECK((c && c->speak() == 2));
	CHECK(!f.create(99));
	CHECK((f.has(1) && f.has(2) && !f.has(3)));
}

TEST_CASE("test_factory_multi")
{
	factory<animal, int, multi_thread> f;
	f.regist<dog>(1);
	f.regist<cat>(2);
	auto d = f.create(1);
	auto c = f.create(2);
	CHECK((d && d->speak() == 1));
	CHECK((c && c->speak() == 2));
	CHECK(!f.create(99));
}

// ---- strategy ----
TEST_CASE("test_strategy")
{
	strategy<int(int, int)> s;
	s.set([](int a, int b) { return a + b; });
	CHECK(s.execute(2, 3) == 5);
	s.set([](int a, int b) { return a * b; });
	CHECK(s.execute(2, 3) == 6);
	CHECK((bool)s);
	s.clear();
	CHECK(!(bool)s);
}

TEST_CASE("test_strategy_multi")
{
	strategy<int(int, int), multi_thread> s;
	s.set([](int a, int b) { return a + b; });
	CHECK(s.execute(2, 3) == 5);
	s.set([](int a, int b) { return a * b; });
	CHECK(s.execute(2, 3) == 6);
}

// ---- command ----
TEST_CASE("test_command")
{
	int value = 0;
	command_history<> h;
	h.execute(command([&value] { value = 10; }, [&value] { value = 0; }));
	CHECK(value == 10);
	h.undo();
	CHECK(value == 0);
	h.redo();
	CHECK(value == 10);
	CHECK((h.can_undo() && !h.can_redo()));  // redo 后：可 undo，不可再 redo
}

TEST_CASE("test_command_multi")
{
	int value = 0;
	command_history<multi_thread> h;
	h.execute(command([&value] { value = 10; }, [&value] { value = 0; }));
	CHECK(value == 10);
	h.undo();
	CHECK(value == 0);
	h.redo();
	CHECK(value == 10);
}

// ---- chain ----
TEST_CASE("test_chain")
{
	chain<int> c;
	c.add([](int& r) { if (r < 10) { r += 1; return true; } return false; });
	c.add([](int& r) { r += 100; return true; });
	int r1 = 5;
	CHECK((c.handle(r1) && r1 == 6));   // 第一个处理
	int r2 = 20;
	CHECK((c.handle(r2) && r2 == 120)); // 第二个处理
}

TEST_CASE("test_chain_multi")
{
	chain<int, multi_thread> c;
	c.add([](int& r) { if (r < 10) { r += 1; return true; } return false; });
	c.add([](int& r) { r += 100; return true; });
	int r1 = 5;
	CHECK((c.handle(r1) && r1 == 6));
	int r2 = 20;
	CHECK((c.handle(r2) && r2 == 120));
}

// ---- state_machine ----
enum state { s_idle, s_run, s_stop };
enum event { e_start, e_halt };

TEST_CASE("test_state_machine")
{
	state_machine<state, event> sm;
	sm.start(s_idle);
	sm.add_transition(s_idle, e_start, s_run);
	sm.add_transition(s_run, e_halt, s_stop);
	CHECK(sm.handle(e_start));
	CHECK(sm.current() == s_run);
	CHECK(sm.handle(e_halt));
	CHECK(sm.current() == s_stop);
	CHECK(!sm.handle(e_start));  // s_stop 无 e_start 转换
	CHECK(sm.current() == s_stop);
}

TEST_CASE("test_state_machine_multi")
{
	state_machine<state, event, multi_thread> sm;
	sm.start(s_idle);
	sm.add_transition(s_idle, e_start, s_run);
	sm.add_transition(s_run, e_halt, s_stop);
	CHECK(sm.handle(e_start));
	CHECK(sm.current() == s_run);
	CHECK(sm.handle(e_halt));
	CHECK(sm.current() == s_stop);
	CHECK(!sm.handle(e_start));
}
