// thing 对象系统演示：事件（有向图）与对象（属性快照流 + prov + 合并）。
#include "../network/astral/thing.h"

#include <cstdio>

using namespace xhqm;
using namespace xhqm::thing;

int main()
{
	thing::kernel k;

	// ========== events：有向图（无 provenance） ==========
	std::printf("===== events =====\n");
	astral::uuid root = k.event_create("对话", astral::json{{"topic", "晚饭"}});
	astral::uuid q    = k.event_spawn(root, "提问", astral::json{{"who", "A"}, {"text", "吃什么"}});
	astral::uuid a    = k.event_spawn(q, "回答", astral::json{{"who", "B"}, {"text", "吃面"}});
	std::printf("[event] root 子=%zu; q 父=root(%d) 子=%zu; a 父=q(%d)\n",
		k.event_children(root).size(),
		astral::uuid_equal(k.event_parent(q), root) ? 1 : 0,
		k.event_children(q).size(),
		astral::uuid_equal(k.event_parent(a), q) ? 1 : 0);

	// ========== object：属性快照流 + prov ==========
	std::printf("===== object =====\n");
	astral::uuid t1 = k.object_create("桌子", astral::json{{"name", "桌子"}, {"color", "#ffffff"}});
	// 变化一帧：改颜色（完整属性快照 + prov 描述）
	k.object_set(t1,
		astral::json{{"action", "set"}, {"key", "color"}, {"value", "#ffffffff"}},
		astral::json{{"name", "桌子"}, {"color", "#ffffffff"}});
	// 变化一帧：加位置
	k.object_set(t1,
		astral::json{{"action", "set"}, {"key", "pos"}, {"value", astral::json::array({1, 2, 3})}},
		astral::json{{"name", "桌子"}, {"color", "#ffffffff"}, {"pos", astral::json::array({1, 2, 3})}});

	snapshot s1;
	k.object_get(t1, s1);
	std::printf("[object] %s 最新属性: %s\n", s1.name.c_str(), s1.attrs.dump().c_str());

	// 关联一个对象，再合并
	astral::uuid t2 = k.object_create("桌子(别名)", astral::json{{"name", "桌子"}, {"owner", "张三"}});
	k.object_link(t1, t2);
	astral::uuid primary;
	bool merged = k.merge(t1, t2, "同名同物", 0.9, primary);
	std::printf("[merge] ok=%d primary=%s\n", merged ? 1 : 0, astral::uuid::to_text(primary).c_str());

	snapshot ps;
	k.object_get(primary, ps);
	std::printf("[merge] primary 最新属性: %s\n", ps.attrs.dump().c_str());

	// 检索
	std::printf("[query] object 含'桌子'=%zu, event 含'对话'=%zu\n",
		k.query_object("桌子").size(), k.query_event("对话").size());

	// 持久化往返
	bool saved = k.save_json("/tmp/thing_demo.json");
	std::printf("[persist] save_json=%d nodes=%zu\n", saved ? 1 : 0, (size_t)k.count());
	thing::kernel k2;
	bool loaded = k2.load_json("/tmp/thing_demo.json");
	snapshot ps2;
	bool got = loaded && k2.object_get(primary, ps2);
	std::printf("[persist] load_json=%d primary 最新属性=%s\n",
		loaded ? 1 : 0, got ? ps2.attrs.dump().c_str() : "-");

	return 0;
}
