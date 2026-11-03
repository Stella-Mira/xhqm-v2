#pragma once
#ifndef XHQM_FANTASY_LIGHT
#define XHQM_FANTASY_LIGHT

// 光源定义（GPU 上传布局）+ 场景级光照环境。
#include "fantasy.h"
#include "colors.h"
#include <vector>

namespace xhqm
{
	namespace fantasy
	{
		// 光源类型：0 方向光 / 1 点光 / 2 聚光
		enum class light_type : uint
		{
			directional = 0,
			point = 1,
			spot = 2,
		};

		// 最大光源数（UBO 固定数组容量）
		constexpr uint MAX_LIGHTS = 8;
		// 每光 GPU 布局字节数 = 4 vec4
		constexpr uint LIGHT_BYTES = 64;

		struct light_desc
		{
			light_type type;
			rgba color;          // 光色（含强度）
			point position;      // 点光/聚光：世界位置
			point direction;     // 方向光/聚光：光线行进方向（光源 → 场景）
			value cutoff;        // 聚光内角余弦
			value outer_cutoff;  // 聚光外角余弦

			// 显式 inline 构造：避免隐式默认构造函数在多个 TU 中生成强符号（demo 链接 LNK2005）
			inline light_desc()
				: type(light_type::directional)
				, color(0xffffffff)
				, position(0, 0, 0)
				, direction(0, 0, -1)
				, cutoff(0.9f)
				, outer_cutoff(0.8f)
			{}
		};

		// 场景级光照环境：收拢光源列表 / IBL 环境光 / 光照开关。
		// 它是「数据」（跨帧持久），与「渲染 pass」（theory_pass）分离——
		// 被 theory_scene / theory_shadow / theory_ibl 消费。
		// 复用原「光源节点」类名 light（原 light 节点已废弃删除，theory_light 亦无实现）。
		class xhqm_io light
		{
			std::vector<light_desc> m_lights;
			rgba m_ambient = 0x505050;
			value m_reflection = 1.0f;
			bool m_enabled = true;
			uint m_irradiance = 0;   // irradiance cubemap 纹理句柄（theory_ibl 卷积结果；0=无，走常数 ambient）
			uint m_prefiltered = 0; // prefiltered cubemap（specular IBL 镜面反射采样源）
			// CPU 缓存：lights → float 数组（每光 16 value，与 uniform_lights 布局一致）。
			// lights 变化时失效，避免逐 drawer 每帧重复构造。
			mutable std::vector<value> m_light_data;
			mutable bool m_light_data_dirty = true;
		public:
			light() = default;
			~light() = default;

			// ---- 光源列表 ----
			void lights(const std::vector<light_desc>& value) { m_lights = value; m_light_data_dirty = true; }
			const std::vector<light_desc>& lights() const { return m_lights; }
			void add_light(const light_desc& l) { m_lights.push_back(l); m_light_data_dirty = true; }
			void clear_lights() { m_lights.clear(); m_light_data_dirty = true; }
			// 替换已有方向光，无则追加（阴影 pass 用于把主方向光写回光照环境）
			void set_directional(const light_desc& l)
			{
				for (auto& x : m_lights)
					if (x.type == light_type::directional) { x = l; m_light_data_dirty = true; return; }
				add_light(l);
			}
			uint light_count() const { return static_cast<uint>(m_lights.size()); }

			// ---- IBL 环境光 ----
			void ambient(const rgba& v) { m_ambient = v; }
			const rgba& ambient() const { return m_ambient; }
			void reflection(value v) { m_reflection = v; }
			value reflection() const { return m_reflection; }
			void irradiance(uint v) { m_irradiance = v; }
			uint irradiance() const { return m_irradiance; }
			void prefiltered(uint v) { m_prefiltered = v; }
			uint prefiltered() const { return m_prefiltered; }

			// ---- 光照开关 ----
			void enabled(bool v) { m_enabled = v; }
			bool enabled() const { return m_enabled; }

			// ---- CPU 缓存：lights → float 数组（每光 16 value，与 uniform_lights 布局一致）----
			const std::vector<value>& light_data() const
			{
				if (m_light_data_dirty)
				{
					m_light_data.assign(m_lights.size() * 16, 0);
					for (size_t i = 0; i < m_lights.size(); ++i)
					{
						const light_desc& l = m_lights[i];
						std::array<real, 4> c = xhqm::color::rgba2array(l.color);
						value* p = m_light_data.data() + i * 16;
						p[0] = c[0]; p[1] = c[1]; p[2] = c[2];
						p[3] = static_cast<value>(static_cast<uint>(l.type));
						p[4] = l.position.x(); p[5] = l.position.y(); p[6] = l.position.z();
						p[8] = l.direction.x(); p[9] = l.direction.y(); p[10] = l.direction.z();
						p[12] = l.cutoff; p[13] = l.outer_cutoff;
					}
					m_light_data_dirty = false;
				}
				return m_light_data;
			}
		};
	}
}

#endif
