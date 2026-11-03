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
#ifndef CPPPARSER_H
#define CPPPARSER_H

#include <QMutex>
#include <QObject>
#include <QThread>
#include <QVector>
#include "statementmodel.h"
#include "cpptokenizer.h"
#include "cpppreprocessor.h"
#include "../documentstore.h"
#include "../analysis/parserconfiguration.h"
#include "../analysis/outlinejob.h"
#include <QPointer>
#include <atomic>
#include <optional>

namespace Analysis { class ParserToolchainProbeRequest; }

class CppParser;
using PCppParser = std::shared_ptr<CppParser>;

class CppParser : public QObject, public std::enable_shared_from_this<CppParser>
{
    Q_OBJECT
public:
    struct ParseFileCommand {
        QString fileName;
        bool inProject;
        QString contextFilename;
        bool onlyIfNotParsed;
        bool updateView;
        PAnalysisInputSnapshot inputs;
        SourceReadObserver readObserver;
        AnalysisInput::SourceReadRequestPtr sourceRequest;
        quint64 configurationTicket = 0;
        quint64 requestId = 0;
    };
    using PParseFileCommand = std::unique_ptr<ParseFileCommand>;

    explicit CppParser();
    CppParser(const CppParser&)=delete;
    CppParser& operator=(const CppParser&)=delete;

    ~CppParser();

    enum class ConfigurationState { Ready, Preparing, QueuedForApply, Applying, Failed, Superseded, Retired };
    Q_ENUM(ConfigurationState)
    quint64 beginConfigurationPreparation();
    bool prepareConfigurationAsync(quint64 ticket,
        std::shared_ptr<const Analysis::ParserToolchainProbeRequest> request);
    bool completeConfigurationPreparation(quint64 ticket, Analysis::PParserConfiguration configuration);
    bool failConfigurationPreparation(quint64 ticket, const QString &reason);
    quint64 submitConfiguration(Analysis::PParserConfiguration configuration);
    Analysis::ParserConfigurationValues requestedConfigurationValues() const;
    Analysis::PParserConfiguration appliedConfiguration() const;
    quint64 configurationTicket() const;
    quint64 appliedConfigurationTicket() const;
    QByteArray appliedConfigurationDigest() const;
    ConfigurationState configurationState() const;
    QString configurationError() const;
    int trackedAnalysisWorkerCount() const;
    void stopScheduling();
    bool analysisQuiesced() const;
    bool resumeScheduling();

    void setFileOnlyIncludeOnce(bool includeOnce);
    void addHardDefineByLine(const QString& line);
    void addProjectFile(const QString &fileName, bool needScan);
    void addIncludePath(const QString& value);
    void removeProjectFile(const QString& value);
    void addProjectIncludePath(const QString& value);
    void clearIncludePaths();
    void clearProjectIncludePaths();
    void clearProjectFiles();
    QList<PStatement> getListOfFunctions(const QString& fileName,
                             const QString& phrase,
                             int line) const;
    PStatement findScopeStatement(const QString& filename, int line) const;
    PParsedFileInfo findFileInfo(const QString &filename) const;
    QString findFirstTemplateParamOf(const QString& fileName,
                                     const QString& phrase,
                                     const PStatement& currentScope) const;
    QString findTemplateParamOf(const QString& fileName,
                                     const QString& phrase,
                                     int index,
                                     const PStatement& currentScope) const;
    PStatement findFunctionAt(const QString& fileName,
                            int line) const;
    int findLastOperator(const QString& phrase) const;
    PStatementList findNamespace(const QString& name) const; // return a list of PSTATEMENTS (of the namespace)
    PStatement findStatement(const QString& fullname) const;
    PStatement findStatementOf(const QString& fileName,
                               const QString& phrase,
                               int line) const;
    PStatement findStatementOf(const QString& fileName,
                               const QString& phrase,
                               const PStatement& currentScope,
                               PStatement& parentScopeType) const;

    PStatement findStatementOf(const QString& fileName,
                               const QStringList& expression,
                               const PStatement& currentScope) const;
    PStatement findStatementOf(const QString& fileName,
                               const QStringList& expression,
                               int line) const;
    PStatement findAliasedStatement(const PStatement& statement) const;
    QStringList getFunctionParameterNames(const PStatement& statement) const;

    QList<PStatement> listTypeStatements(const QString& fileName,int line) const;
    QList<PStatement> listLiteralOperators(const QString& fileName,int line) const;

    /**
     * @brief evaluate the expression
     * @param fileName
     * @param expression
     * @param currentScope
     * @return the statement of the evaluation result
     */
    PEvalStatement evalExpression(const QString& fileName,
                               QStringList& expression,
                               const PStatement& currentScope) const;
    PStatement findTypeDefinitionOf(const QString& fileName,
                                    const QString& aType,
                                    const PStatement& currentClass) const;
    PStatement findTypeDef(const PStatement& statement,
                          const QString& fileName) const;
    bool freeze();  // Freeze/Lock (stop reparse while searching)
    bool freeze(const QString& serialId);  // Freeze/Lock (stop reparse while searching)
    // Freeze only a fully up-to-date, idle parser; never wait for pending work.
    bool freezeStable();
    QStringList getClassesList() const;
    QStringList getFileDirectIncludes(const QString& filename) const;
    QSet<QString> getIncludedFiles(const QString& filename) const;
    QSet<QString> getFileUsings(const QString& filename) const;

    QString getHeaderFileName(const QString& relativeTo, const QString& headerName, bool fromNext=false) const;

    void invalidateFile(const QString& fileName);
    bool isLineVisible(const QString& fileName, int line) const;
    bool isIncludeLine(const QString &line) const;
    bool isIncludeNextLine(const QString &line) const;
    bool isProjectHeaderFile(const QString& fileName) const;
    bool isSystemHeaderFile(const QString& fileName) const;
    void parseHardDefines();
    bool parsing() const;
    bool hasPendingWork() const;
    void resetParser();
    void unFreeze(); // UnFree/UnLock (reparse while searching)
    bool fileScanned(const QString& fileName) const;

    bool isFileParsed(const QString& filename) const;

    QString prettyPrintStatement(const PStatement& statement, const QString& filename, int line = -1) const;

    bool enabled() const;
    void setEnabled(bool newEnabled);

    QSet<QString> filesToScan() const;
    void setFilesToScan(const QSet<QString> &newFilesToScan);

    void setOnGetFileStream(const GetFileStreamFunc &newOnGetFileStream);
    void setInputSnapshotProvider(InputSnapshotProvider provider);
    void setSourceReadObserver(SourceReadObserver observer);
    struct SourceJobReport {
        enum class Outcome { Complete, VerifiedNoChange, InputFailed, Stopped, UnverifiedCache };
        Outcome outcome = Outcome::UnverifiedCache;
        quint64 job = 0;
        quint64 inputGeneration = 0;
        QString workingDirectory;
        QHash<QString, AnalysisInput::SourceVersion> reads;
        QHash<QString, AnalysisInput::PathPresence> probes;
        QString error;
        quint64 configurationTicket = 0;
        QByteArray configurationDigest;
        bool complete() const { return outcome == Outcome::Complete || outcome == Outcome::VerifiedNoChange; }
    };
    using SourceJobReportPtr = std::shared_ptr<const SourceJobReport>;
    SourceJobReportPtr sourceJobReport() const;

    int parserId() const;
    quint64 outlineService() const noexcept { return mOutlineIdentities.service(); }
    bool submitOutline(Analysis::OutlineJobRequest request, std::shared_ptr<const Analysis::OutlineResources> resources);
    void cancelOutline();


    const QString &serialId() const;


    const StatementModel &statementList() const;

    ParserLanguage language() const;
    void setLanguage(ParserLanguage newLanguage);

    QSet<QString> projectFiles() const;

    QList<QString> namespaces();

    static bool isIdentifier(const QString& token) {
        return (!token.isEmpty() &&
                ( (token.startsWith("::") && token.length()>2)
                    || isIdentifierStartChar(token.front())
                  ));
        // return (!token.isEmpty() && isIdentChar(token.front())
        //         && !token.contains("\""));
    }

    static constexpr bool isSpaceChar(const QChar& ch) {
        return ch==' ' || ch =='\t';
    }


    bool sharedByFiles() const;
    void setSharedByFiles(bool newSharedByFiles);

    enum class ParseResult { Parsed, Busy, Disabled, NotParsed, InputUnavailable, SourceFailed, ConfigurationPending };
    enum class RequestResult { Accepted, QueuedForOwner, Disabled, InputUnavailable, ConfigurationUnavailable, SchedulingStopped };
    static ParseResult parseFileBlocking(
        PCppParser parser,
        const QString &fileName,
        bool inProject,
        const QString &contextFilename,
            bool onlyIfNotParsed = false,
            bool updateView = true);

    static RequestResult parseFileNonBlocking(
        PCppParser parser,
        const QString &fileName,
        bool inProject,
        const QString &contextFilename,
        bool onlyIfNotParsed = false,
        bool updateView = true);

    static RequestResult parseFileListNonBlocking(
            PCppParser parser,
            bool updateView = true);

    // Queue a coalesced reset barrier without blocking the caller.
    static void resetParsedResultsNonBlocking(PCppParser parser);


signals:
    void configurationStateChanged(quint64 ticket, CppParser::ConfigurationState state);
    void configurationReady(quint64 ticket, const QByteArray &digest);
    void configurationFailed(quint64 ticket, const QString &reason);
    void analysisStopped();
    void outlineFinished(const Analysis::OutlineJobResult &result);
    void configurationApplying(quint64 ticket);
    void analysisRequestSuperseded(quint64 ticket);
    void inputSnapshotUnavailable(const QString &reason);
    void progress(const QString& fileName, int total, int current);
    void onBusy();
    void parseStarted();
    void parseFinished(int total, int updateView);
    void cacheResetStarted();
    void cacheResetFinished();
private:
    void parseHardDefinesInternal();
    void runParsedResultsReset();
    void runInvalidateFiles(const QSet<QString> &paths);
    bool parseFile(const QString& fileName, bool inProject,
                   const QString& contextFilename,
                   bool onlyIfNotParsed = false, bool updateView = true,
                   bool consumeSyncReservation = false,
                   PAnalysisInputSnapshot inputs = {},
                   SourceReadObserver readObserver = {},
                   AnalysisInput::SourceReadRequestPtr sourceRequest = {}, quint64 configurationTicket = 0
                   );
    void parseFileList(bool updateView = true, PAnalysisInputSnapshot inputs = {}, SourceReadObserver readObserver = {},
                       AnalysisInput::SourceReadRequestPtr sourceRequest = {}, quint64 configurationTicket = 0);
    InputCaptureResult captureInputSnapshot() const;
    AnalysisInput::SourceReadRequestPtr captureSourceRequest(PAnalysisInputSnapshot inputs) const;
    void beginSourceJob(const AnalysisInput::SourceReadRequestPtr &request);
    bool finishSourceJob(bool verifiedNoChange = false);
    void clearEngineResultsPreservingConfig();

    PStatement addInheritedStatement(
            const PStatement& derived,
            const PStatement& inherit,
            StatementAccessibility access);

    PStatement addChildStatement(
            // support for multiple parents (only typedef struct/union use multiple parents)
            const PStatement& parent,
            const QString& fileName,
            const QString& aType, // "Type" is already in use
            const QString& command,
            const QString& args,
            const QString& noNameArgs,
            const QString& value,
            int line,
            StatementKind kind,
            const StatementScope& scope,
            const StatementAccessibility& classScope,
            StatementProperties properties); // TODO: InheritanceList not supported
    PStatement addStatement(
            const PStatement& parent,
            const QString &fileName,
            const QString &aType, // "Type" is already in use
            const QString &command,
            const QString &args,
            const QString &noNameArgs,
            const QString& value,
            int line,
            StatementKind kind,
            const StatementScope& scope,
            const StatementAccessibility& classScope,
            StatementProperties properties);
    PStatement addStatement(
            const PStatement& parent,
            const QString &fileName,
            const QString &aType, // "Type" is already in use
            const QString &command,
            int argStart,
            int argEnd,
            const QString& value,
            int line,
            StatementKind kind,
            const StatementScope& scope,
            const StatementAccessibility& classScope,
            StatementProperties properties);
    void addMethodParameterStatement(QStringList words,int line, const PStatement& functionStatement);
    void setInheritance(int index, const PStatement& classStatement, bool isStruct, int maxIndex);
    bool isCurrentScope(const QString& command) const;
    void addSoloScopeLevel(PStatement& statement, int line, bool shouldResetBlock=false); // adds new solo level
    void removeScopeLevel(int line, int maxIndex); // removes level

    int indexOfMatchingBrace(int startAt) const {
        return mTokenizer[startAt]->matchIndex;
    }

    void internalClear();

    QStringList sortFilesByIncludeRelations(const QSet<QString> &files);

    int evaluateConstExpr(int endIndex, bool &ok);
    int evaluateAdditionConstExpr(int endIndex, bool &ok);
    int evaluateMultiplyConstExpr(int endIndex, bool &ok);
    int evaluateConstExprTerm(int endIndex, bool &ok);
    int evaluateLiteralNumber(int endIndex, bool &ok);

    bool checkForConcept(KeywordType &keywordType) { return keywordType == KeywordType::Concept; }
    bool checkForKeyword(KeywordType &keywordType);
    bool checkForNamespace(KeywordType keywordType, int maxIndex);
    bool checkForPreprocessor();
//    bool checkForLambda();
    bool checkForAccessibilitySpecifiers(KeywordType keywordType);
    bool checkForRequires(KeywordType keywordType) { return keywordType == KeywordType::Requires; }
    bool checkForStructs(KeywordType keywordType, int maxIndex);
    bool checkForTypedefEnum(int maxIndex);
    bool checkForTypedefStruct(int maxIndex);
    bool checkForUsing(KeywordType keywordType, int maxIndex);

    void checkAndHandleMethodOrVar(KeywordType keywordType, int maxIndex);

    QSet<QString> internalGetFileUsings(const QString& filename) const;

    PStatement doFindScopeStatement(const QString& filename, int line) const;

    PStatementList doFindNamespace(const QString& name) const; // return a list of PSTATEMENTS (of the namespace)
    PStatement doFindStatement(const QString& fullname) const;
    PStatement doFindStatementOf(const QString& fileName,
                               const QString& phrase,
                               int line) const;
    PStatement doFindStatementOf(const QString& fileName,
                               const QString& phrase,
                               const PStatement& currentScope,
                               PStatement& parentScopeType) const;
    PStatement doFindStatementOf(const QString& fileName,
                               const QString& phrase,
                               const PStatement& currentClass) const;

    PStatement doFindStatementOf(const QString& fileName,
                               const QStringList& expression,
                               const PStatement& currentScope) const;
    PStatement doFindStatementOf(const QString& fileName,
                               const QStringList& expression,
                               int line) const;
    PStatement doFindAliasedStatement(const PStatement& statement, QSet<Statement *> foundSet) const;
    PStatement doFindAliasedStatement(const PStatement& statement) const;
    PStatement doFindNoTemplateSpecializationClass(const PStatement& statement) const;

    QList<PStatement> doListStatements(const QString& fileName,int line, const QSet<StatementKind> &kinds) const;

    PStatement doFindTypeDefinitionOf(const QString& fileName,
                                    const QString& aType,
                                    const PStatement& currentClass) const;
    QString doFindFirstTemplateParamOf(const QString& fileName,
                                     const QString& phrase,
                                     const PStatement& currentScope) const;
    QString doFindTemplateParamOf(const QString& fileName,
                                     const QString& phrase,
                                     int index,
                                     const PStatement& currentScope) const;
    QList<PStatement> getListOfFunctions(const QString& fileName, int line,
                                         const PStatement& statement,
                                         const PStatement& scopeStatement) const;
    QSet<QString> internalGetIncludedFiles(const QString &filename) const;
    PStatement findMacro(const QString& phrase, const QString& fileName) const;
    PStatement findMemberOfStatement(
            const QString& filename,
            const QString& phrase,
            const PStatement& scopeStatement) const ;
    PStatement findMemberOfStatement(
            const QString& phrase,
            const PStatement& scopeStatement) const ;
    QList<PStatement> findMembersOfStatement(const QString& phrase,
                                             const PStatement& scopeStatement) const;
    PStatement findStatementInScope(
            const QString& name,
            const QString& noNameArgs,
            StatementKind kind,
            const PStatement& scope) const;
    PStatement findStatementInScope(
            const QString& name,
            const PStatement& scope) const;
    PStatement findStatementInNamespace(
            const QString& name,
            const QString& namespaceName) const;

    //{Find statement starting from startScope}
    PStatement findStatementStartingFrom(const QString& fileName,
                                         const QString& phrase,
                                         const PStatement& startScope) const;

    /**
     * @brief evaluate the expression (starting from pos) in the scope
     * @param fileName
     * @param phraseExpression
     * @param pos
     * @param scope
     * @param previousResult the result of evalution for expression from 0 to pos-1
     * @param freeScoped if the expression left is
     * @return
     */
    PEvalStatement doEvalExpression(const QString& fileName,
                               QStringList phraseExpression,
                               int &pos,
                               const PStatement& scope,
                               const PEvalStatement& previousResult,
                               bool freeScoped,
                               bool expandMacros) const;

    /*  add +  / minus - */
    PEvalStatement doEvalArithmeticOperation(
            const QString& fileName,
            const QStringList& phraseExpression,
            int &pos,
            const PStatement& scope,
            const PEvalStatement& previousResult,
            bool freeScoped) const;
    /* Pointer to members .* / ->*  */
    PEvalStatement doEvalPointerToMembers(
            const QString& fileName,
            const QStringList& phraseExpression,
            int &pos,
            const PStatement& scope,
            const PEvalStatement& previousResult,
            bool freeScoped) const;

    /*
     * Dereference * / Address-of & / Type Cast / Prefix increment and decrement
     * */
    PEvalStatement doEvalTypeCast(
            const QString& fileName,
            const QStringList& phraseExpression,
            int &pos,
            const PStatement& scope,
            const PEvalStatement& previousResult,
            bool freeScoped) const;


    PEvalStatement doEvalMemberAccess(
            const QString& fileName,
            const QStringList& phraseExpression,
            int &pos,
            const PStatement& scope,
            const PEvalStatement& previousResult,
            bool freeScoped) const;
    PEvalStatement doEvalScopeResolution(
            const QString& fileName,
            const QStringList& phraseExpression,
            int &pos,
            const PStatement& scope,
            const PEvalStatement& previousResult,
            bool freeScoped) const;
    PEvalStatement doEvalTerm(
            const QString& fileName,
            const QStringList& phraseExpression,
            int &pos,
            const PStatement& scope,
            const PEvalStatement& previousResult,
            bool freeScoped) const;

    bool expandMacro(QStringList& phraseExpression,int pos,
                    PStatement macro, QList< QSet<QString> > &usedMacros) const;

    PEvalStatement doCreateEvalNamespace(const PStatement& namespaceStatement) const;

    PEvalStatement doFindAliasedNamespace(const PStatement& namespaceAlias) const;

    PEvalStatement doCreateEvalType(const QString& fileName,const QString& typeName, const PStatement& parentScope) const;
    PEvalStatement doCreateEvalType(const QString& fileName,const PStatement& typeStatement) const;
    PEvalStatement doCreateEvalType(const QString& primitiveType) const;

    PEvalStatement doCreateTypedEvalVar(const QString& fileName,const QString& typeName, const PStatement& parentScope, const PStatement &baseStatement) const;

    PEvalStatement doCreateEvalVariable(
            const QString& fileName,
            const PStatement& varStatement,
            const QString& baseTemplateParams,
            const PStatement& scope) const;
    PEvalStatement doCreateEvalFunction(const QString& fileName, const PStatement& funcStatement) const;
    PEvalStatement doCreateEvalLiteral(const QString& type) const;
    void  doSkipInExpression(const QStringList& expression, int&pos, const QString& startSymbol, const QString& endSymbol) const;

    QString findFunctionPointerName(int startIdx);

    static bool isIdentifierOrPointer(const QString& term){
        switch(term[0].unicode()) {
        case '*':
            return true;
        default:
            return isIdentifier(term);
        }
    }


    static bool isIntegerLiteral(const QString& token){
        if (token.isEmpty())
            return false;
        QChar ch = token.front();
        return (ch>='0' && ch<='9' && !token.contains(".") && !token.contains("e"));
    }
    static bool isFloatLiteral(const QString& token){
        if (token.isEmpty())
            return false;
        QChar ch = token.front();
        return (ch>='0' && ch<='9' && (token.contains(".") || token.contains("e")));
    }
    static bool isStringLiteral(const QString& token){
        if (token.isEmpty())
            return false;
        return (!token.startsWith('\'') && token.contains('"'));
    }

    static bool isCharLiteral(const QString& token){
        if (token.isEmpty())
            return false;
        return (token.startsWith('\''));
    }

    bool isKeyword(const QString& token) const {
        return mCppKeywords.contains(token);
    }

    bool tokenIsTypeOrNonKeyword(const QString& token) const {
        return isIdentifier(token) &&
                (mCppTypeKeywords.contains(token)
                 || !mCppKeywords.contains(token)
                 || token=="const");

    }

    PStatement doParseEvalTypeInfo(
            const QString& fileName,
            const PStatement& scope,
            const QString& type,
            QString& baseType,
            PStatement& typeStatement,
            int& pointerLevel,
            QString& templateParams) const;

    int getBracketEnd(const QString& s, int startAt) const;
    StatementAccessibility getClassMemberAccessibility(const QString& text) const;
    StatementAccessibility getClassMemberAccessibility(KeywordType keywordType) const;
//    int getCurrentBlockBeginSkip() const;
//    int getCurrentBlockEndSkip() const;
    int getCurrentInlineNamespaceEndSkip(int endIndex) const;
    PStatement getCurrentScope() const; // gets last item from last level
    QString getTemplateParam(const PStatement& statement, const QString& filename,
                                  const QString& phrase, int index, const PStatement& currentScope) const;
    int getTemplateParamStart(const QString& s, int startAt, int index) const;
    int getTemplateParamEnd(const QString& s, int startAt) const ;

    void getFullNamespace(
            const QString& phrase,
            QString& sNamespace,
            QString& member) const;
    QString getFullStatementName(
            const QString& command,
            const PStatement& parent) const;
    PStatement getIncompleteClass(
            const QString& command,
            const PStatement& parentScope);
    QString getScopePrefix(const PStatement& statement) const;
    StatementScope  getScope();
    QString getStatementKey(const QString& sName,
                            const QString& sType,
                            const QString& sNoNameArgs) const;
    PStatement getTypeDef(const PStatement& statement,
                          const QString& fileName, const QString& aType) const;
//    void handleCatchBlock();
    void doAddVar(const QString& name, const QString& type, bool isConst, const QString& suffix);
    void handleConcept(int maxIndex);
    void handleEnum(bool isTypedef, int maxIndex);
    void handleForBlock(int maxIndex);
    void handleKeyword(KeywordType skipType, int maxIndex);
    void handleIf(int maxIndex);
    void handleLambda(int index, int maxIndex);
    void handleOperatorOverloading(
            const QString& sType,
            int operatorTokenIndex,
            bool isStatic, int maxIndex);
    void handleMethod(
            StatementKind functionKind,
            const QString& sType,
            const QString& sName,
            int argStart,
            bool isStatic,
            bool isFriend,
            bool isOperatorOverload,
            int maxIndex);
    void handleNamespace(KeywordType skipType, int maxIndex);
    void handleOtherTypedefs(int maxIndex);
    void handlePreprocessor();
    void handleAccessibilitySpecifiers(KeywordType keywordType, int maxIndex);
    bool handleStatement(int maxIndex);
    void handleStructs(bool isTypedef, int maxIndex);
    void handleStructuredBinding(const QString& sType, int maxIndex);
    void handleUsing(int maxIndex);
    void handleVar(const QString& typePrefix,bool isExtern,bool isStatic, int maxIndex);
    void handleInheritance(PStatement derivedClass, PClassInheritanceInfo pInfo);
    void handleInheritances();
    void handleLabel();
    void skipRequires(int maxIndex);
    void internalParse(const QString& fileName);
//    function FindMacroDefine(const Command: AnsiString): PStatement;
    void inheritClassStatement(
            const PStatement& derived,
            bool isStruct,
            const PStatement& base,
            StatementAccessibility access);
    PStatement doFindStatementInScope(const QString& name,
                                      const QString& noNameArgs,
                                      StatementKind kind,
                                      const PStatement& scope) const;
    void internalInvalidateFile(const QString& fileName);
    void internalInvalidateFiles(const QSet<QString>& files);
    QSet<QString> calculateFilesToBeReparsed(const QString& fileName);
    void scanMethodArgs(
            const PStatement& functionStatement,
            int argStart);
    QSet<QString> parseLambdaCaptures(int index);
    QString splitPhrase(const QString& phrase, QString& sClazz,
                QString& sOperator, QString &sMember) const;
    QString removeTemplateParams(const QString& phrase) const;

    bool splitLastMember(const QString& token, QString& lastMember, QString& remaining);


    static bool isIdentifierOrPointerOrReference(const QString& s){
        return isIdentifier(s)
                || s[0] == '*'
                || s[0] == '&';
    }    

    static constexpr  bool isIdentifierStartChar(const QChar& ch){
        return ch.isLetter()
                || ch == '_';
    }

    static constexpr  bool isIdentifierChar(const QChar& ch){
        return ch.isLetter()
                || (ch>='0' && ch<='9')
                || ch == '_';
    }

    static constexpr bool isDigit(const QChar& ch){
        return (ch>='0' && ch<='9');
    }

    static constexpr bool isInvalidFunctionArgsSuffixChar(const QChar& ch) {
        // &&
        switch(ch.unicode()){
        case '.':
        case '-':
        case '+':
        case '/':
        case '%':
        case '*':
        case '|':
        case '?':
            return true;
        default:
            return false;
        }
    }

    /*'(', ';', ':', '{', '}', '#' */
    static constexpr bool isSeperator(const QChar& ch){
        switch(ch.unicode()){
        case '(':
        case ';':
        case ':':
        case '{':
        case '}':
        case '#':
            return true;
        default:
            return false;
        }
    }

    /*';', '{', '}'*/
    static constexpr bool isblockChar(const QChar& ch){
        switch(ch.unicode()){
        case ';':
        case '{':
        case '}':
            return true;
        default:
            return false;
        }
    }

    /* '#', ',', ';', ':', '{', '}', '!', '/', '+', '-', '<', '>' */
    static constexpr bool isInvalidVarPrefixChar(const QChar& ch){
        switch (ch.unicode()) {
        case '#':
        case ',':
        case ';':
        case ':':
        case '{':
        case '}':
        case '!':
        case '/':
        case '+':
        case '-':
        case '<':
        case '>':
            return true;
        default:
            return false;
        }
    }

    /*'{', '}' */
    static constexpr bool isBraceChar(const QChar& ch){
        return ch == '{' || ch =='}';
    }

    static constexpr bool isLineChar(const QChar& ch){
        return ch=='\n' || ch=='\r';
    }

    bool isNotFuncArgs(int startIndex);

    /**
     * @brief Test if a statement is a class/struct/union/namespace/function
     * @param kind
     * @return
     */
    static constexpr bool isNamedScope(StatementKind kind);

    /**
     * @brief Test if a statement is a class/struct/union/enum/enum class/typedef
     * @param kind
     * @return
     */
    bool isTypeStatement(StatementKind kind) const;

    void updateSerialId();

    int indexOfNextSemicolon(int index, int maxIndex);
    int indexOfNextPeriodOrSemicolon(int index, int maxIndex);
    int indexOfNextSemicolonOrLeftBrace(int index, int maxIndex);
    int indexOfNextColon(int index, int maxIndex);
    int indexOfNextLeftBrace(int index, int maxIndex);
    int indexPassParenthesis(int index, int maxIndex);
    int indexOfNextRightParenthesis(int index, int maxIndex);
//    int indexPassBraces(int index);
    int skipAssignment(int index, int maxIndex);
    void skipNextSemicolon(int index, int maxIndex);
    int moveToEndOfStatement(int index, bool checkLambda, int maxIndex);
//    int moveToNextAssignmentOrEndOfStatement(int index, bool checkLambda, int endIndex=-1);
    void skipParenthesis(int index, int maxIndex);
    QString mergeArgs(int startIndex, int endIndex);
    void parseCommandTypeAndArgs(QString& command,
                                 QString& typeSuffix,
                                 QString& args) const;
    QString expandMacros(const QString& text) const;
    static QStringList splitExpression(const QString& expr);
private:
    int mParserId;
    ParserLanguage mLanguage;
    bool mIsSystemHeader;

    int mSerialCount;
    QString mSerialId;
    int mUniqId;
    bool mEnabled;
    int mIndex;
    bool mIsHeader;
    bool mSharedByFiles;
    QString mCurrentFile;
//  stack list , each element is a list of one/many scopes(like intypedef struct  s1,s2;
//  It's used for store scope nesting infos
    QVector<PStatement> mCurrentScope;
    QVector<StatementAccessibility> mMemberAccessibilities;

    StatementAccessibility mCurrentMemberAccessibility;
    StatementModel mStatementList;
    //It's used in preprocessor, so we can't use fIncludeList instead

    CppTokenizer mTokenizer;
    CppPreprocessor mPreprocessor;
    QSet<QString> mProjectFiles;
//    QVector<int> mBlockBeginSkips; //list of for/catch block begin token index;
//    QVector<int> mBlockEndSkips; //list of for/catch block end token index;
    QVector<int> mInlineNamespaceEndSkips; // list for inline namespace end token index;
    QSet<QString> mFilesToScan; // list of base files to scan
    int mFilesScannedCount; // count of files that have been scanned
    int mFilesToScanCount; // count of files and files included in files that have to be scanned
    bool mIsProjectFile;
    int mLockCount; // lock(don't reparse) when we need to find statements in a batch
    bool mParsing;
    QHash<QString,PStatementList> mNamespaces;  // namespace and the statements in its scope
    QList<PClassInheritanceInfo> mClassInheritances;
    QSet<QString> mInlineNamespaces;
    std::atomic_bool mStopForReset{false};
#ifdef QT_DEBUG
    int mLastIndex;
#endif
    mutable QRecursiveMutex mMutex;
    QMap<QString,KeywordType> mCppKeywords;
    QSet<QString> mCppTypeKeywords;

    struct FileListCommand {
        bool updateView = false;
        PAnalysisInputSnapshot inputs;
        SourceReadObserver readObserver;
        AnalysisInput::SourceReadRequestPtr sourceRequest;
        QSet<QString> roots;
    };
    enum class AnalysisCommandKind { ParseFile, ParseFileList, ResetParsedResults, InvalidateFiles, PrepareConfiguration, ApplyConfiguration, BuildOutline };
    struct AnalysisCommand {
        AnalysisCommandKind kind = AnalysisCommandKind::ParseFile;
        quint64 id = 0;
        quint64 ticket = 0;
        ParseFileCommand file;
        FileListCommand files;
        QSet<QString> invalidatedFiles;
        Analysis::OutlineJobRequest outline;
        std::shared_ptr<const Analysis::OutlineResources> outlineResources;
        Analysis::PParserConfiguration configuration;
        std::shared_ptr<const Analysis::ParserToolchainProbeRequest> probe;
        std::shared_ptr<std::atomic_bool> cancelled = std::make_shared<std::atomic_bool>(false);
    };
    QList<AnalysisCommand> mAnalysisQueue;
    std::optional<OutlineDemand> mExpectedOutline;
    Analysis::SemanticIdentitySource mOutlineIdentities;
    QThread *makeOutlineWorker(const PCppParser &owner, const AnalysisCommand &command);
    Analysis::OutlineJobResult runOutlineCommand(const AnalysisCommand &command);
    std::optional<AnalysisCommand> mActiveAnalysisCommand;
    QPointer<QThread> mActiveAnalysisWorker;
    bool mWriterReserved = false;
    bool mSchedulingStopped = false;
    quint64 mNextAnalysisRequest = 0;
    Analysis::ParserConfigurationValues mRequestedConfigurationValues;
    Analysis::PParserConfiguration mAppliedConfiguration;
    QByteArray mAppliedConfigurationDigest;
    quint64 mRequestedConfigurationTicket = 1;
    quint64 mAppliedConfigurationTicket = 1;
    ConfigurationState mConfigurationState = ConfigurationState::Ready;
    QString mConfigurationError;
    bool mRequestedBuilderDirty = false;
    bool mConfigurationSealScheduled = false;
    bool mResetAfterConfiguration = false;
    QSet<QString> mRequestedFilesToScan;
    std::shared_ptr<const Analysis::ParserToolchainProbeRequest> mRequestedProbeTemplate;
    PParseFileCommand mActiveParseFileCommand;
    bool mActiveFileList = false;
    bool mActiveFileListUpdateView = false;
    PAnalysisInputSnapshot mActiveFileListInputs;
    AnalysisInput::SourceReadRequestPtr mActiveFileListSourceRequest;
    SourceReadObserver mActiveFileListReadObserver;
    QSet<QString> mActiveFileListNames;
    std::weak_ptr<CppParser> mAsyncOwner;
    bool mPendingDispatchScheduled = false;
    bool mSyncParseReserved = false;
    InputSnapshotProvider mInputSnapshotProvider;
    SourceReadObserver mSourceReadObserver;
    std::shared_ptr<AnalysisInput::SourceReadSession> mSourceSession;
    AnalysisInput::SourceReadRequestPtr mActiveSourceRequest;
    SourceJobReportPtr mSourceJobReport;
    quint64 mSourceJobSequence = 0;
    quint64 mSourceConfigGeneration = 0;
    quint64 mActiveSourceConfigGeneration = 0;
    quint64 mCompletedSourceConfigGeneration = 0;
    quint64 mActiveSourceConfigurationTicket = 1;
    QByteArray mActiveSourceConfigurationDigest;
    bool mSourceCacheCertified = false;
    bool mSourceJobRebuild = false;
    QSet<QString> mSourceRoots;
    void queueParseFileCommand(const ParseFileCommand &command);
    void queueOlderParseFileCommand(const ParseFileCommand &command);
    void schedulePendingParseCommands();
    void startPendingParseCommands();
    void queueFileListCommand(FileListCommand command, quint64 ticket, bool older = false);
    bool queuedReset() const;
    bool activeReset() const;
    bool onControlOwner() const;
    void changedRequestedConfiguration(const std::function<void(Analysis::ParserConfigurationValues &)> &change);
    void sealRequestedConfiguration();
    bool applyConfigurationValue(quint64 ticket, const Analysis::PParserConfiguration &configuration,
                                 QString &error);
    void runConfigurationCommand(const AnalysisCommand &command, Analysis::PParserConfiguration &result,
                                 QString &error, bool &cancelled);
    void finishAnalysisWorker(quint64 requestId, QThread *worker,
                              Analysis::PParserConfiguration prepared = {}, QString error = {}, bool cancelled = false);
    bool applyReadyConfigurationForBlockingFixture();
    void clearEngineConfiguration();

    friend class TestCppParser; // Regression coverage of private expression tokenization.
    friend class CppFileListParserThread;
    friend class CppFileParserThread;
    friend class CppParsedResultsResetThread;
    friend class CppConfigurationThread;
    friend class TestParserConfigurationQueue;

};

class CppFileParserThread : public QThread {
    Q_OBJECT
public:
    explicit CppFileParserThread(
            PCppParser parser,
            const QString &fileName,
            bool inProject,
            const QString &contextFilename,
            bool onlyIfNotParsed = false,
            bool updateView = true,
            QObject *parent = nullptr,
            PAnalysisInputSnapshot inputs = {},
            SourceReadObserver readObserver = {},
            AnalysisInput::SourceReadRequestPtr sourceRequest = {}, quint64 configurationTicket = 0);
    ~CppFileParserThread();
private:
    PCppParser mParser;
    QString mFileName;
    bool mInProject;
    QString mContextFilename;
    bool mOnlyIfNotParsed;
    bool mUpdateView;
    int mId;
    PAnalysisInputSnapshot mInputs;
    SourceReadObserver mReadObserver;
    AnalysisInput::SourceReadRequestPtr mSourceRequest;
    bool mInputsAvailable = true;
    quint64 mConfigurationTicket = 0;

    // QThread interface
protected:
    void run() override;
};
using PCppParserThread = std::shared_ptr<CppFileParserThread>;

class CppFileListParserThread: public QThread {
    Q_OBJECT
public:
    explicit CppFileListParserThread(
            PCppParser parser,
            bool updateView = true,
            QObject *parent = nullptr,
            PAnalysisInputSnapshot inputs = {},
            SourceReadObserver readObserver = {},
            AnalysisInput::SourceReadRequestPtr sourceRequest = {}, quint64 configurationTicket = 0);
private:
    PCppParser mParser;
    bool mUpdateView;
    PAnalysisInputSnapshot mInputs;
    SourceReadObserver mReadObserver;
    AnalysisInput::SourceReadRequestPtr mSourceRequest;
    bool mInputsAvailable = true;
    quint64 mConfigurationTicket = 0;
    // QThread interface
protected:
    void run() override;
};

class CppParsedResultsResetThread: public QThread {
public:
    explicit CppParsedResultsResetThread(PCppParser parser, QSet<QString> invalidatedFiles = {});
protected:
    void run() override;
private:
    PCppParser mParser;
    const QSet<QString> mInvalidatedFiles;
};

#endif // CPPPARSER_H
