#pragma once
#ifndef XHQM_FANTASY_RHI_VK
#define XHQM_FANTASY_RHI_VK
#include "rhi_no.h"

namespace xhqm
{
	namespace fantasy
	{
		// 工厂：创建 Vulkan 后端（当前为占位实现）
		render_backend* create_vk_backend();
	}
}

#endif
