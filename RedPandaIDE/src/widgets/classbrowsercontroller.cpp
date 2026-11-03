// SPDX-License-Identifier: GPL-3.0-or-later
#include "classbrowsercontroller.h"

#include <QMetaObject>
#include <QThread>
#include <limits>

namespace {
bool sameOptions(const Analysis::OutlineOptions &a, const Analysis::OutlineOptions &b)
{
    return a.alphabetical == b.alphabetical && a.groupByKind == b.groupByKind &&
        a.showInherited == b.showInherited && a.hideSingleUnderscore == b.hideSingleUnderscore &&
        a.hideDoubleUnderscore == b.hideDoubleUnderscore;
}
}

ClassBrowserController::ClassBrowserController(std::shared_ptr<const Analysis::OutlineResources> resources,
                                               CurrentSource currentSource, QObject *parent)
    : QObject(parent), mResources(std::move(resources)), mCurrentSource(std::move(currentSource))
{
}

void ClassBrowserController::setContext(Context context)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (mStopped) return;
    const bool parserChanged = mContext.parser != context.parser;
    const bool differentTree = parserChanged || mContext.workspace != context.workspace ||
        mContext.wholeProject != context.wholeProject || (!context.wholeProject && mContext.source.document != context.source.document) ||
        (!context.wholeProject && mContext.source.logical != context.source.logical);
    const bool changed = differentTree || !sameOptions(mContext.options, context.options) ||
        mContext.source.logical != context.source.logical || mContext.source.physical != context.source.physical ||
        mContext.contextFile != context.contextFile || mContext.source.revision != context.source.revision || mContext.source.generation != context.source.generation;
    if (!changed) return;
    if (mContext.parser) mContext.parser->cancelOutline();
    mContext = std::move(context);
    mRequest = {};
    mWaitingForSource.reset();
    mAttemptedProjectScan.reset();
    if (differentTree) mModel.expect({}, true);
    if (parserChanged) connectParser();
    changeState(mContext.parser ? State::Reading : State::Empty);
    refresh();
}

void ClassBrowserController::connectParser()
{
    for (const auto &connection : std::as_const(mConnections)) disconnect(connection);
    mConnections.clear();
    if (!mContext.parser) return;
    const QPointer<CppParser> current(mContext.parser.get());
    const auto reevaluate = [this, current] {
        if (current && current == mContext.parser.get()) refresh();
    };
    const auto reading = [this, current] {
        if (current && current == mContext.parser.get()) changeState(State::Reading);
    };
    mConnections.append(connect(current.data(), &CppParser::parseStarted, this, reading, Qt::QueuedConnection));
    mConnections.append(connect(current.data(), &CppParser::cacheResetStarted, this, reading, Qt::QueuedConnection));
    mConnections.append(connect(current.data(), &CppParser::cacheResetFinished, this, reevaluate, Qt::QueuedConnection));
    mConnections.append(connect(current.data(), &CppParser::parseFinished, this, reevaluate, Qt::QueuedConnection));
    mConnections.append(connect(current.data(), &CppParser::configurationStateChanged, this, reevaluate, Qt::QueuedConnection));
    mConnections.append(connect(current.data(), &CppParser::outlineFinished, this, [this, current](const Analysis::OutlineJobResult &result) {
        if (current && current == mContext.parser.get()) receive(result);
    }));
}

void ClassBrowserController::refresh()
{
    if (mStopped || mUpdateQueued) return;
    mUpdateQueued = true;
    QMetaObject::invokeMethod(this, [this] {
        mUpdateQueued = false;
        if (!mStopped) update();
    }, Qt::QueuedConnection);
}

void ClassBrowserController::update()
{
    using State = CppParser::ConfigurationState;
    const auto parser = mContext.parser;
    if (!parser || (!mContext.wholeProject && mContext.source.logical.isEmpty())) {
        changeState(ClassBrowserController::State::Empty);
        return;
    }
    const auto state = parser->configurationState();
    if (state == State::Failed || state == State::Retired) {
        changeState(ClassBrowserController::State::Unavailable);
        return;
    }
    if (state != State::Ready) {
        changeState(ClassBrowserController::State::Reading);
        return;
    }
    const auto current = mCurrentSource();
    if ((!mContext.wholeProject && (current.document != mContext.source.document || current.logical != mContext.source.logical)) ||
            (current.document && !current.revision)) {
        changeState(ClassBrowserController::State::Reading);
        return;
    }
    const auto report = parser->sourceJobReport();
    Analysis::OutlineStamp requested{parser->appliedConfigurationTicket(), parser->appliedConfigurationDigest(),
                                    current.generation, report ? report->workingDirectory : QString{}};
    bool rootCertified = mContext.wholeProject;
    if (report && !mContext.wholeProject) {
        const auto found = report->reads.constFind(exactPathSpelling(current.physical));
        rootCertified = found != report->reads.cend() &&
            (!current.document || (found->documentId == current.document && found->revision == current.revision));
    }
    if (!report || !rootCertified || report->configurationTicket != requested.configurationTicket || report->inputGeneration != current.generation ||
            report->configurationDigest != requested.configurationDigest) {
        changeState(ClassBrowserController::State::Reading);
        if (!mWaitingForSource || !(*mWaitingForSource == requested)) {
            mWaitingForSource = requested;
            mSourceJobAtRequest = report ? report->job : 0;
            const auto status = mContext.wholeProject
                ? CppParser::parseFileListNonBlocking(parser)
                : CppParser::parseFileNonBlocking(parser, mContext.source.logical, mContext.workspace != 0, mContext.contextFile, true);
            if (status != CppParser::RequestResult::Accepted)
                changeState(ClassBrowserController::State::Unavailable);
        } else if (report && report->job != mSourceJobAtRequest && !report->complete()) {
            changeState(ClassBrowserController::State::Unavailable);
        }
        return;
    }
    if (!report->complete()) {
        changeState(ClassBrowserController::State::Unavailable);
        return;
    }
    mWaitingForSource.reset();
    if (mRequest.demand.valid() && mRequest.sourceJob == report->job &&
            mRequest.stamp == requested && mRequest.documentRevision == current.revision)
        return;
    if (mSequence == std::numeric_limits<quint64>::max()) {
        changeState(ClassBrowserController::State::Unavailable);
        return;
    }
    mRequest = {};
    mRequest.demand = {parser->outlineService(), ++mSequence, mContext.workspace};
    mRequest.stamp = std::move(requested);
    mRequest.sourceJob = report->job;
    mRequest.documentId = current.document;
    mRequest.documentRevision = current.revision;
    mRequest.primaryFile = mContext.source.logical;
    mRequest.primaryPath = mContext.source.physical;
    mRequest.wholeProject = mContext.wholeProject;
    mRequest.options = mContext.options;
    if (mModel.expect(mRequest.demand, false) != OutlineApply::Applied || !parser->submitOutline(mRequest, mResources))
        changeState(ClassBrowserController::State::Unavailable);
    else
        changeState(ClassBrowserController::State::Reading);
}

void ClassBrowserController::receive(const Analysis::OutlineJobResult &result)
{
    using Status = Analysis::OutlineJobStatus;
    if (mStopped || !result.matches(mRequest) || !mContext.parser ||
            mContext.parser->configurationTicket() != result.stamp.configurationTicket ||
            mContext.parser->appliedConfigurationDigest() != result.stamp.configurationDigest)
        return;
    const auto current = mCurrentSource();
    if (current.generation != result.stamp.inputGeneration || current.document != result.documentId ||
            current.revision != result.documentRevision) {
        mRequest = {};
        refresh();
        return;
    }
    const auto report = mContext.parser->sourceJobReport();
    if (result.status == Status::Obsolete || (report && report->job != mRequest.sourceJob)) {
        mRequest = {};
        refresh();
        return;
    }
    if (result.status == Status::Ready && mModel.apply(result.demand, result.publication) == OutlineApply::Applied) {
        changeState(State::Ready);
    } else if (result.status == Status::IncompleteProjectRoots) {
        if (!mAttemptedProjectScan || !(*mAttemptedProjectScan == result.stamp)) {
            mAttemptedProjectScan = result.stamp;
            if (CppParser::parseFileListNonBlocking(mContext.parser) == CppParser::RequestResult::Accepted) {
                changeState(State::Reading);
                return;
            }
        }
        changeState(State::Partial);
    } else if (result.status == Status::Partial) {
        changeState(State::Partial);
    } else if (result.status == Status::ResourceLimit || result.status == Status::TimedOut) {
        changeState(State::ResourceLimit);
    } else {
        changeState(State::Unavailable);
    }
}

void ClassBrowserController::changeState(State state)
{
    if (mState == state) return;
    mState = state;
    emit stateChanged(state);
}

void ClassBrowserController::stop()
{
    if (mStopped) return;
    mStopped = true;
    if (mContext.parser) mContext.parser->cancelOutline();
    mContext = {};
    connectParser();
    mModel.expect({}, true);
    changeState(State::Empty);
}
