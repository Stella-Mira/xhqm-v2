#include "rhi_vk.h"
#include "spirv.h"
#include "ubo.h"
// 可移植性子集/健壮性特性结构体在头文件里受 beta 开关保护（VK_KHR_portability_subset 用得上）
#ifndef VK_ENABLE_BETA_EXTENSIONS
#define VK_ENABLE_BETA_EXTENSIONS
#endif
// 平台 surface 扩展：Windows 用 Win32，Linux 用 XCB，macOS 用 Metal（MoltenVK）
#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined(__APPLE__)
#define VK_USE_PLATFORM_METAL_EXT
#else
#define VK_USE_PLATFORM_XCB_KHR
#include <xcb/xcb.h>
#endif
#if defined(__APPLE__)
// macOS：从 NSView 取/建 CAMetalLayer（rhi_vk_macos.mm 实现）
void* rhi_vk_macos_metal_layer(void* nsview_ptr);
#endif
#include <vulkan/vulkan.h>
#include <unordered_map>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <functional>

namespace xhqm
{
	namespace fantasy
	{
		namespace
		{
			uint32_t find_memory_type(VkPhysicalDevice phys, uint32_t type_filter, VkMemoryPropertyFlags props)
			{
				VkPhysicalDeviceMemoryProperties mp{};
				vkGetPhysicalDeviceMemoryProperties(phys, &mp);
				uint32_t fallback = 0;            // 属性放宽时的兜底候选（任一满足 type_filter 的类型）
				bool have_fallback = false;
				for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
				{
					if (type_filter & (1u << i))
					{
						if ((mp.memoryTypes[i].propertyFlags & props) == props)
							return i;             // 精确匹配所需属性
						if (!have_fallback)
						{
							fallback = i;
							have_fallback = true;   // 记录首个候选，无精确匹配时回退用
						}
					}
				}
				if (have_fallback)
					return fallback;
				// 无任何满足 type_filter 的类型（type_filter==0 或设备异常）：回退 0 而非 ~0u，
				// 避免 vkAllocateMemory 收到非法 memoryTypeIndex 触发校验层报错；能否分配由返回的 VkResult 判断。
				return 0;
			}

			// UBO 布局与 uniform 名 → 偏移映射已收敛到 ubo.h（GL/Vulkan 共用单一事实源）
			// 环形 UBO slot 总数（Vulkan 专属实现细节）：每个 draw 固化一份独立 uniform 数据，避免后画覆盖先画。
			// slot0 = CPU 暂存区（uniform_* 的写入目标），不参与环形分配：若 draw 的 dynamicOffset 命中 slot0，
			// GPU 执行时读到的会是后续 draw 持续覆写中的暂存区 → 拿到别人的矩阵/灯光（单帧几何跳变闪烁的根因）。
			// 环形槽还需 ≥ 2×单帧同 program 最大 draw 数：CPU 可领先 GPU 一帧录制（双 fence），录制 N+1 帧时
			// 不得覆写 N 帧仍在读的槽；1024 槽（1023 环形）支持单帧 ≤511 次 draw
			//（9 角色 MMD 场景：主 pass ~190 + 阴影 pass ~190 + 杂项）。
			constexpr uint UBO_SLOTS = 1024;

			// UBO 上传粒度与「已使用」位图规模。
			// 原先每个 draw 把整块 UBO_SIZE(38240B) 从常驻映射内存复制进独占 slot，源是
			// HOST_VISIBLE|HOST_COHERENT（Windows 上通常是 write-combined）⇒ 不可缓存读，
			// 实测 0.156ms CPU/draw（38240B ≈ 600 cache line × ~250ns）。而 38240 里
			// 32768 是 bones[512]、4096 是 instances[64]，不写骨骼/实例时整段都是死的。
			// 改法：uniform 写 CPU 侧暂存（cached），draw 时只复制「曾经被写过」的 64B 块
			//（主着色器约 1.4KB，2~3 个连续区间）。
			constexpr uint UBO_BLK = 64;                                    // 复制粒度 = cache line
			constexpr uint UBO_BLKS = (UBO_SIZE + UBO_BLK - 1) / UBO_BLK;   // 598 块
			constexpr uint UBO_USED_WORDS = (UBO_BLKS + 63) / 64;           // 10 个 u64 位图

			// 每个 program 的描述符集份数（每帧在飞一份）。
			// 根因：描述符集是跨帧共享的可变资源——录制期 vkUpdateDescriptorSets 改写时，
			// 上一帧使用同一份集的提交可能仍在 GPU 执行 → 未定义行为 → 偶发单帧采样错纹理（闪烁）。
			// 修复：每帧轮转独立的一份；begin_frame 的 fence 等待保证当前帧这份
			// 上次使用（N-MAX 帧前）已完成。
			constexpr uint MAX_DESC_SETS = 1024;   // 每绘制轮转一份（贴图等描述符录制期即写即用，需覆盖单帧最大绘制数）
			constexpr uint WRITTEN_NONE = 0xFFFFFFFFu;   // texture_rot 初值：该 unit 从未写入过任何一份描述符集

			// 顶点布局哈希（用于管线缓存键）
			size_t layout_hash(const std::vector<uint>& locations)
			{
				size_t h = 0;
				for (auto l : locations) h = h * 31 + l;
				return h;
			}

			// 分量数 -> VkFormat
			VkFormat float_format(uint count)
			{
				switch (count)
				{
				case 1: return VK_FORMAT_R32_SFLOAT;
				case 2: return VK_FORMAT_R32G32_SFLOAT;
				case 3: return VK_FORMAT_R32G32B32_SFLOAT;
				case 4: return VK_FORMAT_R32G32B32A32_SFLOAT;
				default: return VK_FORMAT_R32G32B32A32_SFLOAT;
				}
			}

			// 图元类型 -> 管线拓扑
			VkPrimitiveTopology prim_topology(rhi_primitive prim)
			{
				switch (prim)
				{
				case rhi_primitive::points: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
				case rhi_primitive::lines: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
				case rhi_primitive::triangle_fan: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
				case rhi_primitive::quads:
				case rhi_primitive::triangles:
				default: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
				}
			}
		}

		class vk_backend : public render_backend
		{
			// ---- 设备 ----
			VkInstance m_instance = VK_NULL_HANDLE;
			VkPhysicalDevice m_phys = VK_NULL_HANDLE;
			VkDevice m_device = VK_NULL_HANDLE;
			VkQueue m_queue = VK_NULL_HANDLE;
			std::uint32_t m_queue_family = 0;
			std::string m_gpu_name;
			uint m_ubo_align = 256;   // 设备 minUniformBufferOffsetAlignment（init 时查询）

			// ---- 内存池（host visible 大块子分配，减少 vkAllocateMemory 次数）----
			struct mem_block { VkDeviceMemory memory = VK_NULL_HANDLE; VkDeviceSize size = 0; VkDeviceSize used = 0; uint32_t type = 0; };
			std::vector<mem_block> m_mem_blocks;

			bool pool_alloc(VkDeviceSize size, VkDeviceSize align, uint32_t typeIndex, VkDeviceMemory& memory, VkDeviceSize& offset)
			{
				align = align ? align : 1;
				for (auto& blk : m_mem_blocks)
				{
					if (blk.type != typeIndex) continue;   // 内存类型必须匹配：device-local 块不可被 host 映射路径复用（MoltenVK 上 map 会失败）
					VkDeviceSize aligned = (blk.used + align - 1) / align * align;
					if (aligned + size <= blk.size)
					{
						memory = blk.memory;
						offset = aligned;
						blk.used = aligned + size;
						return true;
					}
				}
				VkDeviceSize block_size = (size > (64ull << 20)) ? size : (64ull << 20);
				VkMemoryAllocateInfo mai{};
				mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
				mai.allocationSize = block_size;
				mai.memoryTypeIndex = typeIndex;
				VkDeviceMemory mem = VK_NULL_HANDLE;
				if (vkAllocateMemory(m_device, &mai, nullptr, &mem) != VK_SUCCESS) return false;
				mem_block blk;
				blk.memory = mem;
				blk.size = block_size;
				blk.used = 0;
				blk.type = typeIndex;
				VkDeviceSize aligned = (blk.used + align - 1) / align * align;
				memory = mem;
				offset = aligned;
				blk.used = aligned + size;
				m_mem_blocks.push_back(blk);
				return true;
			}

			void pool_destroy()
			{
				for (auto& blk : m_mem_blocks) if (blk.memory) vkFreeMemory(m_device, blk.memory, nullptr);
				m_mem_blocks.clear();
			}

			// ---- 多窗口表面 / 交换链：每窗口一份完整状态（surface/swapchain/附件/命令池/信号量/fence 全部独立） ----
			struct surface_state
			{
				void* window = nullptr;
				VkSurfaceKHR surface = VK_NULL_HANDLE;
				VkSwapchainKHR swapchain = VK_NULL_HANDLE;
#if !defined(_WIN32) && !defined(__APPLE__)
				xcb_window_t xcb_window = 0;
				xcb_connection_t* xcb_conn = nullptr;
#endif
				VkExtent2D extent{};
				std::vector<VkImage> images;
				std::vector<VkImageView> views;
				VkRenderPass render_pass = VK_NULL_HANDLE;
				std::vector<VkFramebuffer> framebuffers;
				VkCommandPool cmd_pool = VK_NULL_HANDLE;
				std::vector<VkCommandBuffer> cmd_bufs;
				// 每帧在飞一对信号量（与 fence 同轮转）：单份跨帧复用会在上一帧 present 尚未消费时
				// 被再次 signal（二值信号量重复 signal = 未定义行为）→ 偶发显示错乱/闪烁
				std::vector<VkSemaphore> image_avails;
				std::vector<VkSemaphore> render_dones;
				std::vector<VkFence> fences;
				std::uint32_t image_index = 0;
				std::uint32_t frame_index = 0;
				bool frame_active = false;   // begin_frame/end_frame 之间为 true（compute 离帧走 one_shot）
				bool render_pass_open = false;   // 当前命令缓冲是否处于 render pass 内（vkCmdBeginRenderPass..EndRenderPass）
				VkImage depth_image = VK_NULL_HANDLE;
				VkDeviceMemory depth_memory = VK_NULL_HANDLE;
				VkImageView depth_view = VK_NULL_HANDLE;
				// MSAA 多采样颜色附件（m_msaa > 1 时）
				VkImage msaa_image = VK_NULL_HANDLE;
				VkDeviceMemory msaa_memory = VK_NULL_HANDLE;
				VkImageView msaa_view = VK_NULL_HANDLE;
			};
			VkFormat m_color_format = VK_FORMAT_B8G8R8A8_UNORM;
			std::unordered_map<void*, surface_state> m_surfaces;
			surface_state* m_active = nullptr;   // 当前帧目标窗口
			std::uint32_t m_frame_serial = 0;   // 帧序号（每个 begin_frame +1）：描述符集回卷的安全性判据
			std::uint32_t m_max_fences = 0;   // 各表面 fence 数的最大值 = 在飞帧数上界

			// ---- GPU 查询（遮挡查询）----
			VkQueryPool m_query_pool = VK_NULL_HANDLE;
			std::unordered_map<uint, std::uint32_t> m_queries;   // query id -> pool index
			std::uint32_t m_query_next = 0;
			std::uint32_t m_query_counter = 0;
			static constexpr std::uint32_t QUERY_COUNT = 64;

			// ---- GPU 时间戳查询（per-pass 计时）----
			VkQueryPool m_timestamp_pool = VK_NULL_HANDLE;
			std::unordered_map<uint, std::uint32_t> m_timestamps;   // id -> pool index
			std::uint32_t m_timestamp_next = 0;
			std::uint32_t m_timestamp_counter = 0;
			static constexpr std::uint32_t TIMESTAMP_COUNT = 64;
			float m_timestamp_period = 1.0f;   // 设备 timestampPeriod（纳秒/刻度）
			VkFormat m_depth_stencil_format = VK_FORMAT_D32_SFLOAT_S8_UINT;   // 主/离屏颜色目标的深度附件（按设备能力选）
			VkFormat m_depth_format = VK_FORMAT_D32_SFLOAT;                   // 深度专用（可采样）目标（按设备能力选）

			// ---- 渲染状态（blend/depth 进管线缓存键，point/line 走动态状态）----
			bool m_blend = true;
			uint m_instances = 1;   // 实例数（draw_instanced 临时设置）
			bool m_depth = true;    // 开启深度测试（标准深度 + LESS）
			bool m_depth_less_equal = false;
			bool m_stencil = false;      // stencil 测试开关
			uint m_stencil_ref = 0;      // stencil 参考值
			uint m_stencil_mask = 0xFF;  // stencil 比较/写掩码
			bool m_stencil_write = false; // 是否 REPLACE 写入（否则 EQUAL 测试）
			bool m_color_write = true;   // 颜色写入开关（遮挡查询 pass 关闭，只写深度）
			uint m_msaa = 1;             // MSAA 采样数（1=关闭）
			float m_point_size = 1.0f;
			float m_line_width = 1.0f;
			float m_clear_color[4] = { 0.05f, 0.05f, 0.1f, 1.0f };   // clear(rgba) 写入，render pass 清除色

			// 渲染状态 + render_pass → 管线缓存键。
			// 状态用固定位段（40..45 开关位、46..48 msaa、52+ 拓扑），render_pass 指针先散列再混入，
			// 避免：① 间接 draw 漏 blend/depth/stencil 导致状态错配复用；② 裸指针 XOR 与位段碰撞。
			size_t pipeline_key(size_t vlayout, VkPrimitiveTopology topo)
			{
				uintptr_t rp = (uintptr_t)active_render_pass();
				size_t rp_hash = (size_t)rp;
				rp_hash ^= rp_hash >> 33;
				rp_hash *= 0xff51afd7ed558ccdULL;
				rp_hash ^= rp_hash >> 33;
				return vlayout
					^ ((size_t)topo << 52)
					^ (m_blend ? (size_t(1) << 40) : 0)
					^ (m_depth ? (size_t(1) << 41) : 0)
					^ (m_depth_less_equal ? (size_t(1) << 42) : 0)
					^ (m_color_write ? 0 : (size_t(1) << 43))
					^ (m_stencil ? (size_t(1) << 44) : 0)
					^ (m_stencil_write ? (size_t(1) << 45) : 0)
					^ ((size_t)m_msaa << 46)
					^ rp_hash;
			}

			// 选一个设备实际支持的深度/深度模板格式（优先桌面通用的 D32 系列）
			VkFormat pick_depth_format(bool stencil)
			{
				const VkFormat with_stencil[] = { VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM_S8_UINT };
				const VkFormat without_stencil[] = { VK_FORMAT_D32_SFLOAT, VK_FORMAT_X8_D24_UNORM_PACK32, VK_FORMAT_D16_UNORM };
				const VkFormat* list = stencil ? with_stencil : without_stencil;
				const uint count = stencil ? 3 : 3;
				for (uint i = 0; i < count; ++i)
				{
					VkFormatProperties fp{};
					vkGetPhysicalDeviceFormatProperties(m_phys, list[i], &fp);
					VkFormatFeatureFlags need = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
					if (stencil) need |= 0;
					if ((fp.optimalTilingFeatures & need) == need) return list[i];
				}
				return stencil ? VK_FORMAT_D32_SFLOAT_S8_UINT : VK_FORMAT_D32_SFLOAT;
			}

			// ---- 程序 ----
			struct program_t
			{
				VkShaderModule vs = VK_NULL_HANDLE;
				VkShaderModule fs = VK_NULL_HANDLE;
				std::vector<std::uint32_t> vs_spv;   // 预编译 SPIR-V（shader 二进制缓存用）
				std::vector<std::uint32_t> fs_spv;
				VkDescriptorSetLayout desc_layout = VK_NULL_HANDLE;
				VkDescriptorPool desc_pool = VK_NULL_HANDLE;
				VkDescriptorSet desc_sets[MAX_DESC_SETS] = {};   // 每绘制轮转一份（本 program 独占）
				uint desc_rot = 0;             // 本 program 自己的描述符集轮转（帧起点 +1，每绘制 +1）
				uint desc_frame_rot = 0;       // 本帧起点计数（单帧吃满整环时的回卷基准）
				uint desc_wrap_frame = 0;      // 上次回卷时的帧序号（判断"上一圈是否已完成"）
				VkPipelineLayout layout = VK_NULL_HANDLE;
				std::unordered_map<size_t, VkPipeline> pipelines;  // 按顶点布局缓存管线
				VkBuffer ubo_buffer = VK_NULL_HANDLE;
				VkDeviceMemory ubo_memory = VK_NULL_HANDLE;
				VkDeviceSize ubo_offset = 0;   // 内存池子分配偏移（desc_set 绑定时用）
				void* ubo_data = nullptr;
				bool ubo_dirty = false;   // 已废弃：只被赋值、从未被读取（保留字段避免动其它地方）
				// CPU 侧暂存：uniform_* 的写入目标（普通堆内存，cache 友好）。
				// 它同时是「本次绘制要固化进 slot 的内容」的唯一事实源：原实现拿 ubo_data 自己
				// 当暂存（slot0），于是每个 draw 必须整块自拷贝一次，且源是不可缓存映射内存。
				std::vector<char> ubo_stage;
				std::uint64_t ubo_used[UBO_USED_WORDS] = {};   // 「曾被写过」的 64B 块位图（只置位，不清除）
				uint ubo_slot = 0;   // 环形 slot 计数器（单调递增，模 (UBO_SLOTS-1) 后 +1 取槽）
				uint ubo_frame_start = 0;   // 本帧起始计数值（begin_frame 重置，用于环形过冲警告）
				uint textures[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
				uint texture_rot[12] = { WRITTEN_NONE, WRITTEN_NONE, WRITTEN_NONE, WRITTEN_NONE, WRITTEN_NONE, WRITTEN_NONE,
					WRITTEN_NONE, WRITTEN_NONE, WRITTEN_NONE, WRITTEN_NONE, WRITTEN_NONE, WRITTEN_NONE };
			};
			std::unordered_map<uint, program_t> m_programs;
			std::uint32_t m_program_counter = 0;

			// ---- 计算着色器程序 ----
			struct compute_program_t
			{
				VkShaderModule cs = VK_NULL_HANDLE;
				VkDescriptorSetLayout desc_layout = VK_NULL_HANDLE;
				VkDescriptorPool desc_pool = VK_NULL_HANDLE;
				VkDescriptorSet desc_set = VK_NULL_HANDLE;
				VkPipelineLayout layout = VK_NULL_HANDLE;
				VkPipeline pipeline = VK_NULL_HANDLE;
			};
			std::unordered_map<uint, compute_program_t> m_computes;
			std::uint32_t m_compute_counter = 0;

			// ---- 存储缓冲（SSBO，M3/M9 GPU 驱动）----
			struct storage_t
			{
				VkBuffer buffer = VK_NULL_HANDLE;
				VkDeviceMemory memory = VK_NULL_HANDLE;
				VkDeviceSize offset = 0;
				VkDeviceSize size = 0;
			};
			std::unordered_map<uint, storage_t> m_storages;
			std::uint32_t m_storage_counter = 0;

			// ---- 离屏渲染目标（MRT）----
			struct offscreen_t
			{
				std::vector<VkImage> images;
				std::vector<VkDeviceMemory> memories;
				std::vector<VkImageView> views;
				VkImage depth_image = VK_NULL_HANDLE;
				VkDeviceMemory depth_memory = VK_NULL_HANDLE;
				VkImageView depth_view = VK_NULL_HANDLE;
				VkRenderPass render_pass = VK_NULL_HANDLE;
				VkFramebuffer framebuffer = VK_NULL_HANDLE;
				uint w = 0, h = 0, color_count = 0;
				std::vector<uint> color_tex_ids;   // 每个 color attachment 的 texture id 缓存（0=未创建，修复重复建 sampler 泄漏）
				uint depth_tex_id = 0;             // depth texture id 缓存
			};
			std::unordered_map<uint, offscreen_t> m_offscreens;
			std::uint32_t m_offscreen_counter = 0;
			uint m_offscreen_active = 0;   // 0 = swapchain，否则为离屏 id
			uint m_draw_calls = 0;         // 累计 draw 调用次数

			// ---- 立方体深度离屏（M6 点光阴影）----
			struct cubemap_t
			{
				VkImage image = VK_NULL_HANDLE;
				VkDeviceMemory memory = VK_NULL_HANDLE;
				VkImageView cube_view = VK_NULL_HANDLE;            // 6 面采样视图（含所有 mip）
				std::vector<VkImageView> face_views;               // 每面每 mip 渲染视图（face * mip_levels + mip）
				std::vector<VkFramebuffer> framebuffers;           // 每面每 mip framebuffer
				VkRenderPass render_pass = VK_NULL_HANDLE;
				uint size = 0;
				uint mip_levels = 1;
				uint texture_id = 0;   // 供 bind_texture 的 texture id（image=0 表示未创建）
				bool color = false;    // true=颜色（R16G16B16A16F），false=深度（D32_SFLOAT）
			};
			std::unordered_map<uint, cubemap_t> m_cubemaps;
			std::uint32_t m_cubemap_counter = 0;
			uint m_cubemap_active = 0;   // 0 = 非立方体，否则为 cubemap id

			// ---- 顶点缓冲 ----
			struct vertex_t
			{
				VkBuffer buffer = VK_NULL_HANDLE;
				VkDeviceMemory memory = VK_NULL_HANDLE;
				VkDeviceSize offset = 0;   // 内存池内偏移
				VkDeviceSize size = 0;     // 字节数（vertex_update 越界护栏）
				std::vector<uint> locations;
				uint count = 0;
			};
			std::unordered_map<uint, vertex_t> m_vertex_buffers;
			std::uint32_t m_vertex_counter = 0;

			// ---- 索引缓冲 ----
			struct index_t
			{
				VkBuffer buffer = VK_NULL_HANDLE;
				VkDeviceMemory memory = VK_NULL_HANDLE;
				uint count = 0;
			};
			std::unordered_map<uint, index_t> m_index_buffers;
			std::uint32_t m_index_counter = 0;

			// ---- indirect 缓冲（M3/M7 GPU 驱动） ----
			struct indirect_t
			{
				VkBuffer buffer = VK_NULL_HANDLE;
				VkDeviceMemory memory = VK_NULL_HANDLE;
				uint count = 0;
			};
			std::unordered_map<uint, indirect_t> m_indirect_buffers;
			std::uint32_t m_indirect_counter = 0;

			// ---- 纹理 ----
			struct texture_t
			{
				VkImage image = VK_NULL_HANDLE;
				VkDeviceMemory memory = VK_NULL_HANDLE;
				VkImageView view = VK_NULL_HANDLE;
				VkSampler sampler = VK_NULL_HANDLE;
				bool owns_image = true;   // 离屏纹理为 false：image/view/memory 由离屏目标持有
				bool is_float = false;    // RGBA32F（texture_update 用）
				uint w = 0, h = 0;
			};
			std::unordered_map<uint, texture_t> m_textures;
			std::uint32_t m_texture_counter = 0;
			uint m_default_texture = 0;        // 1×1 白兜底纹理 id（懒创建，见 default_texture_2d）
			uint m_default_shadow_tex = 0;     // 1×1 深度兜底（sampler2DShadow 绑定）
			uint m_default_cube_tex = 0;       // 1×1 白立方体兜底（samplerCube 绑定）
			uint m_default_cube_shadow_tex = 0; // 1×1 深度立方体兜底（samplerCubeShadow 绑定）

			uint m_current_program = 0;

			VkShaderModule create_module(const std::vector<std::uint32_t>& spv)
			{
				VkShaderModuleCreateInfo ci{};
				ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
				ci.codeSize = spv.size() * sizeof(std::uint32_t);
				ci.pCode = spv.data();
				VkShaderModule mod = VK_NULL_HANDLE;
				vkCreateShaderModule(m_device, &ci, nullptr, &mod);
				return mod;
			}

			VkBuffer create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkDeviceMemory& memory, VkDeviceSize& offset)
			{
				VkBufferCreateInfo bci{};
				bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
				bci.size = size;
				bci.usage = usage;
				bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
				VkBuffer buffer = VK_NULL_HANDLE;
				vkCreateBuffer(m_device, &bci, nullptr, &buffer);

				VkMemoryRequirements mr{};
				vkGetBufferMemoryRequirements(m_device, buffer, &mr);
				uint32_t typeIndex = find_memory_type(m_phys, mr.memoryTypeBits,
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
				pool_alloc(mr.size, mr.alignment, typeIndex, memory, offset);
				vkBindBufferMemory(m_device, buffer, memory, offset);
				return buffer;
			}

			VkSampler create_sampler(const sampler_desc& desc, float max_lod)
			{
				VkSamplerCreateInfo sci{};
				sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
				sci.magFilter = desc.linear_mag ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
				sci.minFilter = desc.linear_min ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
				sci.mipmapMode = desc.mipmap ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
				VkSamplerAddressMode am = desc.clamp ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT;
				sci.addressModeU = am;
				sci.addressModeV = am;
				sci.addressModeW = am;
				sci.maxLod = max_lod;
				sci.compareEnable = desc.compare ? VK_TRUE : VK_FALSE;
				sci.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
				sci.anisotropyEnable = desc.anisotropy > 1.0f ? VK_TRUE : VK_FALSE;
				sci.maxAnisotropy = desc.anisotropy > 1.0f ? desc.anisotropy : 1.0f;
				VkSampler sampler = VK_NULL_HANDLE;
				vkCreateSampler(m_device, &sci, nullptr, &sampler);
				return sampler;
			}

			void one_shot(const std::function<void(VkCommandBuffer)>& record)
			{
				VkCommandPoolCreateInfo cpci{};
				cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
				cpci.queueFamilyIndex = m_queue_family;
				VkCommandPool pool = VK_NULL_HANDLE;
				vkCreateCommandPool(m_device, &cpci, nullptr, &pool);

				VkCommandBufferAllocateInfo cbai{};
				cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
				cbai.commandPool = pool;
				cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
				cbai.commandBufferCount = 1;
				VkCommandBuffer cb = VK_NULL_HANDLE;
				vkAllocateCommandBuffers(m_device, &cbai, &cb);

				VkCommandBufferBeginInfo bi{};
				bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
				bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
				vkBeginCommandBuffer(cb, &bi);
				record(cb);
				vkEndCommandBuffer(cb);

				VkSubmitInfo si{};
				si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
				si.commandBufferCount = 1;
				si.pCommandBuffers = &cb;
				vkQueueSubmit(m_queue, 1, &si, VK_NULL_HANDLE);
				vkQueueWaitIdle(m_queue);

				vkDestroyCommandPool(m_device, pool, nullptr);
			}

			// 当前帧应使用的描述符集（每绘制轮转一份，避免改写仍在 GPU 使用的集）
			VkDescriptorSet current_desc_set(program_t& p)
			{
				return p.desc_sets[p.desc_rot % MAX_DESC_SETS];
			}

			// 等待「已提交且仍在飞」的帧完成：跳过多表面当前帧那根刚被 reset、尚未提交的 fence。
			// 用它替代 vkDeviceWaitIdle —— 后者会把呈现队列一并排空，FIFO 下可能连带等垂直同步。
			void wait_submitted()
			{
				if (!m_device) return;
				for (auto& kv : m_surfaces)
				{
					surface_state& s = kv.second;
					if (s.fences.empty()) continue;
					std::vector<VkFence> f;
					f.reserve(s.fences.size());
					for (std::size_t i = 0; i < s.fences.size(); ++i)
						if ((uint)i != s.frame_index) f.push_back(s.fences[i]);
					if (!f.empty()) vkWaitForFences(m_device, (uint)f.size(), f.data(), VK_TRUE, UINT64_MAX);
				}
			}

			// 每个绘制结束后轮转描述符集：贴图描述符在录制期写入当前集，
			// 帧内多个绘制若共用一份集会被最后绑定的贴图覆盖（全白/串色）。
			//
			// 轮转计数是【每 program 独立】的。原来是一个全局计数被所有 program 共用，于是
			// "每帧 445 次绘制"这种场景每约 2 帧就把 1024 的环转满一次，触发一次 vkDeviceWaitIdle
			// —— 掉帧尖刺的根源；而每个 program 自己每帧只用几十到一百多次，本该转得慢得多。
			//
			// 回卷的正确性判据（不再需要设备空闲）：
			//   回卷要覆写的那份集是在「上一次回卷」时写的。若从那之后已经过去 ≥ 在飞帧数
			//   那么多帧，那时的帧必然已完成 —— begin_frame 每帧都在等 ic 帧前那根 fence。
			//   满足就回卷、零等待；不满足（单帧同 program 绘制极多）才退回一次
			//   「只等自己提交的帧」的同步，并保留老行为作为最后兜底。
			void advance_desc_set(program_t& p)
			{
				if (p.desc_rot >= (uint)MAX_DESC_SETS)
				{
					if (p.desc_rot - p.desc_frame_rot >= (uint)MAX_DESC_SETS)
					{
						// 单帧就把整个环吃满：环内任何回卷点都还被本帧已录制的命令引用，
						// 不可能安全复用 ⇒ 保持老行为（等设备空闲 + 回卷到本帧起点）。
						vkDeviceWaitIdle(m_device);
						p.desc_rot = p.desc_frame_rot;
					}
					else
					{
						if (m_frame_serial - p.desc_wrap_frame < m_max_fences) wait_submitted();
						p.desc_rot = 0;
						p.desc_wrap_frame = m_frame_serial;
					}
				}
				++p.desc_rot;
			}

			// 活动窗口交换链是否可用（原单窗口 m_swapchain 判空语义）
			bool swapchain_ready() const { return m_active && m_active->swapchain != VK_NULL_HANDLE; }

			// 销毁单个窗口的完整状态（等价原 destroy_swapchain，按窗口隔离）
			void destroy_surface_state(surface_state& s)

			{
				if (!m_device) return;
				vkDeviceWaitIdle(m_device);   // 先等队列空闲，再销毁在用的 swapchain/附件（否则驱动崩溃）
				for (auto& f : s.framebuffers) if (f) vkDestroyFramebuffer(m_device, f, nullptr);
				s.framebuffers.clear();
				if (s.render_pass) { vkDestroyRenderPass(m_device, s.render_pass, nullptr); s.render_pass = VK_NULL_HANDLE; }
				if (s.depth_view) { vkDestroyImageView(m_device, s.depth_view, nullptr); s.depth_view = VK_NULL_HANDLE; }
				if (s.depth_image) { vkDestroyImage(m_device, s.depth_image, nullptr); s.depth_image = VK_NULL_HANDLE; }
				if (s.depth_memory) { vkFreeMemory(m_device, s.depth_memory, nullptr); s.depth_memory = VK_NULL_HANDLE; }
				if (s.msaa_view) { vkDestroyImageView(m_device, s.msaa_view, nullptr); s.msaa_view = VK_NULL_HANDLE; }
				if (s.msaa_image) { vkDestroyImage(m_device, s.msaa_image, nullptr); s.msaa_image = VK_NULL_HANDLE; }
				if (s.msaa_memory) { vkFreeMemory(m_device, s.msaa_memory, nullptr); s.msaa_memory = VK_NULL_HANDLE; }
				for (auto& v : s.views) if (v) vkDestroyImageView(m_device, v, nullptr);
				s.views.clear();
				s.images.clear();
				if (s.swapchain) { vkDestroySwapchainKHR(m_device, s.swapchain, nullptr); s.swapchain = VK_NULL_HANDLE; }
				for (auto& f : s.fences) if (f) vkDestroyFence(m_device, f, nullptr);
				s.fences.clear();
				for (VkSemaphore sem : s.render_dones) if (sem) vkDestroySemaphore(m_device, sem, nullptr);
				for (VkSemaphore sem : s.image_avails) if (sem) vkDestroySemaphore(m_device, sem, nullptr);
				s.render_dones.clear();
				s.image_avails.clear();
				if (s.cmd_pool) { vkDestroyCommandPool(m_device, s.cmd_pool, nullptr); s.cmd_pool = VK_NULL_HANDLE; }
				if (s.surface) { vkDestroySurfaceKHR(m_instance, s.surface, nullptr); s.surface = VK_NULL_HANDLE; }
#if !defined(_WIN32) && !defined(__APPLE__)
				if (s.xcb_conn) { xcb_disconnect(s.xcb_conn); s.xcb_conn = nullptr; }
#endif
			}

			// 全部窗口销毁 + XCB 连接（析构用）
			void destroy_all_surfaces()
			{
				if (!m_device) return;
				for (auto& kv : m_surfaces) destroy_surface_state(kv.second);
				m_surfaces.clear();
				m_active = nullptr;
			}

			void destroy_programs()
			{
				for (auto& kv : m_programs)
				{
					program_t& p = kv.second;
					for (auto& pp : p.pipelines) if (pp.second) vkDestroyPipeline(m_device, pp.second, nullptr);
					p.pipelines.clear();
					if (p.layout) vkDestroyPipelineLayout(m_device, p.layout, nullptr);
					if (p.desc_sets[0] && p.desc_pool) vkFreeDescriptorSets(m_device, p.desc_pool, MAX_DESC_SETS, p.desc_sets);
					if (p.desc_pool) vkDestroyDescriptorPool(m_device, p.desc_pool, nullptr);
					if (p.desc_layout) vkDestroyDescriptorSetLayout(m_device, p.desc_layout, nullptr);
					if (p.ubo_memory) vkUnmapMemory(m_device, p.ubo_memory);
					if (p.ubo_buffer) vkDestroyBuffer(m_device, p.ubo_buffer, nullptr);
					// ubo_memory 由内存池统一释放
					if (p.vs) vkDestroyShaderModule(m_device, p.vs, nullptr);
					if (p.fs) vkDestroyShaderModule(m_device, p.fs, nullptr);
				}
				m_programs.clear();
			}

			void destroy_vertex_buffers()
			{
				for (auto& kv : m_vertex_buffers)
				{
					if (kv.second.buffer) vkDestroyBuffer(m_device, kv.second.buffer, nullptr);
					// memory 由内存池统一释放
				}
				m_vertex_buffers.clear();
				for (auto& kv : m_index_buffers)
				{
					if (kv.second.buffer) vkDestroyBuffer(m_device, kv.second.buffer, nullptr);
					// memory 由内存池统一释放
				}
				m_index_buffers.clear();
			}

			void destroy_textures()
			{
				for (auto& kv : m_textures)
				{
					texture_t& t = kv.second;
					if (t.sampler) vkDestroySampler(m_device, t.sampler, nullptr);
					// 仅销毁自有的 view/image/memory；离屏/立方体注册的纹理（owns_image=false）
					// 其 image/view 属于离屏目标，已由 delete_offscreen 销毁——此处再销毁会二次释放导致驱动崩溃
					if (t.owns_image)
					{
						if (t.view) vkDestroyImageView(m_device, t.view, nullptr);
						if (t.image) vkDestroyImage(m_device, t.image, nullptr);
						if (t.memory) vkFreeMemory(m_device, t.memory, nullptr);
					}
				}
				m_textures.clear();
			}

		public:
			virtual ~vk_backend()
			{
				if (m_device) vkDeviceWaitIdle(m_device);   // 析构前同步队列（防在飞命令/呈现未完成即销毁资源）
				destroy_all_surfaces();
				destroy_programs();
				destroy_vertex_buffers();
				destroy_textures();
				for (auto& kv : m_computes) shader_compute_delete(kv.first);
				m_computes.clear();
				for (auto& kv : m_storages) if (kv.second.buffer) vkDestroyBuffer(m_device, kv.second.buffer, nullptr);
				m_storages.clear();
				if (m_query_pool) { vkDestroyQueryPool(m_device, m_query_pool, nullptr); m_query_pool = VK_NULL_HANDLE; }
				if (m_timestamp_pool) { vkDestroyQueryPool(m_device, m_timestamp_pool, nullptr); m_timestamp_pool = VK_NULL_HANDLE; }
				// 离屏/立方体目标：必须先于 pool_destroy 销毁（其 image 引用内存池内存，
				// 池释放后 device 隐式销毁悬空 image 会导致驱动崩溃）
				for (auto& kv : m_offscreens)
				{
					offscreen_t& o = kv.second;
					if (o.framebuffer) vkDestroyFramebuffer(m_device, o.framebuffer, nullptr);
					if (o.render_pass) vkDestroyRenderPass(m_device, o.render_pass, nullptr);
					for (auto& v : o.views) if (v) vkDestroyImageView(m_device, v, nullptr);
					for (auto& img : o.images) if (img) vkDestroyImage(m_device, img, nullptr);
					for (auto& mem : o.memories) if (mem) vkFreeMemory(m_device, mem, nullptr);
					if (o.depth_view) vkDestroyImageView(m_device, o.depth_view, nullptr);
					if (o.depth_image) vkDestroyImage(m_device, o.depth_image, nullptr);
					if (o.depth_memory) vkFreeMemory(m_device, o.depth_memory, nullptr);
				}
				m_offscreens.clear();
				for (auto& kv : m_cubemaps)
				{
					cubemap_t& c = kv.second;
					for (auto& f : c.framebuffers) if (f) vkDestroyFramebuffer(m_device, f, nullptr);
					if (c.render_pass) vkDestroyRenderPass(m_device, c.render_pass, nullptr);
					if (c.cube_view) vkDestroyImageView(m_device, c.cube_view, nullptr);
					for (auto& v : c.face_views) if (v) vkDestroyImageView(m_device, v, nullptr);
					if (c.image) vkDestroyImage(m_device, c.image, nullptr);
					if (c.memory) vkFreeMemory(m_device, c.memory, nullptr);
				}
				m_cubemaps.clear();
				pool_destroy();   // 释放内存池大块
				if (!getenv("XHQM_FULL_DEVICE_DESTROY")) {
					// 默认跳过 vkDestroyDevice/vkDestroyInstance：进程退出时由 OS 回收全部
					// GPU 资源。NVIDIA 驱动（32.0.15.9186）在应用完整销毁路径后于
					// vkDestroyDevice 内部崩溃（EIP=0）；应用侧资源销毁顺序已逐项核对
					// 无双重销毁/悬空引用，属驱动缺陷。设 XHQM_FULL_DEVICE_DESTROY=1
					// 可强制完整销毁（调试用）。
					m_device = VK_NULL_HANDLE; m_instance = VK_NULL_HANDLE;
				} else {
					if (m_device) vkDestroyDevice(m_device, nullptr);
					if (m_instance) vkDestroyInstance(m_instance, nullptr);
				}
				spirv_finalize();
				// 退役：后端的静态资源（sprite::_shader 等 DLL 加载期静态）可能在
				// 本析构之后才析构，其 reset() 会调用 render_backend_active()——
				// 此时必须落到 none 桩而不是已销毁的 this（悬空 map → 崩溃）
				render_backend_retire(this);
			}

			virtual const char* name() const noexcept override { return "vulkan"; }
			virtual bool supports_binary() const noexcept override { return false; }

			virtual bool init(std::string& error) override
			{
				if (m_device) return true;

				VkApplicationInfo app{};
				app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
				app.pApplicationName = "xhqm";
				app.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
				app.apiVersion = VK_API_VERSION_1_3;

				// 平台 surface 扩展
#if defined(_WIN32)
				const char* inst_exts[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME };
				uint inst_ext_count = 2;
#elif defined(__APPLE__)
				// MoltenVK 经 Vulkan loader 时必须启用 portability 枚举（否则枚举到的扩展在实例创建时报
				// VK_ERROR_EXTENSION_NOT_PRESENT）；直链 libMoltenVK 时该扩展同样受支持，无副作用
				const char* inst_exts[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_METAL_SURFACE_EXTENSION_NAME,
					VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME };
				uint inst_ext_count = 3;
#else
				const char* inst_exts[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_XCB_SURFACE_EXTENSION_NAME };
				uint inst_ext_count = 2;
#endif
				VkInstanceCreateInfo ici{};
				ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
				ici.pApplicationInfo = &app;
#if defined(__APPLE__)
				ici.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif
				ici.enabledExtensionCount = inst_ext_count;
				ici.ppEnabledExtensionNames = inst_exts;

				// 验证层（环境变量 XHQM_VALIDATION=1 时启用，不可用则静默跳过）
				const char* validation_layer = "VK_LAYER_KHRONOS_validation";
				{
					const char* env = std::getenv("XHQM_VALIDATION");
					if (env && env[0] == '1')
					{
						std::uint32_t layer_count = 0;
						vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
						std::vector<VkLayerProperties> layers(layer_count);
						vkEnumerateInstanceLayerProperties(&layer_count, layers.data());
						for (auto& l : layers)
						{
							if (std::strcmp(l.layerName, validation_layer) == 0)
							{
								ici.enabledLayerCount = 1;
								ici.ppEnabledLayerNames = &validation_layer;
								break;
							}
						}
					}
				}

				if (vkCreateInstance(&ici, nullptr, &m_instance) != VK_SUCCESS)
				{
					error = "vkCreateInstance failed";
					return false;
				}

				std::uint32_t count = 0;
				vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
				if (count == 0) { error = "no Vulkan physical device"; return false; }
				std::vector<VkPhysicalDevice> devices(count);
				vkEnumeratePhysicalDevices(m_instance, &count, devices.data());
				m_phys = devices[0];

				VkPhysicalDeviceProperties props{};
				vkGetPhysicalDeviceProperties(m_phys, &props);
				m_gpu_name = props.deviceName;
				m_ubo_align = props.limits.minUniformBufferOffsetAlignment ? props.limits.minUniformBufferOffsetAlignment : 256;
				m_timestamp_period = props.limits.timestampPeriod;

				// 深度格式按设备能力选（MoltenVK 不支持 D24_UNORM_S8_UINT/D16_UNORM_S8_UINT，
				// 硬编码会导致 render pass / image view / image 创建全部触发校验错误，桌面驱动虽多数支持但不保证）
				m_depth_stencil_format = pick_depth_format(true);
				m_depth_format = pick_depth_format(false);

				std::uint32_t qcount = 0;
				vkGetPhysicalDeviceQueueFamilyProperties(m_phys, &qcount, nullptr);
				std::vector<VkQueueFamilyProperties> qfams(qcount);
				vkGetPhysicalDeviceQueueFamilyProperties(m_phys, &qcount, qfams.data());
				bool found = false;
				for (std::uint32_t i = 0; i < qcount; ++i)
				{
					if (qfams[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { m_queue_family = i; found = true; break; }
				}
				if (!found) { error = "no graphics queue family"; return false; }

				float priority = 1.0f;
				VkDeviceQueueCreateInfo dqci{};
				dqci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
				dqci.queueFamilyIndex = m_queue_family;
				dqci.queueCount = 1;
				dqci.pQueuePriorities = &priority;

				std::vector<const char*> dev_exts = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
				VkPhysicalDeviceFeatures features{};
				{
					// wideLines 等特性按物理设备能力请求（MoltenVK 不支持宽线，硬要会 VK_ERROR_FEATURE_NOT_PRESENT）
					VkPhysicalDeviceFeatures supported{};
					vkGetPhysicalDeviceFeatures(m_phys, &supported);
					features.wideLines = supported.wideLines ? VK_TRUE : VK_FALSE;
				}

				// VK_EXT_robustness2 的 nullDescriptor：GL 里采样未绑定的 sampler 是合法的（读默认值），
				// Vulkan 默认要求每个被着色器引用的描述符都必须写入过，否则该次绘制使用"从未更新"的描述符
				// （VUID-vkCmdDraw*-None-08114）——引擎大量存在"没纹理就不绑"的路径（阴影/IBL/材质贴图），
				// 逐绑定造兜底纹理成本高且视图类型各异；开启 nullDescriptor 后未写入的描述符合法（读 0），
				// 与 GL 语义对齐。设备不支持时保持原样（仅校验层报错，绘制仍由驱动容错）。
				VkPhysicalDeviceRobustness2FeaturesEXT null_desc{};
				bool has_robustness2 = false;
				bool has_portability_subset = false;   // MoltenVK 等可移植实现必须显式启用该扩展
				{
					uint32_t ext_count = 0;
					vkEnumerateDeviceExtensionProperties(m_phys, nullptr, &ext_count, nullptr);
					std::vector<VkExtensionProperties> exts(ext_count);
					vkEnumerateDeviceExtensionProperties(m_phys, nullptr, &ext_count, exts.data());
					for (auto& e : exts)
					{
						if (std::strcmp(e.extensionName, VK_EXT_ROBUSTNESS_2_EXTENSION_NAME) == 0) has_robustness2 = true;
						if (std::strcmp(e.extensionName, "VK_KHR_portability_subset") == 0) has_portability_subset = true;
					}
					if (has_portability_subset) dev_exts.push_back("VK_KHR_portability_subset");

					if (has_robustness2)
					{
						null_desc.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT;
						VkPhysicalDeviceFeatures2 f2{};
						f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
						f2.pNext = &null_desc;
						vkGetPhysicalDeviceFeatures2(m_phys, &f2);
						if (null_desc.nullDescriptor == VK_TRUE)
						{
							dev_exts.push_back(VK_EXT_ROBUSTNESS_2_EXTENSION_NAME);
						}
						else
						{
							has_robustness2 = false;
							null_desc = VkPhysicalDeviceRobustness2FeaturesEXT{};
						}
					}
				}

				VkDeviceCreateInfo dci{};
				dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
				dci.queueCreateInfoCount = 1;
				dci.pQueueCreateInfos = &dqci;
				dci.enabledExtensionCount = (uint32_t)dev_exts.size();
				dci.ppEnabledExtensionNames = dev_exts.data();
				dci.pEnabledFeatures = &features;
				// 启用了 VK_KHR_portability_subset 就必须把特性结构链进 pNext：
				// 缺省时按“全部不支持”处理，比较采样器（阴影）会被判成不可移植（误报 mutableComparisonSamplers）
				VkPhysicalDevicePortabilitySubsetFeaturesKHR port_features{};
				if (has_portability_subset)
				{
					port_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_FEATURES_KHR;
					VkPhysicalDeviceFeatures2 f2{};
					f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
					f2.pNext = &port_features;
					vkGetPhysicalDeviceFeatures2(m_phys, &f2);
				}
				if (has_robustness2 && null_desc.nullDescriptor == VK_TRUE)
				{
					null_desc.pNext = nullptr;
					if (has_portability_subset) { null_desc.pNext = &port_features; }
					dci.pNext = &null_desc;
				}
				else if (has_portability_subset)
				{
					port_features.pNext = nullptr;
					dci.pNext = &port_features;
				}

				if (vkCreateDevice(m_phys, &dci, nullptr, &m_device) != VK_SUCCESS)
				{
					error = "vkCreateDevice failed";
					return false;
				}
				vkGetDeviceQueue(m_device, m_queue_family, 0, &m_queue);

				// 预建各类默认纹理（绘制录制期不再做一次性提交，行为可预期）
				default_texture_2d();
				default_texture_shadow();
				default_texture_cube();
				default_texture_cube_shadow();

				spirv_initialize();
				return true;
			}

			virtual bool create_surface(void* native_window, uint w, uint h, std::string& error) override
			{
				if (!m_device) { error = "Vulkan device not created"; return false; }

				// 同窗口已存在：尺寸一致 → 仅激活；尺寸变化 → 销毁重建（resize 路径）
				auto old_it = m_surfaces.find(native_window);
				if (old_it != m_surfaces.end())
				{
					if (old_it->second.extent.width == w && old_it->second.extent.height == h)
					{
						m_active = &old_it->second;
						return true;
					}
					surface_state old = std::move(old_it->second);
					m_surfaces.erase(old_it);
					if (m_active == &old) m_active = nullptr;
					destroy_surface_state(old);
				}

				surface_state st;
				st.window = native_window;

#if defined(_WIN32)
				VkWin32SurfaceCreateInfoKHR sci{};
				sci.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
				sci.hinstance = GetModuleHandle(nullptr);
				sci.hwnd = reinterpret_cast<HWND>(native_window);
				if (vkCreateWin32SurfaceKHR(m_instance, &sci, nullptr, &st.surface) != VK_SUCCESS)
				{
					error = "vkCreateWin32SurfaceKHR failed";
					return false;
				}
#elif defined(__APPLE__)
				// macOS：MoltenVK Metal 表面（NSView → CAMetalLayer）
				void* layer = rhi_vk_macos_metal_layer(native_window);
				if (!layer)
				{
					error = "CAMetalLayer unavailable (winId?)";
					return false;
				}
				VkMetalSurfaceCreateInfoEXT sci{};
				sci.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
				sci.pLayer = layer;
				if (vkCreateMetalSurfaceEXT(m_instance, &sci, nullptr, &st.surface) != VK_SUCCESS)
				{
					error = "vkCreateMetalSurfaceEXT failed";
					return false;
				}
#else
				// Linux/XCB：每窗口独立连接（多表面共存时共享连接的 present 有驱动兼容问题）
				xcb_window_t wid = static_cast<xcb_window_t>(reinterpret_cast<std::uintptr_t>(native_window));
				st.xcb_window = wid;
				st.xcb_conn = xcb_connect(nullptr, nullptr);
				if (!st.xcb_conn || xcb_connection_has_error(st.xcb_conn))
				{
					if (st.xcb_conn) { xcb_disconnect(st.xcb_conn); st.xcb_conn = nullptr; }
					error = "xcb_connect failed (DISPLAY?)";
					return false;
				}
				VkXcbSurfaceCreateInfoKHR sci{};
				sci.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
				sci.connection = st.xcb_conn;
				sci.window = wid;
				if (vkCreateXcbSurfaceKHR(m_instance, &sci, nullptr, &st.surface) != VK_SUCCESS)
				{
					error = "vkCreateXcbSurfaceKHR failed";
					return false;
				}
#endif

				VkSurfaceCapabilitiesKHR caps{};
				vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_phys, st.surface, &caps);

				VkSwapchainCreateInfoKHR swci{};
				swci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
				swci.surface = st.surface;
				swci.minImageCount = (caps.minImageCount < 2) ? 2 : caps.minImageCount;
				swci.imageFormat = m_color_format;
				swci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
				// 优先采用表面当前实际尺寸（WM 可能已调整窗口）；UINT32_MAX=未定义尺寸时退回调用方尺寸
				st.extent.width = (caps.currentExtent.width != 0xFFFFFFFFu && caps.currentExtent.width) ? caps.currentExtent.width : (w ? w : caps.currentExtent.width);
				st.extent.height = (caps.currentExtent.height != 0xFFFFFFFFu && caps.currentExtent.height) ? caps.currentExtent.height : (h ? h : caps.currentExtent.height);
				swci.imageExtent = st.extent;
				swci.imageArrayLayers = 1;
				// 附加 TRANSFER_SRC：read_pixels/--snapshot 需要把交换链图像拷回主机内存
				// （缺这个 usage 时 vkCmdCopyImageToBuffer 非法，读回恒为全黑，无法自检画面）
				swci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
				swci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
				swci.preTransform = caps.currentTransform;
				// 透明支持：表面支持非不透明合成时用 POST/PRE_MULTIPLIED，否则回退 OPAQUE
				swci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
				if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR) swci.compositeAlpha = VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
				else if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR) swci.compositeAlpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
				swci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
				swci.clipped = VK_TRUE;

				if (vkCreateSwapchainKHR(m_device, &swci, nullptr, &st.swapchain) != VK_SUCCESS)
				{
					error = "vkCreateSwapchainKHR failed";
					return false;
				}

				std::uint32_t ic = 0;
				vkGetSwapchainImagesKHR(m_device, st.swapchain, &ic, nullptr);
				st.images.resize(ic);
				vkGetSwapchainImagesKHR(m_device, st.swapchain, &ic, st.images.data());

				st.views.resize(ic);
				for (std::uint32_t i = 0; i < ic; ++i)
				{
					VkImageViewCreateInfo vci{};
					vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
					vci.image = st.images[i];
					vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
					vci.format = m_color_format;
					vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					vci.subresourceRange.levelCount = 1;
					vci.subresourceRange.layerCount = 1;
					vkCreateImageView(m_device, &vci, nullptr, &st.views[i]);
				}

				// 深度缓冲（D32，用于深度测试）
				{
					VkImageCreateInfo dici{};
					dici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
					dici.imageType = VK_IMAGE_TYPE_2D;
					dici.format = m_depth_stencil_format;   // 深度 + stencil 合并（按设备能力选）
					dici.extent = { st.extent.width, st.extent.height, 1 };
					dici.mipLevels = 1;
					dici.arrayLayers = 1;
					dici.samples = msaa_flag();
					dici.tiling = VK_IMAGE_TILING_OPTIMAL;
					dici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
					dici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
					dici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
					vkCreateImage(m_device, &dici, nullptr, &st.depth_image);

					VkMemoryRequirements mr{};
					vkGetImageMemoryRequirements(m_device, st.depth_image, &mr);
					VkMemoryAllocateInfo mai{};
					mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
					mai.allocationSize = mr.size;
					mai.memoryTypeIndex = find_memory_type(m_phys, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
					vkAllocateMemory(m_device, &mai, nullptr, &st.depth_memory);
					vkBindImageMemory(m_device, st.depth_image, st.depth_memory, 0);

					VkImageViewCreateInfo vci{};
					vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
					vci.image = st.depth_image;
					vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
					vci.format = m_depth_stencil_format;
					vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
					vci.subresourceRange.levelCount = 1;
					vci.subresourceRange.layerCount = 1;
					vkCreateImageView(m_device, &vci, nullptr, &st.depth_view);
				}

				// MSAA 多采样颜色附件（m_msaa > 1 时，resolve 到 swapchain）
				if (m_msaa > 1)
				{
					VkImageCreateInfo mici{};
					mici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
					mici.imageType = VK_IMAGE_TYPE_2D;
					mici.format = m_color_format;
					mici.extent = { st.extent.width, st.extent.height, 1 };
					mici.mipLevels = 1;
					mici.arrayLayers = 1;
					mici.samples = msaa_flag();
					mici.tiling = VK_IMAGE_TILING_OPTIMAL;
					mici.usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
					mici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
					mici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
					vkCreateImage(m_device, &mici, nullptr, &st.msaa_image);

					VkMemoryRequirements mr{};
					vkGetImageMemoryRequirements(m_device, st.msaa_image, &mr);
					VkMemoryAllocateInfo mai{};
					mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
					mai.allocationSize = mr.size;
					mai.memoryTypeIndex = find_memory_type(m_phys, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
					vkAllocateMemory(m_device, &mai, nullptr, &st.msaa_memory);
					vkBindImageMemory(m_device, st.msaa_image, st.msaa_memory, 0);

					VkImageViewCreateInfo vci{};
					vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
					vci.image = st.msaa_image;
					vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
					vci.format = m_color_format;
					vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					vci.subresourceRange.levelCount = 1;
					vci.subresourceRange.layerCount = 1;
					vkCreateImageView(m_device, &vci, nullptr, &st.msaa_view);
				}

				VkAttachmentDescription att{};
				att.format = m_color_format;
				att.samples = msaa_flag();
				att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
				att.storeOp = (m_msaa > 1) ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
				att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
				att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
				att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				att.finalLayout = (m_msaa > 1) ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

				VkAttachmentDescription depthAtt{};
				depthAtt.format = m_depth_stencil_format;
				depthAtt.samples = msaa_flag();
				depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
				depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
				depthAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
				depthAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
				depthAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				depthAtt.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

				if (m_msaa > 1)
				{
					// 多采样：附件 0=多采样颜色, 1=resolve(单采样), 2=多采样深度
					VkAttachmentDescription resolveAtt{};
					resolveAtt.format = m_color_format;
					resolveAtt.samples = VK_SAMPLE_COUNT_1_BIT;
					resolveAtt.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
					resolveAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
					resolveAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
					resolveAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
					resolveAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
					resolveAtt.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

					VkAttachmentReference attRef{};
					attRef.attachment = 0;
					attRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
					VkAttachmentReference resolveRef{};
					resolveRef.attachment = 1;
					resolveRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
					VkAttachmentReference depthRef{};
					depthRef.attachment = 2;
					depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

					VkAttachmentDescription attachments[3] = { att, resolveAtt, depthAtt };
					VkSubpassDescription sub{};
					sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
					sub.colorAttachmentCount = 1;
					sub.pColorAttachments = &attRef;
					sub.pResolveAttachments = &resolveRef;
					sub.pDepthStencilAttachment = &depthRef;

					VkRenderPassCreateInfo rpci{};
					rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
					rpci.attachmentCount = 3;
					rpci.pAttachments = attachments;
					rpci.subpassCount = 1;
					rpci.pSubpasses = &sub;
					if (vkCreateRenderPass(m_device, &rpci, nullptr, &st.render_pass) != VK_SUCCESS)
					{
						error = "vkCreateRenderPass failed";
						return false;
					}

					st.framebuffers.resize(ic);
					for (std::uint32_t i = 0; i < ic; ++i)
					{
						VkImageView fbAtts[3] = { st.msaa_view, st.views[i], st.depth_view };
						VkFramebufferCreateInfo fci{};
						fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
						fci.renderPass = st.render_pass;
						fci.attachmentCount = 3;
						fci.pAttachments = fbAtts;
						fci.width = st.extent.width;
						fci.height = st.extent.height;
						fci.layers = 1;
						vkCreateFramebuffer(m_device, &fci, nullptr, &st.framebuffers[i]);
					}
				}
				else
				{
					VkAttachmentReference attRef{};
					attRef.attachment = 0;
					attRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

					VkAttachmentReference depthRef{};
					depthRef.attachment = 1;
					depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

					VkAttachmentDescription attachments[2] = { att, depthAtt };
					VkSubpassDescription sub{};
					sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
					sub.colorAttachmentCount = 1;
					sub.pColorAttachments = &attRef;
					sub.pDepthStencilAttachment = &depthRef;

					VkRenderPassCreateInfo rpci{};
					rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
					rpci.attachmentCount = 2;
					rpci.pAttachments = attachments;
					rpci.subpassCount = 1;
					rpci.pSubpasses = &sub;
					if (vkCreateRenderPass(m_device, &rpci, nullptr, &st.render_pass) != VK_SUCCESS)
					{
						error = "vkCreateRenderPass failed";
						return false;
					}

					st.framebuffers.resize(ic);
					for (std::uint32_t i = 0; i < ic; ++i)
					{
						VkImageView fbAtts[2] = { st.views[i], st.depth_view };
						VkFramebufferCreateInfo fci{};
						fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
						fci.renderPass = st.render_pass;
						fci.attachmentCount = 2;
						fci.pAttachments = fbAtts;
						fci.width = st.extent.width;
						fci.height = st.extent.height;
						fci.layers = 1;
						vkCreateFramebuffer(m_device, &fci, nullptr, &st.framebuffers[i]);
					}
				}

				VkCommandPoolCreateInfo cpci{};
				cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
				cpci.queueFamilyIndex = m_queue_family;
				cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
				vkCreateCommandPool(m_device, &cpci, nullptr, &st.cmd_pool);

				st.cmd_bufs.resize(ic);
				VkCommandBufferAllocateInfo cbai{};
				cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
				cbai.commandPool = st.cmd_pool;
				cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
				cbai.commandBufferCount = ic;
				vkAllocateCommandBuffers(m_device, &cbai, st.cmd_bufs.data());

				VkSemaphoreCreateInfo sci2{};
				sci2.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
				st.image_avails.resize(ic);
				st.render_dones.resize(ic);
				for (std::uint32_t i = 0; i < ic; ++i)
				{
					vkCreateSemaphore(m_device, &sci2, nullptr, &st.image_avails[i]);
					vkCreateSemaphore(m_device, &sci2, nullptr, &st.render_dones[i]);
				}

				st.fences.resize(ic);
				VkFenceCreateInfo fci2{};
				fci2.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
				fci2.flags = VK_FENCE_CREATE_SIGNALED_BIT;
				for (std::uint32_t i = 0; i < ic; ++i) vkCreateFence(m_device, &fci2, nullptr, &st.fences[i]);
				// 在飞帧数上界（各表面取最大）：描述符集回卷时用来判断「上一圈那帧是否已完成」
				if (ic > m_max_fences) m_max_fences = ic;

				m_surfaces[native_window] = std::move(st);
				m_active = &m_surfaces[native_window];
				return true;
			}

			virtual void destroy_surface(void* native_window = nullptr) override
			{
				if (native_window)
				{
					auto it = m_surfaces.find(native_window);
					if (it == m_surfaces.end()) return;
					surface_state s = std::move(it->second);
					bool was_active = (m_active == &it->second);
					m_surfaces.erase(it);
					if (was_active)
					{
						m_active = nullptr;
						if (!m_surfaces.empty()) m_active = &m_surfaces.begin()->second;
					}
					destroy_surface_state(s);
				}
				else destroy_all_surfaces();
			}

			virtual void begin_frame(std::string& error) override
			{
				surface_state* st = m_active;
				if (!st || !st->swapchain) { error = "no swapchain"; return; }
				++m_frame_serial;    // 帧序号（描述符集回卷的安全性判据用它）
				vkWaitForFences(m_device, 1, &st->fences[st->frame_index], VK_TRUE, UINT64_MAX);
				// 等待返回后 fence 处于已信号态；vkQueueSubmit 要求 fence 未信号，须先复位，
				// 否则提交未定义行为（驱动若不复位则后续等待立即返回，帧间同步全失 → 闪烁）
				vkResetFences(m_device, 1, &st->fences[st->frame_index]);
				// 记录各 program 本帧起始 UBO 计数（环形过冲检测基准）与描述符集轮转基准。
				// 帧起点把 desc_rot 也推进一格：否则本帧第一次绘制会覆写上一帧最后一次绘制用的
				// 那份集，而那一帧可能仍在飞。
				for (auto& kv : m_programs)
				{
					kv.second.ubo_frame_start = kv.second.ubo_slot;
					++kv.second.desc_rot;
					kv.second.desc_frame_rot = kv.second.desc_rot;
				}
				// 兜底：swapchain 图数超过描述符集份数时，帧间强制同步，保证轮转集不再被使用（正确性优先）
				if (st->images.size() > MAX_DESC_SETS) vkDeviceWaitIdle(m_device);
				VkResult r = vkAcquireNextImageKHR(m_device, st->swapchain, UINT64_MAX, st->image_avails[st->frame_index], VK_NULL_HANDLE, &st->image_index);
				if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) { error = "vkAcquireNextImageKHR failed"; return; }

				VkCommandBuffer cb = st->cmd_bufs[st->image_index];
				vkResetCommandBuffer(cb, 0);
				VkCommandBufferBeginInfo bi{};
				bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
				vkBeginCommandBuffer(cb, &bi);

				VkClearValue clears[3] = {};
				clears[0].color = { { m_clear_color[0], m_clear_color[1], m_clear_color[2], m_clear_color[3] } };
				uint clear_count = 2;
				if (m_msaa > 1) { clears[1].color = { { m_clear_color[0], m_clear_color[1], m_clear_color[2], m_clear_color[3] } }; clears[2].depthStencil = { 1.0f, 0 }; clear_count = 3; }
				else { clears[1].depthStencil = { 1.0f, 0 }; }   // 标准深度：清为 far（1）
				VkRenderPassBeginInfo rp{};
				rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
				rp.renderPass = st->render_pass;
				rp.framebuffer = st->framebuffers[st->image_index];
				rp.renderArea.extent = st->extent;
				rp.clearValueCount = clear_count;
				rp.pClearValues = clears;
				vkCmdBeginRenderPass(cb, &rp, VK_SUBPASS_CONTENTS_INLINE); st->render_pass_open = true;

				VkViewport vp{};
				// Vulkan NDC y 向下（OpenGL y 向上）：负高度翻转 Y，让引擎的 GL 风格矩阵正确映射
				vp.x = 0.0f;
				vp.y = (float)st->extent.height;
				vp.width = (float)st->extent.width;
				vp.height = -(float)st->extent.height;
				vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
				vkCmdSetViewport(cb, 0, 1, &vp);
				VkRect2D sc{};
				sc.extent = st->extent;
				vkCmdSetScissor(cb, 0, 1, &sc);
				st->frame_active = true;
			}

			virtual void end_frame() override
			{
				surface_state* st = m_active;
				if (!st || !st->swapchain) return;
				if (!st->frame_active) return;   // begin_frame 失败（如表面过期）：本帧无录制，直接退出
				st->frame_active = false;
				VkCommandBuffer cb = st->cmd_bufs[st->image_index];
				if (st->render_pass_open) { vkCmdEndRenderPass(cb); st->render_pass_open = false; }
				vkEndCommandBuffer(cb);

				VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
				VkSubmitInfo si{};
				si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
				si.waitSemaphoreCount = 1;
				si.pWaitSemaphores = &st->image_avails[st->frame_index];
				si.pWaitDstStageMask = &wait_stage;
				si.commandBufferCount = 1;
				si.pCommandBuffers = &cb;
				si.signalSemaphoreCount = 1;
				si.pSignalSemaphores = &st->render_dones[st->image_index];
				vkQueueSubmit(m_queue, 1, &si, st->fences[st->frame_index]);

				VkPresentInfoKHR pi{};
				pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
				pi.waitSemaphoreCount = 1;
				pi.pWaitSemaphores = &st->render_dones[st->image_index];
				pi.swapchainCount = 1;
				pi.pSwapchains = &st->swapchain;
				pi.pImageIndices = &st->image_index;
				VkResult pr = vkQueuePresentKHR(m_queue, &pi);
				if (pr != VK_SUCCESS)
				st->frame_index = (st->frame_index + 1) % st->fences.size();
			}

			virtual uint shader_create(const std::string& vert, const std::string& frag, std::string& error) override
			{
				if (!m_device) { error = "Vulkan device not created"; return 0; }
				std::vector<std::uint32_t> vspv, fspv;
				if (!compile_glsl_to_spv(vert, spirv_stage::vertex, vspv, error)) return 0;
				if (!compile_glsl_to_spv(frag, spirv_stage::fragment, fspv, error)) return 0;
				return build_program(vspv, fspv, error);
			}

			// 从 SPIR-V 构建 program（shader module + 描述符集 + UBO + 管线布局）
			uint build_program(std::vector<std::uint32_t>& vspv, std::vector<std::uint32_t>& fspv, std::string& error)
			{
				program_t p;
				p.vs = create_module(vspv);
				p.fs = create_module(fspv);
				if (!p.vs || !p.fs) { error = "vkCreateShaderModule failed"; return 0; }
				p.vs_spv = vspv;
				p.fs_spv = fspv;

				// 描述符集布局：binding 0 = UBO，binding 1-3 = 纹理采样器，binding 4-7 = 阴影采样器（级联）
				VkDescriptorSetLayoutBinding bindings[10] = {};
				bindings[0].binding = 0;
				bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
				bindings[0].descriptorCount = 1;
				bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
				for (uint i = 0; i < 9; ++i)
				{
					bindings[1 + i].binding = 1 + i;
					bindings[1 + i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
					// binding 4 = 阴影采样器数组（shadowMap[4] 四元素），其余各 1
					bindings[1 + i].descriptorCount = (i == 3) ? 4 : 1;
					bindings[1 + i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
				}
				VkDescriptorSetLayoutCreateInfo dslci{};
				dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
				dslci.bindingCount = 10;
				dslci.pBindings = bindings;
				if (vkCreateDescriptorSetLayout(m_device, &dslci, nullptr, &p.desc_layout) != VK_SUCCESS)
				{
					error = "vkCreateDescriptorSetLayout failed";
					return 0;
				}

				// 描述符池 + 描述符集（每帧在飞一份，共 MAX_DESC_SETS 份轮转）
				VkDescriptorPoolSize pool_sizes[2] = {};
				pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
				pool_sizes[0].descriptorCount = 1 * MAX_DESC_SETS;
				pool_sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				pool_sizes[1].descriptorCount = 12 * MAX_DESC_SETS;   // 3 颜色 + 4 阴影数组 + 5 其它（cube/normal/ao/…）
				VkDescriptorPoolCreateInfo dpci{};
				dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
				// 程序销毁时会 vkFreeDescriptorSets 逐份释放，池必须带 FREE_DESCRIPTOR_SET_BIT
				dpci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
				dpci.maxSets = MAX_DESC_SETS;
				dpci.poolSizeCount = 2;
				dpci.pPoolSizes = pool_sizes;
				if (vkCreateDescriptorPool(m_device, &dpci, nullptr, &p.desc_pool) != VK_SUCCESS)
				{
					error = "vkCreateDescriptorPool failed";
					return 0;
				}

				VkDescriptorSetAllocateInfo dsai{};
				dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
				dsai.descriptorPool = p.desc_pool;
				dsai.descriptorSetCount = MAX_DESC_SETS;
				VkDescriptorSetLayout layouts[MAX_DESC_SETS];
				for (uint i = 0; i < MAX_DESC_SETS; ++i) layouts[i] = p.desc_layout;
				dsai.pSetLayouts = layouts;
				if (vkAllocateDescriptorSets(m_device, &dsai, p.desc_sets) != VK_SUCCESS)
				{
					error = "vkAllocateDescriptorSets failed";
					return 0;
				}

				// UBO（环形 slot 大缓冲，每个 draw 固化一份独立 uniform；slot 按设备对齐）
				// 独占内存 + 常驻映射：与池内其它缓冲共享同一 VkDeviceMemory 时，双重 map 会 MEMORY_MAP_FAILED（MoltenVK 严格执行规范）
				VkDeviceSize ustride = (UBO_SIZE + m_ubo_align - 1) / m_ubo_align * m_ubo_align;
				VkDeviceSize ubo_bytes = ustride * UBO_SLOTS;
				{
					VkBufferCreateInfo bci{};
					bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
					bci.size = ubo_bytes;
					bci.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
					bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
					if (vkCreateBuffer(m_device, &bci, nullptr, &p.ubo_buffer) != VK_SUCCESS) { error = "ubo buffer create failed"; return 0; }
					VkMemoryRequirements mr{};
					vkGetBufferMemoryRequirements(m_device, p.ubo_buffer, &mr);
					uint32_t typeIndex = find_memory_type(m_phys, mr.memoryTypeBits,
						VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
					VkMemoryAllocateInfo mai{};
					mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
					mai.allocationSize = mr.size;
					mai.memoryTypeIndex = typeIndex;
					if (vkAllocateMemory(m_device, &mai, nullptr, &p.ubo_memory) != VK_SUCCESS) { error = "ubo memory alloc failed"; return 0; }
					vkBindBufferMemory(m_device, p.ubo_buffer, p.ubo_memory, 0);
					p.ubo_offset = 0;
					if (vkMapMemory(m_device, p.ubo_memory, 0, ubo_bytes, 0, &p.ubo_data) != VK_SUCCESS) { error = "ubo map failed"; return 0; }
					// CPU 侧暂存（uniform 的写入目标与 draw 时的复制源）：零初始化，
					// 未写过的块恒为 0，配合 ubo_used 只复制用到的区间
					p.ubo_stage.assign(UBO_SIZE, 0);
				}

				// binding 0（动态 UBO）的 buffer 信息全程不变 → 创建时一次性写入全部 8 份集；
				// 每个 draw 用 vkCmdBindDescriptorSets 的 dynamicOffset 区分数据槽，无需逐帧改写描述符集
				VkDescriptorBufferInfo dbi{};
				dbi.buffer = p.ubo_buffer;
				dbi.offset = 0;
				dbi.range = UBO_SIZE;
				VkWriteDescriptorSet wds0{};
				wds0.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				wds0.dstBinding = 0;
				wds0.descriptorCount = 1;
				wds0.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
				wds0.pBufferInfo = &dbi;
				for (uint i = 0; i < MAX_DESC_SETS; ++i)
				{
					wds0.dstSet = p.desc_sets[i];
					vkUpdateDescriptorSets(m_device, 1, &wds0, 0, nullptr);
				}

				// 管线布局（含描述符集布局）
				VkPipelineLayoutCreateInfo plci{};
				plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
				plci.setLayoutCount = 1;
				plci.pSetLayouts = &p.desc_layout;
				vkCreatePipelineLayout(m_device, &plci, nullptr, &p.layout);

				uint id = ++m_program_counter;
				m_programs[id] = p;
				return id;
			}

			virtual uint shader_create(const shader_binary& bin, std::string& error) override
			{
				if (!m_device) { error = "Vulkan device not created"; return 0; }
				// 解包 blob = [vs_size(4B)][vs_spv][fs_size(4B)][fs_spv]
				const unsigned char* p = bin.blob.data();
				size_t remain = bin.blob.size();
				auto read_u32 = [&]() -> std::uint32_t
				{
					if (remain < 4) return 0;
					std::uint32_t v = 0;
					std::memcpy(&v, p, 4);
					p += 4; remain -= 4;
					return v;
				};
				std::uint32_t vs_size = read_u32();
				std::uint32_t fs_size = read_u32();
				if (vs_size == 0 || fs_size == 0 || remain < (size_t)vs_size + fs_size)
				{
					error = "invalid shader binary";
					return 0;
				}
				std::vector<std::uint32_t> vspv(vs_size), fspv(fs_size);
				std::memcpy(vspv.data(), p, vs_size * 4); p += vs_size * 4;
				std::memcpy(fspv.data(), p, fs_size * 4);
				return build_program(vspv, fspv, error);
			}

			virtual void shader_delete(uint id) override
			{
				auto it = m_programs.find(id);
				if (it == m_programs.end()) return;
				if (m_device) vkDeviceWaitIdle(m_device);   // 在飞的命令缓冲仍引用管线/UBO/描述符集
				program_t& p = it->second;
				for (auto& kv : p.pipelines) if (kv.second) vkDestroyPipeline(m_device, kv.second, nullptr);
				p.pipelines.clear();
				if (p.layout) vkDestroyPipelineLayout(m_device, p.layout, nullptr);
				if (p.desc_sets[0] && p.desc_pool) vkFreeDescriptorSets(m_device, p.desc_pool, MAX_DESC_SETS, p.desc_sets);
				if (p.desc_pool) vkDestroyDescriptorPool(m_device, p.desc_pool, nullptr);
				if (p.desc_layout) vkDestroyDescriptorSetLayout(m_device, p.desc_layout, nullptr);
				if (p.ubo_memory) { vkUnmapMemory(m_device, p.ubo_memory); vkFreeMemory(m_device, p.ubo_memory, nullptr); }
				if (p.ubo_buffer) vkDestroyBuffer(m_device, p.ubo_buffer, nullptr);
				if (p.vs) vkDestroyShaderModule(m_device, p.vs, nullptr);
				if (p.fs) vkDestroyShaderModule(m_device, p.fs, nullptr);
				m_programs.erase(it);
			}

			virtual bool shader_get_binary(uint id, shader_binary& out) override
			{
				auto it = m_programs.find(id);
				if (it == m_programs.end()) return false;
				program_t& p = it->second;
				if (p.vs_spv.empty() || p.fs_spv.empty()) return false;

				std::uint32_t vs_size = (std::uint32_t)p.vs_spv.size();
				std::uint32_t fs_size = (std::uint32_t)p.fs_spv.size();
				out.blob.resize(8 + vs_size * 4 + fs_size * 4);
				unsigned char* d = out.blob.data();
				std::memcpy(d, &vs_size, 4); d += 4;
				std::memcpy(d, &fs_size, 4); d += 4;
				std::memcpy(d, p.vs_spv.data(), vs_size * 4); d += vs_size * 4;
				std::memcpy(d, p.fs_spv.data(), fs_size * 4);
				out.format = 1;
				out.backend = "vulkan";
				return true;
			}

			virtual uint vertex_create(const values& attrib, const std::vector<uint>& locations, std::string& error) override
			{
				if (!m_device) { error = "Vulkan device not created"; return 0; }
				VkDeviceSize size = attrib.size() * sizeof(fantasy::value);
				VkDeviceMemory memory = VK_NULL_HANDLE;
				VkDeviceSize offset = 0;
				VkBuffer buffer = create_buffer(size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, memory, offset);
				if (!buffer) { error = "vertex buffer create failed"; return 0; }
				void* data = nullptr;
				VkResult mr = vkMapMemory(m_device, memory, offset, size, 0, &data);
				if (mr != VK_SUCCESS || !data)
				{
					fprintf(stderr, "[VK] vertex_create vkMapMemory=%d memory=%llu size=%llu off=%llu\n",
						mr, (unsigned long long)memory, (unsigned long long)size, (unsigned long long)offset);
					error = "vkMapMemory failed";
					return 0;
				}
				std::memcpy(data, attrib.data(), (size_t)size);
				vkUnmapMemory(m_device, memory);

				uint stride = 0;
				for (auto l : locations) stride += l;

				uint id = ++m_vertex_counter;
				vertex_t v;
				v.buffer = buffer;
				v.memory = memory;
				v.offset = offset;
				v.size = size;
				v.locations = locations;
				v.count = (uint)(attrib.size() / (stride ? stride : 1));
				m_vertex_buffers[id] = v;
				return id;
			}

			virtual void vertex_update(uint id, const values& attrib) override
			{
				auto it = m_vertex_buffers.find(id);
				if (it == m_vertex_buffers.end()) return;
				vertex_t& v = it->second;
				VkDeviceSize bytes = (VkDeviceSize)attrib.size() * (VkDeviceSize)sizeof(fantasy::value);
				if (bytes <= 0 || bytes > v.size) return;
				void* data = nullptr;
				vkMapMemory(m_device, v.memory, v.offset, bytes, 0, &data);
				std::memcpy(data, attrib.data(), (size_t)bytes);
				vkUnmapMemory(m_device, v.memory);
			}

			virtual void vertex_delete(uint id) override
			{
				auto it = m_vertex_buffers.find(id);
				if (it == m_vertex_buffers.end()) return;
				if (it->second.buffer) vkDestroyBuffer(m_device, it->second.buffer, nullptr);
				m_vertex_buffers.erase(it);   // memory 由内存池统一释放
			}

			virtual uint index_create(const std::vector<uint>& indices, std::string& error) override
			{
				if (!m_device) { error = "Vulkan device not created"; return 0; }
				VkDeviceSize size = indices.size() * sizeof(uint);
				VkDeviceMemory memory = VK_NULL_HANDLE;
				VkDeviceSize offset = 0;
				VkBuffer buffer = create_buffer(size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, memory, offset);
				void* data = nullptr;
				vkMapMemory(m_device, memory, offset, size, 0, &data);
				std::memcpy(data, indices.data(), (size_t)size);
				vkUnmapMemory(m_device, memory);

				uint id = ++m_index_counter;
				index_t v;
				v.buffer = buffer;
				v.memory = memory;
				v.count = (uint)indices.size();
				m_index_buffers[id] = v;
				return id;
			}

			virtual void index_delete(uint id) override
			{
				auto it = m_index_buffers.find(id);
				if (it == m_index_buffers.end()) return;
				if (it->second.buffer) vkDestroyBuffer(m_device, it->second.buffer, nullptr);
				m_index_buffers.erase(it);   // memory 由内存池统一释放
			}

			virtual uint indirect_create(const std::vector<uint>& commands, std::string& error) override
			{
				if (!m_device) { error = "Vulkan device not created"; return 0; }
				VkDeviceSize size = commands.size() * sizeof(uint);
				VkDeviceMemory memory = VK_NULL_HANDLE;
				VkDeviceSize offset = 0;
				VkBuffer buffer = create_buffer(size, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, memory, offset);
				void* data = nullptr;
				vkMapMemory(m_device, memory, offset, size, 0, &data);
				std::memcpy(data, commands.data(), (size_t)size);
				vkUnmapMemory(m_device, memory);
				uint id = ++m_indirect_counter;
				indirect_t v;
				v.buffer = buffer;
				v.memory = memory;
				v.count = (uint)(commands.size() / 5);
				m_indirect_buffers[id] = v;
				return id;
			}

			virtual void indirect_delete(uint id) override
			{
				auto it = m_indirect_buffers.find(id);
				if (it == m_indirect_buffers.end()) return;
				if (it->second.buffer) vkDestroyBuffer(m_device, it->second.buffer, nullptr);
				m_indirect_buffers.erase(it);
			}

			virtual uint texture_create(const std::vector<rgba>& flat, uint w, uint h, std::string& error) override
			{
				if (!m_device) { error = "Vulkan device not created"; return 0; }
				if (flat.empty() || flat.size() != (size_t)w * h) { error = "illegal texture data"; return 0; }

				VkDeviceSize size = flat.size() * sizeof(rgba);

				// 暂存缓冲
				VkDeviceMemory staging_memory = VK_NULL_HANDLE;
				VkDeviceSize staging_offset = 0;
				VkBuffer staging = create_buffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, staging_memory, staging_offset);
				void* data = nullptr;
				vkMapMemory(m_device, staging_memory, staging_offset, size, 0, &data);
				std::memcpy(data, flat.data(), (size_t)size);
				vkUnmapMemory(m_device, staging_memory);

				// 图像
				uint mips = 1;
				{ uint s = w > h ? w : h; while (s > 1) { s >>= 1; ++mips; } }

				VkImageCreateInfo ici{};
				ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
				ici.imageType = VK_IMAGE_TYPE_2D;
				ici.format = VK_FORMAT_R8G8B8A8_SRGB;
				ici.extent = { w, h, 1 };
				ici.mipLevels = mips;
				ici.arrayLayers = 1;
				ici.samples = VK_SAMPLE_COUNT_1_BIT;
				ici.tiling = VK_IMAGE_TILING_OPTIMAL;
				ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
				ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
				ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				VkImage image = VK_NULL_HANDLE;
				vkCreateImage(m_device, &ici, nullptr, &image);

				VkMemoryRequirements mr{};
				vkGetImageMemoryRequirements(m_device, image, &mr);
				VkMemoryAllocateInfo mai{};
				mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
				mai.allocationSize = mr.size;
				mai.memoryTypeIndex = find_memory_type(m_phys, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
				VkDeviceMemory memory = VK_NULL_HANDLE;
				vkAllocateMemory(m_device, &mai, nullptr, &memory);
				vkBindImageMemory(m_device, image, memory, 0);

				// 布局转换 + 拷贝
				one_shot([&](VkCommandBuffer cb) {
					VkImageMemoryBarrier b1{};
					b1.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
					b1.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
					b1.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
					b1.srcAccessMask = 0;
					b1.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
					b1.image = image;
					b1.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					b1.subresourceRange.levelCount = 1;
					b1.subresourceRange.layerCount = 1;
					vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
						0, 0, nullptr, 0, nullptr, 1, &b1);

					VkBufferImageCopy region{};
					region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					region.imageSubresource.layerCount = 1;
					region.imageExtent = { w, h, 1 };
					vkCmdCopyBufferToImage(cb, staging, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

					// mipmap 链：每级从上一级 blit（src DST→SRC，dst UNDEFINED→DST）
					int32_t mw = (int32_t)w, mh = (int32_t)h;
					for (uint m = 1; m < mips; ++m)
					{
						int32_t nw = mw > 1 ? mw / 2 : 1;
						int32_t nh = mh > 1 ? mh / 2 : 1;

						VkImageMemoryBarrier src_b{};
						src_b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
						src_b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
						src_b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
						src_b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
						src_b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
						src_b.image = image;
						src_b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
						src_b.subresourceRange.baseMipLevel = m - 1;
						src_b.subresourceRange.levelCount = 1;
						src_b.subresourceRange.layerCount = 1;

						VkImageMemoryBarrier dst_b{};
						dst_b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
						dst_b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
						dst_b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
						dst_b.srcAccessMask = 0;
						dst_b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
						dst_b.image = image;
						dst_b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
						dst_b.subresourceRange.baseMipLevel = m;
						dst_b.subresourceRange.levelCount = 1;
						dst_b.subresourceRange.layerCount = 1;

						VkImageMemoryBarrier barrs[2] = { src_b, dst_b };
						vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2, barrs);

						VkImageBlit blit{};
						blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
						blit.srcSubresource.mipLevel = m - 1;
						blit.srcSubresource.baseArrayLayer = 0;
						blit.srcSubresource.layerCount = 1;
						blit.srcOffsets[1] = { mw, mh, 1 };
						blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
						blit.dstSubresource.mipLevel = m;
						blit.dstSubresource.baseArrayLayer = 0;
						blit.dstSubresource.layerCount = 1;
						blit.dstOffsets[1] = { nw, nh, 1 };
						vkCmdBlitImage(cb, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

						mw = nw; mh = nh;
					}

					// 最终：所有 level → SHADER_READ_ONLY
					VkImageMemoryBarrier b2{};
					b2.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
					b2.oldLayout = (mips > 1) ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
					b2.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
					b2.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
					b2.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
					b2.image = image;
					b2.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					b2.subresourceRange.levelCount = (mips > 1) ? (mips - 1) : 1;
					b2.subresourceRange.layerCount = 1;
					vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
						0, 0, nullptr, 0, nullptr, 1, &b2);

					if (mips > 1)
					{
						VkImageMemoryBarrier b3{};
						b3.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
						b3.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
						b3.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
						b3.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
						b3.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
						b3.image = image;
						b3.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
						b3.subresourceRange.baseMipLevel = mips - 1;
						b3.subresourceRange.levelCount = 1;
						b3.subresourceRange.layerCount = 1;
						vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
							0, 0, nullptr, 0, nullptr, 1, &b3);
					}
				});

				vkDestroyBuffer(m_device, staging, nullptr);
				// staging_memory 由内存池统一释放

				// 图像视图
				VkImageViewCreateInfo vci{};
				vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
				vci.image = image;
				vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
				vci.format = VK_FORMAT_R8G8B8A8_SRGB;
				vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				vci.subresourceRange.levelCount = mips;
				vci.subresourceRange.layerCount = 1;
				VkImageView view = VK_NULL_HANDLE;
				vkCreateImageView(m_device, &vci, nullptr, &view);

				// 采样器（M4 sampler 抽象）
				sampler_desc sd;
				sd.mipmap = (mips > 1);
				sd.max_lod = (float)(mips - 1);
				VkSampler sampler = create_sampler(sd, sd.max_lod);

				uint id = ++m_texture_counter;
				texture_t t;
				t.image = image;
				t.memory = memory;
				t.view = view;
				t.sampler = sampler;
				t.w = w;
				t.h = h;
				m_textures[id] = t;
				return id;
			}

			virtual uint texture_create_float(const std::vector<value>& flat, uint w, uint h, std::string& error) override
			{
				if (!m_device) { error = "Vulkan device not created"; return 0; }
				if (flat.size() != (size_t)w * h * 4) { error = "illegal float texture data"; return 0; }
				VkDeviceSize size = (VkDeviceSize)flat.size() * sizeof(value);
				VkDeviceMemory staging_memory = VK_NULL_HANDLE;
				VkDeviceSize staging_offset = 0;
				VkBuffer staging = create_buffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, staging_memory, staging_offset);
				void* data = nullptr;
				vkMapMemory(m_device, staging_memory, staging_offset, size, 0, &data);
				std::memcpy(data, flat.data(), (size_t)size);
				vkUnmapMemory(m_device, staging_memory);

				VkImageCreateInfo ici{};
				ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
				ici.imageType = VK_IMAGE_TYPE_2D;
				ici.format = VK_FORMAT_R32G32B32A32_SFLOAT;
				ici.extent = { w, h, 1 };
				ici.mipLevels = 1;
				ici.arrayLayers = 1;
				ici.samples = VK_SAMPLE_COUNT_1_BIT;
				ici.tiling = VK_IMAGE_TILING_OPTIMAL;
				ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
				ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
				ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				VkImage image = VK_NULL_HANDLE;
				vkCreateImage(m_device, &ici, nullptr, &image);
				VkMemoryRequirements mr{};
				vkGetImageMemoryRequirements(m_device, image, &mr);
				VkMemoryAllocateInfo mai{};
				mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
				mai.allocationSize = mr.size;
				mai.memoryTypeIndex = find_memory_type(m_phys, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
				VkDeviceMemory memory = VK_NULL_HANDLE;
				vkAllocateMemory(m_device, &mai, nullptr, &memory);
				vkBindImageMemory(m_device, image, memory, 0);

				one_shot([&](VkCommandBuffer cb) {
					VkImageMemoryBarrier b{};
					b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
					b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
					b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
					b.srcAccessMask = 0;
					b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
					b.image = image;
					b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					b.subresourceRange.levelCount = 1;
					b.subresourceRange.layerCount = 1;
					vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
					VkBufferImageCopy region{};
					region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					region.imageSubresource.layerCount = 1;
					region.imageExtent = { w, h, 1 };
					vkCmdCopyBufferToImage(cb, staging, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
					b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
					b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
					b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
					b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
					vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
				});
				vkDestroyBuffer(m_device, staging, nullptr);

				VkImageViewCreateInfo vci{};
				vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
				vci.image = image;
				vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
				vci.format = VK_FORMAT_R32G32B32A32_SFLOAT;
				vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				vci.subresourceRange.levelCount = 1;
				vci.subresourceRange.layerCount = 1;
				VkImageView view = VK_NULL_HANDLE;
				vkCreateImageView(m_device, &vci, nullptr, &view);

				sampler_desc sd;
				sd.mipmap = false;
				sd.linear_min = false;   // 浮点数据用最近邻
				sd.linear_mag = false;
				VkSampler sampler = create_sampler(sd, 0.0f);

				uint id = ++m_texture_counter;
				texture_t t;
				t.image = image;
				t.memory = memory;
				t.view = view;
				t.sampler = sampler;
				t.is_float = true;
				t.w = w;
				t.h = h;
				m_textures[id] = t;
				return id;
			}

			virtual void texture_update(uint id, const std::vector<value>& flat) override
			{
				auto it = m_textures.find(id);
				if (it == m_textures.end() || !it->second.is_float) return;
				texture_t& t = it->second;
				if (flat.size() != (size_t)t.w * t.h * 4) return;
				VkDeviceSize size = (VkDeviceSize)flat.size() * sizeof(value);
				VkDeviceMemory staging_memory = VK_NULL_HANDLE;
				VkDeviceSize staging_offset = 0;
				VkBuffer staging = create_buffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, staging_memory, staging_offset);
				void* data = nullptr;
				vkMapMemory(m_device, staging_memory, staging_offset, size, 0, &data);
				std::memcpy(data, flat.data(), (size_t)size);
				vkUnmapMemory(m_device, staging_memory);

				one_shot([&](VkCommandBuffer cb) {
					VkImageMemoryBarrier b{};
					b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
					b.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
					b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
					b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
					b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
					b.image = t.image;
					b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					b.subresourceRange.levelCount = 1;
					b.subresourceRange.layerCount = 1;
					vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
					VkBufferImageCopy region{};
					region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					region.imageSubresource.layerCount = 1;
					region.imageExtent = { t.w, t.h, 1 };
					vkCmdCopyBufferToImage(cb, staging, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
					b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
					b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
					b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
					b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
					vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
				});
				vkDestroyBuffer(m_device, staging, nullptr);
			}

			virtual uint texture_create_compressed(const std::vector<std::uint8_t>& bc1, uint w, uint h, std::string& error) override
			{
				if (!m_device) { error = "Vulkan device not created"; return 0; }
				size_t need = (size_t)((w + 3) / 4) * ((h + 3) / 4) * 8;
				if (bc1.size() < need) { error = "illegal BC1 size"; return 0; }

				VkDeviceMemory staging_memory = VK_NULL_HANDLE;
				VkDeviceSize staging_offset = 0;
				VkBuffer staging = create_buffer((VkDeviceSize)need, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, staging_memory, staging_offset);
				void* data = nullptr;
				vkMapMemory(m_device, staging_memory, staging_offset, (VkDeviceSize)need, 0, &data);
				std::memcpy(data, bc1.data(), need);
				vkUnmapMemory(m_device, staging_memory);

				// BC1 单级（v1 不做 mipmap）
				VkImageCreateInfo ici{};
				ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
				ici.imageType = VK_IMAGE_TYPE_2D;
				ici.format = VK_FORMAT_BC1_RGB_UNORM_BLOCK;
				ici.extent = { w, h, 1 };
				ici.mipLevels = 1;
				ici.arrayLayers = 1;
				ici.samples = VK_SAMPLE_COUNT_1_BIT;
				ici.tiling = VK_IMAGE_TILING_OPTIMAL;
				ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
				ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
				ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				VkImage image = VK_NULL_HANDLE;
				vkCreateImage(m_device, &ici, nullptr, &image);

				VkMemoryRequirements mr{};
				vkGetImageMemoryRequirements(m_device, image, &mr);
				VkMemoryAllocateInfo mai{};
				mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
				mai.allocationSize = mr.size;
				mai.memoryTypeIndex = find_memory_type(m_phys, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
				VkDeviceMemory memory = VK_NULL_HANDLE;
				vkAllocateMemory(m_device, &mai, nullptr, &memory);
				vkBindImageMemory(m_device, image, memory, 0);

				one_shot([&](VkCommandBuffer cb) {
					VkImageMemoryBarrier b1{};
					b1.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
					b1.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
					b1.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
					b1.srcAccessMask = 0;
					b1.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
					b1.image = image;
					b1.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					b1.subresourceRange.levelCount = 1;
					b1.subresourceRange.layerCount = 1;
					vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
						0, 0, nullptr, 0, nullptr, 1, &b1);

					VkBufferImageCopy region{};
					region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					region.imageSubresource.layerCount = 1;
					region.imageExtent = { w, h, 1 };
					vkCmdCopyBufferToImage(cb, staging, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

					VkImageMemoryBarrier b2{};
					b2.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
					b2.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
					b2.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
					b2.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
					b2.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
					b2.image = image;
					b2.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					b2.subresourceRange.levelCount = 1;
					b2.subresourceRange.layerCount = 1;
					vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
						0, 0, nullptr, 0, nullptr, 1, &b2);
				});

				vkDestroyBuffer(m_device, staging, nullptr);

				VkImageViewCreateInfo vci{};
				vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
				vci.image = image;
				vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
				vci.format = VK_FORMAT_BC1_RGB_UNORM_BLOCK;
				vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				vci.subresourceRange.levelCount = 1;
				vci.subresourceRange.layerCount = 1;
				VkImageView view = VK_NULL_HANDLE;
				vkCreateImageView(m_device, &vci, nullptr, &view);

				sampler_desc sd;
				sd.mipmap = false;
				VkSampler sampler = create_sampler(sd, 0.0f);

				uint id = ++m_texture_counter;
				texture_t t;
				t.image = image;
				t.memory = memory;
				t.view = view;
				t.sampler = sampler;
				m_textures[id] = t;
				return id;
			}

			virtual uint texture_create_cubemap(const std::vector<std::vector<rgba>>& faces, uint size, std::string& error) override
			{
				if (!m_device) { error = "Vulkan device not created"; return 0; }
				if (faces.size() < 6) { error = "cubemap needs 6 faces"; return 0; }

				VkDeviceSize face_bytes = (VkDeviceSize)size * size * sizeof(rgba);
				VkDeviceSize total = face_bytes * 6;
				VkDeviceMemory staging_memory = VK_NULL_HANDLE;
				VkDeviceSize staging_offset = 0;
				VkBuffer staging = create_buffer(total, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, staging_memory, staging_offset);
				void* data = nullptr;
				vkMapMemory(m_device, staging_memory, staging_offset, total, 0, &data);
				for (uint i = 0; i < 6; ++i) std::memcpy((char*)data + i * face_bytes, faces[i].data(), (size_t)face_bytes);
				vkUnmapMemory(m_device, staging_memory);

				VkImageCreateInfo ici{};
				ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
				ici.imageType = VK_IMAGE_TYPE_2D;
				ici.format = VK_FORMAT_R8G8B8A8_SRGB;
				ici.extent = { size, size, 1 };
				ici.mipLevels = 1;
				ici.arrayLayers = 6;
				ici.samples = VK_SAMPLE_COUNT_1_BIT;
				ici.tiling = VK_IMAGE_TILING_OPTIMAL;
				ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
				ici.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
				ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
				ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				VkImage image = VK_NULL_HANDLE;
				vkCreateImage(m_device, &ici, nullptr, &image);

				VkMemoryRequirements mr{};
				vkGetImageMemoryRequirements(m_device, image, &mr);
				VkMemoryAllocateInfo mai{};
				mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
				mai.allocationSize = mr.size;
				mai.memoryTypeIndex = find_memory_type(m_phys, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
				VkDeviceMemory memory = VK_NULL_HANDLE;
				vkAllocateMemory(m_device, &mai, nullptr, &memory);
				vkBindImageMemory(m_device, image, memory, 0);

				one_shot([&](VkCommandBuffer cb) {
					VkImageMemoryBarrier b1{};
					b1.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
					b1.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
					b1.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
					b1.srcAccessMask = 0;
					b1.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
					b1.image = image;
					b1.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					b1.subresourceRange.levelCount = 1;
					b1.subresourceRange.layerCount = 6;
					vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b1);

					for (uint i = 0; i < 6; ++i)
					{
						VkBufferImageCopy region{};
						region.bufferOffset = i * face_bytes;
						region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
						region.imageSubresource.baseArrayLayer = i;
						region.imageSubresource.layerCount = 1;
						region.imageExtent = { size, size, 1 };
						vkCmdCopyBufferToImage(cb, staging, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
					}

					VkImageMemoryBarrier b2{};
					b2.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
					b2.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
					b2.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
					b2.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
					b2.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
					b2.image = image;
					b2.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					b2.subresourceRange.levelCount = 1;
					b2.subresourceRange.layerCount = 6;
					vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b2);
				});

				vkDestroyBuffer(m_device, staging, nullptr);

				VkImageViewCreateInfo vci{};
				vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
				vci.image = image;
				vci.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
				vci.format = VK_FORMAT_R8G8B8A8_SRGB;
				vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				vci.subresourceRange.levelCount = 1;
				vci.subresourceRange.layerCount = 6;
				VkImageView view = VK_NULL_HANDLE;
				vkCreateImageView(m_device, &vci, nullptr, &view);

				sampler_desc sd;
				sd.mipmap = false;
				VkSampler sampler = create_sampler(sd, 0.0f);

				uint id = ++m_texture_counter;
				texture_t t;
				t.image = image;
				t.memory = memory;
				t.view = view;
				t.sampler = sampler;
				m_textures[id] = t;
				return id;
			}

			virtual void texture_delete(uint id) override
			{
				auto it = m_textures.find(id);
				if (it == m_textures.end()) return;
				if (it->second.sampler) vkDestroySampler(m_device, it->second.sampler, nullptr);
				if (it->second.owns_image)
				{
					if (it->second.view) vkDestroyImageView(m_device, it->second.view, nullptr);
					if (it->second.image) vkDestroyImage(m_device, it->second.image, nullptr);
					if (it->second.memory) vkFreeMemory(m_device, it->second.memory, nullptr);
				}
				m_textures.erase(it);
			}

			virtual std::uint64_t texture_bindless_handle(uint id) override
			{
				// Vulkan bindless = descriptor indexing；此处返回纹理 id 作索引代理
				return (std::uint64_t)id;
			}

			virtual void use_program(uint program) override { m_current_program = program; }

			// unit → (binding, 数组元素)：0..2 颜色纹理(binding 1..3)，3..6 阴影(binding 4 数组 0..3)，
			// 7/8 normal/ao(binding 8/9)，9 cube 阴影(binding 5 cubeShadow)，10/11 cube 探测(binding 6/7)
			static void unit_binding(uint unit, uint& binding, uint& array_element)
			{
				binding = 1 + unit;
				array_element = 0;
				if (unit >= 3 && unit <= 6) { binding = 4; array_element = unit - 3; }
				else if (unit == 9) { binding = 5; }
				else if (unit == 10) { binding = 6; }
				else if (unit == 11) { binding = 7; }
			}

			// 该 unit 是否为 2D 采样器（可用 2D 默认纹理兜底；cube/shadow 绑定视图类型不同，不做兜底）
			static bool unit_is_2d(uint unit) { return unit <= 2 || unit == 7 || unit == 8; }

			// 默认 2D 纹理（1×1 白）：着色器静态引用了某个 sampler2D 但引擎没绑纹理时的兜底。
			// GL 下未绑定 sampler2D 采样是合法的（读默认值），Vulkan 必须显式写入描述符，
			// 否则该次绘制使用"从未更新过"的描述符（VUID-vkCmdDraw-None-08114），采样结果未定义（常见整屏白）。
			uint default_texture_2d()
			{
				if (m_default_texture) return m_default_texture;
				std::vector<rgba> px(1, 0xffffffffu);   // 不透明白（rgba 的字节序为 R,G,B,A）
				std::string err;
				m_default_texture = texture_create(px, 1, 1, err);
				return m_default_texture;
			}

			// 造 1×1 默认深度纹理（可选立方体 / 比较采样器）：阴影、cube 阴影、IBL 绑定未写入时的兜底。
			// 视图/格式必须与描述符绑定的类型一致（sampler2DShadow 要深度视图 + 比较采样器，
			// samplerCube 要 CUBE 视图），否则反而制造新的描述符违规。
			uint create_default_depth_texture(bool cube, bool compare)
			{
				VkImageCreateInfo ici{};
				ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
				ici.imageType = VK_IMAGE_TYPE_2D;
				ici.format = m_depth_format;
				ici.extent = { 1, 1, 1 };
				ici.mipLevels = 1;
				ici.arrayLayers = cube ? 6 : 1;
				ici.samples = VK_SAMPLE_COUNT_1_BIT;
				ici.tiling = VK_IMAGE_TILING_OPTIMAL;
				ici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
				ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
				ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				if (cube) ici.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
				VkImage image = VK_NULL_HANDLE;
				if (vkCreateImage(m_device, &ici, nullptr, &image) != VK_SUCCESS) return 0;

				VkMemoryRequirements mr{};
				vkGetImageMemoryRequirements(m_device, image, &mr);
				VkMemoryAllocateInfo mai{};
				mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
				mai.allocationSize = mr.size;
				mai.memoryTypeIndex = find_memory_type(m_phys, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
				VkDeviceMemory memory = VK_NULL_HANDLE;
				if (vkAllocateMemory(m_device, &mai, nullptr, &memory) != VK_SUCCESS) { vkDestroyImage(m_device, image, nullptr); return 0; }
				vkBindImageMemory(m_device, image, memory, 0);

				// 深度图内容无所谓，但布局必须是可采样布局（描述符声明 SHADER_READ_ONLY）
				one_shot([&](VkCommandBuffer cb) {
					VkImageMemoryBarrier b{};
					b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
					b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
					b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
					b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
					b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
					b.image = image;
					b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
					b.subresourceRange.levelCount = 1;
					b.subresourceRange.layerCount = cube ? 6 : 1;
					b.srcAccessMask = 0;
					b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
					vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
						0, 0, nullptr, 0, nullptr, 1, &b);
				});

				VkImageViewCreateInfo vci{};
				vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
				vci.image = image;
				vci.viewType = cube ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
				vci.format = m_depth_format;
				vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
				vci.subresourceRange.levelCount = 1;
				vci.subresourceRange.layerCount = cube ? 6 : 1;
				VkImageView view = VK_NULL_HANDLE;
				if (vkCreateImageView(m_device, &vci, nullptr, &view) != VK_SUCCESS)
				{
					vkFreeMemory(m_device, memory, nullptr);
					vkDestroyImage(m_device, image, nullptr);
					return 0;
				}

				sampler_desc sd;
				sd.mipmap = false;
				sd.compare = compare;
				VkSampler sampler = create_sampler(sd, 0.0f);

				uint tid = ++m_texture_counter;
				texture_t t;
				t.image = image;
				t.memory = memory;
				t.view = view;
				t.sampler = sampler;
				t.owns_image = true;
				t.w = 1; t.h = 1;
				m_textures[tid] = t;
				return tid;
			}

			// 默认立方体颜色纹理（1×1 白六面）：irradiance / prefiltered 绑定未写入时的兜底
			uint default_texture_cube()
			{
				if (m_default_cube_tex) return m_default_cube_tex;
				if (!m_device) return 0;
				std::vector<std::vector<rgba>> faces(6, std::vector<rgba>(1, 0xffffffffu));
				std::string err;
				m_default_cube_tex = texture_create_cubemap(faces, 1, err);
				return m_default_cube_tex;
			}

			uint default_texture_shadow()
			{
				if (!m_default_shadow_tex) m_default_shadow_tex = create_default_depth_texture(false, true);
				return m_default_shadow_tex;
			}

			uint default_texture_cube_shadow()
			{
				if (!m_default_cube_shadow_tex) m_default_cube_shadow_tex = create_default_depth_texture(true, true);
				return m_default_cube_shadow_tex;
			}

			// 把 unit 对应的纹理写入"当前轮转份"描述符集；未绑定/未知 id → 按绑定类型用默认纹理兜底
			bool write_texture_binding(program_t& p, uint unit)
			{
				uint binding = 0, array_element = 0;
				unit_binding(unit, binding, array_element);
				uint tex = p.textures[unit];
				auto tit = m_textures.find(tex);
				if (tit == m_textures.end())
				{
					// 未绑定/未知 id：按绑定类型给默认纹理（2D 白 / 深度 / 立方体 / 深度立方体），
					// 保证"没纹理就不绑"的绘制路径在 Vulkan 下也有合法描述符
					if (unit_is_2d(unit)) tex = default_texture_2d();
					else if (unit <= 6) tex = default_texture_shadow();
					else if (unit == 9) tex = default_texture_cube_shadow();
					else if (unit == 10 || unit == 11) tex = default_texture_cube();
					else return false;
					tit = m_textures.find(tex);
					if (tit == m_textures.end()) return false;
				}

				VkDescriptorImageInfo dii{};
				dii.sampler = tit->second.sampler;
				dii.imageView = tit->second.view;
				dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				VkWriteDescriptorSet wds{};
				wds.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				wds.dstSet = current_desc_set(p);
				wds.dstBinding = binding;
				wds.dstArrayElement = array_element;
				wds.descriptorCount = 1;
				wds.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				wds.pImageInfo = &dii;
				vkUpdateDescriptorSets(m_device, 1, &wds, 0, nullptr);
				p.texture_rot[unit] = p.desc_rot;   // 按本 program 自己的轮转记账（原来是全局计数）
				return true;
			}

			// 描述符集按绘制轮转，轮转后新的一份是空的 → 绘制前把该 program 已知的绑定补写到当前份。
			// 2D 绑定即使从未 bind_texture 也要写默认纹理：着色器可能静态引用了它。
			void flush_textures(program_t& p)
			{
				for (uint u = 0; u < 12; ++u)
				{
					if (p.texture_rot[u] == p.desc_rot) continue;
					write_texture_binding(p, u);
				}
			}

			// uniform 写入：写进 CPU 侧暂存，并把覆盖到的 64B 块标记为「已使用」。
			// 「已使用」只置位、不清除 —— UBO 是有状态的块，一次绘制要读到本次绘制之前设定的字段
			//（例如只在初始化时设过一次的值），而目的地是本次绘制独占的新 slot，
			// 所以必须把「所有可能被着色器读到的块」都带过去，而不是只带本次改过的块。
			void ubo_write(program_t& p, uint off, const void* src, uint bytes)
			{
				if (p.ubo_stage.empty() || !src || bytes == 0) return;
				if (off + bytes > (uint)p.ubo_stage.size()) return;
				std::memcpy(p.ubo_stage.data() + off, src, bytes);
				const uint b0 = off / UBO_BLK;
				const uint b1 = (off + bytes - 1) / UBO_BLK;
				for (uint b = b0; b <= b1; ++b) p.ubo_used[b >> 6] |= (std::uint64_t(1) << (b & 63));
			}

			// 把本次绘制的 uniform 状态固化进本次绘制独占的 slot（必须在命令录制期做，
			// 不能在执行期读共享暂存 —— 这是原注释记的跨帧竞态）。
			// 与原先 memcpy(slot, ubo_data, UBO_SIZE) 等价：写过的块逐字节相同；
			// 没写过的块着色器不会读（原先读到的是映射内存的原始内容，同样无意义），
			// 只是不再把这段死区搬一遍。
			void emit_ubo(program_t& p, uint dynamicOffset)
			{
				if (!p.ubo_data || p.ubo_stage.empty()) return;
				char* dst = (char*)p.ubo_data + dynamicOffset;
				const char* src = p.ubo_stage.data();
				uint b = 0;
				while (b < UBO_BLKS)
				{
					if (!(p.ubo_used[b >> 6] & (std::uint64_t(1) << (b & 63)))) { ++b; continue; }
					uint e = b;
					while (e < UBO_BLKS && (p.ubo_used[e >> 6] & (std::uint64_t(1) << (e & 63)))) ++e;
					uint off = b * UBO_BLK;
					uint end = e * UBO_BLK;
					if (end > UBO_SIZE) end = UBO_SIZE;
					std::memcpy(dst + off, src + off, end - off);
					b = e;
				}
			}

			virtual void bind_texture(uint unit, uint texture) override
			{
				auto pit = m_programs.find(m_current_program);
				if (pit == m_programs.end()) return;
				program_t& p = pit->second;
				if (unit >= 12) return;   // 有效 unit 0..11（binding 1..9 + cubeShadow 5 + irradiance 6 + prefilter 7）
				p.textures[unit] = texture;
				write_texture_binding(p, unit);
			}

			virtual void uniform_matrix(uint program, const char* name, const value* data, uint count) override
			{
				auto it = m_programs.find(program);
				if (it == m_programs.end() || !it->second.ubo_data) return;
				int off = ubo_offset(name);
				if (off < 0) return;
				uint bytes = count * 16 * sizeof(value);
				if (off + bytes > UBO_SIZE) return;
				ubo_write(it->second, (uint)off, data, bytes);
			}

			virtual void uniform_vec4(uint program, const char* name, value x, value y, value z, value w) override
			{
				auto it = m_programs.find(program);
				if (it == m_programs.end() || !it->second.ubo_data) return;
				int off = ubo_offset(name);
				if (off < 0) return;
				value v[4] = { x, y, z, w };
				ubo_write(it->second, (uint)off, v, sizeof(v));
			}

			virtual void uniform_vec3(uint program, const char* name, value x, value y, value z) override
			{
				auto it = m_programs.find(program);
				if (it == m_programs.end() || !it->second.ubo_data) return;
				int off = ubo_offset(name);
				if (off < 0) return;
				value v[3] = { x, y, z };
				ubo_write(it->second, (uint)off, v, sizeof(v));
			}

			virtual void uniform_int(uint program, const char* name, int v) override
			{
				auto it = m_programs.find(program);
				if (it == m_programs.end() || !it->second.ubo_data) return;
				int off = ubo_offset(name);
				if (off < 0) return;
				ubo_write(it->second, (uint)off, &v, sizeof(v));
			}

			virtual void uniform_float(uint program, const char* name, value v) override
			{
				auto it = m_programs.find(program);
				if (it == m_programs.end() || !it->second.ubo_data) return;
				int off = ubo_offset(name);
				if (off < 0) return;
				ubo_write(it->second, (uint)off, &v, sizeof(v));
			}

			virtual void uniform_lights(uint program, uint count, const value* data) override
			{
				auto it = m_programs.find(program);
				if (it == m_programs.end() || !it->second.ubo_data) return;
				if (count > UBO_MAX_LIGHTS) count = UBO_MAX_LIGHTS;
				uint bytes = count * 64;
				ubo_write(it->second, UBO_LIGHTS, data, bytes);
				int lc = (int)count;
				// 原为硬编码偏移 304（== ubo_flag(FLAG_LIGHT_COUNT)）；这里换成常量，避免布局一改就静默错位
				ubo_write(it->second, ubo_flag(FLAG_LIGHT_COUNT), &lc, sizeof(lc));
			}

			virtual void draw(uint vertex, rhi_primitive prim, uint first, uint count) override
			{
				if (!swapchain_ready()) return;
				auto pit = m_programs.find(m_current_program);
				if (pit == m_programs.end()) return;
				program_t& p = pit->second;
				auto vit = m_vertex_buffers.find(vertex);
				if (vit == m_vertex_buffers.end()) return;

				// 按（顶点布局 + 拓扑 + 混合 + 深度）缓存/创建管线
				VkPrimitiveTopology topo = prim_topology(prim);
				size_t key = pipeline_key(layout_hash(vit->second.locations), topo);
				VkPipeline pipeline = p.pipelines[key];
				if (!pipeline)
				{
					pipeline = create_pipeline(p.vs, p.fs, p.layout, vit->second.locations, topo);
					p.pipelines[key] = pipeline;
				}
				if (!pipeline) return;

				// （UBO 描述符已在创建时写入全部 8 份集，无需逐帧改写）

				VkCommandBuffer cb = m_active->cmd_bufs[m_active->image_index];

				// 分配环形 slot（slot0 保留不用；uniform 暂存已移到 CPU 侧 ubo_stage，
				// 见 UBO_SLOTS 注释），把本次绘制的 uniform 固化成命令流内联数据（每个 draw 独立）
				uint slot = 1 + (p.ubo_slot++ % (UBO_SLOTS - 1));
				uint ustride = (UBO_SIZE + m_ubo_align - 1) / m_ubo_align * m_ubo_align;
				uint dynamicOffset = slot * ustride;
				// 点大小随 UBO 下发：Vulkan 没有 glPointSize 状态，POINT_LIST 的点尺寸必须由着色器写 gl_PointSize
				ubo_write(p, UBO_CAMERA_POS + 12, &m_point_size, sizeof(float));
				emit_ubo(p, dynamicOffset);   // 记录期把 uniform 固化进本绘制独占的 slot（不能挪到执行期）

				// 描述符集按绘制轮转：把 program 已知绑定（含 2D 兜底）补写到当前份
				flush_textures(p);

				vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
				VkDescriptorSet dset = current_desc_set(p);
				vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, p.layout, 0, 1, &dset, 1, &dynamicOffset);
				VkDeviceSize offsets[] = { 0 };
				vkCmdBindVertexBuffers(cb, 0, 1, &vit->second.buffer, offsets);
				vkCmdSetLineWidth(cb, m_line_width);
				// draw(first, count) 语义与 glDrawArrays 一致：first=起始顶点，count=顶点数量
				uint n = (count ? count : vit->second.count);
				vkCmdDraw(cb, n, m_instances, first, 0);
				advance_desc_set(p);
				++m_draw_calls;
			}

			virtual void draw_indexed(uint vertex, uint index, rhi_primitive prim, uint first, uint count) override
			{
				if (!swapchain_ready()) return;
				auto pit = m_programs.find(m_current_program);
				if (pit == m_programs.end()) return;
				program_t& p = pit->second;
				auto vit = m_vertex_buffers.find(vertex);
				if (vit == m_vertex_buffers.end()) return;
				auto iit = m_index_buffers.find(index);
				if (iit == m_index_buffers.end()) return;

				VkPrimitiveTopology topo = prim_topology(prim);
				size_t key = pipeline_key(layout_hash(vit->second.locations), topo);
				VkPipeline pipeline = p.pipelines[key];
				if (!pipeline)
				{
					pipeline = create_pipeline(p.vs, p.fs, p.layout, vit->second.locations, topo);
					p.pipelines[key] = pipeline;
				}
				if (!pipeline) return;

				// （UBO 描述符已在创建时写入全部 8 份集，无需逐帧改写）

				VkCommandBuffer cb = m_active->cmd_bufs[m_active->image_index];

				uint slot = 1 + (p.ubo_slot++ % (UBO_SLOTS - 1));
				uint ustride = (UBO_SIZE + m_ubo_align - 1) / m_ubo_align * m_ubo_align;
				uint dynamicOffset = slot * ustride;
				// 点大小随 UBO 下发：Vulkan 没有 glPointSize 状态，POINT_LIST 的点尺寸必须由着色器写 gl_PointSize
				ubo_write(p, UBO_CAMERA_POS + 12, &m_point_size, sizeof(float));
				emit_ubo(p, dynamicOffset);   // 记录期把 uniform 固化进本绘制独占的 slot（不能挪到执行期）

				// 描述符集按绘制轮转：把 program 已知绑定（含 2D 兜底）补写到当前份
				flush_textures(p);

				vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
				VkDescriptorSet dset = current_desc_set(p);
				vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, p.layout, 0, 1, &dset, 1, &dynamicOffset);
				VkDeviceSize offsets[] = { 0 };
				vkCmdBindVertexBuffers(cb, 0, 1, &vit->second.buffer, offsets);
				vkCmdBindIndexBuffer(cb, iit->second.buffer, 0, VK_INDEX_TYPE_UINT32);
				vkCmdSetLineWidth(cb, m_line_width);
				uint n = (count ? count : iit->second.count);
				vkCmdDrawIndexed(cb, n, m_instances, first, 0, 0);
				advance_desc_set(p);
				++m_draw_calls;
			}

			virtual void draw_instanced(uint vertex, rhi_primitive prim, uint first, uint count, uint instances) override
			{
				uint prev = m_instances;
				m_instances = instances;
				draw(vertex, prim, first, count);
				m_instances = prev;
			}

			virtual void draw_indexed_instanced(uint vertex, uint index, rhi_primitive prim, uint first, uint count, uint instances) override
			{
				uint prev = m_instances;
				m_instances = instances;
				draw_indexed(vertex, index, prim, first, count);
				m_instances = prev;
			}

			virtual void draw_indexed_indirect(uint vertex, uint index, uint indirect, uint offset, uint count) override
			{
				if (!swapchain_ready()) return;
				auto pit = m_programs.find(m_current_program);
				if (pit == m_programs.end()) return;
				program_t& p = pit->second;
				auto vit = m_vertex_buffers.find(vertex);
				if (vit == m_vertex_buffers.end()) return;
				auto iit = m_index_buffers.find(index);
				if (iit == m_index_buffers.end()) return;
				auto dit = m_indirect_buffers.find(indirect);
				if (dit == m_indirect_buffers.end()) return;

				VkPrimitiveTopology topo = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
				size_t key = pipeline_key(layout_hash(vit->second.locations), topo);
				VkPipeline pipeline = p.pipelines[key];
				if (!pipeline) { pipeline = create_pipeline(p.vs, p.fs, p.layout, vit->second.locations, topo); p.pipelines[key] = pipeline; }
				if (!pipeline) return;

				// （UBO 描述符已在创建时写入全部 8 份集，无需逐帧改写）

				VkCommandBuffer cb = m_active->cmd_bufs[m_active->image_index];
				uint slot = 1 + (p.ubo_slot++ % (UBO_SLOTS - 1));
				uint ustride = (UBO_SIZE + m_ubo_align - 1) / m_ubo_align * m_ubo_align;
				uint dynamicOffset = slot * ustride;
				// 点大小随 UBO 下发：Vulkan 没有 glPointSize 状态，POINT_LIST 的点尺寸必须由着色器写 gl_PointSize
				ubo_write(p, UBO_CAMERA_POS + 12, &m_point_size, sizeof(float));
				emit_ubo(p, dynamicOffset);   // 记录期把 uniform 固化进本绘制独占的 slot（不能挪到执行期）

				// 描述符集按绘制轮转：把 program 已知绑定（含 2D 兜底）补写到当前份
				flush_textures(p);

				vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
				VkDescriptorSet dset = current_desc_set(p);
				vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, p.layout, 0, 1, &dset, 1, &dynamicOffset);
				VkDeviceSize offsets[] = { 0 };
				vkCmdBindVertexBuffers(cb, 0, 1, &vit->second.buffer, offsets);
				vkCmdBindIndexBuffer(cb, iit->second.buffer, 0, VK_INDEX_TYPE_UINT32);
				vkCmdDrawIndexedIndirect(cb, dit->second.buffer, offset * sizeof(uint), count, sizeof(VkDrawIndexedIndirectCommand));
				advance_desc_set(p);
				++m_draw_calls;
			}

			virtual void draw_indexed_indirect_storage(uint vertex, uint index, uint storage_id, uint byte_offset, uint count) override
			{
				if (!swapchain_ready()) return;
				auto pit = m_programs.find(m_current_program);
				if (pit == m_programs.end()) return;
				program_t& p = pit->second;
				auto vit = m_vertex_buffers.find(vertex);
				if (vit == m_vertex_buffers.end()) return;
				auto iit = m_index_buffers.find(index);
				if (iit == m_index_buffers.end()) return;
				auto sit = m_storages.find(storage_id);
				if (sit == m_storages.end()) return;

				VkPrimitiveTopology topo = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
				size_t key = pipeline_key(layout_hash(vit->second.locations), topo);
				VkPipeline pipeline = p.pipelines[key];
				if (!pipeline) { pipeline = create_pipeline(p.vs, p.fs, p.layout, vit->second.locations, topo); p.pipelines[key] = pipeline; }
				if (!pipeline) return;

				// （UBO 描述符已在创建时写入全部 8 份集，无需逐帧改写）

				VkCommandBuffer cb = m_active->cmd_bufs[m_active->image_index];
				uint slot = 1 + (p.ubo_slot++ % (UBO_SLOTS - 1));
				uint ustride = (UBO_SIZE + m_ubo_align - 1) / m_ubo_align * m_ubo_align;
				uint dynamicOffset = slot * ustride;
				// 点大小随 UBO 下发：Vulkan 没有 glPointSize 状态，POINT_LIST 的点尺寸必须由着色器写 gl_PointSize
				ubo_write(p, UBO_CAMERA_POS + 12, &m_point_size, sizeof(float));
				emit_ubo(p, dynamicOffset);   // 记录期把 uniform 固化进本绘制独占的 slot（不能挪到执行期）

				// 描述符集按绘制轮转：把 program 已知绑定（含 2D 兜底）补写到当前份
				flush_textures(p);

				vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
				VkDescriptorSet dset = current_desc_set(p);
				vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, p.layout, 0, 1, &dset, 1, &dynamicOffset);
				VkDeviceSize offsets[] = { 0 };
				vkCmdBindVertexBuffers(cb, 0, 1, &vit->second.buffer, offsets);
				vkCmdBindIndexBuffer(cb, iit->second.buffer, 0, VK_INDEX_TYPE_UINT32);
				vkCmdDrawIndexedIndirect(cb, sit->second.buffer, byte_offset, count, sizeof(VkDrawIndexedIndirectCommand));
				advance_desc_set(p);
				++m_draw_calls;
			}

			virtual uint query_create(std::string& error) override
			{
				if (!m_device) { error = "Vulkan device not created"; return 0; }
				if (!m_query_pool)
				{
					VkQueryPoolCreateInfo qpci{};
					qpci.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
					qpci.queryType = VK_QUERY_TYPE_OCCLUSION;
					qpci.queryCount = QUERY_COUNT;
					if (vkCreateQueryPool(m_device, &qpci, nullptr, &m_query_pool) != VK_SUCCESS)
					{
						error = "vkCreateQueryPool failed";
						return 0;
					}
					m_query_next = 0;
				}
				if (m_query_next >= QUERY_COUNT) { error = "query pool exhausted"; return 0; }
				uint id = ++m_query_counter;
				m_queries[id] = m_query_next++;
				return id;
			}

			virtual void query_delete(uint id) override
			{
				m_queries.erase(id);
			}

			virtual void query_begin(uint id) override
			{
				auto it = m_queries.find(id);
				if (it == m_queries.end() || !swapchain_ready()) return;
				VkCommandBuffer cb = m_active->cmd_bufs[m_active->image_index];
				vkCmdResetQueryPool(cb, m_query_pool, it->second, 1);
				vkCmdBeginQuery(cb, m_query_pool, it->second, 0);
			}

			virtual void query_end(uint id) override
			{
				auto it = m_queries.find(id);
				if (it == m_queries.end() || !swapchain_ready()) return;
				VkCommandBuffer cb = m_active->cmd_bufs[m_active->image_index];
				vkCmdEndQuery(cb, m_query_pool, it->second);
			}

			virtual uint query_result(uint id) override
			{
				auto it = m_queries.find(id);
				if (it == m_queries.end() || !m_query_pool) return 0;
				std::uint64_t result = 0;
				vkGetQueryPoolResults(m_device, m_query_pool, it->second, 1,
					sizeof(result), &result, sizeof(result), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
				return (uint)result;
			}

			virtual uint timestamp_query_create(std::string& error) override
			{
				if (!m_device) { error = "Vulkan device not created"; return 0; }
				if (!m_timestamp_pool)
				{
					VkQueryPoolCreateInfo qpci{};
					qpci.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
					qpci.queryType = VK_QUERY_TYPE_TIMESTAMP;
					qpci.queryCount = TIMESTAMP_COUNT;
					if (vkCreateQueryPool(m_device, &qpci, nullptr, &m_timestamp_pool) != VK_SUCCESS)
					{
						error = "vkCreateQueryPool(timestamp) failed";
						return 0;
					}
					m_timestamp_next = 0;
				}
				if (m_timestamp_next >= TIMESTAMP_COUNT) { error = "timestamp pool exhausted"; return 0; }
				uint id = ++m_timestamp_counter;
				m_timestamps[id] = m_timestamp_next++;
				return id;
			}

			virtual void timestamp_query_delete(uint id) override
			{
				m_timestamps.erase(id);
			}

			virtual void timestamp_record(uint id) override
			{
				auto it = m_timestamps.find(id);
				if (it == m_timestamps.end() || !m_timestamp_pool || !swapchain_ready()) return;
				VkCommandBuffer cb = m_active->cmd_bufs[m_active->image_index];
				vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_timestamp_pool, it->second);
			}

			virtual std::uint64_t timestamp_result(uint id) override
			{
				auto it = m_timestamps.find(id);
				if (it == m_timestamps.end() || !m_timestamp_pool) return 0;
				std::uint64_t result = 0;
				vkGetQueryPoolResults(m_device, m_timestamp_pool, it->second, 1,
					sizeof(result), &result, sizeof(result), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
				return (std::uint64_t)((double)result * (double)m_timestamp_period);
			}

			virtual uint shader_compute_create(const std::string& comp, std::string& error) override
			{
				if (!m_device) { error = "Vulkan device not created"; return 0; }
				std::vector<std::uint32_t> cspv;
				if (!compile_glsl_to_spv(comp, spirv_stage::compute, cspv, error)) return 0;

				compute_program_t p;
				p.cs = create_module(cspv);
				if (!p.cs) { error = "vkCreateShaderModule failed"; return 0; }

				// 描述符集布局：binding 0 = UBO，binding 1..4 = STORAGE_BUFFER（SSBO）
				VkDescriptorSetLayoutBinding bindings[5]{};
				bindings[0].binding = 0;
				bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				bindings[0].descriptorCount = 1;
				bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
				for (uint b = 1; b <= 4; ++b)
				{
					bindings[b].binding = b;
					bindings[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
					bindings[b].descriptorCount = 1;
					bindings[b].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
				}
				VkDescriptorSetLayoutCreateInfo dslci{};
				dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
				dslci.bindingCount = 5;
				dslci.pBindings = bindings;
				if (vkCreateDescriptorSetLayout(m_device, &dslci, nullptr, &p.desc_layout) != VK_SUCCESS)
				{
					error = "vkCreateDescriptorSetLayout failed";
					return 0;
				}

				VkDescriptorPoolSize pool_sizes[2]{};
				pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				pool_sizes[0].descriptorCount = 1;
				pool_sizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				pool_sizes[1].descriptorCount = 4;
				VkDescriptorPoolCreateInfo dpci{};
				dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
				dpci.maxSets = 1;
				dpci.poolSizeCount = 2;
				dpci.pPoolSizes = pool_sizes;
				if (vkCreateDescriptorPool(m_device, &dpci, nullptr, &p.desc_pool) != VK_SUCCESS)
				{
					error = "vkCreateDescriptorPool failed";
					return 0;
				}

				VkDescriptorSetAllocateInfo dsai{};
				dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
				dsai.descriptorPool = p.desc_pool;
				dsai.descriptorSetCount = 1;
				dsai.pSetLayouts = &p.desc_layout;
				VkResult dsr = vkAllocateDescriptorSets(m_device, &dsai, &p.desc_set);
				if (dsr != VK_SUCCESS)
				{
					error = "vkAllocateDescriptorSets failed (compute)";
					return 0;
				}

				VkPipelineLayoutCreateInfo plci{};
				plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
				plci.setLayoutCount = 1;
				plci.pSetLayouts = &p.desc_layout;
				vkCreatePipelineLayout(m_device, &plci, nullptr, &p.layout);

				// compute 管线
				VkComputePipelineCreateInfo cpci{};
				cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
				cpci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
				cpci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
				cpci.stage.module = p.cs;
				cpci.stage.pName = "main";
				cpci.layout = p.layout;
				if (vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &cpci, nullptr, &p.pipeline) != VK_SUCCESS)
				{
					error = "vkCreateComputePipelines failed";
					return 0;
				}

				uint id = ++m_compute_counter;
				m_computes[id] = p;
				return id;
			}

			virtual void shader_compute_delete(uint id) override
			{
				auto it = m_computes.find(id);
				if (it == m_computes.end()) return;
				compute_program_t& p = it->second;
				if (p.pipeline) vkDestroyPipeline(m_device, p.pipeline, nullptr);
				if (p.layout) vkDestroyPipelineLayout(m_device, p.layout, nullptr);
				if (p.desc_set && p.desc_pool) vkFreeDescriptorSets(m_device, p.desc_pool, 1, &p.desc_set);
				if (p.desc_pool) vkDestroyDescriptorPool(m_device, p.desc_pool, nullptr);
				if (p.desc_layout) vkDestroyDescriptorSetLayout(m_device, p.desc_layout, nullptr);
				if (p.cs) vkDestroyShaderModule(m_device, p.cs, nullptr);
				m_computes.erase(it);
			}

			virtual void compute_dispatch(uint program, uint x, uint y, uint z) override
			{
				auto it = m_computes.find(program);
				if (it == m_computes.end()) return;
				compute_program_t& p = it->second;

				auto record = [&](VkCommandBuffer cb)
				{
					vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, p.pipeline);
					vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, p.layout, 0, 1, &p.desc_set, 0, nullptr);
					vkCmdDispatch(cb, x, y, z);
					// 屏障：compute 写 SSBO → 后续 indirect draw / 顶点属性读取
					VkMemoryBarrier barrier{};
					barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
					barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
					barrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
					vkCmdPipelineBarrier(cb,
						VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
						VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
						0, 1, &barrier, 0, nullptr, 0, nullptr);
				};

				if (m_active && m_active->frame_active && m_active->swapchain && !m_active->render_pass_open)
				{
					VkCommandBuffer cb = m_active->cmd_bufs[m_active->image_index];
					record(cb);
				}
				else
				{
					// 离帧同步 compute（初始化/验证场景）：one_shot 提交并等待，随后 CPU 可读回
					one_shot(record);
				}
			}

			virtual uint storage_create(size_t bytes, const void* data, std::string& error) override
			{
				if (!m_device) { error = "Vulkan device not created"; return 0; }
				if (bytes == 0) { error = "empty storage"; return 0; }
				VkDeviceMemory memory = VK_NULL_HANDLE;
				VkDeviceSize offset = 0;
				VkBuffer buffer = create_buffer((VkDeviceSize)bytes,
					VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, memory, offset);
				if (data)
				{
					void* p = nullptr;
					vkMapMemory(m_device, memory, offset, (VkDeviceSize)bytes, 0, &p);
					std::memcpy(p, data, bytes);
					vkUnmapMemory(m_device, memory);
				}
				uint id = ++m_storage_counter;
				storage_t s;
				s.buffer = buffer;
				s.memory = memory;
				s.offset = offset;
				s.size = (VkDeviceSize)bytes;
				m_storages[id] = s;
				return id;
			}

			virtual void storage_delete(uint id) override
			{
				auto it = m_storages.find(id);
				if (it == m_storages.end()) return;
				if (it->second.buffer) vkDestroyBuffer(m_device, it->second.buffer, nullptr);
				m_storages.erase(it);	// memory 由内存池统一释放
			}

			virtual void storage_update(uint id, const void* data, size_t bytes) override
			{
				auto it = m_storages.find(id);
				if (it == m_storages.end() || !data) return;
				if (bytes <= 0 || bytes > (size_t)it->second.size) return;
				void* p = nullptr;
				vkMapMemory(m_device, it->second.memory, it->second.offset, (VkDeviceSize)bytes, 0, &p);
				std::memcpy(p, data, bytes);
				vkUnmapMemory(m_device, it->second.memory);
			}

			virtual void storage_read(uint id, void* out, size_t bytes) override
			{
				auto it = m_storages.find(id);
				if (it == m_storages.end() || !out) return;
				if (bytes <= 0 || bytes > (size_t)it->second.size) return;
				// 存储缓冲用 HOST_VISIBLE|HOST_COHERENT 内存：等待设备空闲后直接映射读回
				vkDeviceWaitIdle(m_device);
				void* p = nullptr;
				vkMapMemory(m_device, it->second.memory, it->second.offset, (VkDeviceSize)bytes, 0, &p);
				std::memcpy(out, p, bytes);
				vkUnmapMemory(m_device, it->second.memory);
			}

			virtual void compute_storage_bind(uint program, uint binding, uint storage_id) override
			{
				if (binding < 1 || binding > 4) return;
				auto cit = m_computes.find(program);
				if (cit == m_computes.end()) return;
				auto sit = m_storages.find(storage_id);
				if (sit == m_storages.end()) return;
				VkDescriptorBufferInfo binfo{};
				binfo.buffer = sit->second.buffer;
				binfo.offset = 0;   // 相对 buffer 自身的偏移（buffer 已按 pool 偏移 vkBindBufferMemory）
				binfo.range = sit->second.size;
				VkWriteDescriptorSet w{};
				w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				w.dstSet = cit->second.desc_set;
				w.dstBinding = binding;
				w.dstArrayElement = 0;
				w.descriptorCount = 1;
				w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				w.pBufferInfo = &binfo;
				vkUpdateDescriptorSets(m_device, 1, &w, 0, nullptr);
			}

			virtual uint create_offscreen(uint w, uint h, uint color_count, std::string& error, bool hdr = false) override
			{
				if (!m_device) { error = "Vulkan device not created"; return 0; }
				if (color_count > 4) { error = "illegal color count"; return 0; }

				offscreen_t o;
				o.w = w; o.h = h; o.color_count = color_count;
				o.images.resize(color_count);
				o.memories.resize(color_count);
				o.views.resize(color_count);
				const bool depth_only = (color_count == 0);

				for (uint i = 0; i < color_count; ++i)
				{
					VkImageCreateInfo ici{};
					ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
					ici.imageType = VK_IMAGE_TYPE_2D;
					ici.format = hdr ? VK_FORMAT_R16G16B16A16_SFLOAT : VK_FORMAT_R8G8B8A8_UNORM;
					ici.extent = { w, h, 1 };
					ici.mipLevels = 1;
					ici.arrayLayers = 1;
					ici.samples = VK_SAMPLE_COUNT_1_BIT;
					ici.tiling = VK_IMAGE_TILING_OPTIMAL;
					ici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
					ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
					ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
					vkCreateImage(m_device, &ici, nullptr, &o.images[i]);
					VkMemoryRequirements mr{};
					vkGetImageMemoryRequirements(m_device, o.images[i], &mr);
					VkMemoryAllocateInfo mai{};
					mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
					mai.allocationSize = mr.size;
					mai.memoryTypeIndex = find_memory_type(m_phys, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
					vkAllocateMemory(m_device, &mai, nullptr, &o.memories[i]);
					vkBindImageMemory(m_device, o.images[i], o.memories[i], 0);
					VkImageViewCreateInfo vci{};
					vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
					vci.image = o.images[i];
					vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
					vci.format = hdr ? VK_FORMAT_R16G16B16A16_SFLOAT : VK_FORMAT_R8G8B8A8_UNORM;
					vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					vci.subresourceRange.levelCount = 1;
					vci.subresourceRange.layerCount = 1;
					vkCreateImageView(m_device, &vci, nullptr, &o.views[i]);
				}

				// 深度附件（depth_only 用可采样 D32_SFLOAT；颜色离屏用 D24_S8）
				{
					VkFormat depth_fmt = depth_only ? m_depth_format : m_depth_stencil_format;
					VkImageCreateInfo dici{};
					dici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
					dici.imageType = VK_IMAGE_TYPE_2D;
					dici.format = depth_fmt;
					dici.extent = { w, h, 1 };
					dici.mipLevels = 1;
					dici.arrayLayers = 1;
					dici.samples = VK_SAMPLE_COUNT_1_BIT;
					dici.tiling = VK_IMAGE_TILING_OPTIMAL;
					dici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
						| (depth_only ? VK_IMAGE_USAGE_SAMPLED_BIT : 0);
					dici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
					dici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
					vkCreateImage(m_device, &dici, nullptr, &o.depth_image);
					VkMemoryRequirements mr{};
					vkGetImageMemoryRequirements(m_device, o.depth_image, &mr);
					VkMemoryAllocateInfo mai{};
					mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
					mai.allocationSize = mr.size;
					mai.memoryTypeIndex = find_memory_type(m_phys, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
					vkAllocateMemory(m_device, &mai, nullptr, &o.depth_memory);
					vkBindImageMemory(m_device, o.depth_image, o.depth_memory, 0);
					VkImageViewCreateInfo vci{};
					vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
					vci.image = o.depth_image;
					vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
					vci.format = depth_fmt;
					vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
					vci.subresourceRange.levelCount = 1;
					vci.subresourceRange.layerCount = 1;
					vkCreateImageView(m_device, &vci, nullptr, &o.depth_view);
				}

				if (depth_only)
				{
					// 深度专用 render pass：仅深度附件，finalLayout 可采样
					VkAttachmentDescription datt{};
					datt.format = m_depth_format;
					datt.samples = VK_SAMPLE_COUNT_1_BIT;
					datt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
					datt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
					datt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
					datt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
					datt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
					datt.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

					VkAttachmentReference dref{};
					dref.attachment = 0;
					dref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

					VkSubpassDescription sub{};
					sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
					sub.colorAttachmentCount = 0;
					sub.pColorAttachments = nullptr;
					sub.pDepthStencilAttachment = &dref;

					// external subpass 依赖（深度语义版）：上一帧采样完成后才清/写本帧深度；
					// 本帧深度写完成且布局转到 SHADER_READ_ONLY 后才允许后续 pass 采样，
					// 防读到未完成/黑内容（与颜色离屏同款同步结构）
					std::array<VkSubpassDependency, 2> deps{};
					deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
					deps[0].dstSubpass = 0;
					deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
					deps[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
					deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
					deps[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
					deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
					deps[1].srcSubpass = 0;
					deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
					deps[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
					deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
					deps[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
					deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
					deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

					VkRenderPassCreateInfo rpci{};
					rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
					rpci.attachmentCount = 1;
					rpci.pAttachments = &datt;
					rpci.subpassCount = 1;
					rpci.pSubpasses = &sub;
					rpci.dependencyCount = (uint)deps.size();
					rpci.pDependencies = deps.data();
					if (vkCreateRenderPass(m_device, &rpci, nullptr, &o.render_pass) != VK_SUCCESS)
					{
						error = "vkCreateRenderPass failed";
						return 0;
					}

					VkFramebufferCreateInfo fci{};
					fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
					fci.renderPass = o.render_pass;
					fci.attachmentCount = 1;
					fci.pAttachments = &o.depth_view;
					fci.width = w;
					fci.height = h;
					fci.layers = 1;
					vkCreateFramebuffer(m_device, &fci, nullptr, &o.framebuffer);
				}
				else
				{
					// render pass：N 颜色附件 + 深度
					std::vector<VkAttachmentDescription> atts(color_count + 1);
					std::vector<VkAttachmentReference> color_refs(color_count);
					for (uint i = 0; i < color_count; ++i)
					{
						atts[i].format = hdr ? VK_FORMAT_R16G16B16A16_SFLOAT : VK_FORMAT_R8G8B8A8_UNORM;
						atts[i].samples = VK_SAMPLE_COUNT_1_BIT;
						atts[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
						atts[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
						atts[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
						atts[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
						atts[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
						atts[i].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
						color_refs[i].attachment = i;
						color_refs[i].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
					}
					atts[color_count].format = m_depth_stencil_format;
					atts[color_count].samples = VK_SAMPLE_COUNT_1_BIT;
					atts[color_count].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
					atts[color_count].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
					atts[color_count].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
					atts[color_count].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
					atts[color_count].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
					atts[color_count].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

					VkAttachmentReference depth_ref{};
					depth_ref.attachment = color_count;
					depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

					VkSubpassDescription sub{};
					sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
					sub.colorAttachmentCount = color_count;
					sub.pColorAttachments = color_refs.data();
					sub.pDepthStencilAttachment = &depth_ref;

					// external subpass dependency：保证 render pass 结束后颜色附件 layout 转换
					//（finalLayout SHADER_READ_ONLY）在下一次采样前完成，避免采样到未完成/黑的内容。
					// src 阶段同时覆盖 LATE_FRAGMENT_TESTS（深度写）与 FRAGMENT_SHADER（前一深度专用
					// pass 的布局转换完成点），否则本 pass 的片元读阴影深度图不等待深度写/转换完成
					// → 个别帧采样未完成深度 → 阴影图案闪变
					std::array<VkSubpassDependency, 2> deps{};
					deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
					deps[0].dstSubpass = 0;
					deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
						| VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT
						| VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
					deps[0].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
					deps[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
						| VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
						| VK_ACCESS_SHADER_READ_BIT;
					deps[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
					deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
					deps[1].srcSubpass = 0;
					deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
					deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
					deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
					deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
					deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
					deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

					VkRenderPassCreateInfo rpci{};
					rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
					rpci.attachmentCount = color_count + 1;
					rpci.pAttachments = atts.data();
					rpci.subpassCount = 1;
					rpci.pSubpasses = &sub;
					rpci.dependencyCount = (uint)deps.size();
					rpci.pDependencies = deps.data();
					if (vkCreateRenderPass(m_device, &rpci, nullptr, &o.render_pass) != VK_SUCCESS)
					{
						error = "vkCreateRenderPass failed";
						return 0;
					}

					// framebuffer
					std::vector<VkImageView> fb_atts(color_count + 1);
					for (uint i = 0; i < color_count; ++i) fb_atts[i] = o.views[i];
					fb_atts[color_count] = o.depth_view;
					VkFramebufferCreateInfo fci{};
					fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
					fci.renderPass = o.render_pass;
					fci.attachmentCount = color_count + 1;
					fci.pAttachments = fb_atts.data();
					fci.width = w;
					fci.height = h;
					fci.layers = 1;
					vkCreateFramebuffer(m_device, &fci, nullptr, &o.framebuffer);
				}

				uint id = ++m_offscreen_counter;
				m_offscreens[id] = o;
				return id;
			}

			virtual void delete_offscreen(uint id) override
			{
				auto it = m_offscreens.find(id);
				if (it == m_offscreens.end()) return;
				if (m_device) vkDeviceWaitIdle(m_device);   // 在飞的命令缓冲仍引用该 render pass / framebuffer
				offscreen_t& o = it->second;
				if (o.framebuffer) vkDestroyFramebuffer(m_device, o.framebuffer, nullptr);
				if (o.render_pass) vkDestroyRenderPass(m_device, o.render_pass, nullptr);
				for (auto& v : o.views) if (v) vkDestroyImageView(m_device, v, nullptr);
				for (auto& img : o.images) if (img) vkDestroyImage(m_device, img, nullptr);
				for (auto& mem : o.memories) if (mem) vkFreeMemory(m_device, mem, nullptr);
				if (o.depth_view) vkDestroyImageView(m_device, o.depth_view, nullptr);
				if (o.depth_image) vkDestroyImage(m_device, o.depth_image, nullptr);
				if (o.depth_memory) vkFreeMemory(m_device, o.depth_memory, nullptr);
				m_offscreens.erase(it);
			}

			virtual void begin_offscreen(uint id) override

			{
				auto it = m_offscreens.find(id);
				surface_state* st = m_active;
				if (it == m_offscreens.end() || !st || !st->swapchain) return;
				VkCommandBuffer cb = st->cmd_bufs[st->image_index];
				if (st->render_pass_open) { vkCmdEndRenderPass(cb); st->render_pass_open = false; }

				offscreen_t& o = it->second;
				VkClearValue clears[5] = {};
				for (uint i = 0; i < o.color_count; ++i) clears[i].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
				clears[o.color_count].depthStencil = { 1.0f, 0 };
				VkRenderPassBeginInfo rp{};
				rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
				rp.renderPass = o.render_pass;
				rp.framebuffer = o.framebuffer;
				rp.renderArea.extent = { o.w, o.h };
				rp.clearValueCount = o.color_count + 1;
				rp.pClearValues = clears;
				vkCmdBeginRenderPass(cb, &rp, VK_SUBPASS_CONTENTS_INLINE); st->render_pass_open = true;

				VkViewport vp{};
				vp.x = 0; vp.y = (float)o.h;
				vp.width = (float)o.w;
				vp.height = -(float)o.h;
				vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
				vkCmdSetViewport(cb, 0, 1, &vp);
				VkRect2D sc{};
				sc.extent = { o.w, o.h };
				vkCmdSetScissor(cb, 0, 1, &sc);

				m_offscreen_active = id;
			}

			virtual void end_offscreen() override

			{
				surface_state* st = m_active;
				if (!st || !st->swapchain || (!m_offscreen_active && !m_cubemap_active)) return;
				VkCommandBuffer cb = st->cmd_bufs[st->image_index];
				if (st->render_pass_open) { vkCmdEndRenderPass(cb); st->render_pass_open = false; }
				m_offscreen_active = 0;
				m_cubemap_active = 0;

				// 回到 swapchain render pass
				VkClearValue clears[3] = {};
				clears[0].color = { { m_clear_color[0], m_clear_color[1], m_clear_color[2], m_clear_color[3] } };
				uint clear_count = 2;
				if (m_msaa > 1) { clears[1].color = { { m_clear_color[0], m_clear_color[1], m_clear_color[2], m_clear_color[3] } }; clears[2].depthStencil = { 1.0f, 0 }; clear_count = 3; }
				else { clears[1].depthStencil = { 1.0f, 0 }; }
				VkRenderPassBeginInfo rp{};
				rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
				rp.renderPass = st->render_pass;
				rp.framebuffer = st->framebuffers[st->image_index];
				rp.renderArea.extent = st->extent;
				rp.clearValueCount = clear_count;
				rp.pClearValues = clears;
				vkCmdBeginRenderPass(cb, &rp, VK_SUBPASS_CONTENTS_INLINE); st->render_pass_open = true;

				VkViewport vp{};
				vp.x = 0.0f;
				vp.y = (float)st->extent.height;
				vp.width = (float)st->extent.width;
				vp.height = -(float)st->extent.height;
				vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
				vkCmdSetViewport(cb, 0, 1, &vp);
				VkRect2D sc{};
				sc.extent = st->extent;
				vkCmdSetScissor(cb, 0, 1, &sc);

				m_offscreen_active = 0;
			}

			virtual uint offscreen_texture(uint id, uint attachment) override
			{
				auto it = m_offscreens.find(id);
				if (it == m_offscreens.end() || attachment >= it->second.color_count) return 0;
				offscreen_t& o = it->second;
				if (attachment < o.color_tex_ids.size() && o.color_tex_ids[attachment] != 0)
					return o.color_tex_ids[attachment];   // 缓存命中，避免每帧重建 sampler 泄漏

				sampler_desc sd;
				sd.mipmap = false;
				VkSampler sampler = create_sampler(sd, 0.0f);

				uint tid = ++m_texture_counter;
				texture_t t;
				t.image = o.images[attachment];
				t.view = o.views[attachment];
				t.memory = VK_NULL_HANDLE;
				t.sampler = sampler;
				t.owns_image = false;
				m_textures[tid] = t;
				if (attachment >= o.color_tex_ids.size()) o.color_tex_ids.resize(attachment + 1, 0);
				o.color_tex_ids[attachment] = tid;
				return tid;
			}

			virtual uint offscreen_depth_texture(uint id) override
			{
				auto it = m_offscreens.find(id);
				if (it == m_offscreens.end() || !it->second.depth_view) return 0;
				offscreen_t& o = it->second;
				if (o.depth_tex_id != 0) return o.depth_tex_id;   // 缓存命中

				// 深度比较采样器（阴影采样）
				sampler_desc sd;
				sd.mipmap = false;
				sd.compare = true;
				VkSampler sampler = create_sampler(sd, 0.0f);

				uint tid = ++m_texture_counter;
				texture_t t;
				t.image = o.depth_image;
				t.view = o.depth_view;
				t.memory = VK_NULL_HANDLE;
				t.sampler = sampler;
				t.owns_image = false;
				m_textures[tid] = t;
				o.depth_tex_id = tid;
				return tid;
			}

			virtual void read_offscreen(uint id, sint x, sint y, uint* out) override
			{
				auto it = m_offscreens.find(id);
				if (it == m_offscreens.end() || !out || it->second.images.empty()) return;
				offscreen_t& o = it->second;
				vkDeviceWaitIdle(m_device);

				// 离屏渲染采用 Y 翻转视口，图像存储为左上角原点；调用方给左下角 → 翻转
				sint vx = (x < 0) ? 0 : x;
				sint vy = (sint)o.h - y - 1;
				if (vy < 0) vy = 0;
				if (vx >= (sint)o.w) vx = (sint)o.w - 1;
				if (vy >= (sint)o.h) vy = (sint)o.h - 1;

				VkDeviceSize size = 4;   // 1 像素 RGBA8
				VkDeviceMemory staging_mem = VK_NULL_HANDLE;
				VkDeviceSize staging_offset = 0;
				VkBuffer staging = create_buffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, staging_mem, staging_offset);

				VkImage src = o.images[0];
				one_shot([&](VkCommandBuffer cb)
				{
					VkImageMemoryBarrier b{};
					b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
					b.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
					b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
					b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
					b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
					b.image = src;
					b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					b.subresourceRange.levelCount = 1;
					b.subresourceRange.layerCount = 1;
					vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
						0, 0, nullptr, 0, nullptr, 1, &b);

					VkBufferImageCopy region{};
					region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					region.imageSubresource.layerCount = 1;
					region.imageOffset = { (int32_t)vx, (int32_t)vy, 0 };
					region.imageExtent = { 1, 1, 1 };
					vkCmdCopyImageToBuffer(cb, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging, 1, &region);

					b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
					b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
					b.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
					b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
					vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
						0, 0, nullptr, 0, nullptr, 1, &b);
				});

				void* data = nullptr;
				vkMapMemory(m_device, staging_mem, staging_offset, size, 0, &data);
				std::memcpy(out, data, (size_t)size);
				vkUnmapMemory(m_device, staging_mem);
				vkDestroyBuffer(m_device, staging, nullptr);
			}

			virtual uint create_offscreen_cubemap_depth(uint size, std::string& error) override
			{
				if (!m_device) { error = "Vulkan device not created"; return 0; }
				if (size == 0) { error = "illegal cubemap size"; return 0; }
				cubemap_t c;
				c.size = size;

				// 深度图像：cube-compatible，6 layers
				VkImageCreateInfo ici{};
				ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
				ici.imageType = VK_IMAGE_TYPE_2D;
				ici.format = m_depth_format;
				ici.extent = { size, size, 1 };
				ici.mipLevels = 1;
				ici.arrayLayers = 6;
				ici.samples = VK_SAMPLE_COUNT_1_BIT;
				ici.tiling = VK_IMAGE_TILING_OPTIMAL;
				ici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
				ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
				ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				ici.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
				vkCreateImage(m_device, &ici, nullptr, &c.image);

				VkMemoryRequirements mr{};
				vkGetImageMemoryRequirements(m_device, c.image, &mr);
				VkMemoryAllocateInfo mai{};
				mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
				mai.allocationSize = mr.size;
				mai.memoryTypeIndex = find_memory_type(m_phys, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
				vkAllocateMemory(m_device, &mai, nullptr, &c.memory);
				vkBindImageMemory(m_device, c.image, c.memory, 0);

				// 每面 2D 视图 + 立方体采样视图（深度固定单 mip）
				c.face_views.resize(6);
				for (uint f = 0; f < 6; ++f)
				{
					VkImageViewCreateInfo vci{};
					vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
					vci.image = c.image;
					vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
					vci.format = m_depth_format;
					vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
					vci.subresourceRange.levelCount = 1;
					vci.subresourceRange.baseArrayLayer = f;
					vci.subresourceRange.layerCount = 1;
					vkCreateImageView(m_device, &vci, nullptr, &c.face_views[f]);
				}
				VkImageViewCreateInfo cvci{};
				cvci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
				cvci.image = c.image;
				cvci.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
				cvci.format = m_depth_format;
				cvci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
				cvci.subresourceRange.levelCount = 1;
				cvci.subresourceRange.layerCount = 6;
				vkCreateImageView(m_device, &cvci, nullptr, &c.cube_view);

				// 深度专用 render pass
				VkAttachmentDescription datt{};
				datt.format = m_depth_format;
				datt.samples = VK_SAMPLE_COUNT_1_BIT;
				datt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
				datt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
				datt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
				datt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
				datt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				datt.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				VkAttachmentReference dref{};
				dref.attachment = 0;
				dref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
				VkSubpassDescription sub{};
				sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
				sub.colorAttachmentCount = 0;
				sub.pDepthStencilAttachment = &dref;
				VkRenderPassCreateInfo rpci{};
				rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
				rpci.attachmentCount = 1;
				rpci.pAttachments = &datt;
				rpci.subpassCount = 1;
				rpci.pSubpasses = &sub;
				vkCreateRenderPass(m_device, &rpci, nullptr, &c.render_pass);

				// 每面 framebuffer（深度固定单 mip）
				c.framebuffers.resize(6);
				for (uint f = 0; f < 6; ++f)
				{
					VkFramebufferCreateInfo fci{};
					fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
					fci.renderPass = c.render_pass;
					fci.attachmentCount = 1;
					fci.pAttachments = &c.face_views[f];
					fci.width = size;
					fci.height = size;
					fci.layers = 1;
					vkCreateFramebuffer(m_device, &fci, nullptr, &c.framebuffers[f]);
				}

				uint id = ++m_cubemap_counter;
				m_cubemaps[id] = c;
				return id;
			}

			virtual uint create_offscreen_cubemap(uint size, std::string& error, uint mip_levels = 1) override
			{
				if (!m_device) { error = "Vulkan device not created"; return 0; }
				if (size == 0) { error = "illegal cubemap size"; return 0; }
				if (mip_levels == 0) mip_levels = 1;
				cubemap_t c;
				c.size = size;
				c.color = true;
				c.mip_levels = mip_levels;

				VkImageCreateInfo ici{};
				ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
				ici.imageType = VK_IMAGE_TYPE_2D;
				ici.format = VK_FORMAT_R16G16B16A16_SFLOAT;
				ici.extent = { size, size, 1 };
				ici.mipLevels = mip_levels;
				ici.arrayLayers = 6;
				ici.samples = VK_SAMPLE_COUNT_1_BIT;
				ici.tiling = VK_IMAGE_TILING_OPTIMAL;
				ici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
				ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
				ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				ici.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
				vkCreateImage(m_device, &ici, nullptr, &c.image);

				VkMemoryRequirements mr{};
				vkGetImageMemoryRequirements(m_device, c.image, &mr);
				VkMemoryAllocateInfo mai{};
				mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
				mai.allocationSize = mr.size;
				mai.memoryTypeIndex = find_memory_type(m_phys, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
				vkAllocateMemory(m_device, &mai, nullptr, &c.memory);
				vkBindImageMemory(m_device, c.image, c.memory, 0);

				c.face_views.resize(6 * mip_levels);
				for (uint f = 0; f < 6; ++f)
					for (uint m = 0; m < mip_levels; ++m)
					{
						VkImageViewCreateInfo vci{};
						vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
						vci.image = c.image;
						vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
						vci.format = VK_FORMAT_R16G16B16A16_SFLOAT;
						vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
						vci.subresourceRange.levelCount = 1;
						vci.subresourceRange.baseMipLevel = m;
						vci.subresourceRange.baseArrayLayer = f;
						vci.subresourceRange.layerCount = 1;
						vkCreateImageView(m_device, &vci, nullptr, &c.face_views[f * mip_levels + m]);
					}
				VkImageViewCreateInfo cvci{};
				cvci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
				cvci.image = c.image;
				cvci.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
				cvci.format = VK_FORMAT_R16G16B16A16_SFLOAT;
				cvci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				cvci.subresourceRange.levelCount = mip_levels;
				cvci.subresourceRange.layerCount = 6;
				vkCreateImageView(m_device, &cvci, nullptr, &c.cube_view);

				VkAttachmentDescription catt{};
				catt.format = VK_FORMAT_R16G16B16A16_SFLOAT;
				catt.samples = VK_SAMPLE_COUNT_1_BIT;
				catt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
				catt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
				catt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
				catt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
				catt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				catt.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				VkAttachmentReference cref{};
				cref.attachment = 0;
				cref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				VkSubpassDescription sub{};
				sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
				sub.colorAttachmentCount = 1;
				sub.pColorAttachments = &cref;
				VkRenderPassCreateInfo rpci{};
				rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
				rpci.attachmentCount = 1;
				rpci.pAttachments = &catt;
				rpci.subpassCount = 1;
				rpci.pSubpasses = &sub;
				vkCreateRenderPass(m_device, &rpci, nullptr, &c.render_pass);

				c.framebuffers.resize(6 * mip_levels);
				for (uint f = 0; f < 6; ++f)
					for (uint m = 0; m < mip_levels; ++m)
					{
						uint ms = (size >> m); if (ms < 1) ms = 1;
						VkFramebufferCreateInfo fci{};
						fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
						fci.renderPass = c.render_pass;
						fci.attachmentCount = 1;
						fci.pAttachments = &c.face_views[f * mip_levels + m];
						fci.width = ms;
						fci.height = ms;
						fci.layers = 1;
						vkCreateFramebuffer(m_device, &fci, nullptr, &c.framebuffers[f * mip_levels + m]);
					}

				uint id = ++m_cubemap_counter;
				m_cubemaps[id] = c;
				return id;
			}

			virtual void delete_offscreen_cubemap(uint id) override
			{
				if (m_device) vkDeviceWaitIdle(m_device);
				auto it = m_cubemaps.find(id);
				if (it == m_cubemaps.end()) return;
				cubemap_t& c = it->second;
				for (auto& fb : c.framebuffers) if (fb) vkDestroyFramebuffer(m_device, fb, nullptr);
				if (c.render_pass) vkDestroyRenderPass(m_device, c.render_pass, nullptr);
				for (auto& fv : c.face_views) if (fv) vkDestroyImageView(m_device, fv, nullptr);
				if (c.cube_view) vkDestroyImageView(m_device, c.cube_view, nullptr);
				if (c.image) vkDestroyImage(m_device, c.image, nullptr);
				if (c.memory) vkFreeMemory(m_device, c.memory, nullptr);
				m_cubemaps.erase(it);
			}

			virtual void delete_offscreen_cubemap_depth(uint id) override
			{
				if (m_device) vkDeviceWaitIdle(m_device);
				auto it = m_cubemaps.find(id);
				if (it == m_cubemaps.end()) return;
				cubemap_t& c = it->second;
				for (auto& fb : c.framebuffers) if (fb) vkDestroyFramebuffer(m_device, fb, nullptr);
				if (c.render_pass) vkDestroyRenderPass(m_device, c.render_pass, nullptr);
				for (auto& fv : c.face_views) if (fv) vkDestroyImageView(m_device, fv, nullptr);
				if (c.cube_view) vkDestroyImageView(m_device, c.cube_view, nullptr);
				if (c.image) vkDestroyImage(m_device, c.image, nullptr);
				if (c.memory) vkFreeMemory(m_device, c.memory, nullptr);
				m_cubemaps.erase(it);
			}

			virtual void begin_offscreen_cubemap(uint id, uint face, uint mip = 0) override
			{
				auto it = m_cubemaps.find(id);
				if (it == m_cubemaps.end() || face > 5 || mip >= it->second.mip_levels || !swapchain_ready()) return;
				VkCommandBuffer cb = m_active->cmd_bufs[m_active->image_index];
				if (m_active->render_pass_open) { vkCmdEndRenderPass(cb); m_active->render_pass_open = false; }

				cubemap_t& c = it->second;
				uint ms = (c.size >> mip); if (ms < 1) ms = 1;
				VkClearValue clear{};
				if (c.color) clear.color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
				else clear.depthStencil = { 1.0f, 0 };
				VkRenderPassBeginInfo rp{};
				rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
				rp.renderPass = c.render_pass;
				rp.framebuffer = c.framebuffers[face * c.mip_levels + mip];
				rp.renderArea.extent = { ms, ms };
				rp.clearValueCount = 1;
				rp.pClearValues = &clear;
				vkCmdBeginRenderPass(cb, &rp, VK_SUBPASS_CONTENTS_INLINE); m_active->render_pass_open = true;

				VkViewport vp{};
				vp.x = 0; vp.y = (float)ms;
				vp.width = (float)ms;
				vp.height = -(float)ms;
				vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
				vkCmdSetViewport(cb, 0, 1, &vp);
				VkRect2D sc{};
				sc.extent = { ms, ms };
				vkCmdSetScissor(cb, 0, 1, &sc);

				m_cubemap_active = id;
			}

			virtual uint offscreen_cubemap_texture(uint id) override
			{
				auto it = m_cubemaps.find(id);
				if (it == m_cubemaps.end()) return 0;
				// 惰性注册为可采样纹理（与 bind_texture 的 texture id 空间一致）
				cubemap_t& c = it->second;
				if (!c.texture_id)
				{
					sampler_desc sd;
					sd.mipmap = false;
					sd.compare = true;   // 深度立方体做阴影采样（samplerCubeShadow 比较采样）
					VkSampler sampler = create_sampler(sd, 0.0f);
					uint tid = ++m_texture_counter;
					texture_t t;
					t.image = c.image;
					t.memory = VK_NULL_HANDLE;   // image 由 cubemap 持有，memory 亦然（destroy_textures 不释放）
					t.view = c.cube_view;
					t.sampler = sampler;
					t.owns_image = false;
					m_textures[tid] = t;
					c.texture_id = tid;
				}
				return c.texture_id;
			}

			virtual uint draw_call_count() override { return m_draw_calls; }
			virtual void reset_draw_call_count() override { m_draw_calls = 0; }

			virtual void blend(bool enable) override { m_blend = enable; }
			virtual void depth_test(bool enable, bool less_equal) override { m_depth = enable; m_depth_less_equal = less_equal; }
			virtual void color_write(bool enable) override { m_color_write = enable; }

			virtual void stencil_write(uint ref, uint mask) override
			{
				m_stencil = true;
				m_stencil_write = true;
				m_stencil_ref = ref;
				m_stencil_mask = mask;
			}

			virtual void stencil_test(uint ref, uint mask) override
			{
				m_stencil = true;
				m_stencil_write = false;
				m_stencil_ref = ref;
				m_stencil_mask = mask;
			}

			virtual void stencil_disable() override { m_stencil = false; }

			virtual void set_msaa(uint samples) override
			{
				// 采样数取 1/2/4/8 中的合法值（需在 create_surface 前调用）
				m_msaa = (samples >= 8) ? 8 : (samples >= 4) ? 4 : (samples >= 2) ? 2 : 1;
			}

			VkSampleCountFlagBits msaa_flag() const
			{
				switch (m_msaa)
				{
				case 2: return VK_SAMPLE_COUNT_2_BIT;
				case 4: return VK_SAMPLE_COUNT_4_BIT;
				case 8: return VK_SAMPLE_COUNT_8_BIT;
				default: return VK_SAMPLE_COUNT_1_BIT;
				}
			}
			virtual void point_size(value s) override { m_point_size = s; }
			virtual void line_size(value s) override { m_line_width = s; }

			virtual uint value_type() override { return 0; }
			virtual uint value_size() override { return sizeof(fantasy::value); }
			virtual void finish() override { if (m_device) vkDeviceWaitIdle(m_device); }
			virtual void clear(rgba color) override
			{
				// Vulkan 清除推迟到 render pass begin（clearValue），此处记录颜色，与 GL 的 clear() 视觉对齐
				// rgba 字节序 BGR(A)：xhqm_rgba(r,g,b,a)=r|g<<8|b<<16|a<<24
				m_clear_color[0] = (color & 0xFF) / 255.0f;
				m_clear_color[1] = ((color >> 8) & 0xFF) / 255.0f;
				m_clear_color[2] = ((color >> 16) & 0xFF) / 255.0f;
				m_clear_color[3] = ((color >> 24) & 0xFF) / 255.0f;
			}
			virtual void viewport(sint, sint, sint, sint) override {}
			virtual void winding(bool) override {}
			virtual void read_pixels(sint x, sint y, sint w, sint h, uint* out) override
			{
				surface_state* st = m_active;
				if (!m_device || !st || !st->swapchain || !out || w <= 0 || h <= 0) return;
				if (st->images.empty()) return;

				vkDeviceWaitIdle(m_device);

				// 图像 Y 翻转（GL 左下角 -> Vulkan 左上角）
				sint vx = (x < 0) ? 0 : x;
				sint vy = (sint)st->extent.height - y - h;
				if (vy < 0) vy = 0;
				if (vx + w > (sint)st->extent.width) w = (sint)st->extent.width - vx;
				if (vy + h > (sint)st->extent.height) h = (sint)st->extent.height - vy;
				if (w <= 0 || h <= 0) return;

				VkImage src = st->images[st->image_index];
				VkDeviceSize size = (VkDeviceSize)w * h * 4;
				VkDeviceMemory staging_mem = VK_NULL_HANDLE;
				VkDeviceSize staging_offset = 0;
				VkBuffer staging = create_buffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, staging_mem, staging_offset);

				one_shot([&](VkCommandBuffer cb)
				{
					VkImageMemoryBarrier b{};
					b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
					b.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
					b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
					b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
					b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
					b.image = src;
					b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					b.subresourceRange.levelCount = 1;
					b.subresourceRange.layerCount = 1;
					b.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
					b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
					vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
						0, 0, nullptr, 0, nullptr, 1, &b);

					VkBufferImageCopy region{};
					region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					region.imageSubresource.layerCount = 1;
					region.imageOffset = { (int32_t)vx, (int32_t)vy, 0 };
					region.imageExtent = { (uint32_t)w, (uint32_t)h, 1 };
					vkCmdCopyImageToBuffer(cb, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging, 1, &region);

					b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
					b.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
					b.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
					b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
					vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
						0, 0, nullptr, 0, nullptr, 1, &b);
				});

				void* data = nullptr;
				vkMapMemory(m_device, staging_mem, staging_offset, size, 0, &data);
				std::memcpy(out, data, (size_t)size);
				vkUnmapMemory(m_device, staging_mem);

				vkDestroyBuffer(m_device, staging, nullptr);
				// staging_mem 由内存池统一释放
			}

		private:
			// 当前绑定的 render pass（管线创建/缓存键用；swapchain / 离屏 / 立方体）
			VkRenderPass active_render_pass() const
			{
				if (m_cubemap_active)
				{
					auto it = m_cubemaps.find(m_cubemap_active);
					if (it != m_cubemaps.end()) return it->second.render_pass;
				}
				if (m_offscreen_active)
				{
					auto it = m_offscreens.find(m_offscreen_active);
					if (it != m_offscreens.end()) return it->second.render_pass;
				}
				return m_active ? m_active->render_pass : VK_NULL_HANDLE;
			}
			// 当前 render pass 的颜色附件数（深度专用=0，swapchain=1，MRT/HDR=color_count）
			uint active_color_attachments() const
			{
				if (m_cubemap_active) return 0;   // 立方体深度：无颜色附件
				if (m_offscreen_active)
				{
					auto it = m_offscreens.find(m_offscreen_active);
					if (it != m_offscreens.end()) return it->second.color_count;
				}
				return 1;
			}

			VkPipeline create_pipeline(VkShaderModule vs, VkShaderModule fs, VkPipelineLayout layout, const std::vector<uint>& locations, VkPrimitiveTopology topology)
			{
				VkPipelineShaderStageCreateInfo stages[2] = {};
				stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
				stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
				stages[0].module = vs;
				stages[0].pName = "main";
				stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
				stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
				stages[1].module = fs;
				stages[1].pName = "main";

				// 顶点布局：按 locations（每属性分量数）动态生成
				std::vector<VkVertexInputAttributeDescription> attrs;
				uint stride = 0;
				for (auto l : locations) stride += l;
				uint offset = 0;
				for (uint i = 0; i < (uint)locations.size(); ++i)
				{
					VkVertexInputAttributeDescription a{};
					a.location = i;
					a.binding = 0;
					if (locations[i] == 0)
					{
						// 该属性缺失：GL 允许顶点属性未绑定（着色器读到默认值，且引擎用 UBO flags 门控其使用），
						// 但 Vulkan 要求管线为着色器声明的每个输入 location 都提供属性描述，
						// 否则管线无效（VUID-VkGraphicsPipelineCreateInfo-Input-07904），
						// 在 MoltenVK 上表现为这类绘制整体不渲染（整批顶点消失）。
						// 补一个占位描述（偏移 0、单分量）：其数值不会被使用（对应 flags 为 0）。
						a.format = VK_FORMAT_R32_SFLOAT;
						a.offset = 0;
						attrs.push_back(a);
						continue;
					}
					a.format = float_format(locations[i]);
					a.offset = offset;
					attrs.push_back(a);
					offset += locations[i] * sizeof(float);
				}

				VkVertexInputBindingDescription bind{};
				bind.binding = 0;
				bind.stride = stride * sizeof(float);
				bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

				VkPipelineVertexInputStateCreateInfo visci{};
				visci.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
				visci.vertexBindingDescriptionCount = (stride ? 1u : 0u);
				visci.pVertexBindingDescriptions = (stride ? &bind : nullptr);
				visci.vertexAttributeDescriptionCount = (uint)attrs.size();
				visci.pVertexAttributeDescriptions = attrs.data();

				VkPipelineInputAssemblyStateCreateInfo iasci{};
				iasci.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
				iasci.topology = topology;

				VkPipelineViewportStateCreateInfo vpsci{};
				vpsci.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
				vpsci.viewportCount = 1;
				vpsci.scissorCount = 1;

				VkPipelineRasterizationStateCreateInfo rsci{};
				rsci.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
				rsci.polygonMode = VK_POLYGON_MODE_FILL;
				rsci.cullMode = VK_CULL_MODE_NONE;
				rsci.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
				rsci.lineWidth = 1.0f;

				VkPipelineMultisampleStateCreateInfo msci{};
				msci.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
				msci.rasterizationSamples = msaa_flag();

				uint color_attachments = active_color_attachments();
				std::vector<VkPipelineColorBlendAttachmentState> cba_vec(color_attachments);
				for (auto& cba : cba_vec)
				{
					cba.colorWriteMask = m_color_write ? 0xf : 0x0;
					cba.blendEnable = m_blend ? VK_TRUE : VK_FALSE;
					cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
					cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
					cba.colorBlendOp = VK_BLEND_OP_ADD;
					cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
					cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
					cba.alphaBlendOp = VK_BLEND_OP_ADD;
				}
				VkPipelineColorBlendStateCreateInfo cbsci{};
				cbsci.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
				cbsci.attachmentCount = color_attachments;
				cbsci.pAttachments = color_attachments ? cba_vec.data() : nullptr;

				VkPipelineDepthStencilStateCreateInfo dssci{};
				dssci.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
				dssci.depthTestEnable = m_depth ? VK_TRUE : VK_FALSE;
				dssci.depthWriteEnable = m_depth ? VK_TRUE : VK_FALSE;
				dssci.depthCompareOp = m_depth_less_equal ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_LESS;
				// stencil 状态
				dssci.stencilTestEnable = m_stencil ? VK_TRUE : VK_FALSE;
				dssci.front.compareOp = m_stencil_write ? VK_COMPARE_OP_ALWAYS : VK_COMPARE_OP_EQUAL;
				dssci.front.failOp = VK_STENCIL_OP_KEEP;
				dssci.front.passOp = m_stencil_write ? VK_STENCIL_OP_REPLACE : VK_STENCIL_OP_KEEP;
				dssci.front.depthFailOp = VK_STENCIL_OP_KEEP;
				dssci.front.compareMask = m_stencil_mask;
				dssci.front.writeMask = m_stencil_write ? m_stencil_mask : 0;
				dssci.front.reference = m_stencil_ref;
				dssci.back = dssci.front;

				VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_LINE_WIDTH };
				VkPipelineDynamicStateCreateInfo dsci{};
				dsci.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
				dsci.dynamicStateCount = 3;
				dsci.pDynamicStates = dyn;

				VkGraphicsPipelineCreateInfo gpci{};
				gpci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
				gpci.stageCount = 2;
				gpci.pStages = stages;
				gpci.pVertexInputState = &visci;
				gpci.pInputAssemblyState = &iasci;
				gpci.pViewportState = &vpsci;
				gpci.pRasterizationState = &rsci;
				gpci.pMultisampleState = &msci;
				gpci.pDepthStencilState = &dssci;
				gpci.pColorBlendState = &cbsci;
				gpci.pDynamicState = &dsci;
				gpci.layout = layout;
				gpci.renderPass = active_render_pass();
				gpci.subpass = 0;

				VkPipeline pipeline = VK_NULL_HANDLE;
				vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &gpci, nullptr, &pipeline);
				return pipeline;
			}
		};

		render_backend* create_vk_backend()
		{
			static vk_backend instance;
			return &instance;
		}
	}
}
