#ifndef XHQM_TENSOR_STATS
#define XHQM_TENSOR_STATS

// 统计算子（串行桩）：var（方差）/ stddev（标准差）/ cov（协方差矩阵）。
#include "tensor_base.h"
#include "linalg.h"
#include <array>
#include <vector>
#include <algorithm>
#include <cmath>

namespace xhqm {
	namespace tensor_base {

		// 全元素方差（sample=true 时 N-1 无偏，否则 N 总体）
		template<class type, xhqm::size dim>
		type var(const xhqm::tensor<dim, type>& t, bool sample = true)
		{
			const xhqm::size n = t.size();
			if (n == 0 || (sample && n < 2)) return type(0);
			const type mu = xhqm::div(sum_all(t), type(n));
			type s = type(0);
			for (xhqm::size i = 0; i < n; ++i) { type d = xhqm::sub(t.value(i), mu); s = xhqm::sum(s, xhqm::mul(d, d)); }
			return xhqm::div(s, sample ? type(n - 1) : type(n));
		}

		// 全元素标准差
		template<class type, xhqm::size dim>
		type stddev(const xhqm::tensor<dim, type>& t, bool sample = true)
		{
			return xhqm::sqrt(var(t, sample));
		}

		// 沿维方差：axis 归约，每片按 sample 归一化
		template<class type, xhqm::size dim>
		xhqm::tensor<dim - 1, type> var(const xhqm::tensor<dim, type>& t, xhqm::size axis, bool sample = true)
		{
			std::array<xhqm::size, dim - 1> out_dims{};
			xhqm::size oi = 0;
			for (xhqm::size k = 0; k < dim; ++k)
				if (k != axis) out_dims[oi++] = t.dimension(k);
			xhqm::tensor<dim - 1, type> out(out_dims, type(0));
			const xhqm::size n = t.dimension(axis);
			if (n == 0 || (sample && n < 2)) return out;
			auto mu = reduce_sum(t, axis);
			for (xhqm::size i = 0; i < mu.size(); ++i) mu.value(i) = xhqm::div(mu.value(i), type(n));
			for (xhqm::size flat = 0; flat < out.size(); ++flat)
			{
				auto out_coord = out.index(flat);
				type s = type(0);
				std::array<xhqm::size, dim> coord{};
				for (xhqm::size a = 0; a < n; ++a)
				{
					xhqm::size oi2 = 0;
					for (xhqm::size k = 0; k < dim; ++k)
						coord[k] = (k == axis) ? a : out_coord[oi2++];
					type d = xhqm::sub(t.value(t.index(coord)), mu.value(flat));
					s = xhqm::sum(s, xhqm::mul(d, d));
				}
				out.value(flat) = xhqm::div(s, sample ? type(n - 1) : type(n));
			}
			return out;
		}

		// 沿维标准差
		template<class type, xhqm::size dim>
		xhqm::tensor<dim - 1, type> stddev(const xhqm::tensor<dim, type>& t, xhqm::size axis, bool sample = true)
		{
			auto v = var(t, axis, sample);
			for (xhqm::size i = 0; i < v.size(); ++i) v.value(i) = xhqm::sqrt(v.value(i));
			return v;
		}

		// 协方差矩阵：t 为 n 观测 × p 变量，返回 p×p（N-1 归一化）
		template<class type>
		xhqm::tensor<2, type> cov(const xhqm::tensor<2, type>& t)
		{
			const xhqm::size n = t.dimension(0), p = t.dimension(1);
			xhqm::tensor<2, type> r(std::array<xhqm::size, 2>{ p, p }, type(0));
			if (n < 2) return r;
			auto mu = reduce_sum(t, 0);
			for (xhqm::size j = 0; j < p; ++j) mu.value(j) = xhqm::div(mu.value(j), type(n));
			for (xhqm::size a = 0; a < p; ++a)
				for (xhqm::size b = 0; b < p; ++b)
				{
					type s = type(0);
					for (xhqm::size i = 0; i < n; ++i)
						s = xhqm::sum(s, xhqm::mul(xhqm::sub(t(i, a), mu.value(a)), xhqm::sub(t(i, b), mu.value(b))));
					r(a, b) = xhqm::div(s, type(n - 1));
				}
			return r;
		}

		// —— 差分（串行桩）：相邻差，1-D 与 2-D 沿维 ——
		template<class type>
		xhqm::tensor<1, type> diff(const xhqm::tensor<1, type>& t)
		{
			const xhqm::size n = t.dimension(0);
			if (n < 2) return xhqm::tensor<1, type>();
			xhqm::tensor<1, type> r(std::array<xhqm::size, 1>{ n - 1 }, type(0));
			for (xhqm::size i = 0; i + 1 < n; ++i) r[i] = xhqm::sub(t[i + 1], t[i]);
			return r;
		}
		template<class type>
		xhqm::tensor<2, type> diff(const xhqm::tensor<2, type>& t, xhqm::size axis)
		{
			const xhqm::size m = t.dimension(0), n = t.dimension(1);
			if (axis == 0)
			{
				if (m < 2) return xhqm::tensor<2, type>();
				xhqm::tensor<2, type> r(std::array<xhqm::size, 2>{ m - 1, n }, type(0));
				for (xhqm::size j = 0; j < n; ++j)
					for (xhqm::size i = 0; i + 1 < m; ++i)
						r(i, j) = xhqm::sub(t(i + 1, j), t(i, j));
				return r;
			}
			if (n < 2) return xhqm::tensor<2, type>();
			xhqm::tensor<2, type> r(std::array<xhqm::size, 2>{ m, n - 1 }, type(0));
			for (xhqm::size i = 0; i < m; ++i)
				for (xhqm::size j = 0; j + 1 < n; ++j)
					r(i, j) = xhqm::sub(t(i, j + 1), t(i, j));
			return r;
		}

		// —— 数值梯度（端点单侧差分，内部中心差分）——
		template<class type>
		xhqm::tensor<1, type> gradient(const xhqm::tensor<1, type>& t)
		{
			const xhqm::size n = t.dimension(0);
			if (n < 2) return xhqm::tensor<1, type>();
			xhqm::tensor<1, type> r(std::array<xhqm::size, 1>{ n }, type(0));
			r[0] = xhqm::sub(t[1], t[0]);
			for (xhqm::size i = 1; i + 1 < n; ++i)
				r[i] = xhqm::div(xhqm::sub(t[i + 1], t[i - 1]), type(2));
			r[n - 1] = xhqm::sub(t[n - 1], t[n - 2]);
			return r;
		}

		// —— 中位数 / 众数 / 百分位（串行桩，全元素）——
		template<class type, xhqm::size dim>
		type median(const xhqm::tensor<dim, type>& t)
		{
			const xhqm::size n = t.size();
			if (n == 0) return type(0);
			std::vector<type> s(n);
			for (xhqm::size i = 0; i < n; ++i) s[i] = t.value(i);
			std::sort(s.begin(), s.end());
			if (n % 2 == 1) return s[n / 2];
			return xhqm::div(xhqm::sum(s[n / 2 - 1], s[n / 2]), type(2));
		}
		template<class type, xhqm::size dim>
		type mode(const xhqm::tensor<dim, type>& t)
		{
			const xhqm::size n = t.size();
			if (n == 0) return type(0);
			std::vector<type> s(n);
			for (xhqm::size i = 0; i < n; ++i) s[i] = t.value(i);
			std::sort(s.begin(), s.end());
			type best = s[0]; xhqm::size bestc = 0, i = 0;
			while (i < n)
			{
				xhqm::size j = i;
				while (j < n && s[j] == s[i]) ++j;
				if (j - i > bestc) { bestc = j - i; best = s[i]; }
				i = j;
			}
			return best;
		}
		template<class type, xhqm::size dim>
		type prctile(const xhqm::tensor<dim, type>& t, const type& p)
		{
			const xhqm::size n = t.size();
			if (n == 0) return type(0);
			std::vector<type> s(n);
			for (xhqm::size i = 0; i < n; ++i) s[i] = t.value(i);
			std::sort(s.begin(), s.end());
			if (n == 1) return s[0];
			type idx = xhqm::mul(xhqm::div(p, type(100)), type(n - 1));
			if (idx < type(0)) idx = type(0);		// 负浮点强转 size_t 是 UB，先钳位
			if (idx > type(n - 1)) idx = type(n - 1);
			const xhqm::size lo = (xhqm::size)idx;
			const type frac = xhqm::sub(idx, type(lo));
			if (lo + 1 >= n) return s[n - 1];
			return xhqm::sum(xhqm::mul(s[lo], xhqm::sub(type(1), frac)), xhqm::mul(s[lo + 1], frac));
		}
		template<class type, xhqm::size dim>
		type quantile(const xhqm::tensor<dim, type>& t, const type& q) { return prctile(t, xhqm::mul(q, type(100))); }
		template<class type, xhqm::size dim>
		type iqr(const xhqm::tensor<dim, type>& t) { return xhqm::sub(prctile(t, type(75)), prctile(t, type(25))); }

		// —— NaN 鲁棒归约（忽略 NaN）——
		template<class type, xhqm::size dim>
		type nansum(const xhqm::tensor<dim, type>& t)
		{
			type r = type(0);
			for (xhqm::size i = 0; i < t.size(); ++i) if (!std::isnan(t.value(i))) r = xhqm::sum(r, t.value(i));
			return r;
		}
		template<class type, xhqm::size dim>
		type nanmean(const xhqm::tensor<dim, type>& t)
		{
			type r = type(0); xhqm::size c = 0;
			for (xhqm::size i = 0; i < t.size(); ++i) if (!std::isnan(t.value(i))) { r = xhqm::sum(r, t.value(i)); ++c; }
			return (c == 0) ? type(0) : xhqm::div(r, type(c));
		}
		template<class type, xhqm::size dim>
		type nanmax(const xhqm::tensor<dim, type>& t)
		{
			type r = type(0); bool first = true;
			for (xhqm::size i = 0; i < t.size(); ++i)
				if (!std::isnan(t.value(i))) { if (first || t.value(i) > r) { r = t.value(i); first = false; } }
			return r;
		}
		template<class type, xhqm::size dim>
		type nanmin(const xhqm::tensor<dim, type>& t)
		{
			type r = type(0); bool first = true;
			for (xhqm::size i = 0; i < t.size(); ++i)
				if (!std::isnan(t.value(i))) { if (first || t.value(i) < r) { r = t.value(i); first = false; } }
			return r;
		}

		// —— 去重（排序后去重，升序）——
		template<class type>
		xhqm::tensor<1, type> unique(const xhqm::tensor<1, type>& t)
		{
			const xhqm::size n = t.dimension(0);
			std::vector<type> s(n);
			for (xhqm::size i = 0; i < n; ++i) s[i] = t[i];
			std::sort(s.begin(), s.end());
			std::vector<type> u;
			for (xhqm::size i = 0; i < n; ++i)
				if (u.empty() || u.back() != s[i]) u.push_back(s[i]);
			xhqm::tensor<1, type> r(std::array<xhqm::size, 1>{ u.size() }, type(0));
			for (xhqm::size i = 0; i < u.size(); ++i) r[i] = u[i];
			return r;
		}

		// —— 按行字典序排序 ——
		template<class type>
		xhqm::tensor<2, type> sortrows(const xhqm::tensor<2, type>& t)
		{
			const xhqm::size m = t.dimension(0), n = t.dimension(1);
			std::vector<std::vector<type>> rows(m);
			for (xhqm::size i = 0; i < m; ++i)
			{
				rows[i].resize(n);
				for (xhqm::size j = 0; j < n; ++j) rows[i][j] = t(i, j);
			}
			std::sort(rows.begin(), rows.end());
			xhqm::tensor<2, type> r(std::array<xhqm::size, 2>{ m, n }, type(0));
			for (xhqm::size i = 0; i < m; ++i)
				for (xhqm::size j = 0; j < n; ++j) r(i, j) = rows[i][j];
			return r;
		}

		// —— 集合运算（串行桩）——
		template<class type, xhqm::size dim>
		bool ismember(const type& val, const xhqm::tensor<dim, type>& t)
		{
			for (xhqm::size i = 0; i < t.size(); ++i) if (t.value(i) == val) return true;
			return false;
		}
		template<class type>
		xhqm::tensor<1, type> intersect(const xhqm::tensor<1, type>& a, const xhqm::tensor<1, type>& b)
		{
			const xhqm::size na = a.dimension(0), nb = b.dimension(0);
			std::vector<type> sa(na), sb(nb);
			for (xhqm::size i = 0; i < na; ++i) sa[i] = a[i];
			for (xhqm::size i = 0; i < nb; ++i) sb[i] = b[i];
			std::sort(sa.begin(), sa.end());
			std::sort(sb.begin(), sb.end());
			std::vector<type> out;
			xhqm::size i = 0, j = 0;
			while (i < na && j < nb)
			{
				if (sa[i] < sb[j]) ++i;
				else if (sb[j] < sa[i]) ++j;
				else { out.push_back(sa[i]); ++i; ++j; }
			}
			xhqm::tensor<1, type> r(std::array<xhqm::size, 1>{ out.size() }, type(0));
			for (xhqm::size k = 0; k < out.size(); ++k) r[k] = out[k];
			return r;
		}
		template<class type>
		xhqm::tensor<1, type> setunion(const xhqm::tensor<1, type>& a, const xhqm::tensor<1, type>& b)
		{
			const xhqm::size na = a.dimension(0), nb = b.dimension(0);
			std::vector<type> s(na + nb);
			for (xhqm::size i = 0; i < na; ++i) s[i] = a[i];
			for (xhqm::size i = 0; i < nb; ++i) s[na + i] = b[i];
			std::sort(s.begin(), s.end());
			std::vector<type> u;
			for (xhqm::size i = 0; i < s.size(); ++i)
				if (u.empty() || u.back() != s[i]) u.push_back(s[i]);
			xhqm::tensor<1, type> r(std::array<xhqm::size, 1>{ u.size() }, type(0));
			for (xhqm::size i = 0; i < u.size(); ++i) r[i] = u[i];
			return r;
		}
		template<class type>
		xhqm::tensor<1, type> setdiff(const xhqm::tensor<1, type>& a, const xhqm::tensor<1, type>& b)
		{
			const xhqm::size na = a.dimension(0), nb = b.dimension(0);
			std::vector<type> sa(na), sb(nb);
			for (xhqm::size i = 0; i < na; ++i) sa[i] = a[i];
			for (xhqm::size i = 0; i < nb; ++i) sb[i] = b[i];
			std::sort(sa.begin(), sa.end());
			std::sort(sb.begin(), sb.end());
			std::vector<type> out;
			xhqm::size j = 0;
			for (xhqm::size i = 0; i < na; ++i)
			{
				while (j < nb && sb[j] < sa[i]) ++j;
				if (j < nb && sb[j] == sa[i]) continue;
				out.push_back(sa[i]);
			}
			xhqm::tensor<1, type> r(std::array<xhqm::size, 1>{ out.size() }, type(0));
			for (xhqm::size k = 0; k < out.size(); ++k) r[k] = out[k];
			return r;
		}

	}
}

#endif
