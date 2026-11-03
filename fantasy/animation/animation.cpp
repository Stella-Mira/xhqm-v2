#include "animation.h"
#include <algorithm>
#include <cmath>

namespace xhqm
{
	namespace fantasy
	{
		// 四元数球面插值（M9：替换矩阵线性插值，混合不塌缩）
		quat quat::slerp(const quat& a, const quat& b, value t)
		{
			value cosom = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
			quat bb = b;
			if (cosom < 0) { cosom = -cosom; bb = quat(-b.x, -b.y, -b.z, -b.w); }
			value s0, s1;
			if (cosom > 0.9995f) { s0 = 1 - t; s1 = t; }
			else
			{
				value omega = std::acos(cosom);
				value sinom = std::sin(omega);
				s0 = std::sin((1 - t) * omega) / sinom;
				s1 = std::sin(t * omega) / sinom;
			}
			return quat(s0 * a.x + s1 * bb.x, s0 * a.y + s1 * bb.y, s0 * a.z + s1 * bb.z, s0 * a.w + s1 * bb.w);
		}

		quat quat::from_matrix(const matrix& m)
		{
			value trace = m(0, 0) + m(1, 1) + m(2, 2);
			quat q;
			if (trace > 0)
			{
				value s = std::sqrt(trace + 1) * 2;
				q.w = 0.25f * s;
				q.x = (m(2, 1) - m(1, 2)) / s;
				q.y = (m(0, 2) - m(2, 0)) / s;
				q.z = (m(1, 0) - m(0, 1)) / s;
			}
			else if (m(0, 0) > m(1, 1) && m(0, 0) > m(2, 2))
			{
				value s = std::sqrt(1 + m(0, 0) - m(1, 1) - m(2, 2)) * 2;
				q.w = (m(2, 1) - m(1, 2)) / s;
				q.x = 0.25f * s;
				q.y = (m(0, 1) + m(1, 0)) / s;
				q.z = (m(0, 2) + m(2, 0)) / s;
			}
			else if (m(1, 1) > m(2, 2))
			{
				value s = std::sqrt(1 + m(1, 1) - m(0, 0) - m(2, 2)) * 2;
				q.w = (m(0, 2) - m(2, 0)) / s;
				q.x = (m(0, 1) + m(1, 0)) / s;
				q.y = 0.25f * s;
				q.z = (m(1, 2) + m(2, 1)) / s;
			}
			else
			{
				value s = std::sqrt(1 + m(2, 2) - m(0, 0) - m(1, 1)) * 2;
				q.w = (m(1, 0) - m(0, 1)) / s;
				q.x = (m(0, 2) + m(2, 0)) / s;
				q.y = (m(1, 2) + m(2, 1)) / s;
				q.z = 0.25f * s;
			}
			return q;
		}

		matrix quat::to_matrix() const
		{
			value xx = x * x, yy = y * y, zz = z * z;
			value xy = x * y, xz = x * z, yz = y * z, wx = w * x, wy = w * y, wz = w * z;
			matrix r(4, 4);
			r(0, 0) = 1 - 2 * (yy + zz); r(0, 1) = 2 * (xy - wz);     r(0, 2) = 2 * (xz + wy);     r(0, 3) = 0;
			r(1, 0) = 2 * (xy + wz);     r(1, 1) = 1 - 2 * (xx + zz); r(1, 2) = 2 * (yz - wx);     r(1, 3) = 0;
			r(2, 0) = 2 * (xz - wy);     r(2, 1) = 2 * (yz + wx);     r(2, 2) = 1 - 2 * (xx + yy); r(2, 3) = 0;
			r(3, 0) = 0; r(3, 1) = 0; r(3, 2) = 0; r(3, 3) = 1;
			return r;
		}

		struct animation_clip::channel
		{
			int bone = -1;
			std::vector<float> times;
			std::vector<vect<value>> positions;
			std::vector<vect<value>> rotations;
			std::vector<vect<value>> scales;
		};

		void animation_clip::set_channel(int bone,
			const std::vector<float>& times,
			const std::vector<vect<value>>& positions,
			const std::vector<vect<value>>& rotations,
			const std::vector<vect<value>>& scales)
		{
			if (times.empty() || positions.size() != times.size()) return;
			channel c;
			c.bone = bone;
			c.times = times;
			c.positions = positions;
			c.rotations = rotations;
			if (c.rotations.size() != times.size()) c.rotations.resize(times.size(), vect<value>());
			c.scales = scales;
			if (c.scales.size() != times.size()) c.scales.resize(times.size(), vect<value>(1, 1, 1));
			m_channels.push_back(std::move(c));
			if (times.back() > m_duration) m_duration = times.back();
		}

		// 线性插值（单分量辅助）
		static value lerp(value a, value b, value t) { return a + (b - a) * t; }

		void animation_clip::sample(float time, skeleton& skel) const
		{
			for (const channel& c : m_channels)
			{
				const auto& ts = c.times;
				if (ts.empty()) continue;
				// 找区间
				int idx = 0;
				if (time <= ts.front()) idx = 0;
				else if (time >= ts.back()) idx = ((int)ts.size() >= 2) ? (int)ts.size() - 2 : 0;
				else
				{
					for (size_t i = 0; i + 1 < ts.size(); ++i)
					{
						if (time >= ts[i] && time <= ts[i + 1]) { idx = (int)i; break; }
					}
				}
				int idx1 = std::min(idx + 1, (int)ts.size() - 1);
				value t = 0;
				if (ts[idx1] > ts[idx]) t = (time - ts[idx]) / (ts[idx1] - ts[idx]);

				// 插值位置/旋转/缩放
				const vect<value>& p0 = c.positions[idx];
				const vect<value>& p1 = c.positions[idx1];
				vect<value> pos(lerp(p0.x(), p1.x(), t), lerp(p0.y(), p1.y(), t), lerp(p0.z(), p1.z(), t));

				const vect<value>& r0 = c.rotations[idx];
				const vect<value>& r1 = c.rotations[idx1];
				vect<value> rot(lerp(r0.x(), r1.x(), t), lerp(r0.y(), r1.y(), t), lerp(r0.z(), r1.z(), t));

				const vect<value>& s0 = c.scales[idx];
				const vect<value>& s1 = c.scales[idx1];
				vect<value> scl(lerp(s0.x(), s1.x(), t), lerp(s0.y(), s1.y(), t), lerp(s0.z(), s1.z(), t));

				// 构建局部姿态：translate * rotate_z * rotate_y * rotate_x * scale
				matrix local = xhqm::translate(pos.x(), pos.y(), pos.z())
					* xhqm::rotate_z(rot.z()) * xhqm::rotate_y(rot.y()) * xhqm::rotate_x(rot.x())
					* xhqm::scale(scl.x(), scl.y(), scl.z());
				skel.set_local_pose(c.bone, local);
			}
		}

		// 矩阵逐元素线性插值
		static matrix lerp_matrix(const matrix& a, const matrix& b, value t)
		{
			matrix r(4, 4);
			for (int i = 0; i < 4; ++i)
				for (int j = 0; j < 4; ++j)
					r(i, j) = a(i, j) + (b(i, j) - a(i, j)) * t;
			return r;
		}

		// 提取矩阵 x/y/z 缩放（旋转部分的列向量长度）
		static vect<value> matrix_scale(const matrix& m)
		{
			return vect<value>(
				std::sqrt(m(0, 0) * m(0, 0) + m(1, 0) * m(1, 0) + m(2, 0) * m(2, 0)),
				std::sqrt(m(0, 1) * m(0, 1) + m(1, 1) * m(1, 1) + m(2, 1) * m(2, 1)),
				std::sqrt(m(0, 2) * m(0, 2) + m(1, 2) * m(1, 2) + m(2, 2) * m(2, 2)));
		}

		void animation_blender::blend(const animation_clip& a, float time_a,
			const animation_clip& b, float time_b,
			float weight, skeleton& out)
		{
			// 采样两个 clip 到临时 skeleton
			skeleton sa = out;
			skeleton sb = out;
			a.sample(time_a, sa);
			b.sample(time_b, sb);

			int n = out.bone_count();
			for (int i = 0; i < n; ++i)
			{
				matrix ma = sa.get_local_pose(i);
				matrix mb = sb.get_local_pose(i);

				// 提取缩放并去缩放，得到纯旋转矩阵（M9 补：缩放线性混合，不丢失）
				vect<value> sa_s = matrix_scale(ma);
				vect<value> sb_s = matrix_scale(mb);
				matrix na = ma, nb = mb;
				for (int c = 0; c < 3; ++c)
				{
					value ia = (sa_s[c] > 1e-8f) ? 1.0f / sa_s[c] : 1.0f;
					value ib = (sb_s[c] > 1e-8f) ? 1.0f / sb_s[c] : 1.0f;
					for (int r = 0; r < 3; ++r) { na(r, c) *= ia; nb(r, c) *= ib; }
				}

				// 旋转 slerp、缩放/平移线性
				quat qm = quat::slerp(quat::from_matrix(na), quat::from_matrix(nb), weight);
				matrix m = qm.to_matrix();
				value sx = lerp(sa_s[0], sb_s[0], weight);
				value sy = lerp(sa_s[1], sb_s[1], weight);
				value sz = lerp(sa_s[2], sb_s[2], weight);
				for (int r = 0; r < 3; ++r)
				{
					m(r, 0) *= sx;
					m(r, 1) *= sy;
					m(r, 2) *= sz;
				}
				m(0, 3) = lerp(ma(0, 3), mb(0, 3), weight);
				m(1, 3) = lerp(ma(1, 3), mb(1, 3), weight);
				m(2, 3) = lerp(ma(2, 3), mb(2, 3), weight);
				out.set_local_pose(i, m);
			}
		}
	}
}
