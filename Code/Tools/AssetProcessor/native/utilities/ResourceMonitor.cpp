/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <native/utilities/ResourceMonitor.h>

#if defined(AZ_PLATFORM_WINDOWS)
#include <AzCore/PlatformIncl.h>
#include <winioctl.h>
#include <Psapi.h>
#endif

namespace AssetProcessor
{
    ResourceMonitor::ResourceMonitor()
    {
#if defined(AZ_PLATFORM_WINDOWS)
        // Take initial CPU sample so first delta is valid
        FILETIME idleTime, kernelTime, userTime;
        if (GetSystemTimes(&idleTime, &kernelTime, &userTime))
        {
            m_prevIdleTime = (static_cast<AZ::u64>(idleTime.dwHighDateTime) << 32) | idleTime.dwLowDateTime;
            m_prevKernelTime = (static_cast<AZ::u64>(kernelTime.dwHighDateTime) << 32) | kernelTime.dwLowDateTime;
            m_prevUserTime = (static_cast<AZ::u64>(userTime.dwHighDateTime) << 32) | userTime.dwLowDateTime;
            m_hasPrevCpuSample = true;
        }
        m_prevDiskSampleTime = AZStd::chrono::steady_clock::now();
#endif
    }

    void ResourceMonitor::Sample()
    {
#if defined(AZ_PLATFORM_WINDOWS)
        // --- CPU utilization via GetSystemTimes ---
        FILETIME idleTime, kernelTime, userTime;
        if (GetSystemTimes(&idleTime, &kernelTime, &userTime))
        {
            AZ::u64 idle = (static_cast<AZ::u64>(idleTime.dwHighDateTime) << 32) | idleTime.dwLowDateTime;
            AZ::u64 kernel = (static_cast<AZ::u64>(kernelTime.dwHighDateTime) << 32) | kernelTime.dwLowDateTime;
            AZ::u64 user = (static_cast<AZ::u64>(userTime.dwHighDateTime) << 32) | userTime.dwLowDateTime;

            if (m_hasPrevCpuSample)
            {
                AZ::u64 deltaIdle = idle - m_prevIdleTime;
                AZ::u64 deltaKernel = kernel - m_prevKernelTime;
                AZ::u64 deltaUser = user - m_prevUserTime;

                // kernelTime includes idle time, so total active = kernel + user - idle
                AZ::u64 deltaTotal = deltaKernel + deltaUser;
                if (deltaTotal > 0)
                {
                    m_current.m_cpuPercent = (1.0f - static_cast<float>(deltaIdle) / static_cast<float>(deltaTotal)) * 100.0f;
                }
            }

            m_prevIdleTime = idle;
            m_prevKernelTime = kernel;
            m_prevUserTime = user;
            m_hasPrevCpuSample = true;
        }

        // --- Available physical memory ---
        MEMORYSTATUSEX memInfo;
        memInfo.dwLength = sizeof(MEMORYSTATUSEX);
        if (GlobalMemoryStatusEx(&memInfo))
        {
            m_current.m_availableMemoryMB = memInfo.ullAvailPhys / (1024 * 1024);
        }

        // --- Disk I/O via IOCTL_DISK_PERFORMANCE on PhysicalDrive0 ---
        auto now = AZStd::chrono::steady_clock::now();
        HANDLE hDisk = CreateFileW(
            L"\\\\.\\PhysicalDrive0",
            0, // No read/write access needed
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr);

        if (hDisk != INVALID_HANDLE_VALUE)
        {
            DISK_PERFORMANCE diskPerf;
            DWORD bytesReturned = 0;
            if (DeviceIoControl(hDisk, IOCTL_DISK_PERFORMANCE, nullptr, 0, &diskPerf, sizeof(diskPerf), &bytesReturned, nullptr))
            {
                AZ::u64 bytesRead = diskPerf.BytesRead.QuadPart;
                AZ::u64 bytesWritten = diskPerf.BytesWritten.QuadPart;

                if (m_hasPrevDiskSample)
                {
                    auto elapsed = AZStd::chrono::duration<float>(now - m_prevDiskSampleTime).count();
                    if (elapsed > 0.0f)
                    {
                        AZ::u64 deltaBytes = (bytesRead - m_prevDiskBytesRead) + (bytesWritten - m_prevDiskBytesWritten);
                        m_current.m_diskIoBytesPerSec = static_cast<float>(deltaBytes) / elapsed;
                    }
                }

                m_prevDiskBytesRead = bytesRead;
                m_prevDiskBytesWritten = bytesWritten;
                m_hasPrevDiskSample = true;
                m_prevDiskSampleTime = now;
            }
            CloseHandle(hDisk);
        }

        // --- Context switches ---
        // Context switch monitoring is available but requires NtQuerySystemInformation
        // which is undocumented. For now, we leave this at 0 and rely on CPU/memory/IO
        // as the primary saturation indicators. This can be extended later if needed.
        m_current.m_contextSwitchesPerSec = 0.0f;

#else
        // Non-Windows platforms: report unsaturated defaults
        m_current.m_cpuPercent = 0.0f;
        m_current.m_availableMemoryMB = 8192;
        m_current.m_diskIoBytesPerSec = 0.0f;
        m_current.m_contextSwitchesPerSec = 0.0f;
#endif
    }

    ResourceSnapshot ResourceMonitor::GetSnapshot() const
    {
        return m_current;
    }

    bool ResourceMonitor::IsSaturated(float cpuThreshold, AZ::u64 memoryMinMB, [[maybe_unused]] float contextSwitchBaselineMultiplier) const
    {
        if (m_current.m_cpuPercent > cpuThreshold)
        {
            return true;
        }

        if (m_current.m_availableMemoryMB < memoryMinMB)
        {
            return true;
        }

        return false;
    }
} // namespace AssetProcessor
