#ifndef XHQM_TOOL_PROCESS
#define XHQM_TOOL_PROCESS

#include <functional>
#include "../../../head.h"
#include "../../../algebra/value.h"

namespace xhqm
{
	namespace parallel
	{
		class xhqm_io process
		{
		public:
			using dir = std::string;						// 工作目录
			using env = std::map<xhqm::text, xhqm::text>;	// 环境变量
			static constexpr auto npos = static_cast<xhqm::count>(-1); //下标上限

			// 错误类型（对标 QProcess::ProcessError）
			enum class error : unsigned char
			{
				none = 0,		// 无错误
				fail_start,		// 启动失败
				crashed,		// 崩溃退出
				timeout,		// 超时终止
				read_error,		// 读取管道错误
				write_error,	// 写入管道错误
			};

			// 退出状态（对标 QProcess::ExitStatus）
			enum class exit_status : unsigned char
			{
				normal = 0,		// 正常退出
				crash = 1,		// 异常崩溃
			};

			struct result
			{
				int exit_code = 0;							// 退出码
				exit_status exit_stat = exit_status::normal;	// 退出状态
				error exec_error = error::none;				// 错误类型
				xhqm::string exec_msg;						// 错误描述
				xhqm::count exec_time = 0;					// 执行时间
				bool terminated = false;					// 是否由本对象主动终止（terminate / 超时强杀）
				int term_signal = 0;						// 终止方式：0=无，15=温和(SIGTERM/CTRL_BREAK)，9=强杀(SIGKILL/Job)
				std::vector<xhqm::text> exec_out;			// 标准输出
				std::vector<xhqm::text> exec_err;			// 标准错误
				bool success() const { return (exit_code == 0) && (exec_error == error::none); }
			};
		private:
			struct hander;
			bool m_shell{ false };						// command 模式标记
			bool m_raw_line{ false };					// command_line 模式标记（整串即命令行）
			std::string m_program;						// program 模式：程序路径
			std::vector<std::string> m_arguments;		// program 模式：程序参数
			std::string m_command;						// command 模式：命令字符串（可含管道等 shell 特性）
			dir m_workdir;								// 工作目录
			env m_env;									// 环境变量
			bool m_capture_stdout{ true };				// 捕获标准输出
			bool m_capture_stderr{ true };				// 捕获标准错误
			bool m_stderr_stdout{ true };				// 合并标准错误到标准输出
			bool m_throw_on_error{ false };				// 错误时抛出异常
			std::function<void(std::string&)> m_function_i;			// 输入数据捕获
			std::function<void(const std::string&)> m_function_o;	// 输出数据捕获
			std::function<void(const char*, std::size_t)> m_function_b;	// 原始字节捕获（二进制 stdout）
			process::result m_result;
			std::shared_ptr<process::hander> hander_ptr;
		public:
			process(const xhqm::count& millis = process::npos);
			~process() { this->wait(10); }

			inline const process::result& get_result() const { return m_result; }

			// program 模式：直接执行程序（对标 QProcess::start）
			void program(const std::string& path, const std::vector<std::string>& args = std::vector<std::string>());

			// command 模式：shell 执行命令字符串（对标 QProcess::startCommand）
			void command(const std::string& line);

			// 原始命令行模式：整串直接作为系统的命令行交给进程（Windows 不套 cmd /c；POSIX 等价 sh -c）。
			// 用于"命令行已由上层拼好、引号/管道/非 ASCII 必须原样保留"的场景
			// （ffmpeg dshow 设备名、含中文的路径、`a | b` 管道串等）。
			void command_line(const std::string& line);

			// 参数设置
			inline void workdir(const dir& value) { m_workdir = value; }
			inline void environment(const env& value) { m_env = value; }
			inline void capture_stdout(bool value) { m_capture_stdout = value; }
			inline void capture_stderr(bool value) { m_capture_stderr = value; }
			inline void stderr_stdout(bool value) { m_stderr_stdout = value; }
			inline void throw_on_error(bool value) { m_throw_on_error = value; }
			inline void function_i(std::function<void(std::string&)> value) { m_function_i = std::move(value); }
			inline void function_o(std::function<void(const std::string&)> value) { m_function_o = std::move(value); }

			// 原始字节捕获（二进制 stdout，如 `ffmpeg -f s16le -` 抽 PCM、抽 JPEG 帧）：
			// 设置后 stdout **不再按行切分、不剥 \r**，每个读到的块原样回调（文本仍用 function_o）。
			// 说明：exec_out/function_o 都是"按行"语义，二进制流必须走这条，否则数据被切坏。
			inline void function_b(std::function<void(const char*, std::size_t)> value) { m_function_b = std::move(value); }

			// 生命周期
			void timeout(const xhqm::count& value);
			inline void wait(const xhqm::count& value) { timeout(value); wait(); }
			void exec();
			void wait();
			std::string input(std::string& input);
			void close_write();
			bool active();
			bool runing();

			// ---- 终止与探测（长驻子进程用：mediamtx / ffmpeg / ffplay 这类要"起得来也停得掉"）----

			// 子进程标识（诊断/日志/外部托管用）；未启动或已回收返回 0
			unsigned long pid() const;

			// 等待至多 millis 毫秒：true = 已退出（并完成收尾：join 读线程、关句柄、取退出码），
			// false = 仍在运行（**不杀**，交调用方决定后续）；millis = process::npos 表示一直等
			bool wait_for(const xhqm::count& millis);

			// 立即强制终止（POSIX：kill(-pid, SIGKILL) 进程组；Windows：TerminateJobObject 整树）。
			// 只发终止，不收尾 —— 之后请调 wait() 拿退出码与收尾。
			bool terminate();

			// 温和终止 → 宽限 grace_ms → 仍未退出则强杀并等它收尾；返回是否已退出。
			//   POSIX：SIGTERM 到进程组；Windows：GenerateConsoleCtrlEvent(CTRL_BREAK)
			bool terminate(const xhqm::count& grace_ms);
		};
	}
}

#endif
