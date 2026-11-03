#include "execsys.h"

#include <thread>
#include <atomic>
#include <vector>
#include <string>
#include <exception>

namespace xhqm
{
	xhqm::parallel::process::result execsys::execute_cmd(const std::string& line)
	{
		xhqm::parallel::process proc;
		proc.command(line);
		proc.exec();
		proc.wait();
		return proc.get_result();
	}

	xhqm::parallel::process::result execsys::execute_program(const std::string& program, const std::vector<std::string>& args)
	{
		xhqm::parallel::process proc;
		proc.program(program, args);
		proc.exec();
		proc.wait();
		return proc.get_result();
	}

	std::vector<xhqm::parallel::process::result> execsys::execute_bat(const std::vector<std::string>& commands, xhqm::size max_concurrent)
	{
		std::vector<std::thread> workers;
		std::atomic<size_t> next_command{ 0 };
		std::vector<xhqm::parallel::process::result> results(commands.size());
		for (xhqm::size i = 0; i < max_concurrent; ++i)
		{
			workers.emplace_back([&]()
			{
				while (true)
				{
					const size_t idx = next_command.fetch_add(1);
					if (idx >= commands.size()) break;
					try
					{
						results[idx] = execute_cmd(commands[idx]);
					}
					catch (const std::exception& e)
					{
						results[idx].exit_code = -1;
						results[idx].exec_error = xhqm::parallel::process::error::fail_start;
						results[idx].exec_msg = e.what();
					}
				}
			});
		}
		for (auto& worker : workers)
		{
			worker.join();
		}
		return results;
	}
}
