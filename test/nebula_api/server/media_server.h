#ifndef NEBULA_API_SERVER_MEDIA_SERVER
#define NEBULA_API_SERVER_MEDIA_SERVER

// ============================================================
// 中枢服务端（M4）：data 子类 + 设备目录 + 指令下发
//
// 职责：
//   设备目录    订阅 devices 通道累积（register/unregister；条目 60s 未刷新即过期）
//   流可见      订阅 streams 通道（最近流地址缓存，供上层查询）
//   指令下发    command()：publish 到 commands/<device_id> → 等 acks 回执
//               （3s 超时重发 ×3，仍无回执 = 设备离线）
//   兜底合成    低功耗设备声明后服务端代合成（M5，接口预留）
// ============================================================

#include "../common/protocol.h"

#include <condition_variable>
#include <future>
#include <map>
#include <mutex>

namespace nebula_api
{
	namespace server
	{
		using xhqm::nebula::json;
		using xhqm::nebula::uuid;
		namespace media = xhqm::hardware::media;   // 命名空间别名

		class media_server : public xhqm::nebula::data
		{
		public:
			explicit media_server(std::shared_ptr<xhqm::nebula::link_b> link)
				: data(std::move(link))
			{
			}

			json collect() override { return {}; }   // 中枢节点：无采集

			// ---- 上线 ----
			bool online(int sse_port, int event_port, const xhqm::text& host = "0.0.0.0",
				const xhqm::nebula::addr& bcast_addr = nullptr)
			{
				if (m_online) return true;
				if (!this->listen(sse_port, event_port, host, bcast_addr)) return false;

				adopt(common::devices_channel(), "devices");
				adopt(common::streams_channel(), "streams");
				adopt(common::acks_channel(), "acks");

				// devices：维护设备目录（多发布者通道：快照逐条处理，register/unregister 累积）
				subscribe(common::devices_channel(), 0, [this](const uuid&, const json& data) {
					this->on_devices(data);
				}, {});
				// streams：缓存最近流地址（多发布者：快照逐条）
				subscribe(common::streams_channel(), 0, [this](const uuid&, const json& data) {
					this->on_streams(data);
				}, {});
				// acks：匹配待决指令（多发布者：快照逐条）
				subscribe(common::acks_channel(), 0, [this](const uuid&, const json& data) {
					this->on_acks(data);
				}, {});

				m_online = true;
				return true;
			}

			void offline()
			{
				if (!m_online) return;
				m_online = false;
				this->stop();
			}

			// ---- 设备目录（查询时过滤 60s 未刷新条目）----
			std::vector<media::caps> devices() const
			{
				std::vector<media::caps> out;
				xhqm::u64 now = xhqm::timesys::ntime();
				std::lock_guard<std::mutex> lk(m_mutex);
				for (const auto& kv : m_directory)
				{
					if (now - kv.second.second > 60ull * 1000000000ull) continue;   // 60s 过期
					out.push_back(kv.second.first);
				}
				return out;
			}

			// 某设备是否在线
			bool has_device(const xhqm::text& device_id) const
			{
				for (const auto& c : devices())
					if (c.id == device_id) return true;
				return false;
			}

			// 最近流地址（streams 缓存）
			xhqm::text stream_url(const xhqm::text& device_id) const
			{
				std::lock_guard<std::mutex> lk(m_mutex);
				auto it = m_streams.find(device_id);
				return (it != m_streams.end()) ? it->second : "";
			}

			// ---- 指令下发（回执等待；3s 超时重发 ×3）----
			json command(const xhqm::text& device_id, const xhqm::text& action,
				const json& params, int timeout_ms = 3000)
			{
				json cmd = common::make_command(action, params);
				xhqm::text cmd_id = cmd.value("id", "");
				uuid ch = common::command_channel(device_id);

				auto prom = std::make_shared<std::promise<json>>();
				auto fut = prom->get_future();
				{
					std::lock_guard<std::mutex> lk(m_mutex);
					m_pending[cmd_id] = prom;
				}

				json ack;
				for (int attempt = 0; attempt < 3; ++attempt)
				{
					publish(ch, cmd);
					if (fut.wait_for(std::chrono::milliseconds(timeout_ms)) == std::future_status::ready)
					{
						ack = fut.get();
						break;
					}
				}
				{
					std::lock_guard<std::mutex> lk(m_mutex);
					m_pending.erase(cmd_id);
				}
				if (ack.is_null())
					return json{ { "ok", false }, { "error", "timeout" } };
				// 直返执行结果（result 内含 ok/url/error）；ack 外壳（cmd_id/device/ts）是传输层
				return ack.value("result", json::object());
			}

		private:
			// 快照逐条处理：多发布者通道下每个发布者的窗口独立，
			// 只取最后一条会把不同设备互相覆盖（M4 实测目录永远只剩一个设备）
			template<class F>
			void for_each_message(const json& pushed, F&& f)
			{
				if (pushed.is_array())
				{
					for (const auto& item : pushed)
					{
						if (!item.is_object() || !item.contains("json")) continue;
						f(item["json"]);
					}
				}
				else if (pushed.is_object())
				{
					f(pushed);
				}
			}

			// devices 通道消息：register/unregister 维护目录
			void on_devices(const json& pushed)
			{
				for_each_message(pushed, [this](const json& msg) {
					xhqm::text op = msg.value("op", "");
					if (op == "register")
					{
						media::caps c;
						if (!c.from_json(msg.value("caps", json::object()))) return;
						// 注意：key 必须先取出——m_directory[c.id] = {move(c),...}
						// 右侧先求值会把 c.id move 走，key 变空串（M4 实测两设备互相覆盖）
						xhqm::text id = c.id;
						std::lock_guard<std::mutex> lk(m_mutex);
						m_directory[std::move(id)] = { std::move(c), xhqm::timesys::ntime() };
					}
					else if (op == "unregister")
					{
						std::lock_guard<std::mutex> lk(m_mutex);
						m_directory.erase(msg.value("device", ""));
					}
				});
			}

			// streams 通道消息：up/down 维护流地址缓存
			void on_streams(const json& pushed)
			{
				for_each_message(pushed, [this](const json& msg) {
					xhqm::text op = msg.value("op", "");
					xhqm::text device = msg.value("device", "");
					if (device.empty()) return;
					std::lock_guard<std::mutex> lk(m_mutex);
					if (op == "up") m_streams[device] = msg.value("url", "");
					else if (op == "down") m_streams.erase(device);
				});
			}

			// acks 通道消息：匹配待决指令并放行等待者
			void on_acks(const json& pushed)
			{
				for_each_message(pushed, [this](const json& msg) {
					xhqm::text cmd_id = msg.value("cmd_id", "");
					if (cmd_id.empty()) return;
					std::lock_guard<std::mutex> lk(m_mutex);
					auto it = m_pending.find(cmd_id);
					if (it == m_pending.end()) return;
					it->second->set_value(msg);
				});
			}

			// 设备目录：id → {caps, 最近 register 时间（纳秒）}
			std::map<xhqm::text, std::pair<media::caps, xhqm::u64>> m_directory;
			// 流地址缓存：device_id → url
			std::map<xhqm::text, xhqm::text> m_streams;
			// 待决指令：cmd_id → promise（acks 到达时放行）
			std::map<xhqm::text, std::shared_ptr<std::promise<json>>> m_pending;
			mutable std::mutex m_mutex;
			bool m_online = false;
		};
	}
}

#endif
