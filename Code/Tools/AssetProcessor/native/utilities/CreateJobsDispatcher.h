/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <AzCore/std/chrono/chrono.h>
#include <AzCore/std/containers/queue.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/functional.h>
#include <AzCore/std/parallel/atomic.h>
#include <AzCore/std/parallel/condition_variable.h>
#include <AzCore/std/parallel/mutex.h>
#include <AzCore/std/parallel/thread.h>
#include <AssetBuilderSDK/AssetBuilderSDK.h>

namespace AssetProcessor
{
    //! Thread pool that executes CreateJobs calls in parallel.
    //! Only the blocking m_createJobFunction network call runs on worker threads.
    //! All shared state access (database, analysis tracking, etc.) remains on the caller's thread.
    class CreateJobsDispatcher
    {
    public:
        using CreateJobsFunction = AZStd::function<void(
            const AssetBuilderSDK::CreateJobsRequest&,
            AssetBuilderSDK::CreateJobsResponse&)>;

        struct WorkItem
        {
            CreateJobsFunction m_createJobFunction;
            AssetBuilderSDK::CreateJobsRequest m_request;
            AssetBuilderSDK::CreateJobsResponse m_response;
            AZ::s64 m_runKey = 0;
            AZStd::string m_logFileName;
        };

        explicit CreateJobsDispatcher(int numWorkers = 8);
        ~CreateJobsDispatcher();

        AZ_DISABLE_COPY_MOVE(CreateJobsDispatcher);

        //! Submit a work item. Does NOT wake workers — call DispatchAll() after all items are queued.
        //! Returns the index for later retrieval via GetResult().
        int Submit(WorkItem&& item);

        //! Wake all workers to begin processing submitted items.
        //! Must be called after all Submit() calls for this batch.
        //! Wakes all workers to begin processing the submitted items.
        void DispatchAll();

        //! Returns the number of pending (submitted but not yet dispatched) items.
        size_t NumPending() const { return m_items.size(); }

        //! Block until all submitted work items have completed.
        //! WARNING: do not call from the AP main thread — builder responses are delivered
        //! via queued signals on the main thread, so blocking it starves response delivery.
        void WaitForAll();

        //! Block until at least one more work item completes, all work is done, or the
        //! timeout expires — whichever comes first. Safe to call from the main thread in a
        //! loop that pumps the Qt event queue between calls. Returns current completed count.
        int WaitForProgress(AZStd::chrono::milliseconds timeout);

        //! Returns true if all submitted work items have completed.
        bool IsComplete() const;

        //! Get a completed work item by index. Only valid after WaitForAll()/IsComplete().
        WorkItem& GetResult(int index);

        //! Clear all results. Call before submitting a new batch.
        void Reset();

        //! Returns the number of worker threads.
        int NumWorkers() const { return static_cast<int>(m_workers.size()); }

    private:
        void WorkerThread();

        AZStd::vector<WorkItem> m_items;
        AZStd::queue<int> m_workQueue;
        AZStd::mutex m_queueMutex;
        AZStd::condition_variable m_queueCV;
        AZStd::vector<AZStd::thread> m_workers;
        AZStd::atomic<bool> m_shutdown{false};
        AZStd::atomic<int> m_completedCount{0};
        AZStd::mutex m_completionMutex;
        AZStd::condition_variable m_completionCV;
    };
} // namespace AssetProcessor
