#pragma once
#ifndef XHQM_FANTASY_ASSET_MANAGER
#define XHQM_FANTASY_ASSET_MANAGER

// 资源注册表（M10 资源句柄系统）：按名字登记/查找可共享资源。
// 资源本身是 shared_ptr（material）/ resource（texture，内嵌 shared_ptr）引用计数；
// 这里提供「名字 → 资源」的登记与复用，为 glTF 加载与场景序列化提供查找入口。
#include "fantasy.h"
#include "drawer.h"
#include <unordered_map>

namespace xhqm
{
	namespace fantasy
	{
		class asset_manager
		{
			std::unordered_map<std::string, material> m_materials;
			std::unordered_map<std::string, texture> m_textures;
		public:
			asset_manager() = default;

			void set_material(const std::string& name, const material& m) { m_materials[name] = m; }
			material find_material(const std::string& name) const
			{
				auto it = m_materials.find(name);
				return (it != m_materials.end()) ? it->second : material();
			}
			bool has_material(const std::string& name) const { return m_materials.count(name) != 0; }

			void set_texture(const std::string& name, const texture& t) { m_textures[name] = t; }
			texture find_texture(const std::string& name) const
			{
				auto it = m_textures.find(name);
				return (it != m_textures.end()) ? it->second : texture();
			}
			bool has_texture(const std::string& name) const { return m_textures.count(name) != 0; }

			void clear() { m_materials.clear(); m_textures.clear(); }
			size_t material_count() const { return m_materials.size(); }
			size_t texture_count() const { return m_textures.size(); }
		};
	}
}

#endif
