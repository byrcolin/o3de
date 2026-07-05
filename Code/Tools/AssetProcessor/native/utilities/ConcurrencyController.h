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
#include <AzCore/std/containers/unordered_map.h>
#include <AzCore/std/string/string.h>
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

        //! Snapshot of all metrics that feed scaling decisions, for UI display.
        struct Metrics
        {
            enum class State { RampingUp, Steady, ScalingDown, ExternalLoad };

            // Resource utilization
            float m_cpuPercent = 0.0f;
            AZ::u64 m_availableMemoryMB = 0;
            float m_memoryUsedPercent = 0.0f;
            float m_contextSwitchesPerSec = 0.0f;
            float m_csBaselinePerSec = 0.0f;

            // Scaling state
            State m_state = State::RampingUp;
            const char* m_stateName = "Init";
            unsigned int m_activeBuilders = 0;
            unsigned int m_targetConcurrent = 1;
            unsigned int m_jobsInFlight = 0;
            float m_throughputJobsPerSec = 0.0f;

            // Priority
            const char* m_priorityName = "Normal";
            bool m_isForeground = true;
            bool m_externalLoad = false;

            // Builder resource totals
            AZ::u64 m_totalBuilderMemoryMB = 0;
            unsigned int m_totalBuilderThreads = 0;
        };

        //! Get a snapshot of current metrics for display.
        Metrics GetMetrics() const;

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

        //! Get the per-job-key concurrency limit for a specific job key.
        //! 0 = no limit for this type.
        unsigned int GetPerKeyLimit(const AZStd::string& jobKey) const;

        //! Notify that a job has completed. Feeds the throughput tracker.
        void OnJobCompleted();

        //! Notify that a new builder process was launched with the given PID.
        void OnBuilderLaunched(AZ::u32 pid);

        //! Notify that a builder process has exited.
        void OnBuilderExited(AZ::u32 pid);

        //! Periodic tick — call every ~1 second. Samples resources, makes scaling decisions.
        //! @param jobsInFlight Number of jobs currently being processed by builders.
        void Tick(unsigned int jobsInFlight = 0);

        //! Report whether the AP window is in the foreground (visible, not minimized).
        //! When foreground: run at Normal priority for fastest processing.
        //! When background/minimized: drop to BelowNormal to be a good citizen.
        void SetForeground(bool isForeground);

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

        // With parallel CreateJobs dispatch (8 worker threads, each with their own builder process),
        // the AP can now feed work to many more builders. The architecture cap is raised to 64
        // to utilize high-core-count machines. The real bottleneck was serial CreateJobs, not
        // ProcessJob dispatch — now that CreateJobs runs in parallel, we can saturate more cores.
        static constexpr unsigned int ApArchitectureCap = 64;

        // Proof-of-life: track consecutive scale-ups with zero throughput.
        // If we scale up multiple times without seeing any job completions,
        // stop ramping to avoid deadlocking the event loop.
        unsigned int m_zeroThroughputScaleUps = 0;
        static constexpr unsigned int MaxZeroThroughputScaleUps = 3; // allow 1->2->4->8 blind, then require proof

        // Stall detection: if throughput is 0 for this many ticks while at steady state,
        // scale down aggressively (builders may be deadlocked on AP socket comms).
        unsigned int m_stallTickCount = 0;
        static constexpr unsigned int StallTickThreshold = 30; // ~30 seconds at 1s tick (shaders take minutes)
        float m_lastKnownThroughput = 0.0f;
        unsigned int m_jobsInFlight = 0; // updated each Tick() from RCController

        // Launch rate limiter: prevent connection storms by limiting new builder
        // starts per dispatch cycle. Existing builders continue working unaffected.
        // Note: in resident mode, most dispatches reuse existing builders (no new connection).
        // The ramp (1->2->4->8->16) already gates startup; this is a safety net only.
        unsigned int m_launchesThisCycle = 0;
        AZStd::chrono::steady_clock::time_point m_lastLaunchCycleReset;
        static constexpr unsigned int MaxLaunchesPerSecond = 64;

    public:
        //! Check if a new builder launch is allowed this cycle. Call from DispatchJobsImpl
        //! before starting each new job. Returns false if the rate limit is hit.
        bool CanLaunchBuilder();

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
        unsigned int m_perKeyLimit = 0; // 0 = no global per-key limit
        AZStd::unordered_map<AZStd::string, unsigned int> m_jobKeyLimits; // per-type limits (job key → max concurrent)

        // --- Previous state for external load transitions ---
        State m_stateBeforeExternalLoad = State::RampingUp;

        // --- Priority management ---
        bool m_isForeground = true; // AP window is visible and not minimized
        bool m_prioritiesApplied = false; // Fixed priority policy applied once at first tick
    };
} // namespace AssetProcessor
