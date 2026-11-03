#pragma once
#ifndef XHQM_FANTASY_SPIRV
#define XHQM_FANTASY_SPIRV

// GLSL -> SPIR-V 编译链（仅 Vulkan 后端使用，依赖 glslang 库）
#include <cstdint>
#include <string>
#include <vector>

namespace xhqm
{
	namespace fantasy
	{
		// 着色器阶段
		enum class spirv_stage : int
		{
			vertex = 0,
			fragment = 1,
			compute = 2,
		};

		// 进程级初始化 / 结束（各调用一次）
		void spirv_initialize();
		void spirv_finalize();

		// 编译 GLSL 源码为 SPIR-V（成功返回 true，spv 为 32 位字数组）
		bool compile_glsl_to_spv(const std::string& source, spirv_stage stage,
			std::vector<std::uint32_t>& spv, std::string& error);
	}
}

#endif
