#include "data.h"

namespace xhqm
{
	namespace nebula
	{
		static text channel_text(const data::channel_kind& k)
		{
			switch (k)
			{
			case data::channel_kind::node_c: return "node";
			case data::channel_kind::device: return "device";
			case data::channel_kind::source: return "source";
			default:                         return "none";
			}
		}

		std::vector<nebula::uuid> data::channels(const nebula::uuid& parent) const
		{
			std::vector<nebula::uuid> out;
			std::lock_guard<std::mutex> lk(m_data_mutex);
			for (const auto& kv : m_windows)
				if (kv.second.parent == parent) out.push_back(kv.first);
			return out;
		}

		nebula::uuid data::create(channel_kind kind, const text& name, const astral::json& info)
		{
			nebula::uuid cid = this->uuid();
			if (kind != channel_kind::node_c)
			{
				cid = xhqm::cipher::uuid::creat_uuid_v4({});
			}
			if (cid != nebula::uuid{})
			{
				std::lock_guard<std::mutex> lk(m_data_mutex);
				if (m_windows.find(cid) == m_windows.end())
				{
					data::window w(cid);
					w.kind = kind;
					w.name(name);
					w.info(info);
					m_windows.emplace(cid, std::move(w));
				}
				// 记忆内核同步建档（历史查询的落点；ensure 幂等）
				json json; json["type"] = "ensure"; json["name"] = name; json["kind"] = channel_text(kind); json["info"] = info;
				m_kernel.ensure(this->uuid().to_text(), cid, channel_text(kind) + "-" + name, json);
			}

			return cid;
		}

		void data::remove(const nebula::uuid& id)
		{
			std::vector<nebula::uuid> kill;   // {通道 uuid, 内核 form}
			std::function<void(const nebula::uuid&)> collect = [&](const nebula::uuid& p)
			{
				// 级联收集：子通道一并移除（源随设备、设备随节点）
				for (const auto& kv : m_windows)
				{
					if (kv.second.parent == p)
					{
						kill.push_back(kv.first);
						collect(kv.first);
					}
				}
			};

			{
				std::lock_guard<std::mutex> lk(m_data_mutex);
				auto it = m_windows.find(id);
				if (it != m_windows.end())
				{
					kill.push_back(id); collect(id);
					for (const auto& kv : kill)
					{
						m_windows.erase(kv);
					}
				}
			}
		}

		bool data::connect(const nebula::uuid& child, const nebula::uuid& parent)
		{
			if (child && parent && child != parent)
			{
				std::lock_guard<std::mutex> lk(m_data_mutex);
				auto ic = m_windows.find(child);
				auto ip = m_windows.find(parent);
				if (ic != m_windows.end() && ip != m_windows.end())
				{
					// 记忆内核双向关联（设备↔节点、源↔设备）
					m_kernel.set_connect(child, parent);
					ic->second.parent = parent;
					return true;
				}
			}
			return false;
		}

		bool data::updata_info(const nebula::uuid& id, const text& name, const astral::json& info)
		{
			std::lock_guard<std::mutex> lk(m_data_mutex);
			auto it = m_windows.find(id);
			if (it != m_windows.end())
			{
				json json; json["type"] = "update"; json["name"] = name; json["kind"] = channel_text(it->second.kind); json["info"] = info;
				m_kernel.update_info(this->uuid().to_text(), id, channel_text(it->second.kind) + "-" + name);
				m_kernel.update_data(this->uuid().to_text(), id, json);
				it->second.name(name);
				it->second.info(info);
				return true;
			}
			return false;
		}

		bool data::insert_data(const nebula::uuid& id, const astral::json& frame)
		{
			const xhqm::u64 now = xhqm::timesys::ntime();
			{
				std::lock_guard<std::mutex> lk(m_data_mutex);
				auto it = m_windows.find(id);
				if (it != m_windows.end())
				{
					json json; json["type"] = "insert"; json["data"] = frame;
					m_kernel.update_data(this->uuid().to_text(), id, json);
					it->second.view.push_back({ now, frame });
					//时长淘汰（帧龄 > 上限）条数淘汰（容量 > 上限）
					auto& view = it->second.view;
					const xhqm::u64 cap_size = (xhqm::u64)m_capacity;
					const xhqm::u64 dur_time = (xhqm::u64)m_duration * 1000000ull;
					while (cap_size > 0 && view.size() > m_capacity) view.pop_front();
					while (dur_time > 0 && view.size() && now - view.front().first > dur_time) view.pop_front();
					return true;
				}
			}
			return false;
		}

		astral::json data::select_message(const nebula::uuid& id)
		{
			std::lock_guard<std::mutex> lk(m_data_mutex);
			astral::json out = astral::json::object();
			auto it = m_windows.find(id);
			if (it != m_windows.end())
			{
				if (it->second.view.size())
				{
					const auto& kv = it->second.view.back();
					out = astral::json{ {"time", kv.first}, {"data", kv.second} };
				}
			}
			return out;
		}

		astral::json data::select_windows(const nebula::uuid& id)
		{
			std::lock_guard<std::mutex> lk(m_data_mutex);
			astral::json arr = astral::json::array();
			auto it = m_windows.find(id);
			if (it != m_windows.end())
			{
				for (const auto& kv : it->second.view)
				{
					arr.push_back(astral::json{ {"time", kv.first}, {"data", kv.second} });
				}
			}
			return arr;
		}

		bool data::frame_record(const astral::json& rec, xhqm::u64& time, astral::json& frame)
		{
			// 内核条目形如 {time, form, json:{type, data}}（update_data / ensure 落库时的形状）：
			//   时间在**外层**记录戳上 —— 内层没有 time，原实现取内层，历史时间因此恒 0；
			//   载荷在内层 json.data 上；ensure / update_info 建档的桩条目没有 data 键 → 不是帧；
			//   内层缺键的读法原是 const operator[]（nlohmann JSON_ASSERT，release 下 UB），此处改为查找判定。
			if (!rec.is_object()) return false;
			astral::json inner = rec.value("json", astral::json::object());
			if (!inner.is_object()) return false;
			auto iter = inner.find("data");
			if (iter == inner.end()) return false;
			time = rec.value("time", xhqm::u64(0));
			frame = *iter;
			return true;
		}

		astral::json data::select_history(const nebula::uuid& id)
		{
			std::lock_guard<std::mutex> lk(m_data_mutex);
			astral::json arr = astral::json::array();
			if (auto n = m_kernel.get(id))
			{
				xhqm::u64 time = 0;
				astral::json frame;
				for (const auto& rec : n->datas_json())
				{
					if (frame_record(rec, time, frame))
					{
						arr.push_back(astral::json{
							{"time", time},
							{"data", frame}
							});
					}
				}
			}
			return arr;
		}
	}
}
