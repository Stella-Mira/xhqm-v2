#ifndef XHQM_SOFTWARE_SYSTEM
#define XHQM_SOFTWARE_SYSTEM

#include "message.h"
#include "command.h"
#include "project.h"
#include "utility.h"

namespace xhqm
{
	namespace software
	{
		class xhqm_io system_root
			: public software::packet
		{
		public:
			explicit system_root();
			virtual ~system_root();
			system_root(system_root&&) = delete;
			system_root(const system_root&) = delete;

			message_root* get_message_root();
			command_root* get_command_root();
			project_root* get_project_root();
			utility_root* get_utility_root();
		};
	}
}

#endif

