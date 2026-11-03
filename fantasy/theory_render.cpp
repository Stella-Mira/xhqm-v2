#include "theory_render.h"
#include "rhi_no.h"
#include "shader_preprocess.h"
#include "shader_sources.h"
#include "ubo.h"
#include "camera.h"
#include "model_ecs.h"
#include <cmath>

namespace xhqm
{
	namespace fantasy
	{
		namespace
		{
			// 收集场景第一个方向光；无则退回前上方向光
			fantasy::light_desc gather_directional(fantasy::object* root_ptr)
			{
				fantasy::light_desc out;
				out.type = fantasy::light_type::directional;
				out.color = 0xffffffff;
				out.direction = point(0.0f, -0.6f, -0.8f);
				if (!root_ptr) return out;
				for (auto* obj : (*root_ptr))
				{
					auto* m_ptr = dynamic_cast<fantasy::models*>(obj);
					if (!m_ptr) continue;
					for (auto* d : m_ptr->handle_drawers())
					{
						const std::vector<fantasy::light_desc>& ls = d->lights();
						for (const auto& l : ls)
							if (l.type == fantasy::light_type::directional) return l;
					}
				}
				return out;
			}

			// 批量设置所有 drawer 的级联数（阴影深度生成期间置 0，防自采样）
			void set_all_cascade(fantasy::object* root_ptr, uint count)
			{
				if (!root_ptr) return;
				for (auto* obj : (*root_ptr))
				{
					auto* m_ptr = dynamic_cast<fantasy::models*>(obj);
					if (!m_ptr) continue;
					for (auto* d : m_ptr->handle_drawers()) d->cascade_count(count);
				}
			}

			// 把阴影深度图 / 光 VP / 相机位写进每个 drawer 的阴影态（方向光换主光，点光保留）
			void apply_shadow_state(fantasy::object* root_ptr, shadow_map& sm, const point& cam_pos, const fantasy::light_desc& sun, light* light_env)
			{
				if (!root_ptr) return;
				uint n = sm.ready() ? 1 : 0;
				uint dt = sm.ready() ? sm.depth_texture() : 0;
				fantasy::matrix lvp = sm.light_view_proj();
				// 光照环境路径：主方向光统一更新到 light_env（drawer 优先读它）
				if (light_env) light_env->set_directional(sun);
				for (auto* obj : (*root_ptr))
				{
					auto* m_ptr = dynamic_cast<fantasy::models*>(obj);
					if (!m_ptr) continue;
					for (auto* d : m_ptr->handle_drawers())
					{
						if (!d->shadow()) { d->cascade_count(0); continue; }

						if (!light_env)
						{
							// 无光照环境：方向光合并进 drawer 的 m_lights（旧路径）
							std::vector<fantasy::light_desc> ls = d->lights();
							bool has_dir = false;
							for (auto& l : ls)
								if (l.type == fantasy::light_type::directional) { l = sun; has_dir = true; }
							if (!has_dir) ls.push_back(sun);
							d->lights(ls);
						}

						if (!n || !dt) { d->cascade_count(0); continue; }
						d->cascade_count(1);
						d->shadow_texs()[0] = dt;
						auto& lvps = d->light_vps();
						lvps.resize(1, fantasy::matrix(4, 4));
						lvps[0] = lvp;
						d->cascade_splits()[0] = value(0.0f);
						d->camera_pos(cam_pos);
					}
				}
			}

			// 取相机当前视图尺寸（0 兜底为 1）
			inline void view_wh(object* cam, uint& w, uint& h)
			{
				std::array<uint, 3> sz{ 0, 0, 0 };
				if (camera* rc = dynamic_cast<camera*>(cam)) sz = rc->view_size();
				w = sz[0] ? sz[0] : 1;
				h = sz[1] ? sz[1] : 1;
			}

			// 相机世界位置（阴影 pass 的 cam_pos）
			inline point camera_world_pos(object* cam)
			{
				if (camera* rc = dynamic_cast<camera*>(cam))
				{
					const matrix& m = rc->system::coord();
					return point(m(0, 3), m(1, 3), m(2, 3));
				}
				return point(0, 0, 0);
			}

			// 完整 window 矩阵（camera 内聚）
			inline matrix camera_window(object* cam)
			{
				if (camera* rc = dynamic_cast<camera*>(cam))
					return rc->window();
				return matrix(4, 4);
			}

			// 清屏（camera 清屏 + 背景）
			inline void camera_clear(object* cam, rgba c)
			{
				if (camera* rc = dynamic_cast<camera*>(cam)) rc->clear(c);
				else render_clearn(c);
			}
		}

		// ===== theory_shadow =====

		void theory_shadow::declare(frame_graph& graph)
		{
			graph.add_pass("shadow", {}, {}, [this] { render(); });
		}

		void theory_shadow::render()
		{
			if (!m_camera || !m_enabled) return;
			fantasy::object* root = m_camera->root_ptr();
			if (!root) return;
			render_backend& rhi = render_backend_active();

			fantasy::point cam_pos = camera_world_pos(m_camera);

			fantasy::light_desc sun;
			if (m_light_env && !m_light_env->lights().empty())
			{
				// 从场景级光照环境取方向光（优先）
				bool has_dir = false;
				for (const auto& l : m_light_env->lights())
					if (l.type == fantasy::light_type::directional) { sun = l; has_dir = true; break; }
				if (!has_dir)
				{
					sun.type = fantasy::light_type::directional;
					sun.color = 0xffffffff;
					sun.direction = point(0.0f, -0.6f, -0.8f);
				}
			}
			else
			{
				sun = gather_directional(root);
			}

			fantasy::aabb scene_box;
			bool have_box = false;
			for (auto* obj : (*root))
			{
				auto* m_ptr = dynamic_cast<fantasy::models*>(obj);
				if (!m_ptr) continue;
				bool any_shadow = false;
				for (auto* d : m_ptr->handle_drawers())
					if (d && d->shadow()) { any_shadow = true; break; }
				if (!any_shadow) continue;
				fantasy::aabb local = m_ptr->updata_aabb();
				if (!local.good()) continue;
				fantasy::matrix wm = fantasy::system::transform(root, m_ptr);
				fantasy::aabb world = local.access(wm);
				if (!have_box) { scene_box = world; have_box = true; }
				else scene_box.attach(world);
			}
			if (!have_box) return;

			// size() 改过就要重建：原实现只在 !ready() 时 setup，于是 --shadow-size 是空操作——
			// 这曾让我把「耗时与阴影图分辨率无关」当结论（见 DESIGN_NOTES 第十一节量具教训②）。
			if (m_shadow.ready() && m_applied != m_size) m_shadow.reset();
			if (!m_shadow.ready())
			{
				std::string err;
				if (!m_shadow.setup(rhi, m_size, m_size, err)) return;
				m_applied = m_size;
			}
			fantasy::matrix lvp = fantasy::shadow_map::compute_light_view_proj(sun.direction, scene_box);
			m_shadow.light_view_proj(lvp);

			m_shadow.begin();
			rhi.reset_draw_call_count();
			set_all_cascade(root, 0);
			fantasy::system::traverse(lvp, root, fantasy::matrix(4, 4));
			fantasy::models::drawer(root);
			m_shadow.end();

			apply_shadow_state(root, m_shadow, cam_pos, sun, m_light_env);
		}

		// ===== theory_scene =====

		theory_scene::~theory_scene()
		{
			if (m_hdr) { render_backend_active().delete_offscreen(m_hdr); m_hdr = 0; }
		}

		void theory_scene::declare(frame_graph& graph)
		{
			graph.add_pass("scene", {}, {}, [this] { render(); });
		}

		void theory_scene::render()
		{
			if (!m_camera) return;
			m_camera->life();   // orbit 姿态在渲染前应用到 coord
			render_backend& rhi = render_backend_active();

			uint w = 0, h = 0;
			view_wh(m_camera, w, h);
			if (!m_hdr || w != m_w || h != m_h)
			{
				if (m_hdr) rhi.delete_offscreen(m_hdr);
				m_hdr = 0;
				std::string err;
				m_hdr = rhi.create_offscreen(w, h, 1, err, true);   // RGBA16F HDR
				m_w = w;
				m_h = h;
			}
			if (!m_hdr)
			{
				fprintf(stderr, "[scene] HDR offscreen FAIL (w=%u h=%u)\n", w, h);
				return;
			}

			fantasy::object* root = m_camera->root_ptr();
			fantasy::matrix window = camera_window(m_camera);

			// 光照环境广播：把场景级光照环境传给所有 drawer（drawer 优先用它，替代各自 m_lights）
			if (m_light_env)
			{
				for (auto* obj : (*root))
				{
					auto* m_ptr = dynamic_cast<fantasy::models*>(obj);
					if (!m_ptr) continue;
					for (auto* d : m_ptr->handle_drawers()) d->light_env(m_light_env);
				}
			}

			rhi.begin_offscreen(m_hdr);
			camera_clear(m_camera, xhqm_rgba(0, 0, 0, 0));
			fantasy::system::traverse(window, root, fantasy::matrix(4, 4));
			fantasy::models::drawer(root);
			rhi.end_offscreen();

			fg()->set_handle("scene", rhi.offscreen_texture(m_hdr, 0));
		}

		// ===== theory_instancing =====

		void theory_instancing::declare(frame_graph& graph)
		{
			graph.add_pass("instancing", {}, {}, [this] { render(); });
		}

		void theory_instancing::render()
		{
			if (!m_root) return;
			// 广度遍历收集 model_ecs 节点（迭代方式同 models::drawer），读 SoA 批量提交实例数据
			for (auto* object_ptr : (*m_root))
			{
				auto* ecs_ptr = dynamic_cast<fantasy::model_ecs*>(object_ptr);
				if (ecs_ptr) { ecs_ptr->sync_instances(); }   // 版本守卫：数据未变时零开销
			}
		}

		// ===== theory_depth =====

		theory_depth::~theory_depth()
		{
			if (m_target) { render_backend_active().delete_offscreen(m_target); m_target = 0; }
		}

		void theory_depth::declare(frame_graph& graph)
		{
			graph.add_pass("depth", {}, {}, [this] { render(); });
		}

		void theory_depth::render()
		{
			if (!m_camera) return;
			render_backend& rhi = render_backend_active();
			fantasy::object* root = m_camera->root_ptr();
			if (!root) return;

			uint w = 0, h = 0;
			view_wh(m_camera, w, h);
			if (!m_target || w != m_w || h != m_h)
			{
				if (m_target) rhi.delete_offscreen(m_target);
				m_target = 0;
				std::string err;
				m_target = rhi.create_offscreen(w, h, 1, err, true);   // RGBA16F，R 存线性视图深度
				m_w = w; m_h = h;
			}
			if (!m_target) return;

			if (!m_shader)
			{
				const bool vk = (rhi.name()[0] == 'v');
				m_shader = shader(
					shader_preprocess(shader_sources::depth_vert(), vk),
					shader_preprocess(shader_sources::depth_frag(), vk));
			}
			if (!m_shader) return;

			// 相机视角变换：与主渲染一致，depth = clipPos.w = 线性视图深度
			fantasy::matrix window = camera_window(m_camera);
			fantasy::system::traverse(window, root, fantasy::matrix(4, 4));

			rhi.begin_offscreen(m_target);
			rhi.clear(xhqm_rgba(0, 0, 0, 0));   // 背景 depth=0（近），SSAO/SSR 按 depth<=0 跳过

			for (auto* obj : (*root))
			{
				fantasy::models* m = dynamic_cast<fantasy::models*>(obj);
				if (!m || !m->show()) continue;
				for (fantasy::drawer* d : m->handle_drawers())
				{
					prefab* p = dynamic_cast<prefab*>(d);
					if (!p) continue;
					if (!p->vertex_id() || !p->index_id()) continue;

					rhi.use_program(m_shader);
					rhi.uniform_matrix(m_shader, "tranMatrix", p->system().tramat().data(), 1);
					rhi.uniform_int(m_shader, "hasSkin", p->open_skin() ? 1 : 0);
					rhi.uniform_int(m_shader, "hasBlend", p->open_blend() ? 1 : 0);
					rhi.uniform_float(m_shader, "blendWeight", p->blend_weight());
					rhi.uniform_int(m_shader, "hasInstances", p->instances().empty() ? 0 : 1);

					if (p->open_skin() && !p->bone_palette().empty())
					{
						const count max_bones = UBO_MAX_BONES;
						count bone_count = (p->bone_palette().size() < max_bones) ? p->bone_palette().size() : max_bones;
						std::vector<value> bones(bone_count * 16);
						for (count b = 0; b < bone_count; ++b)
						{
							fantasy::matrix t = p->bone_palette()[b].tramat();
							for (fantasy::index i = 0; i < 16; ++i) bones[b * 16 + i] = t.data()[i];
						}
						rhi.uniform_matrix(m_shader, "bones", bones.data(), static_cast<uint>(bone_count));
					}
					if (!p->instances().empty())
					{
						count n = (p->instances().size() < UBO_MAX_INSTANCES) ? p->instances().size() : UBO_MAX_INSTANCES;
						std::vector<value> inst(n * 16);
						for (count i = 0; i < n; ++i)
						{
							fantasy::matrix tr = p->instances()[i].tramat();
							for (fantasy::index k = 0; k < 16; ++k) inst[i * 16 + k] = tr.data()[k];
						}
						rhi.uniform_matrix(m_shader, "instances", inst.data(), static_cast<uint>(n));
					}

					rhi.draw_indexed(p->vertex_id(), p->index_id(), rhi_primitive::triangles, 0, static_cast<uint>(3 * p->size()));
				}
			}
			rhi.use_program(0);
			rhi.end_offscreen();

			fg()->set_handle("depth", rhi.offscreen_texture(m_target, 0));
		}

		// ===== theory_tonemap =====

		void theory_tonemap::declare(frame_graph& graph)
		{
			graph.add_pass("tonemap", {}, {}, [this] { render(); });
		}

		void theory_tonemap::render()
		{
			if (!m_camera) return;
			uint scene = fg()->handle("scene");
			if (!scene) return;
			render_backend& rhi = render_backend_active();

			uint w = 0, h = 0;
			view_wh(m_camera, w, h);
			if (!m_tonemap.ready() || w != m_w || h != m_h)
			{
				if (m_tonemap.ready()) m_tonemap.reset();
				std::string err;
				m_tonemap.setup(rhi, w, h, err);
				m_w = w; m_h = h;
			}
			uint ldr = m_tonemap.apply(scene, m_exposure);
			m_ldr = ldr;
			if (!m_present) return;   // 离屏相机：只留 LDR 句柄，不 present

			// 多相机/多窗口：present 前按本相机视图设置视口（GL 生效；Vulkan begin_frame 已按窗口设整屏，此处为分屏子区域）
			if (m_vp_x >= 0 && m_vp_w > 0 && m_vp_h > 0)
				rhi.viewport(m_vp_x, m_vp_y, (sint)m_vp_w, (sint)m_vp_h);
			else
				rhi.viewport(0, 0, (sint)w, (sint)h);
			m_tonemap.present(ldr);
		}

		// ===== theory_bloom =====

		void theory_bloom::declare(frame_graph& graph)
		{
			graph.add_pass("bloom", {}, {}, [this] { render(); });
		}

		void theory_bloom::render()
		{
			if (!m_camera) return;
			uint scene = fg()->handle("scene");
			if (!scene) return;
			render_backend& rhi = render_backend_active();

			uint w = 0, h = 0;
			view_wh(m_camera, w, h);
			if (!m_bloom.ready() || w != m_w || h != m_h)
			{
				if (m_bloom.ready()) m_bloom.reset();
				std::string err;
				m_bloom.setup(rhi, w, h, err);
				m_w = w; m_h = h;
			}
			m_bloom.threshold(m_threshold);
			m_bloom.intensity(m_intensity);
			uint out = m_bloom.apply(scene);
			fg()->set_handle("scene", out);
		}

		// ===== theory_fxaa =====

		void theory_fxaa::declare(frame_graph& graph)
		{
			graph.add_pass("fxaa", {}, {}, [this] { render(); });
		}

		void theory_fxaa::render()
		{
			if (!m_camera) return;
			uint scene = fg()->handle("scene");
			if (!scene) return;
			render_backend& rhi = render_backend_active();

			uint w = 0, h = 0;
			view_wh(m_camera, w, h);
			if (!m_fxaa.ready() || w != m_w || h != m_h)
			{
				if (m_fxaa.ready()) m_fxaa.reset();
				std::string err;
				m_fxaa.setup(rhi, w, h, err);
				m_w = w; m_h = h;
			}
			uint out = m_fxaa.apply(scene);
			fg()->set_handle("scene", out);
		}

		// ===== theory_ssao =====

		void theory_ssao::declare(frame_graph& graph)
		{
			graph.add_pass("ssao", {}, {}, [this] { render(); });
		}

		void theory_ssao::render()
		{
			if (!m_camera) return;
			uint scene = fg()->handle("scene");
			uint depth = fg()->handle("depth");   // 线性视图深度（theory_depth 提供）
			if (!scene || !depth) return;
			render_backend& rhi = render_backend_active();

			uint w = 0, h = 0;
			view_wh(m_camera, w, h);
			if (!m_ready || w != m_w || h != m_h)
			{
				if (m_ready) { m_ssao.reset(); m_compose.reset(); }
				std::string err;
				m_ready = m_ssao.setup(rhi, w, h, err) && m_compose.setup(rhi, w, h, err);
				m_w = w; m_h = h;
			}
			value aspect = value(w) / value(h);
			value tan_half_fov = std::tan(xhqm::radians(value(22.5)));
			uint ao = m_ssao.apply(depth, aspect, tan_half_fov, m_radius, m_bias);
			if (!ao) return;
			uint composed = m_compose.apply(scene, ao, 0);   // 0=multiply：scene × ao
			fg()->set_handle("scene", composed);
			fg()->set_handle("ao", ao);
		}

		// ===== theory_ssr =====

		void theory_ssr::declare(frame_graph& graph)
		{
			graph.add_pass("ssr", {}, {}, [this] { render(); });
		}

		void theory_ssr::render()
		{
			if (!m_camera) return;
			uint scene = fg()->handle("scene");
			uint depth = fg()->handle("depth");
			if (!scene || !depth) return;
			render_backend& rhi = render_backend_active();

			uint w = 0, h = 0;
			view_wh(m_camera, w, h);
			if (!m_ready || w != m_w || h != m_h)
			{
				if (m_ready) { m_ssr.reset(); m_compose.reset(); }
				std::string err;
				m_ready = m_ssr.setup(rhi, w, h, err) && m_compose.setup(rhi, w, h, err);
				m_w = w; m_h = h;
			}
			value aspect = value(w) / value(h);
			value tan_half_fov = std::tan(xhqm::radians(value(22.5)));
			uint refl = m_ssr.apply(scene, depth, aspect, tan_half_fov, m_step_size, m_bias);
			if (!refl) return;
			uint composed = m_compose.apply(scene, refl, 1);   // 1=add：scene + reflection
			fg()->set_handle("scene", composed);
			fg()->set_handle("ssr", refl);
		}

		// ===== theory_taa =====

		void theory_taa::declare(frame_graph& graph)
		{
			graph.add_pass("taa", {}, {}, [this] { render(); });
		}

		void theory_taa::render()
		{
			if (!m_camera) return;
			uint scene = fg()->handle("scene");
			if (!scene) return;
			render_backend& rhi = render_backend_active();

			uint w = 0, h = 0;
			view_wh(m_camera, w, h);
			if (!m_ready || w != m_w || h != m_h)
			{
				if (m_ready) m_taa.reset();
				std::string err;
				m_ready = m_taa.setup(rhi, w, h, err);
				m_w = w; m_h = h;
				m_history = 0;   // resize 后历史帧失效
			}
			uint history = m_history ? m_history : scene;
			uint out = m_taa.apply(scene, history, m_alpha);
			m_history = out;
			++m_frame;
			fg()->set_handle("scene", out);
		}

		// ===== theory_ibl =====

		void theory_ibl::declare(frame_graph& graph)
		{
			graph.add_pass("ibl", {}, {}, [this] { render(); });
		}

		void theory_ibl::render()
		{
			if (!m_light_env) return;
			if (m_done) { if (m_irradiance) fg()->set_handle("irradiance", m_irradiance); return; }
			render_backend& rhi = render_backend_active();

			// 外部环境贴图（GPU 纹理）：CPU 卷积无法采样，需 GPU 卷积（后续）；本次仅程序化生成。
			if (m_environment_map != 0) return;

			// 1. 环境贴图（equirect 全景图转换，或程序化渐变天空）
			const uint env_size = 32;
			std::array<real, 4> amb = xhqm::color::rgba2array(m_light_env->ambient());
			std::vector<std::vector<rgba>> env(6);
			if (!m_equirect.empty() && m_equirect_w > 0 && m_equirect_h > 0)
			{
				// equirectangular → cubemap（方向采样，用 vect 归一化）
				for (uint f = 0; f < 6; ++f)
				{
					env[f].resize(env_size * env_size);
					for (uint y = 0; y < env_size; ++y)
						for (uint x = 0; x < env_size; ++x)
						{
							value u = ((value)x + 0.5f) / (value)env_size;
							value v = ((value)y + 0.5f) / (value)env_size;
							value a = u * 2.0f - 1.0f, b = v * 2.0f - 1.0f;
							vect<value> d;
							if (f == 0) d = vect<value>(1, -b, -a);
							else if (f == 1) d = vect<value>(-1, -b, a);
							else if (f == 2) d = vect<value>(a, 1, b);
							else if (f == 3) d = vect<value>(a, -1, -b);
							else if (f == 4) d = vect<value>(a, -b, 1);
							else d = vect<value>(-a, -b, -1);
							d = d.normal();
							value theta = std::acos(std::max(-1.0f, std::min(1.0f, d.y())));
							value phi = std::atan2(d.z(), d.x());
							const value TAU = 2.0f * (value)xhqm::pi;
							value eu = phi / TAU + 0.5f;
							value ev = theta / (value)xhqm::pi;
							uint ex = (uint)(eu * (value)(m_equirect_w - 1));
							uint ey = (uint)(ev * (value)(m_equirect_h - 1));
							env[f][y * env_size + x] = m_equirect[ey][ex];
						}
				}
			}
			else
			{
				for (uint f = 0; f < 6; ++f)
				{
					env[f].resize(env_size * env_size);
					for (uint y = 0; y < env_size; ++y)
						for (uint x = 0; x < env_size; ++x)
						{
							value t = (value)y / (value)(env_size - 1);
							std::array<real, 4> px = {
								(0.15f + 0.6f * t) * amb[0],
								(0.2f + 0.65f * t) * amb[1],
								(0.35f + 0.65f * t) * amb[2],
								1.0f
							};
							env[f][y * env_size + x] = xhqm::color::array2rgba(px);
						}
				}
			}
			std::string err;
			m_environment_map = rhi.texture_create_cubemap(env, env_size, err);   // 环境贴图（specular 反射采样源）

			// 2. CPU 侧辐照度卷积（半球余弦加权采样，方向 → 面/uv 映射采样环境）
			const uint irr_size = m_size;
			const int NS = 32;
			const value PI = 3.14159265f;
			std::vector<std::vector<rgba>> irr(6);
			for (uint f = 0; f < 6; ++f)
			{
				irr[f].resize(irr_size * irr_size);
				for (uint y = 0; y < irr_size; ++y)
					for (uint x = 0; x < irr_size; ++x)
					{
						value u = ((value)x + 0.5f) / (value)irr_size;
						value v = ((value)y + 0.5f) / (value)irr_size;
						value a = u * 2.0f - 1.0f, b = v * 2.0f - 1.0f;
						value nx, ny, nz;
						if (f == 0) { nx = 1; ny = -b; nz = -a; }
						else if (f == 1) { nx = -1; ny = -b; nz = a; }
						else if (f == 2) { nx = a; ny = 1; nz = b; }
						else if (f == 3) { nx = a; ny = -1; nz = -b; }
						else if (f == 4) { nx = a; ny = -b; nz = 1; }
						else { nx = -a; ny = -b; nz = -1; }
						value len = std::sqrt(nx * nx + ny * ny + nz * nz);
						nx /= len; ny /= len; nz /= len;

						value ux, uy, uz;
						if (std::fabs(ny) < 0.999f) { ux = 0; uy = 1; uz = 0; }
						else { ux = 1; uy = 0; uz = 0; }
						value rx = uy * nz - uz * ny, ry = uz * nx - ux * nz, rz = ux * ny - uy * nx;
						len = std::sqrt(rx * rx + ry * ry + rz * rz); rx /= len; ry /= len; rz /= len;
						value vx = ny * rz - nz * ry, vy = nz * rx - nx * rz, vz = nx * ry - ny * rx;

						value acc_r = 0, acc_g = 0, acc_b = 0;
						for (int i = 0; i < NS; ++i)
						{
							value phi = 2.0f * PI * (value)i * 0.618034f;
							value theta = std::acos(1.0f - ((value)i + 0.5f) / (value)NS);
							value st = std::sin(theta), ct = std::cos(theta);
							value sp = std::sin(phi), cp = std::cos(phi);
							value dx = st * cp * rx + st * sp * vx + ct * nx;
							value dy = st * cp * ry + st * sp * vy + ct * ny;
							value dz = st * cp * rz + st * sp * vz + ct * nz;
							value ax2 = std::fabs(dx), ay2 = std::fabs(dy), az2 = std::fabs(dz);
							uint sf; value sc, tc, ma;
							if (ax2 >= ay2 && ax2 >= az2) { sf = (dx > 0) ? 0u : 1u; ma = ax2; sc = (dx > 0) ? -dz : dz; tc = -dy; }
							else if (ay2 >= az2) { sf = (dy > 0) ? 2u : 3u; ma = ay2; sc = dx; tc = (dy > 0) ? dz : -dz; }
							else { sf = (dz > 0) ? 4u : 5u; ma = az2; sc = (dz > 0) ? dx : -dx; tc = -dy; }
							value su = (sc / ma + 1.0f) * 0.5f;
							value sv = (tc / ma + 1.0f) * 0.5f;
							uint tx = (uint)(su * (value)(env_size - 1));
							uint ty = (uint)(sv * (value)(env_size - 1));
							std::array<real, 4> cc = xhqm::color::rgba2array(env[sf][ty * env_size + tx]);
							acc_r += cc[0] * ct; acc_g += cc[1] * ct; acc_b += cc[2] * ct;
						}
						std::array<real, 4> out = {
							std::min(1.0f, acc_r * PI / (value)NS),
							std::min(1.0f, acc_g * PI / (value)NS),
							std::min(1.0f, acc_b * PI / (value)NS),
							1.0f
						};
						irr[f][y * irr_size + x] = xhqm::color::array2rgba(out);
					}
			}
			m_irradiance = rhi.texture_create_cubemap(irr, irr_size, err);
			m_light_env->irradiance(m_irradiance);   // 回写光照环境，供 drawer 绑定采样

			// 3. CPU 预过滤卷积（specular IBL，固定 roughness=0.5 简化；GGX 重要性采样）
			{
				const uint pf_size = 64;
				const int PFS = 32;
				const value roughness = 0.5f;
				const value a = roughness * roughness;
				std::vector<std::vector<rgba>> pf(6);
				for (uint f = 0; f < 6; ++f)
				{
					pf[f].resize(pf_size * pf_size);
					for (uint y = 0; y < pf_size; ++y)
						for (uint x = 0; x < pf_size; ++x)
						{
							value u = ((value)x + 0.5f) / (value)pf_size;
							value v = ((value)y + 0.5f) / (value)pf_size;
							value aa = u * 2.0f - 1.0f, bb = v * 2.0f - 1.0f;
							value nx, ny, nz;
							if (f == 0) { nx = 1; ny = -bb; nz = -aa; }
							else if (f == 1) { nx = -1; ny = -bb; nz = aa; }
							else if (f == 2) { nx = aa; ny = 1; nz = bb; }
							else if (f == 3) { nx = aa; ny = -1; nz = -bb; }
							else if (f == 4) { nx = aa; ny = -bb; nz = 1; }
							else { nx = -aa; ny = -bb; nz = -1; }
							value len = std::sqrt(nx * nx + ny * ny + nz * nz);
							nx /= len; ny /= len; nz /= len;
							// 切线基（半球 up 取 +Z，退化改 +X）
							value ux, uy, uz;
							if (std::fabs(nz) < 0.999f) { ux = 0; uy = 0; uz = 1; }
							else { ux = 1; uy = 0; uz = 0; }
							value rx = uy * nz - uz * ny, ry = uz * nx - ux * nz, rz = ux * ny - uy * nx;
							len = std::sqrt(rx * rx + ry * ry + rz * rz); rx /= len; ry /= len; rz /= len;
							value vx = ny * rz - nz * ry, vy = nz * rx - nx * rz, vz = nx * ry - ny * rx;

							value acc_r = 0, acc_g = 0, acc_b = 0, wsum = 0;
							for (int i = 0; i < PFS; ++i)
							{
								value phi = 2.0f * PI * (value)i * 0.618034f;
								value xi = ((value)i + 0.5f) / (value)PFS;
								value cosT = std::sqrt((1.0f - xi) / (1.0f + (a * a - 1.0f) * xi));
								value sinT = std::sqrt(std::max(0.0f, 1.0f - cosT * cosT));
								value hx = std::cos(phi) * sinT, hy = std::sin(phi) * sinT, hz = cosT;
								value hwx = hx * rx + hy * vx + hz * nx;
								value hwy = hx * ry + hy * vy + hz * ny;
								value hwz = hx * rz + hy * vz + hz * nz;
								value ndoth = std::max(0.0f, hwx * nx + hwy * ny + hwz * nz);
								value lx = 2.0f * ndoth * hwx - nx;
								value ly = 2.0f * ndoth * hwy - ny;
								value lz = 2.0f * ndoth * hwz - nz;
								value ndotl = lx * nx + ly * ny + lz * nz;
								if (ndotl > 0.0f)
								{
									value ax2 = std::fabs(lx), ay2 = std::fabs(ly), az2 = std::fabs(lz);
									uint sf; value sc, tc, ma;
									if (ax2 >= ay2 && ax2 >= az2) { sf = (lx > 0) ? 0u : 1u; ma = ax2; sc = (lx > 0) ? -lz : lz; tc = -ly; }
									else if (ay2 >= az2) { sf = (ly > 0) ? 2u : 3u; ma = ay2; sc = lx; tc = (ly > 0) ? lz : -lz; }
									else { sf = (lz > 0) ? 4u : 5u; ma = az2; sc = (lz > 0) ? lx : -lx; tc = -ly; }
									value su = (sc / ma + 1.0f) * 0.5f, sv = (tc / ma + 1.0f) * 0.5f;
									uint tx = (uint)(su * (value)(env_size - 1)), ty = (uint)(sv * (value)(env_size - 1));
									std::array<real, 4> cc = xhqm::color::rgba2array(env[sf][ty * env_size + tx]);
									acc_r += cc[0] * ndotl; acc_g += cc[1] * ndotl; acc_b += cc[2] * ndotl; wsum += ndotl;
								}
							}
							value inv = wsum > 1e-6f ? 1.0f / wsum : 0.0f;
							std::array<real, 4> out = {
								std::min(1.0f, acc_r * inv), std::min(1.0f, acc_g * inv), std::min(1.0f, acc_b * inv), 1.0f
							};
							pf[f][y * pf_size + x] = xhqm::color::array2rgba(out);
						}
				}
				m_prefiltered = rhi.texture_create_cubemap(pf, pf_size, err);
				m_light_env->prefiltered(m_prefiltered);
			}

			m_done = true;
			fg()->set_handle("irradiance", m_irradiance);
			fg()->set_handle("prefiltered", m_prefiltered);
		}

		// ===== theory_gizmo_sync =====

		void theory_gizmo_sync::declare(frame_graph& graph)
		{
			graph.add_pass("gizmo_sync", {}, {}, [this] { render(); });
		}

		void theory_gizmo_sync::render()
		{
			if (!m_camera || !m_gizmo) return;
			// 相机世界摆放矩阵（camera 用 coord）
			matrix cam_world = matrix(4, 4);
			if (camera* rc = dynamic_cast<camera*>(m_camera)) cam_world = rc->system::coord();
			else return;
			// 相机纯旋转 → gizmo 局部 = 屏幕固定位置 × 逆旋转（让三轴指向世界）
			matrix cam_rot = xhqm::extract_rote(cam_world);
			m_gizmo->system::coord(xhqm::translate(m_screen_pos.x(), m_screen_pos.y(), m_screen_pos.z()) * cam_rot.invmat());
		}

		// ===== 默认管线组装 =====

		theory_frame_graph* setup_pipeline_for(object* cam, uint flags)
		{
			if (!cam) return nullptr;

			theory_frame_graph* fg = new theory_frame_graph();
			auto add = [&](theory_pass* p)
			{
				p->fg(fg);
				fg->register_pass(p);   // 预注册：fg->run() 首帧即可执行
			};

			add(new theory_shadow(cam));
			if (flags & PIPE_INSTANCING) add(new theory_instancing(cam->root_ptr()));   // ECS 批量提交先于 scene 执行
			add(new theory_scene(cam));
			if ((flags & (PIPE_SSAO | PIPE_SSR)) != 0) add(new theory_depth(cam));
			if (flags & PIPE_SSAO) add(new theory_ssao(cam));
			if (flags & PIPE_SSR) add(new theory_ssr(cam));
			if (flags & PIPE_BLOOM) add(new theory_bloom(cam));
			if (flags & PIPE_FXAA) add(new theory_fxaa(cam));
			if (flags & PIPE_TAA) add(new theory_taa(cam));
			add(new theory_tonemap(cam));
			return fg;
		}

		theory_frame_graph* setup_default_pipeline(world* w, uint flags)
		{
			if (!w) return nullptr;
			theory_frame_graph* fg = setup_pipeline_for(w->camera(), flags);
			if (!fg) return nullptr;
			// 挂 world theory 树：alive() 按挂载序先跑各 pass 的 life()，fg 殿后 compile+execute
			for (theory_pass* p : fg->passes()) w->add_theory(p);
			w->add_theory(fg);
			return fg;
		}
	}
}
