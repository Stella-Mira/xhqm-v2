#ifndef XHQM_TEST
#define XHQM_TEST

// Catch2 v2.13.10 单头测试框架（test/catch.hpp）
// 自研轻量框架迁移而来：CHECK 直接用 Catch2 原生宏（表达式分解），
// CHECK_NEAR 保持原绝对误差语义 std::fabs(a-b) <= eps。
#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#define CHECK_NEAR(a, b, eps) \
	do { \
		double _xhqm_a = (double)(a); \
		double _xhqm_b = (double)(b); \
		CHECK(std::fabs(_xhqm_a - _xhqm_b) <= (double)(eps)); \
	} while (0)

#endif
