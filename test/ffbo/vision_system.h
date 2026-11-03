// 视觉子系统（FAFB 光感受器 → 小眼 → raycast 采样 → ON/OFF 通路 → 注入 LIF）
//
// 数据依据（全部实测核对）：
//   visual_neuron_types.csv.gz：R1-6=7172 / R7=1254 / R8=1280，带 side；
//     lamina 单极细胞 L1-L5 带功能标注（L1/L5=ON、L4=OFF、L3=Luminance）
//   coordinates.csv.gz：每神经元 3D 坐标（nm）。R1-6 按 10um 体素聚类得
//     left 798 / right 792 个小眼，对上真实果蝇 ~780 小眼/眼
//
// 坐标系：FAFB 解剖系 x=前后 y=左右 z=上下；MuJoCo head 局部系 x=左右 y=前后 z=上下
//   （实测 head 局部 x 轴在世界≈(0,±1,0)，即局部 x 对应左右）
//   解剖系 F → 头部局部系 L：L.x=-F.y、L.y=-F.x、L.z=F.z
#pragma once
#ifndef XHQM_TEST_FFBO_VISION_SYSTEM_H
#define XHQM_TEST_FFBO_VISION_SYSTEM_H

#include "../../head.h"

#include <mujoco/mujoco.h>

#include <cstdint>
#include <unordered_map>
#include <string>
#include <unordered_map>
#include <vector>

struct Vec3v { double x = 0, y = 0, z = 0; };

struct ommatidium
{
	Vec3v dir_local;                     // 视线方向（MuJoCo 头部局部系，单位向量）
	std::vector<xhqm::size> pr;          // 该小眼的 R1-6 在 FAFB 网络中的索引
};

// 柱状单元：一个小眼 + 与之空间对应的 lamina 细胞（按坐标就近配对）
struct cartridge
{
	std::size_t om = 0;                  // 指向 eye_model::om
	std::vector<xhqm::size> lam;         // 该柱的 lamina 单极细胞索引
	int kind = 0;                        // 0=L2 1=ON(L1/L5) 2=OFF(L4) 3=L3
};

struct eye_model
{
	std::string side;
	std::vector<ommatidium> om;
	std::vector<cartridge> cart;
	std::vector<double> dirs, dist, nrm;
	std::vector<int> geom;
	std::vector<float> lum, lum_prev;
	Vec3v center_anat;                   // 复眼中心（解剖系 nm）
};

struct vis_cell
{
	std::string type, family, subsystem, side;
};

struct vision_system
{
	std::unordered_map<std::string, vis_cell> cell;
	std::unordered_map<std::string, Vec3v> coord;
	eye_model eye[2];

	std::vector<xhqm::size> color_pr;    // R7/R8 颜色光感受器
	// R1-6 按左右分组：用于给嗅觉信息编码侧向（这一群体带 side 标注，可靠）
	std::vector<xhqm::size> pr_left, pr_right;
	float odor_side_in = 0.0f;        // 气味相对头部的侧向（>0 左），由主程序每帧写入
	float odor_side_gain = 1000.0f;   // 侧向编码增益：8e4 时放电差已饱和(3733/全群一半)，
	                                  // 降到 1000 并靠扫描找线性区（目标 5~20% 全群）
	float neur_side_left = 0.0f, neur_side_right = 0.0f;   // 侧向在光感受器上造成的活动量
	// lamina 单极细胞按功能分组（ON/OFF/Luminance/L2），供下游注入与统计
	std::vector<xhqm::size> lam_on, lam_off, lam_lum, lam_l2;
	int n_ommatidia = 0;
	bool ready = false;

	// 视觉更新降频：raycast 1610 条小眼射线约 +40ms/帧（实测：无视觉 47fps → 有视觉 18fps），
	// 故默认每 4 帧采样一次。时序差分（ON/OFF）在 5~10Hz 下仍然有效，
	// 因为 T4/T5 检测的是小眼亮度随时间的变化，不需要与物理同频。
	int vis_every = 4;
	long long frame_no = 0;

	// 本帧视觉活动（诊断用）
	float last_on = 0.0f, last_off = 0.0f, sum_lum = 0.0f;
	double t_ray = 0, t_inj = 0, t_cart = 0, t_total = 0;   // 分层计时（累加秒）
	std::size_t n_cart = 0;

	// 增益标定：LIF 阈值 0.85~1.15、dt=1ms、tau=10ms，故每步需 ~0.1 的电流增量才显著。
	// 亮度 L∈[0.15,1.0]，gain_pr=60 时注入仅 9~60（每步 0.009~0.06），实测光感受器几乎不发放。
	float gain_pr = 250.0f;              // 光感受器亮度 → 电流
	float gain_on = 600.0f;              // ON/OFF 通路（时序差分）增益
	float gain_lum = 300.0f;             // Luminance 通路
};

bool vision_import(vision_system& vs, const char* vis_csv, const char* coord_csv);
void vision_build(vision_system& vs, const std::unordered_map<std::uint64_t, xhqm::size>& fafb_id2idx);
void vision_step(vision_system& vs, mjModel* m, mjData* d, int head_body, std::vector<float>& cur);

#endif
