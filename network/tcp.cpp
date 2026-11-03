#include "tcp.h"

#include <string>

namespace xhqm
{
	namespace network
	{
		void tcp_conn::attach(hander sock) { m_sock = sock; }

		void tcp_conn::close()
		{
			if (m_sock != error_socket) { close_socket(m_sock); m_sock = error_socket; }
		}

		bool tcp_conn::send(const std::string& data)
		{
			if (!good()) return false;
			return send_frame(m_sock, data);
		}

		bool tcp_conn::recv(std::string& data)
		{
			if (!good()) return false;
			return recv_frame(m_sock, data);
		}

		bool tcp_conn::recv_timeout(int ms)
		{
			if (!good()) return false;
			set_recv_timeout(m_sock, ms);
			return true;
		}

		bool tcp_conn::send_timeout(int ms)
		{
			if (!good()) return false;
			set_send_timeout(m_sock, ms);
			return true;
		}

		bool tcp_server::listen(int port)
		{
			init_network();
			hander s = open_tcp();
			if (s == error_socket) return false;
			if (!listen_tcp(s, port)) { close_socket(s); return false; }
			m_sock = s;
			return true;
		}

		tcp_conn* tcp_server::accept()
		{
			if (!good()) return nullptr;
			hander c = accept_tcp(m_sock);
			if (c == error_socket) return nullptr;
			tcp_conn* conn = new tcp_conn();
			conn->attach(c);
			return conn;
		}

		bool tcp_server::recv_timeout(int ms)
		{
			if (!good()) return false;
			set_recv_timeout(m_sock, ms);
			return true;
		}

		void tcp_server::close()
		{
			if (m_sock != error_socket) { close_socket(m_sock); m_sock = error_socket; }
		}

		bool tcp_client::connect(const std::string& host, int port)
		{
			init_network();
			// 按目标 family 建 socket（IPv4 目标 AF_INET，IPv6 目标 AF_INET6），
			// 服务器侧才用双栈 listen，客户端无需双栈
			hander s = connect_tcp_auto(host.c_str(), port);
			if (s == error_socket) return false;
			m_conn.attach(s);
			return true;
		}

		bool tcp_client::connect_timeout(const std::string& host, int port, int timeout_ms)
		{
			init_network();
			hander s = connect_tcp_auto_timeout(host.c_str(), port, timeout_ms);
			if (s == error_socket) return false;
			m_conn.attach(s);
			return true;
		}
	}
}
