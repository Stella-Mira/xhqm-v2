#ifndef XHQM_NEBULA_STAR
#define XHQM_NEBULA_STAR

#include "zero.h"

namespace xhqm
{
	namespace nebula
	{
		// type：可插拔组件的基类。约定：派生组件能由 `nebula::zero*` 构造。
		//   ★ 2026-10-06（用户口径）：
		//     ① `type_name()` 是**纯虚** —— 每个能力必须如实报出自己的类型名
		//        （`type_device::type_name()` 返回 "type_device"）。纯虚是**故意**的：
		//        谁忘了写就编不过，而不是悄悄借基类的名字（"认不出谁是谁"就是这么来的）。
		//     ② `type_info()` 由基类给出**通用实现**（对应仓库里各能力各写一份的 node_info()）：
		//        默认 {type, kind, text, tools}；有特殊字段（channels/devices…）的能力覆盖它即可。
		class type : public xhqm::software::signal_receiver
		{
		protected:
			nebula::json _info = nullptr;
			nebula::zero* _hand = nullptr;
		public:
			explicit type(nebula::zero* hand) : _hand{ hand }
			{
				if (_hand == nullptr)
				{
					throw xhqm::error("nebula::zero is nullptr");
				}
			}

			// 本组件的**实际类型名**（如 "type_device"）。纯虚：不写就不给编过。
			virtual xhqm::text type_name() const = 0;

			const nebula::json& info(void) const { return _info; };

			void info(const nebula::json& value) { _info = value; };

			// 组件级声明（通用实现）。tools 用**本节点**的工具表（与既有 node_info() 行为一致）。
			virtual nebula::json type_info() const
			{
				nebula::json tools = nebula::json::array();
				if (_hand) for (auto& k : _hand->kitset()) tools.push_back(k.make_json());
				return nebula::json{
					{ "type", type_name() },
					{ "kind", _info.is_object() ? _info.value("kind", nebula::text()) : nebula::text() },
					{ "text", _info.is_object() ? _info.value("text", nebula::text()) : nebula::text() },
					{ "tools", tools }
				};
			}

			virtual ~type() = default;
		};

		template<typename ...types>
		class star : public zero
		{
		protected:
			std::array<type*, sizeof...(types)> _list{};
		public:
			star(const star&) = delete;
			star& operator=(const star&) = delete;
			explicit star(const nebula::json& config)
				: nebula::zero(config), _list{ new types(this)... }
			{
				name(config.value("name", "star"));
				if (config.contains("info") && !config["info"].is_null())
				{
					info(config["info"]);
				}
			}
			virtual ~star() override
			{
				for (auto& one : _list) 
				{ 
					delete one; one = nullptr;
				}
			}
			virtual nebula::text star_name() const = 0;
			virtual nebula::json zero_info() override
			{
				nebula::json star_info = info();
				if (_list.size()) //实时更新info["list"]
				{
					// ★ 局部变量**不能叫 types**：外层模板是 `template<typename ...types>`，
					//   同名局部变量会遮蔽模板参数 —— MSVC 放过，GCC 直接报错：
					//     error: declaration of 'xhqm::nebula::json types' shadows template parameter
					//   实测（GCC 11）会让**整个 stella 32 个目标全挂**。JSON 键仍叫 "types"，线上格式不变。
					nebula::json type_list = nebula::json::array();
					nebula::json tools = nebula::json::array();
					for (auto* one : _list)
					{
						if (!one) continue;
						type_list.push_back(one->type_info());
					}
					for (auto& k : this->kitset())
					{
						tools.push_back(k.make_json());
					}
					star_info["list"] = nebula::json{
						{ "uuid", this->uuid().to_text() },
						{ "star", this->star_name() },
						{ "name", this->name() },
						{ "types", type_list },
						{ "tools", tools }
					};
					info(star_info);
				}
				return star_info;
			}
		};
	}
}

#endif
