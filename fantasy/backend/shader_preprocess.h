#pragma once
#ifndef XHQM_FANTASY_SHADER_PREPROCESS
#define XHQM_FANTASY_SHADER_PREPROCESS

// 统一着色器源预处理：把 shader_sources.h 里的 XHQM_* 宏展开为
// GLSL 330 core（GL 后端）或 GLSL 450（Vulkan 后端）。
// 宏清单：
//   XHQM_VERSION       -> #version 330 core / #version 450
//   XHQM_UBO_BLOCK     -> layout(std140) uniform UBO {...} ubo;（GL）/ set=binding 版（VK）
//   XHQM_DEPTH_REMAP   -> 空（GL）/ gl_Position.z = 0.5*(z+w)（VK）
//   XHQM_SHADOW_UV_FLIP-> 空（GL）/ shadowUV.y = 1 - shadowUV.y（VK，阴影图采样 v 翻转，见 #48）
//   XHQM_LIGHTING      -> 完整逐灯光循环 / 极简漫反射（apple_gl=true：Apple GL 驱动上 UBO 灯光数组
//                         结构体拷贝与动态索引会杀死片元，仅该组合走极简；其余平台保持完整循环）
//   XHQM_TEX0/1/2      -> 颜色纹理采样器声明（VK 绑定 binding 1..3）
//   XHQM_SHADOW_MAP    -> 级联阴影采样器数组声明（VK 绑定 binding 4..7）
//   XHQM_SPRITE_TEX    -> sprite 单纹理声明（VK 绑定 binding 1）
//   XHQM_COLOR_TEX     -> 图元单纹理声明（VK 绑定 binding 1）
#include "fantasy.h"     // 取 xhqm_io（导出宏）；只带 <string> 的话本文件在 DLL 外不可见（实测 LNK2019）
#include <string>

namespace xhqm
{
	namespace fantasy
	{
		// apple_gl：仅 macOS 上的 OpenGL 后端置 true（Apple GL 驱动兼容性变通），默认 false 不影响其它平台
		std::string xhqm_io shader_preprocess(const std::string& src, bool vulkan, bool apple_gl = false);
	}
}

#endif
