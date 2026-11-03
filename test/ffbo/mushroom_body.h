// 蘑菇体多巴胺调制可塑性（果蝇嗅觉联想学习的真实机制）
//
// 生物学依据（数据实测）：
//   KC(5177) → MBON(96)：169,931 突触 / 38,915 对 —— 学习发生处
//   PAM(307) → KC/MBON：奖励型多巴胺（PAM01~15）
//   PPL(24)  → KC/MBON：惩罚型多巴胺（PPL1xx/2xx）
//   ALPN → KC：316,862 突触（嗅觉输入）
//   ORN 2,281 个按嗅小球分类（ORN_DA1/VA1d/...）→ 气味通道
//
// 学习规则（三因子 Hebb，果蝇经典模型）：
//   1) 条件刺激 CS：气味激活 ORN → ALPN → KC 发放
//   2) KC 发放时在 KC→MBON 突触上留下 eligibility 痕迹（指数衰减）
//   3) 无条件刺激 US：PAM（奖励）/ PPL（惩罚）发放作为多巴胺信号 DA
//   4) 痕迹与 DA 重合时改权重：w += lr * trace * DA
//      奖励 → 增强 KC→MBON（该气味更吸引）；惩罚 → 减弱（该气味更回避）
//
// 只作用在 KC→MBON 这个子图上（38,915 条边），不动 139k 全局网络。
#pragma once
#ifndef XHQM_TEST_FFBO_MUSHROOM_BODY_H
#define XHQM_TEST_FFBO_MUSHROOM_BODY_H

#include "../../head.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// 一条可塑突触：在 FAFB 脑网络的 CSR 中的位置 + 基础权重（可塑部分叠加其上）
struct plastic_edge
{
	xhqm::size k = 0;      // 在 netcsr::val 中的下标（即该边的权重位置）
	xhqm::size pre = 0;    // KC 索引（用于查 eligibility 痕迹）
	float base = 0.0f;     // 突触前归一化后的基础权重
	float trace = 0.0f;    // eligibility 痕迹（KC 发放时置 1，指数衰减）
};

struct mushroom_body
{
	// 群体索引
	std::vector<xhqm::size> kc, mbon, pam, ppl, alpn, orn;
	// ORN 按嗅小球通道分组（primary_type 如 ORN_DA1）
	std::vector<std::string> glomeruli;
	std::vector<std::vector<xhqm::size>> glom_orn;     // 每个嗅小球 → ORN 索引
	std::unordered_map<std::string, int> glom_id;

	// 可塑突触（KC→MBON）
	std::vector<plastic_edge> edges;
	std::vector<std::vector<xhqm::size>> kc_out;       // KC 索引 → 它在 edges 中的下标
	std::vector<float> plastic;    // 每条可塑突触的累积可塑量（相对 base 的乘性偏移）

	// 学习参数（可调）
	float lr = 0.02f;              // 学习率
	float tau_trace = 1.0f;        // 痕迹时间常数（秒）
	float w_min = 0.0f;            // 可塑部分下限
	float w_max = 3.0f;            // 可塑部分上限（相对 base 的倍数）
	float w_decay = 0.001f;        // 同源性衰减（防饱和）

	// 运行时信号（外部写入）
	float da_reward = 0.0f;        // PAM 活动 → 奖励信号
	float da_punish = 0.0f;        // PPL 活动 → 惩罚信号
	float odor_on = 0.0f;          // 当前气味浓度（0~1）

	// 统计
	std::size_t n_edges = 0;
	double w_delta_total = 0.0;    // 累计权重变化（观测学习是否发生）
	long long n_updates = 0;
	std::vector<float> mbon_readout;   // MBON 活动读出（趋避指标）

	bool ready = false;
	std::string info;
};

// 从 FAFB 的 cell_types / classification 建索引；id2idx 是 root_id → 网络索引
bool mb_import(mushroom_body& mb, const char* cell_types_csv, const char* classification_csv,
	const std::unordered_map<std::uint64_t, xhqm::size>& fafb_id2idx);

// 构建可塑突触表（遍历脑网络的 CSR，挑出 KC→MBON 边）
void mb_build_edges(mushroom_body& mb,
	const std::vector<xhqm::size>& csr_rp, const std::vector<xhqm::size>& csr_col);

// 每帧调用：更新痕迹 + 按 DA 改权重（直接写 CSR 权重数组）
void mb_step(mushroom_body& mb, std::vector<float>& csr_val, const std::vector<float>& spike,
	float dt, float reward, float punish);

#endif
