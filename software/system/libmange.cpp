#include "libmange.h"
#include "../pattern/instance.h"

#ifdef XHQM_WINDOWS
#include <Windows.h>
#else
#include<dlfcn.h>
#endif

namespace xhqm
{
	namespace
	{
		std::string& last_error()
		{
			thread_local std::string err;
			return err;
		}

		std::string system_error()
		{
#ifdef XHQM_WINDOWS
			const DWORD code = GetLastError();
			if (code == 0) return "";
			LPSTR buf = nullptr;
			const DWORD len = FormatMessageA(
				FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
				NULL, code, 0, reinterpret_cast<LPSTR>(&buf), 0, NULL);
			std::string msg;
			if (len > 0 && buf != nullptr)
			{
				msg.assign(buf, len);
				while (!msg.empty() && (msg.back() == '\r' || msg.back() == '\n')) msg.pop_back();
				LocalFree(buf);
			}
			return msg;
#else
			const char* msg = dlerror();
			return msg ? std::string(msg) : std::string();
#endif
		}
	}

	_libary::_libary(_libary&& move)  noexcept
	{
		m_id = move.m_id;
		move.m_id = nullptr;
	}

	_libary::_libary(xhqm::libid id)
	{
		_libary::reset(id);
	}

	_libary::_libary(const std::string& path)
	{
		_libary::reset(path);
	}

	_libary::~_libary()
	{
		_libary::reset();
	}

	void _libary::reset()
	{
		if (m_id)
		{
			libmange::freelib(m_id);
		}
		m_id = nullptr;
	}

	void _libary::reset(xhqm::libid id)
	{
		if (m_id)
		{
			libmange::freelib(m_id);
		}
		m_id = id;
	}

	void _libary::reset(const std::string& path)
	{
		if (m_id)
		{
			libmange::freelib(m_id);
		}
		m_id = libmange::loadlib(path);
	}

	_libary& _libary::operator =(_libary&& move)  noexcept
	{
		if (this != &move)
		{
			_libary::reset();
			m_id = move.m_id;
			move.m_id = nullptr;
		}
		return *this;
	}


	libid libmange::loadlib(const std::string& libpath)
	{
		last_error().clear();
		libid id = nullptr;
#ifdef XHQM_WINDOWS
		// 无条件走宽字符，识别中文路径（避免 setlocale 的全局副作用）
		std::wstring wlibpath = std::filesystem::path(libpath).wstring();
		id = reinterpret_cast<libid>(LoadLibraryW(wlibpath.c_str()));
#else
		id = dlopen(libpath.c_str(), RTLD_LAZY);
#endif
		if (id == nullptr) last_error() = system_error();
		return id;
	}

	void libmange::freelib(libid id)
	{
		if (id)
		{
#ifdef XHQM_WINDOWS
			FreeLibrary(reinterpret_cast<HINSTANCE>(id));
#else
			dlclose(id);
#endif
		}
	}


	libfunc libmange::function(libid id, const std::string& funname)
	{
#ifdef XHQM_WINDOWS
		return reinterpret_cast<libfunc>(GetProcAddress(reinterpret_cast<HINSTANCE>(id), funname.c_str()));
#else
		return reinterpret_cast<libfunc>(dlsym(id, funname.c_str()));
#endif
	}

	void libmange::depend(const std::string& dirpath)
	{
#ifdef XHQM_WINDOWS
		std::wstring wdir = std::filesystem::path(dirpath).wstring();
		SetDllDirectoryW(wdir.c_str());
#else
		// dlopen 的依赖搜索由 RPATH / LD_LIBRARY_PATH 决定，运行时无法等价注入
		(void)dirpath;
#endif
	}


	libid libmange::self()
	{
#ifdef XHQM_WINDOWS
		return reinterpret_cast<libid>(GetModuleHandleW(NULL));
#else
		return dlopen(NULL, RTLD_LAZY);
#endif
	}

	std::string libmange::error()
	{
		if (!last_error().empty()) return last_error();
		return system_error();
	}
}
