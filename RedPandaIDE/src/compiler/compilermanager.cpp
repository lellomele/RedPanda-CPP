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
#include <algorithm>
#include <QUuid>
#include "projectcompiler.h"
#ifdef Q_OS_MACOS
#include <sys/posix_shm.h>
#endif

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

void CompilerManager::compile(const QString& filename, FileType fileType, const QByteArray& encoding, bool rebuild, CppCompileType compileType)
{
    if (!pSettings->compilerSets().defaultSet()) {
        QMessageBox::critical(mMainWindow,
                              tr("No compiler set"),
                              tr("No compiler set is configured.")+tr("Can't start debugging."));
        return;
    }
    {
        QMutexLocker locker(&mCompileMutex);
        if (mCompiler!=nullptr) {
            return;
        }
        mCompileErrorCount = 0;
        mCompileIssueCount = 0;
        //deleted when thread finished
#ifdef ENABLE_SDCC
        if (pSettings->compilerSets().defaultSet()->compilerType()==CompilerType::SDCC) {
            mCompiler = new SDCCFileCompiler(filename,encoding,compileType,false);
        } else
#endif
        if (fileType == FileType::NASM){
            mCompiler = new NASMFileCompiler(filename);
        } else  {
            mCompiler = new FileCompiler(filename,encoding,fileType,compileType,false);
        }
        mCompiler->setParserForFile(getParserForFile(filename));
        mCompiler->setRebuild(rebuild);
        trackWorker(mCompiler);
        connect(mCompiler, &Compiler::finished, mCompiler, &QObject::deleteLater);
        connect(mCompiler, &Compiler::compileFinished, this, &CompilerManager::onCompileFinished);
        connect(mCompiler, &Compiler::compileIssue, this, &CompilerManager::onCompileIssue);
        if (mMainWindow) {
            connect(mCompiler, &Compiler::compileStarted, mMainWindow, &MainWindow::onCompileStarted);
            connect(mCompiler, &Compiler::compileStarted, mMainWindow, &MainWindow::clearToolsOutput);

            connect(mCompiler, &Compiler::compileOutput, mMainWindow, &MainWindow::logToolsOutput);
            connect(mCompiler, &Compiler::compileIssue, mMainWindow, &MainWindow::onCompileIssue);
            connect(mCompiler, &Compiler::compileErrorOccured, mMainWindow, &MainWindow::onCompileErrorOccured);
        }
        mCompiler->start();
    }
}

void CompilerManager::compileProject(std::shared_ptr<Project> project, bool rebuild)
{
    if (!pSettings->compilerSets().defaultSet()) {
        QMessageBox::critical(mMainWindow,
                              tr("No compiler set"),
                              tr("No compiler set is configured.")+tr("Can't start debugging."));
        return;
    }
    {
        QMutexLocker locker(&mCompileMutex);
        if (mCompiler!=nullptr) {
            return;
        }
        mCompileErrorCount = 0;
        mCompileIssueCount = 0;
        //deleted when thread finished
        mCompiler = createProjectCompiler(project);
        mCompiler->setRebuild(rebuild);
        trackWorker(mCompiler);
        connect(mCompiler, &Compiler::finished, mCompiler, &QObject::deleteLater);
        connect(mCompiler, &Compiler::compileFinished, this, &CompilerManager::onCompileFinished);

        connect(mCompiler, &Compiler::compileIssue, this, &CompilerManager::onCompileIssue);
        if (mMainWindow) {
            connect(mCompiler, &Compiler::compileStarted, mMainWindow, &MainWindow::onProjectCompileStarted);
            connect(mCompiler, &Compiler::compileStarted, mMainWindow, &MainWindow::clearToolsOutput);

            connect(mCompiler, &Compiler::compileOutput, mMainWindow, &MainWindow::logToolsOutput);
            connect(mCompiler, &Compiler::compileIssue, mMainWindow, &MainWindow::onCompileIssue);
            connect(mCompiler, &Compiler::compileErrorOccured, mMainWindow, &MainWindow::onCompileErrorOccured);
        }
        mCompiler->start();
    }
}

void CompilerManager::cleanProject(std::shared_ptr<Project> project)
{
    if (!pSettings->compilerSets().defaultSet()) {
        QMessageBox::critical(mMainWindow,
                              tr("No compiler set"),
                              tr("No compiler set is configured.")+tr("Can't start debugging."));
        return;
    }
    {
        QMutexLocker locker(&mCompileMutex);
        if (mCompiler!=nullptr) {
            return;
        }
        mCompileErrorCount = 0;
        mCompileIssueCount = 0;
        //deleted when thread finished
        ProjectCompiler* compiler = createProjectCompiler(project);
        compiler->setOnlyClean(true);
        mCompiler = compiler;
        mCompiler->setRebuild(false);
        trackWorker(mCompiler);
        connect(mCompiler, &Compiler::finished, mCompiler, &QObject::deleteLater);
        connect(mCompiler, &Compiler::compileFinished, this, &CompilerManager::onCompileFinished);

        connect(mCompiler, &Compiler::compileIssue, this, &CompilerManager::onCompileIssue);
        if (mMainWindow) {
            connect(mCompiler, &Compiler::compileStarted, mMainWindow, &MainWindow::onProjectCompileStarted);
            connect(mCompiler, &Compiler::compileStarted, mMainWindow, &MainWindow::clearToolsOutput);

            connect(mCompiler, &Compiler::compileOutput, mMainWindow, &MainWindow::logToolsOutput);
            connect(mCompiler, &Compiler::compileIssue, mMainWindow, &MainWindow::onCompileIssue);
            connect(mCompiler, &Compiler::compileErrorOccured, mMainWindow, &MainWindow::onCompileErrorOccured);
        }
        mCompiler->start();
    }
}

void CompilerManager::buildProjectMakefile(std::shared_ptr<Project> project)
{
    if (!pSettings->compilerSets().defaultSet()) {
        QMessageBox::critical(mMainWindow,
                              tr("No compiler set"),
                              tr("No compiler set is configured.")+tr("Can't start debugging."));
        return;
    }
    {
        QMutexLocker locker(&mCompileMutex);
        if (mCompiler!=nullptr) {
            return;
        }
        ProjectCompiler* pCompiler=createProjectCompiler(project);
        pCompiler->buildMakeFile();
        delete pCompiler;
    }
}

void CompilerManager::checkSyntax(const QString &filename, const QByteArray& encoding, const QString &content, std::shared_ptr<Project> project)
{
    if (!pSettings->compilerSets().defaultSet()) {
        QMessageBox::critical(mMainWindow,
                              tr("No compiler set"),
                              tr("No compiler set is configured.")+tr("Can't start debugging."));
        return;
    }
    {
        QMutexLocker locker(&mBackgroundSyntaxCheckMutex);
        if (mBackgroundSyntaxChecker!=nullptr) {
            return;
        }

        mSyntaxCheckErrorCount = 0;
        mSyntaxCheckIssueCount = 0;

        //deleted when thread finished
        mBackgroundSyntaxChecker = new StdinCompiler(filename,encoding, content,true);
        if (!project)
            mBackgroundSyntaxChecker->setParserForFile(getParserForFile(filename));
        mBackgroundSyntaxChecker->setProject(project);
        trackWorker(mBackgroundSyntaxChecker);
        connect(mBackgroundSyntaxChecker, &Compiler::finished, mBackgroundSyntaxChecker, &QThread::deleteLater);
        connect(mBackgroundSyntaxChecker, &Compiler::compileIssue, this, &CompilerManager::onSyntaxCheckIssue);
        connect(mBackgroundSyntaxChecker, &Compiler::compileFinished, this, &CompilerManager::onSyntaxCheckFinished);
        if (mMainWindow) {
            connect(mBackgroundSyntaxChecker, &Compiler::compileStarted, mMainWindow, &MainWindow::onSyntaxCheckStarted);
            connect(mBackgroundSyntaxChecker, &Compiler::compileIssue, mMainWindow, &MainWindow::onCompileIssue);
            connect(mBackgroundSyntaxChecker, &Compiler::compileErrorOccured, mMainWindow, &MainWindow::onCompileErrorOccured);
            //connect(mBackgroundSyntaxChecker, &Compiler::compileOutput, mMainWindow, &MainWindow::logToolsOutput);
        }
        mBackgroundSyntaxChecker->start();
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
    mCompiler=nullptr;
    emit compileFinished(filename, false);
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

ProjectCompiler *CompilerManager::createProjectCompiler(std::shared_ptr<Project> project)
{
#ifdef ENABLE_SDCC
    if (project->options().type==ProjectType::MicroController)
        return new SDCCProjectCompiler(project);
    else
#endif
        return new ProjectCompiler(project);
}

PCppParser CompilerManager::getParserForFile(const QString &filename)
{
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

