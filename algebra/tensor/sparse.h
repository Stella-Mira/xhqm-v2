#ifndef XHQM_TENSOR_SPARSE
#define XHQM_TENSOR_SPARSE

// COO 稀疏矩阵：只存非零三元组 (row, col, value)。
// 支持与稠密 vector/matrix 及稀疏矩阵相乘、转置、转回稠密。
#include "matrix.h"
#include "vector.h"
#include <vector>
#include <map>

namespace xhqm
{
	namespace tensor_base
	{
		template<class type>
		class sparse_matrix
		{
			xhqm::size m_rows = 0;
			xhqm::size m_cols = 0;
			std::vector<xhqm::index> m_row;	// 非零行
			std::vector<xhqm::index> m_col;	// 非零列
			store<type> m_val;				// 非零值（store<bool> 连续 1 字节，规避 vector<bool> 位压缩无 data()）
		public:
			sparse_matrix() = default;

			// 从稠密矩阵提取非零（|v| > eps）
			sparse_matrix(const xhqm::matrix<type>& dense, const type& eps = type(0))
				: m_rows(dense.rows()), m_cols(dense.cols())
			{
				for (xhqm::index i = 0; i < m_rows; ++i)
					for (xhqm::index j = 0; j < m_cols; ++j)
					{
						const type v = dense(i, j);
						if (xhqm::abs(v) > eps)
						{
							m_row.push_back(i);
							m_col.push_back(j);
							m_val.push_back(v);
						}
					}
			}

			// 从 COO 三元组构造
			sparse_matrix(const xhqm::size& rows, const xhqm::size& cols,
				const std::vector<xhqm::index>& r, const std::vector<xhqm::index>& c, const store<type>& v)
				: m_rows(rows), m_cols(cols), m_row(r), m_col(c), m_val(v) {}

			xhqm::size rows() const { return m_rows; }
			xhqm::size cols() const { return m_cols; }
			xhqm::size nnz() const { return m_val.size(); }
			const std::vector<xhqm::index>& row() const { return m_row; }
			const std::vector<xhqm::index>& col() const { return m_col; }
			const store<type>& val() const { return m_val; }

			// 转置
			sparse_matrix transpose() const
			{
				return sparse_matrix(m_cols, m_rows, m_col, m_row, m_val);
			}

			// 转回稠密
			xhqm::matrix<type> dense() const
			{
				xhqm::matrix<type> d(m_rows, m_cols, type(0));
				for (xhqm::size k = 0; k < nnz(); ++k)
					d(m_row[k], m_col[k]) = m_val[k];
				return d;
			}

			// 稀疏 × 稠密向量
			xhqm::vector<type> operator*(const xhqm::vector<type>& v) const
			{
				if (v.size() != m_cols) return xhqm::vector<type>();	// 维度不匹配防越界读
				xhqm::vector<type> r(m_rows, type(0));
				// 后端 SpMV kernel（f16/f32/f64/i8/i16/i32/i64/bool）；失败回退串行
				if (tensor_base::tensor_backend_active().spmv(r.data(), v.data(), m_row.data(), m_col.data(), m_val.data(),
					nnz(), m_rows, m_cols, tensor_base::dtype_of<type>::value))
					return r;
				for (xhqm::size k = 0; k < nnz(); ++k)
					r[m_row[k]] = xhqm::sum(r[m_row[k]], xhqm::mul(m_val[k], v[m_col[k]]));
				return r;
			}

			// 稀疏 × 稠密矩阵
			xhqm::matrix<type> operator*(const xhqm::matrix<type>& m) const
			{
				if (m.rows() != m_cols) return xhqm::matrix<type>();	// 维度不匹配防越界读
				xhqm::matrix<type> r(m_rows, m.cols(), type(0));
				for (xhqm::size k = 0; k < nnz(); ++k)
					for (xhqm::index j = 0; j < m.cols(); ++j)
						r(m_row[k], j) = xhqm::sum(r(m_row[k], j), xhqm::mul(m_val[k], m(m_col[k], j)));
				return r;
			}

			// 稀疏 × 稀疏
			sparse_matrix operator*(const sparse_matrix& s) const
			{
				std::map<std::pair<xhqm::index, xhqm::index>, type> acc;
				for (xhqm::size i = 0; i < nnz(); ++i)
					for (xhqm::size j = 0; j < s.nnz(); ++j)
						if (m_col[i] == s.m_row[j])
							acc[{ m_row[i], s.m_col[j] }] = xhqm::sum(acc[{ m_row[i], s.m_col[j] }], xhqm::mul(m_val[i], s.m_val[j]));
				std::vector<xhqm::index> rr, cc; store<type> vv;
				for (const auto& kv : acc)
				{
					if (xhqm::abs(kv.second) > xhqm::preci<type>)
					{
						rr.push_back(kv.first.first);
						cc.push_back(kv.first.second);
						vv.push_back(kv.second);
					}
				}
				return sparse_matrix(m_rows, s.m_cols, rr, cc, vv);
			}
		};
	}
}

#endif
