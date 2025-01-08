/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#if !defined(Q_MOC_RUN)
#include <QString>
#include <QStringList>
#include <QVector>
#include <QMetaType>
#include <QPixmap>
#endif

namespace O3DE::ProjectManager
{
    class GemInfo
    {
    public:
        enum Platform
        {
            Android = 1 << 0,
            iOS = 1 << 1,
            Linux = 1 << 2,
            macOS = 1 << 3,
            Windows = 1 << 4,
            NumPlatforms = 5
        };
        Q_DECLARE_FLAGS(Platforms, Platform)
        static QString GetPlatformString(Platform platform);

        enum Type
        {
            Asset = 1 << 0,
            Code = 1 << 1,
            Tool = 1 << 2,
            NumTypes = 3
        };
        Q_DECLARE_FLAGS(Types, Type)
        static QString GetTypeString(Type type);

        enum GemOrigin
        {
            Open3DEngine = 1 << 0,
            Local = 1 << 1,
            Remote = 1 << 2,
            NumGemOrigins = 3
        };
        Q_DECLARE_FLAGS(GemOrigins, GemOrigin)
        static QString GetGemOriginString(GemOrigin origin);

        enum DownloadStatus
        {
            UnknownDownloadStatus = -1,
            NotDownloaded,
            Downloading,
            DownloadSuccessful,
            DownloadFailed,
            Downloaded
        };
        static QString GetDownloadStatusString(DownloadStatus status);

        static Platforms GetPlatformFromString(const QString& platformText);

        static Platforms GetPlatformsFromStringList(const QStringList& platformStrings);

        GemInfo() = default;
        GemInfo(const QString& name, const QString& creator, const QString& summary, Platforms platforms, bool isAdded);
        bool IsPlatformSupported(Platform platform) const;
        QString GetNameWithVersionSpecifier(const QString& comparator = "==") const;
        bool IsValid() const;
        bool IsCompatible() const;

        bool operator<(const GemInfo& gemInfo) const;

        QStringList GetPlatformsAsStringList() const;

        QString m_path;
        QString m_name = "Unknown Gem Name";
        QString m_displayName;
        QString m_origin = "Unknown Creator";
        GemOrigin m_gemOrigin = Local;
        QString m_originURL;

        ////////////////////////////////////////////////////////
        // icon
        // When displayed in the project manager the gem icon can be inside or outside the gem
        // The optional icon uri is the icon you will see, if set, when the gem is remote or has no icon path
        //   *If set this icon file is automatically downloaded and cached if it exists
        // The icon path is the icon you will see if set when the gem is local
        // Normally these are exactly the same icon, but do not have to be
        // If neither is set the default gem.svg will be used

        // Icon path is the optional relative path to the icon file in the gem from the gem root
        // If appended to the gem root this would get you the local icon file
        // This is the icon you will see in the program manager if the object is local
        // i.e
        // c:/Gems/Input <--this gems local root
        QString m_iconPath; // i.e. "resources/icon.jpg"
        QString m_iconPreviewPath; // This is the full local path
        QPixmap m_iconPixMap;
        // would instruct the program manager to use "c:/Gems/Input/reources/icon.jpg" as the
        // icon in program manager when this gem is local

        // Icon uri is the optional full internet address of the icon to be cached and seen in the
        // program manager when the gem is remote or if no icon path is set
        //   *If set this icon file is automatically downloaded and cached if it exists
        QString m_iconUri; // i.e. "https://overlo3de.com/apmg/input/resources/icon.jpg"
        QString m_iconUriPreviewPath; //This is the local cache of the iconUri
        QPixmap m_iconUriPixMap;


        bool m_isAdded = false; //! Is the gem explicitly added (not a dependency) and enabled in the project?
        bool m_isEngineGem = false;
        bool m_isProjectGem = false;
        QString m_summary = "No summary provided.";
        Platforms m_platforms;
        Types m_types; //! Asset and/or Code and/or Tool
        DownloadStatus m_downloadStatus = UnknownDownloadStatus;
        QStringList m_features;
        QString m_requirement;
        QString m_licenseText;
        QString m_licenseLink;
        QString m_directoryLink;
        QString m_documentationLink;
        QString m_repoUri;
        QString m_version = "Unknown Version";
        QString m_lastUpdatedDate = "Unknown Date";
        int m_binarySizeInKB = 0;
        QStringList m_dependencies;
        QStringList m_compatibleEngines;
        QStringList m_incompatibleEngineDependencies; //! Specific to the current project's engine 
        QStringList m_incompatibleGemDependencies; //! Specific to the current project and engine
        QString m_downloadSourceUri;
        QString m_sourceControlUri;
        QString m_sourceControlRef;
    };
} // namespace O3DE::ProjectManager

Q_DECLARE_METATYPE(O3DE::ProjectManager::GemInfo);

Q_DECLARE_OPERATORS_FOR_FLAGS(O3DE::ProjectManager::GemInfo::Platforms);
Q_DECLARE_OPERATORS_FOR_FLAGS(O3DE::ProjectManager::GemInfo::Types);
Q_DECLARE_OPERATORS_FOR_FLAGS(O3DE::ProjectManager::GemInfo::GemOrigins);

