#include <algorithm>
#include "link.h"

namespace xhqm
{
	namespace nebula
	{
		// ------------------------------------------------ L1 机制 ----------------

		void link::heart_time(uint t, uint f, uint s)
		{
			m_t = t; m_f = f; m_s = s;
		}

		size link::degree() const
		{
			nebula::size count = 0;
			for (auto& addr : m_addr_listen)
			{
				count += addr->size_neighbors();
			}
			return count;
		}

		bool link::life()
		{
			// 零号协议无序、事件队列无限：多 worker 乱序执行合法，帧序不设保障。
			for (auto& addr : m_addr_listen)
			{
				addr->opened();
				addr->frame_signal.disconnect_all();
				addr->broad_signal.disconnect_all();
				//addr->frame_signal.connect(static_cast<nebula::life*>(this), &life::recv_event, xhqm::signal_type::signal_direct);
				//addr->broad_signal.connect(static_cast<nebula::life*>(this), &life::recv_event, xhqm::signal_type::signal_direct);
				addr->frame_signal.connect([this, uuid = addr->uuid()](const nebula::json& event) { this->recv_event(uuid, event); }, xhqm::signal_type::signal_direct);
				addr->broad_signal.connect([this, uuid = addr->uuid()](const nebula::json& event) { this->recv_event(uuid, event); }, xhqm::signal_type::signal_direct);

			}
			if (!m_ping_runing)
			{
				m_ping_runing = true;
				if (m_ping_thread.joinable())
				{
					m_ping_thread.join();
				}
				m_ping_thread = std::thread([this]
				{
					this->ping_runing();
				});
			}
			return true;
		}

		bool link::stop()
		{
			for (auto& addr : m_addr_listen)
			{
				addr->closed();
			}
			if (m_ping_runing)
			{
				m_ping_runing = false;
				if (m_ping_thread.joinable())
				{
					m_ping_thread.join();
				}
			}
			return true;
		}

		void link::insert_addr(const nebula::addr& ad)
		{
			auto it = std::find(m_addr_listen.begin(), m_addr_listen.end(), ad);
			if (it == m_addr_listen.end())
			{
				m_addr_listen.push_back(ad);
			}
		}

		void link::remove_addr(const nebula::addr& ad)
		{
			auto it = std::find(m_addr_listen.begin(), m_addr_listen.end(), ad);
			if (it != m_addr_listen.end())
			{
				m_addr_listen.erase(it);
			}
		}

		bool link::remove_neighbor(const nebula::uuid& id)
		{
			for (auto& addr : m_addr_listen)
			{
				if (addr->remove_neighbor(id))
				{
					return true;
				}
			}
			return false;
		}

		bool link::remove_neighbor(const nebula::uuid& a_id, const nebula::uuid& id)
		{
			for (auto& addr : m_addr_listen)
			{
				if (addr->uuid() == a_id)
				{
					if (addr->remove_neighbor(id))
					{
						return true;
					}
				}
			}
			return false;
		}

		bool link::insert_neighbor(const nebula::uuid& a_id, const nebula::addr& id, bool force)
		{
			if ((this->degree() >= bound_n() && !force) || !id) return false;						// 邻居满额不再添加
			if (this->has_neighbor(id->uuid())) return false;		// 邻居已存在无需添加
			for (auto& addr : m_addr_listen)
			{
				if (addr->uuid() == a_id)
				{
					addr->insert_neighbor(id);
					return true;
				}
			}
			return false;
		}

		std::vector<nebula::uuid> link::neighbors_uuid() const
		{
			std::vector<nebula::uuid> list;
			for (auto& addr : m_addr_listen)
			{
				addr->for_neighbors([&list](const nebula::addr& neighbor)
				{
					if (neighbor) list.push_back(neighbor->uuid());
				}, true);
			}
			return list;
		}

		std::vector<nebula::addr> link::neighbors_addr() const
		{
			std::vector<nebula::addr> list;
			for (auto& addr : m_addr_listen)
			{
				addr->for_neighbors([&list](const nebula::addr& neighbor)
					{
						if (neighbor) list.push_back(neighbor);
					}, true);
			}
			return list;
		}

		bool link::has_neighbor(const nebula::uuid& id) const
		{
			for (auto& addr : m_addr_listen)
			{
				if (addr->has_neighbor(id))
				{
					return true;
				}
			}
			return false;
		}

		void link::ping_runing()  //发送心跳
		{
			xhqm::timer timer;
			while (m_ping_runing)
			{
				timer.uptimer(); //心跳通知
				this->send_heart(this->make_ping());// 心跳通知
				this->life_event(this->make_dath());// 判死清理
				if (degree() < bound_m())// 规则1：x<m 广播 bind 求邻（自动补连接）
				{
					this->send_broad(this->make_bind());
				}
				xhqm::size rt = timer.mtimes();
				xhqm::timesys::msleep(rt < m_t ? m_t - rt : m_t);
			}
			m_ping_runing = false;
		}

		text link::exec_event(const nebula::json& event)
		{
			text type = nebula::life::exec_event(event);
			if (type == "") {}
			else if (type == "ping") { exec_ping(event); type = ""; }
			else if (type == "bind") { exec_bind(event); type = ""; }
			else if (type == "dath") { exec_dath(event); type = ""; }
			else if (type == "link") { exec_link(event); type = ""; }
			else if (type == "send") { exec_send(event); type = ""; }
			return type;//本层级不处理的事件
		}

		void link::send_heart(const nebula::json& heart)
		{
			nebula::json send = heart;
			send["send_time"] = xhqm::timesys::ntime();
			send["send_node"] = this->uuid().to_text();
			auto neighbors = this->neighbors_addr();
			for (const auto& neighbor : neighbors)
			{
				if (neighbor->alive()) //发送失败，标记为失活但不移除
				{
					neighbor->alive(neighbor->send_frame(send));
				}
			}
		}

		void link::send_broad(const nebula::json& broad)
		{
			nebula::json send = broad;
			send["send_time"] = xhqm::timesys::ntime();
			send["send_node"] = this->uuid().to_text();
			for (auto& addr : m_addr_listen)
			{
				send["send_addr"] = addr->make_json();
				addr->send_broad(send);
			}
		}

		void link::recv_event(const nebula::uuid& recv, const nebula::json& event)
		{
			json copy = event;
			copy["recv_time"] = xhqm::timesys::ntime();
			copy["recv_addr"] = nebula::uuid::to_text(recv);
			life_event(copy);
		}
		// ------------------------------------------------ 构造器 ----------------

		nebula::json link::make_dath()
		{
			json event;
			event["type"] = "dath";
			return event;
		}
		nebula::json link::make_ping()
		{
			json event;
			event["type"] = "ping";
			return event;
		}
		nebula::json link::make_bind()
		{
			json event;
			event["type"] = "bind";
			event["name"] = this->name();
			event["info"] = this->info();
			return event;
		}
		nebula::json link::make_link()
		{
			json event;
			event["type"] = "link";
			event["name"] = this->name();
			event["info"] = this->info();
			return event;
		}
		nebula::json link::make_send(const nebula::json& value)
		{
			// send 包装：{type:"send", goto: 路由目标, data: 完整载荷}。
			json event;
			event["type"] = "send";
			event["goto"] = value["goto"];
			event["data"] = value["data"];
			return event;
		}
		// ------------------------------------------------ 执行器 ----------------
		void link::exec_send(const json& event)
		{
			nebula::uuid target;
			nebula::text uuid = event.value("goto", "");
			if (bool(target = uuid) && event.contains("data"))
			{
				json send = event["data"];
				send["send_time"] = xhqm::timesys::ntime();
				send["send_node"] = this->uuid().to_text();
				for (auto& addr : m_addr_listen)
				{
					std::vector<nebula::addr> neighbors;
					send["send_addr"] = addr->make_json();
					addr->run_neighbors(target, [&neighbors](const nebula::addr& neighbor) {
						neighbors.push_back(neighbor);
						}, true);
					for (const auto& neighbor : neighbors)
					{
						if (neighbor->alive()) //发送失败，标记为失活但不移除
						{
							neighbor->alive(neighbor->send_frame(send, static_cast<sint>(this->m_s)));
						}
					}
				}
			}
		}
		void link::exec_dath(const json& event)
		{
			// 判死：fail = (time - last)/心跳间隔（纳秒统一），超上限清理死邻居
			xhqm::u64 time = xhqm::timesys::ntime();
			xhqm::u64 inte = m_t ? xhqm::u64(m_t) * 1000000ull : 1000000000ull;   // 毫秒 → 纳秒
			for (auto& addr : m_addr_listen)
			{
				std::vector<nebula::uuid> targets;
				addr->for_neighbors([time, inte, limt = this->m_f, &targets](const nebula::addr& neighbor) {
					neighbor->fail((time - neighbor->last()) / inte);
					if (neighbor->fail() >= limt)
					{
						targets.push_back(neighbor->uuid());
					}
					}, true);

				for (const auto& neighbor : targets)
				{
					addr->remove_neighbor(neighbor);
				}
			}
		}
		void link::exec_ping(const json& event)
		{
			// 心跳：查表命中更新最后活跃时间 + 清零计数器；未挂表忽略（等 link 先到）
			nebula::text text_uuid;
			nebula::uuid send_uuid;
			send_uuid = (text_uuid = event.value("send_node", ""));
			if (send_uuid)
			{
				for (auto& addr : m_addr_listen)
				{
					addr->run_neighbors(send_uuid, [](const nebula::addr& neighbor) {
						neighbor->last(xhqm::timesys::ntime());
						neighbor->fail(0); neighbor->alive(true);
						}, true);
				}
			}
		}
		void link::exec_bind(const json& event)
		{
			// 广播求邻：x<n 且未挂表 → 挂表（乐观）→ 包装 link 发送事件入池；否则抛弃
			nebula::text text_uuid;
			nebula::uuid send_uuid;
			nebula::uuid recv_uuid;
			send_uuid = (text_uuid = event.value("send_node", ""));
			recv_uuid = (text_uuid = event.value("recv_addr", ""));
			if (send_uuid && recv_uuid)
			{
				if (degree() >= bound_n()) return;
				if (send_uuid == this->uuid()) return;   // 自己的广播
				if (this->has_neighbor(send_uuid)) return; //已经是邻居
				if (nebula::addr nb_addr = addr_from(event.value("send_addr", nebula::json())))
				{
					nb_addr->uuid(send_uuid);
					nb_addr->name(event.value("name", nebula::text()));
					nb_addr->info(event.value("info", nebula::json()));
					insert_neighbor(recv_uuid, nb_addr, false);
					this->life_event(make_send(nebula::json{
						{ "goto", send_uuid.to_text()}, {"data", make_link()}
						}));
				}
			}
		}
		void link::exec_link(const json& event)
		{
			// 确认：x<m 才挂表；m<=x 抛弃（单边关系由心跳超时自愈）
			nebula::text text_uuid;
			nebula::uuid send_uuid;
			nebula::uuid recv_uuid;
			send_uuid = (text_uuid = event.value("send_node", ""));
			recv_uuid = (text_uuid = event.value("recv_addr", ""));
			if (send_uuid && recv_uuid)
			{
				if (degree() >= bound_m()) return;
				if (send_uuid == this->uuid()) return;   // 自己的广播
				if (this->has_neighbor(send_uuid)) return; //已经是邻居
				if (nebula::addr nb_addr = addr_from(event.value("send_addr", nebula::json())))
				{
					nb_addr->uuid(send_uuid);
					nb_addr->name(event.value("name", nebula::text()));
					nb_addr->info(event.value("info", nebula::json()));
					insert_neighbor(recv_uuid, nb_addr, false);
				}
			}
		}
	}
}
