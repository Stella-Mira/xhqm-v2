#ifndef XHQM_NEBULA_LINK
#define XHQM_NEBULA_LINK

// nebula L1 节点（link 层）：有界度邻居图（0<=m<=x<=n）+ 入网 + 心跳判死 + 事件词汇。
// 网络后端使用：帧经 frame_signal/broad_signal 直达 life::recv_event；
// link 自己只持有心跳线程（ping），收发 reactor 全部归 addr。

#include "nebula.h"
#include "life.h"
#include "addr.h"

namespace xhqm
{
	namespace nebula
	{
		class xhqm_io link : public life
		{
		protected:
			xhqm::size m_f = 10;//上限失联次数
			xhqm::size m_t = 1000;//心跳间隔毫秒
			xhqm::size m_s = 1000;//发送超时毫秒
			std::vector<addr> m_addr_listen;  // 自身地址
			std::atomic<bool> m_ping_runing;  // 心跳标志
			std::thread m_ping_thread;		  // 心跳线程 //发心跳需要实时性，不并入存活线程池
		protected:
			virtual void ping_runing();
			nebula::json make_dath();
			nebula::json make_ping();
			nebula::json make_bind();
			nebula::json make_link();
			nebula::json make_send(const nebula::json&);
			virtual void exec_send(const nebula::json&);
			virtual void exec_dath(const nebula::json&);
			virtual void exec_ping(const nebula::json&);
			virtual void exec_bind(const nebula::json&);
			virtual void exec_link(const nebula::json&);
			virtual text exec_event(const nebula::json&);
			virtual void send_heart(const nebula::json&);	//发送一条心跳（私发，无 ACK）
			virtual void send_broad(const nebula::json&);	//发送一条广播（公开，无 ACK）
			virtual void recv_event(const nebula::uuid&, const nebula::json&);	//发送一条广播（公开，无 ACK）
		public:
			explicit link(const xhqm::text& path, xhqm::size m, xhqm::size n, xhqm::size c, xhqm::size a, xhqm::size size, xhqm::size time)
				: nebula::life(path, m, n, c, a, size, time), m_addr_listen{}, m_ping_runing{ false }
			{

			}

			virtual ~link() override
			{
				this->dath();
				this->stop();
			};

			void insert_addr(const nebula::addr& ad);
			void remove_addr(const nebula::addr& ad);

			bool remove_neighbor(const nebula::uuid& id);
			bool remove_neighbor(const nebula::uuid& a_id, const nebula::uuid& id);
			bool insert_neighbor(const nebula::uuid& a_id, const nebula::addr& id, bool force);
			std::vector<nebula::uuid> neighbors_uuid() const;
			std::vector<nebula::addr> neighbors_addr() const;
			bool has_neighbor(const nebula::uuid&) const;


			// 心跳间隔 / 失联上限 / 发送超时
			void heart_time(uint t, uint f, uint s);
			size degree() const;// 实际连接个数
			bool life();//启动节点
			bool stop();//结束节点

			//事件 event
			//本地同步执行 exec_event(event)
			//本地异步执行 life_event(event)
			//远端异步执行 recv_event(send_event(event))
		};
	}
}

#endif
