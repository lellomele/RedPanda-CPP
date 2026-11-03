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
#include "codecompletionpopup.h"
#include <QVarLengthArray>
#include "../analysis/semanticpublication.h"
#include "../utils.h"
#include "../mainwindow.h"
#include "../editor.h"
#include "../editormanager.h"
#include "../symbolusagemanager.h"
#include "../colorscheme.h"
#include "../iconsmanager.h"

#include <QKeyEvent>
#include <QVBoxLayout>
#include <QDebug>
#include <QApplication>
#include <QPainter>

CodeCompletionPopup::CodeCompletionPopup(ColorManager *colorManager,IconsManager *iconsManager,QWidget *parent) :
    QWidget(parent),
    mListView(nullptr),
    mSymbolUsageManager(nullptr)
{
    setWindowFlags(Qt::Popup);
    mColorManager = colorManager;
    mListView = new CodeCompletionListView(this);
    mModel=new CodeCompletionListModel(iconsManager);
    mDelegate = new CodeCompletionListItemDelegate(mModel,this);
    QItemSelectionModel *m=mListView->selectionModel();
    mListView->setModel(mModel);
    delete m;
    mListView->setItemDelegate(mDelegate);
    setLayout(new QVBoxLayout());
    layout()->addWidget(mListView);
    layout()->setContentsMargins(0, 0, 0, 0);
    mListView->setFocus();

    mShowKeywords=true;
    mRecordUsage = false;
    mSortByScope = true;

    mShowCount = 1000;
    mShowCodeSnippets = true;

    mIgnoreCase = false;

    mHideSymbolsStartWithTwoUnderline = false;
    mHideSymbolsStartWithUnderline = false;
}

CodeCompletionPopup::~CodeCompletionPopup()
{
    delete mListView;
    delete mModel;
}

void CodeCompletionPopup::setKeypressedCallback(const KeyPressedCallback &newKeypressedCallback)
{
    mListView->setKeypressedCallback(newKeypressedCallback);
}

void CodeCompletionPopup::setShowEditorCaretFunc(const ShowEditorCaretFunc &newShowEditorCaretFunc)
{
    mListView->setShowEditorCaretFunc(newShowEditorCaretFunc);
}

void CodeCompletionPopup::prepareSearch(
        const QString &preWord,
        const QStringList &ownerExpression,
        const QString& memberOperator,
        const QStringList& memberExpression,
        const QString &filename,
        int line,
        CodeCompletionType type,
        const QSet<QString>& customKeywords)
{
    Q_ASSERT(QThread::currentThread() == thread());
    mFullCompletionStatementList.clear();
    mModel->replaceRows({});
    mIncludedFiles.clear();
    mUsings.clear();
    mAddedStatements.clear();
    if (!isEnabled())
        return;
    // hideEvent releases the parser; queued search requests may arrive afterwards.
    const bool parserOptional = type == CodeCompletionType::KeywordsOnly
            || type == CodeCompletionType::ComplexKeyword;
    if (!mParser && !parserOptional)
        return;
    const auto parser = mParser;
    const bool reading = !parserOptional && parser && parser->freeze();
    const auto finishRead = finally([this, parser, reading] {
        mCurrentScope.reset();
        if (reading) parser->unFreeze();
    });
    if (reading) {
        mIncludedFiles = mParser->getIncludedFiles(filename);
        mCurrentScope = mParser->findScopeStatement(filename, line);
    } else if (!parserOptional && type != CodeCompletionType::Normal) {
        return;
    }

    QCursor oldCursor = cursor();
    setCursor(Qt::CursorShape::WaitCursor);

    mMemberPhrase = memberExpression.join("");
    mMemberOperator = memberOperator;
    switch(type) {
    case CodeCompletionType::ComplexKeyword:
        getCompletionListForComplexKeyword(preWord);
        break;
    case CodeCompletionType::Labels:
        getCompletionListForLabels(filename,line);
        break;
    case CodeCompletionType::Types:
        getCompletionListForTypes(preWord,filename,line);
        break;
    case CodeCompletionType::FunctionWithoutDefinition:
        getCompletionForFunctionWithoutDefinition(preWord, ownerExpression,memberOperator,memberExpression, filename,line);
        break;
    case CodeCompletionType::Namespaces:
        getCompletionListForNamespaces(preWord,filename,line);
        break;
    case CodeCompletionType::KeywordsOnly:
        mIncludedFiles.clear();
        getKeywordCompletionFor(customKeywords);
        break;
    case CodeCompletionType::Macros:
        getMacroCompletionList(filename, line);
        break;
    case CodeCompletionType::LiteralOperators:
        getCompletionListForLiteralOperators(filename,line);
        break;
    default:
        getCompletionFor(ownerExpression,memberOperator,memberExpression, filename,line, customKeywords, reading);
    }
    setCursor(oldCursor);
}

bool CodeCompletionPopup::search(const QString &memberPhrase, bool autoHideOnSingleResult, const QString& schemeName)
{
    Q_ASSERT(QThread::currentThread() == thread());

    mMemberPhrase = memberPhrase;

    if (!isEnabled()) {
        hide();
        return false;
    }

    QCursor oldCursor = cursor();
    setCursor(Qt::CursorShape::WaitCursor);

    filterList(memberPhrase);
    setCursor(oldCursor);

    if (mModel->rowCount({}) != 0) {
        Q_ASSERT(!schemeName.isEmpty());
        PColorSchemeItem item = mColorManager->getItem(schemeName, COLOR_SCHEME_ACTIVE_LINE);
        if (item && item->background().isValid())
            mDelegate->setCurrentSelectionColor(item->background());
        else
            mDelegate->setCurrentSelectionColor(palette().highlight().color());
        item = mColorManager->getItem(schemeName, COLOR_SCHEME_TEXT);
        if (item && item->foreground().isValid())
            mDelegate->setNormalColor(item->foreground());
        else
            mDelegate->setNormalColor(palette().color(QPalette::Text));
        item = mColorManager->getItem(schemeName, SYNS_AttrReserveWord_Type);
        if (item && item->foreground().isValid())
            mDelegate->setMatchedColor(item->foreground());
        else
            mDelegate->setMatchedColor(palette().color(QPalette::HighlightedText));
        mListView->setCurrentIndex(mModel->index(0,0));
        show();
        // if only one suggestion, and is exactly the symbol to search, hide the frame (the search is over)
        // if only one suggestion and auto hide , don't show the frame
        if (mModel->rowCount({}) == 1)
            if (autoHideOnSingleResult
                    || (memberPhrase == mModel->statement(mModel->index(0, 0))->command)) {
            return true;
        }
    } else {
        hide();
    }
    return false;
}

std::optional<CompletionEntry> CodeCompletionPopup::selectedStatement() const
{
    if (!isEnabled()) return {};
    const auto current = mListView->currentIndex();
    const auto *entry = mModel->statement(current.isValid() ? current : mModel->index(0, 0));
    return entry ? std::optional<CompletionEntry>{*entry} : std::nullopt;
}

CompletionInsertion CodeCompletionPopup::insertionFor(const CompletionEntry &entry, bool appendFunc) const
{
    const auto parser = mParser;
    if (!appendFunc || !entry.alias || !parser || !parser->freeze(entry.alias->parserSerial)) return entry;
    const auto release = finally([&] { parser->unFreeze(); });
    // Reconstruct only the alias lookup parameters, while the certificate
    // still refers to the graph that produced this row.
    auto query = std::make_shared<Statement>();
    query->kind = StatementKind::Alias;
    query->type = entry.alias->type;
    query->fileName = entry.alias->file;
    if (const auto resolved = parser->findAliasedStatement(query))
        return {resolved->command, resolved->fullName, resolved->args, resolved->kind};
    return entry;
}

void CodeCompletionPopup::addChildren(const PStatement& scopeStatement,
                                      const QString &fileName,
                                      int line,
                                      bool onlyTypes)
{
    if (scopeStatement && !isIncluded(scopeStatement->fileName)
      && !isIncluded(scopeStatement->definitionFileName))
        return;
    const StatementMap& children = mParser->statementList().childrenStatements(scopeStatement);
    if (children.isEmpty())
        return;

    if (onlyTypes) {
        if (!scopeStatement) { //Global scope
            for (const PStatement& childStatement: children) {
                if (!isTypeKind(childStatement->kind))
                    continue;
                if (childStatement->fileName.isEmpty()) {
                    // hard defines
                    addStatement(childStatement,fileName,-1);
                } else if (
                           isIncluded(childStatement->fileName)
                           || isIncluded(childStatement->definitionFileName)
                           ) {
                    //we must check if the statement is included by the file
                    addStatement(childStatement,fileName,line);
                }
            }
        } else {
            for (const PStatement& childStatement: children) {
                if (!isTypeKind(childStatement->kind))
                    continue;
                addStatement(childStatement,fileName,line);
            }
        }
    } else {
        if (!scopeStatement) { //Global scope
            for (const PStatement& childStatement: children) {
                if (childStatement->fileName.isEmpty()) {
                    // hard defines
                    addStatement(childStatement,fileName,-1);
                } else if (
                           isIncluded(childStatement->fileName)
                           || isIncluded(childStatement->definitionFileName)
                           ) {
                    //we must check if the statement is included by the file
                    addStatement(childStatement,fileName,line);
                }
            }
        } else {
            for (const PStatement& childStatement: children) {
                addStatement(childStatement,fileName,line);
            }
        }

    }
}

void CodeCompletionPopup::addFunctionWithoutDefinitionChildren(const PStatement& scopeStatement, const QString &fileName, int line)
{
    if (scopeStatement && !isIncluded(scopeStatement->fileName)
      && !isIncluded(scopeStatement->definitionFileName))
        return;
    const StatementMap& children = mParser->statementList().childrenStatements(scopeStatement);
    if (children.isEmpty())
        return;

    for (const PStatement& childStatement: children) {
        if (childStatement->inSystemHeader())
            continue;
        if (childStatement->fileName.isEmpty()) {
            // hard defines, do nothing
            continue;
        }
        switch(childStatement->kind) {
        case StatementKind::Constructor:
        case StatementKind::Function:
        case StatementKind::Destructor:
            if (!childStatement->hasDefinition())
                addStatement(childStatement,fileName,line);
            break;
        case StatementKind::Class:
        case StatementKind::Namespace:
            if (isIncluded(childStatement->fileName))
                addStatement(childStatement,fileName,line);
            break;
        default:
            break;
        }
    }
}

void CodeCompletionPopup::addStatement(const PStatement& statement, const QString &fileName, int line)
{
    if (mAddedStatements.contains(statement->command))
        return;
    if (statement->kind == StatementKind::Constructor
            || statement->kind == StatementKind::Destructor
            || statement->kind == StatementKind::Label
            || statement->kind == StatementKind::Block
            || statement->kind == StatementKind::Lambda
            || (
                statement->properties.testFlag(StatementProperty::OperatorOverloading)
                && statement->kind != StatementKind::LiteralOperator
                )
            || statement->properties.testFlag(StatementProperty::DummyStatement)
            )
        return;
    if ((line!=-1)
            && (line < statement->line)
            && (fileName == statement->fileName))
        return;
    mAddedStatements.insert(statement->command);
    if (statement->kind == StatementKind::UserCodeSnippet || !statement->command.contains("<"))
        appendCandidate(statement);
}

void CodeCompletionPopup::appendCandidate(const PStatement &statement)
{
    CompletionEntry entry;
    entry.command = statement->command;
    entry.fullName = statement->fullName;
    entry.args = statement->args;
    entry.kind = statement->kind;
    entry.iconKind = getKindOfStatement(statement);
    entry.scope = statement->scope;
    entry.accessibility = statement->accessibility;
    entry.inherited = statement->isInherited();
    entry.systemHeader = statement->inSystemHeader();
    if (mRecordUsage && mSymbolUsageManager) {
        if (const auto usage = mSymbolUsageManager->findUsage(entry.fullName))
            entry.usageCount = usage->count;
    }
    if (entry.kind == StatementKind::Alias)
        entry.alias = CompletionAliasQuery{statement->type, statement->fileName, mParser->serialId()};
    mFullCompletionStatementList.append(std::move(entry));
}

static bool nameComparator(const CompletionEntry &statement1, const CompletionEntry &statement2) {
    return statement1.command < statement2.command;
}

static bool defaultComparator(const CompletionEntry &statement1, const CompletionEntry &statement2) {
    if (statement1.matchPosSpan!=statement2.matchPosSpan)
        return statement1.matchPosSpan < statement2.matchPosSpan;
    if (statement1.firstMatchLength != statement2.firstMatchLength)
        return statement1.firstMatchLength > statement2.firstMatchLength;
    if (statement1.matchPosTotal != statement2.matchPosTotal)
        return statement1.matchPosTotal < statement2.matchPosTotal;
    if (statement1.caseMatched != statement2.caseMatched)
        return statement1.caseMatched > statement2.caseMatched;
    // Show user template first
    if (statement1.kind == StatementKind::UserCodeSnippet) {
        if (statement2.kind != StatementKind::UserCodeSnippet)
            return true;
        else
            return statement1.command < statement2.command;
    } else if (statement2.kind == StatementKind::UserCodeSnippet) {
        return false;
        // show keywords first
    } else if ((statement1.kind == StatementKind::Keyword)
               && (statement2.kind != StatementKind::Keyword)) {
        return true;
    } else if ((statement1.kind != StatementKind::Keyword)
               && (statement2.kind == StatementKind::Keyword)) {
        return false;
    } else
        return nameComparator(statement1,statement2);
}

static bool sortByScopeComparator(const CompletionEntry &statement1, const CompletionEntry &statement2){
    if (statement1.matchPosSpan!=statement2.matchPosSpan)
        return statement1.matchPosSpan < statement2.matchPosSpan;
    if (statement1.firstMatchLength != statement2.firstMatchLength)
        return statement1.firstMatchLength > statement2.firstMatchLength;
    if (statement1.matchPosTotal != statement2.matchPosTotal)
        return statement1.matchPosTotal < statement2.matchPosTotal;
    if (statement1.caseMatched != statement2.caseMatched)
        return statement1.caseMatched > statement2.caseMatched;
    // Show user template first
    if (statement1.kind == StatementKind::UserCodeSnippet) {
        if (statement2.kind != StatementKind::UserCodeSnippet)
            return true;
        else
            return statement1.command < statement2.command;
    } else if (statement2.kind == StatementKind::UserCodeSnippet) {
        return false;
        // show non-system defines before keyword
    } else if (statement1.kind == StatementKind::Keyword) {
        if (statement2.kind != StatementKind::Keyword) {
            //s1 keyword / s2 system defines, s1 < s2, should return true
            //s1 keyword / s2 not system defines, s2 < s1, should return false;
            return  statement2.inSystemHeader();
        } else
            return statement1.command < statement2.command;
    } else if (statement2.kind == StatementKind::Keyword) {
        //s1 system defines / s2 keyword, s2 < s1, should return false;
        //s1 not system defines / s2 keyword, s1 < s2, should return true;
        return  (!statement1.inSystemHeader());
    }
    // Show stuff from local headers first
    if (statement1.inSystemHeader() != statement2.inSystemHeader())
        return !(statement1.inSystemHeader());
        // Show local statements first
    if (statement1.scope != StatementScope::Global
               && statement2.scope == StatementScope::Global ) {
        return true;
    } else if (statement1.scope == StatementScope::Global
               && statement2.scope != StatementScope::Global ) {
        return false;
    } else
        return nameComparator(statement1,statement2);
}

static bool sortWithUsageComparator(const CompletionEntry &statement1, const CompletionEntry &statement2) {
    if (statement1.matchPosSpan!=statement2.matchPosSpan)
        return statement1.matchPosSpan < statement2.matchPosSpan;
    if (statement1.firstMatchLength != statement2.firstMatchLength)
        return statement1.firstMatchLength > statement2.firstMatchLength;
    if (statement1.matchPosTotal != statement2.matchPosTotal)
        return statement1.matchPosTotal < statement2.matchPosTotal;
    if (statement1.caseMatched != statement2.caseMatched)
        return statement1.caseMatched > statement2.caseMatched;
    // Show user template first
    if (statement1.kind == StatementKind::UserCodeSnippet) {
        if (statement2.kind != StatementKind::UserCodeSnippet)
            return true;
        else
            return statement1.command < statement2.command;
    } else if (statement2.kind == StatementKind::UserCodeSnippet) {
        return false;
        //show most freq first
    }
    if (statement1.usageCount != statement2.usageCount)
        return statement1.usageCount > statement2.usageCount;

    if ((statement1.kind != StatementKind::Keyword)
               && (statement2.kind == StatementKind::Keyword)) {
        return true;
    } else if ((statement1.kind == StatementKind::Keyword)
               && (statement2.kind != StatementKind::Keyword)) {
        return false;
    } else
        return nameComparator(statement1,statement2);
}

static bool sortByScopeWithUsageComparator(const CompletionEntry &statement1, const CompletionEntry &statement2){
    if (statement1.matchPosSpan!=statement2.matchPosSpan)
        return statement1.matchPosSpan < statement2.matchPosSpan;
    if (statement1.firstMatchLength != statement2.firstMatchLength)
        return statement1.firstMatchLength > statement2.firstMatchLength;
    if (statement1.matchPosTotal != statement2.matchPosTotal)
        return statement1.matchPosTotal < statement2.matchPosTotal;
    if (statement1.caseMatched != statement2.caseMatched)
        return statement1.caseMatched > statement2.caseMatched;
    // Show user template first
    if (statement1.kind == StatementKind::UserCodeSnippet) {
        if (statement2.kind != StatementKind::UserCodeSnippet)
            return true;
        else
            return statement1.command < statement2.command;
    } else if (statement2.kind == StatementKind::UserCodeSnippet) {
        return false;
        //show most freq first
    }
    if (statement1.usageCount != statement2.usageCount)
        return statement1.usageCount > statement2.usageCount;

        // show non-system defines before keyword
    if (statement1.kind == StatementKind::Keyword) {
        if (statement2.kind != StatementKind::Keyword) {
            //s1 keyword / s2 system defines, s1 < s2, should return true
            //s1 keyword / s2 not system defines, s2 < s1, should return false;
            return  statement2.inSystemHeader();
        } else
            return statement1.command < statement2.command;
    } else if (statement2.kind == StatementKind::Keyword) {
        //s1 system defines / s2 keyword, s2 < s1, should return false;
        //s1 not system defines / s2 keyword, s1 < s2, should return true;
        return  (!statement1.inSystemHeader());
    }
    // Show stuff from local headers first
    if (statement1.inSystemHeader() != statement2.inSystemHeader())
        return !(statement1.inSystemHeader());
        // Show local statements first
    if (statement1.scope != StatementScope::Global
               && statement2.scope == StatementScope::Global ) {
        return true;
    } else if (statement1.scope == StatementScope::Global
               && statement2.scope != StatementScope::Global ) {
        return false;
    } else
        return nameComparator(statement1,statement2);
}

void CodeCompletionPopup::filterList(const QString &member)
{
    const bool hideDouble = mHideSymbolsStartWithTwoUnderline && !member.startsWith("__");
    const bool hideSingle = mHideSymbolsStartWithUnderline && !member.startsWith('_');
    const auto sensitivity = mIgnoreCase ? Qt::CaseInsensitive : Qt::CaseSensitive;
    QList<CompletionEntry> rows;
    rows.reserve(member.isEmpty() ? mFullCompletionStatementList.size()
                                  : std::min(mFullCompletionStatementList.size(), qsizetype(256)));
    for (const auto &candidate : std::as_const(mFullCompletionStatementList)) {
        const auto &command = candidate.command;
        if ((hideDouble && command.startsWith("__")) || (hideSingle && command.startsWith('_'))) continue;
        if (command.size() < member.size()) continue;

        QVarLengthArray<CompletionMatchRange, 8> matches;
        qsizetype caseMatched = 0;
        qint64 totalPosition = 0;
        qsizetype searchFrom = 0;
        bool found = true;
        for (const QChar ch : member) {
            const qsizetype position = command.indexOf(ch, searchFrom, sensitivity);
            if (position < 0) {
                found = false;
                break;
            }
            if (!matches.isEmpty() && matches.back().end == position)
                ++matches.back().end;
            else
                matches.append({position, position + 1});
            if (ch == command[position]) ++caseMatched;
            totalPosition += position;
            searchFrom = position + 1;
        }
        if (!found) continue;
        auto row = candidate;
        row.caseMatched = caseMatched;
        row.matchPosTotal = totalPosition;
        if (!matches.isEmpty()) {
            row.firstMatchLength = matches.front().end - matches.front().start;
            row.matchPosSpan = matches.back().end - matches.front().start;
            row.matchPositions = QList<CompletionMatchRange>(matches.cbegin(), matches.cend());
        }
        rows.append(std::move(row));
    }
    const auto comparator = mRecordUsage
        ? (mSortByScope ? sortByScopeWithUsageComparator : sortWithUsageComparator)
        : (mSortByScope ? sortByScopeComparator : defaultComparator);
    std::sort(rows.begin(), rows.end(), comparator);
    mModel->replaceRows(std::move(rows));
}

void CodeCompletionPopup::getKeywordCompletionFor(const QSet<QString> &customKeywords)
{
    //add keywords
    if (!customKeywords.isEmpty()) {
        foreach (const QString& keyword,customKeywords) {
            addKeyword(keyword);
        }
    }
}

void CodeCompletionPopup::getMacroCompletionList(const QString &fileName, int line)
{
    const StatementMap& statementMap = mParser->statementList().childrenStatements(nullptr);
    foreach(const PStatement& statement, statementMap.values()) {
        if (statement->kind==StatementKind::Preprocessor)
            addStatement(statement,fileName,line);
    }
}

void CodeCompletionPopup::getCompletionFor(
        QStringList ownerExpression,
        const QString& memberOperator,
        const QStringList& memberExpression,
        const QString &fileName,
        int line,
        const QSet<QString>& customKeywords, bool reading)
{

    if (memberOperator.isEmpty() && ownerExpression.isEmpty() && memberExpression.isEmpty())
        return;

    bool isLambdaReturnType = (
                memberOperator=="->"
                && ownerExpression.startsWith("[")
                && ownerExpression.endsWith(")"));

    if (memberOperator.isEmpty()) {
        //C++ preprocessor directives
        if (mMemberPhrase.startsWith('#')) {
            if (mShowKeywords) {
                foreach (const QString& keyword, CppDirectives) {
                    addKeyword(keyword);
                }
            }
            return;
        }

        //docstring tags (javadoc style)
        if (mMemberPhrase.startsWith('@')) {
            if (mShowKeywords) {
                foreach (const QString& keyword,JavadocTags) {
                    addKeyword(keyword);
                }
            }
            return;
        }

        //the identifier to be completed is not a member of variable/class
        if (mShowCodeSnippets) {
            //add custom code templates
            foreach (const PCodeSnippet& codeIn,mCodeSnippets) {
                if (!codeIn->code.isEmpty()) {
                    CompletionEntry entry;
                    entry.command = codeIn->prefix;
                    entry.value = codeIn->code;
                    entry.kind = entry.iconKind = StatementKind::UserCodeSnippet;
                    entry.fullName = codeIn->prefix;
                    mFullCompletionStatementList.append(std::move(entry));
                }
            }
        }

        if (mShowKeywords) {
            //add keywords
            if (!customKeywords.isEmpty()) {
                foreach (const QString& keyword,customKeywords) {
                    addKeyword(keyword);
                }
            }
        }
    } else if  (isLambdaReturnType) {
            foreach (const QString& keyword,CppTypeKeywords) {
                addKeyword(keyword);
            }
    }

    if (!reading) return;
    {

        if (memberOperator.isEmpty() || isLambdaReturnType) {
            PStatement scopeStatement = mCurrentScope;
            // repeat until reach global
            while (scopeStatement) {
                //add members of current scope that not added before
                if (scopeStatement->kind == StatementKind::Namespace) {
                    PStatementList namespaceStatementsList =
                            mParser->findNamespace(scopeStatement->fullName);
                    foreach (const PStatement& namespaceStatement,*namespaceStatementsList) {
                        addChildren(namespaceStatement, fileName, line, isLambdaReturnType);
                    }
                } else if (scopeStatement->kind == StatementKind::Class) {
                    addChildren(scopeStatement, fileName, -1, isLambdaReturnType);
                } else {
                    addChildren(scopeStatement, fileName, line, isLambdaReturnType);
                }

                // add members of all usings (in current scope ) and not added before
                foreach (const QString& namespaceName,scopeStatement->usingList) {
                    PStatementList namespaceStatementsList =
                            mParser->findNamespace(namespaceName);
                    if (!namespaceStatementsList)
                        continue;
                    foreach (const PStatement& namespaceStatement,*namespaceStatementsList) {
                        addChildren(namespaceStatement, fileName, line, isLambdaReturnType);
                    }
                }
                if (scopeStatement->kind == StatementKind::Lambda) {
                    foreach (const QString& phrase, scopeStatement->lambdaCaptures) {
                        if (phrase=="&" || phrase == "=" || phrase =="this")
                            continue;
                        PStatement statement = mParser->findStatementOf(
                            scopeStatement->fileName,
                                phrase,scopeStatement->line);
                        if (statement)
                            addStatement(statement,scopeStatement->fileName, scopeStatement->line);
                    }
                    if (scopeStatement->lambdaCaptures.contains("&")
                            || scopeStatement->lambdaCaptures.contains("=")) {
                        scopeStatement = scopeStatement->parentScope.lock();
                    } else if (scopeStatement->lambdaCaptures.contains("this")) {
                        do {
                            scopeStatement = scopeStatement->parentScope.lock();
                        } while (scopeStatement && scopeStatement->kind!=StatementKind::Class
                                 && scopeStatement->kind!=StatementKind::Namespace);
                    } else {
                        do {
                            scopeStatement = scopeStatement->parentScope.lock();
                        } while (scopeStatement && scopeStatement->kind!=StatementKind::Namespace);
                    }
                    continue;
                }
                scopeStatement=scopeStatement->parentScope.lock();
            }

            // add all global members and not added before
            addChildren(nullptr, fileName, line, isLambdaReturnType);

            // add members of all fusings
            mUsings = mParser->getFileUsings(fileName);
            foreach (const QString& namespaceName, mUsings) {
                PStatementList namespaceStatementsList =
                        mParser->findNamespace(namespaceName);
                if (!namespaceStatementsList)
                    continue;
                foreach (const PStatement& namespaceStatement, *namespaceStatementsList) {
                    addChildren(namespaceStatement, fileName, line, isLambdaReturnType);
                }
            }

        } else {
            //the identifier to be completed is a member of variable/class
            if (memberOperator == "::" && ownerExpression.isEmpty()) {
                // start with '::', we only find in global
                // add all global members and not added before
                addChildren(nullptr, fileName, line);
                return;
            }
            if (memberExpression.length()==2 && memberExpression.front()!="~")
                return;
            if (memberExpression.length()>2)
                return;

            PStatement scope = mCurrentScope;//the scope the expression in
            PStatement parentTypeStatement;
            PEvalStatement ownerStatement = mParser->evalExpression(fileName,
                                        ownerExpression,
                                        scope);

            if(!ownerStatement || !ownerStatement->effectiveTypeStatement) {
                return;
            }
            if (memberOperator == "::") {
                if (ownerStatement->kind==EvalStatementKind::Namespace) {
                    //there might be many statements corresponding to one namespace;
                    PStatementList namespaceStatementsList =
                            mParser->findNamespace(ownerStatement->baseType);
                    if (namespaceStatementsList) {
                        foreach (const PStatement& namespaceStatement, *namespaceStatementsList) {
                            addChildren(namespaceStatement, fileName, line);
                        }
                    }
                    return;
                }
            }

            // find the most inner scope statement that has a name (not a block)
            PStatement scopeTypeStatement = mCurrentScope;
            while (scopeTypeStatement && !isScopeTypeKind(scopeTypeStatement->kind)) {
                scopeTypeStatement = scopeTypeStatement->parentScope.lock();
            }
            if (
                    (memberOperator != "::")
                    && (
                        ownerStatement->kind == EvalStatementKind::Variable)
                    ) {
                // Get type statement  of current (scope) statement
                PStatement classTypeStatement = ownerStatement->effectiveTypeStatement;

                if (!classTypeStatement)
                    return;
                // It's a iterator
                if (ownerStatement
                        && ownerStatement->typeStatement
                        && STLIterators.contains(ownerStatement->typeStatement->command)
                        && (memberOperator == "->"
                            || memberOperator == "->*")
                ) {
                    PStatement parentScope = ownerStatement->typeStatement->parentScope.lock();
                    if (STLContainers.contains(parentScope->fullName)) {
                        QString typeName=mParser->findFirstTemplateParamOf(fileName,ownerStatement->templateParams, parentScope);
//                        qDebug()<<"typeName"<<typeName<<lastResult->baseStatement->type<<lastResult->baseStatement->command;
                        classTypeStatement=mParser->findTypeDefinitionOf(fileName, typeName,parentScope);
                        if (!classTypeStatement)
                            return;
                    } else if (STLMaps.contains(parentScope->fullName)) {
                        QString typeName="std::pair";
                        classTypeStatement=mParser->findTypeDefinitionOf(fileName, typeName,parentScope);
                        if (!classTypeStatement)
                            return;
                    }
                } else if (STLPointers.contains(classTypeStatement->fullName)
                   && (memberOperator == "->"
                       || memberOperator == "->*")
                        && ownerStatement->baseStatement) {
                                   //is a smart pointer
                    QString typeName= mParser->findFirstTemplateParamOf(
                                fileName,
                                ownerStatement->baseStatement->type,
                                scope);
                    classTypeStatement = mParser->findTypeDefinitionOf(
                                fileName,
                                typeName,
                                scope);
                    if (!classTypeStatement)
                        return;
                } else {
                    //normal member access
                    if (memberOperator=="." && ownerStatement->pointerLevel !=0)
                        return;
                    if (memberOperator=="->" && ownerStatement->pointerLevel!=1)
                        return;
                }
                if (!isIncluded(classTypeStatement->fileName) &&
                    !isIncluded(classTypeStatement->definitionFileName))
                    return;
                if ((classTypeStatement == scopeTypeStatement) || (ownerStatement->effectiveTypeStatement->command == "this")) {
                    //we can use all members
                    addChildren(classTypeStatement,fileName,-1);
                } else { // we can only use public members
                    const StatementMap& children = mParser->statementList().childrenStatements(classTypeStatement);
                    if (children.isEmpty())
                        return;
                    foreach (const PStatement& childStatement, children) {
                        if ((childStatement->accessibility==StatementAccessibility::Public)
                                && !(
                                    childStatement->kind == StatementKind::Constructor
                                    || childStatement->kind == StatementKind::Destructor)
                                && !mAddedStatements.contains(childStatement->command)) {
                            addStatement(childStatement,fileName,-1);
                        }
                    }
                }
            //todo friend
            } else if ((memberOperator == "::")
                       && (ownerStatement->kind == EvalStatementKind::Type)) {
                //we can add all child enum definess
                PStatement classTypeStatement = ownerStatement->effectiveTypeStatement;
                if (!classTypeStatement)
                    return;
                if (!isIncluded(classTypeStatement->fileName) &&
                    !isIncluded(classTypeStatement->definitionFileName))
                    return;
                if (classTypeStatement->kind == StatementKind::EnumType
                        || classTypeStatement->kind == StatementKind::EnumClassType) {
                    const StatementMap& children =
                            mParser->statementList().childrenStatements(classTypeStatement);
                    foreach (const PStatement& child,children) {
                        addStatement(child,fileName,line);
                    }
                } else {
                    //class
                    if (classTypeStatement == scopeTypeStatement) {
                        //we can use all static members
                        const StatementMap& children =
                                mParser->statementList().childrenStatements(classTypeStatement);
                        foreach (const PStatement& childStatement, children) {
                            if (
                              (childStatement->isStatic())
                               || (childStatement->kind == StatementKind::Typedef
                                || childStatement->kind == StatementKind::Class
                                || childStatement->kind == StatementKind::Enum
                                || childStatement->kind == StatementKind::EnumClassType
                                || childStatement->kind == StatementKind::EnumType
                                   )) {
                                addStatement(childStatement,fileName,-1);
                            }
                        }
                    } else {
                        // we can only use public static members
                        const StatementMap& children =
                                mParser->statementList().childrenStatements(classTypeStatement);
                        foreach (const PStatement& childStatement,children) {
                            if (
                              (childStatement->isStatic())
                               || (childStatement->kind == StatementKind::Typedef
                                || childStatement->kind == StatementKind::Class
                                || childStatement->kind == StatementKind::Enum
                                || childStatement->kind == StatementKind::EnumClassType
                                || childStatement->kind == StatementKind::EnumType
                                   )) {
                                if (childStatement->accessibility == StatementAccessibility::Public)
                                    addStatement(childStatement,fileName,-1);
                            }
                        }
                    }
                }
            }
        }
    }
}

void CodeCompletionPopup::getCompletionForFunctionWithoutDefinition(const QString& preWord, QStringList ownerExpression, const QString &memberOperator, const QStringList &memberExpression, const QString &fileName, int line)
{
    if(!mParser) {
        return;
    }
    if (!mParser->enabled())
        return;
    if (memberOperator.isEmpty() && ownerExpression.isEmpty() && memberExpression.isEmpty())
        return;

    {

        if (memberOperator.isEmpty()) {
            getCompletionListForComplexKeyword(preWord);
            PStatement scopeStatement = mCurrentScope;
            //add members of current scope that not added before
            while (scopeStatement && scopeStatement->kind!=StatementKind::Namespace
                   && scopeStatement->kind!=StatementKind::Class) {
                scopeStatement = scopeStatement->parentScope.lock();
            }
            if (scopeStatement) {
                if (scopeStatement->kind == StatementKind::Namespace) {
                    //namespace;
                    PStatementList namespaceStatementsList =
                            mParser->findNamespace(scopeStatement->fullName);
                    if (namespaceStatementsList) {
                        foreach (const PStatement& namespaceStatement, *namespaceStatementsList) {
                            addFunctionWithoutDefinitionChildren(namespaceStatement, fileName, line);
                        }
                    }
                } else {
                    //class
                    addKeyword("operator");
                    addFunctionWithoutDefinitionChildren(scopeStatement, fileName, line);
                }
            } else {
                //global
                addFunctionWithoutDefinitionChildren(scopeStatement, fileName, line);
            }
        } else {
            if (memberOperator != "::")
                return;
            //the identifier to be completed is a member of variable/class

            if (ownerExpression.isEmpty()) {
                // start with '::', we only find in global
                // add all global members and not added before
                addFunctionWithoutDefinitionChildren(nullptr, fileName, line);
                return;
            }
            if (memberExpression.length()==2 && memberExpression.front()!="~")
                return;
            if (memberExpression.length()>2)
                return;

            PStatement scope = mCurrentScope;//the scope the expression in
            PStatement parentTypeStatement;
            PEvalStatement ownerStatement = mParser->evalExpression(fileName,
                                        ownerExpression,
                                        scope);
            if(!ownerStatement  || !ownerStatement->effectiveTypeStatement) {
                return;
            }
            if (ownerStatement->kind==EvalStatementKind::Namespace) {
                //there might be many statements corresponding to one namespace;
                PStatementList namespaceStatementsList =
                        mParser->findNamespace(ownerStatement->baseType);
                if (namespaceStatementsList) {
                    foreach (const PStatement& namespaceStatement, *namespaceStatementsList) {
                        addFunctionWithoutDefinitionChildren(namespaceStatement, fileName, line);
                    }
                }
                return;
            } else if (ownerStatement->effectiveTypeStatement->kind == StatementKind::Class) {
                addKeyword("operator");
                addFunctionWithoutDefinitionChildren(ownerStatement->effectiveTypeStatement, fileName, line);
            }
        }
    }
}

void CodeCompletionPopup::getCompletionListForLabels(const QString &fileName, int line)
{
    if (!mParser->enabled())
        return;

    {
        PStatement scopeStatement = mParser->findScopeStatement(fileName,line);
        if (scopeStatement && (scopeStatement->kind == StatementKind::Function
                || scopeStatement->kind == StatementKind::Constructor
                || scopeStatement->kind == StatementKind::Destructor
                || scopeStatement->kind == StatementKind::Lambda)
                ) {
            foreach(const PStatement &child, scopeStatement->children) {
                if (child->kind == StatementKind::Label) {
                    appendCandidate(child);
                }
            }
        }
    }
}

void CodeCompletionPopup::getCompletionListForComplexKeyword(const QString &preWord)
{
    mFullCompletionStatementList.clear();
    if (preWord == "long") {
        addKeyword("long");
        addKeyword("double");
        addKeyword("int");
    } else if (preWord == "short") {
        addKeyword("int");
    } else if (preWord == "signed") {
        addKeyword("long");
        addKeyword("short");
        addKeyword("int");
        addKeyword("char");
    } else if (preWord == "unsigned") {
        addKeyword("long");
        addKeyword("short");
        addKeyword("int");
        addKeyword("char");
    } else if (preWord == "using") {
        addKeyword("namespace");
    }
}

void CodeCompletionPopup::getCompletionListForNamespaces(const QString &/*preWord*/,
                                                         const QString& fileName,
                                                         int line)
{
    if (!mParser->enabled())
        return;

    {
        QList<QString> namespaceNames = mParser->namespaces();
        foreach (const QString& name, namespaceNames) {
            PStatementList namespaces = mParser->findNamespace(name);
            foreach(const PStatement& statement, *namespaces) {
                if (isIncluded(statement->fileName)
                        || isIncluded(statement->definitionFileName)) {
                    addStatement(statement,fileName,line);
                    continue;
                }
            }

        }
    }
}

void CodeCompletionPopup::getCompletionListForTypes(const QString &preWord, const QString &fileName, int line)
{
    if (preWord=="typedef") {
        addKeyword("const");
        addKeyword("struct");
        addKeyword("class");
    }

    if (mShowKeywords) {
        //add keywords
        foreach (const QString& keyword,CppTypeKeywords) {
            addKeyword(keyword);
        }
    }
    if (!mParser->enabled())
        return;

    {
        QList<PStatement> statements = mParser->listTypeStatements(fileName,line);
        foreach(const PStatement& statement, statements) {
            if (isIncluded(statement->fileName)
                    || isIncluded(statement->definitionFileName)) {
                addStatement(statement,fileName,line);
            }
        }
    }
}

void CodeCompletionPopup::getCompletionListForLiteralOperators(const QString &fileName, int line)
{
    if (!mParser->enabled())
        return;

    {
        QList<PStatement> statements = mParser->listLiteralOperators(fileName,line);
        foreach(const PStatement& statement, statements) {
            if (isIncluded(statement->fileName)
                    || isIncluded(statement->definitionFileName)) {
                addStatement(statement,fileName,line);
            }
        }
    }
}

void CodeCompletionPopup::addKeyword(const QString &keyword)
{
    CompletionEntry entry;
    entry.command = entry.fullName = keyword;
    entry.kind = entry.iconKind = StatementKind::Keyword;
    mFullCompletionStatementList.append(std::move(entry));
}

bool CodeCompletionPopup::isIncluded(const QString &fileName)
{
    return mIncludedFiles.contains(fileName);
}

SymbolUsageManager *CodeCompletionPopup::symbolUsageManager() const
{
    return mSymbolUsageManager;
}

void CodeCompletionPopup::setSymbolUsageManager(SymbolUsageManager *newSymbolUsageManager)
{
    mSymbolUsageManager = newSymbolUsageManager;
}

void CodeCompletionPopup::setHideSymbolsStartWithTwoUnderline(bool newHideSymbolsStartWithTwoUnderline)
{
    mHideSymbolsStartWithTwoUnderline = newHideSymbolsStartWithTwoUnderline;
}

void CodeCompletionPopup::setLineHeightFactor(float factor)
{
    mDelegate->setLineHeightFactor(factor);
}

bool CodeCompletionPopup::hideSymbolsStartWithTwoUnderline() const
{
    return mHideSymbolsStartWithTwoUnderline;
}

bool CodeCompletionPopup::hideSymbolsStartWithUnderline() const
{
    return mHideSymbolsStartWithUnderline;
}

void CodeCompletionPopup::setHideSymbolsStartWithUnderline(bool newHideSymbolsStartWithUnderline)
{
    mHideSymbolsStartWithUnderline = newHideSymbolsStartWithUnderline;
}

const QString &CodeCompletionPopup::memberOperator() const
{
    return mMemberOperator;
}

const QList<PCodeSnippet> &CodeCompletionPopup::codeSnippets() const
{
    return mCodeSnippets;
}

void CodeCompletionPopup::setCodeSnippets(const QList<PCodeSnippet> &newCodeSnippets)
{
    mCodeSnippets = newCodeSnippets;
}

void CodeCompletionPopup::setColors(const std::shared_ptr<QHash<StatementKind, std::shared_ptr<ColorSchemeItem> > > &newColors)
{
    mColors = newColors;
}

const QString &CodeCompletionPopup::memberPhrase() const
{
    return mMemberPhrase;
}

const std::shared_ptr<QHash<StatementKind, std::shared_ptr<ColorSchemeItem> > >& CodeCompletionPopup::colors() const
{
    return mColors;
}

bool CodeCompletionPopup::sortByScope() const
{
    return mSortByScope;
}

void CodeCompletionPopup::setSortByScope(bool newSortByScope)
{
    mSortByScope = newSortByScope;
}

bool CodeCompletionPopup::ignoreCase() const
{
    return mIgnoreCase;
}

void CodeCompletionPopup::setIgnoreCase(bool newIgnoreCase)
{
    mIgnoreCase = newIgnoreCase;
}

bool CodeCompletionPopup::showCodeSnippets() const
{
    return mShowCodeSnippets;
}

void CodeCompletionPopup::setShowCodeSnippets(bool newShowCodeIns)
{
    mShowCodeSnippets = newShowCodeIns;
}

bool CodeCompletionPopup::showKeywords() const
{
    return mShowKeywords;
}

void CodeCompletionPopup::setShowKeywords(bool newShowKeywords)
{
    mShowKeywords = newShowKeywords;
}

bool CodeCompletionPopup::recordUsage() const
{
    return mRecordUsage;
}

void CodeCompletionPopup::setRecordUsage(bool newRecordUsage)
{
    mRecordUsage = newRecordUsage;
}

int CodeCompletionPopup::showCount() const
{
    return mShowCount;
}

void CodeCompletionPopup::setShowCount(int newShowCount)
{
    mShowCount = newShowCount;
}

const PCppParser &CodeCompletionPopup::parser() const
{
    return mParser;
}

void CodeCompletionPopup::setParser(const PCppParser &newParser)
{
    mParser = newParser;
}

void CodeCompletionPopup::hideEvent(QHideEvent *event)
{
    Q_ASSERT(QThread::currentThread() == thread());
    mListView->setKeypressedCallback(nullptr);
    mModel->replaceRows({});
    mFullCompletionStatementList.clear();
    mIncludedFiles.clear();
    mUsings.clear();
    mAddedStatements.clear();
    mCurrentScope = nullptr;
    mParser = nullptr;
    QWidget::hideEvent(event);
}

bool CodeCompletionPopup::event(QEvent *event)
{
    bool result = QWidget::event(event);
    if (event->type() == QEvent::FontChange) {
        if (mListView) {
            mListView->setFont(font());
            mDelegate->setFont(font());
        }
    }
    return result;
}

CodeCompletionListModel::CodeCompletionListModel(IconsManager *iconsManager, QObject *parent)
    : QAbstractListModel(parent), mIconsManager(iconsManager)
{
}

int CodeCompletionListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(mRows.size());
}

QVariant CodeCompletionListModel::data(const QModelIndex &index, int role) const
{
    const auto *entry = statement(index);
    return entry && role == Qt::DisplayRole ? QVariant(entry->command) : QVariant{};
}

const CompletionEntry *CodeCompletionListModel::statement(const QModelIndex &index) const
{
    if (!index.isValid() || index.model() != this || index.row() < 0 || index.row() >= mRows.size())
        return nullptr;
    return &mRows.at(index.row());
}

QPixmap CodeCompletionListModel::statementIcon(const QModelIndex &index, int size) const
{
    const auto *entry = statement(index);
    if (!mIconsManager || !entry) return {};
    return mIconsManager->getPixmapForOutline({entry->iconKind, entry->scope, entry->accessibility, entry->inherited}, size);
}

void CodeCompletionListModel::replaceRows(QList<CompletionEntry> rows)
{
    beginResetModel();
    mRows = std::move(rows);
    endResetModel();
}

void CodeCompletionListItemDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    const auto *statement = mModel ? mModel->statement(index) : nullptr;
    if (statement) {
        QFont normalFont{font()};
        QFont matchedFont{font()};
        normalFont.setBold(false);
        matchedFont.setBold(true);
        painter->save();
        painter->setFont(normalFont);
        QColor normalColor = mNormalColor;
        QColor matchedColor = mMatchedColor;
        if (option.state & QStyle::State_Selected) {
            painter->fillRect(option.rect, mCurrentSelectionColor);
        }
        QFontMetrics fm{font()};
        int iconSize = fm.height()*0.8;
        QPixmap icon = mModel->statementIcon(index, iconSize);
        int x=option.rect.left();
        if (!icon.isNull()) {
            qreal dpr=icon.devicePixelRatioF();
            int x1=x+(option.rect.height()-icon.width()/dpr)/2;
            int y1=option.rect.top()+(option.rect.height()-icon.height()/dpr)/2;
            painter->drawPixmap(x1,y1,icon);
            x+=option.rect.height();
        }
        QString text = statement->command;
        int pos=0;
        int padding = (option.rect.height()-painter->fontMetrics().height())/2;
        int y=option.rect.bottom()-painter->fontMetrics().descent()-padding;
        for (const auto &matchPosition : statement->matchPositions) {
            if (pos<matchPosition.start) {
                QString t = text.mid(pos,matchPosition.start-pos);
                painter->setPen(normalColor);
                painter->setFont(normalFont);
                painter->drawText(x,y,t);
                x+=painter->fontMetrics().horizontalAdvance(t);
            }
            QString t = text.mid(matchPosition.start, matchPosition.end-matchPosition.start);
            painter->setPen(matchedColor);
            painter->setFont(matchedFont);
            painter->drawText(x,y,t);
            x+=painter->fontMetrics().horizontalAdvance(t);
            pos=matchPosition.end;
        }
        if (pos<text.length()) {
            QString t = text.mid(pos,text.length()-pos);
            painter->setPen(normalColor);
            painter->setFont(normalFont);
            painter->drawText(x,y,t);
            x+=painter->fontMetrics().horizontalAdvance(t);
        }
        painter->restore();
    } else {
        QStyledItemDelegate::paint(painter, option, index);
    }
}

const QColor &CodeCompletionListItemDelegate::normalColor() const
{
    return mNormalColor;
}

void CodeCompletionListItemDelegate::setNormalColor(const QColor &newNormalColor)
{
    mNormalColor = newNormalColor;
}

const QColor &CodeCompletionListItemDelegate::matchedColor() const
{
    return mMatchedColor;
}

void CodeCompletionListItemDelegate::setMatchedColor(const QColor &newMatchedColor)
{
    mMatchedColor = newMatchedColor;
}

const QFont &CodeCompletionListItemDelegate::font() const
{
    return mFont;
}

void CodeCompletionListItemDelegate::setFont(const QFont &newFont)
{
    mFont = newFont;
}

QSize CodeCompletionListItemDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QSize size = QStyledItemDelegate::sizeHint(option, index);
    size.setHeight(QFontMetrics(mFont).height()*mLineHeightFactor);
    return size;
}

float CodeCompletionListItemDelegate::lineHeightFactor() const
{
    return mLineHeightFactor;
}

void CodeCompletionListItemDelegate::setLineHeightFactor(float newLineHeightFactor)
{
    mLineHeightFactor = newLineHeightFactor;
}

QColor CodeCompletionListItemDelegate::currentSelectionColor() const
{
    return mCurrentSelectionColor;
}

void CodeCompletionListItemDelegate::setCurrentSelectionColor(const QColor &newCurrentSelectionColor)
{
    mCurrentSelectionColor = newCurrentSelectionColor;
}

CodeCompletionListItemDelegate::CodeCompletionListItemDelegate(CodeCompletionListModel *model, QWidget *parent) : QStyledItemDelegate(parent),
    mModel(model)
{
    mNormalColor = qApp->palette().color(QPalette::Text);
    mMatchedColor = qApp->palette().color(QPalette::BrightText);
    mLineHeightFactor = 1.0;
}
