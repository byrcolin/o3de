/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <AzCore/base.h>
#include <AzCore/std/chrono/chrono.h>

#if AZ_TRAIT_OS_PLATFORM_APPLE || AZ_TRAIT_OS_IS_HOST_OS_PLATFORM  // stub for non-Windows
#endif

namespace AssetProcessor
{
    struct ResourceSnapshot
    {
        float m_cpuPercent = 0.0f;          // 0-100
        AZ::u64 m_availableMemoryMB = 0;
        float m_diskIoBytesPerSec = 0.0f;
        float m_contextSwitchesPerSec = 0.0f;
    };

    //! Monitors system resource utilization using lightweight OS APIs.
    //! Windows: GetSystemTimes, GlobalMemoryStatusEx, IOCTL_DISK_PERFORMANCE
    //! Other platforms: returns default (unsaturated) values.
    class ResourceMonitor
    {
    public:
        ResourceMonitor();
        ~ResourceMonitor() = default;

        //! Sample current system resources. Call periodically (e.g., every 1 second).
        void Sample();

        //! Get the latest resource snapshot.
        ResourceSnapshot GetSnapshot() const;

        //! Returns true if any resource metric exceeds its saturation threshold.
        bool IsSaturated(float cpuThreshold, AZ::u64 memoryMinMB, float contextSwitchBaselineMultiplier) const;

    private:
        ResourceSnapshot m_current;

#if defined(AZ_PLATFORM_WINDOWS)
        // CPU tracking via GetSystemTimes deltas
        AZ::u64 m_prevIdleTime = 0;
        AZ::u64 m_prevKernelTime = 0;
        AZ::u64 m_prevUserTime = 0;
        bool m_hasPrevCpuSample = false;

        // Disk I/O tracking via IOCTL_DISK_PERFORMANCE deltas
        AZ::u64 m_prevDiskBytesRead = 0;
        AZ::u64 m_prevDiskBytesWritten = 0;
        bool m_hasPrevDiskSample = false;
        AZStd::chrono::steady_clock::time_point m_prevDiskSampleTime;
#endif
    };
} // namespace AssetProcessor
