/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/std/containers/vector.h>
#include <AzCore/std/string/string.h>
#include <AzCore/std/parallel/mutex.h>

namespace AZ
{
    namespace DX12
    {
        //! Provides in-process HLSL compilation via IDxcCompiler3, eliminating dxc.exe process spawning.
        //! Thread-safe: IDxcCompiler3::Compile() supports concurrent calls from multiple threads.
        //! Falls back gracefully if dxcompiler.dll cannot be loaded.
        class DxcCompilerLibrary final
        {
        public:
            struct CompileResult
            {
                AZStd::vector<uint8_t> m_objectCode;    //!< Compiled DXIL binary (equivalent to -Fo output)
                AZStd::string m_disassemblyText;         //!< DXIL disassembly text (for dynamic branch counting)
                AZStd::vector<uint8_t> m_pdbData;        //!< Debug PDB data (equivalent to -Fd output)
                AZStd::string m_errors;                   //!< Compilation errors/warnings
                bool m_succeeded = false;
            };

            static DxcCompilerLibrary& Instance();

            //! Returns true if the DXC library is loaded and ready for use.
            //! Thread-safe; initialization happens once on first call.
            bool IsAvailable();

            //! Compile HLSL source code to DXIL using the in-process DXC library.
            //! @param sourceData Pointer to UTF-8 HLSL source text
            //! @param sourceDataSize Size of source text in bytes
            //! @param arguments Complete DXC argument list (e.g. {"-E", "MainVS", "-T", "vs_6_2", "-Zi"})
            //! @param generateDebugInfo If true, PDB data will be extracted from the result
            CompileResult Compile(
                const void* sourceData,
                size_t sourceDataSize,
                const AZStd::vector<AZStd::string>& arguments,
                bool generateDebugInfo);

        private:
            DxcCompilerLibrary() = default;
            ~DxcCompilerLibrary();

            bool Initialize();

            void* m_dxcModule = nullptr;   // HMODULE to dxcompiler.dll
            void* m_compiler = nullptr;    // IDxcCompiler3*
            void* m_utils = nullptr;       // IDxcUtils*
            bool m_initialized = false;
            bool m_available = false;
            AZStd::mutex m_initMutex;
        };
    }
}
