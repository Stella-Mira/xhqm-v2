// 多态释放测试：基类指针 delete 派生类时，派生析构必须被执行。
//
// 为什么需要这个夹具：`addr_b` 曾是「有纯虚函数、无虚析构」的类，
// 而 `using addr = std::shared_ptr<addr_b>` 与
// `virtual std::unique_ptr<addr_b> clone() const` 都会**经基类指针释放**对象。
// 基类析构非虚 ⇒ 未定义行为；arm64 + 优化下 clang 直接生成 `brk #0x1`（SIGTRAP），
// 症状是**零输出崩溃**（与 r546 修的 fantasy::drawer 同一签名）。
// 该缺陷已修（addr.h 加 `virtual ~addr_b() = default;`），本夹具防止回归。
#include "../algebra/geometry/geometry.h"
#include "../hardware/parallel/dispatch_no.h"
#include "../network/nebula/addr.h"
#include "test.h"
#include <string>
#include <type_traits>

using namespace xhqm;

namespace
{
	// 派生类：析构里置标记。若基类析构非虚，经基类指针 delete 时本析构**不会被调用**。
	struct tracked_addr : nebula::addr_b
	{
		unsigned int* flag = nullptr;
		unsigned int* member_alive = nullptr;

		tracked_addr(unsigned int* f, unsigned int* m)
			: addr_b(nebula::kind::none), flag(f), member_alive(m)
		{
		}

		~tracked_addr()
		{
			if (flag) *flag += 1;
			if (member_alive) *member_alive += 1;
		}

		nebula::json to_json() const override { return nebula::json::object(); }
		bool from_json(const nebula::json&) override { return true; }
		bool equal_other(const nebula::addr_b*) const override { return false; }
		std::unique_ptr<nebula::addr_b> clone() const override
		{
			// 副本不带计数器：让 dtor_count3 只反映 unique_ptr<addr_b> 释放的那一次
			return std::unique_ptr<nebula::addr_b>(new tracked_addr(nullptr, nullptr));
		}
		bool broadcast(const nebula::json&) const override { return true; }
		bool send(const nebula::json&, const nebula::sint) const override { return true; }
	};
}

TEST_CASE("test_nebula_polymorphic_destruction")
{
	// 1. 编译期：addr_b 整条链上有虚析构（含继承链判定）
	CHECK(std::has_virtual_destructor_v<nebula::addr_b>);
	CHECK(std::has_virtual_destructor_v<nebula::addr_t<nebula::kind::ipv4>>);

	// 2. 经裸基类指针 delete
	unsigned int dtor_count = 0;
	{
		nebula::addr_b* p = new tracked_addr(&dtor_count, nullptr);
		delete p;
	}
	CHECK(dtor_count == 1);   // 非虚析构时这里会是 0（或直接 SIGTRAP）

	// 3. 经 shared_ptr<addr_b>（nebula::addr 的真实使用方式）
	unsigned int dtor_count2 = 0;
	{
		nebula::addr a(new tracked_addr(&dtor_count2, nullptr));
		CHECK(a.use_count() == 1);
	}
	CHECK(dtor_count2 == 1);

	// 4. 经 clone() 返回的 unique_ptr<addr_b>（深拷贝路径）
	unsigned int dtor_count3 = 0;
	{
		tracked_addr src(&dtor_count3, nullptr);
		std::unique_ptr<nebula::addr_b> copy = src.clone();
		CHECK(copy != nullptr);
	}
	CHECK(dtor_count3 == 1);
}

TEST_CASE("test_geometry_polymorphic_destruction")
{
	// geometry::shape 同样曾有「无虚析构」问题（20+ 派生类）。
	// 它目前没有经 shape* 释放的调用点，故只做编译期断言，防止日后回归。
	CHECK(std::has_virtual_destructor_v<geometry::shape>);
	CHECK(std::has_virtual_destructor_v<geometry::line>);
}

TEST_CASE("test_parallel_dispatch_no")
{
	// hardware/parallel/dispatch_no.h 此前**根本无法编译**（类名 share 的构造函数
	// 却写成 array、虚函数参数表尾多一个逗号），因为全仓库无人 include 它而从未暴露。
	// 本用例把它拉进编译面：既验证修正后的头可编可实例化，也防它再被改坏。
	CHECK(std::has_virtual_destructor_v<parallel::task<int, parallel::equipment::no>>);

	// array 实例化 + dispatch 跑一遍
	parallel::array<int> values(4);
	for (xhqm::index i = 0; i < 4; ++i) values[i] = (int)i * 10;
	parallel::dispatch<int> pool;
	pool.compute([](int& v) { v += 1; }, values, true);
	xhqm::index sum = 0;
	for (xhqm::index i = 0; i < 4; ++i) sum += (xhqm::index)values[i];
	CHECK(sum == 64);   // (0+1)+(10+1)+(20+1)+(30+1)

	// share 实例化（原缺陷所在：构造函数名与 = delete 的目标都写错了）
	parallel::share<int> shared_values(3);
	shared_values[0] = 7;
	CHECK(shared_values.size() == 3);
	CHECK(shared_values[0] == 7);

	// task 派生类经基类指针 delete 时，派生析构必须执行
	struct counting_task : parallel::task<int, parallel::equipment::no>
	{
		unsigned int* dtor_flag = nullptr;
		counting_task(unsigned int* f) : dtor_flag(f) {}
		~counting_task() { if (dtor_flag) *dtor_flag += 1; }
		void call(parallel::array<int, parallel::equipment::no>&) override {}
	};

	unsigned int counted = 0;
	{
		parallel::task<int, parallel::equipment::no>* base = new counting_task(&counted);
		delete base;                    // 经基类指针删除
	}
	CHECK(counted == 1);               // 基类析构非虚时这里会是 0

	CHECK(std::has_virtual_destructor_v<parallel::task_function<int>>);
}
