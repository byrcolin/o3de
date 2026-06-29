/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <native/utilities/ThroughputTracker.h>

namespace AssetProcessor
{
    void ThroughputTracker::RecordCompletion()
    {
        CompletionEntry entry;
        entry.m_time = AZStd::chrono::steady_clock::now();
        m_completions.push_back(entry);
        PruneOldEntries();
    }

    float ThroughputTracker::GetThroughput() const
    {
        PruneOldEntries();

        if (m_completions.size() < 2)
        {
            return m_completions.empty() ? 0.0f : 1.0f; // At least 1 if we have any
        }

        auto windowStart = m_completions.front().m_time;
        auto windowEnd = m_completions.back().m_time;
        float elapsed = AZStd::chrono::duration<float>(windowEnd - windowStart).count();

        if (elapsed <= 0.0f)
        {
            return static_cast<float>(m_completions.size()); // All completed in same instant
        }

        return static_cast<float>(m_completions.size()) / elapsed;
    }

    float ThroughputTracker::RecordConcurrencyChange(unsigned int newConcurrency)
    {
        float currentThroughput = GetThroughput();
        float delta = 0.0f;

        if (m_hasLastChange)
        {
            delta = currentThroughput - m_throughputAtLastChange;
        }

        m_throughputAtLastChange = currentThroughput;
        m_concurrencyAtLastChange = newConcurrency;
        m_hasLastChange = true;

        return delta;
    }

    bool ThroughputTracker::IsDiminishingReturns(float thresholdPercent) const
    {
        if (!m_hasLastChange || m_throughputAtLastChange <= 0.0f)
        {
            return false;
        }

        float currentThroughput = GetThroughput();
        float improvement = (currentThroughput - m_throughputAtLastChange) / m_throughputAtLastChange;

        return improvement < thresholdPercent;
    }

    void ThroughputTracker::SetWindowDuration(float seconds)
    {
        m_windowSeconds = seconds;
    }

    void ThroughputTracker::PruneOldEntries() const
    {
        auto cutoff = AZStd::chrono::steady_clock::now() -
            AZStd::chrono::duration_cast<AZStd::chrono::steady_clock::duration>(
                AZStd::chrono::duration<float>(m_windowSeconds));

        while (!m_completions.empty() && m_completions.front().m_time < cutoff)
        {
            m_completions.pop_front();
        }
    }
} // namespace AssetProcessor
