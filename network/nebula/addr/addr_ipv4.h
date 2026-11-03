#ifndef XHQM_NEBULA_ADDR_IPV4
#define XHQM_NEBULA_ADDR_IPV4

// addr_ipv4：IPv4 传输（长连接 + 多路复用 reactor 版）。结构对齐 addr_uuid，把「进程内 emit」换成真实网络腿：
//   * 一个 addr_ipv4 = 一个端点，同时具备两种腿：TCP {host, port} 定向 + UDP {broad} 广播；
//   * 两张进程级静态表（与 addr_uuid 的 uuid_registry 同构）：
//       ipv4_registry —— 已 opened 的端点（uuid → 端点）：同 uuid 直投，进程内联调不绕 socket；
//       ipv4_conns    —— 长连接表（对端 uuid → 连接）：端点与它的克隆句柄**共用同一条长连**，
//                        也能复用别人拨进来的连接（TCP 全双工），所以同一对节点永远只有一条连接；
//   * 定向走长连接，I/O 全部由**一条 reactor 线程**用 WSAPoll（POSIX 为 poll）多路复用：
//       同一份 poll 集合里同时管住 {TCP 监听 accept、每条长连接的读、待写连接的写、UDP 广播收包}，
//       不再「每连接 2 线程（读 + 写）」；连接少则每端点 1 线程、连接多也是 1 线程；
//   * 唤醒 / 关停方案：
//       唤醒 —— **唤醒管**（一对回环 UDP 套接字，127.0.0.1:port）：send_remote 每次入队后写 1 字节，
//         reactor 立刻从 poll 返回并把发队列抽干（drain-until-empty）。这是「突发帧不丢」的关键：
//         只靠 poll 超时轮询的话一轮只出队一次，有界发队列会先把超出的帧按「满丢最旧」丢掉
//         （实测：200 帧紧循环只剩 64 帧到达）。
//       关停 —— 置 m_stop → 关监听/UDP/唤醒管 → join reactor。Windows 上跨线程 close 不保证唤醒
//         正阻塞在 poll 上的调用，所以兜底是 poll 超时（s_react_ms = 200ms）：
//         **最坏退出延迟 ≈ s_react_ms + 本轮 handler 耗时**（无帧时实测 join 在 200ms 内返回）。
//   * 连接一律非阻塞（ioctlsocket FIONBIO）：发由 POLLOUT 驱动出队（帧级 4 字节长度前缀 + 负载，
//     逐帧记录已发字节数 sent，写不完下轮接着写），收由 POLLIN 驱动，accept 与 UDP 收包同集合；
//   * 克隆句柄（addr_from 造、link 挂进邻居表的那种）只带对端的 host/port/uuid，是发送侧路由句柄：
//     自己不 opened、不进端点注册表、不额外开 reactor；它把「拨号 + 收帧」交给
//     ipv4_host_of() 找到的**本地端点**（把它挂进邻居表的那个），这样回程帧才会走那个端点的
//     frame_signal（link 连的是它）。只有找不到任何本地端点时才退化为自己开 reactor 自理；
//   * 重复连接收敛：两端可能同时互相拨号（各建一条 TCP），两条各走一半就是半死链路。
//     reconcile_live 在 poll 轮次之间收敛掉多余的那条（uuid 大的一侧丢自己 dial 的），
//     并且**绝不动唯一的那条连接**——宁可有短暂重复，也不让某个方向没有可用链路。
//
// 加锁纪律（重要）：持锁只做内存操作，任何可能阻塞的调用（send/recv/accept/poll/join）一律在锁外：
//   m_conn_mutex   —— 本对象入向连接表 m_in（进程级 Accept 登记）
//   m_live_mutex   —— 本 reactor 名下的连接快照 m_live（只做 vector 增删与 owner 赋值）
//   conn::mutex    —— 该连接的 outbox / sent / 运行标志；等队列排空用 cond 等待，**不**在持锁时发帧
//   连接表 ipv4_conn_mutex / 端点表 ipv4_mutex 为进程级静态表的锁；嵌套顺序固定为
//   端点表 → 连接表 → m_live → conn::mutex，且从不反向获取（has_neighbor 走 addr_b 自己的锁，无环）。

#include "./addr_b.h"
#include "../../tcp.h"
#include "../../udp.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace xhqm
{
	namespace nebula
	{
		class xhqm_io addr_ipv4 : public addr_b
		{
		public:
			using conn_t = xhqm::network::tcp_conn;
			using socket_t = xhqm::network::udp_socket;
			using answer_t = xhqm::network::tcp_server;

			// 一条长连接：单 reactor 多路复用（无读/写线程），发队列有界（零号协议允许丢与乱序：队满丢最旧）
			struct conn : public std::enable_shared_from_this<conn>
			{
				xhqm::network::hander sock = xhqm::network::error_socket;   // 已建立的帧级 TCP 连接（非阻塞）
				nebula::uuid peer{};                  // 路由键（表键）：拨号侧 = 目标 uuid 标签；接入侧由首帧 send_node 学得
				nebula::uuid node{};                  // 对端**真实节点** uuid（帧里 send_addr.uuid，退化取 send_node）：仅供重复连接收敛
				addr_ipv4* owner = nullptr;           // 本连接的 reactor 归属（拨号方或接入方）；闭锁后不再改
				std::deque<text> outbox;              // 待发帧（已带 4 字节长度前缀）：只在 reactor / send_remote 里动
				text inbox;                           // 收帧缓冲：长度前缀 + 负载累计（只由 owner 的 reactor 线程碰）
				xhqm::size need = 0;                  // 当前帧待收字节数（0 = 还在收 4 字节长度前缀）
				xhqm::size sent = 0;                  // 队首帧已发出的字节数（部分写）
				std::mutex mutex;                     // 队列锁（连接自带，跨对象共用同一条连接）
				std::condition_variable cond;         // 入队 / 排空 / 运行标志变化
				std::atomic<bool> runing{ false };    // 运行标志：假 = 本连接停止收发
				std::atomic<bool> good{ false };      // 连接可用（写失败 / 空闲超时置假）
				std::atomic<bool> dying{ false };     // 待摘除（重复连接仲裁失败；由 reactor 在本轮后收摊）
				bool dialed = false;                  // 方向：真 = 本端点 dial 出去的，假 = accept 接入的（仲裁依据）
				std::atomic<xhqm::u64> last{ 0 };     // 最近一次成功收/发帧的时刻（ns，空闲回收依据）
			};

			// 发队列上限：满丢最旧（零号协议允许丢）。
			// 取 1024（≈80KB/连接）：reactor 是单线程多路复用，接收侧要 parse + emit，突发时
			// 发送方一口气入队几百帧会瞬间超过小队列；64 太小（实测 200 帧紧循环只剩 64 帧到达），
			// 1024 让「入队即唤醒 + 出队抽干」有足够缓冲吸收突发，同时仍是有界队列。
			static constexpr xhqm::size s_outbox = 1024;
			static constexpr xhqm::size s_idle_ms = 30000;  // 空闲回收：这么久没收到任何帧即收连接
			static constexpr xhqm::size s_react_ms = 200;   // reactor poll 超时（毫秒）：决定唤醒/关停延迟上限
		protected:
			text m_host;                                   // 本端点主机（局域网广播用）
			sint m_port = 0;                               // 本端点 TCP 端口（定向长连接）
			sint m_broad = 0;                              // UDP 广播端口（同网共享）；0 = 与 m_port 同号
			xhqm::size m_s = 1000;                          // 建连 / 发送超时毫秒
			xhqm::size m_idle = s_idle_ms;                 // 空闲回收毫秒
			std::atomic<bool> m_runing{ false };           // 端点运行标志（opened / closed）
			std::atomic<bool> m_stop{ true };              // reactor 退出标志（最坏 m_react_ms 毫秒被看到）
			std::atomic<bool> m_closed{ true };             // closed() 幂等（克隆句柄也要能收摊）
			// TCP 监听句柄（opened 创建 + 非阻塞）：reactor 线程每轮 poll 都要读，故用原子
			std::atomic<xhqm::network::hander> m_answer{ xhqm::network::error_socket };
			// UDP 广播套接字句柄（opened 创建，收发共用）：同为 reactor 每轮必读，用原子
			std::atomic<xhqm::network::hander> m_udp{ xhqm::network::error_socket };
			// 唤醒管（自回环 UDP 对，只为把 reactor 从 poll 里叫醒）：Windows 上跨线程 close
			// 不保证唤醒阻塞中的 poll，且「入队即唤醒」是突发帧不丢的前提——发 1 字节到 m_wake_to，
			// reactor 在 m_wake_from 上收到即可空转一轮把发队列抽干；关停仍靠 m_stop + poll 超时兜底。
			std::atomic<xhqm::network::hander> m_wake_from{ xhqm::network::error_socket };  // reactor 读端
			std::atomic<xhqm::network::hander> m_wake_to{ xhqm::network::error_socket };    // 发送端（写）
			std::atomic<bool> m_wake_flag{ false };        // 合并连续唤醒：已有一字节在途就不再发
			sint m_wake_port = 0;                          // 唤醒管接收端端口（回环）
			struct sockaddr_in m_wake_addr{};              // 唤醒目标地址（127.0.0.1:m_wake_port）
			std::thread m_reactor;                         // 唯一的多路复用线程（accept + 读 + 写 + UDP 收）
			mutable std::mutex m_conn_mutex;               // 本对象入向连接表锁
			std::vector<std::shared_ptr<conn>> m_in;       // 我接入的长连（对端 uuid 未学前先挂这里，保活）
			mutable std::mutex m_live_mutex;               // 本 reactor 名下连接表锁（只做内存增删；conn_count 只读也要取）
			std::vector<std::shared_ptr<conn>> m_live;     // 本 reactor 负责 poll 的连接
		protected:
			std::shared_ptr<conn> make_conn(const nebula::uuid& peer, const nebula::uuid& node, const xhqm::network::hander& sock, bool dialed);
			std::shared_ptr<conn> adopt_single();                                 // 克隆句柄兜底：本对象只有一条接入连接时借它回发
			void reconcile_live(const std::vector<std::shared_ptr<conn>>& live);   // 同一对节点只留一条连接（双方同时拨号时收敛）
			void wakeup();                                 // 入队即唤醒 reactor（1 字节回环 UDP，合并连发）
			void reactor_open();                           // 惰性起 reactor 线程（幂等，持 m_live_mutex 内调用）
			void reactor_add_live(const std::shared_ptr<conn>& c);  // 认领连接：owner 空则定为本人并在需要时开 reactor
			void conn_drop(const std::shared_ptr<conn>& c);        // 摘连接（可从任意线程调；阻塞 IO 已结束）
			void stop_conn(const std::shared_ptr<conn>& c);        // 停止某条连接：清标志 + 关套接字 + 通知等待者
			void stop_all();                                       // 停本对象名下全部连接（收摊用）
			void learn_conn(const std::shared_ptr<conn>& c, const nebula::json& frame);  // 首帧学对端 uuid → 进进程级连接表
			// 真正干活的一侧：用自己的 poll 集合与 frame_signal，按 target 的 host/port 拨号
			// node = 对端**真实节点** uuid（帧里 send_addr.uuid；无则留空）：只用于重复连接收敛，不做路由
			bool send_remote(const nebula::uuid& peer, const nebula::uuid& node, const text& host, const sint& port,
				const nebula::json& value, sint timeout_ms);
			addr_ipv4* select_host(const nebula::uuid& peer);      // 定「谁来做这件事」：本端点 / 把它挂邻居表的本地端点
			void reactor_runing();                                 // 唯一 reactor：poll 集合 = 监听 + 读 + 写 + UDP
			void recv_tcp(const std::shared_ptr<conn>& c);         // POLLIN：非阻塞读帧 → frame_signal
			void send_tcp(const std::shared_ptr<conn>& c);         // POLLOUT：非阻塞出队写帧
			void recv_udp();                                       // POLLIN：UDP 收包 → broad_signal
			bool accept_tcp();                                     // POLLIN：监听可读 → 接入新长连
		public:
			explicit addr_ipv4(const text& host = "", const sint& port = 0, const sint& broad = 0);
			virtual ~addr_ipv4() override;

			virtual json make_json() const override;       // + host / port / broad
			virtual bool opened() override;                // 绑 UDP + 监听 TCP + 进端点注册表（reactor 由 I/O 需要时惰性起）
			virtual void closed() override;                // 退注册表 + 停 reactor + 收自己名下的长连
			virtual bool send_broad(const json&) override; // UDP 广播（无 UDP 腿时退化为进程内广播）
			virtual bool send_frame(const json&) override; // 定向：进程内直投，否则长连接入队
			virtual bool send_frame(const json&, sint) override; // 定向 + 等「写出队列」

			inline const text& host() const { return m_host; }
			inline sint port() const { return m_port; }
			inline sint broad() const { return m_broad; }
			inline void send_time(xhqm::size ms) { m_s = ms; }
			inline void idle_time(xhqm::size ms) { m_idle = ms; }
			size conn_count() const;                       // 本对象名下的长连数（自检 / 测试用）
			static size conn_total();                      // 进程级长连表大小（自检 / 测试用）
		};
	}
}

#endif
