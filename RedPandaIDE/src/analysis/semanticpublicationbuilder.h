// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SEMANTICPUBLICATIONBUILDER_H
#define SEMANTICPUBLICATIONBUILDER_H
#include "semanticpublication.h"
#include <QStringList>

namespace Analysis {
// This private adapter contract is read only by the engine owner. Source IDs
// and borrowed views never appear in a published object or a GUI callback.
using SemanticSourceId = quint64;
struct SemanticSourceLocation { QStringView filename; int line = -1, column = -1; };
struct SemanticSourceSymbol {
    SemanticSourceId parent = 0;
    QStringView name, qualifiedName, type, arguments, signature, value, templateArguments, selectionKey;
    // Only a proven scope group (e.g. reopened namespace in the same context).
    // Empty means no merging by name. Classes/anonymous scopes must not be
    // merged across translation contexts merely because full names match.
    QStringView scopeGroup;
    StatementKind kind{};
    StatementScope scope{};
    StatementAccessibility accessibility{};
    quint32 properties = 0;
    SemanticSourceLocation declaration, definition;
};
struct SemanticSourceVisitor {
    void *context = nullptr;
    bool (*visit)(void *, SemanticSourceId) = nullptr;
    bool operator()(SemanticSourceId id) const { return visit(context, id); }
};
class SemanticGraphSource {
public:
    virtual ~SemanticGraphSource() = default;
    virtual bool symbol(SemanticSourceId, SemanticSourceSymbol &) const = 0;
    virtual bool roots(QStringView file, SemanticSourceVisitor) const = 0;
    virtual bool children(SemanticSourceId, SemanticSourceVisitor) const = 0;
};
enum class SemanticBuildStatus { Complete, Partial, BudgetExceeded, InvalidInput, AllocationFailure, SourceFailure, Cancelled, TimedOut };
struct SemanticPublicationDiagnostics {
    SnapshotBuildDiagnostics graph;
    quint64 sourceFiles = 0, uniquePayloads = 0, textCodeUnits = 0;
    quint64 textUnitsExamined = 0;
    bool sourceUnavailable = false, identityExhausted = false;
    bool textLimitReached = false;
};
struct OutlineOptions {
    bool alphabetical = false;
    bool groupByKind = false;
    bool showInherited = true;
    bool hideSingleUnderscore = false;
    bool hideDoubleUnderscore = false;
};
struct SemanticPublicationRequest {
    PublicationIdentity identity;
    QStringList files; // One CurrentFile or the whole requested union.
    QString primaryFile;
    SnapshotBuildLimits limits;
    std::size_t initialReservation = 0;
    quint64 maximumTextUnitsExamined = 16000000;
    bool wholeProject = false;
    OutlineOptions outline;
    std::shared_ptr<const std::atomic_bool> cancelled;
    int maximumMilliseconds = 5000;
};
struct SemanticPublicationBuild {
    SemanticBuildStatus status = SemanticBuildStatus::InvalidInput;
    SemanticPublicationPtr publication;
    SemanticPublicationDiagnostics diagnostics;
    // Complete is structural completeness only. A freshness controller must
    // separately certify cache/read provenance before publishing ReadyFresh.
    bool complete() const { return status == SemanticBuildStatus::Complete && bool(publication); }
};
class SemanticPublicationBuilder {
public:
    static SemanticPublicationBuild build(const SemanticGraphSource &,
        const SemanticPublicationRequest &, SemanticIdentitySource &,
        const SemanticMemoryLedger &, std::shared_ptr<SemanticReclamationScheduler>);
};
} // namespace Analysis
#endif
