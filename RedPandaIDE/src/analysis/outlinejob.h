// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "semanticpublicationbuilder.h"
#include <QByteArray>
#include <QMetaType>

struct OutlineDemand {
    quint64 service = 0;
    quint64 sequence = 0;
    quint64 workspace = 0;

    bool valid() const { return service && sequence; }
    friend bool operator==(OutlineDemand a, OutlineDemand b)
    {
        return a.service == b.service && a.sequence == b.sequence && a.workspace == b.workspace;
    }
};

namespace Analysis {

struct OutlineStamp {
    quint64 configurationTicket = 0;
    QByteArray configurationDigest;
    quint64 inputGeneration = 0;
    QString workingDirectory;

    friend bool operator==(const OutlineStamp &a, const OutlineStamp &b)
    {
        return a.configurationTicket == b.configurationTicket && a.configurationDigest == b.configurationDigest &&
            a.inputGeneration == b.inputGeneration && a.workingDirectory == b.workingDirectory;
    }
};

struct OutlineJobRequest {
    OutlineDemand demand;
    OutlineStamp stamp;
    quint64 sourceJob = 0;
    quint64 documentId = 0;
    quint64 documentRevision = 0;
    QString primaryFile;
    QString primaryPath;
    bool wholeProject = false;
    OutlineOptions options;
    std::shared_ptr<const std::atomic_bool> cancelled;
};

// Supplied only inside the writer reservation that owns the certified engine.
// Both the file list and graph keys carry the same exact lexical spellings.
struct OutlineCertifiedInput {
    OutlineStamp stamp;
    quint64 sourceJob = 0;
    QStringList files;
    bool complete = false;
};

enum class OutlineJobStatus {
    Ready,
    Obsolete,
    Unavailable,
    Partial,
    IncompleteProjectRoots,
    ResourceLimit,
    Cancelled,
    TimedOut
};

struct OutlineJobResult {
    OutlineDemand demand;
    OutlineStamp stamp;
    quint64 documentId = 0;
    quint64 documentRevision = 0;
    OutlineJobStatus status = OutlineJobStatus::Unavailable;
    SemanticPublicationPtr publication;

    bool matches(const OutlineJobRequest &current) const;
};

struct OutlineResources {
    SemanticMemoryLedger ledger;
    std::shared_ptr<SemanticReclamationScheduler> retirement;
};

OutlineJobResult buildOutline(const SemanticGraphSource &graph, const OutlineCertifiedInput &inputs,
    const OutlineJobRequest &request, SemanticIdentitySource &identities,
    const SemanticMemoryLedger &ledger, const std::shared_ptr<SemanticReclamationScheduler> &retirement);

} // namespace Analysis

Q_DECLARE_METATYPE(Analysis::OutlineJobResult)
