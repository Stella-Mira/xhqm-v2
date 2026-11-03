#pragma once
#ifndef XHQM_TENSOR_BACKEND
#define XHQM_TENSOR_BACKEND

// ============================================================
// 张量后端抽象：设备选择沿用硬件库（hardware::device）。
// 编译宏由 CMake 选项 XHQM_DEVICE + XHQM_TENSOR_BACKEND 注入：
//   TENSOR_WITH_CPU  —— 编译 CPU 后端（ON 时含 blas→simd→并行内部降级；OFF 时仅并行桩）
//   TENSOR_WITH_GPU  —— 编译 GPU 后端（cuda/metal；无 CUDA 为桩回退 CPU）
//   XHQM_TENSOR_CUDA / XHQM_TENSOR_METAL —— GPU 实际后端
//   （no 后端恒编入；net 占位回退 CPU/no）
// ============================================================

#include "../../../hardware/hardware.h"
#include <cstdint>

namespace xhqm
{
	namespace tensor_base
	{
		// 设备类别：直接沿用硬件库（no/cpu/gpu/net/auto）
		using device = xhqm::hardware::device;

		// 编译期默认后端
		constexpr device compiled_backend(void) noexcept { return xhqm::hardware::compiled_device(); }
		// 某后端是否已编译进来
		constexpr bool backend_compiled(const device& kind) noexcept { return xhqm::hardware::device_compiled(kind); }
		// 运行时激活的后端（auto 模式下可调用 active_backend(kind) 切换）
		inline device& active_backend_ref(void) noexcept { return xhqm::hardware::active_device_ref(); }
		inline device active_backend(void) noexcept { return xhqm::hardware::active_device(); }
		inline void active_backend(const device& kind) noexcept { xhqm::hardware::active_device(kind); }
		// 后端名
		inline const char* backend_name(const device& kind) noexcept { return xhqm::hardware::device_name(kind); }
	}
}

#endif
