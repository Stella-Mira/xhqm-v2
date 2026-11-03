#pragma once
#ifndef XHQM_FANTASY_IK
#define XHQM_FANTASY_IK

// 逆向运动学（IK）：CCD 迭代求解，使骨骼链末端到达目标
#include "fantasy.h"
#include "skeleton.h"

namespace xhqm
{
	namespace fantasy
	{
		class ik_solver
		{
		public:
			// CCD 迭代：chain 是从根到末端的骨骼索引序列（升序），target 是世界空间目标
			// 每轮从末端前一个关节往根回退，绕关节旋转使末端逼近目标
			static void solve_ccd(skeleton& skel, const std::vector<int>& chain, const point& target, int iterations = 10);
		};
	}
}

#endif
