#include "test.h"

#include "../fantasy/drawer.h"
#include "../fantasy/backend/ubo.h"
#include "../fantasy/backend/shader_sources.h"
#include "../fantasy/backend/shader_preprocess.h"

#include <string>

using namespace xhqm;

// 辅助：判断源码中是否还有未展开的 XHQM_ 宏
static bool has_xhqm_macro_left(const std::string& src)
{
	return src.find("XHQM_") != std::string::npos;
}

TEST_CASE("toon: ubo布局与偏移一致")
{
	// ubo_offset 名称映射与常量一致
	CHECK(fantasy::ubo_offset("materialToon") == fantasy::UBO_MATERIAL_TOON);
	CHECK(fantasy::ubo_offset("toonRimColor") == fantasy::UBO_TOON_RIM);
	// 紧随 materialFlags 之后，instances 之前
	CHECK(fantasy::UBO_MATERIAL_TOON == fantasy::UBO_MATERIAL_FLAGS + 16);
	CHECK(fantasy::UBO_TOON_RIM == fantasy::UBO_MATERIAL_TOON + 16);
	CHECK(fantasy::UBO_INSTANCES == fantasy::UBO_TOON_RIM + 16);

	// GLSL 布局文本包含两个新字段（GL/VK 单一事实来源）
	const std::string glsl = fantasy::ubo_glsl(false);
	CHECK(glsl.find("materialToon") != std::string::npos);
	CHECK(glsl.find("toonRimColor") != std::string::npos);
	CHECK(glsl.find("materialFlags") < glsl.find("materialToon"));
	CHECK(glsl.find("materialToon") < glsl.find("toonRimColor"));
	CHECK(glsl.find("toonRimColor") < glsl.find("instances"));
}

TEST_CASE("toon: GL 预处理产物")
{
	const std::string vert = fantasy::shader_preprocess(fantasy::shader_sources::toon_vert(), false, false);
	const std::string frag = fantasy::shader_preprocess(fantasy::shader_sources::toon_frag(), false, false);

	// GLSL 330 版本头
	CHECK(vert.find("#version 330 core") != std::string::npos);
	CHECK(frag.find("#version 330 core") != std::string::npos);

	// 顶点与 prefab 共用（蒙皮/形变/实例化兼容）
	CHECK(vert.find("instances") != std::string::npos);

	// 卡通光照已展开：ramp 量化 + 边缘光 + UBO 字段
	CHECK(frag.find("materialToon") != std::string::npos);
	CHECK(frag.find("toonRimColor") != std::string::npos);
	CHECK(frag.find("smoothstep") != std::string::npos);

	// 无残留宏
	CHECK_FALSE(has_xhqm_macro_left(vert));
	CHECK_FALSE(has_xhqm_macro_left(frag));
}

TEST_CASE("toon: Vulkan 预处理产物")
{
	const std::string frag = fantasy::shader_preprocess(fantasy::shader_sources::toon_frag(), true, false);
	CHECK(frag.find("#version 450") != std::string::npos);
	CHECK(frag.find("materialToon") != std::string::npos);
	CHECK_FALSE(has_xhqm_macro_left(frag));
}

TEST_CASE("toon: AppleGL 简化光照")
{
	const std::string frag = fantasy::shader_preprocess(fantasy::shader_sources::toon_frag(), false, true);
	// 简化版仍是合法 GLSL 330，含 ramp 量化
	CHECK(frag.find("#version 330 core") != std::string::npos);
	CHECK(frag.find("smoothstep") != std::string::npos);
	CHECK_FALSE(has_xhqm_macro_left(frag));
}

TEST_CASE("toon: 材质参数默认值")
{
	fantasy::material_desc mat;
	CHECK_NEAR(mat.ramp_threshold, 0.5, 1e-6);
	CHECK_NEAR(mat.ramp_softness, 0.08, 1e-6);
	CHECK_NEAR(mat.rim_power, 3.0, 1e-6);
	CHECK_NEAR(mat.spec_threshold, 0.0, 1e-6);
	CHECK(mat.rim_color == 0x00000000);
}

TEST_CASE("toon: toon_prefab 接线")
{
	fantasy::toon_prefab toon;
	// 无渲染环境下 shader() 走 none 后端也应安全（编译延迟到真实后端，id 为 0 属预期）
	toon.shader();

	fantasy::toon_prefab other;
	other.shader();
	// 静态共享：所有 toon_prefab 持有同一 shader 对象
	CHECK(toon.shader_id().share_ptr == other.shader_id().share_ptr);

	// 未调 shader() 的实例持有独立空 shader，接线后才挂上共享 shader
	fantasy::toon_prefab plain;
	CHECK(plain.shader_id().share_ptr != toon.shader_id().share_ptr);
}
