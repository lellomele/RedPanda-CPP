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
#ifndef COMPILER_H
#define COMPILER_H

#include <QThread>
#include <atomic>
#include "filecompilerequest.h"
#include "../operationresult.h"
#include "../settings.h"
#include "../common.h"
#include "../parser/cppparser.h"
#include "../utils/file.h"

class Project;
class QProcess;
class Compiler : public QThread
{
    Q_OBJECT
public:
    enum class TargetType {
        Invalid,
        cttNone,
        File,
        Project,
        StdIn
    };
    Compiler(const QString& filename, bool onlyCheckSyntax);
    explicit Compiler(Compilation::FileCompileRequest::Pointer request);
    Compiler(const Compiler&)=delete;
    Compiler& operator=(const Compiler&)=delete;

    bool isRebuild() const;
    void setRebuild(bool isRebuild);

    const PCppParser &parserForFile() const;
    void setParserForFile(const PCppParser &newParserForFile);
    void setPreparationMailbox(std::shared_ptr<Compilation::PreparationMailbox> mailbox);
    const Compilation::FileCompileRequest::Pointer &fileRequest() const { return mFileRequest; }
    OperationResult taskOutcome() const { return mTaskOutcome; }
    Compilation::ProcessOwnershipLevel processOwnershipLevel() const { return mProcessOwnershipLevel; }

signals:
    void compileStarted();
    void compileFinished(const QString& filename);
    void compileOutput(const QString& msg);
    void compileIssue(PCompileIssue issue);
    void compileErrorOccured(const QString& reason);
    void toolchainCapabilitiesAvailable(QString identity, bool supportNLS, bool supportCharset, bool compilerUTF8);
public slots:
    void stopCompile();

protected:
    Compiler(Compilation::FrozenConfiguration::Pointer projectConfiguration, bool capabilitiesKnown);
    void run() override;
    void processOutput(QString& line);
    virtual QString getFileNameFromOutputLine(QString &line);
    virtual int getLineNumberFromOutputLine(QString &line);
    virtual int getColunmnFromOutputLine(QString &line);
    virtual CompileIssueType getIssueTypeFromOutputLine(QString &line);

protected:
    virtual PCompilerSet compilerSet();
    virtual bool prepareForCompile() = 0;
    virtual QByteArray pipedText();
    virtual bool prepareForRebuild() = 0;
    virtual bool beforeRunExtraCommand(int idx);
    virtual QStringList getCharsetArgument(const QByteArray& encoding, FileType fileType, bool onlyCheckSyntax);
    virtual QStringList getCppGccImportStdSources(bool checkSyntax);
    virtual QStringList getCCompileArguments(bool checkSyntax);
    virtual QStringList getCppCompileArguments(bool checkSyntax);
    virtual QStringList getCIncludeArguments();
    virtual QStringList getProjectIncludeArguments();
    virtual QStringList getCppIncludeArguments();
    virtual QStringList getLibraryArguments(FileType fileType);
    virtual QStringList parseFileIncludesForAutolink(
            const QString& filename,
            QSet<QString>& parsedFiles);
    virtual bool parseForceUTF8ForAutolink(
            const QString& filename,
            QSet<QString>& parsedFiles);
    void log(const QString& msg);
    void error(const QString& msg);
    void runCommand(const QString& cmd, const QStringList& arguments, const QString& workingDir, const QByteArray& inputText=QByteArray(), const QString& outputFile=QString());
    QString escapeCommandForLog(const QString &cmd, const QStringList &arguments);
    const QMap<QString, QString> &macroVariables() const { return mMacroVariables; }
    const Compilation::ConfigurationValues &configuration() const;
    void validateGeneratedOutputPath(const QString &path) const;
    void checkCancellation() const;

protected:
    bool mOnlyCheckSyntax;
    bool mOnlyGenerateMakefile = false;
    QString mCompiler;
    QStringList mArguments;
    QList<QString> mExtraCompilersList;
    QList<QStringList> mExtraArgumentsList;
    QList<QString> mExtraOutputFilesList;
    QString mOutputFile;
    int mErrorCount = 0;
    int mWarningCount = 0;
    PCompileIssue mLastIssue;
    QString mFilename;
    QString mDirectory;
    bool mRebuild;
    bool mSetLANG;
    PCppParser mParserForFile;
    bool mForceEnglishOutput;
    bool mSuppressDebugInfo = false;
    QStringList mProtectedInputPaths;

private:
    Compiler(const QString &filename, bool onlyCheckSyntax, bool captureLegacyMacros);
    void writeQaProcessTrace(const char *stage, const QProcess *process = nullptr, int started = -1) const;
    const std::shared_ptr<std::atomic_bool> mStop = std::make_shared<std::atomic_bool>(false);
    Compilation::FileCompileRequest::Pointer mFileRequest;
    Compilation::FrozenConfiguration::Pointer mProjectConfiguration;
    bool mProjectCapabilitiesKnown = false;
    Compilation::FrozenConfiguration::Pointer prepareCapabilities(Compilation::FrozenConfiguration::Pointer configuration);
    std::shared_ptr<Compilation::PreparationMailbox> mPreparationMailbox;
    Compilation::FrozenConfiguration::Pointer mResolvedConfiguration;
    Compilation::AutolinkResolution mResolvedAutolink;
    OperationResult mTaskOutcome = OperationResult::cancelled();
    Compilation::ProcessOwnershipLevel mProcessOwnershipLevel = Compilation::ProcessOwnershipLevel::NoProcessStarted;
    qint64 mCapturedDiagnosticBytes = 0;
    QString mDiagnosticLineBuffer;
    QMap<QString, QString> mMacroVariables;
    QString mQaProcessTracePath;
    qint64 mQaTraceReference = 0;
    std::atomic<qint64> mQaStopReference{-1};
};


#endif // COMPILER_H
