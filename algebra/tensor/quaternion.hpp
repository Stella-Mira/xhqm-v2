#ifndef XHQM_QUATERNION
#define XHQM_QUATERNION

#include "../mathf.h"
#include "../itype.h"
#include "../ftype.h"
#include <array>

namespace xhqm
{
	//四元数 w + xi + yj + zk（Hamilton 乘法），单位四元数表示三维旋转
	//与 fantasy::quat 相互独立；本类型是 algebra 的一等代数类型
	template<typename type> struct quaternion
	{
		type x = type();
		type y = type();
		type z = type();
		type w = type(1);

		quaternion() = default;
		quaternion(const type& x, const type& y, const type& z, const type& w) : x(x), y(y), z(z), w(w) {}
		//实部 w + 纯向量 v
		quaternion(const type& w, const std::array<type, 3>& v) : x(v[0]), y(v[1]), z(v[2]), w(w) {}

		inline type norm2() const { return x * x + y * y + z * z + w * w; }	//模方
		inline type norm() const { return std::sqrt(norm2()); }				//模长
		inline quaternion conjugate() const { return quaternion(-x, -y, -z, w); }
		inline quaternion inverse() const { return conjugate() / norm2(); }
		inline quaternion normalized() const { return (*this) / norm(); }
		inline quaternion identity() const { return quaternion(); }

		//Hamilton 积 q1⊗q2（先 q1 后 q2）
		inline quaternion operator * (const quaternion& q) const
		{
			return quaternion(
				w * q.x + x * q.w + y * q.z - z * q.y,
				w * q.y - x * q.z + y * q.w + z * q.x,
				w * q.z + x * q.y - y * q.x + z * q.w,
				w * q.w - x * q.x - y * q.y - z * q.z);
		}
		inline quaternion operator + (const quaternion& q) const { return quaternion(x + q.x, y + q.y, z + q.z, w + q.w); }
		inline quaternion operator - (const quaternion& q) const { return quaternion(x - q.x, y - q.y, z - q.z, w - q.w); }
		inline quaternion operator - () const { return quaternion(-x, -y, -z, -w); }
		inline quaternion operator * (const type& s) const { return quaternion(x * s, y * s, z * s, w * s); }
		inline quaternion operator / (const type& s) const { return quaternion(x / s, y / s, z / s, w / s); }

		inline quaternion operator += (const quaternion& q) { (*this) = (*this) + q; return (*this); }
		inline quaternion operator -= (const quaternion& q) { (*this) = (*this) - q; return (*this); }
		inline quaternion operator *= (const quaternion& q) { (*this) = (*this) * q; return (*this); }
		inline quaternion operator *= (const type& s) { (*this) = (*this) * s; return (*this); }
		inline quaternion operator /= (const type& s) { (*this) = (*this) / s; return (*this); }

		//旋转三维向量（四元数须为单位四元数）：v' = v + w·t + u×t，t = 2u×v，u = (x,y,z)
		inline std::array<type, 3> rotate(const std::array<type, 3>& v) const
		{
			const std::array<type, 3> u = { x, y, z };
			const std::array<type, 3> t = {
				type(2) * (u[1] * v[2] - u[2] * v[1]),
				type(2) * (u[2] * v[0] - u[0] * v[2]),
				type(2) * (u[0] * v[1] - u[1] * v[0]) };
			return {
				v[0] + w * t[0] + (u[1] * t[2] - u[2] * t[1]),
				v[1] + w * t[1] + (u[2] * t[0] - u[0] * t[2]),
				v[2] + w * t[2] + (u[0] * t[1] - u[1] * t[0]) };
		}

		//单位四元数 -> 3x3 旋转矩阵（行主序，9 元素）
		inline std::array<type, 9> to_matrix() const
		{
			type xx = x * x, yy = y * y, zz = z * z;
			type xy = x * y, xz = x * z, yz = y * z, wx = w * x, wy = w * y, wz = w * z;
			return {
				type(1) - type(2) * (yy + zz), type(2) * (xy - wz),         type(2) * (xz + wy),
				type(2) * (xy + wz),         type(1) - type(2) * (xx + zz), type(2) * (yz - wx),
				type(2) * (xz - wy),         type(2) * (yz + wx),         type(1) - type(2) * (xx + yy) };
		}

		static quaternion from_matrix(const std::array<type, 9>& m);
		static quaternion from_axis_angle(const std::array<type, 3>& axis, const type& angle);
		static quaternion from_euler(const type& x, const type& y, const type& z);	//弧度，绕 x/y/z 顺序
		std::array<type, 4> to_axis_angle() const;	//返回 {x,y,z 单位轴, 角度}
		std::array<type, 3> to_euler() const;
		static quaternion slerp(const quaternion& a, const quaternion& b, const type& t);
		static type dot(const quaternion& a, const quaternion& b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }

		// so(3) 指数映射：旋转向量 v=(x,y,z)（xyz 视为 ω，|ω| 为旋转角）→ 单位四元数
		static quaternion exp(const quaternion& v)
		{
			const type theta = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
			if (theta < xhqm::preci<type>) return quaternion();
			const type s = std::sin(theta / type(2)) / theta;
			return quaternion(v.x * s, v.y * s, v.z * s, std::cos(theta / type(2)));
		}
		// so(3) 对数映射：单位四元数 → 旋转向量（纯虚四元数，xyz=ω）
		static quaternion log(const quaternion& q)
		{
			const quaternion n = q.normalized();
			const type vnorm = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
			if (vnorm < xhqm::preci<type>) return quaternion(type(0), type(0), type(0), type(0));
			const type theta = type(2) * std::atan2(vnorm, n.w);
			const type s = theta / vnorm;
			return quaternion(n.x * s, n.y * s, n.z * s, type(0));
		}
	};

	template<typename type> using quat = quaternion<type>;

	//—— 3x3 旋转矩阵 -> 四元数 ——
	template<typename type>
	quaternion<type> quaternion<type>::from_matrix(const std::array<type, 9>& m)
	{
		type trace = m[0] + m[4] + m[8];
		quaternion q;
		if (trace > type(0))
		{
			type s = std::sqrt(trace + type(1)) * type(2);
			q.w = type(0.25) * s;
			q.x = (m[7] - m[5]) / s;
			q.y = (m[2] - m[6]) / s;
			q.z = (m[3] - m[1]) / s;
		}
		else if (m[0] > m[4] && m[0] > m[8])
		{
			type s = std::sqrt(type(1) + m[0] - m[4] - m[8]) * type(2);
			q.w = (m[7] - m[5]) / s;
			q.x = type(0.25) * s;
			q.y = (m[1] + m[3]) / s;
			q.z = (m[2] + m[6]) / s;
		}
		else if (m[4] > m[8])
		{
			type s = std::sqrt(type(1) + m[4] - m[0] - m[8]) * type(2);
			q.w = (m[2] - m[6]) / s;
			q.x = (m[1] + m[3]) / s;
			q.y = type(0.25) * s;
			q.z = (m[5] + m[7]) / s;
		}
		else
		{
			type s = std::sqrt(type(1) + m[8] - m[0] - m[4]) * type(2);
			q.w = (m[3] - m[1]) / s;
			q.x = (m[2] + m[6]) / s;
			q.y = (m[5] + m[7]) / s;
			q.z = type(0.25) * s;
		}
		return q;
	}

	//—— 轴角 -> 四元数 ——
	template<typename type>
	quaternion<type> quaternion<type>::from_axis_angle(const std::array<type, 3>& axis, const type& angle)
	{
		type n = std::sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
		if (n < xhqm::preci<type>) return quaternion();
		type s = std::sin(angle / type(2)) / n;
		return quaternion(axis[0] * s, axis[1] * s, axis[2] * s, std::cos(angle / type(2)));
	}

	//—— 四元数 -> 轴角 ——
	template<typename type>
	std::array<type, 4> quaternion<type>::to_axis_angle() const
	{
		std::array<type, 4> r = { type(), type(), type(), type() };
		type cw = (w > type(1)) ? type(1) : (w < type(-1) ? type(-1) : w);
		type angle = type(2) * std::acos(cw);
		type s = std::sqrt(type(1) - cw * cw);
		if (s > xhqm::preci<type>)
		{
			type inv = type(1) / s;
			r[0] = x * inv;
			r[1] = y * inv;
			r[2] = z * inv;
		}
		r[3] = angle;
		return r;
	}

	//—— 欧拉角 -> 四元数（q = qz ⊗ qy ⊗ qx）——
	template<typename type>
	quaternion<type> quaternion<type>::from_euler(const type& x, const type& y, const type& z)
	{
		type cx = std::cos(x / type(2)), sx = std::sin(x / type(2));
		type cy = std::cos(y / type(2)), sy = std::sin(y / type(2));
		type cz = std::cos(z / type(2)), sz = std::sin(z / type(2));
		quaternion qx(sx, type(), type(), cx);
		quaternion qy(type(), sy, type(), cy);
		quaternion qz(type(), type(), sz, cz);
		return qz * qy * qx;
	}

	//—— 四元数 -> 欧拉角（xyz / roll-pitch-yaw）——
	template<typename type>
	std::array<type, 3> quaternion<type>::to_euler() const
	{
		std::array<type, 3> r = { type(), type(), type() };
		type sinp = type(2) * (w * y - z * x);
		if (std::abs(sinp) >= type(1))
			r[1] = std::copysign(xhqm::number<type>::pi / type(2), sinp);	//pitch ±90°
		else
			r[1] = std::asin(sinp);
		r[0] = std::atan2(type(2) * (w * x + y * z), type(1) - type(2) * (x * x + y * y));	//roll
		r[2] = std::atan2(type(2) * (w * z + x * y), type(1) - type(2) * (y * y + z * z));	//yaw
		return r;
	}

	//—— 球面插值 ——
	template<typename type>
	quaternion<type> quaternion<type>::slerp(const quaternion& a, const quaternion& b, const type& t)
	{
		type cosom = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
		quaternion bb = b;
		if (cosom < type(0)) { cosom = -cosom; bb = quaternion(-b.x, -b.y, -b.z, -b.w); }
		type s0, s1;
		if (cosom > type(0.9995)) { s0 = type(1) - t; s1 = t; }
		else
		{
			type omega = std::acos(cosom);
			type sinom = std::sin(omega);
			s0 = std::sin((type(1) - t) * omega) / sinom;
			s1 = std::sin(t * omega) / sinom;
		}
		return quaternion(s0 * a.x + s1 * bb.x, s0 * a.y + s1 * bb.y, s0 * a.z + s1 * bb.z, s0 * a.w + s1 * bb.w);
	}

	//—— 基本运算（mathf 模式）——
	template<typename type> inline quaternion<type> sum(const quaternion<type>& left, const quaternion<type>& right) { return left + right; }
	template<typename type> inline quaternion<type> sub(const quaternion<type>& left, const quaternion<type>& right) { return left - right; }
	template<typename type> inline quaternion<type> mul(const quaternion<type>& left, const quaternion<type>& right) { return left * right; }	//Hamilton 积
	template<typename type> inline type dot(const quaternion<type>& left, const quaternion<type>& right) { return quaternion<type>::dot(left, right); }	//四维标量点积
	template<typename type> inline quaternion<type> div(const quaternion<type>& left, const quaternion<type>& right) { return left * right.inverse(); }
	template<typename type> inline type abs(const quaternion<type>& value) { return value.norm(); }
	template<typename type> inline bool equal(const quaternion<type>& left, const quaternion<type>& right) { return left.x == right.x && left.y == right.y && left.z == right.z && left.w == right.w; }
	template<typename type> inline bool isnan(const quaternion<type>& value) { return std::isnan(value.x) || std::isnan(value.y) || std::isnan(value.z) || std::isnan(value.w); }
	template<typename type> inline bool isinf(const quaternion<type>& value) { return std::isinf(value.x) || std::isinf(value.y) || std::isinf(value.z) || std::isinf(value.w); }

	//—— 序列化（格式：x,y,z,w）——
	template<> inline std::string save(const quaternion<xhqm::f32>& value) { return std::to_string(value.x) + "," + std::to_string(value.y) + "," + std::to_string(value.z) + "," + std::to_string(value.w); }
	template<> inline std::string save(const quaternion<xhqm::f64>& value) { return std::to_string(value.x) + "," + std::to_string(value.y) + "," + std::to_string(value.z) + "," + std::to_string(value.w); }
	template<> inline std::string save(const quaternion<xhqm::fld>& value) { return std::to_string(value.x) + "," + std::to_string(value.y) + "," + std::to_string(value.z) + "," + std::to_string(value.w); }

	template<typename type>
	inline quaternion<type> read_quaternion(const std::string& value)
	{
		std::array<type, 4> v = { type(), type(), type(), type() };
		xhqm::index start = 0;
		for (xhqm::index i = 0; i < 4; ++i)
		{
			xhqm::index comma = value.find_first_of(',', start);
			std::stringstream ss(value.substr(start, comma - start));
			ss >> v[i];
			start = comma + 1;
		}
		return quaternion<type>(v[0], v[1], v[2], v[3]);
	}
	template<> inline quaternion<xhqm::f32> read(const std::string& value) { return xhqm::read_quaternion<xhqm::f32>(value); }
	template<> inline quaternion<xhqm::f64> read(const std::string& value) { return xhqm::read_quaternion<xhqm::f64>(value); }
	template<> inline quaternion<xhqm::fld> read(const std::string& value) { return xhqm::read_quaternion<xhqm::fld>(value); }
}

#endif
