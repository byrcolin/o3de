/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#if !defined(Q_MOC_RUN)
#include <QDialog>
#endif

QT_FORWARD_DECLARE_CLASS(QListView)
QT_FORWARD_DECLARE_CLASS(QStandardItemModel)

namespace O3DE::ProjectManager
{
    QT_FORWARD_DECLARE_CLASS(FormLineEditWidget)

    class GemRepoAddDialog
        : public QDialog
    {
    public:
        explicit GemRepoAddDialog(QWidget* parent = nullptr);
        ~GemRepoAddDialog() = default;

        QString GetRepoPath();

    private:
        FormLineEditWidget* m_repoPath = nullptr;
        QListView* m_curatedRepos = nullptr;
        QStandardItemModel* m_curatedReposModel = nullptr;
        QListView* m_uncuratedRepos = nullptr;
        QStandardItemModel* m_uncuratedReposModel = nullptr;
    };
} // namespace O3DE::ProjectManager
