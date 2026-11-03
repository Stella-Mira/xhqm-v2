#pragma once
#ifndef XHQM_FANTASY_RHI_NO
#define XHQM_FANTASY_RHI_NO

// 渲染后端抽象（RHI）：把渲染上下文 / 着色器 / 顶点 / 纹理 / 绘制收敛到一个虚接口，
// OpenGL 与 Vulkan 各为独立翻译单元实现，运行时经 render_backend_active() 调度。
#include "fantasy.h"
#include "backend.h"

namespace xhqm
{
	namespace fantasy
	{
		// 图元类型
		enum class rhi_primitive : uint
		{
			points = 0,		// 点
			lines = 1,		// 线
			triangles = 2,	// 三角形
			quads = 3,		// 四边形
			triangle_fan = 4,// 三角扇
		};

		// 采样器描述（M4 sampler 抽象）：替换散落的硬编码采样参数
		struct sampler_desc
		{
			bool linear_min = true;    // min filter：线性/最近
			bool linear_mag = true;    // mag filter：线性/最近
			bool mipmap = true;        // 是否使用 mipmap
			bool clamp = true;         // clamp-to-edge / repeat
			bool compare = false;      // 深度比较（阴影采样）
			float max_lod = 0.0f;      // 最大 LOD（0=自动）
			float anisotropy = 1.0f;   // 各向异性
		};

		// 渲染后端抽象接口
		class xhqm_io render_backend
		{
		public:
			virtual ~render_backend() = default;

			virtual const char* name() const noexcept = 0;
			virtual bool supports_binary() const noexcept = 0;

			// ---- 上下文 ----
			virtual bool init(std::string& error) = 0;
			virtual uint value_type() = 0;	// 顶点数值类型（GL_FLOAT 等）
			virtual uint value_size() = 0;	// sizeof(value)
			virtual void finish() = 0;
			virtual void clear(rgba color) = 0;
			virtual void viewport(sint x, sint y, sint w, sint h) = 0;
			virtual void winding(bool clockwise) = 0;
			virtual void read_pixels(sint x, sint y, sint w, sint h, uint* out) = 0;

			// ---- 帧与表面（Vulkan 需要；GL 为 no-op）----
			// native_window：Windows 下为 HWND，Linux/XCB 下为 X11 Window id
			// 多窗口：每个窗口一份 swapchain 状态；同窗口重复 create = 激活 + 尺寸变化时重建
			virtual bool create_surface(void* native_window, uint w, uint h, std::string& error) = 0;
			// 销毁指定窗口的表面（nullptr = 销毁全部 / 单窗口退化为原语义）
			virtual void destroy_surface(void* native_window = nullptr) = 0;
			virtual void begin_frame(std::string& error) = 0;
			virtual void end_frame() = 0;

			// ---- 着色器 ----
			virtual uint shader_create(const std::string& vert, const std::string& frag, std::string& error) = 0;
			virtual uint shader_create(const shader_binary& bin, std::string& error) = 0;
			virtual void shader_delete(uint id) = 0;
			virtual bool shader_get_binary(uint id, shader_binary& out) = 0;

			// ---- 顶点缓冲 ----
			virtual uint vertex_create(const values& attrib, const std::vector<uint>& locations, std::string& error) = 0;
			virtual void vertex_delete(uint id) = 0;
			// 动态更新顶点数据（M2 streaming：原地 glBufferSubData / staging copy，替换整块重建）
			virtual void vertex_update(uint id, const values& attrib) = 0;

			// ---- 索引缓冲 ----
			virtual uint index_create(const std::vector<uint>& indices, std::string& error) = 0;
			virtual void index_delete(uint id) = 0;

			// ---- 纹理 ----
			virtual uint texture_create(const std::vector<rgba>& flat, uint w, uint h, std::string& error) = 0;
			// BC1 压缩纹理（M4）：bc1 为 ceil(w/4)*ceil(h/4)*8 字节块流
			virtual uint texture_create_compressed(const std::vector<std::uint8_t>& bc1, uint w, uint h, std::string& error) = 0;
			// 浮点纹理（M9 GPU 蒙皮调色板 / SSAO / Hi-Z）：R32G32B32A32F，flat 每像素 4 个 value
			virtual uint texture_create_float(const std::vector<value>& flat, uint w, uint h, std::string& error) = 0;
			// 更新浮点纹理内容（同尺寸，每像素 4 value）
			virtual void texture_update(uint id, const std::vector<value>& flat) = 0;
			virtual uint texture_create_cubemap(const std::vector<std::vector<rgba>>& faces, uint size, std::string& error) = 0;
			virtual void texture_delete(uint id) = 0;
			// bindless 纹理句柄（M3）：GL 返回 ARB_bindless_texture 64 位句柄并驻留，Vulkan 返回纹理 id（descriptor 索引代理）
			virtual std::uint64_t texture_bindless_handle(uint id) = 0;

			// ---- 渲染状态 ----
			virtual void blend(bool enable) = 0;
			virtual void depth_test(bool enable, bool less_equal) = 0;
			virtual void color_write(bool enable) = 0;   // 颜色写入开关（遮挡查询 pass 关闭）
			virtual void stencil_write(uint ref, uint mask) = 0;   // 开启测试，REPLACE 写入 ref（描边/遮罩写入）
			virtual void stencil_test(uint ref, uint mask) = 0;    // 开启测试，EQUAL 比较 ref
			virtual void stencil_disable() = 0;                    // 关闭 stencil 测试
			virtual void set_msaa(uint samples) = 0;               // 设置 MSAA 采样数（1=关闭，需在 create_surface 前调用）

			// ---- 离屏渲染目标（MRT 多颜色附件，用于后处理/阴影）----
			virtual uint create_offscreen(uint w, uint h, uint color_count, std::string& error, bool hdr = false) = 0;
			virtual void delete_offscreen(uint id) = 0;
			virtual void begin_offscreen(uint id) = 0;   // 切换到离屏 framebuffer
			virtual void end_offscreen() = 0;            // 回到 swapchain
			// 取离屏颜色附件纹理句柄（供 bind_texture 采样，做后处理/阴影采样；attachment < color_count）
			virtual uint offscreen_texture(uint id, uint attachment) = 0;
			// 取离屏深度纹理句柄（供阴影采样；仅 color_count==0 的深度专用目标有效）
			virtual uint offscreen_depth_texture(uint id) = 0;
			// 立方体深度离屏（M6 点光阴影）：size×size 六面深度纹理，供 6 面渲染 + 阴影采样
			virtual uint create_offscreen_cubemap_depth(uint size, std::string& error) = 0;
			virtual void delete_offscreen_cubemap_depth(uint id) = 0;
			// 立方体颜色离屏（M10 IBL）：size×size 六面 RGBA16F 颜色纹理，供 6 面渲染（环境贴图/辐照度/预过滤卷积）。
			// mip_levels：预过滤（specular IBL）需多 mip（每级对应一个粗糙度），1 为单 mip。
			virtual uint create_offscreen_cubemap(uint size, std::string& error, uint mip_levels = 1) = 0;
			virtual void delete_offscreen_cubemap(uint id) = 0;
			virtual void begin_offscreen_cubemap(uint id, uint face, uint mip = 0) = 0;   // face 0..5（+X/-X/+Y/-Y/+Z/-Z）
			virtual uint offscreen_cubemap_texture(uint id) = 0;            // 取立方体纹理句柄（供 bind_texture）
			// 读离屏颜色附件 0 的单个像素（x,y 为左下角原点；拾取用）
			virtual void read_offscreen(uint id, sint x, sint y, uint* out) = 0;

			// ---- 统计 ----
			virtual uint draw_call_count() = 0;         // 返回累计 draw 调用次数
			virtual void reset_draw_call_count() = 0;   // 重置计数
			virtual void use_program(uint program) = 0;
			virtual void bind_texture(uint unit, uint texture) = 0;
			virtual void uniform_matrix(uint program, const char* name, const value* data, uint count) = 0; // 列主序
			virtual void uniform_vec4(uint program, const char* name, value x, value y, value z, value w) = 0;
			virtual void uniform_vec3(uint program, const char* name, value x, value y, value z) = 0;
			virtual void uniform_int(uint program, const char* name, int v) = 0;
			virtual void uniform_float(uint program, const char* name, value v) = 0;
			// 多光源：上传 count 个光源（data 每光 16 个 value，见 light.h LIGHT_BYTES）
			virtual void uniform_lights(uint program, uint count, const value* data) = 0;
			virtual void point_size(value s) = 0;
			virtual void line_size(value s) = 0;
			virtual void draw(uint vertex, rhi_primitive prim, uint first, uint count) = 0;
			virtual void draw_indexed(uint vertex, uint index, rhi_primitive prim, uint first, uint count) = 0;
			virtual void draw_instanced(uint vertex, rhi_primitive prim, uint first, uint count, uint instances) = 0;
			virtual void draw_indexed_instanced(uint vertex, uint index, rhi_primitive prim, uint first, uint count, uint instances) = 0;

			// indirect draw（M3/M7 GPU 驱动）：indirect buffer 存 DrawIndexedIndirectCommand（每命令 5 个 uint）
			virtual uint indirect_create(const std::vector<uint>& commands, std::string& error) = 0;
			virtual void indirect_delete(uint id) = 0;
			virtual void draw_indexed_indirect(uint vertex, uint index, uint indirect, uint offset, uint count) = 0;
			// 从存储缓冲（SSBO）作为 indirect 命令源绘制（compute 剔除结果直接消费）
			virtual void draw_indexed_indirect_storage(uint vertex, uint index, uint storage_id, uint byte_offset, uint count) = 0;

			// ---- 计算着色器 ----
			virtual uint shader_compute_create(const std::string& comp, std::string& error) = 0;
			virtual void shader_compute_delete(uint id) = 0;
			virtual void compute_dispatch(uint program, uint x, uint y, uint z) = 0;

			// ---- 存储缓冲（SSBO，M3/M9 GPU 驱动：compute 读对象 AABB / 写 indirect 命令 / 骨骼调色板）----
			virtual uint storage_create(size_t bytes, const void* data, std::string& error) = 0;
			virtual void storage_delete(uint id) = 0;
			virtual void storage_update(uint id, const void* data, size_t bytes) = 0;
			// 读回存储缓冲内容（CPU 侧读取，验证/调试用；调用前须确保 GPU 写入已完成）
			virtual void storage_read(uint id, void* out, size_t bytes) = 0;
			// 绑定存储缓冲到 compute program 的 binding 点（compute_dispatch 前调用）
			virtual void compute_storage_bind(uint program, uint binding, uint storage_id) = 0;

			// ---- GPU 查询（遮挡查询：统计通过深度测试的样本数）----
			virtual uint query_create(std::string& error) = 0;
			virtual void query_delete(uint id) = 0;
			virtual void query_begin(uint id) = 0;   // 记录到命令流：开始统计
			virtual void query_end(uint id) = 0;     // 结束统计
			virtual uint query_result(uint id) = 0;  // 同步读回样本数

			// ---- GPU 时间戳（per-pass 计时；GL_TIMESTAMP / vkCmdWriteTimestamp）----
			virtual uint timestamp_query_create(std::string& error) = 0;
			virtual void timestamp_query_delete(uint id) = 0;
			virtual void timestamp_record(uint id) = 0;              // 命令流当前点打时间戳
			virtual std::uint64_t timestamp_result(uint id) = 0;     // 同步读回纳秒时间戳
		};

		// 运行时后端选择与调度
		xhqm_io render_backend& render_backend_active();
		xhqm_io void render_backend_select(backend_kind kind);
		// 后端析构时退役：active 指针切到 none 桩（防止 DLL 加载期静态资源
		// 在后端之后析构时调用已销毁后端的悬空 map）
		xhqm_io void render_backend_retire(render_backend* self);
		// 显式关闭后端：active 切到 none 桩。GL 上下文的生命周期短于进程静态
		// shader（inline static _shader 在静态析构阶段仍会调 shader_delete），
		// 须在 GL 上下文销毁前切 none，让后续删除变 no-op，避免无上下文崩溃。
		xhqm_io void render_backend_shutdown();
	}
}

#endif
