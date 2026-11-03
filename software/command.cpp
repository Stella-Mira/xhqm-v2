#include "command.h"
#include "system/codesys.h"

namespace xhqm
{
	namespace software
	{
		command::command(const std::string& name)
		{
			object::name(name);
			object::type("command");
		}

		bool command::redo()
		{
			if (!m_active)
			{
				m_sucess = this->_redo();
				m_active = true;
				return m_sucess;
			}
			return false;
		}

		bool command::undo()
		{
			if (m_active && m_sucess)
			{
				m_active = false;
				m_sucess = false;
				return this->_undo();
			}
			return false;
		}

		void command::save(void)
		{
		}

		void command::read(void)
		{
		}

		bool command::read_command(const std::string& string, std::string& command, std::vector<std::string>& values)
		{
			auto list = xhqm::codesys::split_string(string, ' ');
			if (list.size())
			{
				command = list.front();
				values = std::vector<std::string>(list.begin() + 1, list.end());
			}
			return list.size();
		}

		command_root::command_root()
		{
			object::type("command_root");
		}

		command_root::~command_root()
		{

		}

		void command_root::redo()
		{
		}

		void software::command_root::undo()
		{
		}

		void command_root::redo(command* cptr)
		{
		}

		void command_root::redo(std::string cstr)
		{
		}
		void command_root::save(void)
		{
		}
		void command_root::read(void)
		{
		}
	}
}
