#include "udp.h"

#include <cstring>
#include <vector>

namespace xhqm
{
	namespace network
	{
		bool udp_socket::bind(int port)
		{
			init_network();
			hander s = open_udp();
			if (s == error_socket) return false;
			if (!bind_udp(s, port)) { close_socket(s); return false; }
			m_sock = s;
			return true;
		}

		bool udp_socket::send_to(const std::string& data, const std::string& host, unsigned short port)
		{
			if (!good()) return false;
			return sendto_udp(m_sock, data.data(), data.size(), host.c_str(), static_cast<int>(port));
		}

		bool udp_socket::broadcast_to(const std::string& data, unsigned short port)
		{
			if (!good()) return false;
			return sendto_broadcast_udp(m_sock, data.data(), data.size(), static_cast<int>(port));
		}

		bool udp_socket::recv_timeout(int ms)
		{
			if (!good()) return false;
			set_recv_timeout(m_sock, ms);
			return true;
		}

		int udp_socket::local_port() const
		{
			if (m_sock == error_socket) return 0;
			sockaddr_storage ss{};
#ifdef _WIN32
			int len = sizeof(ss);
#else
			socklen_t len = sizeof(ss);
#endif
			if (getsockname(m_sock, reinterpret_cast<sockaddr*>(&ss), &len) != 0) return 0;
			if (ss.ss_family == AF_INET)
				return ntohs(reinterpret_cast<sockaddr_in*>(&ss)->sin_port);
			if (ss.ss_family == AF_INET6)
				return ntohs(reinterpret_cast<sockaddr_in6*>(&ss)->sin6_port);
			return 0;
		}

		bool udp_socket::recv_from(std::string& data, std::string& host, unsigned short& port)
		{
			if (!good()) return false;
			std::vector<char> buf(65536);   // UDP datagram 上限
			std::size_t got = 0;
			if (!recvfrom_udp(m_sock, buf.data(), buf.size(), got, host, port)) return false;
			data.assign(buf.data(), got);
			return true;
		}

		void udp_socket::close()
		{
			if (m_sock != error_socket) { close_socket(m_sock); m_sock = error_socket; }
		}
	}
}
