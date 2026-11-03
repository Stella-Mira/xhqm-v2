#pragma once
#ifndef XHQM_FANTASY_ANIMATION
#define XHQM_FANTASY_ANIMATION

// 动画 clip：关键帧 + 线性插值采样，驱动 skeleton 的局部姿态
#include "fantasy.h"
#include "skeleton.h"

namespace xhqm
{
	namespace fantasy
	{
		// 四元数（动画混合用，M9：slerp 替换矩阵线性插值，避免体积塌缩）
		struct quat
		{
			value x = 0, y = 0, z = 0, w = 1;
			quat() = default;
			quat(value x_, value y_, value z_, value w_) : x(x_), y(y_), z(z_), w(w_) {}

			static quat slerp(const quat& a, const quat& b, value t);
			static quat from_matrix(const matrix& m);
			matrix to_matrix() const;
		};

		class animation_clip
		{
			struct channel;
			std::vector<channel> m_channels;
			float m_duration = 0.0f;
		public:
			animation_clip() = default;

			// 设置一个骨骼通道的关键帧（times 升序，positions/rotations/scales 与 times 等长）
			// rotations 为欧拉角（弧度，x/y/z 顺序）
			void set_channel(int bone,
				const std::vector<float>& times,
				const std::vector<vect<value>>& positions,
				const std::vector<vect<value>>& rotations,
				const std::vector<vect<value>>& scales);

			float duration() const { return m_duration; }

			// 采样：把 time 时刻的各骨骼局部姿态写入 skeleton（调用方随后 compute_palette）
			void sample(float time, skeleton& skel) const;
		};

		// 动画混合：两个 clip 的采样按权重混合（矩阵线性插值，weight∈[0,1]）
		class animation_blender
		{
		public:
			// out 需已建好骨骼结构；混合局部姿态写入 out
			static void blend(const animation_clip& a, float time_a,
				const animation_clip& b, float time_b,
				float weight, skeleton& out);
		};
	}
}

#endif
