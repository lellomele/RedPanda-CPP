// SPDX-License-Identifier: GPL-3.0-or-later
#include "outlinejob.h"

namespace Analysis {

bool OutlineJobResult::matches(const OutlineJobRequest &current) const
{
    return demand == current.demand && stamp == current.stamp &&
        documentId == current.documentId && documentRevision == current.documentRevision;
}

OutlineJobResult buildOutline(const SemanticGraphSource &graph, const OutlineCertifiedInput &inputs,
    const OutlineJobRequest &request, SemanticIdentitySource &identities,
    const SemanticMemoryLedger &ledger, const std::shared_ptr<SemanticReclamationScheduler> &retirement)
{
    OutlineJobResult result{request.demand, request.stamp, request.documentId, request.documentRevision};
    if (!request.demand.valid() || request.demand.service != identities.service() ||
            !request.stamp.configurationTicket || request.stamp.configurationDigest.isEmpty() ||
            request.stamp.workingDirectory.isEmpty())
        return result;
    if (!(request.stamp == inputs.stamp) || request.sourceJob != inputs.sourceJob) {
        result.status = OutlineJobStatus::Obsolete;
        return result;
    }
    if (!inputs.complete || !inputs.sourceJob)
        return result;

    SemanticPublicationRequest build;
    build.identity = {request.demand.service, inputs.sourceJob, request.stamp.inputGeneration,
                      request.stamp.configurationTicket, request.demand.workspace};
    build.wholeProject = request.wholeProject;
    build.primaryFile = request.primaryFile;
    build.outline = request.options;
    build.cancelled = request.cancelled;
    if (request.wholeProject) {
        build.files = inputs.files;
    } else {
        if (request.primaryFile.isEmpty() || !inputs.files.contains(request.primaryFile))
            return result;
        build.files = {request.primaryFile};
    }
    const auto output = SemanticPublicationBuilder::build(graph, build, identities, ledger, retirement);
    switch (output.status) {
    case SemanticBuildStatus::Complete:
        if (!output.publication) return result;
        result.status = OutlineJobStatus::Ready;
        result.publication = output.publication;
        break;
    case SemanticBuildStatus::Partial: {
        const auto &graph = output.diagnostics.graph;
        const bool limited = output.diagnostics.textLimitReached || graph.parentDepthLimited ||
            graph.nodeLimitReached || graph.sourceLimitReached || graph.workLimitReached || graph.pendingLimitReached;
        result.status = limited ? OutlineJobStatus::ResourceLimit : OutlineJobStatus::Partial;
        break;
    }
    case SemanticBuildStatus::AllocationFailure:
    case SemanticBuildStatus::BudgetExceeded:
        result.status = OutlineJobStatus::ResourceLimit;
        break;
    case SemanticBuildStatus::Cancelled:
        result.status = OutlineJobStatus::Cancelled;
        break;
    case SemanticBuildStatus::TimedOut:
        result.status = OutlineJobStatus::TimedOut;
        break;
    case SemanticBuildStatus::InvalidInput:
    case SemanticBuildStatus::SourceFailure:
        break;
    }
    return result;
}

} // namespace Analysis
