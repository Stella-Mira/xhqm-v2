#pragma once
#ifndef XHQM_TENSOR_BACKEND_GPU
#define XHQM_TENSOR_BACKEND_GPU
#include "tensor_backend.h"

namespace xhqm
{
	namespace tensor_base
	{
		// 工厂：创建 GPU 后端（单例；device::gpu）。
		// 当前为桩：声明内存搬运能力，但算子静默回退主机串行执行，
		// 不反向依赖 fantasy 的 rhi，避免层级倒挂。
		tensor_backend* create_gpu_backend();
	}
}

#endif
