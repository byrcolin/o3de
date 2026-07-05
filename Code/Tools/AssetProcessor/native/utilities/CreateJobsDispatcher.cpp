/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <native/utilities/CreateJobsDispatcher.h>
#include <native/utilities/assetUtils.h>
#include <native/utilities/ThreadHelper.h>
#include <native/resourcecompiler/RCCommon.h>

namespace AssetProcessor
{
    CreateJobsDispatcher::CreateJobsDispatcher(int numWorkers)
    {
        AZStd::thread_desc desc;
        desc.m_name = "CreateJobs Worker";

        for (int i = 0; i < numWorkers; ++i)
        {
            m_workers.emplace_back(desc, [this]() { WorkerThread(); });
        }
    }

    CreateJobsDispatcher::~CreateJobsDispatcher()
    {
        {
            AZStd::lock_guard<AZStd::mutex> lock(m_queueMutex);
            m_shutdown = true;
        }
        m_queueCV.notify_all();

        for (auto& worker : m_workers)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }
    }

    int CreateJobsDispatcher::Submit(WorkItem&& item)
    {
        // Just queue the item — don't wake workers yet (vector may still be growing).
        int index = static_cast<int>(m_items.size());
        m_items.push_back(AZStd::move(item));
        return index;
    }

    void CreateJobsDispatcher::DispatchAll()
    {
        const int totalItems = static_cast<int>(m_items.size());

        // All items are now in m_items and won't be reallocated.
        // Build the work queue and wake all workers.
        {
            AZStd::lock_guard<AZStd::mutex> lock(m_queueMutex);
            for (int i = 0; i < totalItems; ++i)
            {
                m_workQueue.push(i);
            }
        }
        m_queueCV.notify_all();
    }

    void CreateJobsDispatcher::WaitForAll()
    {
        AZStd::unique_lock<AZStd::mutex> lock(m_completionMutex);
        m_completionCV.wait(lock, [this]()
        {
            return m_completedCount.load() >= static_cast<int>(m_items.size());
        });
    }

    int CreateJobsDispatcher::WaitForCount(int count)
    {
        AZStd::unique_lock<AZStd::mutex> lock(m_completionMutex);
        m_completionCV.wait(lock, [this, count]()
        {
            return m_completedCount.load() >= count;
        });
        return m_completedCount.load();
    }

    bool CreateJobsDispatcher::IsComplete() const
    {
        return m_completedCount.load() >= static_cast<int>(m_items.size());
    }

    CreateJobsDispatcher::WorkItem& CreateJobsDispatcher::GetResult(int index)
    {
        return m_items[index];
    }

    void CreateJobsDispatcher::Reset()
    {
        m_items.clear();
        // Queue should already be empty after WaitForAll, but clear just in case
        AZStd::lock_guard<AZStd::mutex> lock(m_queueMutex);
        m_workQueue = {};
        m_completedCount = 0;
    }

    void CreateJobsDispatcher::WorkerThread()
    {
        while (true)
        {
            int itemIndex = -1;

            {
                AZStd::unique_lock<AZStd::mutex> lock(m_queueMutex);
                m_queueCV.wait(lock, [this]()
                {
                    return m_shutdown || !m_workQueue.empty();
                });

                if (m_shutdown && m_workQueue.empty())
                {
                    return;
                }

                itemIndex = m_workQueue.front();
                m_workQueue.pop();
            }

            // Execute the CreateJobs call on this worker thread.
            // The createJobFunction internally acquires a builder from the pool (thread-safe via BuilderManager mutex),
            // sends the request over a socket, and blocks on a semaphore until the builder process responds.
            // NOTE: We do NOT install a JobLogTraceListener here because TraceMessageBus is not thread-safe
            // for handler connect/disconnect. CreateJobs log output goes to the builder process stdout instead.
            WorkItem& item = m_items[itemIndex];

            SetThreadLocalJobId(item.m_runKey);
            item.m_createJobFunction(item.m_request, item.m_response);
            SetThreadLocalJobId(0);

            // Signal completion
            {
                AZStd::lock_guard<AZStd::mutex> lock(m_completionMutex);
                m_completedCount.fetch_add(1);
            }
            m_completionCV.notify_one();
        }
    }
} // namespace AssetProcessor
