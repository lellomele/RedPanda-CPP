// SPDX-License-Identifier: GPL-3.0-or-later
#include "src/compiler/compileconfiguration.h"
#include "src/compiler/generatedtextfile.h"
#include <QtTest>
#include <QTemporaryDir>
#include <thread>

using namespace Compilation;

class CompileConfigurationChecks : public QObject {
    Q_OBJECT
private slots:
    void pendingAndConflictingPreparationAreNotUsableRequests() {
        QVERIFY(PreparationResult{}.ready());
        const auto pending = PreparationResult::pending("Waiting for analysis");
        QVERIFY(!pending.ready()); QCOMPARE(pending.status, PreparationStatus::Pending);
        QVERIFY(!pending.reason.isEmpty());
        const auto conflict = PreparationResult::conflict("Source revision changed");
        QVERIFY(!conflict.ready()); QCOMPARE(conflict.status, PreparationStatus::Conflict);
    }
    void generatedMakefileCommitReplacesWholeFile() {
        QTemporaryDir dir;
        const QString target = dir.filePath("Makefile.win");
        { QFile old(target); QVERIFY(old.open(QIODevice::WriteOnly)); QCOMPARE(old.write("previous\n"), qint64(9)); }
        GeneratedTextFile candidate(target, true);
        QVERIFY(candidate.open().succeeded());
        QVERIFY(candidate.appendLine("# Progetto città").succeeded());
        { QFile old(target); QVERIFY(old.open(QIODevice::ReadOnly)); QCOMPARE(old.readAll(), QByteArray("previous\n")); }
        QVERIFY(candidate.appendLine("all: target").succeeded());
        QVERIFY(candidate.commit().succeeded());
        QFile committed(target); QVERIFY(committed.open(QIODevice::ReadOnly));
        QCOMPARE(committed.readAll(), QString("# Progetto città\nall: target\n").toUtf8());
    }
    void failedOrAbandonedMakefilePreservesPreviousVersion() {
        QTemporaryDir dir;
        const QString target = dir.filePath("Makefile.win");
        { QFile old(target); QVERIFY(old.open(QIODevice::WriteOnly)); QCOMPARE(old.write("previous\n"), qint64(9)); }
        {
            GeneratedTextFile abandoned(target, true);
            QVERIFY(abandoned.open().succeeded());
            QVERIFY(abandoned.appendLine("incomplete:").succeeded());
        }
        { QFile old(target); QVERIFY(old.open(QIODevice::ReadOnly)); QCOMPARE(old.readAll(), QByteArray("previous\n")); }
        GeneratedTextFile limited(target, true, 10);
        QVERIFY(limited.open().succeeded());
        QVERIFY(limited.appendLine("first").succeeded());
        const auto failed = limited.appendLine("second");
        QVERIFY(!failed.succeeded()); QVERIFY(!failed.error.isEmpty());
        QVERIFY(!limited.commit().succeeded());
        QFile old(target); QVERIFY(old.open(QIODevice::ReadOnly)); QCOMPARE(old.readAll(), QByteArray("previous\n"));
    }
    void invalidMakefileTargetIsReportedBeforeCommit() {
        QTemporaryDir dir;
        GeneratedTextFile invalid(dir.path(), true);
        QVERIFY(!invalid.open().succeeded());
        QVERIFY(!invalid.commit().succeeded());
        QVERIFY(QFileInfo(dir.path()).isDir());
    }
    void publishedValuesHaveNoMutableAliases() {
        ConfigurationValues source;
        source.toolchain.cCompiler = "original-gcc";
        source.toolchain.compileOptions[CC_CMD_OPT_DEBUG_INFO] = COMPILER_OPTION_ON;
        CompilerOption option{}; option.setting = "-g3";
        source.toolchain.optionCatalogue[CC_CMD_OPT_DEBUG_INFO] = option;
        source.project.emplace(); source.project->includeDirs.append("original-include");
        source.autolink.enabled = true;
        source.autolink.rules["lib.h"] = {"-loriginal", true};
        source.autolink.directIncludes["main.cpp"] = {"/include/lib.h"};
        source.environment.insert("PATH", "original-path");
        source.macros["project"] = "original-project";
        const auto request = FrozenConfiguration::capture(source);
        source.toolchain.cCompiler = "changed-gcc";
        source.toolchain.compileOptions.clear();
        source.toolchain.optionCatalogue[CC_CMD_OPT_DEBUG_INFO].setting = "-g0";
        source.project->includeDirs[0] = "changed-include";
        source.autolink.rules["lib.h"].linkOption = "-lchanged";
        source.autolink.directIncludes.clear();
        source.environment.insert("PATH", "changed-path");
        source.macros["project"] = "changed-project";
        const auto &frozen = request->values();
        QCOMPARE(frozen.toolchain.cCompiler, QString("original-gcc"));
        QCOMPARE(frozen.toolchain.compileOptions.value(CC_CMD_OPT_DEBUG_INFO), QString(COMPILER_OPTION_ON));
        QCOMPARE(frozen.toolchain.optionCatalogue.value(CC_CMD_OPT_DEBUG_INFO).setting, QString("-g3"));
        QCOMPARE(frozen.project->includeDirs, QStringList{"original-include"});
        QCOMPARE(frozen.autolink.resolve("main.cpp").linkOptions, QStringList{"-loriginal"});
        QCOMPARE(frozen.environment.value("PATH"), QString("original-path"));
        QCOMPARE(frozen.macros.value("project"), QString("original-project"));
    }
    void assemblyOverrideOnlyChangesTheLocalPlan() {
        ConfigurationValues source;
        source.toolchain.compileOptions[CC_CMD_OPT_DEBUG_INFO] = COMPILER_OPTION_ON;
        const auto request = FrozenConfiguration::capture(source);
        const auto local = request->values().effectiveCompileOptions(true);
        QVERIFY(!local.contains(CC_CMD_OPT_DEBUG_INFO));
        QCOMPARE(source.toolchain.compileOptions.value(CC_CMD_OPT_DEBUG_INFO), QString(COMPILER_OPTION_ON));
        QCOMPARE(request->values().effectiveCompileOptions().value(CC_CMD_OPT_DEBUG_INFO), QString(COMPILER_OPTION_ON));
    }
    void projectOptionsOverrideAsOneValue() {
        ConfigurationValues source;
        source.toolchain.compileOptions["default"] = "on";
        source.project.emplace();
        QCOMPARE(source.effectiveCompileOptions().value("default"), QString("on"));
        source.project->compilerOptions["project"] = "on";
        const auto effective = source.effectiveCompileOptions();
        QVERIFY(!effective.contains("default"));
        QCOMPARE(effective.value("project"), QString("on"));
    }
    void autolinkKeepsIncludeOrderAndHandlesCycles() {
        AutolinkValues links;
        links.enabled = true;
        links.rules["main.cpp"] = {"-lmain", false};
        links.rules["left.h"] = {"-lleft", false};
        links.rules["right.h"] = {"-lright", true};
        links.directIncludes["main.cpp"] = {"/headers/left.h", "/headers/right.h"};
        links.directIncludes["/headers/right.h"] = {"main.cpp"};
        const auto result = links.resolve("main.cpp");
        QVERIFY(result.complete);
        QCOMPARE(result.linkOptions, (QStringList{"-lmain", "-lright", "-lleft"}));
        QVERIFY(result.forceExecUTF8);
    }
    void incompleteOrExcessiveAnalysisNeverPublishesPartialLinks() {
        AutolinkValues links;
        links.enabled = true; links.complete = false;
        links.rules["main.cpp"] = {"-lpartial", true};
        auto result = links.resolve("main.cpp");
        QVERIFY(!result.complete); QVERIFY(!result.error.isEmpty()); QVERIFY(result.linkOptions.isEmpty());
        links.complete = true; links.nodeBudget = 1;
        links.directIncludes["main.cpp"] = {"other.h"};
        result = links.resolve("main.cpp");
        QVERIFY(!result.complete); QVERIFY(result.linkOptions.isEmpty()); QVERIFY(!result.forceExecUTF8);
        links.nodeBudget = 100; links.edgeBudget = 0;
        result = links.resolve("main.cpp");
        QVERIFY(!result.complete); QVERIFY(result.linkOptions.isEmpty());
    }
    void sharedFrozenConfigurationSurvivesSourceDestruction() {
        FrozenConfiguration::Pointer request;
        {
            ConfigurationValues source;
            source.environment.insert("PATH", "captured");
            source.toolchain.compileOptions[CC_CMD_OPT_DEBUG_INFO] = COMPILER_OPTION_ON;
            request = FrozenConfiguration::capture(source);
        }
        QString workerPath, workerDebug;
        std::thread worker([request, &workerPath, &workerDebug] {
            workerPath = request->values().environment.value("PATH");
            workerDebug = request->values().effectiveCompileOptions().value(CC_CMD_OPT_DEBUG_INFO);
        });
        worker.join();
        QCOMPARE(workerPath, QString("captured"));
        QCOMPARE(workerDebug, QString(COMPILER_OPTION_ON));
    }
};
QTEST_GUILESS_MAIN(CompileConfigurationChecks)
#include "test_compile_configuration.moc"
