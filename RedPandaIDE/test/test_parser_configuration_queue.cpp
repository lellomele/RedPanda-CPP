// SPDX-License-Identifier: GPL-3.0-or-later
#include "test_parser_configuration_queue.h"
#include "src/parser/cppparser.h"
#include "src/pathidentity.h"
#include "src/widgets/classbrowsercontroller.h"
#include "src/analysis/parsertoolchainprobe.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QPointer>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <functional>
#include <mutex>
#include <thread>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {
using State = CppParser::ConfigurationState;
using Request = CppParser::RequestResult;
bool spin(const std::function<bool()> &predicate, int milliseconds = 5000)
{
    QElapsedTimer elapsed; elapsed.start();
    do {
        if (predicate()) return true;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(1);
    } while (elapsed.elapsed() < milliseconds);
    return predicate();
}
struct GateState {
    QSemaphore entered, release;
    std::atomic_bool once{false};
    void holdOnce() { if (!once.exchange(true)) { entered.release(); release.acquire(); } }
};
struct Gate {
    std::shared_ptr<GateState> state = std::make_shared<GateState>();
    ~Gate() { state->release.release(16); }
    bool wait() const { return spin([&] { return state->entered.tryAcquire(); }); }
    void open() const { state->release.release(16); }
};
Analysis::PParserConfiguration config(QString macro, bool enabled = true)
{
    Analysis::ParserConfigurationValues values; values.enabled = enabled;
    values.hardDefineLines = {"#define CONFIG_" + macro + " 1"};
    return Analysis::ParserConfiguration::create(std::move(values));
}
struct Fixture {
    QTemporaryDir directory;
    PCppParser parser = std::make_shared<CppParser>();
    std::shared_ptr<DocumentStore> store = std::make_shared<DocumentStore>();
    std::shared_ptr<QHash<QString, QStringList>> buffers = std::make_shared<QHash<QString, QStringList>>();
    QString file;
    DocumentId document = 0;
    Fixture() {
        file = directory.filePath("source.cpp");
        (*buffers)[file] = {"#ifdef CONFIG_A", "int from_A;", "#else", "int from_other;", "#endif"};
        document = store->add(file, [data = buffers, name = file](QStringList &lines) {
            lines = data->value(name); return data->contains(name);
        });
        parser->setInputSnapshotProvider([data = store] { return data->capture(); });
    }
    ~Fixture() {
        if (!parser) return;
        parser->stopScheduling();
        spin([&] { return parser->trackedAnalysisWorkerCount() == 0 && !parser->hasPendingWork(); });
    }
    quint64 apply(Analysis::PParserConfiguration value) {
        const auto ticket = parser->submitConfiguration(std::move(value));
        if (!ticket || !spin([&] { return parser->configurationState() == State::Ready
                && parser->appliedConfigurationTicket() == ticket && parser->trackedAnalysisWorkerCount() == 0; })) return 0;
        return ticket;
    }
    bool idle() const { return spin([&] { return !parser->hasPendingWork() && parser->trackedAnalysisWorkerCount() == 0; }); }
    void change(QStringList lines) {
        store->beginChange(document); (*buffers)[file] = std::move(lines); store->commitChange(document);
    }
    Request parse(bool onlyIfNotParsed = false) const {
        return CppParser::parseFileNonBlocking(parser, file, false, {}, onlyIfNotParsed, true);
    }
};
struct CallbackCleanup {
    PCppParser parser;
    QObject *context;
    std::shared_ptr<GateState> gate;
    ~CallbackCleanup() {
        QObject::disconnect(parser.get(), nullptr, context, nullptr);
        gate->release.release(16);
        parser->stopScheduling();
        spin([&] { return parser->trackedAnalysisWorkerCount() == 0 && !parser->hasPendingWork(); });
    }
};
bool ticketSeen(const QSignalSpy &spy, quint64 ticket)
{
    for (const auto &arguments : spy) if (!arguments.isEmpty() && arguments[0].toULongLong() == ticket) return true;
    return false;
}
bool touch(const QString &path, const QByteArray &bytes = "ready")
{
    QFile file(path); return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
}
QByteArray contents(const QString &path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; }
Analysis::ParserToolchainProbeRequest::Pointer probeRequest(const QString &directory, bool hold = false,
                                                          bool child = false)
{
    Analysis::ParserToolchainProbeValues values; values.configured = true;
    values.base = config("BASE")->values(); values.maximumTotalMilliseconds = 10000;
    const auto command = [&](QString mode) {
        Compilation::ProcessProbeRequest result;
        result.executable = QCoreApplication::applicationFilePath(); result.workingDirectory = directory;
        result.arguments = {"--qa-parser-configuration-probe"};
        result.environment = QProcessEnvironment::systemEnvironment();
        result.environment.insert("QT_QPA_PLATFORM", "offscreen");
        result.environment.insert("REDPANDA_QUEUE_PROBE_MODE", mode);
        result.environment.insert("REDPANDA_QUEUE_PROBE_DIRECTORY", directory);
        result.environment.insert("REDPANDA_QUEUE_PROBE_HOLD", hold ? "1" : "0");
        result.environment.insert("REDPANDA_QUEUE_PROBE_CHILD", child ? "1" : "0");
        result.deadlineMilliseconds = 7000; result.maximumOutputBytes = 65536;
        return result;
    };
    values.cIncludes = command("includes"); values.cppIncludes = command("includes");
    values.hardDefines = command("defines");
    return Analysis::ParserToolchainProbeRequest::create(std::move(values));
}
} // namespace

bool TestParserConfigurationQueue::controlMutexAvailable(CppParser *parser)
{
    std::atomic_bool available{false};
    std::thread other([&] {
        if (parser->mMutex.tryLock(250)) { available = true; parser->mMutex.unlock(); }
    });
    other.join(); return available.load();
}

void TestParserConfigurationQueue::active_A_keeps_its_configuration_and_source_until_B_applies()
{
    Fixture f; QVERIFY(f.directory.isValid());
    const auto a = f.apply(config("A")); QVERIFY(a);
    Gate gate; f.parser->setSourceReadObserver([state = gate.state](const QString &) { state->holdOnce(); });
    const auto revisionA = f.store->revisionFor(f.document);
    CppParser::SourceJobReportPtr reportA;
    auto *parser = f.parser.get(); QObject callbacks;
    const auto connection = connect(parser, &CppParser::parseFinished, &callbacks, [&](int, int) {
        const auto report = parser->sourceJobReport();
        if (report && report->configurationTicket == a) reportA = report;
    }, Qt::DirectConnection);
    CallbackCleanup cleanup{f.parser, &callbacks, gate.state};
    QCOMPARE(f.parse(), Request::Accepted); QVERIFY(gate.wait());
    const auto b = parser->beginConfigurationPreparation(); QVERIFY(b > a);
    QCOMPARE(parser->appliedConfigurationTicket(), a); QVERIFY(parser->appliedConfiguration()->hardDefineLines.contains("#define CONFIG_A 1"));
    f.change({"#ifdef CONFIG_B", "int from_B;", "#endif"});
    const auto revisionB = f.store->revisionFor(f.document); QVERIFY(revisionB > revisionA);
    QCOMPARE(f.parse(), Request::Accepted); // B waits behind its preparation barrier.
    QVERIFY(parser->completeConfigurationPreparation(b, config("B")));
    QCOMPARE(parser->configurationState(), State::QueuedForApply); QCOMPARE(parser->appliedConfigurationTicket(), a);
    gate.open(); QVERIFY(f.idle()); disconnect(connection);
    QVERIFY(reportA); QVERIFY2(reportA->complete(), qPrintable(reportA->error));
    QCOMPARE(reportA->configurationTicket, a); QCOMPARE(reportA->reads.value(f.file).revision, revisionA);
    QCOMPARE(reportA->reads.value(f.file).origin, AnalysisInput::SourceOrigin::OpenDocument);
    QCOMPARE(parser->appliedConfigurationTicket(), b);
    const auto reportB = parser->sourceJobReport(); QVERIFY(reportB && reportB->complete());
    QCOMPARE(reportB->configurationTicket, b); QCOMPARE(reportB->reads.value(f.file).revision, revisionB);
    QVERIFY(parser->findStatement("from_B")); QVERIFY(!parser->findStatement("from_A"));
}

void TestParserConfigurationQueue::superseded_B_never_commits_after_C_is_requested()
{
    Fixture f; QVERIFY(f.apply(config("A")));
    Gate applyGate; std::atomic<quint64> blocked{0}; auto *parser = f.parser.get();
    QSignalSpy ready(parser, &CppParser::configurationReady); QObject callbacks;
    const auto b = parser->beginConfigurationPreparation(); QVERIFY(b);
    const auto connection = connect(parser, &CppParser::configurationApplying, &callbacks, [&, state = applyGate.state](quint64 ticket) {
        if (ticket == b) { blocked = ticket; state->holdOnce(); }
    }, Qt::DirectConnection);
    CallbackCleanup cleanup{f.parser, &callbacks, applyGate.state};
    QVERIFY(parser->completeConfigurationPreparation(b, config("B"))); QVERIFY(applyGate.wait());
    QCOMPARE(blocked.load(), b);
    const auto c = parser->beginConfigurationPreparation(); QVERIFY(c > b);
    QVERIFY(parser->completeConfigurationPreparation(c, config("C")));
    QVERIFY(!parser->completeConfigurationPreparation(b, config("B")));
    QVERIFY(!parser->failConfigurationPreparation(b, "late failure B"));
    applyGate.open(); QVERIFY(f.idle()); disconnect(connection);
    QCOMPARE(parser->configurationState(), State::Ready); QCOMPARE(parser->appliedConfigurationTicket(), c);
    QVERIFY(!ticketSeen(ready, b)); QVERIFY(ticketSeen(ready, c));
    QVERIFY(parser->appliedConfiguration()->hardDefineLines.contains("#define CONFIG_C 1"));
}

void TestParserConfigurationQueue::failed_preparation_rejects_fallback_and_equal_values_get_new_authority()
{
    Fixture f; const auto a = f.apply(config("A")); QVERIFY(a); auto *parser = f.parser.get();
    const auto digest = parser->appliedConfiguration()->digest();
    const auto b = parser->beginConfigurationPreparation(); QVERIFY(parser->failConfigurationPreparation(b, "injected B preparation failure"));
    QCOMPARE(parser->configurationState(), State::Failed); QCOMPARE(f.parse(), Request::ConfigurationUnavailable);
    QCOMPARE(parser->appliedConfigurationTicket(), a); QCOMPARE(parser->appliedConfiguration()->digest(), digest);
    QVERIFY(parser->configurationError().contains("injected B"));
    QSignalSpy ready(parser, &CppParser::configurationReady);
    const auto c = parser->submitConfiguration(config("A")); QVERIFY(c > b);
    QVERIFY(f.idle()); QCOMPARE(parser->configurationState(), State::Ready);
    QCOMPARE(parser->appliedConfigurationTicket(), c); QCOMPARE(parser->appliedConfiguration()->digest(), digest);
    QVERIFY(ticketSeen(ready, c)); QCOMPARE(f.parse(), Request::Accepted); QVERIFY(f.idle());
    const auto report = parser->sourceJobReport(); QVERIFY(report && report->complete());
    QCOMPARE(report->configurationTicket, c); QCOMPARE(report->configurationDigest, digest);
}

void TestParserConfigurationQueue::coalescing_preserves_latest_source_and_configuration_partition()
{
    Fixture f; const auto a = f.apply(config("A")); QVERIFY(a);
    Gate gate; auto *parser = f.parser.get();
    parser->setSourceReadObserver([state = gate.state](const QString &) { state->holdOnce(); });
    QCOMPARE(f.parse(), Request::Accepted); QVERIFY(gate.wait());
    f.change({"int revision_two;"}); QCOMPARE(f.parse(true), Request::Accepted);
    f.change({"int revision_three;"}); const auto revision = f.store->revisionFor(f.document);
    QCOMPARE(f.parse(false), Request::Accepted);
    {
        QMutexLocker lock(&parser->mMutex); int count = 0;
        for (const auto &queued : parser->mAnalysisQueue) if (queued.kind == CppParser::AnalysisCommandKind::ParseFile) {
            ++count; QCOMPARE(queued.ticket, a); QCOMPARE(queued.file.configurationTicket, a);
            QVERIFY(queued.file.inputs); QCOMPARE(queued.file.inputs->documents().value(f.document)->revision, revision);
            QVERIFY(!queued.file.onlyIfNotParsed); QVERIFY(queued.file.updateView);
        }
        QCOMPARE(count, 1);
    }
    const auto b = parser->beginConfigurationPreparation();
    f.change({"int ticket_B;"}); QCOMPARE(f.parse(), Request::Accepted);
    {
        QMutexLocker lock(&parser->mMutex);
        for (const auto &queued : parser->mAnalysisQueue) if (queued.kind == CppParser::AnalysisCommandKind::ParseFile) {
            QCOMPARE(queued.ticket, queued.file.configurationTicket);
            if (queued.ticket == b) QCOMPARE(queued.file.inputs->documents().value(f.document)->revision, f.store->revisionFor(f.document));
        }
    }
    QVERIFY(parser->completeConfigurationPreparation(b, config("B"))); gate.open(); QVERIFY(f.idle());
    const auto report = parser->sourceJobReport(); QVERIFY(report && report->complete()); QCOMPARE(report->configurationTicket, b);
    QVERIFY(parser->findStatement("ticket_B")); QVERIFY(!parser->findStatement("revision_two"));
}

void TestParserConfigurationQueue::direct_callbacks_are_outside_control_mutex_and_terminal_precedes_next_writer()
{
    Fixture f; const auto a = f.apply(config("A")); QVERIFY(a); auto *parser = f.parser.get();
    std::atomic_bool unlocked{true}, readyOnOwner{true}, reentered{false}; std::atomic<quint64> bTicket{0}, callbackTicket{0};
    std::mutex eventsMutex; QStringList events;
    const auto observe = [&](QString event) {
        if (!controlMutexAvailable(parser)) unlocked = false;
        std::lock_guard<std::mutex> lock(eventsMutex); events.append(std::move(event));
    };
    QObject callbacks;
    const auto c1 = connect(parser, &CppParser::configurationStateChanged, &callbacks, [&](quint64, State) { observe("state"); }, Qt::DirectConnection);
    const auto c2 = connect(parser, &CppParser::configurationApplying, &callbacks, [&](quint64 ticket) { observe(QString("apply:%1").arg(ticket)); }, Qt::DirectConnection);
    const auto c3 = connect(parser, &CppParser::configurationReady, &callbacks, [&](quint64 ticket, const QByteArray &) {
        observe("ready");
        if (ticket == bTicket.load()) {
            if (QThread::currentThread() != parser->thread()) readyOnOwner = false;
            else callbackTicket = parser->submitConfiguration(config("CALLBACK"));
        }
    }, Qt::DirectConnection);
    const auto c4 = connect(parser, &CppParser::onBusy, &callbacks, [&] { observe("busy"); }, Qt::DirectConnection);
    const auto c5 = connect(parser, &CppParser::parseStarted, &callbacks, [&] { observe("start"); }, Qt::DirectConnection);
    const auto c6 = connect(parser, &CppParser::progress, &callbacks, [&](const QString &, int, int) { observe("progress"); }, Qt::DirectConnection);
    const auto c7 = connect(parser, &CppParser::parseFinished, &callbacks, [&](int, int) {
        observe("terminal");
        if (!reentered.exchange(true)) {
            CppParser::resetParsedResultsNonBlocking(parser->shared_from_this());
            CppParser::parseFileNonBlocking(parser->shared_from_this(), f.file, false, {}, false, true);
        }
    }, Qt::DirectConnection);
    const auto c8 = connect(parser, &CppParser::configurationFailed, &callbacks, [&](quint64, const QString &) { observe("failed"); }, Qt::DirectConnection);
    Gate gate; parser->setSourceReadObserver([state = gate.state](const QString &) { state->holdOnce(); });
    CallbackCleanup cleanup{f.parser, &callbacks, gate.state};
    QCOMPARE(f.parse(), Request::Accepted); QVERIFY(gate.wait());
    const auto b = parser->submitConfiguration(config("B")); bTicket = b; QVERIFY(b > a);
    gate.open(); QVERIFY(f.idle());
    QVERIFY(readyOnOwner.load()); QVERIFY(callbackTicket.load() > b);
    const auto failed = parser->beginConfigurationPreparation(); QVERIFY(parser->failConfigurationPreparation(failed, "callback failure test"));
    for (auto c : {c1, c2, c3, c4, c5, c6, c7, c8}) disconnect(c);
    QVERIFY(unlocked.load());
    const int busy = events.indexOf("busy"), start = events.indexOf("start"), terminal = events.indexOf("terminal");
    QVERIFY(busy >= 0 && start > busy && terminal > start);
    const int progress = events.indexOf("progress"); if (progress >= 0) QVERIFY(progress > start && progress < terminal);
    QVERIFY(events.indexOf(QString("apply:%1").arg(b)) > terminal);
}

void TestParserConfigurationQueue::legacy_setters_seal_one_batch_before_immediate_parse()
{
    Fixture f; QVERIFY(f.apply(config("A"))); auto *parser = f.parser.get();
    QSignalSpy applied(parser, &CppParser::configurationApplying);
    for (int i = 0; i < 100; ++i) parser->addHardDefineByLine(QString("#define BATCH_%1 %1").arg(i));
    parser->addIncludePath(f.directory.filePath("includes")); parser->setFileOnlyIncludeOnce(true);
    const auto requested = parser->requestedConfigurationValues();
    QVERIFY(requested.hardDefineLines.contains("#define BATCH_99 99"));
    QVERIFY(!parser->appliedConfiguration()->hardDefineLines.contains("#define BATCH_99 99"));
    f.change({"#if BATCH_99 == 99", "int complete_batch;", "#else", "int stale_batch;", "#endif"});
    QCOMPARE(f.parse(), Request::Accepted); QVERIFY(f.idle());
    QCOMPARE(applied.count(), 1); QVERIFY(parser->appliedConfiguration()->includeOnce);
    QVERIFY(parser->findStatement("complete_batch")); QVERIFY(!parser->findStatement("stale_batch"));
    QCOMPARE(parser->sourceJobReport()->configurationTicket, parser->appliedConfigurationTicket());
}

void TestParserConfigurationQueue::stop_is_nonblocking_without_event_pump_and_waits_for_worker_ack()
{
    Fixture f; QVERIFY(f.apply(config("A"))); auto *parser = f.parser.get();
    Gate gate; parser->setSourceReadObserver([state = gate.state](const QString &) { state->holdOnce(); });
    QCOMPARE(f.parse(), Request::Accepted); QVERIFY(gate.wait());
    auto queuedRan = std::make_shared<std::atomic_bool>(false); QMetaObject::invokeMethod(this, [queuedRan] { *queuedRan = true; }, Qt::QueuedConnection);
    std::mutex mutex; std::condition_variable wake; bool returned = false; std::atomic_bool watchdogReleased{false};
    std::thread watchdog([&] {
        std::unique_lock<std::mutex> lock(mutex);
        if (!wake.wait_for(lock, std::chrono::seconds(2), [&] { return returned; })) {
            watchdogReleased = true; gate.open();
        }
    });
    QSignalSpy stopped(parser, &CppParser::analysisStopped);
    parser->stopScheduling();
    const bool acknowledgedWhileBusy = parser->analysisQuiesced() || stopped.count() != 0;
    { std::lock_guard<std::mutex> lock(mutex); returned = true; } wake.notify_all(); watchdog.join();
    QVERIFY(!acknowledgedWhileBusy);
    QVERIFY(!watchdogReleased.load()); QVERIFY(!queuedRan->load()); QCOMPARE(f.parse(), Request::SchedulingStopped);
    QVERIFY(parser->trackedAnalysisWorkerCount() > 0); QVERIFY(parser->hasPendingWork());
    gate.open(); QVERIFY(f.idle()); QVERIFY(queuedRan->load());
    QCOMPARE(stopped.count(), 1);
    QVERIFY(parser->analysisQuiesced());
    QVERIFY(parser->resumeScheduling()); QCOMPARE(f.parse(), Request::Accepted); QVERIFY(f.idle());
}

void TestParserConfigurationQueue::async_toolchain_probe_prepares_and_applies_complete_values()
{
    Fixture f; QVERIFY(f.apply(config("A"))); auto *parser = f.parser.get();
    const auto b = parser->beginConfigurationPreparation();
    QSignalSpy ready(parser, &CppParser::configurationReady);
    QVERIFY(parser->prepareConfigurationAsync(b, probeRequest(f.directory.path())));
    QCOMPARE(f.parse(), Request::Accepted); QVERIFY(f.idle());
    QCOMPARE(parser->configurationState(), State::Ready); QCOMPARE(parser->appliedConfigurationTicket(), b);
    QVERIFY(ticketSeen(ready, b));
    const auto applied = parser->appliedConfiguration(); QVERIFY(applied);
    QVERIFY(applied->includePaths.contains(f.directory.filePath("qa-includes")));
    QVERIFY(applied->hardDefineLines.contains("#define CONFIG_PROBED 1"));
    QVERIFY(!applied->compilerPreparationIdentity.isEmpty());
    const auto report = parser->sourceJobReport(); QVERIFY(report && report->complete());
    QCOMPARE(report->configurationTicket, b); QCOMPARE(report->configurationDigest, applied->digest());
}

void TestParserConfigurationQueue::cancelled_owned_probe_keeps_service_until_early_child_is_quiescent()
{
    Fixture f; QVERIFY(f.apply(config("A"))); auto *parser = f.parser.get();
    const auto b = parser->beginConfigurationPreparation(); QSignalSpy ready(parser, &CppParser::configurationReady);
    QVERIFY(parser->prepareConfigurationAsync(b, probeRequest(f.directory.path(), true, true)));
    const QString readyFile = f.directory.filePath("probe-ready"), marker = f.directory.filePath("child-marker");
    QVERIFY(spin([&] { return QFile::exists(readyFile) && !contents(marker).isEmpty(); }));
    QVERIFY(parser->trackedAnalysisWorkerCount() > 0);
    std::weak_ptr<CppParser> service = f.parser;
    parser->stopScheduling();
    QVERIFY(parser->trackedAnalysisWorkerCount() > 0); QVERIFY(!service.expired());
    QVERIFY(f.idle()); QVERIFY(!ticketSeen(ready, b));
    const auto captured = contents(marker); QTest::qWait(100); QCOMPARE(contents(marker), captured);
    const auto report = parser->sourceJobReport();
    QVERIFY(!report || report->configurationTicket != b || !report->complete());
    // No anonymous detached worker retains the parser after registry/Qt disposal.
    f.parser.reset(); QVERIFY(spin([&] { return service.expired(); }));
}

void TestParserConfigurationQueue::invalid_configuration_and_wrong_thread_calls_never_become_ready()
{
    Fixture f; const auto a = f.apply(config("A")); QVERIFY(a); auto *parser = f.parser.get();
    const auto ticket = parser->configurationTicket(); std::atomic<quint64> wrongTicket{99};
    std::atomic_bool wrongComplete{true};
    std::thread other([&] {
        wrongTicket = parser->beginConfigurationPreparation();
        wrongComplete = parser->completeConfigurationPreparation(ticket, config("WRONG"));
    }); other.join();
    QCOMPARE(wrongTicket.load(), quint64(0)); QVERIFY(!wrongComplete.load()); QCOMPARE(parser->configurationTicket(), ticket);
    Analysis::ParserConfigurationValues invalid; invalid.language = ParserLanguage::None; invalid.enabled = true;
    const auto b = parser->submitConfiguration(Analysis::ParserConfiguration::create(std::move(invalid))); QVERIFY(b > a);
    QVERIFY(f.idle()); QCOMPARE(parser->configurationState(), State::Failed);
    QVERIFY(!parser->configurationError().isEmpty()); QCOMPARE(parser->appliedConfigurationTicket(), a);
    QCOMPARE(f.parse(), Request::ConfigurationUnavailable);
    const auto c = parser->submitConfiguration(config("DISABLED", false)); QVERIFY(c > b); QVERIFY(f.idle());
    QCOMPARE(f.parse(), Request::Disabled); QCOMPARE(parser->appliedConfigurationTicket(), c);
}

void TestParserConfigurationQueue::queued_outline_uses_latest_certified_demand()
{
    Fixture f;
    QVERIFY(f.apply(config("A")));
    QCOMPARE(f.parse(), Request::Accepted);
    QVERIFY(f.idle());
    auto *parser = f.parser.get();
    const auto report = parser->sourceJobReport();
    QVERIFY(report && report->complete());
    const auto document = f.store->snapshotFor(f.document);
    QVERIFY(document);
    struct ImmediateReclaimer final : Analysis::SemanticReclamationScheduler {
        void retire(Analysis::RetiredSemanticAllocation *allocation) noexcept override { allocation->reclaim(); }
    };
    const auto resources = std::make_shared<Analysis::OutlineResources>(
        Analysis::OutlineResources{Analysis::SemanticMemoryLedger(1024 * 1024), std::make_shared<ImmediateReclaimer>()});
    Analysis::OutlineJobRequest request;
    request.demand = {parser->outlineService(), 1, 0};
    request.stamp = {report->configurationTicket, report->configurationDigest, report->inputGeneration, report->workingDirectory};
    request.sourceJob = report->job;
    request.documentId = document->id;
    request.documentRevision = document->revision;
    request.primaryFile = document->filename;
    request.primaryPath = document->absoluteFilename;
    QSignalSpy published(parser, &CppParser::outlineFinished);
    QVERIFY(parser->submitOutline(request, resources));
    ++request.demand.sequence;
    QVERIFY(parser->submitOutline(request, resources));
    QVERIFY(spin([&] { return published.size() == 1; }));
    auto result = published.takeFirst().at(0).value<Analysis::OutlineJobResult>();
    QCOMPARE(result.status, Analysis::OutlineJobStatus::Ready);
    QVERIFY(result.matches(request));
    QVERIFY(result.publication && result.publication->outline());
    const auto &outline = *result.publication->outline();
    QCOMPARE(outline.roots().size(), std::size_t(1));
    QCOMPARE(outline.label(outline.nodes()[outline.roots().front()]), QStringView(u"from_A : int"));

    // A second analysis of the same immutable input may already have taken
    // ownership of the graph when the queued projection starts.
    Gate parsing;
    QObject receiver;
    CallbackCleanup cleanup{f.parser, &receiver, parsing.state};
    QObject::connect(parser, &CppParser::parseStarted, &receiver,
        [gate = parsing.state] { gate->holdOnce(); }, Qt::DirectConnection);
    QCOMPARE(f.parse(), Request::Accepted);
    QVERIFY(parsing.wait());
    ++request.demand.sequence;
    QVERIFY(parser->submitOutline(request, resources));
    parsing.open();
    QVERIFY(spin([&] { return published.size() == 1; }));
    const auto superseded = published.takeFirst().at(0).value<Analysis::OutlineJobResult>();
    QCOMPARE(superseded.status, Analysis::OutlineJobStatus::Obsolete);
    QVERIFY(!superseded.publication);
    const auto newer = parser->sourceJobReport();
    QVERIFY(newer && newer->complete() && newer->job != request.sourceJob);
    QCOMPARE(newer->inputGeneration, request.stamp.inputGeneration);
    request.sourceJob = newer->job;

    ++request.demand.sequence;
    QVERIFY(parser->submitOutline(request, resources));
    parser->cancelOutline();
    QVERIFY(f.idle());
    QCOMPARE(published.count(), 0);
    result.publication.reset();
    QCOMPARE(resources->ledger.usage().admitted(), std::size_t(0));
}

void TestParserConfigurationQueue::outline_controller_switches_files_with_same_generation()
{
    Fixture f;
    QVERIFY(f.apply(config("A")));
    const QString other = f.directory.filePath("other.cpp");
    const auto otherId = f.store->add(other, [](QStringList &output) {
        output = {"int only_in_other;"};
        return true;
    });
    QVERIFY(otherId);
    DocumentId active = f.document;
    const auto current = [&] {
        const auto snapshot = f.store->capture();
        const auto document = snapshot.snapshot->documents().value(active);
        return ClassBrowserController::SourceIdentity{snapshot.snapshot->generation(), document->id, document->revision,
            document->filename, document->absoluteFilename};
    };
    struct ImmediateReclaimer final : Analysis::SemanticReclamationScheduler {
        void retire(Analysis::RetiredSemanticAllocation *allocation) noexcept override { allocation->reclaim(); }
    };
    const auto resources = std::make_shared<Analysis::OutlineResources>(
        Analysis::OutlineResources{Analysis::SemanticMemoryLedger(1024 * 1024), std::make_shared<ImmediateReclaimer>()});
    ClassBrowserController controller(resources, current);
    const auto context = [&] {
        ClassBrowserController::Context result;
        result.parser = f.parser;
        result.source = current();
        return result;
    };
    const auto ready = [&] { return controller.state() == ClassBrowserController::State::Ready; };
    controller.setContext(context());
    QVERIFY(spin(ready));
    const auto previous = controller.model()->index(0, 0);
    QCOMPARE(controller.model()->data(previous).toString(), QString("from_A : int"));
    const auto firstGeneration = current().generation;
    active = otherId;
    QCOMPARE(current().generation, firstGeneration);
    controller.setContext(context());
    QCOMPARE(controller.model()->rowCount(), 0);
    QVERIFY(spin(ready));
    QCOMPARE(controller.model()->data(controller.model()->index(0, 0)).toString(), QString("only_in_other : int"));
    QVERIFY(!controller.model()->data(previous).isValid());
    controller.stop();
    QCOMPARE(controller.model()->rowCount(), 0);
    QVERIFY(f.idle());
    QCOMPARE(resources->ledger.usage().admitted(), std::size_t(0));
}

void TestParserConfigurationQueue::project_outline_completes_a_single_file_manifest()
{
    Fixture f;
    const QString other = f.directory.filePath("other.cpp");
    QVERIFY(touch(other, "int only_in_other;"));
    auto project = config("A")->values();
    project.projectFiles = {f.file, other};
    QVERIFY(f.apply(Analysis::ParserConfiguration::create(std::move(project))));
    QCOMPARE(f.parse(), Request::Accepted);
    QVERIFY(f.idle());
    const auto initial = f.parser->sourceJobReport();
    QVERIFY(initial && initial->complete());
    QVERIFY(!initial->reads.contains(exactPathSpelling(other)));

    struct ImmediateReclaimer final : Analysis::SemanticReclamationScheduler {
        void retire(Analysis::RetiredSemanticAllocation *allocation) noexcept override { allocation->reclaim(); }
    };
    const auto resources = std::make_shared<Analysis::OutlineResources>(
        Analysis::OutlineResources{Analysis::SemanticMemoryLedger(1024 * 1024), std::make_shared<ImmediateReclaimer>()});
    const auto current = [&] {
        const auto capture = f.store->capture();
        return ClassBrowserController::SourceIdentity{capture.snapshot->generation()};
    };
    ClassBrowserController controller(resources, current);
    ClassBrowserController::Context context;
    context.parser = f.parser;
    context.workspace = f.parser->outlineService();
    context.source = current();
    context.wholeProject = true;
    controller.setContext(std::move(context));
    QVERIFY(spin([&] { return controller.state() == ClassBrowserController::State::Ready; }));
    QCOMPARE(controller.model()->rowCount(), 2);
    QVERIFY(f.parser->sourceJobReport()->reads.contains(exactPathSpelling(other)));
}

int runParserConfigurationQueueHelper()
{
    if (!QCoreApplication::arguments().contains("--qa-parser-configuration-probe")) return -1;
    const QString directory = qEnvironmentVariable("REDPANDA_QUEUE_PROBE_DIRECTORY");
    const QString mode = qEnvironmentVariable("REDPANDA_QUEUE_PROBE_MODE");
    if (mode == "child") {
        QElapsedTimer timer; timer.start();
        QFile marker(QDir(directory).filePath("child-marker"));
        if (!marker.open(QIODevice::WriteOnly | QIODevice::Append)) return 11;
        while (timer.elapsed() < 15000) { marker.write("x"); marker.flush(); QThread::msleep(10); }
        return 0;
    }
#ifdef Q_OS_WIN
    if (qEnvironmentVariableIntValue("REDPANDA_QUEUE_PROBE_CHILD") == 1 && mode == "includes") {
        const QString executable = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
        QString command = '"' + executable + "\" --qa-parser-configuration-probe";
        STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION process{};
        qputenv("REDPANDA_QUEUE_PROBE_MODE", "child");
        const BOOL started = CreateProcessW(reinterpret_cast<LPCWSTR>(executable.utf16()),
            reinterpret_cast<LPWSTR>(command.data()), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
            nullptr, nullptr, &startup, &process);
        qputenv("REDPANDA_QUEUE_PROBE_MODE", mode.toUtf8());
        if (!started) return 12;
        CloseHandle(process.hThread); CloseHandle(process.hProcess);
        QElapsedTimer childDeadline; childDeadline.start();
        while (contents(QDir(directory).filePath("child-marker")).isEmpty() && childDeadline.elapsed() < 3000) QThread::msleep(5);
        if (contents(QDir(directory).filePath("child-marker")).isEmpty()) return 13;
    }
#endif
    if (!touch(QDir(directory).filePath("probe-ready"))) return 14;
    if (qEnvironmentVariableIntValue("REDPANDA_QUEUE_PROBE_HOLD") == 1) {
        QElapsedTimer deadline; deadline.start();
        while (!QFile::exists(QDir(directory).filePath("probe-release")) && deadline.elapsed() < 15000) QThread::msleep(5);
    }
    QByteArray output = mode == "defines" ? QByteArray("#define CONFIG_PROBED 1\n")
        : QByteArray("#include <...> search starts here:\n ") + QDir(directory).filePath("qa-includes").toUtf8() + "\nEnd of search list.\n";
    std::fwrite(output.constData(), 1, output.size(), stdout); std::fflush(stdout); return 0;
}
