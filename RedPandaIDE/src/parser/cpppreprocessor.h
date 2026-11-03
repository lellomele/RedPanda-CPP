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
#ifndef CPPPREPROCESSOR_H
#define CPPPREPROCESSOR_H

#include <QObject>
#include <QDeadlineTimer>
#include <QTextStream>
#include "parserutils.h"
#include "../documentstore.h"
#include "../analysis/sourceinput.h"

// Observes a source read for diagnostics/tests; never supplies text and must
// not call GUI objects. The parser copies it into each accepted job.
using SourceReadObserver = std::function<void(const QString &)>;

#define MAX_DEFINE_EXPAND_DEPTH 20
enum class DefineArgTokenType{
    Symbol,
    Identifier,
    Space,
    Sharp,
    DSharp,
    Other
};
struct DefineArgToken {
    QString value;
    DefineArgTokenType type;
};
using PDefineArgToken = std::shared_ptr<DefineArgToken>;

struct ParsedFile {
    int index; // 0-based for programming convenience
    QString fileName; // Record filename, but not used now
    QString sourcePath; // Qualified source root used by the immutable read session.
    QStringList buffer; // do not concat them all
    int branches; //branch levels;
    qsizetype entryBranches = 0;
    PParsedFileInfo fileInfo; // info of this file
};

using PParsedFile = std::shared_ptr<ParsedFile>;

class CppPreprocessor
{
    enum class ContentType {
        AnsiCComment,
        CppComment,
        String,
        Character,
        EscapeSequence,
        RawStringPrefix,
        RawString,
        Other
    };
    typedef qlonglong NumberType;
public:
    enum class DependencyStatus { Complete, Cancelled, TimedOut, LimitExceeded, InputUnavailable, UnresolvedInclude };
    struct DependencyLimits {
        qint64 workUnits = 128LL * 1024 * 1024;
        qint64 macroUnits = 16LL * 1024 * 1024;
        qsizetype directives = 256 * 1024;
        qsizetype edges = 64 * 1024;
        int includeDepth = 128;
        int milliseconds = 10000;
    };
    struct DependencyRequest {
        QString root;
        QStringList includePaths;
        QStringList projectIncludePaths;
        QStringList hardDefines;
        AnalysisInput::ReadPolicy readPolicy;
        std::shared_ptr<const std::atomic_bool> cancelled;
        DependencyLimits limits;
    };
    struct DependencyResult {
        DependencyStatus status = DependencyStatus::InputUnavailable;
        QString error;
        QMap<QString, QStringList> directIncludes;
        QHash<QString, AnalysisInput::SourceVersion> reads;
        QHash<QString, AnalysisInput::PathPresence> probes;
        qint64 workUnits = 0;
        bool complete() const { return status == DependencyStatus::Complete; }
    };
    // A fresh private instance using disk input only. The result follows the
    // IDE's include semantics; it is not a prediction of every compiler
    // preprocessor extension or a filesystem snapshot.
    static DependencyResult scanDependencies(const DependencyRequest &request);

    explicit CppPreprocessor();
    CppPreprocessor(const CppPreprocessor&)=delete;
    CppPreprocessor& operator=(const CppPreprocessor&)=delete;

    void clear();
    // Call only while the owning parser has exclusive access.
    void clearParsedResultsPreservingConfig();

    void clearTempResults();
    void getDefineParts(const QString& input, QString &name, QString &args, QString &value);
    void addHardDefineByLine(const QString& line);
    void setScanOptions(bool parseSystem, bool parseLocal) {
        mParseSystem = parseSystem;
        mParseLocal=parseLocal;
    }
    void preprocess(const QString& fileName);
    void stopForParserReset() {mStopForParserReset = true;}

    void dumpDefinesTo(const QString& fileName) const;
    void dumpIncludesListTo(const QString& fileName) const;
    void addIncludePath(const QString& fileName);
    void addProjectIncludePath(const QString& fileName);
    void clearIncludePaths() {
        mIncludePaths.clear();
        mIncludePathList.clear();
    }
    void clearProjectIncludePaths() {
        mProjectIncludePaths.clear();
        mProjectIncludePathList.clear();
    }
    void removeScannedFile(const QString& filename);

    PDefine getDefine(const QString& name) const{
        return mDefines.value(name,PDefine());
    }

    QString expandMacros(QString text) const;

    const QStringList& result() const{
        return mResult;
    };

    PParsedFileInfo findFileInfo(const QString& fileName) const {
        return mFileInfos.value(fileName);
    }

    void removeFileInfo(const QString& fileName) {
        mFileInfos.remove(fileName);
    }

    bool fileScanned(const QString& fileName) const {
        return mScannedFiles.contains(fileName);
    }

    const QSet<QString>& includePaths() const {
        return mIncludePaths;
    }

    const QSet<QString>& scannedFiles() const {
        return mScannedFiles;
    }

    const QSet<QString> &projectIncludePaths() const {
        return mProjectIncludePaths;
    }

    const DefineMap &hardDefines() const { return mHardDefines; }

    const QList<QString> &includePathList() const { return mIncludePathList; }

    const QList<QString> &projectIncludePathList() const { return mProjectIncludePathList; }
    void setOnGetFileStream(const GetFileStreamFunc &newOnGetFileStream) { mOnGetFileStream = newOnGetFileStream; }
    void setInputSnapshot(PAnalysisInputSnapshot snapshot) { mInputSnapshot = std::move(snapshot); }
    void setSourceReadSession(std::shared_ptr<AnalysisInput::SourceReadSession> session) { mSourceReadSession = std::move(session); }
    void setSourceReadObserver(SourceReadObserver observer) { mSourceReadObserver = std::move(observer); }

    static QList<PDefineArgToken> tokenizeValue(const QString& value);
    static void combineLinesEndingWithBackslash(QStringList& text);
    static void replaceCommentsBySpaceChar(QStringList& text);
    bool fileOnlyIncludeOnce() const;
    void setFileOnlyIncludeOnce(bool newFileOnlyIncludeOnce);
    bool evaluateIf(const QString& line) const;
    QString expandMacrosInConditioningExpression(QString line) const;

private:
    struct DependencyControl {
        const DependencyRequest &request;
        QDeadlineTimer deadline;
        qint64 work = 0;
        qint64 macroUnits = 0;
        qsizetype directives = 0;
        qsizetype edges = 0;
        int expressionDepth = 0;
        int expansionDepth = 0;
    };
    void dependencyWork(qint64 units) const;
    void dependencyExpansion(qint64 size, qint64 copyCost = 0) const;
    using WorkCheckpoint = std::function<void(qint64)>;
    static QList<PDefineArgToken> tokenizeValue(const QString& value, const WorkCheckpoint &checkpoint);
    static void combineLinesEndingWithBackslash(QStringList& text, const WorkCheckpoint &checkpoint);
    static void replaceCommentsBySpaceChar(QStringList& text, const WorkCheckpoint &checkpoint, bool directivesOnly);
    DependencyControl *mDependency = nullptr;

    enum class BranchResult {
        isTrue,   /* This branch is true */
        isFalse_but_trued, /* This branch is false, but a previous branch is true */
        isFalse, /* This branch and all previous branches is false */
        parentIsFalse
    };

    bool supportCPP23() const;

    QString expandFunctionLikeMacro(PDefine define,const QString &args, const QSet<QString> &macrosToBeIgnored) const;
    void preprocessBuffer();
    void skipToPreprocessor();
    QString getNextPreprocessor();

    QString expandMacros(QString text, bool handleBuffer);
    QString expandMacros(QString text, bool handleBuffer, const QSet<QString>& macrosToBeIgnored);
    QString expandMacros(QString text, const QSet<QString>& macrosToBeIgnored) const;
    QString expandMacro(QString &text, const PDefine &define, int &i, bool handleBuffer, const QSet<QString> &macrosToBeIgnored, QSet<QString> &macrosUsed);

    void handleDefine(const QString& tokens);
    void handleUndefine(const QString& tokens);
    void handleIf(const QString& tokens);
    void handleIfdef(const QString& tokens);
    void handleIfndef(const QString& tokens);
    void handleElif(const QString& tokens);
    void handleElifdef(const QString& tokens);
    void handleElifndef(const QString& tokens);
    void handleElse(const QString& tokens);
    void handleEndif(const QString& tokens);
    void handleInclude(const QString&tokens);
    void handleIncludeNext(const QString& tokens);
    void handlePragma(const QString& tokens);

    void handleInclude(const QString& line, bool fromNext);
    void handlePreprocessor(const QString& command, const QString& tokens);
    QString removeGCCAttributes(const QString& line);
    void removeGCCAttribute(const QString&line, QString& newLine, int &i, const QString& word);

    // current file stuff
    PParsedFile getInclude(int index) const {
        return mIncludeStack[index];
    }
    void openInclude(QString fileName, bool documentRoot = false);
    void closeInclude();

    // branch stuff
    BranchResult getCurrentBranch(){
        if (!mBranchResults.isEmpty())
            return mBranchResults.last();
        else
            return BranchResult::isTrue;
    }
    BranchResult calcElseBranchResult(BranchResult oldResult);
    BranchResult calcUnsupportedElseBranchResult(BranchResult oldResult);
    bool sameResultWithCurrentBranch(BranchResult value) {
        return (getCurrentBranch()==BranchResult::isTrue && value == BranchResult::isTrue)
                || (getCurrentBranch()!=BranchResult::isTrue && value != BranchResult::isTrue);
    }
    void setCurrentBranch(BranchResult value){
        if (!mDependency && !sameResultWithCurrentBranch(value)) {
            mCurrentFileInfo->insertBranch(mIndex+1,value==BranchResult::isTrue);
        }
        mBranchResults.append(value);
    }
    void removeCurrentBranch(){
        BranchResult value = getCurrentBranch();
        if (mBranchResults.size()>0) {
            mBranchResults.pop_back();
        }
        if (!mDependency && !sameResultWithCurrentBranch(value)) {
            mCurrentFileInfo->insertBranch(mIndex,getCurrentBranch()==BranchResult::isTrue);
        }
    }
    void addDefinesInFile(const QString& fileName);
    void addDefineByParts(const QString& name, const QString& args,
                          const QString& value, bool hardCoded);
    void addDefineByLine(const QString& line, bool hardCoded);
    PDefine getHardDefine(const QString& name){
        return mHardDefines.value(name,PDefine());
    };
    void invalidDefinesInFile(const QString& fileName);

    void parseArgs(PDefine define);

    /*
     * '_','a'..'z','A'..'Z','0'..'9'
     */
    static  bool isWordChar(const QChar& ch) {
        return (ch=='_'
                || ch.isLetter()
                || (ch>='0' && ch<='9'));
    }

    /*
     * 'A'..'Z', '0'..'9', 'a'..'z', '_', '*', '&', '~'
     */
    static  bool isIdentChar(const QChar& ch) {
        return (ch=='_' || ch == '*' || ch == '&' || ch == '~' ||
                ch.isLetter()
                || (ch>='0' && ch<='9'));
    }

    /*
     * '\r','\n'
     */
    static  bool isLineChar(const QChar& ch) { return ch=='\r' || ch == '\n'; }
    /*
     *  '\t' ' '
     */
    static  bool isSpaceChar(const QChar& ch) { return ch == ' ' || ch == '\t'; }

    /*
     * 'A'..'Z', 'a'..'z', '_'
     */
    static  bool isMacroIdentStartChar(const QChar& ch) { return ch.isLetter() || ch == '_'; }

    /*
     * '0'..'9'
     */
    static  bool isDigit(const QChar& ch) { return (ch>='0' && ch<='9'); }

    /*
     * '0'..'9','x',X','a'..'f','A'..'F','u','U','l','L'
     */
    static  bool isNumberChar(const QChar& ch);

    QString lineBreak() { return "\n"; }

    bool skipParenthesis(const QString&line, int& index, int step = 1) const;
    bool skipSpaces(const QString &expr, int& pos) const;
    bool evalNumber(const QString &expr, NumberType& result, int& pos) const;
    bool evalTerm(const QString &expr, NumberType& result, int& pos) const;
    bool evalUnaryExpr(const QString &expr, NumberType& result, int& pos) const;
    bool evalMulExpr(const QString &expr, NumberType& result, int& pos) const;
    bool evalAddExpr(const QString &expr, NumberType& result, int& pos) const;
    bool evalShiftExpr(const QString &expr, NumberType& result, int& pos) const;
    bool evalRelationExpr(const QString &expr, NumberType& result, int& pos) const;
    bool evalEqualExpr(const QString &expr, NumberType& result, int& pos) const;
    bool evalBitAndExpr(const QString &expr, NumberType& result, int& pos) const;
    bool evalBitXorExpr(const QString &expr, NumberType& result, int& pos) const;
    bool evalBitOrExpr(const QString &expr, NumberType& result, int& pos) const;
    bool evalLogicAndExpr(const QString &expr, NumberType& result, int& pos) const;
    bool evalLogicOrExpr(const QString &expr, NumberType& result, int& pos) const;
    bool evalConnditionalExpr(const QString &expr, NumberType& result, int& pos) const;
    bool evalExpr(const QString &expr, NumberType& result, int& pos) const;

    bool evaluateExpression(QString line) const;
private:
    //temporary data when preprocessing single file
    int mIndex; // points to current file buffer.
    QString mFileName;
    QStringList mBuffer;
    QStringList mResult;
    PParsedFileInfo mCurrentFileInfo;
    int mPreProcIndex;    
    QList<PParsedFile> mIncludeStack; // stack of files we've stepped into. last one is current file, first one is source file
    QList<BranchResult> mBranchResults;// stack of branch results (boolean). last one is current branch, first one is outermost branch
    DefineMap mDefines; // working set, editable
    QSet<QString> mProcessed; // dictionary to save filename already processed

    bool mFileJustOpenned;
    QString mFileIncludeOnceToken;
    QSet<QString> mFilesCouldRepeatInclude;
    QHash<QString, QStringList> mFileCache;
    //Result across processings.
    //used by parser even preprocess finished
    QHash<QString, PParsedFileInfo> mFileInfos;
    QHash<QString, PDefineMap> mFileDefines; //dictionary to save defines for each headerfile;
    QHash<QString, PDefineMap> mFileUndefines; //dictionary to save defines for each headerfile;
    QSet<QString> mScannedFiles;

    //option data for the parser
    //{ List of current project's include path }
    DefineMap mHardDefines; // set by "cpp -dM -E -xc NUL"
    QSet<QString> mProjectIncludePaths;
    //we also need include paths in order (for #include_next)
    QList<QString> mIncludePathList;
    QList<QString> mProjectIncludePathList;
    //{ List of current compiler set's include path}
    QSet<QString> mIncludePaths;

    QMap<QString, std::function<void(const QString&)>> mPreprocessorHandlers;

    bool mParseSystem;
    bool mParseLocal;
    bool mSupportCPP23 = false;

    bool mStopForParserReset;
    bool mFileOnlyIncludeOnce;

    GetFileStreamFunc mOnGetFileStream;
    PAnalysisInputSnapshot mInputSnapshot;
    std::shared_ptr<AnalysisInput::SourceReadSession> mSourceReadSession;
    SourceReadObserver mSourceReadObserver;
};

using PCppPreprocessor = std::shared_ptr<CppPreprocessor>;

#endif // CPPPREPROCESSOR_H
