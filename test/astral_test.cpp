// astral 测试：统一 node + 五类接口（增删改查连）+ json/二进制/mmap 持久化
#include "../network/astral/kernel.h"
#include "../network/astral/kernel_tag.h"
#include "../network/astral/mmap_index.h"
#include "test.h"

using namespace xhqm;
using namespace xhqm::astral;

TEST_CASE("test_create_select")
{
	kernel_tag mem;
	const text form = "mira";

	// 增：建 data 节点 + 关联标签（自动建 link 节点 + connect）
	uuid t = mem.create(form, "note", json("hello"), "text");
	uuid a = mem.create(form, "song", json::array({ 0.1, 0.2 }), "audio");
	CHECK(!uuid_equal(t, uuid_none()));
	CHECK(!uuid_equal(a, uuid_none()));
	CHECK((mem.has(t) && mem.has(a)));
	CHECK(mem.count() == 4);   // note + song + text link + audio link

	// 查 get
	const node* nd = mem.get(t);
	CHECK((nd && nd->info == "note"));
	CHECK(nd->links.size() == 1);   // 关联 text

	// 查 select_nodes：uuid 精准（返回 to_json 数组）
	auto sel = mem.select_nodes({ t });
	CHECK((sel.is_array() && sel.size() == 1));
	CHECK(sel[0].value("uuid", "") == xhqm::cipher::uuid::to_text(t));

	// 查 select_info：info 子串模糊（vector<text>，返回 node_list）
	auto by_note = mem.select_info({ "note" });
	CHECK(by_note.size() == 1);
	auto by_sub = mem.select_info({ "no" });
	CHECK(by_sub.size() == 1);   // note 子串

	// 查 select_time（返回 node_list，link 节点 datas 空被跳过）
	auto by_time = mem.select_time(0);
	CHECK(by_time.size() == 2);   // note + song 都有数据
}

TEST_CASE("test_connect")
{
	kernel mem;
	uuid a = mem.create("m", "A", json("1"));
	uuid b = mem.create("m", "B", json("2"));

	// 连：双向关联
	CHECK(mem.set_connect(a, b));
	CHECK(mem.link_of(a).size() == 1);
	CHECK(mem.link_of(b).size() == 1);
	CHECK(mem.get(a)->links.count(b) == 1);
	CHECK(mem.get(b)->links.count(a) == 1);

	// 断
	CHECK(mem.dis_connect(a, b));
	CHECK(mem.link_of(a).empty());
	CHECK(mem.link_of(b).empty());
}

TEST_CASE("test_update_remove")
{
	kernel_tag mem;
	uuid t = mem.create("m", "note", json("v1"), "text");

	// 改 data（追加）
	CHECK(mem.submit("m", t, json("v2")));
	CHECK(mem.get(t)->datas.size() == 2);

	// 改 info
	CHECK(mem.update("m", t, "note2"));
	CHECK(mem.get(t)->info == "note2");

	// 改 links（追加标签）
	CHECK(mem.submit("m", t, std::vector<text>{ "extra" }));
	CHECK(mem.get(t)->links.size() == 2);   // text + extra

	// 删（time=0 整节点 + 级联清空 link）
	CHECK(mem.remove("m", t, 0));
	CHECK(!mem.has(t));
	// 级联：text link 和 extra link 变成空 link，自动删除
	CHECK(mem.count() == 0);
}

TEST_CASE("test_remove_time")
{
	kernel mem;
	uuid t = mem.create("m", "note", json("v1"));
	mem.update_data("m", t, json("v2"));
	mem.update_data("m", t, json("v3"));
	CHECK(mem.get(t)->datas.size() == 3);

	// 取中间时间点，删之前的（保留 mid 及之后）
	xhqm::u64 mid = mem.get(t)->datas[1].value("time", xhqm::u64(0));
	CHECK(mem.remove_time("m", t, mid));
	CHECK(mem.has(t));                          // 节点保留
	CHECK(mem.get(t)->datas.size() == 2);       // v2、v3 保留

	// 删整个节点
	CHECK(mem.remove_node("m", t));
	CHECK(!mem.has(t));
}

TEST_CASE("test_persist")
{
	kernel_tag mem;
	uuid t = mem.create("m", "note", json("hello"), "text");
	mem.submit("m", t, json("world"));

	// json 持久化
	const text path = "astral_test_tmp.json";
	CHECK(mem.save_json(path));

	kernel mem2;
	CHECK(mem2.load_json(path));
	CHECK(mem2.count() == mem.count());
	CHECK(mem2.has(t));
	const node* nd = mem2.get(t);
	CHECK((nd && nd->info == "note"));
	CHECK(nd->datas.size() == 2);
	CHECK(nd->links.size() == 1);

	// 二进制持久化
	const text bpath = "astral_test_tmp.bin";
	CHECK(mem.save_binary(bpath));
	kernel mem3;
	CHECK(mem3.load_binary(bpath));
	CHECK(mem3.count() == mem.count());
	CHECK(mem3.has(t));

	// mmap 只读索引（v8 统一 node）
	const text mpath = "astral_test_tmp.mmap";
	CHECK(mem.save_mmap(mpath));
	mmap_index idx;
	CHECK(idx.open(mpath));
	CHECK(idx.count() == mem.count());
	text minfo;
	json mdata;
	CHECK(idx.get(t, minfo, mdata));
	CHECK(minfo == "note");
	CHECK(mdata.size() == 2);
	CHECK(idx.find_info("note").size() == 1);
}
