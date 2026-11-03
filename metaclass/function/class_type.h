#ifndef XHQM_CLASS_TYPE
#define XHQM_CLASS_TYPE

#include <type_traits>

namespace xhqm {

	inline namespace templates{

		//是否为拟似函数
		template <typename typein, typename is_void = void>
		struct is_pseudo_s
		{
			static constexpr bool value = false;
		};
		template <typename typein>
		struct is_pseudo_s<typein, std::void_t<decltype(&typein::operator())>>
		{
			static constexpr bool value = true;
		};
		template <typename typein, typename is_void = void> constexpr bool is_pseudo = false;
		template <typename typein> constexpr bool is_pseudo<typein, std::void_t<decltype(&typein::operator())>>  = true;


		//是否为类类型
		template<typename type>
		class is_class {
		protected:
            template <typename U> 
			static xhqm::byte_1 _is_class(void* U::*) { return 1; };
            template <typename> static xhqm::byte_2 _is_class(...) { return 0; };
		public:
			static constexpr bool isvalue = sizeof(_is_class<type>(0)) == sizeof(xhqm::byte_2);
			static constexpr bool isclass = sizeof(_is_class<type>(0)) == sizeof(xhqm::byte_1);
			static constexpr bool ispseudo = xhqm::is_pseudo_s<type>::value;
		};

		template<typename _type>
		class is_pointer {
		protected:
			template <typename __type>
			struct is_pointer_s {
				static constexpr bool _is_pointer = false;
			};

			template <typename __type>
			struct is_pointer_s<__type*> {
				static constexpr bool _is_pointer = true;
			};
		public:
			static constexpr bool value = is_pointer_s<_type>::_is_pointer;
		};



		//类型变迁
		template<class type_in> struct class_type_s {
			using type = type_in;
			using type_ptr = type_in*;
			using type_ref = type_in&;
			using const_type = const type_in;
			using const_type_ptr = const type_in*;
			using const_type_ref = const type_in&;
			using const_type_ptr_ref = const type_in*&;
			using void_ptr = void*;
			using remove_type = void;
			using remove_ptr = type_in;
			using remove_ref = type_in;
			using remove_const = type_in;
			using remove_const_ptr = type_in;
			using remove_const_ref = type_in;
			using remove_const_ptr_ref = type_in;
		};
		template<> struct class_type_s<void> {
			using type = void;
			using type_ptr = void*;
			using type_ref = void*;
			using const_type = const void;
			using const_type_ptr = const void*;
			using const_type_ref = const void*;
			using const_type_ptr_ref = const void*;
			using void_ptr = void*;
			using remove_type = void;
			using remove_ptr = void;
			using remove_ref = void;
			using remove_const = void;
			using remove_const_ptr = void;
			using remove_const_ref = void;
			using remove_const_ptr_ref = void;
		};
		template<class type_in> struct class_type_s<type_in*> :public class_type_s<type_in> { using remove_ptr = type_in; };
		template<class type_in> struct class_type_s<type_in&> :public class_type_s<type_in> { using remove_ref = type_in; };
		template<class type_in> struct class_type_s<type_in*&> :public class_type_s<type_in> { using remove_ptr = type_in; using remove_ref = type_in; };
		template<class type_in> struct class_type_s<const type_in> :public class_type_s<type_in> { using remove_const = type_in; };
		template<class type_in> struct class_type_s<const type_in*> :public class_type_s<type_in> {
			using remove_ptr = const type_in;
			using remove_const = type_in*;
			using remove_const_ptr = type_in;
		};
		template<class type_in> struct class_type_s<const type_in&> :public class_type_s<type_in> {
			using remove_ref = const type_in;
			using remove_const = type_in&;
			using remove_const_ref = type_in;
		};
		template<class type_in> struct class_type_s<const type_in*&> :public class_type_s<type_in> {
			using remove_ptr = const type_in&;
			using remove_ref = const type_in*;
			using remove_const = type_in*&;
			using remove_const_ptr_ref = type_in;
		};

		template<class type_in> using class_type = typename class_type_s<type_in>::type;
		template<class type_in> using class_ptr = typename class_type_s<type_in>::type_ptr;
		template<class type_in> using class_ref = typename class_type_s<type_in>::type_ref;
		template<class type_in> using const_class = typename class_type_s<type_in>::const_type;
		template<class type_in> using const_class_ptr = typename class_type_s<type_in>::const_type_ptr;
		template<class type_in> using const_class_ref = typename class_type_s<type_in>::const_type_ref;
		template<class type_in> using const_class_ptr_ref = typename class_type_s<type_in>::const_type_ptr_ref;
		template<class type_in> using void_ptr = typename class_type_s<type_in>::void_ptr;
		template<class type_in> using remove_type = typename class_type_s<type_in>::remove_type;
		template<class type_in> using remove_ptr = typename class_type_s<type_in>::remove_ptr;
		template<class type_in> using remove_ref = typename class_type_s<type_in>::remove_ref;
		template<class type_in> using remove_const = typename class_type_s<type_in>::remove_const;
		template<class type_in> using remove_const_ptr = typename class_type_s<type_in>::remove_const_ptr;
		template<class type_in> using remove_const_ref = typename class_type_s<type_in>::remove_const_ref;
		template<class type_in> using remove_const_ptr_ref = typename class_type_s<type_in>::remove_const_ptr_ref;

	}
}
#endif
