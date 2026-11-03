/*
 * Copyright (C) 2020-2022 Roy Qu (royqh1979@gmail.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "compilermanager.h"
#include "filecompiler.h"
#include "../project.h"
#ifdef ENABLE_SDCC
#include "sdccfilecompiler.h"
#include "sdccprojectcompiler.h"
#endif
#include "nasmfilecompiler.h"
#include "stdincompiler.h"
#include "../mainwindow.h"
#include "../editormanager.h"
#include "executablerunner.h"
#include "ojproblemcasesrunner.h"
#include "../utils.h"
#include "../utils/pe.h"
#include "../utils/parsearg.h"
#include "../utils/terminal.h"
#include "../systemconsts.h"
#include "../settings.h"
#include <QMessageBox>
#include <QElapsedTimer>
#include <QStatusBar>
#include <algorithm>
#include <QUuid>
#include "projectcompiler.h"
#include "compilationcapture.h"
#include "projectbuildcapture.h"
#include "../analysis/parserconfigurationcapture.h"
#include <qt_utils/charsetinfo.h>
#include "frozencompilearguments.h"
#include <QTimer>
#include <QPointer>
#include <limits>
#include <qsynedit/document.h>

namespace {
bool projectInputsReady(const std::shared_ptr<Project> &project, MainWindow *window, bool foreground)
{
    if (!project)
        return true;
    const auto result = project->validateBuildInputs();
    if (result.succeeded())
        return true;
    if (foreground)
        QMessageBox::warning(window, QCoreApplication::translate("CompilerManager", "Project input conflict"), result.error);
    else if (window)
        window->statusBar()->showMessage(result.error, 15000);
    return false;
}
}
#ifdef Q_OS_MACOS
#include <sys/posix_shm.h>
#endif

int CompilerManager::trackedWorkerCount() const
{
    if (QThread::currentThread() != thread())
        return -1;
    return mWorkers.size();
}

CompilerManager::CompilerManager(QObject *parent) : QObject(parent),
    mCompileMutex(),
    mBackgroundSyntaxCheckMutex(),
    mRunnerMutex()
{
    mCompiler = nullptr;
    mBackgroundSyntaxChecker = nullptr;
    mRunner = nullptr;
    mSyntaxCheckErrorCount = 0;
    mSyntaxCheckIssueCount = 0;
    mCompileErrorCount = 0;
    mCompileIssueCount = 0;
    mSyntaxCheckErrorCount = 0;
    mMainWindow = nullptr;
}

CompilerManager::~CompilerManager()
{
    // Normal close paths must reject shutdown() == false before destroying the
    // window/settings. This fallback also drains tasks in startup/QA teardown.
    if (!shutdown())
        qCritical("Compiler workers are still stopping; active QThread objects were not deleted.");
}

void CompilerManager::trackWorker(QThread *worker)
{
    mWorkers.insert(worker);
    connect(worker, &QThread::finished, this, [this, worker] {
        mWorkers.remove(worker);
    });
    connect(worker, &QObject::destroyed, this, [this, worker] {
        mWorkers.remove(worker);
    });
}

bool CompilerManager::shutdown(int timeoutMilliseconds)
{
    if (QThread::currentThread() != thread())
        return false;
    const auto workers = mWorkers;
    for (QThread *worker : workers) {
        if (auto *compiler = qobject_cast<Compiler *>(worker))
            compiler->stopCompile();
        else if (auto *runner = qobject_cast<Runner *>(worker))
            runner->stop();
    }
    QElapsedTimer deadline;
    deadline.start();
    for (QThread *worker : workers) {
        const qint64 remaining = std::max<qint64>(0, timeoutMilliseconds - deadline.elapsed());
        if (!worker->wait(static_cast<unsigned long>(remaining)))
            return false;
        // Waiting does not pump the GUI queue. Clear only completed current
        // tasks here; queued callbacks also guard against stale sender objects.
        if (worker == mRunner) {
            mRunner = nullptr;
            mTempFileOwner.reset();
        }
        if (worker == mCompiler)
            mCompiler = nullptr;
        if (worker == mBackgroundSyntaxChecker)
            mBackgroundSyntaxChecker = nullptr;
        mWorkers.remove(worker);
    }
    return true;
}

Compilation::FileCompileRequest::Pointer CompilerManager::captureFileRequest(const QString &filename, FileType fileType,
    const QByteArray &encoding, CppCompileType compileType, bool syntaxOnly, bool rebuild,
    const QString &content, Project *project, Compilation::CompileIdentity identity, PAnalysisInputSnapshot &inputs)
{
    if (QThread::currentThread() != thread())
        throw CompileError(tr("Compilation must be submitted on its owner thread."));
    if (syntaxOnly && content.size() > 32LL * 1024 * 1024)
        throw CompileError(tr("The source exceeds the background syntax-check size limit (32 Mi characters)."));
    const auto set = project ? pSettings->compilerSets().getSet(project->options().compilerSet)
                             : pSettings->compilerSets().defaultSet();
    // Syntax-only consumes no linker flags or execution charset, so it must
    // neither copy unused rules nor await an autolink dependency graph.
    const bool autolinkUsed = !syntaxOnly && (fileType == FileType::CSource || fileType == FileType::CppSource);
    auto captured = Compilation::captureCompilerConfiguration(set, project,
        mMainWindow ? mMainWindow->macroVariables(false) : QMap<QString, QString>{}, autolinkUsed);
    if (!captured.preparation.ready()) throw CompileError(captured.preparation.reason);
    if (mNextCompileJobId == std::numeric_limits<quint64>::max())
        throw CompileError(tr("The compilation job identifier space is exhausted."));
    identity.jobId = ++mNextCompileJobId;
    if (mMainWindow) {
        const auto capturedInputs = mMainWindow->editorManager()->inputSnapshotProvider()();
        if (!capturedInputs.succeeded()) throw CompileError(capturedInputs.error);
        inputs = capturedInputs.snapshot;
        if (const auto source = inputs->document(filename)) {
            if ((identity.documentId && identity.documentId != source->id)
                    || (identity.textRevision && identity.textRevision != source->revision))
                throw CompileError(tr("The source revision changed before compilation was submitted."));
            if (syntaxOnly) {
                const auto *editor = mMainWindow->editorManager()->getOpenedEditor(filename);
                const QString separator = editor ? editor->document()->lineBreak() : QStringLiteral("\n");
                qsizetype position = 0;
                const QStringView submitted{content};
                for (qsizetype line = 0; line < source->lines.size(); ++line) {
                    const auto &text = source->lines.at(line);
                    if (position > content.size() || submitted.mid(position, text.size()) != QStringView{text})
                        throw CompileError(tr("The captured source text does not match its document revision."));
                    position += text.size();
                    if (line + 1 < source->lines.size()) {
                        if (submitted.mid(position, separator.size()) != QStringView{separator})
                            throw CompileError(tr("The captured source text does not match its document revision."));
                        position += separator.size();
                    }
                }
                if (position != content.size())
                    throw CompileError(tr("The captured source text does not match its document revision."));
            }
            identity.documentId = source->id; identity.textRevision = source->revision;
        }
    }
    Compilation::FileCompileValues values;
    values.identity = identity; values.filename = filename; values.fileType = fileType;
    const auto argumentUse = fileType == FileType::CppSource || fileType == FileType::CCppHeader
        ? Compilation::ArgumentUse::Cpp : Compilation::ArgumentUse::C;
    values.unavailableToolchainMacros = Compilation::unavailableToolchainMacros(captured.values, argumentUse, syntaxOnly);
    if (inputs)
        for (auto it = inputs->documents().cbegin(); it != inputs->documents().cend(); ++it)
            if (it.value()) values.openDocumentRevisions.insert(it.value()->id, it.value()->revision);
    values.originalSourceEncoding = encoding;
    values.sourceEncoding = syntaxOnly ? QByteArray(ENCODING_UTF8) : encoding;
    values.compileType = compileType; values.syntaxOnly = syntaxOnly; values.rebuild = rebuild;
    values.inputMode = syntaxOnly ? Compilation::FileInputMode::StandardInput : Compilation::FileInputMode::SavedDiskFile;
    values.standardInput = content;
    const QFileInfo disk(filename);
    const auto registered = inputs ? inputs->document(filename) : PDocumentSnapshot{};
    values.sourcePath = registered ? registered->absoluteFilename : disk.absoluteFilePath();
    const QFileInfo acceptedFile(values.sourcePath);
    values.workingDirectory = acceptedFile.absolutePath();
    values.diskSize = acceptedFile.size(); values.diskModifiedMilliseconds = acceptedFile.lastModified().toMSecsSinceEpoch();
    if (captured.values.autolink.rules.isEmpty()) {
        // No dependency can contribute a flag when the accepted catalogue is empty.
        captured.values.autolink.enabled = false;
        captured.values.autolink.complete = true;
    }
    if (captured.values.autolink.enabled) {
        Analysis::ParserConfigurationValues base;
        base.language = argumentUse == Compilation::ArgumentUse::Cpp ? ParserLanguage::CPlusPlus : ParserLanguage::C;
        if (captured.values.project) base.projectIncludePaths = captured.values.project->includeDirs;
        values.dependencyEncodings = {QByteArrayLiteral("UTF-8")};
        const auto addEncoding = [&](const QByteArray &codec) {
            if (!codec.isEmpty() && values.dependencyEncodings.size() < 64 && !values.dependencyEncodings.contains(codec))
                values.dependencyEncodings.append(codec);
        };
        addEncoding(captured.values.preferences.systemEncoding);
        const auto locales = pCharsetInfoManager->findCharsetByLocale(pCharsetInfoManager->localeName());
        for (const auto &locale : locales) addEncoding(locale->name);
        const auto probe = Analysis::captureParserConfiguration(set.get(), std::move(base), values.workingDirectory,
            captured.values.environment, values.dependencyEncodings.mid(0, 4));
        values.dependencyProbe = probe.request;
        values.dependencyPreparationError = probe.preparation.reason;
    }
    const auto capabilities = mCapabilityCache.constFind(captured.values.toolchain.identity);
    if (capabilities != mCapabilityCache.cend()) {
        captured.values.toolchain.supportNLS = capabilities->supportNLS;
        captured.values.toolchain.supportConvertingCharset = capabilities->supportConvertingCharset;
        captured.values.toolchain.compilerUsingUTF8 = capabilities->compilerUsingUTF8;
        values.capabilitiesKnown = true;
    }
    values.configuration = Compilation::FrozenConfiguration::capture(captured.values);
    return Compilation::FileCompileRequest::capture(values);
}

bool CompilerManager::diagnosticsAreCurrent(const Compilation::FileCompileRequest::Pointer &request) const
{
    if (!request || !request->values().identity.documentId || !mMainWindow) return true;
    const auto &wanted = request->values();
    auto *manager = mMainWindow->editorManager();
    auto *editor = manager->getOpenedEditor(wanted.filename);
    return manager->documentId(editor) == wanted.identity.documentId
            && manager->documentRevision(editor) == wanted.identity.textRevision;
}

void CompilerManager::connectCapturedCompiler(Compiler *compiler, bool syntaxOnly)
{
    const QPointer<Compiler> source(compiler);
    const auto request = compiler->fileRequest();
    connect(compiler, &Compiler::finished, compiler, &QObject::deleteLater);
    connect(compiler, &Compiler::compileFinished, this,
        syntaxOnly ? &CompilerManager::onSyntaxCheckFinished : &CompilerManager::onCompileFinished);
    connect(compiler, &Compiler::compileIssue, this,
        syntaxOnly ? &CompilerManager::onSyntaxCheckIssue : &CompilerManager::onCompileIssue);
    connect(compiler, &Compiler::toolchainCapabilitiesAvailable, this,
        [this](const QString &identity, bool nls, bool charset, bool utf8) {
            if (mCapabilityCache.size() >= 32 && !mCapabilityCache.contains(identity)) mCapabilityCache.clear();
            mCapabilityCache.insert(identity, {nls, charset, utf8});
        });
    if (!mMainWindow) return;
    connect(compiler, &Compiler::compileStarted, mMainWindow,
        syntaxOnly ? &MainWindow::onSyntaxCheckStarted : &MainWindow::onCompileStarted);
    if (!syntaxOnly) {
        connect(compiler, &Compiler::compileStarted, mMainWindow, &MainWindow::clearToolsOutput);
        connect(compiler, &Compiler::compileOutput, mMainWindow, &MainWindow::logToolsOutput);
    }
    connect(compiler, &Compiler::compileIssue, this, [this, source, request](PCompileIssue issue) {
        // Job execution retains its accepted input. Only editor hints are
        // rejected when a newer document revision owns a diagnostic filename.
        if (!source || !mMainWindow || !issue) return;
        if (!request) { mMainWindow->onCompileIssue(issue); return; }
        // The original diagnostic always remains available in the panel. Only
        // editor hints require a matching accepted document/revision.
        auto published = std::make_shared<CompileIssue>(*issue);
        published->hintEligibility = DiagnosticHintEligibility::Suppressed;
        if (diagnosticsAreCurrent(request)) {
            auto *manager = mMainWindow->editorManager();
            auto *editor = manager->getOpenedEditor(published->filename);
            const auto id = manager->documentId(editor);
            const auto accepted = request->values().openDocumentRevisions.constFind(id);
            if (accepted != request->values().openDocumentRevisions.cend()
                    && manager->documentRevision(editor) == accepted.value()) {
                published->hintEligibility = DiagnosticHintEligibility::CapturedDocument;
                published->diagnosticDocumentId = id;
                published->diagnosticTextRevision = accepted.value();
            }
        }
        mMainWindow->onCompileIssue(std::move(published));
    });
    connect(compiler, &Compiler::compileErrorOccured, this, [this, source, syntaxOnly](const QString &reason) {
        if (!source || !mMainWindow || mMainWindow->isQuitting()) return;
        if (syntaxOnly) {
            // Automatic checks report recoverable preparation/process errors
            // without interrupting typing or cancelling a foreground run task.
            mMainWindow->statusBar()->showMessage(reason, 10000);
            mMainWindow->logToolsOutput(tr("Syntax check could not finish: %1").arg(reason));
        } else if (source == mCompiler) {
            mMainWindow->onCompileErrorOccured(reason);
        }
    });
}

bool CompilerManager::compiling() const
{
    QMutexLocker locker(&mCompileMutex);
    return mCompiler!=nullptr;
}

bool CompilerManager::backgroundSyntaxChecking() const
{
    QMutexLocker locker(&mBackgroundSyntaxCheckMutex);
    return mBackgroundSyntaxChecker!=nullptr;
}

bool CompilerManager::running() const
{
    QMutexLocker locker(&mRunnerMutex);
    return (mRunner!=nullptr && !mRunner->pausing());
}

void CompilerManager::compile(const QString& filename, FileType fileType, const QByteArray& encoding,
    bool rebuild, CppCompileType compileType, Compilation::CompileIdentity identity)
{
    QMutexLocker locker(&mCompileMutex);
    if (mCompiler) return;
    std::unique_ptr<Compiler> candidate;
    try {
        PAnalysisInputSnapshot inputs;
        const auto request = captureFileRequest(filename, fileType, encoding, compileType, false, rebuild, {}, nullptr, identity, inputs);
        mCompileErrorCount = 0; mCompileIssueCount = 0;
        mLastCompileOutcome = OperationResult::cancelled();
        mLastCompileOwnership = Compilation::ProcessOwnershipLevel::NoProcessStarted;
        mCanContinueLastCompile = false;
#ifdef ENABLE_SDCC
        if (request->values().configuration->values().toolchain.compilerType == CompilerType::SDCC)
            candidate = std::make_unique<SDCCFileCompiler>(request);
        else
#endif
        if (fileType == FileType::NASM) candidate = std::make_unique<NASMFileCompiler>(request);
        else candidate = std::make_unique<FileCompiler>(request);
        mCompiler = candidate.get();
        trackWorker(mCompiler);
        connectCapturedCompiler(mCompiler, false);
        candidate->start();
        if (!candidate->isRunning() && !candidate->isFinished())
            throw CompileError(tr("The compilation worker could not start."));
        candidate.release();
    } catch (const BaseError &error) {
        mCompiler = nullptr; candidate.reset();
        mLastCompileOutcome = OperationResult::failure(error.reason());
        mCanContinueLastCompile = false;
        locker.unlock();
        QMessageBox::critical(mMainWindow, tr("Compilation could not start"), error.reason());
    }
}

void CompilerManager::compileProject(std::shared_ptr<Project> project, bool rebuild)
{
    submitProjectBuild(project, rebuild ? ProjectBuildAction::Rebuild : ProjectBuildAction::Build);
}

void CompilerManager::cleanProject(std::shared_ptr<Project> project)
{
    submitProjectBuild(project, ProjectBuildAction::Clean);
}

void CompilerManager::buildProjectMakefile(std::shared_ptr<Project> project)
{
    submitProjectBuild(project, ProjectBuildAction::GenerateMakefile);
}

void CompilerManager::submitProjectBuild(const std::shared_ptr<Project> &project, ProjectBuildAction action)
{
    if (!project || QThread::currentThread() != thread() || compiling() || !projectInputsReady(project, mMainWindow, true))
        return;
    auto set = pSettings->compilerSets().getSet(project->options().compilerSet);
    if (!set) set = pSettings->compilerSets().defaultSet();
    if (!set) {
        QMessageBox::critical(mMainWindow, tr("No compiler set"), tr("No compiler set is configured."));
        return;
    }

    const bool makefileOnly = action == ProjectBuildAction::GenerateMakefile;
    std::unique_ptr<ProjectCompiler> candidate;
    ActiveProjectBuild intent{project, action, {}, {}};
    try {
        auto captured = Compilation::captureCompilerConfiguration(set, project.get(),
            mMainWindow ? mMainWindow->macroVariables(false) : QMap<QString, QString>{}, false);
        if (!captured.preparation.ready()) throw CompileError(captured.preparation.reason);
        auto &values = captured.values;
        const auto capabilities = mCapabilityCache.constFind(values.toolchain.identity);
        const bool known = capabilities != mCapabilityCache.cend();
        if (known) {
            values.toolchain.supportNLS = capabilities->supportNLS;
            values.toolchain.supportConvertingCharset = capabilities->supportConvertingCharset;
            values.toolchain.compilerUsingUTF8 = capabilities->compilerUsingUTF8;
        }
        const bool custom = project->options().useCustomMakefile && !project->options().customMakefile.isEmpty();
        const auto driver = values.toolchain.cCompiler.isEmpty() ? values.toolchain.cppCompiler : values.toolchain.cCompiler;
        // A user-supplied Makefile can drive its own tools. Preserve that
        // path when the configured C/C++ driver is absent.
        const bool noProbe = known || (custom && (makefileOnly || !QFileInfo(driver).isExecutable()));
        auto snapshot = Compilation::captureProjectBuild(*project, std::move(values), noProbe,
            mMainWindow ? mMainWindow->editorManager()->documentInputGeneration() : 0);
        intent.makefile = snapshot.makefilePath;
        intent.output = snapshot.outputPath;
#ifdef ENABLE_SDCC
        if (snapshot.options.type == ProjectType::MicroController)
            candidate = std::make_unique<SDCCProjectCompiler>(std::move(snapshot));
        else
#endif
            candidate = std::make_unique<ProjectCompiler>(std::move(snapshot));
    } catch (const BaseError &error) {
        QMessageBox::critical(mMainWindow, tr("Compile Error"), error.reason());
        return;
    }

    QMutexLocker locker(&mCompileMutex);
    if (mCompiler) return;
    mCompileErrorCount = 0;
    mCompileIssueCount = 0;
    mLastCompileOutcome = OperationResult::cancelled();
    mLastCompileOwnership = Compilation::ProcessOwnershipLevel::NoProcessStarted;
    mCanContinueLastCompile = false;
    candidate->setOnlyClean(action == ProjectBuildAction::Clean);
    candidate->setOnlyGenerateMakefile(makefileOnly);
    candidate->setRebuild(action == ProjectBuildAction::Rebuild);
    mCompiler = candidate.release();
    mActiveProjectBuild = std::move(intent);
    trackWorker(mCompiler);
    connect(mCompiler, &Compiler::finished, mCompiler, &QObject::deleteLater);
    connect(mCompiler, &Compiler::compileFinished, this, &CompilerManager::onCompileFinished);
    connect(mCompiler, &Compiler::compileIssue, this, &CompilerManager::onCompileIssue);
    connect(mCompiler, &Compiler::toolchainCapabilitiesAvailable, this,
        [this](const QString &identity, bool nls, bool charset, bool utf8) {
            if (mCapabilityCache.size() >= 32 && !mCapabilityCache.contains(identity)) mCapabilityCache.clear();
            mCapabilityCache.insert(identity, {nls, charset, utf8});
        });
    if (mMainWindow) {
        if (!makefileOnly) {
            connect(mCompiler, &Compiler::compileStarted, mMainWindow, &MainWindow::onProjectCompileStarted);
            connect(mCompiler, &Compiler::compileStarted, mMainWindow, &MainWindow::clearToolsOutput);
            connect(mCompiler, &Compiler::compileOutput, mMainWindow, &MainWindow::logToolsOutput);
            connect(mCompiler, &Compiler::compileIssue, mMainWindow, &MainWindow::onCompileIssue);
        }
        connect(mCompiler, &Compiler::compileErrorOccured, mMainWindow, &MainWindow::onCompileErrorOccured);
    }
    mCompiler->start();
}

void CompilerManager::checkSyntax(const QString &filename, const QByteArray& encoding, const QString &content,
    std::shared_ptr<Project> project, Compilation::CompileIdentity identity)
{
    // Stdin syntax checks use the captured source and project option values,
    // not project-unit disk bindings or saved-file preconditions.
    QMutexLocker locker(&mBackgroundSyntaxCheckMutex);
    if (mBackgroundSyntaxChecker) return;
    std::unique_ptr<Compiler> candidate;
    try {
        FileType fileType = getFileType(filename);
        if (fileType == FileType::Other) fileType = FileType::CppSource;
        PAnalysisInputSnapshot inputs;
        const auto request = captureFileRequest(filename, fileType, encoding, CppCompileType::Normal,
            true, false, content, project.get(), identity, inputs);
        mSyntaxCheckErrorCount = 0; mSyntaxCheckIssueCount = 0;
        mLastSyntaxCheckOutcome = OperationResult::cancelled();
        mLastSyntaxCheckOwnership = Compilation::ProcessOwnershipLevel::NoProcessStarted;
        mLastSyntaxCompilerType = request->values().configuration->values().toolchain.compilerType;
        candidate = std::make_unique<StdinCompiler>(request);
        mBackgroundSyntaxChecker = candidate.get();
        trackWorker(mBackgroundSyntaxChecker);
        connectCapturedCompiler(mBackgroundSyntaxChecker, true);
        candidate->start();
        if (!candidate->isRunning() && !candidate->isFinished())
            throw CompileError(tr("The syntax-check worker could not start."));
        candidate.release();
    } catch (const BaseError &error) {
        mBackgroundSyntaxChecker = nullptr; candidate.reset();
        mLastSyntaxCheckOutcome = OperationResult::failure(error.reason());
        locker.unlock();
        if (mMainWindow) mMainWindow->statusBar()->showMessage(error.reason(), 10000);
        emit compileFinished(filename, true);
    }
}

void CompilerManager::run(
        const QString &filename,
        const QString &arguments,
        const QString &workDir,
        const QStringList& binDirs)
{
    QMutexLocker locker(&mRunnerMutex);
    if (mRunner!=nullptr && !mRunner->pausing()) {
        return;
    }
    QString redirectInputFilename;
    bool redirectInput=false;
    if (pSettings->executor().redirectInput()
            && !pSettings->executor().inputFilename().isEmpty()) {
        redirectInput =true;
        redirectInputFilename = pSettings->executor().inputFilename();
    }
    bool useCustomTerminal = pSettings->environment().useCustomTerminal();
    ExecutableRunner * execRunner;
    if (!PortableExecutable(filename).isWin32GuiApp()) {
        QString consolePauserPath = getFilePath(pSettings->dirs().appLibexecDir(), CONSOLE_PAUSER);
        QStringList execArgs = {consolePauserPath};
        if (redirectInput) {
            if (useCustomTerminal)
                execArgs << "--redirect-input" << redirectInputFilename;
            else
                execArgs << "--redirect-input" << "-";
        }
        if (pSettings->executor().pauseConsole())
            execArgs << "--pause-console";
#ifdef Q_OS_WIN
        if (pSettings->executor().enableVirualTerminalSequence())
            execArgs << "--enable-virtual-terminal-sequence";
        QString triplet = pSettings->compilerSets().defaultSet()->dumpMachine();
        if (triplet.contains("-linux-"))
            execArgs << "--run-in-wsl";
        QString sharedMemoryId = QUuid::createUuid().toString();
#else
        QString sharedMemoryId = "/r" + QUuid::createUuid().toString(QUuid::StringFormat::Id128);
#ifdef Q_OS_MACOS
        sharedMemoryId = sharedMemoryId.mid(0, PSHMNAMLEN);
#endif
#endif
        bool requireConsolePauser = execArgs.length() > 1;
        if (requireConsolePauser) {
            if (!fileExists(consolePauserPath)) {
                QMessageBox::critical(mMainWindow,
                                         tr("Can't find Console Pauser"),
                                         tr("Console Pauser \"%1\" doesn't exists!")
                                         .arg(consolePauserPath));
                return;
            }
            execArgs << "--shared-memory" << sharedMemoryId;

            // translation items: please keep sync with `tools/consolepauser/argparser.hpp`
            execArgs << "--add-translation" << "EXIT=" + tr("Press ANY key to exit...");
            execArgs << "--add-translation" << "USAGE_HEADER=" + tr("Process exited after");
            execArgs << "--add-translation" << "USAGE_RETURN_VALUE=" + tr("Return value");
            execArgs << "--add-translation" << "USAGE_CPU_TIME=" + tr("CPU time");
            execArgs << "--add-translation" << "USAGE_MEMORY=" + tr("Memory");

            // end of console pauser args
            execArgs << "--";
        } else {
            execArgs.clear();
        }
        execArgs << localizePath(filename);
        execArgs += parseArgumentsWithoutVariables(arguments);
        if (useCustomTerminal) {
            auto [filename, args, fileOwner] = wrapCommandForTerminalEmulator(
                pSettings->environment().terminalPath(),
                pSettings->environment().terminalArgumentsPattern(),
                execArgs,
                &pSettings->dirs()
            );
            //delete when thread finished
            execRunner = new ExecutableRunner(filename, args, workDir);
            mTempFileOwner = std::move(fileOwner);
        } else {
            //delete when thread finished
            execRunner = new ExecutableRunner(execArgs[0], execArgs.mid(1), workDir);
        }
        execRunner->setStartConsole(true);
        if (requireConsolePauser)
            execRunner->setShareMemoryId(sharedMemoryId);
    } else {
        //delete when thread finished
        execRunner = new ExecutableRunner(filename, parseArgumentsWithoutVariables(arguments), workDir);
    }
    if (redirectInput && !useCustomTerminal) {
        execRunner->setRedirectInput(true);
        execRunner->setRedirectInputFilename(redirectInputFilename);
    }
    execRunner->addBinDirs(binDirs);

#ifndef Q_OS_WIN
    execRunner->addBinDir(pSettings->dirs().appDir());
#endif

    mRunner = execRunner;

    // Retain a terminal-wrapper script until its worker actually finishes,
    // even if a paused console is detached and a new run starts.
    trackWorker(mRunner);
    const auto scriptOwner = std::shared_ptr<NonExclusiveTemporaryFileOwner>(std::move(mTempFileOwner));
    connect(mRunner, &Runner::finished, mRunner, [owner = scriptOwner]() mutable { owner.reset(); });
    connect(this, &CompilerManager::signalStopAllRunners, mRunner, &Runner::stop);
    connect(mRunner, &Runner::finished, this ,&CompilerManager::onRunnerTerminated);
    connect(mRunner, &Runner::finished, mRunner ,&Runner::deleteLater);
    connect(mRunner, &Runner::pausingForFinish, this ,&CompilerManager::onRunnerPausing);
    if (mMainWindow) {
        connect(mRunner, &Runner::finished, mMainWindow ,&MainWindow::onRunFinished);
        connect(mRunner, &Runner::pausingForFinish, mMainWindow ,&MainWindow::onRunPausingForFinish);
        connect(mRunner, &Runner::runErrorOccurred, mMainWindow ,&MainWindow::onRunErrorOccured);
    }
    mRunner->start();
}


void CompilerManager::runProblem(const QString &filename, const QString &arguments, const QString &workDir, POJProblemCase problemCase,
                                 const POJProblem& problem
                                 )
{
    QMutexLocker locker(&mRunnerMutex);
    doRunProblem(filename, arguments, workDir, QVector<POJProblemCase> {problemCase}, problem);

}

void CompilerManager::runProblem(const QString &filename, const QString &arguments, const QString &workDir, const QVector<POJProblemCase>& problemCases,
                                 const POJProblem& problem
                                 )
{
    QMutexLocker locker(&mRunnerMutex);
    doRunProblem(filename, arguments, workDir, problemCases, problem);
}
void CompilerManager::doRunProblem(const QString &filename, const QString &arguments, const QString &workDir, const QVector<POJProblemCase>& problemCases,
                                   const POJProblem& problem)
{
    if (mRunner!=nullptr) {
        return;
    }
    OJProblemCasesRunner * execRunner = new OJProblemCasesRunner(filename, parseArgumentsWithoutVariables(arguments), workDir, problemCases);
    mRunner = execRunner;
    if (pSettings->executor().enableCaseLimit()) {
        execRunner->setExecTimeout(pSettings->executor().caseTimeout());
        execRunner->setMemoryLimit(pSettings->executor().caseMemoryLimit()*1024); //convert kb to bytes
    }
    size_t timeLimit = problem->getTimeLimitInMilliseconds();
    size_t memoryLimit = problem->getMemoryLimitInBytes();
    if (timeLimit>0)
        execRunner->setExecTimeout(timeLimit);
    if (memoryLimit)
        execRunner->setMemoryLimit(memoryLimit);
    trackWorker(mRunner);
    connect(this, &CompilerManager::signalStopAllRunners, mRunner, &Runner::stop);
    connect(mRunner, &Runner::finished, this ,&CompilerManager::onRunnerTerminated);
    connect(mRunner, &Runner::finished, mRunner ,&Runner::deleteLater);
    if (mMainWindow) {
        connect(mRunner, &Runner::finished, mMainWindow ,&MainWindow::onRunProblemFinished);
        connect(mRunner, &Runner::runErrorOccurred, mMainWindow ,&MainWindow::onRunErrorOccured);
        connect(execRunner, &OJProblemCasesRunner::caseStarted, mMainWindow, &MainWindow::onOJProblemCaseStarted);
        connect(execRunner, &OJProblemCasesRunner::caseFinished, mMainWindow, &MainWindow::onOJProblemCaseFinished);
        connect(execRunner, &OJProblemCasesRunner::newOutputGetted, mMainWindow, &MainWindow::onOJProblemCaseNewOutputGetted);
        connect(execRunner, &OJProblemCasesRunner::resetOutput, mMainWindow, &MainWindow::onOJProblemCaseResetOutput);
        if (pSettings->executor().redirectStderrToToolLog())  {
            connect(execRunner, &OJProblemCasesRunner::logStderrOutput, mMainWindow, &MainWindow::logToolsOutput);
        }
    }
    mRunner->start();
}

void CompilerManager::stopRun()
{
    QMutexLocker locker(&mRunnerMutex);
    if (mRunner!=nullptr) {
        // Stop is asynchronous. Keep ownership and the finished connection
        // until the worker and child process have released their resources.
        mRunner->stop();
    }
}

void CompilerManager::stopAllRunners()
{
    emit signalStopAllRunners();
}

void CompilerManager::stopPausing()
{
    QMutexLocker locker(&mRunnerMutex);
    if (mRunner!=nullptr && mRunner->pausing()) {
        mRunner->stop();
    }
}

void CompilerManager::stopCompile()
{
    QMutexLocker locker(&mCompileMutex);
    if (mCompiler!=nullptr)
        mCompiler->stopCompile();
}

void CompilerManager::stopCheckSyntax()
{
    QMutexLocker locker(&mBackgroundSyntaxCheckMutex);
    if (mBackgroundSyntaxChecker!=nullptr)
        mBackgroundSyntaxChecker->stopCompile();
}

bool CompilerManager::canCompile(const QString &) const
{
    return !compiling();
}

void CompilerManager::onCompileFinished(const QString& filename)
{
    QMutexLocker locker(&mCompileMutex);
    if (sender() != mCompiler)
        return;
    mLastCompileOutcome = mCompiler->taskOutcome();
    mLastCompileOwnership = mCompiler->processOwnershipLevel();
    mCanContinueLastCompile = mLastCompileOutcome.succeeded();
    if (const auto request = mCompiler->fileRequest()) {
        if (request->values().identity.documentId) {
            auto *editor = mMainWindow ? mMainWindow->editorManager()->getOpenedEditor(request->values().filename) : nullptr;
            mCanContinueLastCompile = mCanContinueLastCompile && editor
                && mMainWindow->editorManager()->documentId(editor) == request->values().identity.documentId;
        }
    }
    const auto intent = std::exchange(mActiveProjectBuild, std::nullopt);
    bool openMakefile = false;
    if (intent) {
        const auto project = intent->project.lock();
        const bool current = project && mMainWindow && mMainWindow->project() == project;
        openMakefile = mLastCompileOutcome.succeeded() && current && project->makeFileName() == intent->makefile;
        mCanContinueLastCompile = mCanContinueLastCompile && current &&
            (intent->action == ProjectBuildAction::Build || intent->action == ProjectBuildAction::Rebuild) &&
            project->outputFilename() == intent->output;
    }
    mCompiler = nullptr;
    locker.unlock();
    if (intent && intent->action == ProjectBuildAction::GenerateMakefile) {
        if (openMakefile) emit projectMakefileReady(intent->makefile);
    } else {
        emit compileFinished(filename, false);
    }
}

void CompilerManager::onRunnerTerminated()
{
    QMutexLocker locker(&mRunnerMutex);
    if (sender() != mRunner)
        return;
    mRunner=nullptr;
    mTempFileOwner=nullptr;
}

void CompilerManager::onRunnerPausing()
{
    QMutexLocker locker(&mRunnerMutex);
    if (sender() != mRunner)
        return;
    disconnect(mRunner, &Runner::finished, this ,&CompilerManager::onRunnerTerminated);
    if (mMainWindow) {
        disconnect(mRunner, &Runner::finished, mMainWindow ,&MainWindow::onRunFinished);
        disconnect(mRunner, &Runner::runErrorOccurred, mMainWindow ,&MainWindow::onRunErrorOccured);
    }
    mRunner=nullptr;
    mTempFileOwner=nullptr;
}

void CompilerManager::onCompileIssue(PCompileIssue issue)
{
    if (issue->type == CompileIssueType::Error)
        mCompileErrorCount++;
    mCompileIssueCount++;
}

void CompilerManager::onSyntaxCheckFinished(const QString& filename)
{
    QMutexLocker locker(&mBackgroundSyntaxCheckMutex);
    if (sender() != mBackgroundSyntaxChecker)
        return;
    mLastSyntaxCheckOutcome = mBackgroundSyntaxChecker->taskOutcome();
    mLastSyntaxCheckOwnership = mBackgroundSyntaxChecker->processOwnershipLevel();
    mBackgroundSyntaxChecker=nullptr;
    emit compileFinished(filename, true);
}

void CompilerManager::onSyntaxCheckIssue(PCompileIssue issue)
{
    if (issue->type == CompileIssueType::Error)
        mSyntaxCheckErrorCount++;
    if (issue->type == CompileIssueType::Error ||
            issue->type == CompileIssueType::Warning)
        mSyntaxCheckIssueCount++;
}

PCppParser CompilerManager::getParserForFile(const QString &filename)
{
    if (!mMainWindow) return {};
    Editor* editor = mMainWindow->editorManager()->getOpenedEditor(filename);
    if (editor)
        return editor->parser();
    return PCppParser();
}

MainWindow *CompilerManager::mainWindow() const
{
    return mMainWindow;
}

void CompilerManager::setMainWindow(MainWindow *newMainWindow)
{
    mMainWindow = newMainWindow;
}

int CompilerManager::syntaxCheckIssueCount() const
{
    return mSyntaxCheckIssueCount;
}

int CompilerManager::compileIssueCount() const
{
    return mCompileIssueCount;
}

int CompilerManager::syntaxCheckErrorCount() const
{
    return mSyntaxCheckErrorCount;
}

int CompilerManager::compileErrorCount() const
{
    return mCompileErrorCount;
}

CompileError::CompileError(const QString &reason):BaseError(reason)
{

}

