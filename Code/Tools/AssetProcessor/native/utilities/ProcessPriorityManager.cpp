/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <native/utilities/ProcessPriorityManager.h>
#include <AzCore/Debug/Trace.h>
#include <AzCore/std/parallel/lock.h>

#if defined(AZ_PLATFORM_WINDOWS)
#include <AzCore/PlatformIncl.h>
#include <TlHelp32.h>
#endif

namespace AssetProcessor
{
    void ProcessPriorityManager::RegisterPid(AZ::u32 pid)
    {
        AZStd::lock_guard<AZStd::mutex> lock(m_mutex);
        m_trackedPids.insert(pid);
    }

    void ProcessPriorityManager::UnregisterPid(AZ::u32 pid)
    {
        AZStd::lock_guard<AZStd::mutex> lock(m_mutex);
        m_trackedPids.erase(pid);
    }

    void ProcessPriorityManager::SetAllPriority([[maybe_unused]] AZ::u32 priorityClass)
    {
#if defined(AZ_PLATFORM_WINDOWS)
        AZStd::lock_guard<AZStd::mutex> lock(m_mutex);

        if (priorityClass == m_currentPriorityClass)
        {
            return; // Already at this priority
        }

        AZStd::set<AZ::u32> deadPids;

        for (AZ::u32 pid : m_trackedPids)
        {
            HANDLE hProcess = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (hProcess)
            {
                if (!SetPriorityClass(hProcess, static_cast<DWORD>(priorityClass)))
                {
                    AZ_Warning("ProcessPriorityManager", false, "Failed to set priority for PID %u (error %u)", pid, GetLastError());
                }
                CloseHandle(hProcess);
            }
            else
            {
                // Process may have exited
                deadPids.insert(pid);
            }
        }

        // Clean up dead PIDs
        for (AZ::u32 pid : deadPids)
        {
            m_trackedPids.erase(pid);
        }

        m_currentPriorityClass = priorityClass;
#endif
    }

    bool ProcessPriorityManager::DetectExternalLoad() const
    {
#if defined(AZ_PLATFORM_WINDOWS)
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE)
        {
            return false;
        }

        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(PROCESSENTRY32W);

        bool found = false;
        if (Process32FirstW(snap, &pe))
        {
            do
            {
                if (_wcsicmp(pe.szExeFile, L"Editor.exe") == 0 ||
                    _wcsicmp(pe.szExeFile, L"GameLauncher.exe") == 0 ||
                    // O3DE project launchers follow the pattern: <ProjectName>.GameLauncher.exe
                    wcsstr(pe.szExeFile, L"GameLauncher.exe") != nullptr ||
                    wcsstr(pe.szExeFile, L"ServerLauncher.exe") != nullptr)
                {
                    found = true;
                    break;
                }
            } while (Process32NextW(snap, &pe));
        }

        CloseHandle(snap);
        return found;
#else
        return false;
#endif
    }

    AZStd::set<AZ::u32> ProcessPriorityManager::FindProcessesByName([[maybe_unused]] const char* exeName)
    {
        AZStd::set<AZ::u32> pids;

#if defined(AZ_PLATFORM_WINDOWS)
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE)
        {
            return pids;
        }

        // Convert exeName to wide string for comparison
        wchar_t wideExeName[MAX_PATH];
        size_t converted = 0;
        mbstowcs_s(&converted, wideExeName, exeName, MAX_PATH - 1);

        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(PROCESSENTRY32W);

        if (Process32FirstW(snap, &pe))
        {
            do
            {
                if (_wcsicmp(pe.szExeFile, wideExeName) == 0)
                {
                    pids.insert(pe.th32ProcessID);
                }
            } while (Process32NextW(snap, &pe));
        }

        CloseHandle(snap);
#endif
        return pids;
    }
} // namespace AssetProcessor
