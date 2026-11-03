#ifndef XHQM_NETWORK
#define XHQM_NETWORK

#include "../head.h"
#include <climits>
#include <cstring>
//#define XHQM_WINDOWS

#ifdef XHQM_WINDOWS
#include <WinSock2.h>
#include <Ws2tcpip.h>
#pragma comment(lib,"ws2_32.lib")
namespace xhqm
{
	namespace network
	{
		using error_id = int;
		using hander = SOCKET;
		using address = sockaddr_in;
		using byteptr = char*;
		const hander error_socket = INVALID_SOCKET;

		bool good_network();
		void init_network();
		void exit_network();

		inline hander open_socket(int af, int type, int protocol)
		{
			return ::socket(af, type, protocol);
		}

		inline bool close_socket(hander sock)
		{
			return (::closesocket(sock) == 0);
		}

		inline error_id error_code()
		{
			return ::WSAGetLastError();
		}
	}
}
#else //XHQM_LINUX XHQM_MAC
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
namespace xhqm
{
	namespace network
	{
		using error_id = int;
		using hander = int;
		using byteptr = void*;
		using address = sockaddr_in;
		const hander error_socket = -1;

		bool good_network();
		void init_network();
		void exit_network();

		inline hander open_socket(int af, int type, int protocol)
		{
			return ::socket(af, type, protocol);
		}

		inline bool close_socket(hander sock)
		{
			return (::close(sock) == 0);
		}

		inline error_id error_code()
		{
			return errno;
		}
	}
}
#endif

// ---- 平台无关层：基于 hander 抽象的字节收发与 TCP/UDP 连接辅助 ----
// 全部跨平台差异收敛到上面两个块；此处只依赖 hander/error_socket/open_socket 等抽象。
namespace xhqm {
	namespace network
	{
		// 循环 send 定长（int 分块，Windows/POSIX 兼容；false = 连接断开或错误）
		inline bool send_bytes(hander sock, const char* p, std::size_t n)
		{
			while (n > 0)
			{
				int chunk = (n > static_cast<std::size_t>(INT_MAX)) ? INT_MAX : static_cast<int>(n);
#ifdef _WIN32
				int r = ::send(sock, p, chunk, 0);
#else
				// MSG_NOSIGNAL：对端已关闭时回写（如短连 ack）会触发 SIGPIPE 打死整个进程
				//（cmd_probe 实测：frame_runing 回 ok 时对端刚 close，SIGPIPE 终止进程）
				int r = ::send(sock, p, chunk, MSG_NOSIGNAL);
#endif
				if (r <= 0) return false;
				p += r; n -= static_cast<std::size_t>(r);
			}
			return true;
		}

		// 循环 recv 定长
		inline bool recv_bytes(hander sock, char* p, std::size_t n)
		{
			while (n > 0)
			{
				int chunk = (n > static_cast<std::size_t>(INT_MAX)) ? INT_MAX : static_cast<int>(n);
				int r = ::recv(sock, p, chunk, 0);
				if (r <= 0) return false;
				p += r; n -= static_cast<std::size_t>(r);
			}
			return true;
		}

		// ---- 地址解析（IPv4/IPv6/主机名 统一，优先 IPv6）----
		inline bool resolve_addr(const char* host, int port, sockaddr_storage& out, int& out_len)
		{
			addrinfo hints{};
			hints.ai_family = AF_UNSPEC;
			addrinfo* res = nullptr;
			if (::getaddrinfo(host, std::to_string(port).c_str(), &hints, &res) != 0 || !res) return false;
			bool ok = false;
			for (addrinfo* it = res; it; it = it->ai_next)
			{
				if (it->ai_family == AF_INET6 || it->ai_family == AF_INET)
				{
					out_len = static_cast<int>(it->ai_addrlen);
					std::memcpy(&out, it->ai_addr, it->ai_addrlen);
					ok = true;
					break;
				}
			}
			::freeaddrinfo(res);
			return ok;
		}

		// 查 socket 本地 family（双栈 AF_INET6 与 IPv4 目标适配用）
		inline int sock_family(hander sock)
		{
			sockaddr_storage ss{};
#ifdef _WIN32
			int len = sizeof(ss);
#else
			socklen_t len = sizeof(ss);
#endif
			if (::getsockname(sock, reinterpret_cast<sockaddr*>(&ss), &len) != 0) return AF_UNSPEC;
			return static_cast<int>(ss.ss_family);
		}

		// IPv4 sockaddr_in → IPv4-mapped sockaddr_in6（双栈 socket 连 IPv4 目标用）
		inline void map_v4_in_v6(const sockaddr_in& a4, sockaddr_in6& a6)
		{
			a6 = {};
			a6.sin6_family = AF_INET6;
			a6.sin6_port = a4.sin_port;
			std::uint8_t* b = a6.sin6_addr.s6_addr;
			std::memset(b, 0, 10);
			b[10] = 0xFF; b[11] = 0xFF;
			std::memcpy(b + 12, &a4.sin_addr, 4);
		}

		// ---- TCP ----
		// 双栈：优先 AF_INET6 + 关 V6ONLY（同时收 IPv4 映射），失败回退 AF_INET
		inline hander open_tcp()
		{
			hander s = open_socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
			if (s != error_socket)
			{
				int no = 0;
				setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&no), sizeof(no));
				return s;
			}
			return open_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		}

		// bind(any:port) + listen；先试 IPv6 any（双栈 socket），失败回退 IPv4 any
		inline bool listen_tcp(hander sock, int port)
		{
			int one = 1;
			setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof(one));
			sockaddr_in6 addr6{};
			addr6.sin6_family = AF_INET6;
			addr6.sin6_addr = in6addr_any;
			addr6.sin6_port = htons(static_cast<unsigned short>(port));
			if (::bind(sock, reinterpret_cast<sockaddr*>(&addr6), sizeof(addr6)) == 0)
				return ::listen(sock, 1024) == 0;
			sockaddr_in addr4{};
			addr4.sin_family = AF_INET;
			addr4.sin_addr.s_addr = INADDR_ANY;
			addr4.sin_port = htons(static_cast<unsigned short>(port));
			if (::bind(sock, reinterpret_cast<sockaddr*>(&addr4), sizeof(addr4)) != 0) return false;
			return ::listen(sock, 1024) == 0;
		}

		// accept 返回新连接 hander（失败 error_socket）
		inline hander accept_tcp(hander sock)
		{
			return ::accept(sock, nullptr, nullptr);
		}

		// 解析 host（IPv4/IPv6 文本或主机名）并 connect（自动适配 socket family）
		inline bool connect_tcp(hander sock, const char* host, int port)
		{
			sockaddr_storage addr{};
			int len = 0;
			if (!resolve_addr(host, port, addr, len)) return false;
			// IPv6 socket + IPv4 目标：转 v4-mapped
			if (sock_family(sock) == AF_INET6 && addr.ss_family == AF_INET)
			{
				sockaddr_in6 a6;
				map_v4_in_v6(*reinterpret_cast<sockaddr_in*>(&addr), a6);
				return ::connect(sock, reinterpret_cast<sockaddr*>(&a6), sizeof(a6)) == 0;
			}
			return ::connect(sock, reinterpret_cast<sockaddr*>(&addr), len) == 0;
		}

		// 按目标 family 建 socket 并连接（客户端便捷版：IPv4 目标开 AF_INET，IPv6 目标开 AF_INET6）
		inline hander connect_tcp_auto(const char* host, int port)
		{
			sockaddr_storage addr{};
			int len = 0;
			if (!resolve_addr(host, port, addr, len)) return error_socket;
			hander s = open_socket(static_cast<int>(addr.ss_family), SOCK_STREAM, IPPROTO_TCP);
			if (s == error_socket) return error_socket;
			if (::connect(s, reinterpret_cast<sockaddr*>(&addr), len) != 0)
			{
				close_socket(s);
				return error_socket;
			}
			return s;
		}

		// 非阻塞 connect + poll 超时（已解析地址；成功返回 true，失败/超时返回 false）
		// 注意：不可用 select/fd_set——Windows 套接字是句柄，值可超过 FD_SETSIZE(64)，
		// FD_SET 会越界写栈（/GS 检测到即进程崩溃，实测 20 节点高句柄值下必现）
		inline bool connect_addr_timeout(hander sock, const sockaddr_storage& addr, int len, int timeout_ms)
		{
			// 设非阻塞
#ifdef _WIN32
			u_long mode = 1;
			if (ioctlsocket(sock, FIONBIO, &mode) != 0) return false;
#else
			int flags = fcntl(sock, F_GETFL, 0);
			if (flags < 0 || fcntl(sock, F_SETFL, flags | O_NONBLOCK) < 0) return false;
#endif

			int r = ::connect(sock, reinterpret_cast<const sockaddr*>(&addr), len);

			// 恢复阻塞（成功/失败统一恢复）
			auto restore_blocking = [&]() {
#ifdef _WIN32
				u_long bm = 0;
				ioctlsocket(sock, FIONBIO, &bm);
#else
				fcntl(sock, F_SETFL, flags);
#endif
			};

			if (r == 0)
			{
				restore_blocking();
				return true;
			}

			// 等待可写（poll 超时即 connect 超时；句柄无 FD_SETSIZE 上限）
#ifdef _WIN32
			WSAPOLLFD pfd{};
			pfd.fd = sock;
			pfd.events = POLLOUT;
			int pr = WSAPoll(&pfd, 1, timeout_ms);
#else
			pollfd pfd{};
			pfd.fd = static_cast<int>(sock);
			pfd.events = POLLOUT;
			int pr = ::poll(&pfd, 1, timeout_ms);
#endif

			restore_blocking();

			if (pr <= 0) return false;   // 超时或错误
#ifdef _WIN32
			if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) return false;
#else
			if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) return false;
#endif

			// 检查 SO_ERROR（连接是否真正建立）
			int err = 0;
#ifdef _WIN32
			int elen = sizeof(err);
			if (getsockopt(sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &elen) != 0) return false;
#else
			socklen_t elen = sizeof(err);
			if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &elen) != 0) return false;
#endif
			return err == 0;
		}

		// 非阻塞 connect + select 超时（按 host/port 解析；适配已有 socket family）
		inline bool connect_tcp_timeout(hander sock, const char* host, int port, int timeout_ms)
		{
			sockaddr_storage addr{};
			int len = 0;
			if (!resolve_addr(host, port, addr, len)) return false;
			// IPv6 socket + IPv4 目标：转 v4-mapped
			if (sock_family(sock) == AF_INET6 && addr.ss_family == AF_INET)
			{
				sockaddr_in6 a6;
				map_v4_in_v6(*reinterpret_cast<sockaddr_in*>(&addr), a6);
				return connect_addr_timeout(sock, *reinterpret_cast<sockaddr_storage*>(&a6), sizeof(a6), timeout_ms);
			}
			return connect_addr_timeout(sock, addr, len, timeout_ms);
		}

		// 设 recv 超时（timeout_ms 毫秒，超时后 recv 失败返回）
		inline void set_recv_timeout(hander sock, int timeout_ms)
		{
#ifdef _WIN32
			DWORD t = static_cast<DWORD>(timeout_ms);
			setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&t), sizeof(t));
#else
			timeval tv;
			tv.tv_sec = timeout_ms / 1000;
			tv.tv_usec = (timeout_ms % 1000) * 1000;
			setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
		}

		// 设 send 超时（对端停止读取时 send 会永久阻塞——混沌压测关停挂死实证）
		inline void set_send_timeout(hander sock, int timeout_ms)
		{
#ifdef _WIN32
			DWORD t = static_cast<DWORD>(timeout_ms);
			setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&t), sizeof(t));
#else
			timeval tv;
			tv.tv_sec = timeout_ms / 1000;
			tv.tv_usec = (timeout_ms % 1000) * 1000;
			setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
		}

		// 按目标 family 建 socket + 超时连接（客户端用；顺带设收发超时）
		inline hander connect_tcp_auto_timeout(const char* host, int port, int timeout_ms)
		{
			sockaddr_storage addr{};
			int len = 0;
			if (!resolve_addr(host, port, addr, len)) return error_socket;
			hander s = open_socket(static_cast<int>(addr.ss_family), SOCK_STREAM, IPPROTO_TCP);
			if (s == error_socket) return error_socket;
			if (!connect_addr_timeout(s, addr, len, timeout_ms))
			{
				close_socket(s);
				return error_socket;
			}
			set_recv_timeout(s, timeout_ms);
			set_send_timeout(s, timeout_ms);
			return s;
		}

		// ---- UDP ----
		inline hander open_udp()
		{
			hander s = open_socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
			if (s != error_socket)
			{
				int no = 0;
				setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&no), sizeof(no));
				return s;
			}
			return open_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		}

		// bind(any:port)；先试 IPv6 any，失败回退 IPv4 any；SO_REUSEADDR（多实例同机可绑同端口）
		inline bool bind_udp(hander sock, int port)
		{
			int one = 1;
			setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof(one));
#ifdef SO_REUSEPORT
			// 同机多实例「都收到」广播/组播：Linux 只看 SO_REUSEADDR，
			// 而 BSD/macOS 必须 SO_REUSEPORT——否则后绑者直接 bind 失败（或收不到广播），
			// 表现为多节点发现/quorum 起不来（macOS 实测：3 节点绑同一 UDP 端口时 degree 恒为 0）。
			setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, reinterpret_cast<const char*>(&one), sizeof(one));
#endif
			// 纪律（三平台实测一致）：共享端口的 UDP socket 只准收发**广播/组播**——
			// SO_REUSEPORT 下单播按 hash 只投其中一个 socket（Linux/BSD 一致，
			// Windows 的 SO_REUSEADDR 同样只投一个），同机多节点间的定向消息
			// 必须走各自 TCP 事件端口（nebula 的 eventsend/addr->send 已如此），
			// 谁将来在共享端口上加 UDP 单播就会踩错投。
			sockaddr_in6 addr6{};
			addr6.sin6_family = AF_INET6;
			addr6.sin6_addr = in6addr_any;
			addr6.sin6_port = htons(static_cast<unsigned short>(port));
			if (::bind(sock, reinterpret_cast<sockaddr*>(&addr6), sizeof(addr6)) == 0) return true;
			sockaddr_in addr4{};
			addr4.sin_family = AF_INET;
			addr4.sin_addr.s_addr = INADDR_ANY;
			addr4.sin_port = htons(static_cast<unsigned short>(port));
			return ::bind(sock, reinterpret_cast<sockaddr*>(&addr4), sizeof(addr4)) == 0;
		}

		// datagram 发送到 host:port（IPv4/IPv6 文本或主机名，自动适配 socket family）
		inline bool sendto_udp(hander sock, const char* data, std::size_t n, const char* host, int port)
		{
			sockaddr_storage addr{};
			int len = 0;
			if (!resolve_addr(host, port, addr, len)) return false;
			int sz = static_cast<int>(n);
			// IPv6 socket + IPv4 目标：转 v4-mapped
			if (sock_family(sock) == AF_INET6 && addr.ss_family == AF_INET)
			{
				sockaddr_in6 a6;
				map_v4_in_v6(*reinterpret_cast<sockaddr_in*>(&addr), a6);
				return ::sendto(sock, data, sz, 0, reinterpret_cast<sockaddr*>(&a6), sizeof(a6)) == sz;
			}
			return ::sendto(sock, data, sz, 0, reinterpret_cast<sockaddr*>(&addr), len) == sz;
		}

		// 局域网广播发送（SO_BROADCAST + 255.255.255.255）
		inline bool sendto_broadcast_udp(hander sock, const char* data, std::size_t n, int port)
		{
			int one = 1;
			setsockopt(sock, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&one), sizeof(one));
			return sendto_udp(sock, data, n, "255.255.255.255", port);
		}

		// datagram 接收（填来源 host/port，IPv4/IPv6 通用）
		inline bool recvfrom_udp(hander sock, char* buf, std::size_t cap, std::size_t& got,
			std::string& host, unsigned short& port)
		{
			sockaddr_storage addr{};
#ifdef _WIN32
			int len = sizeof(addr);
#else
			socklen_t len = sizeof(addr);
#endif
			int r = ::recvfrom(sock, buf, static_cast<int>(cap), 0,
				reinterpret_cast<sockaddr*>(&addr), &len);
			if (r <= 0) return false;
			got = static_cast<std::size_t>(r);
			char ip[INET6_ADDRSTRLEN] = {};
			if (addr.ss_family == AF_INET6)
			{
				sockaddr_in6* a6 = reinterpret_cast<sockaddr_in6*>(&addr);
				if (IN6_IS_ADDR_V4MAPPED(&a6->sin6_addr))
				{
					// 双栈 socket 收 v4 包呈 v4-mapped：还原成纯 IPv4 文本，便于后续直连
					inet_ntop(AF_INET, a6->sin6_addr.s6_addr + 12, ip, sizeof(ip));
				}
				else
				{
					inet_ntop(AF_INET6, &a6->sin6_addr, ip, sizeof(ip));
				}
				port = ntohs(a6->sin6_port);
			}
			else
			{
				sockaddr_in* a4 = reinterpret_cast<sockaddr_in*>(&addr);
				inet_ntop(AF_INET, &a4->sin_addr, ip, sizeof(ip));
				port = ntohs(a4->sin_port);
			}
			host = ip;
			return true;
		}

		// ---- 帧级（4 字节长度前缀 + 负载）：统一帧协议，tcp_conn 与 frame_stream 共用 ----
		inline void put_u32(std::uint8_t* p, std::uint32_t v)
		{
			for (int i = 0; i < 4; ++i) p[i] = static_cast<std::uint8_t>(v >> (8 * i));
		}
		inline std::uint32_t get_u32(const std::uint8_t* p)
		{
			std::uint32_t v = 0;
			for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(p[i]) << (8 * i);
			return v;
		}

		// hander 帧级收发（TCP 消息边界）
		inline bool send_frame(hander sock, const std::string& data)
		{
			std::uint8_t hdr[4];
			put_u32(hdr, static_cast<std::uint32_t>(data.size()));
			if (!send_bytes(sock, reinterpret_cast<const char*>(hdr), 4)) return false;
			return send_bytes(sock, data.data(), data.size());
		}
		inline bool recv_frame(hander sock, std::string& data)
		{
			std::uint8_t hdr[4];
			if (!recv_bytes(sock, reinterpret_cast<char*>(hdr), 4)) return false;
			std::uint32_t len = get_u32(hdr);
			if (len > (1u << 30)) return false;   // 上限 1GB 防异常
			data.resize(len);
			if (len > 0 && !recv_bytes(sock, data.data(), len)) return false;
			return true;
		}
	}
}

#endif
