#ifndef XHQM_SYMBOLS_HAS
#define XHQM_SYMBOLS_HAS

#include "class_type.h"

#define xhqm_symbol_return(symbol) namespace xhqm { template <typename type, typename ...argv_type>	\
		struct symbol##_in { using return_type = decltype(std::declval<type>().symbol(std::declval<argv_type>()...)); }; }

#define xhqm_symbols_has(symbol) namespace xhqm {				\
		template<typename type, typename is_void = void> struct symbol##_in_class { static constexpr bool yes = false; };		\
		template<typename type> struct symbol##_in_class<type, std::void_t<decltype(&type::symbol)>> { static constexpr bool yes = true; };		}


namespace xhqm {

	inline namespace templates {

		template<typename type>
		class symbols_has : public xhqm::is_class<type> {
		protected:
			template <typename typein, typename void_type = void> static constexpr bool _has_sum = false;
			template <typename typein> static constexpr bool _has_sum<typein, std::void_t<decltype(std::declval<typein>().operator+(std::declval<typein>()))>> = true;
			template <typename typein, typename void_type = void> static constexpr bool _has_sub = false;
			template <typename typein> static constexpr bool _has_sub<typein, std::void_t<decltype(std::declval<typein>().operator-(std::declval<typein>()))>> = true;
			template <typename typein, typename void_type = void> static constexpr bool _has_dot = false;
			template <typename typein> static constexpr bool _has_dot<typein, std::void_t<decltype(std::declval<typein>().operator*(std::declval<typein>()))>> = true;
			template <typename typein, typename void_type = void> static constexpr bool _has_div = false;
			template <typename typein> static constexpr bool _has_div<typein, std::void_t<decltype(std::declval<typein>().operator/(std::declval<typein>()))>> = true;

			template <typename typein, typename void_type = void> static constexpr bool _has_equal = false;
			template <typename typein> static constexpr bool _has_equal<typein, std::void_t<decltype(std::declval<typein>().operator==(std::declval<typein>(), std::declval<typein>()))>> = true;
			template <typename typein, typename void_type = void> static constexpr bool _has_less_than = false;
			template <typename typein> static constexpr bool _has_less_than<typein, std::void_t<decltype(std::declval<typein>().operator< (std::declval<typein>(), std::declval<typein>()))>> = true;
			template <typename typein, typename void_type = void> static constexpr bool _has_greater_than = false;
			template <typename typein> static constexpr bool _has_greater_than<typein, std::void_t<decltype(std::declval<typein>().operator> (std::declval<typein>(), std::declval<typein>()))>> = true;
			template <typename typein, typename void_type = void> static constexpr bool _has_less_equal = false;
			template <typename typein> static constexpr bool _has_less_equal<typein, std::void_t<decltype(std::declval<typein>().operator<=(std::declval<typein>(), std::declval<typein>()))>> = true;
			template <typename typein, typename void_type = void> static constexpr bool _has_greater_equal = false;
			template <typename typein> static constexpr bool _has_greater_equal<typein, std::void_t<decltype(std::declval<typein>().operator>=(std::declval<typein>(), std::declval<typein>()))>> = true;

		public:
			static constexpr bool has_sum = xhqm::is_class<type>::isvalue || _has_sum<type>;
			static constexpr bool has_sub = xhqm::is_class<type>::isvalue || _has_sub<type>;
			static constexpr bool has_dot = xhqm::is_class<type>::isvalue || _has_dot<type>;
			static constexpr bool has_div = xhqm::is_class<type>::isvalue || _has_div<type>;

			static constexpr bool has_equal = xhqm::is_class<type>::isvalue || _has_equal<type>;
			static constexpr bool has_less_than = xhqm::is_class<type>::isvalue || _has_less_than<type>;
			static constexpr bool has_greater_than = xhqm::is_class<type>::isvalue || _has_greater_than<type>;
			static constexpr bool has_less_equal = xhqm::is_class<type>::isvalue || _has_less_equal<type>;
			static constexpr bool has_greater_equal = xhqm::is_class<type>::isvalue || _has_greater_equal<type>;
		};
	}
}

#endif