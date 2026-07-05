/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */


#include <RHI.Builders/ShaderPlatformInterface.h>
#include <RHI.Builders/DxcCompilerLibrary.h>

#include <Atom/RHI.Edit/Utils.h>
#include <Atom/RHI.Reflect/DX12/PipelineLayoutDescriptor.h>
#include <Atom/RHI.Reflect/DX12/ShaderStageFunction.h>
#include <Atom/RHI/RHIUtils.h>

#include <AzCore/IO/FileIO.h>
#include <AzCore/IO/SystemFile.h>
#include <AzCore/Serialization/Json/JsonUtils.h>
#include <AzCore/std/chrono/chrono.h>
#include <AzFramework/StringFunc/StringFunc.h>

namespace AZ
{
    namespace DX12
    {
        static const char* DX12ApiName = "dx12";
        static const char* DX12ShaderPlatformName = "DX12ShaderPlatform";
        static const char* PlatformShaderHeader = "Builders/ShaderHeaders/Platform/Windows/DX12/PlatformHeader.hlsli";
        static const char* AzslShaderHeader = "Builders/ShaderHeaders/Platform/Windows/DX12/AzslcHeader.azsli";

        ShaderPlatformInterface::ShaderPlatformInterface(uint32_t apiUniqueIndex)
            : RHI::ShaderPlatformInterface(apiUniqueIndex), m_apiName{ DX12ApiName }
        {
        }

        RHI::APIType ShaderPlatformInterface::GetAPIType() const
        {
            return RHI::APIType{ DX12ApiName };
        }

        AZ::Name ShaderPlatformInterface::GetAPIName() const
        {
            return m_apiName;
        }

        RHI::Ptr<RHI::ShaderStageFunction> ShaderPlatformInterface::CreateShaderStageFunction(const StageDescriptor& stageDescriptor)
        {
            RHI::Ptr<ShaderStageFunction> newShaderStageFunction =  ShaderStageFunction::Create(RHI::ToRHIShaderStage(stageDescriptor.m_stageType));

            const auto& byteCode = stageDescriptor.m_byteCode;
            const int byteCodeIndex = 0;
            newShaderStageFunction->SetByteCode(byteCodeIndex, byteCode);

            // Read the json data with the specialization constants offsets.
            // If the shader was not compiled with specialization constants this attribute will be empty.
            AZStd::string fileName;
            if (!stageDescriptor.m_extraData.empty())
            {
                auto jsonOutcome = JsonSerializationUtils::ReadJsonFile(stageDescriptor.m_extraData);
                if (!jsonOutcome.IsSuccess())
                {
                    AZ_Error(DX12ShaderPlatformName, false, "%s", jsonOutcome.GetError().c_str());
                    return nullptr;
                }

                const rapidjson::Document& doc = jsonOutcome.GetValue();
                ShaderStageFunction::SpecializationOffsets offsets;
                for (auto itr = doc.MemberBegin(); itr != doc.MemberEnd(); ++itr)
                {
                    if (!AZ::StringFunc::LooksLikeInt(itr->name.GetString()))
                    {
                        AZ_Error(DX12ShaderPlatformName, false, "SpecializationId %s is not an Int", itr->name.GetString());
                        continue;
                    }
                    uint32_t specializationId = static_cast<uint32_t>(AZ::StringFunc::ToInt(itr->name.GetString()));
                    uint32_t offset = itr->value.GetUint();
                    offsets[specializationId] = offset;
                }
                newShaderStageFunction->SetSpecializationOffsets(byteCodeIndex, offsets);
            }
         
            newShaderStageFunction->Finalize();

            return newShaderStageFunction;
        }

        bool ShaderPlatformInterface::IsShaderStageForRaster(RHI::ShaderHardwareStage shaderStageType) const
        {
            bool hasRasterProgram = false;

            hasRasterProgram |= shaderStageType == RHI::ShaderHardwareStage::Vertex;
            hasRasterProgram |= shaderStageType == RHI::ShaderHardwareStage::Fragment;
            hasRasterProgram |= shaderStageType == RHI::ShaderHardwareStage::Geometry;

            return hasRasterProgram;
        }

        bool ShaderPlatformInterface::IsShaderStageForCompute(RHI::ShaderHardwareStage shaderStageType) const
        {
            return (shaderStageType == RHI::ShaderHardwareStage::Compute);
        }

        bool ShaderPlatformInterface::IsShaderStageForRayTracing(RHI::ShaderHardwareStage shaderStageType) const
        {
            return (shaderStageType == RHI::ShaderHardwareStage::RayTracing);
        }

        RHI::Ptr<RHI::PipelineLayoutDescriptor> ShaderPlatformInterface::CreatePipelineLayoutDescriptor()
        {
            return PipelineLayoutDescriptor::Create();
        }

        bool ShaderPlatformInterface::BuildPipelineLayoutDescriptor(
            RHI::Ptr<RHI::PipelineLayoutDescriptor> pipelineLayoutDescriptorBase,
            const ShaderResourceGroupInfoList& srgInfoList,
            const RootConstantsInfo& rootConstantsInfo,
            const RHI::ShaderBuildArguments& shaderBuildArguments)
        {
            PipelineLayoutDescriptor* pipelineLayoutDescriptor = azrtti_cast<PipelineLayoutDescriptor*>(pipelineLayoutDescriptorBase.get());
            AZ_Assert(pipelineLayoutDescriptor, "PipelineLayoutDescriptor should have been created by now");

            for (const ShaderResourceGroupInfo& srgInfo : srgInfoList)
            {
                ShaderResourceGroupVisibility srgVisibility;
                // Copy the resources binding info so we can erase the static samplers 
                // while adding them to the m_staticSamplersShaderStageMask list.
                // Each static sampler has it's own visibility. All other resources share the same visibility mask.
                auto resourcesBindingInfo = srgInfo.m_bindingInfo.m_resourcesRegisterMap;
                for (const RHI::ShaderInputStaticSamplerDescriptor& staticSamplerDescriptor : srgInfo.m_layout->GetStaticSamplers())
                {
                    auto findIt = resourcesBindingInfo.find(staticSamplerDescriptor.m_name);
                    if (findIt != resourcesBindingInfo.end())
                    {
                        // Erase the static sampler from the resource list so we don't use it when calculating
                        // the descriptor table shader stage mask.
                        resourcesBindingInfo.erase(findIt);
                    }
                    else
                    {
                        AZ_Error(DX12ShaderPlatformName, false, "Could not find binding info for static sampler '%s'", staticSamplerDescriptor.m_name.GetCStr());
                        return false;
                    }
                }

                const bool dxcDisableOptimizations = RHI::ShaderBuildArguments::HasArgument(shaderBuildArguments.m_dxcArguments, "-Od");
                if (dxcDisableOptimizations)
                {
                    // When optimizations are disabled (-Od), all resources declared in the source file are available to all stages
                    // (when enabled only the resources which are referenced in a stage are bound to the stage)
                    srgVisibility.m_descriptorTableShaderStageMask = RHI::ShaderStageMask::All;                    
                }
                else
                {
                    for (const auto& bindInfo : resourcesBindingInfo)
                    {
                        srgVisibility.m_descriptorTableShaderStageMask |= bindInfo.second.m_shaderStageMask;
                    }

                    srgVisibility.m_descriptorTableShaderStageMask |= srgInfo.m_bindingInfo.m_constantDataBindingInfo.m_shaderStageMask;
                }

                pipelineLayoutDescriptor->AddShaderResourceGroupVisibility(srgVisibility);

                if (rootConstantsInfo.m_totalSizeInBytes > 0)
                {
                    AZ_Assert((rootConstantsInfo.m_totalSizeInBytes % 4) == 0, "Inline constant size is not a multiple of 32 bit");
                    pipelineLayoutDescriptor->SetRootConstantBinding(RootConstantBinding{ rootConstantsInfo.m_totalSizeInBytes / 4, rootConstantsInfo.m_registerId, rootConstantsInfo.m_spaceId });
                }           
            }

            return pipelineLayoutDescriptor->Finalize() == RHI::ResultCode::Success;
        }

        bool ShaderPlatformInterface::CompilePlatformInternal(
            [[maybe_unused]] const AssetBuilderSDK::PlatformInfo& platform,
            const AZStd::string& shaderSourcePath,
            const AZStd::string& functionName,
            RHI::ShaderHardwareStage shaderStage,
            const AZStd::string& tempFolderPath,
            StageDescriptor& outputDescriptor,
            const RHI::ShaderBuildArguments& shaderBuildArguments,
            const bool useSpecializationConstants) const
        {
            AZStd::vector<uint8_t> shaderByteCode;
            AZStd::string specializationOffsetsFile;
            // Compile HLSL shader to byte code
            bool compiledSucessfully = CompileHLSLShader(
                shaderSourcePath,                        // shader source filepath
                tempFolderPath,                          // AP job temp folder
                functionName,                            // name of function that is the entry point
                shaderStage,                             // shader stage (vertex shader, pixel shader, ...)
                shaderBuildArguments,
                shaderByteCode,                          // compiled shader output
                outputDescriptor.m_byProducts,           // dynamic branch count output & byproduct files
                specializationOffsetsFile,               // path to the json file with the specialization offsets
                useSpecializationConstants);             // if the shader stage it's using specialization constants

            if (!compiledSucessfully)
            {
                AZ_Error(DX12ShaderPlatformName, false, "Failed to compile HLSL shader");
                return false;
            }

            const char byteCodeHeader[] = { 'D', 'X', 'B', 'C' };
            if (shaderByteCode.size() > sizeof(byteCodeHeader) && memcmp(shaderByteCode.data(), byteCodeHeader, sizeof(byteCodeHeader)) == 0)
            {
                outputDescriptor.m_stageType = shaderStage;
                outputDescriptor.m_byteCode = AZStd::move(shaderByteCode);
                outputDescriptor.m_extraData = AZStd::move(specializationOffsetsFile);
            }
            else
            {
                AZ_Error(DX12ShaderPlatformName, false, "Compiled shader for %s is invalid", shaderSourcePath.c_str());
                return false;
            }

            return true;
        }

        const char* ShaderPlatformInterface::GetAzslHeader(const AssetBuilderSDK::PlatformInfo& platform) const
        {
            AZ_UNUSED(platform);
            return AzslShaderHeader;
        }

        bool ShaderPlatformInterface::CompileHLSLShader(
            const AZStd::string& shaderSourceFile,
            const AZStd::string& tempFolder,
            const AZStd::string& entryPoint,
            const RHI::ShaderHardwareStage shaderStageType,
            const RHI::ShaderBuildArguments& shaderBuildArguments,
            AZStd::vector<uint8_t>& compiledShader,
            ByProducts& byProducts,
            AZStd::string& specializationOffsetsFile,
            const bool useSpecializationConstants) const
        {
            // Shader compiler executable
            const auto dxcRelativePath = RHI::GetDirectXShaderCompilerPath("Builders/DirectXShaderCompiler/dxc.exe");

            // NOTE:
            // Running DX12 on PC with DXIL shaders requires modern GPUs and at least Windows 10 Build 1803 or later for Shader Model 6.2
            // https://github.com/Microsoft/DirectXShaderCompiler/wiki/Running-Shaders

            // -Fo "Output object file"
            AZStd::string shaderOutputFile;
            AzFramework::StringFunc::Path::GetFileName(shaderSourceFile.c_str(), shaderOutputFile);
            AzFramework::StringFunc::Path::Join(tempFolder.c_str(), shaderOutputFile.c_str(), shaderOutputFile);
            AzFramework::StringFunc::Path::ReplaceExtension(shaderOutputFile, "dxil.bin");

            // -Fh "Output header file containing object code", used for counting dynamic branches
            AZStd::string objectCodeOutputFile;
            AzFramework::StringFunc::Path::GetFileName(shaderSourceFile.c_str(), objectCodeOutputFile);
            AzFramework::StringFunc::Path::Join(tempFolder.c_str(), objectCodeOutputFile.c_str(), objectCodeOutputFile);
            AzFramework::StringFunc::Path::ReplaceExtension(objectCodeOutputFile, "dxil.txt");

            // Stage profile name parameter
            // Note: RayTracing shaders must be compiled with version 6_3, while the rest of the stages
            // are compiled with version 6_2, so RayTracing cannot share the version constant.
            const AZStd::string shaderModelVersion = "6_2";
            const AZStd::unordered_map<RHI::ShaderHardwareStage, AZStd::string> stageToProfileName =
            {
                {RHI::ShaderHardwareStage::Vertex,                 "vs_" + shaderModelVersion},
                {RHI::ShaderHardwareStage::Fragment,               "ps_" + shaderModelVersion},
                {RHI::ShaderHardwareStage::Compute,                "cs_" + shaderModelVersion},
                {RHI::ShaderHardwareStage::Geometry,               "gs_" + shaderModelVersion},
                {RHI::ShaderHardwareStage::RayTracing,             "lib_6_3"}
            };
            auto profileIt = stageToProfileName.find(shaderStageType);
            if (profileIt == stageToProfileName.end())
            {
                AZ_Error(DX12ShaderPlatformName, false, "Unsupported shader stage");
                return false;
            }

            const bool graphicsDevMode = RHI::IsGraphicsDevModeEnabled();

            // Compilation parameters
            auto dxcArguments = shaderBuildArguments.m_dxcArguments;
            if (graphicsDevMode || BuildHasDebugInfo(shaderBuildArguments))
            {
                RHI::ShaderBuildArguments::AppendArguments(dxcArguments, { "-Zi", "-Zss", "-Od" });
            }

            unsigned char sha1[RHI::Sha1NumBytes];
            RHI::PrependArguments args;
            args.m_sourceFile = shaderSourceFile.c_str();
            args.m_prependFile = PlatformShaderHeader;
            args.m_destinationFolder = tempFolder.c_str();
            args.m_digest = &sha1;

            const auto dxcInputFile = RHI::PrependFile(args);  // Prepend PAL header & obtain hash

            const bool needDebugInfo = graphicsDevMode || shaderBuildArguments.m_generateDebugInfo;

            // Compute PDB file path for debug builds (needed by both library and process paths)
            AZStd::string symbolDatabaseFilePath;
            if (needDebugInfo)
            {
                AZStd::string sha1hex = RHI::ByteToHexString(sha1);
                symbolDatabaseFilePath = dxcInputFile.c_str();
                AZStd::string pdbFileName = sha1hex + "-" + profileIt->second;
                AzFramework::StringFunc::Path::ReplaceFullName(symbolDatabaseFilePath, pdbFileName.c_str(), "pdb");
                if (AZ::IO::SystemFile::Exists(symbolDatabaseFilePath.c_str()))
                {
                    AZ_Warning(DX12ShaderPlatformName, false, "debug symbol file %s already exists -> skipping PDB write", symbolDatabaseFilePath.c_str());
                    symbolDatabaseFilePath.clear();
                }
            }

            // Entry point argument (empty for ray tracing library shaders)
            const bool isRayTracing = (shaderStageType == RHI::ShaderHardwareStage::RayTracing);

            const auto compileStartTime = AZStd::chrono::high_resolution_clock::now();

            // =====================================================================
            // Try in-process DXC library compilation first (eliminates process spawn overhead)
            // =====================================================================
            auto& dxcLib = DxcCompilerLibrary::Instance();
            if (dxcLib.IsAvailable())
            {
                // Load the prepended source file into memory
                auto sourceLoadResult = AZ::RHI::LoadFileBytes(dxcInputFile.c_str());
                if (!sourceLoadResult)
                {
                    AZ_Error(DX12ShaderPlatformName, false, "Failed to load shader source: %s",
                        sourceLoadResult.GetError().c_str());
                    return false;
                }
                const auto& sourceBytes = sourceLoadResult.GetValue();

                // Build the DXC argument list (same flags as the command line, minus file-output args)
                AZStd::vector<AZStd::string> libArgs;
                if (!isRayTracing)
                {
                    libArgs.push_back("-E");
                    libArgs.push_back(entryPoint);
                }
                libArgs.push_back("-T");
                libArgs.push_back(profileIt->second);
                // Add all user/build arguments (e.g. -Od, -Zi, -Zss, defines, etc.)
                for (const auto& arg : dxcArguments)
                {
                    libArgs.push_back(arg);
                }

                auto compileResult = dxcLib.Compile(
                    sourceBytes.data(), sourceBytes.size(), libArgs, needDebugInfo);

                if (!compileResult.m_succeeded)
                {
                    // Report errors in the same format as ExecuteShaderCompiler
                    AZ_Error(DX12ShaderPlatformName, false, "DXC compilation failed for '%s':\n%s",
                        shaderSourceFile.c_str(), compileResult.m_errors.c_str());
                    return false;
                }

                // Log warnings if any
                if (!compileResult.m_errors.empty())
                {
                    AZ_Warning(DX12ShaderPlatformName, false, "DXC warnings for '%s':\n%s",
                        shaderSourceFile.c_str(), compileResult.m_errors.c_str());
                }

                // Handle specialization constants (still requires dxsc.exe with file-based I/O)
                if (useSpecializationConstants)
                {
                    // Write the compiled bytecode to disk so dxsc.exe can read it
                    {
                        AZ::IO::SystemFile outFile;
                        if (!outFile.Open(shaderOutputFile.c_str(),
                            AZ::IO::SystemFile::SF_OPEN_CREATE | AZ::IO::SystemFile::SF_OPEN_WRITE_ONLY))
                        {
                            AZ_Error(DX12ShaderPlatformName, false,
                                "Failed to write shader output for dxsc: %s", shaderOutputFile.c_str());
                            return false;
                        }
                        outFile.Write(compileResult.m_objectCode.data(), compileResult.m_objectCode.size());
                        outFile.Close();
                    }

                    // Run dxsc.exe for specialization constant patching
                    const auto dxscRelativePath = RHI::GetDirectXShaderCompilerPath("Builders/DirectXShaderCompiler/dxsc.exe");

                    AZStd::string shaderOutputCommon;
                    AzFramework::StringFunc::Path::GetFileName(shaderSourceFile.c_str(), shaderOutputCommon);
                    AzFramework::StringFunc::Path::Join(tempFolder.c_str(), shaderOutputCommon.c_str(), shaderOutputCommon);

                    AZStd::string patchedShaderOutput = shaderOutputCommon;
                    AzFramework::StringFunc::Path::ReplaceExtension(patchedShaderOutput, "dxil.patched.bin");
                    AZStd::string offsetsOutput = shaderOutputCommon;
                    AzFramework::StringFunc::Path::ReplaceExtension(offsetsOutput, "offsets.json");

                    const auto dxscCommandOptions = AZStd::string::format(
                        "-sv=%lu -o=\"%s\" -f=\"%s\" \"%s\"",
                        static_cast<unsigned long>(SCSentinelValue),
                        patchedShaderOutput.c_str(),
                        offsetsOutput.c_str(),
                        shaderOutputFile.c_str());

                    if (!RHI::ExecuteShaderCompiler(dxscRelativePath, dxscCommandOptions, shaderSourceFile, tempFolder, "DXSC"))
                    {
                        return false;
                    }

                    // Read the patched output
                    auto patchedLoadResult = AZ::RHI::LoadFileBytes(patchedShaderOutput.c_str());
                    if (!patchedLoadResult)
                    {
                        AZ_Error(DX12ShaderPlatformName, false, "%s", patchedLoadResult.GetError().c_str());
                        return false;
                    }
                    compiledShader = patchedLoadResult.TakeValue();
                    specializationOffsetsFile = offsetsOutput;
                }
                else
                {
                    // No specialization — use the in-memory bytecode directly (no file I/O needed)
                    compiledShader = AZStd::move(compileResult.m_objectCode);
                }

                // Count dynamic branches from the DXIL disassembly
                if (!compileResult.m_disassemblyText.empty())
                {
                    byProducts.m_dynamicBranchCount = aznumeric_cast<uint32_t>(
                        AZ::RHI::RegexCount(compileResult.m_disassemblyText, "^ *(br|indirectbr|switch) "));
                }
                else
                {
                    byProducts.m_dynamicBranchCount = ByProducts::UnknownDynamicBranchCount;
                }

                // Write PDB to disk if debug info was generated
                if (needDebugInfo && !compileResult.m_pdbData.empty() && !symbolDatabaseFilePath.empty())
                {
                    AZ::IO::SystemFile pdbFile;
                    if (pdbFile.Open(symbolDatabaseFilePath.c_str(),
                        AZ::IO::SystemFile::SF_OPEN_CREATE | AZ::IO::SystemFile::SF_OPEN_WRITE_ONLY))
                    {
                        pdbFile.Write(compileResult.m_pdbData.data(), compileResult.m_pdbData.size());
                        pdbFile.Close();
                        byProducts.m_intermediatePaths.emplace(AZStd::move(symbolDatabaseFilePath));
                    }
                }

                // Write disassembly text to file for debug byproducts (matches original -Fh behavior)
                if (needDebugInfo && !compileResult.m_disassemblyText.empty())
                {
                    AZ::IO::SystemFile disasmFile;
                    if (disasmFile.Open(objectCodeOutputFile.c_str(),
                        AZ::IO::SystemFile::SF_OPEN_CREATE | AZ::IO::SystemFile::SF_OPEN_WRITE_ONLY))
                    {
                        disasmFile.Write(compileResult.m_disassemblyText.data(), compileResult.m_disassemblyText.size());
                        disasmFile.Close();
                        byProducts.m_intermediatePaths.emplace(AZStd::move(objectCodeOutputFile));
                    }
                }

                const auto compileEndTime = AZStd::chrono::high_resolution_clock::now();
                const auto compileMs = AZStd::chrono::duration_cast<AZStd::chrono::milliseconds>(compileEndTime - compileStartTime).count();
                AZ_TracePrintf(DX12ShaderPlatformName, "DXC LIBRARY compile '%s' [%s] took %lld ms\n",
                    shaderSourceFile.c_str(), profileIt->second.c_str(), static_cast<long long>(compileMs));

                return true;
            }

            // =====================================================================
            // Fallback: process-based compilation via dxc.exe (original code path)
            // =====================================================================
            AZStd::string symbolDatabaseFileCliArgument{" "};
            if (needDebugInfo && !symbolDatabaseFilePath.empty())
            {
                symbolDatabaseFileCliArgument = " -Fd \"" + symbolDatabaseFilePath + "\" ";
                byProducts.m_intermediatePaths.emplace(symbolDatabaseFilePath);
            }

            const auto params = RHI::ShaderBuildArguments::ListAsString(dxcArguments);
            const auto dxcEntryPoint = isRayTracing ? "" : AZStd::string::format("-E %s", entryPoint.c_str());
            const auto dxcCommandOptions = AZStd::string::format("%s -T %s %s -Fo \"%s\" -Fh \"%s\"%s\"%s\"",
                                                                 dxcEntryPoint.c_str(),
                                                                 profileIt->second.c_str(),
                                                                 params.c_str(),
                                                                 shaderOutputFile.c_str(),
                                                                 objectCodeOutputFile.c_str(),
                                                                 symbolDatabaseFileCliArgument.c_str(),
                                                                 dxcInputFile.c_str());

            if (!RHI::ExecuteShaderCompiler(dxcRelativePath, dxcCommandOptions, shaderSourceFile, tempFolder, "DXC"))
            {
                return false;
            }

            if (useSpecializationConstants)
            {
                const auto dxscRelativePath = RHI::GetDirectXShaderCompilerPath("Builders/DirectXShaderCompiler/dxsc.exe");

                AZStd::string shaderOutputCommon;
                AzFramework::StringFunc::Path::GetFileName(shaderSourceFile.c_str(), shaderOutputCommon);
                AzFramework::StringFunc::Path::Join(tempFolder.c_str(), shaderOutputCommon.c_str(), shaderOutputCommon);

                AZStd::string patchedShaderOutput = shaderOutputCommon;
                AzFramework::StringFunc::Path::ReplaceExtension(patchedShaderOutput, "dxil.patched.bin");
                AZStd::string offsetsOutput = shaderOutputCommon;
                AzFramework::StringFunc::Path::ReplaceExtension(offsetsOutput, "offsets.json");

                const auto dxscCommandOptions = AZStd::string::format(
                    "-sv=%lu -o=\"%s\" -f=\"%s\" \"%s\"",
                    static_cast<unsigned long>(SCSentinelValue),
                    patchedShaderOutput.c_str(),
                    offsetsOutput.c_str(),
                    shaderOutputFile.c_str());

                if (!RHI::ExecuteShaderCompiler(dxscRelativePath, dxscCommandOptions, shaderSourceFile, tempFolder, "DXSC"))
                {
                    return false;
                }
                shaderOutputFile = patchedShaderOutput;
                specializationOffsetsFile = offsetsOutput;
            }

            auto shaderOutputFileLoadResult = AZ::RHI::LoadFileBytes(shaderOutputFile.c_str());
            if (!shaderOutputFileLoadResult)
            {
                AZ_Error(DX12ShaderPlatformName, false, "%s", shaderOutputFileLoadResult.GetError().c_str());
                return false;
            }
            compiledShader = shaderOutputFileLoadResult.TakeValue();

            auto objectCodeLoadResult = AZ::RHI::LoadFileString(objectCodeOutputFile.c_str());
            if (objectCodeLoadResult)
            {
                byProducts.m_dynamicBranchCount = aznumeric_cast<uint32_t>(AZ::RHI::RegexCount(objectCodeLoadResult.GetValue(), "^ *(br|indirectbr|switch) "));
            }
            else
            {
                byProducts.m_dynamicBranchCount = ByProducts::UnknownDynamicBranchCount;
            }

            if (needDebugInfo)
            {
                byProducts.m_intermediatePaths.emplace(AZStd::move(objectCodeOutputFile));
            }

            const auto compileEndTime = AZStd::chrono::high_resolution_clock::now();
            const auto compileMs = AZStd::chrono::duration_cast<AZStd::chrono::milliseconds>(compileEndTime - compileStartTime).count();
            AZ_TracePrintf(DX12ShaderPlatformName, "DXC PROCESS compile '%s' [%s] took %lld ms\n",
                shaderSourceFile.c_str(), profileIt->second.c_str(), static_cast<long long>(compileMs));

            return true;
        }
    }
}
