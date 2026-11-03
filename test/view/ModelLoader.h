#pragma once
// assimp 模型 -> prefab（纹理 + LBS 蒙皮）+ 程序化双骨棒
// 依赖：xhqm/fantasy + assimp + QtGui(QImage)，不依赖 QOpenGLWidget
#include <xhqm/fantasy>
#include <xhqm/algebra>
#include <assimp/scene.h>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <QImage>
#include <map>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace demo
{
	using namespace xhqm;

	// QImage -> 二维 rgba（与 RenderWidget::read_image 同语义）
	inline std::vector<std::vector<xhqm::rgba>> read_image(QImage& img)
	{
		std::vector<std::vector<xhqm::rgba>> out;
		if (img.isNull()) return out;
		img = img.convertToFormat(QImage::Format_RGBA8888);
		int w = img.width(), h = img.height();
		out.resize(h, std::vector<xhqm::rgba>(w, 0));
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x)
				out[y][x] = *reinterpret_cast<uint*>(img.scanLine(y) + x * 4);
		return out;
	}

	inline std::vector<std::vector<xhqm::rgba>> read_image(const std::string& path)
	{
		QImage img(path.c_str());
		return read_image(img);
	}

	inline std::vector<std::vector<xhqm::rgba>> read_image(const uint8_t* data, size_t size)
	{
		QImage img = QImage::fromData(data, (int)size);
		return read_image(img);
	}

	// PMX 2.0 轻量贴图解析：assimp 的 PMX 导入器不把贴图挂到 aiMaterial，
	// 这里直接读 pmx 的贴图段与材质段，返回「材质序 → 贴图相对路径」（空串 = 无贴图）
	inline std::vector<std::string> pmx_material_textures(const std::string& pmx_path)
	{
		std::vector<std::string> out;
		
		if(pmx_path.find("丝袜") != std::string::npos)
		{
			return out;
		}
		
		
		FILE* f = nullptr;
#ifdef _WIN32
		if (fopen_s(&f, pmx_path.c_str(), "rb") != 0 || !f) return out;
#else
		f = std::fopen(pmx_path.c_str(), "rb");
		if (!f) return out;
#endif

		auto rd = [&](void* p, size_t n) { return fread(p, 1, n, f) == n; };
		auto r_u8 = [&]() { uint8_t v = 0; rd(&v, 1); return v; };
		auto r_u32 = [&]() { uint32_t v = 0; rd(&v, 4); return v; };
		auto r_f32 = [&]() { float v = 0; rd(&v, 4); return v; };
		auto r_varint = [&]() {
			uint32_t v = 0;
			for (int i = 0; i < 4; ++i) { uint8_t b = r_u8(); v |= (uint32_t)(b & 0x7F) << (7 * i); if (!(b & 0x80)) break; }
			return v;
		};
		bool utf8 = true;   // PMX 文本编码由全局设置首项决定（"0"=UTF16LE "1"=UTF8）
		auto r_text = [&]() {
			uint32_t len = r_u32();
			std::string s;
			if (len == 0) return s;
			if (utf8)
			{
				s.resize(len);
				rd(&s[0], len);
			}
			else
			{
				std::vector<uint16_t> u16(len / 2);
				rd(u16.data(), len);
				for (uint16_t c : u16)
				{
					if (c < 0x80) s.push_back((char)c);
					else if (c < 0x800) { s.push_back((char)(0xC0 | (c >> 6))); s.push_back((char)(0x80 | (c & 0x3F))); }
					else { s.push_back((char)(0xE0 | (c >> 12))); s.push_back((char)(0x80 | ((c >> 6) & 0x3F))); s.push_back((char)(0x80 | (c & 0x3F))); }
				}
			}
			return s;
		};

		char magic[4] = {};
		if (!rd(magic, 4) || memcmp(magic, "PMX ", 4) != 0) { fclose(f); return out; }
		(void)r_f32();   // version
		uint8_t gcount = r_u8();
		for (uint8_t i = 0; i < gcount; ++i)
		{
			// 全局设置文本：首项按 UTF-8 读，内容 "0"=UTF16LE，"1"=UTF8
			uint32_t len = r_u32();
			std::vector<char> buf(len ? len : 1, 0);
			if (len) rd(buf.data(), len);
			if (i == 0) utf8 = !(len == 1 && buf[0] == '0');
		}
		// 顶点段（跳过）
		uint32_t vcount = r_u32();
		for (uint32_t i = 0; i < vcount; ++i)
		{
			uint8_t s32[32]; rd(s32, 32);            // pos12 + normal12 + uv8
			uint8_t extra = r_u8();
			for (uint8_t e = 0; e < extra; ++e) { uint8_t s16[16]; rd(s16, 16); }
			uint8_t wt = r_u8();
			if (wt == 0) { uint8_t s4[4]; rd(s4, 4); }
			else if (wt == 1) { uint8_t s8[8]; rd(s8, 8); }
			else if (wt == 2) { uint8_t s16[16]; rd(s16, 16); }
			else if (wt == 3) { uint8_t s32b[32]; rd(s32b, 32); }   // SDEF: 8 + 24
			else { uint8_t s16[16]; rd(s16, 16); }                  // QDEF
		}
		// 面段（跳过）
		uint32_t fcount = r_u32();
		std::vector<uint32_t> fidx((size_t)fcount * 3);
		if (fcount) rd(fidx.data(), fidx.size() * 4);
		// 贴图段
		uint32_t tcount = r_u32();
		std::vector<std::string> texs;
		texs.reserve(tcount);
		for (uint32_t i = 0; i < tcount; ++i) { texs.push_back(r_text()); (void)r_u8(); }
		// 材质段
		uint32_t mcount = r_u32();
		for (uint32_t i = 0; i < mcount; ++i)
		{
			(void)r_text(); (void)r_text();            // name jp / en
			uint8_t s44[44]; rd(s44, 44);              // diffuse16 + specular12 + power4 + ambient12
			uint8_t flag = r_u8();
			uint8_t s20[20]; rd(s20, 20);              // edgeColor16 + edgeSize4
			uint32_t tex_idx = (flag & 1) ? r_varint() : 0;   // PMX 索引 1-based，0 = 无
			(void)r_varint();                          // sphere 索引
			(void)r_u8();                              // sphere 模式
			uint8_t toon_shared = r_u8();
			if (toon_shared == 0) (void)r_text(); else (void)r_u8();
			(void)r_text();                            // memo
			(void)r_varint();                          // 面数
			out.push_back((tex_idx > 0 && tex_idx - 1 < texs.size()) ? texs[tex_idx - 1] : std::string());
		}
		fclose(f);
		return out;
	}

	// assimp aiMatrix4x4 与 xhqm matx 同为「列向量 + 平移在第 3 列」约定，直接拷贝
	inline xhqm::fantasy::matrix to_xhqm(const aiMatrix4x4& m)
	{
		xhqm::fantasy::matrix r(4, 4);
		for (int i = 0; i < 4; ++i)
			for (int j = 0; j < 4; ++j)
				r(i, j) = static_cast<xhqm::fantasy::value>(m[i][j]);
		return r;
	}

	// 递归收集节点全局变换（列向量约定：world = parent * local）
	inline void collect_world(const aiNode* node,
		const xhqm::fantasy::matrix& parent,
		std::map<std::string, aiNode*>& node_map,
		std::map<std::string, xhqm::fantasy::matrix>& world_map)
	{
		if (!node) return;
		xhqm::fantasy::matrix local = to_xhqm(node->mTransformation);
		xhqm::fantasy::matrix world = parent * local;
		node_map[node->mName.C_Str()] = const_cast<aiNode*>(node);
		world_map.emplace(node->mName.C_Str(), world);
		for (unsigned int i = 0; i < node->mNumChildren; ++i)
			collect_world(node->mChildren[i], world, node_map, world_map);
	}

	// 加载单个网格到 prefab（含纹理与骨骼蒙皮）
	inline xhqm::fantasy::prefab* load_mesh_prefab(const std::string& dir, const aiScene* scene, aiMesh* mesh,
		std::map<std::string, aiNode*>& node_map,
		std::map<std::string, xhqm::fantasy::matrix>& world_map,
		int& out_bone_count,
		const std::vector<std::string>* pmx_texs = nullptr)
	{
		using namespace xhqm::fantasy;
		if (!mesh || mesh->mNumFaces == 0) return nullptr;

		prefab* p = new prefab();
		p->size(mesh->mNumFaces);

		// 1. 顶点
		std::vector<std::array<point, 3>> tris(mesh->mNumFaces);
		for (unsigned int i = 0; i < mesh->mNumFaces; ++i)
			for (int j = 0; j < 3; ++j)
			{
				aiVector3D v = mesh->mVertices[mesh->mFaces[i].mIndices[j]];
				tris[i][j] = point(v.x, v.y, v.z);
			}
		p->vertex(tris);

		// 2. 法线
		if (mesh->HasNormals())
		{
			std::vector<std::array<value, 3>> norms(mesh->mNumFaces * 3);
			for (unsigned int i = 0; i < mesh->mNumFaces; ++i)
				for (int j = 0; j < 3; ++j)
				{
					aiVector3D n = mesh->mNormals[mesh->mFaces[i].mIndices[j]];
					norms[i * 3 + j] = { n.x, n.y, n.z };
				}
			p->normal(norms);
		}

		// 2b. 切线（法线贴图 TBN）
		if (mesh->HasTangentsAndBitangents())
		{
			std::vector<std::array<value, 3>> tangs(mesh->mNumFaces * 3);
			for (unsigned int i = 0; i < mesh->mNumFaces; ++i)
				for (int j = 0; j < 3; ++j)
				{
					aiVector3D t = mesh->mTangents[mesh->mFaces[i].mIndices[j]];
					tangs[i * 3 + j] = { t.x, t.y, t.z };
				}
			p->tangent(tangs);
		}

		// 3. UV
		if (mesh->HasTextureCoords(0))
		{
			std::vector<std::array<value, 2>> uvs(mesh->mNumFaces * 3);
			for (unsigned int i = 0; i < mesh->mNumFaces; ++i)
				for (int j = 0; j < 3; ++j)
				{
					aiVector3D uv = mesh->mTextureCoords[0][mesh->mFaces[i].mIndices[j]];
					uvs[i * 3 + j] = { uv.x, uv.y };
				}
			p->flipuv(uvs);
		}

		// 4. 顶点色
		if (mesh->HasVertexColors(0))
		{
			std::vector<std::array<value, 4>> cols(mesh->mNumFaces * 3);
			for (unsigned int i = 0; i < mesh->mNumFaces; ++i)
				for (int j = 0; j < 3; ++j)
				{
					aiColor4D c = mesh->mColors[0][mesh->mFaces[i].mIndices[j]];
					cols[i * 3 + j] = { c.r, c.g, c.b, c.a };
				}
			p->colour(cols);
		}

		// 5. 纹理
		bool has_texture = false;
		// pmx 直载：assimp 的 PMX 导入器不填 aiMaterial 贴图，用 pmx 解析出的材质贴图表
		if (pmx_texs && mesh->mMaterialIndex < pmx_texs->size() && !(*pmx_texs)[mesh->mMaterialIndex].empty())
		{
			has_texture = true;
			p->texture_layer_1(texture(read_image(dir + (*pmx_texs)[mesh->mMaterialIndex])));
		}
		else if (mesh->HasTextureCoords(0) && mesh->mMaterialIndex < scene->mNumMaterials)
		{
			aiMaterial* mat = scene->mMaterials[mesh->mMaterialIndex];
			aiString tex_path;
			if (mat->GetTexture(aiTextureType_DIFFUSE, 0, &tex_path) == AI_SUCCESS)
			{
				has_texture = true;
				std::string key = tex_path.C_Str();
				if (!key.empty() && key[0] == '*')
				{
					int idx = std::stoi(key.substr(1));
					if (idx >= 0 && idx < (int)scene->mNumTextures)
					{
						aiTexture* t = scene->mTextures[idx];
						if (t->mHeight == 0)
							p->texture_layer_1(texture(read_image(reinterpret_cast<const uint8_t*>(t->pcData), t->mWidth)));
						else
						{
							QImage img(reinterpret_cast<const uchar*>(t->pcData), t->mWidth, t->mHeight, QImage::Format_ARGB32);
							p->texture_layer_1(texture(read_image(img)));
						}
					}
				}
				else
				{
					p->texture_layer_1(texture(read_image(dir + key)));
				}
			}
		}
		// 无纹理：用材质基础色（glTF baseColorFactor / 漫反射色）；不依赖 UV，纯色模型（gizmo 等）也能上色
		if (!has_texture && mesh->mMaterialIndex < scene->mNumMaterials)
		{
			aiMaterial* mat = scene->mMaterials[mesh->mMaterialIndex];
			aiColor4D base;
			if (mat->Get(AI_MATKEY_BASE_COLOR, base) == AI_SUCCESS ||
				mat->Get(AI_MATKEY_COLOR_DIFFUSE, base) == AI_SUCCESS)
			{
				p->color_face(xhqm_rgba(
					(xhqm::fantasy::uint)(base.r * 255.0f),
					(xhqm::fantasy::uint)(base.g * 255.0f),
					(xhqm::fantasy::uint)(base.b * 255.0f), 255));
			}
		}

		// 6. 骨骼蒙皮（LBS）
		out_bone_count = 0;
		if (mesh->HasBones())
		{
			out_bone_count = (int)mesh->mNumBones;
			std::map<int, std::vector<std::pair<int, float>>> vtx_weights;
			for (unsigned int b = 0; b < mesh->mNumBones; ++b)
			{
				aiBone* bone = mesh->mBones[b];
				for (unsigned int w = 0; w < bone->mNumWeights; ++w)
					vtx_weights[bone->mWeights[w].mVertexId].push_back({ (int)b, bone->mWeights[w].mWeight });
			}

			std::vector<matrix> palette;
			palette.reserve(mesh->mNumBones);
			for (unsigned int b = 0; b < mesh->mNumBones; ++b)
			{
				aiBone* bone = mesh->mBones[b];
				std::string name = bone->mName.C_Str();
				matrix offset = to_xhqm(bone->mOffsetMatrix);
				matrix node_world(4, 4);
				auto it = world_map.find(name);
				if (it != world_map.end()) node_world = it->second;
				palette.push_back(node_world * offset);
			}
			p->open_skin(true);
			p->bone_palette(palette);

			std::vector<std::array<value, 4>> skin_idx(mesh->mNumFaces * 3);
			std::vector<std::array<value, 4>> skin_wgt(mesh->mNumFaces * 3);
			for (unsigned int i = 0; i < mesh->mNumFaces; ++i)
				for (int j = 0; j < 3; ++j)
				{
					int vid = (int)mesh->mFaces[i].mIndices[j];
					std::array<value, 4> idx = { 0,0,0,0 };
					std::array<value, 4> wgt = { 0,0,0,0 };
					auto it = vtx_weights.find(vid);
					if (it != vtx_weights.end())
					{
						auto& list = it->second;
						std::sort(list.begin(), list.end(),
							[](const std::pair<int, float>& a, const std::pair<int, float>& b) { return a.second > b.second; });
						int n = (int)std::min<size_t>(list.size(), 4);
						float sum = 0.0f;
						for (int k = 0; k < n; ++k) { idx[k] = (value)list[k].first; wgt[k] = list[k].second; sum += wgt[k]; }
						if (sum > 0.0f) for (int k = 0; k < n; ++k) wgt[k] /= sum;
					}
					else { idx = { 0,0,0,0 }; wgt = { 1,0,0,0 }; }
					skin_idx[i * 3 + j] = idx;
					skin_wgt[i * 3 + j] = wgt;
				}
			p->skin_index(skin_idx);
			p->skin_weight(skin_wgt);
		}

		p->poit_size(3);
		p->line_size(1);
		p->open_poit(0);
		p->open_line(0);
		p->open_face(1);
		p->open_light(0);
		return p;
	}

	// 加载模型文件，返回一个 models 树，骨骼信息写入 out_bone_count
	inline xhqm::fantasy::models* load_model(const std::string& path, const std::string& name, int& out_bone_count)
	{
		using namespace xhqm::fantasy;
		Assimp::Importer importer;
		const aiScene* scene = importer.ReadFile(path + name,
			aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace);
		if (!scene || !scene->mRootNode) return nullptr;

		std::map<std::string, aiNode*> node_map;
		std::map<std::string, matrix> world_map;
		collect_world(scene->mRootNode, matrix(4, 4), node_map, world_map);

		// pmx：assimp 不挂贴图，额外解析材质贴图表
		std::vector<std::string> pmx_texs;
		const std::vector<std::string>* pmx_texs_ptr = nullptr;
		if (name.size() > 4 && name.compare(name.size() - 4, 4, ".pmx") == 0)
		{
			pmx_texs = pmx_material_textures(path + name);
			pmx_texs_ptr = &pmx_texs;
		}

		models* root = new models();
		out_bone_count = 0;
		for (unsigned int m = 0; m < scene->mNumMeshes; ++m)
		{
			int bone_count = 0;
			prefab* p = load_mesh_prefab(path, scene, scene->mMeshes[m], node_map, world_map, bone_count, pmx_texs_ptr);
			if (p) { root->insert_drawer(p); out_bone_count += bone_count; }
		}
		return root;
	}

	// 程序化双骨棒：沿 Z 轴 0..H，关节 z=H/2，骨 0 下、骨 1 上
	inline xhqm::fantasy::prefab* make_skinned_bar()
	{
		using namespace xhqm::fantasy;
		const float H = 2.0f, W = 0.35f;
		const int S = 10;

		std::vector<std::array<point, 3>> tris;
		std::vector<std::array<value, 3>> norms;
		std::vector<std::array<value, 4>> skin_idx;
		std::vector<std::array<value, 4>> skin_wgt;

		auto skin_of = [&](float z) -> std::array<value, 4> {
			float t = z / H;
			return { 1.0f - t, t, 0.0f, 0.0f };
		};

		auto push_quad = [&](const point& p0, const point& p1, const point& p2, const point& p3, const xhqm::vect<value>& n)
		{
			std::array<point, 3> t0 = { p0, p1, p2 };
			std::array<point, 3> t1 = { p0, p2, p3 };
			std::array<point, 3>* ts[2] = { &t0, &t1 };
			for (int k = 0; k < 2; ++k)
			{
				tris.push_back(*ts[k]);
				for (int c = 0; c < 3; ++c)
				{
					norms.push_back({ n.x(), n.y(), n.z() });
					skin_idx.push_back({ 0, 1, 0, 0 });
					skin_wgt.push_back(skin_of((*ts[k])[c].z()));
				}
			}
		};

		const float hw = W * 0.5f;
		for (int s = 0; s < S; ++s)
		{
			float z0 = H * s / S, z1 = H * (s + 1) / S;
			point a(hw, -hw, z0), b(hw, hw, z0), c(-hw, hw, z0), d(-hw, -hw, z0);
			point A(hw, -hw, z1), B(hw, hw, z1), C(-hw, hw, z1), D(-hw, -hw, z1);
			push_quad(a, b, B, A, xhqm::vect<value>(1, 0, 0));
			push_quad(b, c, C, B, xhqm::vect<value>(0, 1, 0));
			push_quad(c, d, D, C, xhqm::vect<value>(-1, 0, 0));
			push_quad(d, a, A, D, xhqm::vect<value>(0, -1, 0));
		}
		{
			point a(hw, -hw, 0), b(hw, hw, 0), c(-hw, hw, 0), d(-hw, -hw, 0);
			push_quad(d, c, b, a, xhqm::vect<value>(0, 0, -1));
		}
		{
			point a(hw, -hw, H), b(hw, hw, H), c(-hw, hw, H), d(-hw, -hw, H);
			push_quad(a, b, c, d, xhqm::vect<value>(0, 0, 1));
		}

		prefab* p = new prefab();
		p->size(tris.size());
		p->vertex(tris);
		p->normal(norms);
		p->skin_index(skin_idx);
		p->skin_weight(skin_wgt);
		p->open_skin(true);
		p->open_poit(0);
		p->open_line(1);
		p->open_face(1);
		p->open_light(0);
		p->color_face(xhqm_rgba(255, 160, 40, 255));
		p->color_line(xhqm_rgba(40, 40, 40, 255));
		return p;
	}

	// 双骨棒弯曲调色板：骨 0 不动，骨 1 绕关节 (0,0,H/2) 绕 X 轴旋转 angle（度）
	inline std::vector<xhqm::fantasy::matrix> bar_palette(float angle_deg)
	{
		using namespace xhqm::fantasy;
		const value H = 2.0f, jz = H * 0.5f;
		value a = xhqm::radians(angle_deg);
		matrix hinge = xhqm::translate(value(0), value(0), value(jz))
			* xhqm::rotate_x(a)
			* xhqm::translate(value(0), value(0), value(-jz));
		std::vector<matrix> palette;
		palette.reserve(2);
		palette.push_back(matrix(4, 4));
		palette.push_back(hinge);
		return palette;
	}

	// ==================== 骨骼动画（assimp aiAnimation） ====================

	// 单骨骼 node 的动画通道（位置/旋转/缩放关键帧）
	struct bone_anim
	{
		std::vector<float> p_times, r_times, s_times;
		std::vector<aiVector3D> p_keys;
		std::vector<aiQuaternion> r_keys;
		std::vector<aiVector3D> s_keys;
	};

	// 动画 clip：全局时长 + 各骨骼 node 的通道 + 骨骼映射（palette 顺序）
	// 自持骨骼层级副本（不引用 assimp scene 的 aiNode，避免悬空）
	struct anim_clip
	{
		struct node_data
		{
			std::string name;
			xhqm::fantasy::matrix bind_local = xhqm::fantasy::matrix(4, 4);  // 绑定局部变换
			int parent = -1;
		};
		std::string name;                          // 动画片段名
		float duration = 0.0f;
		std::vector<node_data> nodes;              // 深度优先（父在子前）
		std::vector<std::string> bone_names;       // 骨骼 node 名（与 prefab palette 顺序一致）
		std::vector<xhqm::fantasy::matrix> offsets;   // 骨骼 offset（蒙皮逆绑定）
		std::map<std::string, bone_anim> channels;    // node 名 -> 动画通道
		bool valid() const { return !nodes.empty() && !bone_names.empty(); }
	};

	// 线性插值（位置/缩放）
	inline aiVector3D anim_lerp(const aiVector3D& a, const aiVector3D& b, float t) { return a + (b - a) * t; }
	// 四元数球面插值
	inline aiQuaternion anim_slerp(const aiQuaternion& a, const aiQuaternion& b, float t)
	{
		aiQuaternion r; aiQuaternion::Interpolate(r, a, b, t); return r;
	}

	inline aiVector3D sample_pos(const bone_anim& c, float time)
	{
		if (c.p_keys.empty()) return aiVector3D(0, 0, 0);
		if (c.p_keys.size() == 1) return c.p_keys[0];
		for (size_t i = 0; i + 1 < c.p_times.size(); ++i)
			if (time <= c.p_times[i + 1])
			{
				float t0 = c.p_times[i], t1 = c.p_times[i + 1];
				float k = (t1 > t0) ? (time - t0) / (t1 - t0) : 0.0f;
				return anim_lerp(c.p_keys[i], c.p_keys[i + 1], k);
			}
		return c.p_keys.back();
	}
	inline aiQuaternion sample_rot(const bone_anim& c, float time)
	{
		if (c.r_keys.empty()) return aiQuaternion();
		if (c.r_keys.size() == 1) return c.r_keys[0];
		for (size_t i = 0; i + 1 < c.r_times.size(); ++i)
			if (time <= c.r_times[i + 1])
			{
				float t0 = c.r_times[i], t1 = c.r_times[i + 1];
				float k = (t1 > t0) ? (time - t0) / (t1 - t0) : 0.0f;
				return anim_slerp(c.r_keys[i], c.r_keys[i + 1], k);
			}
		return c.r_keys.back();
	}
	inline aiVector3D sample_scl(const bone_anim& c, float time)
	{
		if (c.s_keys.empty()) return aiVector3D(1, 1, 1);
		if (c.s_keys.size() == 1) return c.s_keys[0];
		for (size_t i = 0; i + 1 < c.s_times.size(); ++i)
			if (time <= c.s_times[i + 1])
			{
				float t0 = c.s_times[i], t1 = c.s_times[i + 1];
				float k = (t1 > t0) ? (time - t0) / (t1 - t0) : 0.0f;
				return anim_lerp(c.s_keys[i], c.s_keys[i + 1], k);
			}
		return c.s_keys.back();
	}

	// 四元数 -> 4x4 旋转矩阵
	inline xhqm::fantasy::matrix quat_matrix(const aiQuaternion& q)
	{
		aiMatrix3x3 m3 = q.GetMatrix();
		aiMatrix4x4 m4;
		m4 = aiMatrix4x4(m3);
		return to_xhqm(m4);
	}

	// 采样所有 node 世界变换：按动画通道更新局部，累乘父变换
	inline void sample_worlds(const anim_clip& anim, float time, std::map<std::string, xhqm::fantasy::matrix>& bone_world)
	{
		std::vector<xhqm::fantasy::matrix> worlds;
		worlds.resize(anim.nodes.size(), xhqm::fantasy::matrix(4, 4));
		for (size_t i = 0; i < anim.nodes.size(); ++i)
		{
			const auto& nd = anim.nodes[i];
			xhqm::fantasy::matrix local = nd.bind_local;
			auto it = anim.channels.find(nd.name);
			if (it != anim.channels.end())
			{
				const bone_anim& c = it->second;
				aiVector3D p = sample_pos(c, time);
				aiQuaternion r = sample_rot(c, time);
				aiVector3D s = sample_scl(c, time);
				xhqm::fantasy::matrix T = xhqm::translate(xhqm::fantasy::value(p.x), xhqm::fantasy::value(p.y), xhqm::fantasy::value(p.z));
				xhqm::fantasy::matrix R = quat_matrix(r);
				xhqm::fantasy::matrix S = xhqm::scale(xhqm::fantasy::value(s.x), xhqm::fantasy::value(s.y), xhqm::fantasy::value(s.z));
				local = T * R * S;
			}
			xhqm::fantasy::matrix parent_world = (nd.parent >= 0) ? worlds[nd.parent] : xhqm::fantasy::matrix(4, 4);
			worlds[i] = parent_world * local;
			bone_world.insert_or_assign(nd.name, worlds[i]);
		}
	}

	// 采样：返回蒙皮调色板（palette[i] = world(bone_i) * offset_i）
	inline std::vector<xhqm::fantasy::matrix> sample_palette(const anim_clip& anim, float time)
	{
		std::map<std::string, xhqm::fantasy::matrix> bone_world;
		sample_worlds(anim, time, bone_world);
		std::vector<xhqm::fantasy::matrix> palette;
		palette.reserve(anim.bone_names.size());
		for (size_t i = 0; i < anim.bone_names.size(); ++i)
		{
			auto it = bone_world.find(anim.bone_names[i]);
			xhqm::fantasy::matrix w = (it != bone_world.end()) ? it->second : xhqm::fantasy::matrix(4, 4);
			// 防御：动画采样可能插值出 NaN/Inf/过大骨骼矩阵（Assimp 四元数反平行 slerp、退化缩放等），
			// 会让蒙皮顶点被拉到屏幕外（巨大橙色三角形）+ 阴影图写 NaN 导致驱动崩溃；异常时退回单位阵。
			bool bad = false;
			for (int r = 0; r < 4 && !bad; ++r)
				for (int c = 0; c < 4 && !bad; ++c)
				{
					float v = (float)w(r, c);
					if (!std::isfinite(v) || std::fabs(v) > 1.0e6f) { bad = true; }
				}
			if (bad)
			{
				static int dbg_bad = 0;
				if (++dbg_bad <= 10) fprintf(stderr, "[ANIM-NAN] bone %zu CLAMPED clip=%s\n", i, anim.name.c_str());
				w = xhqm::fantasy::matrix(4, 4);
			}
			palette.push_back(w * anim.offsets[i]);
		}
		return palette;
	}

	// 递归收集 node 树到 anim_clip（自持副本，避免 importer 销毁后 aiNode 悬空）
	inline void collect_node_tree(const aiNode* n, int parent, anim_clip& out)
	{
		if (!n) return;
		int idx = (int)out.nodes.size();
		anim_clip::node_data nd;
		nd.name = n->mName.C_Str();
		nd.bind_local = to_xhqm(n->mTransformation);
		nd.parent = parent;
		out.nodes.push_back(nd);
		for (unsigned int i = 0; i < n->mNumChildren; ++i)
			collect_node_tree(n->mChildren[i], idx, out);
	}

	// 从单个 aiAnimation 生成 clip（复用共享骨架 base）
	inline anim_clip clip_from_animation(aiAnimation* a, const anim_clip& base, unsigned int ai)
	{
		float tps = (a->mTicksPerSecond > 0) ? (float)a->mTicksPerSecond : 25.0f;
		anim_clip clip = base;
		clip.name = (a->mName.length > 0) ? a->mName.C_Str() : ("anim_" + std::to_string(ai));
		clip.duration = (float)(a->mDuration / tps);
		for (unsigned int ch = 0; ch < a->mNumChannels; ++ch)
		{
			aiNodeAnim* na = a->mChannels[ch];
			bone_anim ba;
			for (unsigned int k = 0; k < na->mNumPositionKeys; ++k)
			{ ba.p_times.push_back((float)na->mPositionKeys[k].mTime / tps); ba.p_keys.push_back(na->mPositionKeys[k].mValue); }
			for (unsigned int k = 0; k < na->mNumRotationKeys; ++k)
			{ ba.r_times.push_back((float)na->mRotationKeys[k].mTime / tps); ba.r_keys.push_back(na->mRotationKeys[k].mValue); }
			for (unsigned int k = 0; k < na->mNumScalingKeys; ++k)
			{ ba.s_times.push_back((float)na->mScalingKeys[k].mTime / tps); ba.s_keys.push_back(na->mScalingKeys[k].mValue); }
			clip.channels[na->mNodeName.C_Str()] = ba;
		}
		return clip;
	}

	// 收集场景内全部动画 clip（PMX 通常无，VMD 有）
	inline void collect_clips(const aiScene* scene, const anim_clip& base, std::vector<anim_clip>& out_clips)
	{
		if (!scene || scene->mNumAnimations == 0) return;
		for (unsigned int ai = 0; ai < scene->mNumAnimations; ++ai)
			out_clips.push_back(clip_from_animation(scene->mAnimations[ai], base, ai));
	}

	// 加载模型 + 全部动画 clip：返回 models 树，动画写入 out_clips（每个 clip 独立含骨架副本）。
	// vmd 非空时额外加载 VMD 动画（骨骼关键帧，节点名与 PMX 骨骼名匹配）。
	inline xhqm::fantasy::models* load_model_animated(const std::string& path, const std::string& name, std::vector<anim_clip>& out_clips, const std::string& vmd = "")
	{
		using namespace xhqm::fantasy;
		Assimp::Importer importer;
		const aiScene* scene = importer.ReadFile(path + name,
			aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace);
		if (!scene || !scene->mRootNode) return nullptr;

		// 共享骨架：节点树 + 骨骼映射 + offset（所有 clip 复用，拷贝进每个 clip）
		anim_clip base;
		collect_node_tree(scene->mRootNode, -1, base);
		for (unsigned int m = 0; m < scene->mNumMeshes; ++m)
		{
			aiMesh* mesh = scene->mMeshes[m];
			for (unsigned int b = 0; b < mesh->mNumBones; ++b)
			{
				aiBone* bone = mesh->mBones[b];
				base.bone_names.push_back(bone->mName.C_Str());
				base.offsets.push_back(to_xhqm(bone->mOffsetMatrix));
			}
		}

		// 动画：PMX 内嵌（通常无）+ 可选 VMD
		out_clips.clear();
		collect_clips(scene, base, out_clips);
		if (!vmd.empty())
		{
			Assimp::Importer vimp;
			const aiScene* vs = vimp.ReadFile(vmd, 0);
			collect_clips(vs, base, out_clips);
		}
		if (out_clips.empty())
		{
			base.name = "BasePose";
			base.duration = 0.0f;
			out_clips.push_back(std::move(base));
		}

		// 加载 mesh（复用既有静态蒙皮路径）
		std::map<std::string, aiNode*> node_map;
		std::map<std::string, matrix> world_map;
		collect_world(scene->mRootNode, matrix(4, 4), node_map, world_map);

		// pmx：assimp 不挂贴图，额外解析材质贴图表
		std::vector<std::string> pmx_texs;
		const std::vector<std::string>* pmx_texs_ptr = nullptr;
		if (name.size() > 4 && name.compare(name.size() - 4, 4, ".pmx") == 0)
		{
			pmx_texs = pmx_material_textures(path + name);
			pmx_texs_ptr = &pmx_texs;
		}

		models* root = new models();
		for (unsigned int m = 0; m < scene->mNumMeshes; ++m)
		{
			int bone_count = 0;
			prefab* p = load_mesh_prefab(path, scene, scene->mMeshes[m], node_map, world_map, bone_count, pmx_texs_ptr);
			if (p) root->insert_drawer(p);
		}
		return root;
	}

	// 加载模型，每个 mesh 一个独立 models 节点（用于 gizmo 等需单独显隐的模型）
	inline std::vector<xhqm::fantasy::models*> load_model_separate(const std::string& path, const std::string& name)
	{
		using namespace xhqm::fantasy;
		Assimp::Importer importer;
		const aiScene* scene = importer.ReadFile(path + name,
			aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace);
		if (!scene || !scene->mRootNode) return {};

		std::map<std::string, aiNode*> node_map;
		std::map<std::string, matrix> world_map;
		collect_world(scene->mRootNode, matrix(4, 4), node_map, world_map);

		std::vector<models*> parts;
		for (unsigned int m = 0; m < scene->mNumMeshes; ++m)
		{
			int bone_count = 0;
			prefab* p = load_mesh_prefab(path, scene, scene->mMeshes[m], node_map, world_map, bone_count);
			if (p)
			{
				p->open_skin(false);   // gizmo 静态：顶点即绑定姿势，无需蒙皮（蒙皮在 Vulkan 下会错误变形）
				models* part = new models();
				part->insert_drawer(p);
				parts.push_back(part);
			}
		}
		return parts;
	}
}
