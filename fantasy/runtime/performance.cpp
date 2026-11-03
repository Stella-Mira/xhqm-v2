#include "performance.h"
#include "rhi_no.h"

namespace xhqm
{
	namespace fantasy
	{
		void performance_stats::begin_frame()
		{
			render_backend_active().reset_draw_call_count();
		}

		void performance_stats::end_frame(double dt_ms)
		{
			m_draw_calls = render_backend_active().draw_call_count();
			m_frame_ms = dt_ms;
			++m_frames;
		}

		gpu_timer::~gpu_timer()
		{
			render_backend& rhi = render_backend_active();
			if (m_begin) rhi.timestamp_query_delete(m_begin);
			if (m_end) rhi.timestamp_query_delete(m_end);
		}

		bool gpu_timer::begin(std::string& error)
		{
			render_backend& rhi = render_backend_active();
			m_begin = rhi.timestamp_query_create(error);
			m_end = rhi.timestamp_query_create(error);
			m_ready = (m_begin != 0 && m_end != 0);
			if (m_ready) rhi.timestamp_record(m_begin);
			return m_ready;
		}

		double gpu_timer::end_ms()
		{
			if (!m_ready) return 0.0;
			render_backend& rhi = render_backend_active();
			rhi.timestamp_record(m_end);
			std::uint64_t b = rhi.timestamp_result(m_begin);
			std::uint64_t e = rhi.timestamp_result(m_end);
			if (e < b) return 0.0;
			return double(e - b) / 1e6;
		}
	}
}
