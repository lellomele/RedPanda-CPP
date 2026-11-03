// SPDX-License-Identifier: GPL-3.0-or-later
#include "src/compiler/boundedprocessprobe.h"
#include "src/compiler/frozencompilearguments.h"
#include "src/compiler/filecompilerequest.h"
#include "src/compiler/toolchaincapabilities.h"
#include "src/compiler/preparationconfigurationkey.h"
#include "test/test_containedprocess.h"
#include <QCoreApplication>
#include <QFile>
#include <QTest>
#include <QThread>
#include <QStringDecoder>
#include <QStringEncoder>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <thread>
using namespace Compilation;

class FrozenCompilationChecks : public QObject {
    Q_OBJECT
private:
    static CompilerOption option(QString setting, CompilerOptionType type, bool linker = false) {
        CompilerOption value{};
        value.setting = std::move(setting); value.type = type;
        value.isC = true; value.isCpp = true; value.isLinker = linker;
        return value;
    }
    static ProcessProbeRequest request(const QString &mode) {
        ProcessProbeRequest value;
        value.executable = QCoreApplication::applicationFilePath();
        value.arguments = {QStringLiteral("--probe-fixture"), mode};
        value.environment = QProcessEnvironment::systemEnvironment();
        return value;
    }
    static ProcessProbeResult run(const ProcessProbeRequest &value, std::atomic_bool &cancel) {
        ProcessProbeResult result;
        std::thread worker([&] { result = runBoundedProcessProbe(value, cancel); });
        worker.join();
        return result;
    }
private slots:
    void assembly_options_are_local_and_catalogue_expands_values() {
        ConfigurationValues live;
        live.toolchain.compileOptions = {{CC_CMD_OPT_DEBUG_INFO, "on"}, {"number", "4"}, {"choice", "c++17"}};
        live.toolchain.optionCatalogue[CC_CMD_OPT_DEBUG_INFO] = option("-g3", CompilerOptionType::Checkbox);
        auto numeric = option("-Wl,--stack,", CompilerOptionType::Number); numeric.scale = 1024;
        live.toolchain.optionCatalogue["number"] = numeric;
        live.toolchain.optionCatalogue["choice"] = option("-std=", CompilerOptionType::Choice);
        const auto captured = FrozenConfiguration::capture(live);
        live.toolchain.compileOptions[CC_CMD_OPT_DEBUG_INFO] = "off";
        const auto asmFlags = optionArguments(captured->values(), ArgumentUse::Cpp, false, true);
        QVERIFY(!asmFlags.contains("-g3"));
        QVERIFY(asmFlags.contains("-Wl,--stack,4096"));
        QVERIFY(asmFlags.contains("-std=c++17"));
        QVERIFY(optionArguments(captured->values(), ArgumentUse::Cpp, false).contains("-g3"));
        QCOMPARE(live.toolchain.compileOptions.value(CC_CMD_OPT_DEBUG_INFO), QString("off"));
    }
    void linker_order_and_incomplete_dependency_outcome_are_explicit() {
        ConfigurationValues config;
        config.toolchain.libDirs = {"tool-lib"}; config.toolchain.staticLink = true;
        config.toolchain.compileOptions["link"] = "on";
        config.toolchain.optionCatalogue["link"] = option("-Wl,tool", CompilerOptionType::Checkbox, true);
        config.toolchain.useCustomLinkParams = true; config.toolchain.customLinkParams = "-lglobal";
        config.project.emplace(); config.project->libDirs = {"project-lib"};
        config.project->guiApplication = true; config.project->linkerCommand = "-lproject";
        config.project->staticLink = true;
        config.autolink.enabled = true;
        AutolinkResolution resolved; resolved.linkOptions = {"-lfirst -lsecond"};
        const auto result = libraryArguments(config, resolved, true);
        QVERIFY(result.preparation.ready());
        QCOMPARE(result.arguments, (QStringList{"-Ltool-lib", "-Lproject-lib", "-lfirst", "-lsecond",
                 "-Wl,tool", "-lglobal", "-mwindows", "-lproject", "-static"}));
        resolved.complete = false; resolved.error = "analysis not ready";
        const auto rejected = libraryArguments(config, resolved, true);
        QVERIFY(!rejected.preparation.ready()); QVERIFY(rejected.arguments.isEmpty());
        QCOMPARE(rejected.preparation.reason, QString("analysis not ready"));
    }
    void charset_and_process_environment_are_captured_values() {
        ConfigurationValues live;
        live.preferences.systemEncoding = "windows-1252"; live.preferences.consoleEncoding = "IBM850";
        live.toolchain.supportConvertingCharset = true; live.toolchain.autoAddCharsetParams = true;
        live.toolchain.execCharset = ENCODING_OEM_DEFAULT;
        live.toolchain.supportNLS = true; live.toolchain.forceEnglishOutput = true;
        live.toolchain.binDirs = {"tool-bin"}; live.environment.insert("PATH", "captured-path");
        const auto captured = FrozenConfiguration::capture(live);
        live.preferences.systemEncoding = "UTF-8"; live.environment.insert("PATH", "new-path");
        QCOMPARE(charsetArguments(captured->values(), ENCODING_SYSTEM_DEFAULT, false, false),
                 (QStringList{"-finput-charset=windows-1252", "-fexec-charset=IBM850"}));
        QCOMPARE(charsetArguments(captured->values(), ENCODING_UTF8_BOM, true, false), QStringList{"-finput-charset=UTF-8"});
        QVERIFY(charsetArguments(captured->values(), ENCODING_ASCII, false, true).isEmpty());
        const auto environment = processEnvironment(captured->values(), "compiler-dir", ';');
        QVERIFY(environment.value("PATH").startsWith("compiler-dir;"));
        QVERIFY(environment.value("PATH").endsWith("captured-path"));
        QCOMPARE(environment.value("LC_ALL"), QString("C"));
        QVERIFY(environment.value("CFLAGS").isEmpty());
    }
    void probe_rejects_gui_thread_execution() {
        std::atomic_bool cancel{false};
        const auto result = runBoundedProcessProbe(request("environment"), cancel);
        QVERIFY(!result.preparation.ready());
    }
    void probe_uses_captured_environment_and_returns_output() {
        auto value = request("environment"); value.environment.insert("REDPANDA_PROBE_FIXTURE", "captured");
        std::atomic_bool cancel{false};
        const auto result = run(value, cancel);
        QVERIFY2(result.preparation.ready(), qPrintable(result.preparation.reason));
        QCOMPARE(result.output, QByteArray("captured")); QCOMPARE(result.exitCode, 0);
    }
    void probe_bounds_output_and_deadline() {
        std::atomic_bool cancel{false};
        auto value = request("flood"); value.maximumOutputBytes = 1024;
        auto result = run(value, cancel);
        QVERIFY(!result.preparation.ready()); QVERIFY(result.output.size() <= 1024);
        value = request("wait"); value.deadlineMilliseconds = 50;
        QElapsedTimer time; time.start(); result = run(value, cancel);
        QVERIFY(!result.preparation.ready()); QVERIFY(time.elapsed() < 4000);
        QVERIFY(result.preparation.reason.contains("deadline"));
    }
    void probe_cancellation_is_a_distinct_terminal_outcome() {
        std::atomic_bool cancel{false}; ProcessProbeResult result;
        const auto value = request("wait");
        std::thread worker([&] { result = runBoundedProcessProbe(value, cancel); });
        QTest::qWait(30); cancel.store(true, std::memory_order_release); worker.join();
        QCOMPARE(result.preparation.status, PreparationStatus::Cancelled);
        result = run(value, cancel);
        QCOMPARE(result.preparation.status, PreparationStatus::Cancelled);
    }
    void preparation_mailbox_delivers_only_one_immutable_result() {
        ConfigurationValues values; values.toolchain.cCompiler = "captured-compiler";
        PreparationMailbox mailbox; std::atomic_bool cancel{false}; PreparationDelivery received;
        std::thread worker([&] { received = mailbox.wait(1000, cancel); });
        const auto original = FrozenConfiguration::capture(values);
        const bool firstAccepted = mailbox.deliver({{}, original});
        values.toolchain.cCompiler = "edited-compiler";
        const bool secondAccepted = mailbox.deliver({{}, FrozenConfiguration::capture(values)});
        worker.join();
        QVERIFY(firstAccepted); QVERIFY(!secondAccepted);
        QVERIFY(received.preparation.ready());
        QCOMPARE(received.configuration->values().toolchain.cCompiler, QString("captured-compiler"));
    }
    void preparation_mailbox_deadline_and_cancel_reject_late_results() {
        std::atomic_bool cancel{false};
        PreparationMailbox expired; PreparationDelivery received;
        std::thread timed([&] { received = expired.wait(20, cancel); }); timed.join();
        QVERIFY(!received.preparation.ready()); QVERIFY(received.preparation.reason.contains("deadline"));
        QVERIFY(!expired.deliver({{}, FrozenConfiguration::capture({})}));
        PreparationMailbox stopped;
        std::thread worker([&] { received = stopped.wait(5000, cancel); });
        stopped.cancel(); worker.join();
        QCOMPARE(received.preparation.status, PreparationStatus::Cancelled);
        QVERIFY(!stopped.deliver({{}, FrozenConfiguration::capture({})}));
    }
    void file_request_retains_input_and_revision_after_caller_changes() {
        FileCompileValues source;
        source.identity = {17, 19, 23, 31, 41, 53};
        source.openDocumentRevisions.insert(31, 41);
        source.inputMode = FileInputMode::StandardInput;
        source.standardInput = "int captured;"; source.filename = "source.cpp";
        ConfigurationValues config; config.toolchain.cCompiler = "captured-gcc";
        source.configuration = FrozenConfiguration::capture(config);
        const auto submitted = FileCompileRequest::capture(source);
        source.standardInput = "int changed;"; source.identity.textRevision = 99;
        source.openDocumentRevisions[31] = 99;
        config.toolchain.cCompiler = "new-gcc";
        QCOMPARE(submitted->values().standardInput, QString("int captured;"));
        QCOMPARE(submitted->values().identity.textRevision, quint64(41));
        QCOMPARE(submitted->values().openDocumentRevisions.value(31), quint64(41));
        QCOMPARE(submitted->values().configuration->values().toolchain.cCompiler, QString("captured-gcc"));
    }
    void malformed_binary_codec_probes_do_not_follow_unchecked_offsets() {
        QVERIFY(!peImportsUnicodeArguments({}));
        QVERIFY(!peImportsUnicodeArguments(QByteArray("not a PE file")));
        QByteArray truncated(64, '\0'); truncated[0] = 'M'; truncated[1] = 'Z';
        for (int i = 0; i < 4; ++i) truncated[0x3c + i] = char(0xff);
        QVERIFY(!peImportsUnicodeArguments(truncated));
        for (int size = 0; size < 1024; size += 17) {
            QByteArray candidate(size, '\0');
            for (int i = 0; i < size; ++i) candidate[i] = char((i * 137 + size) & 255);
            QVERIFY(!peImportsUnicodeArguments(candidate));
        }
    }
    void real_gcc_capabilities_use_the_worker_probe() {
        ConfigurationValues config;
        config.toolchain.compilerType = CompilerType::GCC;
        config.toolchain.cCompiler = qEnvironmentVariable("REDPANDA_QA_COMPILER");
        if (config.toolchain.cCompiler.isEmpty()) config.toolchain.cCompiler = QStandardPaths::findExecutable("gcc");
        if (config.toolchain.cCompiler.isEmpty()) QSKIP("An explicit QA GCC or gcc in PATH is required.");
        config.preferences.systemEncoding = "UTF-8";
        config.environment = QProcessEnvironment::systemEnvironment();
        std::atomic_bool cancel{false}; CapabilityProbeResult result;
        std::thread worker([&] { result = probeToolchainCapabilities(config, cancel); }); worker.join();
        QVERIFY2(result.preparation.ready(), qPrintable(result.preparation.reason));
#ifdef Q_OS_WIN
        QCOMPARE(result.ownership, ProcessOwnershipLevel::WindowsTreeQuiescent);
#endif
    }
    void ansi_codecs_and_split_utf8_are_supported_by_the_target_qt() {
        QStringEncoder ansi("windows-1252");
        QVERIFY(ansi.isValid());
        QStringDecoder utf8("UTF-8");
        const QString first = utf8(QByteArray("citt\xc3", 5));
        const QString second = utf8(QByteArray("\xa0\n", 2));
        QCOMPARE(first + second, QString::fromUtf8("città\n"));
    }
    void autolink_rule_scanning_has_a_cumulative_budget() {
        AutolinkValues values; values.enabled = true; values.ruleMatchBudget = 1;
        values.rules["first.h"] = {"-lfirst", false}; values.rules["last.h"] = {"-llast", true};
        const auto exhausted = values.resolve("/include/last.h");
        QVERIFY(!exhausted.complete); QVERIFY(exhausted.linkOptions.isEmpty()); QVERIFY(!exhausted.forceExecUTF8);
        values.ruleMatchBudget = -1;
        QVERIFY(!values.resolve("last.h").complete);
    }
    void compiler_binary_read_allocation_tracks_actual_size() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        const QString path = fixture.filePath("tiny-executable.bin");
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write("x"), qint64(1)); file.close();
        std::atomic_bool cancel{false};
        const auto tiny = readBoundedCompilerBinary(path, cancel);
        QVERIFY2(tiny.preparation.ready(), qPrintable(tiny.preparation.reason));
        QCOMPARE(tiny.bytes, QByteArray("x"));
        QVERIFY(tiny.bytes.capacity() < 64 * 1024);
        QVERIFY(file.open(QIODevice::WriteOnly)); QVERIFY(file.resize(64LL * 1024 * 1024 + 1)); file.close();
        const auto oversized = readBoundedCompilerBinary(path, cancel);
        QVERIFY(!oversized.preparation.ready()); QVERIFY(oversized.bytes.isEmpty());
        cancel.store(true, std::memory_order_release);
        QCOMPARE(readBoundedCompilerBinary(path, cancel).preparation.status, PreparationStatus::Cancelled);
    }
    void preparation_guard_compares_effective_config_not_ui_or_probe_results() {
        ConfigurationValues values;
        values.toolchain.compileOptions = {{CC_CMD_OPT_STD, "c++17"}};
        values.environment.insert("B", "second"); values.environment.insert("A", "first");
        const auto original = preparationConfigurationKey(values);
        values.toolchain.name = "Renamed display label";
        values.toolchain.supportNLS = true; values.toolchain.supportConvertingCharset = true;
        values.autolink.directIncludes["source.cpp"] = {"header.h"}; values.autolink.complete = true;
        QCOMPARE(preparationConfigurationKey(values), original);
        values.toolchain.compileOptions[CC_CMD_OPT_STD] = "c++20";
        QVERIFY(preparationConfigurationKey(values) != original);
        values.toolchain.compileOptions[CC_CMD_OPT_STD] = "c++17";
        values.environment.insert("A", "changed");
        QVERIFY(preparationConfigurationKey(values) != original);
        values.environment.insert("A", "first");
        values.autolink.rules["header.h"] = {"-lnew", false};
        QVERIFY(preparationConfigurationKey(values) != original);
    }
    void unavailable_directory_macros_fail_only_when_the_command_expands_them() {
        ConfigurationValues values;
        values.toolchain.useCustomCompileParams = true;
        values.toolchain.customCompileParams = QStringLiteral("-I$INCLUDE -Dliteral='$LIB'");
        QCOMPARE(unavailableToolchainMacros(values, ArgumentUse::Cpp, true), QStringList{"INCLUDE"});
        values.toolchain.useCustomLinkParams = true;
        values.toolchain.customLinkParams = QStringLiteral("-L${LIB}");
        QCOMPARE(unavailableToolchainMacros(values, ArgumentUse::Cpp, false), (QStringList{"INCLUDE", "LIB"}));
        values.macros["INCLUDE"] = "/captured/include"; values.macros["LIB"] = "/captured/library";
        QVERIFY(unavailableToolchainMacros(values, ArgumentUse::Cpp, false).isEmpty());
        QVERIFY(optionArguments(values, ArgumentUse::Cpp, false).contains("-I/captured/include"));
    }
    void probe_driver_exit_waits_for_the_entire_tree() {
#ifdef Q_OS_WIN
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        auto value = request("unused");
        const auto marker = fixture.filePath("normal-child.txt");
        value.arguments = {"--contained-driver", marker, "0", "200"};
        std::atomic_bool cancel{false};
        QElapsedTimer elapsed; elapsed.start();
        const auto result = run(value, cancel);
        QVERIFY2(result.preparation.ready(), qPrintable(result.preparation.reason));
        QCOMPARE(result.ownership, ProcessOwnershipLevel::WindowsTreeQuiescent);
        QVERIFY(elapsed.elapsed() >= 1000);
        QVERIFY(QFile::exists(marker));
#else
        QSKIP("Official process tree qualification is Windows 10 / Qt 6.11.2.");
#endif
    }
    void cancellation_during_normal_tree_drain_does_not_release_early_children() {
#ifdef Q_OS_WIN
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        const auto marker = fixture.filePath("cancelled-child.txt");
        const auto executable = QCoreApplication::applicationFilePath();
        std::atomic_bool cancel{false}, waitingForChild{false};
        bool driverExited = false, treeWasActive = false, treeDrained = false;
        std::thread worker([&] {
            QProcess process; process.setProgram(executable);
            process.setArguments({"--contained-driver", marker, "0", "1000"});
            ContainedProcess tree;
            if (!tree.prepare(process).success) { waitingForChild.store(true); return; }
            process.start();
            if (!process.waitForStarted(5000) || !process.waitForFinished(5000)) { tree.terminate(); tree.waitForDrained(process, 5000); waitingForChild.store(true); return; }
            driverExited = process.state() == QProcess::NotRunning;
            treeWasActive = tree.observe().status == ProcessTreeStatus::Active;
            waitingForChild.store(true, std::memory_order_release);
            completeContainedProcess(process, tree, cancel, false);
            treeDrained = tree.observe().drained();
        });
        QElapsedTimer wait; wait.start();
        while (!waitingForChild.load(std::memory_order_acquire) && wait.elapsed() < 6000) QTest::qWait(1);
        cancel.store(true, std::memory_order_release);
        worker.join();
        QVERIFY(driverExited); QVERIFY(treeWasActive); QVERIFY(treeDrained);
        QTest::qWait(1100); QVERIFY(!QFile::exists(marker));
#else
        QSKIP("Official process tree qualification is Windows 10 / Qt 6.11.2.");
#endif
    }
    void probe_deadline_includes_children_after_driver_exit() {
#ifdef Q_OS_WIN
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        auto value = request("unused"); const auto marker = fixture.filePath("deadline-child.txt");
        value.arguments = {"--contained-driver", marker, "0", "1000"}; value.deadlineMilliseconds = 80;
        std::atomic_bool cancel{false};
        const auto result = run(value, cancel);
        QVERIFY(!result.preparation.ready()); QVERIFY(result.preparation.reason.contains("deadline"));
        QCOMPARE(result.ownership, ProcessOwnershipLevel::WindowsTreeQuiescent);
        QTest::qWait(1100); QVERIFY(!QFile::exists(marker));
#else
        QSKIP("Official process tree qualification is Windows 10 / Qt 6.11.2.");
#endif
    }
};

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const int containedHelper = runContainedProcessTestHelper();
    if (containedHelper >= 0) return containedHelper;
    if (argc >= 3 && QString::fromLocal8Bit(argv[1]) == "--probe-fixture") {
        const QString mode = QString::fromLocal8Bit(argv[2]);
        QFile output; if (!output.open(stdout, QIODevice::WriteOnly)) return 5;
        if (mode == "environment") { output.write(qgetenv("REDPANDA_PROBE_FIXTURE")); output.flush(); }
        else if (mode == "wait") QThread::msleep(10000);
        else if (mode == "flood") for (int i = 0; i < 4096; ++i) { output.write(QByteArray(4096, 'x')); output.flush(); }
        return 0;
    }
    FrozenCompilationChecks checks;
    return QTest::qExec(&checks, argc, argv);
}
#include "test_frozen_compilation.moc"
