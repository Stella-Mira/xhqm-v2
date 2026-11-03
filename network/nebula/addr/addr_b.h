#ifndef XHQM_NEBULA_ADDR_BASE
#define XHQM_NEBULA_ADDR_BASE

#include <map>
#include <thread>
#include <atomic>
#include "../nebula.h"
#include "../../../software/signal.h"

namespace xhqm
{
	namespace nebula
	{
		enum class kind 
		{ 
			no = 0, 
			uuid, 
			ipv4, 
			ipv6, 
			serial, 
			bluetooth, 
			nearlink, 
			quantum
		};

		xhqm_io size hardware_allow();
		xhqm_io text kind_text(const kind& k);
		xhqm_io kind text_kind(const text& s);

		// addr_b 是**所有地址实现**的接口：它自己就持有 frame_signal/broad_signal（link 往这两个信号上挂槽），
		// 因此接收者基类 signal_receiver 也放在这里——地址销毁时自动断开，是接口契约的一部分。
		// 好处：① 具体实现不会再漏继承 signal_receiver（漏了就留悬垂槽，销毁后 emit 会炸）；
		//       ② 不再需要多继承（此前 addr* 与 signal_receiver* 之间有 this 偏移，
		//          而信号槽的去重/自动断开按接收者指针工作，偏移很微妙）；
		//       ③ 所有地址都能统一 dynamic_cast 到 signal_receiver。
		class xhqm_io addr_b : public node, public xhqm::software::signal_receiver
		{
		protected:
			bool m_alive = true;
			xhqm::nebula::size m_fail = 0;
			xhqm::nebula::size m_last = 0;
			xhqm::nebula::kind m_type = xhqm::nebula::kind::no;
			std::map<nebula::uuid, std::shared_ptr<addr_b>, uuid_less> m_neighbors;	// 邻居节点：快照 API 需可拷贝：句柄用 shared_ptr
			std::atomic<bool> m_opened{ false };  //运行标志
			mutable std::mutex m_mutex;
		public:
			addr_b(const addr_b&) = delete;
			addr_b& operator=(const addr_b&) = delete;
			addr_b(nebula::kind _kind, nebula::uuid _uuid = {})
				: node(_uuid), m_type{ _kind }
			{

			}

			virtual bool opened() = 0;							// 启动网络 core（监听 + reactor 线程；幂等）
			virtual void closed() = 0;							// 关闭通道（停止 reactor 并唤醒阻塞）
			virtual bool send_broad(const json& value) = 0;					// 发送广播，地址是邻居地址（无 ACK）
			virtual bool send_frame(const json& value) = 0;					// 发送数据，地址是邻居地址（无 ACK）
			virtual bool send_frame(const json& value, sint timeout_ms) = 0;	// 发送数据，地址是邻居地址（带 ACK）
			virtual nebula::json make_json() const override; // 序列化
			inline bool is_open() const { return m_opened; }

			// 帧到达信号（零号协议无序、事件队列无限）：core 的 reactor 线程读到帧即 emit；
			xhqm::software::signal<const json&> frame_signal;	// 事件帧（TCP 事件通道）
			xhqm::software::signal<const json&> broad_signal;	// 广播帧（UDP 广播通道）


			inline void alive(bool value)
			{
				std::lock_guard<std::mutex> lock(m_mutex); m_alive = value;
			}
			inline void fail(xhqm::nebula::size value)
			{
				std::lock_guard<std::mutex> lock(m_mutex); m_fail = value;
			}
			inline void last(xhqm::nebula::size value)
			{
				std::lock_guard<std::mutex> lock(m_mutex); m_last = value;
			}
			inline bool alive() const
			{
				std::lock_guard<std::mutex> lock(m_mutex); return m_alive;
			}
			inline xhqm::nebula::size fail() const
			{
				std::lock_guard<std::mutex> lock(m_mutex); return m_fail;
			}
			inline xhqm::nebula::size last() const
			{
				std::lock_guard<std::mutex> lock(m_mutex); return m_last;
			}
			inline xhqm::nebula::kind type() const
			{
				std::lock_guard<std::mutex> lock(m_mutex); return m_type;
			}

			inline bool has_neighbor(const nebula::uuid& id) const
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				return (m_neighbors.find(id) != m_neighbors.end());
			}
			inline bool remove_neighbor(const nebula::uuid& id)
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				return (m_neighbors.erase(id) != 0);
			}
			inline void insert_neighbor(const std::shared_ptr<addr_b>& ev)
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_neighbors.emplace(ev->uuid(), ev);
				ev->last(xhqm::timesys::ntime());   // 走访问器（各自持锁）：直写成员与访问器的加锁读竞争
				ev->fail(0); ev->alive(true);
			}

			size size_neighbors() const
			{
				std::lock_guard<std::mutex> lk(m_mutex);
				return m_neighbors.size();
			}

			template<typename fun>
			void for_neighbors(fun&& function, bool with_lock)
			{
				if (with_lock)
				{
					std::lock_guard<std::mutex> lock(m_mutex);
					for (auto& neighbor : m_neighbors)
					{
						function(neighbor.second);
					}
				}
				else
				{
					for (auto& neighbor : m_neighbors)
					{
						function(neighbor.second);
					}
				}
			}

			template<typename fun>
			void run_neighbors(const nebula::uuid id, fun&& function, bool with_lock)
			{
				if (with_lock)
				{
					std::lock_guard<std::mutex> lock(m_mutex);
					auto it = m_neighbors.find(id);
					if (it != m_neighbors.end())
					{
						function(it->second);
					}
				}
				else
				{
					auto it = m_neighbors.find(id);
					if (it != m_neighbors.end())
					{
						function(it->second);
					}
				}
			}
		};
	};
}

#endif
