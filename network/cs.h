#ifndef XHQM_NETWORK_CS
#define XHQM_NETWORK_CS

// 通用 C/S（客户端/服务端）模式模板
// 帧级会话 + 请求-响应，传输层可替换（TCP / 串口 / ...）
//
// 会话 concept（一条已建立的连接/链路）：
//   bool send(const std::string& data)   发送一帧
//   bool recv(std::string& data)         接收一帧
//   void close()
//   bool good() const
//
// 服务端 concept（能监听并 accept 出会话）：
//   bool listen(...)                     监听
//   session_type* accept()               接受一条连接，返回会话（调用方负责 delete）
//   void close()
//
// 客户端 concept（能连到服务端并收发）：
//   bool connect(...)                    连接
//   send / recv / close / good
//
// 帧协议（4 字节长度前缀 + 负载）统一在 network.h（put_u32/get_u32/send_frame/recv_frame）：
//   TCP 的 tcp_conn 已内置帧级；串口等字节流用 frame_stream 包装

#include "../head.h"
#include "network.h"

#include <chrono>
#include <functional>
#include <thread>

namespace xhqm
{
	namespace network
	{
		// 帧长上限默认值（1GB）：防止异常长度头导致巨量分配
		constexpr std::uint32_t frame_max_default = 1u << 30;

		// 帧流包装：把「原始字节流」包成帧级会话（4 字节长度前缀 + 负载）
		// 供串口等无帧边界的传输复用；TCP 的 tcp_conn 已内置帧协议，无需此包装
		//
		// 帧长上限可配置，两处：
		//   max_frame —— 模板参数：按传输给出编译期默认（串口这类低速链路给 MB 级即可）
		//   max_size  —— 成员：构造后可运行时收紧/放宽（0 = 不限，谨慎使用）
		// 收：长度头超过上限即判定「失步 / 对端异常」，返回 false（调用方据此收链路）；
		// 发：超过上限直接返回 false（本端就拒，别等对端丢）。
		template<class byte_stream, std::uint32_t max_frame = frame_max_default>
		struct frame_stream
		{
			byte_stream& s;
			std::uint32_t max_size = max_frame;   // 帧长上限（0 = 不限）

			// 当前生效的上限（便于上层自检/打印）
			inline std::uint32_t limit() const { return max_size; }

			inline bool oversized(std::size_t len) const
			{
				return (max_size != 0) && (len > max_size);
			}

			bool send(const std::string& data)
			{
				if (oversized(data.size())) return false;
				std::uint8_t hdr[4];
				put_u32(hdr, static_cast<std::uint32_t>(data.size()));
				return s.write(hdr, 4) && s.write(reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
			}

			bool recv(std::string& data)
			{
				std::uint8_t hdr[4];
				if (!s.read(hdr, 4)) return false;
				std::size_t len = get_u32(hdr);
				if (oversized(len)) return false;   // 超过配置上限：判为失步/异常
				data.resize(len);
				if (len > 0 && !s.read(reinterpret_cast<std::uint8_t*>(&data[0]), len)) return false;
				return true;
			}
		};

		// 通用服务端：server_type 提供 accept()，session_type 是帧级会话
		// handler：请求字符串 -> 响应字符串（消息编码由上层决定，如 JSON）
		template<class server_type, class session_type>
		class cs_server
		{
		public:
			using handler_type = std::function<std::string(const std::string&)>;

			cs_server(server_type server, handler_type handler)
				: m_server(std::move(server)), m_handler(std::move(handler)) {}

			// 阻塞监听循环：accept → 每 session 独立线程处理，直到连接断开
			void run()
			{
				while (true)
				{
					session_type* session = m_server.accept();
					if (!session)
					{
						std::this_thread::sleep_for(std::chrono::milliseconds(10));
						continue;
					}
					std::thread(&cs_server::serve, this, session).detach();
				}
			}

			// 处理单条连接（供测试/单线程复用）
			void serve(session_type* session)
			{
				std::string req;
				while (session->recv(req))
				{
					try
					{
						std::string resp = m_handler(req);
						if (!session->send(resp)) break;
					}
					catch (...) { break; }
				}
				delete session;
			}

		private:
			server_type m_server;
			handler_type m_handler;
		};

		// 通用客户端：client_type 提供帧级 send/recv
		template<class client_type>
		class cs_client
		{
		public:
			cs_client(client_type client) : m_client(std::move(client)) {}

			// 同步请求-响应
			bool request(const std::string& req, std::string& resp)
			{
				if (!m_client.send(req)) return false;
				if (!m_client.recv(resp)) return false;
				return true;
			}

			bool good() const { return m_client.good(); }

		private:
			client_type m_client;
		};
	}
}

#endif
