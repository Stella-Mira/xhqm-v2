#ifndef XHQM_NEBULA_ADDR_SERIAL
#define XHQM_NEBULA_ADDR_SERIAL

// addr_serial：串口传输（点对点长链路版）。结构对齐 addr_ipv4，把「TCP 长连接」换成「串口字节流 + 帧化」：
//   * 一个 addr_serial = 一个串口端点：
//       host = 设备名（Windows "COM3" / POSIX "/dev/ttyUSB0"）；
//       port = **波特率**（默认 115200）。注意与 addr_ipv4 的 port（TCP 端口）语义不同：
//              本类的 port 就是波特率，make_json 里同时给出 "baud" 别名以免上层误读；
//   * 两张进程级静态表（与 addr_ipv4 的 ipv4_registry / ipv4_conns 同构）：
//       serial_registry —— 已 opened 的端点（uuid → 端点）：同 uuid 直投，进程内联调不绕串口；
//       serial_conns    —— 链路表（对端 uuid → 链路）：端点与它的克隆句柄**共用同一条串口链路**，
//                          同一对节点永远只有一条链路；
//   * 克隆句柄（按 addr_ipv4 的语义：addr_from 造、被 link 挂进邻居表的那种，自身不 opened）
//     只带对端的 uuid + host/baud，是发送侧路由句柄：优先复用「本进程里已持有这条串口缆的端点」的链路；
//   * 帧化：串口是无帧边界的字节流 → 复用 network/cs.h 的 frame_stream<serial_stream>
//           （4 字节小端长度前缀 + 负载，与 network.h 的 send_frame/recv_frame、tcp_conn 全局同协议）。
//           半包由「字节流 read 读满 n 字节」在字节流内部循环补齐；粘包由 frame_stream 按长度前缀切分；
//   * 可注入（可测试性）：字节流抽象成 serial_stream，因此「帧化 + 收发状态机」在没有物理串口时也能完整验证：
//       真串口   = serial_port_stream（Windows: CreateFile("\\\\.\\COM3") + SetCommState/SetCommTimeouts；
//                                      POSIX  : open("/dev/ttyUSB0") + termios 8N1）；
//       离线仿真 = serial_mem_stream（内存队列，可成对成缆、可按任意边界喂半包）；
//   * 并发模型：一条链路 = 读线程（frame_stream::recv → frame_signal）+ 写线程（有界发队列 → frame_stream::send，
//           兼空闲回收看门狗）；一条字节流只有一个写线程，帧头与负载之间不会被别的写者插入；
//   * 关停顺序（见 addr_serial.cpp）：closed() → 清运行标志 → 退注册表 → 取回本对象名下链路 → 停链路线程并 join
//           → 链路/字节流析构时才真正 CloseHandle / close(fd)。串口句柄**不在** closed() 里关，
//           避免「句柄先关、读线程还阻塞在 ReadFile」的竞态；
//   * send_broad 取舍（串口物理上没有广播）：**不退化为「写串口总线」**，只做进程内同 kind（serial）端点广播，
//           对齐 addr_ipv4「无 UDP 腿时退化为进程内广播」的既有纪律；串口缆上的求邻办法见 .cpp 注释；
//   * 邻居语义：点对点，对端 uuid 由帧里的 send_node 学得（同 addr_ipv4 的 learn_conn 思路），学不到保持未知。

#include "./addr_b.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace xhqm
{
	namespace nebula
	{
		// ------------------------------------------------ 可注入字节流 ----------------
		// 串口只保证「字节流」，本接口是帧化层（frame_stream）看到的传输抽象：
		//   read(p, n) —— 读满 n 字节才返回 true；半包在实现内部循环补齐；仅在链路关闭/出错时返回 false；
		//   write(p, n) —— 写满 n 字节才返回 true；
		//   open/close/good —— 生命周期与可用性。
		// 假流（serial_mem_stream）与真串口（serial_port_stream）都实现本接口，
		// 所以收发状态机（读线程/写线程/学习对端 uuid）可以脱离硬件完整验证。
		class serial_stream
		{
		public:
			virtual ~serial_stream() = default;
			virtual bool open() = 0;											// 打开（幂等）
			virtual void close() = 0;											// 关闭并中断阻塞中的 read/write（幂等）
			virtual bool good() const = 0;										// 可用
			virtual bool read(std::uint8_t* data, std::size_t n) = 0;			// 读满 n 字节
			virtual bool write(const std::uint8_t* data, std::size_t n) = 0;	// 写满 n 字节
		};

		// ------------------------------------------------ 真串口字节流 ----------------
		// Windows：CreateFile("\\\\.\\COM3") + SetCommState(波特率/8N1) + SetCommTimeouts(读 100ms 轮询)
		// POSIX  ：open("/dev/ttyUSB0", O_RDWR|O_NOCTTY) + termios(原始模式, VMIN=0/VTIME=1)
		// 句柄字段用 intptr_t 承载，平台头只出现在 .cpp（本头文件不引 <windows.h>/<termios.h>）。
		class serial_port_stream : public serial_stream
		{
			text m_host;						// 设备名（"COM3" / "/dev/ttyUSB0"）
			sint m_port = 115200;				// 波特率
			std::intptr_t m_handle = -1;		// Windows: HANDLE；POSIX: fd
			std::atomic<bool> m_alive{ false };	// 运行标志（close 后为假：读写循环退出）
			std::atomic<bool> m_open{ false };	// 是否已打开
		public:
			explicit serial_port_stream(const text& host, const sint& baud = 115200);
			virtual ~serial_port_stream() override;
			serial_port_stream(const serial_port_stream&) = delete;
			serial_port_stream& operator=(const serial_port_stream&) = delete;

			inline const text& host() const { return m_host; }
			inline sint port() const { return m_port; }
			inline std::intptr_t handle() const { return m_handle; }

			virtual bool open() override;
			virtual void close() override;
			virtual bool good() const override;
			virtual bool read(std::uint8_t* data, std::size_t n) override;
			virtual bool write(const std::uint8_t* data, std::size_t n) override;
		};

		// ------------------------------------------------ 内存字节流（离线仿真 / 测试） ----------------
		// 一条「缆」= 两条方向相反的队列：a 写 → a.m_tx == b.m_rx → b 读。
		//   pair(l, r)   造一对互连流（等于一根串口线）；成对后 a 写出的字节就是 b 读到的字节；
		//   单流使用      feed(bytes) 手动喂「远端发来的字节」（可按任意边界切碎模拟半包/粘包）；
		//                 take(out)  取走「本端写出的字节」（用于校验真发出去的帧）；
		//                 feed_eof() 模拟远端断线（此后 read 立即失败）。
		class serial_mem_stream : public serial_stream
		{
		public:
			struct channel
			{
				std::mutex mutex;
				std::condition_variable cond;
				std::deque<std::uint8_t> buf;
				std::size_t limit = 1u << 20;	// 每方向缓冲上限（1MB）：防假流无界增长
				std::atomic<bool> alive{ true };	// 缆是否还通（原子：good() 无锁读）
			};
		private:
			std::shared_ptr<channel> m_tx;		// 本端写出 → 对端读出
			std::shared_ptr<channel> m_rx;		// 对端写出 → 本端读出
			std::atomic<bool> m_open{ false };
		public:
			serial_mem_stream();
			virtual ~serial_mem_stream() override;
			serial_mem_stream(const serial_mem_stream&) = delete;
			serial_mem_stream& operator=(const serial_mem_stream&) = delete;

			// 造一对互连流（一根缆的两端）；两侧都必须是已构造的 shared_ptr
			static void pair(const std::shared_ptr<serial_mem_stream>& left, const std::shared_ptr<serial_mem_stream>& right);

			virtual bool open() override;
			virtual void close() override;
			virtual bool good() const override;
			virtual bool read(std::uint8_t* data, std::size_t n) override;
			virtual bool write(const std::uint8_t* data, std::size_t n) override;

			bool feed(const std::string& data);									// 喂「远端发来的字节」（可任意切碎）
			void feed_eof();													// 远端断线：本流后续 read 失败
			std::size_t take(std::string& out, std::size_t timeout_ms = 1000);	// 取走本端写出的字节
		};

		// ------------------------------------------------ 串口地址 ----------------
		class xhqm_io addr_serial : public addr_b
		{
		public:
			using stream_t = serial_stream;
			using factory_t = std::function<std::shared_ptr<stream_t>()>;

			// 一条点对点链路：读线程 + 写线程（兼空闲看门狗）+ 有界发队列（零号协议允许丢与乱序：队满丢最旧）
			struct conn
			{
				std::shared_ptr<stream_t> stream;	// 帧化前的字节流（真串口 / 假流）
				nebula::uuid peer{};				// 对端节点 uuid：由首帧 send_node 学得，学不到保持未知
				addr_serial* owner = nullptr;		// 建它的对象：收摊由 owner 负责
				std::deque<nebula::json> outbox;	// 待发帧
				std::mutex mutex;					// 队列锁（连接自带；链路可被端点与克隆句柄共用）
				std::condition_variable cond;		// 队列 / 运行标志变化
				std::atomic<bool> runing{ false };	// 读写线程运行标志
				std::atomic<bool> good{ false };	// 链路可用（写失败 / 空闲回收置假）
				std::atomic<xhqm::u64> last{ 0 };	// 最近一次收发成功的时刻（ns）
				std::thread reader;
				std::thread writer;

				// 兜底收摊：线程若还 joinable 就 join（正常路径已在 stop_conn 里 join 过，这里是双保险，
				// 防止「链路被丢弃时 std::thread 仍 joinable」触发 std::terminate）
				~conn();
			};

			static constexpr sint s_baud = 115200;			// 默认波特率（= 本类的 port 语义）
			static constexpr xhqm::size s_outbox = 64;		// 每链路发队列上限
			static constexpr std::uint32_t s_frame_max = 1u << 20;	// 单帧上限 1MB（串口速率下远够；超限判失步收缆）
			static constexpr xhqm::size s_poll_ms = 100;	// 写线程 / 看门狗轮询周期（也是关停唤醒粒度）
			static constexpr xhqm::size s_idle_ms = 30000;	// 空闲回收：这么久没有任何收发即收链路
		protected:
			text m_host;									// 本端点串口设备名（Windows "COM3" / POSIX "/dev/ttyUSB0"）
			sint m_port = s_baud;							// 本端点波特率（port 语义 = 波特率）
			xhqm::size m_s = 1000;							// 带 ACK 发送的等待毫秒
			std::atomic<xhqm::size> m_idle{ s_idle_ms };	// 空闲回收毫秒（0 = 关闭回收；写线程看门狗读，故用原子）
			std::atomic<bool> m_runing{ false };			// 本端点已 opened（reactor 可跑）
			std::atomic<bool> m_stop{ false };				// 已被显式 closed()：不再建链（opened() 会清）
			std::atomic<bool> m_closed{ true };				// 本对象「没有需要收摊的东西」（closed() 幂等用）
			std::shared_ptr<stream_t> m_inject;				// 注入的单条字节流（opened(stream) / 测试）
			factory_t m_factory;							// 注入的字节流工厂（每次建链取一条新流）
			mutable std::mutex m_conn_mutex;				// 本对象名下链路锁
			std::shared_ptr<conn> m_conn;					// 本对象名下的那条点对点链路（0 或 1 条）
		protected:
			std::shared_ptr<stream_t> make_stream(const text& host, const sint& baud);
			std::shared_ptr<conn> link();					// 本对象名下可用链路（无 / 已坏则为空）
			std::shared_ptr<conn> open_link(const nebula::uuid& peer, const text& host, const sint& baud,
				const std::shared_ptr<stream_t>& stream = nullptr);
			static void stop_conn(const std::shared_ptr<conn>& c);	// 停线程并 join（静态：可从进程级表回调里调用）
			void start_conn(const std::shared_ptr<conn>& c);
			void learn_conn(conn* c, const nebula::json& frame);	// 首帧学对端 uuid → 进进程级链路表
			bool deliver(const nebula::uuid& peer, const nebula::json& value, sint timeout_ms);
			bool send_remote(const nebula::uuid& peer, const text& host, const sint& baud,
				const nebula::json& value, sint timeout_ms);
			void reader_runing(conn* c);					// 每链路：frame_stream::recv → frame_signal
			void writer_runing(conn* c);					// 每链路：发队列 → frame_stream::send（兼空闲看门狗）
		public:
			explicit addr_serial(const text& host = "", const sint& port = s_baud);
			virtual ~addr_serial() override;

			virtual json make_json() const override;				// + host / port(波特率) / baud
			virtual bool opened() override;							// 打开串口 + 起读/写线程 + 进端点注册表（幂等）
			bool opened(const std::shared_ptr<stream_t>& stream);	// 同上，但用注入的字节流（无硬件 / 虚拟串口对）
			void inject(const std::shared_ptr<stream_t>& stream);	// 只登记注入流（不启动；供 opened() 用）
			void stream_factory(const factory_t& factory);			// 只登记流工厂（每次建链一条新流）
			virtual void closed() override;							// 退注册表 + 停本对象名下链路（读/写 join）
			virtual bool send_broad(const json&) override;			// 进程内同 kind 端点广播（串口无物理广播，见 .cpp）
			virtual bool send_frame(const json&) override;			// 定向：进程内直投，否则走串口链路（无 ACK）
			virtual bool send_frame(const json&, sint) override;	// 定向 + 等「写出队列排空」（带 ACK 语义）

			inline const text& host() const { return m_host; }
			inline sint port() const { return m_port; }				// 波特率
			inline sint baud() const { return m_port; }				// 同上（语义别名）
			inline void send_time(xhqm::size ms) { m_s = ms; }
			inline void idle_time(xhqm::size ms) { m_idle = ms; }
			size conn_count() const;								// 本对象名下的链路数（自检 / 测试用）
			static size conn_total();								// 进程级链路表大小（自检 / 测试用）
			static bool conn_learned(const nebula::uuid& peer);		// 该对端 uuid 是否已学到链路（自检 / 测试用）
		};
	}
}

#endif
