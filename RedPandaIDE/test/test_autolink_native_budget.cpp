// SPDX-License-Identifier: GPL-3.0-or-later
#include "src/compiler/compileconfiguration.h"
#include "src/compiler/preparationconfigurationkey.h"
#include <QCoreApplication>
#include <QTest>
using namespace Compilation;

class AutolinkNativeBudgetChecks : public QObject {
    Q_OBJECT
private slots:
    void native_suffix_distinguishes_kelvin_sign_and_preserves_native_ascii_case() {
        AutolinkValues value; value.enabled = true;
        value.rules["k.h"] = {"-lk", false};
        QVERIFY(value.resolve(QStringLiteral("/include/\u212a.h")).linkOptions.isEmpty());
        QCOMPARE(value.resolve("/include/k.h").linkOptions, QStringList{"-lk"});
#ifdef Q_OS_WIN
        QCOMPARE(value.resolve("/include/K.h").linkOptions, QStringList{"-lk"});
#else
        QVERIFY(value.resolve("/include/K.h").linkOptions.isEmpty());
#endif
    }
    void suffix_keeps_legacy_segment_boundary_and_rule_order() {
        AutolinkValues value; value.enabled = true;
        value.rules["dir/file.h"] = {"-ldir", false};
        value.rules["file.h"] = {"-lshort", false};
        QCOMPARE(value.resolve("/x/dir/file.h").linkOptions, QStringList{"-ldir"});
        QCOMPARE(value.resolve("/x/notdir/file.h").linkOptions, QStringList{"-lshort"});
        QVERIFY(value.resolve("/x/somefile.h").linkOptions.isEmpty());
        QCOMPARE(value.resolve("file.h").linkOptions, QStringList{"-lshort"});
    }
    void long_rule_work_is_cumulative_and_discards_partial_flags() {
        AutolinkValues value; value.enabled = true;
        const auto stem = QString(256, QLatin1Char('a'));
        value.rules[stem + ".hpp"] = {"-llong", false};
        value.rules["small.h"] = {"-lsmall", true};
        value.directIncludes["/source.cpp"] = {"/a/" + stem + ".hxx", "/b/" + stem + ".hxx", "/inc/small.h"};
        value.ruleSuffixCodeUnitBudget = 3500;
        const auto result = value.resolve("/source.cpp");
        QVERIFY(!result.complete); QVERIFY(result.error.contains("text-work"));
        QVERIFY(result.ruleSuffixCodeUnitsExamined > 0);
        QVERIFY(result.ruleSuffixCodeUnitsExamined <= value.ruleSuffixCodeUnitBudget);
        QVERIFY(result.linkOptions.isEmpty()); QVERIFY(!result.forceExecUTF8);
    }
    void negative_budget_is_invalid_and_exact_rule_needs_no_suffix_work() {
        AutolinkValues value; value.enabled = true; value.rules["exact.h"] = {"-lexact", false};
        value.ruleSuffixCodeUnitBudget = 0;
        const auto exact = value.resolve("exact.h");
        QVERIFY(exact.complete); QCOMPARE(exact.linkOptions, QStringList{"-lexact"});
        QCOMPARE(exact.ruleSuffixCodeUnitsExamined, qsizetype(0));
        QVERIFY(!value.resolve("/include/exact.h").complete);
        value.ruleSuffixCodeUnitBudget = -1;
        QVERIFY(!value.resolve("exact.h").complete);
    }
    void preparation_guard_includes_the_new_work_budget() {
        ConfigurationValues value;
        const auto before = preparationConfigurationKey(value);
        --value.autolink.ruleSuffixCodeUnitBudget;
        QVERIFY(preparationConfigurationKey(value) != before);
    }
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv); AutolinkNativeBudgetChecks checks;
    return QTest::qExec(&checks, argc, argv);
}
#include "test_autolink_native_budget.moc"
