#ifndef XHQM_SOFTWARE_COMMAND
#define XHQM_SOFTWARE_COMMAND

#include "packet.h"

namespace xhqm
{
	namespace software
	{
		class xhqm_io command
			: public software::packet
		{
			bool m_sucess = false;
		protected:
			std::vector<std::string> m_values;
			std::vector<std::string> m_return;
			virtual bool _redo() = 0;
			virtual bool _undo() = 0;
		public:
			command(const std::string& name);

			virtual ~command() {};
			virtual bool redo();
			virtual bool undo();
			virtual void save(void) override;
			virtual void read(void) override;

			inline bool is_sucess() const { return m_sucess; };
			inline const std::vector<std::string>& get_return(void) const { return m_return; }
			inline void set_values(const std::vector<std::string>& values) { m_values = values; }
			static bool read_command(const std::string& string, std::string& command, std::vector<std::string>& values);
		};

		class xhqm_io command_root : 
			public software::packet
		{
		public:
			explicit command_root();
			virtual ~command_root();
			command_root(command_root&&) = delete;
			command_root(const command_root&) = delete;
		public:
			virtual void redo();
			virtual void undo();
			virtual void redo(command* cptr);
			virtual void redo(std::string cstr);
			virtual void save(void) override;
			virtual void read(void) override;
		};
	}
}

#endif

