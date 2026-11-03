#ifndef XHQM_TEMPLATE
#define XHQM_TEMPLATE
#include "../head.h"
#include <list>
#include <array>
#include <vector>
#include <functional>

namespace xhqm 
{
	template <typename _type> struct rm_ref { using type = _type; };
	template <typename _type> struct rm_ref<_type&> { using type = _type; };
	template <typename _type> struct rm_ref<_type&&> { using type = _type; };
	template <typename _type> struct rm_const { using type = _type; };
	template <typename _type> struct rm_const<const _type> { using type = _type; };

	template <typename _type> using rm_ref_t = typename rm_ref<_type>::type;
	template <typename _type> using rm_const_t = typename rm_const<_type>::type;
	template <typename _type> using rm_const_ref_t = rm_const_t<rm_ref_t<_type>>;
	template <typename _type> using const_ref_t = const rm_const_t<rm_ref_t<_type>>&;

	template <typename _type> constexpr rm_ref_t<_type>&& move(_type&& value_) noexcept { return static_cast<rm_ref_t<_type>&&>(value_); }
	template <typename _type> constexpr inline const_ref_t<_type> copy(const _type& value_) noexcept { return value_; }

	inline namespace templates
	{
		//bool
		template <class> constexpr bool always_true = true;
		template <class> constexpr bool always_false = false;
		template <class, class> constexpr bool is_same = false;
		template <class type> constexpr bool is_same<type, type> = true;

		//计数
		template<class types> struct arg_types_s { using type = types; };
		template<class ...type_arg> constexpr xhqm::size size_count = sizeof...(type_arg);

		//bool 类型选择
		template<bool select, class type_1, class type_2> struct select_if { using type = type_1; };
		template<class type_1, class type_2> struct select_if<false, type_1, type_2> { using type = type_2; };
		template<bool select, class type_1, class type_2> using select_if_type = typename select_if<select, type_1, type_2>::type;

		//下标 类型选择
		template<xhqm::size select, class type_select, class ...type_arg>
		struct select_index { using type = typename select_index<select - 1, type_arg...>::type; };
		template<class type_select, class ...type_arg>
		struct select_index<0, type_select, type_arg...> { using type = type_select; };
		template<xhqm::size select, class ...type_arg>
		using select_index_type = typename select_index<select, type_arg...>::type;

		//类型选择
		template<xhqm::size select, class ...type_arg> using select_class = select_index_type<select, type_arg...>;
		template<xhqm::size select, class ...type_arg> using select_function = std::function<select_class<select, type_arg...>>;


		////容器
		//template<class type_in> using list = std::list<type_in>;
		//template<class type_in> using vector = std::vector<type_in>;
		//超容器
		template<xhqm::size size, class type_in, template<class> class save_type>
        struct docker_s { using type = save_type<typename docker_s<size - 1, type_in, save_type>::type>; };
		template<class type_in, template<class> class save_type>
        struct docker_s<1, type_in, save_type> { using type = save_type<type_in>; };

		template<class type_in, xhqm::size dim = 1, template<class> class save_type = std::vector>
		using docker = typename docker_s<dim, type_in, save_type>::type;
		template<class type_in, xhqm::size dim, template<class> class save_type = std::vector>
		using docker_few = typename docker_s<dim - 1, type_in, save_type>::type;
		template<class type_in, xhqm::size dim, template<class> class save_type = std::vector>
		using docker_more = typename docker_s<dim + 1, type_in, save_type>::type;

		
		////数组
		//template<class type_in, xhqm::size size> using array = std::array<type_in, size>;
		//超数组
		template<class type_in, xhqm::size dim, xhqm::size ...dims>
		struct hyper_s { using type = std::array<typename hyper_s<type_in, dims...>::type, dim>; };
		template<class type_in, xhqm::size dim>
		struct hyper_s<type_in, dim> { using type = std::array<type_in, dim>; };

		// Windows SDK <rpcndr.h> 把 hyper 定义为 MIDL 基类型宏（#define hyper __int64），
		// 会在下面这个别名声明处展开成 "using __int64 = ..." 而报 C2062；
		// 仅在声明期间摘掉该宏，声明后立即恢复，不影响 SDK 自身对 hyper 的使用
		//（同类处理见 algebra/geometry/geometry.h 的 #undef hyper）
#pragma push_macro("hyper")
#undef hyper
		template<class type_in, xhqm::size ...dims>
		using hyper = typename hyper_s<type_in, dims...>::type;
#pragma pop_macro("hyper")
		template<class type_in, xhqm::size dim, xhqm::size ...dims>
		using hyper_few = typename hyper_s<type_in, dims...>::type;
		template<class type_in, xhqm::size dim, xhqm::size ...dims>
		using hyper_more = typename hyper_s<type_in, 1, dim, dims...>::type;
	}

	//	//template<xhqm::size size> 
	//	//class writing {
	//	//public:
	//	//	using writ = xhqm::tuple<size, xhqm::byte>;
	//	//protected:
	//	//	writ m_writing;
	//	//public:
	//	//	template<class ...signs>
	//	//	writing(signs...sign) : m_writing(std::forward<signs>(sign)...) { };
	//	//};

	//template<class ...argvs> void expand(argvs&&...argv) { }

	//template<class type, class ...argvs>
	//std::vector<type> expand_vector(argvs&&... point)
	//{
	//	return { std::forward<type>(point)... };
	//}

	//template<class type, class ...argvs>
	//std::array<type, sizeof...(argvs)> expand_array(argvs&&... point)
	//{
	//	return { std::forward<type>(point)... };
	//}

}
#endif
