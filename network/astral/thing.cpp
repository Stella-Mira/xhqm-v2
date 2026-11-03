#include "thing.h"

namespace xhqm
{
	namespace astral
	{
		// ================= snapshot =================
		astral::json snapshot::to_json() const
		{
			return astral::json{
				{"id", astral::uuid::to_text(id)},
				{"name", name},
				{"attrs", attrs},
			};
		}

		// ================= kernel =================
		// ---------- events（无 provenance） ----------
		astral::uuid thing::event_create(const astral::text& name, const astral::json& content,
			const astral::uuid& parent)
		{
			astral::json ev;
			ev["type"] = "event";
			ev["content"] = content;
			astral::uuid id = create(m_form, "event:" + name, ev);
			if (!astral::uuid_equal(parent, astral::uuid{}))
				event_set_parent(id, parent);
			return id;
		}

		bool thing::event_append(const astral::uuid& id, const astral::json& content)
		{
			astral::json ev;
			ev["type"] = "event";
			ev["content"] = content;
			return update_data(m_form, id, ev);
		}

		astral::uuid thing::event_spawn(const astral::uuid& parent, const astral::text& child_name,
			const astral::json& content)
		{
			astral::uuid child = event_create(child_name, content);
			astral::json ref;
			ref["type"] = "child";
			ref["info"] = child_name;
			ref["uuid"] = astral::uuid::to_text(child);
			update_data(m_form, parent, ref);
			event_set_parent(child, parent);
			return child;
		}

		bool thing::event_set_parent(const astral::uuid& child, const astral::uuid& parent)
		{
			if (astral::uuid_equal(parent, astral::uuid{})) return true;
			return update(m_form, child,
				[](const astral::text&, astral::node& n, const astral::uuid& parent) -> bool {
				n.links[parent] = "parent";   // 单向：子 → 父
				return true;
			}, parent);
		}

		astral::uuid thing::event_parent(const astral::uuid& id) const
		{
			const astral::node* n = get(id);
			if (!n) return astral::uuid{};
			for (const auto& [lid, linfo] : n->links)
				if (linfo == "parent") return lid;
			return astral::uuid{};
		}

		std::vector<astral::uuid> thing::event_children(const astral::uuid& id) const
		{
			std::vector<astral::uuid> out;
			const astral::node* n = get(id);
			if (!n) return out;
			for (const auto& rec : n->datas)
			{
				if (!rec.is_object()) continue;
				const astral::json& j = rec.value("json", astral::json::object());
				if (j.value("type", astral::text()) == "child" && j.contains("uuid"))
				{
					astral::uuid cu = astral::uuid::to_uuid(j.at("uuid").get<astral::text>());
					if (!astral::uuid_equal(cu, astral::uuid{})) out.push_back(cu);
				}
			}
			return out;
		}

		// ---------- object（属性快照流 + prov） ----------
		astral::uuid thing::object_create(const astral::text& name, const astral::json& attrs)
		{
			astral::json frame = attrs.is_object() ? attrs : astral::json::object();
			frame["prov"] = astral::json{ {"action", "create"} };
			return create(m_form, "object:" + name, frame);
		}

		bool thing::object_set(const astral::uuid& id, const astral::json& prov, const astral::json& attrs)
		{
			astral::json frame = attrs.is_object() ? attrs : astral::json::object();
			frame["prov"] = prov;
			return update_data(m_form, id, frame);
		}

		bool thing::object_link(const astral::uuid& a, const astral::uuid& b)
		{
			return set_connect(a, b);   // 双向关联
		}

		bool thing::object_get(const astral::uuid& id, snapshot& out) const
		{
			const astral::node* n = get(id);
			if (!n || !n->datas.is_array() || n->datas.empty()) return false;
			out = snapshot{};
			out.id = id;
			const astral::text& info = n->info;
			if (info.compare(0, 7, "object:") == 0)
				out.name = info.substr(7);
			// 最后一帧 = 最新属性快照
			const astral::json& frame = n->datas.back().value("json", astral::json::object());
			out.attrs = frame.is_object() ? frame : astral::json::object();
			out.attrs.erase("prov");
			return true;
		}

		// ---------- 合并（身份消解） ----------
		bool thing::merge(const astral::uuid& a, const astral::uuid& b,
			const astral::text& reason, double confidence, astral::uuid& primary_out)
		{
			snapshot sa, sb;
			if (!object_get(a, sa) || !object_get(b, sb)) return false;

			bool a_primary = (sa.attrs.size() >= sb.attrs.size());
			const astral::uuid primary = a_primary ? a : b;
			const snapshot& prim_snap = a_primary ? sa : sb;
			const snapshot& sec_snap = a_primary ? sb : sa;

			// 属性合并：次对象覆盖主对象同 key
			astral::json merged = prim_snap.attrs;
			if (sec_snap.attrs.is_object())
				for (auto it = sec_snap.attrs.begin(); it != sec_snap.attrs.end(); ++it)
					merged[it.key()] = it.value();

			astral::json prov = astral::json{
				{"action", "merge"},
				{"merged_from", astral::uuid::to_text(sec_snap.id)},
				{"reason", reason},
				{"confidence", confidence},
			};
			object_set(primary, prov, merged);

			// 次对象标记 merged_into
			astral::json st;
			st["type"] = "state";
			st["state"] = "merged_into";
			st["target"] = astral::uuid::to_text(primary);
			update_data(m_form, sec_snap.id, st);
			update_info(m_form, sec_snap.id, "object:merged:" + astral::uuid::to_text(primary));

			set_connect(primary, sec_snap.id);

			primary_out = primary;
			return true;
		}

		// ---------- 检索 ----------
		std::vector<snapshot> thing::query_object(const astral::text& word)
		{
			std::vector<snapshot> out;
			auto nodes = select_info({ word });
			for (const auto& n : nodes)
			{
				if (n.info.compare(0, 7, "object:") != 0) continue;
				snapshot s;
				if (object_get(n.uuid, s)) out.push_back(std::move(s));
			}
			return out;
		}

		std::vector<astral::uuid> thing::query_event(const astral::text& word)
		{
			std::vector<astral::uuid> out;
			auto nodes = select_info({ word });
			for (const auto& n : nodes)
				if (n.info.compare(0, 6, "event:") == 0) out.push_back(n.uuid);
			return out;
		}
	}
}
