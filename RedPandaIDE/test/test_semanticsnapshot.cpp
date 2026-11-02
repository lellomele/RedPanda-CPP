// SPDX-License-Identifier: GPL-3.0-or-later
#include "test_semanticsnapshot.h"
#include "src/analysis/semanticsnapshotbuilder.h"
#include <QSet>
#include <QTest>
#include <algorithm>
#include <type_traits>

using namespace Analysis;

static_assert(std::is_const<FileSnapshotPtr::element_type>::value,
              "Semantic publications must not expose mutable ownership");
static_assert(std::is_const<MacroCatalogPtr::element_type>::value,
              "Macro publications must not expose mutable ownership");

namespace {
const QString Source = QStringLiteral("C:/snapshot/source.cpp");
const QString Header = QStringLiteral("C:/snapshot/header.h");

PStatement symbol(const QString &name, StatementKind kind, const QString &file = Source,
                  int line = 1, PStatement parent = {})
{
    auto statement = std::make_shared<Statement>();
    statement->command = name;
    statement->fullName = parent ? parent->fullName + QStringLiteral("::") + name : name;
    statement->kind = kind;
    statement->scope = parent ? StatementScope::ClassLocal : StatementScope::Global;
    statement->accessibility = StatementAccessibility::Public;
    statement->fileName = file;
    statement->line = line;
    statement->definitionFileName = file;
    statement->definitionLine = line;
    statement->parentScope = parent;
    return statement;
}

void child(const PStatement &parent, const PStatement &value)
{
    parent->children.insert(value->fullName, value);
}

StatementMap map(std::initializer_list<PStatement> statements)
{
    StatementMap result;
    for (const auto &statement : statements)
        result.insert(statement->fullName, statement);
    return result;
}

SnapshotIdentity identity()
{
    SnapshotIdentity result;
    result.serviceId = 17;
    result.jobId = 23;
    result.inputGeneration = 31;
    result.configurationGeneration = 47;
    result.workspaceGeneration = 59;
    result.primaryFile = Source;
    result.sources = {{Source, 3, 11, {}}, {Header, 5, 13, {}}};
    return result;
}

NodeIndex find(const FileSnapshotPtr &snapshot, const QString &name)
{
    for (qsizetype i = 0; i < snapshot->nodes().size(); ++i)
        if (snapshot->nodes()[i].symbol.qualifiedName == name)
            return static_cast<NodeIndex>(i);
    return NoNode;
}

void verifyTree(const FileSnapshotPtr &snapshot)
{
    // Parent-before-child indexing makes cycles impossible and permits a
    // nonrecursive consumer even for deeply nested, malformed input graphs.
    QSet<NodeIndex> reachable;
    QVector<NodeIndex> pending = snapshot->roots();
    for (NodeIndex root : snapshot->roots()) {
        QVERIFY(root < static_cast<quint64>(snapshot->nodes().size()));
        QCOMPARE(snapshot->nodes()[root].parent, NoNode);
    }
    while (!pending.isEmpty()) {
        const NodeIndex index = pending.takeLast();
        QVERIFY(!reachable.contains(index));
        reachable.insert(index);
        const auto &node = snapshot->nodes()[index];
        for (NodeIndex next : node.children) {
            QVERIFY(next < static_cast<quint64>(snapshot->nodes().size()));
            QVERIFY(index < next);
            QCOMPARE(snapshot->nodes()[next].parent, index);
            pending.append(next);
        }
    }
    QCOMPARE(reachable.size(), snapshot->nodes().size());
}
} // namespace

void TestSemanticSnapshot::cycles_are_bounded_and_output_is_acyclic()
{
    auto outer = symbol("Outer", StatementKind::Class);
    auto inner = symbol("Inner", StatementKind::Class, Source, 2, outer);
    child(outer, inner);
    child(inner, outer); // Strong child cycle, independent of weak parent chain.
    const auto childCycle = SemanticSnapshotBuilder::file(identity(), Source, map({outer}));
    QVERIFY(!childCycle->complete());
    QVERIFY(childCycle->diagnostics().cycleDetected);
    QCOMPARE(childCycle->nodes().size(), 2);
    verifyTree(childCycle);
    outer->children.clear();
    inner->children.clear(); // Release the deliberately malformed source graph.

    outer->parentScope = inner;
    const auto parentCycle = SemanticSnapshotBuilder::file(identity(), Source, map({outer}));
    QVERIFY(!parentCycle->complete());
    QVERIFY(parentCycle->diagnostics().cycleDetected);
    QCOMPARE(parentCycle->nodes().size(), 2);
    verifyTree(parentCycle);
}

void TestSemanticSnapshot::reopened_namespaces_and_parent_placeholders()
{
    auto first = symbol("Ns", StatementKind::Namespace, Header, 10);
    auto second = symbol("Ns", StatementKind::Namespace, Source, 20);
    auto a = symbol("a", StatementKind::Variable, Source, 21, first);
    auto b = symbol("b", StatementKind::Variable, Source, 22, second);
    child(first, a);
    child(second, b);
    // Each symbol appears in both the flat file map and its scope's children.
    const auto result = SemanticSnapshotBuilder::file(identity(), Source, map({first, second, a, b}));
    QVERIFY(result->complete());
    QCOMPARE(result->roots().size(), 1);
    QCOMPARE(result->nodes().size(), 3);
    const NodeIndex ns = find(result, "Ns");
    QVERIFY(ns != NoNode);
    QCOMPARE(result->nodes()[ns].children.size(), 2);
    QCOMPARE(result->nodes()[ns].symbol.declaration.filename, Source);
    QVERIFY(!result->nodes()[ns].symbol.contextOnly);
    verifyTree(result);

    auto headerClass = symbol("Imported", StatementKind::Class, Header, 30, first);
    auto method = symbol("method", StatementKind::Function, Header, 31, headerClass);
    method->definitionFileName = Source;
    method->definitionLine = 50;
    auto unrelated = symbol("unrelated", StatementKind::Variable, Header, 32, headerClass);
    child(headerClass, method);
    child(headerClass, unrelated);
    const auto placeholders = SemanticSnapshotBuilder::file(identity(), Source, map({method}));
    QVERIFY(placeholders->complete());
    QCOMPARE(placeholders->nodes().size(), 3);
    QVERIFY(find(placeholders, unrelated->fullName) == NoNode);
    const NodeIndex parent = find(placeholders, headerClass->fullName);
    QVERIFY(parent != NoNode);
    QVERIFY(placeholders->nodes()[parent].symbol.contextOnly);
    const auto &navigation = placeholders->nodes()[find(placeholders, method->fullName)].symbol;
    QCOMPARE(navigation.declaration.filename, Header);
    QCOMPARE(navigation.declaration.line, 31);
    QCOMPARE(navigation.definition.filename, Source);
    QCOMPARE(navigation.definition.line, 50);
    QVERIFY(!navigation.contextOnly);
    verifyTree(placeholders);
}

void TestSemanticSnapshot::inherited_visibility_and_navigation_are_values()
{
    auto type = symbol("Derived", StatementKind::Class);
    auto inherited = symbol("work", StatementKind::Function, Header, 9, type);
    inherited->type = "int";
    inherited->args = "(const Item &item)";
    inherited->noNameArgs = "(const Item &)";
    inherited->templateSpecializationParams = "<Item>";
    inherited->properties = StatementProperty::Inherited | StatementProperty::Virtual
        | StatementProperty::Override | StatementProperty::HasDefinition | StatementProperty::InProject;
    inherited->accessibility = StatementAccessibility::Protected;
    inherited->definitionFileName = Header;
    inherited->definitionLine = 19;
    child(type, inherited);
    auto local = symbol("temporary", StatementKind::Variable, Source, 10, inherited);
    local->scope = StatementScope::Local;
    child(inherited, local);
    auto block = symbol("block", StatementKind::Block, Source, 11, inherited);
    auto lambda = symbol("lambda", StatementKind::Lambda, Source, 12, inherited);
    child(inherited, block);
    child(inherited, lambda);
    const auto result = SemanticSnapshotBuilder::file(identity(), Source, map({type, local, block, lambda}));
    QVERIFY(result->complete());
    QCOMPARE(result->nodes().size(), 2);
    const auto &value = result->nodes()[find(result, inherited->fullName)].symbol;
    QVERIFY(value.hasProperty(StatementProperty::Inherited));
    QVERIFY(value.hasProperty(StatementProperty::Virtual));
    QVERIFY(value.hasProperty(StatementProperty::Override));
    QVERIFY(value.hasProperty(StatementProperty::InProject));
    QCOMPARE(value.accessibility, StatementAccessibility::Protected);
    QCOMPARE(value.type, QString("int"));
    QCOMPARE(value.arguments, QString("(const Item &item)"));
    QCOMPARE(value.signature, QString("(const Item &)"));
    QCOMPARE(value.templateArguments, QString("<Item>"));
    QCOMPARE(value.definition.line, 19);
    QVERIFY(value.contextOnly);
    verifyTree(result);
}

void TestSemanticSnapshot::publication_survives_source_mutation_and_destruction()
{
    FileSnapshotPtr result;
    std::weak_ptr<Statement> weak;
    SnapshotIdentity envelope = identity();
    {
        auto root = symbol("Original", StatementKind::Class);
        auto member = symbol("value", StatementKind::Variable, Source, 7, root);
        member->type = "int";
        member->value = "42";
        member->properties = StatementProperty::Static;
        child(root, member);
        weak = root;
        result = SemanticSnapshotBuilder::file(envelope, Source, map({root}));
        root->command = "Changed";
        member->type = "double";
        member->value = "99";
        member->fileName = Header;
        member->properties = {};
        root->children.clear();
        envelope.sources[0].revision = 99;
    }
    QVERIFY(weak.expired());
    QVERIFY(result->complete());
    QCOMPARE(result->nodes().size(), 2);
    const auto &root = result->nodes()[find(result, "Original")].symbol;
    const auto &member = result->nodes()[find(result, "Original::value")].symbol;
    QCOMPARE(root.name, QString("Original"));
    QCOMPARE(member.type, QString("int"));
    QCOMPARE(member.value, QString("42"));
    QCOMPARE(member.declaration.filename, Source);
    QVERIFY(member.hasProperty(StatementProperty::Static));
    QCOMPARE(result->identity().sources[0].revision, quint64(11));
    auto copy = result->nodes();
    copy[0].symbol.name.clear();
    QVERIFY(!result->nodes()[0].symbol.name.isEmpty());
    verifyTree(result);
}

void TestSemanticSnapshot::macros_and_include_configuration_are_immutable()
{
    auto a = symbol("A", StatementKind::Preprocessor);
    auto b = symbol("B", StatementKind::Preprocessor, Header);
    auto function = symbol("not_macro", StatementKind::Function);
    a->value = "17";
    b->args = "(x)";
    b->value = "((x)+1)";
    IncludeSearchConfiguration includes;
    includes.generation = 41;
    includes.projectDirectories = {"C:/project/first", "C:/project/second", "C:/project/first"};
    includes.systemDirectories = {"C:/toolchain/c++", "C:/toolchain/include"};
    const auto result = SemanticSnapshotBuilder::macros(identity(), map({b, function, a, a}), includes);
    QVERIFY(result->complete());
    QCOMPARE(result->macros().size(), 2);
    QCOMPARE(result->macros()[0].name, QString("A"));
    QCOMPARE(result->macros()[1].name, QString("B"));
    a->value = "0";
    b->args.clear();
    std::reverse(includes.projectDirectories.begin(), includes.projectDirectories.end());
    includes.systemDirectories.clear();
    QCOMPARE(result->macros()[0].value, QString("17"));
    QCOMPARE(result->macros()[1].arguments, QString("(x)"));
    QCOMPARE(result->includeConfiguration().projectDirectories,
             QStringList({"C:/project/first", "C:/project/second", "C:/project/first"}));
    QCOMPARE(result->includeConfiguration().systemDirectories.size(), 2);
    QCOMPARE(result->includeConfiguration().generation, quint64(41));
    QCOMPARE(result->identity().jobId, quint64(23));
}

void TestSemanticSnapshot::build_limits_report_partial_results()
{
    auto root = symbol("Root", StatementKind::Class);
    auto nested = symbol("Nested", StatementKind::Class, Source, 2, root);
    auto leaf = symbol("leaf", StatementKind::Variable, Source, 3, nested);
    child(root, nested);
    child(nested, leaf);
    SnapshotBuildLimits limits;
    limits.maximumNodes = 1;
    const auto nodeLimited = SemanticSnapshotBuilder::file(identity(), Source, map({root}), limits);
    QVERIFY(!nodeLimited->complete());
    QVERIFY(nodeLimited->diagnostics().nodeLimitReached);
    QCOMPARE(nodeLimited->nodes().size(), 1);
    verifyTree(nodeLimited);

    limits = {};
    limits.maximumSourceStatements = 1;
    const auto sourceLimited = SemanticSnapshotBuilder::file(identity(), Source, map({root, nested, leaf}), limits);
    QVERIFY(!sourceLimited->complete());
    QVERIFY(sourceLimited->diagnostics().sourceLimitReached);
    QVERIFY(sourceLimited->diagnostics().visitedStatements <= quint64(1));
    verifyTree(sourceLimited);

    limits = {};
    limits.maximumParentDepth = 1;
    const auto depthLimited = SemanticSnapshotBuilder::file(identity(), Source, map({leaf}), limits);
    QVERIFY(!depthLimited->complete());
    QVERIFY(depthLimited->diagnostics().parentDepthLimited);
    QVERIFY(depthLimited->nodes().size() <= 2);
    verifyTree(depthLimited);

    limits = {};
    limits.maximumNodes = 0;
    const auto empty = SemanticSnapshotBuilder::file(identity(), Source, map({root}), limits);
    QVERIFY(!empty->complete());
    QVERIFY(empty->nodes().isEmpty());
    auto macro = symbol("MACRO", StatementKind::Preprocessor);
    const auto noMacros = SemanticSnapshotBuilder::macros(identity(), map({macro}), {}, limits);
    QVERIFY(!noMacros->complete());
    QVERIFY(noMacros->diagnostics().nodeLimitReached);
    QVERIFY(noMacros->macros().isEmpty());
}

void TestSemanticSnapshot::input_identity_copies_revision_manifest()
{
    DocumentStore store;
    const auto sourceId = store.add(Source, [](QStringList &lines) { lines = {"int original;"}; return true; });
    const auto headerId = store.add(Header, [](QStringList &lines) { lines = {}; return true; });
    const auto captured = store.capture();
    QVERIFY(captured.succeeded());
    const auto result = SemanticSnapshotBuilder::inputIdentity(captured.snapshot, 2, 3, 5, 7, Source);
    QVERIFY(store.changed(sourceId));
    QVERIFY(store.renamed(headerId, Header + ".renamed"));
    QCOMPARE(result.serviceId, quint64(2));
    QCOMPARE(result.jobId, quint64(3));
    QCOMPARE(result.configurationGeneration, quint64(5));
    QCOMPARE(result.workspaceGeneration, quint64(7));
    QCOMPARE(result.inputGeneration, captured.snapshot->generation());
    QCOMPARE(result.primaryFile, Source);
    QCOMPARE(result.sources.size(), 2);
    QCOMPARE(result.sources[0].filename, Header);
    QCOMPARE(result.sources[0].documentId, headerId);
    QCOMPARE(result.sources[0].revision, quint64(1));
    QCOMPARE(result.sources[1].filename, Source);
    QCOMPARE(result.sources[1].documentId, sourceId);
    QCOMPARE(result.sources[1].revision, quint64(1));
}

void TestSemanticSnapshot::dense_shared_graph_has_a_cumulative_work_budget()
{
    auto root = symbol("Dense", StatementKind::Namespace);
    auto shared = symbol("shared", StatementKind::Variable, Source, 10, root);
    for (int scope = 0; scope < 3; ++scope) {
        auto type = symbol(QString("Scope%1").arg(scope), StatementKind::Class, Source, 2 + scope, root);
        child(root, type);
        for (int repeat = 0; repeat < 32; ++repeat)
            child(type, shared); // Few unique symbols, many repeated/shared edges.
    }
    SnapshotBuildLimits limits;
    limits.maximumWorkItems = 24;
    const auto partial = SemanticSnapshotBuilder::file(identity(), Source, map({root}), limits);
    const auto &work = partial->diagnostics();
    QVERIFY(!partial->complete());
    QVERIFY(work.workLimitReached);
    QVERIFY(!work.nodeLimitReached && !work.sourceLimitReached && !work.pendingLimitReached);
    QCOMPARE(work.workItems, limits.maximumWorkItems);
    QVERIFY(work.sourceEntriesExamined > work.visitedStatements);
    QVERIFY(work.scheduledFrames <= work.workItems);
    QVERIFY(work.peakPendingFrames <= work.scheduledFrames);
    // Exhaustion must stop globally, rather than continue draining a worklist
    // and repeatedly scanning children after pushMap() reaches the budget.
    QVERIFY(work.processedFrames < work.scheduledFrames);
    QVERIFY(work.visitedStatements < quint64(5));
    verifyTree(partial);

    limits.maximumWorkItems = 1000;
    const auto complete = SemanticSnapshotBuilder::file(identity(), Source, map({root}), limits);
    QVERIFY(complete->complete());
    QCOMPARE(complete->nodes().size(), 5);
    QCOMPARE(complete->diagnostics().visitedStatements, quint64(5));
    QVERIFY(complete->diagnostics().sourceEntriesExamined >= quint64(100));
    QVERIFY(complete->diagnostics().scheduledFrames <= complete->diagnostics().workItems);
    verifyTree(complete);
}

void TestSemanticSnapshot::pending_and_macro_scans_have_independent_bounds()
{
    auto repeated = symbol("Repeated", StatementKind::Variable);
    StatementMap roots;
    for (int i = 0; i < 8; ++i)
        roots.insert(repeated->fullName, repeated);
    SnapshotBuildLimits limits;
    limits.maximumPendingFrames = 4;
    const auto pendingLimited = SemanticSnapshotBuilder::file(identity(), Source, roots, limits);
    QVERIFY(!pendingLimited->complete());
    QVERIFY(pendingLimited->diagnostics().pendingLimitReached);
    QCOMPARE(pendingLimited->diagnostics().peakPendingFrames, quint64(4));
    QCOMPARE(pendingLimited->diagnostics().scheduledFrames, quint64(4));
    QCOMPARE(pendingLimited->diagnostics().processedFrames, quint64(0));
    QVERIFY(pendingLimited->nodes().isEmpty());

    StatementMap nulls;
    for (int i = 0; i < 8; ++i)
        nulls.insert(QString::number(i), {});
    limits = {};
    limits.maximumWorkItems = 3;
    const auto macros = SemanticSnapshotBuilder::macros(identity(), nulls, {}, limits);
    QVERIFY(!macros->complete());
    QVERIFY(macros->diagnostics().workLimitReached);
    QCOMPARE(macros->diagnostics().workItems, quint64(3));
    QCOMPARE(macros->diagnostics().sourceEntriesExamined, quint64(3));
    QCOMPARE(macros->diagnostics().visitedStatements, quint64(0));
    QVERIFY(macros->macros().isEmpty());
}
