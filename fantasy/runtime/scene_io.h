#pragma once
#ifndef XHQM_FANTASY_SCENE_IO
#define XHQM_FANTASY_SCENE_IO

// 场景序列化（M10）：保存/加载 prefab 几何 + 材质 + 变换 到 JSON（nlohmann）。
// 与 gltf 原生加载互补：这是引擎运行时场景的原生格式（含 skin/blend/instances 之外的全部静态几何与材质）。
#include "fantasy.h"
#include "drawer.h"
#include <vector>
#include <string>

namespace xhqm
{
	namespace fantasy
	{
		// 保存一组 (prefab 几何 + 材质 + 世界变换) 到 JSON；失败返回 false 且 error 有原因
		xhqm_io bool scene_save(const std::string& path,
			const std::vector<prefab*>& meshes, const std::vector<matrix>& transforms, std::string& error);

		// 加载 JSON，重建 prefab（调用方负责 delete）与变换；失败返回 false 且 error 有原因
		xhqm_io bool scene_load(const std::string& path,
			std::vector<prefab*>& meshes, std::vector<matrix>& transforms, std::string& error);
	}
}

#endif
