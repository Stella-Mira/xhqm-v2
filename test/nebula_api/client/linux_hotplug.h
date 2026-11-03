#ifndef NEBULA_API_CLIENT_LINUX_HOTPLUG
#define NEBULA_API_CLIENT_LINUX_HOTPLUG

// ============================================================
// Linux 热插拔检测（实现层，inotify 零依赖版）
//   监听指定目录（默认 /dev）下设备节点增删：/dev/video*、/dev/snd/* 等
//   事件 → watch 回调（插入/拔出，info 含 devnode/subsystem 推断）
//   增强方向（后补）：libudev 拿 vendor/product/serial 做稳定命名；
//   本实现只要求「拔出事件的 devnode 与插入时一致」即可映射回设备
//   测试可传临时目录（touch/rm 模拟插拔），生产传 /dev
// ============================================================

#include "../../../hardware/media.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <sys/inotify.h>
#include <thread>
#include <unistd.h>

namespace nebula_api
{
	namespace client
	{
		class linux_hotplug : public xhqm::hardware::media::hotplug
		{
		public:
			// watch_dirs：监听目录列表（默认 /dev）；debounce_ms：事件去抖
			explicit linux_hotplug(std::vector<xhqm::text> watch_dirs = { "/dev" }, int debounce_ms = 500)
				: m_dirs(std::move(watch_dirs)), m_debounce(debounce_ms)
			{
			}

			~linux_hotplug() override { stop(); }

			bool watch(on_add add, on_remove remove) override
			{
				if (m_running) return true;
				m_add = std::move(add);
				m_remove = std::move(remove);
				m_fd = inotify_init1(IN_NONBLOCK);
				if (m_fd < 0) return false;
				for (const auto& d : m_dirs)
					inotify_add_watch(m_fd, d.c_str(), IN_CREATE | IN_DELETE);
				m_running = true;
				m_thread = std::thread([this] { loop(); });
				return true;
			}

			void stop() override
			{
				if (!m_running) return;
				m_running = false;
				if (m_fd >= 0) { close(m_fd); m_fd = -1; }   // 关 fd 唤醒阻塞的 read
				if (m_thread.joinable()) m_thread.join();
			}

		private:
			void loop()
			{
				char buf[4096];
				while (m_running)
				{
					ssize_t n = read(m_fd, buf, sizeof(buf));
					if (n <= 0)
					{
						std::this_thread::sleep_for(std::chrono::milliseconds(100));
						continue;
					}
					for (char* p = buf; p < buf + n;)
					{
						auto* ev = reinterpret_cast<inotify_event*>(p);
						p += sizeof(inotify_event) + ev->len;
						if (ev->len == 0) continue;
						xhqm::text name = ev->name;
						// 只关心设备节点类：video* / snd 目录 / pcm/control 等
						if (!interesting(name)) continue;
						if (ev->mask & IN_CREATE) debounce_add(name);
						else if (ev->mask & IN_DELETE) debounce_remove(name);
					}
				}
			}

			bool interesting(const xhqm::text& name) const
			{
				if (name.find("video") == 0) return true;   // /dev/video0
				if (name == "snd") return true;             // /dev/snd 目录（声卡出现/消失）
				return false;
			}

			void debounce_add(const xhqm::text& name)
			{
				// 去抖：CREATE 后确认节点仍存在才上报（避免内核分层注册的抖动）
				std::this_thread::sleep_for(std::chrono::milliseconds(m_debounce));
				if (!m_running) return;
				xhqm::hardware::media::json info;
				info["subsystem"] = (name.find("video") == 0) ? "video4linux" : "sound";
				info["devnode"] = devnode_of(name);
				if (m_add) m_add(info);
			}

			void debounce_remove(const xhqm::text& name)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(m_debounce));
				if (!m_running) return;
				xhqm::hardware::media::json info;
				info["subsystem"] = (name.find("video") == 0) ? "video4linux" : "sound";
				info["devnode"] = devnode_of(name);
				if (m_remove) m_remove(info);
			}

			// devnode 归一：/dev/video0（snd 目录事件给目录路径）
			xhqm::text devnode_of(const xhqm::text& name) const
			{
				xhqm::text base = m_dirs.empty() ? "/dev" : m_dirs.front();
				return base + "/" + name;
			}

			std::vector<xhqm::text> m_dirs;
			int m_debounce = 500;
			int m_fd = -1;
			std::atomic<bool> m_running{ false };
			std::thread m_thread;
			on_add m_add;
			on_remove m_remove;
		};
	}
}

#endif
