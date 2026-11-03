#ifndef XHQM_NETWORK_TCP
#define XHQM_NETWORK_TCP

#include "network.h"

#include <string>

namespace xhqm
{
	namespace network
	{
		// 一条已建立的 TCP 连接（帧级：4 字节长度前缀 + 数据）
		class xhqm_io tcp_conn
		{
		public:
			tcp_conn() = default;
			~tcp_conn() { close(); }
			tcp_conn(tcp_conn&& other) noexcept : m_sock(other.m_sock) { other.m_sock = error_socket; }
			tcp_conn& operator=(tcp_conn&& other) noexcept
			{
				if (this != &other) { close(); m_sock = other.m_sock; other.m_sock = error_socket; }
				return *this;
			}
			tcp_conn(const tcp_conn&) = delete;
			tcp_conn& operator=(const tcp_conn&) = delete;

			bool send(const std::string& data);
			bool recv(std::string& data);
			// 设接收超时（毫秒）：服务端防单个卡死连接堵死 accept 循环
			bool recv_timeout(int ms);
			// 设发送超时（毫秒）：对端停止读取时 send 不会永久阻塞
			bool send_timeout(int ms);
			void close();
			bool good() const { return m_sock != error_socket; }
			hander hand() const { return m_sock; }   // 句柄访问（对端地址等诊断用）
			void attach(hander sock);   // 内部：accept/connect 后挂接句柄

		private:
			hander m_sock = error_socket;
		};

		// TCP 服务器（listen / accept 同步模型，配合 cs_server 使用）
		class xhqm_io tcp_server
		{
		public:
			tcp_server() = default;
			~tcp_server() { close(); }
			tcp_server(tcp_server&& other) noexcept : m_sock(other.m_sock) { other.m_sock = error_socket; }
			tcp_server& operator=(tcp_server&& other) noexcept
			{
				if (this != &other) { close(); m_sock = other.m_sock; other.m_sock = error_socket; }
				return *this;
			}
			tcp_server(const tcp_server&) = delete;
			tcp_server& operator=(const tcp_server&) = delete;

			bool listen(int port);
			tcp_conn* accept();
			// 监听 socket 设接收超时：Windows 上跨线程 close() 不保证唤醒阻塞中的 accept()，
			// 设超时后 accept 周期返回错误，事件循环得以检查运行标记并退出（否则关停 join 可能永久挂起）
			bool recv_timeout(int ms);
			void close();
			bool good() const { return m_sock != error_socket; }

		private:
			hander m_sock = error_socket;
		};

		// TCP 客户端（connect + 帧级收发）
		class xhqm_io tcp_client
		{
		public:
			tcp_client() = default;
			~tcp_client() { close(); }
			tcp_client(tcp_client&& other) noexcept : m_conn(std::move(other.m_conn)) {}
			tcp_client& operator=(tcp_client&& other) noexcept
			{
				if (this != &other) m_conn = std::move(other.m_conn);
				return *this;
			}
			tcp_client(const tcp_client&) = delete;
			tcp_client& operator=(const tcp_client&) = delete;

			bool connect(const std::string& host, int port);
			// 带超时连接（connect 非阻塞 + select；同时设 recv 超时，请求不会无限等）
			bool connect_timeout(const std::string& host, int port, int timeout_ms);
			bool send(const std::string& data) { return m_conn.send(data); }
			bool recv(std::string& data) { return m_conn.recv(data); }
			void close() { m_conn.close(); }
			bool good() const { return m_conn.good(); }

		private:
			tcp_conn m_conn;
		};
	}
}

#endif
