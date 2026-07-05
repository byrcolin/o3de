/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <native/utilities/BuilderList.h>

namespace AssetProcessor
{
    void BuilderList::AddBuilder(AZStd::shared_ptr<Builder> builder, BuilderPurpose purpose)
    {
        if (purpose == BuilderPurpose::CreateJobs)
        {
            m_createJobsBuilders.push_back(AZStd::move(builder));
        }
        else
        {
            m_builders.emplace(builder->GetUuid(), builder);
        }
    }

    AZStd::shared_ptr<Builder> BuilderList::Find(AZ::Uuid uuid)
    {
        for (auto& builder : m_createJobsBuilders)
        {
            if (builder && builder->GetUuid() == uuid)
            {
                return builder;
            }
        }

        auto itr = m_builders.find(uuid);

        return itr != m_builders.end() ? itr->second : nullptr;
    }

    BuilderRef BuilderList::GetFirst(BuilderPurpose purpose)
    {
        if (purpose == BuilderPurpose::CreateJobs)
        {
            for (auto it = m_createJobsBuilders.begin(); it != m_createJobsBuilders.end(); )
            {
                auto& builder = *it;
                if (builder && builder->IsReadyToWork())
                {
                    builder->PumpCommunicator();

                    if (builder->IsValid())
                    {
                        return BuilderRef(builder);
                    }

                    it = m_createJobsBuilders.erase(it);
                }
                else
                {
                    ++it;
                }
            }

            return {};
        }

        for (auto itr = m_builders.begin(); itr != m_builders.end();)
        {
            auto& builder = itr->second;

            if (builder->IsReadyToWork())
            {
                builder->PumpCommunicator();

                if (builder->IsValid())
                {
                    return BuilderRef(builder);
                }

                itr = m_builders.erase(itr);
            }
            else
            {
                ++itr;
            }
        }

        return {};
    }

    AZStd::string BuilderList::RemoveByConnectionId(AZ::u32 connId)
    {
        AZStd::string uuidString;

        // Note that below the connectionId will be set to 0.
        // The builder might not be destroyed immediately if another thread is currently holding a reference.
        // If the builder is currently in use, this will signal to the waiting thread to not expect a reply
        // and fail the current job request.

        for (auto it = m_createJobsBuilders.begin(); it != m_createJobsBuilders.end(); ++it)
        {
            if (*it && (*it)->GetConnectionId() == connId)
            {
                uuidString = (*it)->UuidString();
                (*it)->m_connectionId = 0;
                m_createJobsBuilders.erase(it);
                return uuidString;
            }
        }

        for (auto itr = m_builders.begin(); itr != m_builders.end(); ++itr)
        {
            auto& builder = itr->second;

            if (builder->GetConnectionId() == connId)
            {
                uuidString = builder->UuidString();
                builder->m_connectionId = 0;
                m_builders.erase(itr);

                return uuidString;
            }
        }

        return {};
    }

    void BuilderList::RemoveByUuid(AZ::Uuid uuid)
    {
        for (auto it = m_createJobsBuilders.begin(); it != m_createJobsBuilders.end(); ++it)
        {
            if (*it && (*it)->GetUuid() == uuid)
            {
                m_createJobsBuilders.erase(it);
                return;
            }
        }

        m_builders.erase(uuid);
    }

    void BuilderList::PumpIdleBuilders()
    {
        // Idle builders will not have their event pump run inside the job that they are performing, so we need to pump them here.
        // These are all the builders that are "ready to work", i.e. are not currently working.
        for (auto& builder : m_createJobsBuilders)
        {
            if (builder && builder->IsReadyToWork())
            {
                builder->PumpCommunicator();
            }
        }

        for (auto pair : m_builders)
        {
            auto builder = pair.second;

            if (builder->IsReadyToWork())
            {
                builder->PumpCommunicator();
            }
        }
    }
}
