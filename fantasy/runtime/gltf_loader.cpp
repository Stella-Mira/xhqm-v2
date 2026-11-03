#include "gltf_loader.h"
#include <fstream>
#include <cstring>
#include <cstdint>
#include <array>
#include <memory>

namespace xhqm
{
	namespace fantasy
	{
		namespace gltf
		{
			using json = nlohmann::json;

			static bool read_file(const std::string& path, std::vector<std::uint8_t>& out)
			{
				std::ifstream f(path, std::ios::binary);
				if (!f) return false;
				f.seekg(0, std::ios::end);
				std::streamoff n = f.tellg();
				f.seekg(0, std::ios::beg);
				out.resize((size_t)n);
				if (n > 0) f.read(reinterpret_cast<char*>(out.data()), n);
				return f.good() || f.eof();
			}

			static bool decode_base64(const std::string& in, std::vector<std::uint8_t>& out)
			{
				static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
				int rev[256];
				for (int i = 0; i < 256; ++i) rev[i] = -1;
				for (int i = 0; i < 64; ++i) rev[(unsigned char)tbl[i]] = i;
				out.clear();
				int val = 0, bits = -8;
				for (char c : in)
				{
					if (c == '=') break;
					int v = rev[(unsigned char)c];
					if (v < 0) continue;
					val = (val << 6) | v;
					bits += 6;
					if (bits >= 0)
					{
						out.push_back((std::uint8_t)((val >> bits) & 0xFF));
						bits -= 8;
					}
				}
				return true;
			}

			struct acc_view
			{
				const std::uint8_t* data = nullptr;
				size_t count = 0;
				size_t stride = 0;
				int comp_type = 0;
				int num_comp = 1;
			};

			static size_t comp_size(int comp_type)
			{
				switch (comp_type)
				{
				case 5120: case 5121: return 1;   // int8 / uint8
				case 5122: case 5123: return 2;   // int16 / uint16
				case 5125: case 5126: return 4;   // uint32 / float32
				default: return 0;
				}
			}

			static bool make_accessor(const json& root, const std::vector<std::vector<std::uint8_t>>& buffers,
				size_t acc_idx, acc_view& out)
			{
				if (acc_idx >= root["accessors"].size()) return false;
				const json& acc = root["accessors"][acc_idx];
				if (!acc.is_object()) return false;
				out.count = acc.value("count", (size_t)0);
				out.comp_type = acc.value("componentType", 0);
				std::string t = acc.value("type", "SCALAR");
				out.num_comp = 1;
				if (t == "VEC2") out.num_comp = 2;
				else if (t == "VEC3") out.num_comp = 3;
				else if (t == "VEC4") out.num_comp = 4;
				else if (t == "MAT4") out.num_comp = 16;
				size_t cs = comp_size(out.comp_type);
				if (cs == 0) return false;

				if (acc.contains("bufferView"))
				{
					size_t bv = acc["bufferView"].get<size_t>();
					if (bv >= root["bufferViews"].size()) return false;
					const json& view = root["bufferViews"][bv];
					size_t buf_idx = view.value("buffer", (size_t)0);
					size_t voff = view.value("byteOffset", (size_t)0);
					size_t stride = view.value("byteStride", (size_t)0);
					if (stride == 0) stride = cs * (size_t)out.num_comp;
					out.stride = stride;
					size_t aoff = acc.value("byteOffset", (size_t)0);
					if (buf_idx >= buffers.size()) return false;
					const std::vector<std::uint8_t>& buf = buffers[buf_idx];
					size_t off = voff + aoff;
					size_t needed = (out.count == 0) ? 0 : (out.count - 1) * out.stride + cs * (size_t)out.num_comp;
					if (off >= buf.size() || off + needed > buf.size()) return false;
					out.data = buf.data() + off;
				}
				else
				{
					out.stride = cs * (size_t)out.num_comp;
					out.data = nullptr;   // 稀疏/常量 accessor：按全零处理
				}
				return true;
			}

			static float read_float(const acc_view& v, size_t i, int c)
			{
				if (!v.data) return 0.0f;
				const std::uint8_t* p = v.data + i * v.stride + (size_t)c * comp_size(v.comp_type);
				switch (v.comp_type)
				{
				case 5126: { float f; std::memcpy(&f, p, 4); return f; }
				case 5121: return (float)(*p) / 255.0f;
				case 5123: { std::uint16_t u; std::memcpy(&u, p, 2); return (float)u / 65535.0f; }
				case 5120: { float v = (float)(*(const std::int8_t*)p) / 127.0f; return v < -1.0f ? -1.0f : v; }
				case 5122: { std::int16_t s; std::memcpy(&s, p, 2); float v = (float)s / 32767.0f; return v < -1.0f ? -1.0f : v; }
				default: return 0.0f;
				}
			}

			static std::uint32_t read_index(const acc_view& v, size_t i)
			{
				if (!v.data) return 0;
				const std::uint8_t* p = v.data + i * v.stride;
				switch (v.comp_type)
				{
				case 5121: return (std::uint32_t)(*p);
				case 5123: { std::uint16_t u; std::memcpy(&u, p, 2); return u; }
				case 5125: { std::uint32_t u; std::memcpy(&u, p, 4); return u; }
				default: return 0;
				}
			}

			static std::uint32_t float_to_rgba_byte(float v)
			{
				if (v < 0.0f) v = 0.0f;
				if (v > 1.0f) v = 1.0f;
				return (std::uint32_t)(v * 255.0f + 0.5f);
			}

			static material make_material(const json& root, size_t mat_idx)
			{
				auto m = std::make_shared<material_desc>();
				if (mat_idx >= root["materials"].size()) return m;
				const json& mat = root["materials"][mat_idx];
				if (mat.contains("pbrMetallicRoughness"))
				{
					const json& pbr = mat["pbrMetallicRoughness"];
					if (pbr.contains("baseColorFactor"))
					{
						const json& c = pbr["baseColorFactor"];
						m->base_color = xhqm_rgba(
							float_to_rgba_byte(c[0].get<float>()),
							float_to_rgba_byte(c[1].get<float>()),
							float_to_rgba_byte(c[2].get<float>()),
							float_to_rgba_byte(c[3].get<float>()));
						m->alpha = c[3].get<float>();
					}
					m->metallic = pbr.value("metallicFactor", 1.0f);
					m->roughness = pbr.value("roughnessFactor", 1.0f);
				}
				if (mat.contains("emissiveFactor"))
				{
					const json& e = mat["emissiveFactor"];
					m->emissive = xhqm_rgba(
						float_to_rgba_byte(e[0].get<float>()),
						float_to_rgba_byte(e[1].get<float>()),
						float_to_rgba_byte(e[2].get<float>()), 0);
				}
				if (mat.contains("doubleSided")) m->double_sided = mat["doubleSided"].get<bool>();
				return m;
			}

			static matrix node_local(const json& node)
			{
				if (node.contains("matrix"))
				{
					matrix M(4, 4);
					const json& a = node["matrix"];
					// glTF 列主序：data[col*4+row] = mat(row,col)
					for (int c = 0; c < 4; ++c)
						for (int r = 0; r < 4; ++r)
							M(r, c) = a[c * 4 + r].get<float>();
					return M;
				}
				matrix T = xhqm::translate(0.0f, 0.0f, 0.0f);
				matrix R(4, 4);
				matrix S = xhqm::scale(1.0f, 1.0f, 1.0f);
				if (node.contains("translation"))
				{
					const json& t = node["translation"];
					T = xhqm::translate(t[0].get<float>(), t[1].get<float>(), t[2].get<float>());
				}
				if (node.contains("scale"))
				{
					const json& s = node["scale"];
					S = xhqm::scale(s[0].get<float>(), s[1].get<float>(), s[2].get<float>());
				}
				if (node.contains("rotation"))
				{
					const json& q = node["rotation"];
					float x = q[0].get<float>(), y = q[1].get<float>(), z = q[2].get<float>(), w = q[3].get<float>();
					float xx = x * x, yy = y * y, zz = z * z;
					float xy = x * y, xz = x * z, yz = y * z, wx = w * x, wy = w * y, wz = w * z;
					R(0, 0) = 1 - 2 * (yy + zz); R(0, 1) = 2 * (xy - wz); R(0, 2) = 2 * (xz + wy);
					R(1, 0) = 2 * (xy + wz); R(1, 1) = 1 - 2 * (xx + zz); R(1, 2) = 2 * (yz - wx);
					R(2, 0) = 2 * (xz - wy); R(2, 1) = 2 * (yz + wx); R(2, 2) = 1 - 2 * (xx + yy);
				}
				return T * R * S;
			}

			static void walk_node(const json& root, const std::vector<std::vector<std::uint8_t>>& buffers,
				size_t node_idx, const matrix& parent_world,
				std::vector<prefab*>& prefabs, std::vector<matrix>& transforms)
			{
				if (node_idx >= root["nodes"].size()) return;
				const json& node = root["nodes"][node_idx];
				matrix world = parent_world * node_local(node);

				if (node.contains("mesh"))
				{
					size_t mesh_idx = node["mesh"].get<size_t>();
					if (mesh_idx < root["meshes"].size())
					{
						const json& mesh = root["meshes"][mesh_idx];
						for (const json& prim : mesh.value("primitives", json::array()))
						{
							const json& attrs = prim.value("attributes", json::object());
							size_t pos_acc = attrs.contains("POSITION") ? attrs["POSITION"].get<size_t>() : (size_t)-1;
							if (pos_acc == (size_t)-1) continue;
							size_t nrm_acc = attrs.contains("NORMAL") ? attrs["NORMAL"].get<size_t>() : (size_t)-1;
							size_t uv_acc = attrs.contains("TEXCOORD_0") ? attrs["TEXCOORD_0"].get<size_t>() : (size_t)-1;

							acc_view pos, nrm, uv;
							if (!make_accessor(root, buffers, pos_acc, pos)) continue;
							bool has_nrm = (nrm_acc != (size_t)-1) && make_accessor(root, buffers, nrm_acc, nrm);
							bool has_uv = (uv_acc != (size_t)-1) && make_accessor(root, buffers, uv_acc, uv);

							std::vector<std::uint32_t> indices;
							size_t tri_count = 0;
							if (prim.contains("indices"))
							{
								acc_view idx;
								if (make_accessor(root, buffers, prim["indices"].get<size_t>(), idx))
								{
									indices.resize(idx.count);
									for (size_t i = 0; i < idx.count; ++i) indices[i] = read_index(idx, i);
									tri_count = idx.count / 3;
								}
							}
							else
							{
								tri_count = pos.count / 3;
								indices.resize(pos.count);
								for (size_t i = 0; i < pos.count; ++i) indices[i] = (std::uint32_t)i;
							}
							if (tri_count == 0) continue;

							prefab* p = new prefab();
							p->size((xhqm::count)tri_count);
							std::vector<std::array<point, 3>> verts(tri_count);
							std::vector<std::array<value, 3>> norms(has_nrm ? tri_count * 3 : 0);
							std::vector<std::array<value, 2>> uvs(has_uv ? tri_count * 3 : 0);
							for (size_t t = 0; t < tri_count; ++t)
							{
								std::uint32_t i0 = indices[t * 3 + 0];
								std::uint32_t i1 = indices[t * 3 + 1];
								std::uint32_t i2 = indices[t * 3 + 2];
								verts[t] = {
									point(read_float(pos, i0, 0), read_float(pos, i0, 1), read_float(pos, i0, 2)),
									point(read_float(pos, i1, 0), read_float(pos, i1, 1), read_float(pos, i1, 2)),
									point(read_float(pos, i2, 0), read_float(pos, i2, 1), read_float(pos, i2, 2)) };
								if (has_nrm)
								{
									norms[t * 3 + 0] = { read_float(nrm, i0, 0), read_float(nrm, i0, 1), read_float(nrm, i0, 2) };
									norms[t * 3 + 1] = { read_float(nrm, i1, 0), read_float(nrm, i1, 1), read_float(nrm, i1, 2) };
									norms[t * 3 + 2] = { read_float(nrm, i2, 0), read_float(nrm, i2, 1), read_float(nrm, i2, 2) };
								}
								if (has_uv)
								{
									uvs[t * 3 + 0] = { read_float(uv, i0, 0), read_float(uv, i0, 1) };
									uvs[t * 3 + 1] = { read_float(uv, i1, 0), read_float(uv, i1, 1) };
									uvs[t * 3 + 2] = { read_float(uv, i2, 0), read_float(uv, i2, 1) };
								}
							}
							p->vertex(verts);
							if (has_nrm) p->normal(norms);
							if (has_uv) p->flipuv(uvs);
							if (prim.contains("material")) p->material(make_material(root, prim["material"].get<size_t>()));
							prefabs.push_back(p);
							transforms.push_back(world);
						}
					}
				}

				if (node.contains("children"))
				{
					for (const json& c : node["children"])
						walk_node(root, buffers, c.get<size_t>(), world, prefabs, transforms);
				}
			}

			bool load(const std::string& path, load_result& out, std::string& error)
			{
				std::vector<std::uint8_t> file;
				if (!read_file(path, file)) { error = "cannot read file: " + path; return false; }

				std::vector<std::uint8_t> json_bytes;
				std::vector<std::vector<std::uint8_t>> buffers;

				if (file.size() >= 12 && std::memcmp(file.data(), "glTF", 4) == 0)
				{
					// GLB：12 字节头 + JSON/BIN 块
					std::uint32_t length = 0;
					std::memcpy(&length, file.data() + 8, 4);
					size_t off = 12;
					while (off + 8 <= file.size() && off < (size_t)length)
					{
						std::uint32_t clen = 0, ctype = 0;
						std::memcpy(&clen, file.data() + off, 4);
						std::memcpy(&ctype, file.data() + off + 4, 4);
						off += 8;
						if (off + (size_t)clen > file.size()) break;
						if (ctype == 0x4E4F534Au) json_bytes.assign(file.data() + off, file.data() + off + clen);
						else if (ctype == 0x004E4942u) buffers.emplace_back(file.data() + off, file.data() + off + clen);
						off += (size_t)clen;
					}
				}
				else
				{
					json_bytes = file;
				}

				if (json_bytes.empty()) { error = "empty glTF JSON"; return false; }
				json root = json::parse(json_bytes.begin(), json_bytes.end(), nullptr, false);
				if (root.is_discarded()) { error = "invalid glTF JSON"; return false; }

				if (root.contains("buffers"))
				{
					for (const json& b : root["buffers"])
					{
						if (!b.contains("uri")) continue;   // GLB BIN 已在 buffers[0]
						std::string uri = b["uri"].get<std::string>();
						if (uri.rfind("data:", 0) == 0)
						{
							size_t comma = uri.find(',');
							if (comma == std::string::npos) continue;
							std::vector<std::uint8_t> decoded;
							if (decode_base64(uri.substr(comma + 1), decoded)) buffers.push_back(std::move(decoded));
						}
						else
						{
							std::string base = path.substr(0, path.find_last_of("/\\") + 1);
							std::vector<std::uint8_t> bin;
							if (read_file(base + uri, bin)) buffers.push_back(std::move(bin));
						}
					}
				}

				size_t scene_idx = root.value("scene", (size_t)0);
				if (!root.contains("scenes") || scene_idx >= root["scenes"].size()) { error = "no scene"; return false; }
				const json& scene = root["scenes"][scene_idx];
				matrix identity(4, 4);
				for (const json& n : scene.value("nodes", json::array()))
					walk_node(root, buffers, n.get<size_t>(), identity, out.prefabs, out.transforms);

				if (out.prefabs.empty()) { error = "no mesh loaded"; return false; }
				return true;
			}
		}
	}
}
