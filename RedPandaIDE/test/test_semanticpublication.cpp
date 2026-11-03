// SPDX-License-Identifier: GPL-3.0-or-later
#include "test_semanticpublication.h"
#include "src/analysis/semanticpublicationbuilder.h"
#include "src/analysis/enginesemanticadapter.h"
#include "src/analysis/semanticsnapshotbuilder.h"
#include <QTest>
#include <QSemaphore>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>

using namespace Analysis;
static_assert(std::is_const<SemanticPublicationPtr::element_type>::value, "Published ownership must be const");
namespace {
class ManualReclaimer final : public SemanticReclamationScheduler {
public:
    void retire(RetiredSemanticAllocation *value) noexcept override {
        std::lock_guard<std::mutex> lock(mutex);
        value->nextRetired = head; head = value; ++queued;
    }
    void drain() {
        while (true) {
            RetiredSemanticAllocation *value;
            {
                std::lock_guard<std::mutex> lock(mutex);
                value = head;
                if (!value) break;
                head = value->nextRetired; --queued;
            }
            value->reclaim();
        }
    }
    void drainOnWorker() { std::thread worker([this] { drain(); }); worker.join(); }
    std::mutex mutex;
    RetiredSemanticAllocation *head = nullptr;
    std::size_t queued = 0;
};
struct InputValue {
    QString name, qualifiedName, type, arguments, signature, value, group, key;
    QString declaration = "source.cpp", definition = "source.cpp";
    int line = 1, definitionLine = 1;
    SemanticSourceId parent = 0;
    StatementKind kind = StatementKind::Variable;
    StatementScope scope = StatementScope::Global;
    StatementAccessibility accessibility = StatementAccessibility::Public;
    quint32 properties = static_cast<quint32>(StatementProperty::InProject);
    std::vector<SemanticSourceId> children;
};
class Source final : public SemanticGraphSource {
public:
    bool symbol(SemanticSourceId id, SemanticSourceSymbol &out) const override {
        if (throwOnRead) throw std::runtime_error("injected source failure");
        const auto found = values.find(id); if (found == values.end()) return false;
        const auto &v = found->second;
        out.parent = v.parent; out.name = v.name; out.qualifiedName = v.qualifiedName;
        out.type = v.type; out.arguments = v.arguments; out.signature = v.signature; out.value = v.value;
        out.scopeGroup = v.group; out.selectionKey = v.key; out.kind = v.kind; out.scope = v.scope;
        out.accessibility = v.accessibility; out.properties = v.properties;
        out.declaration = {v.declaration, v.line, -1}; out.definition = {v.definition, v.definitionLine, -1};
        return true;
    }
    bool roots(QStringView name, SemanticSourceVisitor visitor) const override {
        const auto found = files.find(name.toString()); if (found == files.end()) return false;
        for (const auto id : found->second) if (!visitor(id)) return false;
        return true;
    }
    bool children(SemanticSourceId id, SemanticSourceVisitor visitor) const override {
        ++childScans[id];
        const auto found = values.find(id); if (found == values.end()) return false;
        for (const auto next : found->second.children) if (!visitor(next)) return false;
        return true;
    }
    void add(SemanticSourceId id, QString name, StatementKind kind = StatementKind::Variable, SemanticSourceId parent = 0) {
        InputValue value; value.name = name; value.qualifiedName = name; value.kind = kind; value.parent = parent;
        values.emplace(id, std::move(value));
    }
    std::map<SemanticSourceId, InputValue> values;
    std::map<QString, std::vector<SemanticSourceId>> files;
    mutable std::map<SemanticSourceId, int> childScans;
    bool throwOnRead = false;
};
SemanticPublicationRequest request(bool project = false)
{
    SemanticPublicationRequest result; result.identity = {19, 1, 2, 3, 5};
    result.files = {"source.cpp"}; result.primaryFile = "source.cpp"; result.wholeProject = project;
    return result;
}
Source simpleSource(int textSize = 0)
{
    Source source; source.add(1, "Root", StatementKind::Namespace); source.values[1].group = "Root";
    source.add(2, "Root::member", StatementKind::Variable, 1); source.values[2].type = "int";
    source.values[1].children = {2}; source.files["source.cpp"] = {1, 2};
    if (textSize) source.values[2].value = QString(textSize, QLatin1Char('x'));
    return source;
}
void verifyTree(const SemanticPublicationPtr &publication)
{
    QVERIFY(publication && publication->outline());
    const auto &tree = *publication->outline();
    std::vector<bool> seen(tree.nodes().size(), false);
    std::vector<NodeIndex> pending(tree.roots().begin(), tree.roots().end());
    while (!pending.empty()) {
        const auto id = pending.back(); pending.pop_back(); QVERIFY(id < tree.nodes().size()); QVERIFY(!seen[id]); seen[id] = true;
        const auto &node = tree.nodes()[id]; QVERIFY(publication->symbol(node.symbol));
        QVERIFY(node.firstChild <= tree.children().size()); QVERIFY(node.childCount <= tree.children().size() - node.firstChild);
        for (quint32 i = 0; i < node.childCount; ++i) {
            const auto child = tree.children()[node.firstChild + i]; QVERIFY(child > id); QVERIFY(child < tree.nodes().size());
            QCOMPARE(tree.nodes()[child].parent, id); pending.push_back(child);
        }
    }
    for (bool reachable : seen) QVERIFY(reachable);
}
QString field(const SemanticPublicationPtr &publication, const SemanticOutlineNode &node, SemanticTextRef SemanticRecord::*member)
{
    const auto *record = publication->symbol(node.symbol);
    return record ? publication->text(node.symbol, record->*member).toString() : QString{};
}
} // namespace

void TestSemanticPublication::union_shares_payload_and_scans_children_once()
{
    auto source = simpleSource();
    source.files["second.cpp"] = {1, 2, 1}; // Same namespace/member graph in both flat maps.
    auto input = request(true); input.files.append("second.cpp");
    SemanticIdentitySource ids(19); SemanticMemoryLedger ledger(1024 * 1024);
    auto reclaim = std::make_shared<ManualReclaimer>();
    auto built = SemanticPublicationBuilder::build(source, input, ids, ledger, reclaim);
    QVERIFY(built.complete()); QCOMPARE(built.diagnostics.uniquePayloads, quint64(2));
    QCOMPARE(built.publication->shards().size(), std::size_t(1));
    QCOMPARE(built.publication->segments()[0]->members().size(), std::size_t(2));
    QCOMPARE(source.childScans[1], 1); QCOMPARE(source.childScans[2], 1);
    QVERIFY(ledger.usage().live > 0); QCOMPARE(ledger.usage().scratch, std::size_t(0));
    verifyTree(built.publication);
    built.publication.reset(); QVERIFY(ledger.usage().retired > 0); reclaim->drainOnWorker();
    QCOMPARE(ledger.usage().admitted(), std::size_t(0));
}
void TestSemanticPublication::exact_ids_and_ambiguous_selection()
{
    Source source; source.add(1, "overload", StatementKind::Function); source.add(2, "overload", StatementKind::Function);
    source.values[1].signature = "(int)"; source.values[2].signature = "(int)"; source.values[2].line = 9;
    source.files["source.cpp"] = {1, 2};
    SemanticIdentitySource ids(19); SemanticMemoryLedger ledger(1024 * 1024); auto reclaim = std::make_shared<ManualReclaimer>();
    auto first = SemanticPublicationBuilder::build(source, request(), ids, ledger, reclaim); QVERIFY(first.complete());
    const auto &node = first.publication->outline()->nodes()[0];
    const auto key = field(first.publication, node, &SemanticRecord::selectionKey);
    const auto matches = first.publication->selection(key); QCOMPARE(matches.status, SelectionResolution::Ambiguous); QCOMPARE(matches.count, quint32(2));
    QCOMPARE(first.publication->selection(key, 1).status, SelectionResolution::Ambiguous); QVERIFY(first.publication->selection(key, 1).hasMore);
    QCOMPARE(first.publication->exact(matches.candidates[0]).status, SelectionResolution::Exact);
    auto next = request(); next.identity.job = 2;
    auto second = SemanticPublicationBuilder::build(source, next, ids, ledger, reclaim); QVERIFY(second.complete());
    QCOMPARE(second.publication->exact(matches.candidates[0]).status, SelectionResolution::Missing);
    QVERIFY(second.publication->outline()->id() != first.publication->outline()->id());
    first.publication.reset(); second.publication.reset(); reclaim->drainOnWorker(); QCOMPARE(ledger.usage().admitted(), std::size_t(0));
}
void TestSemanticPublication::old_pins_and_retirement_stay_charged()
{
    const auto source = simpleSource(5000); auto reclaim = std::make_shared<ManualReclaimer>();
    SemanticIdentitySource probeIds(19); SemanticMemoryLedger probeLedger(1024 * 1024);
    auto probe = SemanticPublicationBuilder::build(source, request(), probeIds, probeLedger, reclaim); QVERIFY(probe.complete());
    const auto measured = probeLedger.usage(); QVERIFY(measured.live > 0);
    probe.publication.reset(); reclaim->drainOnWorker(); QCOMPARE(probeLedger.usage().admitted(), std::size_t(0));
    SemanticIdentitySource ids(19); SemanticMemoryLedger ledger(measured.peakAdmitted + measured.live / 2);
    auto first = SemanticPublicationBuilder::build(source, request(), ids, ledger, reclaim); QVERIFY(first.complete());
    auto pinned = first.publication;
    auto next = request(); next.identity.job = 2;
    auto rejected = SemanticPublicationBuilder::build(source, next, ids, ledger, reclaim);
    QCOMPARE(rejected.status, SemanticBuildStatus::BudgetExceeded); QVERIFY(!rejected.publication);
    reclaim->drainOnWorker(); QVERIFY(ledger.usage().live > 0); // Old reader still owns its exact data.
    QVERIFY(field(pinned, pinned->outline()->nodes()[1], &SemanticRecord::value).size() == 5000);
    first.publication.reset(); QCOMPARE(ledger.usage().retired, std::size_t(0));
    // A retained old publication prevents credit even after the cache/current owner releases it.
    QVERIFY(ledger.usage().admitted() <= ledger.usage().limit);
    pinned.reset(); QVERIFY(ledger.usage().retired > 0);
    reclaim->drainOnWorker(); QCOMPARE(ledger.usage().admitted(), std::size_t(0));
    auto recovered = SemanticPublicationBuilder::build(source, next, ids, ledger, reclaim); QVERIFY(recovered.complete());
    recovered.publication.reset(); reclaim->drainOnWorker(); QCOMPARE(ledger.usage().admitted(), std::size_t(0));
}
void TestSemanticPublication::allocation_cap_precedes_payload_and_scratch_growth()
{
    const auto source = simpleSource(10000); auto reclaim = std::make_shared<ManualReclaimer>(); SemanticIdentitySource ids(19);
    for (const std::size_t capacity : {std::size_t(0), std::size_t(256), std::size_t(4096)}) {
        SemanticMemoryLedger ledger(capacity);
        auto built = SemanticPublicationBuilder::build(source, request(), ids, ledger, reclaim);
        QCOMPARE(built.status, SemanticBuildStatus::BudgetExceeded); QVERIFY(!built.publication);
        QVERIFY(ledger.usage().admitted() <= capacity); QVERIFY(ledger.usage().peakAdmitted <= capacity);
        reclaim->drainOnWorker(); QCOMPARE(ledger.usage().admitted(), std::size_t(0));
    }
    SemanticMemoryLedger ticketLedger(1024);
    std::vector<SemanticBuildReservation> emptyTickets;
    for (int i = 0; i < 128; ++i) {
        auto ticket = ticketLedger.tryReserve();
        if (!ticket) break;
        emptyTickets.push_back(std::move(ticket));
    }
    QVERIFY(!emptyTickets.empty()); QVERIFY(emptyTickets.size() < 128);
    QVERIFY(ticketLedger.usage().admitted() <= std::size_t(1024));
    emptyTickets.clear(); QCOMPARE(ticketLedger.usage().admitted(), std::size_t(0));
}
void TestSemanticPublication::cycles_and_traversal_limits_are_explicit_partial_results()
{
    auto source = simpleSource(); source.values[2].children = {1};
    SemanticIdentitySource ids(19); SemanticMemoryLedger ledger(1024 * 1024); auto reclaim = std::make_shared<ManualReclaimer>();
    auto cycle = SemanticPublicationBuilder::build(source, request(), ids, ledger, reclaim);
    QCOMPARE(cycle.status, SemanticBuildStatus::Partial); QVERIFY(cycle.diagnostics.graph.cycleDetected); verifyTree(cycle.publication);
    auto limited = request(); limited.limits.maximumWorkItems = 4;
    auto partial = SemanticPublicationBuilder::build(source, limited, ids, ledger, reclaim);
    QCOMPARE(partial.status, SemanticBuildStatus::Partial); QVERIFY(partial.diagnostics.graph.workLimitReached);
    QVERIFY(partial.diagnostics.graph.workItems <= quint64(4)); verifyTree(partial.publication);
    auto textLimited = request(); textLimited.maximumTextUnitsExamined = 2;
    auto text = SemanticPublicationBuilder::build(source, textLimited, ids, ledger, reclaim);
    QCOMPARE(text.status, SemanticBuildStatus::Partial); QVERIFY(text.diagnostics.textLimitReached); QVERIFY(!text.complete());
    cycle.publication.reset(); partial.publication.reset(); text.publication.reset(); reclaim->drainOnWorker(); QCOMPARE(ledger.usage().admitted(), std::size_t(0));
}

void TestSemanticPublication::native_adapter_preserves_outline_values()
{
    const auto make = [](QString name, StatementKind kind, QString file, int line, PStatement parent = {}) {
        auto value = std::make_shared<Statement>(); value->command = name; value->kind = kind; value->fileName = file;
        value->line = line; value->definitionFileName = file; value->definitionLine = line;
        value->scope = parent ? StatementScope::ClassLocal : StatementScope::Global; value->parentScope = parent;
        value->fullName = parent ? parent->fullName + "::" + name : name; value->accessibility = StatementAccessibility::Public;
        value->properties = StatementProperty::InProject; return value;
    };
    auto ns1 = make("Ns", StatementKind::Namespace, "header.h", 2);
    auto ns2 = make("Ns", StatementKind::Namespace, "source.cpp", 3);
    auto type = make("Derived", StatementKind::Class, "source.cpp", 4, ns2);
    auto method = make("method", StatementKind::Function, "header.h", 8, type);
    method->type = "int"; method->args = "(int n)"; method->noNameArgs = "(int)";
    method->definitionFileName = "source.cpp"; method->definitionLine = 20;
    method->properties |= StatementProperty::Inherited | StatementProperty::Virtual | StatementProperty::HasDefinition;
    method->accessibility = StatementAccessibility::Protected;
    ns2->children.insert(type->fullName, type); type->children.insert(method->fullName, method);
    StatementMap values; for (const auto &value : {ns1, ns2, type, method}) values.insert(value->fullName, value);
    const auto oracle = SemanticSnapshotBuilder::file({}, "source.cpp", values); QVERIFY(oracle->complete());
    SemanticMemoryLedger ledger(1024 * 1024); SemanticIdentitySource ids(19); auto reclaim = std::make_shared<ManualReclaimer>();
    SemanticPublicationBuild built;
    {
        auto reservation = ledger.tryReserve(); auto scratch = reservation.resource(SemanticMemoryKind::Scratch);
        const QString filename = "source.cpp";
        const EngineSemanticFileView files[]{{filename, &values}};
        EngineSemanticGraphSource adapter(files, 1, scratch);
        built = SemanticPublicationBuilder::build(adapter, request(), ids, ledger, reclaim);
    }
    QVERIFY(built.complete()); QCOMPARE(built.publication->outline()->nodes().size(), std::size_t(oracle->nodes().size()));
    for (const auto &expected : oracle->nodes()) {
        bool matched = false;
        for (const auto &node : built.publication->outline()->nodes()) {
            const auto *record = built.publication->symbol(node.symbol);
            if (field(built.publication, node, &SemanticRecord::qualifiedName) != expected.symbol.qualifiedName) continue;
            matched = true; QCOMPARE(record->kind, expected.symbol.kind); QCOMPARE(record->accessibility, expected.symbol.accessibility);
            QCOMPARE(record->properties, expected.symbol.properties); QCOMPARE(record->declaration.line, expected.symbol.declaration.line);
            QCOMPARE(record->definition.line, expected.symbol.definition.line);
            QCOMPARE(built.publication->text(node.symbol, record->definition.filename).toString(), expected.symbol.definition.filename);
        }
        QVERIFY(matched);
    }
    method->type = "changed_after_publication"; values.clear(); ns2->children.clear(); type->children.clear();
    bool immutableType = false;
    for (const auto &node : built.publication->outline()->nodes())
        if (field(built.publication, node, &SemanticRecord::qualifiedName) == "Ns::Derived::method") immutableType = field(built.publication, node, &SemanticRecord::type) == "int";
    QVERIFY(immutableType); verifyTree(built.publication);
    built.publication.reset(); reclaim->drainOnWorker(); QCOMPARE(ledger.usage().admitted(), std::size_t(0));
}
void TestSemanticPublication::final_drop_defers_destruction_off_caller()
{
    struct Probe { explicit Probe(std::thread::id *at): at(at) {} ~Probe() { *at = std::this_thread::get_id(); } std::thread::id *at; };
    std::thread::id destroyed;
    const auto caller = std::this_thread::get_id(); SemanticMemoryLedger ledger(16384); auto reclaim = std::make_shared<ManualReclaimer>();
    auto reservation = ledger.tryReserve(); auto memory = reservation.resource(SemanticMemoryKind::Payload);
    auto owner = Detail::makeSemanticOwner<Probe>(memory, reclaim, &destroyed); reservation.close(); owner.reset();
    QCOMPARE(destroyed, std::thread::id{}); QVERIFY(ledger.usage().retired > 0);
    reclaim->drainOnWorker(); QVERIFY(destroyed != caller); QVERIFY(destroyed != std::thread::id{});
    memory.reset(); QCOMPARE(ledger.usage().admitted(), std::size_t(0));
}
void TestSemanticPublication::concurrent_reservation_admission_is_bounded()
{
    SemanticMemoryLedger ledger(8192); QSemaphore ready, release; std::atomic<int> admitted{0};
    const auto work = [&] { auto reservation = ledger.tryReserve(4096); if (reservation) ++admitted; ready.release(); release.acquire(); };
    std::thread first(work), second(work); ready.acquire(2);
    QCOMPARE(admitted.load(), 1); QCOMPARE(ledger.usage().reserved, std::size_t(4096));
    release.release(2); first.join(); second.join(); QCOMPARE(ledger.usage().admitted(), std::size_t(0));
    QVERIFY(ledger.usage().peakAdmitted <= std::size_t(8192));
}
void TestSemanticPublication::invalid_or_unavailable_source_is_not_complete()
{
    Source source; SemanticMemoryLedger ledger(1024 * 1024); SemanticIdentitySource ids(19); auto reclaim = std::make_shared<ManualReclaimer>();
    auto missing = SemanticPublicationBuilder::build(source, request(), ids, ledger, reclaim);
    QCOMPARE(missing.status, SemanticBuildStatus::Partial); QVERIFY(missing.diagnostics.sourceUnavailable); QVERIFY(!missing.complete());
    source = simpleSource(); source.throwOnRead = true;
    auto failed = SemanticPublicationBuilder::build(source, request(), ids, ledger, reclaim);
    QCOMPARE(failed.status, SemanticBuildStatus::SourceFailure); QVERIFY(!failed.publication);
    auto invalid = request(); invalid.identity.service = 0;
    QCOMPARE(SemanticPublicationBuilder::build(source, invalid, ids, ledger, reclaim).status, SemanticBuildStatus::InvalidInput);
    missing.publication.reset(); reclaim->drainOnWorker(); QCOMPARE(ledger.usage().admitted(), std::size_t(0));
}
