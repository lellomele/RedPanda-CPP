// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "compileconfiguration.h"
#include "../projectoptions.h"

namespace Compilation {

struct ProjectBuildUnit {
    QString path;
    QString buildCommand;
    QByteArray sourceEncoding;
    bool compile = false;
    bool compileAsCpp = false;
    bool link = false;
    bool overrideBuildCommand = false;
};

struct ProjectBuildSnapshot {
    QString projectPath;
    QString directory;
    QString displayName;
    QString makefilePath;
    QString outputPath;
    ProjectOptions options;
    QList<ProjectBuildUnit> units;
    QHash<QString, QSet<QString>> knownDependencies;
    FrozenConfiguration::Pointer configuration;
    bool capabilitiesKnown = false;
};

} // namespace Compilation
