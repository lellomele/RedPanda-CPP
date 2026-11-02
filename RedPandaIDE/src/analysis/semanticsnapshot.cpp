// SPDX-License-Identifier: GPL-3.0-or-later
#include "semanticsnapshotbuilder.h"

#include <QSet>
#include <algorithm>

namespace Analysis {
namespace {
bool scopeKind(StatementKind kind)
{
    return kind == StatementKind::Namespace || kind == StatementKind::Class
        || kind == StatementKind::EnumType || kind == StatementKind::EnumClassType;
}

bool excluded(const Statement &statement)
{
    return statement.scope == StatementScope::Local || statement.kind == StatementKind::Block
        || statement.kind == StatementKind::Lambda;
}

QString keyPart(const QString &text)
{
    return QString::number(text.size()) + QLatin1Char(':') + text;
}

QString scopeKey(const Statement &statement)
{
    return QString::number(static_cast<int>(statement.kind)) + QLatin1Char(':')
        + keyPart(statement.fullName) + keyPart(statement.templateSpecializationParams);
}

SemanticSymbol symbolValue(const Statement &statement, const QString &primaryFile)
{
    SemanticSymbol value;
    value.name = statement.command;
    value.qualifiedName = statement.fullName;
    value.type = statement.type;
    value.arguments = statement.args;
    value.signature = statement.noNameArgs;
    value.value = statement.value;
    value.templateArguments = statement.templateSpecializationParams;
    value.kind = statement.kind;
    value.scope = statement.scope;
    value.accessibility = statement.accessibility;
    value.properties = static_cast<quint32>(static_cast<int>(statement.properties));
    value.declaration = {statement.fileName, statement.line, -1};
    value.definition = {statement.definitionFileName, statement.definitionLine, -1};
    const QString declarationKey = AnalysisInputSnapshot::pathKey(statement.fileName);
    value.selectionKey = QString::number(static_cast<int>(statement.kind)) + QLatin1Char(':')
        + keyPart(declarationKey) + keyPart(statement.fullName) + keyPart(statement.noNameArgs);
    const QString fileKey = AnalysisInputSnapshot::pathKey(primaryFile);
    value.contextOnly = declarationKey != fileKey
        && AnalysisInputSnapshot::pathKey(statement.definitionFileName) != fileKey;
    return value;
}

bool prefer(const SemanticSymbol &candidate, const SemanticSymbol &existing, const QString &primaryFile)
{
    const QString key = AnalysisInputSnapshot::pathKey(primaryFile);
    const auto rank = [&key](const SemanticSymbol &symbol) {
        return (AnalysisInputSnapshot::pathKey(symbol.declaration.filename) == key ? 4 : 0)
            + (AnalysisInputSnapshot::pathKey(symbol.definition.filename) == key ? 2 : 0)
            + (symbol.hasProperty(StatementProperty::HasDefinition) ? 1 : 0);
    };
    const int candidateRank = rank(candidate), existingRank = rank(existing);
    if (candidateRank != existingRank)
        return candidateRank > existingRank;
    if (candidate.declaration.filename != existing.declaration.filename)
        return candidate.declaration.filename < existing.declaration.filename;
    return candidate.declaration.line >= 0
        && (existing.declaration.line < 0 || candidate.declaration.line < existing.declaration.line);
}
} // namespace

SnapshotIdentity SemanticSnapshotBuilder::inputIdentity(const PAnalysisInputSnapshot &inputs,
                                                       quint64 serviceId, quint64 jobId,
                                                       quint64 configurationGeneration,
                                                       quint64 workspaceGeneration,
                                                       const QString &primaryFile)
{
    SnapshotIdentity identity;
    identity.serviceId = serviceId;
    identity.jobId = jobId;
    identity.configurationGeneration = configurationGeneration;
    identity.workspaceGeneration = workspaceGeneration;
    identity.primaryFile = primaryFile;
    if (inputs) {
        identity.inputGeneration = inputs->generation();
        identity.sources.reserve(inputs->documents().size());
        for (const auto &document : inputs->documents())
            identity.sources.append({document->filename, document->id, document->revision, {}});
        std::sort(identity.sources.begin(), identity.sources.end(), [](const SourceRevision &a, const SourceRevision &b) {
            if (a.filename != b.filename)
                return a.filename < b.filename;
            return a.documentId < b.documentId;
        });
    }
    return identity;
}

FileSnapshotPtr SemanticSnapshotBuilder::file(const SnapshotIdentity &identity,
                                             const QString &filename,
                                             const StatementMap &fileStatements,
                                             const SnapshotBuildLimits &limits)
{
    std::unique_ptr<SemanticFileSnapshot> candidate(new SemanticFileSnapshot);
    candidate->mIdentity = identity;
    candidate->mFilename = filename;
    auto &nodes = candidate->mNodes;
    auto &roots = candidate->mRoots;
    auto &diagnostics = candidate->mDiagnostics;
    QHash<QString, NodeIndex> scopes;
    bool exhausted = false;
    const auto consumeWork = [&] {
        if (diagnostics.workItems >= limits.maximumWorkItems) {
            diagnostics.workLimitReached = true;
            exhausted = true;
            return false;
        }
        ++diagnostics.workItems;
        return true;
    };

    const auto appendNode = [&](const Statement &statement, NodeIndex parent) -> NodeIndex {
        if (static_cast<quint64>(nodes.size()) >= limits.maximumNodes
                || static_cast<quint64>(nodes.size()) >= NoNode) {
            diagnostics.nodeLimitReached = true;
            exhausted = true;
            return NoNode;
        }
        const NodeIndex index = static_cast<NodeIndex>(nodes.size());
        nodes.append({symbolValue(statement, filename), parent, {}});
        if (parent == NoNode)
            roots.append(index);
        else
            nodes[parent].children.append(index);
        return index;
    };
    struct Parent { NodeIndex node = NoNode; bool allowed = true; };
    const auto ensureScope = [&](PStatement source) -> Parent {
        QVector<PStatement> chain;
        QSet<const Statement *> ancestors;
        NodeIndex parent = NoNode;
        while (source) {
            if (!consumeWork())
                return {NoNode, false};
            ++diagnostics.parentSteps;
            if (excluded(*source))
                return {NoNode, false};
            if (!scopeKind(source->kind))
                break;
            if (ancestors.contains(source.get())) {
                diagnostics.cycleDetected = true;
                break;
            }
            ancestors.insert(source.get());
            const auto existing = scopes.constFind(scopeKey(*source));
            if (existing != scopes.constEnd()) {
                parent = existing.value();
                const auto value = symbolValue(*source, filename);
                if (prefer(value, nodes[parent].symbol, filename))
                    nodes[parent].symbol = value;
                ++diagnostics.mergedScopes;
                break;
            }
            if (static_cast<quint64>(chain.size()) >= limits.maximumParentDepth) {
                diagnostics.parentDepthLimited = true;
                exhausted = true;
                break;
            }
            chain.append(source);
            source = source->parentScope.lock();
        }
        while (!chain.isEmpty()) {
            const auto item = chain.takeLast();
            const NodeIndex created = appendNode(*item, parent);
            if (created == NoNode)
                return {NoNode, false};
            scopes.insert(scopeKey(*item), created);
            parent = created;
        }
        return {parent, true};
    };

    struct Frame { PStatement source; bool leaving = false; };
    QVector<Frame> pending;
    QSet<const Statement *> visited;
    QSet<const Statement *> active;
    const auto scheduleFrame = [&](const PStatement &source, bool leaving) {
        if (static_cast<quint64>(pending.size()) >= limits.maximumPendingFrames) {
            diagnostics.pendingLimitReached = true;
            exhausted = true;
            return false;
        }
        pending.append({source, leaving});
        ++diagnostics.scheduledFrames;
        diagnostics.peakPendingFrames = std::max(diagnostics.peakPendingFrames,
                                                static_cast<quint64>(pending.size()));
        return true;
    };
    const auto pushMap = [&](const StatementMap &statements) {
        auto it = statements.constEnd();
        while (!exhausted && it != statements.constBegin()) {
            --it;
            // Charge even a visited/null entry. Shared children can make edge
            // traversal arbitrarily larger than the unique-symbol set.
            if (!consumeWork())
                break;
            ++diagnostics.sourceEntriesExamined;
            const auto &source = it.value();
            if (!source)
                continue;
            if (visited.contains(source.get())) {
                if (active.contains(source.get()))
                    diagnostics.cycleDetected = true;
                continue;
            }
            if (!scheduleFrame(source, false))
                break;
        }
    };
    pushMap(fileStatements);
    while (!exhausted && !pending.isEmpty()) {
        const Frame frame = pending.takeLast();
        ++diagnostics.processedFrames;
        if (frame.leaving) {
            active.remove(frame.source.get());
            continue;
        }
        if (visited.contains(frame.source.get())) {
            if (active.contains(frame.source.get()))
                diagnostics.cycleDetected = true;
            continue;
        }
        if (diagnostics.visitedStatements >= limits.maximumSourceStatements) {
            diagnostics.sourceLimitReached = true;
            exhausted = true;
            break;
        }
        visited.insert(frame.source.get());
        ++diagnostics.visitedStatements;
        if (excluded(*frame.source))
            continue;
        const Parent parent = scopeKind(frame.source->kind)
            ? ensureScope(frame.source) : ensureScope(frame.source->parentScope.lock());
        if (exhausted)
            break;
        if (!parent.allowed)
            continue;
        if (!scopeKind(frame.source->kind) && appendNode(*frame.source, parent.node) == NoNode)
            break;
        if (diagnostics.nodeLimitReached)
            break;
        active.insert(frame.source.get());
        if (!consumeWork() || !scheduleFrame(frame.source, true))
            break;
        pushMap(frame.source->children);
    }
    // Deterministic source order only. View-specific name/type/inheritance
    // policies can project these immutable values without querying the engine.
    const auto less = [&nodes](NodeIndex a, NodeIndex b) {
        const auto &left = nodes[a].symbol, &right = nodes[b].symbol;
        if (left.declaration.filename != right.declaration.filename)
            return left.declaration.filename < right.declaration.filename;
        if (left.declaration.line != right.declaration.line)
            return left.declaration.line < right.declaration.line;
        if (left.selectionKey != right.selectionKey)
            return left.selectionKey < right.selectionKey;
        return a < b;
    };
    std::sort(roots.begin(), roots.end(), less);
    for (auto &node : nodes)
        std::sort(node.children.begin(), node.children.end(), less);
    return FileSnapshotPtr(std::move(candidate));
}

MacroCatalogPtr SemanticSnapshotBuilder::macros(const SnapshotIdentity &identity,
                                               const StatementMap &macroStatements,
                                               const IncludeSearchConfiguration &includes,
                                               const SnapshotBuildLimits &limits)
{
    std::unique_ptr<MacroCatalogSnapshot> candidate(new MacroCatalogSnapshot);
    candidate->mIdentity = identity;
    candidate->mIncludeConfiguration = includes;
    QSet<const Statement *> visited;
    for (const auto &statement : macroStatements) {
        if (candidate->mDiagnostics.workItems >= limits.maximumWorkItems) {
            candidate->mDiagnostics.workLimitReached = true;
            break;
        }
        ++candidate->mDiagnostics.workItems;
        ++candidate->mDiagnostics.sourceEntriesExamined;
        if (!statement || visited.contains(statement.get()))
            continue;
        if (candidate->mDiagnostics.visitedStatements >= limits.maximumSourceStatements) {
            candidate->mDiagnostics.sourceLimitReached = true;
            break;
        }
        visited.insert(statement.get());
        ++candidate->mDiagnostics.visitedStatements;
        if (statement->kind != StatementKind::Preprocessor)
            continue;
        if (static_cast<quint64>(candidate->mMacros.size()) >= limits.maximumNodes) {
            candidate->mDiagnostics.nodeLimitReached = true;
            break;
        }
        candidate->mMacros.append(symbolValue(*statement, identity.primaryFile));
    }
    std::sort(candidate->mMacros.begin(), candidate->mMacros.end(), [](const SemanticSymbol &a, const SemanticSymbol &b) {
        if (a.name != b.name)
            return a.name < b.name;
        return a.selectionKey < b.selectionKey;
    });
    return MacroCatalogPtr(std::move(candidate));
}
} // namespace Analysis
