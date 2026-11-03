#ifndef XHQM_ANALYSIS_AXIS
#define XHQM_ANALYSIS_AXIS

// analysis 枚举：替代 MATLAB 的 1 基 dim 与字符串/魔法数参数。
// 0 基 + 强类型枚举，语义清晰，避免「sum(A,1) 沿列」这类历史包袱。

namespace xhqm
{
	namespace analysis
	{
		// 归约 / 操作方向（0 基）
		enum class axis : int
		{
			rows = 0,   // 沿行方向归约（每列归约），≈ MATLAB dim=1
			cols = 1,   // 沿列方向归约（每行归约），≈ MATLAB dim=2
		};

		// 范数类型
		enum class norm_type : int
		{
			fro = 0,       // Frobenius：sqrt(Σ|a_ij|²)
			one = 1,       // 1 范数：最大列和
			spectral = 2,  // 谱范数：最大奇异值（默认）
			inf = 3,       // 无穷范数：最大行和
		};

		// 方差 / 标准差归一化
		enum class variance : int
		{
			sample = 0,      // N-1（无偏估计，默认）
			population = 1,  // N（总体）
		};

		// 排序方向
		enum class sort_order : int
		{
			ascend = 0,
			descend = 1,
		};
	}
}

#endif
