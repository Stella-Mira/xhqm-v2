#pragma once
#ifndef XHQM_FANTASY_SKELETON
#define XHQM_FANTASY_SKELETON

// 骨骼层级树：管理骨骼父子关系与姿态，计算蒙皮调色板
#include "fantasy.h"

namespace xhqm
{
	namespace fantasy
	{
		class skeleton
		{
			struct bone_node
			{
				std::string name;
				matrix bind_pose = matrix(4, 4);   // 绑定姿态（模型空间）
				matrix local_pose = matrix(4, 4);  // 局部姿态（相对父骨骼）
				int parent = -1;
				std::vector<int> children;
			};
			std::vector<bone_node> m_bones;
		private:
			// DFS 计算世界姿态（不依赖骨骼索引顺序，父先于子）
			std::vector<matrix> compute_world() const;
		public:
			skeleton() = default;

			// 添加骨骼，返回骨骼索引；parent 为父骨骼索引（-1 表示根）
			int add_bone(const std::string& name, int parent);
			int bone_count() const { return (int)m_bones.size(); }

			// 绑定姿态（模型空间，蒙皮时的逆矩阵用）
			void set_bind_pose(int bone, const matrix& m);
			// 局部姿态（相对父骨骼）
			void set_local_pose(int bone, const matrix& m);
			// 读取局部姿态（动画混合用）
			const matrix& get_local_pose(int bone) const;
			const matrix& get_bind_pose(int bone) const;

			// 计算蒙皮调色板：palette[i] = world_pose[i] * inv(bind_pose[i])
			// 列向量约定（与着色器 skin * pos 一致）
			std::vector<matrix> compute_palette() const;
			// 计算世界姿态（world = parent_world * local）
			std::vector<matrix> compute_world_poses() const;
		};
	}
}

#endif
