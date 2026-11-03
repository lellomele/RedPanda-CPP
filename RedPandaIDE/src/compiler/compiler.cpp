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
#include "compiler.h"
#include "../analysis/parsertoolchainprobe.h"
#include "../parser/cpppreprocessor.h"
#include "frozencompilearguments.h"
#include "toolchaincapabilities.h"
#include "processcancellation.h"
#include "processtreecompletion.h"
#include <exception>
#include "compilerinfo.h"
#include "../utils.h"
#include "../utils/escape.h"
#include "../utils/parsearg.h"
#include "../utils/parsemacros.h"
#include "compilermanager.h"
#include "../systemconsts.h"

#include <cmath>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QFile>
#include <QSaveFile>
#include <QDir>
#include <QStringDecoder>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QProcess>
#include <QString>
#include <QTime>
#include <QApplication>
#include "../editor.h"
#include "../mainwindow.h"
#include "../editormanager.h"
#include "../parser/cppparser.h"
#include "../autolinkmanager.h"
#include "qt_utils/charsetinfo.h"
#include "../project.h"
#include "../pathidentity.h"

#define COMPILE_PROCESS_END "---//END//----"

Compiler::Compiler(const QString &filename, bool onlyCheckSyntax): Compiler(filename, onlyCheckSyntax, true)
{
}

Compiler::Compiler(const QString &filename, bool onlyCheckSyntax, bool captureLegacyMacros):
    QThread{},
    mOnlyCheckSyntax{onlyCheckSyntax},
    mFilename{filename},
    mRebuild{false},
    mParserForFile{},
    mForceEnglishOutput{false}
{
    mParserForFile = nullptr;
    if (!filename.isEmpty()) mProtectedInputPaths.append(filename);
    // CompilerManager constructs each task on the GUI thread, before start().
    // Worker argument expansion must not read editor widgets or window lifetime.
    if (captureLegacyMacros && pMainWindow && QThread::currentThread() == pMainWindow->thread())
        mMacroVariables = pMainWindow->macroVariables();
    // Snapshot QA opt-in on the GUI thread. A delayed worker must not depend
    // on a test helper restoring the environment or on GUI lifetime.
    const auto *application = QCoreApplication::instance();
    if (application && QThread::currentThread() == application->thread()
            && !application->property("uiTestConfigPath").toString().isEmpty()) {
        mQaProcessTracePath = qEnvironmentVariable("REDPANDA_QA_COMPILER_TRACE");
        if (!mQaProcessTracePath.isEmpty()) {
            QElapsedTimer reference;
            reference.start();
            mQaTraceReference = reference.msecsSinceReference();
        }
    }
}

Compiler::Compiler(Compilation::FileCompileRequest::Pointer request):
    Compiler(request ? request->values().filename : QString{}, request && request->values().syntaxOnly, false)
{
    if (!request || !request->values().configuration)
        throw CompileError(tr("Compilation requires a captured immutable request."));
    if (request->values().maximumDiagnosticBytes < 0 || request->values().maximumConvertedOutputBytes < 0)
        throw CompileError(tr("Compilation output budgets are invalid."));
    mFileRequest = std::move(request);
    if (mFileRequest) {
        mProtectedInputPaths = {mFileRequest->values().sourcePath};
        mRebuild = mFileRequest->values().rebuild;
        if (mFileRequest->values().configuration)
            mMacroVariables = mFileRequest->values().configuration->values().macros;
    }
}

void Compiler::setPreparationMailbox(std::shared_ptr<Compilation::PreparationMailbox> mailbox)
{
    Q_ASSERT(QThread::currentThread() == thread() && !isRunning());
    mPreparationMailbox = std::move(mailbox);
}

const Compilation::ConfigurationValues &Compiler::configuration() const
{
    if (!mResolvedConfiguration)
        throw CompileError(tr("Compilation has no prepared immutable configuration."));
    return mResolvedConfiguration->values();
}

void Compiler::validateGeneratedOutputPath(const QString &path) const
{
    const auto spelling = [](const QString &value) {
        const QFileInfo file(value);
        return exactPathSpelling(file.absoluteFilePath());
    };
    const QString target = spelling(path);
    if (!NativePathKey(target).valid())
        throw CompileError(tr("The generated output path is invalid: '%1'.").arg(path));
    const auto wouldReplace = [&](const QString &source) {
        if (source.isEmpty()) return false;
        const QString input = spelling(source);
        if (input == target || sameExistingFile(input, target) == ExistingFileMatch::Same) return true;
        // Canonical spelling also protects an existing symbolic alias when a
        // filesystem does not expose native IDs. It does not fold Unicode or
        // mistake a distinct case-sensitive Windows file for the input.
        const QString sourceCanonical = QFileInfo(input).canonicalFilePath();
        const QString targetCanonical = QFileInfo(target).canonicalFilePath();
        if (!sourceCanonical.isEmpty() && !targetCanonical.isEmpty()
                && exactPathSpelling(sourceCanonical) == exactPathSpelling(targetCanonical)) return true;
        return false;
    };
    for (const auto &source : mProtectedInputPaths)
        if (wouldReplace(source))
            throw CompileError(tr("The generated output would replace an input file: '%1'.").arg(path));
    if (mResolvedConfiguration) {
        const auto &toolchain = configuration().toolchain;
        for (const auto &program : {toolchain.cCompiler, toolchain.cppCompiler, toolchain.make,
                                   toolchain.resourceCompiler, configuration().preferences.nasmPath})
            if (wouldReplace(program))
                throw CompileError(tr("The generated output would replace a compiler program: '%1'.").arg(path));
        for (const auto &program : toolchain.additionalPrograms)
            if (wouldReplace(program))
                throw CompileError(tr("The generated output would replace a compiler program: '%1'.").arg(path));
    }
}

void Compiler::writeQaProcessTrace(const char *stage, const QProcess *process, int started) const
{
    if (mQaProcessTracePath.isEmpty())
        return;
    QElapsedTimer now;
    now.start();
    const qint64 reference = now.msecsSinceReference();
    QJsonObject event{{"stage", QString::fromLatin1(stage)},
                      {"job", QString::number(reinterpret_cast<quintptr>(this), 16)},
                      {"monotonicMilliseconds", double(reference)},
                      {"elapsedMilliseconds", double(reference - mQaTraceReference)},
                      {"stopRequested", mStop->load(std::memory_order_acquire)},
                      {"stopMonotonicMilliseconds", double(mQaStopReference.load(std::memory_order_acquire))}};
    if (process) {
        event.insert("program", QFileInfo(process->program()).fileName());
        event.insert("processState", int(process->state()));
        event.insert("processId", double(process->processId()));
        event.insert("processError", int(process->error()));
    }
    if (started >= 0)
        event.insert("started", started != 0);
    // Only opt-in workers append metadata, never source text or command args.
    // Serialize simultaneous foreground/background QA jobs into complete lines.
    static QMutex traceMutex;
    QMutexLocker locker(&traceMutex);
    QFile file(mQaProcessTracePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append))
        file.write(QJsonDocument(event).toJson(QJsonDocument::Compact) + '\n');
}

void Compiler::run()
{
    writeQaProcessTrace("task-enter");
    emit compileStarted();
    auto action = finally([this]{
        if (mResolvedConfiguration) error(COMPILE_PROCESS_END);
        if (mStop->load(std::memory_order_acquire))
            mTaskOutcome = OperationResult::cancelled();
        else if (mErrorCount > 0 && mTaskOutcome.succeeded())
            mTaskOutcome = OperationResult::failure(tr("Compilation reported errors."));
        writeQaProcessTrace("task-exit");
        emit compileFinished(mFilename);
    });
    if (!mFileRequest)
        mProcessOwnershipLevel = Compilation::ProcessOwnershipLevel::DriverOnlyUnverifiedTree;
    try {
        if (mStop->load(std::memory_order_acquire))
            return;
        if (mFileRequest) {
            if (!mFileRequest->values().unavailableToolchainMacros.isEmpty())
                throw CompileError(tr("Toolchain directory macros are not available: %1. Open Compiler Options and configure the toolchain directories before using these macros.")
                    .arg(mFileRequest->values().unavailableToolchainMacros.join(QStringLiteral(", "))));
            auto prepared = mFileRequest->values().configuration;
            if (mPreparationMailbox) {
                log(tr("Waiting for code analysis..."));
                const auto delivery = mPreparationMailbox->wait(5000, *mStop);
                if (!delivery.preparation.ready()) {
                    if (delivery.preparation.status == Compilation::PreparationStatus::Cancelled) return;
                    mTaskOutcome = delivery.preparation.status == Compilation::PreparationStatus::Conflict
                        ? OperationResult::conflict(delivery.preparation.reason)
                        : OperationResult::failure(delivery.preparation.reason);
                    emit compileErrorOccured(delivery.preparation.reason);
                    return;
                }
                prepared = delivery.configuration;
            }
            if (!prepared)
                throw CompileError(tr("Compilation has no captured configuration."));
            if (prepared->values().autolink.enabled && !prepared->values().autolink.complete) {
                const auto &request = mFileRequest->values();
                const bool cpp = request.fileType == FileType::CppSource || request.fileType == FileType::CCppHeader;
                const auto &toolchain = prepared->values().toolchain;
                const QString program = cpp ? toolchain.cppCompiler : toolchain.cCompiler;
                if (!QFileInfo(program).isFile() || !QFileInfo(program).isExecutable())
                    throw CompileError(tr("The compiler executable is unavailable: '%1'.").arg(program));
                if (!request.dependencyProbe)
                    throw CompileError(request.dependencyPreparationError.isEmpty()
                        ? tr("The source dependencies could not be prepared.") : request.dependencyPreparationError);
                const QFileInfo disk(request.sourcePath);
                if (!disk.isFile() || disk.size() != request.diskSize
                        || disk.lastModified().toMSecsSinceEpoch() != request.diskModifiedMilliseconds) {
                    const QString reason = tr("The saved source changed before dependency analysis. Build it again.");
                    mTaskOutcome = OperationResult::conflict(reason);
                    emit compileErrorOccured(reason);
                    return;
                }
                log(tr("Reading the saved source dependencies..."));
                const auto discovered = Analysis::discoverParserToolchain(*request.dependencyProbe, *mStop);
                if (discovered.ownership != Compilation::ProcessOwnershipLevel::NoProcessStarted)
                    mProcessOwnershipLevel = discovered.ownership;
                if (discovered.preparation.status == Compilation::PreparationStatus::Cancelled) return;
                if (!discovered.preparation.ready() || !discovered.configuration)
                    throw CompileError(discovered.preparation.reason.isEmpty()
                        ? tr("The source dependencies could not be prepared.") : discovered.preparation.reason);
                CppPreprocessor::DependencyRequest dependency;
                dependency.root = exactPathSpelling(request.sourcePath);
                dependency.includePaths = discovered.configuration->includePaths;
                dependency.projectIncludePaths = discovered.configuration->projectIncludePaths;
                dependency.hardDefines = discovered.configuration->hardDefineLines;
                dependency.readPolicy.workingDirectory = request.workingDirectory;
                dependency.readPolicy.encodings = request.dependencyEncodings;
                dependency.cancelled = mStop;
                const auto observed = CppPreprocessor::scanDependencies(dependency);
                if (observed.status == CppPreprocessor::DependencyStatus::Cancelled) return;
                auto values = prepared->values();
                if (observed.status == CppPreprocessor::DependencyStatus::UnresolvedInclude) {
                    // An unresolved directive can be an ordinary source error
                    // or use include semantics only the real compiler knows.
                    // Let GCC diagnose the source, without a partial link graph.
                    log(tr("Autolink was skipped for this build: %1").arg(observed.error));
                    values.autolink = {};
                } else {
                    if (!observed.complete()) throw CompileError(observed.error);
                    const auto root = observed.reads.constFind(dependency.root);
                    if (root == observed.reads.cend() || root->stamp.size != request.diskSize
                            || root->stamp.modifiedMilliseconds != request.diskModifiedMilliseconds) {
                        const QString reason = tr("The saved source changed during dependency analysis. Build it again.");
                        mTaskOutcome = OperationResult::conflict(reason);
                        emit compileErrorOccured(reason);
                        return;
                    }
                    values.autolink.directIncludes = observed.directIncludes;
                    values.autolink.complete = true;
                    values.autolink.freshness = Compilation::AutolinkValues::Freshness::VerifiedReadManifest;
                    values.autolink.analysisIdentity = QStringLiteral("saved-disk:%1:%2")
                        .arg(QString::fromLatin1(discovered.configuration->compilerPreparationIdentity.toHex()),
                             QString::fromLatin1(root->rawSha256.toHex()));
                    values.autolink.nodeBudget = 8192;
                    values.autolink.edgeBudget = 65536;
                }
                prepared = Compilation::FrozenConfiguration::capture(values);
            }
            if (!mFileRequest->values().capabilitiesKnown) {
                log(tr("Preparing the compiler toolchain..."));
                const auto probe = Compilation::probeToolchainCapabilities(prepared->values(), *mStop);
                if (probe.ownership != Compilation::ProcessOwnershipLevel::NoProcessStarted)
                    mProcessOwnershipLevel = probe.ownership;
                if (!probe.preparation.ready()) {
                    if (probe.preparation.status == Compilation::PreparationStatus::Cancelled) return;
                    throw CompileError(probe.preparation.reason);
                }
                auto values = prepared->values();
                values.toolchain.supportNLS = probe.facts.supportNLS;
                values.toolchain.supportConvertingCharset = probe.facts.supportConvertingCharset;
                values.toolchain.compilerUsingUTF8 = probe.facts.compilerUsingUTF8;
                prepared = Compilation::FrozenConfiguration::capture(values);
                emit toolchainCapabilitiesAvailable(values.toolchain.identity, probe.facts.supportNLS,
                    probe.facts.supportConvertingCharset, probe.facts.compilerUsingUTF8);
            }
            mResolvedConfiguration = std::move(prepared);
            mResolvedAutolink = configuration().autolink.resolve(exactPathSpelling(mFileRequest->values().sourcePath));
            if (!mResolvedAutolink.complete)
                throw CompileError(mResolvedAutolink.error);
        }
        mTaskOutcome = OperationResult::success();
        if (!prepareForCompile()){
            mTaskOutcome = OperationResult::failure(tr("Compilation preparation could not be completed."));
            return;
        }
        if (mStop->load(std::memory_order_acquire)) return;
        if (mRebuild && !prepareForRebuild()) {
            throw CompileError(tr("Clean before rebuild failed."));
        }
        mErrorCount = 0;
        mWarningCount = 0;
        QElapsedTimer timer;
        timer.start();
        runCommand(mCompiler, mArguments, mDirectory, pipedText());
        for(int i=0;i<mExtraArgumentsList.count();i++) {
            if (mStop->load(std::memory_order_acquire) || !mTaskOutcome.succeeded())
                break;
            if (!beforeRunExtraCommand(i)) {
                mTaskOutcome = OperationResult::failure(tr("A required intermediate compilation artifact was not produced."));
                log(mTaskOutcome.error);
                break;
            }
            QString command = escapeCommandForLog(mExtraCompilersList[i], mExtraArgumentsList[i]);
            if (mExtraOutputFilesList[i].isEmpty()) {
                log(tr(" - Command: %1").arg(command));
            } else {
                log(tr(" - Command: %1 > %2").arg(command, escapeArgumentForPlatformShell(mExtraOutputFilesList[i], false)));
            }
            runCommand(mExtraCompilersList[i],mExtraArgumentsList[i],mDirectory, pipedText(),mExtraOutputFilesList[i]);
        }
        log("");
        log(tr("Compile Result:"));
        log("------------------");
        log(tr("- Errors: %1").arg(mErrorCount));
        log(tr("- Warnings: %1").arg(mWarningCount));
        if (!mOutputFile.isEmpty()) {
            log(tr("- Output Filename: %1").arg(mOutputFile));
            QLocale locale = QLocale::system();
            log(tr("- Output Size: %1").arg(locale.formattedDataSize(QFileInfo(mOutputFile).size())));
        }
        log(tr("- Compilation Time: %1 secs").arg(timer.elapsed() / 1000.0));
    } catch (const BaseError &e) {
        mTaskOutcome = OperationResult::failure(e.reason());
        ++mErrorCount;
        emit compileErrorOccured(e.reason());
    } catch (const std::exception &e) {
        mTaskOutcome = OperationResult::failure(QString::fromUtf8(e.what()));
        ++mErrorCount;
        emit compileErrorOccured(tr("Compilation task was cancelled: %1").arg(QString::fromUtf8(e.what())));
    }

}

QString Compiler::getFileNameFromOutputLine(QString &line) {
    QString temp;
    line = line.trimmed();
    while (true) {
        int pos;
        if (line.length() > 2 && line[1]==':') { // full file path at start, ignore this ':'
            pos = line.indexOf(':',2);
        } else {
            pos = line.indexOf(':');
        }
        if ( pos < 0) {
            break;
        }
        temp = line.mid(0,pos);
        line.remove(0,pos+1);
        line=line.trimmed();
        if (temp.compare("<stdin>", Qt::CaseInsensitive)==0 ) {
            temp = mFilename;
            return temp;
        } else if (temp.compare("{standard input}", Qt::CaseInsensitive)==0 ) {
            temp = mFilename;
            return temp;
        }

        QFileInfo fileInfo(temp);
        if (fileInfo.fileName() == QLatin1String("ld.exe")) { // skip ld.exe
            continue;
        } else if (fileInfo.fileName() == QLatin1String("make")) { // skip make.exe
            continue;
        } else if (fileInfo.fileName() == QLatin1String("mingw32-make")) { // skip mingw32-make.exe
            continue;
        } else if (fileInfo.suffix()=="o") { // skip obj file
            continue;
        } else {
            break;
        }
    }
    if (!mDirectory.isEmpty()) {
        QFileInfo info(temp);
        return info.isRelative()?generateAbsolutePath(mDirectory,temp):cleanPath(temp);
    }
    return temp;
}

int Compiler::getLineNumberFromOutputLine(QString &line)
{
    line = line.trimmed();
    int pos = line.indexOf(':');
    int result=0;
    if (pos < 0) {
        pos = line.indexOf(',');
    }
    if (pos>=0) {
        result = QStringView(line.constData(), pos).toInt();
        if (result > 0)
            line.remove(0,pos+1);
    } else {
        result = line.toInt();
        if (result > 0)
            line="";
    }
    return result;
}

int Compiler::getColunmnFromOutputLine(QString &line)
{
    line = line.trimmed();
    int pos = line.indexOf(':');
    int result=0;
    if (pos < 0) {
        pos = line.indexOf(',');
    }
    if (pos>=0) {
        result = QStringView(line.constData(), pos).toInt();
        if (result > 0)
            line.remove(0,pos+1);
    }
    return result;
}

CompileIssueType Compiler::getIssueTypeFromOutputLine(QString &line)
{
    CompileIssueType result = CompileIssueType::Other;
    line = line.trimmed();
    if (line.startsWith(tr("error:"))) {
        mErrorCount += 1;
        line = tr("[Error] ")+line.mid(tr("error:").length());
        result = CompileIssueType::Error;
    } else if (line.startsWith(tr("warning:"))) {
        mWarningCount += 1;
        line = tr("[Warning] ")+line.mid(tr("warning:").length());
        result = CompileIssueType::Warning;
    } else {
        int pos = line.indexOf(':');
        if (pos>=0) {
            QString s=line.mid(0,pos);
            if (s == "error" || s == "fatal error"
                    || s == "syntax error") {
                mErrorCount += 1;
                line = tr("[Error] ")+line.mid(pos+1);
                result = CompileIssueType::Error;
            } else if (s.startsWith("warning")
                       || s.startsWith(tr("warning"))) {
                mWarningCount += 1;
                line = tr("[Warning] ")+line.mid(pos+1);
                result = CompileIssueType::Warning;
            } else if (s == "info"
                       || s == tr("info")) {
                mWarningCount += 1;
                line = tr("[Info] ")+line.mid(pos+1);
                result = CompileIssueType::Info;
            } else if (s == "note"
                       || s == tr("note")) {
                mWarningCount += 1;
                line = tr("[Note] ")+line.mid(pos+1);
                result = CompileIssueType::Note;
            }
        }
    }

    return result;
}

PCompilerSet Compiler::compilerSet()
{
    if (mFileRequest)
        throw CompileError(tr("A captured compilation must not read live compiler settings."));
    if (mProject) {
        int index = mProject->options().compilerSet;
        PCompilerSet set = pSettings->compilerSets().getSet(index);
        if (set)
            return set;
    }
    return pSettings->compilerSets().defaultSet();
}

QByteArray Compiler::pipedText()
{
    return QByteArray();
}

bool Compiler::beforeRunExtraCommand(int /* idx */)
{
    return true;
}

void Compiler::processOutput(QString &line)
{
    if (line == COMPILE_PROCESS_END) {
        if (mLastIssue) {
            emit compileIssue(mLastIssue);
            mLastIssue.reset();
        }
        return;
    }
    if (line.startsWith(">>>"))
        line.remove(0,3);
    QString referencePrefix = QString(" referenced by ");
    if(mLastIssue && line.startsWith(referencePrefix)) {
            line.remove(0,referencePrefix.length());
            mLastIssue->filename = getFileNameFromOutputLine(line);
            //qDebug()<<line;
            mLastIssue->line = getLineNumberFromOutputLine(line);
            emit compileIssue(mLastIssue);
            mLastIssue.reset();
            return;
    }
    QString inFilePrefix = QString("In file included from ");
    QString fromPrefix = QString("from ");
    PCompileIssue issue = std::make_shared<CompileIssue>();
    issue->type = CompileIssueType::Other;
    issue->endColumn = -1;
    if (mResolvedConfiguration)
        issue->gccDiagnostic = configuration().toolchain.compilerType == CompilerType::GCC;
    else {
        const auto diagnosticCompiler = compilerSet();
        issue->gccDiagnostic = diagnosticCompiler && diagnosticCompiler->compilerType() == CompilerType::GCC;
    }
    if (line.startsWith(inFilePrefix)) {
        line.remove(0,inFilePrefix.length());
        issue->filename = getFileNameFromOutputLine(line);
        issue->line = getLineNumberFromOutputLine(line) - 1; // editor line starts from 0, gdb line starts from 1
        if (issue->line >= 0)
            issue->column = getColunmnFromOutputLine(line) - 1; // editor ch starts from 0, gdb col starts from 1
        issue->type = getIssueTypeFromOutputLine(line);
        issue->description = inFilePrefix + issue->filename;
        emit compileIssue(issue);
        return;
    } else if(line.startsWith(fromPrefix)) {
        line.remove(0,fromPrefix.length());
        issue->filename = getFileNameFromOutputLine(line);
        issue->line = getLineNumberFromOutputLine(line) - 1; // editor line starts from 0, gdb line starts from 1
        if (issue->line >= 0)
            issue->column = getColunmnFromOutputLine(line) - 1; // editor ch starts from 0, gdb col starts from 1
        issue->type = getIssueTypeFromOutputLine(line);
        issue->description = "                 from " + issue->filename;
        emit compileIssue(issue);
        return;
    }

    // Ignore code snippets that GCC produces
    // they always start with a space
    if (line.length()>0 && line[0] == ' ') {
        if (!mLastIssue)
            return;
        QString s = line.trimmed();
        if (s.startsWith('|') && s.indexOf('^')) {
            int pos = 0;
            while (pos < s.length()) {
                if (s[pos]=='^')
                    break;
                pos++;
            }
            if (pos<s.length()) {
                int i=pos+1;
                while (i<s.length()) {
                    if (s[i]!='~' && s[i]!='^')
                        break;
                    i++;
                }
                mLastIssue->endColumn = mLastIssue->column+i-pos;
                emit compileIssue(mLastIssue);
                mLastIssue.reset();
            }
        }
        return;
    }

    if (mLastIssue) {
        emit compileIssue(mLastIssue);
        mLastIssue.reset();
    }

    QString originalDescription;
    // assume regular main.cpp:line:col: message
    issue->filename = getFileNameFromOutputLine(line);
    issue->line = getLineNumberFromOutputLine(line) - 1; // editor line starts from 0, gdb line starts from 1
    if (issue->line >= 0) {
        issue->column = getColunmnFromOutputLine(line) - 1; // editor ch starts from 0, gdb col starts from 1
        originalDescription = line.trimmed();
        issue->type = getIssueTypeFromOutputLine(line);
        if (issue->column<=0 && issue->type == CompileIssueType::Other) {
            issue->type = CompileIssueType::Error; //linkage error
            mErrorCount += 1;
        }
    } else {
        issue->column = -1;
        originalDescription = line.trimmed();
        issue->type = getIssueTypeFromOutputLine(line);
    }
    issue->originalDescription = originalDescription;
    issue->description = line.trimmed();
    if (issue->line<0 && (issue->filename=="ld" || issue->filename=="lld")) {
        mLastIssue = issue;
    } else if (issue->line<0) {
        emit compileIssue(issue);
    } else
        mLastIssue = issue;
}

void Compiler::stopCompile()
{
    if (!mQaProcessTracePath.isEmpty()) {
        QElapsedTimer now;
        now.start();
        qint64 unset = -1;
        mQaStopReference.compare_exchange_strong(unset, now.msecsSinceReference(), std::memory_order_release);
    }
    mStop->store(true, std::memory_order_release);
    if (mPreparationMailbox) mPreparationMailbox->cancel();
}

QStringList Compiler::getCharsetArgument(const QByteArray& encoding,FileType fileType, bool checkSyntax)
{
    if (mResolvedConfiguration)
        return Compilation::charsetArguments(configuration(), encoding, checkSyntax,
            (fileType == FileType::CSource || fileType == FileType::CppSource) && mResolvedAutolink.forceExecUTF8);
    QStringList result;
    bool forceExecUTF8=false;
    // test if force utf8 from autolink infos
    if ((fileType == FileType::CSource ||
            fileType == FileType::CppSource) && pSettings->editor().enableAutolink()
            && mParserForFile){
        int waitCount = 0;
        //wait parsing ends, at most 1 second
        while(mParserForFile->parsing()) {
            if (waitCount>10)
                break;
            waitCount++;
            QThread::msleep(100);
            QApplication *app=dynamic_cast<QApplication*>(
                        QApplication::instance());
            app->processEvents();
        }
        if (waitCount<=10) {
            QSet<QString> parsedFiles;
            forceExecUTF8 = parseForceUTF8ForAutolink(
                        mFilename,
                        parsedFiles);
        }
    }
    if ((forceExecUTF8 || compilerSet()->autoAddCharsetParams()) && encoding != ENCODING_ASCII
            && compilerSet()->supportConvertingCharset()) {
        QString encodingName;
        QString execEncodingName;
        QString compilerSetExecCharset = compilerSet()->execCharset();
        QString systemEncodingName = pCharsetInfoManager->getDefaultSystemEncoding();
        if (encoding == ENCODING_SYSTEM_DEFAULT) {
            encodingName = systemEncodingName;
        } else if (encoding == ENCODING_UTF8_BOM) {
            encodingName = "UTF-8";
        } else if (encoding == ENCODING_UTF16_BOM) {
            encodingName = "UTF-16";
        } else if (encoding == ENCODING_UTF32_BOM) {
            encodingName = "UTF-32";
        } else {
            encodingName = encoding;
        }
        if (forceExecUTF8) {
            execEncodingName = "UTF-8";
        } else if (compilerSetExecCharset == ENCODING_SYSTEM_DEFAULT || compilerSetExecCharset.isEmpty()) {
            execEncodingName = systemEncodingName;
        } else if (compilerSetExecCharset == ENCODING_OEM_DEFAULT) {
            execEncodingName = pCharsetInfoManager->getDefaultConsoleEncoding();
        } else {
            execEncodingName = compilerSetExecCharset;
        }
        //qDebug()<<encodingName<<execEncodingName;
        if (checkSyntax) {
            result << "-finput-charset=" + encodingName;
        } else if (QString::compare(encodingName, execEncodingName, Qt::CaseInsensitive) != 0) {
            result += {
                "-finput-charset=" + encodingName,
                "-fexec-charset=" + execEncodingName,
            };
        }
    }
    return result;
}

QStringList Compiler::getCppGccImportStdSources(bool checkSyntax)
{
    if (mResolvedConfiguration) {
        const auto &options = configuration().toolchain.compileOptions;
        const QString standard = options.value(CC_CMD_OPT_STD);
        if (!checkSyntax && options.value(CC_CMD_OPT_ENABLE_GCC_IMPORT_STD) == COMPILER_OPTION_ON
            && (standard.startsWith("c++2") || standard.startsWith("gnu++2")))
            return {"-fsearch-include-path", "bits/std.cc", "bits/std.compat.cc"};
        return {};
    }
    QStringList result;
    if (!checkSyntax && compilerSet()->getCompileOptionValue(CC_CMD_OPT_ENABLE_GCC_IMPORT_STD) == COMPILER_OPTION_ON) {
        // libstdc++ extends `import std` to C++20
        // FIXME: use robust method to check C++ standard version
        const QString &std = compilerSet()->getCompileOptionValue(CC_CMD_OPT_STD);
        if (std.startsWith("c++2") || std.startsWith("gnu++2")) {
            result += {"-fsearch-include-path", "bits/std.cc", "bits/std.compat.cc"};
        }
    }
    return result;
}

QStringList Compiler::getCCompileArguments(bool checkSyntax)
{
    if (mResolvedConfiguration)
        return Compilation::optionArguments(configuration(), Compilation::ArgumentUse::C, checkSyntax, mSuppressDebugInfo);
    QStringList result;
    if (checkSyntax) {
        result << "-fsyntax-only";
    }

    QMap<QString, QString> compileOptions;
    if (mProject && !mProject->options().compilerOptions.isEmpty()) {
        compileOptions = mProject->options().compilerOptions;
    } else {
        compileOptions = compilerSet()->compileOptions();
    }
    foreach (const QString& key, compileOptions.keys()) {
        if (compileOptions[key]=="")
            continue;
        PCompilerOption pOption = CompilerInfoManager::getCompilerOption(compilerSet()->compilerType(), key);
        if (pOption && pOption->isC && !pOption->isLinker) {
            if (pOption->type == CompilerOptionType::Checkbox)
                result << pOption->setting;
            else if (pOption->type == CompilerOptionType::Input)
                result += {pOption->setting, compileOptions[key]};
            else if (pOption->type == CompilerOptionType::Number) {
                bool ok;
                int val = compileOptions[key].toInt(&ok);
                if (ok && val>0) {
                    val = pOption->scale * val;
                    result += QString("%1%2").arg(pOption->setting).arg(val);
                }
            } else {
                result << pOption->setting + compileOptions[key];
            }
        }
    }

    QMap<QString, QString> macros = mMacroVariables;

    if (compilerSet()->useCustomCompileParams() && !compilerSet()->customCompileParams().isEmpty()) {
        result << parseArguments(compilerSet()->customCompileParams(), macros, true);
    }

    if (mProject) {
        QString s = mProject->options().compilerCmd;
        if (!s.isEmpty()) {
            s.replace("_@@_", " ");
            result << parseArguments(s, macros, true);
        }
    }

    return result;
}

QStringList Compiler::getCppCompileArguments(bool checkSyntax)
{
    if (mResolvedConfiguration)
        return Compilation::optionArguments(configuration(), Compilation::ArgumentUse::Cpp, checkSyntax, mSuppressDebugInfo);
    QStringList result;
    if (checkSyntax) {
        result << "-fsyntax-only";
    }
    QMap<QString, QString> compileOptions;
    if (mProject && !mProject->options().compilerOptions.isEmpty()) {
        compileOptions = mProject->options().compilerOptions;
    } else {
        compileOptions = compilerSet()->compileOptions();
    }
    foreach (const QString& key, compileOptions.keys()) {
        if (compileOptions[key]=="")
            continue;
        PCompilerOption pOption = CompilerInfoManager::getCompilerOption(compilerSet()->compilerType(), key);
        if (pOption && pOption->isCpp && !pOption->isLinker) {
            if (pOption->type == CompilerOptionType::Checkbox)
                result << pOption->setting;
            else if (pOption->type == CompilerOptionType::Input)
                result += {pOption->setting, compileOptions[key]};
            else if (pOption->type == CompilerOptionType::Number) {
                bool ok;
                int val = compileOptions[key].toInt(&ok);
                if (ok && val>0) {
                    val = pOption->scale * val;
                    result += QString("%1%2").arg(pOption->setting).arg(val);
                }
            } else {
                result << pOption->setting + compileOptions[key];
            }
        }
    }

    QMap<QString, QString> macros = mMacroVariables;
    if (compilerSet()->useCustomCompileParams() && !compilerSet()->customCompileParams().isEmpty()) {
        result << parseArguments(compilerSet()->customCompileParams(), macros, true);
    }
    if (mProject) {
        QString s = mProject->options().cppCompilerCmd;
        if (!s.isEmpty()) {
            s.replace("_@@_", " ");
            result << parseArguments(s, macros, true);
        }
    }

    return result;
}


QStringList Compiler::getCIncludeArguments()
{
    if (mResolvedConfiguration) return Compilation::includeArguments(configuration().toolchain.cIncludeDirs);
    QStringList result;
    foreach (const QString& folder,compilerSet()->CIncludeDirs()) {
        result << "-I" + folder;
    }
    return result;
}

QStringList Compiler::getProjectIncludeArguments()
{
    if (mResolvedConfiguration)
        return configuration().project ? Compilation::includeArguments(configuration().project->includeDirs) : QStringList{};
    QStringList result;
    if (mProject) {
        foreach (const QString& folder,mProject->options().includeDirs) {
            result << "-I" + folder;
        }
//        result +=  QString(" -I\"%1\"").arg(extractFilePath(mProject->filename()));
    }
    return result;
}

QStringList Compiler::getCppIncludeArguments()
{
    if (mResolvedConfiguration) return Compilation::includeArguments(configuration().toolchain.cppIncludeDirs);
    QStringList result;
    foreach (const QString& folder,compilerSet()->CppIncludeDirs()) {
        result << "-I" + folder;
    }
    return result;
}

QStringList Compiler::getLibraryArguments(FileType fileType)
{
    if (mResolvedConfiguration) {
        const auto plan = Compilation::libraryArguments(configuration(), mResolvedAutolink,
            fileType == FileType::CSource || fileType == FileType::CppSource, mSuppressDebugInfo);
        if (!plan.preparation.ready()) throw CompileError(plan.preparation.reason);
        return plan.arguments;
    }
    QStringList result;

    //Add libraries
    foreach (const QString& folder, compilerSet()->libDirs()) {
        result << "-L" + folder;
    }

    //add libs added via project
    if (mProject) {
        foreach (const QString& folder, mProject->options().libDirs){
            result << "-L" + folder;
        }
    }

    //Add auto links
    // is file and auto link enabled
    if (pSettings->editor().enableAutolink() && (fileType == FileType::CSource ||
            fileType == FileType::CppSource)
            && mParserForFile){
        int waitCount = 0;
        //wait parsing ends, at most 1 second
        while(mParserForFile->parsing()) {
            if (waitCount>10)
                break;
            waitCount++;
            QThread::msleep(100);
            QApplication *app=dynamic_cast<QApplication*>(
                        QApplication::instance());
            app->processEvents();
        }
        if (waitCount<=10) {
            QSet<QString> parsedFiles;
            result += parseFileIncludesForAutolink(mFilename, parsedFiles);
        }
    }

    //add compiler set link options
    //options like "-static" must be added after "-lxxx"
    QMap<QString, QString> compileOptions;
    if (mProject && !mProject->options().compilerOptions.isEmpty()) {
        compileOptions = mProject->options().compilerOptions;
    } else {
        compileOptions = compilerSet()->compileOptions();
    }
    foreach (const QString& key, compileOptions.keys()) {
        if (compileOptions[key]=="")
            continue;
        PCompilerOption pOption = CompilerInfoManager::getCompilerOption(compilerSet()->compilerType(), key);
        if (pOption && pOption->isLinker) {
            if (pOption->type == CompilerOptionType::Checkbox)
                result << pOption->setting;
            else if (pOption->type == CompilerOptionType::Input)
                result += {pOption->setting, compileOptions[key]};
            else if (pOption->type == CompilerOptionType::Number) {
                bool ok;
                int val = compileOptions[key].toInt(&ok);
                if (ok && val>0) {
                    val = pOption->scale * val;
                    result += QString("%1%2").arg(pOption->setting).arg(val);
                }
            } else {
                result << pOption->setting + compileOptions[key];
            }
        }
    }

    // Add global compiler linker extras
    if (compilerSet()->useCustomLinkParams() && !compilerSet()->customLinkParams().isEmpty()) {
        QMap<QString, QString> macros = mMacroVariables;
        QStringList params = parseArguments(compilerSet()->customLinkParams(), macros, true);
        if (!params.isEmpty()) {
            foreach(const QString& param, params)
                result << param;
        }
    }

    if (mProject) {
        if (mProject->options().type == ProjectType::GUI) {
            result << "-mwindows";
        }

        if (!mProject->options().linkerCmd.isEmpty()) {
            QString s = mProject->options().linkerCmd;
            if (!s.isEmpty()) {
                s.replace("_@@_", " "); // historical reason
                result += parseArguments(s, {}, true);
            }
        }
        if (mProject->options().staticLink)
            result << "-static";
    } else {
        if (compilerSet()->staticLink()) {
            result << "-static";
        }
    }
    return result;
}

QStringList Compiler::parseFileIncludesForAutolink(
        const QString &filename,
        QSet<QString>& parsedFiles)
{
    QStringList result;
    if (parsedFiles.contains(filename))
        return result;
    parsedFiles.insert(filename);
    PAutolink autolink = pAutolinkManager->getLink(filename);
    if (autolink) {
        result += parseArgumentsWithoutVariables(autolink->linkOption);
    }
    QStringList includedFiles = mParserForFile->getFileDirectIncludes(filename);
//    log(QString("File %1 included:").arg(filename));
//    for (int i=includedFiles.size()-1;i>=0;i--) {
//        QString includeFilename = includedFiles[i];
//        log(includeFilename);
//    }

    for (int i=includedFiles.size()-1;i>=0;i--) {
        QString includeFilename = includedFiles[i];
        result += parseFileIncludesForAutolink(includeFilename, parsedFiles);
    }
    return result;
}

bool Compiler::parseForceUTF8ForAutolink(const QString &filename, QSet<QString> &parsedFiles)
{
    bool result;
    if (parsedFiles.contains(filename))
        return false;
    parsedFiles.insert(filename);
    PAutolink autolink = pAutolinkManager->getLink(filename);
    if (autolink && autolink->execUseUTF8) {
        return true;
    }
    QStringList includedFiles = mParserForFile->getFileDirectIncludes(filename);
//    log(QString("File %1 included:").arg(filename));
//    for (int i=includedFiles.size()-1;i>=0;i--) {
//        QString includeFilename = includedFiles[i];
//        log(includeFilename);
//    }

    for (int i=includedFiles.size()-1;i>=0;i--) {
        QString includeFilename = includedFiles[i];
        result = parseForceUTF8ForAutolink(
                    includeFilename,
                    parsedFiles);
        if (result)
            return true;
    }
    return false;
}

void Compiler::runCommand(const QString &cmd, const QStringList &arguments, const QString &workingDir, const QByteArray& inputText, const QString& outputFile)
{
    writeQaProcessTrace("command-enter");
    // Declared before QProcess: this records return after its destructor, which
    // can distinguish native startup/cleanup delays from polling cancellation.
    auto commandExit = finally([this] { writeQaProcessTrace("command-exit"); });
    if (mStop->load(std::memory_order_acquire))
        return;
    QProcess process;
    auto processExit = finally([this, &process] { writeQaProcessTrace("before-process-destruction", &process); });
    bool errorOccurred = false;
    process.setProgram(cmd);
    QString cmdDir = extractFileDir(cmd);
    const bool compilerErrorUTF8 = mResolvedConfiguration ? configuration().toolchain.compilerUsingUTF8
                                                          : compilerSet()->isCompilerUsingUTF8();
    const bool outputUTF8 = compilerErrorUTF8;
    QProcessEnvironment env;
    if (mResolvedConfiguration) {
        env = Compilation::processEnvironment(configuration(), cmdDir, QDir::listSeparator());
    } else {
    env = QProcessEnvironment::systemEnvironment();
    QString path = env.value("PATH");
#ifdef Q_OS_WIN
    QStringList binDirs=compilerSet()->binDirs();
    if (!binDirs.isEmpty()) {
        if (path.isEmpty()) {
            path = binDirs.join(PATH_SEPARATOR);
        } else {
            path = binDirs.join(PATH_SEPARATOR) + PATH_SEPARATOR + path;
        }
    }
#endif
    if (!cmdDir.isEmpty()) {
        if (path.isEmpty()) {
            path = cmdDir;
        } else {
            path = cmdDir + PATH_SEPARATOR + path;
        }
        env.insert("PATH",path);
    }

    // ProjectCompiler selects the shell for generated makefiles explicitly;
    // custom makefiles retain their own shell selection.
    if (compilerSet() && compilerSet()->supportNLS() && compilerSet()->forceEnglishOutput()) {
        env.insert("LANGUAGE", "");
        env.insert("LC_ALL", "C"); // https://pubs.opengroup.org/onlinepubs/009695399/basedefs/xbd_chap07.html#tag_07_02
    }
    }
    //env.insert("LDFLAGS","-Wl,--stack,12582912");
    env.insert("LDFLAGS","");
    env.insert("CFLAGS","");
    env.insert("CXXFLAGS","");
    process.setProcessEnvironment(env);
    process.setArguments(arguments);
    process.setWorkingDirectory(workingDir);
    QSaveFile output;
    output.setDirectWriteFallback(false);
    bool outputWriteFailed = false;
    bool outputLimitReached = false;
    qint64 convertedOutputBytes = 0;
    const auto acceptDiagnostic = [this, &process, &outputLimitReached](qsizetype size) {
        if (mFileRequest && size > mFileRequest->values().maximumDiagnosticBytes - mCapturedDiagnosticBytes) {
            outputLimitReached = true; process.kill(); return false;
        }
        mCapturedDiagnosticBytes += size;
        return true;
    };
    if (!outputFile.isEmpty()) {
        validateGeneratedOutputPath(outputFile);
        output.setFileName(outputFile);
        if (!output.open(QIODevice::WriteOnly))
            throw CompileError(tr("Cannot stage output '%1': %2").arg(outputFile, output.errorString()));
    }
    process.connect(&process, &QProcess::errorOccurred,
                    [&](){
                        errorOccurred= true;
                    });
    const QByteArray codec = mResolvedConfiguration ? (compilerErrorUTF8 ? QByteArray(ENCODING_UTF8)
                                            : configuration().preferences.systemEncoding) : QByteArray(ENCODING_UTF8);
    const auto errorDecoder = std::make_shared<QStringDecoder>(codec.constData());
    const auto outputDecoder = std::make_shared<QStringDecoder>(codec.constData());
    if (mResolvedConfiguration && (!errorDecoder->isValid() || !outputDecoder->isValid()))
        throw CompileError(tr("The captured compiler output encoding is not available."));
    process.connect(&process, &QProcess::readyReadStandardError,[&process,this,compilerErrorUTF8,errorDecoder,acceptDiagnostic](){
        const auto bytes = process.readAllStandardError();
        if (!acceptDiagnostic(bytes.size())) return;
        if (mResolvedConfiguration)
            this->error((*errorDecoder)(bytes));
        else if (compilerErrorUTF8)
            this->error(QString::fromUtf8(bytes));
        else
            this->error(QString::fromLocal8Bit(bytes));
    });
    process.connect(&process, &QProcess::readyReadStandardOutput,[&process,this,outputUTF8,&outputFile,&output,&outputWriteFailed,
        &outputLimitReached,&convertedOutputBytes,outputDecoder,acceptDiagnostic](){
        const auto bytes = process.readAllStandardOutput();
        if (!outputFile.isEmpty()) {
            if (mFileRequest && bytes.size() > mFileRequest->values().maximumConvertedOutputBytes - convertedOutputBytes) {
                outputLimitReached = true; process.kill(); return;
            }
            convertedOutputBytes += bytes.size();
            if (!outputWriteFailed && output.write(bytes) != bytes.size()) {
                outputWriteFailed = true;
                process.kill();
            }
        } else {
            if (!acceptDiagnostic(bytes.size())) return;
            if (mResolvedConfiguration)
                this->log((*outputDecoder)(bytes));
            else if (outputUTF8)
                this->log(QString::fromUtf8(bytes));
            else
                this->log(QString::fromLocal8Bit(bytes));
        }
    });
    process.connect(&process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),[this](){
        this->error(COMPILE_PROCESS_END);
    });
    const auto disconnectBeforeOutputDestruction = finally([&process] { process.disconnect(); });
#ifdef Q_OS_WIN
    Compilation::ContainedProcess processTree;
    const auto containment = processTree.prepare(process);
    if (!containment.success) throw CompileError(containment.error);
    bool treeCompleted = false;
    QString treeFailure;
    const auto finishTree = [&](bool force) {
        if (treeCompleted) return;
        const auto completed = Compilation::completeContainedProcess(process, processTree, *mStop, force, {},
            [this](const QString &reason) { log(reason); });
        treeCompleted = true;
        treeFailure = completed.error;
        if (mFileRequest) mProcessOwnershipLevel = Compilation::ProcessOwnershipLevel::WindowsTreeQuiescent;
    };
    // This guard runs before callbacks/output are destroyed, including when an
    // exception interrupts startup, input transfer or output conversion.
    const auto acknowledgeTreeBeforeExit = finally([&] { finishTree(true); });
#endif
    writeQaProcessTrace("before-start", &process);
#ifndef Q_OS_WIN
    mProcessOwnershipLevel = Compilation::ProcessOwnershipLevel::DriverOnlyUnverifiedTree;
#endif
    process.start();
    writeQaProcessTrace("after-start", &process);
    const bool started = process.waitForStarted(5000);
    writeQaProcessTrace("after-wait-for-started", &process, started ? 1 : 0);
    if (!started) {
        ProcessCancellation::stop(process);
        if (mStop->load(std::memory_order_acquire)) return;
#ifdef Q_OS_WIN
        const auto creation = processTree.creationResult();
        if (!creation.success) throw CompileError(creation.error);
#endif
        throw CompileError(tr("The compiler process for '%1' could not start: %2").arg(mFilename, process.errorString()));
    }
#ifdef Q_OS_WIN
    const auto creation = processTree.creationResult();
    if (!creation.success) throw CompileError(creation.error);
#endif
    if (!inputText.isEmpty()) {
        if (process.write(inputText) != inputText.size())
            throw CompileError(tr("Could not send the captured source text to the compiler."));
    }
    bool writeChannelClosed = false;
    while (true) {
        if (process.bytesToWrite()==0 && !writeChannelClosed ) {
            writeChannelClosed=true;
            process.closeWriteChannel();
        }
        process.waitForFinished(ProcessCancellation::pollMilliseconds);
        if (process.state()!=QProcess::Running) {
            break;
        }
        if (mStop->load(std::memory_order_acquire)) {
            writeQaProcessTrace("before-cancellation", &process);
#ifdef Q_OS_WIN
            processTree.terminate();
#endif
            if (!ProcessCancellation::stop(process))
                emit compileErrorOccured(tr("The compiler process could not be stopped within the timeout."));
            writeQaProcessTrace("after-cancellation", &process);
            break;
        }
        if (errorOccurred)
            break;
    }
#ifdef Q_OS_WIN
    finishTree(errorOccurred || outputWriteFailed || outputLimitReached || mStop->load(std::memory_order_acquire)
        || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0);
#endif
    if (errorOccurred) {
        if (outputLimitReached)
            throw CompileError(tr("Compiler output exceeded the captured output budget."));
        if (outputWriteFailed)
            throw CompileError(tr("Cannot write output '%1': %2").arg(outputFile, output.errorString()));
        switch (process.error()) {
        case QProcess::FailedToStart:
            throw CompileError(tr("The compiler process for '%1' failed to start.").arg(mFilename));
            break;
        case QProcess::Crashed:
            if (!mStop->load(std::memory_order_acquire))
                throw CompileError(tr("The compiler process crashed after starting successfully."));
            break;
        case QProcess::Timedout:
            throw CompileError(tr("The last waitFor...() function timed out."));
            break;
        case QProcess::WriteError:
            throw CompileError(tr("An error occurred when attempting to write to the compiler process."));
            break;
        case QProcess::ReadError:
            throw CompileError(tr("An error occurred when attempting to read from the compiler process."));
            break;
        default:
            throw CompileError(tr("An unknown error occurred."));
        }
    }
    if (mStop->load(std::memory_order_acquire)) {
        mTaskOutcome = OperationResult::cancelled();
        return;
    }
    if (outputWriteFailed)
        throw CompileError(tr("Cannot write output '%1': %2").arg(outputFile, output.errorString()));
    if (outputLimitReached)
        throw CompileError(tr("Compiler output exceeded the captured output budget."));
#ifdef Q_OS_WIN
    if (!treeFailure.isEmpty()) throw CompileError(treeFailure);
#endif
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        mTaskOutcome = OperationResult::failure(tr("Compiler process exited with code %1.").arg(process.exitCode()));
        log(mTaskOutcome.error);
        return;
    }
    if (!outputFile.isEmpty() && !output.commit())
        throw CompileError(tr("Cannot commit output '%1': %2").arg(outputFile, output.errorString()));
}

QString Compiler::escapeCommandForLog(const QString &cmd, const QStringList &arguments)
{
    return escapeCommandForPlatformShell(extractFileName(cmd), arguments);
}

const PCppParser &Compiler::parserForFile() const
{
    return mParserForFile;
}

void Compiler::setParserForFile(const PCppParser &newParserForFile)
{
    mParserForFile = newParserForFile;
}

const std::shared_ptr<Project> &Compiler::project() const
{
    return mProject;
}

void Compiler::setProject(const std::shared_ptr<Project> &newProject)
{
    if (mFileRequest)
        throw CompileError(tr("A captured file compilation must not retain a live project."));
    if (QThread::currentThread() != thread() || isRunning())
        throw CompileError(tr("Project compilation inputs must be captured on their owner thread."));
    mProject = newProject;
    mProjectSourceEncodings.clear();
    mProtectedInputPaths.clear();
    mProjectSystemEncoding = pCharsetInfoManager->getDefaultSystemEncoding();
    mProjectConsoleEncoding = pCharsetInfoManager->getDefaultConsoleEncoding();
    if (mProject) {
        mProtectedInputPaths.append(mProject->filename());
        for (const auto &unit : mProject->unitList()) {
            mProtectedInputPaths.append(unit->fileName());
            const Editor *editor = mProject->unitEditor(unit);
            QByteArray source = editor ? editor->fileEncoding() : unit->realEncoding();
            if (source.isEmpty()) {
                source = unit->encoding();
                if (source == ENCODING_PROJECT) source = mProject->options().encoding;
                if (source == ENCODING_AUTO_DETECT) source.clear();
            }
            if (source == ENCODING_SYSTEM_DEFAULT) source = mProjectSystemEncoding;
            else if (source == ENCODING_OEM_DEFAULT) source = mProjectConsoleEncoding;
            else if (source == ENCODING_UTF8_BOM) source = ENCODING_UTF8;
            else if (source == ENCODING_UTF16_BOM) source = ENCODING_UTF16;
            else if (source == ENCODING_UTF32_BOM) source = ENCODING_UTF32;
            mProjectSourceEncodings.insert(reinterpret_cast<quintptr>(unit.get()), source);
        }
    }
}

bool Compiler::isRebuild() const
{
    return mRebuild;
}

void Compiler::setRebuild(bool isRebuild)
{
    mRebuild = isRebuild;
}

void Compiler::log(const QString &msg)
{
    emit compileOutput(msg);
}

void Compiler::error(const QString &msg)
{
    if (msg != COMPILE_PROCESS_END)
        emit compileOutput(msg);
    if (mResolvedConfiguration) {
        // QProcess chunks do not end on a diagnostic line boundary. Preserve
        // the exact decoded message until a newline or explicit EOF arrives.
        if (msg == COMPILE_PROCESS_END) {
            if (!mDiagnosticLineBuffer.isEmpty()) {
                QString final = std::move(mDiagnosticLineBuffer);
                mDiagnosticLineBuffer.clear();
                processOutput(final);
            }
            QString marker = msg;
            processOutput(marker);
        } else {
            mDiagnosticLineBuffer.append(msg);
            qsizetype begin = 0;
            while (true) {
                const qsizetype newline = mDiagnosticLineBuffer.indexOf('\n', begin);
                if (newline < 0) break;
                QString line = mDiagnosticLineBuffer.mid(begin, newline - begin);
                if (!line.isEmpty()) processOutput(line);
                begin = newline + 1;
            }
            if (begin) mDiagnosticLineBuffer.remove(0, begin);
        }
        return;
    }
    for (QString& s:msg.split("\n")) {
        if (!s.isEmpty())
            processOutput(s);
    }
}
