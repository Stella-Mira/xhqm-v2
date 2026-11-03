#include "system.h"

namespace xhqm
{
	namespace software
	{
		system_root::system_root()
		{
			object::type("system_root");
			add_child_ptr(new message_root());
			add_child_ptr(new command_root());
			add_child_ptr(new project_root());
			add_child_ptr(new utility_root());
		}

		system_root::~system_root()
		{
		}
		message_root* system_root::get_message_root()
		{
			return dynamic_cast<message_root*>(this->child_ptr(0));
		}
		command_root* system_root::get_command_root()
		{
			return dynamic_cast<command_root*>(this->child_ptr(1));
		}
		project_root* system_root::get_project_root()
		{
			return dynamic_cast<project_root*>(this->child_ptr(2));
		}
		utility_root* system_root::get_utility_root()
		{
			return dynamic_cast<utility_root*>(this->child_ptr(3));
		}
	}
}