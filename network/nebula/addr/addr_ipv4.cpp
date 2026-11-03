#include "addr_ipv4.h"

#include <algorithm>
#include <atomic>
#include <set>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#endif

namespace xhqm
{
	namespace nebula
	{
		// 两张进程级静态表（与 addr_uuid 的 uuid_registry 同构）：
		//   ipv4_registry —— 已打开的端点（uuid → 端点）：同 uuid 直投，进程内联调不绕 socket；
		//   ipv4_conns    —— 长连接表（对端 uuid → 连接）：端点与克隆句柄共用同一条长连，
		//                    也能复用别人拨进来的连接，所以同一对节点只有一条连接。
		static std::mutex ipv4_mutex;
		static std::map<nebula::uuid, addr_ipv4*, uuid_less> ipv4_registry;

		static std::mutex ipv4_conn_mutex;
		static std::map<nebula::uuid, std::shared_ptr<addr_ipv4::conn>, uuid_less> ipv4_conns;

		static addr_ipv4* ipv4_local(const nebula::uuid& id)
		{
			std::lock_guard<std::mutex> lk(ipv4_mutex);
			auto it = ipv4_registry.find(id);
			return (it != ipv4_registry.end()) ? it->second : nullptr;
		}

		// 克隆句柄（自身未 opened）找「把我挂在邻居表里的那个本地端点」：
		// 拨号与收帧都由它来做，收到的帧才会走它自己的 frame_signal（link 连的是它）。
		static addr_ipv4* ipv4_host_of(const nebula::uuid& peer, const addr_ipv4* self)
		{
			std::lock_guard<std::mutex> lk(ipv4_mutex);
			for (auto& kv : ipv4_registry)
			{
				addr_ipv4* one = kv.second;
				if (!one || one == self) continue;
				if (one->has_neighbor(peer)) { return one; }   // has_neighbor 走各自的表锁，无逆序
			}
			return nullptr;
		}

		static std::shared_ptr<addr_ipv4::conn> ipv4_conn_find(const nebula::uuid& peer)
		{
			if (!peer) return nullptr;
			std::lock_guard<std::mutex> lk(ipv4_conn_mutex);
			auto it = ipv4_conns.find(peer);
			if (it != ipv4_conns.end() && it->second && it->second->good) { return it->second; }
			return nullptr;
		}

		// 入表：已有一条**可用**连接就不动；陈旧（已断）条目则由新连接顶替
		static void ipv4_conn_put(const std::shared_ptr<addr_ipv4::conn>& c)
		{
			if (!c || !c->peer) return;
			std::lock_guard<std::mutex> lk(ipv4_conn_mutex);
			auto it = ipv4_conns.find(c->peer);
			if (it == ipv4_conns.end() || !it->second || !it->second->good) { ipv4_conns[c->peer] = c; }
		}

		static void ipv4_conn_erase(const std::shared_ptr<addr_ipv4::conn>& c)
		{
			if (!c || !c->peer) return;
			std::lock_guard<std::mutex> lk(ipv4_conn_mutex);
			auto it = ipv4_conns.find(c->peer);
			if (it != ipv4_conns.end() && it->second == c) { ipv4_conns.erase(it); }
		}

		// ------------------------------------------------ poll 平台原语 ----------------
		// 只用 POLLIN / POLLOUT 语义位，Windows 侧映射到 POLLRDNORM / POLLWRNORM。
		// 不用 select/fd_set：Windows 句柄值可超过 FD_SETSIZE(64)，FD_SET 会越界写栈（见 network.h 注释）。
#ifdef _WIN32
		using pollfd_t = WSAPOLLFD;
		static const short s_poll_read = POLLRDNORM;
		static const short s_poll_write = POLLWRNORM;
		static int poll_wait(pollfd_t* fds, int n, int ms) { return ::WSAPoll(fds, static_cast<ULONG>(n), static_cast<INT>(ms)); }
		static bool poll_bad(const pollfd_t& p) { return (p.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0; }
#else
		using pollfd_t = struct pollfd;
		static const short s_poll_read = POLLIN;
		static const short s_poll_write = POLLOUT;
		static int poll_wait(pollfd_t* fds, int n, int ms) { return ::poll(fds, static_cast<nfds_t>(n), ms); }
		static bool poll_bad(const pollfd_t& p) { return (p.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0; }
#endif

		// 非阻塞开关：连接一律非阻塞，send / recv 由 poll 的可写 / 可读驱动（阻塞 IO 会拖死整条 reactor）
		static bool sock_nonblocking(const xhqm::network::hander& s)
		{
			if (s == xhqm::network::error_socket) return false;
#ifdef _WIN32
			u_long mode = 1;
			return ::ioctlsocket(s, FIONBIO, &mode) == 0;
#else
			int flags = ::fcntl(s, F_GETFL, 0);
			if (flags < 0) return false;
			return ::fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
		}

		// 非阻塞 recv / send 的「暂时无数据 / 内核缓冲满」判定：不是错误，等下一轮 poll
		static bool would_block()
		{
#ifdef _WIN32
			const int e = ::WSAGetLastError();
			return (e == WSAEWOULDBLOCK || e == WSAEINTR);
#else
			return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR);
#endif
		}

		static void sock_close(xhqm::network::hander& s)
		{
			if (s != xhqm::network::error_socket)
			{
				xhqm::network::close_socket(s);
				s = xhqm::network::error_socket;
			}
		}

		static int sock_send(const xhqm::network::hander& s, const char* p, int n)
		{
#ifdef _WIN32
			return ::send(s, p, n, 0);
#else
			// MSG_NOSIGNAL：对端已关闭时回写会触发 SIGPIPE 打死整个进程（见 network.h send_bytes 注释）
			return ::send(s, p, n, MSG_NOSIGNAL);
#endif
		}

		// ------------------------------------------------ 生命周期 ----------------

		addr_ipv4::addr_ipv4(const text& host, const sint& port, const sint& broad)
			: addr_b(xhqm::nebula::kind::ipv4), m_host{ host }, m_port{ port }
			, m_broad{ broad > 0 ? broad : port }
		{
			// 裸用 network.h 原语（connect_tcp_auto_timeout / open_udp / bind_udp / listen_tcp）
			// 不会自动 WSAStartup：只有 tcp_client::connect* 与 tcp_server::listen 内部才调 init_network。
			// 而**克隆句柄自己拨号**时可能一个端点都没 opened 过 → 没有这一步就会 connect 报 10093
			// (WSANOTINITIALISED)，定向腿静默全丢。这里按仓库既有做法初始化；
			// 析构**不**调 exit_network()——WSAStartup 是引用计数的，WSACleanup 会把别人还在用的套接字一起废掉。
			xhqm::network::init_network();
		}

		addr_ipv4::~addr_ipv4()
		{
			this->closed();   // 克隆句柄也有名下连接要收，不能只在 opened 过时才收摊
		}

		// 本机对外 IPv4（默认路由出口）。用途：通配地址不是**可拨号**地址——若把 0.0.0.0 原样播给对端，
		// 对端会去 connect 0.0.0.0，定向腿（TCP 长连接）整条失效，只剩广播能用。
		// 上一次 UDP connect 只为让内核按默认路由选出口地址（不发包），失败返回空串。
		static xhqm::text local_ipv4_hint()
		{
			xhqm::network::hander s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
			if (s == xhqm::network::error_socket) return xhqm::text{};
			sockaddr_in dst{};
			dst.sin_family = AF_INET;
			dst.sin_port = htons(53);
			dst.sin_addr.s_addr = inet_addr("8.8.8.8");
			xhqm::text out;
			if (::connect(s, reinterpret_cast<sockaddr*>(&dst), sizeof(dst)) == 0)
			{
				sockaddr_in src{};
				socklen_t len = sizeof(src);   // Linux: getsockname 要 socklen_t*（Windows 用 int 的习惯在此不成立）
				if (::getsockname(s, reinterpret_cast<sockaddr*>(&src), &len) == 0)
				{
					char buf[INET_ADDRSTRLEN]{};
					if (::inet_ntop(AF_INET, &src.sin_addr, buf, sizeof(buf))) out = buf;
				}
			}
#ifdef _WIN32
			::closesocket(s);
#else
			::close(s);
#endif
			return out;
		}

		nebula::json addr_ipv4::make_json() const
		{
			nebula::json j = addr_b::make_json();
			j["host"] = m_host;
			// 通配/空 host：广播出去时给对端一个能拨的地址（本机对外 IPv4）
			if (m_host.empty() || m_host == "0.0.0.0" || m_host == "*")
			{
				const xhqm::text local = local_ipv4_hint();
				if (!local.empty()) { j["host"] = local; }
			}
			j["port"] = m_port;
			j["broad"] = m_broad;
			return j;
		}

		size addr_ipv4::conn_count() const
		{
			std::set<conn*> seen;
			{
				std::lock_guard<std::mutex> lk(m_conn_mutex);
				for (const auto& c : m_in) { if (c) seen.insert(c.get()); }
			}
			{
				std::lock_guard<std::mutex> lk(m_live_mutex);
				for (const auto& c : m_live) { if (c) seen.insert(c.get()); }
			}
			{
				std::lock_guard<std::mutex> lk(ipv4_conn_mutex);
				for (const auto& kv : ipv4_conns) { if (kv.second && kv.second->owner == this) seen.insert(kv.second.get()); }
			}
			return seen.size();
		}

		size addr_ipv4::conn_total()
		{
			std::lock_guard<std::mutex> lk(ipv4_conn_mutex);
			return ipv4_conns.size();
		}

		bool addr_ipv4::opened()
		{
			if (m_runing) return true;   // 幂等
			m_stop = false;              // 允许 reactor 重新跑（closed 后再 opened）
			m_runing = true;
			m_closed = false;
			m_opened = true;             // addr_b::is_open() 的运行标志（此前从不置位 → 恒假）

			xhqm::network::init_network();
			// UDP：常驻一个套接字，收发共用（源端口 = 广播端口，稳定、可回）。
			// 保持阻塞模式：只在 poll 报告可读后才调 recvfrom_udp，一次一定拿到数据报，不会阻塞 reactor。
			xhqm::network::hander udp = xhqm::network::error_socket;
			if (m_broad > 0)
			{
				xhqm::network::hander s = xhqm::network::open_udp();
				if (s != xhqm::network::error_socket && xhqm::network::bind_udp(s, m_broad))
				{
					udp = s;
				}
				else { sock_close(s); }   // 绑不上（端口占用）→ 退化为进程内广播
			}
			// TCP：监听本端点端口（与 UDP 端口空间互不冲突，可同号）。
			// 直接用 hander 而非 tcp_server：监听套接字也要非阻塞（poll 报可读到 accept 之间可能被抢走），
			// 而 tcp_server 不暴露裸句柄。
			xhqm::network::hander answer = xhqm::network::error_socket;
			if (m_port > 0)
			{
				xhqm::network::hander s = xhqm::network::open_tcp();
				if (s != xhqm::network::error_socket
					&& xhqm::network::listen_tcp(s, m_port)
					&& sock_nonblocking(s))
				{
					answer = s;
				}
				else { sock_close(s); }
			}
			// 唤醒管：一对**回环 IPv4** UDP 套接字（写端发 1 字节 → 读端被 poll 报告可读）。
			// 不能用 network::open_udp()：它优先建 AF_INET6 双栈套接字，回环地址不能按 IPv4 填。
			// 读端非阻塞：只在 poll 报告可读后才 recv，一次一定拿到那 1 字节，不会挂住 reactor。
			xhqm::network::hander wake_from = xhqm::network::error_socket;
			xhqm::network::hander wake_to = xhqm::network::error_socket;
			sint wake_port = 0;
			{
				xhqm::network::hander a = xhqm::network::open_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
				xhqm::network::hander b = xhqm::network::open_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
				sockaddr_in self{};
				self.sin_family = AF_INET;
				self.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);   // 127.0.0.1
				self.sin_port = 0;                                // 端口由系统分配
				socklen_t len = sizeof(self);  // 同上
				const bool binda = (a != xhqm::network::error_socket)
					&& (::bind(a, reinterpret_cast<sockaddr*>(&self), sizeof(self)) == 0)
					&& (::getsockname(a, reinterpret_cast<sockaddr*>(&self), &len) == 0)   // 取回系统分配的端口
					&& sock_nonblocking(a);
				if (binda && b != xhqm::network::error_socket
					&& ::connect(b, reinterpret_cast<sockaddr*>(&self), sizeof(self)) == 0)
				{
					wake_from = a; wake_to = b; wake_port = ::ntohs(self.sin_port);
					m_wake_addr = self;
				}
				// 建不起来（极少）就退化为纯超时轮询：功能仍正确，只是突发帧可能被有界队列丢弃
				if (wake_from == xhqm::network::error_socket) { sock_close(a); sock_close(b); }
			}
			{
				// m_answer / m_udp 是 reactor 线程每轮都要读的句柄（原子，避免两线程竞争读写）；
				// m_reactor 只在 opened / closed 与 reactor 之间传手，全部在 m_live_mutex 内：
				//   opened 装填（reactor 还没起） → reactor 持锁取连接快照 → closed 先置 m_stop / m_runing=false
				//   并取得 join → reactor 退出后才 close 句柄，故 reactor 眼中句柄恒有效。
				std::lock_guard<std::mutex> lk(m_live_mutex);
				m_udp = udp;
				m_answer = answer;
				m_wake_from = wake_from;
				m_wake_to = wake_to;
				m_wake_port = wake_port;
				m_wake_flag = false;
			}
			{
				std::lock_guard<std::mutex> lk(ipv4_mutex);
				ipv4_registry[uuid()] = this;
			}
			// reactor 线程：本端点有任一条网络腿（TCP 监听 / UDP 广播）就起；纯进程内端点（直投）不必空转。
			std::lock_guard<std::mutex> lk(m_live_mutex);
			if (m_answer.load() != xhqm::network::error_socket || m_udp.load() != xhqm::network::error_socket) { reactor_open(); }
			return true;
		}

		void addr_ipv4::closed()
		{
			if (m_closed.exchange(true)) return;   // 幂等（未 opened 的克隆首次调用也要走完收摊）
			m_opened = false;                       // 与 opened() 配对维护 is_open()
			m_stop = true;                          // 让 reactor 最多 s_react_ms 后看到并退出
			m_runing = false;                       // 之后 reactor 不再把监听 / UDP 放进 poll 集合

			{
				std::lock_guard<std::mutex> lk(ipv4_mutex);
				auto it = ipv4_registry.find(uuid());
				if (it != ipv4_registry.end() && it->second == this) { ipv4_registry.erase(it); }
			}
			// 唤醒所有等队列的发送方：reactor 走了，没人再帮它们出队
			{
				std::vector<std::shared_ptr<conn>> wake;
				{
					std::lock_guard<std::mutex> lk(m_live_mutex);
					wake = m_live;
				}
				for (auto& c : wake) { std::lock_guard<std::mutex> lk(c->mutex); c->cond.notify_all(); }
			}

			// 停「本对象名下」的连接（m_in + m_live + 进程级表里 owner == this 的）。
			// stop_conn 不发帧、不 join：只清标志 + 关套接字 + 退进程级表，可与 reactor 并发。
			stop_all();

			if (m_reactor.joinable()) { m_reactor.join(); }   // 最坏 ≈ s_react_ms + 本轮 handler 耗时

			// reactor 线程若已自行摘过，这里是兜底：把还挂在进程级表里的自己名下条目清干净
			std::vector<std::shared_ptr<conn>> left;
			{
				std::lock_guard<std::mutex> lk(ipv4_conn_mutex);
				for (auto it = ipv4_conns.begin(); it != ipv4_conns.end(); )
				{
					if (it->second && it->second->owner == this) { left.push_back(it->second); it = ipv4_conns.erase(it); }
					else { ++it; }
				}
			}
			for (auto& c : left) { stop_conn(c); }

			{
				// join 之后 reactor 已不再运行：此处独占 m_live 才安全
				std::lock_guard<std::mutex> lk(m_live_mutex);
				m_live.clear();
			}
			// 句柄最后关（reactor 已退出）：监听 + UDP 广播 + 唤醒管
			xhqm::network::hander answer = m_answer.exchange(xhqm::network::error_socket);
			sock_close(answer);
			xhqm::network::hander udp = m_udp.exchange(xhqm::network::error_socket);
			sock_close(udp);
			xhqm::network::hander wake_to = m_wake_to.exchange(xhqm::network::error_socket);
			sock_close(wake_to);
			m_wake_flag = false;
			xhqm::network::hander wake_from = m_wake_from.exchange(xhqm::network::error_socket);
			sock_close(wake_from);
		}

		// ------------------------------------------------ 连接登记 ----------------

		void addr_ipv4::reactor_open()
		{
			// 调用者须持 m_live_mutex：保证「起线程」与 closed() 的 join 不会交叉
			if (m_stop || m_reactor.joinable()) return;   // 已关停 / 已在跑
			m_reactor = std::thread([this] { this->reactor_runing(); });
		}

		// 入队即唤醒：往唤醒管写 1 字节，reactor 立刻从 poll 返回并把发队列抽干。
		// 突发帧场景（测试里 200 帧的紧循环）全靠它——否则每轮 poll 超时才出队一次，
		// 有界队列（s_outbox）会先把超出的帧按「满丢最旧」丢掉。m_wake_flag 合并连发，
		// 只有「已有一字节在途」时才不发，保证唤醒不会丢失。
		void addr_ipv4::wakeup()
		{
			const xhqm::network::hander s = m_wake_to.load();
			if (s == xhqm::network::error_socket) return;
			if (m_wake_flag.exchange(true)) return;   // 已有一字节在途：合并
			const char one = 1;
			if (::send(s, &one, 1, 0) <= 0) { m_wake_flag = false; }
		}

		void addr_ipv4::reactor_add_live(const std::shared_ptr<conn>& c)
		{
			if (!c) return;
			std::lock_guard<std::mutex> lk(m_live_mutex);
			if (c->owner == nullptr)
			{
				c->owner = this;
				// 克隆句柄兜底路径：它没有 opened（不进注册表、没别人替它收帧）→ 自己开 reactor
				if (!m_runing) { reactor_open(); }
			}
			if (c->owner != this) return;
			m_live.push_back(c);
		}

		std::shared_ptr<addr_ipv4::conn> addr_ipv4::make_conn(const nebula::uuid& peer, const nebula::uuid& node,
			const xhqm::network::hander& sock, bool dialed)
		{
			if (sock == xhqm::network::error_socket) return nullptr;
			auto c = std::make_shared<conn>();
			c->sock = sock;
			c->peer = peer;
			c->node = node;
			c->dialed = dialed;
			c->last = xhqm::timesys::ntime();
			c->good = true;
			c->runing = true;
			reactor_add_live(c);     // 认领归属（可能顺带拉起 reactor）
			ipv4_conn_put(c);        // 对端已知（拨号侧）才入表；接入侧等 learn_conn
			return c;
		}

		void addr_ipv4::conn_drop(const std::shared_ptr<conn>& c)
		{
			if (!c) return;
			stop_conn(c);            // 内部会退进程级表
			{
				std::lock_guard<std::mutex> lk(m_live_mutex);
				m_live.erase(std::remove(m_live.begin(), m_live.end(), c), m_live.end());
			}
		}

		void addr_ipv4::stop_all()
		{
			// 三个来源合并（可能重合）：本对象接入的 m_in、本 reactor 的 m_live、进程级表里 owner == this 的
			std::vector<std::shared_ptr<conn>> mine;
			{
				std::lock_guard<std::mutex> lk(m_conn_mutex);
				mine = m_in;
			}
			{
				std::lock_guard<std::mutex> lk(m_live_mutex);
				for (const auto& c : m_live) { if (c) mine.push_back(c); }
			}
			{
				std::lock_guard<std::mutex> lk(ipv4_conn_mutex);
				for (auto it = ipv4_conns.begin(); it != ipv4_conns.end(); )
				{
					if (it->second && it->second->owner == this) { mine.push_back(it->second); it = ipv4_conns.erase(it); }
					else { ++it; }
				}
			}
			for (auto& c : mine) { conn_drop(c); }
			{
				std::lock_guard<std::mutex> lk(m_conn_mutex);
				m_in.clear();
			}
		}

		void addr_ipv4::stop_conn(const std::shared_ptr<conn>& c)
		{
			if (!c) return;
			c->runing = false;       // 先置标志：reactor 本轮即便还在快照里也会跳过
			c->good = false;
			{
				std::lock_guard<std::mutex> lk(c->mutex);
				c->cond.notify_all();   // 唤醒等队列排空的发送方
			}
			xhqm::network::hander s = c->sock;
			sock_close(s);           // 非阻塞套接字：close 不阻塞；reactor 拿旧句柄只会得 WSAENOTSOCK
			c->sock = s;
			ipv4_conn_erase(c);
		}

		// 克隆句柄都靠自己拨号（找不到代拨端点）时，复用已有的一条**接入**连接回发：
		// 本端点名下恰好只有一条接入连接（且路由键与本次目标不同，例如回程帧 send_node 与 send_addr.uuid 不同）
		// 就可以安全地借它承载本次发送——避免每个路由标签各拨一条半死连接。
		std::shared_ptr<addr_ipv4::conn> addr_ipv4::adopt_single()
		{
			std::shared_ptr<conn> found;
			std::lock_guard<std::mutex> lk(m_live_mutex);
			for (const auto& c : m_live)
			{
				if (!c || !c->runing || c->dying || c->dialed) continue;
				if (found) { return nullptr; }   // 多于一条：不猜
				found = c;
			}
			return found;
		}

		// 同一对节点可能同时互相拨号（各建一条 TCP）。两条都活着、却各走各的一半 = 半死链路，
		// 所以要在 poll 轮次之间（不能在某条连接正在解析收帧缓冲时）收敛掉多余的那条：
		//   本端点 uuid 较大、且名下同时有「我 dial 出去的」与「对端拨进来的」同节点连接 → 收掉我 dial 的那条
		//   （对端 uuid 更小，本来就该由它拨进来）。反之（我 uuid 更小）保留自己的 dial。
		// 同一对节点可能同时互相拨号（各建一条 TCP）。两条都活着、却各走各的一半 = 半死链路，
		// 所以要在 poll 轮次之间（不能在某条连接正在解析收帧缓冲时）收敛掉多余的那条：
		//   本端点 uuid 较大、且名下同时有「我 dial 出去的」与「对端拨进来的」同节点连接 → 收掉我 dial 的那条
		//   （对端 uuid 更小，本来就该由它拨进来）。反之（我 uuid 更小）保留自己的 dial。
		// **绝不动唯一的那条连接**：宁可有短暂重复，也不能让某个方向没有可用链路。
		void addr_ipv4::reconcile_live(const std::vector<std::shared_ptr<conn>>& live)
		{
			for (const auto& c : live)
			{
				if (!c || !c->runing || !c->node || c->dying || !c->dialed) continue;
				if (uuid_less()(uuid(), c->node)) continue;      // 我 uuid 更小：这条 dial 该留
				bool has_inbound = false;
				for (const auto& o : live)
				{
					if (o && o != c && o->runing && !o->dying && !o->dialed && o->node == c->node) { has_inbound = true; break; }
				}
				if (!has_inbound) continue;                      // 没有替代链路：保留
				c->dying = true;
				c->good = false;
				ipv4_conn_erase(c);
			}
		}

		void addr_ipv4::learn_conn(const std::shared_ptr<conn>& c, const nebula::json& frame)
		{
			if (!c) return;
			if (!c->node)
			{
				// 对端**真实节点** uuid：优先取帧里它的地址快照 send_addr.uuid（link 的广播/发送帧都带），
				// 否则退回 send_node —— 后者是发送方 uuid，只在「对方用自己 uuid 当路由标签」时等价。
				nebula::uuid node;
				if (frame.contains("send_addr") && frame["send_addr"].is_object())
				{
					node = nebula::uuid::to_uuid(frame["send_addr"].value("uuid", ""));
				}
				if (!node) { node = nebula::uuid::to_uuid(frame.value("send_node", "")); }
				if (!node) return;   // 帧里没有可用身份：保持未知，等后续帧
				c->node = node;
				if (!c->peer) { c->peer = node; }   // 接入侧：路由键 = 对端真实 uuid（行为与原实现一致）
			}
			if (!c->peer) { c->peer = c->node; }
			ipv4_conn_put(c);   // 接入连接登记进进程级表，之后谁都能复用它回发
		}

		// ------------------------------------------------ reactor ----------------
		// 唯一的 I/O 线程：一份 poll 集合同时管 {监听 accept、各连接读、待写连接写、UDP 广播收}。
		// 关停：closed() 置 m_stop（并 m_runing=false 使监听 / UDP 立刻退出 poll 集合）；
		// poll 超时 s_react_ms 毫秒一轮，所以最坏 s_react_ms 毫秒内看到标志并退出 → join 有界。
		void addr_ipv4::reactor_runing()
		{
			std::vector<std::shared_ptr<conn>> live;      // 本轮快照（锁外遍历；shared_ptr 保活）
			std::vector<std::shared_ptr<conn>> refs;      // 与 pfds 一一对应的连接
			std::vector<int> kinds;                       // 0 = 监听，1 = TCP 连接，2 = UDP
			std::vector<pollfd_t> pfds;
			xhqm::network::hander answer = xhqm::network::error_socket;
			xhqm::network::hander udp = xhqm::network::error_socket;
			xhqm::network::hander wake = xhqm::network::error_socket;
			const xhqm::u64 idle_ns = xhqm::u64(m_idle) * 1000000ull;   // 毫秒 → 纳秒

			while (!m_stop)
			{
				pfds.clear(); kinds.clear(); refs.clear();
				{
					// 持锁只取连接快照（内存操作），随后全部 IO 在锁外
					std::lock_guard<std::mutex> lk(m_live_mutex);
					live = m_live;
				}
				answer = m_answer.load();
				udp = m_udp.load();
				wake = m_wake_from.load();
				if (m_runing && answer != xhqm::network::error_socket)
				{
					pollfd_t p{};
					p.fd = answer; p.events = s_poll_read;
					pfds.push_back(p); kinds.push_back(0); refs.push_back(nullptr);
				}
				if (m_runing && udp != xhqm::network::error_socket)
				{
					pollfd_t p{};
					p.fd = udp; p.events = s_poll_read;
					pfds.push_back(p); kinds.push_back(2); refs.push_back(nullptr);
				}
				if (wake != xhqm::network::error_socket)
				{
					pollfd_t p{};
					p.fd = wake; p.events = s_poll_read;
					pfds.push_back(p); kinds.push_back(3); refs.push_back(nullptr);
				}
				for (const auto& c : live)
				{
					if (!c || !c->runing || c->sock == xhqm::network::error_socket) continue;
					pollfd_t p{};
					p.fd = c->sock; p.events = s_poll_read;
					{
						std::lock_guard<std::mutex> lk(c->mutex);
						if (!c->outbox.empty()) { p.events |= s_poll_write; }
					}
					pfds.push_back(p); kinds.push_back(1); refs.push_back(c);
				}

				const int total = static_cast<int>(pfds.size());
				const int got = (total > 0) ? poll_wait(pfds.data(), total, static_cast<int>(s_react_ms)) : 0;
				// got < 0：poll 自身出错（例如集合里刚有句柄被别处 close）：本轮到超时分支重来
				if (got > 0)
				{
					for (int i = 0; i < total && !m_stop; ++i)
					{
						const short re = pfds[i].revents;
						if (re == 0) continue;
						if (kinds[i] == 0)
						{
							if (re & s_poll_read) { this->accept_tcp(); }             // 接入一条；剩余 pending 下轮再取
						}
						else if (kinds[i] == 3)
						{
							// 唤醒管：收掉那 1 字节并清标志，随后本轮就是「出队轮」
							if (re & s_poll_read)
							{
								char sink[64];
								while (::recv(wake, sink, sizeof(sink), 0) > 0) {}
								m_wake_flag = false;
							}
						}
						else if (kinds[i] == 2)
						{
							if (re & s_poll_read) { this->recv_udp(); }
						}
						else
						{
							std::shared_ptr<conn> c = refs[i];
							if (!c || !c->runing) continue;
							const short want = pfds[i].events;
							if ((re & want & s_poll_write) && !poll_bad(pfds[i])) { this->send_tcp(c); }
							if (c->runing && (re & want & s_poll_read)) { this->recv_tcp(c); }
						}
					}
				}

				// 出队轮（drain-until-empty）：无论本轮被谁唤醒（连接可写 / 唤醒管 / 超时），
				// 都把每条连接的发队列抽干，直到 EWOULDBLOCK。这一条是「突发帧不丢」的关键：
				// 只靠 poll 超时轮询的话，一轮只出队一次，有界队列会先把超出的帧按「满丢最旧」丢掉。
				// 注意用本轮结束后的最新 m_live 快照（accept 进来的新连接也在里面）。
				{
					std::lock_guard<std::mutex> lk(m_live_mutex);
					live = m_live;
				}
				for (const auto& c : live)
				{
					if (!c || !c->runing || c->sock == xhqm::network::error_socket) continue;
					{
						std::lock_guard<std::mutex> lk(c->mutex);
						if (c->outbox.empty()) continue;
					}
					this->send_tcp(c);   // 非阻塞 send：写满即 EWOULDBLOCK 返回，不会挂住 reactor
				}

				// 重复连接收敛 + 空闲回收：这么久没收到任何帧即收连接（含「接入但一直没学出对端 uuid」的僵尸连接），
				// 以及收敛输掉的那条（dying：不能在帧解析中途摘，必须等本轮 poll 处理完）
				const xhqm::u64 now = xhqm::timesys::ntime();
				for (const auto& c : live)
				{
					if (!c || !c->runing) continue;
					if (c->dying) { conn_drop(c); continue; }
					if ((now - c->last.load()) <= idle_ns) continue;
					conn_drop(c);
				}
				reconcile_live(live);
			}
			// 退出前兜底：停掉所有名下连接（shared_ptr 保证此处安全）
			std::vector<std::shared_ptr<conn>> last;
			{
				std::lock_guard<std::mutex> lk(m_live_mutex);
				last = m_live;
			}
			for (auto& c : last) { stop_conn(c); }
		}

		// ---------------- 读：POLLIN ----------------

		void addr_ipv4::recv_tcp(const std::shared_ptr<conn>& c)
		{
			if (!c) return;
			char buf[16384];
			// 一轮最多读 64KB，避免一条大流量连接饿死同一 reactor 上的其他连接
			for (int round = 0; round < 4 && c->runing; ++round)
			{
				const int r = ::recv(c->sock, buf, static_cast<int>(sizeof(buf)), 0);
				if (r > 0) { c->inbox.append(buf, static_cast<xhqm::size>(r)); }
				else if (r == 0) { conn_drop(c); return; }        // 对端正常关闭
				else
				{
					if (would_block()) break;                     // 本轮读干净了，等下一轮 poll
					conn_drop(c); return;                         // 真错误：收连接
				}

				// 逐帧提取：4 字节长度前缀 + 负载（协议见 network.h 的 send_frame/recv_frame）
				bool progress = true;
				while (progress && c->runing)
				{
					progress = false;
					if (c->need == 0)
					{
						if (c->inbox.size() < 4) break;
						std::uint8_t hdr[4];
						std::memcpy(hdr, c->inbox.data(), 4);
						const std::uint32_t len = xhqm::network::get_u32(hdr);
						if (len > (1u << 30)) { conn_drop(c); return; }   // 上限 1GB：异常长度视为坏连接
						c->inbox.erase(0, 4);
						c->need = len;
						if (len == 0) { c->need = 0; continue; }          // 空帧：直接找下一帧
					}
					if (c->inbox.size() < c->need) break;
					text data = c->inbox.substr(0, c->need);
					c->inbox.erase(0, c->need);
					c->need = 0;
					progress = true;

					c->last = xhqm::timesys::ntime();   // 收到帧即刷新空闲计时
					try
					{
						json frame = json::parse(data);
						if (!frame.is_object() || !frame.contains("type")) continue;
						learn_conn(c, frame);               // 接入侧：首帧 send_node → 记住对端 uuid 并登记连接
						if (!c->dying) { this->frame_signal.emit(frame); }   // 输掉仲裁的连接不再上报
					}
					catch (...) {}
				}
				if (static_cast<xhqm::size>(r) < sizeof(buf)) break;   // 没读满 → 内核已空
			}
		}

		// ---------------- 写：POLLOUT ----------------

		void addr_ipv4::send_tcp(const std::shared_ptr<conn>& c)
		{
			if (!c || !c->runing) return;
			std::unique_lock<std::mutex> lk(c->mutex);
			if (c->outbox.empty()) return;
			// 部分写：sent = 队首帧已发出的字节数，写不完保留队首、下轮 POLLOUT 接着写
			while (c->runing && !c->outbox.empty())
			{
				const text& data = c->outbox.front();
				const char* p = data.data() + c->sent;
				const int n = static_cast<int>(data.size() - c->sent);
				if (n <= 0)
				{
					c->outbox.pop_front();
					c->sent = 0;
					continue;
				}
				lk.unlock();                                  // 真正发帧不持队列锁（socket 已非阻塞，安全）
				const int r = sock_send(c->sock, p, n);
				lk.lock();
				if (r > 0)
				{
					c->last = xhqm::timesys::ntime();   // 有成功流量即刷新空闲计时（纯发送方向也要保活）
					c->sent += static_cast<xhqm::size>(r);
					if (c->sent >= c->outbox.front().size())
					{
						c->outbox.pop_front();
						c->sent = 0;
						c->cond.notify_all();                 // 通知等待版 send_frame：队列在排空
					}
				}
				else if (r < 0 && would_block())
				{
					break;                                    // 内核缓冲满：等下一轮 POLLOUT
				}
				else
				{
					lk.unlock();
					conn_drop(c);                             // 写失败 = 对端不可达：收连接
					return;
				}
			}
			c->cond.notify_all();
		}

		// ---------------- UDP 广播收包：POLLIN ----------------

		void addr_ipv4::recv_udp()
		{
			const xhqm::network::hander s = m_udp.load();
			if (s == xhqm::network::error_socket) return;
			// 只在 poll 报告可读后调一次：阻塞 UDP 套接字此时一定有整条数据报，不会挂住 reactor
			std::vector<char> buf(65536);   // UDP datagram 上限
			std::size_t got = 0;
			text host; unsigned short port = 0;
			if (!xhqm::network::recvfrom_udp(s, buf.data(), buf.size(), got, host, port)) return;
			// 远端字节不可信：畸形包 parse 抛异常会顺本线程冒出 → std::terminate
			try
			{
				json frame = json::parse(std::string(buf.data(), got));
				if (frame.is_object()) { this->broad_signal.emit(frame); }
			}
			catch (...) {}
		}

		// ---------------- 接入：POLLIN（监听） ----------------

		bool addr_ipv4::accept_tcp()
		{
			xhqm::network::hander s = m_answer.load();
			if (s == xhqm::network::error_socket) return false;
			s = xhqm::network::accept_tcp(s);   // 监听已非阻塞：无 pending 连接返回 error_socket
			if (s == xhqm::network::error_socket) return false;
			if (!sock_nonblocking(s)) { sock_close(s); return false; }
			std::shared_ptr<conn> c = make_conn(nebula::uuid{}, nebula::uuid{}, s, false);   // 对端身份由首帧学得
			if (!c) { sock_close(s); return false; }
			{
				std::lock_guard<std::mutex> lk(m_conn_mutex);
				m_in.push_back(c);   // 未识别对端前先由本对象保活
			}
			return true;
		}

		// ------------------------------------------------ 发送 ----------------

		addr_ipv4* addr_ipv4::select_host(const nebula::uuid& peer)
		{
			// 1) 已打开的端点自己干；2) 克隆句柄交给「把它挂进邻居表的本地端点」——
			//    否则连接挂在克隆上，回程帧只会发到克隆的信号，而 link 只连自己端点的信号。
			if (m_runing) return this;
			if (addr_ipv4* host = ipv4_host_of(peer, this)) return host;
			return this;   // 还没被任何端点收作邻居：退化为自己拨号（自己开 reactor，析构时自己收摊）
		}

		bool addr_ipv4::send_remote(const nebula::uuid& peer, const nebula::uuid& node, const text& host, const sint& port,
			const nebula::json& value, sint timeout_ms)
		{
			if (!peer || peer == uuid()) return false;   // 只带自己 uuid 的路由标签：不拨号、不自连
			// 进程级长连表：出向 / 入向复用，同一对节点只有一条（原语义不变）
			std::shared_ptr<conn> c = ipv4_conn_find(peer);
			// 路由键不同（回程帧的 send_node 与本次目标标签不同）时，退一步在**本端点名下**找可用连接：
			// 按路由键或已学到的真实节点 uuid 命中都算，命中就用它回发，绝不因此再拨一条
			// （否则同一对节点会堆出多条各自半死的链路）。
			if (!c)
			{
				// 路由键未命中：在本端点名下按路由键 / 真实节点 uuid 再找一次（回程帧两者常不同）
				std::lock_guard<std::mutex> lk(m_live_mutex);
				for (const auto& one : m_live)
				{
					if (!one || !one->runing || one->dying) continue;
					if (one->peer == peer || (node && one->node == node)) { c = one; break; }
				}
			}
			// 克隆句柄兜底：本端点名下只有一条接入连接时借它回发
			if (!c && !m_runing) { c = adopt_single(); }
			// 没有就按 target 的 host/port 拨号（阻塞在 connect 超时上，但不持任何成员锁）
			if (!c && !host.empty() && port > 0)
			{
				xhqm::network::hander s = xhqm::network::connect_tcp_auto_timeout(
					host.c_str(), port, static_cast<int>(m_s));
				if (s != xhqm::network::error_socket)
				{
					sock_nonblocking(s);   // 建连后挂牌为非阻塞，收发全部交给 poll
					// owner = 本对象 → 收帧走本端点的 frame_signal；node 已知时一并登记（用于重复连接收敛）
					std::shared_ptr<conn> fresh = make_conn(peer, node, s, true);
					// 并发下可能已有别人抢先建好：新连接没进表就收掉，用表里那条
					auto exist = ipv4_conn_find(peer);
					if (fresh && exist && exist != fresh) { conn_drop(fresh); c = exist; }
					else if (fresh) { c = fresh; }
					else { sock_close(s); }
				}
			}
			if (!c) return false;

			// 入队（有界，满丢最旧）；timeout>0 时等队列排空——作为「已交给内核」的活性信号
			std::unique_lock<std::mutex> lk(c->mutex);
			if (!c->good) return false;
			if (c->outbox.size() >= s_outbox)
			{
				c->outbox.pop_front();   // 队满丢最旧（零号协议允许丢）
				c->sent = 0;             // 丢的是队首：部分写进度一并作废
			}
			// 帧级编码（4 字节长度前缀 + 负载）：由 reactor 在 POLLOUT 时非阻塞写出
			text frame(value.dump());
			text wire;
			wire.resize(4 + frame.size());
			xhqm::network::put_u32(reinterpret_cast<std::uint8_t*>(&wire[0]), static_cast<std::uint32_t>(frame.size()));
			if (!frame.empty()) { std::memcpy(&wire[4], frame.data(), frame.size()); }
			c->outbox.push_back(std::move(wire));
			c->cond.notify_all();
			lk.unlock();      // 唤醒只要 1 字节回环发送，不碰任何成员锁，但也不必占着队列锁
			this->wakeup();   // 入队即唤醒 reactor 出队（否则要等 poll 超时，突发帧会被有界队列丢掉）
			lk.lock();
			if (timeout_ms > 0)
			{
				const sint t = timeout_ms < 0 ? -timeout_ms : timeout_ms;
				c->cond.wait_for(lk, std::chrono::milliseconds(t),
					[&c] { return c->outbox.empty() || !c->good || !c->runing; });
			}
			return c->good;
		}

		bool addr_ipv4::send_frame(const json& value)
		{
			const nebula::uuid peer = uuid();
			if (!peer) return false;
			// 1) 本进程内已有该 uuid 的已打开端点（含自身）→ 直投，等价 addr_uuid 的进程内路由
			if (addr_ipv4* local = ipv4_local(peer))
			{
				local->frame_signal.emit(value);
				return true;
			}
			// 2) 谁来做这件事 —— 见 select_host；干活的一侧用自己的 poll 集合与 frame_signal。
			// node = 本邻居句柄承载的真实节点 uuid：广播求邻时由 addr_from 设好，
			// 与路由标签 peer 区分开，供重复连接收敛使用（收敛靠真实节点身份，不用标签）。
			return select_host(peer)->send_remote(peer, uuid(), m_host, m_port, value, 0);
		}

		bool addr_ipv4::send_frame(const json& value, sint timeout_ms)
		{
			const nebula::uuid peer = uuid();
			if (!peer) return false;
			if (addr_ipv4* local = ipv4_local(peer))
			{
				local->frame_signal.emit(value);
				return true;
			}
			return select_host(peer)->send_remote(peer, uuid(), m_host, m_port, value, timeout_ms);
		}

		bool addr_ipv4::send_broad(const json& value)
		{
			// 有 UDP 腿 → 走网上广播（同网共享广播端口；源端口稳定）；
			// 无 UDP 腿（纯进程内端点 / 广播端口被占）→ 退化为进程内广播，语义对齐 addr_uuid
			const xhqm::network::hander udp = m_udp.load();
			if (udp != xhqm::network::error_socket && m_broad > 0)
			{
				const text data = value.dump();
				return xhqm::network::sendto_broadcast_udp(udp, data.data(), data.size(),
					static_cast<int>(m_broad));
			}
			std::vector<addr_ipv4*> list;
			{
				std::lock_guard<std::mutex> lk(ipv4_mutex);
				for (auto& kv : ipv4_registry) { if (kv.second) list.push_back(kv.second); }
			}
			for (auto* one : list) { one->broad_signal.emit(value); }   // 锁外 emit：槽里再进注册表不自锁
			return !list.empty();
		}
	}
}
