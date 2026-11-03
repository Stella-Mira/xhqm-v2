#pragma once
#ifndef XHQM_FANTASY_PERFORMANCE
#define XHQM_FANTASY_PERFORMANCE

// 性能统计：帧时间 + draw call 计数 + GPU 时间戳计时
#include "fantasy.h"
#include <cstdint>
#include <string>

namespace xhqm
{
	namespace fantasy
	{
		class performance_stats
		{
			uint m_draw_calls = 0;   // 最近一帧 draw calls
			double m_frame_ms = 0;   // 最近一帧耗时（毫秒）
			uint m_frames = 0;       // 累计帧数
		public:
			performance_stats() = default;

			// 帧开始：重置 draw call 计数
			void begin_frame();
			// 帧结束：记录帧耗时，读取 draw calls
			void end_frame(double dt_ms);

			uint draw_calls() const { return m_draw_calls; }
			double frame_ms() const { return m_frame_ms; }
			uint frames() const { return m_frames; }
		};

		// GPU 时间戳计时器：begin() 打起始点，end_ms() 打结束点并返回 GPU 耗时（毫秒）
		// 用法：gpu_timer t; t.begin(err); ...渲染...; double ms = t.end_ms();
		class gpu_timer
		{
			uint m_begin = 0;
			uint m_end = 0;
			bool m_ready = false;
		public:
			gpu_timer() = default;
			~gpu_timer();

			bool begin(std::string& error);   // 创建两个时间戳并打起始点
			double end_ms();                  // 打结束点并同步读回耗时（毫秒），失败返回 0
		};
	}
}

#endif
