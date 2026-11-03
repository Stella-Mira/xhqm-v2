#ifndef XHQM_DUAL_QUATERNION
#define XHQM_DUAL_QUATERNION

// 对偶四元数：表示刚体变换（旋转 + 平移），形式 real + ε·dual。
// real = 旋转四元数，dual = 0.5·t·real（t 为平移的纯虚四元数）。
#include "quaternion.hpp"
#include <array>

namespace xhqm
{
	template<typename type> struct dual_quaternion
	{
		quaternion<type> real;	// 旋转
		quaternion<type> dual;	// 对偶部

		dual_quaternion() = default;	// 恒等变换（real=1，dual=0）
		dual_quaternion(const quaternion<type>& r, const quaternion<type>& d) : real(r), dual(d) {}

		// 从旋转 + 平移构造
		dual_quaternion(const quaternion<type>& r, const std::array<type, 3>& t)
			: real(r)
		{
			quaternion<type> tq(t[0], t[1], t[2], type(0));
			dual = tq * r * type(0.5);
		}

		// 乘法（组合变换）：(r1+εd1)(r2+εd2) = r1r2 + ε(r1d2 + d1r2)
		dual_quaternion operator*(const dual_quaternion& v) const
		{
			return dual_quaternion(real * v.real, real * v.dual + dual * v.real);
		}

		// 共轭
		dual_quaternion conjugate() const
		{
			return dual_quaternion(real.conjugate(), dual.conjugate());
		}

		// 归一化（按实部范数，保证刚体变换）
		dual_quaternion normalized() const
		{
			const type n = real.norm();
			return dual_quaternion(real / n, dual / n);
		}

		// 变换三维点 p（p' = real·p·real* + 2·dual·real*）
		std::array<type, 3> transform(const std::array<type, 3>& p) const
		{
			quaternion<type> pq(p[0], p[1], p[2], type(0));
			quaternion<type> r = real * pq * real.conjugate() + (dual * real.conjugate()) * type(2);
			return { r.x, r.y, r.z };
		}

		// 提取旋转四元数
		quaternion<type> rotation() const { return real.normalized(); }
		// 提取平移 t = 2·dual·real*
		std::array<type, 3> translation() const
		{
			quaternion<type> t = (dual * real.conjugate()) * type(2);
			return { t.x, t.y, t.z };
		}
	};
}

#endif
