// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SEMANTICSNAPSHOT_H
#define SEMANTICSNAPSHOT_H

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>
#include <limits>
#include <memory>

// The DTO layer shares value enums with the engine, not its mutable objects.
enum class StatementKind;
enum class StatementScope;
enum class StatementAccessibility;
enum class StatementProperty;

namespace Analysis {

using NodeIndex = quint32;
constexpr NodeIndex NoNode = std::numeric_limits<NodeIndex>::max();

struct SourceLocation {
    QString filename;
    int line = -1;
    int column = -1;
};
struct SourceRevision {
    QString filename;
    quint64 documentId = 0;
    quint64 revision = 0;
    QByteArray diskContentHash;
};
// Identity describes the job's input, not a claim that every retained engine
// cache entry is fresh. The coordinator validates the dependency manifest.
struct SnapshotIdentity {
    quint64 serviceId = 0;
    quint64 jobId = 0;
    quint64 inputGeneration = 0;
    quint64 configurationGeneration = 0;
    quint64 workspaceGeneration = 0;
    QString primaryFile;
    QVector<SourceRevision> sources;
};
struct IncludeSearchConfiguration {
    quint64 generation = 0;
    QStringList projectDirectories;
    QStringList systemDirectories;
};
// No child/parent pointers and no ranking/usage fields. All strings and flags
// are detached value objects once the builder publishes its const owner.
struct SemanticSymbol {
    QString name;
    QString qualifiedName;
    QString type;
    QString arguments;
    QString signature;
    QString value;
    QString templateArguments;
    QString selectionKey;
    StatementKind kind{};
    StatementScope scope{};
    StatementAccessibility accessibility{};
    quint32 properties = 0;
    SourceLocation declaration;
    SourceLocation definition;
    bool contextOnly = false;
    bool hasProperty(StatementProperty property) const {
        return (properties & static_cast<quint32>(property)) != 0;
    }
};
struct OutlineNode {
    SemanticSymbol symbol;
    NodeIndex parent = NoNode;
    QVector<NodeIndex> children;
};
struct SnapshotBuildDiagnostics {
    quint64 visitedStatements = 0;
    quint64 mergedScopes = 0;
    // Cumulative work counts every scanned map entry (including duplicates and
    // nulls), parent-chain step and scheduled leave frame. A single graph may
    // contain far more edges than distinct symbols.
    quint64 workItems = 0;
    quint64 sourceEntriesExamined = 0;
    quint64 parentSteps = 0;
    quint64 scheduledFrames = 0;
    quint64 processedFrames = 0;
    quint64 peakPendingFrames = 0;
    bool cycleDetected = false;
    bool parentDepthLimited = false;
    bool nodeLimitReached = false;
    bool sourceLimitReached = false;
    bool workLimitReached = false;
    bool pendingLimitReached = false;
    bool complete() const {
        return !cycleDetected && !parentDepthLimited && !nodeLimitReached && !sourceLimitReached
            && !workLimitReached && !pendingLimitReached;
    }
};
struct SnapshotBuildLimits {
    quint32 maximumNodes = 250000;
    quint64 maximumSourceStatements = 1000000;
    quint32 maximumParentDepth = 4096;
    quint64 maximumWorkItems = 2000000;
    quint32 maximumPendingFrames = 1000000;
};
class SemanticSnapshotBuilder;
class SemanticFileSnapshot {
public:
    const SnapshotIdentity &identity() const { return mIdentity; }
    const QString &filename() const { return mFilename; }
    const QVector<OutlineNode> &nodes() const { return mNodes; }
    const QVector<NodeIndex> &roots() const { return mRoots; }
    const SnapshotBuildDiagnostics &diagnostics() const { return mDiagnostics; }
    bool complete() const { return mDiagnostics.complete(); }
private:
    friend class SemanticSnapshotBuilder;
    SemanticFileSnapshot() = default;
    SnapshotIdentity mIdentity;
    QString mFilename;
    QVector<OutlineNode> mNodes;
    QVector<NodeIndex> mRoots;
    SnapshotBuildDiagnostics mDiagnostics;
};
using FileSnapshotPtr = std::shared_ptr<const SemanticFileSnapshot>;
class MacroCatalogSnapshot {
public:
    const SnapshotIdentity &identity() const { return mIdentity; }
    const QVector<SemanticSymbol> &macros() const { return mMacros; }
    const IncludeSearchConfiguration &includeConfiguration() const { return mIncludeConfiguration; }
    const SnapshotBuildDiagnostics &diagnostics() const { return mDiagnostics; }
    bool complete() const { return mDiagnostics.complete(); }
private:
    friend class SemanticSnapshotBuilder;
    MacroCatalogSnapshot() = default;
    SnapshotIdentity mIdentity;
    IncludeSearchConfiguration mIncludeConfiguration;
    QVector<SemanticSymbol> mMacros;
    SnapshotBuildDiagnostics mDiagnostics;
};
using MacroCatalogPtr = std::shared_ptr<const MacroCatalogSnapshot>;
} // namespace Analysis
#endif
