/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <RHI.Builders/DxcCompilerLibrary.h>

// Windows COM headers required by dxcapi.h (IUnknown, BSTR, etc.)
#include <AzCore/PlatformIncl.h>
#include <unknwn.h>
#include <OAIdl.h>
#include <dxcapi.h>

#include <Atom/RHI.Edit/Utils.h>
#include <AzCore/Component/ComponentApplicationBus.h>
#include <AzFramework/StringFunc/StringFunc.h>

#include <string>
#include <vector>

namespace AZ
{
    namespace DX12
    {
        static const char* DxcLibraryLogName = "DxcCompilerLibrary";

        // Helper: convert UTF-8 narrow string to UTF-16 wide string
        static std::wstring ToWide(const AZStd::string& str)
        {
            if (str.empty())
            {
                return {};
            }
            int wlen = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), nullptr, 0);
            std::wstring wstr(wlen, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), wstr.data(), wlen);
            return wstr;
        }

        DxcCompilerLibrary& DxcCompilerLibrary::Instance()
        {
            static DxcCompilerLibrary instance;
            return instance;
        }

        DxcCompilerLibrary::~DxcCompilerLibrary()
        {
            if (m_compiler)
            {
                static_cast<IDxcCompiler3*>(m_compiler)->Release();
                m_compiler = nullptr;
            }
            if (m_utils)
            {
                static_cast<IDxcUtils*>(m_utils)->Release();
                m_utils = nullptr;
            }
            if (m_dxcModule)
            {
                FreeLibrary(static_cast<HMODULE>(m_dxcModule));
                m_dxcModule = nullptr;
            }
        }

        bool DxcCompilerLibrary::IsAvailable()
        {
            AZStd::lock_guard<AZStd::mutex> lock(m_initMutex);
            if (!m_initialized)
            {
                m_available = Initialize();
                m_initialized = true;
            }
            return m_available;
        }

        bool DxcCompilerLibrary::Initialize()
        {
            // Resolve the path to dxcompiler.dll shipped alongside dxc.exe in the O3DE build
            const auto dxcRelativePath = RHI::GetDirectXShaderCompilerPath("Builders/DirectXShaderCompiler/dxc.exe");

            const char* executableFolder = nullptr;
            AZ::ComponentApplicationBus::BroadcastResult(
                executableFolder, &AZ::ComponentApplicationBus::Events::GetExecutableFolder);
            if (!executableFolder)
            {
                AZ_Warning(DxcLibraryLogName, false, "Cannot determine executable folder");
                return false;
            }

            AZStd::string dxcExeAbsPath;
            AzFramework::StringFunc::Path::Join(executableFolder, dxcRelativePath.c_str(), dxcExeAbsPath);

            // Replace the exe filename with dxcompiler.dll
            AZStd::string dxcDir;
            AzFramework::StringFunc::Path::GetFullPath(dxcExeAbsPath.c_str(), dxcDir);

            AZStd::string dxcDllPath;
            AzFramework::StringFunc::Path::Join(dxcDir.c_str(), "dxcompiler.dll", dxcDllPath);

            // Use LoadLibraryEx with LOAD_WITH_ALTERED_SEARCH_PATH so that dxil.dll
            // (a dependency of dxcompiler.dll) is found in the same directory.
            HMODULE module = LoadLibraryExA(
                dxcDllPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
            if (!module)
            {
                AZ_Warning(DxcLibraryLogName, false,
                    "Failed to load dxcompiler.dll from '%s' (error %lu). "
                    "Falling back to process-based compilation.",
                    dxcDllPath.c_str(), GetLastError());
                return false;
            }
            m_dxcModule = module;

            // Resolve DxcCreateInstance entry point
            auto createInstance = reinterpret_cast<DxcCreateInstanceProc>(
                GetProcAddress(module, "DxcCreateInstance"));
            if (!createInstance)
            {
                AZ_Warning(DxcLibraryLogName, false,
                    "DxcCreateInstance not found in dxcompiler.dll. "
                    "Falling back to process-based compilation.");
                return false;
            }

            // Create the compiler instance (thread-safe for concurrent Compile() calls)
            IDxcCompiler3* compiler = nullptr;
            HRESULT hr = createInstance(CLSID_DxcCompiler, __uuidof(IDxcCompiler3),
                reinterpret_cast<void**>(&compiler));
            if (FAILED(hr) || !compiler)
            {
                AZ_Warning(DxcLibraryLogName, false,
                    "Failed to create IDxcCompiler3 (HRESULT 0x%08X). "
                    "Falling back to process-based compilation.", hr);
                return false;
            }
            m_compiler = compiler;

            // Create the utils instance (for include handler creation)
            IDxcUtils* utils = nullptr;
            hr = createInstance(CLSID_DxcUtils, __uuidof(IDxcUtils),
                reinterpret_cast<void**>(&utils));
            if (FAILED(hr) || !utils)
            {
                AZ_Warning(DxcLibraryLogName, false,
                    "Failed to create IDxcUtils (HRESULT 0x%08X). "
                    "Falling back to process-based compilation.", hr);
                compiler->Release();
                m_compiler = nullptr;
                return false;
            }
            m_utils = utils;

            AZ_TracePrintf(DxcLibraryLogName,
                "DXC library API initialized successfully from '%s'", dxcDllPath.c_str());
            return true;
        }

        DxcCompilerLibrary::CompileResult DxcCompilerLibrary::Compile(
            const void* sourceData,
            size_t sourceDataSize,
            const AZStd::vector<AZStd::string>& arguments,
            bool generateDebugInfo)
        {
            CompileResult result;

            auto* compiler = static_cast<IDxcCompiler3*>(m_compiler);
            auto* utils = static_cast<IDxcUtils*>(m_utils);

            // Prepare source buffer
            DxcBuffer sourceBuffer;
            sourceBuffer.Ptr = sourceData;
            sourceBuffer.Size = static_cast<SIZE_T>(sourceDataSize);
            sourceBuffer.Encoding = DXC_CP_UTF8;

            // Convert arguments to wide strings for the DXC API
            std::vector<std::wstring> wideArgStorage;
            std::vector<LPCWSTR> argPtrs;
            wideArgStorage.reserve(arguments.size());
            argPtrs.reserve(arguments.size());

            for (const auto& arg : arguments)
            {
                wideArgStorage.push_back(ToWide(arg));
                argPtrs.push_back(wideArgStorage.back().c_str());
            }

            // Create a default include handler (resolves #include from filesystem)
            IDxcIncludeHandler* includeHandler = nullptr;
            utils->CreateDefaultIncludeHandler(&includeHandler);

            // Compile
            IDxcResult* compileResult = nullptr;
            HRESULT hr = compiler->Compile(
                &sourceBuffer,
                argPtrs.data(),
                static_cast<UINT32>(argPtrs.size()),
                includeHandler,
                __uuidof(IDxcResult),
                reinterpret_cast<void**>(&compileResult));

            if (includeHandler)
            {
                includeHandler->Release();
            }

            if (FAILED(hr) || !compileResult)
            {
                result.m_errors = AZStd::string::format(
                    "DXC Compile call failed with HRESULT 0x%08X", hr);
                return result;
            }

            // Check compilation status
            HRESULT status;
            compileResult->GetStatus(&status);

            // Extract errors/warnings
            IDxcBlobUtf8* errorBlob = nullptr;
            compileResult->GetOutput(DXC_OUT_ERRORS, __uuidof(IDxcBlobUtf8),
                reinterpret_cast<void**>(&errorBlob), nullptr);
            if (errorBlob && errorBlob->GetStringLength() > 0)
            {
                result.m_errors = AZStd::string(
                    errorBlob->GetStringPointer(), errorBlob->GetStringLength());
            }
            if (errorBlob)
            {
                errorBlob->Release();
            }

            if (FAILED(status))
            {
                compileResult->Release();
                return result;
            }

            // Extract compiled shader object (DXIL binary)
            IDxcBlob* objectBlob = nullptr;
            compileResult->GetOutput(DXC_OUT_OBJECT, __uuidof(IDxcBlob),
                reinterpret_cast<void**>(&objectBlob), nullptr);
            if (objectBlob && objectBlob->GetBufferSize() > 0)
            {
                const auto* data = static_cast<const uint8_t*>(objectBlob->GetBufferPointer());
                result.m_objectCode.assign(data, data + objectBlob->GetBufferSize());

                // Get DXIL disassembly for dynamic branch counting.
                // We disassemble the compiled object rather than using -Fh file output.
                DxcBuffer objectBuffer;
                objectBuffer.Ptr = objectBlob->GetBufferPointer();
                objectBuffer.Size = objectBlob->GetBufferSize();
                objectBuffer.Encoding = 0; // binary

                IDxcResult* disasmResult = nullptr;
                HRESULT disasmHr = compiler->Disassemble(
                    &objectBuffer, __uuidof(IDxcResult),
                    reinterpret_cast<void**>(&disasmResult));
                if (SUCCEEDED(disasmHr) && disasmResult)
                {
                    IDxcBlobUtf8* disasmBlob = nullptr;
                    disasmResult->GetOutput(DXC_OUT_DISASSEMBLY, __uuidof(IDxcBlobUtf8),
                        reinterpret_cast<void**>(&disasmBlob), nullptr);
                    if (disasmBlob && disasmBlob->GetStringLength() > 0)
                    {
                        result.m_disassemblyText = AZStd::string(
                            disasmBlob->GetStringPointer(), disasmBlob->GetStringLength());
                    }
                    if (disasmBlob)
                    {
                        disasmBlob->Release();
                    }
                    disasmResult->Release();
                }
            }
            if (objectBlob)
            {
                objectBlob->Release();
            }

            // Extract PDB debug info if requested
            if (generateDebugInfo && compileResult->HasOutput(DXC_OUT_PDB))
            {
                IDxcBlob* pdbBlob = nullptr;
                compileResult->GetOutput(DXC_OUT_PDB, __uuidof(IDxcBlob),
                    reinterpret_cast<void**>(&pdbBlob), nullptr);
                if (pdbBlob && pdbBlob->GetBufferSize() > 0)
                {
                    const auto* data = static_cast<const uint8_t*>(pdbBlob->GetBufferPointer());
                    result.m_pdbData.assign(data, data + pdbBlob->GetBufferSize());
                }
                if (pdbBlob)
                {
                    pdbBlob->Release();
                }
            }

            result.m_succeeded = true;
            compileResult->Release();
            return result;
        }
    }
}
