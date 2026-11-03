#ifndef XHQM_NEBULA_ZERO
#define XHQM_NEBULA_ZERO

// ============================================================
// zero：零号协议的顶层节点基类（原 tool 层与原来的 zero 组装层合并为一个名字）
//   词表：
//     logic   逻辑（自组织执行过程）
//     proxy   委托（goto 执行 kit 或内嵌 event，结果经 message 返回）
//     claim   索取（goto 的 uuid/name/info/tool，结果经 message 返回）
//     message 消息（goto 空=广播人人收，非空=通知该节点）
//   工具：kit 工具表（ket_make/ket_name/ket_info + string/number/boolean/object_param 生成 JSON Schema），
//         挂到节点上即全网可见（claim 应答里带 tools 声明）。
//   组装：两个 config 便捷构造（由配置文件决定实际类型），c 缺省 = min(m+1, 硬件)，a 缺省 = min(n*2, 硬件)。
//   继承链：node → data → life → link → find → post → zero → star（star 是组件组合层）
//   每个真实节点都是全息的：自带完整栈，在自己的组合逻辑里选择用哪些能力。
// ============================================================

#include "nebula.h"
#include "post.h"

#include <mutex>
#include <deque>
#include <chrono>
#include <functional>

namespace xhqm
{
	namespace nebula
	{
		class xhqm_io zero : public post
		{
		public:
			using fun = std::function<nebula::json(const nebula::json&)>;
			class kit : public nebula::node
			{
				zero::fun make;
				nebula::json schema;
			public:
				kit& param(const nebula::text& name, const nebula::text& type,
					const nebula::text& desc, bool required)
				{
					schema["type"] = "object";
					schema["properties"][name] = nebula::json{ {"type", type}, {"description", desc} };
					if (required) schema["required"].push_back(name);
					return *this;
				}
				virtual nebula::json make_json() const override
				{
					nebula::json j = nebula::node::make_json();
					j["inputSchema"] = (schema.is_null() ? nebula::json::object() : schema);
					return j;
				}

				nebula::json operator ()(const nebula::json& input)
				{
					try
					{
						return make(input);
					}
					catch (...) {};
					return json::object();
				}

				inline operator bool() const
				{
					return bool(make);
				}
			public:
				kit& ket_make(zero::fun _make)
				{
					this->make = _make;
					return *this;
				}
				kit& ket_name(const nebula::text& _name)
				{
					this->name(_name);
					return *this;
				}
				kit& ket_info(const nebula::json& _info)
				{
					this->info(_info);
					return *this;
				}
				kit& ket_uuid(const nebula::uuid& _uuid)
				{
					this->uuid(_uuid);
					return *this;
				}
				kit& object_param(const nebula::text& name,
					const nebula::text& desc = "", bool required = true)
				{
					return param(name, "object", desc, required);
				}
				kit& string_param(const nebula::text& name,
					const nebula::text& desc = "", bool required = true)
				{
					return param(name, "string", desc, required);
				}
				kit& number_param(const nebula::text& name,
					const nebula::text& desc = "", bool required = true)
				{
					return param(name, "number", desc, required);
				}
				kit& boolean_param(const nebula::text& name,
					const nebula::text& desc = "", bool required = true)
				{
					return param(name, "boolean", desc, required);
				}
			};
		protected:
			std::mutex m_logic_mutex;
			mutable std::mutex m_tool_mutex;
			std::map<xhqm::text, zero::kit> m_tool_kitset;
		protected:
			nebula::json make_logic(const nebula::json&);
			nebula::json make_proxy(const nebula::json&);
			nebula::json make_claim(const nebula::json&);
			nebula::json make_message(const nebula::json&);
			virtual void exec_logic(const nebula::json&);
			virtual void exec_proxy(const nebula::json&);
			virtual void exec_claim(const nebula::json&);
			virtual void exec_message(const nebula::json&);
			virtual text exec_event(const json&) override;
		public:
			zero(const zero&) = delete;
			zero& operator=(const zero&) = delete;
			explicit zero(const nebula::json& config);// 通过配置文件决定实际类型
			// 池参数：c = min(m+1, 硬件)（池下限）；a = max(c, min(n*2, 硬件))（池上限）。
			// 两者只从入参 / config 取数，**不读基类成员**——基类链（post → … → life）此刻尚未构造。
			explicit zero(const nebula::json& config, xhqm::size m, xhqm::size n);
			explicit zero(const xhqm::text& path, xhqm::size m, xhqm::size n, xhqm::size c, xhqm::size a, xhqm::size size, xhqm::size time)
				: nebula::post(path, m, n, c, a, size, time)
			{

			}

			std::vector<zero::kit> kitset() const
			{
				std::lock_guard<std::mutex> lk(m_tool_mutex);
				std::vector<zero::kit> list;
				for (const auto& kit : m_tool_kitset)
				{
					list.push_back(kit.second);
				}
				return list;
			}

			bool insert_tool(const zero::kit& kit)
			{
				// 新插入=成功、重复插入=拒绝（insert 语义）
				std::lock_guard<std::mutex> lk(m_tool_mutex);
				if (!kit) return false;
				if (m_tool_kitset.find(kit.uuid().to_text()) != m_tool_kitset.end()) return false;
				m_tool_kitset.emplace(kit.uuid().to_text(), kit);
				return true;
			}
			bool remove_tool(const zero::kit& kit)
			{
				std::lock_guard<std::mutex> lk(m_tool_mutex);
				return (m_tool_kitset.erase(kit.uuid().to_text()) != 0);
			}

			// 触发一次全局claim事件
			void broad_claim(const nebula::json&);
			// 调用方传裸载荷：{goto, data}
			void create_message(const nebula::json&);
			// 节点执行逻辑
			virtual void logic(const nebula::json&) = 0;
			// 消息接收信号
			xhqm::software::signal<const nebula::json&> data_signal;

			virtual nebula::json zero_info() = 0;
		};
	}
}

#endif
