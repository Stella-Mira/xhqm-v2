#pragma once
#ifndef XHQM_TENSOR_BACKEND_NO
#define XHQM_TENSOR_BACKEND_NO
#include "tensor_backend.h"

namespace xhqm
{
	namespace tensor_base
	{
		// 工厂：创建串行桩后端（单例；device::none，兜底与显式选择均可用）
		tensor_backend* create_no_backend();
	}
}

#endif
