/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <GemRepo/GemRepoAddDialog.h>
#include <FormFolderBrowseEditWidget.h>
#include <AzCore/Utils/Utils.h>
#include <PythonBindingsInterface.h>
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QAbstractItemView>
#include <QListView>
#include <QStandardItemModel>

namespace O3DE::ProjectManager
{
    GemRepoAddDialog::GemRepoAddDialog(QWidget* parent)
        : QDialog(parent)
    {
        setWindowTitle(tr("Add a User Repository"));
        setModal(true);
        setObjectName("addGemRepoDialog");

        QVBoxLayout* vLayout = new QVBoxLayout();
        vLayout->setContentsMargins(30, 30, 25, 10);
        vLayout->setSpacing(0);
        vLayout->setAlignment(Qt::AlignTop);
        setLayout(vLayout);

        QLabel* instructionTitleLabel = new QLabel(tr("Enter a valid path to add a new user repository"));
        instructionTitleLabel->setObjectName("gemRepoAddDialogInstructionTitleLabel");
        instructionTitleLabel->setAlignment(Qt::AlignLeft);
        vLayout->addWidget(instructionTitleLabel);

        vLayout->addSpacing(10);

        QLabel* instructionContextLabel = new QLabel(tr("The path can be a Repository URL or a Local Path in your directory."));
        instructionContextLabel->setAlignment(Qt::AlignLeft);
        vLayout->addWidget(instructionContextLabel);

        m_repoPath = new FormFolderBrowseEditWidget(tr("Repository Path"), "", this);
        m_repoPath->setFixedSize(QSize(600, 100));
        vLayout->addWidget(m_repoPath);

        vLayout->addSpacing(10);

        QLabel* curatedReposLabel = new QLabel(tr("Curated Repos"));
        curatedReposLabel->setToolTip(
            "All curated repos start as uncurated and then can be promoted to curated. "
            "The bar for curated repos is higher than uncurated repos. "
            "The criteria needed for a repo to be curated is: All objects in the repo have to be LEGAL, maintained, safe, and useful. "
            "Anyone can create a PR to have any repo promoted from uncurated to curated if they believe it meets the criteria. "
            "Promotion from uncurated to curated requires 2 maintainers and O3DE director approval. "
            "O3DE DOES NOT vet the contents of ANY repos other than O3DE canonical repos. "
            "O3DE offers no guarantee, stated or implied, of fitness for any particular use. "
            "O3DE assumes no liability for the contents of any repo other than canonical repos. "
            "Curated repos are regularly reviewed to make sure they fit the criteria. "
            "If there is lapse in any criteria, the repo will be demoted to uncurated."
            "O3DE reserves the right to remove or demote any repo at any time for any reason, including no reason. "
            "If a DMCA takedown is issued about any repo, anything ILLEGAL is reported and confirmed, or any violation by sanctioned entity occurs the repo will be removed immediately and the owner may or may not be notified. "
            "Anyone may petition the demotion or removal of any curated repo: You have to convince 2 maintainers to sign off that the repo should be removed or demoted, it will be removed or demoted. "
            "If given a reason for demotion or removal and after remediation, anyone may resubmit it for consideration. Priority will be given to any remediation done within 2 weeks of removal or demotion. "
            "If the repo is removed or demoted, anyone may appeal this decision directly to the TSC. "
            "!!!PROCEED WITH CAUTION!!! "
        );
        curatedReposLabel->setAlignment(Qt::AlignLeft);
        vLayout->addWidget(curatedReposLabel);

        m_curatedRepos = new QListView();
        m_curatedRepos->setStyleSheet("QListView { border: 1px solid white; }");
        m_curatedRepos->setSelectionMode(QAbstractItemView::SingleSelection);
        m_curatedRepos->setFixedSize(QSize(600, 100));
        m_curatedRepos->setObjectName("gemRepoAddDialogCuratedRepos");
        m_curatedRepos->setEditTriggers(QListView::NoEditTriggers);

        m_curatedReposModel = new QStandardItemModel(this);
        m_curatedRepos->setModel(m_curatedReposModel);

        vLayout->addWidget(m_curatedRepos);

        QString curated_repos = PythonBindingsInterface::Get()->GetCacheFile("https://canonical.o3de.org/curated.json");
        QFile curatedFile(curated_repos);
        if (curatedFile.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            QByteArray jsonData = curatedFile.readAll();
            curatedFile.close();

            QJsonDocument document = QJsonDocument::fromJson(jsonData);
            QJsonObject jsonObject = document.object();
            if (jsonObject.contains("curated") && jsonObject["curated"].isArray())
            {
                QJsonArray curatedRepos = jsonObject["curated"].toArray();
                for (const QJsonValue& value : qAsConst(curatedRepos))
                {
                    QStandardItem* item = new QStandardItem(value.toString());
                    m_curatedReposModel->appendRow(item);
                }
            }
        }

        connect(
            m_curatedRepos->selectionModel(),
            &QItemSelectionModel::selectionChanged,
            [=](const QItemSelection& /*selected*/, const QItemSelection& /*deselected*/)
            {
            QModelIndex index = m_curatedRepos->currentIndex();
            QString itemText = index.data(Qt::DisplayRole).toString();
            m_repoPath->lineEdit()->setText(itemText);
            }
        );

        QLabel* uncuratedReposLabel = new QLabel(tr("Uncurated Repos"));
        uncuratedReposLabel->setToolTip(
            "The bar for uncurated repos is lower than curated repos. "
            "The criteria needed for a repo to be uncurated is: All objects in the repo have to be LEGAL. "
            "Anyone can create a PR to have any repo added to uncurated if they believe it meets the criteria. "
            "Additions to uncurated requires 2 maintainers approval. "
            "O3DE DOES NOT vet the contents of ANY repos other than O3DE canonical repos. "
            "O3DE offers no guarantee, stated or implied, of fitness for any particular use. "
            "O3DE assumes no liability for the contents of any repo other than canonical repos. "
            "Uncurated repos are NOT reviewed to make sure they fit the criteria, they rely entirely on the community to police. "
            "O3DE reserves the right to remove or demote any repo at any time for any reason, including no reason. "
            "If a DMCA takedown is issued about any repo, anything ILLEGAL is reported and confirmed, or any violation by sanctioned entity occurs the repo will be removed immediately and the owner may or may not be notified. "
            "Anyone may petition the removal of any uncurated repo: You have to convince 2 maintainers to sign off that the repo should be removed, and it will be removed. "
            "If given a reason for removal and after remediation, anyone may resubmit it for consideration. Priority will be given to any remediation done within 2 weeks of removal. "
            "If the repo is removed, anyone may appeal the decision directly to the TSC. "
            "!!!PROCEED WITH CAUTION!!! "
        );
        communityReposLabel->setAlignment(Qt::AlignLeft);
        vLayout->addWidget(communityReposLabel);

        m_uncuratedRepos = new QListView();
        m_uncuratedRepos->setStyleSheet("QListView { border: 1px solid white; }");
        m_uncuratedRepos->setSelectionMode(QAbstractItemView::SingleSelection);
        m_uncuratedRepos->setFixedSize(QSize(600, 100));
        m_uncuratedRepos->setObjectName("gemRepoAddDialogCommunityRepos");
        m_uncuratedRepos->setEditTriggers(QListView::NoEditTriggers);

        m_uncuratedReposModel = new QStandardItemModel(this);
        m_uncuratedRepos->setModel(m_uncuratedReposModel);

        vLayout->addWidget(m_uncuratedRepos);

        QString uncurated_repos = PythonBindingsInterface::Get()->GetCacheFile("https://canonical.o3de.org/uncurated.json");
        QFile file(uncurated_repos);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            QByteArray jsonData = file.readAll();
            file.close();

            QJsonDocument document = QJsonDocument::fromJson(jsonData);
            QJsonObject jsonObject = document.object();
            if (jsonObject.contains("uncurated") && jsonObject["uncurated"].isArray())
            {
                QJsonArray communityRepos = jsonObject["uncurated"].toArray();
                for (const QJsonValue& value : qAsConst(communityRepos))
                {
                    QStandardItem* item = new QStandardItem(value.toString());
                    m_uncuratedReposModel->appendRow(item);
                }
            }
        }

        connect(
            m_uncuratedRepos->selectionModel(),
            &QItemSelectionModel::selectionChanged,
            [=](const QItemSelection& /*selected*/, const QItemSelection& /*deselected*/)
            {
                QModelIndex index = m_uncuratedRepos->currentIndex();
                QString itemText = index.data(Qt::DisplayRole).toString();
                m_repoPath->lineEdit()->setText(itemText);
            }
        );

        vLayout->addSpacing(10);

        QLabel* warningLabel = new QLabel(tr("Online repositories may contain files that could potentially harm your computer,"
            " please ensure you understand the risks before downloading Gems from third-party sources."));
        warningLabel->setObjectName("gemRepoAddDialogWarningLabel");
        warningLabel->setWordWrap(true);
        warningLabel->setAlignment(Qt::AlignLeft);
        vLayout->addWidget(warningLabel);

        vLayout->addSpacing(40);

        QDialogButtonBox* dialogButtons = new QDialogButtonBox();
        dialogButtons->setObjectName("footer");
        vLayout->addWidget(dialogButtons);

        QPushButton* cancelButton = dialogButtons->addButton(tr("Cancel"), QDialogButtonBox::RejectRole);
        cancelButton->setProperty("secondary", true);
        QPushButton* applyButton = dialogButtons->addButton(tr("Add"), QDialogButtonBox::ApplyRole);
        applyButton->setProperty("primary", true);

        connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
        connect(applyButton, &QPushButton::clicked, this, &QDialog::accept);
    }

    QString GemRepoAddDialog::GetRepoPath()
    {
        return m_repoPath->lineEdit()->text();
    }
} // namespace O3DE::ProjectManager
