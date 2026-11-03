#include "zero.h"

namespace xhqm
{
	namespace nebula
	{
		// ------------------------------------------------ 组装构造（原 zero 层，已并入本层） ----------------

		// 池参数派生：c = min(m+1, 硬件)，a = max(c, min(n*2, 硬件))。
		// 只从入参取数，**不读基类成员**：本类的基类链（post → … → life）在 mem-initializer
		// 求值时尚未构造，读 life::m_m / m_n 是未定义行为——而且 nlohmann 的
		// value(key, 默认值) 的默认值实参必然被求值，所以即便 config 显式写了 c/a 也照样会读到。
		// a 的下限夹到 c：xhqm::parallel::threads 对 c == 0 或 a < c 直接抛异常
		// （m/n 取 0 的退化配置原来只会撞上随机值，现在要保证它不抛）。
		static xhqm::size pool_c(xhqm::size m) { return std::min<xhqm::size>(m + 1, hardware_allow()); }
		static xhqm::size pool_a(xhqm::size c, xhqm::size n)
		{
			return std::max(c, std::min<xhqm::size>(n * 2, hardware_allow()));
		}
		static constexpr xhqm::size default_m = 3;   // config 缺 m 时的连接下限
		static constexpr xhqm::size default_n = 9;   // config 缺 n 时的连接上限

		zero::zero(const nebula::json& config, xhqm::size m, xhqm::size n)
			: nebula::zero(config.value("path", ""), m, n
				, pool_c(m), pool_a(pool_c(m), n)
				, config.value("size", xhqm::npos), config.value("time", 1000))//ms
		{

		}

		zero::zero(const nebula::json& config)
			: nebula::zero(config.value("path", "")
				, config.value("m", default_m), config.value("n", default_n)
				, config.value("c", pool_c(config.value("m", default_m)))
				, config.value("a", pool_a(pool_c(config.value("m", default_m)), config.value("n", default_n)))
				, config.value("size", xhqm::npos), config.value("time", 1000))//ms
		{

		}

		// ------------------------------------------------ 词表构造 ----------------

		nebula::json zero::make_logic(const nebula::json& value)
		{
			nebula::json ev;
			ev["type"] = "logic";
			ev["info"] = value;
			ev["make"] = this->uuid().to_text();
			ev["from"] = this->uuid().to_text();
			return ev;
		}
		nebula::json zero::make_proxy(const nebula::json& value)
		{
			nebula::json ev;
			ev["type"] = "proxy";
			ev["make"] = this->uuid().to_text();
			ev["from"] = this->uuid().to_text();
			ev["tool"] = value;//json::array<uuid, argv>
			return ev;
		}
		nebula::json zero::make_claim(const nebula::json& value)
		{
			nebula::json ev;
			ev["type"] = "claim";
			ev["make"] = this->uuid().to_text();
			if (value.is_object() && value.value("broad", false))//是否广播
			{
				ev["from"] = this->uuid().to_text();
			}
			return ev;
		}
		nebula::json zero::make_message(const nebula::json& value)
		{
			nebula::json ev;
			ev["type"] = "message";
			ev["make"] = this->uuid().to_text();
			ev["goto"] = value.value("goto", "");
			ev["data"] = value["data"];
			return ev;
		}
		void zero::exec_logic(const nebula::json& event)
		{
			std::lock_guard<std::mutex> lk(m_logic_mutex); this->logic(event.value("info", json::object()));
		}
		void zero::exec_proxy(const nebula::json& event)
		{
			nebula::json reply = json::array();
			nebula::json tools = event.value("tool", json::array());
			// 远端事件不可信：缺省值一律给 json（value(key, "") 走的是 get<std::string>()，
			// 非字符串载荷会抛 type_error 并从事件池冒出），随后显式判类型
			nebula::json from = event.value("from", nebula::json{});
			if (!from.is_string()) { from = ""; }
			for (const auto& item : tools)
			{
				if (!item.is_object()) continue;// 空/畸形条目防护（value() 于 null 上会抛）
				const nebula::json id_json = item.value("uuid", nebula::json{});
				if (!id_json.is_string()) continue;
				const nebula::text id = id_json.get<nebula::text>();
				zero::kit tool;                  // 空 kit：未绑函数时调用返回空对象
				{
					std::lock_guard<std::mutex> lk(m_tool_mutex);
					auto iter = m_tool_kitset.find(id);
					if (iter == m_tool_kitset.end()) continue;
					tool = iter->second;
				}
				try
				{
					nebula::json res = tool(item.value("argv", nebula::json::object()));
					reply.push_back({ {"uuid",id},{"reply",res} });
				}
				catch (...)
				{
					reply.push_back({ {"uuid",id},{"reply",json::object()} });
				}
			}
			create_message({ {"goto", from},{"data",reply} });
		}
		void zero::exec_claim(const nebula::json& event)
		{
			nebula::json from = event.value("from", "");
			// ★ 2026-10-06（用户口径：锁加在这里）：
			//   claim 会**并发**到达（每条 claim 一个事件池 worker），而 zero_info() 里
			//   `star::zero_info()` 会**写** info()（末句 `info(star_info)`），logic 轮次里的
			//   sync_info() 也写同一份 json ⇒ 两个线程对同一份 nlohmann::json 边写边读。
			//   实测：star_device 收到两条 claim（from 相同、broad=0）即 0xC0000005，节点整个退出。
			//   这里与 exec_logic **共用同一把锁**（不新增成员 ⇒ 类布局不变 ⇒ 各节点不必重编）：
			//   claim 与 logic 串行后，info() 的写者不再互相踩；create_message 放到锁外（网络 IO 不占锁）。
			nebula::json info;
			{
				std::lock_guard<std::mutex> lk(m_logic_mutex);
				info = this->zero_info();
			}
			create_message({ {"goto", from},{"data",info} });
		}
		void zero::exec_message(const nebula::json& event)
		{
			this->data_signal(event);
		}
		text zero::exec_event(const nebula::json& event)
		{
			text type = post::exec_event(event);
			if (type == "") {}
			else if (type == "logic") { exec_logic(event); type = ""; }
			else if (type == "proxy") { exec_proxy(event); type = ""; }
			else if (type == "claim") { exec_claim(event); type = ""; }
			else if (type == "message") { exec_message(event); type = ""; }
			return type;   // 再往上（star 等用户节点）
		}


		void zero::broad_claim(const nebula::json& value)
		{
			this->send_broad(make_claim(value));
		}

		void zero::create_message(const nebula::json& event)
		{
			nebula::json ev = make_message(event);
			ev["from"] = this->uuid().to_text();

			if (ev.value("goto", "") == "")
			{
				send_broad(ev);
			}
			else if (ev.value("goto", "") == this->uuid().to_text())
			{
				this->life_event(ev);   // 自己发给自己：本地投递（邻居表无自身，走 send 会丢）
			}
			else if (ev.value("goto", "") != this->uuid().to_text())
			{
				// link::make_send 的载荷键是 "data"（原样传 "event" 会让载荷变 null）
				this->life_event(make_send({ {"goto", ev["goto"]}, {"data", ev} }));
			}
		}
	}
}
