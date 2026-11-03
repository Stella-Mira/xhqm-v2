#ifndef XHQM_COMMAND
#define XHQM_COMMAND

// 命令：execute + undo 回调；命令历史：撤销/重做栈
// 单线程：无锁；多线程：加锁，undo/redo 操作栈
#include <vector>
#include <mutex>
#include <functional>
#include "thread_mode.h"

namespace xhqm
{
	// 命令对象：execute 执行，undo 撤销（undo 可为空表示不可撤销）
	class command
	{
		std::function<void()> m_execute;
		std::function<void()> m_undo;
	public:
		command() = default;
		command(std::function<void()> execute, std::function<void()> undo = nullptr)
			: m_execute(std::move(execute)), m_undo(std::move(undo)) {}

		void execute() { if (m_execute) m_execute(); }
		void undo() { if (m_undo) m_undo(); }
		bool undoable() const { return (bool)m_undo; }
	};

	// 主模板：只声明，实现见单/多线程特化
	template<class thread = xhqm::single_thread>
	class command_history;

	// 单线程命令历史（默认）
	template<>
	class command_history<xhqm::single_thread>
	{
		std::vector<xhqm::command> m_undo_stack;
		std::vector<xhqm::command> m_redo_stack;
	public:
		// 执行命令并压入撤销栈（新命令清空重做栈）
		void execute(xhqm::command cmd)
		{
			cmd.execute();
			m_undo_stack.push_back(std::move(cmd));
			m_redo_stack.clear();
		}

		bool undo()
		{
			if (m_undo_stack.empty()) return false;
			xhqm::command cmd = std::move(m_undo_stack.back());
			m_undo_stack.pop_back();
			cmd.undo();
			m_redo_stack.push_back(std::move(cmd));
			return true;
		}

		bool redo()
		{
			if (m_redo_stack.empty()) return false;
			xhqm::command cmd = std::move(m_redo_stack.back());
			m_redo_stack.pop_back();
			cmd.execute();
			m_undo_stack.push_back(std::move(cmd));
			return true;
		}

		bool can_undo() const { return !m_undo_stack.empty(); }
		bool can_redo() const { return !m_redo_stack.empty(); }
	};

	// 多线程命令历史
	template<>
	class command_history<xhqm::multi_thread>
	{
		mutable std::mutex m_mutex;
		std::vector<xhqm::command> m_undo_stack;
		std::vector<xhqm::command> m_redo_stack;
	public:
		command_history() = default;
		command_history(command_history&&) = delete;
		command_history(const command_history&) = delete;

		void execute(xhqm::command cmd)
		{
			cmd.execute();
			std::lock_guard<std::mutex> lock(m_mutex);
			m_undo_stack.push_back(std::move(cmd));
			m_redo_stack.clear();
		}

		bool undo()
		{
			xhqm::command cmd;
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				if (m_undo_stack.empty()) return false;
				cmd = std::move(m_undo_stack.back());
				m_undo_stack.pop_back();
			}
			cmd.undo();
			std::lock_guard<std::mutex> lock(m_mutex);
			m_redo_stack.push_back(std::move(cmd));
			return true;
		}

		bool redo()
		{
			xhqm::command cmd;
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				if (m_redo_stack.empty()) return false;
				cmd = std::move(m_redo_stack.back());
				m_redo_stack.pop_back();
			}
			cmd.execute();
			std::lock_guard<std::mutex> lock(m_mutex);
			m_undo_stack.push_back(std::move(cmd));
			return true;
		}

		bool can_undo() const { std::lock_guard<std::mutex> lock(m_mutex); return !m_undo_stack.empty(); }
		bool can_redo() const { std::lock_guard<std::mutex> lock(m_mutex); return !m_redo_stack.empty(); }
	};
}

#endif
