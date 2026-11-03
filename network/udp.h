#ifndef XHQM_NETWORK_UDP
#define XHQM_NETWORK_UDP

#include "network.h"

#include <string>

namespace xhqm
{
	namespace network
	{
		// UDP 套接字公共实现（datagram：无连接，bind 端口 + 收发带地址）
		class xhqm_io udp_socket
		{
		public:
			udp_socket() = default;
			~udp_socket() { close(); }
			udp_socket(udp_socket&& other) noexcept : m_sock(other.m_sock) { other.m_sock = error_socket; }
			udp_socket& operator=(udp_socket&& other) noexcept
			{
				if (this != &other) { close(); m_sock = other.m_sock; other.m_sock = error_socket; }
				return *this;
			}
			udp_socket(const udp_socket&) = delete;
			udp_socket& operator=(const udp_socket&) = delete;

			bool bind(int port);
			bool send_to(const std::string& data, const std::string& host, unsigned short port);
			bool broadcast_to(const std::string& data, unsigned short port);
			bool recv_from(std::string& data, std::string& host, unsigned short& port);
			bool recv_timeout(int ms);
			int local_port() const;   // 本 socket 绑定的本地端口（bind(0) 后取系统分配端口）
			void close();
			bool good() const { return m_sock != error_socket; }

		private:
			hander m_sock = error_socket;
		};

		// UDP 服务器：bind 已知端口，收 datagram 后按来源地址回发
		class xhqm_io udp_server : public udp_socket
		{
		public:
			bool listen(int port) { return bind(port); }
		};

		// UDP 客户端：bind 本地端口（0 = 系统分配临时端口），向指定目标收发
		class xhqm_io udp_client : public udp_socket
		{
		public:
			bool open(int port = 0) { return bind(port); }
		};
	}
}

#endif
