#include "skeleton.h"
#include <functional>

namespace xhqm
{
	namespace fantasy
	{
		int skeleton::add_bone(const std::string& name, int parent)
		{
			bone_node b;
			b.name = name;
			b.parent = parent;
			int idx = (int)m_bones.size();
			if (parent >= 0 && parent < (int)m_bones.size())
			{
				m_bones[parent].children.push_back(idx);
			}
			m_bones.push_back(std::move(b));
			return idx;
		}

		void skeleton::set_bind_pose(int bone, const matrix& m)
		{
			if (bone >= 0 && bone < (int)m_bones.size()) m_bones[bone].bind_pose = m;
		}

		void skeleton::set_local_pose(int bone, const matrix& m)
		{
			if (bone >= 0 && bone < (int)m_bones.size()) m_bones[bone].local_pose = m;
		}

		const matrix& skeleton::get_local_pose(int bone) const
		{
			static const matrix identity = matrix(4, 4);
			if (bone >= 0 && bone < (int)m_bones.size()) return m_bones[bone].local_pose;
			return identity;
		}

		const matrix& skeleton::get_bind_pose(int bone) const
		{
			static const matrix identity = matrix(4, 4);
			if (bone >= 0 && bone < (int)m_bones.size()) return m_bones[bone].bind_pose;
			return identity;
		}

		std::vector<matrix> skeleton::compute_world() const
		{
			std::vector<matrix> world(m_bones.size(), matrix(4, 4));
			std::vector<char> visited(m_bones.size(), 0);
			// 从每个根骨骼（parent < 0）出发 DFS，父先于子，不依赖骨骼索引顺序（父<子）
			std::function<void(int)> dfs = [&](int i)
			{
				if (i < 0 || i >= (int)m_bones.size() || visited[i]) return;
				visited[i] = 1;
				const bone_node& b = m_bones[i];
				if (b.parent >= 0 && b.parent < (int)m_bones.size())
					world[i] = world[b.parent] * b.local_pose;
				else
					world[i] = b.local_pose;
				for (int c : b.children) dfs(c);
			};
			for (size_t i = 0; i < m_bones.size(); ++i)
				if (m_bones[i].parent < 0) dfs((int)i);
			// 兜底：未被根触达的孤立骨骼（异常父子关系），补算避免留默认矩阵
			for (size_t i = 0; i < m_bones.size(); ++i)
				if (!visited[i]) dfs((int)i);
			return world;
		}

		std::vector<matrix> skeleton::compute_palette() const
		{
			std::vector<matrix> world = compute_world();
			std::vector<matrix> palette(m_bones.size(), matrix(4, 4));
			for (size_t i = 0; i < m_bones.size(); ++i)
			{
				// palette = world * inv(bind)
				palette[i] = world[i] * m_bones[i].bind_pose.invmat();
			}
			return palette;
		}

		std::vector<matrix> skeleton::compute_world_poses() const
		{
			return compute_world();
		}
	}
}
