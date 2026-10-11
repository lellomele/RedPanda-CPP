#include <QTest>
#include <QAbstractItemModelTester>
#include <QComboBox>
#include <QPersistentModelIndex>
#include <QSignalSpy>
#include "test_search_models.h"
#include "src/widgets/searchresultview.h"

namespace {

PSearchResults addHistory(SearchResultModel &model, const QString &keyword)
{
    return model.addSearchResults(keyword, QSynedit::ssoMatchCase, false, SearchFileScope::Folder);
}

PSearchResultTreeItem fileResult(const QString &filename, int matches)
{
    auto file = std::make_shared<SearchResultTreeItem>();
    file->filename = filename;
    file->parent = nullptr;
    for (int i = 0; i < matches; ++i) {
        auto result = std::make_shared<SearchResultTreeItem>();
        result->filename = filename;
        result->line = i;
        result->start = i;
        result->len = 1;
        result->text = QString("match %1").arg(i);
        result->selected = false;
        result->parent = file.get();
        file->results.append(result);
    }
    return file;
}

} // namespace

void TestSearchModels::history_combobox_reentrancy_has_balanced_resets()
{
    SearchResultModel model;
    QAbstractItemModelTester historyTester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    QAbstractItemModelTester treeTester(model.treeModel(), QAbstractItemModelTester::FailureReportingMode::QtTest);
    QComboBox history;
    history.setModel(&model);
    connect(&history, QOverload<int>::of(&QComboBox::currentIndexChanged), &model, &SearchResultModel::setCurrentIndex);
    connect(&model, &SearchResultModel::currentIndexChanged, &history, [&] {
        history.setCurrentIndex(model.currentIndex());
    });
    int depth = 0;
    bool nestedReset = false;
    connect(model.treeModel(), &QAbstractItemModel::modelAboutToBeReset, [&] {
        nestedReset = nestedReset || depth != 0;
        ++depth;
    });
    connect(model.treeModel(), &QAbstractItemModel::modelReset, [&] { --depth; });
    QSignalSpy begin(model.treeModel(), &QAbstractItemModel::modelAboutToBeReset);
    QSignalSpy end(model.treeModel(), &QAbstractItemModel::modelReset);
    for (int i = 0; i < MAX_SEARCH_RESULTS + 5; ++i) {
        auto result = addHistory(model, QString("search-%1").arg(i));
        model.addResultToSearchResults(result, fileResult(QString("file-%1.cpp").arg(i), 2));
        QCOMPARE(model.currentIndex(), 0);
        QCOMPARE(history.currentIndex(), 0);
        QCOMPARE(model.currentResults(), result);
        history.setCurrentIndex(qMin(1, model.rowCount() - 1));
        QCOMPARE(model.currentIndex(), history.currentIndex());
    }
    model.removeSearchResults(0);
    model.clear();
    QCOMPARE(depth, 0);
    QVERIFY(!nestedReset);
    QCOMPARE(begin.count(), end.count());
    QCOMPARE(model.currentIndex(), -1);
    QCOMPARE(history.currentIndex(), -1);
}

void TestSearchModels::tree_parents_use_file_rows()
{
    SearchResultModel model;
    auto results = addHistory(model, "symbol");
    auto *tree = model.treeModel();
    QAbstractItemModelTester tester(tree, QAbstractItemModelTester::FailureReportingMode::QtTest);
    const QList<int> counts{3, 1, 5};
    for (int file = 0; file < counts.size(); ++file)
        model.addResultToSearchResults(results, fileResult(QString("file-%1.cpp").arg(file), counts[file]));
    QCOMPARE(tree->rowCount(), counts.size());
    for (int file = 0; file < counts.size(); ++file) {
        const auto parent = tree->index(file, 0);
        QVERIFY(parent.isValid());
        QVERIFY(!tree->parent(parent).isValid());
        QCOMPARE(tree->rowCount(parent), counts[file]);
        for (int match = 0; match < counts[file]; ++match) {
            const auto child = tree->index(match, 0, parent);
            QVERIFY(child.isValid());
            QCOMPARE(tree->parent(child), parent);
            QCOMPARE(tree->rowCount(child), 0);
            QString filename;
            int line = -1;
            int start = -1;
            QVERIFY(tree->getItemFileAndLineChar(child, filename, line, start));
            QCOMPARE(filename, QString("file-%1.cpp").arg(file));
            QCOMPARE(line, match);
            QCOMPARE(start, match);
        }
    }
}

void TestSearchModels::removing_history_preserves_selection_data()
{
    QTest::addColumn<int>("selected");
    QTest::addColumn<int>("removed");
    QTest::addColumn<QString>("expected");
    QTest::newRow("current-first-remove-current") << 0 << 0 << QString("second");
    QTest::newRow("current-first-remove-middle") << 0 << 1 << QString("third");
    QTest::newRow("current-first-remove-last") << 0 << 2 << QString("third");
    QTest::newRow("current-middle-remove-first") << 1 << 0 << QString("second");
    QTest::newRow("current-middle-remove-current") << 1 << 1 << QString("first");
    QTest::newRow("current-middle-remove-last") << 1 << 2 << QString("second");
    QTest::newRow("current-last-remove-first") << 2 << 0 << QString("first");
    QTest::newRow("current-last-remove-middle") << 2 << 1 << QString("first");
    QTest::newRow("current-last-remove-current") << 2 << 2 << QString("second");
}

void TestSearchModels::removing_history_preserves_selection()
{
    QFETCH(int, selected);
    QFETCH(int, removed);
    QFETCH(QString, expected);
    SearchResultModel model;
    QAbstractItemModelTester historyTester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    QAbstractItemModelTester treeTester(model.treeModel(), QAbstractItemModelTester::FailureReportingMode::QtTest);
    for (const QString &keyword : {QString("first"), QString("second"), QString("third")}) {
        auto result = addHistory(model, keyword);
        model.addResultToSearchResults(result, fileResult(keyword + ".cpp", 2));
    }
    model.setCurrentIndex(selected);
    model.removeSearchResults(removed);
    QCOMPARE(model.resultsCount(), 2);
    QVERIFY(model.currentResults());
    QCOMPARE(model.currentResults()->keyword(), expected);
    QCOMPARE(model.treeModel()->rowCount(), 1);
    QVERIFY(model.treeModel()->data(model.treeModel()->index(0, 0)).toString().startsWith(expected));
}

void TestSearchModels::invalid_indexes_and_clear_are_safe()
{
    SearchResultModel model;
    auto result = addHistory(model, "one");
    model.addResultToSearchResults(result, fileResult("one.cpp", 1));
    auto *tree = model.treeModel();
    QPersistentModelIndex file(tree->index(0, 0));
    QPersistentModelIndex child(tree->index(0, 0, file));
    QVERIFY(file.isValid());
    QVERIFY(child.isValid());
    model.removeSearchResults(-1);
    model.removeSearchResults(100);
    model.setCurrentIndex(-1);
    model.setCurrentIndex(100);
    QCOMPARE(model.currentResults(), result);
    QCOMPARE(model.resultsCount(), 1);
    QVERIFY(!result->result(-1));
    QVERIFY(!result->result(100));
    QCOMPARE(tree->flags({}), Qt::NoItemFlags);
    QVERIFY(!tree->data({}).isValid());
    QSignalSpy changed(&model, &SearchResultModel::currentIndexChanged);
    model.clear();
    QVERIFY(!file.isValid());
    QVERIFY(!child.isValid());
    QCOMPARE(model.currentIndex(), -1);
    QCOMPARE(model.resultsCount(), 0);
    QCOMPARE(tree->rowCount(), 0);
    QCOMPARE(changed.count(), 1);
    model.clear();
    QCOMPARE(tree->rowCount(), 0);
    QCOMPARE(changed.count(), 2);
    auto recovered = addHistory(model, "recovered");
    model.addResultToSearchResults(recovered, fileResult("recovered.cpp", 1));
    QCOMPARE(tree->rowCount(), 1);
}

void TestSearchModels::switching_history_invalidates_old_tree_indexes()
{
    SearchResultModel model;
    auto first = addHistory(model, "first");
    model.addResultToSearchResults(first, fileResult("first.cpp", 3));
    auto second = addHistory(model, "second");
    model.addResultToSearchResults(second, fileResult("second.cpp", 1));
    auto *tree = model.treeModel();
    QPersistentModelIndex index(tree->index(0, 0));
    model.setCurrentIndex(1);
    QVERIFY(!index.isValid());
    QCOMPARE(model.currentResults(), first);
    QCOMPARE(tree->rowCount(tree->index(0, 0)), 3);
    model.setCurrentIndex(0);
    QCOMPARE(model.currentResults(), second);
    QCOMPARE(tree->rowCount(tree->index(0, 0)), 1);
}

void TestSearchModels::checkboxes_emit_change_notifications()
{
    SearchResultModel model;
    auto result = addHistory(model, "one");
    model.addResultToSearchResults(result, fileResult("one.cpp", 2));
    auto *tree = model.treeModel();
    QAbstractItemModelTester tester(tree, QAbstractItemModelTester::FailureReportingMode::QtTest);
    tree->setSelectable(true);
    const auto parent = tree->index(0, 0);
    const auto child = tree->index(0, 0, parent);
    QVERIFY(!tree->flags(parent).testFlag(Qt::ItemIsUserCheckable));
    QVERIFY(tree->flags(child).testFlag(Qt::ItemIsUserCheckable));
    QCOMPARE(tree->data(child, Qt::CheckStateRole).toInt(), int(Qt::Checked));
    QSignalSpy changed(tree, &QAbstractItemModel::dataChanged);
    QVERIFY(tree->setData(child, Qt::Unchecked, Qt::CheckStateRole));
    QCOMPARE(tree->data(child, Qt::CheckStateRole).toInt(), int(Qt::Unchecked));
    QCOMPARE(changed.count(), 1);
    QVERIFY(tree->setData(child, Qt::Unchecked, Qt::CheckStateRole));
    QCOMPARE(changed.count(), 1);
    QVERIFY(!tree->setData(parent, Qt::Checked, Qt::CheckStateRole));
    tree->setSelectable(false);
    const auto disabledChild = tree->index(0, 0, tree->index(0, 0));
    QVERIFY(!tree->flags(disabledChild).testFlag(Qt::ItemIsUserCheckable));
    QVERIFY(!tree->setData(disabledChild, Qt::Checked, Qt::CheckStateRole));
}

void TestSearchModels::null_results_and_children_are_rejected()
{
    SearchResultModel model;
    auto result = addHistory(model, "one");
    model.addResultToSearchResults({}, {});
    model.addResultToSearchResults(result, {});
    QCOMPARE(model.treeModel()->rowCount(), 0);
    auto file = fileResult("one.cpp", 2);
    file->results.prepend(PSearchResultTreeItem{});
    file->results[1]->parent = nullptr;
    model.addResultToSearchResults(result, file);
    QCOMPARE(model.treeModel()->rowCount(), 1);
    QCOMPARE(model.treeModel()->rowCount(model.treeModel()->index(0, 0)), 2);
    QCOMPARE(file->results[0]->parent, file.get());
    QAbstractItemModelTester tester(model.treeModel(), QAbstractItemModelTester::FailureReportingMode::QtTest);
}

void TestSearchModels::history_limit_and_duplicate_replacement()
{
    SearchResultModel model;
    auto detached = addHistory(model, "detached");
    for (int i = 0; i < MAX_SEARCH_RESULTS + 5; ++i)
        addHistory(model, QString::number(i));
    QCOMPARE(model.resultsCount(), MAX_SEARCH_RESULTS);
    QCOMPARE(model.currentResults()->keyword(), QString::number(MAX_SEARCH_RESULTS + 4));
    QSignalSpy inserted(model.treeModel(), &QAbstractItemModel::rowsInserted);
    model.addResultToSearchResults(detached, fileResult("detached.cpp", 2));
    QCOMPARE(inserted.count(), 0);
    QCOMPARE(model.treeModel()->rowCount(), 0);
    auto duplicate = addHistory(model, QString::number(MAX_SEARCH_RESULTS + 4));
    QCOMPARE(model.resultsCount(), MAX_SEARCH_RESULTS);
    QCOMPARE(model.currentResults(), duplicate);
    QCOMPARE(model.currentIndex(), 0);
}

void TestSearchModels::occurrence_history_replacement_is_consistent()
{
    SearchResultModel model;
    QAbstractItemModelTester historyTester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    QAbstractItemModelTester treeTester(model.treeModel(), QAbstractItemModelTester::FailureReportingMode::QtTest);
    auto first = model.addSearchResults("before", "scope", SearchFileScope::wholeProject);
    model.addResultToSearchResults(first, fileResult("before.cpp", 2));
    auto replacement = model.addSearchResults("after", "scope", SearchFileScope::wholeProject);
    QCOMPARE(model.resultsCount(), 1);
    QCOMPARE(model.currentResults(), replacement);
    QCOMPARE(model.treeModel()->rowCount(), 0);
    model.removeSearchResults(0);
    QCOMPARE(model.currentIndex(), -1);
    QCOMPARE(model.resultsCount(), 0);
}
