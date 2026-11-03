#ifndef XHQM_TOOL_FILESYS
#define XHQM_TOOL_FILESYS
#include <filesystem>
#include "../../head.h"
namespace xhqm
{
	class xhqm_io filesys
	{
	public:
		static bool is_dir(const std::string& strpath);
		static bool is_file(const std::string& strpath);
		static bool is_exists(const std::string& strpath);
		static std::vector<std::string> dir_list(const std::string& strdirpath);
		static std::vector<std::string> file_list(const std::string& strdirpath);
		static std::vector<std::string> path_list(const std::string& strdirpath);
		static bool rename(const std::string& oldpath, const std::string& newpath);
		static bool move_dir(const std::string& oldpath, const std::string& newpath);
		static bool move_file(const std::string& oldpath, const std::string& newpath);
		static bool copy_dir(const std::string& oldpath, const std::string& newpath);
		static bool copy_file(const std::string& oldpath, const std::string& newpath);
		static bool create_dir(const std::string& strpath);
		static bool create_file(const std::string& strpath);
		static bool delete_dir(const std::string& strpath);
		static bool delete_file(const std::string& strpath);
		static std::string env_value(const std::string& envname);
		static std::string data_directory(const std::string& appname);
		static std::string temp_directory();
		static std::string path_directory();
		static std::string work_directory();
		static std::string current_path();

	};
}

#endif