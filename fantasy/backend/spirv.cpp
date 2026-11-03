#include "spirv.h"
// 可选版本信息（glslang 发行包提供 build_info.h；缺失时 EXT 字段按缺省处理）
#if defined(__has_include)
#  if __has_include(<glslang/build_info.h>)
#    include <glslang/build_info.h>
#  endif
#endif
#include <glslang/Public/ShaderLang.h>
// glslang 布局差异：新版放 Public/ResourceLimits.h，发行版（如 Ubuntu 11.8）放 Include/ResourceLimits.h
#if defined(__has_include)
#  if __has_include(<glslang/Public/ResourceLimits.h>)
#    include <glslang/Public/ResourceLimits.h>
#  else
#    include <glslang/Include/ResourceLimits.h>
#  endif
#else
#  include <glslang/Public/ResourceLimits.h>
#endif
#include <glslang/SPIRV/GlslangToSpv.h>

namespace xhqm
{
	namespace fantasy
	{
		namespace
		{
			// 自提供默认资源上限，避免依赖 glslang-default-resource-limits.lib
			// （该库引用了与本工具链不匹配的 STL 符号 _Find_first_of_pos_vectorized）
			TBuiltInResource default_resource_limits()
			{
				TBuiltInResource r = {};
				r.maxLights = 32;
				r.maxClipPlanes = 6;
				r.maxTextureUnits = 32;
				r.maxTextureCoords = 32;
				r.maxVertexAttribs = 64;
				r.maxVertexUniformComponents = 4096;
				r.maxVaryingFloats = 64;
				r.maxVertexTextureImageUnits = 32;
				r.maxCombinedTextureImageUnits = 80;
				r.maxTextureImageUnits = 32;
				r.maxFragmentUniformComponents = 4096;
				r.maxDrawBuffers = 32;
				r.maxVertexUniformVectors = 128;
				r.maxVaryingVectors = 8;
				r.maxFragmentUniformVectors = 16;
				r.maxVertexOutputVectors = 16;
				r.maxFragmentInputVectors = 15;
				r.minProgramTexelOffset = -8;
				r.maxProgramTexelOffset = 7;
				r.maxClipDistances = 8;
				r.maxComputeWorkGroupCountX = 65535;
				r.maxComputeWorkGroupCountY = 65535;
				r.maxComputeWorkGroupCountZ = 65535;
				r.maxComputeWorkGroupSizeX = 1024;
				r.maxComputeWorkGroupSizeY = 1024;
				r.maxComputeWorkGroupSizeZ = 64;
				r.maxComputeUniformComponents = 1024;
				r.maxComputeTextureImageUnits = 16;
				r.maxComputeImageUniforms = 8;
				r.maxComputeAtomicCounters = 8;
				r.maxComputeAtomicCounterBuffers = 1;
				r.maxVaryingComponents = 60;
				r.maxVertexOutputComponents = 64;
				r.maxGeometryInputComponents = 64;
				r.maxGeometryOutputComponents = 128;
				r.maxFragmentInputComponents = 128;
				r.maxImageUnits = 8;
				r.maxCombinedImageUnitsAndFragmentOutputs = 8;
				r.maxCombinedShaderOutputResources = 8;
				r.maxImageSamples = 0;
				r.maxVertexImageUniforms = 0;
				r.maxTessControlImageUniforms = 0;
				r.maxTessEvaluationImageUniforms = 0;
				r.maxGeometryImageUniforms = 0;
				r.maxFragmentImageUniforms = 8;
				r.maxCombinedImageUniforms = 8;
				r.maxGeometryTextureImageUnits = 16;
				r.maxGeometryOutputVertices = 256;
				r.maxGeometryTotalOutputComponents = 1024;
				r.maxGeometryUniformComponents = 1024;
				r.maxGeometryVaryingComponents = 64;
				r.maxTessControlInputComponents = 128;
				r.maxTessControlOutputComponents = 128;
				r.maxTessControlTextureImageUnits = 16;
				r.maxTessControlUniformComponents = 1024;
				r.maxTessControlTotalOutputComponents = 4096;
				r.maxTessEvaluationInputComponents = 128;
				r.maxTessEvaluationOutputComponents = 128;
				r.maxTessEvaluationTextureImageUnits = 16;
				r.maxTessEvaluationUniformComponents = 1024;
				r.maxTessPatchComponents = 120;
				r.maxPatchVertices = 32;
				r.maxTessGenLevel = 64;
				r.maxViewports = 16;
				r.maxVertexAtomicCounters = 0;
				r.maxTessControlAtomicCounters = 0;
				r.maxTessEvaluationAtomicCounters = 0;
				r.maxGeometryAtomicCounters = 0;
				r.maxFragmentAtomicCounters = 8;
				r.maxCombinedAtomicCounters = 8;
				r.maxAtomicCounterBindings = 1;
				r.maxVertexAtomicCounterBuffers = 0;
				r.maxTessControlAtomicCounterBuffers = 0;
				r.maxTessEvaluationAtomicCounterBuffers = 0;
				r.maxGeometryAtomicCounterBuffers = 0;
				r.maxFragmentAtomicCounterBuffers = 1;
				r.maxCombinedAtomicCounterBuffers = 1;
				r.maxAtomicCounterBufferSize = 16384;
				r.maxTransformFeedbackBuffers = 4;
				r.maxTransformFeedbackInterleavedComponents = 64;
				r.maxCullDistances = 8;
				r.maxCombinedClipAndCullDistances = 8;
				r.maxSamples = 4;
				r.maxMeshOutputVerticesNV = 256;
				r.maxMeshOutputPrimitivesNV = 512;
				r.maxMeshWorkGroupSizeX_NV = 32;
				r.maxMeshWorkGroupSizeY_NV = 1;
				r.maxMeshWorkGroupSizeZ_NV = 1;
				r.maxTaskWorkGroupSizeX_NV = 32;
				r.maxTaskWorkGroupSizeY_NV = 1;
				r.maxTaskWorkGroupSizeZ_NV = 1;
				r.maxMeshViewCountNV = 4;
#if defined(GLSLANG_VERSION_MAJOR) && GLSLANG_VERSION_MAJOR > 11
				// 新版 glslang 增加 EXT mesh/task 字段（发行版 11.x 结构无，条件跳过）
				r.maxMeshOutputVerticesEXT = 256;
				r.maxMeshOutputPrimitivesEXT = 512;
				r.maxMeshWorkGroupSizeX_EXT = 32;
				r.maxMeshWorkGroupSizeY_EXT = 1;
				r.maxMeshWorkGroupSizeZ_EXT = 1;
				r.maxTaskWorkGroupSizeX_EXT = 32;
				r.maxTaskWorkGroupSizeY_EXT = 1;
				r.maxTaskWorkGroupSizeZ_EXT = 1;
				r.maxMeshViewCountEXT = 4;
#endif
				r.maxDualSourceDrawBuffersEXT = 1;

				r.limits.nonInductiveForLoops = 1;
				r.limits.whileLoops = 1;
				r.limits.doWhileLoops = 1;
				r.limits.generalUniformIndexing = 1;
				r.limits.generalAttributeMatrixVectorIndexing = 1;
				r.limits.generalVaryingIndexing = 1;
				r.limits.generalSamplerIndexing = 1;
				r.limits.generalVariableIndexing = 1;
				r.limits.generalConstantMatrixVectorIndexing = 1;
				return r;
			}

			EShLanguage to_glslang_stage(spirv_stage stage)
			{
				switch (stage)
				{
				case spirv_stage::vertex: return EShLangVertex;
				case spirv_stage::fragment: return EShLangFragment;
				case spirv_stage::compute: return EShLangCompute;
				default: return EShLangVertex;
				}
			}
		}

		void spirv_initialize()
		{
			glslang::InitializeProcess();
		}

		void spirv_finalize()
		{
			glslang::FinalizeProcess();
		}

		bool compile_glsl_to_spv(const std::string& source, spirv_stage stage,
			std::vector<std::uint32_t>& spv, std::string& error)
		{
			EShLanguage lang = to_glslang_stage(stage);

			const char* src = source.c_str();
			glslang::TShader shader(lang);
			shader.setStrings(&src, 1);
			shader.setEnvInput(glslang::EShSourceGlsl, lang, glslang::EShClientVulkan, 100);
			shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_0);
			shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);

			TBuiltInResource resources = default_resource_limits();
			if (!shader.parse(&resources, 100, false, EShMsgSpvRules))
			{
				error = shader.getInfoLog();
				return false;
			}

			glslang::TProgram program;
			program.addShader(&shader);
			if (!program.link(EShMsgSpvRules))
			{
				error = program.getInfoLog();
				return false;
			}

			glslang::GlslangToSpv(*program.getIntermediate(lang), spv);
			return !spv.empty();
		}
	}
}
