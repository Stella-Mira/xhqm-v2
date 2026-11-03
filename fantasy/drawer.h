#pragma once
#ifndef XHQM_FANTASY_DRAWER
#define XHQM_FANTASY_DRAWER

#include "fantasy.h"
#include "colors.h"
#include "backend.h"
#include "light.h"
#include <memory>

std::string xhqm_io _xhqm_render_error_(const char* file, int line);
#define xhqm_render_error _xhqm_render_error_(__FILE__, __LINE__)

namespace xhqm
{
	namespace fantasy
	{
		fantasy::id xhqm_io render_id();
		fantasy::uint xhqm_io render_type();
		fantasy::uint xhqm_io render_size();
		void xhqm_io render_finish();
		void xhqm_io render_clearn(xhqm::rgba rgba);
		void xhqm_io render_window(sint x, sint y, sint w, sint h);
		void xhqm_io render_winding(bool clockwise);
		void xhqm_io render_pixels(sint x, sint y, uint* rgba_id);
		void xhqm_io render_pixels(sint x, sint y, sint w, sint h, uint* rgba_id);

		struct xhqm_io aabb
		{
			point min;
			point max;
		public:
			bool good();
			aabb stand();
			aabb attach(const aabb& box);
			aabb attach(const point& pot);
			aabb access(const matrix& mat);
		};

		class xhqm_io _vertex
		{
			fantasy::id _shader_vao_id = 0;
			std::string _creat_error = u8"";
		public:
			_vertex() {};

			_vertex(_vertex&& move) noexcept;

			_vertex(const _vertex& copy) = delete;

			_vertex(const fantasy::values& attrib, const std::vector<fantasy::uint>& locations);

			~_vertex();

			void reset(const fantasy::values& attrib, const std::vector<fantasy::uint>& locations);

			void reset();


			void operator =(const _vertex& copy) = delete;
			bool operator =(_vertex&& move) noexcept;
			operator fantasy::id() const
			{
				return _shader_vao_id;
			}
		};

		class xhqm_io _shader
		{
			fantasy::id _shader_id = 0;
			std::string _vert_shader = u8"";
			std::string _frag_shader = u8"";
			std::string _creat_error = u8"";
		public:
			_shader() {};

			_shader(_shader&& move) noexcept;

			_shader(const _shader& copy) = delete;

			_shader(const std::string& vert, const std::string& frag);

			~_shader();

			void reset(const std::string& vert, const std::string& frag);

			void reset();

			// 预编译二进制：直接加载（不做源码编译）
			bool reset(const shader_binary& binary);
			// 提取已编译二进制（用于缓存）
			bool binary(shader_binary& out) const;
			// 编译/加载是否成功
			bool good(void) const;
			// 最近一次错误信息
			const std::string& error(void) const;

			void operator =(const _shader& copy) = delete;
			bool operator =(_shader&& move) noexcept;
			operator fantasy::id() const
			{
				return _shader_id;
			}
		};

		class xhqm_io _texture
		{
			fantasy::id _texture_id = 0;
			std::string _creat_error = u8"";
		public:
			_texture() {};

			_texture(_texture&& move)  noexcept;

			_texture(const _texture& copy) = delete;

			_texture(const fantasy::rgba& color); //纯色纹理 2*2

			_texture(const std::vector<fantasy::rgba>& scale); //刻度纹理 2*100

			_texture(const std::vector<std::vector<fantasy::rgba>>& image); //图片纹理 2*100

			~_texture();

			void reset();
			void reset(const fantasy::rgba& color);
			void reset(const std::vector<fantasy::rgba>& scale);
			void reset(const std::vector<std::vector<fantasy::rgba>>& image);

			void operator =(const _texture& copy) = delete;
			bool operator =(_texture&& move)  noexcept;
			operator fantasy::id() const
			{
				return _texture_id;
			}
		};

		using vertex = fantasy::resource<fantasy::_vertex, fantasy::id>;

		using shader = fantasy::resource<fantasy::_shader, fantasy::id>;

		using texture = fantasy::resource<fantasy::_texture, fantasy::id>;

		// 材质描述（可共享：std::shared_ptr 引用计数）。
		// base_color / metallic / roughness / ao / alpha / emissive 进 per-material UBO；
		// normal_map / ao_map 是可选项（法线贴图 TBN 在 M1 接）。
		struct xhqm_io material_desc
		{
			fantasy::rgba base_color = 0xffffffff;  // 基础色（默认白，乘面颜色，兼容旧路径）
			value metallic = 0.0f;                   // 金属度
			value roughness = 0.5f;                  // 粗糙度
			fantasy::rgba emissive = 0x00000000;     // 自发光
			value ao = 1.0f;                         // 环境光遮蔽
			value alpha = 1.0f;                      // 不透明度
			bool double_sided = false;               // 双面
			fantasy::texture normal_map;             // 法线贴图（0=无）
			fantasy::texture ao_map;                 // AO 贴图（0=无）
			// 卡通渲染参数（toon 着色路径使用；PBR 路径忽略）
			value ramp_threshold = 0.5f;             // 明暗分档阈值（halfLambert 空间）
			value ramp_softness = 0.08f;             // 分档过渡带宽（0=硬边两档）
			value rim_power = 3.0f;                  // 边缘光锐度（fresnel 幂次，越大越窄）
			value spec_threshold = 0.0f;             // 卡通高光阈值（<=0 关闭高光）
			fantasy::rgba rim_color = 0x00000000;    // 边缘光颜色（rgb）与强度（a，0=关闭）
		};

		using material = std::shared_ptr<fantasy::material_desc>;
	}

	namespace fantasy
	{
		class xhqm_io drawer
		{
		protected:
			inline static fantasy::shader _select_shader;  //默认shader
		protected:
			bool _is_select = false;
			bool m_open_light = 1;
			light* m_light_env = nullptr;   // 场景级光照环境（可选，优先于 m_lights）
			bool m_shadow = true;              // 参与方向光阴影（投影/接收）；UI 辅助器（gizmo 等）可关
			fantasy::matrix m_system;
			fantasy::matrix m_world;              // 模型世界矩阵（多光源世界坐标用）
			std::vector<fantasy::light_desc> m_lights; // 多光源列表
			uint m_cascade_count = 0;                    // 级联阴影级数（0=关）
			std::array<uint, 4> m_shadow_texs = {};     // 每级深度纹理句柄
			std::vector<fantasy::matrix> m_light_vps;  // 每级光空间 view-proj
			std::array<value, 4> m_cascade_splits = {}; // 每级 far 分割距离
			fantasy::point m_camera_pos = point(0, 0, 0); // 相机世界位置
			value m_exposure = 1;      // 色彩分级：曝光
			value m_contrast = 1;      // 色彩分级：对比度
			value m_saturation = 1;    // 色彩分级：饱和度
			int m_tonemap = 0;         // 色调映射：0=Reinhard 1=ACES filmic
			fantasy::rgba m_ambient = 0x505050; // IBL 环境光颜色（提亮：无光面也可见，避免暗剪影）
			value m_reflection = 1;    // IBL 镜面反射强度
			fantasy::material m_material;  // 材质（可共享，引用计数）
			fantasy::values m_attrib;
			fantasy::point m_lignt_pos;
			fantasy::rgba m_color_lignt;
			fantasy::aabb _aabb_size;
			fantasy::real _poit_size = 1;
			fantasy::real _line_size = 1;
			fantasy::real _face_size = 1;
			fantasy::rgba _colour = 0x0;
			fantasy::rgba _select = 0x0;
			fantasy::vertex _vertex_id;
			fantasy::shader _shader_id;
		protected:
			virtual void _aabb_(const xhqm::count& location = 3);
			virtual void _render_id_() = 0;
		public:
			static const fantasy::shader& select_shader();
		public:
			explicit drawer();

			// 虚析析：drawer 有多条派生链（prefab / toon_prefab / …），而它们**是**通过 `drawer*` 被 delete 的。
			// models::insert_drawer 存 vector<drawer*>，models::delete_drawers 按基类指针 delete；
			// 基类析析非虚 ⇒ 未定义行为。实测（macOS / Apple clang 21 / arm64 / Release）：
			// delete 一个 `new prefab()` 直接 SIGTRAP（`brk #0x1`，栈停在 ~models → delete_drawers 内），且过程**零输出**，极难定位。
			virtual ~drawer();
			
			virtual void shader() = 0;
			virtual void updata() = 0;
			virtual void render() = 0;
		public:
			void render_id(const fantasy::uint& color_id);
		public:
			inline void access(const matrix& mat) { m_system = m_system * mat; }

			inline const matrix& system(void) const { return m_system; }
			inline void system(const matrix& value) { m_system = value; }
			inline const matrix& world(void) const { return m_world; }
			inline void world(const matrix& value) { m_world = value; }
			inline const std::vector<fantasy::light_desc>& lights(void) const { return m_lights; }
			inline void lights(const std::vector<fantasy::light_desc>& value) { m_lights = value; }
			inline uint cascade_count(void) const { return m_cascade_count; }
			inline void cascade_count(uint value) { m_cascade_count = value; }
			inline std::array<uint, 4>& shadow_texs(void) { return m_shadow_texs; }
			inline std::vector<fantasy::matrix>& light_vps(void) { return m_light_vps; }
			inline std::array<value, 4>& cascade_splits(void) { return m_cascade_splits; }
			inline const fantasy::point& camera_pos(void) const { return m_camera_pos; }
			inline void camera_pos(const fantasy::point& value) { m_camera_pos = value; }
			inline value exposure(void) const { return m_exposure; }
			inline void exposure(value v) { m_exposure = v; }
			inline value contrast(void) const { return m_contrast; }
			inline void contrast(value v) { m_contrast = v; }
			inline value saturation(void) const { return m_saturation; }
			inline void saturation(value v) { m_saturation = v; }
			inline int tonemap(void) const { return m_tonemap; }
			inline void tonemap(int v) { m_tonemap = v; }
			inline const fantasy::rgba& ambient(void) const { return m_ambient; }
			inline void ambient(const fantasy::rgba& v) { m_ambient = v; }
			inline value reflection(void) const { return m_reflection; }
			inline void reflection(value v) { m_reflection = v; }
			inline const fantasy::material& material(void) const { return m_material; }
			inline void material(const fantasy::material& value) { m_material = value; }
			inline bool get_select(void) const { return _is_select; }
			inline void set_select(bool value) { _is_select = value; }
			bool open_light(void) const { return m_open_light; };
			void open_light(bool value) { m_open_light = value; };
			inline light* light_env(void) const { return m_light_env; }
			inline void light_env(light* value) { m_light_env = value; }
			bool shadow(void) const { return m_shadow; };
			void shadow(bool value) { m_shadow = value; };

			inline real poit_size(void) const { return _poit_size; }
			inline void poit_size(real value) { _poit_size = value; }
			inline real line_size(void) const { return _line_size; }
			inline void line_size(real value) { _line_size = value; }
			inline real face_size(void) const { return _face_size; }
			inline void face_size(real value) { _face_size = value; }
			inline aabb aabb_size(void) const { return _aabb_size; }
			inline void aabb_size(aabb value) { _aabb_size = value; }

			inline fantasy::rgba texture_colour(void) const { return _colour; }
			inline void texture_colour(fantasy::rgba value) { _colour = value; }
			inline fantasy::rgba texture_select(void) const { return _select; }
			inline void texture_select(fantasy::rgba value) { _select = value; }

			inline const fantasy::vertex& vertex_id(void) const { return _vertex_id; }
			inline void vertex_id(const fantasy::vertex& value) { _vertex_id = value; }
			inline const fantasy::shader& shader_id(void) const { return _shader_id; }
			inline void shader_id(const fantasy::shader& value) { _shader_id = value; }

			inline const xhqm::rgba& color_lignt(void) const { return m_color_lignt; };
			inline void color_lignt(const xhqm::rgba& value) { m_color_lignt = value; };
			inline const xhqm::fantasy::point& lignt_pos(void) const { return m_lignt_pos; };
			inline void lignt_pos(const xhqm::fantasy::point& value) { m_lignt_pos = value; };
		};

		// 多目标 blendshape（M9）：一个形变目标 = 位置位移 + 法线形变（每顶点）
		struct xhqm_io blend_target
		{
			std::vector<std::array<value, 3>> delta;         // 位置位移（每顶点，size = 三角形数*3）
			std::vector<std::array<value, 3>> normal_delta;  // 法线形变（每顶点）
		};

		class xhqm_io prefab : public drawer
		{
			inline static fantasy::shader _shader;  //默认shader
		protected:
			bool m_open_poit = 1;
			bool m_open_line = 1;
			bool m_open_face = 1;
			xhqm::count m_size = 0;//三角形个数
			std::array<bool, 10> m_open_attrib = {};
			std::vector<std::array<point, 3>> m_vertex;
			std::vector<std::array<value, 4>> m_colour;
			std::vector<std::array<value, 3>> m_normal;
			std::vector<std::array<value, 2>> m_flipuv;
			std::vector<std::array<value, 1>> m_values;
			// 柔性蒙皮（LBS）：骨骼索引以浮点存储，着色器内 int() 取整
			bool m_open_skin = 0;
			std::vector<std::array<value, 4>> m_skin_index;	// 每顶点 4 个骨骼索引
			std::vector<std::array<value, 4>> m_skin_weight;	// 每顶点 4 个骨骼权重
			std::vector<matrix> m_bone_palette;				// 骨骼矩阵调色板（模型空间）
			// 形变（blendshape）：单通道位移，每顶点一个 vec3
			bool m_open_blend = 0;
			value m_blend_weight = 0;
			std::vector<std::array<value, 3>> m_blend_delta;	// 每顶点形变位移
			std::vector<std::array<value, 3>> m_blend_normal_delta;	// 每顶点法线形变位移（M9 blendshape 法线）
			// 多目标 blendshape（M9）：N 个目标 + N 个权重，updata 时 CPU 混合进 m_blend_delta
			std::vector<blend_target> m_blend_targets;
			std::vector<value> m_blend_target_weights;
			std::vector<std::array<value, 3>> m_tangent;	// 每顶点切线（法线贴图 TBN）
			std::vector<uint> m_index;	// 索引缓冲（面 + 线，M2 weld 去重后生成）
			fantasy::id m_index_id = 0;	// 索引缓冲句柄
			std::vector<matrix> m_instances;	// 实例变换列表（instancing，空 = 非实例化）
			std::array<fantasy::texture, 3> m_texture_layer;
			xhqm::fantasy::rgba m_color_poit = 0xff000000;
			xhqm::fantasy::rgba m_color_line = 0xff000000;
			xhqm::fantasy::rgba m_color_face = 0xffffffff;
		public:
			virtual ~prefab();
			virtual void shader() override;
			virtual void updata() override;
			virtual void render() override;
		protected:
			virtual void _render_id_() override;
		public:
			const xhqm::count& size(void) const { return m_size; };
			void size(const xhqm::count& value) { m_size = value; };
			const bool& open_poit(void) const { return m_open_poit; };
			void open_poit(const bool& value) { m_open_poit = value; };
			const bool& open_line(void) const { return m_open_line; };
			void open_line(const bool& value) { m_open_line = value; };
			const bool& open_face(void) const { return m_open_face; };
			void open_face(const bool& value) { m_open_face = value; };

			const xhqm::rgba& color_poit(void) const { return m_color_poit; };
			void color_poit(const xhqm::rgba& value) { m_color_poit = value; };
			const xhqm::rgba& color_line(void) const { return m_color_line; };
			void color_line(const xhqm::rgba& value) { m_color_line = value; };
			const xhqm::rgba& color_face(void) const { return m_color_face; };
			void color_face(const xhqm::rgba& value) { m_color_face = value; };

			const std::vector<std::array<point, 3>>& vertex(void) const { return m_vertex; };
			const std::vector<std::array<value, 4>>& colour(void) const { return m_colour; };
			const std::vector<std::array<value, 3>>& normal(void) const { return m_normal; };
			const std::vector<std::array<value, 2>>& flipuv(void) const { return m_flipuv; };
			const std::vector<std::array<value, 1>>& values(void) const { return m_values; };
			const std::array<fantasy::texture, 3>& textures(void) const { return m_texture_layer; };
			bool vertex(const std::vector<std::array<point, 3>>& value) 
			{ 
				bool good = (value.size() == m_size * 1); if (good) { m_vertex = value; } return good;
			};
			bool colour(const std::vector<std::array<value, 4>>& value)
			{
				bool good = (value.size() == m_size * 3); if (good) { m_colour = value; } return good;
			};
			bool normal(const std::vector<std::array<value, 3>>& value)
			{
				bool good = (value.size() == m_size * 3); if (good) { m_normal = value; } return good;
			};
			bool flipuv(const std::vector<std::array<value, 2>>& value)
			{
				bool good = (value.size() == m_size * 3); if (good) { m_flipuv = value; } return good;
			};
			bool values(const std::vector<std::array<value, 1>>& value)
			{
				bool good = (value.size() == m_size * 3); if (good) { m_values = value; } return good;
			};
			const bool& open_skin(void) const { return m_open_skin; };
			void open_skin(const bool& value) { m_open_skin = value; };
			const bool& open_blend(void) const { return m_open_blend; };
			void open_blend(const bool& value) { m_open_blend = value; };
			const value& blend_weight(void) const { return m_blend_weight; };
			void blend_weight(const value& value) { m_blend_weight = value; };
			const std::vector<std::array<value, 3>>& blend_delta(void) const { return m_blend_delta; };
			bool blend_delta(const std::vector<std::array<value, 3>>& value)
			{
				bool good = (value.size() == m_size * 3); if (good) { m_blend_delta = value; } return good;
			};
			const std::vector<std::array<value, 3>>& blend_normal_delta(void) const { return m_blend_normal_delta; };
			bool blend_normal_delta(const std::vector<std::array<value, 3>>& value)
			{
				bool good = (value.size() == m_size * 3); if (good) { m_blend_normal_delta = value; } return good;
			};
			// 多目标 blendshape（M9）：设置 N 个形变目标（每目标 delta + normal_delta 尺寸须为 m_size*3）
			bool blend_targets(const std::vector<blend_target>& value)
			{
				for (const auto& t : value)
					if (t.delta.size() != m_size * 3 || t.normal_delta.size() != m_size * 3) return false;
				m_blend_targets = value;
				return true;
			};
			void blend_target_weights(const std::vector<value>& value) { m_blend_target_weights = value; };
			const std::vector<blend_target>& blend_targets(void) const { return m_blend_targets; };
			const std::vector<value>& blend_target_weights(void) const { return m_blend_target_weights; };
			const std::vector<std::array<value, 3>>& tangent(void) const { return m_tangent; };
			bool tangent(const std::vector<std::array<value, 3>>& value)
			{
				bool good = (value.size() == m_size * 3); if (good) { m_tangent = value; } return good;
			};
			const std::vector<std::array<value, 4>>& skin_index(void) const { return m_skin_index; };
			const std::vector<std::array<value, 4>>& skin_weight(void) const { return m_skin_weight; };
			const std::vector<matrix>& bone_palette(void) const { return m_bone_palette; };
			void bone_palette(const std::vector<matrix>& value) { m_bone_palette = value; };
			// 索引缓冲句柄（theory_depth 深度 pass 直接绘制用）
			const fantasy::id& index_id(void) const { return m_index_id; };
			const std::vector<matrix>& instances(void) const { return m_instances; };
			void instances(const std::vector<matrix>& value) { m_instances = value; };
			bool skin_index(const std::vector<std::array<value, 4>>& value)
			{
				bool good = (value.size() == m_size * 3); if (good) { m_skin_index = value; } return good;
			};
			bool skin_weight(const std::vector<std::array<value, 4>>& value)
			{
				bool good = (value.size() == m_size * 3); if (good) { m_skin_weight = value; } return good;
			};
			// M3 静态批处理：合并多个同一局部坐标系、同一属性布局、同一材质的静态 prefab 为单个 prefab。
			// 不做矩阵烘焙（调用方负责把输入置于同一坐标系，如单模型多个子网格）；仅拼接几何，
			// 复用 updata 的 weld 去重，draw call 由 N 降为 1。返回新 prefab（调用方负责释放）；
			// 输入为空 / 布局不一致 / 含蒙皮·形变·实例化时返回 nullptr。
			static prefab* batch_static(const std::vector<prefab*>& sources);
			// 由绑定矩阵与当前矩阵计算皮肤调色板：palette[i] = pose[i] * inv(bind[i])
			// 列向量约定（与着色器 tranMatrix * vec4 一致），标准 LBS 蒙皮矩阵
			inline static std::vector<matrix> skin_palette(const std::vector<matrix>& bind, const std::vector<matrix>& pose)
			{
				std::vector<matrix> palette;
				palette.reserve(bind.size());
				for (xhqm::index i = 0; i < bind.size() && i < pose.size(); ++i)
				{
					palette.push_back(pose[i] * bind[i].invmat());
				}
				return palette;
			};
			void texture_layer_0(const fantasy::texture& texture)
			{
				m_texture_layer[0] = texture;
			}
			void texture_layer_1(const fantasy::texture& texture)
			{
				m_texture_layer[1] = texture;
			}
			void texture_layer_2(const fantasy::texture& texture)
			{
				m_texture_layer[2] = texture;
			}
		};

		//toon_prefab：卡通渲染网格（prefab 的着色变体）
		//几何/蒙皮/形变/实例化与 prefab 完全一致，仅片元走 ramp 分档光照 + 硬边高光 + 边缘光；
		//材质卡通参数见 material_desc 的 ramp_*/rim_*/spec_threshold 字段。
		class xhqm_io toon_prefab : public prefab
		{
			inline static fantasy::shader _toon_shader;      // 卡通默认 shader（静态共享）
			inline static bool _toon_shader_ready = false;   // none 后端 id 恒 0，用就绪标志避免重复创建
		public:
			virtual ~toon_prefab();
			virtual void shader() override;
		};

		class xhqm_io sprite : public drawer
		{
			inline static fantasy::shader _shader;  //默认shader
		protected:
			xhqm::count m_size = 0;//三角形个数
			std::array<bool, 3> m_open_attrib{};
			xhqm::fantasy::texture m_texture;
			xhqm::fantasy::value m_layer = 0; //z值
			xhqm::fantasy::rgba m_color = 0xffffffff;
			std::vector<std::array<point, 3>> m_vertex;
			std::vector<std::array<value, 4>> m_colour;
			std::vector<std::array<value, 2>> m_flipuv;
		public:
			virtual void shader() override;
			virtual void updata() override;
			virtual void render() override;
		protected:
			virtual void _render_id_() override;
		public:
			const xhqm::count& size(void) const { return m_size; };
			void size(const xhqm::count& value) { m_size = value; };
			const fantasy::rgba& color(void) const { return m_color; };
			void color(const fantasy::rgba& value) { m_color = value; };
			const fantasy::value& layer(void) const { return m_layer; };
			void layer(const fantasy::value& value) { m_layer = value; };

			const std::vector<std::array<point, 3>>& vertex(void) const { return m_vertex; };
			const std::vector<std::array<value, 4>>& colour(void) const { return m_colour; };
			const std::vector<std::array<value, 2>>& flipuv(void) const { return m_flipuv; };
			const fantasy::texture& texture(void) const { return m_texture; };
			bool vertex(const std::vector<std::array<point, 3>>& value)
			{
				bool good = (value.size() == m_size * 1); if (good) { m_vertex = value; } return good;
			};
			bool colour(const std::vector<std::array<value, 4>>& value)
			{
				bool good = (value.size() == m_size * 3); if (good) { m_colour = value; } return good;
			};
			bool flipuv(const std::vector<std::array<value, 2>>& value)
			{
				bool good = (value.size() == m_size * 3); if (good) { m_flipuv = value; } return good;
			};
			void texture(const fantasy::texture& texture)
			{
				m_texture = texture;
			}
		};
	}
}

#endif