#include "ik.h"
#include <cmath>

namespace xhqm
{
	namespace fantasy
	{
		// 从矩阵提取平移（列向量约定，平移在第 3 列）
		static point matrix_pos(const matrix& m)
		{
			return point(m(0, 3), m(1, 3), m(2, 3));
		}

		static vector cross(const vector& a, const vector& b)
		{
			return vector(
				a.y() * b.z() - a.z() * b.y(),
				a.z() * b.x() - a.x() * b.z(),
				a.x() * b.y() - a.y() * b.x());
		}

		static value angle_between(const vector& a, const vector& b)
		{
			value d = a.length() * b.length();
			if (d < 1e-8f) return 0;
			value c = (a.x() * b.x() + a.y() * b.y() + a.z() * b.z()) / d;
			c = (c > 1.0f) ? 1.0f : (c < -1.0f) ? -1.0f : c;
			return std::acos(c);
		}

		void ik_solver::solve_ccd(skeleton& skel, const std::vector<int>& chain, const point& target, int iterations)
		{
			if (chain.size() < 2) return;
			int end = chain.back();

			for (int iter = 0; iter < iterations; ++iter)
			{
				// 从末端前一个关节向根回退
				for (int i = (int)chain.size() - 2; i >= 0; --i)
				{
					int joint = chain[i];
					std::vector<matrix> world = skel.compute_world_poses();
					point joint_pos = matrix_pos(world[joint]);
					point end_pos = matrix_pos(world[end]);

					vector j2e = end_pos - joint_pos;
					vector j2t = target - joint_pos;
					if (j2e.length() < 1e-8f || j2t.length() < 1e-8f) continue;

					value angle = angle_between(j2e, j2t);
					if (angle < 1e-5f) continue;

					vector axis = cross(j2e, j2t);
					if (axis.length() < 1e-8f) continue;
					axis = axis.normal();

					// 绕关节旋转（局部空间左乘）
					matrix rot = xhqm::rotate_v(axis, angle);
					skel.set_local_pose(joint, rot * skel.get_local_pose(joint));
				}
			}
		}
	}
}
