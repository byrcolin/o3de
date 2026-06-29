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
#include <AzFramework/Process/ProcessCommon_fwd.h>
#include <native/utilities/ResourceMonitor.h>
#include <native/utilities/ThroughputTracker.h>
#include <native/utilities/ProcessPriorityManager.h>

namespace AssetProcessor
{
    //! Dynamic concurrency controller for the Asset Processor.
    //!
    //! Replaces the static maxJobs calculation with a feedback-controlled autoscaler:
    //! - Starts with 1 builder at Normal priority
    //! - Ramps up one at a time while throughput improves and resources are available
    //! - Scales back on diminishing returns or resource saturation
    //! - Lowers builder priority when Editor/Game is detected
    //!
    //! All settings are configurable via the Settings Registry under:
    //!   /Amazon/AssetProcessor/Settings/DynamicConcurrency/
    class ConcurrencyController
    {
    public:
        ConcurrencyController();
        ~ConcurrencyController() = default;

        //! Initialize from settings registry. Call once during AP startup.
        void Initialize();

        //! Returns true if dynamic concurrency is enabled.
        bool IsEnabled() const;

        //! Get the current maximum number of concurrent jobs allowed.
        //! Called by RCController::DispatchJobsImpl() instead of m_maxJobs.
        unsigned int GetMaxConcurrentJobs() const;

        //! Get the process priority to use when launching a new builder.
        //! Returns PROCESSPRIORITY_NORMAL or PROCESSPRIORITY_BELOWNORMAL depending on external load.
        AzFramework::ProcessPriority GetLaunchPriority() const;

        //! Notify that a job has completed. Feeds the throughput tracker.
        void OnJobCompleted();

        //! Notify that a new builder process was launched with the given PID.
        void OnBuilderLaunched(AZ::u32 pid);

        //! Notify that a builder process has exited.
        void OnBuilderExited(AZ::u32 pid);

        //! Periodic tick — call every ~1 second. Samples resources, makes scaling decisions.
        void Tick();

    private:
        enum class State
        {
            RampingUp,
            Steady,
            ScalingDown,
            ExternalLoad
        };

        void LoadSettings();
        void EvaluateScaling();
        void TransitionTo(State newState);

        // --- Components ---
        ResourceMonitor m_resourceMonitor;
        ThroughputTracker m_throughputTracker;
        ProcessPriorityManager m_priorityManager;

        // --- State ---
        State m_state = State::RampingUp;
        unsigned int m_targetConcurrent = 1;
        unsigned int m_hardCapMaxJobs = 0; // 0 = no cap (from old maxJobs setting)
        unsigned int m_numCores = 4; // Detected at Initialize()

        // AP's single-threaded Qt event loop can't service more than N builder
        // socket connections simultaneously without starving during shader compilation
        // (builders request #include dependency resolution from AP over sockets).
        // Tested: 12 deadlocks, 8 deadlocks on all-shader workloads, 4 is stable.
        static constexpr unsigned int ApArchitectureCap = 4;

        // Proof-of-life: track consecutive scale-ups with zero throughput.
        // If we scale up multiple times without seeing any job completions,
        // stop ramping to avoid deadlocking the event loop.
        unsigned int m_zeroThroughputScaleUps = 0;
        static constexpr unsigned int MaxZeroThroughputScaleUps = 3; // allow 1->2->4->8 blind, then require proof

        // Stall detection: if throughput is 0 for this many ticks while at steady state,
        // scale down aggressively (builders are likely deadlocked on AP socket comms).
        unsigned int m_stallTickCount = 0;
        static constexpr unsigned int StallTickThreshold = 10; // ~10 seconds at 1s tick rate
        float m_lastKnownThroughput = 0.0f;

        // --- Timing ---
        AZStd::chrono::steady_clock::time_point m_lastScaleTime;
        unsigned int m_saturatedReadingsCount = 0;

        // --- Settings (from registry) ---
        bool m_enabled = true;
        float m_cooldownSeconds = 3.0f;
        float m_cpuThreshold = 85.0f;
        AZ::u64 m_memoryMinMB = 2048;
        float m_contextSwitchMultiplier = 3.0f;
        float m_diminishingReturnThreshold = 0.10f;
        unsigned int m_maxConcurrentCap = 0; // 0 = no explicit cap
        unsigned int m_saturatedReadingsRequired = 3;

        // --- Previous state for external load transitions ---
        State m_stateBeforeExternalLoad = State::RampingUp;
    };
} // namespace AssetProcessor
