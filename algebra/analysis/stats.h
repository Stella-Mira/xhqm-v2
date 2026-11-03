#ifndef XHQM_ANALYSIS_STATS
#define XHQM_ANALYSIS_STATS

// 归约 / 统计（MATLAB 风格）：sum / prod / mean / max / min / std / var / cumsum / cumprod / diff。
#include "axis.h"
#include "../tensor/matrix.h"
#include "../tensor/vector.h"
#include "../tensor/linalg.h"   // tensor_base::reduce_last（axis::cols 大矩阵走 kernel）
#include "../tensor/stats.h"    // tensor_base::var/stddev/cov
#include <algorithm>
#include <limits>
#include <map>

namespace xhqm
{
	namespace analysis
	{
		namespace detail
		{
			// matrix/vector → tensor：零拷贝 values()（axis::cols 大矩阵走 reduce kernel 用）
			template<class type>
			const xhqm::tensor<2, type>& as_tensor(const matrix<type>& m) { return m.values(); }
			template<class type>
			const xhqm::tensor<1, type>& as_tensor1(const vector<type>& v) { return v.values(); }
			// tensor → matrix/vector：显式包装构造（内部 memcpy）
			template<class type>
			matrix<type> as_matrix(const xhqm::tensor<2, type>& t) { return matrix<type>(t); }
			template<class type>
			vector<type> as_vector(const xhqm::tensor<1, type>& t) { return vector<type>(t); }
			// tensor<1,index> → std::vector<index>
			inline std::vector<xhqm::index> as_index_vec(const xhqm::tensor<1, xhqm::index>& t)
			{
				std::vector<xhqm::index> r(t.dimension(0));
				for (xhqm::size i = 0; i < t.dimension(0); ++i) r[i] = t[i];
				return r;
			}
			// matrix → tensor<1>（行优先线性化，无 values() 对应，保留拷贝）
			template<class type>
			xhqm::tensor<1, type> as_t1_flat(const matrix<type>& m)
			{
				xhqm::tensor<1, type> t(std::array<xhqm::size, 1>{ m.size() }, type(0));
				for (xhqm::size i = 0; i < m.size(); ++i) t[i] = m.data()[i];
				return t;
			}
		}

		// —— 全元素求和（走 sum_all 桩）——
		template<class type>
		type sum(const matrix<type>& m)
		{
			return xhqm::tensor_base::sum_all(detail::as_tensor(m));
		}
		template<class type>
		type sum(const vector<type>& v)
		{
			return xhqm::tensor_base::sum_all(detail::as_tensor1(v));
		}
		// 沿维求和：axis::rows 每列求和，axis::cols 每行求和（走 reduce_sum 桩）
		template<class type>
		vector<type> sum(const matrix<type>& m, axis a)
		{
			return detail::as_vector(xhqm::tensor_base::reduce_sum(detail::as_tensor(m), (xhqm::size)a));
		}

		// —— 全元素求积（走 prod_all 桩）——
		template<class type>
		type prod(const matrix<type>& m)
		{
			return xhqm::tensor_base::prod_all(detail::as_tensor(m));
		}
		template<class type>
		type prod(const vector<type>& v)
		{
			return xhqm::tensor_base::prod_all(detail::as_tensor1(v));
		}

		// —— 均值 ——
		template<class type>
		type mean(const matrix<type>& m)
		{
			return (m.size() == 0) ? type(0) : sum(m) / type(m.size());
		}
		template<class type>
		type mean(const vector<type>& v)
		{
			return (v.size() == 0) ? type(0) : sum(v) / type(v.size());
		}

		// —— 全元素最大 / 最小（走 max_all / min_all 桩）——
		template<class type>
		type max(const matrix<type>& m)
		{
			return xhqm::tensor_base::max_all(detail::as_tensor(m));
		}
		template<class type>
		type min(const matrix<type>& m)
		{
			return xhqm::tensor_base::min_all(detail::as_tensor(m));
		}
		template<class type>
		type max(const vector<type>& v)
		{
			return xhqm::tensor_base::max_all(detail::as_tensor1(v));
		}
		template<class type>
		type min(const vector<type>& v)
		{
			return xhqm::tensor_base::min_all(detail::as_tensor1(v));
		}

		// —— 方差 / 标准差（样本方差，N-1 归一化，走 var/stddev 桩）——
		template<class type>
		type var(const matrix<type>& m)
		{
			return xhqm::tensor_base::var(detail::as_tensor(m), true);
		}
		template<class type>
		type var(const vector<type>& v)
		{
			return xhqm::tensor_base::var(detail::as_tensor1(v), true);
		}
		// 标准差（MATLAB 名 std；因与 ::std 命名空间冲突，C++ 接口命名为 stddev）
		template<class type>
		type stddev(const matrix<type>& m) { return xhqm::tensor_base::stddev(detail::as_tensor(m), true); }
		template<class type>
		type stddev(const vector<type>& v) { return xhqm::tensor_base::stddev(detail::as_tensor1(v), true); }

		// —— 累积和（cumsum，1-D 前缀走 cumsum_last 桩） ——
		template<class type>
		vector<type> cumsum(const vector<type>& v)
		{
			if (v.size() == 0) return vector<type>();
			return detail::as_vector(xhqm::tensor_base::cumsum_last(detail::as_tensor1(v)));
		}
		template<class type>
		vector<type> cumsum(const matrix<type>& m)
		{
			return detail::as_vector(xhqm::tensor_base::cumsum_last(detail::as_t1_flat(m)));
		}

		// —— 累积积（cumprod，1-D 前缀走 cumprod_last 桩） ——
		template<class type>
		vector<type> cumprod(const vector<type>& v)
		{
			if (v.size() == 0) return vector<type>();
			return detail::as_vector(xhqm::tensor_base::cumprod_last(detail::as_tensor1(v)));
		}
		template<class type>
		vector<type> cumprod(const matrix<type>& m)
		{
			return detail::as_vector(xhqm::tensor_base::cumprod_last(detail::as_t1_flat(m)));
		}

		// —— 差分 diff(v) = v[1:end] - v[0:end-1]（走 diff 桩）——
		template<class type>
		vector<type> diff(const vector<type>& v)
		{
			return detail::as_vector(xhqm::tensor_base::diff(detail::as_tensor1(v)));
		}

		// —— 沿维累积和 / 累积积（走 cumsum / cumprod 桩）——
		template<class type>
		matrix<type> cumsum(const matrix<type>& m, axis a)
		{
			return detail::as_matrix(xhqm::tensor_base::cumsum(detail::as_tensor(m), (xhqm::size)a));
		}
		template<class type>
		matrix<type> cumprod(const matrix<type>& m, axis a)
		{
			return detail::as_matrix(xhqm::tensor_base::cumprod(detail::as_tensor(m), (xhqm::size)a));
		}
		// —— 沿维差分：axis::rows 沿行，axis::cols 沿列（走 diff 桩）——
		template<class type>
		matrix<type> diff(const matrix<type>& m, axis a)
		{
			return detail::as_matrix(xhqm::tensor_base::diff(detail::as_tensor(m), (xhqm::size)a));
		}

		// —— 极差 range = max - min ——
		template<class type>
		type range(const matrix<type>& m) { return max(m) - min(m); }
		template<class type>
		type range(const vector<type>& v) { return max(v) - min(v); }

		// —— 中位数 / 众数（走 median / mode 桩）——
		template<class type>
		type median(const vector<type>& v)
		{
			return xhqm::tensor_base::median(detail::as_tensor1(v));
		}
		template<class type>
		type median(const matrix<type>& m)
		{
			return xhqm::tensor_base::median(detail::as_tensor(m));
		}
		template<class type>
		type mode(const vector<type>& v)
		{
			return xhqm::tensor_base::mode(detail::as_tensor1(v));
		}
		template<class type>
		type mode(const matrix<type>& m)
		{
			return xhqm::tensor_base::mode(detail::as_tensor(m));
		}

		// —— 累积最大 / 最小（1-D 前缀走 cummax_last / cummin_last 桩）——
		template<class type>
		vector<type> cummax(const vector<type>& v)
		{
			if (v.size() == 0) return vector<type>();
			return detail::as_vector(xhqm::tensor_base::cummax_last(detail::as_tensor1(v)));
		}
		template<class type>
		vector<type> cummin(const vector<type>& v)
		{
			if (v.size() == 0) return vector<type>();
			return detail::as_vector(xhqm::tensor_base::cummin_last(detail::as_tensor1(v)));
		}

		// —— 数值梯度（走 gradient 桩）——
		template<class type>
		vector<type> gradient(const vector<type>& v)
		{
			return detail::as_vector(xhqm::tensor_base::gradient(detail::as_tensor1(v)));
		}

		// —— 协方差矩阵（m：n 观测 × p 变量，返回 p×p，N-1 归一化，走 cov 桩）——
		template<class type>
		matrix<type> cov(const matrix<type>& m)
		{
			return detail::as_matrix(xhqm::tensor_base::cov(detail::as_tensor(m)));
		}

		// —— 相关系数矩阵 ——
		template<class type>
		matrix<type> corrcoef(const matrix<type>& m)
		{
			matrix<type> c = cov(m);
			const xhqm::size p = c.rows();
			for (xhqm::size i = 0; i < p; ++i)
				for (xhqm::size j = 0; j < p; ++j)
				{
					const type denom = xhqm::sqrt(c(i, i) * c(j, j));
					c(i, j) = (denom > type(0)) ? c(i, j) / denom : type(0);
				}
			return c;
		}

		// —— 排序（升序，走 sort 桩） ——
		template<class type>
		vector<type> sort(const vector<type>& v)
		{
			return detail::as_vector(xhqm::tensor_base::sort(detail::as_tensor1(v)));
		}

		// —— 去重（排序后去重，升序，走 unique 桩）——
		template<class type>
		vector<type> unique(const vector<type>& v)
		{
			return detail::as_vector(xhqm::tensor_base::unique(detail::as_tensor1(v)));
		}

		// —— 沿维求积：axis::rows 每列求积，axis::cols 每行求积（走 reduce_prod 桩）——
		template<class type>
		vector<type> prod(const matrix<type>& m, axis a)
		{
			return detail::as_vector(xhqm::tensor_base::reduce_prod(detail::as_tensor(m), (xhqm::size)a));
		}

		// —— 沿维均值：axis::rows 每列均值，axis::cols 每行均值 ——
		template<class type>
		vector<type> mean(const matrix<type>& m, axis a)
		{
			vector<type> r = sum(m, a);
			const type n = type((a == axis::rows) ? m.rows() : m.cols());
			for (xhqm::size i = 0; i < r.size(); ++i) r[i] = xhqm::div(r[i], n);
			return r;
		}

		// —— 沿维最大 / 最小：axis::rows 每列，axis::cols 每行（走 reduce_max / reduce_min 桩）——
		template<class type>
		vector<type> max(const matrix<type>& m, axis a)
		{
			if (m.size() == 0) return vector<type>();
			return detail::as_vector(xhqm::tensor_base::reduce_max(detail::as_tensor(m), (xhqm::size)a));
		}
		template<class type>
		vector<type> min(const matrix<type>& m, axis a)
		{
			if (m.size() == 0) return vector<type>();
			return detail::as_vector(xhqm::tensor_base::reduce_min(detail::as_tensor(m), (xhqm::size)a));
		}

		// —— 沿维最大/最小值的 [值, 索引]（对标 MATLAB [M,I] = max(A,[],dim)）——
		template<class type>
		struct minmax_result { vector<type> value; std::vector<xhqm::index> index; };

		template<class type>
		minmax_result<type> max_index(const matrix<type>& m, axis a)
		{
			minmax_result<type> r;
			if (m.size() == 0) return r;
			const auto& t = detail::as_tensor(m);
			auto idx = xhqm::tensor_base::argmax(t, (xhqm::size)a);
			auto val = xhqm::tensor_base::reduce_max(t, (xhqm::size)a);
			r.index = detail::as_index_vec(idx);
			r.value = detail::as_vector(val);
			return r;
		}
		template<class type>
		minmax_result<type> min_index(const matrix<type>& m, axis a)
		{
			minmax_result<type> r;
			if (m.size() == 0) return r;
			const auto& t = detail::as_tensor(m);
			auto idx = xhqm::tensor_base::argmin(t, (xhqm::size)a);
			auto val = xhqm::tensor_base::reduce_min(t, (xhqm::size)a);
			r.index = detail::as_index_vec(idx);
			r.value = detail::as_vector(val);
			return r;
		}

		// —— 最大 / 最小值的线性索引（行优先，走 argmax_all / argmin_all 桩）——
		template<class type>
		xhqm::size argmax(const matrix<type>& m)
		{
			return (xhqm::size)xhqm::tensor_base::argmax_all(detail::as_tensor(m));
		}
		template<class type>
		xhqm::size argmax(const vector<type>& v)
		{
			return (xhqm::size)xhqm::tensor_base::argmax_all(detail::as_tensor1(v));
		}
		template<class type>
		xhqm::size argmin(const matrix<type>& m)
		{
			return (xhqm::size)xhqm::tensor_base::argmin_all(detail::as_tensor(m));
		}
		template<class type>
		xhqm::size argmin(const vector<type>& v)
		{
			return (xhqm::size)xhqm::tensor_base::argmin_all(detail::as_tensor1(v));
		}

		// —— 方差 / 标准差（variance::sample = N-1 无偏，variance::population = N 总体，走桩）——
		template<class type>
		type var(const vector<type>& v, variance w)
		{
			return xhqm::tensor_base::var(detail::as_tensor1(v), w == variance::sample);
		}
		template<class type>
		type var(const matrix<type>& m, variance w)
		{
			return xhqm::tensor_base::var(detail::as_tensor(m), w == variance::sample);
		}
		template<class type>
		type stddev(const vector<type>& v, variance w) { return xhqm::tensor_base::stddev(detail::as_tensor1(v), w == variance::sample); }
		template<class type>
		type stddev(const matrix<type>& m, variance w) { return xhqm::tensor_base::stddev(detail::as_tensor(m), w == variance::sample); }

		// —— 沿维方差 / 标准差：axis::rows 每列，axis::cols 每行（走桩）——
		template<class type>
		vector<type> var(const matrix<type>& m, axis a, variance w = variance::sample)
		{
			return detail::as_vector(xhqm::tensor_base::var(detail::as_tensor(m), (xhqm::size)a, w == variance::sample));
		}
		template<class type>
		vector<type> stddev(const matrix<type>& m, axis a, variance w = variance::sample)
		{
			return detail::as_vector(xhqm::tensor_base::stddev(detail::as_tensor(m), (xhqm::size)a, w == variance::sample));
		}

		// —— 排序（sort_order::ascend / descend，走 sort 桩）——
		template<class type>
		vector<type> sort(const vector<type>& v, sort_order order)
		{
			return detail::as_vector(xhqm::tensor_base::sort(detail::as_tensor1(v), order == sort_order::descend));
		}

		// —— 排序索引（sort_order::ascend 默认，走 sort_index 桩）——
		template<class type>
		std::vector<xhqm::index> sort_index(const vector<type>& v, sort_order order = sort_order::ascend)
		{
			return detail::as_index_vec(xhqm::tensor_base::sort_index(detail::as_tensor1(v), order == sort_order::descend));
		}

		// —— 逻辑归约：all（全真）/ any（存在真，走 all/any 桩）——
		inline bool all(const matrix<bool>& m)
		{
			return xhqm::tensor_base::all(detail::as_tensor(m));
		}
		inline bool any(const matrix<bool>& m)
		{
			return xhqm::tensor_base::any(detail::as_tensor(m));
		}
		inline bool all(const vector<bool>& v)
		{
			return xhqm::tensor_base::all(detail::as_tensor1(v));
		}
		inline bool any(const vector<bool>& v)
		{
			return xhqm::tensor_base::any(detail::as_tensor1(v));
		}

		// —— NaN 鲁棒统计（忽略 NaN，走桩）——
		template<class type>
		type nansum(const vector<type>& v) { return xhqm::tensor_base::nansum(detail::as_tensor1(v)); }
		template<class type>
		type nanmean(const vector<type>& v) { return xhqm::tensor_base::nanmean(detail::as_tensor1(v)); }
		template<class type>
		type nanmax(const vector<type>& v) { return xhqm::tensor_base::nanmax(detail::as_tensor1(v)); }
		template<class type>
		type nanmin(const vector<type>& v) { return xhqm::tensor_base::nanmin(detail::as_tensor1(v)); }

		// —— 百分位数（p ∈ [0,100]，线性插值，走桩）——
		template<class type>
		type prctile(const vector<type>& v, const type& p) { return xhqm::tensor_base::prctile(detail::as_tensor1(v), p); }
		template<class type>
		type quantile(const vector<type>& v, const type& q) { return xhqm::tensor_base::quantile(detail::as_tensor1(v), q); }
		template<class type>
		type iqr(const vector<type>& v) { return xhqm::tensor_base::iqr(detail::as_tensor1(v)); }

		// —— 集合运算（走桩）——
		template<class type>
		bool ismember(const type& val, const vector<type>& v) { return xhqm::tensor_base::ismember(val, detail::as_tensor1(v)); }
		template<class type>
		vector<type> intersect(const vector<type>& a, const vector<type>& b)
		{
			return detail::as_vector(xhqm::tensor_base::intersect(detail::as_tensor1(a), detail::as_tensor1(b)));
		}
		template<class type>
		vector<type> setunion(const vector<type>& a, const vector<type>& b)
		{
			return detail::as_vector(xhqm::tensor_base::setunion(detail::as_tensor1(a), detail::as_tensor1(b)));
		}
		template<class type>
		vector<type> setdiff(const vector<type>& a, const vector<type>& b)
		{
			return detail::as_vector(xhqm::tensor_base::setdiff(detail::as_tensor1(a), detail::as_tensor1(b)));
		}

		// —— 按行字典序排序（走 sortrows 桩）——
		template<class type>
		matrix<type> sortrows(const matrix<type>& m)
		{
			return detail::as_matrix(xhqm::tensor_base::sortrows(detail::as_tensor(m)));
		}
	}
}

#endif
