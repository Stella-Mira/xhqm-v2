#include "post.h"

namespace xhqm
{
	namespace nebula
	{
		// ------------------------------------------------ maket造信封） ----------------

		nebula::json post::make_post(const nebula::text& type, const nebula::uuid& goal,
			const nebula::uuid& channel, const nebula::json& data)
		{
			nebula::json ev;
			ev["type"] = type;
			ev["id"] = nebula::uuid::creat_uuid_v4({}).to_text();   // 事件 idt与节点 uuid 区分：去重/追踪用）
			ev["from"] = this->uuid().to_text();                    // 发起节点身份t不是地址）
			ev["goal"] = goal.to_text();                            // 目标节点身份
			ev["channel"] = channel.to_text();
			if (!data.is_null()) { ev["data"] = data; }
			return ev;
		}

		bool post::send_goal(const nebula::uuid& goal, const nebula::json& value)
		{
			if (!goal) return false;
			// 可达性判定与 find 的选路同一口径：直连邻居，或图上存在「下一跳是我邻居」的路径。
			// 发不出去就不发t零号协议：问不等答，无路径自然丢），调用方据此记一次 fail。
			if (!this->routable(goal)) { return false; }
			// 信封直接入池：goal == 我 → 本层处理；否则 find::exec_relay 逐跳转发。
			// 路由只发生在 find 一处，post 不碰地址、不算路径、不维护途经表。
			this->life_event(value);
			return true;
		}

		bool post::this_post(const nebula::json& event)
		{
			return (event.value("goal", "") == this->uuid().to_text());
		}

		// ------------------------------------------------ exect处理） ----------------

		void post::exec_care(const nebula::json& event)
		{
			if (!this_post(event)) return;   // 不是给我的：转发已由 find 负责，这里什么都不做
			const nebula::uuid from = nebula::uuid::to_uuid(event.value("from", ""));
			const nebula::uuid channel = nebula::uuid::to_uuid(event.value("channel", ""));
			if (!from || !channel) return;
			std::lock_guard<std::mutex> lk(m_push_mutex);
			m_channel[channel].insert(from);          // 通道 → 订阅者t推送时按通道取人）
			push_state& state = m_subscribers[from];  // 订阅者 → 状态
			state.channel.insert(channel);
			state.fail = 0;                           // 主动订阅视为一次存活刷新
		}

		void post::exec_omit(const nebula::json& event)
		{
			if (!this_post(event)) return;
			const nebula::uuid from = nebula::uuid::to_uuid(event.value("from", ""));
			const nebula::uuid channel = nebula::uuid::to_uuid(event.value("channel", ""));
			if (!from || !channel) return;
			std::lock_guard<std::mutex> lk(m_push_mutex);
			auto ic = m_channel.find(channel);
			if (ic != m_channel.end())
			{
				ic->second.erase(from);
				if (ic->second.empty()) { m_channel.erase(ic); }
			}
			auto is = m_subscribers.find(from);
			if (is != m_subscribers.end())
			{
				is->second.channel.erase(channel);
				is->second.fail = 0;
				if (is->second.channel.empty()) { m_subscribers.erase(is); }
			}
		}

		void post::exec_push(const nebula::json& event)
		{
			if (!this_post(event)) return;
			const nebula::uuid from = nebula::uuid::to_uuid(event.value("from", ""));
			const nebula::uuid channel = nebula::uuid::to_uuid(event.value("channel", ""));
			if (!channel) return;
			this->care_hander(event.value("data", nebula::json::object()), from, channel);
		}

		text post::exec_event(const nebula::json& event)
		{
			text type = find::exec_event(event);
			if (type == "") {}
			else if (type == "care") { exec_care(event); type = ""; }
			else if (type == "omit") { exec_omit(event); type = ""; }
			else if (type == "push") { exec_push(event); type = ""; }
			return type;   // 再往上ttool 的 logic/proxy/claim/message）
		}

		// ------------------------------------------------ 维护t心跳顺带） ----------------

		void post::exec_dath(const nebula::json& event)
		{
			find::exec_dath(event);   // 图上老化t时长老化 + 半径收缩都挂在心跳驱动上）
			std::vector<nebula::uuid> drop;
			{
				std::lock_guard<std::mutex> lk(m_push_mutex);
				for (const auto& kv : m_subscribers)
				{
					if (kv.second.fail >= m_max_fail || kv.second.channel.empty())
					{
						drop.push_back(kv.first);   // 投不出去 / 已无通道 → 摘掉
					}
				}
				for (const auto& id : drop)
				{
					auto is = m_subscribers.find(id);
					if (is == m_subscribers.end()) continue;
					for (const auto& ch : is->second.channel)
					{
						auto ic = m_channel.find(ch);
						if (ic == m_channel.end()) continue;
						ic->second.erase(id);
						if (ic->second.empty()) { m_channel.erase(ic); }
					}
					m_subscribers.erase(is);
				}
			}
		}

		// ------------------------------------------------ 对外 ----------------

		void post::care(const nebula::uuid& owner, const nebula::uuid& channel)
		{
			// 订阅者的动作：信封 goal = 拥有者；沿途逐跳转发，到拥有者后由它登记我
			send_goal(owner, make_post("care", owner, channel));
		}

		void post::omit(const nebula::uuid& owner, const nebula::uuid& channel)
		{
			send_goal(owner, make_post("omit", owner, channel));
		}

		void post::push(const nebula::json& data, const nebula::uuid& subscriber, const nebula::uuid& channel)
		{
			if (send_goal(subscriber, make_post("push", subscriber, channel, data))) return;
			std::lock_guard<std::mutex> lk(m_push_mutex);
			auto is = m_subscribers.find(subscriber);
			if (is != m_subscribers.end()) { is->second.fail += 1; }
		}

		xhqm::size post::publish(const nebula::json& data, const nebula::uuid& channel)
		{
			const std::vector<nebula::uuid> list = this->subscribers(channel);
			xhqm::size sent = 0;
			for (const auto& subscriber : list)
			{
				if (send_goal(subscriber, make_post("push", subscriber, channel, data)))
				{
					++sent;
					continue;
				}
				std::lock_guard<std::mutex> lk(m_push_mutex);
				auto is = m_subscribers.find(subscriber);
				if (is != m_subscribers.end()) { is->second.fail += 1; }
			}
			return sent;
		}

		std::vector<nebula::uuid> post::subscribers(const nebula::uuid& channel) const
		{
			std::lock_guard<std::mutex> lk(m_push_mutex);
			std::vector<nebula::uuid> list;
			auto it = m_channel.find(channel);
			if (it != m_channel.end()) { list.assign(it->second.begin(), it->second.end()); }
			return list;
		}
	}
}
