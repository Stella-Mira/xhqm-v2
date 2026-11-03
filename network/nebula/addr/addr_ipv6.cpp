#include "addr_ipv6.h"

#include <algorithm>

namespace xhqm
{
	namespace nebula
	{
		// ---------------------------------------------------------------- 平台小工具 ----------------
#ifdef XHQM_WINDOWS
		using sock_len_t = int;
		static const short s_poll_in = POLLRDNORM;   // Windows 的 WSAPoll 用 POLLRDNORM（POLLIN 在 winsock 语义含糊）
		static int poll_wait(pollfd* fds, unsigned long n, int ms)
		{
			return ::WSAPoll(fds, n, ms);
		}
#else
		using sock_len_t = socklen_t;
		static const short s_poll_in = POLLIN;
		static int poll_wait(pollfd* fds, unsigned long n, int ms)
		{
			return ::poll(fds, static_cast<nfds_t>(n), ms);
		}
#endif

		using hander_t = xhqm::network::hander;
		static const hander_t s_bad = xhqm::network::error_socket;

		// 非阻塞：reactor 单线程驱动全部长连，任何一次阻塞读写都会拖死其它连接
		static bool set_nonblock(hander_t sock)
		{
#ifdef XHQM_WINDOWS
			u_long mode = 1;
			return ::ioctlsocket(sock, FIONBIO, &mode) == 0;
#else
			const int flags = ::fcntl(sock, F_GETFL, 0);
			if (flags < 0) return false;
			return ::fcntl(sock, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
		}

		// 内核缓冲已满 / 暂无数据：不是错误，等下一轮 poll
		static bool net_would_block()
		{
#ifdef XHQM_WINDOWS
			return xhqm::network::error_code() == WSAEWOULDBLOCK;
#else
			const int e = xhqm::network::error_code();
			return (e == EWOULDBLOCK || e == EAGAIN);
#endif
		}

		// 被信号打断：重试
		static bool net_interrupted()
		{
#ifdef XHQM_WINDOWS
			return xhqm::network::error_code() == WSAEINTR;
#else
			return xhqm::network::error_code() == EINTR;
#endif
		}

		static unsigned short local_port_of(hander_t sock)
		{
			sockaddr_storage ss{};
			sock_len_t len = sizeof(ss);
			if (::getsockname(sock, reinterpret_cast<sockaddr*>(&ss), &len) != 0) return 0;
			if (ss.ss_family == AF_INET6) return ntohs(reinterpret_cast<sockaddr_in6*>(&ss)->sin6_port);
			if (ss.ss_family == AF_INET) return ntohs(reinterpret_cast<sockaddr_in*>(&ss)->sin_port);
			return 0;
		}

		// 短连发完就关：先 shutdown(写端) 再 close，避免「对端尚有未读数据」时 close 触发 RST 冲掉在途数据
		static void shutdown_write(hander_t sock)
		{
#ifdef XHQM_WINDOWS
			::shutdown(sock, SD_SEND);
#else
			::shutdown(sock, SHUT_WR);
#endif
		}

		// host 里的接口索引后缀（"fe80::1%16" → 16；无 % 返回 0）
		static unsigned int host_zone(const text& host)
		{
			const xhqm::size pos = host.find('%');
			if (pos == text::npos) return 0;
			unsigned int zone = 0;
			try { zone = static_cast<unsigned int>(std::stoul(host.substr(pos + 1))); }
			catch (...) { return 0; }
			return zone;
		}

		// ---------------------------------------------------------------- 进程级两张表 ----------------
		// 与 addr_ipv4 的 ipv4_registry / ipv4_conns 同构：
		//   ipv6_registry —— 已打开的端点（uuid → 端点）：同 uuid 直投，进程内联调不绕 socket；
		//   ipv6_conns    —— 长连接表（对端 uuid → 连接）：端点与克隆句柄共用同一条长连，
		//                    也能复用别人拨进来的连接，所以同一对节点只有一条连接。
		static std::mutex ipv6_mutex;
		static std::map<nebula::uuid, addr_ipv6*, uuid_less> ipv6_registry;

		static std::mutex ipv6_conn_mutex;
		static std::map<nebula::uuid, std::shared_ptr<addr_ipv6::conn>, uuid_less> ipv6_conns;

		static addr_ipv6* ipv6_local(const nebula::uuid& id)
		{
			std::lock_guard<std::mutex> lk(ipv6_mutex);
			auto it = ipv6_registry.find(id);
			return (it != ipv6_registry.end()) ? it->second : nullptr;
		}

		// 克隆句柄（自身未 opened）找「把我挂在邻居表里的那个本地端点」：
		// 拨号与收帧都由它来做，收到的帧才会走它自己的 frame_signal（link 连的是它）。
		static addr_ipv6* ipv6_host_of(const nebula::uuid& peer, const addr_ipv6* self)
		{
			std::lock_guard<std::mutex> lk(ipv6_mutex);
			for (auto& kv : ipv6_registry)
			{
				addr_ipv6* one = kv.second;
				if (!one || one == self) continue;
				if (one->has_neighbor(peer)) { return one; }   // has_neighbor 走各自的表锁，无逆序
			}
			return nullptr;
		}

		static std::shared_ptr<addr_ipv6::conn> ipv6_conn_find(const nebula::uuid& peer)
		{
			if (!peer) return nullptr;
			std::lock_guard<std::mutex> lk(ipv6_conn_mutex);
			auto it = ipv6_conns.find(peer);
			if (it != ipv6_conns.end() && it->second && it->second->good) { return it->second; }
			return nullptr;
		}

		// 入表：已有一条**可用**连接就不动；陈旧（已断）条目则由新连接顶替
		static void ipv6_conn_put(const std::shared_ptr<addr_ipv6::conn>& c)
		{
			if (!c || !c->peer) return;
			std::lock_guard<std::mutex> lk(ipv6_conn_mutex);
			auto it = ipv6_conns.find(c->peer);
			if (it == ipv6_conns.end() || !it->second || !it->second->good) { ipv6_conns[c->peer] = c; }
		}

		static void ipv6_conn_erase(const std::shared_ptr<addr_ipv6::conn>& c)
		{
			if (!c || !c->peer) return;
			std::lock_guard<std::mutex> lk(ipv6_conn_mutex);
			auto it = ipv6_conns.find(c->peer);
			if (it != ipv6_conns.end() && it->second == c) { ipv6_conns.erase(it); }
		}

		// ---------------------------------------------------------------- 构造 / 序列化 ----------------

		addr_ipv6::addr_ipv6(const text& host, const sint& port, const sint& broad)
			: addr_b(xhqm::nebula::kind::ipv6), m_host{ host }, m_port{ port }
			, m_broad{ broad > 0 ? broad : port }
		{
			// 本对象自己申请一份网络子系统引用（WSAStartup / SIGPIPE 忽略）：
			// addr_ipv6 直接用 network.h 的裸句柄封装（open_tcp/listen_tcp/open_udp/bind_udp/
			// accept_tcp/connect_tcp_auto_timeout），不走 tcp_server、udp_socket、tcp_client
			// 这三个会自动 init_network 的包装。漏掉这一句的后果实测：open_* 全返回坏句柄、
			// connect 报 WSANOTINITIALISED(10093)，定向腿静默全丢（组播腿则退化成进程内广播）。
			// 克隆句柄也会走到这里：它可能在没有宿主端点的进程里走同步短连退化路径，同样需要 WSA。
			xhqm::network::init_network();
		}

		addr_ipv6::~addr_ipv6()
		{
			this->closed();   // 克隆句柄也有名下连接要收，不能只在 opened 过时才收摊
			// 这里**不**调 exit_network()：与仓库既有做法一致（tcp_server/udp_socket/tcp_client/
			// addr_ipv4 都只 init 不 exit）——引用计数一旦归零就 WSACleanup，会把别人仍在用的
			// 套接字一起废掉；networks 的引用计数本就只增不减，多留一份引用无副作用。
		}

		nebula::json addr_ipv6::make_json() const
		{
			nebula::json j = addr_b::make_json();
			j["host"] = m_host;
			j["port"] = m_port;
			j["broad"] = m_broad;
			return j;
		}

		size addr_ipv6::conn_count() const
		{
			std::lock_guard<std::mutex> lk(m_conn_mutex);
			return m_in.size();   // 死亡连接由 reactor 即时摘表，所以这里就是「活连接数」
		}

		size addr_ipv6::conn_total()
		{
			std::lock_guard<std::mutex> lk(ipv6_conn_mutex);
			return ipv6_conns.size();
		}

		size addr_ipv6::conn_good()
		{
			size count = 0;
			std::lock_guard<std::mutex> lk(ipv6_conn_mutex);
			for (auto& kv : ipv6_conns) { if (kv.second && kv.second->good) { ++count; } }
			return count;
		}

		// ---------------------------------------------------------------- 生命周期 ----------------

		// 组播腿：常驻 UDP 套接字，加入链路本地全节点组播 ff02::1（IPv6 没有广播，这是替代方案）。
		// 逐接口尝试加入（索引 1..32）；host 带 "%N" 时只加入该接口（"fe80::1%16" 这类链路本地地址）。
		// 全部失败 → 返回 false，m_group 保持坏句柄 → send_broad 退化为进程内广播。
		bool addr_ipv6::group_open()
		{
			if (m_broad <= 0) return false;

			hander_t s = xhqm::network::open_udp();
			if (s == s_bad) return false;
			if (!xhqm::network::bind_udp(s, m_broad))   // 绑 :: 任意（SO_REUSEADDR：同机多端点可共享组播端口）
			{
				xhqm::network::close_socket(s);
				return false;
			}
			set_nonblock(s);

			int loop = 1;   // 同机互相收得到（默认即 1，显式设一遍防系统策略改过）
			::setsockopt(s, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, reinterpret_cast<const char*>(&loop), sizeof(loop));
			int hops = 1;   // 链路本地组播只在本段内，跳数 1
			::setsockopt(s, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, reinterpret_cast<const char*>(&hops), sizeof(hops));

			in6_addr group{};
			if (::inet_pton(AF_INET6, s_group, &group) != 1)
			{
				xhqm::network::close_socket(s);
				return false;
			}

			std::vector<unsigned int> want;
			const unsigned int zone = host_zone(m_host);
			if (zone > 0) { want.push_back(zone); }
			else { for (unsigned int idx = 1; idx <= 32; ++idx) { want.push_back(idx); } }

			for (unsigned int idx : want)
			{
				ipv6_mreq mreq{};
				mreq.ipv6mr_multiaddr = group;
				mreq.ipv6mr_interface = idx;
				if (::setsockopt(s, IPPROTO_IPV6, IPV6_JOIN_GROUP, reinterpret_cast<const char*>(&mreq), sizeof(mreq)) == 0)
				{
					m_ifaces.push_back(idx);
				}
			}
			if (m_ifaces.empty())
			{
				xhqm::network::close_socket(s);
				return false;
			}
			{
				std::lock_guard<std::mutex> lk(m_net_mutex);
				m_group = s;
			}
			return true;
		}

		// 唤醒管：WSAPoll 等不了事件对象（Windows 无 eventfd），用一对回环 UDP 套接字自唤醒。
		// 没有它，突发入队的帧要等 poll 超时（s_react_ms）才被处理，64 深的发队列会被丢光。
		bool addr_ipv6::wake_open()
		{
			hander_t r = xhqm::network::open_udp();
			if (r == s_bad) return false;
			if (!xhqm::network::bind_udp(r, 0))
			{
				xhqm::network::close_socket(r);
				return false;
			}
			const unsigned short port = local_port_of(r);
			hander_t w = xhqm::network::open_udp();
			if (w == s_bad || port == 0)
			{
				if (w != s_bad) { xhqm::network::close_socket(w); }
				xhqm::network::close_socket(r);
				return false;
			}
			set_nonblock(r);
			set_nonblock(w);
			{
				std::lock_guard<std::mutex> lk(m_net_mutex);
				m_wake_r = r;
				m_wake_w = w;
				m_wake_port = port;
			}
			return true;
		}

		bool addr_ipv6::opened()
		{
			if (m_runing) return true;   // 幂等
			m_runing = true;
			m_closed = false;
			m_opened = true;             // addr_b::is_open() 的运行标志（此前从不置位 → 恒假）
			m_wake_pending = false;

			// 组播腿（失败即退化，不影响定向腿）
			group_open();
			// 唤醒管（失败只是丢帧窗口变大：poll 超时兜底）
			wake_open();
			// TCP：监听本端点端口（与 UDP 端口空间互不冲突，可同号）；绑 :: 双栈，v4-mapped 也能收
			if (m_port > 0)
			{
				hander_t s = xhqm::network::open_tcp();
				if (s != s_bad && xhqm::network::listen_tcp(s, m_port))
				{
					set_nonblock(s);
					std::lock_guard<std::mutex> lk(m_net_mutex);
					m_answer = s;
				}
				else if (s != s_bad)
				{
					xhqm::network::close_socket(s);
				}
			}
			{
				std::lock_guard<std::mutex> lk(ipv6_mutex);
				ipv6_registry[uuid()] = this;
			}
			m_frame_thread = std::thread([this] { this->reactor_runing(); });
			return true;
		}

		void addr_ipv6::closed()
		{
			if (m_closed.exchange(true)) return;   // 幂等（未 opened 的克隆首次调用也要走完收摊）
			m_opened = false;                       // 与 opened() 配对维护 is_open()
			m_runing = false;

			{
				std::lock_guard<std::mutex> lk(ipv6_mutex);
				auto it = ipv6_registry.find(uuid());
				if (it != ipv6_registry.end() && it->second == this) { ipv6_registry.erase(it); }
			}

			// 名下长连先标死（唤醒在 send_frame(带超时) 上等「已落内核」的线程），
			// 套接字等 reactor 退出后再关：否则 poll 集合里的句柄被别的线程抽掉，行为由内核兜底
			std::vector<std::shared_ptr<conn>> mine;
			{
				std::lock_guard<std::mutex> lk(m_conn_mutex);
				mine.swap(m_in);
			}
			for (auto& c : mine)
			{
				if (!c) continue;
				c->runing = false;
				c->good = false;
				std::lock_guard<std::mutex> lk(c->mutex);
				c->cond.notify_all();
			}

			if (m_frame_thread.joinable()) { m_frame_thread.join(); }   // poll 超时 ≤ s_react_ms，必醒

			// reactor 已退出：此后独占，安全收摊
			{
				std::lock_guard<std::mutex> lk(ipv6_conn_mutex);
				for (auto it = ipv6_conns.begin(); it != ipv6_conns.end(); )
				{
					if (it->second && it->second->owner == this) { it = ipv6_conns.erase(it); }
					else { ++it; }
				}
			}
			for (auto& c : mine) { conn_close(c.get()); }
			mine.clear();

			std::lock_guard<std::mutex> lk(m_net_mutex);
			if (m_answer != s_bad) { xhqm::network::close_socket(m_answer); m_answer = s_bad; }
			if (m_group != s_bad) { xhqm::network::close_socket(m_group); m_group = s_bad; }
			if (m_wake_r != s_bad) { xhqm::network::close_socket(m_wake_r); m_wake_r = s_bad; }
			if (m_wake_w != s_bad) { xhqm::network::close_socket(m_wake_w); m_wake_w = s_bad; }
			m_wake_port = 0;
			m_ifaces.clear();
		}

		// ---------------------------------------------------------------- 长连接 ----------------

		std::shared_ptr<addr_ipv6::conn> addr_ipv6::make_conn(const nebula::uuid& peer, const std::shared_ptr<conn_t>& sock)
		{
			if (!sock || !sock->good()) return nullptr;
			auto c = std::make_shared<conn>();
			c->sock = sock;
			c->peer = peer;
			c->owner = this;
			c->last = xhqm::timesys::ntime();
			c->good = true;
			c->runing = true;
			{
				std::lock_guard<std::mutex> lk(m_conn_mutex);
				m_in.push_back(c);   // 未识别对端（接入侧）也先由本对象保活
			}
			ipv6_conn_put(c);        // 对端已知（拨号侧）才入表；接入侧等 learn_conn
			return c;
		}

		void addr_ipv6::conn_close(conn* c)
		{
			if (!c) return;
			if (c->sock) { c->sock->close(); }
		}

		void addr_ipv6::drop_conn(const std::shared_ptr<conn>& c)
		{
			if (!c) return;
			c->runing = false;
			c->good = false;
			conn_close(c.get());
			{
				std::lock_guard<std::mutex> lk(c->mutex);
				c->outbox.clear();
				c->cond.notify_all();   // 唤醒 send_frame(带超时) 的等待者
			}
			{
				std::lock_guard<std::mutex> lk(m_conn_mutex);
				m_in.erase(std::remove(m_in.begin(), m_in.end(), c), m_in.end());
			}
			ipv6_conn_erase(c);
		}

		void addr_ipv6::learn_conn(conn* c, const nebula::json& frame)
		{
			if (!c || c->peer) return;
			nebula::uuid peer = nebula::uuid::to_uuid(frame.value("send_node", ""));
			if (!peer) return;   // 帧里没有 send_node：保持未知，等后续帧
			c->peer = peer;
			ipv6_conn_put(c->shared_from_this());   // 接入连接登记进进程级表，之后谁都能复用它回发
		}

		// 自唤醒：合并多次入队（已有唤醒在途就不再发），避免每帧一次 sendto
		void addr_ipv6::wake()
		{
			if (m_wake_pending.exchange(true)) return;
			hander_t w = s_bad;
			unsigned short port = 0;
			{
				std::lock_guard<std::mutex> lk(m_net_mutex);
				w = m_wake_w;
				port = m_wake_port;
			}
			if (w == s_bad || port == 0) return;
			sockaddr_in6 dst{};
			dst.sin6_family = AF_INET6;
			::inet_pton(AF_INET6, "::1", &dst.sin6_addr);
			dst.sin6_port = htons(port);
			const char one = 1;
			::sendto(w, &one, 1, 0, reinterpret_cast<sockaddr*>(&dst), sizeof(dst));
		}

		// ---------------------------------------------------------------- reactor ----------------

		void addr_ipv6::reactor_runing()
		{
			while (m_runing)
			{
				hander_t answer = s_bad, group = s_bad, wake_r = s_bad;
				{
					std::lock_guard<std::mutex> lk(m_net_mutex);
					answer = m_answer;
					group = m_group;
					wake_r = m_wake_r;
				}
				std::vector<std::shared_ptr<conn>> list;
				{
					std::lock_guard<std::mutex> lk(m_conn_mutex);
					list = m_in;
				}

				// 死亡连接回收：读写失败 / 空闲超时 / 被 closed() 标死
				const xhqm::u64 now = xhqm::timesys::ntime();
				const xhqm::u64 idle = xhqm::u64(m_idle) * 1000000ull;   // 毫秒 → 纳秒
				for (auto it = list.begin(); it != list.end(); )
				{
					conn* c = it->get();
					const bool live = (c && c->runing && c->good && c->sock && c->sock->good()
						&& (now - c->last.load()) <= idle);
					if (live) { ++it; continue; }
					drop_conn(*it);
					it = list.erase(it);
				}

				// poll 集合：[监听套接字 | 组播套接字 | 唤醒管 | 全部长连]（位置固定，回读按下标）
				std::vector<pollfd> fds;
				fds.reserve(3 + list.size());
				const int at_answer = (answer != s_bad) ? static_cast<int>(fds.size()) : -1;
				if (answer != s_bad) { pollfd p{}; p.fd = answer; p.events = s_poll_in; fds.push_back(p); }
				const int at_group = (group != s_bad) ? static_cast<int>(fds.size()) : -1;
				if (group != s_bad) { pollfd p{}; p.fd = group; p.events = s_poll_in; fds.push_back(p); }
				const int at_wake = (wake_r != s_bad) ? static_cast<int>(fds.size()) : -1;
				if (wake_r != s_bad) { pollfd p{}; p.fd = wake_r; p.events = s_poll_in; fds.push_back(p); }
				const xhqm::size base = fds.size();
				for (auto& c : list) { pollfd p{}; p.fd = c->sock->hand(); p.events = s_poll_in; fds.push_back(p); }

				// 返回 ≤0（超时 / 错误）也往下走：先把唤醒管与发队列处理掉
				poll_wait(fds.data(), static_cast<unsigned long>(fds.size()), static_cast<int>(s_react_ms));

				// 1) 唤醒管排空后再清标志：清标志与发队列处理之间有新入队，会再补一次唤醒，不会漏
				if (at_wake >= 0)
				{
					char buf[64];
					while (::recvfrom(wake_r, buf, static_cast<int>(sizeof(buf)), 0, nullptr, nullptr) > 0) {}
					m_wake_pending = false;
				}

				// 2) 新连接接入（非阻塞 accept，一次排空）
				if (at_answer >= 0 && (fds[at_answer].revents & (s_poll_in | POLLERR | POLLHUP | POLLNVAL)))
				{
					for (int i = 0; i < 64 && m_runing; ++i)
					{
						hander_t s = xhqm::network::accept_tcp(answer);
						if (s == s_bad) break;             // 排空（非阻塞：无更多连接即 WSAEWOULDBLOCK）
						set_nonblock(s);
						auto sock = std::make_shared<conn_t>();
						sock->attach(s);
						make_conn(nebula::uuid{}, sock);   // 对端 uuid 由首帧 send_node 学得；make_conn 已挂进 m_in
					}
				}

				// 3) 组播收帧（非阻塞排空，一次最多 64 个 datagram；畸形包只丢自己）
				if (at_group >= 0 && (fds[at_group].revents & (s_poll_in | POLLERR | POLLHUP | POLLNVAL)))
				{
					char buf[65536];   // UDP datagram 上限
					for (int i = 0; i < 64 && m_runing; ++i)
					{
						sock_len_t len = sizeof(sockaddr_storage);
						sockaddr_storage from{};
						const int r = ::recvfrom(group, buf, static_cast<int>(sizeof(buf)), 0,
							reinterpret_cast<sockaddr*>(&from), &len);
						if (r <= 0) break;
						// 远端字节不可信：畸形包 parse 抛异常会顺 reactor 线程冒出 → std::terminate
						try
						{
							json frame = json::parse(std::string(buf, static_cast<xhqm::size>(r)));
							if (frame.is_object()) { this->broad_signal.emit(frame); }
						}
						catch (...) {}
					}
				}

				// 4) 长连读写：不依赖 POLLOUT——非阻塞 send 立刻返回，写不动就等下一轮
				for (xhqm::size i = 0; i < list.size() && m_runing; ++i)
				{
					conn* c = list[i].get();
					if (!c || !c->runing || !c->good) continue;
					const short rev = fds[base + i].revents;
					if (rev & (s_poll_in | POLLERR | POLLHUP | POLLNVAL))
					{
						if (!conn_read(c)) { drop_conn(list[i]); continue; }
					}
					if (!conn_write(c)) { drop_conn(list[i]); continue; }
				}
			}
		}

		// 非阻塞读 + 拆帧（4 字节长度前缀 + 载荷，与 network::recv_frame 同协议）
		bool addr_ipv6::conn_read(conn* c)
		{
			if (!c || !c->sock || !c->sock->good()) return false;
			hander_t s = c->sock->hand();
			char buf[8192];
			for (;;)
			{
				const int r = ::recv(s, buf, static_cast<int>(sizeof(buf)), 0);
				if (r > 0)
				{
					c->inbuf.append(buf, static_cast<xhqm::size>(r));
					c->last = xhqm::timesys::ntime();
					if (c->inbuf.size() > s_read_cap) return false;   // 对端灌水：不解析、直接收连接
					for (;;)
					{
						if (c->inbuf.size() < 4) break;
						const std::uint32_t len = xhqm::network::get_u32(
							reinterpret_cast<const std::uint8_t*>(c->inbuf.data()));
						if (len > (1u << 26)) return false;   // 单帧上限 64MB：长度不可信
						if (c->inbuf.size() < 4 + static_cast<xhqm::size>(len)) break;   // 帧未到齐
						const text body = c->inbuf.substr(4, len);
						c->inbuf.erase(0, 4 + static_cast<xhqm::size>(len));
						if (body.empty()) continue;
						try
						{
							json frame = json::parse(body);
							if (!frame.is_object()) continue;
							learn_conn(c, frame);   // 接入侧：首帧 send_node → 记住对端 uuid 并登记连接
							if (frame.contains("type")) { this->frame_signal.emit(frame); }
						}
						catch (...) {}
					}
					continue;
				}
				if (r == 0) return false;            // 对端正常关闭
				if (net_would_block()) return true;  // 已读干净
				if (net_interrupted()) continue;
				return false;
			}
		}

		// 非阻塞发队列：outbox → pending → 内核；写不动就留着，下一轮 poll 继续
		bool addr_ipv6::conn_write(conn* c)
		{
			if (!c || !c->sock || !c->sock->good()) return false;
			hander_t s = c->sock->hand();
			for (;;)
			{
				if (c->pending.empty())
				{
					json value;
					{
						std::lock_guard<std::mutex> lk(c->mutex);
						if (c->outbox.empty())
						{
							c->writing = false;
							c->cond.notify_all();
							return true;
						}
						value = std::move(c->outbox.front());
						c->outbox.pop_front();
						c->writing = true;
					}
					std::string body;
					try { body = value.dump(); }
					catch (...) { continue; }   // dump 自身异常（畸形本地数据）：丢这一帧，不牵连连接
					if (body.size() > (1u << 26)) { continue; }
					std::uint8_t hdr[4];
					xhqm::network::put_u32(hdr, static_cast<std::uint32_t>(body.size()));
					c->pending.assign(reinterpret_cast<const char*>(hdr), 4);
					c->pending.append(body);
				}
				const int r = ::send(s, c->pending.data(), static_cast<int>(c->pending.size()), 0);
				if (r > 0)
				{
					c->pending.erase(0, static_cast<xhqm::size>(r));
					if (c->pending.empty())
					{
						std::lock_guard<std::mutex> lk(c->mutex);
						c->writing = false;
						c->cond.notify_all();   // 通知 send_frame(带超时)：已全部落内核
					}
					continue;
				}
				if (net_would_block()) return true;   // 内核缓冲满：留着，下一轮 poll 继续
				if (net_interrupted()) continue;
				return false;
			}
		}

		// ---------------------------------------------------------------- 发送 ----------------

		bool addr_ipv6::deliver(const nebula::uuid& peer, const nebula::json& value, sint timeout_ms)
		{
			if (!peer) return false;

			// 1) 本进程内已有该 uuid 的已打开端点（含自身）→ 直投，等价 addr_uuid 的进程内路由
			if (addr_ipv6* local = ipv6_local(peer))
			{
				local->frame_signal.emit(value);
				return true;
			}
			// 2) 谁来做这件事：已打开的端点自己干；克隆句柄交给「把它挂进邻居表的本地端点」——
			//    否则连接挂在克隆上，回程帧只会发到克隆的信号，而 link 只连自己端点的信号。
			addr_ipv6* sender = m_runing ? this : ipv6_host_of(peer, this);
			if (!sender) sender = this;   // 还没被任何端点收作邻居：退化为自己拨号（无 reactor 时走同步短连）
			return sender->send_remote(peer, m_host, m_port, value, timeout_ms);
		}

		bool addr_ipv6::send_remote(const nebula::uuid& peer, const text& host, const sint& port,
			const nebula::json& value, sint timeout_ms)
		{
			// 本对象没有 reactor（纯克隆句柄、又没找到宿主端点）：退化为一次同步短连
			if (!m_runing) return send_oneshot(host, port, value);

			// 进程级长连表：出向 / 入向复用，同一对节点只有一条
			std::shared_ptr<conn> c = ipv6_conn_find(peer);
			if (!c && !host.empty() && port > 0)
			{
				hander_t s = xhqm::network::connect_tcp_auto_timeout(host.c_str(), port, static_cast<int>(m_s));
				if (s != s_bad)
				{
					set_nonblock(s);   // 交给 reactor 后一律非阻塞
					auto sock = std::make_shared<conn_t>();
					sock->attach(s);
					std::shared_ptr<conn> fresh = make_conn(peer, sock);   // owner = this → 收帧走本端点的 frame_signal
					// 并发下可能已有别人抢先建好：新连接没进表就收掉，用表里那条
					auto exist = ipv6_conn_find(peer);
					if (exist && exist != fresh) { drop_conn(fresh); c = exist; }
					else { c = fresh; }
				}
			}
			if (!c) return false;

			// 入队（有界，满丢最旧）；timeout>0 时等「已落内核」——作为活性信号
			{
				std::lock_guard<std::mutex> lk(c->mutex);
				if (c->outbox.size() >= s_outbox) { c->outbox.pop_front(); }
				c->outbox.push_back(value);
				c->cond.notify_all();
			}
			wake();   // 立刻唤醒 reactor（否则要等 poll 超时，突发入队会被丢帧）
			if (timeout_ms > 0)
			{
				std::unique_lock<std::mutex> lk(c->mutex);
				const sint t = timeout_ms < 0 ? -timeout_ms : timeout_ms;
				c->cond.wait_for(lk, std::chrono::milliseconds(t),
					[&c] { return (c->outbox.empty() && !c->writing) || !c->good || !c->runing; });
			}
			return c->good;
		}

		// 无宿主端点的退化路径：连上 → 发一帧 → 关（没有 reactor，收不了回程帧）
		bool addr_ipv6::send_oneshot(const text& host, const sint& port, const nebula::json& value)
		{
			if (host.empty() || port <= 0) return false;
			hander_t s = xhqm::network::connect_tcp_auto_timeout(host.c_str(), port, static_cast<int>(m_s));
			if (s == s_bad) return false;
			bool ok = false;
			try
			{
				const std::string body = value.dump();
				ok = xhqm::network::send_frame(s, body);
			}
			catch (...) { ok = false; }
			shutdown_write(s);
			xhqm::network::close_socket(s);
			return ok;
		}

		bool addr_ipv6::send_frame(const json& value)
		{
			return deliver(uuid(), value, 0);
		}

		bool addr_ipv6::send_frame(const json& value, sint timeout_ms)
		{
			return deliver(uuid(), value, timeout_ms);
		}

		bool addr_ipv6::send_broad(const json& value)
		{
			// IPv6 没有广播：用链路本地全节点组播 ff02::1 近似，对每个已加入接口各发一份
			// （冗余换可用性：同机回环接口与各 LAN 接口都覆盖）。组播腿不可用（无组播能力接口 /
			// 端口被占）→ 退化为进程内广播，语义对齐 addr_uuid 与 addr_ipv4 的无 UDP 腿分支。
			std::string body;
			try { body = value.dump(); }
			catch (...) { return false; }

			hander_t g = s_bad;
			std::vector<unsigned int> ifaces;
			{
				std::lock_guard<std::mutex> lk(m_net_mutex);
				g = m_group;
				ifaces = m_ifaces;
			}
			if (g != s_bad && !ifaces.empty() && m_broad > 0 && !body.empty())
			{
				in6_addr group{};
				if (::inet_pton(AF_INET6, s_group, &group) == 1)
				{
					bool ok = false;
					for (unsigned int idx : ifaces)
					{
						const int ifidx = static_cast<int>(idx);
						::setsockopt(g, IPPROTO_IPV6, IPV6_MULTICAST_IF,
							reinterpret_cast<const char*>(&ifidx), sizeof(ifidx));
						sockaddr_in6 dst{};
						dst.sin6_family = AF_INET6;
						dst.sin6_addr = group;
						dst.sin6_port = htons(static_cast<unsigned short>(m_broad));
						dst.sin6_scope_id = idx;   // 链路本地组播必须带接口索引
						const int r = ::sendto(g, body.data(), static_cast<int>(body.size()), 0,
							reinterpret_cast<sockaddr*>(&dst), sizeof(dst));
						ok = ok || (r == static_cast<int>(body.size()));
					}
					if (ok) return true;
				}
			}

			// 退化：进程内广播（已打开端点全体）
			std::vector<addr_ipv6*> list;
			{
				std::lock_guard<std::mutex> lk(ipv6_mutex);
				for (auto& kv : ipv6_registry) { if (kv.second) list.push_back(kv.second); }
			}
			for (auto* one : list) { one->broad_signal.emit(value); }
			return !list.empty();
		}
	}
}
