#pragma once
#ifndef XHQM_FANTASY_SHADER_SOURCES
#define XHQM_FANTASY_SHADER_SOURCES

// 统一着色器源（GL 与 Vulkan 共用单一事实源）。
// 源码含 XHQM_* 宏，经 shader_preprocess(src, vulkan) 展开为 GLSL 410 / 450。
// 顶点属性/片元输出用 layout(location=N)（GL 330 起支持）；
// varying 接口 GL 用名字匹配（out/in），Vulkan 用 layout(location)，经 XHQM_OUTn/XHQM_INn 宏区分。

namespace xhqm { namespace fantasy { namespace shader_sources {

inline const char* select_vert() { return R"(
XHQM_VERSION
layout(location = 0) in vec3 vertex;
layout(location = 5) in vec4 skinIndex;
layout(location = 6) in vec4 skinWeight;
XHQM_OUT0 vec4 fragColour;
XHQM_UBO_BLOCK

void main()
{
    vec4 pos = vec4(vertex, 1.0);
    if (ubo.flags[XHQM_FLAG_SKIN] != 0) {
        mat4 skin = ubo.bones[int(skinIndex.x + 0.5)] * skinWeight.x
                  + ubo.bones[int(skinIndex.y + 0.5)] * skinWeight.y
                  + ubo.bones[int(skinIndex.z + 0.5)] * skinWeight.z
                  + ubo.bones[int(skinIndex.w + 0.5)] * skinWeight.w;
        pos = skin * pos;
    }
    fragColour = ubo.select;
    gl_Position = ubo.transform * pos;
gl_PointSize = ubo.pointSize;
XHQM_DEPTH_REMAP
}
)"; }

inline const char* select_frag() { return R"(
XHQM_VERSION
XHQM_IN0 vec4 fragColour;
layout(location = 0) out vec4 outColor;

void main()
{
    outColor = fragColour;
}
)"; }

inline const char* prefab_vert() { return R"(
XHQM_VERSION
layout(location = 0) in vec3 vertex;
layout(location = 1) in vec4 colour;
layout(location = 2) in vec3 normal;
layout(location = 3) in vec2 flipuv;
layout(location = 4) in float value;
layout(location = 5) in vec4 skinIndex;
layout(location = 6) in vec4 skinWeight;
layout(location = 7) in vec3 blendDelta;
layout(location = 8) in vec3 tangent;
layout(location = 9) in vec3 blendNormalDelta;
XHQM_OUT0 vec4 fragColour;
XHQM_OUT1 vec3 fragNormal;
XHQM_OUT2 vec3 fragWorldPos;
XHQM_OUT3 vec2 fragFlipuv;
XHQM_OUT4 vec2 fragValues;
XHQM_OUT5 vec3 fragTangent;
XHQM_UBO_BLOCK

void main()
{
    fragColour = (ubo.flags[XHQM_FLAG_COLOUR] != 0) ? colour : ubo.color;
    fragFlipuv = (ubo.flags[XHQM_FLAG_FLIPUV] != 0) ? flipuv : vec2(0.0, 0.0);
    fragValues = vec2(0.0, (ubo.flags[XHQM_FLAG_VALUES] == 0) ? 0.0 : value);

    vec4 localPos = vec4(vertex, 1.0);
    if (ubo.flags[XHQM_FLAG_BLEND] != 0) { localPos.xyz += blendDelta * intBitsToFloat(ubo.flags[XHQM_FLAG_BLEND_WEIGHT]); }
    vec4 localNor = vec4((ubo.flags[XHQM_FLAG_NORMAL] != 0) ? normal : vec3(0.0, 0.0, 1.0), 0.0);
    if (ubo.flags[XHQM_FLAG_BLEND] != 0) { localNor.xyz += blendNormalDelta * intBitsToFloat(ubo.flags[XHQM_FLAG_BLEND_WEIGHT]); }
    mat4 skin = mat4(1.0);
    if (ubo.flags[XHQM_FLAG_SKIN] != 0)
    {
        skin = ubo.bones[int(skinIndex.x + 0.5)] * skinWeight.x
             + ubo.bones[int(skinIndex.y + 0.5)] * skinWeight.y
             + ubo.bones[int(skinIndex.z + 0.5)] * skinWeight.z
             + ubo.bones[int(skinIndex.w + 0.5)] * skinWeight.w;
        localPos = skin * localPos;
        localNor = transpose(inverse(skin)) * localNor;
    }
    if (ubo.flags[XHQM_FLAG_INSTANCES] != 0)
    {
        // 实例变换（instancing）：每实例一个世界矩阵，用实例 ID 索引 UBO 数组
        mat4 inst = ubo.instances[XHQM_INSTANCE_ID];
        localPos = inst * localPos;
        localNor = inst * localNor;
    }
    vec4 clipPos = ubo.transform * localPos;
    fragNormal = normalize(vec3(ubo.world * localNor));
    fragWorldPos = vec3(ubo.world * localPos);
    vec4 localTan = vec4((ubo.flags[XHQM_FLAG_TANGENT] != 0) ? tangent : vec3(1.0, 0.0, 0.0), 0.0);
    if (ubo.flags[XHQM_FLAG_SKIN] != 0) { localTan = transpose(inverse(skin)) * localTan; }
    if (ubo.flags[XHQM_FLAG_INSTANCES] != 0) { localTan = ubo.instances[XHQM_INSTANCE_ID] * localTan; }
    fragTangent = normalize(vec3(ubo.world * localTan));
    gl_Position = clipPos;
gl_PointSize = ubo.pointSize;
XHQM_DEPTH_REMAP

    fragColour = fragColour * ubo.select;
}
)"; }

inline const char* prefab_frag() { return R"(
XHQM_VERSION
XHQM_UBO_BLOCK
XHQM_TEX0
XHQM_TEX1
XHQM_TEX2
XHQM_SHADOW_MAP
XHQM_NORMAL_MAP
XHQM_AO_MAP
XHQM_CUBE_SHADOW
XHQM_IRRADIANCE
XHQM_PREFILTER
XHQM_IN0 vec4 fragColour;
XHQM_IN1 vec3 fragNormal;
XHQM_IN2 vec3 fragWorldPos;
XHQM_IN3 vec2 fragFlipuv;
XHQM_IN4 vec2 fragValues;
XHQM_IN5 vec3 fragTangent;
layout(location = 0) out vec4 outColor;

void main()
{
    vec4 finalColor = fragColour;
    {
        vec4 textureColour_0 = fragColour;
        vec4 textureColour_1 = fragColour;
        vec4 textureColour_2 = fragColour;
        if (ubo.flags[XHQM_FLAG_TEXTURE_2] != 0) { finalColor = textureColour_2 = texture(prefabTexture_2, fragFlipuv); }
        if (ubo.flags[XHQM_FLAG_TEXTURE_1] != 0) { finalColor = textureColour_1 = texture(prefabTexture_1, fragFlipuv); }
        if (ubo.flags[XHQM_FLAG_TEXTURE_0] != 0) { finalColor = textureColour_0 = texture(prefabTexture_0, fragValues); }
        finalColor = finalColor * fragColour;
    }

    // per-material 参数（M1：base_color × 面颜色；metallic/roughness/ao/alpha/emissive）
    vec3 baseColor = finalColor.rgb * ubo.materialBase.rgb;
    float metalness = ubo.materialPBR.x;
    float roughness = ubo.materialPBR.y;
    float ao = ubo.materialPBR.z;
    if (ubo.materialFlags.z != 0) { ao *= texture(aoMap, fragFlipuv).r; }
    float alpha = ubo.materialPBR.w;
    vec3 emissive = ubo.materialEmissive.rgb;

    if (ubo.flags[XHQM_FLAG_LIGHTING] != 0)
    {
        vec3 N = normalize(fragNormal);
        if (ubo.materialFlags.y != 0)
        {
            // tangent TBN（M1 法线贴图：用顶点 tangent + 法线重建 bitangent）
            vec3 T = normalize(fragTangent);
            vec3 B = normalize(cross(N, T));
            vec3 nm = texture(normalMap, fragFlipuv).xyz * 2.0 - 1.0;
            N = normalize(mat3(T, B, N) * nm);
        }
        vec3 V = normalize(ubo.cameraPos - fragWorldPos + vec3(0.0001));

        vec3 F0 = mix(vec3(0.04), baseColor, metalness);
        vec3 diffuse = baseColor * (1.0 - metalness) / 3.1415926;

        XHQM_LIGHTING
        vec3 ambient;
        if (ubo.materialFlags.w > 0.5)
            ambient = texture(irradianceMap, N).rgb * baseColor * (1.0 - metalness) * ao;
        else
            ambient = ubo.ambientColor.rgb * baseColor * (1.0 - metalness) * ao;
        ambient += ubo.ambientColor.rgb * F0 * ubo.ambientColor.w * ao;
        lighting += ambient;

        // IBL 镜面反射（specular）：prefiltered(R) × Karis 环境 BRDF 近似
        if (ubo.materialFlags.w > 0.5)
        {
            vec3 R = reflect(-V, N);
            vec3 prefiltered = texture(prefilteredMap, R).rgb;
            vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
            vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
            vec4 r = roughness * c0 + c1;
            float ndv = max(dot(N, V), 0.0);   // NdotV 在光照循环作用域内，此处按片元重算
            float a004 = min(r.x * r.x, exp2(-9.28 * ndv)) * r.x + r.y;
            vec2 AB = vec2(-1.04, 1.04) * a004 + r.zw;
            lighting += prefiltered * (F0 * AB.x + AB.y);
        }

        finalColor.rgb = lighting;
    }
    else
    {
        finalColor.rgb = baseColor;
    }
    finalColor.rgb += emissive;   // 自发光（光照后叠加）
    // ★ 2026-10-12：原为 finalColor.a = alpha（材质 alpha 直接覆盖），会把纹理自身 alpha 丢掉 —— VRM 那种「RGB=0 + alpha=0」的遮罩贴图
    //   因此被当不透明画成全黑（衣服发黑）。改为相乘后，纹理 alpha=0 的像素才会被下面 `if (outColor.a < 0.1) discard;` 丢掉。
    finalColor.a = alpha * finalColor.a;

    outColor = finalColor;
    outColor.rgb *= ubo.colorGrade.x;
    outColor.rgb = (outColor.rgb - vec3(0.5)) * ubo.colorGrade.y + vec3(0.5);
    float luma = dot(outColor.rgb, vec3(0.2126, 0.7152, 0.0722));
    outColor.rgb = mix(vec3(luma), outColor.rgb, ubo.colorGrade.z);

    // M5：tonemap + gamma 已移到独立 tonemap pass，片元只输出线性 HDR（可 >1）

    if (outColor.a < 0.1)
    {
        discard;
    }
}
)"; }

// 卡通渲染（toon）：顶点路径与 PBR 完全一致（蒙皮/形变/实例化/切线全兼容），片元走 ramp 分档
inline const char* toon_vert() { return prefab_vert(); }

inline const char* toon_frag() { return R"(
XHQM_VERSION
XHQM_UBO_BLOCK
XHQM_TEX0
XHQM_TEX1
XHQM_TEX2
XHQM_SHADOW_MAP
XHQM_NORMAL_MAP
XHQM_AO_MAP
XHQM_IN0 vec4 fragColour;
XHQM_IN1 vec3 fragNormal;
XHQM_IN2 vec3 fragWorldPos;
XHQM_IN3 vec2 fragFlipuv;
XHQM_IN4 vec2 fragValues;
XHQM_IN5 vec3 fragTangent;
layout(location = 0) out vec4 outColor;

void main()
{
    vec4 finalColor = fragColour;
    {
        if (ubo.flags[XHQM_FLAG_TEXTURE_2] != 0) { finalColor = texture(prefabTexture_2, fragFlipuv); }
        if (ubo.flags[XHQM_FLAG_TEXTURE_1] != 0) { finalColor = texture(prefabTexture_1, fragFlipuv); }
        if (ubo.flags[XHQM_FLAG_TEXTURE_0] != 0) { finalColor = texture(prefabTexture_0, fragValues); }
        finalColor = finalColor * fragColour;
    }

    // per-material 参数（base_color × 面颜色；ao/alpha/emissive 与 PBR 路径同义）
    vec3 baseColor = finalColor.rgb * ubo.materialBase.rgb;
    float ao = ubo.materialPBR.z;
    if (ubo.materialFlags.z != 0) { ao *= texture(aoMap, fragFlipuv).r; }
    float alpha = ubo.materialPBR.w;
    vec3 emissive = ubo.materialEmissive.rgb;

    if (ubo.flags[XHQM_FLAG_LIGHTING] != 0)
    {
        vec3 N = normalize(fragNormal);
        if (ubo.materialFlags.y != 0)
        {
            // tangent TBN（法线贴图：与 PBR 路径相同的重建方式）
            vec3 T = normalize(fragTangent);
            vec3 B = normalize(cross(N, T));
            vec3 nm = texture(normalMap, fragFlipuv).xyz * 2.0 - 1.0;
            N = normalize(mat3(T, B, N) * nm);
        }
        vec3 V = normalize(ubo.cameraPos - fragWorldPos + vec3(0.0001));

        XHQM_TOON_LIGHTING
        // 环境光：ao 调制（无 IBL 卷积，卡通路径从简）
        lighting += ubo.ambientColor.rgb * baseColor * ao;

        finalColor.rgb = lighting;
    }
    else
    {
        finalColor.rgb = baseColor;
    }
    finalColor.rgb += emissive;   // 自发光（光照后叠加）
    // ★ 2026-10-12：原为 finalColor.a = alpha（材质 alpha 直接覆盖），会把纹理自身 alpha 丢掉 —— VRM 那种「RGB=0 + alpha=0」的遮罩贴图
    //   因此被当不透明画成全黑（衣服发黑）。改为相乘后，纹理 alpha=0 的像素才会被下面 `if (outColor.a < 0.1) discard;` 丢掉。
    finalColor.a = alpha * finalColor.a;

    outColor = finalColor;
    outColor.rgb *= ubo.colorGrade.x;
    outColor.rgb = (outColor.rgb - vec3(0.5)) * ubo.colorGrade.y + vec3(0.5);
    float luma = dot(outColor.rgb, vec3(0.2126, 0.7152, 0.0722));
    outColor.rgb = mix(vec3(luma), outColor.rgb, ubo.colorGrade.z);

    if (outColor.a < 0.1)
    {
        discard;
    }
}
)"; }

inline const char* sprite_vert() { return R"(
XHQM_VERSION
layout(location = 0) in vec3 vertex;
layout(location = 1) in vec4 colour;
layout(location = 2) in vec2 flipuv;
XHQM_OUT0 vec4 fragColour;
XHQM_OUT1 vec2 fragFlipuv;
XHQM_UBO_BLOCK

void main()
{
    fragColour = (ubo.flags[XHQM_FLAG_COLOUR] != 0) ? colour : ubo.color;
    fragFlipuv = (ubo.flags[XHQM_FLAG_FLIPUV] != 0) ? flipuv : vec2(0.0, 0.0);
    gl_Position = ubo.transform * vec4(vertex, 1.0);
gl_PointSize = ubo.pointSize;
XHQM_DEPTH_REMAP
    fragColour = fragColour * ubo.select;
}
)"; }

inline const char* sprite_frag() { return R"(
XHQM_VERSION
XHQM_IN0 vec4 fragColour;
XHQM_IN1 vec2 fragFlipuv;
layout(location = 0) out vec4 outColor;
XHQM_UBO_BLOCK
XHQM_SPRITE_TEX

void main()
{
    outColor = fragColour;
    if (ubo.flags[XHQM_FLAG_TEXTURE_0] != 0)
    {
        outColor = texture(spriteTexture, fragFlipuv);
    }
}
)"; }

inline const char* point_line_ploy_vert() { return R"(
XHQM_VERSION
layout(location = 0) in vec3 pos;
layout(location = 1) in float value;
XHQM_OUT0 vec2 fragCoord;
XHQM_OUT1 vec4 fragColor;
XHQM_UBO_BLOCK

void main()
{
    gl_Position = ubo.transform * vec4(pos, 1.0);
gl_PointSize = ubo.pointSize;
XHQM_DEPTH_REMAP
    fragCoord = vec2(0.0, value);
    fragColor = ubo.select;
}
)"; }

inline const char* point_line_ploy_frag() { return R"(
XHQM_VERSION
XHQM_IN0 vec2 fragCoord;
XHQM_IN1 vec4 fragColor;
layout(location = 0) out vec4 outColor;
XHQM_COLOR_TEX

void main()
{
    outColor = texture(colorTexture, fragCoord);
    outColor.rgb *= fragColor.rgb;
}
)"; }

inline const char* curve_vert() { return R"(
XHQM_VERSION
layout(location = 0) in vec3 pos;
layout(location = 1) in float uv;
XHQM_OUT0 vec2 fragCoord;
XHQM_OUT1 vec4 fragColor;
XHQM_UBO_BLOCK

void main()
{
    gl_Position = ubo.transform * vec4(pos, 1.0);
gl_PointSize = ubo.pointSize;
XHQM_DEPTH_REMAP
    fragCoord = vec2(0.0, uv);
    fragColor = ubo.select;
}
)"; }

inline const char* curve_frag() { return point_line_ploy_frag(); }

inline const char* nurbs_vert() { return R"(
XHQM_VERSION
layout(location = 0) in vec3 pos;
layout(location = 1) in vec2 uv;
XHQM_OUT0 vec2 fragCoord;
XHQM_OUT1 vec4 fragColor;
XHQM_UBO_BLOCK

void main()
{
    gl_Position = ubo.transform * vec4(pos, 1.0);
gl_PointSize = ubo.pointSize;
XHQM_DEPTH_REMAP
    fragCoord = uv;
    fragColor = ubo.select;
}
)"; }

inline const char* nurbs_frag() { return point_line_ploy_frag(); }

inline const char* occlusion_vert() { return R"(
XHQM_VERSION
layout(location = 0) in vec3 vertex;
XHQM_UBO_BLOCK

void main()
{
    vec4 p = ubo.transform * vec4(vertex, 1.0);
    gl_Position = vec4(p.xyz, 1.0);
gl_PointSize = ubo.pointSize;
XHQM_DEPTH_REMAP
}
)"; }

inline const char* occlusion_frag() { return R"(
XHQM_VERSION
layout(location = 0) out vec4 outColor;

void main() { outColor = vec4(0.0); }
)"; }

inline const char* depth_vert() { return R"(
XHQM_VERSION
layout(location = 0) in vec3 vertex;
layout(location = 5) in vec4 skinIndex;
layout(location = 6) in vec4 skinWeight;
layout(location = 7) in vec3 blendDelta;
XHQM_OUT0 float fragDepth;
XHQM_UBO_BLOCK

void main()
{
    vec4 localPos = vec4(vertex, 1.0);
    if (ubo.flags[XHQM_FLAG_BLEND] != 0) { localPos.xyz += blendDelta * intBitsToFloat(ubo.flags[XHQM_FLAG_BLEND_WEIGHT]); }
    mat4 skin = mat4(1.0);
    if (ubo.flags[XHQM_FLAG_SKIN] != 0)
    {
        skin = ubo.bones[int(skinIndex.x + 0.5)] * skinWeight.x
             + ubo.bones[int(skinIndex.y + 0.5)] * skinWeight.y
             + ubo.bones[int(skinIndex.z + 0.5)] * skinWeight.z
             + ubo.bones[int(skinIndex.w + 0.5)] * skinWeight.w;
        localPos = skin * localPos;
    }
    if (ubo.flags[XHQM_FLAG_INSTANCES] != 0)
    {
        localPos = ubo.instances[XHQM_INSTANCE_ID] * localPos;
    }
    vec4 clipPos = ubo.transform * localPos;
    fragDepth = clipPos.w;   // 透视下 = -viewPos.z = 线性视图深度（正数，0=近）
    gl_Position = clipPos;
gl_PointSize = ubo.pointSize;
XHQM_DEPTH_REMAP
}
)"; }

inline const char* depth_frag() { return R"(
XHQM_VERSION
XHQM_IN0 float fragDepth;
layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vec4(fragDepth, 0.0, 0.0, 1.0);
}
)"; }

}}}
#endif
