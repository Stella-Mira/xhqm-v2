#include "addr_serial.h"

// 传输层：cs.h → network.h（内部先引 WinSock2.h）→ 因此平台头 <windows.h> 必须在其后包含
#include "../../cs.h"

#ifdef XHQM_WINDOWS
#include <windows.h>
#else
#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#endif

#include <algorithm>
#include <chrono>
#include <set>
#include <vector>

namespace xhqm
{
	namespace nebula
	{
		// ================================================ 平台常量 ================

		static constexpr std::size_t s_chunk = 1u << 16;	// 单次 ReadFile/read 的最大字节数
		static constexpr std::size_t s_read_ms = 100;		// 单次读超时（毫秒）：关停时读线程最多这么久退出

#ifndef XHQM_WINDOWS
		// POSIX：波特率数值 → termios 速度常量（缺哪个用 #ifdef 兜住）
		static speed_t serial_speed(const sint& baud)
		{
			switch (baud)
			{
#ifdef B110
			case 110: return B110;
#endif
#ifdef B300
			case 300: return B300;
#endif
#ifdef B600
			case 600: return B600;
#endif
#ifdef B1200
			case 1200: return B1200;
#endif
#ifdef B2400
			case 2400: return B2400;
#endif
#ifdef B4800
			case 4800: return B4800;
#endif
#ifdef B9600
			case 9600: return B9600;
#endif
#ifdef B19200
			case 19200: return B19200;
#endif
#ifdef B38400
			case 38400: return B38400;
#endif
#ifdef B57600
			case 57600: return B57600;
#endif
#ifdef B115200
			case 115200: return B115200;
#endif
#ifdef B230400
			case 230400: return B230400;
#endif
#ifdef B460800
			case 460800: return B460800;
#endif
#ifdef B921600
			case 921600: return B921600;
#endif
#ifdef B1500000
			case 1500000: return B1500000;
#endif
#ifdef B3000000
			case 3000000: return B3000000;
#endif
			default: return B115200;
			}
		}
#endif

		// ================================================ 真串口字节流 ================

		serial_port_stream::serial_port_stream(const text& host, const sint& baud)
			: m_host{ host }, m_port{ baud > 0 ? baud : 115200 }
		{

		}

		serial_port_stream::~serial_port_stream()
		{
			// 关停顺序收尾：close() 只置标志 + 中断挂起 I/O，**句柄在这里才真正关闭**——
			// 由 addr_serial::closed() 保证「先 join 读写线程，再析构字节流」，所以此处不可能还有人阻塞在句柄上。
			this->close();
#ifdef XHQM_WINDOWS
			if (m_handle != -1)
			{
				::CloseHandle(reinterpret_cast<HANDLE>(m_handle));
				m_handle = -1;
			}
#else
			if (m_handle >= 0)
			{
				::close(static_cast<int>(m_handle));
				m_handle = -1;
			}
#endif
		}

		bool serial_port_stream::open()
		{
			if (m_open) return true;			// 幂等
			if (m_host.empty()) return false;
#ifdef XHQM_WINDOWS
			// COM 口要走 \\.\ 设备命名空间：COM10 及以上不加前缀打不开，COM1-9 也接受该前缀（统一加上更省事）
			text path = m_host;
			if (path.size() >= 3 && (path.compare(0, 3, "COM") == 0 || path.compare(0, 3, "com") == 0))
			{
				path = "\\\\.\\" + path;
			}
			HANDLE h = ::CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
				OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (h == INVALID_HANDLE_VALUE) return false;			// 端口不存在 / 被占用 / 无权限

			DCB dcb{};
			dcb.DCBlength = sizeof(DCB);
			if (!::GetCommState(h, &dcb)) { ::CloseHandle(h); return false; }
			dcb.BaudRate = static_cast<DWORD>(m_port);				// 波特率 = 本类的 port
			dcb.ByteSize = 8;
			dcb.Parity = NOPARITY;
			dcb.StopBits = ONESTOPBIT;								// 8N1
			dcb.fBinary = TRUE;
			dcb.fParity = FALSE;
			dcb.fOutxCtsFlow = FALSE;
			dcb.fOutxDsrFlow = FALSE;
			dcb.fDsrSensitivity = FALSE;
			dcb.fOutX = FALSE;
			dcb.fInX = FALSE;
			dcb.fErrorChar = FALSE;
			dcb.fNull = FALSE;
			dcb.fAbortOnError = FALSE;
			dcb.fDtrControl = DTR_CONTROL_ENABLE;
			dcb.fRtsControl = RTS_CONTROL_ENABLE;
			if (!::SetCommState(h, &dcb)) { ::CloseHandle(h); return false; }
			::SetupComm(h, 4096, 4096);

			// 读：字节间隔 20ms 或累计 100ms 即返回已收字节 → read() 循环补齐半包，同时保证关停时最多 100ms 一轮
			COMMTIMEOUTS to{};
			to.ReadIntervalTimeout = 20;
			to.ReadTotalTimeoutMultiplier = 0;
			to.ReadTotalTimeoutConstant = static_cast<DWORD>(s_read_ms);
			to.WriteTotalTimeoutMultiplier = 0;
			to.WriteTotalTimeoutConstant = 500;
			if (!::SetCommTimeouts(h, &to)) { ::CloseHandle(h); return false; }
			::PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);

			m_handle = reinterpret_cast<std::intptr_t>(h);
			m_alive = true;
			m_open = true;
			return true;
#else
			int fd = ::open(m_host.c_str(), O_RDWR | O_NOCTTY);		// 阻塞 + 不做控制终端
			if (fd < 0) return false;
			termios tty{};
			if (::tcgetattr(fd, &tty) != 0) { ::close(fd); return false; }
			// 原始模式：不做行规/回显/信号/字符翻译（帧边界由 frame_stream 负责）
			tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON | IXOFF | IXANY);
			tty.c_oflag &= ~OPOST;
			tty.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
			tty.c_cflag |= (CLOCAL | CREAD);
			tty.c_cflag &= ~CSIZE;
			tty.c_cflag |= CS8;
			tty.c_cflag &= ~PARENB;
			tty.c_cflag &= ~CSTOPB;
#ifdef CRTSCTS
			tty.c_cflag &= ~CRTSCTS;								// 关硬件流控（除非上层另行要求）
#endif
			::cfsetispeed(&tty, serial_speed(m_port));
			::cfsetospeed(&tty, serial_speed(m_port));
			tty.c_cc[VMIN] = 0;										// 不等够字节
			tty.c_cc[VTIME] = 1;									// 100ms 轮询：无字节也返回，便于关停轮询标志
			if (::tcsetattr(fd, TCSANOW, &tty) != 0) { ::close(fd); return false; }
			::tcflush(fd, TCIOFLUSH);

			m_handle = static_cast<std::intptr_t>(fd);
			m_alive = true;
			m_open = true;
			return true;
#endif
		}

		void serial_port_stream::close()
		{
			m_alive = false;
			m_open = false;
#ifdef XHQM_WINDOWS
			if (m_handle != -1)
			{
				// 只中断挂起 I/O；CloseHandle 留给析构（那时读写线程已 join）
				::PurgeComm(reinterpret_cast<HANDLE>(m_handle),
					PURGE_RXABORT | PURGE_RXCLEAR | PURGE_TXABORT | PURGE_TXCLEAR);
			}
#else
			if (m_handle >= 0) { ::tcflush(static_cast<int>(m_handle), TCIOFLUSH); }
#endif
		}

		bool serial_port_stream::good() const
		{
			return (m_open && m_alive && m_handle != -1);
		}

		// 读满 n 字节：串口是字节流，半包在**这里**循环补齐（帧层只看「一帧的字节齐了没有」）
		bool serial_port_stream::read(std::uint8_t* data, std::size_t n)
		{
			if (!data || n == 0) return true;
			if (!m_open || m_handle == -1) return false;
#ifdef XHQM_WINDOWS
			HANDLE h = reinterpret_cast<HANDLE>(m_handle);
			std::size_t off = 0;
			while (off < n)
			{
				if (!m_alive || !m_open) return false;
				const DWORD want = static_cast<DWORD>(std::min<std::size_t>(n - off, s_chunk));
				DWORD got = 0;
				if (::ReadFile(h, data + off, want, &got, nullptr))
				{
					off += static_cast<std::size_t>(got);			// got == 0 = 读超时：接着等剩余字节
					continue;
				}
				const DWORD err = ::GetLastError();
				if (err == ERROR_OPERATION_ABORTED) return false;	// close() 中断了挂起读
				if (err == ERROR_TIMEOUT || err == ERROR_SEM_TIMEOUT) continue;
				return false;
			}
			return true;
#else
			const int fd = static_cast<int>(m_handle);
			std::size_t off = 0;
			while (off < n)
			{
				if (!m_alive || !m_open) return false;
				const ssize_t r = ::read(fd, data + off, std::min<std::size_t>(n - off, s_chunk));
				if (r > 0) { off += static_cast<std::size_t>(r); continue; }
				if (r == 0) continue;								// VTIME 超时：无字节，接着等
				if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
				return false;
			}
			return true;
#endif
		}

		bool serial_port_stream::write(const std::uint8_t* data, std::size_t n)
		{
			if (n == 0) return true;
			if (!data || !m_open || m_handle == -1) return false;
#ifdef XHQM_WINDOWS
			HANDLE h = reinterpret_cast<HANDLE>(m_handle);
			std::size_t off = 0;
			while (off < n)
			{
				if (!m_alive || !m_open) return false;
				const DWORD want = static_cast<DWORD>(std::min<std::size_t>(n - off, s_chunk));
				DWORD put = 0;
				if (!::WriteFile(h, data + off, want, &put, nullptr))
				{
					const DWORD err = ::GetLastError();
					if (err == ERROR_OPERATION_ABORTED) return false;
					if (err == ERROR_TIMEOUT || err == ERROR_SEM_TIMEOUT) continue;
					return false;
				}
				off += static_cast<std::size_t>(put);
			}
			return true;
#else
			const int fd = static_cast<int>(m_handle);
			std::size_t off = 0;
			while (off < n)
			{
				if (!m_alive || !m_open) return false;
				const ssize_t r = ::write(fd, data + off, std::min<std::size_t>(n - off, s_chunk));
				if (r > 0) { off += static_cast<std::size_t>(r); continue; }
				if (r < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
				return false;
			}
			return true;
#endif
		}

		// ================================================ 内存字节流（离线仿真） ================

		serial_mem_stream::serial_mem_stream()
			: m_tx{ std::make_shared<channel>() }, m_rx{ std::make_shared<channel>() }
		{

		}

		serial_mem_stream::~serial_mem_stream()
		{
			this->close();
		}

		void serial_mem_stream::pair(const std::shared_ptr<serial_mem_stream>& left, const std::shared_ptr<serial_mem_stream>& right)
		{
			if (!left || !right || left == right) return;
			std::shared_ptr<channel> c1 = std::make_shared<channel>();	// left 写 → right 读
			std::shared_ptr<channel> c2 = std::make_shared<channel>();	// right 写 → left 读
			left->m_tx = c1; left->m_rx = c2;
			right->m_tx = c2; right->m_rx = c1;
		}

		bool serial_mem_stream::open()
		{
			m_open = true;					// 幂等
			return true;
		}

		void serial_mem_stream::close()
		{
			m_open = false;
			const std::shared_ptr<channel> list[2] = { m_tx, m_rx };
			for (const auto& c : list)
			{
				if (!c) continue;
				std::lock_guard<std::mutex> lk(c->mutex);
				c->alive = false;			// 缆断：本端 read 失败，对端 read 也失败（等价拔线）
				c->cond.notify_all();
			}
		}

		bool serial_mem_stream::good() const
		{
			return (m_open && m_tx && m_rx && m_tx->alive && m_rx->alive);
		}

		bool serial_mem_stream::read(std::uint8_t* data, std::size_t n)
		{
			if (n == 0) return true;
			if (!data) return false;
			const std::shared_ptr<channel> rx = m_rx;
			if (!rx) return false;
			std::unique_lock<std::mutex> lk(rx->mutex);
			std::size_t off = 0;
			while (off < n)
			{
				if (!rx->buf.empty())
				{
					const std::size_t got = std::min(n - off, rx->buf.size());
					for (std::size_t i = 0; i < got; ++i) { data[off + i] = rx->buf.front(); rx->buf.pop_front(); }
					off += got;
					continue;
				}
				if (!rx->alive || !m_open) return false;			// 缆断 / 已关：半包就此作废（关停路径）
				rx->cond.wait_for(lk, std::chrono::milliseconds(50),
					[&rx] { return !rx->buf.empty() || !rx->alive; });
			}
			return true;
		}

		bool serial_mem_stream::write(const std::uint8_t* data, std::size_t n)
		{
			if (n == 0) return true;
			if (!data) return false;
			const std::shared_ptr<channel> tx = m_tx;
			if (!tx) return false;
			std::unique_lock<std::mutex> lk(tx->mutex);
			std::size_t off = 0;
			while (off < n)
			{
				if (!tx->alive || !m_open) return false;
				if (tx->buf.size() < tx->limit)
				{
					const std::size_t put = std::min(tx->limit - tx->buf.size(), n - off);
					for (std::size_t i = 0; i < put; ++i) { tx->buf.push_back(data[off + i]); }
					off += put;
					tx->cond.notify_all();
					continue;
				}
				// 缓冲满：等对端读走（背压）；关停时 50ms 一轮检查标志
				tx->cond.wait_for(lk, std::chrono::milliseconds(50),
					[&tx] { return tx->buf.size() < tx->limit || !tx->alive; });
			}
			return true;
		}

		bool serial_mem_stream::feed(const std::string& data)
		{
			const std::shared_ptr<channel> rx = m_rx;
			if (!rx) return false;
			std::lock_guard<std::mutex> lk(rx->mutex);
			if (!rx->alive) return false;
			if (rx->buf.size() + data.size() > rx->limit) return false;
			for (unsigned char ch : data) { rx->buf.push_back(ch); }
			rx->cond.notify_all();
			return true;
		}

		void serial_mem_stream::feed_eof()
		{
			const std::shared_ptr<channel> rx = m_rx;
			if (!rx) return;
			std::lock_guard<std::mutex> lk(rx->mutex);
			rx->alive = false;
			rx->cond.notify_all();
		}

		std::size_t serial_mem_stream::take(std::string& out, std::size_t timeout_ms)
		{
			const std::shared_ptr<channel> tx = m_tx;
			if (!tx) return 0;
			std::unique_lock<std::mutex> lk(tx->mutex);
			const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
			while (tx->buf.empty() && tx->alive && std::chrono::steady_clock::now() < end)
			{
				tx->cond.wait_for(lk, std::chrono::milliseconds(20));
			}
			std::size_t got = 0;
			while (!tx->buf.empty())
			{
				out.push_back(static_cast<char>(tx->buf.front()));
				tx->buf.pop_front();
				++got;
			}
			return got;
		}

		// ================================================ 进程级静态表 ================
		// 与 addr_ipv4 的 ipv4_registry / ipv4_conns 同构：
		//   serial_registry —— 已 opened 的端点（uuid → 端点）：同 uuid 直投，进程内联调不绕串口；
		//   serial_conns    —— 链路表（对端 uuid → 链路）：端点与克隆句柄共用同一条串口链路。
		static std::mutex serial_mutex;
		static std::map<nebula::uuid, addr_serial*, uuid_less> serial_registry;

		static std::mutex serial_conn_mutex;
		static std::map<nebula::uuid, std::shared_ptr<addr_serial::conn>, uuid_less> serial_conns;

		static addr_serial* serial_local(const nebula::uuid& id)
		{
			std::lock_guard<std::mutex> lk(serial_mutex);
			auto it = serial_registry.find(id);
			return (it != serial_registry.end()) ? it->second : nullptr;
		}

		// 克隆句柄（自身未 opened）找「把我挂在邻居表里的那个本地端点」：
		// 串口缆是独占设备，只能由已经持有它的端点来发；由它发，回程帧才会走它自己的 frame_signal（link 连的是它）。
		static addr_serial* serial_host_of(const nebula::uuid& peer, const addr_serial* self)
		{
			std::lock_guard<std::mutex> lk(serial_mutex);
			for (auto& kv : serial_registry)
			{
				addr_serial* one = kv.second;
				if (!one || one == self) continue;
				if (one->has_neighbor(peer)) { return one; }		// has_neighbor 走各自的表锁，无逆序
			}
			return nullptr;
		}

		static std::shared_ptr<addr_serial::conn> serial_conn_find(const nebula::uuid& peer)
		{
			if (!peer) return nullptr;
			std::lock_guard<std::mutex> lk(serial_conn_mutex);
			auto it = serial_conns.find(peer);
			if (it != serial_conns.end() && it->second && it->second->good) { return it->second; }
			return nullptr;
		}

		// 入表：已有一条**可用**链路就不动；若旧条目已坏则用新链路顶替（返回被顶替的旧链路，由调用方负责 join）
		static std::shared_ptr<addr_serial::conn> serial_conn_put(const std::shared_ptr<addr_serial::conn>& c)
		{
			if (!c || !c->peer) return nullptr;
			std::lock_guard<std::mutex> lk(serial_conn_mutex);
			auto it = serial_conns.find(c->peer);
			if (it == serial_conns.end()) { serial_conns[c->peer] = c; return nullptr; }
			if (!it->second) { it->second = c; return nullptr; }
			if (!it->second->good)
			{
				std::shared_ptr<addr_serial::conn> old = it->second;
				it->second = c;
				return old;
			}
			return nullptr;
		}

		static void serial_conn_erase(const std::shared_ptr<addr_serial::conn>& c)
		{
			if (!c || !c->peer) return;
			std::lock_guard<std::mutex> lk(serial_conn_mutex);
			auto it = serial_conns.find(c->peer);
			if (it != serial_conns.end() && it->second == c) { serial_conns.erase(it); }
		}

		// ================================================ 链路对象 ================

		addr_serial::conn::~conn()
		{
			// 双保险：正常路径已由 stop_conn 收摊；若还有线程 joinable（被意外丢弃），
			// std::thread 析构会 terminate，所以这里先关流唤醒、再 join。
			runing = false;
			good = false;
			cond.notify_all();
			if (stream) stream->close();
			if (reader.joinable()) reader.join();
			if (writer.joinable()) writer.join();
		}

		// ================================================ 构造 / 序列化 ================

		addr_serial::addr_serial(const text& host, const sint& port)
			: addr_b(xhqm::nebula::kind::serial), m_host{ host }, m_port{ port > 0 ? port : s_baud }
		{

		}

		addr_serial::~addr_serial()
		{
			this->closed();		// 克隆句柄也可能名下有链路（自己开过缆），不能只在 opened 过时才收摊
		}

		nebula::json addr_serial::make_json() const
		{
			nebula::json j = addr_b::make_json();	// 已含 kind = "serial"（kind_text/text_kind 早已支持 serial）
			j["host"] = m_host;						// 设备名："COM3" / "/dev/ttyUSB0"
			j["port"] = m_port;						// 注意：本类的 port = 波特率（不是 TCP 端口）
			j["baud"] = m_port;						// 别名：与 ipv4 的 port 区分，上层读哪个都行
			return j;
		}

		size addr_serial::conn_count() const
		{
			std::set<conn*> seen;
			{
				std::lock_guard<std::mutex> lk(m_conn_mutex);
				if (m_conn) seen.insert(m_conn.get());
			}
			{
				std::lock_guard<std::mutex> lk(serial_conn_mutex);
				for (const auto& kv : serial_conns)
				{
					if (kv.second && kv.second->owner == this) seen.insert(kv.second.get());
				}
			}
			return seen.size();
		}

		size addr_serial::conn_total()
		{
			std::lock_guard<std::mutex> lk(serial_conn_mutex);
			return serial_conns.size();
		}

		bool addr_serial::conn_learned(const nebula::uuid& peer)
		{
			return (serial_conn_find(peer) != nullptr);
		}

		// ================================================ 生命周期 ================

		void addr_serial::inject(const std::shared_ptr<stream_t>& stream)
		{
			if (m_runing) return;			// 已 opened：不动正在用的传输，下次 opened() 生效
			m_inject = stream;
		}

		void addr_serial::stream_factory(const factory_t& factory)
		{
			if (m_runing) return;
			m_factory = factory;
		}

		bool addr_serial::opened(const std::shared_ptr<stream_t>& stream)
		{
			this->inject(stream);
			return this->opened();
		}

		bool addr_serial::opened()
		{
			if (m_runing) return true;		// 幂等
			m_stop = false;					// 允许建链（closed() 之后仍可重开）
			m_closed = false;				// 本对象从此「有需要收摊的东西」：closed() 必须真的走完
			std::shared_ptr<stream_t> stream = this->make_stream(m_host, m_port);
			if (!stream || !stream->open() || !stream->good())
			{
				m_closed = true;			// 打不开（端口不存在 / 被占用 / 无权限）：保持未运行
				return false;				// 诚实返回：addr_b::is_open() 仍为假，link::life() 也不会以为它起来了
			}
			m_runing = true;
			{
				std::lock_guard<std::mutex> lk(serial_mutex);
				serial_registry[uuid()] = this;
			}
			std::shared_ptr<conn> c = this->open_link(nebula::uuid{}, m_host, m_port, stream);
			if (!c)
			{
				this->closed();				// 回滚：退注册表、清标志
				return false;
			}
			m_opened = true;				// addr_b::is_open() 的运行标志（三种传输现已一致维护）
			return true;
		}

		void addr_serial::closed()
		{
			if (m_closed.exchange(true)) return;	// 幂等（纯克隆句柄从没建过链：这里直接返回）
			m_stop = true;							// 显式关闭：之后不再自行建链（opened() 会清）
			m_runing = false;
			m_opened = false;
			{
				std::lock_guard<std::mutex> lk(serial_mutex);
				auto it = serial_registry.find(uuid());
				if (it != serial_registry.end() && it->second == this) { serial_registry.erase(it); }
			}
			// 取回本对象名下的链路：自己的那条 + 进程级表里 owner == this 的
			std::vector<std::shared_ptr<conn>> mine;
			{
				std::lock_guard<std::mutex> lk(m_conn_mutex);
				if (m_conn) { mine.push_back(m_conn); m_conn.reset(); }
			}
			{
				std::lock_guard<std::mutex> lk(serial_conn_mutex);
				for (auto it = serial_conns.begin(); it != serial_conns.end(); )
				{
					if (it->second && it->second->owner == this) { mine.push_back(it->second); it = serial_conns.erase(it); }
					else { ++it; }
				}
			}
			// 锁外停链路：关流唤醒阻塞读 → join 读线程 → join 写线程
			for (auto& c : mine) { stop_conn(c); }
			mine.clear();		// 线程已 join；字节流在此析构（真串口此刻才 CloseHandle）
		}

		// ================================================ 链路 ================

		std::shared_ptr<addr_serial::stream_t> addr_serial::make_stream(const text& host, const sint& baud)
		{
			// 注入优先：工厂（每次建链取一条新流）→ 单条注入流（幂等 open；已断的缆由调用方的 good() 拦下）→ 真串口
			if (m_factory) { return m_factory(); }
			if (m_inject) { m_inject->open(); return m_inject; }
			if (host.empty()) return nullptr;
			return std::make_shared<serial_port_stream>(host, baud);
		}

		std::shared_ptr<addr_serial::conn> addr_serial::link()
		{
			std::lock_guard<std::mutex> lk(m_conn_mutex);
			if (m_conn && m_conn->good && m_conn->runing) { return m_conn; }
			return nullptr;
		}

		std::shared_ptr<addr_serial::conn> addr_serial::open_link(const nebula::uuid& peer, const text& host, const sint& baud,
			const std::shared_ptr<stream_t>& stream)
		{
			{
				std::lock_guard<std::mutex> lk(m_conn_mutex);
				if (m_stop) return nullptr;										// 已显式关闭：不再建链
				if (m_conn && m_conn->good && m_conn->runing) { return m_conn; }	// 串口独占：已有可用链路就复用
			}
			// 造流（可能慢 / 可能阻塞在注入工厂里）：不持任何锁
			std::shared_ptr<stream_t> s = stream ? stream : this->make_stream(host, baud);
			if (!s) return nullptr;
			if (!stream && (!s->open() || !s->good())) { return nullptr; }
			auto c = std::make_shared<conn>();
			c->stream = s;
			c->peer = peer;						// 拨号侧建链即知；点对点侧留空，等首帧 send_node 学
			c->owner = this;
			c->last = xhqm::timesys::ntime();
			c->runing = true;
			c->good = true;
			std::shared_ptr<conn> old;
			bool install = false;
			{
				std::lock_guard<std::mutex> lk(m_conn_mutex);
				if (!m_stop && (!m_conn || !m_conn->good || !m_conn->runing))
				{
					old = m_conn;				// 旧链路（多半已被空闲回收/写坏）：交由下方锁外 join
					m_conn = c;
					m_closed = false;			// 有链路要收：closed() 必须真的走完
					install = true;
				}
			}
			if (!install)
			{
				stop_conn(c);					// 别人抢先建好 / 期间被 closed()：收掉刚建的这条（线程未起，只关流）
				return this->link();
			}
			if (old) { stop_conn(old); serial_conn_erase(old); }
			this->start_conn(c);				// m_conn 已挂好，读线程 learn_conn 才找得到自己
			if (std::shared_ptr<conn> replaced = serial_conn_put(c)) { stop_conn(replaced); }
			return c;
		}

		void addr_serial::start_conn(const std::shared_ptr<conn>& c)
		{
			if (!c) return;
			// 线程捕获裸指针（不捕获 shared_ptr）：conn 里存着 std::thread，捕获 shared_ptr 会与线程对象构成引用环
			conn* raw = c.get();
			c->reader = std::thread([this, raw] { this->reader_runing(raw); });
			c->writer = std::thread([this, raw] { this->writer_runing(raw); });
		}

		void addr_serial::stop_conn(const std::shared_ptr<conn>& c)
		{
			if (!c) return;
			c->runing = false;
			c->good = false;
			{
				std::lock_guard<std::mutex> lk(c->mutex);
				c->cond.notify_all();
			}
			if (c->stream) { c->stream->close(); }	// 唤醒阻塞在字节流 read 的读线程（真串口读时间片 100ms）
			if (c->reader.joinable()) { c->reader.join(); }
			if (c->writer.joinable()) { c->writer.join(); }
		}

		void addr_serial::learn_conn(conn* c, const nebula::json& frame)
		{
			if (!c || c->peer) return;
			nebula::uuid peer = nebula::uuid::to_uuid(frame.value("send_node", ""));
			if (!peer) return;					// 帧里没有 send_node：保持未知，等后续帧
			c->peer = peer;
			// 用 owner 名下那条链路的 shared_ptr 入表（不用 shared_from_this：避免读线程里持有临时引用）
			std::shared_ptr<conn> self;
			{
				std::lock_guard<std::mutex> lk(m_conn_mutex);
				if (m_conn.get() == c) { self = m_conn; }
			}
			if (!self) return;
			if (std::shared_ptr<conn> replaced = serial_conn_put(self)) { stop_conn(replaced); }
		}

		// ================================================ 收发线程 ================

		void addr_serial::reader_runing(conn* c)
		{
			if (!c || !c->stream) return;
			// 帧化：4 字节小端长度前缀 + 负载（与 network.h 的 send_frame/recv_frame、tcp_conn 同协议）。
			// 半包：长度前缀/负载的「读满」由字节流内部循环保证；粘包：多帧在一段缓冲里由本层按长度切分。
			// 帧长上限按传输收紧到 s_frame_max（串口速率下 1MB 远够）：超过即判失步，不再尝试巨量 resize。
			xhqm::network::frame_stream<stream_t, s_frame_max> frame{ *c->stream };
			while (c->runing)
			{
				text data;
				if (!frame.recv(data))
				{
					// recv 失败 = 缆断/出错（串口读超时在字节流内部轮询补齐，不会走到这里）
					const xhqm::u64 now = xhqm::timesys::ntime();
					const xhqm::u64 idle = xhqm::u64(m_idle) * 1000000ull;	// 毫秒 → 纳秒
					if (!c->stream || !c->stream->good() || (now - c->last.load()) > idle)
					{
						c->good = false;
						c->runing = false;
						break;
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(20));	// 不空转烧 CPU
					continue;
				}
				c->last = xhqm::timesys::ntime();
				// 远端字节不可信：畸形帧 parse 抛异常会顺本线程冒出 → std::terminate
				try
				{
					json f = json::parse(data);
					if (!f.is_object() || !f.contains("type")) continue;
					this->learn_conn(c, f);				// 首帧 send_node → 记住对端 uuid 并登记链路
					this->frame_signal.emit(f);
				}
				catch (...) {}
			}
			c->good = false;
			c->cond.notify_all();
		}

		void addr_serial::writer_runing(conn* c)
		{
			if (!c || !c->stream) return;
			// 同一个字节流只有这一个写线程 → 帧头与负载之间不会被别的写者插入
			xhqm::network::frame_stream<stream_t, s_frame_max> frame{ *c->stream };
			std::unique_lock<std::mutex> lk(c->mutex);
			while (c->runing)
			{
				c->cond.wait_for(lk, std::chrono::milliseconds(s_poll_ms),
					[&c] { return !c->outbox.empty() || !c->runing; });
				// 空闲回收看门狗：串口是阻塞读，读线程自己没法定期查空闲 → 由本线程每 s_poll_ms 查一次。
				// 注意（与 addr_ipv4 的差别）：串口的「活跃」按**收发双向**算（写成功也刷新 last），
				// 否则只发不收的心跳方向会被误判空闲。
				if (c->runing && m_idle > 0 &&
					(xhqm::timesys::ntime() - c->last.load()) > xhqm::u64(m_idle) * 1000000ull)
				{
					c->good = false;
					c->runing = false;
					if (c->stream) { c->stream->close(); }	// 唤醒阻塞在 read 的读线程
					break;
				}
				while (c->runing && !c->outbox.empty())
				{
					json value = std::move(c->outbox.front());
					c->outbox.pop_front();
					lk.unlock();							// 真正发帧不持队列锁（串口写可能慢）
					const bool ok = (c->stream && frame.send(value.dump()));
					lk.lock();
					if (!ok)
					{
						c->good = false;
						c->runing = false;
						break;
					}
					c->last = xhqm::timesys::ntime();
				}
				c->cond.notify_all();						// 通知等待版 send_frame：队列已排空
			}
			c->good = false;
			c->cond.notify_all();
		}

		// ================================================ 发送 ================

		bool addr_serial::deliver(const nebula::uuid& peer, const nebula::json& value, sint timeout_ms)
		{
			if (!peer) return false;
			// 1) 本进程内已有该 uuid 的已打开端点（含自身）→ 直投，等价 addr_uuid 的进程内路由
			if (addr_serial* local = serial_local(peer))
			{
				local->frame_signal.emit(value);
				return true;
			}
			// 2) 谁来做这件事：已打开的端点自己干；克隆句柄交给「把它挂进邻居表的本地端点」——
			//    串口缆独占，谁持有缆谁发，回程帧才会走它的 frame_signal（link 连的是它）。
			addr_serial* sender = m_runing ? this : serial_host_of(peer, this);
			if (!sender) sender = this;		// 还没被任何端点收作邻居：退化为自己（可能自己拨缆）
			return sender->send_remote(peer, m_host, m_port, value, timeout_ms);
		}

		bool addr_serial::send_remote(const nebula::uuid& peer, const text& host, const sint& baud,
			const nebula::json& value, sint timeout_ms)
		{
			// ① 进程级链路表：该对端已学得链路（learn_conn 登记）→ 直接复用
			std::shared_ptr<conn> c = serial_conn_find(peer);
			// ② 本对象自己名下那条点对点链路（opened() 开的缆 / 之前拨过的缆）
			if (!c) { c = this->link(); }
			// ③ 退化拨号：克隆句柄自带端口名时自己开缆。**只有显式注入了传输才允许**：
			//    邻居句柄里的 host 来自对端广播（是对端机器的串口名），在本机按它 CreateFile 会打开错误设备；
			//    需要「句柄自带端口名直连」的场合（测试假流 / 虚拟串口对）请显式 inject() 或 stream_factory()。
			if (!c && !m_stop && !host.empty() && (m_factory || m_inject))
			{
				c = this->open_link(peer, host, baud);
			}
			if (!c) return false;

			// 入队（有界，满丢最旧）；timeout > 0 时等队列排空——作为「已交给内核/设备」的活性信号
			std::unique_lock<std::mutex> lk(c->mutex);
			if (c->outbox.size() >= s_outbox) { c->outbox.pop_front(); }
			c->outbox.push_back(value);
			c->cond.notify_all();
			if (timeout_ms > 0)
			{
				const sint t = timeout_ms < 0 ? -timeout_ms : timeout_ms;
				c->cond.wait_for(lk, std::chrono::milliseconds(t),
					[&c] { return c->outbox.empty() || !c->good || !c->runing; });
			}
			return c->good;
		}

		bool addr_serial::send_frame(const json& value)
		{
			// 与 addr_ipv4 一致：不按 m_runing 拦截；克隆句柄（addr_from 造、挂进邻居表的那种）本来就不会 opened，
			// 它的发送靠 host_of 找到持有串口缆的本地端点（或自己拨缆）。
			return deliver(uuid(), value, 0);
		}

		bool addr_serial::send_frame(const json& value, sint timeout_ms)
		{
			return deliver(uuid(), value, timeout_ms);
		}

		bool addr_serial::send_broad(const json& value)
		{
			// 取舍：串口是**点对点**链路（一条缆只有一个对端），物理上没有广播这回事。
			// 这里 **不** 退化为「写串口总线」（那会把帧丢给未知设备、且不带任何路由信息），
			// 只做「进程内同 kind（serial）端点广播」，对齐 addr_ipv4「无 UDP 腿 → 进程内广播」的既有纪律。
			// 后果：link 的 bind 求邻不会真的上线；串口缆上的点对点邻居要么静态配置邻居句柄，
			//       要么等对端先发帧（send_node 学得 uuid 后，回程经 host_of 复用本端那条缆）。
			std::vector<addr_serial*> list;
			{
				std::lock_guard<std::mutex> lk(serial_mutex);
				for (auto& kv : serial_registry) { if (kv.second) list.push_back(kv.second); }
			}
			for (auto* one : list) { one->broad_signal.emit(value); }
			return !list.empty();
		}
	}
}
