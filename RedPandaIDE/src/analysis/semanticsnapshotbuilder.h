// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SEMANTICSNAPSHOTBUILDER_H
#define SEMANTICSNAPSHOTBUILDER_H
#include "semanticsnapshot.h"
#include "../documentstore.h"
#include "../parser/parserutils.h"

namespace Analysis {
// Engine-boundary adapter: call only while the engine owner has exclusive
// access to its mutable graph. Consumers include semanticsnapshot.h instead.
class SemanticSnapshotBuilder {
public:
    static SnapshotIdentity inputIdentity(const PAnalysisInputSnapshot &inputs,
                                         quint64 serviceId, quint64 jobId,
                                         quint64 configurationGeneration,
                                         quint64 workspaceGeneration,
                                         const QString &primaryFile);
    // One affected file's flat map, not the whole project. Required parent
    // scopes are copied as value placeholders.
    static FileSnapshotPtr file(const SnapshotIdentity &identity,
                                const QString &filename,
                                const StatementMap &fileStatements,
                                const SnapshotBuildLimits &limits = {});
    // Caller supplies the intended macro map once per job/configuration. No
    // arbitrary class/scope subgraphs are traversed for macro completion.
    static MacroCatalogPtr macros(const SnapshotIdentity &identity,
                                  const StatementMap &macroStatements,
                                  const IncludeSearchConfiguration &includes = {},
                                  const SnapshotBuildLimits &limits = {});
};
} // namespace Analysis
#endif
