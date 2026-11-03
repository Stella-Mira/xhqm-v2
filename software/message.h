#ifndef XHQM_SOFTWARE_MESSAGE
#define XHQM_SOFTWARE_MESSAGE

#include "packet.h"
#include <ctime>

namespace xhqm
{
	namespace software
	{
		class xhqm_io message
		{
		public:
			enum level
			{
				msg_txt = 0,	//无需记录
				msg_log,		//需要记录
				msg_info,		//重要
				msg_warn,		//警告
				msg_error,		//错误
			};
		protected:
			std::time_t m_timepos;
			const xhqm::text m_text;
			const message::level m_level;
		public:
			message(const xhqm::text& text);
			message(const xhqm::text& text, const message::level& level);
			inline const xhqm::text& get_text() const { return m_text; };
			inline const message::level& get_level(void) const { return m_level; };
			inline const std::time_t& timepos(void) const { return m_timepos; };
		};

		class xhqm_io message_root 
			: public software::packet
		{
			friend class software::packet;
		protected:
			std::queue<software::message> m_messages;
		public:
			explicit message_root();
			virtual ~message_root();
			message_root(message_root&&) = delete;
			message_root(const message_root&) = delete;

			virtual void save(void) override;
			virtual void read(void) override;

			inline const xhqm::count message_count(void) const { return m_messages.size(); };
			virtual void push_message(const software::message& message) { m_messages.push(message); }
		};
	}
}

#endif

