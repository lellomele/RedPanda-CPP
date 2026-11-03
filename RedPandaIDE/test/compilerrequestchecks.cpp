// SPDX-License-Identifier: GPL-3.0-or-later
#include "compilerrequestchecks.h"
#include "src/mainwindow.h"
#include "src/editor.h"
#include "src/editormanager.h"
#include "src/settings.h"
#include "src/autolinkmanager.h"
#include "src/compiler/compilermanager.h"
#include "src/compiler/filecompiler.h"
#include "src/compiler/stdincompiler.h"
#include "src/compiler/frozencompilearguments.h"
#include "src/widgets/issuestable.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QTemporaryDir>
#include <QSemaphore>
#include <QThread>

namespace {
class TreeFixtureCompiler final : public Compiler {
public:
    TreeFixtureCompiler(Compilation::FileCompileRequest::Pointer request, QString helper, QString marker,
                        int driverDelay, int childDelay, std::shared_ptr<QSemaphore> retirementGate = {}):
        Compiler(std::move(request)), mHelper(std::move(helper)), mMarker(std::move(marker)),
        mDriverDelay(driverDelay), mChildDelay(childDelay), mRetirementGate(std::move(retirementGate)) {}
protected:
    void run() override {
        Compiler::run();
        if (mRetirementGate) mRetirementGate->tryAcquire(1, 5000);
    }
    bool prepareForCompile() override {
        mCompiler = mHelper;
        mArguments = {"--contained-driver", mMarker, QString::number(mDriverDelay), QString::number(mChildDelay)};
        mDirectory = QFileInfo(mMarker).absolutePath();
        return true;
    }
    bool prepareForRebuild() override { return true; }
private:
    const QString mHelper, mMarker;
    const int mDriverDelay, mChildDelay;
    const std::shared_ptr<QSemaphore> mRetirementGate;
};
}

class CompilerRequestChecks {
public:
    static int run(MainWindow &window, const QString &outputDirectory)
    {
        // This fixture changes in-memory preferences and must run only with the
        // isolated QA profile, never on an ordinary user session.
        if (!QCoreApplication::instance() || QCoreApplication::instance()->property("uiTestConfigPath").toString().isEmpty()) return 2;
        auto *manager = window.findChild<CompilerManager *>();
        auto *table = window.findChild<IssuesTable *>();
        if (!manager || !table || manager->compiling() || manager->backgroundSyntaxChecking()) return 2;
        const int originalDefaultIndex = pSettings->compilerSets().defaultIndex();
        int qaSetIndex = -1;
        const QString qaCompiler = qEnvironmentVariable("REDPANDA_QA_COMPILER");
        if (!qaCompiler.isEmpty()) {
            const QFileInfo program(qaCompiler);
            if (!program.isFile()) return 3;
            const QString cpp = program.dir().filePath(QStringLiteral("g++.exe"));
            if (!QFileInfo(cpp).isFile()) return 3;
            qaSetIndex = int(pSettings->compilerSets().size());
            const auto set = pSettings->compilerSets().addSet(); // Pure values; no GUI autodetection/probes.
            set->setCompilerType(CompilerType::GCC); set->setName(QStringLiteral("Isolated immutable compiler QA"));
            set->setCCompiler(program.absoluteFilePath()); set->setCppCompiler(cpp);
            set->binDirs() = {program.absolutePath()}; set->setTarget(QStringLiteral("x86_64"));
            set->setUseCustomCompileParams(false); set->setUseCustomLinkParams(false);
            set->setAutoAddCharsetParams(true); set->setExecCharset(ENCODING_UTF8);
            set->setStaticLink(false); set->setForceEnglishOutput(true);
            set->setCompileOption(CC_CMD_OPT_STD, QStringLiteral("c++17"));
            pSettings->compilerSets().setDefaultIndex(qaSetIndex);
        }
        const auto restoreDefault = finally([&] {
            pSettings->compilerSets().setDefaultIndex(originalDefaultIndex);
            if (qaSetIndex >= 0) pSettings->compilerSets().deleteSet(qaSetIndex);
        });
        const auto liveSet = pSettings->compilerSets().defaultSet();
        if (!liveSet || (liveSet->compilerType() != CompilerType::GCC && liveSet->compilerType() != CompilerType::Clang)) return 3;
        const auto oldC = liveSet->CCompiler(), oldCpp = liveSet->cppCompiler();
        const auto oldOptions = liveSet->compileOptions();
        const bool oldUseCustomCompile = liveSet->useCustomCompileParams();
        const QString oldCustomCompile = liveSet->customCompileParams();
        const bool oldAutolink = pSettings->editor().enableAutolink();
        const bool oldSyntaxCheck = pSettings->editor().syntaxCheck();
        QTemporaryDir fixture(QDir(outputDirectory).filePath("immutable-compiler-XXXXXX"));
        if (!fixture.isValid()) return 4;
        QPointer<Editor> editor;
        QPointer<Editor> includedEditor;
        const auto restore = finally([&] {
            manager->stopCompile(); manager->stopCheckSyntax();
            QElapsedTimer stopping; stopping.start();
            while (manager->trackedWorkerCount() > 0 && stopping.elapsed() < 5000) {
                QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 10);
                QThread::msleep(1);
            }
            // On a watchdog failure retain the isolated fixture for diagnosis;
            // do not delete paths still owned by an external compiler process.
            if (manager->trackedWorkerCount() > 0) fixture.setAutoRemove(false);
            liveSet->setCCompiler(oldC); liveSet->setCppCompiler(oldCpp); liveSet->setCompileOptions(oldOptions);
            liveSet->setUseCustomCompileParams(oldUseCustomCompile); liveSet->setCustomCompileParams(oldCustomCompile);
            pSettings->editor().setEnableAutolink(oldAutolink);
            pSettings->editor().setSyntaxCheck(oldSyntaxCheck);
            if (includedEditor) window.editorManager()->forceCloseEditor(includedEditor);
            if (editor) window.editorManager()->forceCloseEditor(editor);
        });
        pSettings->editor().setEnableAutolink(false);
        pSettings->editor().setSyntaxCheck(false);
        QJsonArray checks;
        int failures = 0;
        const auto check = [&](const char *name, bool passed) {
            checks.append(QJsonObject{{"name", QString::fromLatin1(name)}, {"passed", passed}});
            if (!passed) ++failures;
        };
        const auto write = [](const QString &path, const QByteArray &bytes) {
            QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
        };
        const auto read = [](const QString &path) {
            QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
        };
        const auto waitFor = [&](const std::function<bool()> &finished, int milliseconds) {
            QElapsedTimer timer; timer.start();
            while (!finished() && timer.elapsed() < milliseconds) {
                QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 10);
                QThread::msleep(1);
            }
            return finished();
        };
        const QString source = fixture.filePath(QStringLiteral("captured-città.cpp"));
        const QByteArray original = "#include <iostream>\nclass Captured { public: int answer() const { return 42; } };\nint main() { std::cout << Captured{}.answer(); return 0; }\n";
        if (!write(source, original)) return 5;
        const auto cachedIncludesBefore = liveSet->cachedDefaultCppIncludeDirs();
        PAnalysisInputSnapshot captureOnlyInputs;
        const auto captureOnlyRequest = manager->captureFileRequest(source, FileType::CppSource, ENCODING_UTF8,
            CppCompileType::Normal, false, false, {}, nullptr, {}, captureOnlyInputs);
        { FileCompiler captureOnlyConstructor(captureOnlyRequest); }
        check("GUI request capture and frozen compiler construction do not trigger lazy toolchain directory discovery",
              liveSet->cachedDefaultCppIncludeDirs() == cachedIncludesBefore);
        liveSet->setUseCustomCompileParams(true); liveSet->setCustomCompileParams(QStringLiteral("-I${LIB}"));
        PAnalysisInputSnapshot unavailableMacroInputs;
        const auto unavailableMacroRequest = manager->captureFileRequest(source, FileType::CppSource, ENCODING_UTF8,
            CppCompileType::Normal, false, false, {}, nullptr, {}, unavailableMacroInputs);
        liveSet->setUseCustomCompileParams(oldUseCustomCompile); liveSet->setCustomCompileParams(oldCustomCompile);
        check("An actually used unavailable directory macro is explicit in the immutable request",
              !unavailableMacroRequest->values().configuration->values().macros.value("LIB").isEmpty()
              || unavailableMacroRequest->values().unavailableToolchainMacros.contains("LIB"));
        editor = window.editorManager()->newEditor(source, ENCODING_UTF8, FileType::CppSource, {}, false, false);
        if (!editor) return 6;

        PAnalysisInputSnapshot inputs;
        const auto request = manager->captureFileRequest(source, FileType::CppSource, ENCODING_UTF8,
            CppCompileType::Normal, false, false, {}, nullptr, {}, inputs);
        const auto mailbox = std::make_shared<Compilation::PreparationMailbox>();
        auto *compiler = new FileCompiler(request);
        QPointer<Compiler> worker(compiler);
        manager->mCompiler = compiler;
        manager->mCompileErrorCount = 0; manager->mCompileIssueCount = 0;
        manager->trackWorker(compiler);
        manager->connectCapturedCompiler(compiler, false);
        compiler->setPreparationMailbox(mailbox);
        const auto started = std::make_shared<bool>(false);
        const auto startConnection = QObject::connect(compiler, &Compiler::compileStarted, &window, [started] { *started = true; });
        compiler->start();
        const bool entered = waitFor([&] { return *started; }, 5000);
        QObject::disconnect(startConnection);
        liveSet->setCCompiler(fixture.filePath("missing-c-compiler.exe"));
        liveSet->setCppCompiler(fixture.filePath("missing-cpp-compiler.exe"));
        liveSet->setCompileOption(CC_CMD_OPT_STD, QStringLiteral("invalid-standard-for-new-request"));
        const auto changedOptions = liveSet->compileOptions();
        mailbox->deliver({{}, request->values().configuration});
        bool finished = waitFor([&] { return !manager->compiling() && (!worker || worker->isFinished()); }, 20000);
        if (!finished && worker) { worker->stopCompile(); finished = waitFor([&] { return !manager->compiling() && (!worker || worker->isFinished()); }, 5000); }
        check("Registered real FileCompiler starts from one accepted immutable request", entered && finished);
        check("Changing live compiler paths/options while the job awaits analysis does not change that job",
              finished && manager->lastCompileOutcome().succeeded() && manager->compileErrorCount() == 0
              && QFileInfo(changeFileExt(source, request->values().configuration->values().toolchain.executableSuffix)).isFile());
        check("Compilation does not restore or overwrite the user's newer compiler preference edit",
              liveSet->CCompiler().endsWith("missing-c-compiler.exe") && liveSet->compileOptions() == changedOptions);
        liveSet->setCCompiler(oldC); liveSet->setCppCompiler(oldCpp); liveSet->setCompileOptions(oldOptions);
        if (!finished) return 7;
        QProcess executable;
        executable.setProgram(changeFileExt(source, request->values().configuration->values().toolchain.executableSuffix));
        executable.setProcessEnvironment(Compilation::processEnvironment(request->values().configuration->values(), QFileInfo(oldCpp).absolutePath(), QDir::listSeparator()));
        executable.start();
        const bool ran = executable.waitForStarted(5000) && executable.waitForFinished(5000);
        if (!ran) { executable.kill(); executable.waitForFinished(2000); }
        check("The real executable produced by the captured request runs successfully",
              ran && executable.exitStatus() == QProcess::NormalExit && executable.exitCode() == 0 && executable.readAllStandardOutput() == QByteArray("42"));
        if (unavailableMacroRequest->values().unavailableToolchainMacros.contains("LIB")) {
            const QString existingOutput = changeFileExt(source, request->values().configuration->values().toolchain.executableSuffix);
            const QByteArray existingBytes = read(existingOutput);
            auto *unavailable = new FileCompiler(unavailableMacroRequest); QPointer<Compiler> unavailableWorker(unavailable);
            manager->mCompiler = unavailable; manager->trackWorker(unavailable); manager->connectCapturedCompiler(unavailable, false);
            QObject::disconnect(unavailable, SIGNAL(compileErrorOccured(QString)), manager, nullptr);
            unavailable->start();
            const bool macroFinished = waitFor([&] { return !manager->compiling() && (!unavailableWorker || unavailableWorker->isFinished()); }, 5000);
            check("Unavailable macros fail recoverably before launch and preserve the previous executable",
                  macroFinished && !manager->lastCompileOutcome().succeeded()
                  && manager->lastCompileOwnership() == Compilation::ProcessOwnershipLevel::NoProcessStarted
                  && read(existingOutput) == existingBytes && !existingBytes.isEmpty());
            if (!macroFinished) { manager->stopCompile(); return 23; }
        }

        // Input is the accepted editor text, even when the current disk file is
        // deliberately invalid. A later editor revision must not receive its diagnostics.
        window.fileSystemWatcher()->removePath(source);
        if (!write(source, "not a valid C++ program!\n")) return 8;
        editor->setContent(QStringLiteral("class StdinOnly {}; int main() { return 0; }\n")); editor->setModified(true);
        pSettings->editor().setEnableAutolink(true);
        PAnalysisInputSnapshot syntaxInputs;
        const auto independentSyntax = manager->captureFileRequest(source, FileType::CppSource, ENCODING_UTF8,
            CppCompileType::Normal, true, false, editor->text(), nullptr, {}, syntaxInputs);
        check("Syntax-only requests do not depend on linker autolink analysis",
              !independentSyntax->values().configuration->values().autolink.enabled
              && independentSyntax->values().configuration->values().autolink.complete);
        manager->checkSyntax(source, ENCODING_UTF8, editor->text(), {});
        const bool stdinFinished = waitFor([&] { return !manager->backgroundSyntaxChecking(); }, 15000);
        check("Real StdinCompiler checks the captured source rather than invalid disk contents",
              stdinFinished && manager->lastSyntaxCheckOutcome().succeeded() && manager->syntaxCheckErrorCount() == 0);
        if (!stdinFinished) { manager->stopCheckSyntax(); return 9; }
        editor->setContent(QStringLiteral("const char *city = \"città\"; int main() { return 0; }\n")); editor->setModified(true);
        bool allEncodings = true;
        for (const QByteArray &originalEncoding : {QByteArray(ENCODING_ASCII), QByteArray("windows-1252"),
                                                   QByteArray(ENCODING_UTF16_BOM), QByteArray(ENCODING_UTF32_BOM)}) {
            manager->checkSyntax(source, originalEncoding, editor->text(), {});
            const bool idle = waitFor([&] { return !manager->backgroundSyntaxChecking(); }, 15000);
            allEncodings = allEncodings && idle && manager->lastSyntaxCheckOutcome().succeeded();
            if (!idle) { manager->stopCheckSyntax(); return 12; }
        }
        check("Captured Unicode stdin text stays correct for ASCII ANSI UTF16 and UTF32 origin metadata", allEncodings);
        editor->setContent(QStringLiteral("int città = 1; int main() { return città; }\n")); editor->setModified(true);
        liveSet->setUseCustomCompileParams(true);
        liveSet->setCustomCompileParams(QStringLiteral("-finput-charset=windows-1252"));
        manager->checkSyntax(source, QByteArray("windows-1252"), editor->text(), {});
        const bool utf8PolicyFinished = waitFor([&] { return !manager->backgroundSyntaxChecking(); }, 15000);
        check("Stdin UTF-8 transport remains correct when custom disk flags request an ANSI input encoding",
              utf8PolicyFinished && manager->lastSyntaxCheckOutcome().succeeded());
        liveSet->setUseCustomCompileParams(oldUseCustomCompile); liveSet->setCustomCompileParams(oldCustomCompile);
        if (!utf8PolicyFinished) { manager->stopCheckSyntax(); return 24; }

        editor->setContent(QStringLiteral("int main( { return 0; }\n")); editor->setModified(true);
        PAnalysisInputSnapshot oldInputs;
        const auto oldSyntaxRequest = manager->captureFileRequest(source, FileType::CppSource, ENCODING_UTF8,
            CppCompileType::Normal, true, false, editor->text(), nullptr, {}, oldInputs);
        const auto syntaxMailbox = std::make_shared<Compilation::PreparationMailbox>();
        auto *oldSyntax = new StdinCompiler(oldSyntaxRequest);
        QPointer<Compiler> oldSyntaxWorker(oldSyntax);
        manager->mBackgroundSyntaxChecker = oldSyntax; manager->mSyntaxCheckErrorCount = 0; manager->mSyntaxCheckIssueCount = 0;
        manager->trackWorker(oldSyntax); manager->connectCapturedCompiler(oldSyntax, true); oldSyntax->setPreparationMailbox(syntaxMailbox);
        table->clearIssues();
        editor->clearSyntaxIssues();
        oldSyntax->start();
        editor->setContent(QStringLiteral("int main() { return 0; }\n")); editor->setModified(true);
        syntaxMailbox->deliver({{}, oldSyntaxRequest->values().configuration});
        const bool oldSyntaxFinished = waitFor([&] { return !manager->backgroundSyntaxChecking()
            && (!oldSyntaxWorker || oldSyntaxWorker->isFinished()); }, 15000);
        check("An accepted old stdin revision still runs but its errors cannot replace newer editor diagnostics",
              oldSyntaxFinished && !manager->lastSyntaxCheckOutcome().succeeded() && manager->syntaxCheckErrorCount() > 0
              && table->count() > 0 && !editor->getSyntaxIssuesAtLine(0));
        if (!oldSyntaxFinished) { manager->stopCheckSyntax(); return 14; }

        const QString header = fixture.filePath(QStringLiteral("captured-header.h"));
        if (!write(header, "int capturedHeader = ;\n")) return 17;
        includedEditor = window.editorManager()->newEditor(header, ENCODING_UTF8, FileType::CCppHeader, {}, false, false);
        if (!includedEditor) return 18;
        editor->setContent(QStringLiteral("#include \"captured-header.h\"\nint main() { return 0; }\n"));
        editor->setModified(true);
        PAnalysisInputSnapshot headerInputs;
        const auto headerRequest = manager->captureFileRequest(source, FileType::CppSource, ENCODING_UTF8,
            CppCompileType::Normal, true, false, editor->text(), nullptr, {}, headerInputs);
        const auto headerMailbox = std::make_shared<Compilation::PreparationMailbox>();
        auto *headerSyntax = new StdinCompiler(headerRequest);
        QPointer<Compiler> headerSyntaxWorker(headerSyntax);
        manager->mBackgroundSyntaxChecker = headerSyntax;
        manager->mSyntaxCheckErrorCount = 0; manager->mSyntaxCheckIssueCount = 0;
        manager->trackWorker(headerSyntax); manager->connectCapturedCompiler(headerSyntax, true);
        headerSyntax->setPreparationMailbox(headerMailbox);
        table->clearIssues(); editor->clearSyntaxIssues(); includedEditor->clearSyntaxIssues();
        headerSyntax->start();
        includedEditor->setContent(QStringLiteral("int capturedHeader = 42;\n")); includedEditor->setModified(true);
        headerMailbox->deliver({{}, headerRequest->values().configuration});
        const bool headerFinished = waitFor([&] { return !manager->backgroundSyntaxChecking()
            && (!headerSyntaxWorker || headerSyntaxWorker->isFinished()); }, 15000);
        bool headerDiagnosticPresent = false;
        for (int row = 0; row < table->count(); ++row)
            if (QFileInfo(table->issue(row)->filename).absoluteFilePath() == header) headerDiagnosticPresent = true;
        check("Original header diagnostics remain in the panel but cannot mark a newer included-editor revision",
              headerFinished && manager->syntaxCheckErrorCount() > 0 && headerDiagnosticPresent
              && !includedEditor->getSyntaxIssuesAtLine(0));
        if (!headerFinished) { manager->stopCheckSyntax(); return 19; }
        includedEditor->setContent(QStringLiteral("int capturedHeader = ;\n")); includedEditor->setModified(true);
        table->clearIssues(); includedEditor->clearSyntaxIssues();
        manager->checkSyntax(source, ENCODING_UTF8, editor->text(), {});
        const bool matchingHeaderFinished = waitFor([&] { return !manager->backgroundSyntaxChecking(); }, 15000);
        check("An unchanged accepted included-editor revision still receives its diagnostic hint",
              matchingHeaderFinished && manager->syntaxCheckErrorCount() > 0 && includedEditor->getSyntaxIssuesAtLine(0));
        if (!matchingHeaderFinished) { manager->stopCheckSyntax(); return 20; }

        const QString previousExecutable = changeFileExt(source, request->values().configuration->values().toolchain.executableSuffix);
        const QByteArray previousExecutableBytes = read(previousExecutable);
        PAnalysisInputSnapshot cancelInputs;
        const auto cancelRequest = manager->captureFileRequest(source, FileType::CppSource, ENCODING_UTF8,
            CppCompileType::Normal, false, true, {}, nullptr, {}, cancelInputs);
        const auto cancelMailbox = std::make_shared<Compilation::PreparationMailbox>();
        auto *cancelled = new FileCompiler(cancelRequest);
        QPointer<Compiler> cancelledWorker(cancelled);
        manager->mCompiler = cancelled; manager->trackWorker(cancelled); manager->connectCapturedCompiler(cancelled, false);
        cancelled->setPreparationMailbox(cancelMailbox); cancelled->start(); manager->stopCompile();
        const bool cancelledFinished = waitFor([&] { return !manager->compiling()
            && (!cancelledWorker || cancelledWorker->isFinished()); }, 5000);
        check("Cancellation while awaiting preparation is distinct and preserves the previous executable",
              cancelledFinished && manager->lastCompileOutcome().status == OperationStatus::Cancelled
              && !previousExecutableBytes.isEmpty() && read(previousExecutable) == previousExecutableBytes);
        if (!cancelledFinished) return 15;

        PAnalysisInputSnapshot changedInputs;
        const QString independentPath = fixture.filePath("independent.cpp");
        const QString ruleName = QStringLiteral("rmele-independent-autolink.h");
        if (!write(independentPath, "#include \"bridge.h\"\nint main() { return RMELE_AUTOLINK_VALUE - 42; }\n")
                || !write(fixture.filePath("bridge.h"), "#include \"rmele-independent-autolink.h\"\n")
                || !write(fixture.filePath(ruleName), "#pragma once\n")) return 16;
        const auto previousRule = pAutolinkManager->links().value(ruleName);
        const auto restoreRule = finally([&] {
            if (previousRule) pAutolinkManager->setLink(ruleName, previousRule->linkOption, previousRule->execUseUTF8);
            else pAutolinkManager->removeLink(ruleName);
        });
        pAutolinkManager->setLink(ruleName, "-DRMELE_AUTOLINK_VALUE=42", false);
        const auto changedRequest = manager->captureFileRequest(independentPath, FileType::CppSource, ENCODING_UTF8,
            CppCompileType::Normal, false, false, {}, nullptr, {}, changedInputs);
        auto *configurationChanged = new FileCompiler(changedRequest);
        QPointer<Compiler> configurationChangedWorker(configurationChanged);
        manager->mCompiler = configurationChanged; manager->trackWorker(configurationChanged);
        manager->connectCapturedCompiler(configurationChanged, false);
        QObject::disconnect(configurationChanged, SIGNAL(compileErrorOccured(QString)), manager, nullptr);
        liveSet->setCompileOption(CC_CMD_OPT_STD, QStringLiteral("changed-standard-while-awaiting-analysis"));
        pAutolinkManager->removeLink(ruleName);
        configurationChanged->start();
        const bool conflictFinished = waitFor([&] { return !manager->compiling()
            && (!configurationChangedWorker || configurationChangedWorker->isFinished()); }, 22000);
        check("A transitive autolink rule from the accepted configuration builds without an editor parser",
              conflictFinished && changedRequest->values().dependencyProbe && manager->lastCompileOutcome().succeeded()
              && QFileInfo(liveSet->getOutputFilename(independentPath)).size() > 0
              && read(previousExecutable) == previousExecutableBytes);
        liveSet->setCompileOptions(oldOptions);
        pSettings->editor().setEnableAutolink(false);
        if (!conflictFinished) { manager->stopCompile(); return 16; }

#ifdef Q_OS_WIN
        const QString helper = qEnvironmentVariable("REDPANDA_QA_PROCESS_HELPER",
            QDir(QCoreApplication::applicationDirPath()).filePath("test-frozen-compilation.exe"));
        check("The process-tree integration fixture executable is available", QFileInfo(helper).isFile());
        if (QFileInfo(helper).isFile()) {
            PAnalysisInputSnapshot treeInputs;
            const auto treeRequest = manager->captureFileRequest(source, FileType::CppSource, ENCODING_UTF8,
                CppCompileType::Normal, false, false, {}, nullptr, {}, treeInputs);
            const QString cancelledMarker = fixture.filePath("cancelled-tree-child.txt");
            auto *treeCompiler = new TreeFixtureCompiler(treeRequest, helper, cancelledMarker, 30000, 700);
            QPointer<Compiler> treeWorker(treeCompiler);
            manager->mCompiler = treeCompiler; manager->mCompileErrorCount = 0; manager->mCompileIssueCount = 0;
            manager->trackWorker(treeCompiler); manager->connectCapturedCompiler(treeCompiler, false);
            QObject::disconnect(treeCompiler, SIGNAL(compileErrorOccured(QString)), manager, nullptr);
            const auto childReady = std::make_shared<bool>(false);
            const auto readyConnection = QObject::connect(treeCompiler, &Compiler::compileOutput, &window,
                [childReady](const QString &output) { bool pid = false; output.trimmed().toULongLong(&pid); if (pid) *childReady = true; });
            treeCompiler->start();
            const bool observedChild = waitFor([&] { return *childReady || !manager->compiling(); }, 5000) && *childReady;
            QObject::disconnect(readyConnection);
            check("A registered compiler owns its early child before cancellation", observedChild && manager->compiling());
            manager->stopCompile();
            const bool stoppedTree = waitFor([&] { return !manager->compiling() && (!treeWorker || treeWorker->isFinished()); }, 7000);
            check("Compiler cancellation reaches the kernel tree acknowledgement before releasing the registered task",
                  stoppedTree && manager->lastCompileOutcome().status == OperationStatus::Cancelled
                  && manager->lastCompileOwnership() == Compilation::ProcessOwnershipLevel::WindowsTreeQuiescent
                  && !manager->canContinueLastCompile());
            if (!stoppedTree) return 21;
            const QString normalMarker = fixture.filePath("normal-tree-child.txt");
            PAnalysisInputSnapshot nextTreeInputs;
            const auto nextTreeRequest = manager->captureFileRequest(source, FileType::CppSource, ENCODING_UTF8,
                CppCompileType::Normal, false, false, {}, nullptr, {}, nextTreeInputs);
            auto *nextCompiler = new TreeFixtureCompiler(nextTreeRequest, helper, normalMarker, 0, 200);
            QPointer<Compiler> nextWorker(nextCompiler);
            manager->mCompiler = nextCompiler; manager->trackWorker(nextCompiler); manager->connectCapturedCompiler(nextCompiler, false);
            QObject::disconnect(nextCompiler, SIGNAL(compileErrorOccured(QString)), manager, nullptr);
            const auto normalChildReady = std::make_shared<bool>(false);
            const auto normalConnection = QObject::connect(nextCompiler, &Compiler::compileOutput, &window,
                [normalChildReady](const QString &output) { bool pid = false; output.trimmed().toULongLong(&pid); if (pid) *normalChildReady = true; });
            nextCompiler->start();
            const bool observedNormalChild = waitFor([&] { return *normalChildReady || !manager->compiling(); }, 5000) && *normalChildReady;
            QObject::disconnect(normalConnection);
            check("A driver that exits before its child does not release the compiler registry gate",
                  observedNormalChild && manager->compiling());
            const bool normalTreeFinished = waitFor([&] { return !manager->compiling() && (!nextWorker || nextWorker->isFinished()); }, 7000);
            check("The next job completes only after its child exits and the cancelled child never writes late output",
                  normalTreeFinished && manager->lastCompileOutcome().succeeded()
                  && manager->lastCompileOwnership() == Compilation::ProcessOwnershipLevel::WindowsTreeQuiescent
                  && QFile::exists(normalMarker) && !QFile::exists(cancelledMarker));
            if (!normalTreeFinished) { manager->stopCompile(); return 22; }
        }
#endif

        // An accidental suffix collision must fail before deleting or replacing
        // the source. Observe the actual compiler result while suppressing only
        // this fixture's expected error dialog.
        PAnalysisInputSnapshot guardInputs;
        const auto captured = manager->captureFileRequest(source, FileType::CppSource, ENCODING_UTF8,
            CppCompileType::Normal, false, false, {}, nullptr, {}, guardInputs);
        auto values = captured->values();
        auto badConfiguration = values.configuration->values(); badConfiguration.toolchain.executableSuffix = "cpp";
        values.configuration = Compilation::FrozenConfiguration::capture(badConfiguration);
        auto *guarded = new FileCompiler(Compilation::FileCompileRequest::capture(values));
        QPointer<Compiler> guardedWorker(guarded);
        manager->mCompiler = guarded; manager->trackWorker(guarded); manager->connectCapturedCompiler(guarded, false);
        QObject::disconnect(guarded, SIGNAL(compileErrorOccured(QString)), manager, nullptr);
        const QByteArray before = read(source);
        guarded->start();
        const bool guardFinished = waitFor([&] { return !manager->compiling() && (!guardedWorker || guardedWorker->isFinished()); }, 10000);
        check("A generated output/source collision fails with the original source bytes preserved",
              guardFinished && !manager->lastCompileOutcome().succeeded() && read(source) == before
              && QDir::fromNativeSeparators(manager->lastCompileOutcome().error).contains(QDir::fromNativeSeparators(source)));
        if (!guardFinished && guardedWorker) { guardedWorker->stopCompile(); return 10; }

#ifdef Q_OS_WIN
        if (QFileInfo(helper).isFile()) {
            // The fixtures are the only modified buffers in this QA profile.
            if (includedEditor) window.editorManager()->forceCloseEditor(includedEditor);
            if (editor) window.editorManager()->forceCloseEditor(editor);
            const QString closeMarker = fixture.filePath("close-tree-child.txt");
            PAnalysisInputSnapshot closeInputs;
            const auto closeRequest = manager->captureFileRequest(source, FileType::CppSource, ENCODING_UTF8,
                CppCompileType::Normal, false, false, {}, nullptr, {}, closeInputs);
            const auto retirementGate = std::make_shared<QSemaphore>();
            auto *closingCompiler = new TreeFixtureCompiler(closeRequest, helper, closeMarker, 30000, 15000, retirementGate);
            QPointer<Compiler> closingWorker(closingCompiler);
            manager->mCompiler = closingCompiler;
            manager->trackWorker(closingCompiler);
            manager->connectCapturedCompiler(closingCompiler, false);
            bool childStarted = false;
            const auto started = QObject::connect(closingCompiler, &Compiler::compileOutput, &window,
                [&](const QString &output) { bool pid = false; output.trimmed().toULongLong(&pid); childStarted |= pid; });
            closingCompiler->start();
            const bool startedBeforeClose = waitFor([&] { return childStarted || !manager->compiling(); }, 5000) && childStarted;
            QObject::disconnect(started);
            QElapsedTimer requestTime;
            requestTime.start();
            const bool closeAcceptedImmediately = window.close();
            const bool responsiveCloseRequest = requestTime.elapsed() < 750;
            const bool taskFinishedFirst = waitFor([&] { return !manager->compiling(); }, 3000);
            check("Window close waits for the registered compiler thread while the UI remains responsive",
                startedBeforeClose && responsiveCloseRequest && !closeAcceptedImmediately && taskFinishedFirst
                && window.isVisible() && window.isQuitting() && closingWorker && !closingWorker->isFinished());
            retirementGate->release();
            const bool retired = waitFor([&] { return !window.isVisible(); }, 5000);
            check("Window close commits after the worker acknowledgement and the child cannot publish output",
                retired && (!closingWorker || closingWorker->isFinished()) && manager->shutdownAcknowledged()
                && manager->lastCompileOwnership() == Compilation::ProcessOwnershipLevel::WindowsTreeQuiescent
                && !QFile::exists(closeMarker));
        }
#endif

        QFile output(QDir(outputDirectory).filePath("compiler-request-result.json"));
        const QByteArray bytes = QJsonDocument(QJsonObject{{"failures", failures}, {"checks", checks}}).toJson();
        if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size()) return 11;
        return failures == 0 ? 0 : 1;
    }
};

int runCompilerRequestChecks(MainWindow &window, const QString &outputDirectory)
{
    return CompilerRequestChecks::run(window, outputDirectory);
}
