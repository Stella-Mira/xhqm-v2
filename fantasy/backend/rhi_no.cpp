#include "rhi_no.h"
#if defined(BUILD_WITH_OPENGL)
#include "rhi_gl.h"
#endif
#if defined(BUILD_WITH_VULKAN)
#include "rhi_vk.h"
#endif

namespace xhqm
{
	namespace fantasy
	{
		namespace
		{
			// 无后端桩（no/cpu/net 设备时使用）
			class none_backend : public render_backend
			{
			public:
				virtual const char* name() const noexcept override { return "none"; }
				virtual bool supports_binary() const noexcept override { return false; }
				virtual bool init(std::string&) override { return false; }
				virtual uint value_type() override { return 0; }
				virtual uint value_size() override { return sizeof(fantasy::value); }
				virtual void finish() override {}
				virtual void clear(rgba) override {}
				virtual void viewport(sint, sint, sint, sint) override {}
				virtual void winding(bool) override {}
				virtual void read_pixels(sint, sint, sint, sint, uint*) override {}
				virtual bool create_surface(void*, uint, uint, std::string&) override { return false; }
				virtual void destroy_surface(void*) override {}
				virtual void begin_frame(std::string&) override {}
				virtual void end_frame() override {}
				virtual uint shader_create(const std::string&, const std::string&, std::string&) override { return 0; }
				virtual uint shader_create(const shader_binary&, std::string&) override { return 0; }
				virtual void shader_delete(uint) override {}
				virtual bool shader_get_binary(uint, shader_binary&) override { return false; }
				virtual uint vertex_create(const values&, const std::vector<uint>&, std::string&) override { return 0; }
				virtual void vertex_delete(uint) override {}
				virtual void vertex_update(uint, const values&) override {}
				virtual uint index_create(const std::vector<uint>&, std::string&) override { return 0; }
				virtual void index_delete(uint) override {}
				virtual uint texture_create(const std::vector<rgba>&, uint, uint, std::string&) override { return 0; }
				virtual uint texture_create_compressed(const std::vector<std::uint8_t>&, uint, uint, std::string&) override { return 0; }
				virtual uint texture_create_float(const std::vector<value>&, uint, uint, std::string&) override { return 0; }
				virtual void texture_update(uint, const std::vector<value>&) override {}
				virtual uint texture_create_cubemap(const std::vector<std::vector<rgba>>&, uint, std::string&) override { return 0; }
				virtual void texture_delete(uint) override {}
				virtual std::uint64_t texture_bindless_handle(uint) override { return 0; }
				virtual void blend(bool) override {}
				virtual void depth_test(bool, bool) override {}
				virtual void color_write(bool) override {}
				virtual void stencil_write(uint, uint) override {}
				virtual void stencil_test(uint, uint) override {}
				virtual void stencil_disable() override {}
				virtual void set_msaa(uint) override {}
				virtual uint create_offscreen(uint, uint, uint, std::string&, bool) override { return 0; }
				virtual void delete_offscreen(uint) override {}
				virtual void begin_offscreen(uint) override {}
				virtual void end_offscreen() override {}
				virtual uint offscreen_texture(uint, uint) override { return 0; }
				virtual uint offscreen_depth_texture(uint) override { return 0; }
				virtual void read_offscreen(uint, sint, sint, uint*) override {}
				virtual uint create_offscreen_cubemap_depth(uint, std::string&) override { return 0; }
				virtual void delete_offscreen_cubemap_depth(uint) override {}
				virtual uint create_offscreen_cubemap(uint, std::string&, uint) override { return 0; }
				virtual void delete_offscreen_cubemap(uint) override {}
				virtual void begin_offscreen_cubemap(uint, uint, uint) override {}
				virtual uint offscreen_cubemap_texture(uint) override { return 0; }
				virtual uint draw_call_count() override { return 0; }
				virtual void reset_draw_call_count() override {}
				virtual void use_program(uint) override {}
				virtual void bind_texture(uint, uint) override {}
				virtual void uniform_matrix(uint, const char*, const value*, uint) override {}
				virtual void uniform_vec4(uint, const char*, value, value, value, value) override {}
				virtual void uniform_vec3(uint, const char*, value, value, value) override {}
				virtual void uniform_int(uint, const char*, int) override {}
				virtual void uniform_float(uint, const char*, value) override {}
				virtual void uniform_lights(uint, uint, const value*) override {}
				virtual void point_size(value) override {}
				virtual void line_size(value) override {}
				virtual void draw(uint, rhi_primitive, uint, uint) override {}
				virtual void draw_indexed(uint, uint, rhi_primitive, uint, uint) override {}
				virtual void draw_instanced(uint, rhi_primitive, uint, uint, uint) override {}
				virtual void draw_indexed_instanced(uint, uint, rhi_primitive, uint, uint, uint) override {}
				virtual uint indirect_create(const std::vector<uint>&, std::string&) override { return 0; }
				virtual void indirect_delete(uint) override {}
				virtual void draw_indexed_indirect(uint, uint, uint, uint, uint) override {}
				virtual void draw_indexed_indirect_storage(uint, uint, uint, uint, uint) override {}
				virtual uint shader_compute_create(const std::string&, std::string&) override { return 0; }
				virtual void shader_compute_delete(uint) override {}
				virtual void compute_dispatch(uint, uint, uint, uint) override {}
				virtual uint storage_create(size_t, const void*, std::string&) override { return 0; }
				virtual void storage_delete(uint) override {}
				virtual void storage_update(uint, const void*, size_t) override {}
				virtual void storage_read(uint, void*, size_t) override {}
				virtual void compute_storage_bind(uint, uint, uint) override {}
				virtual uint query_create(std::string&) override { return 0; }
				virtual void query_delete(uint) override {}
				virtual void query_begin(uint) override {}
				virtual void query_end(uint) override {}
				virtual uint query_result(uint) override { return 0; }
				virtual uint timestamp_query_create(std::string&) override { return 0; }
				virtual void timestamp_query_delete(uint) override {}
				virtual void timestamp_record(uint) override {}
				virtual std::uint64_t timestamp_result(uint) override { return 0; }
			};

			render_backend*& active_ptr()
			{
				static render_backend* active = nullptr;
				return active;
			}

			none_backend& none_instance()
			{
				static none_backend none;
				return none;
			}

			// 后端初始化探测：init() 成功才算可用，失败释放避免泄漏
			bool try_init_backend(render_backend* backend, std::string& error)
			{
				if (!backend) return false;
				if (backend->init(error)) return true;
				fprintf(stderr, "[backend] init failed (%s): %s\n", backend->name(), error.c_str());
				delete backend;
				return false;
			}

			// 按编译宏优先级创建并初始化第一个成功的后端：vk → gl → none
			render_backend* create_auto_backend()
			{
				std::string error;
#if defined(BUILD_WITH_VULKAN)
				if (auto* vk = create_vk_backend(); try_init_backend(vk, error))
					return vk;
#endif
#if defined(BUILD_WITH_OPENGL)
				if (auto* gl = create_gl_backend(); try_init_backend(gl, error))
					return gl;
#endif
				return &none_instance();
			}

			// 单后端探测：指定 vk 或 gl，失败回退 none
			render_backend* create_single_backend(backend_kind kind)
			{
				std::string error;
#if defined(BUILD_WITH_VULKAN)
				if (kind == backend_kind::vulkan)
					if (auto* vk = create_vk_backend(); try_init_backend(vk, error))
						return vk;
#endif
#if defined(BUILD_WITH_OPENGL)
				if (kind == backend_kind::opengl)
					if (auto* gl = create_gl_backend(); try_init_backend(gl, error))
						return gl;
#endif
				return &none_instance();
			}
		}

		render_backend& render_backend_active()
		{
			if (!active_ptr())
			{
				const auto device = xhqm::hardware::active_device();
				if (device == xhqm::hardware::device::no ||
				    device == xhqm::hardware::device::cpu ||
				    device == xhqm::hardware::device::net)
				{
					// no/cpu/net：仅 none 桩
					active_ptr() = &none_instance();
				}
				else if (device == xhqm::hardware::device::gpu)
				{
					// gpu：按编译期后端单探测（BACKEND=VULKAN→vk→none，BACKEND=OPENGL→gl→none）
					// 若编译了双后端（BACKEND=AUTO），走完整回退链
#if defined(BUILD_WITH_VULKAN) && defined(BUILD_WITH_OPENGL)
					active_ptr() = create_auto_backend();
#else
					active_ptr() = create_single_backend(compiled_backend());
#endif
				}
				else
				{
					// auto：完整回退链 vk → gl → none
					active_ptr() = create_auto_backend();
				}
			}
			return *active_ptr();
		}

		void render_backend_select(backend_kind kind)
		{
			render_backend* target = nullptr;
			if (kind == backend_kind::none) target = &none_instance();
#if defined(BUILD_WITH_VULKAN)
			if (kind == backend_kind::vulkan) target = create_vk_backend();
#endif
#if defined(BUILD_WITH_OPENGL)
			if (kind == backend_kind::opengl) target = create_gl_backend();
#endif
			if (target) active_ptr() = target;
		}

		void render_backend_retire(render_backend* self)
		{
			if (active_ptr() == self) active_ptr() = &none_instance();
		}

		void render_backend_shutdown()
		{
			active_ptr() = &none_instance();
		}
	}
}
