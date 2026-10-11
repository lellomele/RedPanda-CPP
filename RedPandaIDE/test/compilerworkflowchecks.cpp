// SPDX-License-Identifier: GPL-3.0-or-later
#include "compilerworkflowchecks.h"
#include "src/mainwindow.h"
#include "src/settings.h"
#include "src/systemconsts.h"
#include "src/editor.h"
#include "src/editormanager.h"
#include "src/compiler/compilermanager.h"
#include "src/compiler/executablerunner.h"
#include "src/widgets/issuestable.h"
#include "src/debugger/gdbmiresultparser.h"
#include "src/diagnosticcatalog.h"
#include "src/project.h"
#include <memory>
#include <QApplication>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QTimer>
#include <QTemporaryDir>
#include <QSaveFile>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QPointer>
#include <QMessageBox>
#include <QProcess>
#include <QProcessEnvironment>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QPlainTextEdit>

namespace {
bool waitUntil(const std::function<bool()> &predicate, int milliseconds) {
    if (predicate()) return true;
    QEventLoop loop;
    QTimer poll, deadline;
    deadline.setSingleShot(true);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (predicate()) loop.quit(); });
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start(10); deadline.start(milliseconds); loop.exec();
    return predicate();
}
bool writeBytes(const QString &path, const QByteArray &bytes) {
    QSaveFile file(path); file.setDirectWriteFallback(false);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
QByteArray readBytes(const QString &path) {
    QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
void hideConsole(QProcess &process) {
#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= 0x08000000; // CREATE_NO_WINDOW for owned console test processes.
    });
#else
    Q_UNUSED(process);
#endif
}
QByteArray boundedProcess(QProcess &process, int milliseconds, bool &finished) {
    hideConsole(process); process.start();
    finished = waitUntil([&] { return process.state() == QProcess::NotRunning; }, milliseconds);
    if (!finished) {
        process.kill();
        waitUntil([&] { return process.state() == QProcess::NotRunning; }, 3000);
    }
    return process.readAllStandardOutput() + process.readAllStandardError();
}
QString gdbQuote(QString text) {
    text.replace('\\', '/'); text.replace('"', "\\\"");
    return '"' + text + '"';
}
}

void runCompilerWorkflowChecks(MainWindow &window, const QString &outputDirectory,
                               const std::function<void(const QString &, bool)> &check) {
    const QString cppCompiler = qEnvironmentVariable("REDPANDA_QA_COMPILER");
    if (cppCompiler.isEmpty()) return; // Optional real-tool checks; fixture checks still run elsewhere.
    const auto record = [&](const QString &name, bool passed) {
        check("Production compiler workflow: " + name, passed);
    };
    QJsonArray workflowTrace;
    auto persistTrace = finally([&] {
        writeBytes(QDir(outputDirectory).filePath("compiler-workflow-trace.json"), QJsonDocument(workflowTrace).toJson());
    });
    int compileSequence = 0;
    const auto captureCompile = [&](const QString &phase, bool finished, bool timedOut, int errors, int issues) {
        const QString name = QString("compiler-workflow-%1-%2.log").arg(++compileSequence, 2, 10, QChar('0')).arg(phase);
        if (auto *output = window.findChild<QPlainTextEdit*>("txtToolsOutput"))
            writeBytes(QDir(outputDirectory).filePath(name), output->toPlainText().toUtf8());
        workflowTrace.append(QJsonObject{{"phase", phase}, {"finished", finished}, {"timedOut", timedOut},
                                        {"errors", errors}, {"issues", issues}, {"log", name}});
    };
    const QFileInfo compilerInfo(cppCompiler);
    record("configured GCC is an existing executable", compilerInfo.isFile() && compilerInfo.isExecutable());
    const bool isolated = !QCoreApplication::instance()->property("uiTestConfigPath").toString().isEmpty();
    record("uses an isolated QA preference profile", isolated);
    if (!isolated || !compilerInfo.isFile()) return;
    // Opt-in worker-side timestamps distinguish native process startup from
    // a subprocess that is already running when cancellation is requested.
    const bool hadProcessTrace = qEnvironmentVariableIsSet("REDPANDA_QA_COMPILER_TRACE");
    const QByteArray previousProcessTrace = qgetenv("REDPANDA_QA_COMPILER_TRACE");
    const QString processTracePath = QDir(outputDirectory).filePath("compiler-workflow-process.jsonl");
    record("compiler startup trace is created", writeBytes(processTracePath, {}));
    qputenv("REDPANDA_QA_COMPILER_TRACE", processTracePath.toUtf8());
    auto restoreProcessTrace = finally([&] {
        if (hadProcessTrace) qputenv("REDPANDA_QA_COMPILER_TRACE", previousProcessTrace);
        else qunsetenv("REDPANDA_QA_COMPILER_TRACE");
    });
    auto *manager = window.findChild<CompilerManager*>();
    auto *table = window.findChild<IssuesTable*>();
    record("uses the window's production CompilerManager and diagnostic panel", manager && table);
    if (!manager || !table) return;
    QTemporaryDir directory(QDir(outputDirectory).filePath(QString::fromUtf8("compila è spazio-XXXXXX")));
    record("Unicode and spaced fixture directory created", directory.isValid());
    if (!directory.isValid()) return;
    const bool oldSyntaxCheck = pSettings->editor().syntaxCheck();
    pSettings->editor().setSyntaxCheck(false);
    manager->stopCheckSyntax();
    record("existing background job reaches a bounded idle state",
           waitUntil([&] { return !manager->compiling() && !manager->backgroundSyntaxChecking(); }, 5000));
    if (manager->compiling() || manager->backgroundSyntaxChecking()) {
        pSettings->editor().setSyntaxCheck(oldSyntaxCheck); directory.setAutoRemove(false); return;
    }
    auto &sets = pSettings->compilerSets();
    const int previousDefault = sets.defaultIndex();
    const int fixtureIndex = int(sets.size());
    QString gccPath = QDir(compilerInfo.absolutePath()).filePath(
#ifdef Q_OS_WIN
        "gcc.exe"
#else
        "gcc"
#endif
    );
    auto detected = std::make_shared<CompilerSet>(compilerInfo.absolutePath(), QFileInfo(gccPath).fileName());
    detected->setName("QA isolated real GCC");
    detected->setCompilerType(CompilerType::GCC);
    detected->setCCompiler(gccPath); detected->setCppCompiler(cppCompiler);
    detected->setAutoAddCharsetParams(false); detected->setForceEnglishOutput(true);
    detected->setStaticLink(true); detected->setCompileOptions({});
    detected->setUseCustomCompileParams(true);
    detected->setCustomCompileParams("-std=c++17 -Wall -Wextra -Wpedantic -g -O0 -pthread -fdiagnostics-color=never");
    auto fixtureSet = sets.addSet(detected); sets.setDefaultIndex(fixtureIndex);
    QPointer<Editor> fixtureEditor;
    bool retainFixtureState = false;
    auto restore = finally([&] {
        if (retainFixtureState || manager->compiling()) {
            directory.setAutoRemove(false);
            return; // An active worker still owns this compiler configuration/input.
        }
        if (fixtureEditor) window.editorManager()->forceCloseEditor(fixtureEditor);
        pSettings->editor().setSyntaxCheck(oldSyntaxCheck);
        sets.setDefaultIndex(previousDefault);
        if (!manager->compiling()) sets.deleteSet(fixtureIndex);
        else directory.setAutoRemove(false); // Do not remove an input still owned by a failing worker.
        window.updateCompilerSet();
    });
    const QString source = directory.filePath(QString::fromUtf8("programma è.cpp"));
    const QString executable = fixtureSet->getOutputFilename(source);
    const QString marker = directory.filePath("runner-result.txt");
    // fopen/argv in the owned C++ fixture intentionally take relative ASCII
    // marker names. Unicode coverage is in the executable path and process cwd,
    // rather than accidentally depending on a narrow CRT's argument code page.
    const QByteArray payload("Production runner completed\n");
    const QByteArray valid = QString::fromUtf8(R"CPP(// unità di compilazione: percorso Unicode e spazi
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <thread>
#include <chrono>
struct WorkflowFixture {
    static int run(int argc, char **argv) {
        if (argc < 2) return 71;
        const bool waiting = argc > 2 && std::strcmp(argv[2], "--wait") == 0;
        std::FILE *file = std::fopen(argv[1], "wb");
        if (!file) return 72;
        const char *text = waiting ? "Waiting for cancellation\n" : "Production runner completed\n";
        const bool written = std::fputs(text, file) >= 0 && std::fclose(file) == 0;
        if (!written) return 73;
        if (waiting) for (;;) std::this_thread::sleep_for(std::chrono::milliseconds(50));
        return 0;
    }
};
int main(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) if (std::strcmp(argv[i], "--compiler-wait") == 0) {
        const char *path = std::getenv("REDPANDA_QA_WAIT_MARKER");
        if (!path) return 74;
        std::FILE *file = std::fopen(path, "wb");
        if (!file) return 75;
        std::fputs("Compiler waiting\n", file); std::fclose(file);
        for (;;) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    int intentionally_unused = 350;
    return WorkflowFixture::run(argc, argv);
}
)CPP").toUtf8();
    record("source fixture saved atomically", writeBytes(source, valid));
    fixtureEditor = window.openFile(source);
    record("source is opened through the real IDE", fixtureEditor && fixtureEditor->filename() == source);
    if (!fixtureEditor) return;
    int missingDialogs = 0;
    bool expectMissingCompiler = false;
    QJsonArray modalMessages;
    QTimer dismiss;
    QObject::connect(&dismiss, &QTimer::timeout, &window, [&] {
        if (auto *message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            modalMessages.append(message->text());
            if (expectMissingCompiler && message->text().contains("no-such-compiler")) ++missingDialogs;
            message->accept();
        }
    });
    dismiss.start(10);
    struct CompileResult { bool finished = false; bool timedOut = false; int errors = 0; int issues = 0; };
    const auto compile = [&](const QString &path) {
        CompileResult result;
        QObject jobScope;
        table->clearIssues();
        if (fixtureEditor) fixtureEditor->clearSyntaxIssues();
        const auto connection = QObject::connect(manager, &CompilerManager::compileFinished, &jobScope,
            [&](const QString &filename, bool syntax) { if (!syntax && filename == path) result.finished = true; });
        manager->compile(path, FileType::CppSource, ENCODING_UTF8, false, CppCompileType::Normal);
        result.finished = waitUntil([&] { return result.finished && !manager->compiling(); }, 30000);
        result.timedOut = !result.finished;
        if (result.timedOut) {
            manager->stopCompile();
            const bool stopped = waitUntil([&] { return !manager->compiling(); }, 5000);
            record("compiler timeout cancels the owned job within five seconds", stopped);
            if (!stopped) directory.setAutoRemove(false);
        }
        result.errors = manager->compileErrorCount(); result.issues = manager->compileIssueCount();
        captureCompile("file", result.finished, result.timedOut, result.errors, result.issues);
        QObject::disconnect(connection);
        return result;
    };
    const auto built = compile(source);
    if (manager->compiling()) { retainFixtureState = true; return; }
    record("real FileCompiler completes within its timeout", built.finished && !built.timedOut);
    record("valid C++ emits a warning and creates its executable", built.errors == 0 && built.issues > 0 && QFileInfo(executable).size() > 0);
    record("compilation leaves original source bytes unchanged", readBytes(source) == valid);
    bool warningSeen = false, originalSeen = false;
    for (int row = 0; row < table->count(); ++row) {
        const auto issue = table->issuesModel()->issue(row);
        if (issue && issue->type == CompileIssueType::Warning) {
            warningSeen = true;
            originalSeen = originalSeen || table->issuesModel()->originalDescription(row).contains("unused");
        }
    }
    record("production warning reaches the panel with original GCC text", warningSeen && originalSeen);

    struct RunnerResult { bool finished; bool withinDeadline; };
    const auto joinRunner = [&](std::unique_ptr<ExecutableRunner> &runner, int deadline) {
        const bool timely = waitUntil([&] { return runner->isFinished(); }, deadline);
        bool completed = timely;
        if (!completed) {
            runner->stop();
            completed = waitUntil([&] { return runner->isFinished(); }, 5000);
        }
        if (completed) { runner->wait(1000); return RunnerResult{true, timely}; }
        directory.setAutoRemove(false);
        retainFixtureState = true;
        auto *retained = runner.release();
        QObject::connect(retained, &QThread::finished, retained, &QObject::deleteLater);
        if (retained->isFinished()) retained->deleteLater();
        return RunnerResult{false, false};
    };
    if (QFileInfo(executable).size() > 0) {
        auto runner = std::make_unique<ExecutableRunner>(executable, QStringList{QFileInfo(marker).fileName()}, directory.path());
        runner->setStartConsole(false); runner->addBinDir(compilerInfo.absolutePath()); runner->setWaitForFinishTime(50);
        QString runError;
        QObject runScope;
        const auto errorConnection = QObject::connect(runner.get(), &Runner::runErrorOccurred, &runScope,
            [&](const QString &reason) { runError = reason; });
        QElapsedTimer runnerTimer; runnerTimer.start();
        runner->start();
        const auto completed = joinRunner(runner, 8000);
        workflowTrace.append(QJsonObject{{"phase", "runner"}, {"finished", completed.finished},
            {"withinDeadline", completed.withinDeadline}, {"error", runError},
            {"markerHex", QString::fromLatin1(readBytes(marker).toHex())}, {"milliseconds", double(runnerTimer.elapsed())}});
        record("production ExecutableRunner completes without a visible console", completed.finished && completed.withinDeadline && runError.isEmpty() && readBytes(marker) == payload);
        QObject::disconnect(errorConnection);
        if (!completed.finished) return;
        // Runner has no public exit-code result. Verify that separately for the
        // same artifact, while the primary execution above uses production code.
        QProcess direct;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("PATH", compilerInfo.absolutePath() + PATH_SEPARATOR + environment.value("PATH"));
        direct.setProcessEnvironment(environment); direct.setWorkingDirectory(directory.path());
        direct.setProgram(executable); direct.setArguments({QFileInfo(marker).fileName()});
        QFile::remove(marker);
        bool exited = false;
        const auto output = boundedProcess(direct, 8000, exited);
        writeBytes(directory.filePath("execution-output.log"), output);
        record("the same compiled artifact exits normally with code zero", exited && direct.error() != QProcess::FailedToStart && direct.exitStatus() == QProcess::NormalExit && direct.exitCode() == 0 && readBytes(marker) == payload);

        const QString waitingMarker = directory.filePath("waiting.txt");
        auto waiting = std::make_unique<ExecutableRunner>(executable, QStringList{QFileInfo(waitingMarker).fileName(), "--wait"}, directory.path());
        waiting->setStartConsole(false); waiting->addBinDir(compilerInfo.absolutePath()); waiting->setWaitForFinishTime(50);
        waiting->start();
        const bool started = waitUntil([&] { return readBytes(waitingMarker) == "Waiting for cancellation\n"; }, 5000);
        const bool waitingWasRunning = waiting->isRunning() && !waiting->isFinished();
        QElapsedTimer stopTimer; stopTimer.start();
        waiting->stop();
        const auto cancelled = joinRunner(waiting, 5000);
        workflowTrace.append(QJsonObject{{"phase", "runner-cancellation"}, {"started", started}, {"wasRunning", waitingWasRunning},
            {"finished", cancelled.finished}, {"withinDeadline", cancelled.withinDeadline}, {"milliseconds", double(stopTimer.elapsed())}});
        record("real slow runner cancellation is bounded and joins its worker", started && waitingWasRunning && cancelled.finished && cancelled.withinDeadline && stopTimer.elapsed() < 5000);
        if (!cancelled.finished) return;
        const QString immediateMarker = directory.filePath("immediate-waiting.txt");
        auto immediate = std::make_unique<ExecutableRunner>(executable, QStringList{QFileInfo(immediateMarker).fileName(), "--wait"}, directory.path());
        immediate->setStartConsole(false); immediate->addBinDir(compilerInfo.absolutePath()); immediate->setWaitForFinishTime(50);
        immediate->start(); immediate->stop();
        const auto immediateCancelled = joinRunner(immediate, 5000);
        record("stop immediately after start is not lost", immediateCancelled.finished && immediateCancelled.withinDeadline);
        if (!immediateCancelled.finished) return;
        QFile::remove(marker);
        auto retryRunner = std::make_unique<ExecutableRunner>(executable, QStringList{QFileInfo(marker).fileName()}, directory.path());
        retryRunner->setStartConsole(false); retryRunner->addBinDir(compilerInfo.absolutePath()); retryRunner->setWaitForFinishTime(50);
        retryRunner->start();
        const auto retryCompleted = joinRunner(retryRunner, 8000);
        record("a new production runner succeeds after both cancellation paths", retryCompleted.finished && retryCompleted.withinDeadline && readBytes(marker) == payload);
        if (!retryCompleted.finished) return;
    }

#ifdef Q_OS_WIN
    // Exercise CompilerManager::run itself without opening a console. This
    // short marker-only fixture has a GUI subsystem but creates no windows.
    const QString pathSource = directory.filePath("path-probe.cpp");
    const QByteArray pathProgram("#include <cstdio>\n#include <cstdlib>\nint main(int argc, char **argv) {\n"
        "if (argc < 2) return 80; std::FILE *file = std::fopen(argv[1], \"wb\");\n"
        "if (!file) return 81; const char *path = std::getenv(\"PATH\");\n"
        "const bool written = std::fputs(path ? path : \"\", file) >= 0;\n"
        "const bool closed = std::fclose(file) == 0; return written && closed ? 0 : 82; }\n");
    record("managed-run PATH fixture saved", writeBytes(pathSource, pathProgram));
    const QString previousFlags = fixtureSet->customCompileParams();
    fixtureSet->setCustomCompileParams(previousFlags + " -mwindows -Wno-misleading-indentation");
    const auto pathBuild = compile(pathSource);
    if (manager->compiling()) { retainFixtureState = true; return; }
    fixtureSet->setCustomCompileParams(previousFlags);
    const QString pathExecutable = fixtureSet->getOutputFilename(pathSource);
    record("managed-run no-console fixture builds with real GCC", pathBuild.finished && !pathBuild.timedOut
           && pathBuild.errors == 0 && QFileInfo(pathExecutable).size() > 0);
    if (QFileInfo(pathExecutable).size() > 0) {
        const bool hadPath = qEnvironmentVariableIsSet("PATH");
        const QByteArray savedPath = qgetenv("PATH");
        const bool oldRedirect = pSettings->executor().redirectInput();
        auto restoreRunEnvironment = finally([&] {
            if (hadPath) qputenv("PATH", savedPath); else qunsetenv("PATH");
            pSettings->executor().setRedirectInput(oldRedirect);
        });
        const QString systemRoot = qEnvironmentVariable("SystemRoot", "C:/Windows");
        qputenv("PATH", (QDir(systemRoot).filePath("System32") + ';' + systemRoot).toUtf8());
        pSettings->executor().setRedirectInput(false);
        const QString pathMarker = directory.filePath("path-result.txt");
        manager->run(pathExecutable, "path-result.txt", directory.path(), {compilerInfo.absolutePath()});
        const bool ran = waitUntil([&] { return !manager->running(); }, 8000);
        if (!ran) {
            manager->stopRun();
            if (!waitUntil([&] { return !manager->running(); }, 5000)) retainFixtureState = true;
        }
        QStringList paths;
        for (const auto &path : QString::fromLocal8Bit(readBytes(pathMarker)).split(';', Qt::SkipEmptyParts))
            paths.append(QDir::cleanPath(QDir::fromNativeSeparators(path)));
        const auto hasPath = [&](const QString &path) {
            return paths.contains(QDir::cleanPath(QDir::fromNativeSeparators(path)), Qt::CaseInsensitive);
        };
        workflowTrace.append(QJsonObject{{"phase", "manager-run-path"}, {"completed", ran},
            {"observedPath", QString::fromLocal8Bit(readBytes(pathMarker))}});
        record("managed run retains the compiler runtime path", ran && !paths.isEmpty() && hasPath(compilerInfo.absolutePath()));
        record("managed run excludes IDE app and private library paths", ran && !paths.isEmpty()
               && !hasPath(pSettings->dirs().appDir()) && !hasPath(pSettings->dirs().appLibexecDir()));
        if (retainFixtureState) return;
    }
#endif

    const QByteArray invalid("int main() { return; }\n");
    record("invalid fixture replaces only its own source atomically", writeBytes(source, invalid));
    fixtureEditor->loadFile({}, false);
    const auto failed = compile(source);
    if (manager->compiling()) { retainFixtureState = true; return; }
    record("syntax error completes as a recoverable compile result", failed.finished && !failed.timedOut && failed.errors > 0 && failed.issues > 0);
    record("failed compilation preserves invalid source for editing", readBytes(source) == invalid && fixtureEditor->text().contains("return;"));
    bool errorOriginal = false, errorTranslated = false, editorHintMatches = false;
    for (int row = 0; row < table->count(); ++row) {
        const auto issue = table->issuesModel()->issue(row);
        if (!issue || issue->type != CompileIssueType::Error) continue;
        errorOriginal = errorOriginal || table->issuesModel()->originalDescription(row).contains("return-statement");
        errorTranslated = errorTranslated || !table->issuesModel()->translatedDescription(row).isEmpty();
        const auto editorIssues = fixtureEditor->getSyntaxIssuesAtLine(issue->line);
        if (editorIssues) for (const auto &hint : *editorIssues)
            editorHintMatches = editorHintMatches || (hint && hint->hint == table->issuesModel()->diagnosticHint(row));
    }
    record("error retains original GCC message in the panel", errorOriginal);
    record("panel and editor tooltip carry the same production diagnostic", editorHintMatches);
    if (DiagnosticCatalog::isItalianInterface()) record("Italian production error also has its local translation", errorTranslated);
    record("corrected fixture is saved for retry", writeBytes(source, valid));
    fixtureEditor->loadFile({}, false);
    const auto retried = compile(source);
    if (manager->compiling()) { retainFixtureState = true; return; }
    record("build succeeds after editing a previous compiler error", retried.finished && !retried.timedOut && retried.errors == 0 && QFileInfo(executable).size() > 0);
    const QString missingTask = directory.filePath("missing-compiler.cpp");
    writeBytes(missingTask, valid);
    const QString originalCompiler = fixtureSet->cppCompiler();
    const QString absentCompiler = directory.filePath("no-such-compiler.exe");
    fixtureSet->setCppCompiler(absentCompiler);
    expectMissingCompiler = true;
    const auto missing = compile(missingTask);
    expectMissingCompiler = false;
    if (manager->compiling()) { retainFixtureState = true; return; }
    fixtureSet->setCppCompiler(originalCompiler);
    record("missing compiler is reported and its job returns to idle", missing.finished && !missing.timedOut && !manager->compiling() && missingDialogs > 0);
    record("missing compiler preserves source and the prior successful artifact", readBytes(missingTask) == valid && QFileInfo(executable).size() > 0);
    const auto restored = compile(source);
    if (manager->compiling()) { retainFixtureState = true; return; }
    record("retry works after restoring the compiler configuration", restored.finished && !restored.timedOut && restored.errors == 0 && QFileInfo(executable).size() > 0);


    // The compiler subprocess is an owned test binary in this case, not a
    // heuristic long GCC build. It enters a marker-attested wait state.
    const QString cancelSource = directory.filePath("cancel-compiler.cpp");
    const QString compilerMarker = directory.filePath("compiler-waiting.txt");
    record("compiler cancellation input fixture saved", writeBytes(cancelSource, valid));
    const bool markerWasSet = qEnvironmentVariableIsSet("REDPANDA_QA_WAIT_MARKER");
    const QByteArray previousMarker = qgetenv("REDPANDA_QA_WAIT_MARKER");
    // The process cwd is already the Unicode fixture directory. This test
    // marker is a relative ASCII name, not a CRT narrow-path encoding test.
    qputenv("REDPANDA_QA_WAIT_MARKER", "compiler-waiting.txt");
    auto restoreMarker = finally([&] {
        if (markerWasSet) qputenv("REDPANDA_QA_WAIT_MARKER", previousMarker);
        else qunsetenv("REDPANDA_QA_WAIT_MARKER");
    });
    const QString originalParameters = fixtureSet->customCompileParams();
    fixtureSet->setCppCompiler(executable); fixtureSet->setCustomCompileParams("--compiler-wait");
    bool cancellationFinished = false;
    QObject cancellationScope;
    const auto cancellationConnection = QObject::connect(manager, &CompilerManager::compileFinished, &cancellationScope,
        [&](const QString &filename, bool syntax) { if (!syntax && filename == cancelSource) cancellationFinished = true; });
    manager->compile(cancelSource, FileType::CppSource, ENCODING_UTF8, false, CppCompileType::Normal);
    const bool compilerStarted = waitUntil([&] { return readBytes(compilerMarker) == "Compiler waiting\n"; }, 5000);
    const bool compilerWasRunning = manager->compiling() && !cancellationFinished;
    QElapsedTimer compilerStop; compilerStop.start();
    manager->stopCompile();
    const bool compilerCancelled = waitUntil([&] { return cancellationFinished && !manager->compiling(); }, 5000);
    QObject::disconnect(cancellationConnection);
    workflowTrace.append(QJsonObject{{"phase", "compiler-cancellation"}, {"started", compilerStarted},
        {"wasRunning", compilerWasRunning}, {"finished", compilerCancelled}, {"milliseconds", double(compilerStop.elapsed())},
        {"markerHex", QString::fromLatin1(readBytes(compilerMarker).toHex())}});
    captureCompile("cancelled", compilerCancelled, !compilerCancelled, manager->compileErrorCount(), manager->compileIssueCount());
    record("real FileCompiler subprocess cancellation is bounded", compilerStarted && compilerWasRunning && compilerCancelled && compilerStop.elapsed() < 5000);
    if (!compilerCancelled) { retainFixtureState = true; return; }
    fixtureSet->setCppCompiler(originalCompiler); fixtureSet->setCustomCompileParams(originalParameters);
    const auto afterCancellation = compile(source);
    record("real GCC compilation succeeds after compiler-process cancellation", afterCancellation.finished && !afterCancellation.timedOut && afterCancellation.errors == 0 && QFileInfo(executable).size() > 0);
    if (manager->compiling()) { retainFixtureState = true; return; }

    // ProjectCompiler/Make uses a separate Project object: the user's current
    // project and every pre-existing editor remain associated as before.
    const auto previousProject = window.project();
    const QString projectDirectory = directory.filePath("project workflow");
    record("project workflow directory created", QDir().mkdir(projectDirectory));
    const QString projectMain = QDir(projectDirectory).filePath("main.cpp");
    const QString projectHelper = QDir(projectDirectory).filePath("helper.cpp");
    const QString projectHeader = QDir(projectDirectory).filePath("helper.h");
    const QString projectFile = QDir(projectDirectory).filePath("qa-project.dev");
    const QByteArray projectMainText("#include \"helper.h\"\nint main() { return sum(2, 3) == 5 ? 0 : 1; }\n");
    const QByteArray projectHelperText("#include \"helper.h\"\nint sum(int a, int b) { return a + b; }\n");
    record("project C++ units and header saved", writeBytes(projectMain, projectMainText)
        && writeBytes(projectHelper, projectHelperText) && writeBytes(projectHeader, "#pragma once\nint sum(int a, int b);\n"));
    const QByteArray projectIni = QString("[Project]\nVer=3\nName=qa-project\nType=1\nIsCpp=1\nModelType=1\nCompilerSet=%1\nUnitCount=3\n"
        "[Unit1]\nFileName=main.cpp\nCompile=1\nCompileCpp=1\nLink=1\nFileEncoding=UTF-8\n"
        "[Unit2]\nFileName=helper.cpp\nCompile=1\nCompileCpp=1\nLink=1\nFileEncoding=UTF-8\n"
        "[Unit3]\nFileName=helper.h\nCompile=0\nCompileCpp=1\nLink=0\nFileEncoding=UTF-8\n").arg(fixtureIndex).toUtf8();
    record("project descriptor saved", writeBytes(projectFile, projectIni));
    const QString make = QDir(compilerInfo.absolutePath()).filePath(
#ifdef Q_OS_WIN
        "mingw32-make.exe"
#else
        "make"
#endif
    );
    record("project workflow has the real Make executable", QFileInfo(make).isExecutable());
    if (QFileInfo(make).isExecutable()) {
        fixtureSet->setMake(make);
        auto project = Project::load(projectFile, window.editorManager(), window.iconsManager(), window.fileSystemWatcher());
        const auto projectCompile = [&](bool rebuild) {
            CompileResult result;
            QObject projectScope;
            table->clearIssues();
            const auto connection = QObject::connect(manager, &CompilerManager::compileFinished, &projectScope,
                [&](const QString &, bool syntax) { if (!syntax) result.finished = true; });
            manager->compileProject(project, rebuild);
            result.finished = waitUntil([&] { return result.finished && !manager->compiling(); }, 30000);
            result.timedOut = !result.finished;
            if (result.timedOut) {
                manager->stopCompile();
                record("project timeout cancels within five seconds", waitUntil([&] { return !manager->compiling(); }, 5000));
            }
            result.errors = manager->compileErrorCount(); result.issues = manager->compileIssueCount();
            captureCompile(rebuild ? "project-rebuild" : "project", result.finished, result.timedOut, result.errors, result.issues);
            QObject::disconnect(connection); return result;
        };
        const auto projectBuilt = projectCompile(false);
        const QString projectExecutable = project->outputFilename();
        record("production ProjectCompiler builds multiple units through Make", projectBuilt.finished && !projectBuilt.timedOut && projectBuilt.errors == 0 && QFileInfo(projectExecutable).size() > 0);
        if (manager->compiling()) { retainFixtureState = true; return; }
        if (QFileInfo(projectExecutable).size() > 0) {
            QProcess projectRun; projectRun.setProgram(projectExecutable); projectRun.setWorkingDirectory(projectDirectory);
            bool finished = false; boundedProcess(projectRun, 8000, finished);
            record("linked multi-unit artifact exits normally with code zero", finished && projectRun.error() != QProcess::FailedToStart
                && projectRun.exitStatus() == QProcess::NormalExit && projectRun.exitCode() == 0);
        }
        record("project error is injected only into its own unit", writeBytes(projectHelper, "#include \"helper.h\"\nint sum(int, int) { return; }\n"));
        // Force a production Rebuild so the test does not depend on Make's
        // filesystem timestamp granularity for edits made in the same second.
        const auto brokenProject = projectCompile(true);
        record("project compile failure reports diagnostics and finishes", brokenProject.finished && !brokenProject.timedOut && brokenProject.errors > 0);
        if (manager->compiling()) { retainFixtureState = true; return; }
        record("project unit corrected for retry", writeBytes(projectHelper, projectHelperText));
        const auto projectRetry = projectCompile(true);
        record("project rebuild succeeds after a previous unit error", projectRetry.finished && !projectRetry.timedOut && projectRetry.errors == 0 && QFileInfo(projectExecutable).size() > 0);
        if (manager->compiling()) { retainFixtureState = true; return; }
        record("project workflow preserves prior window project and source fixture", window.project() == previousProject && fixtureEditor && fixtureEditor->text().contains("intentionally_unused"));
    }

    const QString gdb = QDir(compilerInfo.absolutePath()).filePath(
#ifdef Q_OS_WIN
        "gdb.exe"
#else
        "gdb"
#endif
    );
    if (QFileInfo(gdb).isExecutable() && QFileInfo(executable).size() > 0) {
        QProcess debugger; hideConsole(debugger);
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("PATH", compilerInfo.absolutePath() + PATH_SEPARATOR + environment.value("PATH"));
        environment.insert("LC_ALL", "C"); environment.insert("LANGUAGE", "");
        debugger.setProcessEnvironment(environment); debugger.setProgram(gdb); debugger.setWorkingDirectory(directory.path());
        const QString debugMarker = directory.filePath("debug-result.txt");
        debugger.setArguments({"--nx", "--quiet", "--interpreter=mi2", "--batch",
            "-ex", "set charset UTF-8", "-ex", "file " + gdbQuote(executable), "-ex", "break main",
            "-ex", "set args debug-result.txt", "-ex", "run",
            "-ex", "interpreter-exec mi -stack-list-frames", "-ex", "continue"});
        bool debugFinished = false;
        const auto output = boundedProcess(debugger, 20000, debugFinished);
        writeBytes(QDir(outputDirectory).filePath("compiler-workflow-gdb.log"), output);
        bool framesParsed = false;
        for (QByteArray line : output.split('\n')) {
            // interpreter-exec's MI record is emitted inside a console stream
            // (~"..."). Decode its actual bytes before the production parser.
            line = line.trimmed();
            if (line.startsWith("~\"")) {
                GDBMIResultParser streamParser;
                GDBMIResultType streamType;
                GDBMIResultParser::ParseObject streamValues;
                if (streamParser.parse("stream=" + line.mid(1), "-data-evaluate-expression", streamType, streamValues))
                    line = streamValues["stream"].value().trimmed();
            }
            const int frame = line.indexOf("stack=[");
            if (frame < 0) continue;
            GDBMIResultParser parser; GDBMIResultType type; GDBMIResultParser::ParseObject values;
            framesParsed = parser.parse(line.mid(frame).trimmed(), "-stack-list-frames", type, values)
                && type == GDBMIResultType::FrameStack;
            if (framesParsed) break;
        }
        workflowTrace.append(QJsonObject{{"phase", "gdb"}, {"finished", debugFinished}, {"exitCode", debugger.exitCode()},
            {"framesParsed", framesParsed}, {"breakpointHit", output.contains("breakpoint-hit")},
            {"markerHex", QString::fromLatin1(readBytes(debugMarker).toHex())}});
        record("owned GDB fixture stops at main and production MI parser reads stack frames",
               debugFinished && debugger.error() != QProcess::FailedToStart && debugger.exitStatus() == QProcess::NormalExit && debugger.exitCode() == 0
               && output.contains("breakpoint-hit") && framesParsed && readBytes(debugMarker) == payload);
    }
    dismiss.stop();
    writeBytes(QDir(outputDirectory).filePath("compiler-workflow-messages.json"), QJsonDocument(modalMessages).toJson());
}
