#pragma once
#ifndef XHQM_FANTASY_GLTF_LOADER
#define XHQM_FANTASY_GLTF_LOADER

// glTF 2.0 原生加载（M10）：解析 .glb / .gltf，构建 prefab + material。
// v1 范围：静态网格（POSITION/NORMAL/TEXCOORD_0 + 索引）+ PBR metallic-roughness 材质 + 节点层级变换。
// 不含：图像/纹理解码、蒙皮、动画、morph target（后续补）。
#include "fantasy.h"
#include "drawer.h"
#include <vector>
#include <string>

namespace xhqm
{
	namespace fantasy
	{
		namespace gltf
		{
			struct load_result
			{
				std::vector<prefab*> prefabs;     // 每个网格图元一个（geometry + material 已挂，调用方负责 delete）
				std::vector<matrix> transforms;   // 每个 prefab 的世界变换（层级展平，相对文件根）
			};

			// 加载 glTF；失败返回 false 且 error 有原因
			xhqm_io bool load(const std::string& path, load_result& out, std::string& error);
		}
	}
}

#endif
