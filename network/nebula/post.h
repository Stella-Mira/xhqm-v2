#ifndef XHQM_NEBULA_POST
#define XHQM_NEBULA_POST

// L3 订阅推送（零号协议原则 + make/exec 句式）
//     信封只带「语义 + 身份」，不带地址与路径：
//       { type:"care|omit|push", id, from, goal, channel, data? }
//       from / goal / channel 都是节点或通道的 uuid：地址交给 addr 层按 uuid 解析，
//       路径交给 find 的图（L2 逐跳转发）——post 自己不再算路、不再维护途经表。
//     投递规则（全层统一）：
//       goal == 我      → 本层处理（exec_care / exec_omit / exec_push）
//       goal != 我      → find::exec_relay 逐跳转发，post 不参与
//     订阅表用两张正向/反向索引（推送时按通道直接取人）：
//       m_channel     通道 → 订阅者
//       m_subscribers 订阅者 → 状态（失败数 + 订过的通道）
//     语义约定：care / omit 由**订阅者**发起，goal = 通道拥有者；拥有者登记/注销订阅者。

#include "nebula.h"
#include "find.h"
#include <map>
#include <set>

namespace xhqm
{
	namespace nebula
	{
		class xhqm_io post : public find
		{
		public:
			struct push_state
			{
				xhqm::size fail = 0;              // 投递失败次数（既非邻居、图上也无路径 → +1）
				// 注意：内层容器也要显式给 uuid_less —— 默认 std::less<uuid> 走 uuid::operator<，
				// 它不区分不同 uuid（实测所有 uuid 互相等价），会让「一个通道」永远只装得下 1 个订阅者。
				std::set<nebula::uuid, nebula::uuid_less> channel;   // 该订阅者订过的通道
			};

			xhqm::size m_max_fail = 1;            // 失败上限：达到即摘掉订阅者
			mutable std::mutex m_push_mutex;
			std::map<nebula::uuid, std::set<nebula::uuid, nebula::uuid_less>, uuid_less> m_channel;   // 通道 → 订阅者
			std::map<nebula::uuid, push_state, uuid_less> m_subscribers;           // 订阅者 → 状态
		protected:
			virtual void exec_dath(const nebula::json&) override;
		protected:
			// 造信封：make 的产物只有语义与身份（地址、路径都不再是消息的一部分）
			nebula::json make_post(const nebula::text& type, const nebula::uuid& goal,
				const nebula::uuid& channel, const nebula::json& data = nebula::json());
			// 投递：可达性检查后把信封交给事件池——goal==我 → 本层处理；否则 find 转发
			bool send_goal(const nebula::uuid& goal, const nebula::json& value);
			virtual bool this_post(const nebula::json&);   // 这封信是不是给我的
			virtual void exec_care(const nebula::json&);
			virtual void exec_omit(const nebula::json&);
			virtual void exec_push(const nebula::json&);
			virtual text exec_event(const nebula::json&) override;
		public:
			post(const post&) = delete;
			post& operator=(const post&) = delete;
			explicit post(const xhqm::text& path, xhqm::size m, xhqm::size n, xhqm::size c, xhqm::size a, xhqm::size size, xhqm::size time)
				: nebula::find(path, m, n, c, a, size, time)
			{

			}

			inline xhqm::size max_fail(void) const { return m_max_fail; }
			inline void max_fail(xhqm::size value) { m_max_fail = value; }

			// 订阅 / 退订：订阅者发起，goal = 通道拥有者（拥有者侧登记/注销订阅者）
			void care(const nebula::uuid& owner, const nebula::uuid& channel);
			void omit(const nebula::uuid& owner, const nebula::uuid& channel);
			// 定向推送：goal = 某个订阅者（拥有者侧通常直接用 publish）
			void push(const nebula::json& data, const nebula::uuid& subscriber, const nebula::uuid& channel);
			// 拥有者侧：把该通道的数据推给全部订阅者，返回成功投出的份数（不可达的记一次 fail）
			xhqm::size publish(const nebula::json& data, const nebula::uuid& channel);
			// 本节点登记的某通道订阅者（诊断 / 测试用）
			std::vector<nebula::uuid> subscribers(const nebula::uuid& channel) const;

			// 本节点收到的订阅数据：(data, 来源节点, 通道)
			xhqm::software::signal<const nebula::json&, const nebula::uuid&, const nebula::uuid&> care_hander;
		};
	}
}

#endif
