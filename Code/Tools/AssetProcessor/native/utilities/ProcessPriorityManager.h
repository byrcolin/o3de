/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <AzCore/base.h>
#include <AzCore/std/containers/set.h>
#include <AzCore/std/parallel/mutex.h>

namespace AssetProcessor
{
    //! Manages AssetBuilder process priorities and detects external load (Editor/Game).
    //! Uses Windows Toolhelp32 for process enumeration and OpenProcess+SetPriorityClass
    //! for priority changes. No AzFramework modifications required.
    class ProcessPriorityManager
    {
    public:
        ProcessPriorityManager() = default;
        ~ProcessPriorityManager() = default;

        //! Register a PID that we spawned (call after builder launch).
        void RegisterPid(AZ::u32 pid);

        //! Unregister a PID (call when builder exits).
        void UnregisterPid(AZ::u32 pid);

        //! Set priority class on all tracked builder processes.
        //! priorityClass: NORMAL_PRIORITY_CLASS, BELOW_NORMAL_PRIORITY_CLASS, IDLE_PRIORITY_CLASS
        void SetAllPriority(AZ::u32 priorityClass);

        //! Returns true if Editor.exe or a game launcher is currently running.
        bool DetectExternalLoad() const;

        //! Find PIDs of processes matching a given executable name.
        //! Used to discover newly-spawned AssetBuilder processes.
        static AZStd::set<AZ::u32> FindProcessesByName(const char* exeName);

    private:
        mutable AZStd::mutex m_mutex;
        AZStd::set<AZ::u32> m_trackedPids;
        AZ::u32 m_currentPriorityClass = 0; // 0 = not yet set
    };
} // namespace AssetProcessor
