#pragma once
#ifndef XHQM_TENSOR_BACKEND_METAL
#define XHQM_TENSOR_BACKEND_METAL
#include "tensor_backend.h"

namespace xhqm
{
	namespace tensor_base
	{
		// 工厂：创建 Metal（Apple Silicon）GPU 后端（device::gpu）。
		// 通过 Metal Performance Shaders 与 Metal compute 内核做真实 GPU 并行；
		// 平台无 Metal 设备时返回 nullptr，调用方回退 CPU。
		// 仅 f32 走 Metal kernel；f64 等其余类型返回 false，由调用方回退 CPU。
		tensor_backend* create_metal_backend();
	}
}

#endif
