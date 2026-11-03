#include "shader_preprocess.h"
#include "ubo.h"

namespace xhqm
{
	namespace fantasy
	{
		namespace
		{
			std::string replace_all(std::string s, const std::string& from, const std::string& to)
			{
				size_t pos = 0;
				while ((pos = s.find(from, pos)) != std::string::npos)
				{
					s.replace(pos, from.length(), to);
					pos += to.length();
				}
				return s;
			}
		}

		std::string shader_preprocess(const std::string& src, bool vulkan, bool apple_gl)
		{
			// Apple GL 驱动变通：UBO 灯光数组的结构体拷贝/动态索引会杀死片元，仅此组合走极简漫反射；
			// 其它平台（Windows/Linux GL、全部 Vulkan）保持完整逐灯光循环，行为与 r394 一致
			static const char* k_lighting_simple = R"(
        vec3 lighting = vec3(0.0);
        // apple_gl 兼容：极简兰伯特漫反射（只读第一盏方向光，避免 UBO 灯光数组的复杂读取）
        if (ubo.flags[XHQM_FLAG_LIGHT_COUNT] > 0)
        {
            vec3 lightDir = normalize(-ubo.lights[0].direction.xyz);
            float NdotL = max(dot(N, lightDir), 0.0);
            lighting = baseColor * NdotL / 3.1415926;
        }
)";
			static const char* k_lighting_full = R"(
        vec3 lighting = vec3(0.0);
        for (int i = 0; i < ubo.flags[XHQM_FLAG_LIGHT_COUNT] && i < XHQM_MAX_LIGHTS; ++i)
        {
            Light lt = ubo.lights[i];
            int ltype = int(lt.colorType.w + 0.5);
            vec3 lightDir;
            vec3 radiance = lt.colorType.rgb;
            if (ltype == 0)
            {
                lightDir = normalize(-lt.direction.xyz);
                if (ubo.flags[XHQM_FLAG_SHADOW] != 0)
                {
                    int cascadeCount = ubo.flags[XHQM_FLAG_SHADOW];
                    float viewDepth = length(fragWorldPos - ubo.cameraPos);
                    int cascade = cascadeCount - 1;
                    for (int c = 0; c < cascadeCount - 1; ++c)
                    {
                        float sc = (c == 0) ? ubo.cascadeSplits.x
                                 : (c == 1) ? ubo.cascadeSplits.y
                                 : (c == 2) ? ubo.cascadeSplits.z : ubo.cascadeSplits.w;
                        if (viewDepth < sc) { cascade = c; break; }
                    }
                    vec4 lightSpace = ubo.lightVP[cascade] * vec4(fragWorldPos, 1.0);
                    vec3 proj = lightSpace.xyz / lightSpace.w;
                    vec2 shadowUV = proj.xy * 0.5 + 0.5;
XHQM_SHADOW_UV_FLIP
                    // PCF 3x3 软阴影 + shadow bias + normal offset（M6）
                    const vec2 texel = vec2(1.0 / 2048.0);
                    const float bias = 0.005;
                    shadowUV += N.xy * texel * 2.0;
                    float shadow = 0.0;
                    for (int sx = -1; sx <= 1; ++sx)
                    for (int sy = -1; sy <= 1; ++sy)
                    {
                        // 深度参考从 NDC [-1,1] 重映射到 [0,1]（与深度纹理存储一致，GL 硬件/Vulkan XHQM_DEPTH_REMAP 都归一化到 [0,1]）
                        vec3 sc = vec3(shadowUV + vec2(sx, sy) * texel, 0.5 * proj.z + 0.5 - bias);
                        if (cascade == 0) shadow += texture(shadowMap[0], sc);
                        else if (cascade == 1) shadow += texture(shadowMap[1], sc);
                        else if (cascade == 2) shadow += texture(shadowMap[2], sc);
                        else shadow += texture(shadowMap[3], sc);
                    }
                    shadow /= 9.0;
                    radiance *= shadow;
                }
            }
            else
            {
                vec3 toLight = lt.position.xyz - fragWorldPos;
                float dist = length(toLight);
                lightDir = toLight / max(dist, 0.0001);
                radiance = radiance / (1.0 + 0.09 * dist + 0.032 * dist * dist);
                if (ltype == 1 && ubo.flags[XHQM_FLAG_SHADOW] != 0)
                {
                    // 点光 cube shadow PCF（M6）：立方体存线性深度 distance/far，比较采样
                    // 注：生产需距离写入深度着色器（gl_FragDepth = dist/far）配合 6 面渲染；far 暂按 10
                    vec3 l2f = -toLight;   // 光 → 片元
                    float bias = 0.02;
                    float shadow = 0.0;
                    shadow += texture(cubeShadow, vec4(l2f + vec3( 0.02, 0.0, 0.0), dist / 10.0 - bias));
                    shadow += texture(cubeShadow, vec4(l2f + vec3(-0.02, 0.0, 0.0), dist / 10.0 - bias));
                    shadow += texture(cubeShadow, vec4(l2f + vec3(0.0,  0.02, 0.0), dist / 10.0 - bias));
                    shadow += texture(cubeShadow, vec4(l2f + vec3(0.0, -0.02, 0.0), dist / 10.0 - bias));
                    shadow /= 4.0;
                    radiance *= shadow;
                }
                if (ltype == 2)
                {
                    float cosTheta = dot(normalize(lt.direction.xyz), -lightDir);
                    float epsilon = lt.spot.x - lt.spot.y;
                    float intensity = clamp((cosTheta - lt.spot.y) / max(epsilon, 0.0001), 0.0, 1.0);
                    radiance = radiance * intensity;
                }
            }
            vec3 H = normalize(lightDir + V);
            float NdotL = max(dot(N, lightDir), 0.0);
            float NdotV = max(dot(N, V), 0.0);
            float NdotH = max(dot(N, H), 0.0);

            float a = roughness * roughness;
            float a2 = a * a;
            float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
            float NDF = a2 / (3.1415926 * d * d);
            float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
            float G1V = NdotV / (NdotV * (1.0 - k) + k);
            float G1L = NdotL / (NdotL * (1.0 - k) + k);
            float G = G1V * G1L;
            vec3 F = F0 + (1.0 - F0) * pow(1.0 - max(dot(V, H), 0.0), 5.0);
            vec3 specular = NDF * G * F / max(4.0 * NdotV * NdotL, 0.001);

            lighting += (diffuse + specular) * radiance * NdotL;
        }
)";
		// 卡通光照（toon）：与 k_lighting_full 相同的光源遍历/阴影/衰减/聚光锥，
		// BRDF 尾部替换为 ramp 分档漫反射 + 硬边高光 + 视角边缘光（材质参数见 ubo.materialToon/toonRimColor）
		static const char* k_toon_lighting_simple = R"(
        vec3 lighting = vec3(0.0);
        // apple_gl 兼容：极简 ramp 漫反射（只读第一盏方向光，避免 UBO 灯光数组的复杂读取）
        if (ubo.flags[XHQM_FLAG_LIGHT_COUNT] > 0)
        {
            vec3 lightDir = normalize(-ubo.lights[0].direction.xyz);
            float halfLambert = dot(N, lightDir) * 0.5 + 0.5;
            float ramp = smoothstep(ubo.materialToon.x - ubo.materialToon.y, ubo.materialToon.x + ubo.materialToon.y, halfLambert);
            lighting = baseColor * mix(0.35, 1.0, ramp);
        }
)";
		static const char* k_toon_lighting_full = R"(
        vec3 lighting = vec3(0.0);
        for (int i = 0; i < ubo.flags[XHQM_FLAG_LIGHT_COUNT] && i < XHQM_MAX_LIGHTS; ++i)
        {
            Light lt = ubo.lights[i];
            int ltype = int(lt.colorType.w + 0.5);
            vec3 lightDir;
            vec3 radiance = lt.colorType.rgb;
            if (ltype == 0)
            {
                lightDir = normalize(-lt.direction.xyz);
                if (ubo.flags[XHQM_FLAG_SHADOW] != 0)
                {
                    int cascadeCount = ubo.flags[XHQM_FLAG_SHADOW];
                    float viewDepth = length(fragWorldPos - ubo.cameraPos);
                    int cascade = cascadeCount - 1;
                    for (int c = 0; c < cascadeCount - 1; ++c)
                    {
                        float sc = (c == 0) ? ubo.cascadeSplits.x
                                 : (c == 1) ? ubo.cascadeSplits.y
                                 : (c == 2) ? ubo.cascadeSplits.z : ubo.cascadeSplits.w;
                        if (viewDepth < sc) { cascade = c; break; }
                    }
                    vec4 lightSpace = ubo.lightVP[cascade] * vec4(fragWorldPos, 1.0);
                    vec3 proj = lightSpace.xyz / lightSpace.w;
                    vec2 shadowUV = proj.xy * 0.5 + 0.5;
XHQM_SHADOW_UV_FLIP
                    // PCF 3x3 软阴影 + shadow bias + normal offset（与 PBR 路径一致，卡通光照同样吃阴影）
                    const vec2 texel = vec2(1.0 / 2048.0);
                    const float bias = 0.005;
                    shadowUV += N.xy * texel * 2.0;
                    float shadow = 0.0;
                    for (int sx = -1; sx <= 1; ++sx)
                    for (int sy = -1; sy <= 1; ++sy)
                    {
                        vec3 sc = vec3(shadowUV + vec2(sx, sy) * texel, 0.5 * proj.z + 0.5 - bias);
                        if (cascade == 0) shadow += texture(shadowMap[0], sc);
                        else if (cascade == 1) shadow += texture(shadowMap[1], sc);
                        else if (cascade == 2) shadow += texture(shadowMap[2], sc);
                        else shadow += texture(shadowMap[3], sc);
                    }
                    shadow /= 9.0;
                    radiance *= shadow;
                }
            }
            else
            {
                vec3 toLight = lt.position.xyz - fragWorldPos;
                float dist = length(toLight);
                lightDir = toLight / max(dist, 0.0001);
                radiance = radiance / (1.0 + 0.09 * dist + 0.032 * dist * dist);
                if (ltype == 2)
                {
                    float cosTheta = dot(normalize(lt.direction.xyz), -lightDir);
                    float epsilon = lt.spot.x - lt.spot.y;
                    float intensity = clamp((cosTheta - lt.spot.y) / max(epsilon, 0.0001), 0.0, 1.0);
                    radiance = radiance * intensity;
                }
            }
            vec3 H = normalize(lightDir + V);
            float NdotL = max(dot(N, lightDir), 0.0);
            float NdotV = max(dot(N, V), 0.0);
            float NdotH = max(dot(N, H), 0.0);

            // ramp 分档漫反射：halfLambert 经阈值±过渡带 smoothstep 量化（softness=0 时为硬边两档）
            float halfLambert = NdotL * 0.5 + 0.5;
            float ramp = smoothstep(ubo.materialToon.x - ubo.materialToon.y, ubo.materialToon.x + ubo.materialToon.y, halfLambert);
            ramp = mix(0.35, 1.0, ramp);   // 暗档保底 0.35（避免死黑，环境光另算）
            lighting += baseColor * radiance * ramp;

            // 卡通高光：硬边 smoothstep 量化 Blinn-Phong（materialToon.w=高光阈值，<=0 关闭），只出现在受光面
            if (ubo.materialToon.w > 0.0)
            {
                float spec = smoothstep(ubo.materialToon.w, ubo.materialToon.w + 0.04, NdotH) * step(0.5, ramp);
                lighting += radiance * spec;
            }

            // 视角边缘光：fresnel 幂次锐化，受光面调制（toonRimColor.a=强度，0 关闭）
            if (ubo.toonRimColor.w > 0.0)
            {
                float rim = pow(1.0 - NdotV, ubo.materialToon.z) * ramp;
                lighting += ubo.toonRimColor.rgb * ubo.toonRimColor.w * rim;
            }
        }
)";
			std::string s = src;
			s = replace_all(s, "XHQM_VERSION", vulkan ? "#version 450" : "#version 330 core");
			s = replace_all(s, "XHQM_LIGHTING", apple_gl ? k_lighting_simple : k_lighting_full);
		s = replace_all(s, "XHQM_TOON_LIGHTING", apple_gl ? k_toon_lighting_simple : k_toon_lighting_full);
			s = replace_all(s, "XHQM_UBO_BLOCK", ubo_glsl(vulkan));
			// UBO 布局常量（flag 语义下标 / 数组大小）→ 数字，着色器不硬编码、与 ubo.h 单一事实源对齐
			s = replace_all(s, "XHQM_FLAG_LIGHTING", std::to_string(FLAG_USE_LIGHTING));
			s = replace_all(s, "XHQM_FLAG_COLOUR", std::to_string(FLAG_HAS_COLOUR));
			s = replace_all(s, "XHQM_FLAG_NORMAL", std::to_string(FLAG_HAS_NORMAL));
			s = replace_all(s, "XHQM_FLAG_FLIPUV", std::to_string(FLAG_HAS_FLIPUV));
			s = replace_all(s, "XHQM_FLAG_VALUES", std::to_string(FLAG_HAS_VALUES));
			s = replace_all(s, "XHQM_FLAG_SKIN", std::to_string(FLAG_HAS_SKIN));
			s = replace_all(s, "XHQM_FLAG_TEXTURE_0", std::to_string(FLAG_HAS_TEXTURE_0));
			s = replace_all(s, "XHQM_FLAG_TEXTURE_1", std::to_string(FLAG_HAS_TEXTURE_1));
			s = replace_all(s, "XHQM_FLAG_TEXTURE_2", std::to_string(FLAG_HAS_TEXTURE_2));
			s = replace_all(s, "XHQM_FLAG_BLEND_WEIGHT", std::to_string(FLAG_BLEND_WEIGHT));
			// 注意顺序：先替换长名 XHQM_FLAG_BLEND_WEIGHT，再替换其前缀 XHQM_FLAG_BLEND；反之会变 9_WEIGHT 非法标识符
			s = replace_all(s, "XHQM_FLAG_BLEND", std::to_string(FLAG_HAS_BLEND));
			s = replace_all(s, "XHQM_FLAG_LIGHT_COUNT", std::to_string(FLAG_LIGHT_COUNT));
			s = replace_all(s, "XHQM_FLAG_SHADOW", std::to_string(FLAG_SHADOW_ENABLED));
			s = replace_all(s, "XHQM_FLAG_TANGENT", std::to_string(FLAG_HAS_TANGENT));
			s = replace_all(s, "XHQM_FLAG_INSTANCES", std::to_string(FLAG_HAS_INSTANCES));
			s = replace_all(s, "XHQM_MAX_LIGHTS", std::to_string(UBO_MAX_LIGHTS));
			s = replace_all(s, "XHQM_MAX_CASCADES", std::to_string(UBO_MAX_CASCADES));
			s = replace_all(s, "XHQM_DEPTH_REMAP", vulkan ? "    gl_Position.z = 0.5 * (gl_Position.z + gl_Position.w);" : "");
			// 阴影图采样 v 翻转：仅 Vulkan（离屏翻转视口，NDC +y 写入图像 v=0，采样 0.5*proj+0.5 会把
			// NDC +y 映射到 v=1 → 上下镜像）；GL 视口不翻转（NDC -y 在纹理 v=0，采样天然对齐），勿翻
			s = replace_all(s, "XHQM_SHADOW_UV_FLIP", vulkan ? "                    shadowUV.y = 1.0 - shadowUV.y;" : "");
			s = replace_all(s, "XHQM_INSTANCE_ID", vulkan ? "gl_InstanceIndex" : "gl_InstanceID");

			// varying 接口：GL 330 用名字匹配（out/in 无 layout），Vulkan 450 用 layout(location)
			s = replace_all(s, "XHQM_OUT0", vulkan ? "layout(location = 0) out" : "out");
			s = replace_all(s, "XHQM_OUT1", vulkan ? "layout(location = 1) out" : "out");
			s = replace_all(s, "XHQM_OUT2", vulkan ? "layout(location = 2) out" : "out");
			s = replace_all(s, "XHQM_OUT3", vulkan ? "layout(location = 3) out" : "out");
			s = replace_all(s, "XHQM_OUT4", vulkan ? "layout(location = 4) out" : "out");
			s = replace_all(s, "XHQM_OUT5", vulkan ? "layout(location = 5) out" : "out");
			s = replace_all(s, "XHQM_IN0", vulkan ? "layout(location = 0) in" : "in");
			s = replace_all(s, "XHQM_IN1", vulkan ? "layout(location = 1) in" : "in");
			s = replace_all(s, "XHQM_IN2", vulkan ? "layout(location = 2) in" : "in");
			s = replace_all(s, "XHQM_IN3", vulkan ? "layout(location = 3) in" : "in");
			s = replace_all(s, "XHQM_IN4", vulkan ? "layout(location = 4) in" : "in");
			s = replace_all(s, "XHQM_IN5", vulkan ? "layout(location = 5) in" : "in");

			// 采样器：Vulkan 走 descriptor binding（binding 1..7），GL 走 uniform + glUniform1i
			s = replace_all(s, "XHQM_TEX0",
				vulkan ? "layout(set = 0, binding = 1) uniform sampler2D prefabTexture_0;" : "uniform sampler2D prefabTexture_0;");
			s = replace_all(s, "XHQM_TEX1",
				vulkan ? "layout(set = 0, binding = 2) uniform sampler2D prefabTexture_1;" : "uniform sampler2D prefabTexture_1;");
			s = replace_all(s, "XHQM_TEX2",
				vulkan ? "layout(set = 0, binding = 3) uniform sampler2D prefabTexture_2;" : "uniform sampler2D prefabTexture_2;");
			s = replace_all(s, "XHQM_SHADOW_MAP",
				vulkan
				? "layout(set = 0, binding = 4) uniform sampler2DShadow shadowMap[" + std::to_string(UBO_MAX_CASCADES) + "];"
				: "uniform sampler2DShadow shadowMap[" + std::to_string(UBO_MAX_CASCADES) + "];");
			s = replace_all(s, "XHQM_SPRITE_TEX",
				vulkan ? "layout(set = 0, binding = 1) uniform sampler2D spriteTexture;" : "uniform sampler2D spriteTexture;");
			s = replace_all(s, "XHQM_COLOR_TEX",
				vulkan ? "layout(set = 0, binding = 1) uniform sampler2D colorTexture;" : "uniform sampler2D colorTexture;");
			s = replace_all(s, "XHQM_NORMAL_MAP",
				vulkan ? "layout(set = 0, binding = 8) uniform sampler2D normalMap;" : "uniform sampler2D normalMap;");
			s = replace_all(s, "XHQM_AO_MAP",
				vulkan ? "layout(set = 0, binding = 9) uniform sampler2D aoMap;" : "uniform sampler2D aoMap;");
			s = replace_all(s, "XHQM_CUBE_SHADOW",
				vulkan ? "layout(set = 0, binding = 5) uniform samplerCubeShadow cubeShadow;" : "uniform samplerCubeShadow cubeShadow;");
			s = replace_all(s, "XHQM_IRRADIANCE",
				vulkan ? "layout(set = 0, binding = 6) uniform samplerCube irradianceMap;" : "uniform samplerCube irradianceMap;");
			s = replace_all(s, "XHQM_PREFILTER",
				vulkan ? "layout(set = 0, binding = 7) uniform samplerCube prefilteredMap;" : "uniform samplerCube prefilteredMap;");
			return s;
		}
	}
}
