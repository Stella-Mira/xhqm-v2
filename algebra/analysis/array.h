#ifndef XHQM_ANALYSIS_ARRAY
#define XHQM_ANALYSIS_ARRAY

// 数组操作（MATLAB 风格）：size / numel / reshape / horzcat / vertcat / cat / repmat / fliplr / flipud / tril / triu。
#include "axis.h"
#include "../tensor/matrix.h"
#include "../tensor/vector.h"
#include <array>
#include <utility>

namespace xhqm
{
	namespace analysis
	{
		// 尺寸（MATLAB size 返回维度，区别于 matrix::size 的元素总数）
		template<class type>
		std::array<xhqm::size, 2> size(const matrix<type>& m)
		{
			return { m.rows(), m.cols() };
		}
		template<class type>
		xhqm::size size(const vector<type>& v)
		{
			return v.size();
		}

		// 元素总数
		template<class type>
		xhqm::size numel(const matrix<type>& m)
		{
			return m.size();
		}
		template<class type>
		xhqm::size numel(const vector<type>& v)
		{
			return v.size();
		}

		// 改形状（等元素数），行优先重排
		template<class type>
		matrix<type> reshape(const matrix<type>& m, const xhqm::size& r, const xhqm::size& c)
		{
			if (m.rows() * m.cols() != r * c) return matrix<type>();
			matrix<type> out(r, c);
			for (xhqm::size i = 0; i < r * c; ++i)
				out.data()[i] = m.data()[i];
			return out;
		}

		// 水平拼接 [A B]（行数须一致）
		template<class type>
		matrix<type> horzcat(const matrix<type>& a, const matrix<type>& b)
		{
			if (a.rows() != b.rows()) return matrix<type>();
			matrix<type> r(a.rows(), a.cols() + b.cols());
			for (xhqm::size i = 0; i < a.rows(); ++i)
			{
				for (xhqm::size j = 0; j < a.cols(); ++j) r(i, j) = a(i, j);
				for (xhqm::size j = 0; j < b.cols(); ++j) r(i, a.cols() + j) = b(i, j);
			}
			return r;
		}

		// 垂直拼接 [A; B]（列数须一致）
		template<class type>
		matrix<type> vertcat(const matrix<type>& a, const matrix<type>& b)
		{
			if (a.cols() != b.cols()) return matrix<type>();
			matrix<type> r(a.rows() + b.rows(), a.cols());
			for (xhqm::size j = 0; j < a.cols(); ++j)
			{
				for (xhqm::size i = 0; i < a.rows(); ++i) r(i, j) = a(i, j);
				for (xhqm::size i = 0; i < b.rows(); ++i) r(a.rows() + i, j) = b(i, j);
			}
			return r;
		}

		// 沿维拼接：axis::rows 垂直（增加行），axis::cols 水平（增加列）
		template<class type>
		matrix<type> cat(axis a, const matrix<type>& x, const matrix<type>& y)
		{
			return (a == axis::rows) ? vertcat(x, y) : horzcat(x, y);
		}

		// 平铺复制：repmat(A, m, n) 生成 m×n 块
		template<class type>
		matrix<type> repmat(const matrix<type>& a, const xhqm::size& m, const xhqm::size& n)
		{
			matrix<type> r(a.rows() * m, a.cols() * n);
			for (xhqm::size bi = 0; bi < m; ++bi)
				for (xhqm::size bj = 0; bj < n; ++bj)
					for (xhqm::size i = 0; i < a.rows(); ++i)
						for (xhqm::size j = 0; j < a.cols(); ++j)
							r(bi * a.rows() + i, bj * a.cols() + j) = a(i, j);
			return r;
		}

		// 左右翻转（fliplr）
		template<class type>
		matrix<type> fliplr(const matrix<type>& m)
		{
			matrix<type> r(m.rows(), m.cols());
			for (xhqm::size i = 0; i < m.rows(); ++i)
				for (xhqm::size j = 0; j < m.cols(); ++j)
					r(i, j) = m(i, m.cols() - 1 - j);
			return r;
		}

		// 上下翻转（flipud）
		template<class type>
		matrix<type> flipud(const matrix<type>& m)
		{
			matrix<type> r(m.rows(), m.cols());
			for (xhqm::size i = 0; i < m.rows(); ++i)
				for (xhqm::size j = 0; j < m.cols(); ++j)
					r(i, j) = m(m.rows() - 1 - i, j);
			return r;
		}

		// 下三角（主对角以上置 0）
		template<class type>
		matrix<type> tril(const matrix<type>& m, const xhqm::size& k = 0)
		{
			matrix<type> r = m;
			for (xhqm::size i = 0; i < m.rows(); ++i)
				for (xhqm::size j = 0; j < m.cols(); ++j)
					if (j > i + k) r(i, j) = type(0);
			return r;
		}

		// 上三角（主对角以下置 0）
		template<class type>
		matrix<type> triu(const matrix<type>& m, const xhqm::size& k = 0)
		{
			matrix<type> r = m;
			for (xhqm::size i = 0; i < m.rows(); ++i)
				for (xhqm::size j = 0; j < m.cols(); ++j)
					if (j < i + k) r(i, j) = type(0);
			return r;
		}

		// 最大维长度（MATLAB length = max(size)）
		template<class type>
		xhqm::size length(const matrix<type>& m) { return (m.rows() > m.cols()) ? m.rows() : m.cols(); }

		// 逆时针旋转 90 度（MATLAB rot90）
		template<class type>
		matrix<type> rot90(const matrix<type>& m)
		{
			matrix<type> r(m.cols(), m.rows());
			for (xhqm::size i = 0; i < m.rows(); ++i)
				for (xhqm::size j = 0; j < m.cols(); ++j)
					r(m.cols() - 1 - j, i) = m(i, j);
			return r;
		}

		// 非零元素下标（行优先线性索引）
		template<class type>
		std::vector<xhqm::index> find(const matrix<type>& m)
		{
			std::vector<xhqm::index> idx;
			for (xhqm::size i = 0; i < m.size(); ++i)
				if (m.data()[i] != type(0)) idx.push_back(i);
			return idx;
		}

		// 通用翻转：axis::rows 上下翻（行翻转），axis::cols 左右翻（列翻转）
		template<class type>
		matrix<type> flip(const matrix<type>& m, axis a)
		{
			return (a == axis::rows) ? flipud(m) : fliplr(m);
		}

		// 循环移位：沿行（axis::rows）或列（axis::cols）循环移 k 位
		template<class type>
		matrix<type> circshift(const matrix<type>& m, const xhqm::index& k, axis a)
		{
			matrix<type> r(m.rows(), m.cols());
			if (a == axis::rows)
			{
				const xhqm::index rows = (xhqm::index)m.rows();
				const xhqm::index shift = ((k % rows) + rows) % rows;
				for (xhqm::index i = 0; i < rows; ++i)
					for (xhqm::index j = 0; j < (xhqm::index)m.cols(); ++j)
						r((i + shift) % rows, j) = m(i, j);
			}
			else
			{
				const xhqm::index cols = (xhqm::index)m.cols();
				const xhqm::index shift = ((k % cols) + cols) % cols;
				for (xhqm::index i = 0; i < (xhqm::index)m.rows(); ++i)
					for (xhqm::index j = 0; j < cols; ++j)
						r(i, (j + shift) % cols) = m(i, j);
			}
			return r;
		}

		// Kronecker 积：A ⊗ B
		template<class type>
		matrix<type> kron(const matrix<type>& a, const matrix<type>& b)
		{
			matrix<type> r(a.rows() * b.rows(), a.cols() * b.cols());
			for (xhqm::size ai = 0; ai < a.rows(); ++ai)
				for (xhqm::size aj = 0; aj < a.cols(); ++aj)
					for (xhqm::size bi = 0; bi < b.rows(); ++bi)
						for (xhqm::size bj = 0; bj < b.cols(); ++bj)
							r(ai * b.rows() + bi, aj * b.cols() + bj) = xhqm::mul(a(ai, aj), b(bi, bj));
			return r;
		}

		// 逆时针旋转 90° × k 次
		template<class type>
		matrix<type> rot90(const matrix<type>& m, const xhqm::size& k)
		{
			matrix<type> r = m;
			for (xhqm::size t = 0; t < (k % 4); ++t) r = rot90(r);
			return r;
		}

		// 非零元素 [行,列] 下标（与 find 线性索引对应）
		template<class type>
		std::vector<std::pair<xhqm::index, xhqm::index>> find_rc(const matrix<type>& m)
		{
			std::vector<std::pair<xhqm::index, xhqm::index>> idx;
			for (xhqm::size i = 0; i < m.rows(); ++i)
				for (xhqm::size j = 0; j < m.cols(); ++j)
					if (m(i, j) != type(0)) idx.push_back({ i, j });
			return idx;
		}
	}
}

#endif
