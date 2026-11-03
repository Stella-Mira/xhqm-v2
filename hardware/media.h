#ifndef XHQM_HARDWARE_MEDIA
#define XHQM_HARDWARE_MEDIA

// ============================================================
// 音视频设备抽象层（D13 落地：xhqm 只定义，不做真实现）
//
// 抽象范围：
//   kind / dir / media_type   设备类别与方向语义
//   audio_format/video_format 能力格式描述（json 往返）
//   caps                      设备能力描述（全局命名 + 主机 + 格式列表）
//   device                    设备接口：capability/open/close/good/stream_url
//   device_list               枚举本机设备（实现层提供，本层只定义签名）
//
// 不定义（实现层职责）：
//   字节搬运（read/write 与 MediaMTX 桥接）、平台枚举与采集实现
//   （ALSA/WASAPI/AVFoundation、V4L2/DShow 等，逐平台落地，低功耗设备可只实现子集）
// 设备名约定：mic.<host>.* / cam.<host>.* / spk.<host>.* / scr.<host>.* /
//             v.mix.<name> / v.compose.<name> / v.vloop.<name>（虚拟设备）
// ============================================================

#include "../head.h"
#include "../algebra/itype.h"   // xhqm::u32 等基础整数类型（独立编译必需，实测缺它 enum 声明报错）

#include <functional>

namespace xhqm
{
	namespace hardware
	{
		namespace media
		{
			// 本层 json 别名（head.h 不提供全局 json 别名；与 nebula::json 同源）
			using json = nlohmann::json;

			// ---- 类别与方向 ----

			enum class kind : xhqm::u32
			{
				none = 0,
				mic = 1,        // 麦克风（input · audio）
				cam = 2,        // 摄像头（input · video）
				spk = 3,        // 音响（output · audio）
				scr = 4,        // 屏幕（input 采集 / output 显示 · video）
				mix = 5,        // 虚拟：混音器（多路音频 → 一路）
				compose = 6,    // 虚拟：画面合成（多路视频 → 一路）
				vloop = 7,      // 虚拟：声卡桥（流 ⇄ 虚拟声卡，驱动不自研）
			};

			enum class dir : xhqm::u32
			{
				none = 0,
				input = 1,      // 采集进系统（mic/cam/scr 采集）
				output = 2,     // 系统播出去（spk/scr 显示）
				duplex = 3,     // 双向（scr 常见；vloop 桥接）
			};

			enum class media_type : xhqm::u32
			{
				none = 0,
				audio = 1,
				video = 2,
			};

			inline const char* kind_text(kind k) noexcept
			{
				switch (k)
				{
				case kind::mic: return "mic";
				case kind::cam: return "cam";
				case kind::spk: return "spk";
				case kind::scr: return "scr";
				case kind::mix: return "mix";
				case kind::compose: return "compose";
				case kind::vloop: return "vloop";
				case kind::none:
				default: return "none";
				}
			}

			inline const char* dir_text(dir d) noexcept
			{
				switch (d)
				{
				case dir::input: return "input";
				case dir::output: return "output";
				case dir::duplex: return "duplex";
				case dir::none:
				default: return "none";
				}
			}

			// ---- 格式描述（能力协商单位；json 往返供组网发布）----

			struct audio_format
			{
				xhqm::u32 rate = 16000;          // 采样率 Hz
				xhqm::u32 channels = 1;          // 通道数
				xhqm::text codec = "pcm_s16le";  // 编码（PCM 裸流为主）

				json to_json() const
				{
					return json{ { "rate", rate }, { "channels", channels }, { "codec", codec } };
				}
				bool from_json(const json& j)
				{
					if (!j.is_object()) return false;
					rate = j.value("rate", rate);
					channels = j.value("channels", channels);
					codec = j.value("codec", codec);
					return true;
				}
			};

			struct video_format
			{
				xhqm::u32 width = 1920;          // 分辨率宽
				xhqm::u32 height = 1080;         // 分辨率高
				xhqm::u32 fps = 30;              // 帧率
				xhqm::text codec = "h264";       // 编码（H264 Annex-B 为主）

				json to_json() const
				{
					return json{ { "width", width }, { "height", height }, { "fps", fps }, { "codec", codec } };
				}
				bool from_json(const json& j)
				{
					if (!j.is_object()) return false;
					width = j.value("width", width);
					height = j.value("height", height);
					fps = j.value("fps", fps);
					codec = j.value("codec", codec);
					return true;
				}
			};

			// ---- 设备能力描述（注册/发布的元数据单位）----

			struct caps
			{
				xhqm::text id;      // 全局名："cam.3090.0" / "v.mix.living"
				xhqm::text host;    // 所在主机名
				xhqm::text name;    // 可读名（给人看的；id 是 ASCII slug —— 设备名里的空格/括号/中文
				                    // 在命令行与跨编码传递里会出事，见 stella AGENT.md §3.10）
				kind k = kind::none;
				dir d = dir::none;
				std::vector<media_type> medias;              // 支持的媒体类型
				std::vector<audio_format> audios;            // 音频能力（audio 设备有效）
				std::vector<video_format> videos;            // 视频能力（video 设备有效）

				bool is_audio() const
				{
					for (const auto& m : medias) if (m == media_type::audio) return true;
					return false;
				}
				bool is_video() const
				{
					for (const auto& m : medias) if (m == media_type::video) return true;
					return false;
				}

				json to_json() const
				{
					json ja = json::array();
					for (const auto& a : audios) ja.push_back(a.to_json());
					json jv = json::array();
					for (const auto& v : videos) jv.push_back(v.to_json());
					json jm = json::array();
					for (const auto& m : medias)
						jm.push_back((m == media_type::audio) ? "audio" : "video");
					return json{
						{ "id", id }, { "host", host }, { "name", name },
						{ "kind", kind_text(k) }, { "dir", dir_text(d) },
						{ "medias", jm }, { "audios", ja }, { "videos", jv }
					};
				}
				bool from_json(const json& j)
				{
					if (!j.is_object()) return false;
					id = j.value("id", id);
					host = j.value("host", host);
					name = j.value("name", name);   // 老对端不发这个键 ⇒ 保持原值（空），向后兼容
					xhqm::text kt = j.value("kind", "");
					xhqm::text dt = j.value("dir", "");
					for (xhqm::u32 i = 1; i <= 7; ++i)
						if (kt == kind_text(static_cast<kind>(i))) { k = static_cast<kind>(i); break; }
					for (xhqm::u32 i = 1; i <= 3; ++i)
						if (dt == dir_text(static_cast<dir>(i))) { d = static_cast<dir>(i); break; }
					medias.clear();
					for (const auto& m : j.value("medias", json::array()))
						if (m.is_string()) medias.push_back((m.get<xhqm::text>() == "audio") ? media_type::audio : media_type::video);
					audios.clear();
					for (const auto& a : j.value("audios", json::array()))
					{
						audio_format f;
						if (f.from_json(a)) audios.push_back(f);
					}
					videos.clear();
					for (const auto& v : j.value("videos", json::array()))
					{
						video_format f;
						if (f.from_json(v)) videos.push_back(f);
					}
					return true;
				}
			};

			// ---- 设备接口（实现层继承；xhqm 只定义签名，不定义字节搬运）----

			class device
			{
			public:
				virtual ~device() = default;

				// 能力描述（注册进 nebula devices 通道的元数据）
				virtual caps capability() const = 0;

				// 打开设备（config 为协商后格式：如 {"format":{...}}；空 = 默认格式）
				virtual bool open(const json& config = json::object()) = 0;

				// 关闭设备（释放采集/播放/流）
				virtual void close() = 0;

				// 设备是否可用（已打开且工作正常）
				virtual bool good() const = 0;

				// 流地址：实现层起流（内嵌 MediaMTX）后返回 rtsp://...；
				// 未起流/不支持流式返回空——发布进 streams 通道的 url 来源
				virtual xhqm::text stream_url() const { return ""; }

				// 运行时状态快照（可观测性：status 指令/状态查询用）
				// 约定字段：{ "open": bool, "url": text, "good": bool, "caps": {...} }
				// 实现层可追加自己的字段
				virtual json status() const { return json::object(); }
			};

			// 枚举本机设备（实现层提供；xhqm 只定义签名）
			using device_list = std::function<std::vector<caps>()>;

			// ---- 热插拔监听抽象（实现层提供平台实现；xhqm 只定义签名）----
			// 平台实现：Linux=inotify/libudev、Windows=WM_DEVICECHANGE、macOS=IOKit。
			// 事件信息 json（实现层约定，收方忽略未知字段）：
			//   { "subsystem": "video4linux"|"sound"|..., "devnode": "/dev/video0",
			//     "vendor": "...", "product": "...", "serial": "..."（有则给，稳定命名用） }
			class hotplug
			{
			public:
				using on_add = std::function<void(const json& info)>;     // 设备插入（回调线程内快速返回）
				using on_remove = std::function<void(const json& info)>;  // 设备拔出

				virtual ~hotplug() = default;

				// 开始监听（幂等）；拔出回调的 info 应与插入时一致（供映射回设备 id）
				virtual bool watch(on_add add, on_remove remove) = 0;
				virtual void stop() = 0;
			};
		}
	}
}

#endif
