#include "process_base.h"
#include "../timesys.h"

#include <atomic>
#include <mutex>
#include <thread>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <array>
#include <vector>
#include <map>
#include <string>

#if defined(XHQM_WINDOWS)
#include <windows.h>
#undef min
#undef max
#else
#include <cerrno>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
// macOS/Linux：<signal.h> 可能被 xhqm/software/signal.h（信号槽）遮蔽，
// 这里显式声明一次确保 kill 可用；若系统头已声明则为合法重声明。
extern "C" int kill(pid_t, int);
// 遮蔽会导致 SIGTERM/SIGKILL 常量缺失：include 路径中 software/ 位于系统目录
// 之前，<signal.h> 实际解析到 software/signal.h（信号槽，不提供信号编号），
// 因此按 POSIX 固定值补齐（SIGTERM=15 / SIGKILL=9，Linux/macOS 一致）。
#ifndef SIGTERM
#define SIGTERM 15
#endif
#ifndef SIGKILL
#define SIGKILL 9
#endif
#endif

namespace xhqm
{
	namespace parallel
	{
		namespace
		{
			constexpr std::size_t buffer_size = 4096;

#if defined(XHQM_WINDOWS)
			inline const char* shell_program() { return "cmd.exe"; }
			inline const char* shell_flag() { return "/c"; }
#else
			inline const char* shell_program() { return "/bin/sh"; }
			inline const char* shell_flag() { return "-c"; }
#endif

#if defined(XHQM_WINDOWS)
			// Windows 命令行参数转义（CommandLineToArgvW 反向规则）
			std::string windows_quote_arg(const std::string& arg)
			{
				if (arg.empty()) return "\"\"";
				if (arg.find_first_of(" \t\"") == std::string::npos) return arg;
				std::string out = "\"";
				std::size_t backslashes = 0;
				for (char c : arg)
				{
					if (c == '\\') ++backslashes;
					else if (c == '"')
					{
						out.append(backslashes * 2 + 1, '\\');
						out.push_back('"');
						backslashes = 0;
					}
					else
					{
						out.append(backslashes, '\\');
						backslashes = 0;
						out.push_back(c);
					}
				}
				out.append(backslashes * 2, '\\');
				out.push_back('"');
				return out;
			}

			// program + args 拼成命令行
			std::string windows_command_line(const std::string& program, const std::vector<std::string>& args)
			{
				std::string line = windows_quote_arg(program);
				for (const auto& a : args) { line += ' '; line += windows_quote_arg(a); }
				return line;
			}

			// 环境块：父进程环境 + env_list 覆盖
			std::string windows_env_block(const process::env& env_list)
			{
				std::map<std::string, std::string> env_map;
				LPSTR env = GetEnvironmentStringsA();
				if (env)
				{
					for (LPSTR p = env; *p; p += std::strlen(p) + 1)
					{
						std::string entry(p);
						auto eq = entry.find('=');
						if (eq != std::string::npos)
							env_map[entry.substr(0, eq)] = entry.substr(eq + 1);
					}
					FreeEnvironmentStringsA(env);
				}
				for (const auto& [k, v] : env_list) env_map[k] = v;
				std::string block;
				for (const auto& [k, v] : env_map)
				{
					block += k; block += '='; block += v; block += '\0';
				}
				block += '\0';
				return block;
			}
#endif

			// 行处理：按 \n 切分、剥离 \r，按 capture/callback 分发
			void consume_lines(std::string& data_buffer, bool capture,
				const std::function<void(const std::string&)>& callback,
				std::vector<xhqm::text>& lines)
			{
				std::size_t pos;
				while ((pos = data_buffer.find('\n')) != std::string::npos)
				{
					std::string line = data_buffer.substr(0, pos);
					data_buffer.erase(0, pos + 1);
					if (!line.empty() && line.back() == '\r') line.pop_back();
					if (!line.empty())
					{
						if (capture) lines.push_back(line);
						if (callback) callback(line);
					}
				}
			}
		}

		struct process::hander
		{
			xhqm::timer timer;
			int process_id = -1;
			std::thread worker_out{};
			std::thread worker_err{};
			xhqm::count millis{};
			std::atomic<bool> runing = false;
			// wait_for() 的 WNOHANG 探测可能**顺手把子进程回收掉**（POSIX 侧判退出的唯一正确姿势）：
			// 把状态存在这里，好让随后的 wait() 直接取用，不必再 waitpid 一次（那一次会得 ECHILD）。
			bool reaped = false;
			int  reaped_status = 0;
#if defined(XHQM_WINDOWS)
			HANDLE process_handle_ = NULL;
			HANDLE job_ = NULL;                 // Job 对象（KILL_ON_JOB_CLOSE）：Windows 侧的"进程组/整树回收"
			HANDLE stdin_write_ = NULL;
			HANDLE stdout_read_ = NULL;
			HANDLE stderr_read_ = NULL;
#else
			int stdin_write_ = -1;
			int stdout_read_ = -1;
			int stderr_read_ = -1;
#endif
			void thread_out(process& proc);
			void thread_err(process& proc);
#if defined(XHQM_WINDOWS)
			static inline std::string str_error() { return std::to_string(GetLastError()); }
#endif
		};

		process::process(const xhqm::count& millis)
			: m_result{}
		{
			hander_ptr = std::make_shared<process::hander>();
			hander_ptr->millis = millis;
		}

		void process::program(const std::string& path, const std::vector<std::string>& args)
		{
			m_shell = false;
			m_program = path;
			m_arguments = args;
		}

		void process::command(const std::string& line)
		{
			m_shell = true;
			m_raw_line = false;
			m_command = line;
		}

		void process::command_line(const std::string& line)
		{
			m_shell = false;
			m_raw_line = true;
			m_command = line;
		}

		void process::timeout(const xhqm::count& value)
		{
			hander_ptr->millis = value;
		}

		void process::close_write()
		{
#if defined(XHQM_WINDOWS)
			if (hander_ptr->stdin_write_ != NULL)
			{
				CloseHandle(hander_ptr->stdin_write_);
				hander_ptr->stdin_write_ = NULL;
			}
#else
			if (hander_ptr->stdin_write_ != -1)
			{
				close(hander_ptr->stdin_write_);
				hander_ptr->stdin_write_ = -1;
			}
#endif
		}

		std::string process::input(std::string& input)
		{
			std::string error = "";
			if (!hander_ptr->runing)
			{
				error = "Process not runing";
			}
			else
			{
				if (m_function_i) m_function_i(input);
#if defined(XHQM_WINDOWS)
				DWORD written = 0;
				if (!WriteFile(hander_ptr->stdin_write_, input.c_str(), static_cast<DWORD>(input.size()), &written, NULL)
					|| written != static_cast<DWORD>(input.size()))
				{
					error = "Failed to write: " + hander::str_error();
				}
#else
				const ssize_t written = write(hander_ptr->stdin_write_, input.c_str(), input.size());
				if (written != static_cast<ssize_t>(input.size()))
				{
					error = "Failed to write: " + std::string(strerror(errno));
				}
#endif
			}
			return error;
		}

		void process::exec(void)
		{
			if (this->active()) { return; }

			m_result = {};

			// 确定实际执行的 program + args（command 模式走 shell）
			std::string exec_program;
			std::vector<std::string> exec_args;
			if (m_raw_line)
			{
				// 原始命令行：Windows 直接用整串（下面 raw 分支），POSIX 仍以 sh -c 承载
#if defined(XHQM_WINDOWS)
				exec_program = m_command;
#else
				exec_program = shell_program();
				exec_args = { shell_flag(), m_command };
#endif
			}
			else if (m_shell)
			{
				exec_program = shell_program();
				exec_args = { shell_flag(), m_command };
			}
			else
			{
				exec_program = m_program;
				exec_args = m_arguments;
			}

			if (exec_program.empty())
			{
				m_result.exec_error = process::error::fail_start;
				m_result.exec_msg = "Program path is empty";
				return;
			}

#if defined(XHQM_WINDOWS)
			SECURITY_ATTRIBUTES sa;
			sa.nLength = sizeof(SECURITY_ATTRIBUTES);
			sa.bInheritHandle = TRUE;
			sa.lpSecurityDescriptor = NULL;

			HANDLE child_stdin_read = NULL, child_stdin_write = NULL;
			HANDLE child_stdout_read = NULL, child_stdout_write = NULL;
			HANDLE child_stderr_read = NULL, child_stderr_write = NULL;

			if (!CreatePipe(&child_stdin_read, &child_stdin_write, &sa, 0))
			{
				m_result.exec_error = process::error::fail_start;
				m_result.exec_msg = "Failed to create stdin pipe: " + hander::str_error();
				return;
			}
			SetHandleInformation(child_stdin_write, HANDLE_FLAG_INHERIT, 0);

			if (!CreatePipe(&child_stdout_read, &child_stdout_write, &sa, 0))
			{
				m_result.exec_error = process::error::fail_start;
				m_result.exec_msg = "Failed to create stdout pipe: " + hander::str_error();
				CloseHandle(child_stdin_read); CloseHandle(child_stdin_write);
				return;
			}
			SetHandleInformation(child_stdout_read, HANDLE_FLAG_INHERIT, 0);

			if (!m_stderr_stdout)
			{
				if (!CreatePipe(&child_stderr_read, &child_stderr_write, &sa, 0))
				{
					m_result.exec_error = process::error::fail_start;
					m_result.exec_msg = "Failed to create stderr pipe: " + hander::str_error();
					CloseHandle(child_stdin_read); CloseHandle(child_stdin_write);
					CloseHandle(child_stdout_read); CloseHandle(child_stdout_write);
					return;
				}
				SetHandleInformation(child_stderr_read, HANDLE_FLAG_INHERIT, 0);
			}

			STARTUPINFOW si;
			PROCESS_INFORMATION pi;
			ZeroMemory(&si, sizeof(si));
			si.cb = sizeof(si);
			si.hStdInput = child_stdin_read;
			si.hStdOutput = child_stdout_write;
			si.hStdError = m_stderr_stdout ? child_stdout_write : child_stderr_write;
			si.dwFlags |= STARTF_USESTDHANDLES;
			ZeroMemory(&pi, sizeof(pi));

			// 环境块（env 非空时；否则继承父进程环境）
			std::string env_block;
			if (!m_env.empty())
			{
				env_block = windows_env_block(m_env);
			}

			// 命令行：raw 模式整串直用；否则 program + args 转义拼接
			const std::string command_line = m_raw_line ? m_command : windows_command_line(exec_program, exec_args);
			// UTF-8 → UTF-16：CreateProcessA 会把 UTF-8 字节按系统 ACP 重新解读，
			// 中文路径/设备名（dshow 设备名几乎都是中文）会乱码，故改走 W 版；
			// 环境块同样必须是宽字符块（CreateProcessW 只接受 Unicode 环境块）。
			auto to_wide = [](const std::string& u8) -> std::wstring
			{
				if (u8.empty()) return std::wstring();
				const int need = MultiByteToWideChar(CP_UTF8, 0, u8.c_str(), static_cast<int>(u8.size()), nullptr, 0);
				if (need <= 0) return std::wstring();
				std::wstring w(static_cast<std::size_t>(need), L'\0');
				MultiByteToWideChar(CP_UTF8, 0, u8.c_str(), static_cast<int>(u8.size()), &w[0], need);
				return w;
			};
			std::wstring wline = to_wide(command_line);
			std::vector<wchar_t> wbuf(wline.begin(), wline.end());
			wbuf.push_back(L'\0');
			std::wstring wdir = to_wide(m_workdir);
			std::wstring wenv;
			if (!env_block.empty())
			{
				const char* p = env_block.c_str();
				while (*p)
				{
					const std::string entry(p);
					wenv += to_wide(entry);
					wenv.push_back(L'\0');
					p += entry.size() + 1;
				}
				wenv.push_back(L'\0');
			}
			// CREATE_NEW_PROCESS_GROUP：可用 GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT) 做"温和终止"；
			// CREATE_SUSPENDED：先挂起，等分配进 Job 对象后再放行（保证子进程及其后代都在 Job 里 ⇒ 可整树回收）
			const BOOL ok = CreateProcessW(
				NULL,   // lpApplicationName：NULL，由 lpCommandLine 首个 token 解析并搜索 PATH
				wbuf.data(),
				NULL, NULL, TRUE, CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP | CREATE_SUSPENDED
					| CREATE_UNICODE_ENVIRONMENT,   // 上面传的是宽字符环境块：必须声明，否则 Windows 按 ANSI 解读
				wenv.empty() ? NULL : static_cast<LPVOID>(wenv.data()),
				wdir.empty() ? NULL : wdir.c_str(),
				&si, &pi);

			CloseHandle(child_stdin_read);
			CloseHandle(child_stdout_write);
			if (!m_stderr_stdout) CloseHandle(child_stderr_write);

			if (!ok)
			{
				CloseHandle(child_stdin_write);
				CloseHandle(child_stdout_read);
				if (!m_stderr_stdout) CloseHandle(child_stderr_read);
				m_result.exec_error = process::error::fail_start;
				m_result.exec_msg = "Failed to create process: " + hander::str_error();
				return;
			}

			// Job 对象：KILL_ON_JOB_CLOSE ⇒ 关 Job（或 TerminateJobObject）即整树回收
			HANDLE job = CreateJobObjectA(NULL, NULL);
			if (job)
			{
				JOBOBJECT_EXTENDED_LIMIT_INFORMATION job_info{};
				job_info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
				SetInformationJobObject(job, JobObjectExtendedLimitInformation, &job_info, sizeof(job_info));
				if (!AssignProcessToJobObject(job, pi.hProcess)) { CloseHandle(job); job = NULL; }
			}
			hander_ptr->job_ = job;
			ResumeThread(pi.hThread);          // 放行（上面以 CREATE_SUSPENDED 起，确保已在 Job 内）
			CloseHandle(pi.hThread);
			hander_ptr->process_id = pi.dwProcessId;
			hander_ptr->process_handle_ = pi.hProcess;
			hander_ptr->stdin_write_ = child_stdin_write;
			hander_ptr->stdout_read_ = child_stdout_read;
			hander_ptr->stderr_read_ = m_stderr_stdout ? NULL : child_stderr_read;
#else
			int stdin_pipe[2];
			int stdout_pipe[2];
			int stderr_pipe[2] = { -1, -1 };

			if (pipe(stdin_pipe) == -1)
			{
				m_result.exec_error = process::error::fail_start;
				m_result.exec_msg = "Failed to create stdin pipe: " + std::string(strerror(errno));
				return;
			}
			if (pipe(stdout_pipe) == -1)
			{
				m_result.exec_error = process::error::fail_start;
				m_result.exec_msg = "Failed to create stdout pipe: " + std::string(strerror(errno));
				close(stdin_pipe[0]); close(stdin_pipe[1]);
				return;
			}
			if (!m_stderr_stdout && pipe(stderr_pipe) == -1)
			{
				m_result.exec_error = process::error::fail_start;
				m_result.exec_msg = "Failed to create stderr pipe: " + std::string(strerror(errno));
				close(stdin_pipe[0]); close(stdin_pipe[1]);
				close(stdout_pipe[0]); close(stdout_pipe[1]);
				return;
			}

			const pid_t pid = fork();
			if (pid == -1)
			{
				m_result.exec_error = process::error::fail_start;
				m_result.exec_msg = "Failed to create process: " + std::string(strerror(errno));
				close(stdin_pipe[0]); close(stdin_pipe[1]);
				close(stdout_pipe[0]); close(stdout_pipe[1]);
				if (!m_stderr_stdout) { close(stderr_pipe[0]); close(stderr_pipe[1]); }
				return;
			}

			if (pid == 0)
			{	// 子进程
				setpgid(0, 0);   // 独立进程组：超时可用 kill(-pgid) 杀整棵进程树
				close(stdin_pipe[1]);
				close(stdout_pipe[0]);
				if (!m_stderr_stdout) close(stderr_pipe[0]);

				dup2(stdin_pipe[0], STDIN_FILENO);
				dup2(stdout_pipe[1], STDOUT_FILENO);
				if (m_stderr_stdout)
					dup2(stdout_pipe[1], STDERR_FILENO);
				else
					dup2(stderr_pipe[1], STDERR_FILENO);

				close(stdin_pipe[0]);
				close(stdout_pipe[1]);
				if (!m_stderr_stdout) close(stderr_pipe[1]);

				// 工作目录
				if (!m_workdir.empty() && chdir(m_workdir.c_str()) == -1)
				{
					_exit(127);
				}

				// 环境变量（fork 后子进程副本，不污染父进程）
				for (const auto& [key, value] : m_env)
				{
					setenv(key.c_str(), value.c_str(), 1);
				}

				// 直接执行 program + args
				std::vector<char*> argv;
				argv.reserve(1 + exec_args.size() + 1);
				argv.push_back(const_cast<char*>(exec_program.c_str()));
				for (const auto& a : exec_args)
				{
					argv.push_back(const_cast<char*>(a.c_str()));
				}
				argv.push_back(nullptr);
				execvp(exec_program.c_str(), argv.data());
				_exit(127);
			}
			else
			{	// 父进程
				setpgid(pid, pid);   // 父进程也建组（避免 killpg 竞态）
				close(stdin_pipe[0]);
				close(stdout_pipe[1]);
				if (!m_stderr_stdout) close(stderr_pipe[1]);

				hander_ptr->process_id = pid;
				hander_ptr->stdin_write_ = stdin_pipe[1];
				hander_ptr->stdout_read_ = stdout_pipe[0];
				hander_ptr->stderr_read_ = m_stderr_stdout ? -1 : stderr_pipe[0];
			}
#endif

			hander_ptr->runing = true;
			hander_ptr->timer.uptimer();
			hander_ptr->worker_out = std::thread(&process::hander::thread_out, hander_ptr.get(), std::ref(*this));
			if (!m_stderr_stdout)
			{
				hander_ptr->worker_err = std::thread(&process::hander::thread_err, hander_ptr.get(), std::ref(*this));
			}
		}

		void process::hander::thread_out(process& proc)
		{
			std::array<char, buffer_size> buffer;
			std::string data_buffer;
			for (;;)
			{
#if defined(XHQM_WINDOWS)
				DWORD bytes_read = 0;
				if (!ReadFile(stdout_read_, buffer.data(), static_cast<DWORD>(buffer_size - 1), &bytes_read, NULL))
				{
					if (GetLastError() == ERROR_BROKEN_PIPE) break;// 子进程关闭写端 = EOF
					proc.m_result.exec_error = process::error::read_error;
					proc.m_result.exec_msg = "Read stdout error: " + str_error();
					break;
				}
				if (bytes_read == 0) break;
#else
				ssize_t bytes_read = read(stdout_read_, buffer.data(), buffer_size - 1);
				if (bytes_read == 0) break;// EOF
				if (bytes_read < 0)
				{
					if (errno == EINTR) continue;
					proc.m_result.exec_error = process::error::read_error;
					proc.m_result.exec_msg = "Read stdout error: " + std::string(strerror(errno));
					break;
				}
#endif
				if (proc.m_function_b)
				{
					// 二进制模式（PCM/JPEG 等）：原样回调，**绝不**按行切分或剥 \r
					proc.m_function_b(buffer.data(), static_cast<std::size_t>(bytes_read));
					continue;
				}
				data_buffer.append(buffer.data(), static_cast<std::size_t>(bytes_read));
				consume_lines(data_buffer, proc.m_capture_stdout, proc.m_function_o, proc.m_result.exec_out);
			}
			// flush 残留（无换行的最后一段）
			if (!data_buffer.empty())
			{
				if (proc.m_capture_stdout) proc.m_result.exec_out.push_back(data_buffer);
				if (proc.m_function_o) proc.m_function_o(data_buffer);
			}
		}

		void process::hander::thread_err(process& proc)
		{
			std::array<char, buffer_size> buffer;
			std::string data_buffer;
			for (;;)
			{
#if defined(XHQM_WINDOWS)
				DWORD bytes_read = 0;
				if (!ReadFile(stderr_read_, buffer.data(), static_cast<DWORD>(buffer_size - 1), &bytes_read, NULL))
				{
					if (GetLastError() == ERROR_BROKEN_PIPE) break;
					proc.m_result.exec_error = process::error::read_error;
					proc.m_result.exec_msg = "Read stderr error: " + str_error();
					break;
				}
				if (bytes_read == 0) break;
#else
				ssize_t bytes_read = read(stderr_read_, buffer.data(), buffer_size - 1);
				if (bytes_read == 0) break;
				if (bytes_read < 0)
				{
					if (errno == EINTR) continue;
					proc.m_result.exec_error = process::error::read_error;
					proc.m_result.exec_msg = "Read stderr error: " + std::string(strerror(errno));
					break;
				}
#endif
				data_buffer.append(buffer.data(), static_cast<std::size_t>(bytes_read));
				consume_lines(data_buffer, proc.m_capture_stderr, proc.m_function_o, proc.m_result.exec_err);
			}
			if (!data_buffer.empty())
			{
				if (proc.m_capture_stderr) proc.m_result.exec_err.push_back(data_buffer);
				if (proc.m_function_o) proc.m_function_o(data_buffer);
			}
		}

		void process::wait(void)
		{
			if (!this->active())
			{
				if (m_throw_on_error && m_result.exec_error != process::error::none)
				{
					throw std::runtime_error(m_result.exec_msg);
				}
				return;
			}

			// 1. 关闭 stdin 写端，通知子进程输入结束
			close_write();

#if !defined(XHQM_WINDOWS)
			int status = 0;
			bool have_status = false;
#endif

			// 2. 等待子进程退出（worker 线程并行读，避免管道写满死锁）
#if defined(XHQM_WINDOWS)
			if (hander_ptr->process_handle_ != NULL)
			{
				if (hander_ptr->millis == process::npos)
				{
					WaitForSingleObject(hander_ptr->process_handle_, INFINITE);
				}
				else if (WaitForSingleObject(hander_ptr->process_handle_, static_cast<DWORD>(hander_ptr->millis)) == WAIT_TIMEOUT)
				{
					if (hander_ptr->job_) TerminateJobObject(hander_ptr->job_, static_cast<UINT>(-1));   // 整树
					else TerminateProcess(hander_ptr->process_handle_, static_cast<UINT>(-1));
					m_result.terminated = true;
					m_result.term_signal = 9;
					WaitForSingleObject(hander_ptr->process_handle_, 1000);
					m_result.exec_error = process::error::timeout;
					m_result.exec_msg = "Timeout";
				}
			}
#else
			if (hander_ptr->process_id > 0)
			{
				if (hander_ptr->reaped)
				{
					// wait_for() 的探测已经回收过：直接用存下的状态，别再 waitpid（会得 ECHILD）
					status = hander_ptr->reaped_status;
					have_status = true;
				}
				else if (hander_ptr->millis == process::npos)
				{
					have_status = waitpid(hander_ptr->process_id, &status, 0) > 0;
				}
				else
				{
					xhqm::timer timer;
					int r = 0;
					while (r == 0)
					{
						r = waitpid(hander_ptr->process_id, &status, WNOHANG);
						if (r != 0) { have_status = (r > 0); break; }
						if (timer.mtimes() >= hander_ptr->millis)
						{
							kill(-hander_ptr->process_id, SIGTERM);   // 负号 = 进程组，杀整棵进程树
							std::this_thread::sleep_for(std::chrono::milliseconds(1000));
							if (waitpid(hander_ptr->process_id, &status, WNOHANG) <= 0)
							{
								kill(-hander_ptr->process_id, SIGKILL);
								waitpid(hander_ptr->process_id, &status, 0);
							}
							have_status = true;
							m_result.terminated = true;
							m_result.term_signal = 15;   // 先 SIGTERM；上面必要时已升级为 SIGKILL
							m_result.exec_error = process::error::timeout;
							m_result.exec_msg = "Timeout";
							break;
						}
						std::this_thread::sleep_for(std::chrono::milliseconds(10));
					}
				}
			}
#endif

			// 3-4. 子进程退出后管道写端关闭，worker 阻塞读到 EOF 自然结束；join
			if (hander_ptr->worker_out.joinable()) hander_ptr->worker_out.join();
			if (hander_ptr->worker_err.joinable()) hander_ptr->worker_err.join();

			// 5. 关闭读端
#if defined(XHQM_WINDOWS)
			if (hander_ptr->stdout_read_ != NULL) { CloseHandle(hander_ptr->stdout_read_); hander_ptr->stdout_read_ = NULL; }
			if (hander_ptr->stderr_read_ != NULL) { CloseHandle(hander_ptr->stderr_read_); hander_ptr->stderr_read_ = NULL; }
#else
			if (hander_ptr->stdout_read_ != -1) { close(hander_ptr->stdout_read_); hander_ptr->stdout_read_ = -1; }
			if (hander_ptr->stderr_read_ != -1) { close(hander_ptr->stderr_read_); hander_ptr->stderr_read_ = -1; }
#endif

			// 6. 读退出码 + 退出状态
#if defined(XHQM_WINDOWS)
			if (hander_ptr->process_handle_ != NULL)
			{
				DWORD exit_code = 0;
				if (GetExitCodeProcess(hander_ptr->process_handle_, &exit_code))
				{
					m_result.exit_code = static_cast<int>(exit_code);
					// NTSTATUS 异常码（0xC0000000 及以上）视为崩溃
					m_result.exit_stat = ((exit_code & 0xC0000000) == 0xC0000000)
						? process::exit_status::crash : process::exit_status::normal;
				}
			}
			if (hander_ptr->job_) { CloseHandle(hander_ptr->job_); hander_ptr->job_ = NULL; }   // KILL_ON_JOB_CLOSE：顺带回收残留子树
			CloseHandle(hander_ptr->process_handle_);
			hander_ptr->process_handle_ = NULL;
#else
			if (have_status)
			{
				if (WIFEXITED(status))
				{
					m_result.exit_code = WEXITSTATUS(status);
					m_result.exit_stat = process::exit_status::normal;
				}
				else if (WIFSIGNALED(status))
				{
					m_result.exit_code = -WTERMSIG(status);
					m_result.exit_stat = process::exit_status::crash;
				}
			}
#endif
			hander_ptr->process_id = -1;
			hander_ptr->runing = false;
			m_result.exec_time = hander_ptr->timer.mtimes();
			if (m_throw_on_error && m_result.exec_error != process::error::none)
			{
				throw std::runtime_error(m_result.exec_msg);
			}
		}

		// ---- 终止与探测（长驻子进程：mediamtx / ffmpeg / ffplay 要"起得来也停得掉"）----

		unsigned long process::pid() const
		{
			if (!hander_ptr || hander_ptr->process_id <= 0) return 0ul;
			return static_cast<unsigned long>(hander_ptr->process_id);
		}

		// 只探测退出，不动手；一旦退出就走 wait() 完成收尾（join 读线程、关句柄、取退出码）
		bool process::wait_for(const xhqm::count& millis)
		{
			if (!hander_ptr->runing) return true;
			if (hander_ptr->process_id <= 0) return true;
			xhqm::timer timer;
			for (;;)
			{
#if defined(XHQM_WINDOWS)
				const bool exited = (hander_ptr->process_handle_ != NULL)
					&& (WaitForSingleObject(hander_ptr->process_handle_, 0) == WAIT_OBJECT_0);
#else
				// ★ 绝不能用 `kill(pid, 0)` 当存活探测：**僵尸进程**（已退出、尚未回收）也会让它返回 0
				//   ⇒ 判定永远"还活着"、既不走 wait() 也不返回 ⇒ `wait_for(npos)` 死循环。
				//   实测症状：terminate() 第 3 步（SIGKILL 后 wait_for(npos)）永不返回，
				//   于是**每个带 equip 的节点退出时都挂住**（star_sprite / star_device 复现；
				//   star_echo 无 equip ⇒ 干净退出）。systemd 侧表现为只能等 TimeoutStopSec 后吃 SIGKILL。
				//   正确姿势是 waitpid(WNOHANG)：一次调用同时完成"判退出"与"回收"。
				int status = 0;
				const pid_t r = ::waitpid(static_cast<pid_t>(hander_ptr->process_id), &status, WNOHANG);
				bool exited;
				if (r == static_cast<pid_t>(hander_ptr->process_id))
				{
					hander_ptr->reaped = true;            // 状态先存下，交给 wait() 用
					hander_ptr->reaped_status = status;
					exited = true;
				}
				else if (r == 0) { exited = false; }       // 仍在运行
				else { exited = true; }                    // ECHILD/ESRCH：已回收或已不存在 ⇒ 算退出
#endif
				if (exited) { this->wait(); return true; }
				if (millis != process::npos && timer.mtimes() >= millis) return false;
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			}
		}

		bool process::terminate()
		{
			if (!hander_ptr->runing || hander_ptr->process_id <= 0) return true;
#if defined(XHQM_WINDOWS)
			if (hander_ptr->job_) TerminateJobObject(hander_ptr->job_, 9);            // 整树
			else if (hander_ptr->process_handle_) TerminateProcess(hander_ptr->process_handle_, 9);
#else
			kill(-hander_ptr->process_id, SIGKILL);                                   // 负号 = 进程组
#endif
			m_result.terminated = true;
			m_result.term_signal = 9;
			return true;
		}

		bool process::terminate(const xhqm::count& grace_ms)
		{
			if (!hander_ptr->runing || hander_ptr->process_id <= 0) return true;
			// 1) 温和：POSIX 向进程组发 SIGTERM；Windows 向该控制台进程组发 CTRL_BREAK
#if defined(XHQM_WINDOWS)
			::GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, static_cast<DWORD>(hander_ptr->process_id));
#else
			kill(-hander_ptr->process_id, SIGTERM);
#endif
			m_result.terminated = true;
			m_result.term_signal = 15;
			// 2) 宽限
			if (this->wait_for(grace_ms == process::npos ? 1000 : grace_ms)) return true;
			// 3) 强杀 + 等它真正退出（wait_for(npos) 会走 wait() 收尾）
			this->terminate();
			this->wait_for(process::npos);
			return true;
		}

		bool process::active()
		{
			if (!hander_ptr->runing)
			{
				return hander_ptr->worker_out.joinable() || hander_ptr->worker_err.joinable();
			}
			return hander_ptr->runing;
		}

		bool process::runing()
		{
			return hander_ptr->runing;
		}

	}
}
