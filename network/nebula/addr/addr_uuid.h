#ifndef XHQM_NEBULA_ADDR_UUID
#define XHQM_NEBULA_ADDR_UUID

#include "./addr_b.h"

namespace xhqm
{
	namespace nebula
	{
		class xhqm_io addr_uuid : public addr_b
		{
			bool m_registered = false;           // 是否已进注册表（克隆句柄不进；析构只按此删除）
			void _frame_signal_(const nebula::json&);	// 事件帧（事件通道）
			void _broad_signal_(const nebula::json&);	// 广播帧（广播通道）
		public:
			explicit addr_uuid(const nebula::uuid& _uuid = {});
			virtual ~addr_uuid() override;

			virtual bool opened() override;							// 启动网络 core（回环无需启动）
			virtual void closed() override;							// 关闭通道
			virtual bool send_broad(const nebula::json&) override;			// 发送广播，地址是邻居地址（无 ACK）
			virtual bool send_frame(const nebula::json&) override;			// 发送数据，地址是邻居地址（无 ACK）
			virtual bool send_frame(const nebula::json&, nebula::sint) override;	// 发送数据，地址是邻居地址（带 ACK）
		};
	};
}

#endif
