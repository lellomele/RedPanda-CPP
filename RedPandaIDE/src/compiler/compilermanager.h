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
#ifndef COMPILERMANAGER_H
#define COMPILERMANAGER_H

#include <QObject>
#include <QSet>
#include <QThread>
#include <QMutex>
#include "qt_utils/utils.h"
#include "../utils.h"
#include "../utils/file.h"
#include "../common.h"
#include "filecompilerequest.h"
#include "../operationresult.h"
#include "../documentstore.h"

class MainWindow;
class Runner;
class Project;
class Compiler;
class ProjectCompiler;
class OJProblem;
using POJProblem = std::shared_ptr<OJProblem>;
class OJProblemCase;
using POJProblemCase = std::shared_ptr<OJProblemCase>;
class CppParser;
using PCppParser = std::shared_ptr<CppParser>;

class CompilerManager : public QObject
{
    Q_OBJECT
public:
    explicit CompilerManager(QObject *parent = nullptr);
    CompilerManager(const CompilerManager&)=delete;
    CompilerManager& operator=(const CompilerManager&)=delete;
    ~CompilerManager();

    bool compiling() const;
    bool backgroundSyntaxChecking() const;
    bool running() const;
    // Owner-thread registry observation; -1 means the calling thread is invalid.
    // A finished thread may still own objects until its queued disposal runs.
    int trackedWorkerCount() const;

    void compile(const QString& filename, FileType fileType, const QByteArray& encoding, bool rebuild, CppCompileType compileType,
                 Compilation::CompileIdentity identity = {});
    void compileProject(std::shared_ptr<Project> project, bool rebuild);
    void cleanProject(std::shared_ptr<Project> project);
    void buildProjectMakefile(std::shared_ptr<Project> project);
    void checkSyntax(const QString&filename, const QByteArray& encoding, const QString& content, std::shared_ptr<Project> project,
                     Compilation::CompileIdentity identity = {});
    OperationResult lastCompileOutcome() const { return mLastCompileOutcome; }
    OperationResult lastSyntaxCheckOutcome() const { return mLastSyntaxCheckOutcome; }
    CompilerType lastSyntaxCompilerType() const { return mLastSyntaxCompilerType; }
    bool canContinueLastCompile() const { return mCanContinueLastCompile; }
    Compilation::ProcessOwnershipLevel lastCompileOwnership() const { return mLastCompileOwnership; }
    Compilation::ProcessOwnershipLevel lastSyntaxCheckOwnership() const { return mLastSyntaxCheckOwnership; }
    void run(
            const QString& filename,
            const QString& arguments,
            const QString& workDir,
            const QStringList& extraBinDir);
    void runProblem(
            const QString& filename, const QString& arguments, const QString& workDir, POJProblemCase problemCase,
            const POJProblem& problem
            );
    void runProblem(const QString& filename, const QString& arguments, const QString& workDir, const QVector<POJProblemCase> &problemCases,
                    const POJProblem& problem
                    );
    void stopRun();
    void stopAllRunners();
    // GUI-thread shutdown gate: false keeps live tasks owned for a later retry.
    bool shutdown(int timeoutMilliseconds = 8000);
    void stopPausing();
    void stopCompile();
    void stopCheckSyntax();
    bool canCompile(const QString& filename) const;
    int compileErrorCount() const;

    int syntaxCheckErrorCount() const;

    int compileIssueCount() const;

    int syntaxCheckIssueCount() const;

    MainWindow *mainWindow() const;
    void setMainWindow(MainWindow *newMainWindow);

signals:
    void signalStopAllRunners();
    void compileFinished(const QString& filename, bool isSyntaxCheck);

private slots:
    void doRunProblem(const QString& filename, const QString& arguments, const QString& workDir, const QVector<POJProblemCase> &problemCases,
                      const POJProblem& problem
                      );
    void onRunnerTerminated();
    void onRunnerPausing();
    void onCompileFinished(const QString& filename);
    void onCompileIssue(PCompileIssue issue);
    void onSyntaxCheckFinished(const QString& filename);
    void onSyntaxCheckIssue(PCompileIssue issue);
private:
    friend class CompilerRequestChecks;
    void trackWorker(QThread *worker);
    ProjectCompiler* createProjectCompiler(std::shared_ptr<Project> project);
    PCppParser getParserForFile(const QString& filename);
    Compilation::FileCompileRequest::Pointer captureFileRequest(const QString &filename, FileType fileType,
        const QByteArray &encoding, CppCompileType compileType, bool syntaxOnly, bool rebuild,
        const QString &content, Project *project, Compilation::CompileIdentity identity, PAnalysisInputSnapshot &inputs);
    void connectCapturedCompiler(Compiler *compiler, bool syntaxOnly);
    bool diagnosticsAreCurrent(const Compilation::FileCompileRequest::Pointer &request) const;
private:
    MainWindow *mMainWindow;
    Compiler* mCompiler;
    int mCompileErrorCount;
    int mCompileIssueCount;
    int mSyntaxCheckErrorCount;
    int mSyntaxCheckIssueCount;
    Compiler* mBackgroundSyntaxChecker;
    Runner* mRunner;
    PNonExclusiveTemporaryFileOwner mTempFileOwner;
    // Mutated only on this QObject's GUI thread. Removal uses QThread::finished,
    // not an earlier task-specific completion signal.
    QSet<QThread *> mWorkers;
    quint64 mNextCompileJobId = 0;
    QHash<QString, Compilation::CapabilityFacts> mCapabilityCache;
    OperationResult mLastCompileOutcome = OperationResult::cancelled();
    OperationResult mLastSyntaxCheckOutcome = OperationResult::cancelled();
    CompilerType mLastSyntaxCompilerType = CompilerType::Unknown;
    bool mCanContinueLastCompile = false;
    Compilation::ProcessOwnershipLevel mLastCompileOwnership = Compilation::ProcessOwnershipLevel::NoProcessStarted;
    Compilation::ProcessOwnershipLevel mLastSyntaxCheckOwnership = Compilation::ProcessOwnershipLevel::NoProcessStarted;
    mutable QRecursiveMutex mCompileMutex;
    mutable QRecursiveMutex mBackgroundSyntaxCheckMutex;
    mutable QRecursiveMutex mRunnerMutex;
};

class CompileError : public BaseError {
public:
    explicit CompileError(const QString& reason);
};

#endif // COMPILERMANAGER_H
