#include "drawer.h"
#include "rhi_no.h"
#include "shader_preprocess.h"
#include "shader_sources.h"
#include "ubo.h"
#include <algorithm>
#include <unordered_map>
#include <atomic>
#include <cstring>

std::string _xhqm_render_error_(const char* file, int line)
{
	return std::string("Render Error at ") + file + ":" + std::to_string(line);
}

namespace xhqm
{
	namespace fantasy
	{
		struct _render_info_
		{
			std::atomic<fantasy::id> _render_id_ = 0;   // 原子：多线程并行建树节点时避免 ++ 数据竞争
		};

		fantasy::id render_id()
		{
			_render_info_& render_info = xhqm::instance<fantasy::_render_info_>::get();
			if (render_info._render_id_.load() == 0)
			{
				std::string error;
				render_backend_active().init(error);
			}
			return (render_info._render_id_.fetch_add(1) + 1);
		}

		fantasy::uint render_type() { return render_backend_active().value_type(); }
		fantasy::uint render_size() { return render_backend_active().value_size(); }
		void render_finish() { render_backend_active().finish(); }
		void render_clearn(xhqm::rgba rgba) { render_backend_active().clear(rgba); }
		void render_window(sint x, sint y, sint w, sint h) { render_backend_active().viewport(x, y, w, h); }
		void render_winding(bool clockwise) { render_backend_active().winding(clockwise); }
		void render_pixels(sint x, sint y, uint* rgba_id) { render_backend_active().read_pixels(x, y, 1, 1, rgba_id); }
		void render_pixels(sint x, sint y, sint w, sint h, uint* rgba_ids) { render_backend_active().read_pixels(x, y, w, h, rgba_ids); }

		bool aabb::good()
		{
			if (min.x() > max.x()) return false;
			if (min.y() > max.y()) return false;
			if (min.z() > max.z()) return false;
			return true;
		}

		aabb aabb::stand()
		{
			point pot_min = min;
			point pot_max = max;
			min.x() = std::min(pot_min.x(), pot_max.x());
			min.y() = std::min(pot_min.y(), pot_max.y());
			min.z() = std::min(pot_min.z(), pot_max.z());
			max.x() = std::max(pot_min.x(), pot_max.x());
			max.y() = std::max(pot_min.y(), pot_max.y());
			max.z() = std::max(pot_min.z(), pot_max.z());
			return (*this);
		}

		aabb aabb::attach(const aabb& box)
		{
			min.x() = std::min(min.x(), box.min.x());
			min.y() = std::min(min.y(), box.min.y());
			min.z() = std::min(min.z(), box.min.z());
			max.x() = std::max(max.x(), box.max.x());
			max.y() = std::max(max.y(), box.max.y());
			max.z() = std::max(max.z(), box.max.z());
			return (*this);
		}

		aabb aabb::attach(const point& pot)
		{
			min.x() = std::min(min.x(), pot.x());
			min.y() = std::min(min.y(), pot.y());
			min.z() = std::min(min.z(), pot.z());
			max.x() = std::max(max.x(), pot.x());
			max.y() = std::max(max.y(), pot.y());
			max.z() = std::max(max.z(), pot.z());
			return (*this);
		}

		aabb aabb::access(const matrix& mat)
		{
			min = min * mat;
			max = max * mat;
			return this->stand();
		}

		_vertex::_vertex(_vertex&& move) noexcept
		{
			_shader_vao_id = move._shader_vao_id;
			_creat_error = move._creat_error;
			move._shader_vao_id = 0;
			move._creat_error = u8"";
		}

		_vertex::_vertex(const fantasy::values& attrib, const std::vector<fantasy::uint>& locations)
		{
			_vertex::reset(attrib, locations);
		}

		_vertex::~_vertex()
		{
			_vertex::reset();
		}

		void _vertex::reset(const fantasy::values& attrib, const std::vector<fantasy::uint>& locations)
		{
			_vertex::reset();
			_shader_vao_id = render_backend_active().vertex_create(attrib, locations, _creat_error);
		}

		void _vertex::reset()
		{
			if (_shader_vao_id)
			{
				render_backend_active().vertex_delete(_shader_vao_id);
				_shader_vao_id = 0;
			}
		}

		bool _vertex::operator=(_vertex&& move) noexcept
		{
			if (_shader_vao_id) return false;
			_shader_vao_id = move._shader_vao_id;
			_creat_error = move._creat_error;
			move._shader_vao_id = 0;
			move._creat_error = u8"";
			return true;
		}

		_shader::_shader(_shader&& move) noexcept
		{
			_vert_shader = move._vert_shader;
			_frag_shader = move._frag_shader;
			_creat_error = move._creat_error;
			_shader_id = move._shader_id;
			move._vert_shader = u8"";
			move._frag_shader = u8"";
			move._creat_error = u8"";
			move._shader_id = 0;
		}

		_shader::_shader(const std::string& vert, const std::string& frag)
			: _vert_shader(vert), _frag_shader(frag)
		{
			_shader::reset(vert, frag);
		}

		_shader::~_shader()
		{
			_shader::reset();
		}

		void _shader::reset(const std::string& vert, const std::string& frag)
		{
			_shader::reset();
			_vert_shader = vert;
			_frag_shader = frag;
			_creat_error.clear();
			_shader_id = render_backend_active().shader_create(vert, frag, _creat_error);
		}

		void _shader::reset()
		{
			if (_shader_id != 0)
			{
				render_backend_active().shader_delete(_shader_id);
				_shader_id = 0;
			}
		}

		bool _shader::reset(const shader_binary& binary)
		{
			_shader::reset();
			_creat_error.clear();
			_shader_id = render_backend_active().shader_create(binary, _creat_error);
			return (_shader_id != 0);
		}

		bool _shader::binary(shader_binary& out) const
		{
			return render_backend_active().shader_get_binary(_shader_id, out);
		}

		bool _shader::good(void) const
		{
			return (_shader_id != 0 && _creat_error.empty());
		}

		const std::string& _shader::error(void) const
		{
			return _creat_error;
		}

		bool _shader::operator=(_shader&& move) noexcept
		{
			if (_shader_id) return false;
			_vert_shader = move._vert_shader;
			_frag_shader = move._frag_shader;
			_creat_error = move._creat_error;
			_shader_id = move._shader_id;
			move._vert_shader = u8"";
			move._frag_shader = u8"";
			move._creat_error = u8"";
			move._shader_id = 0;
			return true;
		}

		_texture::_texture(_texture&& move) noexcept
		{
			_creat_error = move._creat_error;
			_texture_id = move._texture_id;
			move._creat_error = u8"";
			move._texture_id = 0;
		}

		_texture::_texture(const fantasy::rgba& color) { _texture::reset(color); }
		_texture::_texture(const std::vector<fantasy::rgba>& scale) { _texture::reset(scale); }
		_texture::_texture(const std::vector<std::vector<fantasy::rgba>>& image) { _texture::reset(image); }
		_texture::~_texture() { _texture::reset(); }

		void _texture::reset(const fantasy::rgba& color)
		{
			_texture::reset(std::vector<std::vector<fantasy::rgba>>{ { color, color }, { color, color } });
		}

		void _texture::reset(const std::vector<fantasy::rgba>& scale)
		{
			_texture::reset(std::vector<std::vector<fantasy::rgba>>{ scale, scale });
		}

		void _texture::reset(const std::vector<std::vector<fantasy::rgba>>& image)
		{
			_texture::reset();

			std::vector<fantasy::rgba> flat;
			// image 为行主序（image[y][x]），行数=高，列数=宽
			fantasy::uint h_size = static_cast<fantasy::uint>(image.size());
			fantasy::uint w_size = image.empty() ? 0 : static_cast<fantasy::uint>(image[0].size());
			for (auto& line : image) { flat.insert(flat.end(), line.begin(), line.end()); }

			_texture_id = render_backend_active().texture_create(flat, w_size, h_size, _creat_error);
		}

		void _texture::reset()
		{
			if (_texture_id != 0)
			{
				render_backend_active().texture_delete(_texture_id);
				_texture_id = 0;
			}
		}

		bool _texture::operator=(_texture&& move) noexcept
		{
			if (_texture_id) return false;
			_creat_error = move._creat_error;
			_texture_id = move._texture_id;
			move._creat_error = u8"";
			move._texture_id = 0;
			return true;
		}
	}

	namespace fantasy
	{
		void drawer::_aabb_(const xhqm::count& location)
		{
			xhqm::count count = m_attrib.size() / location;
			if (count == 0)
			{
				_aabb_size.min = fantasy::point();
				_aabb_size.max = fantasy::point();
			}
			else
			{
				_aabb_size.min = fantasy::point(m_attrib[0], m_attrib[1], m_attrib[2]);
				_aabb_size.max = fantasy::point(m_attrib[0], m_attrib[1], m_attrib[2]);
			}
			for (xhqm::index index = 0; index < count; ++index)
			{
				_aabb_size.attach(fantasy::point(m_attrib[index * location + 0], m_attrib[index * location + 1], m_attrib[index * location + 2]));
			}
		}

		drawer::~drawer()
		{
			// 基类析构：派生类（prefab / toon_prefab 等）经 drawer* 删除时由此入口正确下溯。
			// 本类没有需要在此释放的非 RAII 成员；持有的是 shader/vertex/texture 等句柄对象。
		}

		drawer::drawer()
			: m_system(4, 4)
			, m_world(4, 4)
		{
			_colour = 0xffffffff;
			_select = 0xffffff00;
			m_color_lignt = 0xffffffff;
			m_light_vps.resize(4, fantasy::matrix(4, 4));
		}

		prefab::~prefab()
		{
			if (m_index_id) render_backend_active().index_delete(m_index_id);
			m_index_id = 0;
		}

		const fantasy::shader& drawer::select_shader()
		{
			if (drawer::_select_shader == 0)
			{
				const bool vulkan = (render_backend_active().name()[0] == 'v');
				drawer::_select_shader = fantasy::shader(
					shader_preprocess(shader_sources::select_vert(), vulkan),
					shader_preprocess(shader_sources::select_frag(), vulkan));
			}
			return drawer::_select_shader;
		}

		void drawer::render_id(const fantasy::uint& color_id)
		{
			if (!_vertex_id || !m_system.data()) return;
			fantasy::shader select_shader_id = drawer::select_shader();
			render_backend& rhi = render_backend_active();

			// 记录并切换状态：禁用混合保证唯一颜色，深度 LEQUAL 保证拾取前景
			rhi.blend(false);
			rhi.depth_test(true, true);
			rhi.use_program(select_shader_id);
			{
				rhi.uniform_matrix(select_shader_id, "tranMatrix", m_system.tramat().data(), 1);
				std::array<xhqm::real, 4> color_rgba = xhqm::color::rgba2array(color_id);
				rhi.uniform_vec4(select_shader_id, "uuidSelect", color_rgba[0], color_rgba[1], color_rgba[2], color_rgba[3]);

				// 蒙皮：id 渲染也必须蒙皮，否则拾取轮廓与视觉模型错位
				int has_skin = 0;
				prefab* p = dynamic_cast<prefab*>(this);
				if (p && p->open_skin() && !p->bone_palette().empty())
				{
					has_skin = 1;
					const std::vector<matrix>& palette = p->bone_palette();
					const count max_bones = UBO_MAX_BONES;
					count bone_count = (palette.size() < max_bones) ? palette.size() : max_bones;
					std::vector<value> bones(bone_count * 16);
					for (count b = 0; b < bone_count; ++b)
					{
						matrix t = palette[b].tramat();
						for (fantasy::index i = 0; i < 16; ++i) { bones[b * 16 + i] = t.data()[i]; }
					}
					rhi.uniform_matrix(select_shader_id, "bones", bones.data(), static_cast<uint>(bone_count));
				}
				rhi.uniform_int(select_shader_id, "hasSkin", has_skin);
			}
			rhi.point_size(_poit_size);
			rhi.line_size(_line_size);
			_render_id_();
			rhi.use_program(0);
			// 恢复默认状态
			rhi.blend(true);
			rhi.depth_test(true, false);
		}

		// M2 weld：对交错顶点数组做精确去重，生成真实索引缓冲。
		// attrib 每顶点 stride 个 value；out_attrib 为去重后顶点，out_indices 为逐顶点索引（size == 原顶点数）。
		// 哈希 FNV-1a 对整顶点字节，桶内按字节相等比较——整顶点精确去重，不破坏 skin/tangent/blend 等属性。
		static void weld_vertices(const fantasy::values& attrib, const xhqm::count& stride, fantasy::values& out_attrib, std::vector<fantasy::uint>& out_indices)
		{
			out_attrib.clear();
			out_indices.clear();
			if (stride == 0 || attrib.empty()) return;
			const xhqm::count vert_count = (xhqm::count)(attrib.size() / stride);
			out_attrib.reserve(attrib.size());
			out_indices.reserve(vert_count);
			std::unordered_map<std::uint64_t, std::vector<fantasy::uint>> buckets;
			buckets.reserve((size_t)vert_count * 2);
			const fantasy::value* base = attrib.data();
			for (xhqm::count v = 0; v < vert_count; ++v)
			{
				const fantasy::value* vp = base + (size_t)v * stride;
				const std::uint32_t* bits = reinterpret_cast<const std::uint32_t*>(vp);
				std::uint64_t h = 1469598103934665603ull;
				for (xhqm::count k = 0; k < stride; ++k)
				{
					h ^= (std::uint64_t)bits[k];
					h *= 1099511628211ull;
				}
				auto& bucket = buckets[h];
				fantasy::uint found = 0xffffffffu;
				for (fantasy::uint idx : bucket)
				{
					const fantasy::value* up = out_attrib.data() + (size_t)idx * stride;
					if (0 == std::memcmp(up, vp, (size_t)stride * sizeof(fantasy::value))) { found = idx; break; }
				}
				if (found != 0xffffffffu)
				{
					out_indices.push_back(found);
				}
				else
				{
					fantasy::uint new_idx = (fantasy::uint)(out_attrib.size() / stride);
					out_attrib.insert(out_attrib.end(), vp, vp + stride);
					bucket.push_back(new_idx);
					out_indices.push_back(new_idx);
				}
			}
		}

		void prefab::shader()
		{
			if (prefab::_shader == 0)
			{
				const bool vulkan = (render_backend_active().name()[0] == 'v');
				// apple_gl：仅 macOS 的 OpenGL 后端启用光照变通（Apple GL 驱动上 UBO 灯光数组读取会杀死片元）
				bool apple_gl = false;
#ifdef __APPLE__
				apple_gl = !vulkan;
#endif
				prefab::_shader = fantasy::shader(
					shader_preprocess(shader_sources::prefab_vert(), vulkan, apple_gl),
					shader_preprocess(shader_sources::prefab_frag(), vulkan, apple_gl));
			}

			if (shader_id() == 0) { _shader_id = prefab::_shader; };
		}

		toon_prefab::~toon_prefab()
		{
		}

		void toon_prefab::shader()
		{
			if (!toon_prefab::_toon_shader_ready)
			{
				const bool vulkan = (render_backend_active().name()[0] == 'v');
				bool apple_gl = false;
#ifdef __APPLE__
				apple_gl = !vulkan;
#endif
				toon_prefab::_toon_shader = fantasy::shader(
					shader_preprocess(shader_sources::toon_vert(), vulkan, apple_gl),
					shader_preprocess(shader_sources::toon_frag(), vulkan, apple_gl));
				toon_prefab::_toon_shader_ready = true;
			}

			if (shader_id() == 0) { shader_id(toon_prefab::_toon_shader); };
		}

		void prefab::updata()
		{
			if (shader_id() == 0) { shader(); }

			// M9 多目标 blendshape：CPU 混合 N 个目标（权重和）进单通道 delta，供单通道 shader 路径
			if (!m_blend_targets.empty() && m_blend_target_weights.size() == m_blend_targets.size())
			{
				m_blend_delta.assign(m_size * 3, { 0, 0, 0 });
				m_blend_normal_delta.assign(m_size * 3, { 0, 0, 0 });
				for (size_t t = 0; t < m_blend_targets.size(); ++t)
				{
					value w = m_blend_target_weights[t];
					const auto& td = m_blend_targets[t].delta;
					const auto& tn = m_blend_targets[t].normal_delta;
					for (size_t v = 0; v < (size_t)m_size * 3; ++v)
					{
						m_blend_delta[v][0] += w * td[v][0];
						m_blend_delta[v][1] += w * td[v][1];
						m_blend_delta[v][2] += w * td[v][2];
						m_blend_normal_delta[v][0] += w * tn[v][0];
						m_blend_normal_delta[v][1] += w * tn[v][1];
						m_blend_normal_delta[v][2] += w * tn[v][2];
					}
				}
				m_open_blend = true;
				m_blend_weight = 1.0f;   // 混合已进 delta，单通道权重置 1
			}

			fantasy::uint size = 0;//属性个数
			std::vector<fantasy::uint> locations;
			m_open_attrib = { false, false, false, false, false, false, false, false, false, false };
			if (m_vertex.size() == m_size * 1) { size += 3; locations.push_back(3); m_open_attrib[0] = true; }
			else { locations.push_back(0); }
			if (m_colour.size() == m_size * 3) { size += 4; locations.push_back(4); m_open_attrib[1] = true; }
			else { locations.push_back(0); }
			if (m_normal.size() == m_size * 3) { size += 3; locations.push_back(3); m_open_attrib[2] = true; }
			else { locations.push_back(0); }
			if (m_flipuv.size() == m_size * 3) { size += 2; locations.push_back(2); m_open_attrib[3] = true; }
			else { locations.push_back(0); }
			if (m_values.size() == m_size * 3) { size += 1; locations.push_back(1); m_open_attrib[4] = true; }
			else { locations.push_back(0); }
			if (m_open_skin && m_skin_index.size() == m_size * 3) { size += 4; locations.push_back(4); m_open_attrib[5] = true; }
			else { locations.push_back(0); }
			if (m_open_skin && m_skin_weight.size() == m_size * 3) { size += 4; locations.push_back(4); m_open_attrib[6] = true; }
			else { locations.push_back(0); }
			if (m_open_blend && m_blend_delta.size() == m_size * 3) { size += 3; locations.push_back(3); m_open_attrib[7] = true; }
			else { locations.push_back(0); }
			if (m_tangent.size() == m_size * 3) { size += 3; locations.push_back(3); m_open_attrib[8] = true; }
			else { locations.push_back(0); }
			if (m_blend_normal_delta.size() == m_size * 3) { size += 3; locations.push_back(3); m_open_attrib[9] = true; }
			else { locations.push_back(0); }
			auto push_attrib = [this](const xhqm::index& ind, const xhqm::index& i)
			{
				if (m_open_attrib[0])
				{
					m_attrib.push_back(m_vertex[ind][i].x());
					m_attrib.push_back(m_vertex[ind][i].y());
					m_attrib.push_back(m_vertex[ind][i].z());
				}
				if (m_open_attrib[1])
				{
					m_attrib.push_back(m_colour[ind * 3 + i][0]);
					m_attrib.push_back(m_colour[ind * 3 + i][1]);
					m_attrib.push_back(m_colour[ind * 3 + i][2]);
					m_attrib.push_back(m_colour[ind * 3 + i][3]);
				}
				if (m_open_attrib[2])
				{
					m_attrib.push_back(m_normal[ind * 3 + i][0]);
					m_attrib.push_back(m_normal[ind * 3 + i][1]);
					m_attrib.push_back(m_normal[ind * 3 + i][2]);
				}
				if (m_open_attrib[3])
				{
					m_attrib.push_back(m_flipuv[ind * 3 + i][0]);
					m_attrib.push_back(m_flipuv[ind * 3 + i][1]);
				}
				if (m_open_attrib[4])
				{
					m_attrib.push_back(m_values[ind * 3 + i][0]);
				}
				if (m_open_attrib[5])
				{
					m_attrib.push_back(m_skin_index[ind * 3 + i][0]);
					m_attrib.push_back(m_skin_index[ind * 3 + i][1]);
					m_attrib.push_back(m_skin_index[ind * 3 + i][2]);
					m_attrib.push_back(m_skin_index[ind * 3 + i][3]);
				}
				if (m_open_attrib[6])
				{
					m_attrib.push_back(m_skin_weight[ind * 3 + i][0]);
					m_attrib.push_back(m_skin_weight[ind * 3 + i][1]);
					m_attrib.push_back(m_skin_weight[ind * 3 + i][2]);
					m_attrib.push_back(m_skin_weight[ind * 3 + i][3]);
				}
				if (m_open_attrib[7])
				{
					m_attrib.push_back(m_blend_delta[ind * 3 + i][0]);
					m_attrib.push_back(m_blend_delta[ind * 3 + i][1]);
					m_attrib.push_back(m_blend_delta[ind * 3 + i][2]);
				}
				if (m_open_attrib[8])
				{
					m_attrib.push_back(m_tangent[ind * 3 + i][0]);
					m_attrib.push_back(m_tangent[ind * 3 + i][1]);
					m_attrib.push_back(m_tangent[ind * 3 + i][2]);
				}
				if (m_open_attrib[9])
				{
					m_attrib.push_back(m_blend_normal_delta[ind * 3 + i][0]);
					m_attrib.push_back(m_blend_normal_delta[ind * 3 + i][1]);
					m_attrib.push_back(m_blend_normal_delta[ind * 3 + i][2]);
				}
			};
			for (xhqm::index ind = 0; ind < m_size; ++ind)
			{
				push_attrib(ind, 0);
				push_attrib(ind, 1);
				push_attrib(ind, 2);
			}
			for (xhqm::index ind = 0; ind < m_size; ++ind)
			{
				push_attrib(ind, 0);
				push_attrib(ind, 1);
				push_attrib(ind, 1);
				push_attrib(ind, 2);
				push_attrib(ind, 2);
				push_attrib(ind, 0);
			}

			drawer::_aabb_(size);

			// M2 weld：顶点去重 + 真实索引缓冲（面 3*m_size + 线 6*m_size 共 9*m_size 个索引）
			fantasy::values dedup_attrib;
			weld_vertices(m_attrib, size, dedup_attrib, m_index);
			_vertex_id.reset(dedup_attrib, locations);
			m_attrib.clear();

			if (m_index_id) render_backend_active().index_delete(m_index_id);
			std::string ierr;
			m_index_id = render_backend_active().index_create(m_index, ierr);
		}

		prefab* prefab::batch_static(const std::vector<prefab*>& sources)
		{
			if (sources.empty()) return nullptr;
			for (prefab* p : sources) if (!p) return nullptr;
			prefab* first = sources[0];

			// 属性布局掩码：只按「该槽是否填充」比较，槽位缺省一致才可合并
			auto mask = [](prefab* p) {
				fantasy::uint m = 0;
				if (p->m_colour.size() == p->m_size * 3) m |= 1u;
				if (p->m_normal.size() == p->m_size * 3) m |= 2u;
				if (p->m_flipuv.size() == p->m_size * 3) m |= 4u;
				if (p->m_values.size() == p->m_size * 3) m |= 8u;
				if (p->m_tangent.size() == p->m_size * 3) m |= 16u;
				return m;
			};
			const fantasy::uint fmask = mask(first);
			for (prefab* p : sources)
			{
				if (mask(p) != fmask) return nullptr;
				// 静态批处理只收静态网格：蒙皮 / 形变 / 实例化一律不合并
				if (p->m_open_skin || p->m_open_blend || !p->m_instances.empty()) return nullptr;
			}

			prefab* out = new prefab();
			xhqm::count total = 0;
			for (prefab* p : sources) total += p->m_size;
			out->m_size = total;

			// 逐几何槽拼接（成员函数可访问 protected 成员）
			out->m_vertex.clear();
			out->m_vertex.reserve(total * 1);
			for (prefab* p : sources) out->m_vertex.insert(out->m_vertex.end(), p->m_vertex.begin(), p->m_vertex.end());

			if (fmask & 1u)
			{
				out->m_colour.clear(); out->m_colour.reserve(total * 3);
				for (prefab* p : sources) out->m_colour.insert(out->m_colour.end(), p->m_colour.begin(), p->m_colour.end());
			}
			if (fmask & 2u)
			{
				out->m_normal.clear(); out->m_normal.reserve(total * 3);
				for (prefab* p : sources) out->m_normal.insert(out->m_normal.end(), p->m_normal.begin(), p->m_normal.end());
			}
			if (fmask & 4u)
			{
				out->m_flipuv.clear(); out->m_flipuv.reserve(total * 3);
				for (prefab* p : sources) out->m_flipuv.insert(out->m_flipuv.end(), p->m_flipuv.begin(), p->m_flipuv.end());
			}
			if (fmask & 8u)
			{
				out->m_values.clear(); out->m_values.reserve(total * 3);
				for (prefab* p : sources) out->m_values.insert(out->m_values.end(), p->m_values.begin(), p->m_values.end());
			}
			if (fmask & 16u)
			{
				out->m_tangent.clear(); out->m_tangent.reserve(total * 3);
				for (prefab* p : sources) out->m_tangent.insert(out->m_tangent.end(), p->m_tangent.begin(), p->m_tangent.end());
			}

			// 材质 / 纹理 / 渲染开关 / 颜色沿用首个源
			out->material(first->material());
			out->m_texture_layer = first->m_texture_layer;
			out->m_open_poit = first->m_open_poit;
			out->m_open_line = first->m_open_line;
			out->m_open_face = first->m_open_face;
			out->m_color_poit = first->m_color_poit;
			out->m_color_line = first->m_color_line;
			out->m_color_face = first->m_color_face;
			out->m_open_light = first->m_open_light;
			out->m_open_skin = false;
			out->m_open_blend = false;
			return out;
		}

		void prefab::render()
		{
			if (!_shader_id || !_vertex_id || !m_system.data()) return;

			render_backend& rhi = render_backend_active();
			fantasy::uint program = static_cast<fantasy::uint>(_shader_id);

			rhi.use_program(program);
			{
				rhi.uniform_matrix(program, "tranMatrix", m_system.tramat().data(), 1);
				rhi.uniform_matrix(program, "world", m_world.tramat().data(), 1);

				std::array<xhqm::real, 4> sel = { 1, 1, 1, 1 };
				if (_is_select) { sel = xhqm::color::rgba2array(_select); }
				rhi.uniform_vec4(program, "prefabSelect", sel[0], sel[1], sel[2], sel[3]);

				rhi.uniform_int(program, "useLighting", m_open_light ? 1 : 0);
				if (m_light_env)
				{
					// 场景级光照环境：统一光源列表（CPU 缓存 light_data，避免逐 drawer 重复构造）
					uint count = m_light_env->light_count();
					if (count > fantasy::MAX_LIGHTS) count = fantasy::MAX_LIGHTS;
					rhi.uniform_lights(program, count, count ? m_light_env->light_data().data() : nullptr);
				}
				else
				{
					// 旧路径：每 drawer 自持光源（兼容）；为空则退回默认方向光
					std::vector<fantasy::light_desc> lights = m_lights;
					if (lights.empty())
					{
						fantasy::light_desc fallback;
						fallback.type = fantasy::light_type::directional;
						fallback.color = m_color_lignt;
						fallback.direction = point(0.0f, 0.0f, -1.0f);
						lights.push_back(fallback);
					}
					if (lights.size() > fantasy::MAX_LIGHTS) lights.resize(fantasy::MAX_LIGHTS);
					std::vector<value> light_data(lights.size() * 16, 0);
					for (size_t i = 0; i < lights.size(); ++i)
					{
						const fantasy::light_desc& l = lights[i];
						std::array<xhqm::real, 4> c = xhqm::color::rgba2array(l.color);
						value* p = light_data.data() + i * 16;
						p[0] = c[0]; p[1] = c[1]; p[2] = c[2];
						p[3] = (value)(uint)l.type;
						p[4] = l.position.x(); p[5] = l.position.y(); p[6] = l.position.z();
						p[8] = l.direction.x(); p[9] = l.direction.y(); p[10] = l.direction.z();
						p[12] = l.cutoff; p[13] = l.outer_cutoff;
					}
					rhi.uniform_lights(program, (uint)lights.size(), light_data.data());
				}

				// 级联阴影
				rhi.uniform_int(program, "shadowEnabled", (int)m_cascade_count);
				if (m_cascade_count == 0)
				{
					// 无阴影：解绑全部级联槽（阴影 pass 内绘制时防自采样自身深度附件）
					for (uint c = 0; c < 4; ++c) rhi.bind_texture(3 + c, 0);
				}
				if (m_cascade_count > 0)
				{
					std::vector<value> vp_data((size_t)m_cascade_count * 16);
					for (uint c = 0; c < m_cascade_count; ++c)
					{
						fantasy::matrix t = m_light_vps[c].tramat();
						for (uint i = 0; i < 16; ++i) vp_data[c * 16 + i] = t.data()[i];
					}
					rhi.uniform_matrix(program, "lightVP", vp_data.data(), m_cascade_count);
					rhi.uniform_vec4(program, "cascadeSplits", m_cascade_splits[0], m_cascade_splits[1], m_cascade_splits[2], m_cascade_splits[3]);
					rhi.uniform_vec3(program, "cameraPos", m_camera_pos.x(), m_camera_pos.y(), m_camera_pos.z());
					// 未用到的级联槽显式解绑：否则上一帧的阴影图会残留在这些绑定上，
					// 阴影 pass 内同一 program 绘制时"自采样"自身深度附件（Vulkan 布局冲突），
					// 非阴影期也可能采到过期阴影图。解绑后后端按类型给默认深度纹理兜底。
					for (uint c = 0; c < 4; ++c) { rhi.bind_texture(3 + c, (c < m_cascade_count) ? m_shadow_texs[c] : 0); }
					for (int c = 0; c < 4; ++c)
					{
						std::string name = "shadowMap[" + std::to_string(c) + "]";
						rhi.uniform_int(program, name.c_str(), (int)(3 + c));
					}
				}

				for (int i = 0; i < 3; ++i)
				{
					if (m_texture_layer[i])
					{
						std::string texName = "prefabTexture_" + std::to_string(i);
						rhi.uniform_int(program, texName.c_str(), i);
					}
				}
				rhi.uniform_int(program, "hasTexture_0", (m_texture_layer[0] ? 1 : 0));
				rhi.uniform_int(program, "hasTexture_1", (m_texture_layer[1] ? 1 : 0));
				rhi.uniform_int(program, "hasTexture_2", (m_texture_layer[2] ? 1 : 0));
				rhi.uniform_int(program, "hasColour", (m_open_attrib[1] ? 1 : 0));
				rhi.uniform_int(program, "hasNormal", (m_open_attrib[2] ? 1 : 0));
				rhi.uniform_int(program, "hasFlipuv", (m_open_attrib[3] ? 1 : 0));
				rhi.uniform_int(program, "hasValues", (m_open_attrib[4] ? 1 : 0));
				rhi.uniform_int(program, "hasTangent", (m_open_attrib[8] ? 1 : 0));
				rhi.uniform_int(program, "hasSkin", ((m_open_skin && m_open_attrib[5] && m_open_attrib[6]) ? 1 : 0));
				rhi.uniform_int(program, "hasBlend", ((m_open_blend && m_open_attrib[7]) ? 1 : 0));
				rhi.uniform_float(program, "blendWeight", m_blend_weight);
				rhi.uniform_vec4(program, "colorGrade", m_exposure, m_contrast, m_saturation, 0);
				rhi.uniform_int(program, "tonemapMode", m_tonemap);
				std::array<xhqm::real, 4> amb = xhqm::color::rgba2array(m_light_env ? m_light_env->ambient() : m_ambient);
				rhi.uniform_vec4(program, "ambientColor", amb[0], amb[1], amb[2], m_light_env ? m_light_env->reflection() : m_reflection);

				// per-material 参数（M1）：无 material 时用默认（白/金属 0/粗糙 0.5/ao 1/alpha 1/无自发光）
				{
					std::array<xhqm::real, 4> mat_base = { 1, 1, 1, 1 };
					std::array<xhqm::real, 4> mat_pbr = { 0, 0.5f, 1, 1 };
					std::array<xhqm::real, 4> mat_emissive = { 0, 0, 0, 0 };
					std::array<xhqm::real, 4> mat_flags = { 0, 0, 0, 0 };
					if (m_material)
					{
						const material_desc& mt = *m_material;
						mat_base = xhqm::color::rgba2array(mt.base_color);
						mat_pbr = { mt.metallic, mt.roughness, mt.ao, mt.alpha };
						mat_emissive = xhqm::color::rgba2array(mt.emissive);
						mat_flags[0] = mt.double_sided ? 1.0f : 0.0f;
						mat_flags[1] = mt.normal_map ? 1.0f : 0.0f;   // hasNormalMap
						mat_flags[2] = mt.ao_map ? 1.0f : 0.0f;         // hasAoMap
					}
					// irradiance（IBL）：光照环境提供卷积结果时启用（materialFlags.w 复用为 hasIrradiance）
					mat_flags[3] = (m_light_env && m_light_env->irradiance()) ? 1.0f : 0.0f;
					rhi.uniform_vec4(program, "materialBase", mat_base[0], mat_base[1], mat_base[2], mat_base[3]);
					rhi.uniform_vec4(program, "materialPBR", mat_pbr[0], mat_pbr[1], mat_pbr[2], mat_pbr[3]);
					rhi.uniform_vec4(program, "materialEmissive", mat_emissive[0], mat_emissive[1], mat_emissive[2], mat_emissive[3]);
					rhi.uniform_vec4(program, "materialFlags", mat_flags[0], mat_flags[1], mat_flags[2], mat_flags[3]);
					// 卡通渲染参数（toon 着色路径；PBR 着色器不声明对应字段时后端按未知名忽略）
					std::array<xhqm::real, 4> mat_toon = { 0.5f, 0.08f, 3.0f, 0.0f };
					std::array<xhqm::real, 4> toon_rim = { 0, 0, 0, 0 };
					if (m_material)
					{
						const material_desc& mt = *m_material;
						mat_toon = { mt.ramp_threshold, mt.ramp_softness, mt.rim_power, mt.spec_threshold };
						toon_rim = xhqm::color::rgba2array(mt.rim_color);
					}
					rhi.uniform_vec4(program, "materialToon", mat_toon[0], mat_toon[1], mat_toon[2], mat_toon[3]);
					rhi.uniform_vec4(program, "toonRimColor", toon_rim[0], toon_rim[1], toon_rim[2], toon_rim[3]);
				}
				// 相机世界位置（M1：视线向量 V 依赖，总是上传；级联阴影块内可能再传一次，无害）
				rhi.uniform_vec3(program, "cameraPos", m_camera_pos.x(), m_camera_pos.y(), m_camera_pos.z());

				// 实例变换（M3 instancing）：上传实例矩阵数组 + hasInstances flag
				rhi.uniform_int(program, "hasInstances", (m_instances.empty() ? 0 : 1));
				if (!m_instances.empty())
				{
					fantasy::count n = (m_instances.size() < UBO_MAX_INSTANCES) ? m_instances.size() : UBO_MAX_INSTANCES;
					std::vector<value> inst(n * 16);
					for (fantasy::count i = 0; i < n; ++i)
					{
						fantasy::matrix tr = m_instances[i].tramat();
						for (fantasy::index k = 0; k < 16; ++k) inst[i * 16 + k] = tr.data()[k];
					}
					rhi.uniform_matrix(program, "instances", inst.data(), (uint)n);
				}

				if (m_open_skin && !m_bone_palette.empty())
				{
					// 骨骼矩阵放 UBO（bones[UBO_MAX_BONES]），GL/Vulkan 一致
					const fantasy::count max_bones = UBO_MAX_BONES;
					fantasy::count bone_count = (m_bone_palette.size() < max_bones) ? m_bone_palette.size() : max_bones;
					std::vector<value> bones(bone_count * 16);
					for (fantasy::count b = 0; b < bone_count; ++b)
					{
						fantasy::matrix transposed = m_bone_palette[b].tramat();
						for (fantasy::index i = 0; i < 16; ++i) { bones[b * 16 + i] = transposed.data()[i]; }
					}
					rhi.uniform_matrix(program, "bones", bones.data(), static_cast<uint>(bone_count));
				}
			}

			rhi.point_size(_poit_size);
			rhi.line_size(_line_size);
			rhi.bind_texture(0, static_cast<uint>(m_texture_layer[0]));
			rhi.bind_texture(1, static_cast<uint>(m_texture_layer[1]));
			rhi.bind_texture(2, static_cast<uint>(m_texture_layer[2]));
			rhi.bind_texture(10, (m_light_env && m_light_env->irradiance()) ? m_light_env->irradiance() : 0);
			rhi.uniform_int(program, "irradianceMap", 10);
			rhi.bind_texture(11, (m_light_env && m_light_env->prefiltered()) ? m_light_env->prefiltered() : 0);
			rhi.uniform_int(program, "prefilteredMap", 11);
			rhi.bind_texture(7, (m_material && m_material->normal_map) ? static_cast<uint>(m_material->normal_map) : 0);
			rhi.bind_texture(8, (m_material && m_material->ao_map) ? static_cast<uint>(m_material->ao_map) : 0);

			uint index = 0;
			uint count = 0;
			uint inst_count = (m_instances.empty() ? 1 : (uint)m_instances.size());

			index = 0;
			count = static_cast<uint>(3 * m_size);
			if (m_open_poit && count > 0) {
				std::array<xhqm::real, 4> color_rgba = xhqm::color::rgba2array(m_color_poit);
				rhi.uniform_vec4(program, "prefabColour", color_rgba[0], color_rgba[1], color_rgba[2], color_rgba[3]);
				rhi.draw_indexed_instanced(_vertex_id, m_index_id, rhi_primitive::points, index, count, inst_count);
			}

			index = count;
			count = static_cast<uint>(6 * m_size);
			if (m_open_line && count > 0) {
				std::array<xhqm::real, 4> color_rgba = xhqm::color::rgba2array(m_color_line);
				rhi.uniform_vec4(program, "prefabColour", color_rgba[0], color_rgba[1], color_rgba[2], color_rgba[3]);
				rhi.draw_indexed_instanced(_vertex_id, m_index_id, rhi_primitive::lines, index, count, inst_count);
			}

			index = 0;
			count = static_cast<uint>(3 * m_size);
			if (m_open_face && count > 0) {
				std::array<xhqm::real, 4> color_rgba = xhqm::color::rgba2array(m_color_face);
				rhi.uniform_vec4(program, "prefabColour", color_rgba[0], color_rgba[1], color_rgba[2], color_rgba[3]);
				rhi.draw_indexed_instanced(_vertex_id, m_index_id, rhi_primitive::triangles, index, count, inst_count);
			}

			rhi.bind_texture(0, 0);
			rhi.bind_texture(1, 0);
			rhi.bind_texture(2, 0);
			rhi.use_program(0);
		}

		void prefab::_render_id_()
		{
			render_backend& rhi = render_backend_active();
			uint index = 0;
			uint count = 0;
			uint inst_count = (m_instances.empty() ? 1 : (uint)m_instances.size());
			index = count;
			count = static_cast<uint>(3 * m_size);
			if (m_open_poit) { rhi.draw_indexed_instanced(_vertex_id, m_index_id, rhi_primitive::points, index, count, inst_count); }
			index = count;
			count = static_cast<uint>(6 * m_size);
			if (m_open_line) { rhi.draw_indexed_instanced(_vertex_id, m_index_id, rhi_primitive::lines, index, count, inst_count); }
			index = 0;
			count = static_cast<uint>(3 * m_size);
			if (m_open_face) { rhi.draw_indexed_instanced(_vertex_id, m_index_id, rhi_primitive::triangles, index, count, inst_count); }
		}

		void sprite::shader()
		{
			if (sprite::_shader == 0)
			{
				const bool vulkan = (render_backend_active().name()[0] == 'v');
				sprite::_shader = fantasy::shader(
					shader_preprocess(shader_sources::sprite_vert(), vulkan),
					shader_preprocess(shader_sources::sprite_frag(), vulkan));
			}

			if (shader_id() == 0) { _shader_id = sprite::_shader; };
		}

		void sprite::updata()
		{
			if (shader_id() == 0) { shader(); }

			fantasy::uint size = 0;//属性个数
			std::vector<fantasy::uint> locations;
			m_open_attrib = { false,false, false };
			if (m_vertex.size() == m_size * 1) { size += 3; locations.push_back(3); m_open_attrib[0] = true; }
			else { locations.push_back(0); }
			if (m_colour.size() == m_size * 3) { size += 4; locations.push_back(4); m_open_attrib[1] = true; }
			else { locations.push_back(0); }
			if (m_flipuv.size() == m_size * 3) { size += 2; locations.push_back(2); m_open_attrib[2] = true; }
			else { locations.push_back(0); }
			auto push_attrib = [this](const xhqm::index& ind, const xhqm::index& i)
			{
				if (m_open_attrib[0])
				{
					m_attrib.push_back(m_vertex[ind][i].x());
					m_attrib.push_back(m_vertex[ind][i].y());
					m_attrib.push_back(m_layer);
				}
				if (m_open_attrib[1])
				{
					m_attrib.push_back(m_colour[ind * 3 + i][0]);
					m_attrib.push_back(m_colour[ind * 3 + i][1]);
					m_attrib.push_back(m_colour[ind * 3 + i][2]);
					m_attrib.push_back(m_colour[ind * 3 + i][3]);
				}
				if (m_open_attrib[2])
				{
					m_attrib.push_back(m_flipuv[ind * 3 + i][0]);
					m_attrib.push_back(m_flipuv[ind * 3 + i][1]);
				}
			};
			for (xhqm::index ind = 0; ind < m_size; ++ind)
			{
				push_attrib(ind, 0);
				push_attrib(ind, 1);
				push_attrib(ind, 2);
			}

			_vertex_id.reset(m_attrib, locations);
			drawer::_aabb_(size);
			m_attrib.clear();
		}

		void sprite::render()
		{
			if (!_shader_id || !_vertex_id || !m_system.data()) return;

			render_backend& rhi = render_backend_active();
			fantasy::uint program = static_cast<fantasy::uint>(_shader_id);

			rhi.use_program(program);
			{
				rhi.uniform_matrix(program, "tranMatrix", m_system.tramat().data(), 1);

				std::array<xhqm::real, 4> color_rgba = { 1,1,1,1 };
				if (_is_select) { color_rgba = xhqm::color::rgba2array(_select); }
				rhi.uniform_vec4(program, "spriteSelect", color_rgba[0], color_rgba[1], color_rgba[2], color_rgba[3]);

				rhi.uniform_int(program, "spriteTexture", 0);
				rhi.uniform_int(program, "hasTexture", (m_texture ? 1 : 0));
				rhi.uniform_int(program, "hasColour", (m_open_attrib[1] ? 1 : 0));
				rhi.uniform_int(program, "hasFlipuv", (m_open_attrib[2] ? 1 : 0));
			}

			rhi.point_size(_poit_size);
			rhi.line_size(_line_size);
			rhi.bind_texture(0, static_cast<uint>(m_texture));

			std::array<xhqm::real, 4> color_rgba = xhqm::color::rgba2array(m_color);
			rhi.uniform_vec4(program, "spriteColour", color_rgba[0], color_rgba[1], color_rgba[2], color_rgba[3]);
			rhi.draw(_vertex_id, rhi_primitive::triangles, 0, static_cast<uint>(3 * m_size));

			rhi.bind_texture(0, 0);
			rhi.use_program(0);
		}

		void sprite::_render_id_()
		{
			render_backend_active().draw(_vertex_id, rhi_primitive::triangles, 0, static_cast<uint>(3 * m_size));
		}
	}
}
