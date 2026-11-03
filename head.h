#ifndef XHQM_HEAD
#define XHQM_HEAD

// 禁止 <windows.h>/<winsock2.h> 定义 min/max 宏，避免与 algebra/mathf 的 min/max 模板冲突
#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifdef _MSC_VER
#pragma warning (disable:4251)
#if !defined(XHQM_PROGRAM) && !defined(XHQM_DYNAMIC)
#pragma comment(lib,"../binary/xhqm.lib")
#endif
#define XHQMEXPORT  __declspec(dllexport)
#define XHQMIMPORT  __declspec(dllimport)
#else
#define XHQMEXPORT
#define XHQMIMPORT
#endif

#ifdef XHQM_PROGRAM
#define XHQMCLASS  XHQMEXPORT
#else
#define XHQMCLASS  XHQMIMPORT
#endif

#ifdef XHQM_DYNAMIC
#define xhqm_io XHQMCLASS
#else
#define xhqm_io
#endif

#include <map>
#include <list>
#include <array>
#include <vector>
#include <string>
#include <sstream>
#include <iostream>
#include <initializer_list>
#include <type_traits>

// json：定义 XHQM_JSON_EXTERNAL 则使用环境提供的 nlohmann/json.hpp，
// 否则使用 xhqm 自带 network/json.hpp（已吸收进引擎）
#ifdef XHQM_JSON_EXTERNAL
#include <nlohmann/json.hpp>
#else
#include "./network/json.hpp"
#endif

namespace xhqm_std
{
	template <typename type, std::size_t size>
	using array = std::array<type, size>;

	template <typename type, std::size_t size>
	using grids = std::array<std::array<type, size>, size>;

	template <typename type, std::size_t size>
	using magic = std::array<std::array<std::array<type, size>, size>, size>;

	template <typename type>
	using list = std::list<type>;

	template <typename type>
	using vector = std::vector<type>;

	template <typename type>
	using matrix = std::vector<std::vector<type>>;

	template <typename type>
	using tensor = std::vector<std::vector<std::vector<type>>>;

	template <typename type, std::size_t size>
	struct docker_s { using value = std::array<type, size>; };

	template <typename type>
	struct docker_s<type, 0> { using value = std::vector<type>; };

	template <typename type, std::size_t size = 0>
	using docker = typename xhqm_std::docker_s<type, size>::value;
}

namespace xhqm
{
	struct os {
		enum type {
			os_no = 0,
			os_ios,
			os_mac,
			os_unix,
			os_linux,
			os_posix,
			//os_win32,
			//os_win64,
			os_window,
			os_android,
			os_harmony,
		};

#ifdef _WIN32
		//define something for Windows (32-bit and 64-bit, this part is common)
		constexpr static type this_os = os_window;
#define XHQM_WINDOWS
		//#ifdef _WIN64
		//   //define something for Windows (64-bit only)
		//		constexpr static type this_os = os_win64;
		//#else
		//   //define something for Windows (32-bit only)
		//		constexpr static type this_os = os_win32;
		//#endif
#elif __APPLE__
#if TARGET_IPHONE_SIMULATOR
		constexpr static type this_os = os_ios;// iOS Simulator
#define XHQM_IOS
#elif TARGET_OS_IPHONE
		constexpr static type this_os = os_ios;// iOS device
#define XHQM_IOS
#elif TARGET_OS_MAC
		constexpr static type this_os = os_mac;
#define XHQM_MAC
#elif __ANDROID__
		constexpr static type this_os = os_android;
#define XHQM_ANDROID
#elif __linux__
		constexpr static type this_os = os_linux;
#define XHQM_LINUX
#elif __unix__ // all unices not caught above
		constexpr static type this_os = os_unix;
#define XHQM_UNIX
#elif defined(_POSIX_VERSION)
		constexpr static type this_os = os_posix;
#define XHQM_POSIX
#else
		constexpr static type this_os = os_no;
#define XHQM_OSNO
#endif
#endif
	};
}

namespace xhqm
{
	struct device {
		enum type {
			device_no = 0,
			device_cpu,
			device_gpu,
			device_net,
			device_memory,
			device_cpu_memory,
			device_gpu_memory,
			device_net_memory,
		};
		using value = uint32_t;
		using local = uint8_t*;

		device::type this_device = device::type::device_no;
		std::string name = "device";
		std::string info = "device";
		std::size_t id = 0;

		inline static bool isle()
		{
			device::value dataRef = 0x00000001;
			device::local bytePtr = reinterpret_cast<device::local>(&dataRef);
			return (*bytePtr == 0x01);
		}
		inline static bool isbe()
		{
			device::value dataRef = 0x00000001;
			device::local bytePtr = reinterpret_cast<device::local>(&dataRef);
			return (*bytePtr == 0x00);
		}
	};


	//static std::vector<xhqm::device> devices();
}


namespace xhqm
{
	//宏函数
#define xhqm_redef(fir, sec) typedef fir sec;
#define xhqm_using(sec, fir) using sec = fir;

#define xhqm_max(_val1, _val2) ((_val1) > (_val2) ? (_val1) : (_val2))
#define xhqm_min(_val1, _val2) ((_val1) < (_val2) ? (_val1) : (_val2))
#define xhqm_abs(_val1, _val2) (xhqm_max((_val1), (_val2)) - xhqm_min((_val1), (_val2)))

#define xhqm_rount(_val) ((int)((_val) + 0.5f))
#define xhqm_equal(_val1, _val2, _preci) ((_preci) >= xhqm_abs((_val1), (_val2)))
#define xhqm_preci(_val1, _val2) xhqm_equal((_val1), (_val2), xhqm::preci)

#define xhqm_create(_ptr, _class) if(!_ptr){ _ptr = new _class;}
#define xhqm_delete(_ptr) if(_ptr){ delete _ptr; _ptr = nullptr;}

#define xhqm_byte(_data, _ind)	((unsigned char*)(&_data))[_ind]
#define xhqm_uchar(value) static_cast<unsigned char>(value)
#define xhqm_schar(value) static_cast<signed char>(value)
#define xhqm_f64(value) static_cast<double>(value)
#define xhqm_f32(value) static_cast<float>(value)

	//字符串
#define _XHQMCONNECTSTR(x,y) x##y	//拼接 
#define _XHQMTOSTR(x) #x			//转字符串
#define XHQMCONNECTSTR(x,y) _XHQMCONNECTSTR(x,y)
#define XHQMTOSTR(x) _XHQMTOSTR(x)

	//using measure = double;
	//using integer = std::intmax_t;
	//using natural = std::uintmax_t;

	//模板体系
	using string = std::string;
	using index = std::size_t;	//下标
	using count = std::size_t;	//数量
	using size = std::size_t;   //大小
	using id = std::size_t;	    //ID

	//using coord = typename std::initializer_list<xhqm::index>;
	//using topol = typename std::initializer_list<xhqm::index>;
	//using shape = typename std::initializer_list<xhqm::count>;
	template<xhqm::size dim = 0> using coord = typename xhqm_std::docker_s<xhqm::index, dim>::value;
	template<xhqm::size dim = 0> using topol = typename xhqm_std::docker_s<xhqm::index, dim>::value;
	template<xhqm::size dim = 0> using shape = typename xhqm_std::docker_s<xhqm::count, dim>::value;

	//内存体系
	using real = float;
	using writ = signed char;
	using sign = signed char;
	using byte = unsigned char;
	using text = std::string;
	using byte_1 = std::uint8_t;
	using byte_2 = std::uint16_t;
	using byte_4 = std::uint32_t;
	using byte_8 = std::uint64_t;

	class error : public std::runtime_error
	{
	public: explicit error(const std::string& msg) : std::runtime_error(msg) {}
	};

	inline namespace templates {
		//要求type有默认的构造函数进行初始化
		template <typename type>
		struct initialize {
			const type value;
			inline initialize() :value() {};
			inline initialize(const type& val) :value(val) {};

			static constexpr type init = type();
		};
	};
	inline namespace literals {
		inline bool operator""_false(const char* str, xhqm::size) {
			std::cerr << xhqm::string(str) << std::endl;
			return false;
		}
		inline bool operator""_true(const char* str, xhqm::size) {
			std::cout << xhqm::string(str) << std::endl;
			return true;
		}

		inline void operator""_cout(const char* str, xhqm::size) {
			std::cout << xhqm::string(str) << std::endl;
		}
		inline void operator""_cout(unsigned long long val) {
			std::cout << val << std::endl;
		}
		inline void operator""_cout(long double val) {
			std::cout << val << std::endl;
		}
	};
}


#endif
