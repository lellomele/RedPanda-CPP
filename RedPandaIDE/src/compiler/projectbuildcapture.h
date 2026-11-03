// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "compilationcapture.h"
#include "projectbuildsnapshot.h"
#include "../editor.h"
#include "qt_utils/utils.h"

#include <QElapsedTimer>

namespace Compilation {

inline ProjectBuildSnapshot captureProjectBuild(Project &project, ConfigurationValues configuration,
                                                bool capabilitiesKnown, quint64 documentGeneration)
{
    ProjectBuildSnapshot snapshot;
    snapshot.projectPath = project.filename();
    snapshot.directory = project.directory();
    snapshot.displayName = project.name();
    snapshot.makefilePath = project.makeFileName();
    snapshot.outputPath = project.outputFilename();
    snapshot.options = project.options();
    snapshot.capabilitiesKnown = capabilitiesKnown;

    const auto units = project.unitList();
    snapshot.units.reserve(units.size());
    for (const auto &unit : units) {
        const Editor *editor = project.unitEditor(unit);
        QByteArray encoding = editor ? editor->fileEncoding() : unit->realEncoding();
        if (encoding.isEmpty()) {
            encoding = unit->encoding();
            if (encoding == ENCODING_PROJECT) encoding = snapshot.options.encoding;
            if (encoding == ENCODING_AUTO_DETECT) encoding.clear();
        }
        if (encoding == ENCODING_SYSTEM_DEFAULT) encoding = configuration.preferences.systemEncoding;
        else if (encoding == ENCODING_OEM_DEFAULT) encoding = configuration.preferences.consoleEncoding;
        else if (encoding == ENCODING_UTF8_BOM) encoding = ENCODING_UTF8;
        else if (encoding == ENCODING_UTF16_BOM) encoding = ENCODING_UTF16;
        else if (encoding == ENCODING_UTF32_BOM) encoding = ENCODING_UTF32;
        snapshot.units.append({unit->fileName(), unit->buildCmd(), std::move(encoding),
            unit->compile(), unit->compileCpp(), unit->link(), unit->overrideBuildCmd()});
    }
    snapshot.configuration = FrozenConfiguration::capture(configuration);
    const auto parser = project.cppParser();
    if (!parser || !parser->freezeStable()) return snapshot;
    const auto release = finally([&] { parser->unFreeze(); });
    const auto report = parser->sourceJobReport();
    if (!report || !report->complete() || report->inputGeneration != documentGeneration) return snapshot;

    // Missing graph entries select the writer's conservative header fallback.
    // Keep the lease short when the project has many units.
    QElapsedTimer elapsed;
    elapsed.start();
    qsizetype retainedEdges = 0;
    constexpr qsizetype edgeLimit = 32768;
    for (const auto &unit : std::as_const(snapshot.units)) {
        if (elapsed.elapsed() >= 8 || retainedEdges >= edgeLimit) break;
        if (!unit.compile || !parser->fileScanned(unit.path)) continue;
        auto includes = parser->getIncludedFiles(unit.path);
        if (includes.size() > edgeLimit - retainedEdges) continue;
        retainedEdges += includes.size();
        snapshot.knownDependencies.insert(unit.path, std::move(includes));
    }
    return snapshot;
}

} // namespace Compilation
