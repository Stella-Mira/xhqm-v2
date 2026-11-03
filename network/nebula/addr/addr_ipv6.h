#ifndef XHQM_NEBULA_ADDR_IPV6
#define XHQM_NEBULA_ADDR_IPV6

// addr_ipv6：IPv6 传输（长连接 + 单 reactor 版）。结构对齐 addr_ipv4，把「每连接两线程」
// 收敛为「每端点一条 WSAPoll reactor + 每连接有界发队列」：
//   * 一个 addr_ipv6 = 一个端点，同时具备两种腿：IPv6 TCP {host, port} 定向 + IPv6 组播 {broad} 发现；
//   * 两张进程级静态表（与 addr_ipv4 的 ipv4_registry / ipv4_conns 同构）：
//       ipv6_registry —— 已 opened 的端点（uuid → 端点）：同 uuid 直投，进程内联调不绕 socket；
//       ipv6_conns    —— 长连接表（对端 uuid → 连接）：端点与它的克隆句柄**共用同一条长连**，
//                        也能复用别人拨进来的连接（TCP 全双工），所以同一对节点只有一条连接；
//   * 线程模型：每端点 1 条 reactor 线程，统一 WSAPoll 监听 [监听套接字 | 组播套接字 | 唤醒管 | 名下全部长连]，
//     读写都在该线程内以非阻塞方式完成；发队列有界（零号协议允许丢与乱序：队满丢最旧）。
//     没有「每连接读/写线程」，所以关停只需 join reactor 一条线程，不存在连接线程 join 悬挂；
//   * 唤醒管：WSAPoll 不能等事件对象（Windows 无 eventfd），用一对回环 UDP 套接字自唤醒
//     （发端 sendto 1 字节）。否则 poll 的 200ms 粒度会让突发入队的帧在队列里被丢光；
//   * 广播：**IPv6 没有广播**（RFC 4291 起 IPv4 式广播语义在 IPv6 中不存在），这里选「组播」近似：
//     链路本地全节点组播 ff02::1（IPV6_JOIN_GROUP + IPV6_MULTICAST_IF/MULTICAST_LOOP），
//     发送时对每个成功加入的接口各发一份（冗余换可用性）。加入失败（无组播能力接口）则退化为
//     进程内广播，语义对齐 addr_uuid / addr_ipv4 的无 UDP 腿分支；限制见 .cpp 顶部注释；
//   * 克隆句柄（addr_from 造、link 挂进邻居表的那种）只带对端的 host/port/uuid，
//     是发送侧路由句柄：不 opened、不进端点注册表，把拨号与收帧交给「把它挂进邻居表的本地端点」，
//     没有任何宿主端点时退化为一次同步短连（连上即发即关）。

#include "./addr_b.h"
#include "../../tcp.h"

#include <condition_variable>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace xhqm
{
	namespace nebula
	{
		class xhqm_io addr_ipv6 : public addr_b
		{
		public:
			using conn_t = xhqm::network::tcp_conn;
			using hander_t = xhqm::network::hander;

			// 一条长连接：读写由本端点（或接入方）的 reactor 统一驱动；发队列有界
			struct conn : public std::enable_shared_from_this<conn>
			{
				std::shared_ptr<conn_t> sock;         // 已建立的帧级 TCP 连接（reactor 内设为非阻塞）
				nebula::uuid peer{};                  // 对端节点 uuid：拨号侧建连即知，接入侧由首帧 send_node 学得
				addr_ipv6* owner = nullptr;           // 建它的对象（拨号方或接入方）：收摊与 reactor 归属
				std::deque<nebula::json> outbox;      // 待发帧（有界，满丢最旧）
				std::string inbuf;                    // 入向字节缓冲（4 字节长度前缀 + 载荷；reactor 私有）
				std::string pending;                  // 正在写出、尚未落内核的字节（reactor 私有）
				std::mutex mutex;                     // 队列锁（连接自带，跨对象共用同一条连接）
				std::condition_variable cond;         // 队列 / 运行标志变化
				std::atomic<bool> runing{ false };    // 读写运行标志
				std::atomic<bool> good{ false };      // 连接可用（读写失败 / 空闲超时置假）
				std::atomic<bool> writing{ false };   // 写路径上还有未落内核的字节（pending 非空）
				std::atomic<xhqm::u64> last{ 0 };     // 最近一次成功收到帧的时刻（ns）
			};

			static constexpr xhqm::size s_outbox = 64;       // 每连接发队列上限
			static constexpr xhqm::size s_react_ms = 200;    // reactor poll 超时（无唤醒时的兜底粒度，对齐 addr_ipv4）
			static constexpr xhqm::size s_idle_ms = 30000;   // 空闲回收：这么久没收到任何帧即收连接
			static constexpr xhqm::size s_read_cap = 4 << 20;// 单连接入向缓冲上限（防对端灌水撑爆内存）
			static constexpr const char* s_group = "ff02::1";// 链路本地全节点组播（IPv6 没有广播的替代）
		protected:
			text m_host;                                   // 本端点主机（对端拨号目标）；绑定仍用 :: 任意（双栈）
			sint m_port = 0;                               // 本端点 TCP 端口（定向长连接）
			sint m_broad = 0;                              // 组播端口（同网共享）；0 = 与 m_port 同号
			xhqm::size m_s = 1000;                         // 建连 / 发送超时毫秒
			xhqm::size m_idle = s_idle_ms;                 // 空闲回收毫秒
			std::atomic<bool> m_runing{ false };
			std::atomic<bool> m_closed{ true };             // closed() 幂等（克隆句柄也要能收摊）
			std::mutex m_net_mutex;                        // 套接字句柄锁（组播状态与唤醒管）
			hander_t m_answer = xhqm::network::error_socket;// TCP 监听套接字（opened 惰性创建，reactor 独占 poll）
			hander_t m_group = xhqm::network::error_socket; // UDP 组播套接字（常驻，收发共用）
			hander_t m_wake_r = xhqm::network::error_socket;// 唤醒管接收端（回环 UDP）
			hander_t m_wake_w = xhqm::network::error_socket;// 唤醒管发送端
			unsigned short m_wake_port = 0;                 // 唤醒管接收端口
			std::atomic<bool> m_wake_pending{ false };      // 已投递唤醒（合并多次入队，避免每帧一次 sendto）
			std::vector<unsigned int> m_ifaces;             // 成功加入组播的接口索引（发送时逐个施加 MULTICAST_IF）
			std::thread m_frame_thread;                     // reactor：listen + 组播 + 唤醒管 + 全部长连
			mutable std::mutex m_conn_mutex;                // 名下连接表锁
			std::vector<std::shared_ptr<conn>> m_in;        // 我名下的长连（接入的 + 拨出的）
		protected:
			std::shared_ptr<conn> make_conn(const nebula::uuid& peer, const std::shared_ptr<conn_t>& sock);
			void drop_conn(const std::shared_ptr<conn>& c); // 摘表 + 关套接字（reactor 或 closed 调用）
			void learn_conn(conn* c, const nebula::json& frame);   // 首帧学对端 uuid → 进进程级连接表
			bool deliver(const nebula::uuid& peer, const nebula::json& value, sint timeout_ms);
			// 真正干活的一侧：用自己的连接表与 frame_signal，按 target 的 host/port 拨号
			bool send_remote(const nebula::uuid& peer, const text& host, const sint& port,
				const nebula::json& value, sint timeout_ms);
			bool send_oneshot(const text& host, const sint& port, const nebula::json& value); // 无宿主端点：同步短连
			bool group_open();                              // 建 UDP 组播套接字 + 加入 ff02::1
			bool wake_open();                               // 建唤醒管（回环 UDP 一对）
			void wake();                                    // 自唤醒：让 reactor 立刻处理新入队的帧
			void reactor_runing();                          // 单 reactor：accept + 组播收 + 长连读写 + 空闲回收
			bool conn_read(conn* c);                        // 非阻塞读 + 拆帧（false = 连接结束）
			bool conn_write(conn* c);                       // 非阻塞发队列（false = 连接结束）
			void conn_close(conn* c);                       // 只关套接字（不 join：无连接线程）
		public:
			explicit addr_ipv6(const text& host = "", const sint& port = 0, const sint& broad = 0);
			virtual ~addr_ipv6() override;

			virtual json make_json() const override;       // + host / port / broad
			virtual bool opened() override;                // 建组播 + 监听 TCP + 起 reactor + 进端点注册表
			virtual void closed() override;                // 退注册表 + 收长连 + join reactor + 关套接字
			virtual bool send_broad(const json&) override; // IPv6 组播（ff02::1）；无组播腿时退化为进程内广播
			virtual bool send_frame(const json&) override; // 定向：进程内直投，否则长连接入队
			virtual bool send_frame(const json&, sint) override; // 定向 + 等「已落内核」

			inline const text& host() const { return m_host; }
			inline sint port() const { return m_port; }
			inline sint broad() const { return m_broad; }
			inline void send_time(xhqm::size ms) { m_s = ms; }
			inline void idle_time(xhqm::size ms) { m_idle = ms; }
			size conn_count() const;                       // 本对象名下的长连数（自检 / 测试用）
			static size conn_total();                      // 进程级长连表大小（自检 / 测试用）
			static size conn_good();                       // 进程级长连表里仍可用的连接数（自检 / 测试用）
		};
	}
}

#endif
