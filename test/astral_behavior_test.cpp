// astral 行为全量测试：kernel_tag（标签层）+ kernel（基础层）全部公开行为
// 重点覆盖：级联删除孤儿标签（time=0）、标签复用、upsert、select 各接口、持久化、冷热加载。
#include "../network/astral/kernel.h"
#include "../network/astral/kernel_tag.h"
#include "../network/astral/mmap_index.h"
#include "test.h"
#include <vector>
#include <string>

using namespace xhqm;
using namespace xhqm::astral;

// ===================== kernel_tag 标签层 =====================

// 同名标签复用：多次 create 同一标签名应共用同一标签节点
TEST_CASE("tag_create_reuse")
{
	kernel_tag mem;
	uuid a = mem.create("m", "A", json("1"), "tag");
	uuid b = mem.create("m", "B", json("2"), "tag");
	CHECK(mem.count() == 3);                 // A + B + 共享标签
	auto la = mem.link_of(a);
	auto lb = mem.link_of(b);
	CHECK((la.size() == 1 && lb.size() == 1));
	CHECK(la[0].value("uuid", "") == lb[0].value("uuid", ""));   // 同一标签 uuid
	CHECK(la[0].value("info", "") == "tag");
}

// create 多标签 + 空标签跳过
TEST_CASE("tag_create_multi_and_empty_link")
{
	kernel_tag mem;
	std::vector<text> links = { "x", "", "y" };
	uuid n = mem.create("m", "N", json("v"), links);
	CHECK(mem.count() == 3);                 // N + x + y（空标签跳过）
	auto lk = mem.link_of(n);
	CHECK(lk.size() == 2);
}

// upsert：未命中新建，命中追加 data + 补 links
TEST_CASE("tag_upsert_hit_and_miss")
{
	kernel_tag mem;
	bool up = false;
	uuid a = mem.upsert("m", "note", json("v1"), { "t1" }, &up);
	CHECK(!up);
	CHECK(mem.count() == 2);                 // note + t1

	uuid b = mem.upsert("m", "note", json("v2"), { "t2" }, &up);
	CHECK(up);
	CHECK(uuid_equal(a, b));                 // 命中同一节点
	CHECK(mem.get(a)->datas.size() == 2);    // v1 + v2
	CHECK(mem.get(a)->links.size() == 2);    // t1 + t2
}

// 级联删除：删数据节点 → 孤儿标签自动删除（本次修复的核心行为）
TEST_CASE("tag_remove_cascade")
{
	kernel_tag mem;
	uuid t = mem.create("m", "note", json("v1"), "text");
	mem.submit("m", t, json("v2"));
	mem.update("m", t, "note2");
	mem.submit("m", t, std::vector<text>{ "extra" });
	CHECK(mem.count() == 3);                 // note2 + text + extra

	CHECK(mem.remove("m", t, 0));
	CHECK(!mem.has(t));
	CHECK(mem.count() == 0);                 // 孤儿标签 text/extra 被级联删除
}

// 共享标签：仍被引用的标签不删，最后一个引用移除后才删
TEST_CASE("tag_remove_shared_tag")
{
	kernel_tag mem;
	uuid a = mem.create("m", "A", json("1"), "shared");
	uuid b = mem.create("m", "B", json("2"), "shared");
	CHECK(mem.count() == 3);

	CHECK(mem.remove("m", a, 0));
	CHECK(mem.count() == 2);                 // B + shared（shared 仍被 B 引用）

	CHECK(mem.remove("m", b, 0));
	CHECK(mem.count() == 0);                 // shared 失去唯一引用 → 删除
}

// 直接删标签：数据节点保留，仅解除关联；不误删数据节点
TEST_CASE("tag_remove_tag_directly")
{
	kernel_tag mem;
	uuid d = mem.create("m", "D", json("data"), "label");
	CHECK(mem.count() == 2);

	auto lk = mem.link_of(d);
	uuid tag_id = uuid::to_uuid(lk[0].value("uuid", ""));

	CHECK(mem.remove("m", tag_id, 0));
	CHECK(mem.has(d));                       // 数据节点保留
	CHECK(mem.count() == 1);
	CHECK(mem.get(d)->links.empty());        // 关联已解除
}

// 无标签的数据节点删除：不产生孤儿
TEST_CASE("tag_remove_no_tags")
{
	kernel_tag mem;
	uuid d = mem.create("m", "D", json("data"));
	CHECK(mem.count() == 1);
	CHECK(mem.remove("m", d, 0));
	CHECK(mem.count() == 0);
}

// 级联后重建名册：被删标签名不再残留，可重新创建同名标签
TEST_CASE("tag_rebuild_names_after_cascade")
{
	kernel_tag mem;
	uuid d = mem.create("m", "D", json("data"), "tag");
	CHECK(mem.remove("m", d, 0));
	CHECK(mem.count() == 0);

	mem.rebuild_names();
	uuid d2 = mem.create("m", "D2", json("data2"), "tag");
	CHECK(mem.count() == 2);                 // D2 + 新 tag（而非残留旧 tag 悬空引用）
}

// remove(time>0)：只删时间点，节点保留
TEST_CASE("tag_remove_time")
{
	kernel_tag mem;
	uuid d = mem.create("m", "D", json("v1"));
	mem.submit("m", d, json("v2"));
	mem.submit("m", d, json("v3"));
	CHECK(mem.get(d)->datas.size() == 3);

	xhqm::u64 mid = mem.get(d)->datas[1].value("time", xhqm::u64(0));
	CHECK(mem.remove("m", d, mid));
	CHECK(mem.has(d));
	CHECK(mem.get(d)->datas.size() == 2);    // 保留 mid 及之后
}

// submit 单标签 / 多标签 / 数据 三种重载
TEST_CASE("tag_submit_variants")
{
	kernel_tag mem;
	uuid d = mem.create("m", "D", json("v1"));
	CHECK(mem.submit("m", d, text("a")));
	CHECK(mem.submit("m", d, std::vector<text>{ "b", "c" }));
	CHECK(mem.get(d)->links.size() == 3);
	CHECK(mem.submit("m", d, json("v2")));
	CHECK(mem.get(d)->datas.size() == 2);

	// 对不存在的节点 submit → false
	uuid ghost = uuid::creat_uuid_v4();
	CHECK(!mem.submit("m", ghost, text("x")));
}

// update info
TEST_CASE("tag_update_info")
{
	kernel_tag mem;
	uuid d = mem.create("m", "old", json("v"));
	CHECK(mem.update("m", d, "new"));
	CHECK(mem.get(d)->info == "new");
}

// select_links：标签模糊匹配
TEST_CASE("tag_select_links")
{
	kernel_tag mem;
	mem.create("m", "A", json("va"), "alphabet");
	mem.create("m", "B", json("vb"), "beta");
	auto r = mem.select_links({ "alpha" });
	CHECK(r.is_array());
	CHECK(r.size() == 1);
}

// select_words：info 模糊匹配
TEST_CASE("tag_select_words")
{
	kernel_tag mem;
	mem.create("m", "hello_world", json("1"));
	mem.create("m", "other", json("2"));
	auto r = mem.select_words({ "hello" });
	CHECK(r.is_array());
	CHECK(r.size() == 1);
}

// select_datas：data 内容关键字匹配
TEST_CASE("tag_select_datas")
{
	kernel_tag mem;
	mem.create("m", "A", json("apple"));
	mem.create("m", "B", json("banana"));
	auto r = mem.select_datas({ "apple" });
	CHECK(r.is_array());
	CHECK(r.size() == 1);
}

// select_nodes：uuid 精准匹配
TEST_CASE("tag_select_nodes")
{
	kernel_tag mem;
	uuid a = mem.create("m", "A", json("1"));
	mem.create("m", "B", json("2"));
	auto r = mem.select_nodes({ a });
	CHECK(r.is_array());
	CHECK(r.size() == 1);
	CHECK(r[0].value("uuid", "") == cipher::uuid::to_text(a));
}

// ===================== kernel 基础层 =====================

// create / count / has / get
TEST_CASE("kernel_create_basic")
{
	kernel mem;
	uuid a = mem.create("m", "note", json("hello"));
	CHECK(!uuid_equal(a, uuid_none()));
	CHECK(mem.count() == 1);
	CHECK(mem.has(a));
	CHECK(!mem.has(uuid::creat_uuid_v4()));
	const node* nd = mem.get(a);
	CHECK((nd && nd->info == "note"));
	CHECK(nd->datas.size() == 1);
}

// ensure：none 新建 / 已存在追加 / 指定 uuid 新建
TEST_CASE("kernel_ensure")
{
	kernel mem;
	uuid a = mem.ensure("m", uuid_none(), "A", json("v1"));
	CHECK(!uuid_equal(a, uuid_none()));
	CHECK(mem.count() == 1);

	CHECK(uuid_equal(mem.ensure("m", a, "A", json("v2")), a));   // 已存在 → 追加
	CHECK(mem.get(a)->datas.size() == 2);

	uuid fixed = uuid::creat_uuid_v4();
	CHECK(uuid_equal(mem.ensure("m", fixed, "F", json("vf")), fixed));  // 指定 uuid 新建
	CHECK(mem.has(fixed));
	CHECK(mem.count() == 2);
}

// set_connect / dis_connect 双向关联
TEST_CASE("kernel_connect")
{
	kernel mem;
	uuid a = mem.create("m", "A", json("1"));
	uuid b = mem.create("m", "B", json("2"));

	CHECK(mem.set_connect(a, b));
	CHECK(mem.get(a)->links.size() == 1);
	CHECK(mem.get(b)->links.size() == 1);

	CHECK(mem.dis_connect(a, b));
	CHECK(mem.get(a)->links.empty());
	CHECK(mem.get(b)->links.empty());
}

// update_info / update_data
TEST_CASE("kernel_update")
{
	kernel mem;
	uuid a = mem.create("m", "A", json("1"));
	CHECK(mem.update_info("m", a, "A2"));
	CHECK(mem.get(a)->info == "A2");
	CHECK(mem.update_data("m", a, json("2")));
	CHECK(mem.get(a)->datas.size() == 2);
}

// remove_time / remove_node
TEST_CASE("kernel_remove")
{
	kernel mem;
	uuid a = mem.create("m", "A", json("v1"));
	mem.update_data("m", a, json("v2"));
	mem.update_data("m", a, json("v3"));
	CHECK(mem.get(a)->datas.size() == 3);

	xhqm::u64 mid = mem.get(a)->datas[1].value("time", xhqm::u64(0));
	CHECK(mem.remove_time("m", a, mid));
	CHECK(mem.has(a));
	CHECK(mem.get(a)->datas.size() == 2);

	CHECK(mem.remove_node("m", a));
	CHECK(!mem.has(a));
	CHECK(mem.count() == 0);
}

// select_uuid / select_info / select_data / select_time / select_size
TEST_CASE("kernel_select_variants")
{
	kernel mem;
	uuid a = mem.create("m", "apple_pie", json("fruit"));
	uuid b = mem.create("m", "banana", json("yellow"));
	mem.update_data("m", b, json("ripe"));

	// uuid
	auto su = mem.select_uuid({ a });
	CHECK((su.size() == 1 && uuid_equal(su[0].uuid, a)));

	// info 子串
	auto si = mem.select_info({ "apple" });
	CHECK(si.size() == 1);

	// data 子串（只匹配最新一条）
	auto sd = mem.select_data({ "ripe" });
	CHECK((sd.size() == 1 && uuid_equal(sd[0].uuid, b)));
	auto sd_old = mem.select_data({ "yellow" });   // yellow 已非最新一条 → 不命中
	CHECK(sd_old.empty());

	// time=0 → 全部有数据的节点
	auto st = mem.select_time(0);
	CHECK(st.size() == 2);

	// size=1 → 每个节点最新 1 条
	auto ss = mem.select_size(1);
	CHECK(ss.size() == 2);
	for (const auto& n : ss) CHECK(n.datas.size() == 1);
}

// link_of / data_of
TEST_CASE("kernel_link_of_data_of")
{
	kernel mem;
	uuid a = mem.create("m", "A", json("v"));
	uuid b = mem.create("m", "B", json("w"));
	mem.set_connect(a, b);

	auto lk = mem.link_of(a);
	CHECK((lk.is_array() && lk.size() == 1));

	auto dt = mem.data_of(b);
	CHECK(dt.is_object());
	CHECK(dt.value("info", "") == "B");
	CHECK(dt.at("datas").size() == 1);
}

// json 持久化往返
TEST_CASE("kernel_persist_json")
{
	kernel_tag mem;
	uuid t = mem.create("m", "note", json("hello"), "text");
	mem.submit("m", t, json("world"));

	const text path = "astral_behavior_json.tmp";
	CHECK(mem.save_json(path));
	kernel_tag mem2;
	CHECK(mem2.load_json(path));
	CHECK(mem2.count() == mem.count());
	CHECK(mem2.has(t));
	CHECK(mem2.get(t)->info == "note");
	CHECK(mem2.get(t)->datas.size() == 2);
	std::remove(path.c_str());
}

// 二进制持久化往返
TEST_CASE("kernel_persist_binary")
{
	kernel_tag mem;
	uuid t = mem.create("m", "note", json("hello"), "text");
	mem.submit("m", t, json("world"));

	const text path = "astral_behavior_bin.tmp";
	CHECK(mem.save_binary(path));
	kernel_tag mem2;
	CHECK(mem2.load_binary(path));
	CHECK(mem2.count() == mem.count());
	CHECK(mem2.has(t));
	CHECK(mem2.get(t)->datas.size() == 2);
	std::remove(path.c_str());
}

// 冷热加载：load_index 只载索引，data_fetch 单体加热
TEST_CASE("kernel_cold_fetch")
{
	kernel mem;
	uuid a = mem.create("m", "A", json("data-a"));
	uuid b = mem.create("m", "B", json("data-b"));
	const text path = "astral_behavior_cold.tmp";
	CHECK(mem.save_binary(path));

	kernel cold;
	CHECK(cold.load_index(path));
	CHECK(cold.count() == 2);
	CHECK(cold.loaded_size() == 0);          // 全冷
	CHECK(!cold.loaded_data(a));

	CHECK(cold.data_fetch(a));
	CHECK(cold.loaded_data(a));              // a 转热
	CHECK(!cold.loaded_data(b));             // b 仍冷
	CHECK(cold.loaded_size() == 1);
	CHECK(cold.get(a)->datas.size() == 1);
	std::remove(path.c_str());
}

// mmap 只读索引
TEST_CASE("kernel_mmap")
{
	kernel_tag mem;
	uuid t = mem.create("m", "note", json("hello"), "text");
	mem.submit("m", t, json("world"));

	const text path = "astral_behavior_mmap.tmp";
	CHECK(mem.save_mmap(path));
	mmap_index idx;
	CHECK(idx.open(path));
	CHECK(idx.count() == mem.count());
	text minfo;
	json mdata;
	CHECK(idx.get(t, minfo, mdata));
	CHECK(minfo == "note");
	CHECK(mdata.size() == 2);
	CHECK(idx.find_info("note").size() == 1);
	std::remove(path.c_str());
}

// 级联回归：数据节点（datas 非空）即使因通用 connect 出现在 links 里，也绝不误删
TEST_CASE("tag_remove_does_not_delete_data_node")
{
	kernel_tag mem;
	uuid a = mem.create("m", "A", json("data-a"), "tag");
	uuid b = mem.create("m", "B", json("data-b"));   // 无标签数据节点
	mem.set_connect(a, b);                            // 用通用 connect 把 b 挂进 a.links
	CHECK(mem.count() == 3);

	CHECK(mem.remove("m", a, 0));
	CHECK(!mem.has(a));
	CHECK(mem.has(b));                                // b 是数据节点，不被级联删
	CHECK(mem.count() == 1);                          // tag 变孤儿被删，仅剩 b
}

// remove 模板：按谓词决定是否删整节点
TEST_CASE("kernel_remove_predicate")
{
	kernel mem;
	uuid a = mem.create("m", "A", json("v"));
	CHECK(!mem.remove("m", a, [](const text&, const node&) { return false; }));
	CHECK(mem.has(a));
	CHECK(mem.remove("m", a, [](const text&, const node&) { return true; }));
	CHECK(!mem.has(a));
}

// select_time / select_size 边界
TEST_CASE("kernel_select_time_size_edge")
{
	kernel mem;
	uuid a = mem.create("m", "A", json("1"));
	mem.update_data("m", a, json("2"));
	mem.update_data("m", a, json("3"));

	auto st = mem.select_time(0);
	CHECK((st.size() == 1 && st[0].datas.size() == 3));

	auto ss = mem.select_size(10);   // 请求超出实际条数 → 返回全部
	CHECK((ss.size() == 1 && ss[0].datas.size() == 3));
}

// link_of(wath_info=false) / data_of(full_data=false)
TEST_CASE("kernel_link_data_variants")
{
	kernel mem;
	uuid a = mem.create("m", "A", json("v"));
	uuid b = mem.create("m", "B", json("w"));
	mem.set_connect(a, b);

	auto lk_uuid = mem.link_of(a, false);
	CHECK((lk_uuid.is_array() && lk_uuid.size() == 1));
	CHECK(lk_uuid[0].value("uuid", "") == cipher::uuid::to_text(b));
	CHECK(!lk_uuid[0].contains("info"));

	auto dt = mem.data_of(b, false);
	CHECK(dt.is_object());
	CHECK(dt.at("datas").size() == 0);
}

// 兼容性：旧行为遗留的孤儿标签，新代码加载后不破坏、查询不可见、新建正常、级联不受影响
TEST_CASE("kernel_compat_existing_orphan_tag")
{
	// 1) 用旧行为（base remove_node，无级联）制造孤儿标签并落盘，模拟旧库
	{
		kernel_tag old;
		uuid d = old.create("m", "D", json("data"), "tag");
		CHECK(old.remove_node("m", d));      // 绕过 kernel_tag::remove 的级联
		CHECK(old.count() == 1);             // 仅剩孤儿 tag
		CHECK(old.save_binary("astral_compat_orphan.tmp"));
	}

	// 2) 新代码加载
	kernel_tag mem;
	CHECK(mem.load_binary("astral_compat_orphan.tmp"));
	CHECK(mem.count() == 1);                 // 孤儿 tag 仍在（不主动回收，无害）

	// 3) 查询不命中孤儿标签（datas 空被跳过；select_words 无匹配返回空数组）
	auto r = mem.select_words({ "tag" });
	CHECK((r.is_array() && r.empty()));

	// 4) 重建名册后，新建同名标签正常
	mem.rebuild_names();
	uuid d2 = mem.create("m", "D2", json("data2"), "tag");
	CHECK(mem.has(d2));
	CHECK(mem.link_of(d2).size() == 1);

	// 5) 级联只清理本次孤儿，不动历史孤儿标签
	CHECK(mem.remove("m", d2, 0));
	CHECK(mem.count() == 1);                 // 仅剩历史孤儿 tag
	std::remove("astral_compat_orphan.tmp");
}
