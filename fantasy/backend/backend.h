#pragma once
#ifndef XHQM_FANTASY_BACKEND
#define XHQM_FANTASY_BACKEND

// ============================================================
// 渲染后端抽象：编译宏归一化 + 运行时后端标识 + 预编译 shader 数据
//
// 设备 → 后端映射（编译期 + 运行期一致，均为运行时探测回退）：
//   DEVICE=NO/CPU/NET  → 仅 none（BACKEND 无效）
//   DEVICE=GPU         → BACKEND=AUTO: vk→gl→none
//                        BACKEND=VULKAN: vk→none
//                        BACKEND=OPENGL: gl→none
//   DEVICE=AUTO        → vk→gl→none（BACKEND 无效）
//
// 编译宏由 CMake 选项注入：
//   BUILD_WITH_VULKAN / BUILD_WITH_OPENGL（GPU/AUTO 时按 BACKEND 组合）
// ============================================================

#include "../../hardware/hardware.h"
#include <cstdint>
#include <string>
#include <vector>

namespace xhqm
{
	namespace fantasy
	{
		// 后端种类
		enum class backend_kind : std::uint32_t
		{
			none = 0,	// 无渲染（桩实现）
			opengl = 1,	// OpenGL
			vulkan = 2,	// Vulkan
		};

		// 编译期默认后端：优先 Vulkan，回退 OpenGL，最终 none
		constexpr backend_kind compiled_backend(void) noexcept
		{
#if defined(BUILD_WITH_VULKAN)
			return backend_kind::vulkan;
#elif defined(BUILD_WITH_OPENGL)
			return backend_kind::opengl;
#else
			return backend_kind::none;
#endif
		}

		// 某后端是否已编译进来
		constexpr bool backend_compiled(const backend_kind& kind) noexcept
		{
#if defined(BUILD_WITH_OPENGL) && defined(BUILD_WITH_VULKAN)
			return (kind == backend_kind::opengl || kind == backend_kind::vulkan || kind == backend_kind::none);
#elif defined(BUILD_WITH_OPENGL)
			return (kind == backend_kind::opengl || kind == backend_kind::none);
#elif defined(BUILD_WITH_VULKAN)
			return (kind == backend_kind::vulkan || kind == backend_kind::none);
#else
			return (kind == backend_kind::none);
#endif
		}

		// 运行时激活的后端标识（ informational，实际后端经 render_backend_active() 获取 ）
		inline backend_kind& active_backend_ref(void) noexcept
		{
			static backend_kind active = compiled_backend();
			return active;
		}
		inline backend_kind active_backend(void) noexcept { return active_backend_ref(); }
		inline void active_backend(const backend_kind& kind) noexcept { active_backend_ref() = kind; }

		// 后端名（用于二进制缓存打标，防止跨后端误载）
		inline const char* backend_name(const backend_kind& kind) noexcept
		{
			switch (kind)
			{
			case backend_kind::opengl: return "opengl";
			case backend_kind::vulkan: return "vulkan";
			case backend_kind::none:
			default: return "none";
			}
		}

		// 硬件设备 → 渲染后端：no/cpu/net 恒为 none，gpu/auto 按编译宏取默认
		// 注意：实际运行时后端经 render_backend_active() 动态探测，此处仅为编译期标识
		inline backend_kind device_backend(const xhqm::hardware::device& kind) noexcept
		{
			switch (kind)
			{
			case xhqm::hardware::device::no:
			case xhqm::hardware::device::cpu:
			case xhqm::hardware::device::net:
				return backend_kind::none;
			case xhqm::hardware::device::gpu:
			case xhqm::hardware::device::auto_:
			default:
				return compiled_backend();
			}
		}

		// ---- 着色器预编译抽象数据 ----

		// 着色器源码（编译输入）
		struct shader_source
		{
			std::string vertex;		// 顶点着色器源码
			std::string fragment;	// 片元着色器源码
		};

		// 着色器二进制（预编译产物，可直接加载）
		struct shader_binary
		{
			std::uint32_t format = 0;			// 后端相关二进制格式标识
			std::vector<unsigned char> blob;	// 二进制数据
			std::string backend;				// 后端名（backend_name 结果）
			std::string key;					// 源码哈希/标识，用于缓存命中校验
		};
	}
}

#endif
