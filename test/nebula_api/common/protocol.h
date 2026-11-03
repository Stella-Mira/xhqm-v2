#ifndef NEBULA_API_COMMON_PROTOCOL
#define NEBULA_API_COMMON_PROTOCOL

// ============================================================
// 协议常量与消息构造辅助（PROTOCOL.md v1 的代码落点）
// 两端（client/server）编译同一份头：通道 uuid 与字段名天然一致
// ============================================================

#include "../../../network/nebula/data.h"
#include "../../../hardware/media.h"

namespace nebula_api
{
	namespace common
	{
		using json = xhqm::nebula::json;
		using uuid = xhqm::nebula::uuid;
		namespace media = xhqm::hardware::media;   // 命名空间别名

		// 协议版本（所有消息带 "v" 字段）
		constexpr int protocol_version = 1;

		// ---- 全局通道（固定 uuid 常量，两端同源编译）----
		// devices：设备注册/注销
		// streams：流发布/停止
		// acks：指令回执
		inline const uuid& devices_channel()
		{
			static const uuid ch = uuid::to_uuid("e373c72f-99a5-4f92-8432-07f9d3916692");
			return ch;
		}
		inline const uuid& streams_channel()
		{
			static const uuid ch = uuid::to_uuid("36e63c97-4ee3-4828-aa6a-357b36f784fd");
			return ch;
		}
		inline const uuid& acks_channel()
		{
			static const uuid ch = uuid::to_uuid("26624020-2eb9-4e55-bdd7-2f3588247741");
			return ch;
		}

		// 设备指令通道：uuid v3 确定性派生（namespace + device_id，两端同算同得）
		inline uuid command_channel(const xhqm::text& device_id)
		{
			static const uuid ns = uuid::to_uuid("9f4f7c6e-5e2b-4c6a-9d3e-1a2b3c4d5e6f");
			return uuid::creat_uuid_v3(ns, device_id);
		}

		// ---- 消息构造（外壳字段统一：v / op / ts_ns）----

		// 订阅推送的数据形态 = 窗口快照（数组 [{"time","json"},...]，data 层 upload 后
		// publish 快照语义）；消费端取最后一条的 "json" 为最新消息（PROTOCOL.md §3.0）
		inline json latest_message(const json& pushed)
		{
			if (pushed.is_array() && !pushed.empty())
			{
				const json& last = pushed.back();
				if (last.is_object() && last.contains("json")) return last["json"];
				return last;
			}
			return pushed;
		}

		// devices：上线注册
		inline json make_register(const media::caps& c)
		{
			return json{
				{ "v", protocol_version },
				{ "op", "register" },
				{ "caps", c.to_json() },
				{ "ts_ns", xhqm::timesys::ntime() }
			};
		}

		// devices：主动注销（优雅下线；被动下线靠窗口过期）
		inline json make_unregister(const xhqm::text& device_id)
		{
			return json{
				{ "v", protocol_version },
				{ "op", "unregister" },
				{ "device", device_id },
				{ "ts_ns", xhqm::timesys::ntime() }
			};
		}

		// streams：起流（地址就绪时发）
		inline json make_stream_up(const xhqm::text& device_id, const xhqm::text& url, const json& format)
		{
			return json{
				{ "v", protocol_version },
				{ "op", "up" },
				{ "device", device_id },
				{ "url", url },
				{ "format", format },
				{ "ts_ns", xhqm::timesys::ntime() }
			};
		}

		// streams：停流（设备 close 时发）
		inline json make_stream_down(const xhqm::text& device_id)
		{
			return json{
				{ "v", protocol_version },
				{ "op", "down" },
				{ "device", device_id },
				{ "ts_ns", xhqm::timesys::ntime() }
			};
		}

		// commands/<device_id>：指令（自动填 v/id/ts_ns；id = 回执关联键）
		inline json make_command(const xhqm::text& action, const json& params)
		{
			return json{
				{ "v", protocol_version },
				{ "id", xhqm::cipher::uuid::to_text(xhqm::cipher::uuid::creat_uuid_v4({})) },
				{ "action", action },
				{ "params", params },
				{ "ts_ns", xhqm::timesys::ntime() }
			};
		}

		// acks：指令回执（cmd_id 与指令 id 对应）
		inline json make_ack(const json& cmd, bool ok, const json& result, const xhqm::text& device_id)
		{
			return json{
				{ "v", protocol_version },
				{ "cmd_id", cmd.value("id", "") },
				{ "device", device_id },
				{ "ok", ok },
				{ "result", result },
				{ "ts_ns", xhqm::timesys::ntime() }
			};
		}
	}
}

#endif
