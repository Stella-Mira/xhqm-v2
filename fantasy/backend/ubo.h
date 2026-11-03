#pragma once
#ifndef XHQM_FANTASY_UBO
#define XHQM_FANTASY_UBO

// 统一 UBO 布局（std140 字节偏移）。GL 与 Vulkan 后端共用这一份事实源，
// 消除 rhi_gl.cpp / rhi_vk.cpp 各自维护偏移导致的分叉。
// 与着色器 layout(std140) uniform UBO {...} ubo; 严格一致。
// 所有数组大小（flags/bones/lights/lightVP/instances）与字节偏移均由下方
// UBO_MAX_* 常量派生，改上限只动常量；ubo_glsl() 据此生成一致声明。
#include <string>
#include <string_view>

namespace xhqm
{
	namespace fantasy
	{
		constexpr unsigned int UBO_MAX_LIGHTS = 8;
		constexpr unsigned int UBO_MAX_CASCADES = 4;
		constexpr unsigned int UBO_MAX_FLAGS = 16;
		constexpr unsigned int UBO_MAX_BONES = 512;
		constexpr unsigned int UBO_MAX_INSTANCES = 64;

		constexpr unsigned int UBO_TRANSFORM = 0;
		constexpr unsigned int UBO_COLOR = 64;
		constexpr unsigned int UBO_SELECT = 80;
		constexpr unsigned int UBO_LIGHT_RGB = 96;
		constexpr unsigned int UBO_LIGHT_XYZ = 112;
		constexpr unsigned int UBO_FLAGS = 128;
		constexpr unsigned int UBO_BONES = 384;
		constexpr unsigned int UBO_WORLD = UBO_BONES + UBO_MAX_BONES * 64;                 // 33152
		constexpr unsigned int UBO_LIGHTS = UBO_WORLD + 64;                                // 33216
		constexpr unsigned int UBO_LIGHT_VP = UBO_LIGHTS + UBO_MAX_LIGHTS * 64;            // 33728
		constexpr unsigned int UBO_CASCADE_SPLITS = UBO_LIGHT_VP + UBO_MAX_CASCADES * 64;  // 33984
		constexpr unsigned int UBO_CAMERA_POS = UBO_CASCADE_SPLITS + 16;                   // 34000
		constexpr unsigned int UBO_COLOR_GRADE = UBO_CAMERA_POS + 16;                      // 34016
		constexpr unsigned int UBO_AMBIENT = UBO_COLOR_GRADE + 16;                         // 34032
		constexpr unsigned int UBO_MATERIAL_BASE = UBO_AMBIENT + 16;                       // 34048
		constexpr unsigned int UBO_MATERIAL_PBR = UBO_MATERIAL_BASE + 16;                  // 34064 (metallic, roughness, ao, alpha)
		constexpr unsigned int UBO_MATERIAL_EMISSIVE = UBO_MATERIAL_PBR + 16;              // 34080
		constexpr unsigned int UBO_MATERIAL_FLAGS = UBO_MATERIAL_EMISSIVE + 16;            // 34096 (double_sided)
		constexpr unsigned int UBO_MATERIAL_TOON = UBO_MATERIAL_FLAGS + 16;                // 34112 (ramp_threshold, ramp_softness, rim_power, spec_threshold)
		constexpr unsigned int UBO_TOON_RIM = UBO_MATERIAL_TOON + 16;                      // 34128 (rim_rgb, rim_intensity)
		constexpr unsigned int UBO_INSTANCES = UBO_TOON_RIM + 16;                          // 34144
		constexpr unsigned int UBO_SIZE = UBO_INSTANCES + UBO_MAX_INSTANCES * 64;          // 38240

		// flags[i] 的字节偏移 = UBO_FLAGS + i * 16（std140 int 数组步长 16）
		inline constexpr unsigned int ubo_flag(unsigned int i)
		{
			return UBO_FLAGS + i * 16;
		}

		// flags 语义下标（与着色器 int flags[16] 一致）
		constexpr int FLAG_USE_LIGHTING = 0;
		constexpr int FLAG_HAS_COLOUR = 1;
		constexpr int FLAG_HAS_NORMAL = 2;
		constexpr int FLAG_HAS_FLIPUV = 3;
		constexpr int FLAG_HAS_VALUES = 4;
		constexpr int FLAG_HAS_SKIN = 5;
		constexpr int FLAG_HAS_TEXTURE_0 = 6;
		constexpr int FLAG_HAS_TEXTURE_1 = 7;
		constexpr int FLAG_HAS_TEXTURE_2 = 8;
		constexpr int FLAG_HAS_BLEND = 9;
		constexpr int FLAG_BLEND_WEIGHT = 10;   // 以 float bits 存储，着色器 intBitsToFloat 还原
		constexpr int FLAG_LIGHT_COUNT = 11;
		constexpr int FLAG_SHADOW_ENABLED = 12;
		constexpr int FLAG_TONEMAP = 13;
		constexpr int FLAG_HAS_TANGENT = 14;
		constexpr int FLAG_HAS_INSTANCES = 15;

		// 上层 uniform 名 → UBO 字节偏移；未知名返回 -1（GL/Vulkan 共用同一映射）
		inline int ubo_offset(const char* name)
		{
			// string_view：不在热路径（每 draw 每 uniform 一次）构造 std::string，避免堆分配
			std::string_view n = name;
			if (n == "tranMatrix" || n == "params") return (int)UBO_TRANSFORM;
			if (n == "world") return (int)UBO_WORLD;
			if (n == "lights") return (int)UBO_LIGHTS;
			if (n == "lightVP") return (int)UBO_LIGHT_VP;
			if (n == "cascadeSplits") return (int)UBO_CASCADE_SPLITS;
			if (n == "cameraPos") return (int)UBO_CAMERA_POS;
			if (n == "pointSize") return (int)UBO_CAMERA_POS + 12;   // vec3 后的标量槽（std140）
			if (n == "colorGrade") return (int)UBO_COLOR_GRADE;
			if (n == "ambientColor") return (int)UBO_AMBIENT;
			if (n == "materialBase") return (int)UBO_MATERIAL_BASE;
			if (n == "materialPBR") return (int)UBO_MATERIAL_PBR;
			if (n == "materialEmissive") return (int)UBO_MATERIAL_EMISSIVE;
			if (n == "materialFlags") return (int)UBO_MATERIAL_FLAGS;
			if (n == "materialToon") return (int)UBO_MATERIAL_TOON;
			if (n == "toonRimColor") return (int)UBO_TOON_RIM;
			if (n == "lightCount") return (int)ubo_flag(FLAG_LIGHT_COUNT);
			if (n == "shadowEnabled") return (int)ubo_flag(FLAG_SHADOW_ENABLED);
			if (n == "tonemapMode") return (int)ubo_flag(FLAG_TONEMAP);
			if (n == "hasTangent") return (int)ubo_flag(FLAG_HAS_TANGENT);
			if (n == "hasInstances") return (int)ubo_flag(FLAG_HAS_INSTANCES);
			if (n == "instances") return (int)UBO_INSTANCES;
			if (n == "bones") return (int)UBO_BONES;
			if (n == "prefabColour" || n == "spriteColour") return (int)UBO_COLOR;
			if (n == "prefabSelect" || n == "spriteSelect" || n == "modelSelect" || n == "uuidSelect") return (int)UBO_SELECT;
			if (n == "lightRGB") return (int)UBO_LIGHT_RGB;
			if (n == "lightXYZ") return (int)UBO_LIGHT_XYZ;
			if (n == "useLighting") return (int)ubo_flag(FLAG_USE_LIGHTING);
			if (n == "hasColour") return (int)ubo_flag(FLAG_HAS_COLOUR);
			if (n == "hasNormal") return (int)ubo_flag(FLAG_HAS_NORMAL);
			if (n == "hasFlipuv") return (int)ubo_flag(FLAG_HAS_FLIPUV);
			if (n == "hasValues") return (int)ubo_flag(FLAG_HAS_VALUES);
			if (n == "hasSkin") return (int)ubo_flag(FLAG_HAS_SKIN);
			if (n == "hasTexture_0" || n == "hasTexture") return (int)ubo_flag(FLAG_HAS_TEXTURE_0);
			if (n == "hasTexture_1") return (int)ubo_flag(FLAG_HAS_TEXTURE_1);
			if (n == "hasTexture_2") return (int)ubo_flag(FLAG_HAS_TEXTURE_2);
			if (n == "hasBlend") return (int)ubo_flag(FLAG_HAS_BLEND);
			if (n == "blendWeight") return (int)ubo_flag(FLAG_BLEND_WEIGHT);
			return -1;
		}

		// GLSL std140 块声明：与上面的字节偏移严格一致（同一布局的着色器侧视图）
		// vulkan=true 时带 set/binding 限定符（GL 用 layout(std140) 即可）
		inline std::string ubo_glsl(bool vulkan)
		{
			std::string s;
			s += "struct Light { vec4 colorType; vec4 position; vec4 direction; vec4 spot; };\n";
			s += vulkan
				? "layout(std140, set = 0, binding = 0) uniform UBO {\n"
				: "layout(std140) uniform UBO {\n";
			s += "    mat4 transform;\n";
			s += "    vec4 color;\n";
			s += "    vec4 select;\n";
			s += "    vec3 lightRGB;\n";
			s += "    vec3 lightXYZ;\n";
			s += "    int flags[" + std::to_string(UBO_MAX_FLAGS) + "];\n";
			s += "    mat4 bones[" + std::to_string(UBO_MAX_BONES) + "];\n";
			s += "    mat4 world;\n";
			s += "    Light lights[" + std::to_string(UBO_MAX_LIGHTS) + "];\n";
			s += "    mat4 lightVP[" + std::to_string(UBO_MAX_CASCADES) + "];\n";
			s += "    vec4 cascadeSplits;\n";
			s += "    vec3 cameraPos; float pointSize;\n";   // pointSize 落在 cameraPos 的 std140 尾部 4 字节
			s += "    vec4 colorGrade;\n";
			s += "    vec4 ambientColor;\n";
			s += "    vec4 materialBase;\n";
			s += "    vec4 materialPBR;\n";
			s += "    vec4 materialEmissive;\n";
			s += "    vec4 materialFlags;\n";
			s += "    vec4 materialToon;\n";      // 卡通渲染参数（x=ramp阈值 y=ramp过渡带 z=边缘光锐度 w=高光阈值）
			s += "    vec4 toonRimColor;\n";      // 边缘光颜色与强度（rgb=颜色 a=强度）
			s += "    mat4 instances[" + std::to_string(UBO_MAX_INSTANCES) + "];\n";
			s += "} ubo;\n";
			return s;
		}
	}
}

#endif
