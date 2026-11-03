// SPDX-License-Identifier: GPL-3.0-or-later
#include "cppparser.h"
#include "../analysis/enginesemanticadapter.h"
#include "qt_utils/utils.h"

#include <QDir>
#include <QMetaObject>
#include <algorithm>
#include <limits>

void CppParser::cancelOutline()
{
    if (!onControlOwner()) return;
    QMutexLocker locker(&mMutex);
    mExpectedOutline.reset();
    if (mActiveAnalysisCommand && mActiveAnalysisCommand->kind == AnalysisCommandKind::BuildOutline)
        mActiveAnalysisCommand->cancelled->store(true, std::memory_order_release);
    mAnalysisQueue.erase(std::remove_if(mAnalysisQueue.begin(), mAnalysisQueue.end(), [](auto &command) {
        if (command.kind != AnalysisCommandKind::BuildOutline) return false;
        command.cancelled->store(true, std::memory_order_release);
        return true;
    }), mAnalysisQueue.end());
}

bool CppParser::submitOutline(Analysis::OutlineJobRequest request,
                             std::shared_ptr<const Analysis::OutlineResources> resources)
{
    if (!onControlOwner() || !request.demand.valid() || request.demand.service != outlineService() ||
            !resources || !resources->retirement || weak_from_this().expired())
        return false;
    {
        QMutexLocker locker(&mMutex);
        if (mSchedulingStopped || mConfigurationState != ConfigurationState::Ready ||
                request.stamp.configurationTicket != mAppliedConfigurationTicket ||
                request.stamp.configurationDigest != mAppliedConfigurationDigest ||
                mNextAnalysisRequest == std::numeric_limits<quint64>::max())
            return false;
        if (mExpectedOutline && *mExpectedOutline == request.demand) return true;
    }
    cancelOutline();
    {
        QMutexLocker locker(&mMutex);
        mAsyncOwner = weak_from_this();
        mExpectedOutline = request.demand;
        AnalysisCommand command;
        command.kind = AnalysisCommandKind::BuildOutline;
        command.id = ++mNextAnalysisRequest;
        command.ticket = request.stamp.configurationTicket;
        request.cancelled = command.cancelled;
        command.outline = std::move(request);
        command.outlineResources = std::move(resources);
        mAnalysisQueue.append(std::move(command));
    }
    schedulePendingParseCommands();
    return true;
}

QThread *CppParser::makeOutlineWorker(const PCppParser &owner, const AnalysisCommand &command)
{
    auto result = std::make_shared<Analysis::OutlineJobResult>();
    auto *worker = QThread::create([owner, command, result] { *result = owner->runOutlineCommand(command); });
    worker->setObjectName(QStringLiteral("Outline worker"));
    connect(worker, &QThread::finished, this, [owner, worker, commandId = command.id, result] {
        bool deliver = false;
        {
            QMutexLocker locker(&owner->mMutex);
            if (!owner->mSchedulingStopped && owner->mExpectedOutline && *owner->mExpectedOutline == result->demand) {
                owner->mExpectedOutline.reset();
                deliver = true;
            }
        }
        owner->finishAnalysisWorker(commandId, worker);
        if (deliver) emit owner->outlineFinished(*result);
    }, Qt::QueuedConnection);
    return worker;
}

Analysis::OutlineJobResult CppParser::runOutlineCommand(const AnalysisCommand &command)
{
    using namespace Analysis;
    const auto &request = command.outline;
    OutlineJobResult unavailable{request.demand, request.stamp, request.documentId, request.documentRevision};
    if (command.cancelled->load(std::memory_order_acquire)) {
        unavailable.status = OutlineJobStatus::Cancelled;
        return unavailable;
    }
    const auto report = sourceJobReport();
    if (!report || !report->complete()) return unavailable;
    OutlineCertifiedInput inputs{{report->configurationTicket, report->configurationDigest,
        report->inputGeneration, report->workingDirectory}, report->job, {}, true};
    if (!(inputs.stamp == request.stamp) || report->job != request.sourceJob) {
        unavailable.status = OutlineJobStatus::Obsolete;
        return unavailable;
    }
    const auto &resources = *command.outlineResources;
    try {
        auto reservation = resources.ledger.tryReserve();
        if (!reservation) {
            unavailable.status = OutlineJobStatus::ResourceLimit;
            return unavailable;
        }
        auto scratch = reservation.resource(SemanticMemoryKind::Scratch);
        std::pmr::vector<EngineSemanticFileView> views(scratch.get());
        const auto appendFile = [&](const QString &logical, const QString &physical) {
            if (command.cancelled->load(std::memory_order_acquire)) return false;
            if (!report->reads.contains(exactPathSpelling(physical))) return false;
            const auto file = mPreprocessor.findFileInfo(logical);
            if (!file) return false;
            inputs.files.append(logical);
            views.push_back({QStringView(inputs.files.back()), &file->statements()});
            return true;
        };
        if (request.wholeProject) {
            if (mProjectFiles.size() > 8192) {
                unavailable.status = OutlineJobStatus::ResourceLimit;
                return unavailable;
            }
            inputs.files.reserve(mProjectFiles.size());
            views.reserve(mProjectFiles.size());
            for (const auto &logical : mProjectFiles) {
                if (!isCFile(logical) && !isHFile(logical)) continue;
                const QString physical = QDir(report->workingDirectory).absoluteFilePath(logical);
                if (!appendFile(logical, physical)) {
                    unavailable.status = command.cancelled->load(std::memory_order_acquire)
                        ? OutlineJobStatus::Cancelled : OutlineJobStatus::IncompleteProjectRoots;
                    return unavailable;
                }
            }
        } else {
            const auto source = report->reads.constFind(exactPathSpelling(request.primaryPath));
            if (source == report->reads.cend() ||
                    (request.documentId && (source->documentId != request.documentId || source->revision != request.documentRevision)))
                return unavailable;
            if (!appendFile(request.primaryFile, request.primaryPath)) return unavailable;
        }
        EngineSemanticGraphSource adapter(views.data(), views.size(), scratch);
        return buildOutline(adapter, inputs, request, mOutlineIdentities, resources.ledger, resources.retirement);
    } catch (const std::bad_alloc &) {
        unavailable.status = OutlineJobStatus::ResourceLimit;
    } catch (...) {
        unavailable.status = OutlineJobStatus::Unavailable;
    }
    return unavailable;
}
