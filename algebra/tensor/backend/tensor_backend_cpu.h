#pragma once
#ifndef XHQM_TENSOR_BACKEND_CPU
#define XHQM_TENSOR_BACKEND_CPU
#include "tensor_backend.h"

namespace xhqm
{
	namespace tensor_base
	{
		// 工厂：创建多线程 CPU 后端（单例；device::cpu）
		tensor_backend* create_cpu_backend();
	}
}

#endif
