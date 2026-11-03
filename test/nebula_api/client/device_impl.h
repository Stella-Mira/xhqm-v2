#ifndef NEBULA_API_CLIENT_DEVICE_IMPL
#define NEBULA_API_CLIENT_DEVICE_IMPL

// ============================================================
// 实现层设备骨架（nebula_api/client，M4 预演）
//
// 扩展性设计（本文件的核心命题）：
//   1. 新平台（ALSA/WASAPI/AVFoundation/V4L2/DShow...）
//      → 新 capture_backend 子类，设备类与注册表零改动
//   2. 新设备类型（新 kind 或组合设备）
//      → xhqm::hardware::media::kind 加枚举值 + 新 device 子类（组合已有后端），
//        注册表只认 device 基类，零改动
//   3. 新指令（open/close/set_param/自定义）
//      → command_executor::bind 注册新 handler，分发器零改动
//   4. caps 扩展字段
//      → json 承载，旧端忽略未知字段（组网上线自动兼容）
//
// 分层：抽象（xhqm hardware/media.h）→ 本文件设备骨架 → 平台后端 → 组网发布
// ============================================================

#include "../../../hardware/media.h"
#include "mtx_server.h"

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace nebula_api
{
	namespace client
	{
		namespace media = xhqm::hardware::media;   // 命名空间别名

		// ============================================================
		// 1. 设备实现基类：统一生命周期与流地址（实现层复用，非抽象层职责）
		// ============================================================

		class device_basic : public media::device
		{
		protected:
			media::caps m_caps;         // 能力描述（构造时定死，不可变）
			bool m_open = false;        // 打开状态
			xhqm::text m_url;           // 起流后的 MediaMTX 地址（发布用）

		public:
			explicit device_basic(media::caps caps)
				: m_caps(std::move(caps))
			{
			}

			media::caps capability() const override { return m_caps; }
			bool good() const override { return m_open; }
			xhqm::text stream_url() const override { return m_url; }

		protected:
			void set_open(bool o) { m_open = o; }
			void set_url(xhqm::text u) { m_url = std::move(u); }
		};

		// ============================================================
		// 2. 采集/播放后端（平台相关，可插拔）
		//    新平台 = 新后端子类；设备类不感知平台细节
		// ============================================================

		// 一帧数据（时间戳纳秒，与 astral 对齐）
		struct frame
		{
			std::vector<xhqm::byte> bytes;   // PCM / H264 Annex-B 裸帧
			xhqm::u64 ts_ns = 0;             // 采集时刻（纳秒）
		};

		// 采集后端接口（input 类设备：mic/cam/screen 采集）
		class capture_backend
		{
		public:
			virtual ~capture_backend() = default;

			// 按协商格式启动采集（fmt：audio_format/video_format 的 json）
			virtual bool start(const media::json& fmt) = 0;
			virtual void stop() = 0;

			// 读一帧（阻塞或超时返回 false 表示当前无帧/已停）
			virtual bool read(frame& out) = 0;
		};

		// 桩采集后端（链路验证/默认工厂用：启动即就绪，无真实数据）
		class stub_capture : public capture_backend
		{
		public:
			bool start(const media::json&) override { return true; }
			void stop() override {}
			bool read(frame&) override { return false; }
		};

		// 播放后端接口（output 类设备：spk/screen 播放）
		class render_backend
		{
		public:
			virtual ~render_backend() = default;

			virtual bool start(const media::json& fmt) = 0;
			virtual void stop() = 0;

			// 播一帧（false = 缓冲满/失败）
			virtual bool write(const frame& in) = 0;
		};

		// ============================================================
		// 3. 示例设备：麦克风（组合采集后端 + MediaMTX 起流占位）
		// ============================================================

		class mic_device : public device_basic
		{
			std::shared_ptr<capture_backend> m_backend;   // 平台后端（注入，可换）
			mtx_server& m_mtx;                            // 内嵌 MediaMTX（client 持有，多设备共享）
			xhqm::text m_ffmpeg;                          // ffmpeg 可执行文件路径
			bool m_video = false;                         // 视频设备（cam 类）推 testsrc；音频推 sine
			stream_publisher m_pub;                       // 推流子进程（本阶段 lavfi 测试源）

		public:
			mic_device(xhqm::text id, xhqm::text host, std::shared_ptr<capture_backend> backend,
				mtx_server& mtx, xhqm::text ffmpeg = "ffmpeg", media::audio_format fmt = {}, bool video = false)
				: device_basic(make_caps(std::move(id), std::move(host), fmt, video))
				, m_backend(std::move(backend))
				, m_mtx(mtx)
				, m_ffmpeg(std::move(ffmpeg))
				, m_video(video)
			{
			}

			// 打开：协商格式 → 后端启动 → MediaMTX 推流 → 真实地址
			bool open(const media::json& config = media::json::object()) override
			{
				if (m_open) return true;
				if (!m_backend) return false;
				media::json cfg = config.is_object() ? config : media::json::object();   // null/非对象防御
				media::json fmt = cfg.value("format", media::json::object());
				if (fmt.empty() && !m_caps.audios.empty())
					fmt = m_caps.audios.front().to_json();
				if (!m_backend->start(fmt)) return false;
				if (!m_mtx.start()) return false;
				if (!m_pub.start(m_ffmpeg, m_mtx, m_caps.id, m_video))
				{
					if (m_backend) m_backend->stop();
					return false;
				}
				set_url(m_mtx.url(m_caps.id));   // 真实 rtsp 地址（D15 发布用）
				set_open(true);
				return true;
			}

			void close() override
			{
				if (!m_open) return;
				m_pub.stop();
				if (m_backend) m_backend->stop();
				set_url("");
				set_open(false);
			}

		private:
			static media::caps make_caps(xhqm::text id, xhqm::text host, media::audio_format fmt, bool video)
			{
				media::caps c;
				c.id = std::move(id);
				c.host = std::move(host);
				c.k = video ? media::kind::cam : media::kind::mic;
				c.d = media::dir::input;
				c.medias = { video ? media::media_type::video : media::media_type::audio };
				if (!video && fmt.rate > 0) c.audios.push_back(fmt);
				return c;
			}
		};

		// ============================================================
		// 4. 设备注册表：客户端设备表（只认 device 基类——新设备类型零改动）
		// ============================================================

		class device_registry
		{
			std::map<xhqm::text, std::shared_ptr<media::device>> m_devices;
			mutable std::mutex m_mutex;

		public:
			// 注册设备（id 冲突返回 false）
			bool add(std::shared_ptr<media::device> d)
			{
				if (!d) return false;
				std::lock_guard<std::mutex> lk(m_mutex);
				const auto& id = d->capability().id;
				if (id.empty() || m_devices.find(id) != m_devices.end()) return false;
				m_devices.emplace(id, std::move(d));
				return true;
			}

			bool remove(const xhqm::text& id)
			{
				std::lock_guard<std::mutex> lk(m_mutex);
				auto it = m_devices.find(id);
				if (it == m_devices.end()) return false;
				it->second->close();
				m_devices.erase(it);
				return true;
			}

			std::shared_ptr<media::device> find(const xhqm::text& id) const
			{
				std::lock_guard<std::mutex> lk(m_mutex);
				auto it = m_devices.find(id);
				return (it != m_devices.end()) ? it->second : nullptr;
			}

			// 设备快照：注册进 nebula devices 通道的元数据
			std::vector<media::caps> snapshot() const
			{
				std::vector<media::caps> out;
				std::lock_guard<std::mutex> lk(m_mutex);
				for (const auto& kv : m_devices)
					out.push_back(kv.second->capability());
				return out;
			}

			// 按类别过滤（上层按需枚举）
			std::vector<media::caps> by_kind(media::kind k) const
			{
				std::vector<media::caps> out;
				for (const auto& c : snapshot())
					if (c.k == k) out.push_back(c);
				return out;
			}
		};

		// ============================================================
		// 5. 指令执行器：动作注册表（新指令 = bind 新 handler，分发器零改动）
		// ============================================================

		class command_executor
		{
		public:
			// handler：(device_id 通道上下文, params) → 结果(json)；抛异常 = 执行失败
			using handler = std::function<media::json(const xhqm::text& device_id, const media::json& params)>;

			// 注册动作处理器（重复绑定 = 覆盖）
			command_executor& bind(xhqm::text action, handler h)
			{
				std::lock_guard<std::mutex> lk(m_mutex);
				m_handlers[std::move(action)] = std::move(h);
				return *this;
			}

			// 执行：查表分发；未注册动作返回 {ok:false, error:"unknown_action"}
			media::json execute(const xhqm::text& action, const xhqm::text& device_id,
				const media::json& params) const
			{
				handler h;
				{
					std::lock_guard<std::mutex> lk(m_mutex);
					auto it = m_handlers.find(action);
					if (it == m_handlers.end())
						return media::json{ { "ok", false }, { "error", "unknown_action" } };
					h = it->second;
				}
				try
				{
					media::json r = h(device_id, params);
					if (!r.is_object()) r = media::json::object();
					if (!r.contains("ok")) r["ok"] = true;
					return r;
				}
				catch (const std::exception& e)
				{
					return media::json{ { "ok", false }, { "error", e.what() } };
				}
			}

		private:
			std::map<xhqm::text, handler> m_handlers;
			mutable std::mutex m_mutex;
		};

		// ============================================================
		// 6. 标准指令绑定（open/close/list——设备控制的最小集；
		//    业务指令由使用方继续 bind，扩展点不封闭）
		// ============================================================

		inline void bind_standard_commands(command_executor& ex, device_registry& reg)
		{
			// open {format?}——设备由指令通道上下文（device_id）确定
			ex.bind("open", [&reg](const xhqm::text& device_id, const media::json& p) -> media::json {
				auto d = reg.find(device_id);
				if (!d) return { { "ok", false }, { "error", "no_such_device" } };
				media::json fmt = p.value("format", media::json::object());
				bool ok = d->open(fmt);
				media::json r{ { "ok", ok } };
				if (ok)
				{
					r["url"] = d->stream_url();
					r["caps"] = d->capability().to_json();
				}
				return r;
			});

			// close {}——设备由通道上下文确定
			ex.bind("close", [&reg](const xhqm::text& device_id, const media::json&) -> media::json {
				auto d = reg.find(device_id);
				if (!d) return { { "ok", false }, { "error", "no_such_device" } };
				d->close();
				return { { "ok", true } };
			});

			// list {} → 本客户端全部设备能力快照
			ex.bind("list", [&reg](const xhqm::text&, const media::json&) -> media::json {
				media::json arr = media::json::array();
				for (const auto& c : reg.snapshot()) arr.push_back(c.to_json());
				return { { "ok", true }, { "devices", std::move(arr) } };
			});
		}

	}   // namespace client
}   // namespace nebula_api

#endif
