#ifndef NEBULA_API_CLIENT_MTX_SERVER
#define NEBULA_API_CLIENT_MTX_SERVER

// ============================================================
// MediaMTX 子进程管理（D14：内嵌自包含，每客户端一个实例）
//   start()：fork+exec 启动，轮询端口就绪（最多 5s）
//   stop() ：SIGTERM + waitpid 回收
//   url()  ：rtsp://<host>:<port>/<path> —— 设备起流后的发布地址
// 数据面字节流全部在 MediaMTX 内，xhqm 只发布地址元数据（D15）
// ============================================================

#include "../../../network/nebula/data.h"

#include <chrono>
#include <csignal>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace nebula_api
{
	namespace client
	{
		class mtx_server
		{
		public:
			// binary：mediamtx 可执行文件路径；port：RTSP 端口；host：流地址对外主机
			mtx_server(xhqm::text binary, unsigned short port = 8554, xhqm::text host = "127.0.0.1")
				: m_binary(std::move(binary)), m_port(port), m_host(std::move(host))
			{
			}

			~mtx_server() { stop(); }

			mtx_server(const mtx_server&) = delete;
			mtx_server& operator=(const mtx_server&) = delete;

			// 启动子进程并等端口就绪（失败返回 false；已启动幂等 true）
			bool start()
			{
				if (running()) return true;
				m_pid = fork();
				if (m_pid == 0)
				{
					// 子进程：切到二进制目录（mediamtx.yml 与其同目录），
					// 否则按测试进程 CWD 找 yml、paths 配置缺失推流 400（实测）；
					// 端口经环境变量覆盖（MTX_ 前缀 + 大写配置路径），
					// 否则所有实例绑 yml 默认 8554 互相冲突（实测）
					auto pos = m_binary.rfind('/');
					if (pos != xhqm::text::npos)
					{
						xhqm::text dir = m_binary.substr(0, pos);
						if (chdir(dir.c_str()) != 0) {}
					}
					xhqm::text rtsp = ":" + std::to_string(m_port);
					setenv("MTX_RTSPADDRESS", rtsp.c_str(), 1);
					// 多实例必须错开 UDP/RTP 端口（默认全绑 :8000/:8001，实测冲突实例挂起）；
					// 本套件统一走 TCP 拉流（-rtsp_transport tcp），禁用 UDP/multicast 最省
					setenv("MTX_RTSPTRANSPORTS", "tcp", 1);
					execl(m_binary.c_str(), m_binary.c_str(), nullptr);
					_exit(127);   // exec 失败
				}
				if (m_pid < 0) return false;
				// 等端口就绪（最多 5s）
				for (int i = 0; i < 50; ++i)
				{
					if (port_open()) return true;
					if (child_exited()) return false;
					std::this_thread::sleep_for(std::chrono::milliseconds(100));
				}
				return false;
			}

			void stop()
			{
				if (m_pid > 0)
				{
					kill(m_pid, SIGTERM);
					waitpid(m_pid, nullptr, 0);
					m_pid = -1;
				}
			}

			bool running() const { return (m_pid > 0) && !child_exited(); }

			unsigned short port() const { return m_port; }

			// 流发布地址：rtsp://<host>:<port>/<path>
			xhqm::text url(const xhqm::text& path) const
			{
				return "rtsp://" + m_host + ":" + std::to_string(m_port) + "/" + path;
			}

		private:
			// 端口是否已监听（连一下 127.0.0.1:port）
			bool port_open() const
			{
				xhqm::network::tcp_client c;
				return c.connect_timeout("127.0.0.1", (int)m_port, 300);
			}

			bool child_exited() const
			{
				if (m_pid <= 0) return true;
				int status = 0;
				pid_t r = waitpid(m_pid, &status, WNOHANG);
				return (r == m_pid);
			}

			xhqm::text m_binary;
			unsigned short m_port = 8554;
			xhqm::text m_host;
			pid_t m_pid = -1;
		};

		// ============================================================
		// 推流封装：ffmpeg 子进程把（测试源/采集后端）流推进 MediaMTX
		// 本阶段用 lavfi 测试源模拟采集（sine 音源/testsrc 视频）；
		// 真实采集后端接入时替换 input 参数为管道/设备输入
		// ============================================================

		class stream_publisher
		{
		public:
			// 推流到 mtx 的指定 path：lavfi 音源（mic 类）或 testsrc（cam 类）
			bool start(const xhqm::text& ffmpeg, const mtx_server& mtx, const xhqm::text& path, bool video)
			{
				if (m_pid > 0) return false;
				m_pid = fork();
				if (m_pid == 0)
				{
					xhqm::text rtsp = mtx.url(path);

					if (video)
					{
						// 视频测试源 320x240@10（H264 实时档）
						execlp(ffmpeg.c_str(), ffmpeg.c_str(),
							"-hide_banner", "-loglevel", "warning",
							"-re", "-f", "lavfi", "-i", "testsrc=size=320x240:rate=10",
							"-c:v", "libx264", "-preset", "ultrafast", "-tune", "zerolatency",
							"-rtsp_transport", "tcp", "-f", "rtsp", rtsp.c_str(), nullptr);
					}
					else
					{
						// 音频测试源 16k 单声道正弦（模拟麦克风 PCM）
						execlp(ffmpeg.c_str(), ffmpeg.c_str(),
							"-hide_banner", "-loglevel", "warning",
							"-re", "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=16000",
							"-ac", "1", "-c:a", "aac",
							"-rtsp_transport", "tcp", "-f", "rtsp", rtsp.c_str(), nullptr);
					}
					_exit(127);
				}
				if (m_pid < 0) return false;
				// 等流可读（最多 5s：连接 mtx 路径应建立）
				std::this_thread::sleep_for(std::chrono::milliseconds(1500));
				return alive();
			}

			void stop()
			{
				if (m_pid > 0)
				{
					kill(m_pid, SIGTERM);
					waitpid(m_pid, nullptr, 0);
					m_pid = -1;
				}
			}

			bool alive() const
			{
				if (m_pid <= 0) return false;
				int status = 0;
				return (waitpid(m_pid, &status, WNOHANG) != m_pid);
			}

			~stream_publisher() { stop(); }

		private:
			pid_t m_pid = -1;
		};
	}
}

#endif
