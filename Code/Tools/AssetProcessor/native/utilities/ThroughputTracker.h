/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <AzCore/base.h>
#include <AzCore/std/containers/deque.h>
#include <AzCore/std/chrono/chrono.h>

namespace AssetProcessor
{
    //! Tracks job completion throughput over a sliding time window.
    //! Used by ConcurrencyController to detect diminishing returns from adding more builders.
    class ThroughputTracker
    {
    public:
        ThroughputTracker() = default;
        ~ThroughputTracker() = default;

        //! Record a job completion. Call from FinishJob.
        void RecordCompletion();

        //! Get current throughput in jobs per second over the sliding window.
        float GetThroughput() const;

        //! Record the throughput at the time of a concurrency change.
        //! Returns the throughput delta since the last recorded change.
        float RecordConcurrencyChange(unsigned int newConcurrency);

        //! Returns true if the last scale-up produced less than the threshold improvement.
        bool IsDiminishingReturns(float thresholdPercent) const;

        //! Set the sliding window duration.
        void SetWindowDuration(float seconds);

    private:
        void PruneOldEntries() const;

        struct CompletionEntry
        {
            AZStd::chrono::steady_clock::time_point m_time;
        };

        mutable AZStd::deque<CompletionEntry> m_completions;
        float m_windowSeconds = 30.0f;

        // Throughput at last concurrency change
        float m_throughputAtLastChange = 0.0f;
        unsigned int m_concurrencyAtLastChange = 0;
        bool m_hasLastChange = false;
    };
} // namespace AssetProcessor
