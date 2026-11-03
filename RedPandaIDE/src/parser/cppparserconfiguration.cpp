// SPDX-License-Identifier: GPL-3.0-or-later
#include "cppparser.h"
#include "../analysis/parsertoolchainprobe.h"
#include <QCoreApplication>
#include <QMetaObject>
#include <limits>
#include <exception>
#include <algorithm>
#include <utility>
#include <qt_utils/utils.h>

namespace {
QString configurationMessage(const char *text) {
    return QCoreApplication::translate("ParserConfiguration", text);
}
bool configurationCommand(CppParser::ConfigurationState state) {
    return state == CppParser::ConfigurationState::Preparing
        || state == CppParser::ConfigurationState::QueuedForApply
        || state == CppParser::ConfigurationState::Applying;
}
std::shared_ptr<const Analysis::ParserToolchainProbeRequest> updatedProbe(
    const Analysis::ParserToolchainProbeRequest &source, Analysis::ParserConfigurationValues base)
{
    Analysis::ParserToolchainProbeValues values;
    values.base = std::move(base); values.configured = source.configured; values.kind = source.kind;
    values.cIncludes = source.cIncludes; values.cppIncludes = source.cppIncludes; values.hardDefines = source.hardDefines;
    values.outputEncodings = source.outputEncodings; values.maximumTotalMilliseconds = source.maximumTotalMilliseconds;
    return Analysis::ParserToolchainProbeRequest::create(std::move(values));
}
}

class CppConfigurationThread final : public QThread {
public:
    CppConfigurationThread(PCppParser parser, CppParser::AnalysisCommand command):
        mParser(std::move(parser)), mCommand(std::move(command)) {}
    Analysis::PParserConfiguration result;
    QString error;
    bool cancelled = false;
protected:
    void run() override {
        try { mParser->runConfigurationCommand(mCommand, result, error, cancelled); }
        catch (const std::exception &exception) {
            result.reset(); error = QString::fromUtf8(exception.what());
        }
    }
private:
    const PCppParser mParser;
    const CppParser::AnalysisCommand mCommand;
};

bool CppParser::onControlOwner() const { return QThread::currentThread() == thread(); }

quint64 CppParser::configurationTicket() const {
    QMutexLocker locker(&mMutex); return mRequestedConfigurationTicket;
}
quint64 CppParser::appliedConfigurationTicket() const {
    QMutexLocker locker(&mMutex); return mAppliedConfigurationTicket;
}
QByteArray CppParser::appliedConfigurationDigest() const {
    QMutexLocker locker(&mMutex); return mAppliedConfigurationDigest;
}
CppParser::ConfigurationState CppParser::configurationState() const {
    QMutexLocker locker(&mMutex); return mConfigurationState;
}
QString CppParser::configurationError() const {
    QMutexLocker locker(&mMutex); return mConfigurationError;
}
Analysis::ParserConfigurationValues CppParser::requestedConfigurationValues() const {
    if (!onControlOwner()) return {};
    QMutexLocker locker(&mMutex); return mRequestedConfigurationValues;
}
Analysis::PParserConfiguration CppParser::appliedConfiguration() const {
    QMutexLocker locker(&mMutex); return mAppliedConfiguration;
}
int CppParser::trackedAnalysisWorkerCount() const {
    if (!onControlOwner()) return -1;
    QMutexLocker locker(&mMutex); return mWriterReserved ? 1 : 0;
}

quint64 CppParser::beginConfigurationPreparation()
{
    if (!onControlOwner() || weak_from_this().expired()) return 0;
    QList<AnalysisCommand> discarded;
    quint64 oldTicket = 0, ticket = 0;
    bool superseded = false;
    {
        QMutexLocker locker(&mMutex);
        if (mSchedulingStopped || mRequestedConfigurationTicket == std::numeric_limits<quint64>::max()) return 0;
        mAsyncOwner = weak_from_this();
        oldTicket = mRequestedConfigurationTicket;
        superseded = configurationCommand(mConfigurationState) || !mAnalysisQueue.isEmpty();
        mResetAfterConfiguration |= queuedReset() || activeReset() ||
            (mActiveAnalysisCommand && mActiveAnalysisCommand->kind == AnalysisCommandKind::InvalidateFiles) ||
            std::any_of(mAnalysisQueue.cbegin(), mAnalysisQueue.cend(), [](const auto &command) {
                return command.kind == AnalysisCommandKind::InvalidateFiles;
            });
        ticket = ++mRequestedConfigurationTicket;
        discarded.swap(mAnalysisQueue);
        for (auto &command : discarded) command.cancelled->store(true, std::memory_order_release);
        mExpectedOutline.reset();
        if (mActiveAnalysisCommand && (mActiveAnalysisCommand->kind == AnalysisCommandKind::BuildOutline
                || mActiveAnalysisCommand->kind == AnalysisCommandKind::PrepareConfiguration
                || mActiveAnalysisCommand->kind == AnalysisCommandKind::ApplyConfiguration))
            mActiveAnalysisCommand->cancelled->store(true, std::memory_order_release);
        mRequestedBuilderDirty = false;
        mConfigurationSealScheduled = false;
        mConfigurationState = ConfigurationState::Preparing;
        mConfigurationError.clear();
    }
    if (superseded) emit analysisRequestSuperseded(oldTicket);
    emit configurationStateChanged(ticket, ConfigurationState::Preparing);
    return ticket;
}

bool CppParser::prepareConfigurationAsync(quint64 ticket,
    std::shared_ptr<const Analysis::ParserToolchainProbeRequest> request)
{
    if (!onControlOwner() || !request) return false;
    AnalysisCommand command;
    {
        QMutexLocker locker(&mMutex);
        if (mSchedulingStopped || !ticket || ticket != mRequestedConfigurationTicket
                || mNextAnalysisRequest == std::numeric_limits<quint64>::max()) return false;
        mAsyncOwner = weak_from_this();
        if (mAsyncOwner.expired()) return false;
        mRequestedConfigurationValues = request->base->values();
        mRequestedProbeTemplate = request;
        mRequestedBuilderDirty = false;
        command.kind = AnalysisCommandKind::PrepareConfiguration;
        command.id = ++mNextAnalysisRequest; command.ticket = ticket; command.probe = std::move(request);
        mAnalysisQueue.append(std::move(command));
        mConfigurationState = ConfigurationState::Preparing;
    }
    schedulePendingParseCommands();
    return true;
}

bool CppParser::completeConfigurationPreparation(quint64 ticket, Analysis::PParserConfiguration configuration)
{
    if (!onControlOwner() || !configuration) return false;
    AnalysisCommand command;
    {
        QMutexLocker locker(&mMutex);
        if (mSchedulingStopped || !ticket || ticket != mRequestedConfigurationTicket
                || mNextAnalysisRequest == std::numeric_limits<quint64>::max()) return false;
        command.kind = AnalysisCommandKind::ApplyConfiguration;
        command.id = ++mNextAnalysisRequest; command.ticket = ticket; command.configuration = std::move(configuration);
        mRequestedConfigurationValues = command.configuration->values();
        mRequestedProbeTemplate.reset();
        mRequestedBuilderDirty = false;
        // A resolved value must precede any source work accepted while this
        // ticket was preparing. It never replaces an already active writer.
        mAnalysisQueue.prepend(std::move(command));
        mConfigurationState = ConfigurationState::QueuedForApply;
        mConfigurationError.clear();
    }
    emit configurationStateChanged(ticket, ConfigurationState::QueuedForApply);
    bool carryReset = false;
    {
        QMutexLocker locker(&mMutex);
        if (!mSchedulingStopped && ticket == mRequestedConfigurationTicket)
            carryReset = std::exchange(mResetAfterConfiguration, false);
    }
    if (carryReset) resetParsedResultsNonBlocking(shared_from_this());
    schedulePendingParseCommands();
    return true;
}

bool CppParser::failConfigurationPreparation(quint64 ticket, const QString &reason)
{
    if (!onControlOwner()) return false;
    QList<AnalysisCommand> discarded;
    {
        QMutexLocker locker(&mMutex);
        if (!ticket || ticket != mRequestedConfigurationTicket || mSchedulingStopped) return false;
        discarded.swap(mAnalysisQueue);
        for (auto &command : discarded) command.cancelled->store(true, std::memory_order_release);
        mConfigurationState = ConfigurationState::Failed;
        mConfigurationError = reason;
        mRequestedBuilderDirty = false;
    }
    emit configurationStateChanged(ticket, ConfigurationState::Failed);
    emit configurationFailed(ticket, reason);
    if (configurationTicket() == ticket && configurationState() == ConfigurationState::Failed)
        emit inputSnapshotUnavailable(reason);
    return true;
}

quint64 CppParser::submitConfiguration(Analysis::PParserConfiguration configuration)
{
    if (!configuration) return 0;
    const auto ticket = beginConfigurationPreparation();
    return ticket && completeConfigurationPreparation(ticket, std::move(configuration)) ? ticket : 0;
}

void CppParser::changedRequestedConfiguration(const std::function<void(Analysis::ParserConfigurationValues &)> &change)
{
    if (!onControlOwner()) {
        const auto weak = weak_from_this();
        QMetaObject::invokeMethod(this, [weak, change] { if (const auto owner = weak.lock()) owner->changedRequestedConfiguration(change); }, Qt::QueuedConnection);
        return;
    }
    std::shared_ptr<const Analysis::ParserToolchainProbeRequest> pendingProbe;
    bool begin = false;
    {
        QMutexLocker locker(&mMutex);
        if (mSchedulingStopped) return;
        begin = !mRequestedBuilderDirty;
        pendingProbe = mRequestedProbeTemplate;
    }
    if (begin && !beginConfigurationPreparation()) return;
    bool post = false;
    quint64 ticket = 0;
    {
        QMutexLocker locker(&mMutex);
        // This private callback is a pure value delta from the legacy adapters;
        // it never calls GUI, QObject, IO or engine code.
        change(mRequestedConfigurationValues);
        mRequestedProbeTemplate = std::move(pendingProbe);
        mRequestedBuilderDirty = true;
        mConfigurationState = ConfigurationState::QueuedForApply;
        ticket = mRequestedConfigurationTicket;
        post = !mConfigurationSealScheduled;
        mConfigurationSealScheduled = true;
    }
    emit configurationStateChanged(ticket, ConfigurationState::QueuedForApply);
    if (post) {
        const auto weak = weak_from_this();
        QMetaObject::invokeMethod(this, [weak] { if (const auto owner = weak.lock()) owner->sealRequestedConfiguration(); }, Qt::QueuedConnection);
    }
}

void CppParser::sealRequestedConfiguration()
{
    if (!onControlOwner()) return;
    Analysis::ParserConfigurationValues values;
    std::shared_ptr<const Analysis::ParserToolchainProbeRequest> probe;
    quint64 ticket = 0;
    {
        QMutexLocker locker(&mMutex);
        mConfigurationSealScheduled = false;
        if (mSchedulingStopped || !mRequestedBuilderDirty) return;
        values = mRequestedConfigurationValues; probe = mRequestedProbeTemplate;
        ticket = mRequestedConfigurationTicket; mRequestedBuilderDirty = false;
    }
    if (probe) prepareConfigurationAsync(ticket, updatedProbe(*probe, std::move(values)));
    else completeConfigurationPreparation(ticket, Analysis::ParserConfiguration::create(std::move(values)));
}

void CppParser::stopScheduling()
{
    if (!onControlOwner()) return;
    QList<AnalysisCommand> discarded;
    quint64 ticket = 0;
    {
        QMutexLocker locker(&mMutex);
        if (mSchedulingStopped) return;
        mSchedulingStopped = true; mConfigurationState = ConfigurationState::Retired;
        ticket = mRequestedConfigurationTicket;
        discarded.swap(mAnalysisQueue);
        mRequestedBuilderDirty = false; mConfigurationSealScheduled = false; mResetAfterConfiguration = false;
        if (mActiveAnalysisCommand) mActiveAnalysisCommand->cancelled->store(true, std::memory_order_release);
        mStopForReset.store(true, std::memory_order_release);
    }
    mPreprocessor.stopForParserReset(); mTokenizer.stopForParserReset();
    emit configurationStateChanged(ticket, ConfigurationState::Retired);
    if (analysisQuiesced()) emit analysisStopped();
}

bool CppParser::analysisQuiesced() const
{
    QMutexLocker locker(&mMutex);
    return mSchedulingStopped && !mWriterReserved && !mParsing && !mSyncParseReserved;
}

bool CppParser::resumeScheduling()
{
    if (!onControlOwner()) return false;
    quint64 ticket = 0;
    {
        QMutexLocker locker(&mMutex);
        if (mWriterReserved || mParsing || mSyncParseReserved) return false;
        mSchedulingStopped = false; mStopForReset.store(false, std::memory_order_release);
        mConfigurationState = mAppliedConfigurationTicket == mRequestedConfigurationTicket
            ? ConfigurationState::Ready : ConfigurationState::Failed;
        ticket = mRequestedConfigurationTicket;
    }
    emit configurationStateChanged(ticket, configurationState());
    schedulePendingParseCommands();
    return true;
}

void CppParser::clearEngineConfiguration()
{
    mUniqId = 0; mIsSystemHeader = false; mIsHeader = false; mIsProjectFile = false;
    mFilesScannedCount = 0; mFilesToScanCount = 0;
    internalClear(); mCurrentScope.clear(); mMemberAccessibilities.clear();
    mStatementList.clear(); mProjectFiles.clear(); mFilesToScan.clear();
    mInlineNamespaceEndSkips.clear(); mNamespaces.clear(); mInlineNamespaces.clear(); mClassInheritances.clear();
    mPreprocessor.clear(); mTokenizer.clear();
    mSourceCacheCertified = false; mSourceRoots.clear();
    { QMutexLocker locker(&mMutex); mSourceJobReport.reset(); }
}

bool CppParser::applyConfigurationValue(quint64 ticket,
    const Analysis::PParserConfiguration &configuration, QString &error)
{
    if (!configuration) { error = configurationMessage("The analysis configuration is unavailable."); return false; }
    error = configuration->validate();
    if (!error.isEmpty()) return false;
    const auto digest = configuration->digest();
    emit configurationApplying(ticket);
    bool changed = false;
    {
        QMutexLocker locker(&mMutex);
        if (!mWriterReserved || mSchedulingStopped
                || (mActiveAnalysisCommand && mActiveAnalysisCommand->cancelled->load(std::memory_order_acquire))) return false;
        changed = mAppliedConfigurationDigest != digest;
        mParsing = true;
        if (changed) {
            mAppliedConfiguration.reset();
            mAppliedConfigurationDigest.clear();
            mLanguage = configuration->language;
            mEnabled = configuration->enabled;
        }
    }
    const auto releaseParsing = finally([this] { QMutexLocker locker(&mMutex); mParsing = false; });
    // Once engine mutation begins, finish this value coherently even if a
    // newer intent arrives. Supersession filters authority, not a fake rollback.
    if (changed) {
        clearEngineConfiguration();
        mCppKeywords = CppKeywords; mCppTypeKeywords = CppTypeKeywords;
#ifdef ENABLE_SDCC
        if (mLanguage == ParserLanguage::SDCC) { mCppKeywords.insert(SDCCKeywords); mCppTypeKeywords.unite(SDCCTypeKeywords); }
#endif
        mPreprocessor.setFileOnlyIncludeOnce(configuration->includeOnce);
        for (const auto &path : configuration->includePaths) mPreprocessor.addIncludePath(path);
        for (const auto &path : configuration->projectIncludePaths) mPreprocessor.addProjectIncludePath(path);
        for (const auto &line : configuration->hardDefineLines) mPreprocessor.addHardDefineByLine(line.trimmed());
        mProjectFiles = configuration->projectFiles; mFilesToScan = configuration->projectFiles;
        const bool previousHeader = mIsSystemHeader; mIsSystemHeader = true;
        parseHardDefinesInternal(); mIsSystemHeader = previousHeader;
    }
    {
        QMutexLocker locker(&mMutex);
        mAppliedConfiguration = configuration; mAppliedConfigurationDigest = digest; mAppliedConfigurationTicket = ticket;
        if (changed) ++mSourceConfigGeneration;
    }
    return true;
}

void CppParser::runConfigurationCommand(const AnalysisCommand &command, Analysis::PParserConfiguration &result,
    QString &error, bool &cancelled)
{
    if (command.cancelled->load(std::memory_order_acquire)) { cancelled = true; return; }
    if (command.kind == AnalysisCommandKind::PrepareConfiguration) {
        const auto discovered = Analysis::discoverParserToolchain(*command.probe, *command.cancelled);
        cancelled = discovered.preparation.status == Compilation::PreparationStatus::Cancelled;
        if (!discovered.preparation.ready()) { error = discovered.preparation.reason; return; }
        result = discovered.configuration;
        return;
    }
    if (applyConfigurationValue(command.ticket, command.configuration, error)) result = command.configuration;
    else cancelled = command.cancelled->load(std::memory_order_acquire);
}

bool CppParser::queuedReset() const {
    return std::any_of(mAnalysisQueue.cbegin(), mAnalysisQueue.cend(), [this](const auto &command) {
        return command.kind == AnalysisCommandKind::ResetParsedResults && command.ticket == mRequestedConfigurationTicket;
    });
}
bool CppParser::activeReset() const {
    return mActiveAnalysisCommand && mActiveAnalysisCommand->kind == AnalysisCommandKind::ResetParsedResults
        && mActiveAnalysisCommand->ticket == mRequestedConfigurationTicket;
}

void CppParser::queueParseFileCommand(const ParseFileCommand &source)
{
    auto command = source;
    if (!command.configurationTicket) command.configurationTicket = mRequestedConfigurationTicket;
    for (auto &pending : mAnalysisQueue) {
        if (pending.kind == AnalysisCommandKind::ParseFile && pending.ticket == command.configurationTicket
                && pending.file.fileName == command.fileName && pending.file.inProject == command.inProject) {
            const bool updateView = pending.file.updateView || command.updateView;
            const bool onlyIfNotParsed = pending.file.onlyIfNotParsed && command.onlyIfNotParsed;
            pending.file = command; pending.file.updateView = updateView; pending.file.onlyIfNotParsed = onlyIfNotParsed;
            return;
        }
    }
    AnalysisCommand item; item.kind = AnalysisCommandKind::ParseFile;
    item.id = ++mNextAnalysisRequest; item.ticket = command.configurationTicket; item.file = std::move(command);
    item.file.requestId = item.id;
    mAnalysisQueue.append(std::move(item));
}

void CppParser::queueOlderParseFileCommand(const ParseFileCommand &source)
{
    for (auto &pending : mAnalysisQueue) {
        if (pending.kind == AnalysisCommandKind::ParseFile && pending.ticket == source.configurationTicket
                && pending.file.fileName == source.fileName && pending.file.inProject == source.inProject) {
            pending.file.updateView |= source.updateView; pending.file.onlyIfNotParsed &= source.onlyIfNotParsed; return;
        }
    }
    queueParseFileCommand(source);
}

void CppParser::queueFileListCommand(FileListCommand command, quint64 ticket, bool older)
{
    for (auto &pending : mAnalysisQueue) {
        if (pending.kind == AnalysisCommandKind::ParseFileList && pending.ticket == ticket) {
            pending.files.updateView |= command.updateView;
            pending.files.roots.unite(command.roots);
            if (!older) { pending.files.inputs = std::move(command.inputs); pending.files.readObserver = std::move(command.readObserver); pending.files.sourceRequest = std::move(command.sourceRequest); }
            return;
        }
    }
    AnalysisCommand item; item.kind = AnalysisCommandKind::ParseFileList;
    item.id = ++mNextAnalysisRequest; item.ticket = ticket; item.files = std::move(command);
    mAnalysisQueue.append(std::move(item));
}

void CppParser::schedulePendingParseCommands()
{
    std::weak_ptr<CppParser> weak;
    {
        QMutexLocker locker(&mMutex);
        if (mSchedulingStopped || mParsing || mLockCount > 0 || mWriterReserved || mSyncParseReserved
                || mPendingDispatchScheduled || mAnalysisQueue.isEmpty()) return;
        weak = mAsyncOwner;
        if (weak.expired()) weak = weak_from_this();
        if (weak.expired()) return;
        mAsyncOwner = weak; mPendingDispatchScheduled = true;
    }
    QMetaObject::invokeMethod(this, [weak] { if (const auto parser = weak.lock()) parser->startPendingParseCommands(); }, Qt::QueuedConnection);
}

void CppParser::startPendingParseCommands()
{
    if (!onControlOwner()) return;
    PCppParser owner;
    AnalysisCommand command;
    bool applying = false;
    {
        QMutexLocker locker(&mMutex);
        mPendingDispatchScheduled = false;
        if (mSchedulingStopped || mParsing || mLockCount > 0 || mWriterReserved || mSyncParseReserved || mAnalysisQueue.isEmpty()) return;
        owner = mAsyncOwner.lock(); if (!owner) return;
        const auto &next = mAnalysisQueue.first();
        if ((next.kind == AnalysisCommandKind::ParseFile || next.kind == AnalysisCommandKind::ParseFileList
                || next.kind == AnalysisCommandKind::ResetParsedResults || next.kind == AnalysisCommandKind::InvalidateFiles
                || next.kind == AnalysisCommandKind::BuildOutline)
                && next.ticket != mAppliedConfigurationTicket) return;
        command = mAnalysisQueue.takeFirst();
        mActiveAnalysisCommand = command; mWriterReserved = true;
        applying = command.kind == AnalysisCommandKind::ApplyConfiguration && command.ticket == mRequestedConfigurationTicket;
        if (applying) mConfigurationState = ConfigurationState::Applying;
    }
    if (applying) emit configurationStateChanged(command.ticket, ConfigurationState::Applying);
    QThread *worker = nullptr;
    if (command.kind == AnalysisCommandKind::PrepareConfiguration || command.kind == AnalysisCommandKind::ApplyConfiguration) {
        auto *configuration = new CppConfigurationThread(owner, command);
        worker = configuration;
        connect(worker, &QThread::finished, this, [owner, configuration, id = command.id] {
            owner->finishAnalysisWorker(id, configuration,
                configuration->result, configuration->error, configuration->cancelled);
        }, Qt::QueuedConnection);
    } else if (command.kind == AnalysisCommandKind::BuildOutline) {
        worker = makeOutlineWorker(owner, command);
    } else {
        if (command.kind == AnalysisCommandKind::ResetParsedResults || command.kind == AnalysisCommandKind::InvalidateFiles)
            worker = new CppParsedResultsResetThread(owner, command.invalidatedFiles);
        else if (command.kind == AnalysisCommandKind::ParseFileList)
            worker = new CppFileListParserThread(owner, command.files.updateView, nullptr,
                command.files.inputs, command.files.readObserver, command.files.sourceRequest, command.ticket);
        else worker = new CppFileParserThread(owner, command.file.fileName, command.file.inProject,
            command.file.contextFilename, command.file.onlyIfNotParsed, command.file.updateView, nullptr,
            command.file.inputs, command.file.readObserver, command.file.sourceRequest, command.ticket);
        connect(worker, &QThread::finished, this, [owner, guarded = QPointer<QThread>(worker), id = command.id] {
            owner->finishAnalysisWorker(id, guarded);
        }, Qt::QueuedConnection);
    }
    { QMutexLocker locker(&mMutex); mActiveAnalysisWorker = worker; }
    worker->start();
    if (!worker->isRunning() && !worker->isFinished())
        finishAnalysisWorker(command.id, worker, {}, configurationMessage("The analysis worker could not start."));
}

void CppParser::finishAnalysisWorker(quint64 requestId, QThread *worker,
    Analysis::PParserConfiguration prepared, QString error, bool cancelled)
{
    if (!onControlOwner()) return;
    std::optional<AnalysisCommand> command;
    QByteArray appliedDigest;
    bool current = false;
    bool ready = false;
    {
        QMutexLocker locker(&mMutex);
        if (!mActiveAnalysisCommand || mActiveAnalysisCommand->id != requestId) return;
        command = std::move(mActiveAnalysisCommand);
        mActiveAnalysisCommand.reset();
        mActiveAnalysisWorker = nullptr;
        mWriterReserved = false;
        current = !mSchedulingStopped && command->ticket == mRequestedConfigurationTicket;
        ready = current && command->kind == AnalysisCommandKind::ApplyConfiguration && prepared && !cancelled;
        if (ready) {
            mConfigurationState = ConfigurationState::Ready;
            mConfigurationError.clear();
            appliedDigest = mAppliedConfigurationDigest;
        }
    }
    if (worker) worker->deleteLater();
    if (ready) {
        emit configurationStateChanged(command->ticket, ConfigurationState::Ready);
        if (configurationTicket() == command->ticket && configurationState() == ConfigurationState::Ready)
            emit configurationReady(command->ticket, appliedDigest);
    } else if (current && command->kind == AnalysisCommandKind::PrepareConfiguration) {
        if (prepared && !cancelled) completeConfigurationPreparation(command->ticket, std::move(prepared));
        else failConfigurationPreparation(command->ticket, error.isEmpty()
            ? configurationMessage("Analysis configuration preparation was cancelled.") : error);
    } else if (current && command->kind == AnalysisCommandKind::ApplyConfiguration) {
        failConfigurationPreparation(command->ticket, error.isEmpty()
            ? configurationMessage("The analysis configuration could not be applied.") : error);
    }
    if (analysisQuiesced()) emit analysisStopped();
    else schedulePendingParseCommands();
}
