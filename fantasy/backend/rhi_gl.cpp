#include "rhi_gl.h"
#include "colors.h"
#include "ubo.h"

#include <unordered_map>
#include <unordered_set>
#include <cstring>
#include <cstdio>

//#define GLEW_STATIC
#include <GL/glew.h>
// macOS 下 GL 头在 OpenGL.framework（<OpenGL/gl.h>），非 Darwin 用 <GL/gl.h>
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

namespace xhqm
{
	namespace fantasy
	{
		class gl_backend : public render_backend
		{
			bool m_init = false;
			std::unordered_map<uint, uint> m_vao_vbo;	// VAO -> VBO（顶点删除时一并释放）
			std::unordered_map<uint, uint> m_vao_size;	// VAO -> 字节数（vertex_update 越界护栏）
			std::unordered_map<uint, uint> m_ebo;		// index id -> EBO
			uint m_index_counter = 0;
			std::unordered_map<uint, uint> m_indirect;	// indirect id -> GL indirect buffer
			uint m_indirect_counter = 0;
			std::unordered_map<uint, uint> m_queries;	// query id -> GL query object
			uint m_query_counter = 0;
			std::unordered_map<uint, uint> m_timestamp_queries;	// timestamp id -> GL query object
			uint m_timestamp_counter = 0;
			std::unordered_map<uint, uint> m_computes;	// compute id -> GL program
			uint m_compute_counter = 0;
			std::unordered_map<uint, std::pair<GLuint, GLsizeiptr>> m_storages;	// storage id -> {SSBO, 字节数}
			uint m_storage_counter = 0;
			struct tex_meta { uint w = 0, h = 0; bool is_float = false; };
			std::unordered_map<uint, tex_meta> m_tex_meta;	// 纹理 id -> 元数据（texture_update 用）
			struct offscreen_gl
			{
				uint fbo = 0;
				uint w = 0, h = 0;          // 离屏尺寸（begin_offscreen 须按它设视口，阴影图 2048² 与窗口不同）
				std::vector<uint> colors;   // 颜色附件纹理（可采样）
				uint depth_rb = 0;          // 颜色离屏的深度 renderbuffer
				uint depth_tex = 0;         // 深度专用离屏（阴影图）的可采样深度纹理
			};
			std::unordered_map<uint, offscreen_gl> m_offscreens;	// offscreen id -> FBO 结构
			uint m_offscreen_counter = 0;
			struct cubemap_gl
			{
				uint fbo = 0;      // FBO
				uint tex = 0;      // 立方体纹理
				uint size = 0;
				uint mip_levels = 1;
				bool color = false;   // true=颜色（RGBA16F），false=深度（阴影）
			};
			std::unordered_map<uint, cubemap_gl> m_cubemaps;	// cubemap id -> 结构
			uint m_cubemap_counter = 0;
			std::unordered_set<uint> m_cubemap_texs;	// 立方体纹理 GL 句柄集（bind_texture 判定目标）
			uint m_draw_calls = 0;   // 累计 draw 调用次数
			uint m_current_program = 0;   // 当前 program（draw 时查 UBO 用）
			GLint m_prev_fbo = 0;         // begin_offscreen 前保存的帧缓冲，end_offscreen 恢复
			GLint m_prev_viewport[4] = { 0, 0, 0, 0 };   // begin_offscreen 前保存的视口，end_offscreen 恢复

			// 每个 program 的 UBO：把逐名 uniform 收敛为一次 std140 上传
			struct program_ubo_gl
			{
				GLuint ubo = 0;
				std::vector<char> data;
				bool dirty = false;
			};
			std::unordered_map<uint, program_ubo_gl> m_program_ubos;

			static GLenum prim_of(rhi_primitive p)
			{
				switch (p)
				{
				case rhi_primitive::points: return GL_POINTS;
				case rhi_primitive::lines: return GL_LINES;
				case rhi_primitive::triangles: return GL_TRIANGLES;
				case rhi_primitive::quads: return GL_QUADS;
				case rhi_primitive::triangle_fan: return GL_TRIANGLE_FAN;
				default: return GL_TRIANGLES;
				}
			}

			// 给已链接的 program 建 UBO 并绑定到 binding 0（着色器无 UBO 块则跳过）
			void setup_ubo(GLuint prog)
			{
				GLint idx = glGetUniformBlockIndex(prog, "UBO");
				if (idx == GL_INVALID_INDEX) return;
				glUniformBlockBinding(prog, idx, 0);
				program_ubo_gl p;
				p.data.assign(UBO_SIZE, 0);
				glGenBuffers(1, &p.ubo);
				glBindBuffer(GL_UNIFORM_BUFFER, p.ubo);
				glBufferData(GL_UNIFORM_BUFFER, UBO_SIZE, nullptr, GL_DYNAMIC_DRAW);
				glBindBuffer(GL_UNIFORM_BUFFER, 0);
				m_program_ubos[prog] = std::move(p);
			}

			// draw 前上传脏 UBO 并绑定到 binding 0
			void bind_ubo()
			{
				auto it = m_program_ubos.find(m_current_program);
				if (it == m_program_ubos.end()) return;
				if (it->second.dirty)
				{
					glBindBuffer(GL_UNIFORM_BUFFER, it->second.ubo);
					glBufferSubData(GL_UNIFORM_BUFFER, 0, UBO_SIZE, it->second.data.data());
					it->second.dirty = false;
				}
				glBindBufferRange(GL_UNIFORM_BUFFER, 0, it->second.ubo, 0, UBO_SIZE);
			}

		public:
			virtual const char* name() const noexcept override { return "opengl"; }
			virtual bool supports_binary() const noexcept override { return true; }

			virtual bool init(std::string& error) override
			{
				if (m_init) return true;
				GLenum err = glewInit();
				if (err != GLEW_OK)
				{
					error = reinterpret_cast<const char*>(glewGetErrorString(err));
					fprintf(stderr, "[xhqm GL] glewInit failed: %s\n", error.c_str());
					return false;
				}
				fprintf(stderr, "[xhqm GL] glewInit OK, version %s\n", (const char*)glGetString(GL_VERSION));
				GLint max_ubo = 0;
				glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &max_ubo);
				if ((uint)max_ubo < UBO_SIZE)
				{
					fprintf(stderr, "[xhqm GL] warning: GL_MAX_UNIFORM_BLOCK_SIZE=%d < required UBO_SIZE=%u\n", max_ubo, (uint)UBO_SIZE);
				}
				GLint gl_major = 0, gl_minor = 0;
				glGetIntegerv(GL_MAJOR_VERSION, &gl_major);
				glGetIntegerv(GL_MINOR_VERSION, &gl_minor);
				if (gl_major < 3 || (gl_major == 3 && gl_minor < 3))
				{
					fprintf(stderr, "[xhqm GL] warning: GL context %d.%d < 3.3, shaders use #version 330 core\n", gl_major, gl_minor);
				}
				glEnable(GL_DEPTH_TEST);
				glActiveTexture(GL_TEXTURE0);
				glEnable(GL_BLEND);
				glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
				glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
				glEnable(GL_TEXTURE_2D);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
				m_init = true;
				return true;
			}

			virtual uint value_type() override { return GL_FLOAT; }
			virtual uint value_size() override { return sizeof(fantasy::value); }

			virtual void finish() override { glFinish(); }
			virtual void clear(rgba color) override
			{
				std::array<xhqm::real, 4> c = xhqm::color::rgba2array(color);
				glClearColor(c[0], c[1], c[2], c[3]);
				glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			}
			virtual void viewport(sint x, sint y, sint w, sint h) override { glViewport(x, y, w, h); }
			virtual void winding(bool clockwise) override { glFrontFace(clockwise ? GL_CW : GL_CCW); }
			virtual void read_pixels(sint x, sint y, sint w, sint h, uint* out) override
			{
				glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, out);
			}

			// GL 无需显式表面与帧边界，即时渲染
			virtual bool create_surface(void*, uint, uint, std::string&) override { return true; }
			virtual void destroy_surface(void*) override {}
			virtual void begin_frame(std::string&) override {}
			virtual void end_frame() override {}

			virtual uint shader_create(const std::string& vert, const std::string& frag, std::string& error) override
			{
				GLint success = 0;
				GLchar log[1024];

				GLuint vs = glCreateShader(GL_VERTEX_SHADER);
				const char* vsrc = vert.c_str();
				glShaderSource(vs, 1, &vsrc, NULL);
				glCompileShader(vs);
				glGetShaderiv(vs, GL_COMPILE_STATUS, &success);
				if (!success)
				{
					glGetShaderInfoLog(vs, sizeof(log), NULL, log);
					error = "Vertex shader compilation error: " + std::string(log);
					fprintf(stderr, "[xhqm GL] %s\n", error.c_str());
					glDeleteShader(vs);
					return 0;
				}

				GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
				const char* fsrc = frag.c_str();
				glShaderSource(fs, 1, &fsrc, NULL);
				glCompileShader(fs);
				glGetShaderiv(fs, GL_COMPILE_STATUS, &success);
				if (!success)
				{
					glGetShaderInfoLog(fs, sizeof(log), NULL, log);
					error = "Fragment shader compilation error: " + std::string(log);
					fprintf(stderr, "[xhqm GL] %s\n", error.c_str());
					glDeleteShader(vs);
					glDeleteShader(fs);
					return 0;
				}

				GLuint prog = glCreateProgram();
				glAttachShader(prog, vs);
				glAttachShader(prog, fs);
				glLinkProgram(prog);
				glGetProgramiv(prog, GL_LINK_STATUS, &success);
				if (!success)
				{
					glGetProgramInfoLog(prog, sizeof(log), NULL, log);
					error = "Shader program linking error: " + std::string(log);
					fprintf(stderr, "[xhqm GL] %s\n", error.c_str());
					glDeleteShader(vs);
					glDeleteShader(fs);
					glDeleteProgram(prog);
					return 0;
				}
				glDeleteShader(vs);
				glDeleteShader(fs);
				setup_ubo(prog);
				return prog;
			}

			virtual uint shader_create(const shader_binary& bin, std::string& error) override
			{
				if (bin.blob.empty()) { error = "empty shader binary"; return 0; }
				GLuint prog = glCreateProgram();
				glProgramBinary(prog, bin.format, bin.blob.data(), (GLsizei)bin.blob.size());
				GLint success = 0;
				glGetProgramiv(prog, GL_LINK_STATUS, &success);
				if (!success)
				{
					GLchar log[1024];
					glGetProgramInfoLog(prog, sizeof(log), NULL, log);
					error = "Shader binary load error: " + std::string(log);
					glDeleteProgram(prog);
					return 0;
				}
				setup_ubo(prog);
				return prog;
			}

			virtual void shader_delete(uint id) override
			{
				auto it = m_program_ubos.find(id);
				if (it != m_program_ubos.end())
				{
					if (it->second.ubo) glDeleteBuffers(1, &it->second.ubo);
					m_program_ubos.erase(it);
				}
				if (id) glDeleteProgram(id);
			}

			virtual bool shader_get_binary(uint id, shader_binary& out) override
			{
				if (!id) return false;
				GLint length = 0;
				glGetProgramiv(id, GL_PROGRAM_BINARY_LENGTH, &length);
				if (length <= 0) return false;
				std::vector<unsigned char> blob(static_cast<size_t>(length));
				GLenum format = 0;
				glGetProgramBinary(id, length, NULL, &format, blob.data());
				out.format = static_cast<std::uint32_t>(format);
				out.blob.swap(blob);
				out.backend = "opengl";
				return true;
			}

			virtual uint vertex_create(const values& attrib, const std::vector<uint>& locations, std::string& error) override
			{
				GLuint vao = 0, vbo = 0;
				glGenVertexArrays(1, &vao);
				glGenBuffers(1, &vbo);

				glBindVertexArray(vao);
				glBindBuffer(GL_ARRAY_BUFFER, vbo);
				glBufferData(GL_ARRAY_BUFFER, attrib.size() * sizeof(fantasy::value), attrib.data(), GL_STATIC_DRAW);

				uint stride = 0;
				for (auto loc : locations) stride += loc;
				uint offset = 0;
				for (uint i = 0; i < (uint)locations.size(); ++i)
				{
					if (locations[i] == 0) continue;
					glVertexAttribPointer(i, locations[i], GL_FLOAT, GL_FALSE,
						(GLsizei)(stride * sizeof(fantasy::value)), (void*)(size_t)offset);
					glEnableVertexAttribArray(i);
					offset += locations[i] * sizeof(fantasy::value);
				}

				glBindBuffer(GL_ARRAY_BUFFER, 0);
				glBindVertexArray(0);
				m_vao_vbo[vao] = vbo;
				m_vao_size[vao] = (uint)(attrib.size() * sizeof(fantasy::value));
				return vao;
			}

			virtual void vertex_update(uint id, const values& attrib) override
			{
				auto it = m_vao_vbo.find(id);
				if (it == m_vao_vbo.end()) return;
				GLsizeiptr bytes = (GLsizeiptr)attrib.size() * (GLsizeiptr)sizeof(fantasy::value);
				auto sit = m_vao_size.find(id);
				GLsizeiptr cap = (sit != m_vao_size.end()) ? (GLsizeiptr)sit->second : 0;
				if (bytes <= 0 || bytes > cap) return;
				glBindBuffer(GL_ARRAY_BUFFER, it->second);
				glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, attrib.data());
				glBindBuffer(GL_ARRAY_BUFFER, 0);
			}

			virtual void vertex_delete(uint id) override
			{
				if (!id) return;
				auto it = m_vao_vbo.find(id);
				if (it != m_vao_vbo.end())
				{
					GLuint vbo = it->second;
					glDeleteBuffers(1, &vbo);
					m_vao_vbo.erase(it);
				}
				m_vao_size.erase(id);
				glDeleteVertexArrays(1, &id);
			}

			virtual uint index_create(const std::vector<uint>& indices, std::string& error) override
			{
				GLuint ebo = 0;
				glGenBuffers(1, &ebo);
				glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
				glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint), indices.data(), GL_STATIC_DRAW);
				glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
				uint id = ++m_index_counter;
				m_ebo[id] = ebo;
				return id;
			}

			virtual uint indirect_create(const std::vector<uint>& commands, std::string& error) override
			{
				GLuint buf = 0;
				glGenBuffers(1, &buf);
				glBindBuffer(GL_DRAW_INDIRECT_BUFFER, buf);
				glBufferData(GL_DRAW_INDIRECT_BUFFER, commands.size() * sizeof(uint), commands.data(), GL_DYNAMIC_DRAW);
				glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
				uint id = ++m_indirect_counter;
				m_indirect[id] = buf;
				return id;
			}

			virtual void indirect_delete(uint id) override
			{
				if (!id) return;
				auto it = m_indirect.find(id);
				if (it != m_indirect.end()) { glDeleteBuffers(1, &it->second); m_indirect.erase(it); }
			}

			virtual void draw_indexed_indirect(uint vertex, uint index, uint indirect, uint offset, uint count) override
			{
				bind_ubo();
				glBindVertexArray(vertex);
				auto iit = m_ebo.find(index);
				if (iit != m_ebo.end()) glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iit->second);
				auto dit = m_indirect.find(indirect);
				if (dit != m_indirect.end())
				{
					glBindBuffer(GL_DRAW_INDIRECT_BUFFER, dit->second);
					glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, (void*)(size_t)(offset * sizeof(uint)), (GLsizei)count, 0);
				}
				glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
				glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
				glBindVertexArray(0);
			}

			virtual void draw_indexed_indirect_storage(uint vertex, uint index, uint storage_id, uint byte_offset, uint count) override
			{
				bind_ubo();
				glBindVertexArray(vertex);
				auto iit = m_ebo.find(index);
				if (iit != m_ebo.end()) glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iit->second);
				auto sit = m_storages.find(storage_id);
				if (sit != m_storages.end())
				{
					glBindBuffer(GL_DRAW_INDIRECT_BUFFER, sit->second.first);
					glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, (void*)(size_t)byte_offset, (GLsizei)count, 0);
				}
				glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
				glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
				glBindVertexArray(0);
			}

			virtual void index_delete(uint id) override
			{
				if (!id) return;
				auto it = m_ebo.find(id);
				if (it != m_ebo.end())
				{
					GLuint ebo = it->second;
					glDeleteBuffers(1, &ebo);
					m_ebo.erase(it);
				}
			}

			virtual uint texture_create(const std::vector<rgba>& flat, uint w, uint h, std::string& error) override
			{
				if (flat.empty() || flat.size() != (size_t)w * h)
				{
					error = "Error: Entering data is illegal!";
					return 0;
				}
				GLuint tex = 0;
				glGenTextures(1, &tex);
				glBindTexture(GL_TEXTURE_2D, tex);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, (GLsizei)w, (GLsizei)h, 0, GL_RGBA, GL_UNSIGNED_BYTE, flat.data());
				glGenerateMipmap(GL_TEXTURE_2D);
				glBindTexture(GL_TEXTURE_2D, 0);
				m_tex_meta[tex] = { w, h, false };
				return tex;
			}

			virtual uint texture_create_float(const std::vector<value>& flat, uint w, uint h, std::string& error) override
			{
				if (flat.empty() || flat.size() != (size_t)w * h * 4) { error = "illegal float texture data"; return 0; }
				GLuint tex = 0;
				glGenTextures(1, &tex);
				glBindTexture(GL_TEXTURE_2D, tex);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, (GLsizei)w, (GLsizei)h, 0, GL_RGBA, GL_FLOAT, flat.data());
				glBindTexture(GL_TEXTURE_2D, 0);
				m_tex_meta[tex] = { w, h, true };
				return tex;
			}

			virtual void texture_update(uint id, const std::vector<value>& flat) override
			{
				auto it = m_tex_meta.find(id);
				if (it == m_tex_meta.end() || !it->second.is_float) return;
				uint w = it->second.w, h = it->second.h;
				if (flat.size() != (size_t)w * h * 4) return;
				glBindTexture(GL_TEXTURE_2D, id);
				glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, (GLsizei)w, (GLsizei)h, GL_RGBA, GL_FLOAT, flat.data());
				glBindTexture(GL_TEXTURE_2D, 0);
			}

			virtual uint texture_create_compressed(const std::vector<std::uint8_t>& bc1, uint w, uint h, std::string& error) override
			{
				size_t need = (size_t)((w + 3) / 4) * ((h + 3) / 4) * 8;
				if (bc1.size() < need) { error = "illegal BC1 size"; return 0; }
				GLuint tex = 0;
				glGenTextures(1, &tex);
				glBindTexture(GL_TEXTURE_2D, tex);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGB_S3TC_DXT1_EXT, (GLsizei)w, (GLsizei)h, 0, (GLsizei)need, bc1.data());
				glBindTexture(GL_TEXTURE_2D, 0);
				return tex;
			}

			virtual uint texture_create_cubemap(const std::vector<std::vector<rgba>>& faces, uint size, std::string& error) override
			{
				if (faces.size() < 6) { error = "cubemap needs 6 faces"; return 0; }
				for (const auto& f : faces) if (f.size() != (size_t)size * size) { error = "illegal face size"; return 0; }

				GLuint tex = 0;
				glGenTextures(1, &tex);
				glBindTexture(GL_TEXTURE_CUBE_MAP, tex);
				for (uint i = 0; i < 6; ++i)
					glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_SRGB8_ALPHA8, (GLsizei)size, (GLsizei)size, 0, GL_RGBA, GL_UNSIGNED_BYTE, faces[i].data());
				glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
				glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
				glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
				glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
				m_cubemap_texs.insert(tex);
				return tex;
			}

			virtual void texture_delete(uint id) override { if (id) { glDeleteTextures(1, &id); m_cubemap_texs.erase(id); } }

			virtual std::uint64_t texture_bindless_handle(uint id) override
			{
				if (!id) return 0;
#ifdef GLEW_ARB_bindless_texture
				if (GLEW_ARB_bindless_texture)
				{
					GLuint64 h = glGetTextureHandleARB((GLuint)id);
					if (h) glMakeTextureHandleResidentARB(h);
					return (std::uint64_t)h;
				}
#endif
				return 0;
			}

			virtual void use_program(uint program) override { m_current_program = program; glUseProgram(program); }

			virtual void blend(bool enable) override { (enable ? glEnable : glDisable)(GL_BLEND); }
			virtual void depth_test(bool enable, bool less_equal) override
			{
				(enable ? glEnable : glDisable)(GL_DEPTH_TEST);
				if (enable) glDepthFunc(less_equal ? GL_LEQUAL : GL_LESS);
			}
			virtual void color_write(bool enable) override { glColorMask(enable ? GL_TRUE : GL_FALSE, enable ? GL_TRUE : GL_FALSE, enable ? GL_TRUE : GL_FALSE, enable ? GL_TRUE : GL_FALSE); }

			virtual void stencil_write(uint ref, uint mask) override
			{
				glEnable(GL_STENCIL_TEST);
				glStencilFunc(GL_ALWAYS, (GLint)ref, (GLuint)mask);
				glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
				glStencilMask((GLuint)mask);
			}

			virtual void stencil_test(uint ref, uint mask) override
			{
				glEnable(GL_STENCIL_TEST);
				glStencilFunc(GL_EQUAL, (GLint)ref, (GLuint)mask);
				glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
				glStencilMask(0);
			}

			virtual void stencil_disable() override
			{
				glDisable(GL_STENCIL_TEST);
			}

			virtual void set_msaa(uint samples) override
			{
				if (samples > 1) glEnable(GL_MULTISAMPLE);
				else glDisable(GL_MULTISAMPLE);
			}

			virtual void bind_texture(uint unit, uint texture) override
			{
				glActiveTexture(GL_TEXTURE0 + unit);
				GLenum target = (texture && m_cubemap_texs.count(texture)) ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
				glBindTexture(target, texture);
			}

			virtual void uniform_matrix(uint program, const char* name, const value* data, uint count) override
			{
				auto it = m_program_ubos.find(program);
				if (it != m_program_ubos.end())
				{
					int off = ubo_offset(name);
					if (off >= 0)
					{
						uint bytes = count * 16 * sizeof(value);
						if (off + bytes <= UBO_SIZE)
						{
							std::memcpy(it->second.data.data() + off, data, bytes);
							it->second.dirty = true;
						}
						return;
					}
				}
				GLint loc = glGetUniformLocation(program, name);
				if (loc != -1) glUniformMatrix4fv(loc, (GLsizei)count, GL_FALSE, data);
			}

			virtual void uniform_vec4(uint program, const char* name, value x, value y, value z, value w) override
			{
				auto it = m_program_ubos.find(program);
				if (it != m_program_ubos.end())
				{
					int off = ubo_offset(name);
					if (off >= 0)
					{
						value v[4] = { x, y, z, w };
						std::memcpy(it->second.data.data() + off, v, sizeof(v));
						it->second.dirty = true;
						return;
					}
				}
				GLint loc = glGetUniformLocation(program, name);
				if (loc != -1) glUniform4f(loc, x, y, z, w);
			}

			virtual void uniform_vec3(uint program, const char* name, value x, value y, value z) override
			{
				auto it = m_program_ubos.find(program);
				if (it != m_program_ubos.end())
				{
					int off = ubo_offset(name);
					if (off >= 0)
					{
						value v[3] = { x, y, z };
						std::memcpy(it->second.data.data() + off, v, sizeof(v));
						it->second.dirty = true;
						return;
					}
				}
				GLint loc = glGetUniformLocation(program, name);
				if (loc != -1) glUniform3f(loc, x, y, z);
			}

			virtual void uniform_int(uint program, const char* name, int v) override
			{
				auto it = m_program_ubos.find(program);
				if (it != m_program_ubos.end())
				{
					int off = ubo_offset(name);
					if (off >= 0)
					{
						std::memcpy(it->second.data.data() + off, &v, sizeof(v));
						it->second.dirty = true;
						return;
					}
				}
				GLint loc = glGetUniformLocation(program, name);
				if (loc != -1) glUniform1i(loc, v);
			}

			virtual void uniform_float(uint program, const char* name, value v) override
			{
				auto it = m_program_ubos.find(program);
				if (it != m_program_ubos.end())
				{
					int off = ubo_offset(name);
					if (off >= 0)
					{
						std::memcpy(it->second.data.data() + off, &v, sizeof(v));
						it->second.dirty = true;
						return;
					}
				}
				GLint loc = glGetUniformLocation(program, name);
				if (loc != -1) glUniform1f(loc, v);
			}

			virtual void uniform_lights(uint program, uint count, const value* data) override
			{
				auto it = m_program_ubos.find(program);
				if (it != m_program_ubos.end())
				{
					uint n = (count > UBO_MAX_LIGHTS) ? UBO_MAX_LIGHTS : count;
					std::memcpy(it->second.data.data() + UBO_LIGHTS, data, n * 64);
					int lc = (int)n;
					std::memcpy(it->second.data.data() + ubo_flag(FLAG_LIGHT_COUNT), &lc, sizeof(lc));
					it->second.dirty = true;
					return;
				}
				// data 布局（每光 16 个 value）：[0..3]=color.rgb+type, [4..7]=position, [8..11]=direction, [12..15]=cutoff/outer
				const GLint loc_color = glGetUniformLocation(program, "lights_color[0]");
				const GLint loc_pos = glGetUniformLocation(program, "lights_position[0]");
				const GLint loc_dir = glGetUniformLocation(program, "lights_direction[0]");
				const GLint loc_spot = glGetUniformLocation(program, "lights_spot[0]");
				glUniform1i(glGetUniformLocation(program, "lightCount"), (GLint)count);
				for (uint i = 0; i < count; ++i)
				{
					const value* p = data + i * 16;
					if (loc_color != -1) glUniform4fv(loc_color + (GLint)i, 1, p + 0);
					if (loc_pos != -1) glUniform4fv(loc_pos + (GLint)i, 1, p + 4);
					if (loc_dir != -1) glUniform4fv(loc_dir + (GLint)i, 1, p + 8);
					if (loc_spot != -1) glUniform4fv(loc_spot + (GLint)i, 1, p + 12);
				}
			}

			virtual void point_size(value s) override { glPointSize(s); }
			virtual void line_size(value s) override { glLineWidth(s); }

			virtual void draw(uint vertex, rhi_primitive prim, uint first, uint count) override
			{
				bind_ubo();
				glBindVertexArray(vertex);
				glDrawArrays(prim_of(prim), (GLint)first, (GLsizei)count);
				glBindVertexArray(0);
				++m_draw_calls;
			}

			virtual void draw_indexed(uint vertex, uint index, rhi_primitive prim, uint first, uint count) override
			{
				auto it = m_ebo.find(index);
				if (it == m_ebo.end()) return;
				bind_ubo();
				glBindVertexArray(vertex);
				glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, it->second);
				glDrawElements(prim_of(prim), (GLsizei)count, GL_UNSIGNED_INT, (void*)(size_t)(first * sizeof(uint)));
				glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
				glBindVertexArray(0);
				++m_draw_calls;
			}

			virtual void draw_instanced(uint vertex, rhi_primitive prim, uint first, uint count, uint instances) override
			{
				bind_ubo();
				glBindVertexArray(vertex);
				glDrawArraysInstanced(prim_of(prim), (GLint)first, (GLsizei)count, (GLsizei)instances);
				glBindVertexArray(0);
				++m_draw_calls;
			}

			virtual void draw_indexed_instanced(uint vertex, uint index, rhi_primitive prim, uint first, uint count, uint instances) override
			{
				auto it = m_ebo.find(index);
				if (it == m_ebo.end()) return;
				bind_ubo();
				glBindVertexArray(vertex);
				glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, it->second);
				glDrawElementsInstanced(prim_of(prim), (GLsizei)count, GL_UNSIGNED_INT, (void*)(size_t)(first * sizeof(uint)), (GLsizei)instances);
				glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
				glBindVertexArray(0);
				++m_draw_calls;
			}

			virtual uint query_create(std::string& error) override
			{
				GLuint q = 0;
				glGenQueries(1, &q);
				if (!q) { error = "glGenQueries failed"; return 0; }
				uint id = ++m_query_counter;
				m_queries[id] = q;
				return id;
			}

			virtual void query_delete(uint id) override
			{
				auto it = m_queries.find(id);
				if (it == m_queries.end()) return;
				GLuint q = it->second;
				glDeleteQueries(1, &q);
				m_queries.erase(it);
			}

			virtual void query_begin(uint id) override
			{
				auto it = m_queries.find(id);
				if (it != m_queries.end()) glBeginQuery(GL_SAMPLES_PASSED, it->second);
			}

			virtual void query_end(uint id) override
			{
				auto it = m_queries.find(id);
				if (it != m_queries.end()) glEndQuery(GL_SAMPLES_PASSED);
			}

			virtual uint query_result(uint id) override
			{
				auto it = m_queries.find(id);
				if (it == m_queries.end()) return 0;
				GLuint samples = 0;
				glGetQueryObjectuiv(it->second, GL_QUERY_RESULT, &samples);
				return (uint)samples;
			}

			virtual uint timestamp_query_create(std::string& error) override
			{
				GLuint q = 0;
				glGenQueries(1, &q);
				if (!q) { error = "glGenQueries(timestamp) failed"; return 0; }
				uint id = ++m_timestamp_counter;
				m_timestamp_queries[id] = q;
				return id;
			}

			virtual void timestamp_query_delete(uint id) override
			{
				auto it = m_timestamp_queries.find(id);
				if (it == m_timestamp_queries.end()) return;
				GLuint q = it->second;
				glDeleteQueries(1, &q);
				m_timestamp_queries.erase(it);
			}

			virtual void timestamp_record(uint id) override
			{
				auto it = m_timestamp_queries.find(id);
				if (it != m_timestamp_queries.end()) glQueryCounter(it->second, GL_TIMESTAMP);
			}

			virtual std::uint64_t timestamp_result(uint id) override
			{
				auto it = m_timestamp_queries.find(id);
				if (it == m_timestamp_queries.end()) return 0;
				GLuint64 ns = 0;
				glGetQueryObjectui64v(it->second, GL_QUERY_RESULT, &ns);
				return (std::uint64_t)ns;
			}

			virtual uint shader_compute_create(const std::string& comp, std::string& error) override
			{
				GLuint cs = glCreateShader(GL_COMPUTE_SHADER);
				const char* src = comp.c_str();
				glShaderSource(cs, 1, &src, nullptr);
				glCompileShader(cs);
				GLint ok = 0;
				glGetShaderiv(cs, GL_COMPILE_STATUS, &ok);
				if (!ok)
				{
					GLchar log[1024];
					glGetShaderInfoLog(cs, sizeof(log), nullptr, log);
					error = "Compute shader compile error: " + std::string(log);
					glDeleteShader(cs);
					return 0;
				}
				GLuint prog = glCreateProgram();
				glAttachShader(prog, cs);
				glLinkProgram(prog);
				glDeleteShader(cs);
				glGetProgramiv(prog, GL_LINK_STATUS, &ok);
				if (!ok)
				{
					GLchar log[1024];
					glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
					error = "Compute program link error: " + std::string(log);
					glDeleteProgram(prog);
					return 0;
				}
				uint id = ++m_compute_counter;
				m_computes[id] = prog;
				return id;
			}

			virtual void shader_compute_delete(uint id) override
			{
				auto it = m_computes.find(id);
				if (it == m_computes.end()) return;
				glDeleteProgram(it->second);
				m_computes.erase(it);
			}

			virtual void compute_dispatch(uint program, uint x, uint y, uint z) override
			{
				auto it = m_computes.find(program);
				if (it == m_computes.end()) return;
				glUseProgram(it->second);
				glDispatchCompute((GLuint)x, (GLuint)y, (GLuint)z);
				// 屏障：compute 写 SSBO → 后续 indirect draw / 顶点属性读取
				glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
				glUseProgram(0);
			}

			virtual uint storage_create(size_t bytes, const void* data, std::string& error) override
			{
				if (bytes == 0) { error = "empty storage"; return 0; }
				GLuint ssbo = 0;
				glGenBuffers(1, &ssbo);
				glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);
				glBufferData(GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)bytes, data, GL_DYNAMIC_COPY);
				glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
				uint id = ++m_storage_counter;
				m_storages[id] = { ssbo, (GLsizeiptr)bytes };
				return id;
			}

			virtual void storage_delete(uint id) override
			{
				auto it = m_storages.find(id);
				if (it == m_storages.end()) return;
				GLuint ssbo = it->second.first;
				glDeleteBuffers(1, &ssbo);
				m_storages.erase(it);
			}

			virtual void storage_update(uint id, const void* data, size_t bytes) override
			{
				auto it = m_storages.find(id);
				if (it == m_storages.end()) return;
				if (bytes <= 0 || bytes > (size_t)it->second.second) return;
				glBindBuffer(GL_SHADER_STORAGE_BUFFER, it->second.first);
				glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, (GLsizeiptr)bytes, data);
				glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
			}

			virtual void storage_read(uint id, void* out, size_t bytes) override
			{
				auto it = m_storages.find(id);
				if (it == m_storages.end() || !out) return;
				if (bytes <= 0 || bytes > (size_t)it->second.second) return;
				// 确保 compute 写入对 CPU 读回可见
				glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
				glBindBuffer(GL_SHADER_STORAGE_BUFFER, it->second.first);
				glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, (GLsizeiptr)bytes, out);
				glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
			}

			virtual void compute_storage_bind(uint program, uint binding, uint storage_id) override
			{
				(void)program;	// GL 的 SSBO 绑定点是上下文全局状态，与 program 无关
				auto it = m_storages.find(storage_id);
				if (it == m_storages.end()) return;
				glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, it->second.first);
			}

			virtual uint create_offscreen(uint w, uint h, uint color_count, std::string& error, bool hdr = false) override
			{
				if (color_count > 4) { error = "illegal color count"; return 0; }
				GLint prev_fbo = 0;
				glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
				GLuint fbo = 0;
				glGenFramebuffers(1, &fbo);
				glBindFramebuffer(GL_FRAMEBUFFER, fbo);

				offscreen_gl o;
				o.fbo = fbo;
				o.w = w; o.h = h;
				if (color_count == 0)
				{
					// 深度专用（阴影图）：可采样深度纹理
					glGenTextures(1, &o.depth_tex);
					glBindTexture(GL_TEXTURE_2D, o.depth_tex);
					glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, (GLsizei)w, (GLsizei)h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
					glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, o.depth_tex, 0);
					glDrawBuffer(GL_NONE);
					glReadBuffer(GL_NONE);
				}
				else
				{
					o.colors.resize(color_count);
					std::vector<GLenum> draw_buffers(color_count);
					for (uint i = 0; i < color_count; ++i)
					{
						GLuint tex = 0;
						glGenTextures(1, &tex);
						glBindTexture(GL_TEXTURE_2D, tex);
						if (hdr)
							glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, (GLsizei)w, (GLsizei)h, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
						else
							glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)w, (GLsizei)h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
						glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
						glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
						glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, tex, 0);
						o.colors[i] = tex;
						draw_buffers[i] = GL_COLOR_ATTACHMENT0 + i;
					}
					glDrawBuffers((GLsizei)color_count, draw_buffers.data());

					glGenRenderbuffers(1, &o.depth_rb);
					glBindRenderbuffer(GL_RENDERBUFFER, o.depth_rb);
					glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, (GLsizei)w, (GLsizei)h);
					glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, o.depth_rb);
				}

				if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
				{
					error = "offscreen framebuffer incomplete";
					glBindFramebuffer(GL_FRAMEBUFFER, prev_fbo);
					return 0;
				}
				glBindFramebuffer(GL_FRAMEBUFFER, prev_fbo);

				uint id = ++m_offscreen_counter;
				m_offscreens[id] = o;
				return id;
			}

			virtual void delete_offscreen(uint id) override
			{
				auto it = m_offscreens.find(id);
				if (it == m_offscreens.end()) return;
				for (uint tex : it->second.colors) { if (tex) glDeleteTextures(1, &tex); }
				if (it->second.depth_rb) glDeleteRenderbuffers(1, &it->second.depth_rb);
				if (it->second.depth_tex) glDeleteTextures(1, &it->second.depth_tex);
				if (it->second.fbo) glDeleteFramebuffers(1, &it->second.fbo);
				m_offscreens.erase(it);
			}

			virtual void begin_offscreen(uint id) override
			{
				glGetIntegerv(GL_FRAMEBUFFER_BINDING, &m_prev_fbo);
				glGetIntegerv(GL_VIEWPORT, m_prev_viewport);
				auto it = m_offscreens.find(id);
				glBindFramebuffer(GL_FRAMEBUFFER, (it != m_offscreens.end()) ? it->second.fbo : 0);
				if (it != m_offscreens.end())
				{
					// 视口须跟随离屏尺寸（阴影图 2048² ≠ 窗口尺寸，否则只画进角落，采样全中清空值）
					glViewport(0, 0, (GLsizei)it->second.w, (GLsizei)it->second.h);
					// 深度专用离屏（阴影图）：无颜色附件，深度须手动清 far=1.0
					//（Vulkan 由 render pass loadOp=CLEAR 完成，GL 无等价物，不清则深度图为陈旧/未定义内容）
					if (it->second.colors.empty())
					{
						glClearDepth(1.0);
						glClear(GL_DEPTH_BUFFER_BIT);
					}
				}
			}

			virtual void end_offscreen() override
			{
				glBindFramebuffer(GL_FRAMEBUFFER, m_prev_fbo);
				glViewport(m_prev_viewport[0], m_prev_viewport[1], m_prev_viewport[2], m_prev_viewport[3]);
			}

			virtual uint offscreen_texture(uint id, uint attachment) override
			{
				auto it = m_offscreens.find(id);
				if (it == m_offscreens.end() || attachment >= it->second.colors.size()) return 0;
				return it->second.colors[attachment];
			}

			virtual uint offscreen_depth_texture(uint id) override
			{
				auto it = m_offscreens.find(id);
				if (it == m_offscreens.end()) return 0;
				return it->second.depth_tex;
			}

			virtual void read_offscreen(uint id, sint x, sint y, uint* out) override
			{
				auto it = m_offscreens.find(id);
				if (it == m_offscreens.end() || !out) return;
				GLint prev_fbo = 0;
				glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
				glBindFramebuffer(GL_FRAMEBUFFER, it->second.fbo);
				glReadBuffer(GL_COLOR_ATTACHMENT0);
				glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, out);
				glBindFramebuffer(GL_FRAMEBUFFER, prev_fbo);
			}

			virtual uint create_offscreen_cubemap_depth(uint size, std::string& error) override
			{
				if (size == 0) { error = "illegal cubemap size"; return 0; }
				cubemap_gl c;
				c.size = size;
				glGenTextures(1, &c.tex);
				glBindTexture(GL_TEXTURE_CUBE_MAP, c.tex);
				for (uint i = 0; i < 6; ++i)
					glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_DEPTH_COMPONENT24, (GLsizei)size, (GLsizei)size, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
				glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);   // 阴影比较采样
				glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
				glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

				glGenFramebuffers(1, &c.fbo);
				uint id = ++m_cubemap_counter;
				m_cubemap_texs.insert(c.tex);
				m_cubemaps[id] = c;
				return id;
			}

			virtual uint create_offscreen_cubemap(uint size, std::string& error, uint mip_levels = 1) override
			{
				if (size == 0) { error = "illegal cubemap size"; return 0; }
				if (mip_levels == 0) mip_levels = 1;
				cubemap_gl c;
				c.size = size;
				c.color = true;
				c.mip_levels = mip_levels;
				glGenTextures(1, &c.tex);
				glBindTexture(GL_TEXTURE_CUBE_MAP, c.tex);
				for (uint m = 0; m < mip_levels; ++m)
				{
					GLsizei ms = (GLsizei)(size >> m);
					if (ms < 1) ms = 1;
					for (uint i = 0; i < 6; ++i)
						glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, m, GL_RGBA16F, ms, ms, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
				}
				glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
				glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

				glGenFramebuffers(1, &c.fbo);
				uint id = ++m_cubemap_counter;
				m_cubemap_texs.insert(c.tex);
				m_cubemaps[id] = c;
				return id;
			}

			virtual void delete_offscreen_cubemap(uint id) override
			{
				auto it = m_cubemaps.find(id);
				if (it == m_cubemaps.end()) return;
				if (it->second.tex) glDeleteTextures(1, &it->second.tex);
				if (it->second.fbo) glDeleteFramebuffers(1, &it->second.fbo);
				m_cubemap_texs.erase(it->second.tex);
				m_cubemaps.erase(it);
			}

			virtual void delete_offscreen_cubemap_depth(uint id) override
			{
				auto it = m_cubemaps.find(id);
				if (it == m_cubemaps.end()) return;
				if (it->second.tex) glDeleteTextures(1, &it->second.tex);
				if (it->second.fbo) glDeleteFramebuffers(1, &it->second.fbo);
				m_cubemap_texs.erase(it->second.tex);
				m_cubemaps.erase(it);
			}

			virtual void begin_offscreen_cubemap(uint id, uint face, uint mip = 0) override
			{
				auto it = m_cubemaps.find(id);
				if (it == m_cubemaps.end() || face > 5 || mip >= it->second.mip_levels) return;
				glBindFramebuffer(GL_FRAMEBUFFER, it->second.fbo);
				if (it->second.color)
				{
					glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, it->second.tex, mip);
					glDrawBuffer(GL_COLOR_ATTACHMENT0);
					glReadBuffer(GL_COLOR_ATTACHMENT0);
				}
				else
				{
					glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, it->second.tex, mip);
					glDrawBuffer(GL_NONE);
					glReadBuffer(GL_NONE);
				}
				GLsizei ms = (GLsizei)(it->second.size >> mip);
				if (ms < 1) ms = 1;
				glViewport(0, 0, ms, ms);
			}

			virtual uint offscreen_cubemap_texture(uint id) override
			{
				auto it = m_cubemaps.find(id);
				if (it == m_cubemaps.end()) return 0;
				return it->second.tex;
			}

			virtual uint draw_call_count() override { return m_draw_calls; }
			virtual void reset_draw_call_count() override { m_draw_calls = 0; }
		};

		render_backend* create_gl_backend()
		{
			static gl_backend instance;
			return &instance;
		}
	}
}
