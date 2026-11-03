#pragma once
#ifndef XHQM_FANTASY_FRAME_GRAPH
#define XHQM_FANTASY_FRAME_GRAPH

// 帧图（frame graph）：声明式渲染 pass + 资源依赖，拓扑排序自动编排执行顺序，管理资源生命周期
#include "fantasy.h"
#include "rhi_no.h"
#include <functional>
#include <unordered_map>

namespace xhqm
{
	namespace fantasy
	{
		// xhqm_io：类**必须**带引擎导出宏，否则从 xhqm.dll 外面看不到 add_pass/add_resource…
		//   实测：stella 的 star_sprite（Qt 宿主）链接期报 LNK2019 frame_graph::add_pass 未解析，
		//   而同批 833 个 fantasy 符号都正常 —— 差别就是这一处漏了 xhqm_io。
		class xhqm_io frame_graph
		{
		public:
			struct resource
			{
				std::string name;
				uint w = 0, h = 0, color_count = 1;
				uint handle = 0;      // RHI 离屏 id（execute 时分配）
				int first_use = -1;   // 排序后首次被触碰的 pass 序号
				int last_use = -1;    // 排序后最后被触碰的 pass 序号
			};
			struct pass
			{
				std::string name;
				std::vector<std::string> reads;
				std::vector<std::string> writes;
				std::function<void()> callback;
			};
		private:
			render_backend* m_rhi = nullptr;
			std::vector<resource> m_resources;
			std::vector<pass> m_passes;
			std::unordered_map<std::string, size_t> m_res_index;
			std::vector<size_t> m_order;   // 编译后的 pass 执行顺序（下标）
			bool m_compiled = false;
		public:
			frame_graph() = default;
			~frame_graph();

			void set_backend(render_backend& rhi) { m_rhi = &rhi; }
			void add_resource(const std::string& name, uint w, uint h, uint color_count = 1);
			void add_pass(const std::string& name, std::vector<std::string> reads, std::vector<std::string> writes, std::function<void()> callback);

			// 拓扑排序 + 资源生命周期计算
			bool compile(std::string& error);

			// 按序执行 pass，并在首次写入分配 / 最后读取释放资源
			void execute();

			// 资源离屏句柄（execute 期间有效）
			uint resource_handle(const std::string& name) const;

			void reset();
			size_t pass_count() const { return m_passes.size(); }
			bool compiled() const { return m_compiled; }
		};
	}
}

#endif
