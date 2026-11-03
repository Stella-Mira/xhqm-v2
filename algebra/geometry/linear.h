#ifndef XHQM_LINEAR
#define XHQM_LINEAR

#include "../../metaclass/template.h"
#include "../../metaclass/buffer.h"
#include "../value.h"
#include <iomanip>
#include <cmath>

namespace xhqm
{
	template<class type> class poit;
	template<class type> class vect;
	template<class type> class matx;

	template<class type>
	class matx : protected xhqm::buffer<type>
	{
		friend class xhqm::poit<type>;
		friend class xhqm::vect<type>;
	protected:
		std::array<xhqm::size, 2> m_dimensions;
	public:
		explicit matx(const xhqm::size& m, const xhqm::size& n)
			: xhqm::buffer<type>(m * n), m_dimensions({ m, n })
		{
			if (m == n)
			{
				for (std::size_t i = 0; i < m; ++i)
				{
					for (std::size_t j = 0; j < n; ++j)
					{
						(*this)(i, j) = type((i == j) ? 1 : 0);
					}
				}
			}
		}
		explicit matx(const std::vector<std::vector<type>>& list)
			: matx(list.size(), (list.empty() ? 0 : list[0].size()))
		{
			for (xhqm::size i = 0; i < m_dimensions[0]; ++i)
			{
				for (xhqm::size j = 0; j < m_dimensions[1]; ++j)
				{
					this->operator()(i, j) = list[i][j];
				}
			}
		}
		explicit matx(const xhqm::size& m, const xhqm::size& n, const std::initializer_list<type>& list)
			: matx(m, n)
		{
			xhqm::size size = list.size();
			for (xhqm::size i = 0; i < m; ++i)
			{
				for (xhqm::size j = 0; j < n; ++j)
				{
					xhqm::index ind = i * n + j;
					this->operator()(i, j) = (ind < size ? *(list.begin() + ind) : 0);
				}
			}
		}

		inline const type& operator() (const xhqm::index& i, const xhqm::index& j) const
		{
			return (xhqm::buffer<type>::data()[i * m_dimensions[1] + j]);
		}
		inline type& operator() (const xhqm::index& i, const xhqm::index& j)
		{
			return (xhqm::buffer<type>::data()[i * m_dimensions[1] + j]);
		}

		inline friend matx operator + (const matx& a, const type& b) { matx c = a; c += b; return c; }
		inline friend matx operator - (const matx& a, const type& b) { matx c = a; c -= b; return c; }
		inline friend matx operator * (const matx& a, const type& b) { matx c = a; c *= b; return c; }
		inline friend matx operator / (const matx& a, const type& b) { matx c = a; c /= b; return c; }

		inline friend matx operator * (const matx& a, const matx& b)
		{
			matx c(0, 0);
			if (a.m_dimensions[1] != b.m_dimensions[0])
			{
				std::runtime_error error("Illegitimately multiplied matrices !!!");
			}
			else
			{
				c = matx(a.m_dimensions[0], b.m_dimensions[1]);
				for (xhqm::size i = 0; i < a.m_dimensions[0]; i++)
				{
					for (xhqm::size j = 0; j < b.m_dimensions[1]; j++)
					{
						c(i, j) = 0;
						for (xhqm::size k = 0; k < a.m_dimensions[1]; k++)
							c(i, j) += (a(i, k) * b(k, j));
					}
				}

			}
			return c;
		}

		inline matx operator += (const type& v)
		{
			xhqm::count count = xhqm::buffer<type>::size();
			for (xhqm::size i = 0; i < count; ++i)
			{
				xhqm::buffer<type>::data()[i] += v;
			}
			return (*this);
		}
		inline matx operator -= (const type& v)
		{
			xhqm::count count = xhqm::buffer<type>::size();
			for (xhqm::size i = 0; i < count; ++i)
			{
				xhqm::buffer<type>::data()[i] -= v;
			}
			return (*this);
		}
		inline matx operator *= (const type& v)
		{
			xhqm::count count = xhqm::buffer<type>::size();
			for (xhqm::size i = 0; i < count; ++i)
			{
				xhqm::buffer<type>::data()[i] *= v;
			}
			return (*this);
		}
		inline matx operator /= (const type& v)
		{
			xhqm::count count = xhqm::buffer<type>::size();
			for (xhqm::size i = 0; i < count; ++i)
			{
				xhqm::buffer<type>::data()[i] /= v;
			}
			return (*this);
		}

		inline const type* data() const
		{
			return xhqm::buffer<type>::data();
		}
		inline type* data()
		{
			return xhqm::buffer<type>::data();
		}

		// 矩阵维度访问
		inline std::array<xhqm::size, 2> dimensions() const { return m_dimensions; }
		inline xhqm::size size() const { return m_dimensions[0] * m_dimensions[1]; }
		inline xhqm::size rows() const { return m_dimensions[0]; }
		inline xhqm::size cols() const { return m_dimensions[1]; }

		inline void fill(const type& value)
		{
			xhqm::count count = xhqm::buffer<type>::size();
			for (xhqm::size i = 0; i < count; ++i)
			{
				xhqm::buffer<type>::data()[i] = value;
			}
		}

		inline std::string printf() const
		{
			std::stringstream print;
			for (xhqm::size i = 0; i < m_dimensions[0]; ++i)
			{
				for (xhqm::size j = 0; j < m_dimensions[1]; ++j)
				{
					print << std::fixed << std::setprecision(9) << this->operator()(i, j) << "\t";
				}
				print << std::endl;
			}
			std::cout << print.str() << std::endl;
			return print.str();
		}

		//Function(type&, Args&&...)
		template<class Function, typename ... Args>
		void execfun(Function&& function, Args&& ...args)
		{
			xhqm::count count = xhqm::buffer<type>::size();
			for (xhqm::size i = 0; i < count; ++i)
			{
				function(xhqm::buffer<type>::data()[i], std::forward<Args>(args)...);
			}
		}
	
		//行列式
		inline type det() const
		{
			const xhqm::size n = rows();
			if (n != cols()) return 0;     // 非方阵
			if (n == 0) return 1;          // 空矩阵
			if (n == 1) return xhqm::buffer<type>::value(0);  // 1x1矩阵直接返回元素

			std::vector<type> lu(xhqm::buffer<type>::data(), xhqm::buffer<type>::data() + n * n);
			int sign = 1; // 行列式符号

			for (xhqm::index k = 0; k < n; ++k) {
				// 列主元选择（提高数值稳定性）
				xhqm::index max_row = k;
				type max_val = std::abs(lu[k * n + k]);
				for (xhqm::index i = k + 1; i < n; ++i) {
					if (type curr = std::abs(lu[i * n + k]); curr > max_val) {
						max_val = curr;
						max_row = i;
					}
				}

				// 处理奇异矩阵
				if (max_val < xhqm::preci<type>) return 0;

				// 行交换操作
				if (max_row != k) {
					sign *= -1;
					for (xhqm::index col = 0; col < n; ++col) {
						std::swap(lu[k * n + col], lu[max_row * n + col]);
					}
				}

				// 高斯消元
				for (xhqm::index i = k + 1; i < n; ++i) {
					const type factor = lu[i * n + k] / lu[k * n + k];
					lu[i * n + k] = factor; // 存储L矩阵系数

					// 更新矩阵元素
					for (xhqm::index j = k + 1; j < n; ++j) {
						lu[i * n + j] -= factor * lu[k * n + j];
					}
				}
			}

			// 计算对角元素乘积
			type determinant = sign;
			for (xhqm::index k = 0; k < n; ++k) {
				determinant *= lu[k * n + k];
			}
			return determinant;
		}
		//单位矩阵
		inline matx idemat() const
		{
			const std::size_t n = std::min(m_dimensions[0], m_dimensions[1]);

			matx identity(n, n);
			for (std::size_t i = 0; i < n; ++i)
				for (std::size_t j = 0; j < n; ++j)
					identity(i, j) = type((i == j) ? 1 : 0);
			return identity;
		}
		//转置矩阵
		inline matx tramat() const
		{
			auto dim = m_dimensions;
			matx transposed(dim[1], dim[0]);
			for (xhqm::index i = 0; i < dim[0]; i++)
				for (xhqm::index j = 0; j < dim[1]; j++)
					transposed(j, i) = this->operator()(i, j);
			return transposed;
		};
		//伴随矩阵
		inline matx adjmat() const
		{
			return invmat() * det();
		};
		//逆矩阵
		inline matx invmat() const
		{
			const xhqm::size n = m_dimensions[0];
			if (n != m_dimensions[1]) return matx(0, 0);

			// 复制当前矩阵并初始化逆矩阵为单位矩阵
			matx temp(*this);
			matx inv = this->idemat();

			// 部分主元高斯-约当消元法
			for (xhqm::size pivot = 0; pivot < n; ++pivot) {
				// 列主元选择
				xhqm::size max_row = pivot;
				type max_val = std::abs(temp(pivot, pivot));
				for (xhqm::size row = pivot + 1; row < n; ++row) {
					if (type curr_val = std::abs(temp(row, pivot)); curr_val > max_val) {
						max_val = curr_val;
						max_row = row;
					}
				}

				// 奇异矩阵检测
				if (max_val < xhqm::preci<type>)
					return matx(0, 0);

				// 行交换（主元行和当前行）
				if (max_row != pivot) {
					for (xhqm::size col = 0; col < n; ++col) {
						std::swap(temp(pivot, col), temp(max_row, col));
						std::swap(inv(pivot, col), inv(max_row, col));
					}
				}

				// 归一化主元行
				const type pivot_val = temp(pivot, pivot);
				for (xhqm::size col = 0; col < n; ++col) {
					temp(pivot, col) /= pivot_val;
					inv(pivot, col) /= pivot_val;
				}

				// 消去其他行
				for (xhqm::size row = 0; row < n; ++row) {
					if (row == pivot) continue;

					const type factor = temp(row, pivot);
					for (xhqm::size col = 0; col < n; ++col) {
						temp(row, col) -= factor * temp(pivot, col);
						inv(row, col) -= factor * inv(pivot, col);
					}
				}
			}

			return inv;
		};
		//子矩阵
		inline matx kidmat(const xhqm::index& delm, const xhqm::index& deln) const
		{
			xhqm::index index = 0;
			auto kiddim = m_dimensions;
			kiddim[0] = (kiddim[0] == 0 ? 0 : (delm == xhqm::npos ? kiddim[0] : kiddim[0] - 1));
			kiddim[1] = (kiddim[1] == 0 ? 0 : (deln == xhqm::npos ? kiddim[1] : kiddim[1] - 1));
			xhqm::matx<type> kid(kiddim[0], kiddim[1]);
			for (xhqm::index i = 0; i < m_dimensions[0]; i++)
			{
				if (i == delm) continue;
				for (xhqm::index j = 0; j < m_dimensions[1]; j++)
				{
					if (j == deln) continue;
					kid.value(index++) = this->operator()(i, j);
				}
			}
			return kid;
		};
	};
	
	template<class type>
	class vect : protected xhqm::buffer<type>
	{
		friend class xhqm::poit<type>;
		std::array<xhqm::size, 2> m_dimensions;
	public:
		explicit vect(const type& _x = 0, const type& _y = 0, const type& _z = 0)
			: xhqm::buffer<type>(4), m_dimensions({ 1, 4 })
		{
			this->operator[](0) = _x;
			this->operator[](1) = _y;
			this->operator[](2) = _z;
			this->operator[](3) = 0;
		}

		inline operator xhqm::matx<type>() const
		{
			xhqm::matx<type> mat(m_dimensions[0], m_dimensions[1]);
			for (xhqm::size i = 0, size = mat.size(); i < size; ++i)
			{
				mat.value(i) = this->value(i);
			}
			return mat;
		}

		inline const type& operator[] (const xhqm::index& _index) const { return (xhqm::buffer<type>::data()[_index]); }
		inline type& operator[] (const xhqm::index& _index) { return (xhqm::buffer<type>::data()[_index]); }

		inline vect transposed() { xhqm::size dimension = this->m_dimensions[0]; this->m_dimensions[0] = this->m_dimensions[1]; this->m_dimensions[1] = dimension; return (*this); }
		inline type length() const { return std::sqrt((*this)[0] * (*this)[0] + (*this)[1] * (*this)[1] + (*this)[2] * (*this)[2]); }
		inline bool is_row() const { return (this->m_dimensions[0] == 1); }
		inline bool is_col() const { return (this->m_dimensions[1] == 1); }

		inline const type& x() const { return this->operator[](0); }
		inline const type& y() const { return this->operator[](1); }
		inline const type& z() const { return this->operator[](2); }

		inline type& x() { return this->operator[](0); }
		inline type& y() { return this->operator[](1); }
		inline type& z() { return this->operator[](2); }

		inline std::string printf() const
		{
			std::stringstream print;
			for (xhqm::size i = 0; i < 4; ++i)
			{
				print << std::fixed << std::setprecision(9) << this->operator[](i) << "\t";
			}
			std::cout << print.str() << std::endl;
			return print.str();
		}

		inline type modulus() const { return std::sqrt(vect::dot((*this), (*this))); }
		inline vect normal() const { return xhqm::vect<type>((*this) / length()); }
		static type angle(const vect& a, const vect& b) //0-2pi
		{
			type dot_product = dot(a, b); type len_a = a.length(); type len_b = b.length();
			if (len_a < xhqm::preci<type> || len_b < xhqm::preci<type>) { return 0; }

			// 计算余弦值并限制在[-1, 1]范围内（防止浮点误差）
			type cos_theta = dot_product / (len_a * len_b);
			cos_theta = std::min(static_cast<type>(+1.0), cos_theta);
			cos_theta = std::max(static_cast<type>(-1.0), cos_theta);
			return std::acos(cos_theta);
		}
		static type dot(const vect& a, const vect& b)
		{
			type sum = 0; //点乘
			sum += (a[0] * b[0]);
			sum += (a[1] * b[1]);
			sum += (a[2] * b[2]);
			return sum;
		}
		static vect mul(const vect& a, const vect& b)
		{
			vect c; //叉乘
			c[0] = a[1] * b[2] - a[2] * b[1];
			c[1] = a[2] * b[0] - a[0] * b[2];
			c[2] = a[0] * b[1] - a[1] * b[0];
			return c;
		}


		inline friend vect operator + (const vect& a, const vect& b)
		{
			return vect(a[0] + b[0], a[1] + b[1], a[2] + b[2]);
		}
		inline friend vect operator - (const vect& a, const vect& b)
		{
			return vect(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
		}

		inline friend vect operator * (const vect& vec, const xhqm::matx<type>& mat)
		{
			auto dimensions = mat.dimensions();
			if (dimensions[0] != 4 || dimensions[1] != 4 || vec.is_col())
			{
				throw std::runtime_error("Illegitimately multiplied matrices !!!");
				return vec;
			}
			return vect(
				vec[0] * mat(0, 0) + vec[1] * mat(1, 0) + vec[2] * mat(2, 0) + vec[3] * mat(3, 0),
				vec[0] * mat(0, 1) + vec[1] * mat(1, 1) + vec[2] * mat(2, 1) + vec[3] * mat(3, 1),
				vec[0] * mat(0, 2) + vec[1] * mat(1, 2) + vec[2] * mat(2, 2) + vec[3] * mat(3, 2)
			);
		}
		inline friend vect operator * (const xhqm::matx<type>& mat, const vect& vec)
		{
			auto dimensions = mat.dimensions();
			if (dimensions[0] != 4 || dimensions[1] != 4 || vec.is_row())
			{
				throw std::runtime_error("Illegitimately multiplied matrices !!!");
				return vec;
			}
			vect vect(
				mat(0, 0) * vec[0] + mat(0, 1) * vec[1] + mat(0, 2) * vec[2] + mat(0, 3) * vec[3],
				mat(1, 0) * vec[0] + mat(1, 1) * vec[1] + mat(1, 2) * vec[2] + mat(1, 3) * vec[3],
				mat(2, 0) * vec[0] + mat(2, 1) * vec[1] + mat(2, 2) * vec[2] + mat(2, 3) * vec[3]
			);
			vect.transposed();
			return vect;
		}

		inline friend vect operator + (const vect& a, const type& b) { return vect(a[0] + b, a[1] + b, a[2] + b); }
		inline friend vect operator - (const vect& a, const type& b) { return vect(a[0] - b, a[1] - b, a[2] - b); }
		inline friend vect operator * (const vect& a, const type& b) { return vect(a[0] * b, a[1] * b, a[2] * b); }
		inline friend vect operator / (const vect& a, const type& b) { return vect(a[0] / b, a[1] / b, a[2] / b); }

		inline static vect row(const xhqm::matx<type>& mat, const xhqm::index& pos)
		{
			vect vec(0, 0, 0);
			vec.m_dimensions[0] = 1;
			vec.m_dimensions[1] = 4;
			vec[0] = mat(pos, 0);
			vec[1] = mat(pos, 1);
			vec[2] = mat(pos, 2);
			return vec;
		}
		inline static vect col(const xhqm::matx<type>& mat, const xhqm::index& pos)
		{
			vect vec(0, 0, 0);
			vec.m_dimensions[0] = 4;
			vec.m_dimensions[1] = 1;
			vec[0] = mat(0, pos);
			vec[1] = mat(1, pos);
			vec[2] = mat(2, pos);
			return vec;
		}

		inline bool operator==(const vect& v) const {
			auto a = std::abs(this->x() - v.x());
			auto b = std::abs(this->y() - v.y());
			auto c = std::abs(this->z() - v.z());
			return (a < xhqm::preci<type> && b < xhqm::preci<type> && c < xhqm::preci<type>);
		}

		inline bool operator!=(const vect& v) const {
			auto a = std::abs(this->x() - v.x());
			auto b = std::abs(this->y() - v.y());
			auto c = std::abs(this->z() - v.z());
			return (a > xhqm::preci<type> || b > xhqm::preci<type> || c > xhqm::preci<type>);
		}
	};

	template<class type>
	class poit : protected xhqm::buffer<type>
	{
		friend class xhqm::vect<type>;
		std::array<xhqm::size, 2> m_dimensions;
	public:
		explicit poit(const type& _x = 0, const type& _y = 0, const type& _z = 0)
			: xhqm::buffer<type>(4), m_dimensions({ 1, 4 })
		{
			this->operator[](0) = _x;
			this->operator[](1) = _y;
			this->operator[](2) = _z;
			this->operator[](3) = 1;
		}

		//explicit poit(const std::initializer_list<type>& _list)
		//	: xhqm::buffer<type>(4), m_dimensions({ 1, 4 })
		//{
		//	if (_list.size() > 0) this->operator[](0) = _list[0];
		//	if (_list.size() > 1) this->operator[](1) = _list[1];
		//	if (_list.size() > 2) this->operator[](2) = _list[2];
		//	this->operator[](3) = 1;
		//}

		explicit poit(const xhqm::vect<type>& _vect)
			: xhqm::buffer<type>(4), m_dimensions(_vect.m_dimensions)
		{
			this->operator[](0) = _vect[0];
			this->operator[](1) = _vect[1];
			this->operator[](2) = _vect[2];
			this->operator[](3) = 1;
		}

		inline operator xhqm::matx<type>() const
		{
			xhqm::matx<type> mat(m_dimensions[0], m_dimensions[1]);
			for (xhqm::size i = 0, size = mat.size(); i < size; ++i)
			{
				mat.value(i) = this->value(i);
			}
			return mat;
		}

		inline operator xhqm::vect<type>() const
		{
			xhqm::vect<type> vec;
			vec[0] = this->operator[](0);
			vec[1] = this->operator[](1);
			vec[2] = this->operator[](2);
			if (this->is_col()) vec.transposed();
			return vec;
		}

		inline poit operator= (const xhqm::vect<type>& vec)
		{
			this->operator[](0) = vec[0];
			this->operator[](1) = vec[1];
			this->operator[](2) = vec[2];
			if (vec.is_col()) this->transposed();
			return (*this);
		}

		inline const type& operator[] (const xhqm::index& _index) const { return (xhqm::buffer<type>::data()[_index]); }
		inline type& operator[] (const xhqm::index& _index) { return (xhqm::buffer<type>::data()[_index]); }

		inline poit transposed() { xhqm::size dimension = this->m_dimensions[0]; this->m_dimensions[0] = this->m_dimensions[1]; this->m_dimensions[1] = dimension; return (*this); }
		inline type length() const { return std::sqrt((*this)[0] * (*this)[0] + (*this)[1] * (*this)[1] + (*this)[2] * (*this)[2]); }
		inline bool is_row() const { return (this->m_dimensions[0] == 1); }
		inline bool is_col() const { return (this->m_dimensions[1] == 1); }

		inline const type& x() const { return this->operator[](0); }
		inline const type& y() const { return this->operator[](1); }
		inline const type& z() const { return this->operator[](2); }

		inline type& x() { return this->operator[](0); }
		inline type& y() { return this->operator[](1); }
		inline type& z() { return this->operator[](2); }

		inline std::string printf() const
		{
			std::stringstream print;
			for (xhqm::size i = 0; i < 4; ++i)
			{
				print << std::fixed << std::setprecision(9) << this->operator[](i) << "\t";
			}
			std::cout << print.str() << std::endl;
			return print.str();
		}

		inline friend poit operator + (const poit& a, const poit& b)
		{
			return poit(a[0] + b[0], a[1] + b[1], a[2] + b[2]);
		}
		inline friend poit operator + (const poit& a, const xhqm::vect<type>& b)
		{
			return poit(a[0] + b[0], a[1] + b[1], a[2] + b[2]);
		}
		inline friend poit operator - (const poit& a, const xhqm::vect<type>& b)
		{
			return poit(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
		}
		inline friend poit operator * (const poit& a, const xhqm::vect<type>& b)
		{
			return poit(a[0] * b[0], a[1] * b[1], a[2] * b[2]);
		}
		inline friend xhqm::vect<type> operator - (const poit& a, const poit& b)
		{
			return xhqm::vect<type>(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
		}

		inline friend poit operator * (const poit& pot, const xhqm::matx<type>& mat)
		{
			auto dimensions = mat.dimensions();
			if (dimensions[0] != 4 || dimensions[1] != 4 || pot.is_col())
			{
				throw std::runtime_error("Illegitimately multiplied matrices !!!");
				return pot;
			}
			return poit(
				pot[0] * mat(0, 0) + pot[1] * mat(1, 0) + pot[2] * mat(2, 0) + pot[3] * mat(3, 0),
				pot[0] * mat(0, 1) + pot[1] * mat(1, 1) + pot[2] * mat(2, 1) + pot[3] * mat(3, 1),
				pot[0] * mat(0, 2) + pot[1] * mat(1, 2) + pot[2] * mat(2, 2) + pot[3] * mat(3, 2)
			);
		}
		inline friend poit operator * (const xhqm::matx<type>& mat, const poit& pot)
		{
			auto dimensions = mat.dimensions();
			if (dimensions[0] != 4 || dimensions[1] != 4 || pot.is_row())
			{
				throw std::runtime_error("Illegitimately multiplied matrices !!!");
				return pot;
			}
			poit poit(
				mat(0, 0) * pot[0] + mat(0, 1) * pot[1] + mat(0, 2) * pot[2] + mat(0, 3) * pot[3],
				mat(1, 0) * pot[0] + mat(1, 1) * pot[1] + mat(1, 2) * pot[2] + mat(1, 3) * pot[3],
				mat(2, 0) * pot[0] + mat(2, 1) * pot[1] + mat(2, 2) * pot[2] + mat(2, 3) * pot[3]
			);
			poit.transposed();
			return poit;
		}

		inline friend poit operator + (const poit& a, const type& b) { return poit(a[0] + b, a[1] + b, a[2] + b); }
		inline friend poit operator - (const poit& a, const type& b) { return poit(a[0] - b, a[1] - b, a[2] - b); }
		inline friend poit operator * (const poit& a, const type& b) { return poit(a[0] * b, a[1] * b, a[2] * b); }
		inline friend poit operator / (const poit& a, const type& b) { return poit(a[0] / b, a[1] / b, a[2] / b); }

		inline static poit row(const xhqm::matx<type>& mat, const xhqm::index& pos)
		{
			poit pot(0, 0, 0);
			pot.m_dimensions[0] = 1;
			pot.m_dimensions[1] = 4;
			pot[0] = mat(pos, 0);
			pot[1] = mat(pos, 1);
			pot[2] = mat(pos, 2);
			pot[3] = 0;
			return pot;
		}
		inline static poit col(const xhqm::matx<type>& mat, const xhqm::index& pos)
		{
			poit pot(0, 0, 0);
			pot.m_dimensions[0] = 4;
			pot.m_dimensions[1] = 1;
			pot[0] = mat(0, pos);
			pot[1] = mat(1, pos);
			pot[2] = mat(2, pos);
			return pot;
		}

		inline bool operator==(const poit& p) const {
			auto a = std::abs(this->x() - p.x());
			auto b = std::abs(this->y() - p.y());
			auto c = std::abs(this->z() - p.z());
			return (a < xhqm::preci<type> && b < xhqm::preci<type> && c < xhqm::preci<type>);
		}

		inline bool operator!=(const poit& p) const {
			auto a = std::abs(this->x() - p.x());
			auto b = std::abs(this->y() - p.y());
			auto c = std::abs(this->z() - p.z());
			return (a > xhqm::preci<type> || b > xhqm::preci<type> || c > xhqm::preci<type>);
		}
	};

	//角度制转弧度制
	template<typename type>
	inline type radians(const type& value)
	{
		return static_cast<type>((xhqm::pi / 180) * value);
	}
	//弧度制转角度制
	template<typename type>
	inline type degrees(const type& value)
	{
		return static_cast<type>(value * (180 / xhqm::pi));
	}

	// 构造透视投影矩阵 (3D透视)
	// 参数: 
	// fovy - 垂直视野角度(弧度), 
	// aspect - 宽高比, 
	// near - 近平面距离, 
	// far - 远平面距离
	template<class type>
	static matx<type> perspective(type fovy, type aspect, type near, type far)
	{
		// 计算焦距 (1/tan(fovy/2))
		const type f = 1 / std::tan(fovy / 2);
		matx<type> proj(4, 4);

		// 设置透视投影分量 (行主序布局：平移在 (2,3)、透视除法在 (3,2))
		proj(0, 0) = f / aspect;
		proj(1, 1) = f;
		proj(2, 2) = (far + near) / (near - far);  // 深度映射
		proj(2, 3) = (2 * far * near) / (near - far);  // 标准透视：负号（near-far）
		proj(3, 2) = -1;  // 实现透视除法
		proj(3, 3) = 0;

		return proj;
	}


	// 构造正交投影矩阵 (3D正交)
	// 参数: 
	// left/right - 左右视界, 
	// bottom/top - 下上视界, 
	// near/far - 远近平面
	template<class type>
	static matx<type> ortho(type left, type right, type bottom, type top, type near, type far)
	{
		matx<type> orth(4, 4);

		// 设置正交投影分量
		orth(0, 0) = 2 / (right - left);
		orth(1, 1) = 2 / (top - bottom);
		orth(2, 2) = -2 / (far - near);  // 注意反向Z轴

		orth(0, 3) = -(right + left) / (right - left);
		orth(1, 3) = -(top + bottom) / (top - bottom);
		orth(2, 3) = -(far + near) / (far - near);

		return orth;
	}

	// 旋转矩阵X轴 (弧度制)
	template<class type>
	static matx<type> rotate_x(type angle)
		{
			matx<type> rot = (matx<type>(4, 4)).idemat();
			const type c = std::cos(angle);
			const type s = std::sin(angle);

			// 设置绕X轴旋转分量
			rot(1, 1) = c;
			rot(1, 2) = -s;
			rot(2, 1) = s;
			rot(2, 2) = c;

			return rot;
		}
	
	// 旋转矩阵Y轴 (弧度制)
	template<class type>
	static matx<type> rotate_y(type angle)
		{
			matx<type> rot = (matx<type>(4, 4)).idemat();
			const type c = std::cos(angle);
			const type s = std::sin(angle);

			rot(0, 0) = c;
			rot(0, 2) = s;
			rot(2, 0) = -s;
			rot(2, 2) = c;

			return rot;
		}
	
	// 旋转矩阵Z轴 (弧度制)
	template<class type>
	static matx<type> rotate_z(type angle)
		{
			matx<type> rot = (matx<type>(4, 4)).idemat();
			const type c = std::cos(angle);
			const type s = std::sin(angle);

			rot(0, 0) = c;
			rot(0, 1) = -s;
			rot(1, 0) = s;
			rot(1, 1) = c;

			return rot;
		}
	
	// 旋转矩阵V轴 - 绕任意单位向量轴旋转 (弧度制)
	// 使用罗德里格斯旋转公式 (高效实现)
	template<class type>
	static matx<type> rotate_v(const vect<type>& axis, type angle)
		{
			const type c = std::cos(angle);
			const type s = std::sin(angle);
			const type t = 1 - c;

			// 确保轴是单位向量
			const vect<type> n = axis.normal();

			matx<type> rot(4, 4);

			// 设置旋转矩阵分量
			rot(0, 0) = t * n.x() * n.x() + c;
			rot(0, 1) = t * n.x() * n.y() - s * n.z();
			rot(0, 2) = t * n.x() * n.z() + s * n.y();
			rot(0, 3) = 0;

			rot(1, 0) = t * n.x() * n.y() + s * n.z();
			rot(1, 1) = t * n.y() * n.y() + c;
			rot(1, 2) = t * n.y() * n.z() - s * n.x();
			rot(1, 3) = 0;

			rot(2, 0) = t * n.x() * n.z() - s * n.y();
			rot(2, 1) = t * n.y() * n.z() + s * n.x();
			rot(2, 2) = t * n.z() * n.z() + c;
			rot(2, 3) = 0;

			// 设置齐次坐标分量
			rot(3, 0) = 0;
			rot(3, 1) = 0;
			rot(3, 2) = 0;
			rot(3, 3) = 1;

			return rot;
		}
	
	// 构造平移矩阵 (3D位移)
	template<class type>
	static matx<type> translate(type tx, type ty, type tz)
		{
			matx<type> trans = (matx<type>(4, 4)).idemat();
			// 设置位移分量
			trans(0, 3) = tx;
			trans(1, 3) = ty;
			trans(2, 3) = tz;
			return trans;
		}
	
	// 构造平移矩阵 (向量输入)
	template<class type>
	static matx<type> translate(const vect<type>& t)
		{
			return translate(t.x(), t.y(), t.z());
		}
	
	// 构造缩放矩阵 (独立轴向比例)
	template<class type>
	static matx<type> scale(type sx, type sy, type sz)
		{
			matx<type> scaler(4, 4);

			scaler(0, 0) = sx;
			scaler(1, 1) = sy;
			scaler(2, 2) = sz;
			scaler(3, 3) = 1.0;

			return scaler;
		}
	
	// 构造缩放矩阵 (统一比例)
	template<class type>
	static matx<type> scale(type s)
		{
			return scale(s, s, s);
		}

	// 从4x4矩阵中提取纯偏移移动
	template<class type>
	matx<type> extract_move(const matx<type>& matrix)
	{
		matx<type> result(4, 4);
		//行矩阵
		result(0, 3) = matrix(0, 3);
		result(1, 3) = matrix(1, 3);
		result(2, 3) = matrix(2, 3);
		//列矩阵
		result(3, 0) = matrix(3, 0);
		result(3, 1) = matrix(3, 1);
		result(3, 2) = matrix(3, 2);
		return result;
	}
	// 从4x4矩阵中提取纯旋转矩阵
	template<class type>
	matx<type> extract_rote(const matx<type>& matrix)
	{
		matx<type> result(4, 4);

		//vect<type> col0 = col0.col(matrix, 0).normal();
		//vect<type> col1 = col1.col(matrix, 1).normal();
		//vect<type> col2 = col2.col(matrix, 2).normal();
		//result(0, 0) = col0[0]; result(0, 1) = col1[0]; result(0, 2) = col2[0];
		//result(1, 0) = col0[1]; result(1, 1) = col1[1]; result(1, 2) = col2[1];
		//result(2, 0) = col0[2]; result(2, 1) = col1[2]; result(2, 2) = col2[2];

		vect<type> row0 = row0.row(matrix, 0).normal();
		vect<type> row1 = row1.row(matrix, 1).normal();
		vect<type> row2 = row2.row(matrix, 2).normal();
		result(0, 0) = row0[0]; result(0, 1) = row0[1]; result(0, 2) = row0[2];
		result(1, 0) = row1[0]; result(1, 1) = row1[1]; result(1, 2) = row1[2];
		result(2, 0) = row2[0]; result(2, 1) = row2[1]; result(2, 2) = row2[2];

		return result;
	}
	// 从4x4矩阵中提取纯缩放矩阵 //对镜像无效
	template<class type>
	matx<type> extract_zoom(const matx<type>& matrix)
	{
		matx<type> result(4, 4);

		vect<type> col0; col0.col(matrix, 0);
		vect<type> col1; col1.col(matrix, 1);
		vect<type> col2; col2.col(matrix, 2);
		result(0, 0) = col0.length();
		result(1, 1) = col1.length();
		result(2, 2) = col2.length();

		//vect<type> row0; row0.row(matrix, 0);
		//vect<type> row1; row1.row(matrix, 1);
		//vect<type> row2; row2.row(matrix, 2);
		//result(0, 0) = row0.length();
		//result(1, 1) = row1.length();
		//result(2, 2) = row2.length();

		return result;
	}
}
#endif
