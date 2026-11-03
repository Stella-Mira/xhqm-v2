#ifndef XHQM_ANALYSIS_CONVERT
#define XHQM_ANALYSIS_CONVERT

// vector / matrix ↔ tensor 转换（analysis 内部共享，避免各文件重复定义）。
#include "../tensor/vector.h"
#include "../tensor/matrix.h"
#include <array>

namespace xhqm
{
	namespace analysis
	{
		namespace detail
		{
			// vector/matrix → tensor：零拷贝，直接返回底层 values() 引用（complex<type> 亦适用）
			template<class type>
			const xhqm::tensor<1, type>& as_t1(const vector<type>& v) { return v.values(); }
			template<class type>
			const xhqm::tensor<2, type>& as_t2(const matrix<type>& m) { return m.values(); }
			// tensor → vector/matrix：走显式包装构造（内部 memcpy）
			template<class type>
			vector<type> as_vec(const xhqm::tensor<1, type>& t) { return vector<type>(t); }
			template<class type>
			matrix<type> as_mat(const xhqm::tensor<2, type>& t) { return matrix<type>(t); }
		}
	}
}

#endif
