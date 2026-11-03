#pragma once
#ifndef XHQM_TOOL_EXECSYS
#define XHQM_TOOL_EXECSYS
#include "../../head.h"
#include "process/process_base.h"

namespace xhqm
{
	// 进程执行简化封装（依赖 xhqm::parallel::process）
	class xhqm_io execsys
	{
	public:
		/** * 简化接口：command 模式执行（命令字符串） */
		static xhqm::parallel::process::result execute_cmd(const std::string& line);

		/** * 简化接口：program 模式执行 */
		static xhqm::parallel::process::result execute_program(const std::string& program, const std::vector<std::string>& args = std::vector<std::string>());

		/** * 批量执行（command 模式） */
		static std::vector<xhqm::parallel::process::result> execute_bat(const std::vector<std::string>&, xhqm::size max_concurrent);
	};
}
#endif
