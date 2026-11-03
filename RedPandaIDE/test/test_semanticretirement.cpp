// SPDX-License-Identifier: GPL-3.0-or-later
#include "test_semanticretirement.h"
#include "src/analysis/semanticretirement.h"
#include "src/analysis/semanticpublicationbuilder.h"
#include "src/parser/parserutils.h"
#include <QApplication>
#include <QTest>
#include <QThread>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <stdexcept>
#include <vector>

using namespace Analysis;
using namespace std::chrono_literals;
namespace {
struct ProbeState {
    std::mutex mutex;
    std::condition_variable wake;
    bool entered = false, allow = true;
    std::size_t destroyed = 0;
    std::vector<std::thread::id> threads;
    void destruction() {
        std::unique_lock<std::mutex> lock(mutex);
        entered = true; wake.notify_all();
        wake.wait(lock, [&] { return allow; });
        ++destroyed; threads.push_back(std::this_thread::get_id()); wake.notify_all();
    }
    bool waitEntered() {
        std::unique_lock<std::mutex> lock(mutex);
        return wake.wait_for(lock, 2s, [&] { return entered; });
    }
    void release() { std::lock_guard<std::mutex> lock(mutex); allow = true; wake.notify_all(); }
};
struct ReleaseGate {
    std::shared_ptr<ProbeState> state;
    ~ReleaseGate() { state->release(); }
};
struct Payload {
    Payload(SemanticMemoryResource *memory, std::shared_ptr<ProbeState> state, std::size_t count = 0):
        state(std::move(state)), values(memory) { values.resize(count, 17); }
    ~Payload() { state->destruction(); }
    std::shared_ptr<ProbeState> state;
    std::pmr::vector<int> values;
    std::shared_ptr<const Payload> child;
};
struct Throws {
    Throws() { throw std::runtime_error("Injected constructor failure"); }
};
std::shared_ptr<const Payload> payload(const SemanticMemoryLedger &ledger,
        const std::shared_ptr<SemanticRetirementScheduler> &scheduler,
        const std::shared_ptr<ProbeState> &probe, std::size_t count = 0,
        std::shared_ptr<const Payload> child = {})
{
    auto reservation = ledger.tryReserve();
    if (!reservation) throw SemanticBudgetExceeded();
    auto memory = reservation.resource(SemanticMemoryKind::Payload);
    auto value = Detail::makeSemanticOwner<Payload>(memory, scheduler, memory.get(), probe, count);
    value->child = std::move(child);
    reservation.close();
    return value;
}
bool until(const std::function<bool()> &condition)
{
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!condition()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(1ms);
    }
    return true;
}
} // namespace

void TestSemanticRetirement::gui_drop_reclaims_large_payload_on_one_worker()
{
    QCOMPARE(QThread::currentThread(), qApp->thread());
    const auto gui = std::this_thread::get_id();
    SemanticMemoryLedger ledger(16 * 1024 * 1024);
    auto scheduler = SemanticRetirementScheduler::create();
    auto probe = std::make_shared<ProbeState>(); probe->allow = false;
    ReleaseGate gate{probe};
    auto value = payload(ledger, scheduler, probe, 1024 * 1024);
    const auto admitted = ledger.usage().admitted(); QVERIFY(admitted > 4 * 1024 * 1024);
    value.reset(); // Returns while the large destructor is deliberately blocked.
    QVERIFY(probe->waitEntered());
    QVERIFY(ledger.usage().retired >= 4 * 1024 * 1024);
    scheduler->beginStop();
    auto deferred = scheduler->waitForStopped(0ms);
    QCOMPARE(deferred.status, SemanticRetirementStatus::StoppingDeferred);
    QCOMPARE(deferred.outstanding, std::size_t(1));
    QCOMPARE(deferred.reclaiming, std::size_t(1));
    probe->release();
    const auto stopped = scheduler->waitForStopped(2s);
    QVERIFY(stopped.stopped()); QCOMPARE(stopped.reclaimed, std::uint64_t(1));
    QCOMPARE(probe->threads.size(), std::size_t(1));
    QVERIFY(probe->threads[0] != gui); QCOMPARE(probe->threads[0], stopped.workerThread);
    QCOMPARE(ledger.usage().admitted(), std::size_t(0));
}
void TestSemanticRetirement::old_pins_prevent_false_stopped_and_remain_charged()
{
    SemanticMemoryLedger ledger(2 * 1024 * 1024);
    auto scheduler = SemanticRetirementScheduler::create();
    auto probe = std::make_shared<ProbeState>();
    auto old = payload(ledger, scheduler, probe, 100000);
    auto current = payload(ledger, scheduler, probe, 100000, old);
    current.reset(); scheduler->beginStop();
    QVERIFY(until([&] { return scheduler->state().reclaimed == 1; }));
    const auto deferred = scheduler->waitForStopped(1ms);
    QCOMPARE(deferred.status, SemanticRetirementStatus::StoppingDeferred);
    QCOMPARE(deferred.queued, std::size_t(0)); QCOMPARE(deferred.outstanding, std::size_t(1));
    QVERIFY(ledger.usage().live >= 400000); // Reader still owns the old generation.
    QVERIFY_EXCEPTION_THROWN(payload(ledger, scheduler, probe), SemanticBudgetExceeded);
    old.reset();
    const auto stopped = scheduler->waitForStopped(2s);
    QVERIFY(stopped.stopped()); QCOMPARE(stopped.reclaimed, std::uint64_t(2));
    QCOMPARE(stopped.refusedStopping, std::uint64_t(1));
    QCOMPARE(ledger.usage().admitted(), std::size_t(0));
}
void TestSemanticRetirement::saturation_rejects_admission_but_never_retirement()
{
    SemanticMemoryLedger ledger(1024 * 1024);
    auto scheduler = SemanticRetirementScheduler::create(2);
    auto probe = std::make_shared<ProbeState>(); probe->allow = false;
    ReleaseGate gate{probe};
    auto first = payload(ledger, scheduler, probe), second = payload(ledger, scheduler, probe);
    const auto before = ledger.usage().admitted();
    QVERIFY_EXCEPTION_THROWN(payload(ledger, scheduler, probe), SemanticBudgetExceeded);
    QCOMPARE(ledger.usage().admitted(), before);
    first.reset(); QVERIFY(probe->waitEntered());
    second.reset(); // Already admitted: must queue even while saturated.
    scheduler->beginStop();
    const auto deferred = scheduler->waitForStopped(1ms);
    QCOMPARE(deferred.outstanding, std::size_t(2)); QCOMPARE(deferred.queued, std::size_t(1));
    QVERIFY(!deferred.stopped()); QCOMPARE(deferred.refusedCapacity, std::uint64_t(1));
    probe->release();
    const auto stopped = scheduler->waitForStopped(2s);
    QVERIFY(stopped.stopped()); QCOMPARE(stopped.peakOutstanding, std::size_t(2));
    QCOMPARE(stopped.reclaimed, std::uint64_t(2)); QCOMPARE(ledger.usage().admitted(), std::size_t(0));
}
void TestSemanticRetirement::allocation_and_constructor_failures_balance_registration()
{
    auto scheduler = SemanticRetirementScheduler::create(1);
    SemanticMemoryLedger ledger(4096);
    auto reservation = ledger.tryReserve(); auto memory = reservation.resource(SemanticMemoryKind::Payload);
    QVERIFY_EXCEPTION_THROWN(Detail::makeSemanticOwner<Throws>(memory, scheduler), std::runtime_error);
    QCOMPARE(scheduler->state().outstanding, std::size_t(0));
    reservation.close();
    auto probe = std::make_shared<ProbeState>();
    // A closed reservation rejects allocation after scheduler admission.
    QVERIFY_EXCEPTION_THROWN(Detail::makeSemanticOwner<Payload>(memory, scheduler, memory.get(), probe), SemanticBudgetExceeded);
    QCOMPARE(scheduler->state().outstanding, std::size_t(0));
    memory.reset();
    QVERIFY(scheduler->shutdown(2s).stopped()); QCOMPARE(ledger.usage().admitted(), std::size_t(0));
    QVERIFY_EXCEPTION_THROWN(SemanticRetirementScheduler::create(0), std::invalid_argument);
}
void TestSemanticRetirement::control_block_failure_still_retires_constructed_payload()
{
    auto scheduler = SemanticRetirementScheduler::create();
    SemanticMemoryLedger ledger(4096); auto reservation = ledger.tryReserve();
    auto memory = reservation.resource(SemanticMemoryKind::Payload);
    const auto available = ledger.usage().limit - ledger.usage().admitted();
    const auto fillBytes = available - sizeof(Detail::SemanticOwner<Payload>);
    void *fill = memory->allocate(fillBytes);
    auto probe = std::make_shared<ProbeState>();
    QVERIFY_EXCEPTION_THROWN(Detail::makeSemanticOwner<Payload>(memory, scheduler, memory.get(), probe), SemanticBudgetExceeded);
    QVERIFY(scheduler->shutdown(2s).stopped());
    QCOMPARE(probe->destroyed, std::size_t(1));
    QCOMPARE(scheduler->state().outstanding, std::size_t(0));
    memory->deallocate(fill, fillBytes); reservation.close(); memory.reset();
    QCOMPARE(ledger.usage().admitted(), std::size_t(0));
}
void TestSemanticRetirement::concurrent_drops_use_the_same_worker()
{
    SemanticMemoryLedger ledger(4 * 1024 * 1024);
    auto scheduler = SemanticRetirementScheduler::create(64);
    auto probe = std::make_shared<ProbeState>();
    std::vector<std::shared_ptr<const Payload>> values;
    for (int i = 0; i < 48; ++i) values.push_back(payload(ledger, scheduler, probe, 1000));
    std::vector<std::thread> droppers;
    for (int group = 0; group < 6; ++group)
        droppers.emplace_back([&, group] { for (int i = group; i < 48; i += 6) values[i].reset(); });
    for (auto &thread : droppers) thread.join();
    const auto stopped = scheduler->shutdown(2s);
    QVERIFY(stopped.stopped()); QCOMPARE(stopped.reclaimed, std::uint64_t(48));
    QCOMPARE(probe->threads.size(), std::size_t(48));
    for (const auto &id : probe->threads) QCOMPARE(id, stopped.workerThread);
    QCOMPARE(ledger.usage().admitted(), std::size_t(0));
}
void TestSemanticRetirement::last_pin_keeps_service_alive_without_gui_join()
{
    SemanticMemoryLedger ledger(1024 * 1024);
    auto scheduler = SemanticRetirementScheduler::create();
    std::weak_ptr<SemanticRetirementScheduler> service = scheduler;
    auto probe = std::make_shared<ProbeState>();
    auto pinned = payload(ledger, scheduler, probe, 100000);
    scheduler->beginStop(); QVERIFY(!scheduler->waitForStopped(0ms).stopped());
    scheduler.reset(); QVERIFY(!service.expired());
    pinned.reset();
    QVERIFY(until([&] { return service.expired() && ledger.usage().admitted() == 0; }));
    QCOMPARE(probe->destroyed, std::size_t(1)); QVERIFY(probe->threads[0] != std::this_thread::get_id());
}
void TestSemanticRetirement::weak_control_block_remains_accounted_after_payload_ack()
{
    SemanticMemoryLedger ledger(1024 * 1024);
    auto scheduler = SemanticRetirementScheduler::create();
    auto probe = std::make_shared<ProbeState>();
    auto value = payload(ledger, scheduler, probe, 100000);
    std::weak_ptr<const Payload> weak = value;
    value.reset(); QVERIFY(scheduler->shutdown(2s).stopped()); QVERIFY(weak.expired());
    const auto usage = ledger.usage();
    QVERIFY(usage.retired > 0); QVERIFY(usage.admitted() < 4096); // Small weak control block remains charged.
    weak.reset(); QCOMPARE(ledger.usage().admitted(), std::size_t(0));
}
void TestSemanticRetirement::real_publication_and_pinned_shard_drain_in_stages()
{
    class Source final : public SemanticGraphSource {
    public:
        QString name = "large_symbol", large = QString(300000, QLatin1Char('x')), file = "source.cpp";
        bool symbol(SemanticSourceId id, SemanticSourceSymbol &out) const override {
            if (id != 1) return false;
            out.name = name; out.qualifiedName = name; out.value = large;
            out.kind = StatementKind::Variable; out.scope = StatementScope::Global;
            out.accessibility = StatementAccessibility::Public;
            out.declaration = {file, 4, -1}; out.definition = {file, 4, -1};
            return true;
        }
        bool roots(QStringView, SemanticSourceVisitor visit) const override { return visit(1); }
        bool children(SemanticSourceId, SemanticSourceVisitor) const override { return true; }
    } source;
    SemanticMemoryLedger ledger(8 * 1024 * 1024);
    auto scheduler = SemanticRetirementScheduler::create(4);
    SemanticIdentitySource identities(109);
    SemanticPublicationRequest request; request.identity = {109, 1, 1, 1, 1};
    request.files = {source.file}; request.primaryFile = source.file;
    auto built = SemanticPublicationBuilder::build(source, request, identities, ledger, scheduler);
    QVERIFY(built.complete()); QCOMPARE(scheduler->state().outstanding, std::size_t(4));
    auto pinnedShard = built.publication->shards()[0];
    const auto text = pinnedShard->records()[0].value;
    built.publication.reset(); scheduler->beginStop();
    QVERIFY(until([&] { return scheduler->state().reclaimed == 3; }));
    const auto deferred = scheduler->waitForStopped(1ms);
    QVERIFY(!deferred.stopped()); QCOMPARE(deferred.outstanding, std::size_t(1));
    QCOMPARE(pinnedShard->text(text), QStringView(source.large));
    QVERIFY(ledger.usage().live > 600000);
    pinnedShard.reset();
    const auto stopped = scheduler->waitForStopped(2s);
    QVERIFY(stopped.stopped()); QCOMPARE(stopped.reclaimed, std::uint64_t(4));
    QCOMPARE(ledger.usage().admitted(), std::size_t(0));
}
