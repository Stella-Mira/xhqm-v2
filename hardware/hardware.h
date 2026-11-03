#ifndef XHQM_HARDWARE
#define XHQM_HARDWARE

// ============================================================
// 硬件库：通用设备选择能力，供各模块共享（fantasy 渲染 / algebra 张量 / 未来）。
//
// 关键词：no / cpu / gpu / net / auto
//   no   —— 无设备（串行 / 桩兜底）
//   cpu  —— CPU（内部自动降级 blas → simd → thread）
//   gpu  —— GPU（cuda；无 CUDA 则桩回退 cpu）
//   net  —— 分布式网络（占位，不参与 auto 匹配，需显式指定）
//   auto —— 自动匹配（全编译，运行时 gpu → cpu → no）
//
// 编译宏由 CMake 选项 XHQM_DEVICE 注入（NO / CPU / GPU / NET / AUTO）：
//   XHQM_DEVICE_NO / XHQM_DEVICE_CPU / XHQM_DEVICE_GPU / XHQM_DEVICE_NET / XHQM_DEVICE_AUTO
// ============================================================

// AUTO 归一化：同时启用 CPU 与 GPU（net 可选，另行编译）
#ifdef XHQM_DEVICE_AUTO
#  ifndef XHQM_DEVICE_CPU
#    define XHQM_DEVICE_CPU
#  endif
#  ifndef XHQM_DEVICE_GPU
#    define XHQM_DEVICE_GPU
#  endif
#endif

#include <cstdint>

namespace xhqm
{
	namespace hardware
	{
		// 通用设备类别
		enum class device : std::uint32_t
		{
			no = 0,		// 无（串行/桩）
			cpu = 1,	// CPU
			gpu = 2,	// GPU
			net = 3,	// 分布式网络
			auto_ = 4,	// 自动匹配
		};

		// 编译期默认设备
		constexpr device compiled_device(void) noexcept
		{
#if defined(XHQM_DEVICE_AUTO)
			return device::auto_;
#elif defined(XHQM_DEVICE_GPU)
			return device::gpu;
#elif defined(XHQM_DEVICE_NET)
			return device::net;
#elif defined(XHQM_DEVICE_CPU)
			return device::cpu;
#else
			return device::no;
#endif
		}

		// auto 解析：gpu → cpu → no（net 不参与自动匹配）
		constexpr device resolve_auto(void) noexcept
		{
#if defined(XHQM_DEVICE_GPU)
			return device::gpu;
#elif defined(XHQM_DEVICE_CPU)
			return device::cpu;
#else
			return device::no;
#endif
		}

		// 某设备是否已编译进来
		constexpr bool device_compiled(const device& kind) noexcept
		{
			switch (kind)
			{
			case device::no: return true;
			case device::cpu:
#if defined(XHQM_DEVICE_CPU)
				return true;
#else
				return false;
#endif
			case device::gpu:
#if defined(XHQM_DEVICE_GPU)
				return true;
#else
				return false;
#endif
			case device::net:
#if defined(XHQM_DEVICE_NET)
				return true;
#else
				return false;
#endif
			case device::auto_: return true;
			default: return false;
			}
		}

		// 运行时激活设备
		inline device& active_device_ref(void) noexcept
		{
			static device active = compiled_device();
			return active;
		}
		inline device active_device(void) noexcept { return active_device_ref(); }
		inline void active_device(const device& kind) noexcept { active_device_ref() = kind; }

		// 设备名（用于日志 / 缓存打标）
		inline const char* device_name(const device& kind) noexcept
		{
			switch (kind)
			{
			case device::cpu: return "cpu";
			case device::gpu: return "gpu";
			case device::net: return "net";
			case device::auto_: return "auto";
			case device::no:
			default: return "no";
			}
		}
	}
}

#endif
