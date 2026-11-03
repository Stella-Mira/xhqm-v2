#include "filesys.h"
#include <fstream>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <unistd.h>
#include <limits.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>   // _NSGetExecutablePath
#endif
#endif

namespace fs = std::filesystem;

namespace xhqm
{
	bool filesys::is_dir(const std::string& strpath)
	{
		std::filesystem::path path = strpath;
		if (std::filesystem::exists(path))
		{
			return std::filesystem::is_directory(path);
		}
		return false;
	}
	bool filesys::is_file(const std::string& strpath)
	{
		std::filesystem::path path = strpath;
		if (std::filesystem::exists(path))
		{
			return !std::filesystem::is_directory(path);
		}
		return false;
	}
	bool filesys::is_exists(const std::string& strpath)
	{
		return std::filesystem::exists(strpath);
	}
	std::vector<std::string> filesys::dir_list(const std::string& strdirpath)
	{
		std::vector<std::string> list;
		if (is_dir(strdirpath))
		{
			std::filesystem::directory_iterator dirite(strdirpath);
			for (auto& ite : dirite)
				if (ite.status().type() == std::filesystem::file_type::directory) //is dir
					list.push_back(ite.path().string());
		}
		return list;
	}
	std::vector<std::string> filesys::file_list(const std::string& strdirpath)
	{
		std::vector<std::string> list;
		if (is_dir(strdirpath))
		{
			std::filesystem::directory_iterator dirite(strdirpath);
			for (auto& ite : dirite)
				if (ite.status().type() == std::filesystem::file_type::regular) //is file
					list.push_back(ite.path().filename().string());
		}
		return list;
	}
	std::vector<std::string> filesys::path_list(const std::string& strdirpath)
	{
		std::vector<std::string> list;
		std::vector<std::string> temp;
		temp = dir_list(strdirpath);
		list.insert(list.end(), temp.begin(), temp.end());
		temp = file_list(strdirpath);
		list.insert(list.end(), temp.begin(), temp.end());
		return list;
	}
	bool filesys::rename(const std::string& oldpath, const std::string& newpath)
	{
		if (std::filesystem::exists(oldpath) && !std::filesystem::exists(newpath))
		{
			std::filesystem::rename(oldpath, newpath);
			return true;
		}
		return false;
	}
	bool filesys::move_dir(const std::string& oldpath, const std::string& newpath)
	{
		if (is_dir(oldpath) && !std::filesystem::exists(newpath))
		{
			std::filesystem::rename(oldpath, newpath);
			return true;
		}
		return false;
	}
	bool filesys::move_file(const std::string& oldpath, const std::string& newpath)
	{
		if (is_file(oldpath) && !std::filesystem::exists(newpath))
		{
			std::filesystem::rename(oldpath, newpath);
			return true;
		}
		return false;
	}
	bool filesys::copy_dir(const std::string& oldpath, const std::string& newpath)
	{
		if (is_dir(oldpath) && !std::filesystem::exists(newpath))
		{
			std::filesystem::copy(oldpath, newpath);
			return true;
		}
		return false;
	}
	bool filesys::copy_file(const std::string& oldpath, const std::string& newpath)
	{
		if (is_file(oldpath) && !std::filesystem::exists(newpath))
		{
			return std::filesystem::copy_file(oldpath, newpath);
		}
		return false;
	}
	bool filesys::create_dir(const std::string& strpath)
	{
		std::filesystem::path path = strpath;
		if (!std::filesystem::exists(path))
		{
			return std::filesystem::create_directory(path);
		}
		return false;
	}
	bool filesys::create_file(const std::string& strpath)
	{
		std::filesystem::path path = strpath;
		if (!std::filesystem::exists(path))
		{
			std::fstream file(path, std::ios::out | std::ios::binary);
			file.close();
			return true;
		}
		return false;
	}
	bool filesys::delete_dir(const std::string& strpath)
	{
		if (is_dir(strpath))
		{
			std::filesystem::remove_all(strpath);
			return true;
		}
		return false;
	}
	bool filesys::delete_file(const std::string& strpath)
	{
		if (is_file(strpath))
		{
			std::filesystem::remove(strpath);
			return true;
		}
		return false;
	}

	std::string filesys::env_value(const std::string& envname) {
#ifdef _WIN32
		char* env_value = nullptr;
		size_t len = 0;
		if (_dupenv_s(&env_value, &len, envname.c_str()) == 0 && env_value != nullptr) {
			std::string result(env_value);
			free(env_value);
			return result;
		}
		return "";
#else
		const char* env_value = std::getenv(envname.c_str());
		return env_value ? std::string(env_value) : "";
#endif
	}

	std::string filesys::data_directory(const std::string& appname)
	{
		fs::path data_dir;

#ifdef _WIN32
		std::string appdata = env_value("APPDATA");
		if (!appdata.empty()) {
			data_dir = fs::path(appdata) / appname;
		}
		else {
			data_dir = path_directory() + "\\data";
		}

#elif defined(__APPLE__)
		const char* home = std::getenv("HOME");
		if (home) {
			data_dir = fs::path(home) / "Library" / "Application Support" / appname;
		}
		else {
			data_dir = path_directory() + "/data";
		}

#elif defined(__linux__)
		const char* xdg_data = std::getenv("XDG_DATA_HOME");
		if (xdg_data) {
			data_dir = fs::path(xdg_data) / appname;
		}
		else {
			const char* home = std::getenv("HOME");
			if (home) {
				data_dir = fs::path(home) / ".local" / "share" / appname;
			}
			else {
				data_dir = path_directory() + "/data";
			}
		}
#endif

		// 创建目录（如果不存在）
		if (!data_dir.empty() && !fs::exists(data_dir)) {
			fs::create_directories(data_dir);
		}

		return data_dir.string();
	}

	std::string filesys::temp_directory()
	{
#ifdef _WIN32
		wchar_t buffer[MAX_PATH];
		DWORD length = GetTempPathW(MAX_PATH, buffer);
		if (length > 0 && length < MAX_PATH) {
			return fs::path(buffer, buffer + length).string();
		}
#else
		const char* tmpdir = std::getenv("TMPDIR");
		if (tmpdir) {
			return fs::path(tmpdir).string();
		}
#endif

		return fs::temp_directory_path().string();  // 标准库回退
	}

	std::string filesys::path_directory()
	{
		static fs::path cached_path;
		if (!cached_path.empty()) {
			return cached_path.string();
		}

#if defined(_WIN32)
		wchar_t buffer[MAX_PATH];
		DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
		if (length > 0 && length < MAX_PATH) {
			cached_path = fs::path(buffer, buffer + length).parent_path();
		}
#elif defined(__linux__)
		char buffer[PATH_MAX];
		ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
		if (length != -1) {
			buffer[length] = '\0';
			cached_path = fs::path(buffer).parent_path();
		}
#elif defined(__APPLE__)
		char buffer[PATH_MAX];
		uint32_t size = sizeof(buffer);
		if (_NSGetExecutablePath(buffer, &size) == 0) {
			char real_path[PATH_MAX];
			if (realpath(buffer, real_path) != nullptr) {
				cached_path = fs::path(real_path).parent_path();
			}
		}
#endif

		if (cached_path.empty()) {
			cached_path = fs::current_path();  // 回退方案
		}

		return cached_path.string();
	}

	std::string filesys::work_directory()
	{
		static fs::path cached_path;
		if (!cached_path.empty()) {
			return cached_path.string();
		}

#if defined(_WIN32)
		wchar_t buffer[MAX_PATH];
		DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
		if (length > 0 && length < MAX_PATH) {
			cached_path = fs::path(buffer, buffer + length).parent_path();
		}
#elif defined(__linux__)
		char buffer[PATH_MAX];
		ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
		if (length != -1) {
			buffer[length] = '\0';
			cached_path = fs::path(buffer).parent_path();
		}
#elif defined(__APPLE__)
		char buffer[PATH_MAX];
		uint32_t size = sizeof(buffer);
		if (_NSGetExecutablePath(buffer, &size) == 0) {
			char real_path[PATH_MAX];
			if (realpath(buffer, real_path) != nullptr) {
				cached_path = fs::path(real_path).parent_path();
			}
		}
#endif

		if (cached_path.empty()) {
			cached_path = fs::current_path();  // 回退方案
		}

		return cached_path.string();
	}

	std::string filesys::current_path()
	{
		return std::filesystem::current_path().string();
	}
}