#ifndef NEBULA_API_CLIENT_DEVICE_CLIENT
#define NEBULA_API_CLIENT_DEVICE_CLIENT

// ============================================================
// 边缘客户端（M4）：zero 子类 + 设备注册表 + 指令订阅执行
//
// 生命周期：
//   online()   组网入网 → 登记全局通道 → 每设备注册 + 订阅指令通道
//   指令到达    执行 command_executor → acks 回执（open 成功顺带 streams up）
//   offline()  优雅下线：streams down + devices unregister + 设备 close + 组网 leave
//
// 数据面：MediaMTX 由设备实现内嵌（device_impl.h 的 open 起流占位），
//        客户端只发布 rtsp 地址元数据（D13-D15）
// ============================================================

#include "device_impl.h"
#include "linux_hotplug.h"
#include "../common/protocol.h"
#include "../../../network/nebula/zero.h"

#include <atomic>
#include <chrono>
#include <thread>

namespace nebula_api
{
	namespace client
	{
		using xhqm::nebula::json;
		using xhqm::nebula::uuid;

		class device_client : public xhqm::nebula::zero
		{
		public:
			// 组合连接句柄（link_t<kind> 由调用方创建——模板止步 link 层）；
			// mtx：内嵌 MediaMTX（设备起流数据面，D14）
			explicit device_client(std::shared_ptr<xhqm::nebula::link_b> link, mtx_server& mtx)
				: zero(std::move(link))
				, m_mtx(mtx)
			{
				bind_standard_commands(m_executor, m_registry);
			}

			// MediaMTX 访问（调用方配置端口/起停）
			mtx_server& mtx() { return m_mtx; }

			// 采集抽象：客户端节点本身不采集（采集在各注册设备内），返回空
			json collect() override { return {}; }

			// ---- 设备管理 ----
			bool add_device(std::shared_ptr<media::device> d) { return m_registry.add(std::move(d)); }
			bool remove_device(const xhqm::text& id) { return m_registry.remove(id); }
			std::vector<media::caps> devices() const { return m_registry.snapshot(); }

			// ---- 上线 ----
			// 入网 + 登记全局通道 + 每设备注册（register）+ 订阅各自指令通道
			bool online(int sse_port, int event_port, const xhqm::text& host = "0.0.0.0",
				const xhqm::nebula::addr& bcast_addr = nullptr)
			{
				if (m_online) return true;
				if (!this->listen(sse_port, event_port, host, bcast_addr)) return false;

				this->adopt(common::devices_channel(), "devices");
				this->adopt(common::streams_channel(), "streams");
				this->adopt(common::acks_channel(), "acks");

				for (const auto& c : m_registry.snapshot())
				{
					// 注册（上线即注册，D17）
					this->upload(common::devices_channel(), common::make_register(c));
					// 订阅自己的指令通道（本地消费，D16）
					uuid ch = common::command_channel(c.id);
					this->subscribe(ch, 0, [this, id = c.id](const uuid&, const json& data) {
						this->on_command(id, common::latest_message(data));
					}, {});
				}
				m_online = true;
				// 软状态周期重发（10s）：订阅扩散完成前的发布会丢（推送表未建），
				// 重发保证最终可见（PROTOCOL §5 注册/流软状态）
				m_refresh_running = true;
				m_refresh = std::thread([this] {
					while (m_refresh_running)
					{
						std::this_thread::sleep_for(std::chrono::milliseconds(10000));
						if (!m_refresh_running) break;
						refresh();
					}
				});
				// 邻居变化（入网/补连）与新订阅者出现（sub 事件到达）立即刷新注册——
				// 只靠 10s 周期目录建立慢且与订阅扩散竞态（M4 实测：register 先于
				// 订阅扩散发出即丢；sub 到达必然触发重发，竞态消除）
				m_last_deg = m_link->degree();
				m_deg_hook = m_link->add_event_hook([this](const json& e) {
					xhqm::text type = e.value("type", "");
					if (type == "sub") { refresh(); return; }
					uint d = m_link->degree();
					if (d != m_last_deg) { m_last_deg = d; refresh(); }
				});
				refresh();   // 入网即刷一次
				return true;
			}

			// ---- 优雅下线 ----
			void offline()
			{
				if (!m_online) return;
				m_refresh_running = false;
				if (m_refresh.joinable()) m_refresh.join();
				m_link->remove_event_hook(m_deg_hook);
				for (const auto& c : m_registry.snapshot())
				{
					upload(common::streams_channel(), common::make_stream_down(c.id));
					upload(common::devices_channel(), common::make_unregister(c.id));
					auto d = m_registry.find(c.id);
					if (d) d->close();
				}
				m_online = false;
				this->stop();
			}

			// 暴露执行器（业务指令扩展点，D 指令开放）
			command_executor& executor() { return m_executor; }

			// ---- 热插拔（插入自动识别注册 / 拔出自动停流注销）----
			// factory：info{subsystem,devnode} → 设备实例（真实后端工厂由使用方注入；
			// 默认工厂建 stub 设备供链路验证）。id 由 devnode 确定性派生，拔出时同算定位。
			using device_factory = std::function<std::shared_ptr<media::device>(const media::json& info)>;

			bool attach_hotplug(xhqm::hardware::media::hotplug& hp, device_factory factory = nullptr)
			{
				return hp.watch(
					[this, factory](const media::json& info) { this->on_hotplug_add(info, factory); },
					[this](const media::json& info) { this->on_hotplug_remove(info); });
			}

			// devnode → 全局设备 id（确定性：两端同算；<kind>.<host>.<节点尾名>）
			static xhqm::text device_id_from(const media::json& info)
			{
				xhqm::text dev = info.value("devnode", "");
				auto pos = dev.rfind('/');
				xhqm::text tail = (pos == xhqm::text::npos) ? dev : dev.substr(pos + 1);
				xhqm::text kind = (info.value("subsystem", "") == "video4linux") ? "cam" : "mic";
				return kind + ".local." + tail;
			}

		private:
			// 热插拔：插入 → 工厂建设备 → 注册（在线时立即发布 + 订阅指令通道）
			void on_hotplug_add(const media::json& info, const device_factory& factory)
			{
				auto d = factory ? factory(info) : make_stub_device(info);
				if (!d) return;
				xhqm::text id = d->capability().id;
				if (!m_registry.add(d)) return;
				if (!m_online) return;
				upload(common::devices_channel(), common::make_register(m_registry.find(id)->capability()));
				this->subscribe(common::command_channel(id), 0, [this, id](const uuid&, const json& data) {
					this->on_command(id, common::latest_message(data));
				}, {});
			}

			// 热插拔：拔出 → 停流注销 + 关闭移除（remove 内已 close）
			void on_hotplug_remove(const media::json& info)
			{
				xhqm::text id = device_id_from(info);
				auto d = m_registry.find(id);
				if (!d) return;
				if (m_online)
				{
					upload(common::streams_channel(), common::make_stream_down(id));
					upload(common::devices_channel(), common::make_unregister(id));
				}
				m_registry.remove(id);
			}

			// 默认工厂：stub 后端设备（链路验证用；真实使用方注入真后端工厂）
			std::shared_ptr<media::device> make_stub_device(const media::json& info) const
			{
				xhqm::text id = device_id_from(info);
				xhqm::hardware::media::audio_format af;
				af.rate = 16000; af.channels = 1;
				bool video = (info.value("subsystem", "") == "video4linux");
				return std::make_shared<mic_device>(id, "local",
					std::make_shared<stub_capture>(), m_mtx, "ffmpeg", af, video);
			}

			// 软状态刷新：重发 register（+ 已开流设备的 stream up 地址刷新）
			void refresh()
			{
				if (!m_online) return;
				for (const auto& c : m_registry.snapshot())
				{
					upload(common::devices_channel(), common::make_register(c));
					auto d = m_registry.find(c.id);
					if (d && d->good() && !d->stream_url().empty())
						upload(common::streams_channel(),
							common::make_stream_up(c.id, d->stream_url(), c.to_json()));
				}
			}

			// 指令到达（池线程）：执行 → 回执；open 成功顺带发布流地址
			void on_command(const xhqm::text& device_id, const json& cmd)
			{
				xhqm::text action = cmd.value("action", "");
				json result = m_executor.execute(action, device_id, cmd.value("params", json::object()));
				bool ok = result.value("ok", false);
				upload(common::acks_channel(), common::make_ack(cmd, ok, result, device_id));

				// open 成功：地址就绪 → streams up（D15 流 = 地址发布）
				if (ok && action == "open")
				{
					auto d = m_registry.find(device_id);
					if (d)
					{
						xhqm::text url = result.value("url", d->stream_url());
						if (!url.empty())
							upload(common::streams_channel(),
								common::make_stream_up(device_id, url, d->capability().to_json()));
					}
				}
			}

			device_registry m_registry;
			command_executor m_executor;
			mtx_server& m_mtx;   // 内嵌 MediaMTX（设备起流用）
			bool m_online = false;
			// 软状态重发线程（10s 周期）
			std::thread m_refresh;
			std::atomic<bool> m_refresh_running{ false };
			// 邻居变化钩子（立即刷新注册）
			size_t m_deg_hook = 0;
			uint m_last_deg = 0;
		};
	}
}

#endif
