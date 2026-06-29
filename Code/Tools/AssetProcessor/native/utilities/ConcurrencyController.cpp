/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <native/utilities/ConcurrencyController.h>
#include <native/utilities/Builder.h>
#include <AzCore/Settings/SettingsRegistry.h>
#include <AzCore/Debug/Trace.h>
#include <AzCore/std/parallel/thread.h>

#if defined(AZ_PLATFORM_WINDOWS)
#include <AzCore/PlatformIncl.h>
#endif

namespace AssetProcessor
{
    static constexpr const char* LogChannel = "ConcurrencyController";
    static constexpr const char* SettingsRoot = "/Amazon/AssetProcessor/Settings/DynamicConcurrency";

    ConcurrencyController::ConcurrencyController()
        : m_lastScaleTime(AZStd::chrono::steady_clock::now())
    {
    }

    void ConcurrencyController::Initialize()
    {
        LoadSettings();

        // Detect available cores
        m_numCores = AZStd::thread::hardware_concurrency();
        if (m_numCores == 0)
        {
            m_numCores = 4; // fallback
        }

        if (m_enabled)
        {
            AZ_Printf(LogChannel, "Dynamic concurrency controller enabled.\n");
            AZ_Printf(LogChannel, "  Cooldown: %.1fs, CPU threshold: %.0f%%, Memory min: %llu MB\n",
                m_cooldownSeconds, m_cpuThreshold, m_memoryMinMB);
            AZ_Printf(LogChannel, "  Context switch multiplier: %.1fx, Diminishing return threshold: %.0f%%\n",
                m_contextSwitchMultiplier, m_diminishingReturnThreshold * 100.0f);
            if (m_maxConcurrentCap > 0)
            {
                AZ_Printf(LogChannel, "  Hard cap: %u concurrent jobs\n", m_maxConcurrentCap);
            }
            AZ_Printf(LogChannel, "  Starting with %u concurrent job(s) at Normal priority.\n", m_targetConcurrent);
        }
        else
        {
            AZ_Printf(LogChannel, "Dynamic concurrency controller disabled. Using static job limits.\n");
        }
    }

    bool ConcurrencyController::IsEnabled() const
    {
        return m_enabled;
    }

    unsigned int ConcurrencyController::GetMaxConcurrentJobs() const
    {
        return m_targetConcurrent;
    }

    AzFramework::ProcessPriority ConcurrencyController::GetLaunchPriority() const
    {
        if (m_state == State::ExternalLoad)
        {
            return AzFramework::ProcessPriority::PROCESSPRIORITY_BELOWNORMAL;
        }
        return AzFramework::ProcessPriority::PROCESSPRIORITY_NORMAL;
    }

    void ConcurrencyController::OnJobCompleted()
    {
        m_throughputTracker.RecordCompletion();
    }

    void ConcurrencyController::OnBuilderLaunched(AZ::u32 pid)
    {
        m_priorityManager.RegisterPid(pid);
    }

    void ConcurrencyController::OnBuilderExited(AZ::u32 pid)
    {
        m_priorityManager.UnregisterPid(pid);
    }

    void ConcurrencyController::Tick()
    {
        if (!m_enabled)
        {
            return;
        }

        // Sync tracked PIDs with actual running AssetBuilder processes
        auto currentPids = ProcessPriorityManager::FindProcessesByName("AssetBuilder.exe");
        for (AZ::u32 pid : currentPids)
        {
            m_priorityManager.RegisterPid(pid);
        }

        // Update the Builder's static launch priority based on current state
        Builder::SetDefaultLaunchPriority(GetLaunchPriority());

        // Sample system resources
        m_resourceMonitor.Sample();

        // Check for external load (Editor/Game)
        bool externalLoad = m_priorityManager.DetectExternalLoad();

        if (externalLoad && m_state != State::ExternalLoad)
        {
            // Editor/Game just started — reduce priority and concurrency
            m_stateBeforeExternalLoad = m_state;
            TransitionTo(State::ExternalLoad);

#if defined(AZ_PLATFORM_WINDOWS)
            m_priorityManager.SetAllPriority(BELOW_NORMAL_PRIORITY_CLASS);
#endif
            // Reduce concurrency to half, minimum 2
            m_targetConcurrent = AZStd::GetMax(m_targetConcurrent / 2, 2u);
            AZ_Printf(LogChannel, "External load detected. Reduced to %u jobs at Below Normal priority.\n", m_targetConcurrent);
            return;
        }
        else if (!externalLoad && m_state == State::ExternalLoad)
        {
            // Editor/Game exited — restore priority and resume scaling
#if defined(AZ_PLATFORM_WINDOWS)
            m_priorityManager.SetAllPriority(NORMAL_PRIORITY_CLASS);
#endif
            TransitionTo(State::RampingUp);
            AZ_Printf(LogChannel, "External load gone. Resuming at Normal priority, ramping up from %u.\n", m_targetConcurrent);
            return;
        }

        if (m_state == State::ExternalLoad)
        {
            // While under external load, don't scale up. Just maintain.
            return;
        }

        // Evaluate scaling on cooldown
        auto now = AZStd::chrono::steady_clock::now();
        float elapsed = AZStd::chrono::duration<float>(now - m_lastScaleTime).count();

        if (elapsed < m_cooldownSeconds)
        {
            return;
        }

        EvaluateScaling();
    }

    void ConcurrencyController::LoadSettings()
    {
        auto settingsRegistry = AZ::SettingsRegistry::Get();
        if (!settingsRegistry)
        {
            return;
        }

        AZStd::string root(SettingsRoot);

        bool enabledValue = true;
        if (settingsRegistry->Get(enabledValue, (root + "/enabled").c_str()))
        {
            m_enabled = enabledValue;
        }

        double cooldown = 0.0;
        if (settingsRegistry->Get(cooldown, (root + "/cooldownSeconds").c_str()))
        {
            m_cooldownSeconds = static_cast<float>(cooldown);
        }

        double cpuThreshold = 0.0;
        if (settingsRegistry->Get(cpuThreshold, (root + "/cpuThreshold").c_str()))
        {
            m_cpuThreshold = static_cast<float>(cpuThreshold);
        }

        AZ::s64 memMin = 0;
        if (settingsRegistry->Get(memMin, (root + "/memoryMinMB").c_str()))
        {
            m_memoryMinMB = static_cast<AZ::u64>(memMin);
        }

        double csMult = 0.0;
        if (settingsRegistry->Get(csMult, (root + "/contextSwitchMultiplier").c_str()))
        {
            m_contextSwitchMultiplier = static_cast<float>(csMult);
        }

        double dimRet = 0.0;
        if (settingsRegistry->Get(dimRet, (root + "/diminishingReturnThreshold").c_str()))
        {
            m_diminishingReturnThreshold = static_cast<float>(dimRet);
        }

        AZ::s64 maxCap = 0;
        if (settingsRegistry->Get(maxCap, (root + "/maxConcurrent").c_str()))
        {
            m_maxConcurrentCap = static_cast<unsigned int>(maxCap);
        }

        AZ::s64 satReadings = 0;
        if (settingsRegistry->Get(satReadings, (root + "/saturatedReadingsRequired").c_str()))
        {
            m_saturatedReadingsRequired = static_cast<unsigned int>(satReadings);
        }

        // Also read the old maxJobs as a hard cap (backward compat)
        AZ::s64 oldMaxJobs = 0;
        if (settingsRegistry->Get(oldMaxJobs, "/Amazon/AssetProcessor/Settings/Jobs/maxJobs"))
        {
            if (oldMaxJobs > 0)
            {
                m_hardCapMaxJobs = static_cast<unsigned int>(oldMaxJobs);
            }
        }
    }

    void ConcurrencyController::EvaluateScaling()
    {
        auto snapshot = m_resourceMonitor.GetSnapshot();
        bool saturated = m_resourceMonitor.IsSaturated(m_cpuThreshold, m_memoryMinMB, m_contextSwitchMultiplier);

        if (saturated)
        {
            m_saturatedReadingsCount++;
        }
        else
        {
            m_saturatedReadingsCount = 0;
        }

        switch (m_state)
        {
        case State::RampingUp:
        {
            // Check if we should continue ramping
            if (m_saturatedReadingsCount >= m_saturatedReadingsRequired)
            {
                AZ_Printf(LogChannel, "Resources saturated (CPU: %.0f%%, RAM: %llu MB, CS: %.0f/s). Holding at %u.\n",
                    snapshot.m_cpuPercent, snapshot.m_availableMemoryMB,
                    snapshot.m_contextSwitchesPerSec, m_targetConcurrent);
                m_saturatedReadingsCount = 0;
                TransitionTo(State::Steady);
                break;
            }

            // Check diminishing returns
            if (m_throughputTracker.IsDiminishingReturns(m_diminishingReturnThreshold) && m_throughputTracker.GetThroughput() > 0.0f)
            {
                AZ_Printf(LogChannel, "Diminishing returns detected at %u concurrent. Throughput: %.1f jobs/s. Holding steady.\n",
                    m_targetConcurrent, m_throughputTracker.GetThroughput());
                TransitionTo(State::Steady);
                break;
            }

            // Proof-of-life gate: if we've scaled up several times without seeing
            // ANY job completions, stop ramping. This prevents overwhelming AP's
            // event loop with builder connections that can't be serviced.
            if (m_zeroThroughputScaleUps >= MaxZeroThroughputScaleUps && m_throughputTracker.GetThroughput() == 0.0f)
            {
                // Don't log every tick — just hold
                break;
            }

            // Check caps — AP's architecture limit is the binding constraint
            unsigned int effectiveCap = ApArchitectureCap;
            if (m_maxConcurrentCap > 0)
            {
                effectiveCap = AZStd::GetMin(effectiveCap, m_maxConcurrentCap);
            }
            if (m_hardCapMaxJobs > 0)
            {
                effectiveCap = AZStd::GetMin(effectiveCap, m_hardCapMaxJobs);
            }

            if (m_targetConcurrent >= effectiveCap)
            {
                AZ_Printf(LogChannel, "Reached concurrency cap at %u. Holding steady.\n", m_targetConcurrent);
                TransitionTo(State::Steady);
                break;
            }

            // Exponential ramp: double concurrency until we hit the cap
            unsigned int increment = AZStd::GetMax(m_targetConcurrent, 1u); // double

            // Don't overshoot cap
            unsigned int newTarget = AZStd::GetMin(m_targetConcurrent + increment, effectiveCap);
            m_targetConcurrent = newTarget;

            // Track proof-of-life
            if (m_throughputTracker.GetThroughput() == 0.0f)
            {
                m_zeroThroughputScaleUps++;
            }
            else
            {
                m_zeroThroughputScaleUps = 0;
            }
            float delta = m_throughputTracker.RecordConcurrencyChange(m_targetConcurrent);
            AZ_Printf(LogChannel, "Scaling up to %u concurrent (throughput delta: %+.1f jobs/s, CPU: %.0f%%, RAM: %llu MB).\n",
                m_targetConcurrent, delta, snapshot.m_cpuPercent, snapshot.m_availableMemoryMB);
            m_lastScaleTime = AZStd::chrono::steady_clock::now();
            break;
        }

        case State::Steady:
        {
            // If resources become heavily saturated, scale down
            if (m_saturatedReadingsCount >= m_saturatedReadingsRequired)
            {
                TransitionTo(State::ScalingDown);
                break;
            }

            // Stall detection: if throughput drops to 0 for too long while builders
            // are supposedly running, we're likely in a deadlock scenario where
            // builders are waiting for AP socket responses that can't be serviced.
            float currentThroughput = m_throughputTracker.GetThroughput();
            if (currentThroughput == 0.0f && m_targetConcurrent > 1)
            {
                m_stallTickCount++;
                if (m_stallTickCount >= StallTickThreshold)
                {
                    // Aggressive scale-down: halve concurrency to break the deadlock
                    unsigned int newTarget = AZStd::GetMax(m_targetConcurrent / 2, 1u);
                    AZ_Printf(LogChannel, "Stall detected (%u ticks with 0 throughput). Reducing from %u to %u concurrent.\n",
                        m_stallTickCount, m_targetConcurrent, newTarget);
                    m_targetConcurrent = newTarget;
                    m_stallTickCount = 0;
                    // Stay in Steady — don't transition to RampingUp or ScalingDown
                }
            }
            else
            {
                m_stallTickCount = 0;
            }
            break;
        }

        case State::ScalingDown:
        {
            if (m_targetConcurrent > 1)
            {
                m_targetConcurrent--;
                AZ_Printf(LogChannel, "Scaling down to %u concurrent (CPU: %.0f%%, RAM: %llu MB).\n",
                    m_targetConcurrent, snapshot.m_cpuPercent, snapshot.m_availableMemoryMB);
                m_lastScaleTime = AZStd::chrono::steady_clock::now();
                m_saturatedReadingsCount = 0;
            }

            if (!saturated)
            {
                TransitionTo(State::Steady);
            }
            break;
        }

        case State::ExternalLoad:
            // Handled in Tick() above
            break;
        }
    }

    void ConcurrencyController::TransitionTo(State newState)
    {
        m_state = newState;
        m_saturatedReadingsCount = 0;
        m_lastScaleTime = AZStd::chrono::steady_clock::now();
    }
} // namespace AssetProcessor
