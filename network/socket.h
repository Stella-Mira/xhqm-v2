#ifndef XHQM_SOCKET
#define XHQM_SOCKET

#include "../metaclass/buffer.h"
#include "network.h"

#include <string>

namespace xhqm 
{
	namespace network
	{
		// socket 句柄 RAII 基础（hander 移动语义封装，析构自动 close）
		class sock
		{
		protected:
			hander sock_hander = error_socket;
			sock(const sock&) = delete;
		public:
			sock();
			sock(hander sock);
			sock(sock&& move) noexcept;
			virtual ~sock();

			void operator =(sock&& move) noexcept;

			inline hander hand() const { return sock_hander; }
			inline bool good() const { return (sock_hander != error_socket); }
		};

		// socket 参数封装（af/type/protocol + IPv4/IPv6 地址解析）
		class socket
			: public sock
		{
		protected:
			int m_af = 0;
			int m_type = 0;
			int m_protocol = 0;
			string sock_addr = "";
			error_id last_error = 0;
		public:
			socket(socket&& move) noexcept;
			socket(int af, int type, int protocol);
		public:
			inline int family() const { return m_af; }
			inline int type() const { return m_type; }
			inline int protocol() const { return m_protocol; }
			inline string addr() const { return sock_addr; }
			inline error_id error_code() const { return last_error; }
		public:
			static bool ipv4path(const xhqm::string& path, xhqm::buffer<unsigned char>& host, unsigned short& post);
			static bool ipv6path(const xhqm::string& path, xhqm::buffer<unsigned short>& host, unsigned short& post);
		};
	};
};
#endif
