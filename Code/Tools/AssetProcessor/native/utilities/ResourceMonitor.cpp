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
#include <Pdh.h>
#pragma comment(lib, "Pdh.lib")
#elif defined(AZ_PLATFORM_LINUX)
#include <fstream>
#include <string>
#elif defined(AZ_PLATFORM_MAC)
#include <sys/sysctl.h>
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

        // Initialize PDH query for context switches/sec
        PDH_HQUERY query = nullptr;
        if (PdhOpenQueryW(nullptr, 0, &query) == ERROR_SUCCESS)
        {
            PDH_HCOUNTER counter = nullptr;
            if (PdhAddCounterW(query, L"\\System\\Context Switches/sec", 0, &counter) == ERROR_SUCCESS)
            {
                m_pdhQuery = query;
                m_pdhCounter = counter;
                // First collect to prime the counter (PDH needs two collects for rate counters)
                PdhCollectQueryData(static_cast<PDH_HQUERY>(m_pdhQuery));
            }
            else
            {
                PdhCloseQuery(query);
            }
        }
#elif defined(AZ_PLATFORM_LINUX) || defined(AZ_PLATFORM_MAC)
        m_prevContextSwitchTime = AZStd::chrono::steady_clock::now();
#endif
    }

    ResourceMonitor::~ResourceMonitor()
    {
#if defined(AZ_PLATFORM_WINDOWS)
        if (m_pdhQuery)
        {
            PdhCloseQuery(static_cast<PDH_HQUERY>(m_pdhQuery));
            m_pdhQuery = nullptr;
        }
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
            m_current.m_memoryUsedPercent = static_cast<float>(memInfo.dwMemoryLoad);
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

        // --- Context switches via PDH ---
        if (m_pdhQuery)
        {
            if (PdhCollectQueryData(static_cast<PDH_HQUERY>(m_pdhQuery)) == ERROR_SUCCESS)
            {
                PDH_FMT_COUNTERVALUE counterValue;
                if (PdhGetFormattedCounterValue(static_cast<PDH_HCOUNTER>(m_pdhCounter),
                    PDH_FMT_DOUBLE, nullptr, &counterValue) == ERROR_SUCCESS)
                {
                    m_current.m_contextSwitchesPerSec = static_cast<float>(counterValue.doubleValue);
                }
            }
        }

#elif defined(AZ_PLATFORM_LINUX)
        // --- CPU utilization --- (stub — use /proc/stat if needed)
        m_current.m_cpuPercent = 0.0f;

        // --- Available physical memory from /proc/meminfo ---
        {
            std::ifstream meminfo("/proc/meminfo");
            std::string line;
            while (std::getline(meminfo, line))
            {
                if (line.find("MemAvailable:") == 0)
                {
                    AZ::u64 kB = 0;
                    sscanf(line.c_str(), "MemAvailable: %llu kB", &kB);
                    m_current.m_availableMemoryMB = kB / 1024;
                    break;
                }
            }
        }

        // --- Context switches from /proc/stat ---
        {
            std::ifstream stat("/proc/stat");
            std::string line;
            while (std::getline(stat, line))
            {
                if (line.find("ctxt ") == 0)
                {
                    AZ::u64 ctxt = 0;
                    sscanf(line.c_str(), "ctxt %llu", &ctxt);
                    auto now = AZStd::chrono::steady_clock::now();
                    if (m_hasPrevContextSwitchSample)
                    {
                        float elapsed = AZStd::chrono::duration<float>(now - m_prevContextSwitchTime).count();
                        if (elapsed > 0.0f)
                        {
                            m_current.m_contextSwitchesPerSec = static_cast<float>(ctxt - m_prevContextSwitches) / elapsed;
                        }
                    }
                    m_prevContextSwitches = ctxt;
                    m_hasPrevContextSwitchSample = true;
                    m_prevContextSwitchTime = now;
                    break;
                }
            }
        }

#elif defined(AZ_PLATFORM_MAC)
        // --- CPU utilization --- (stub)
        m_current.m_cpuPercent = 0.0f;

        // --- Available memory via sysctl ---
        {
            int mib[2] = { CTL_HW, HW_MEMSIZE };
            AZ::u64 physMem = 0;
            size_t len = sizeof(physMem);
            sysctl(mib, 2, &physMem, &len, nullptr, 0);
            // macOS doesn't have a simple "available" metric; use vm_statistics for a rough estimate
            m_current.m_availableMemoryMB = physMem / (1024 * 1024) / 4; // conservative placeholder
        }

        // --- Context switches via sysctl ---
        {
            unsigned int csw = 0;
            size_t len = sizeof(csw);
            if (sysctlbyname("vm.stats.sys.v_swtch", &csw, &len, nullptr, 0) == 0)
            {
                auto now = AZStd::chrono::steady_clock::now();
                if (m_hasPrevContextSwitchSample)
                {
                    float elapsed = AZStd::chrono::duration<float>(now - m_prevContextSwitchTime).count();
                    if (elapsed > 0.0f)
                    {
                        m_current.m_contextSwitchesPerSec = static_cast<float>(csw - m_prevContextSwitches) / elapsed;
                    }
                }
                m_prevContextSwitches = static_cast<AZ::u64>(csw);
                m_hasPrevContextSwitchSample = true;
                m_prevContextSwitchTime = now;
            }
        }

#else
        // Unsupported platforms: report unsaturated defaults
        m_current.m_cpuPercent = 0.0f;
        m_current.m_availableMemoryMB = 8192;
        m_current.m_diskIoBytesPerSec = 0.0f;
        m_current.m_contextSwitchesPerSec = 0.0f;
#endif

        // Track baseline (first few samples before builders scale up)
        if (m_baselineSamplesRemaining > 0 && m_current.m_contextSwitchesPerSec > 0.0f)
        {
            m_baselineContextSwitchesPerSec = AZStd::max(m_baselineContextSwitchesPerSec, m_current.m_contextSwitchesPerSec);
            m_baselineSamplesRemaining--;
        }
    }

    ResourceSnapshot ResourceMonitor::GetSnapshot() const
    {
        return m_current;
    }

    bool ResourceMonitor::IsSaturated(float cpuThreshold, AZ::u64 memoryMinMB, float contextSwitchBaselineMultiplier) const
    {
        if (m_current.m_cpuPercent > cpuThreshold)
        {
            return true;
        }

        if (m_current.m_availableMemoryMB < memoryMinMB)
        {
            return true;
        }

        // Context switch saturation: if current CS/s exceeds baseline * multiplier, we're thrashing
        if (m_baselineContextSwitchesPerSec > 0.0f && m_current.m_contextSwitchesPerSec > 0.0f)
        {
            float threshold = m_baselineContextSwitchesPerSec * contextSwitchBaselineMultiplier;
            if (m_current.m_contextSwitchesPerSec > threshold)
            {
                return true;
            }
        }

        return false;
    }
} // namespace AssetProcessor
