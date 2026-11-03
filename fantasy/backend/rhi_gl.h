#pragma once
#ifndef XHQM_FANTASY_RHI_GL
#define XHQM_FANTASY_RHI_GL
#include "rhi_no.h"

namespace xhqm
{
	namespace fantasy
	{
		// 工厂：创建 OpenGL 后端（单例）
		render_backend* create_gl_backend();
	}
}

#endif
