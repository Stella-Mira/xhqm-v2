#ifndef XHQM_TOOL_LIBMANGE
#define XHQM_TOOL_LIBMANGE
#include "../../head.h"
#include "../../metaclass/resource.h"
#include "../../software/system/filesys.h"
#include <functional>

namespace xhqm
{
	typedef void* libid;
	typedef void* libfunc;

	class xhqm_io libmange
	{
	public:
		static libid loadlib(const std::string& libpath);
		static void freelib(libid id);

		// ① 原始：返回符号指针（void*，仅作兜底）
		static libfunc function(libid id, const std::string& funname);

		// 探测符号是否存在
		static bool has_function(libid id, const std::string& funname)
		{
			return libmange::function(id, funname) != nullptr;
		}

		// ② 查一次符号，返回强类型函数指针（高频复用，零运行时开销）
		template<typename return_type, typename... argvs_type>
		static return_type(*function_ptr(libid id, const std::string& funname))(argvs_type...)
		{
			return reinterpret_cast<return_type(*)(argvs_type...)>(libmange::function(id, funname));
		}

		// ③ 查符号 + 直接调用（低频一步到位）
		template<typename return_type, typename... argvs_type>
		static return_type function(libid id, const std::string& funname, argvs_type&&... values)
		{
			auto fn = libmange::function_ptr<return_type, argvs_type...>(id, funname);
			if (fn == nullptr) throw xhqm::error("unknown library function: " + funname);
			return fn(std::forward<argvs_type>(values)...);
		}

		// 自身模块句柄（主程序），用于「主程序导出符号给插件回调」；此句柄不可 freelib
		static libid self();

		static void depend(const std::string& dirpath);
		static std::string error();
	};


	class xhqm_io _libary
	{
		xhqm::libid m_id = nullptr;
	public:
		_libary() {};

		_libary(_libary&& move)  noexcept;

		_libary(const _libary& copy) = delete;

		_libary(xhqm::libid id);

		_libary(const std::string& path);

		~_libary();


		void reset();

		void reset(xhqm::libid id);

		void reset(const std::string& path);

		_libary& operator =(_libary&& move)  noexcept;

		void operator =(const _libary& copy) = delete;


		bool operator==(const _libary& lib) const noexcept
		{
			return (this->m_id == lib.m_id);
		}

		bool operator!=(const _libary& lib) const noexcept
		{
			return (this->m_id != lib.m_id);
		}

		template<typename return_type, typename ...argvs_type>
		return_type function(const std::string& funname, argvs_type&& ... values)
		{
			return xhqm::libmange::function<return_type, argvs_type...>(m_id, funname, std::forward<argvs_type>(values)...);
		}

		operator xhqm::libid() const
		{
			return m_id;
		}
	};

	using libary = xhqm::resource<xhqm::_libary, xhqm::libid>;

}
#endif
