#include "message.h"
#include "system/timesys.h"
#include "system/filesys.h"

namespace xhqm
{
	namespace software
	{
		message::message(const xhqm::text& text)
			: m_text(text), m_level(message::msg_log)
		{
			auto now = std::chrono::system_clock::now();
			m_timepos = std::chrono::system_clock::to_time_t(now);
		}

		message::message(const xhqm::text& text, const message::level& level)
			: m_text(text), m_level(level)
		{
			auto now = std::chrono::system_clock::now();
			m_timepos = std::chrono::system_clock::to_time_t(now);
		}


		message_root::message_root()
		{
			object::type("message_root");
		}

		message_root::~message_root()
		{

		}

		void message_root::save(void)
		{
			auto now = std::chrono::system_clock::now();
			auto timepos = std::chrono::system_clock::to_time_t(now);
			std::string path = xhqm::filesys::current_path() + "/message/" + std::to_string(timepos) + ".log";
			std::ofstream file(path);
			if (file.is_open())
			{
				xhqm::count count = m_messages.size();
				for (xhqm::index index = 0; index < count; ++index)
				{
					std::string info = xhqm::timesys::localtime(m_messages.front().timepos());
					info += std::to_string(m_messages.front().get_level());
					info += m_messages.front().get_text();
					file << info << "\n\n\n";
					m_messages.pop();
				}
				file.close();
			}
		}
		void message_root::read(void)
		{

		}
	}
}
