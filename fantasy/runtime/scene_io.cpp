#include "scene_io.h"
#include "colors.h"
#include <fstream>
#include <memory>
#include "../network/json.hpp"   // depend/nlohmann/json.hpp

namespace xhqm
{
	namespace fantasy
	{
		using json = nlohmann::json;

		// ---- 矩阵 <-> json（行主序 16 float）----
		static json matrix_to_json(const matrix& m)
		{
			json a = json::array();
			for (int r = 0; r < 4; ++r)
				for (int c = 0; c < 4; ++c)
					a.push_back((double)m(r, c));
			return a;
		}
		static matrix matrix_from_json(const json& a)
		{
			matrix m(4, 4);
			int k = 0;
			for (int r = 0; r < 4; ++r)
				for (int c = 0; c < 4; ++c)
					m(r, c) = (value)a[k++].get<double>();
			return m;
		}

		// ---- 材质 <-> json ----
		static json material_to_json(const material& mat)
		{
			json j;
			if (mat)
			{
				j["base_color"] = (uint)mat->base_color;
				j["metallic"] = (double)mat->metallic;
				j["roughness"] = (double)mat->roughness;
				j["emissive"] = (uint)mat->emissive;
				j["ao"] = (double)mat->ao;
				j["alpha"] = (double)mat->alpha;
				j["double_sided"] = mat->double_sided;
			}
			return j;
		}
		static material material_from_json(const json& j)
		{
			auto m = std::make_shared<material_desc>();
			if (!j.is_object()) return m;
			if (j.contains("base_color")) m->base_color = (rgba)j["base_color"].get<uint>();
			if (j.contains("metallic")) m->metallic = (value)j["metallic"].get<double>();
			if (j.contains("roughness")) m->roughness = (value)j["roughness"].get<double>();
			if (j.contains("emissive")) m->emissive = (rgba)j["emissive"].get<uint>();
			if (j.contains("ao")) m->ao = (value)j["ao"].get<double>();
			if (j.contains("alpha")) m->alpha = (value)j["alpha"].get<double>();
			if (j.contains("double_sided")) m->double_sided = j["double_sided"].get<bool>();
			return m;
		}

		bool scene_save(const std::string& path,
			const std::vector<prefab*>& meshes, const std::vector<matrix>& transforms, std::string& error)
		{
			if (meshes.size() != transforms.size()) { error = "mesh/transform count mismatch"; return false; }

			json root;
			json arr = json::array();
			for (size_t i = 0; i < meshes.size(); ++i)
			{
				prefab* p = meshes[i];
				if (!p) continue;
				json item;
				item["transform"] = matrix_to_json(transforms[i]);
				item["material"] = material_to_json(p->material());

				xhqm::count n = p->size();
				item["triangles"] = (uint)n;
				json pos = json::array();
				for (const auto& tri : p->vertex())
					for (int c = 0; c < 3; ++c) { pos.push_back((double)tri[c].x()); pos.push_back((double)tri[c].y()); pos.push_back((double)tri[c].z()); }
				item["positions"] = pos;

				if (p->normal().size() == n * 3)
				{
					json norm = json::array();
					for (const auto& v : p->normal()) { norm.push_back((double)v[0]); norm.push_back((double)v[1]); norm.push_back((double)v[2]); }
					item["normals"] = norm;
				}
				if (p->flipuv().size() == n * 3)
				{
					json uv = json::array();
					for (const auto& v : p->flipuv()) { uv.push_back((double)v[0]); uv.push_back((double)v[1]); }
					item["uvs"] = uv;
				}
				if (p->colour().size() == n * 3)
				{
					json col = json::array();
					for (const auto& v : p->colour()) { col.push_back((double)v[0]); col.push_back((double)v[1]); col.push_back((double)v[2]); col.push_back((double)v[3]); }
					item["colours"] = col;
				}
				if (p->tangent().size() == n * 3)
				{
					json tan = json::array();
					for (const auto& v : p->tangent()) { tan.push_back((double)v[0]); tan.push_back((double)v[1]); tan.push_back((double)v[2]); }
					item["tangents"] = tan;
				}
				arr.push_back(std::move(item));
			}
			root["meshes"] = std::move(arr);

			std::ofstream f(path, std::ios::binary);
			if (!f) { error = "cannot write file: " + path; return false; }
			f << root.dump(2);
			return true;
		}

		bool scene_load(const std::string& path,
			std::vector<prefab*>& meshes, std::vector<matrix>& transforms, std::string& error)
		{
			std::ifstream f(path, std::ios::binary);
			if (!f) { error = "cannot read file: " + path; return false; }
			json root = json::parse(f, nullptr, false);
			if (root.is_discarded() || !root.contains("meshes")) { error = "invalid scene JSON"; return false; }

			for (const json& item : root["meshes"])
			{
				uint n = item.value("triangles", (uint)0);
				if (n == 0 || !item.contains("positions")) continue;
				// 校验数组尺寸，避免畸形 JSON 触发 nlohmann 越界异常
				if (item["positions"].size() != (size_t)n * 9) continue;
				if (item.contains("normals") && item["normals"].size() != (size_t)n * 9) continue;
				if (item.contains("uvs") && item["uvs"].size() != (size_t)n * 6) continue;
				if (item.contains("colours") && item["colours"].size() != (size_t)n * 12) continue;
				if (item.contains("tangents") && item["tangents"].size() != (size_t)n * 9) continue;
				prefab* p = new prefab();
				p->size((xhqm::count)n);

				std::vector<std::array<point, 3>> verts(n);
				const json& pos = item["positions"];
				for (uint t = 0; t < n; ++t)
				{
					verts[t] = {
						point((value)pos[t * 9 + 0].get<double>(), (value)pos[t * 9 + 1].get<double>(), (value)pos[t * 9 + 2].get<double>()),
						point((value)pos[t * 9 + 3].get<double>(), (value)pos[t * 9 + 4].get<double>(), (value)pos[t * 9 + 5].get<double>()),
						point((value)pos[t * 9 + 6].get<double>(), (value)pos[t * 9 + 7].get<double>(), (value)pos[t * 9 + 8].get<double>()) };
				}
				p->vertex(verts);

				if (item.contains("normals"))
				{
					const json& norm = item["normals"];
					std::vector<std::array<value, 3>> arr(n * 3);
					for (size_t i = 0; i < n * 3; ++i)
						arr[i] = { (value)norm[i * 3 + 0].get<double>(), (value)norm[i * 3 + 1].get<double>(), (value)norm[i * 3 + 2].get<double>() };
					p->normal(arr);
				}
				if (item.contains("uvs"))
				{
					const json& uv = item["uvs"];
					std::vector<std::array<value, 2>> arr(n * 3);
					for (size_t i = 0; i < n * 3; ++i)
						arr[i] = { (value)uv[i * 2 + 0].get<double>(), (value)uv[i * 2 + 1].get<double>() };
					p->flipuv(arr);
				}
				if (item.contains("colours"))
				{
					const json& col = item["colours"];
					std::vector<std::array<value, 4>> arr(n * 3);
					for (size_t i = 0; i < n * 3; ++i)
						arr[i] = { (value)col[i * 4 + 0].get<double>(), (value)col[i * 4 + 1].get<double>(), (value)col[i * 4 + 2].get<double>(), (value)col[i * 4 + 3].get<double>() };
					p->colour(arr);
				}
				if (item.contains("tangents"))
				{
					const json& tan = item["tangents"];
					std::vector<std::array<value, 3>> arr(n * 3);
					for (size_t i = 0; i < n * 3; ++i)
						arr[i] = { (value)tan[i * 3 + 0].get<double>(), (value)tan[i * 3 + 1].get<double>(), (value)tan[i * 3 + 2].get<double>() };
					p->tangent(arr);
				}
				if (item.contains("material")) p->material(material_from_json(item["material"]));

				meshes.push_back(p);
				transforms.push_back(item.contains("transform") ? matrix_from_json(item["transform"]) : matrix(4, 4));
			}

			if (meshes.empty()) { error = "no mesh loaded"; return false; }
			return true;
		}
	}
}
