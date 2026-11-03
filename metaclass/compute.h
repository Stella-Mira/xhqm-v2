#ifndef XHQM_COMPUTE
#define XHQM_COMPUTE
#include "../head.h"
#include "template.h"

namespace xhqm {

	inline namespace templates {

		//计算
		template<class type>
		struct compute {
			//计数
			template<type ...vals> static constexpr xhqm::size size_count = sizeof...(vals);

			//bool 值选择
			template<bool select, type val_1, type val_2> struct select_if { static constexpr type value = val_1; };
			template<type val_1, type val_2> struct select_if<false, val_1, val_2> { static constexpr type value = val_2; };

			//下标 值选择
			template<xhqm::size select, type val, type ...vals>
			struct select_index { static constexpr type value = select_index<select - 1, vals...>::value; };
			template<type val, type ...vals>
			struct select_index<0, val, vals...> { static constexpr type value = val; };

			//求和
			template<type val, type ...vals>
			struct sum { static constexpr type value = val + sum<vals...>::value; };
			template<type val>
			struct sum<val> { static constexpr type value = val; };
			template<type ...vals>
			static constexpr type sum_value = sum<vals...>::value;
			//求差
			template<bool select, type val, type ...vals>
			struct sub { static constexpr type value = val - compute::select_if<select, sum<vals...>::value, sub<select, vals...>::value>::value; };
			template<bool select, type val>
			struct sub<select, val> { static constexpr type value = val; };
			template<bool select, type ...vals>
			static constexpr type sub_value = sub<select, vals...>::value;
			//求积
			template<type val, type ...vals>
			struct dot { static constexpr type value = val * dot<vals...>::value; };
			template<type val>
			struct dot<val> { static constexpr type value = val; };
			template<type ...vals>
			static constexpr type dot_value = dot<vals...>::value;
			//求商
			template<bool select, type val, type ...vals>
			struct div { static constexpr type value = val / compute::select_if<select, dot<vals...>::value, div<select, vals...>::value>::value; };
			template<bool select, type val>
			struct div<select, val> { static constexpr type value = val; };
			template<bool select, type ...vals>
			static constexpr type div_value = div<select, vals...>::value;
		};

	}
}
#endif