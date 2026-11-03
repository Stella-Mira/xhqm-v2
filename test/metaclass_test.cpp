// metaclass 测试：block / buffer / cache / buffer_dim
#include "../metaclass/block.h"
#include "../metaclass/buffer.h"
#include "../metaclass/cache.h"
#include "test.h"
#include <array>

using namespace xhqm;

TEST_CASE("test_block")
{
	block b(10);
	CHECK(b.size() == 10);
	CHECK(b.good());

	// 字节偏移访问
	xhqm::byte* p = b.value_ptr<xhqm::byte>(0);
	p[0] = 0xAB;
	p[9] = 0xCD;
	CHECK(b.value_ptr<xhqm::byte>(0)[0] == 0xAB);
	CHECK(b.value_ptr<xhqm::byte>(9)[0] == 0xCD);

	// 重新 init
	b.init(5);
	CHECK(b.size() == 5);

	// 移动
	block b2 = std::move(b);
	CHECK(b2.size() == 5);
	CHECK(b.size() == 0);
}

TEST_CASE("test_buffer")
{
	buffer<int> buf(5);
	CHECK(buf.size() == 5);
	buf.value(0) = 10;
	buf.value(4) = 40;
	CHECK((buf.value(0) == 10 && buf.value(4) == 40));

	// 复制
	buffer<int> buf2 = buf;
	CHECK((buf2.size() == 5 && buf2.value(0) == 10));

	// reset
	buf.reset(3);
	CHECK(buf.size() == 3);
}

TEST_CASE("test_cache")
{
	// 固定尺寸（array 特化）—— 已修复：模板参数名 size → size_v，不再与类型别名 xhqm::size 冲突
	cache<int, 3> c3 = { 1, 2, 3 };
	CHECK((c3.size() == 3 && c3[2] == 3));

	// 动态（vector 特化，size=0）
	cache<int> c0 = { 1, 2, 3 };
	CHECK((c0.size() == 3 && c0[2] == 3));

	// execfun
	int sum = 0;
	c0.execfun([&sum](int& v) { sum += v; });
	CHECK(sum == 6);
}

TEST_CASE("test_buffer_dim")
{
	std::array<xhqm::size, 2> dims = { 3, 4 };
	buffer_dim<int, 2> d1(dims);
	CHECK(d1.size() == 12);
	CHECK((d1.dimensions()[0] == 3 && d1.dimensions()[1] == 4));

	// 复制构造应保留 dimensions（可能丢，记录）
	buffer_dim<int, 2> d2 = d1;
	CHECK(d2.size() == 12);
	CHECK((d2.dimensions()[0] == 3 && d2.dimensions()[1] == 4));
}
