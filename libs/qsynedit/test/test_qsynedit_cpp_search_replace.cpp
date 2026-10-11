#include <QTest>
#include <QCoreApplication>
#include <stdexcept>
#include <QScopeGuard>

#include "test_qsynedit_cpp_search_replace.h"
#include "qsynedit/qsynedit.h"
#include "qsynedit/document.h"
#include "qsynedit/syntaxer/cpp.h"
#include "qsynedit/formatter/cppformatter.h"
#include "qsynedit/searcher/basicsearcher.h"
#include "qsynedit/searcher/regexsearcher.h"

namespace QSynedit {


void TestQSyneditCppSearchReplace::initTestCase()
{
    mEdit = std::make_shared<QSynEdit>();
    QSynedit::EditorOptions options = QSynedit::EditorOption::AltSetsColumnMode
            | QSynedit::EditorOption::DragDropEditing | QSynedit::EditorOption::DropFiles
            | QSynedit::EditorOption::EnhanceHomeKey | QSynedit::EditorOption::EnhanceEndKey
            | QSynedit::EditorOption::RightMouseMovesCursor
            | QSynedit::EditorOption::TabIndent
            | QSynedit::EditorOption::GroupUndo
            | QSynedit::EditorOption::AutoIndent
            | QSynedit::EditorOption::SelectWordByDblClick;
    mEdit->setOptions(options);
    mEdit->setSyntaxer(std::make_shared<CppSyntaxer>());
    mEdit->setFormatter(std::make_shared<CppFormatter>());

    mBasicSearcher = std::make_shared<BasicSearcher>();
    mRegexSearcher = std::make_shared<RegexSearcher>();
    mBasicMatchedAndExitProc = [this](const QString& sFound,
            const QString& sReplace, const CharPos& pos, int wordLen){
        mFoundStrings.append(sFound);
        mReplaces.append(sReplace);
        mFoundPositions.append(pos);
        mFoundLens.append(wordLen);
        return SearchAction::Exit;
    };
    mBasicMatchedAndContinueProc = [this](const QString& sFound,
            const QString& sReplace, const CharPos& pos, int wordLen){
        mFoundStrings.append(sFound);
        mReplaces.append(sReplace);
        mFoundPositions.append(pos);
        mFoundLens.append(wordLen);
        return SearchAction::Skip;
    };
    mReplaceAndExitProc = [this](const QString& sFound,
            const QString& sReplace, const CharPos& pos, int wordLen){
        mFoundStrings.append(sFound);
        mReplaces.append(sReplace);
        mFoundPositions.append(pos);
        mFoundLens.append(wordLen);
        return SearchAction::ReplaceAndExit;
    };
    mReplaceAndContinueProc = [this](const QString& sFound,
            const QString& sReplace, const CharPos& pos, int wordLen){
        mFoundStrings.append(sFound);
        mReplaces.append(sReplace);
        mFoundPositions.append(pos);
        mFoundLens.append(wordLen);
        return SearchAction::Replace;
    };

}

void TestQSyneditCppSearchReplace::clearFounds()
{
    mFoundStrings.clear();
    mReplaces.clear();
    mFoundPositions.clear();
    mFoundLens.clear();
}

void TestQSyneditCppSearchReplace::test_basic_search_forward_in_empty_doc()
{
    SearchOptions options = SearchOption::ssoFromCaret;
    mEdit->clear();
    clearFounds();

    CharPos newEndPos;
    QCOMPARE(mEdit->searchReplace(
                 "thread","",
                 mEdit->fileBegin(),
                 mEdit->fileEnd(),
                 newEndPos,
                         options,
                         mBasicSearcher.get(),
                         nullptr,
                         nullptr),0);
    QCOMPARE(mEdit->caretXY(), CharPos(0,0));
    QCOMPARE(mEdit->selBegin(), CharPos(0,0));
    QCOMPARE(mEdit->selEnd(), CharPos(0,0));
    QCOMPARE(newEndPos,CharPos(0,0));

    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),0);
    QCOMPARE(mFoundStrings,{});
    QCOMPARE(mReplaces,{});
    QCOMPARE(mFoundPositions,{});
    QCOMPARE(mFoundLens,{});
    QCOMPARE(mEdit->caretXY(), CharPos(0,0));
    QCOMPARE(mEdit->selBegin(), CharPos(0,0));
    QCOMPARE(mEdit->selEnd(), CharPos(0,0));
    QCOMPARE(newEndPos,CharPos(0,0));

}

void TestQSyneditCppSearchReplace::test_basic_search_forward_from_caret_without_wrap()
{
    QByteArray encoding;
    mEdit->loadFromFile("resources/queue1.cpp",ENCODING_AUTO_DETECT,encoding);

    SearchOptions options = SearchOption::ssoFromCaret;

    CharPos newEndPos;
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         nullptr,
                         nullptr),1);
    QCOMPARE(mEdit->caretXY(), CharPos(16,3));
    QCOMPARE(mEdit->selBegin(), CharPos(10,3));
    QCOMPARE(mEdit->selEnd(), CharPos(16,3));
    QCOMPARE(newEndPos,CharPos(1,76));

    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         nullptr,
                         nullptr),1);
    QCOMPARE(mEdit->caretXY(), CharPos(12,12));
    QCOMPARE(mEdit->selBegin(), CharPos(6,12));
    QCOMPARE(mEdit->selEnd(), CharPos(12,12));
    QCOMPARE(newEndPos,CharPos(1,76));

    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         nullptr,
                         nullptr),1);
    QCOMPARE(mEdit->caretXY(), CharPos(10,15));
    QCOMPARE(mEdit->selBegin(), CharPos(4,15));
    QCOMPARE(mEdit->selEnd(), CharPos(10,15));
    QCOMPARE(newEndPos,CharPos(1,76));

    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         nullptr,
                         nullptr),1);
    QCOMPARE(mEdit->caretXY(), CharPos(18,18));
    QCOMPARE(mEdit->selBegin(), CharPos(12,18));
    QCOMPARE(mEdit->selEnd(), CharPos(18,18));
    QCOMPARE(newEndPos,CharPos(1,76));

    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         nullptr,
                         nullptr),1);
    QCOMPARE(mEdit->caretXY(), CharPos(11,48));
    QCOMPARE(mEdit->selBegin(), CharPos(5,48));
    QCOMPARE(mEdit->selEnd(), CharPos(11,48));
    QCOMPARE(newEndPos,CharPos(1,76));

    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         nullptr,
                         nullptr),1);
    QCOMPARE(mEdit->caretXY(), CharPos(27,52));
    QCOMPARE(mEdit->selBegin(), CharPos(21,52));
    QCOMPARE(mEdit->selEnd(), CharPos(27,52));
    QCOMPARE(newEndPos,CharPos(1,76));

    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         nullptr,
                         nullptr),1);
    QCOMPARE(mEdit->caretXY(), CharPos(27,62));
    QCOMPARE(mEdit->selBegin(), CharPos(21,62));
    QCOMPARE(mEdit->selEnd(), CharPos(27,62));
    QCOMPARE(newEndPos,CharPos(1,76));

    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         nullptr,
                         nullptr),1);
    QCOMPARE(mEdit->caretXY(), CharPos(35,62));
    QCOMPARE(mEdit->selBegin(), CharPos(29,62));
    QCOMPARE(mEdit->selEnd(), CharPos(35,62));
    QCOMPARE(newEndPos,CharPos(1,76));

    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         nullptr,
                         nullptr),1);
    QCOMPARE(mEdit->caretXY(), CharPos(10,67));
    QCOMPARE(mEdit->selBegin(), CharPos(4,67));
    QCOMPARE(mEdit->selEnd(), CharPos(10,67));
    QCOMPARE(newEndPos,CharPos(1,76));

    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         nullptr,
                         nullptr),1);
    QCOMPARE(mEdit->caretXY(), CharPos(28,72));
    QCOMPARE(mEdit->selBegin(), CharPos(22,72));
    QCOMPARE(mEdit->selEnd(), CharPos(28,72));
    QCOMPARE(newEndPos,CharPos(1,76));

    //not found
     QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                   newEndPos,
                         options,
                         mBasicSearcher.get(),
                         nullptr,
                         nullptr),0);
    QCOMPARE(mEdit->caretXY(), CharPos(28,72));
    QCOMPARE(mEdit->selBegin(), CharPos(22,72));
    QCOMPARE(mEdit->selEnd(), CharPos(28,72));
    QCOMPARE(newEndPos,CharPos(1,76));
}

void TestQSyneditCppSearchReplace::test_basic_search_forward_from_caret_without_wrap_with_callback()
{
    QByteArray encoding;
    mEdit->loadFromFile("resources/queue1.cpp",ENCODING_AUTO_DETECT,encoding);

    SearchOptions options = SearchOption::ssoFromCaret;
    CharPos newEndPos;
    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(10,3)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(16,3));
    QCOMPARE(mEdit->selBegin(), CharPos(10,3));
    QCOMPARE(mEdit->selEnd(), CharPos(16,3));

    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"Thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(6,12)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(12,12));
    QCOMPARE(mEdit->selBegin(), CharPos(6,12));
    QCOMPARE(mEdit->selEnd(), CharPos(12,12));
    QCOMPARE(newEndPos,CharPos(1,76));

    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"Thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(4,15)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(10,15));
    QCOMPARE(mEdit->selBegin(), CharPos(4,15));
    QCOMPARE(mEdit->selEnd(), CharPos(10,15));
    QCOMPARE(newEndPos,CharPos(1,76));

    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(12,18)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(18,18));
    QCOMPARE(mEdit->selBegin(), CharPos(12,18));
    QCOMPARE(mEdit->selEnd(), CharPos(18,18));
    QCOMPARE(newEndPos,CharPos(1,76));

    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"Thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(5,48)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(11,48));
    QCOMPARE(mEdit->selBegin(), CharPos(5,48));
    QCOMPARE(mEdit->selEnd(), CharPos(11,48));
    QCOMPARE(newEndPos,CharPos(1,76));

    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(21,52)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(27,52));
    QCOMPARE(mEdit->selBegin(), CharPos(21,52));
    QCOMPARE(mEdit->selEnd(), CharPos(27,52));
    QCOMPARE(newEndPos,CharPos(1,76));

    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(21,62)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(27,62));
    QCOMPARE(mEdit->selBegin(), CharPos(21,62));
    QCOMPARE(mEdit->selEnd(), CharPos(27,62));
    QCOMPARE(newEndPos,CharPos(1,76));

    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(29,62)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(35,62));
    QCOMPARE(mEdit->selBegin(), CharPos(29,62));
    QCOMPARE(mEdit->selEnd(), CharPos(35,62));
    QCOMPARE(newEndPos,CharPos(1,76));

    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"Thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(4,67)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(10,67));
    QCOMPARE(mEdit->selBegin(), CharPos(4,67));
    QCOMPARE(mEdit->selEnd(), CharPos(10,67));
    QCOMPARE(newEndPos,CharPos(1,76));

    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(22,72)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(28,72));
    QCOMPARE(mEdit->selBegin(), CharPos(22,72));
    QCOMPARE(mEdit->selEnd(), CharPos(28,72));
    QCOMPARE(newEndPos,CharPos(1,76));

    //not found
    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),0);
    QCOMPARE(mFoundStrings,{});
    QCOMPARE(mReplaces,{});
    QCOMPARE(mFoundPositions,{});
    QCOMPARE(mFoundLens,{});
    QCOMPARE(mEdit->caretXY(), CharPos(28,72));
    QCOMPARE(mEdit->selBegin(), CharPos(22,72));
    QCOMPARE(mEdit->selEnd(), CharPos(28,72));
    QCOMPARE(newEndPos,CharPos(1,76));
}

void TestQSyneditCppSearchReplace::test_basic_search_forward_from_caret_with_selection()
{
    //when caret is at selection begin/end, forward search will start from selection end
    QByteArray encoding;
    mEdit->loadFromFile("resources/queue1.cpp",ENCODING_AUTO_DETECT,encoding);

    SearchOptions options = SearchOption::ssoFromCaret;
    CharPos newEndPos;
    //no selection, caret at the second "thread" begin
    //should found the second "thread"
    clearFounds();
    mEdit->setCaretXY(CharPos{6,12});
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"Thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(6,12)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(12,12));
    QCOMPARE(mEdit->selBegin(), CharPos(6,12));
    QCOMPARE(mEdit->selEnd(), CharPos(12,12));
    QCOMPARE(newEndPos,CharPos(1,76));

    //no selection, caret at the second "thread" end
    //should found the third "thread"
    clearFounds();
    mEdit->setCaretXY(CharPos{12,12});
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"Thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(4,15)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(10,15));
    QCOMPARE(mEdit->selBegin(), CharPos(4,15));
    QCOMPARE(mEdit->selEnd(), CharPos(10,15));
    QCOMPARE(newEndPos,CharPos(1,76));

    //the second "thread" selected, caret at selection begin
    //should found the third "thread"
    clearFounds();
    mEdit->setCaretAndSelection(CharPos{6,12},CharPos{6,12},CharPos{12,12});
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"Thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(4,15)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(10,15));
    QCOMPARE(mEdit->selBegin(), CharPos(4,15));
    QCOMPARE(mEdit->selEnd(), CharPos(10,15));
    QCOMPARE(newEndPos,CharPos(1,76));

    //the second "thread" selected, caret at selection end
    //should found the third "thread"
    clearFounds();
    mEdit->setCaretAndSelection(CharPos{12,12},CharPos{6,12},CharPos{12,12});
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"Thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(4,15)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(10,15));
    QCOMPARE(mEdit->selBegin(), CharPos(4,15));
    QCOMPARE(mEdit->selEnd(), CharPos(10,15));
    QCOMPARE(newEndPos,CharPos(1,76));

    // the third "thread" selected, caret before the second "thread"
    // should found the second "thread"
    clearFounds();
    mEdit->setCaretAndSelection(CharPos{4,12},CharPos{4,15},CharPos{4,15});
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"Thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(6,12)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(12,12));
    QCOMPARE(mEdit->selBegin(), CharPos(6,12));
    QCOMPARE(mEdit->selEnd(), CharPos(12,12));
    QCOMPARE(newEndPos,CharPos(1,76));

    // the third "thread" selected, caret after the fifth "thread"
    // should found the sixth "thread"
    clearFounds();
    mEdit->setCaretAndSelection(CharPos{16,19},CharPos{4,15},CharPos{4,15});
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"Thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(5,48)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(11,48));
    QCOMPARE(mEdit->selBegin(), CharPos(5,48));
    QCOMPARE(mEdit->selEnd(), CharPos(11,48));
    QCOMPARE(newEndPos,CharPos(1,76));
}

void TestQSyneditCppSearchReplace::test_basic_search_forward_from_caret_include_current_selection()
{
    //when caret is at selection begin/end, forward search will start from selection end
    QByteArray encoding;
    mEdit->loadFromFile("resources/queue1.cpp",ENCODING_AUTO_DETECT,encoding);

    SearchOptions options = SearchOption::ssoFromCaret | SearchOption::ssoIncludeCurrentSelection;
    CharPos newEndPos;

    //no selection, caret at the second "thread" begin
    //should found the second "thread"
    clearFounds();
    mEdit->setCaretXY(CharPos{6,12});
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"Thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(6,12)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(12,12));
    QCOMPARE(mEdit->selBegin(), CharPos(6,12));
    QCOMPARE(mEdit->selEnd(), CharPos(12,12));
    QCOMPARE(newEndPos,CharPos(1,76));

    //no selection, caret at the second "thread" end
    //should found the third "thread"
    clearFounds();
    mEdit->setCaretXY(CharPos{12,12});
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"Thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(4,15)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(10,15));
    QCOMPARE(mEdit->selBegin(), CharPos(4,15));
    QCOMPARE(mEdit->selEnd(), CharPos(10,15));
    QCOMPARE(newEndPos,CharPos(1,76));

    //the second "thread" selected, caret at selection begin
    //should found the second "thread"
    clearFounds();
    mEdit->setCaretAndSelection(CharPos{6,12},CharPos{6,12},CharPos{12,12});
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"Thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(6,12)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(12,12));
    QCOMPARE(mEdit->selBegin(), CharPos(6,12));
    QCOMPARE(mEdit->selEnd(), CharPos(12,12));
    QCOMPARE(newEndPos,CharPos(1,76));

    //the second "thread" selected, caret at selection end
    //should found the second "thread"
    clearFounds();
    mEdit->setCaretAndSelection(CharPos{12,12},CharPos{6,12},CharPos{12,12});
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"Thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(6,12)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(12,12));
    QCOMPARE(mEdit->selBegin(), CharPos(6,12));
    QCOMPARE(mEdit->selEnd(), CharPos(12,12));
    QCOMPARE(newEndPos,CharPos(1,76));

    // the third "thread" selected, caret before the second "thread"
    // should found the second "thread"
    clearFounds();
    mEdit->setCaretAndSelection(CharPos{4,12},CharPos{4,15},CharPos{4,15});
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"Thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(6,12)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(12,12));
    QCOMPARE(mEdit->selBegin(), CharPos(6,12));
    QCOMPARE(mEdit->selEnd(), CharPos(12,12));
    QCOMPARE(newEndPos,CharPos(1,76));

    // the third "thread" selected, caret after the fifth "thread"
    // should found the sixth "thread"
    clearFounds();
    mEdit->setCaretAndSelection(CharPos{16,19},CharPos{4,15},CharPos{4,15});
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"Thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(5,48)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(11,48));
    QCOMPARE(mEdit->selBegin(), CharPos(5,48));
    QCOMPARE(mEdit->selEnd(), CharPos(11,48));
    QCOMPARE(newEndPos,CharPos(1,76));
}

void TestQSyneditCppSearchReplace::test_basic_search_forward()
{
    //when caret is at selection begin/end, forward search will start from selection end
    QByteArray encoding;
    mEdit->loadFromFile("resources/queue1.cpp",ENCODING_AUTO_DETECT,encoding);

    SearchOptions options = SearchOption::ssoNone;
    CharPos newEndPos;
    //no selection, caret at the second "thread" begin
    //should found the first "thread"
    clearFounds();
    mEdit->setCaretXY(CharPos{6,12});
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(10,3)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(16,3));
    QCOMPARE(mEdit->selBegin(), CharPos(10,3));
    QCOMPARE(mEdit->selEnd(), CharPos(16,3));
    QCOMPARE(newEndPos,CharPos(1,76));

    //no selection, caret at the second "thread" end
    //should found the first "thread"
    clearFounds();
    mEdit->setCaretXY(CharPos{12,12});
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(10,3)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(16,3));
    QCOMPARE(mEdit->selBegin(), CharPos(10,3));
    QCOMPARE(mEdit->selEnd(), CharPos(16,3));
    QCOMPARE(newEndPos,CharPos(1,76));

    //the second "thread" selected, caret at selection begin
    //should found the first "thread"
    clearFounds();
    mEdit->setCaretAndSelection(CharPos{6,12},CharPos{6,12},CharPos{12,12});
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(10,3)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(16,3));
    QCOMPARE(mEdit->selBegin(), CharPos(10,3));
    QCOMPARE(mEdit->selEnd(), CharPos(16,3));
    QCOMPARE(newEndPos,CharPos(1,76));

    //the second "thread" selected, caret at selection end
    //should found the first "thread"
    clearFounds();
    mEdit->setCaretAndSelection(CharPos{12,12},CharPos{6,12},CharPos{12,12});
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(10,3)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(16,3));
    QCOMPARE(mEdit->selBegin(), CharPos(10,3));
    QCOMPARE(mEdit->selEnd(), CharPos(16,3));
    QCOMPARE(newEndPos,CharPos(1,76));

    // the third "thread" selected, caret before the second "thread"
    //should found the first "thread"
    clearFounds();
    mEdit->setCaretAndSelection(CharPos{4,12},CharPos{4,15},CharPos{4,15});
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(10,3)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(16,3));
    QCOMPARE(mEdit->selBegin(), CharPos(10,3));
    QCOMPARE(mEdit->selEnd(), CharPos(16,3));
    QCOMPARE(newEndPos,CharPos(1,76));

    // the third "thread" selected, caret after the fifth "thread"
    //should found the first "thread"
    clearFounds();
    mEdit->setCaretAndSelection(CharPos{16,19},CharPos{4,15},CharPos{4,15});
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(10,3)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(16,3));
    QCOMPARE(mEdit->selBegin(), CharPos(10,3));
    QCOMPARE(mEdit->selEnd(), CharPos(16,3));
    QCOMPARE(newEndPos,CharPos(1,76));
}

void TestQSyneditCppSearchReplace::test_basic_search_forward_from_caret_with_wrap()
{
    //when caret is at selection begin/end, forward search will start from selection end
    QByteArray encoding;
    mEdit->loadFromFile("resources/queue1.cpp",ENCODING_AUTO_DETECT,encoding);

    SearchOptions options = SearchOption::ssoFromCaret | SearchOption::ssoWrapAround;
    CharPos newEndPos;

    //caret before last "thread"
    mEdit->setCaretXY({10,67});

    //found the last "thread"
    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(22,72)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(28,72));
    QCOMPARE(mEdit->selBegin(), CharPos(22,72));
    QCOMPARE(mEdit->selEnd(), CharPos(28,72));
    QCOMPARE(newEndPos,CharPos(1,76));

    //found the first "thread"
    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(10,3)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(16,3));
    QCOMPARE(mEdit->selBegin(), CharPos(10,3));
    QCOMPARE(mEdit->selEnd(), CharPos(16,3));
    QCOMPARE(newEndPos,CharPos(1,76));

    //caret at file end, and don't confirm wrap
    mEdit->setCaretXY(mEdit->fileEnd());
    clearFounds();
    QCOMPARE(mEdit->searchReplace(
                 "thread","",
                 mEdit->fileBegin(),
                 mEdit->fileEnd(),
                 newEndPos,
                 options,
                 mBasicSearcher.get(),
                 mBasicMatchedAndExitProc,
                 [](){ return false; }),
             0);
    QCOMPARE(mFoundStrings,{});
    QCOMPARE(mReplaces,{});
    QCOMPARE(mFoundPositions,{});
    QCOMPARE(mFoundLens,{});
    QCOMPARE(mEdit->caretXY(), mEdit->fileEnd());
    QCOMPARE(mEdit->selBegin(), mEdit->fileEnd());
    QCOMPARE(mEdit->selEnd(), mEdit->fileEnd());
    QCOMPARE(newEndPos,CharPos(1,76));

    //caret at file end, and confirm wrap
    mEdit->setCaretXY(mEdit->fileEnd());
    clearFounds();
    QCOMPARE(mEdit->searchReplace(
                 "thread","",
                 mEdit->fileBegin(),
                 mEdit->fileEnd(),
                 newEndPos,
                 options,
                 mBasicSearcher.get(),
                 mBasicMatchedAndExitProc,
                 [](){ return true; }),
             1);
    QCOMPARE(mFoundStrings,{"thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(10,3)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(16,3));
    QCOMPARE(mEdit->selBegin(), CharPos(10,3));
    QCOMPARE(mEdit->selEnd(), CharPos(16,3));
    QCOMPARE(newEndPos,CharPos(1,76));
}

void TestQSyneditCppSearchReplace::test_search_forward_from_caret_whole_word()
{
    QByteArray encoding;
    mEdit->loadFromFile("resources/queue1.cpp",ENCODING_AUTO_DETECT,encoding);

    SearchOptions options = SearchOption::ssoFromCaret | SearchOption::ssoWholeWord;
    CharPos newEndPos;

    clearFounds();
    QCOMPARE(mEdit->searchReplace("Thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(10,3)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(16,3));
    QCOMPARE(mEdit->selBegin(), CharPos(10,3));
    QCOMPARE(mEdit->selEnd(), CharPos(16,3));
    QCOMPARE(newEndPos,CharPos(1,76));

    clearFounds();
    QCOMPARE(mEdit->searchReplace("Thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(21,62)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(27,62));
    QCOMPARE(mEdit->selBegin(), CharPos(21,62));
    QCOMPARE(mEdit->selEnd(), CharPos(27,62));
    QCOMPARE(newEndPos,CharPos(1,76));

    //not found
    clearFounds();
    QCOMPARE(mEdit->searchReplace("Thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),0);
    QCOMPARE(mFoundStrings,{});
    QCOMPARE(mReplaces,{});
    QCOMPARE(mFoundPositions,{});
    QCOMPARE(mFoundLens,{});
    QCOMPARE(mEdit->caretXY(), CharPos(27,62));
    QCOMPARE(mEdit->selBegin(), CharPos(21,62));
    QCOMPARE(mEdit->selEnd(), CharPos(27,62));
    QCOMPARE(newEndPos,CharPos(1,76));
}

void TestQSyneditCppSearchReplace::test_search_forward_whole_word2()
{
    QStringList text = {
        "uint uint,uint,uint uint",
        "int int,int,int int",
        "uintx uintx,uintx,uintx uintx",
        "uintx uintx,int,uintx uintx",
        "intx intx,intx,intx intx",
    };

    SearchOptions options = SearchOption::ssoWholeWord;
    CharPos newEndPos;

    mEdit->setContent(text);
    clearFounds();
    QCOMPARE(mEdit->searchReplace("int","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndContinueProc,
                         nullptr),6);
    QCOMPARE(newEndPos,CharPos(24,4));

    QCOMPARE(mFoundStrings,
             QStringList(
                 {
                     "int",
                     "int",
                     "int",
                     "int",
                     "int",
                     "int",
                 }));
    QCOMPARE(mReplaces,
             QStringList(
                 {
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                 }));
    QCOMPARE(mFoundPositions,QList<CharPos>(
                 {
                     CharPos(0,1),
                     CharPos(4,1),
                     CharPos(8,1),
                     CharPos(12,1),
                     CharPos(16,1),
                     CharPos(12,3),
             }));
    QCOMPARE(mFoundLens,QList<int>(
                 {
                     3,
                     3,
                     3,
                     3,
                     3,
                     3,
                 }));
}

void TestQSyneditCppSearchReplace::test_search_forward_from_caret_match_case()
{
    QByteArray encoding;
    mEdit->loadFromFile("resources/queue1.cpp",ENCODING_AUTO_DETECT,encoding);

    SearchOptions options = SearchOption::ssoFromCaret | SearchOption::ssoMatchCase ;
    CharPos newEndPos;

    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(10,3)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(16,3));
    QCOMPARE(mEdit->selBegin(), CharPos(10,3));
    QCOMPARE(mEdit->selEnd(), CharPos(16,3));
    QCOMPARE(newEndPos,CharPos(1,76));

    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(12,18)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(18,18));
    QCOMPARE(mEdit->selBegin(), CharPos(12,18));
    QCOMPARE(mEdit->selEnd(), CharPos(18,18));
    QCOMPARE(newEndPos,CharPos(1,76));

    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(21,52)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(27,52));
    QCOMPARE(mEdit->selBegin(), CharPos(21,52));
    QCOMPARE(mEdit->selEnd(), CharPos(27,52));
    QCOMPARE(newEndPos,CharPos(1,76));

    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(21,62)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(27,62));
    QCOMPARE(mEdit->selBegin(), CharPos(21,62));
    QCOMPARE(mEdit->selEnd(), CharPos(27,62));
    QCOMPARE(newEndPos,CharPos(1,76));

    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(29,62)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(35,62));
    QCOMPARE(mEdit->selBegin(), CharPos(29,62));
    QCOMPARE(mEdit->selEnd(), CharPos(35,62));
    QCOMPARE(newEndPos,CharPos(1,76));

    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(22,72)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(28,72));
    QCOMPARE(mEdit->selBegin(), CharPos(22,72));
    QCOMPARE(mEdit->selEnd(), CharPos(28,72));
    QCOMPARE(newEndPos,CharPos(1,76));

    //not found
    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),0);
    QCOMPARE(mFoundStrings,{});
    QCOMPARE(mReplaces,{});
    QCOMPARE(mFoundPositions,{});
    QCOMPARE(mFoundLens,{});
    QCOMPARE(mEdit->caretXY(), CharPos(28,72));
    QCOMPARE(mEdit->selBegin(), CharPos(22,72));
    QCOMPARE(mEdit->selEnd(), CharPos(28,72));
    QCOMPARE(newEndPos,CharPos(1,76));
}

void TestQSyneditCppSearchReplace::test_search_all_forward_in_whole_file_from_start()
{
    QByteArray encoding;
    mEdit->loadFromFile("resources/queue1.cpp",ENCODING_AUTO_DETECT,encoding);

    SearchOptions options = SearchOption::ssoNone;
    CharPos newEndPos;

    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndContinueProc,
                         nullptr),10);
    QCOMPARE(mFoundStrings,
             QStringList(
                 {
                     "thread",
                     "Thread",
                     "Thread",
                     "thread",
                     "Thread",
                     "thread",
                     "thread",
                     "thread",
                     "Thread",
                     "thread",
                 }));
    QCOMPARE(mReplaces,
             QStringList(
                 {
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                 }));
    QCOMPARE(mFoundPositions,QList<CharPos>(
                 {
                     CharPos(10,3),
                     CharPos(6,12),
                     CharPos(4,15),
                     CharPos(12,18),
                     CharPos(5,48),
                     CharPos(21,52),
                     CharPos(21,62),
                     CharPos(29,62),
                     CharPos(4,67),
                     CharPos(22,72)

             }));
    QCOMPARE(mFoundLens,QList<int>(
                 {
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                 }));
    QCOMPARE(newEndPos,CharPos(1,76));
}

void TestQSyneditCppSearchReplace::test_search_all_forward_in_whole_file_from_caret_and_wrap()
{
    QByteArray encoding;
    mEdit->loadFromFile("resources/queue1.cpp",ENCODING_AUTO_DETECT,encoding);

    SearchOptions options = SearchOption::ssoFromCaret | SearchOption::ssoWrapAround;
    CharPos newEndPos;
    //caret at begin of the 3rd "thread"
    mEdit->setCaretXY({4,15});
    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndContinueProc,
                         nullptr),10);
    QCOMPARE(mFoundStrings,
             QStringList(
                 {
                     "Thread",
                     "thread",
                     "Thread",
                     "thread",
                     "thread",
                     "thread",
                     "Thread",
                     "thread",
                     "thread",
                     "Thread",
                 }));
    QCOMPARE(mReplaces,
             QStringList(
                 {
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                 }));
    QCOMPARE(mFoundPositions,QList<CharPos>(
                 {
                     CharPos(4,15),
                     CharPos(12,18),
                     CharPos(5,48),
                     CharPos(21,52),
                     CharPos(21,62),
                     CharPos(29,62),
                     CharPos(4,67),
                     CharPos(22,72),
                     CharPos(10,3),
                     CharPos(6,12),
             }));
    QCOMPARE(mFoundLens,QList<int>(
                 {
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                 }));
    QCOMPARE(newEndPos,CharPos(1,76));

    //caret at end of the third "thread"
    mEdit->setCaretXY({10,15});
    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndContinueProc,
                         nullptr),10);
    QCOMPARE(mFoundStrings,
             QStringList(
                 {
                     "thread",
                     "Thread",
                     "thread",
                     "thread",
                     "thread",
                     "Thread",
                     "thread",
                     "thread",
                     "Thread",
                     "Thread",
                 }));
    QCOMPARE(mReplaces,
             QStringList(
                 {
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                 }));
    QCOMPARE(mFoundPositions,QList<CharPos>(
                 {
                     CharPos(12,18),
                     CharPos(5,48),
                     CharPos(21,52),
                     CharPos(21,62),
                     CharPos(29,62),
                     CharPos(4,67),
                     CharPos(22,72),
                     CharPos(10,3),
                     CharPos(6,12),
                     CharPos(4,15),
             }));
    QCOMPARE(mFoundLens,QList<int>(
                 {
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                 }));
    QCOMPARE(newEndPos,CharPos(1,76));

    //caret in the middle of the third "thread"
    mEdit->setCaretXY({5,15});
    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndContinueProc,
                         nullptr),9);
    QCOMPARE(mFoundStrings,
             QStringList(
                 {
                     "thread",
                     "Thread",
                     "thread",
                     "thread",
                     "thread",
                     "Thread",
                     "thread",
                     "thread",
                     "Thread",
                 }));
    QCOMPARE(mReplaces,
             QStringList(
                 {
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                 }));
    QCOMPARE(mFoundPositions,QList<CharPos>(
                 {
                     CharPos(12,18),
                     CharPos(5,48),
                     CharPos(21,52),
                     CharPos(21,62),
                     CharPos(29,62),
                     CharPos(4,67),
                     CharPos(22,72),
                     CharPos(10,3),
                     CharPos(6,12),
             }));
    QCOMPARE(mFoundLens,QList<int>(
                 {
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                 }));
    QCOMPARE(newEndPos,CharPos(1,76));
}

void TestQSyneditCppSearchReplace::test_search_all_forward_in_whole_file_from_caret_and_no_wrap()
{
    QByteArray encoding;
    mEdit->loadFromFile("resources/queue1.cpp",ENCODING_AUTO_DETECT,encoding);

    SearchOptions options = SearchOption::ssoFromCaret;
    CharPos newEndPos;

    //before the 3rd "thread"
    mEdit->setCaretXY({3,15});
    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndContinueProc,
                         nullptr),8);
    QCOMPARE(mFoundStrings,
             QStringList(
                 {
                     "Thread",
                     "thread",
                     "Thread",
                     "thread",
                     "thread",
                     "thread",
                     "Thread",
                     "thread",
                 }));
    QCOMPARE(mReplaces,
             QStringList(
                 {
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                 }));
    QCOMPARE(mFoundPositions,QList<CharPos>(
                 {
                     CharPos(4,15),
                     CharPos(12,18),
                     CharPos(5,48),
                     CharPos(21,52),
                     CharPos(21,62),
                     CharPos(29,62),
                     CharPos(4,67),
                     CharPos(22,72),
             }));
    QCOMPARE(mFoundLens,QList<int>(
                 {
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                 }));
    QCOMPARE(newEndPos,CharPos(1,76));
}

void TestQSyneditCppSearchReplace::test_search_forward_in_scope()
{
    QByteArray encoding;
    mEdit->loadFromFile("resources/queue1.cpp",ENCODING_AUTO_DETECT,encoding);

    SearchOptions options = SearchOption::ssoNone;
    CharPos newEndPos;
    // Caret position not used in scoped search

    //search from begin of the 2nd "thread" to begin of the 9th "thread"
    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  CharPos(6,12),
                                  CharPos(4,67),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndContinueProc,
                         nullptr),7);
    QCOMPARE(mFoundStrings,
             QStringList(
                 {
                     "Thread",
                     "Thread",
                     "thread",
                     "Thread",
                     "thread",
                     "thread",
                     "thread",
                 }));
    QCOMPARE(mReplaces,
             QStringList(
                 {
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                 }));
    QCOMPARE(mFoundPositions,QList<CharPos>(
                 {
                     CharPos(6,12),
                     CharPos(4,15),
                     CharPos(12,18),
                     CharPos(5,48),
                     CharPos(21,52),
                     CharPos(21,62),
                     CharPos(29,62),
             }));
    QCOMPARE(mFoundLens,QList<int>(
                 {
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                 }));
    QCOMPARE(newEndPos,CharPos(4,67));

    //search from begin of the 2nd "thread" to middle of the 9th "thread"
    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  CharPos(6,12),
                                  CharPos(5,67),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndContinueProc,
                         nullptr),7);
    QCOMPARE(mFoundStrings,
             QStringList(
                 {
                     "Thread",
                     "Thread",
                     "thread",
                     "Thread",
                     "thread",
                     "thread",
                     "thread",
                 }));
    QCOMPARE(mReplaces,
             QStringList(
                 {
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                 }));
    QCOMPARE(mFoundPositions,QList<CharPos>(
                 {
                     CharPos(6,12),
                     CharPos(4,15),
                     CharPos(12,18),
                     CharPos(5,48),
                     CharPos(21,52),
                     CharPos(21,62),
                     CharPos(29,62),
             }));
    QCOMPARE(mFoundLens,QList<int>(
                 {
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                 }));
    QCOMPARE(newEndPos,CharPos(5,67));

    //search from begin of the 2nd "thread" to end of the 9th "thread"
    //caret not in it
    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  CharPos(6,12),
                                  CharPos(10,67),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndContinueProc,
                         nullptr),8);
    QCOMPARE(mFoundStrings,
             QStringList(
                 {
                     "Thread",
                     "Thread",
                     "thread",
                     "Thread",
                     "thread",
                     "thread",
                     "thread",
                     "Thread",
                 }));
    QCOMPARE(mReplaces,
             QStringList(
                 {
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                 }));
    QCOMPARE(mFoundPositions,QList<CharPos>(
                 {
                     CharPos(6,12),
                     CharPos(4,15),
                     CharPos(12,18),
                     CharPos(5,48),
                     CharPos(21,52),
                     CharPos(21,62),
                     CharPos(29,62),
                     CharPos(4,67),
             }));
    QCOMPARE(mFoundLens,QList<int>(
                 {
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                 }));
    QCOMPARE(newEndPos,CharPos(10,67));

    //search from middle of the 2nd "thread" to begin of the 9th "thread"
    //caret not in it
    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  CharPos(7,12),
                                  CharPos(4,67),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndContinueProc,
                         nullptr),6);
    QCOMPARE(mFoundStrings,
             QStringList(
                 {
                     "Thread",
                     "thread",
                     "Thread",
                     "thread",
                     "thread",
                     "thread",
                 }));
    QCOMPARE(mReplaces,
             QStringList(
                 {
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                 }));
    QCOMPARE(mFoundPositions,QList<CharPos>(
                 {
                     CharPos(4,15),
                     CharPos(12,18),
                     CharPos(5,48),
                     CharPos(21,52),
                     CharPos(21,62),
                     CharPos(29,62),
             }));
    QCOMPARE(mFoundLens,QList<int>(
                 {
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                 }));
    QCOMPARE(newEndPos,CharPos(4,67));

    //search from middle of the 2nd "thread" to middle of the 9th "thread"
    //caret not in it
    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  CharPos(7,12),
                                  CharPos(5,67),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndContinueProc,
                         nullptr),6);
    QCOMPARE(mFoundStrings,
             QStringList(
                 {
                     "Thread",
                     "thread",
                     "Thread",
                     "thread",
                     "thread",
                     "thread",
                 }));
    QCOMPARE(mReplaces,
             QStringList(
                 {
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                 }));
    QCOMPARE(mFoundPositions,QList<CharPos>(
                 {
                     CharPos(4,15),
                     CharPos(12,18),
                     CharPos(5,48),
                     CharPos(21,52),
                     CharPos(21,62),
                     CharPos(29,62),
             }));
    QCOMPARE(mFoundLens,QList<int>(
                 {
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                 }));
    QCOMPARE(newEndPos,CharPos(5,67));

    //search from middle of the 2nd "thread" to end of the 9th "thread"
    //caret not in it
    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  CharPos(7,12),
                                  CharPos(10,67),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndContinueProc,
                         nullptr),7);
    QCOMPARE(mFoundStrings,
             QStringList(
                 {
                     "Thread",
                     "thread",
                     "Thread",
                     "thread",
                     "thread",
                     "thread",
                     "Thread",
                 }));
    QCOMPARE(mReplaces,
             QStringList(
                 {
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                 }));
    QCOMPARE(mFoundPositions,QList<CharPos>(
                 {
                     CharPos(4,15),
                     CharPos(12,18),
                     CharPos(5,48),
                     CharPos(21,52),
                     CharPos(21,62),
                     CharPos(29,62),
                     CharPos(4,67),
             }));
    QCOMPARE(mFoundLens,QList<int>(
                 {
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                 }));
    QCOMPARE(newEndPos,CharPos(10,67));

    //search from end of the 2nd "thread" to begin of the 9th "thread"
    //caret not in it
    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  CharPos(12,12),
                                  CharPos(4,67),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndContinueProc,
                         nullptr),6);
    QCOMPARE(mFoundStrings,
             QStringList(
                 {
                     "Thread",
                     "thread",
                     "Thread",
                     "thread",
                     "thread",
                     "thread",
                 }));
    QCOMPARE(mReplaces,
             QStringList(
                 {
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                 }));
    QCOMPARE(mFoundPositions,QList<CharPos>(
                 {
                     CharPos(4,15),
                     CharPos(12,18),
                     CharPos(5,48),
                     CharPos(21,52),
                     CharPos(21,62),
                     CharPos(29,62),
             }));
    QCOMPARE(mFoundLens,QList<int>(
                 {
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                 }));
    QCOMPARE(newEndPos,CharPos(4,67));

    //search from end of the 2nd "thread" to middle of the 9th "thread"
    //caret not in it
    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  CharPos(12,12),
                                  CharPos(5,67),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndContinueProc,
                         nullptr),6);
    QCOMPARE(mFoundStrings,
             QStringList(
                 {
                     "Thread",
                     "thread",
                     "Thread",
                     "thread",
                     "thread",
                     "thread",
                 }));
    QCOMPARE(mReplaces,
             QStringList(
                 {
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                 }));
    QCOMPARE(mFoundPositions,QList<CharPos>(
                 {
                     CharPos(4,15),
                     CharPos(12,18),
                     CharPos(5,48),
                     CharPos(21,52),
                     CharPos(21,62),
                     CharPos(29,62),
             }));
    QCOMPARE(mFoundLens,QList<int>(
                 {
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                 }));
    QCOMPARE(newEndPos,CharPos(5,67));

    //search from end of the 2nd "thread" to end of the 9th "thread"
    //caret not in it
    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  CharPos(12,12),
                                  CharPos(10,67),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndContinueProc,
                         nullptr),7);
    QCOMPARE(mFoundStrings,
             QStringList(
                 {
                     "Thread",
                     "thread",
                     "Thread",
                     "thread",
                     "thread",
                     "thread",
                     "Thread",
                 }));
    QCOMPARE(mReplaces,
             QStringList(
                 {
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                 }));
    QCOMPARE(mFoundPositions,QList<CharPos>(
                 {
                     CharPos(4,15),
                     CharPos(12,18),
                     CharPos(5,48),
                     CharPos(21,52),
                     CharPos(21,62),
                     CharPos(29,62),
                     CharPos(4,67),
             }));
    QCOMPARE(mFoundLens,QList<int>(
                 {
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                 }));
    QCOMPARE(newEndPos,CharPos(10,67));

}

void TestQSyneditCppSearchReplace::test_ensure_caret_pos_not_used_in_scope_search()
{
    QByteArray encoding;
    mEdit->loadFromFile("resources/queue1.cpp",ENCODING_AUTO_DETECT,encoding);

    SearchOptions options = SearchOption::ssoFromCaret;
    CharPos newEndPos;
    // Caret position not used in scoped search

    //search from begin of the 2nd "thread" to begin of the 9th "thread"
    //caret in the middle of the 6th "thread"
    mEdit->setCaretXY({22,52});
    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  CharPos(6,12),
                                  CharPos(4,67),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndContinueProc,
                         nullptr),7);
    QCOMPARE(mFoundStrings,
             QStringList(
                 {
                     "Thread",
                     "Thread",
                     "thread",
                     "Thread",
                     "thread",
                     "thread",
                     "thread",
                 }));
    QCOMPARE(mReplaces,
             QStringList(
                 {
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                 }));
    QCOMPARE(mFoundPositions,QList<CharPos>(
                 {
                     CharPos(6,12),
                     CharPos(4,15),
                     CharPos(12,18),
                     CharPos(5,48),
                     CharPos(21,52),
                     CharPos(21,62),
                     CharPos(29,62),
             }));
    QCOMPARE(mFoundLens,QList<int>(
                 {
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                 }));
    QCOMPARE(newEndPos,CharPos(4,67));
}


void TestQSyneditCppSearchReplace::test_basic_search_backward_in_empty_doc()
{
    mEdit->clear();

    SearchOptions options = SearchOption::ssoFromCaret | SearchOption::ssoBackwards;
    CharPos newEndPos;

    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         nullptr,
                         nullptr),0);
    QCOMPARE(mEdit->caretXY(), CharPos(0,0));
    QCOMPARE(mEdit->selBegin(), CharPos(0,0));
    QCOMPARE(mEdit->selEnd(), CharPos(0,0));
    QCOMPARE(newEndPos,CharPos(0,0));

    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),0);
    QCOMPARE(mFoundStrings,{});
    QCOMPARE(mReplaces,{});
    QCOMPARE(mFoundPositions,{});
    QCOMPARE(mFoundLens,{});
    QCOMPARE(mEdit->caretXY(), CharPos(0,0));
    QCOMPARE(mEdit->selBegin(), CharPos(0,0));
    QCOMPARE(mEdit->selEnd(), CharPos(0,0));
    QCOMPARE(newEndPos,CharPos(0,0));
}

void TestQSyneditCppSearchReplace::test_basic_search_backward_from_caret_without_wrap()
{
    QByteArray encoding;
    mEdit->loadFromFile("resources/queue1.cpp",ENCODING_AUTO_DETECT,encoding);

    SearchOptions options = SearchOption::ssoFromCaret | SearchOption::ssoBackwards;
    CharPos newEndPos;

    //caret at file end
    mEdit->setCaretXY(mEdit->fileEnd());

    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         nullptr,
                         nullptr),1);
    QCOMPARE(mEdit->caretXY(), CharPos(22,72));
    QCOMPARE(mEdit->selBegin(), CharPos(22,72));
    QCOMPARE(mEdit->selEnd(), CharPos(28,72));
    QCOMPARE(newEndPos,CharPos(1,76));

    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         nullptr,
                         nullptr),1);
    QCOMPARE(mEdit->caretXY(), CharPos(4,67));
    QCOMPARE(mEdit->selBegin(), CharPos(4,67));
    QCOMPARE(mEdit->selEnd(), CharPos(10,67));
    QCOMPARE(newEndPos,CharPos(1,76));

    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         nullptr,
                         nullptr),1);
    QCOMPARE(mEdit->caretXY(), CharPos(29,62));
    QCOMPARE(mEdit->selBegin(), CharPos(29,62));
    QCOMPARE(mEdit->selEnd(), CharPos(35,62));
    QCOMPARE(newEndPos,CharPos(1,76));

    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         nullptr,
                         nullptr),1);
    QCOMPARE(mEdit->caretXY(), CharPos(21,62));
    QCOMPARE(mEdit->selBegin(), CharPos(21,62));
    QCOMPARE(mEdit->selEnd(), CharPos(27,62));
    QCOMPARE(newEndPos,CharPos(1,76));

    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         nullptr,
                         nullptr),1);
    QCOMPARE(mEdit->caretXY(), CharPos(21,52));
    QCOMPARE(mEdit->selBegin(), CharPos(21,52));
    QCOMPARE(mEdit->selEnd(), CharPos(27,52));
    QCOMPARE(newEndPos,CharPos(1,76));

    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         nullptr,
                         nullptr),1);
    QCOMPARE(mEdit->caretXY(), CharPos(5,48));
    QCOMPARE(mEdit->selBegin(), CharPos(5,48));
    QCOMPARE(mEdit->selEnd(), CharPos(11,48));
    QCOMPARE(newEndPos,CharPos(1,76));

    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         nullptr,
                         nullptr),1);
    QCOMPARE(mEdit->caretXY(), CharPos(12,18));
    QCOMPARE(mEdit->selBegin(), CharPos(12,18));
    QCOMPARE(mEdit->selEnd(), CharPos(18,18));
    QCOMPARE(newEndPos,CharPos(1,76));

    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         nullptr,
                         nullptr),1);
    QCOMPARE(mEdit->caretXY(), CharPos(4,15));
    QCOMPARE(mEdit->selBegin(), CharPos(4,15));
    QCOMPARE(mEdit->selEnd(), CharPos(10,15));
    QCOMPARE(newEndPos,CharPos(1,76));

    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         nullptr,
                         nullptr),1);
    QCOMPARE(mEdit->caretXY(), CharPos(6,12));
    QCOMPARE(mEdit->selBegin(), CharPos(6,12));
    QCOMPARE(mEdit->selEnd(), CharPos(12,12));
    QCOMPARE(newEndPos,CharPos(1,76));

    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         nullptr,
                         nullptr),1);
    QCOMPARE(mEdit->caretXY(), CharPos(10,3));
    QCOMPARE(mEdit->selBegin(), CharPos(10,3));
    QCOMPARE(mEdit->selEnd(), CharPos(16,3));
    QCOMPARE(newEndPos,CharPos(1,76));

    //not found
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         nullptr,
                         nullptr),0);
    QCOMPARE(mEdit->caretXY(), CharPos(10,3));
    QCOMPARE(mEdit->selBegin(), CharPos(10,3));
    QCOMPARE(mEdit->selEnd(), CharPos(16,3));
    QCOMPARE(newEndPos,CharPos(1,76));
}

void TestQSyneditCppSearchReplace::test_basic_search_backward_from_caret_without_wrap_with_callback()
{
    QByteArray encoding;
    mEdit->loadFromFile("resources/queue1.cpp",ENCODING_AUTO_DETECT,encoding);

    SearchOptions options = SearchOption::ssoFromCaret | SearchOption::ssoBackwards;
    CharPos newEndPos;

    //caret at file end
    mEdit->setCaretXY(mEdit->fileEnd());

    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(22,72)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(22,72));
    QCOMPARE(mEdit->selBegin(), CharPos(22,72));
    QCOMPARE(mEdit->selEnd(), CharPos(28,72));
    QCOMPARE(newEndPos,CharPos(1,76));

    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"Thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(4,67)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(4,67));
    QCOMPARE(mEdit->selBegin(), CharPos(4,67));
    QCOMPARE(mEdit->selEnd(), CharPos(10,67));
    QCOMPARE(newEndPos,CharPos(1,76));

    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(29,62)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(29,62));
    QCOMPARE(mEdit->selBegin(), CharPos(29,62));
    QCOMPARE(mEdit->selEnd(), CharPos(35,62));
    QCOMPARE(newEndPos,CharPos(1,76));

    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(21,62)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(21,62));
    QCOMPARE(mEdit->selBegin(), CharPos(21,62));
    QCOMPARE(mEdit->selEnd(), CharPos(27,62));
    QCOMPARE(newEndPos,CharPos(1,76));

    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(21,52)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(21,52));
    QCOMPARE(mEdit->selBegin(), CharPos(21,52));
    QCOMPARE(mEdit->selEnd(), CharPos(27,52));
    QCOMPARE(newEndPos,CharPos(1,76));

    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"Thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(5,48)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(5,48));
    QCOMPARE(mEdit->selBegin(), CharPos(5,48));
    QCOMPARE(mEdit->selEnd(), CharPos(11,48));
    QCOMPARE(newEndPos,CharPos(1,76));

    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(12,18)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(12,18));
    QCOMPARE(mEdit->selBegin(), CharPos(12,18));
    QCOMPARE(mEdit->selEnd(), CharPos(18,18));
    QCOMPARE(newEndPos,CharPos(1,76));

    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"Thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(4,15)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(4,15));
    QCOMPARE(mEdit->selBegin(), CharPos(4,15));
    QCOMPARE(mEdit->selEnd(), CharPos(10,15));
    QCOMPARE(newEndPos,CharPos(1,76));

    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"Thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(6,12)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(6,12));
    QCOMPARE(mEdit->selBegin(), CharPos(6,12));
    QCOMPARE(mEdit->selEnd(), CharPos(12,12));
    QCOMPARE(newEndPos,CharPos(1,76));

    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(10,3)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(10,3));
    QCOMPARE(mEdit->selBegin(), CharPos(10,3));
    QCOMPARE(mEdit->selEnd(), CharPos(16,3));
    QCOMPARE(newEndPos,CharPos(1,76));
}

void TestQSyneditCppSearchReplace::test_basic_search_backward_from_caret_with_selection()
{
    //when caret is at selection begin/end, backward search will start from selection begin
    QByteArray encoding;
    mEdit->loadFromFile("resources/queue1.cpp",ENCODING_AUTO_DETECT,encoding);

    SearchOptions options = SearchOption::ssoFromCaret | SearchOption::ssoBackwards;
    CharPos newEndPos;

    //no selection, caret at the 9th "thread" end
    //should found the 9th "thread"
    clearFounds();
    mEdit->setCaretXY(CharPos(10,67));
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"Thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(4,67)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(4,67));
    QCOMPARE(mEdit->selBegin(), CharPos(4,67));
    QCOMPARE(mEdit->selEnd(), CharPos(10,67));
    QCOMPARE(newEndPos,CharPos(1,76));

    //no selection, caret at the 9th "thread" begin
    //should found the 8th "thread"
    clearFounds();
    mEdit->setCaretXY(CharPos(4,67));
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(29,62)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(29,62));
    QCOMPARE(mEdit->selBegin(), CharPos(29,62));
    QCOMPARE(mEdit->selEnd(), CharPos(35,62));
    QCOMPARE(newEndPos,CharPos(1,76));

    //the 9th "thread" selected, caret at selection begin
    //should found the 8th "thread"
    clearFounds();
    mEdit->setCaretAndSelection(CharPos(4,67),CharPos(4,67),CharPos(10,67));
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(29,62)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(29,62));
    QCOMPARE(mEdit->selBegin(), CharPos(29,62));
    QCOMPARE(mEdit->selEnd(), CharPos(35,62));
    QCOMPARE(newEndPos,CharPos(1,76));

    //the 9th "thread" selected, caret at selection end
    //should found the 8th "thread"
    clearFounds();
    mEdit->setCaretAndSelection(CharPos(10,67),CharPos(4,67),CharPos(10,67));
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(29,62)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(29,62));
    QCOMPARE(mEdit->selBegin(), CharPos(29,62));
    QCOMPARE(mEdit->selEnd(), CharPos(35,62));
    QCOMPARE(newEndPos,CharPos(1,76));

    //the 8th "thread" selected, caret after the 9th "thread"
    //should found the 9th "thread"
    clearFounds();
    mEdit->setCaretAndSelection(CharPos(0,68),CharPos(29,62),CharPos(35,62));
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"Thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(4,67)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(4,67));
    QCOMPARE(mEdit->selBegin(), CharPos(4,67));
    QCOMPARE(mEdit->selEnd(), CharPos(10,67));
    QCOMPARE(newEndPos,CharPos(1,76));

    //the 8th "thread" selected, caret before the 7th "thread"
    //should found the 6th "thread"
    clearFounds();
    mEdit->setCaretAndSelection(CharPos(28,62),CharPos(29,62),CharPos(35,62));
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(21,62)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(21,62));
    QCOMPARE(mEdit->selBegin(), CharPos(21,62));
    QCOMPARE(mEdit->selEnd(), CharPos(27,62));
    QCOMPARE(newEndPos,CharPos(1,76));
}

void TestQSyneditCppSearchReplace::test_basic_search_backward_from_caret_include_current_selection()
{
    //when caret is at selection begin/end, backward search will start from selection begin
    QByteArray encoding;
    mEdit->loadFromFile("resources/queue1.cpp",ENCODING_AUTO_DETECT,encoding);

    SearchOptions options = SearchOption::ssoFromCaret | SearchOption::ssoBackwards | SearchOption::ssoIncludeCurrentSelection;
    CharPos newEndPos;

    //no selection, caret at the 9th "thread" end
    //should found the 9th "thread"
    clearFounds();
    mEdit->setCaretXY(CharPos(10,67));
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"Thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(4,67)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(4,67));
    QCOMPARE(mEdit->selBegin(), CharPos(4,67));
    QCOMPARE(mEdit->selEnd(), CharPos(10,67));
    QCOMPARE(newEndPos,CharPos(1,76));

    //no selection, caret at the 9th "thread" begin
    //should found the 8th "thread"
    clearFounds();
    mEdit->setCaretXY(CharPos(4,67));
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(29,62)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(29,62));
    QCOMPARE(mEdit->selBegin(), CharPos(29,62));
    QCOMPARE(mEdit->selEnd(), CharPos(35,62));
    QCOMPARE(newEndPos,CharPos(1,76));

    //the 9th "thread" selected, caret at selection begin
    //should found the 9th "thread"
    clearFounds();
    mEdit->setCaretAndSelection(CharPos(4,67),CharPos(4,67),CharPos(10,67));
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"Thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(4,67)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(4,67));
    QCOMPARE(mEdit->selBegin(), CharPos(4,67));
    QCOMPARE(mEdit->selEnd(), CharPos(10,67));
    QCOMPARE(newEndPos,CharPos(1,76));

    //the 9th "thread" selected, caret at selection end
    //should found the 9th "thread"
    clearFounds();
    mEdit->setCaretAndSelection(CharPos(10,67),CharPos(4,67),CharPos(10,67));
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"Thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(4,67)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(4,67));
    QCOMPARE(mEdit->selBegin(), CharPos(4,67));
    QCOMPARE(mEdit->selEnd(), CharPos(10,67));
    QCOMPARE(newEndPos,CharPos(1,76));

    //the 8th "thread" selected, caret after the 9th "thread"
    //should found the 9th "thread"
    clearFounds();
    mEdit->setCaretAndSelection(CharPos(0,68),CharPos(29,62),CharPos(35,62));
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"Thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(4,67)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(4,67));
    QCOMPARE(mEdit->selBegin(), CharPos(4,67));
    QCOMPARE(mEdit->selEnd(), CharPos(10,67));
    QCOMPARE(newEndPos,CharPos(1,76));

    //the 8th "thread" selected, caret before the 7th "thread"
    //should found the 6th "thread"
    clearFounds();
    mEdit->setCaretAndSelection(CharPos(28,62),CharPos(29,62),CharPos(35,62));
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(21,62)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(21,62));
    QCOMPARE(mEdit->selBegin(), CharPos(21,62));
    QCOMPARE(mEdit->selEnd(), CharPos(27,62));
    QCOMPARE(newEndPos,CharPos(1,76));
}

void TestQSyneditCppSearchReplace::test_basic_search_backward_from_caret_with_wrap()
{
    QByteArray encoding;
    mEdit->loadFromFile("resources/queue1.cpp",ENCODING_AUTO_DETECT,encoding);

    SearchOptions options = SearchOption::ssoFromCaret | SearchOption::ssoBackwards | SearchOption::ssoWrapAround;
    CharPos newEndPos;

    //found the first "thread"
    mEdit->setCaretXY({16,3});
    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(10,3)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(10,3));
    QCOMPARE(mEdit->selBegin(), CharPos(10,3));
    QCOMPARE(mEdit->selEnd(), CharPos(16,3));
    QCOMPARE(newEndPos,CharPos(1,76));

    //found the last "thread" (wrap around)
    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(mFoundStrings,{"thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(22,72)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(22,72));
    QCOMPARE(mEdit->selBegin(), CharPos(22,72));
    QCOMPARE(mEdit->selEnd(), CharPos(28,72));
    QCOMPARE(newEndPos,CharPos(1,76));

    //at file begin, don't confirm
    mEdit->setCaretXY({0,0});
    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         [](){ return false; }),
             0);
    QCOMPARE(mFoundStrings,{});
    QCOMPARE(mReplaces,{});
    QCOMPARE(mFoundPositions,{});
    QCOMPARE(mFoundLens,{});
    QCOMPARE(mEdit->caretXY(), CharPos(0,0));
    QCOMPARE(mEdit->selBegin(), CharPos(0,0));
    QCOMPARE(mEdit->selEnd(), CharPos(0,0));
    QCOMPARE(newEndPos,CharPos(1,76));

    // at file begin, confirm
    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndExitProc,
                         [](){ return true; }),
             1);
    QCOMPARE(mFoundStrings,{"thread"});
    QCOMPARE(mReplaces,{""});
    QCOMPARE(mFoundPositions,{CharPos(22,72)});
    QCOMPARE(mFoundLens,{6});
    QCOMPARE(mEdit->caretXY(), CharPos(22,72));
    QCOMPARE(mEdit->selBegin(), CharPos(22,72));
    QCOMPARE(mEdit->selEnd(), CharPos(28,72));
    QCOMPARE(newEndPos,CharPos(1,76));

}

void TestQSyneditCppSearchReplace::test_search_all_backward_in_whole_file_from_end()
{
    QByteArray encoding;
    mEdit->loadFromFile("resources/queue1.cpp",ENCODING_AUTO_DETECT,encoding);

    SearchOptions options = SearchOption::ssoBackwards;
    CharPos newEndPos;

    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndContinueProc,
                         nullptr),10);
    QCOMPARE(mFoundStrings,
             QStringList(
                 {
                     "thread",
                     "Thread",
                     "thread",
                     "thread",
                     "thread",
                     "Thread",
                     "thread",
                     "Thread",
                     "Thread",
                     "thread",
                 }));
    QCOMPARE(mReplaces,
             QStringList(
                 {
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                 }));
    QCOMPARE(mFoundPositions,QList<CharPos>(
                 {
                     CharPos(22,72),
                     CharPos(4,67),
                     CharPos(29,62),
                     CharPos(21,62),
                     CharPos(21,52),
                     CharPos(5,48),
                     CharPos(12,18),
                     CharPos(4,15),
                     CharPos(6,12),
                     CharPos(10,3),
             }));
    QCOMPARE(mFoundLens,QList<int>(
                 {
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                 }));
    QCOMPARE(newEndPos,CharPos(1,76));

}

void TestQSyneditCppSearchReplace::test_search_all_backward_in_whole_file_from_caret_and_wrap()
{
    QByteArray encoding;
    mEdit->loadFromFile("resources/queue1.cpp",ENCODING_AUTO_DETECT,encoding);

    SearchOptions options = SearchOption::ssoBackwards | SearchOption::ssoWrapAround | SearchOption::ssoFromCaret;
    CharPos newEndPos;

    //caret at begin of the 8th "thread"
    mEdit->setCaretXY({29,62});
    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndContinueProc,
                         nullptr),10);
    QCOMPARE(mFoundStrings,
             QStringList(
                 {
                     "thread",
                     "thread",
                     "Thread",
                     "thread",
                     "Thread",
                     "Thread",
                     "thread",
                     "thread",
                     "Thread",
                     "thread",
                 }));
    QCOMPARE(mReplaces,
             QStringList(
                 {
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                 }));
    QCOMPARE(mFoundPositions,QList<CharPos>(
                 {
                     CharPos(21,62),
                     CharPos(21,52),
                     CharPos(5,48),
                     CharPos(12,18),
                     CharPos(4,15),
                     CharPos(6,12),
                     CharPos(10,3),
                     CharPos(22,72),
                     CharPos(4,67),
                     CharPos(29,62),
             }));
    QCOMPARE(mFoundLens,QList<int>(
                 {
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                 }));
    QCOMPARE(newEndPos,CharPos(1,76));

    //caret at end of the 8th "thread"
    mEdit->setCaretXY({35,62});
    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndContinueProc,
                         nullptr),10);
    QCOMPARE(mFoundStrings,
             QStringList(
                 {
                     "thread",
                     "thread",
                     "thread",
                     "Thread",
                     "thread",
                     "Thread",
                     "Thread",
                     "thread",
                     "thread",
                     "Thread",
                 }));
    QCOMPARE(mReplaces,
             QStringList(
                 {
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                 }));
    QCOMPARE(mFoundPositions,QList<CharPos>(
                 {
                     CharPos(29,62),
                     CharPos(21,62),
                     CharPos(21,52),
                     CharPos(5,48),
                     CharPos(12,18),
                     CharPos(4,15),
                     CharPos(6,12),
                     CharPos(10,3),
                     CharPos(22,72),
                     CharPos(4,67),
             }));
    QCOMPARE(mFoundLens,QList<int>(
                 {
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                 }));
    QCOMPARE(newEndPos,CharPos(1,76));

    //caret in the middle of the 8th "thread"
    mEdit->setCaretXY({30,62});
    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndContinueProc,
                         nullptr),9);
    QCOMPARE(mFoundStrings,
             QStringList(
                 {
                     "thread",
                     "thread",
                     "Thread",
                     "thread",
                     "Thread",
                     "Thread",
                     "thread",
                     "thread",
                     "Thread",
                 }));
    QCOMPARE(mReplaces,
             QStringList(
                 {
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                 }));
    QCOMPARE(mFoundPositions,QList<CharPos>(
                 {
                     CharPos(21,62),
                     CharPos(21,52),
                     CharPos(5,48),
                     CharPos(12,18),
                     CharPos(4,15),
                     CharPos(6,12),
                     CharPos(10,3),
                     CharPos(22,72),
                     CharPos(4,67),
             }));
    QCOMPARE(mFoundLens,QList<int>(
                 {
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                 }));
    QCOMPARE(newEndPos,CharPos(1,76));

}

void TestQSyneditCppSearchReplace::test_search_all_backward_in_whole_file_from_caret_and_no_wrap()
{
    QByteArray encoding;
    mEdit->loadFromFile("resources/queue1.cpp",ENCODING_AUTO_DETECT,encoding);

    SearchOptions options = SearchOption::ssoBackwards | SearchOption::ssoFromCaret;
    CharPos newEndPos;

    //before the 8th "thread"
    mEdit->setCaretXY({28,62});
    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndContinueProc,
                         nullptr),7);
    QCOMPARE(mFoundStrings,
             QStringList(
                 {
                     "thread",
                     "thread",
                     "Thread",
                     "thread",
                     "Thread",
                     "Thread",
                     "thread",
                 }));
    QCOMPARE(mReplaces,
             QStringList(
                 {
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                 }));
    QCOMPARE(mFoundPositions,QList<CharPos>(
                 {
                     CharPos(21,62),
                     CharPos(21,52),
                     CharPos(5,48),
                     CharPos(12,18),
                     CharPos(4,15),
                     CharPos(6,12),
                     CharPos(10,3),
             }));
    QCOMPARE(mFoundLens,QList<int>(
                 {
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                 }));
    QCOMPARE(newEndPos,CharPos(1,76));
}

void TestQSyneditCppSearchReplace::test_search_backward_in_scope()
{
    QByteArray encoding;
    mEdit->loadFromFile("resources/queue1.cpp",ENCODING_AUTO_DETECT,encoding);

    SearchOptions options = SearchOption::ssoBackwards;
    CharPos newEndPos;

    //search from begin of the 9th "thread" to begin of the 2th "thread"
    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  CharPos(6,12),
                                  CharPos(4,67),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndContinueProc,
                         nullptr),7);
    QCOMPARE(newEndPos,CharPos(4,67));
    QCOMPARE(mFoundStrings,
             QStringList(
                 {
                     "thread",
                     "thread",
                     "thread",
                     "Thread",
                     "thread",
                     "Thread",
                     "Thread",
                 }));
    QCOMPARE(mReplaces,
             QStringList(
                 {
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                 }));
    QCOMPARE(mFoundPositions,QList<CharPos>(
                 {
                     CharPos(29,62),
                     CharPos(21,62),
                     CharPos(21,52),
                     CharPos(5,48),
                     CharPos(12,18),
                     CharPos(4,15),
                     CharPos(6,12),
             }));
    QCOMPARE(mFoundLens,QList<int>(
                 {
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                 }));

    //search from begin of the 9th "thread" to middle of the 2th "thread"
    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  CharPos(7,12),
                                  CharPos(4,67),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndContinueProc,
                         nullptr),6);
    QCOMPARE(newEndPos,CharPos(4,67));
    QCOMPARE(mFoundStrings,
             QStringList(
                 {
                     "thread",
                     "thread",
                     "thread",
                     "Thread",
                     "thread",
                     "Thread",
                 }));
    QCOMPARE(mReplaces,
             QStringList(
                 {
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                 }));
    QCOMPARE(mFoundPositions,QList<CharPos>(
                 {
                     CharPos(29,62),
                     CharPos(21,62),
                     CharPos(21,52),
                     CharPos(5,48),
                     CharPos(12,18),
                     CharPos(4,15),
             }));
    QCOMPARE(mFoundLens,QList<int>(
                 {
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                 }));

    //search from begin of the 9th "thread" to end of the 2th "thread"
    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  CharPos(12,12),
                                  CharPos(4,67),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndContinueProc,
                         nullptr),6);
    QCOMPARE(newEndPos,CharPos(4,67));
    QCOMPARE(mFoundStrings,
             QStringList(
                 {
                     "thread",
                     "thread",
                     "thread",
                     "Thread",
                     "thread",
                     "Thread",
                 }));
    QCOMPARE(mReplaces,
             QStringList(
                 {
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                 }));
    QCOMPARE(mFoundPositions,QList<CharPos>(
                 {
                     CharPos(29,62),
                     CharPos(21,62),
                     CharPos(21,52),
                     CharPos(5,48),
                     CharPos(12,18),
                     CharPos(4,15),
             }));
    QCOMPARE(mFoundLens,QList<int>(
                 {
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                 }));

    //search from middle of the 9th "thread" to begin of the 2th "thread"
    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  CharPos(6,12),
                                  CharPos(5,67),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndContinueProc,
                         nullptr),7);
    QCOMPARE(newEndPos,CharPos(5,67));
    QCOMPARE(mFoundStrings,
             QStringList(
                 {
                     "thread",
                     "thread",
                     "thread",
                     "Thread",
                     "thread",
                     "Thread",
                     "Thread",
                 }));
    QCOMPARE(mReplaces,
             QStringList(
                 {
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                 }));
    QCOMPARE(mFoundPositions,QList<CharPos>(
                 {
                     CharPos(29,62),
                     CharPos(21,62),
                     CharPos(21,52),
                     CharPos(5,48),
                     CharPos(12,18),
                     CharPos(4,15),
                     CharPos(6,12),
             }));
    QCOMPARE(mFoundLens,QList<int>(
                 {
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                 }));

    //search from middle of the 9th "thread" to middle of the 2th "thread"
    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  CharPos(7,12),
                                  CharPos(5,67),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndContinueProc,
                         nullptr),6);
    QCOMPARE(newEndPos,CharPos(5,67));
    QCOMPARE(mFoundStrings,
             QStringList(
                 {
                     "thread",
                     "thread",
                     "thread",
                     "Thread",
                     "thread",
                     "Thread",
                 }));
    QCOMPARE(mReplaces,
             QStringList(
                 {
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                 }));
    QCOMPARE(mFoundPositions,QList<CharPos>(
                 {
                     CharPos(29,62),
                     CharPos(21,62),
                     CharPos(21,52),
                     CharPos(5,48),
                     CharPos(12,18),
                     CharPos(4,15),
             }));
    QCOMPARE(mFoundLens,QList<int>(
                 {
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                 }));

    //search from middle of the 9th "thread" to end of the 2th "thread"
    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  CharPos(12,12),
                                  CharPos(5,67),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndContinueProc,
                         nullptr),6);
    QCOMPARE(newEndPos,CharPos(5,67));
    QCOMPARE(mFoundStrings,
             QStringList(
                 {
                     "thread",
                     "thread",
                     "thread",
                     "Thread",
                     "thread",
                     "Thread",
                 }));
    QCOMPARE(mReplaces,
             QStringList(
                 {
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                 }));
    QCOMPARE(mFoundPositions,QList<CharPos>(
                 {
                     CharPos(29,62),
                     CharPos(21,62),
                     CharPos(21,52),
                     CharPos(5,48),
                     CharPos(12,18),
                     CharPos(4,15),
             }));
    QCOMPARE(mFoundLens,QList<int>(
                 {
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                 }));

    //search from end of the 9th "thread" to begin of the 2th "thread"
    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  CharPos(6,12),
                                  CharPos(10,67),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndContinueProc,
                         nullptr),8);
    QCOMPARE(newEndPos,CharPos(10,67));
    QCOMPARE(mFoundStrings,
             QStringList(
                 {
                     "Thread",
                     "thread",
                     "thread",
                     "thread",
                     "Thread",
                     "thread",
                     "Thread",
                     "Thread",
                 }));
    QCOMPARE(mReplaces,
             QStringList(
                 {
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                 }));
    QCOMPARE(mFoundPositions,QList<CharPos>(
                 {
                     CharPos(4,67),
                     CharPos(29,62),
                     CharPos(21,62),
                     CharPos(21,52),
                     CharPos(5,48),
                     CharPos(12,18),
                     CharPos(4,15),
                     CharPos(6,12),
             }));
    QCOMPARE(mFoundLens,QList<int>(
                 {
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                 }));

    //search from end of the 9th "thread" to middle of the 2th "thread"
    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  CharPos(7,12),
                                  CharPos(10,67),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndContinueProc,
                         nullptr),7);
    QCOMPARE(newEndPos,CharPos(10,67));
    QCOMPARE(mFoundStrings,
             QStringList(
                 {
                     "Thread",
                     "thread",
                     "thread",
                     "thread",
                     "Thread",
                     "thread",
                     "Thread",
                 }));
    QCOMPARE(mReplaces,
             QStringList(
                 {
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                 }));
    QCOMPARE(mFoundPositions,QList<CharPos>(
                 {
                     CharPos(4,67),
                     CharPos(29,62),
                     CharPos(21,62),
                     CharPos(21,52),
                     CharPos(5,48),
                     CharPos(12,18),
                     CharPos(4,15),
             }));
    QCOMPARE(mFoundLens,QList<int>(
                 {
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                 }));

    //search from end of the 9th "thread" to end of the 2th "thread"
    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  CharPos(12,12),
                                  CharPos(10,67),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndContinueProc,
                         nullptr),7);
    QCOMPARE(newEndPos,CharPos(10,67));
    QCOMPARE(mFoundStrings,
             QStringList(
                 {
                     "Thread",
                     "thread",
                     "thread",
                     "thread",
                     "Thread",
                     "thread",
                     "Thread",
                 }));
    QCOMPARE(mReplaces,
             QStringList(
                 {
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                 }));
    QCOMPARE(mFoundPositions,QList<CharPos>(
                 {
                     CharPos(4,67),
                     CharPos(29,62),
                     CharPos(21,62),
                     CharPos(21,52),
                     CharPos(5,48),
                     CharPos(12,18),
                     CharPos(4,15),
             }));
    QCOMPARE(mFoundLens,QList<int>(
                 {
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                 }));

    //template
    clearFounds();
    QCOMPARE(mEdit->searchReplace("thread","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mBasicMatchedAndContinueProc,
                         nullptr),10);
    QCOMPARE(newEndPos,CharPos(1,76));
    QCOMPARE(mFoundStrings,
             QStringList(
                 {
                     "thread",
                     "Thread",
                     "thread",
                     "thread",
                     "thread",
                     "Thread",
                     "thread",
                     "Thread",
                     "Thread",
                     "thread",
                 }));
    QCOMPARE(mReplaces,
             QStringList(
                 {
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                     "",
                 }));
    QCOMPARE(mFoundPositions,QList<CharPos>(
                 {
                     CharPos(22,72),
                     CharPos(4,67),
                     CharPos(29,62),
                     CharPos(21,62),
                     CharPos(21,52),
                     CharPos(5,48),
                     CharPos(12,18),
                     CharPos(4,15),
                     CharPos(6,12),
                     CharPos(10,3),
             }));
    QCOMPARE(mFoundLens,QList<int>(
                 {
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                     6,
                 }));
}

void TestQSyneditCppSearchReplace::test_search_regex()
{
    QByteArray encoding;
    mEdit->loadFromFile("resources/queue1.cpp",ENCODING_AUTO_DETECT,encoding);

    SearchOptions options = SearchOption::ssoNone;
    CharPos newEndPos;

    clearFounds();
    QCOMPARE(mEdit->searchReplace("\\w+","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mRegexSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(newEndPos,CharPos(1,76));
    QCOMPARE(mFoundStrings,{"include"});
    QCOMPARE(mReplaces, {""});
    QCOMPARE(mFoundPositions,{CharPos(1,0)});
    QCOMPARE(mFoundLens,{7});
    QCOMPARE(mEdit->caretXY(), CharPos(8,0));
    QCOMPARE(mEdit->selBegin(), CharPos(1,0));
    QCOMPARE(mEdit->selEnd(), CharPos(8,0));

    clearFounds();
    QCOMPARE(mEdit->searchReplace("\\d+","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mRegexSearcher.get(),
                         mBasicMatchedAndExitProc,
                         nullptr),1);
    QCOMPARE(newEndPos,CharPos(1,76));
    QCOMPARE(mFoundStrings,{"123"});
    QCOMPARE(mReplaces, {""});
    QCOMPARE(mFoundPositions,{CharPos(13,10)});
    QCOMPARE(mFoundLens,{3});
    QCOMPARE(mEdit->caretXY(), CharPos(16,10));
    QCOMPARE(mEdit->selBegin(), CharPos(13,10));
    QCOMPARE(mEdit->selEnd(), CharPos(16,10));
}

void QSynedit::TestQSyneditCppSearchReplace::test_basic_forward_search_without_wrap2()
{
//    QByteArray encoding;
//    mEdit->loadFromFile("resources/queue1.cpp",ENCODING_AUTO_DETECT,encoding);

//    SearchOptions options = SearchOption::ssoEntireScope;
//    mEdit->searchReplace("stop","",options,
//                         mBasicSearcher.get(),
//                         mBasicMatchedProc,
//                         nullptr);
//    QCOMPARE(mFoundStrings, {"stop"});
//    QCOMPARE(mReplaces, {""});
//    QCOMPARE(mFoundPositions, {CharPos(29,15)});
//    QCOMPARE(mFoundLens, {4});
//    QCOMPARE(mEdit->caretXY(), CharPos(33,15));
//    QCOMPARE(mEdit->selBegin(), CharPos(29,15));
//    QCOMPARE(mEdit->selEnd(), CharPos(33,15));

    QByteArray encoding;
    mEdit->loadFromFile("resources/queue1.cpp",ENCODING_AUTO_DETECT,encoding);

    SearchOptions options = SearchOption::ssoFromCaret;
    CharPos newEndPos;

    QCOMPARE(mEdit->searchReplace("stop","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         nullptr,
                         nullptr),1);
    QCOMPARE(mEdit->caretXY(), CharPos(33,15));
    QCOMPARE(mEdit->selBegin(), CharPos(29,15));
    QCOMPARE(mEdit->selEnd(), CharPos(33,15));
    QCOMPARE(newEndPos,CharPos(1,76));

    QCOMPARE(mEdit->searchReplace("stop","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         nullptr,
                         nullptr),1);
    QCOMPARE(mEdit->caretXY(), CharPos(65,23));
    QCOMPARE(mEdit->selBegin(), CharPos(61,23));
    QCOMPARE(mEdit->selEnd(), CharPos(65,23));
    QCOMPARE(newEndPos,CharPos(1,76));

    QCOMPARE(mEdit->searchReplace("stop","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         nullptr,
                         nullptr),1);
    QCOMPARE(mEdit->caretXY(), CharPos(52,26));
    QCOMPARE(mEdit->selBegin(), CharPos(48,26));
    QCOMPARE(mEdit->selEnd(), CharPos(52,26));
    QCOMPARE(newEndPos,CharPos(1,76));

    QCOMPARE(mEdit->searchReplace("stop","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         nullptr,
                         nullptr),1);
    QCOMPARE(mEdit->caretXY(), CharPos(12,49));
    QCOMPARE(mEdit->selBegin(), CharPos(8,49));
    QCOMPARE(mEdit->selEnd(), CharPos(12,49));
    QCOMPARE(newEndPos,CharPos(1,76));

    QCOMPARE(mEdit->searchReplace("stop","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         nullptr,
                         nullptr),1);
    QCOMPARE(mEdit->caretXY(), CharPos(26,59));
    QCOMPARE(mEdit->selBegin(), CharPos(22,59));
    QCOMPARE(mEdit->selEnd(), CharPos(26,59));
    QCOMPARE(newEndPos,CharPos(1,76));

    QCOMPARE(mEdit->searchReplace("stop","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                                  newEndPos,
                         options,
                         mBasicSearcher.get(),
                         nullptr,
                         nullptr),0);
    QCOMPARE(mEdit->caretXY(), CharPos(26,59));
    QCOMPARE(mEdit->selBegin(), CharPos(22,59));
    QCOMPARE(mEdit->selEnd(), CharPos(26,59));
    QCOMPARE(newEndPos,CharPos(1,76));
}

void TestQSyneditCppSearchReplace::test_replace_forward_from_caret()
{
//    QStringList text1{
//        "int iNTel() {",
//        " ttt;",
//        "}",
//        "int main() {",
//        " UNIT xint0;",
//        " if (xint0>0) {",
//        "  intel(xint0++);",
//        " }",
//        "}",
//        "int test() {",
//        " return 0;",
//        "}"
//    };

    QStringList text1{
        "int int int int",
        "int int int int",
        "int int int int",
    };
    QStringList text2{
        "   ",
        "  int ",
        "   ",
    };

    QStringList text3{
        "   ",
        "  int ",
        "   ",
    };
    QStringList text4{
        "beautiful beautiful beautiful beautiful",
        "beautiful beautiful int beautiful",
        "beautiful beautiful beautiful beautiful",
    };

    SearchOptions options = ssoWrapAround | ssoFromCaret;
    CharPos newEndPos;
    auto matchProc = [this](const QString& sFound,
            const QString& sReplace, const CharPos& pos, int wordLen){
        mFoundStrings.append(sFound);
        mReplaces.append(sReplace);
        mFoundPositions.append(pos);
        mFoundLens.append(wordLen);
        if (pos == CharPos{8,1})
            return SearchAction::Skip;
        return SearchAction::Replace;
    };

    mEdit->setContent(text1);
    mEdit->setCaretXY({8,1});
    mEdit->searchReplace("int","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                         newEndPos,
                         options,
                         mBasicSearcher.get(),
                         matchProc,
                         nullptr);
    QCOMPARE(mEdit->content(),text2);
    QCOMPARE(newEndPos,CharPos(3,2));
    QCOMPARE(mEdit->caretXY(), CharPos(1,1));
    QCOMPARE(mEdit->selBegin(), CharPos(1,1));
    QCOMPARE(mEdit->selEnd(), CharPos(1,1));

    mEdit->setContent(text1);
    mEdit->setCaretXY({9,1});
    mEdit->searchReplace("int","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                         newEndPos,
                         options,
                         mBasicSearcher.get(),
                         matchProc,
                         nullptr);
    QCOMPARE(mEdit->content(),text3);
    QCOMPARE(newEndPos,CharPos(3,2));
    QCOMPARE(mEdit->caretXY(), CharPos(1,1));
    QCOMPARE(mEdit->selBegin(), CharPos(1,1));
    QCOMPARE(mEdit->selEnd(), CharPos(1,1));

    mEdit->setContent(text1);
    mEdit->setCaretXY({8,1});
    mEdit->searchReplace("int","beautiful",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                         newEndPos,
                         options,
                         mBasicSearcher.get(),
                         matchProc,
                         nullptr);
    QCOMPARE(mEdit->content(),text4);
    QCOMPARE(newEndPos,CharPos(39,2));
    QCOMPARE(mEdit->caretXY(), CharPos(19,1));
    QCOMPARE(mEdit->selBegin(), CharPos(19,1));
    QCOMPARE(mEdit->selEnd(), CharPos(19,1));
}

void TestQSyneditCppSearchReplace::test_replace_forward_from_caret_with_selection()
{
    QStringList text1{
        "int int int int",
        "int int int int",
        "int int int int",
    };
    QStringList text2{
        "int int int int",
        "int int int int",
        "int replaced int int",
    };
    QStringList text3{
        "int int int int",
        "int int int int",
        "int int  int",
    };
    QStringList text4{
        "int int int int",
        "int int int int",
        "int int replaced1 int",
    };
    QStringList text5{
        "int int int int",
        "int int int int",
        "int int replaced2 int",
    };
    SearchOptions options = ssoFromCaret;
    CharPos newEndPos;

    //no selection,caret at begin of the second "int" on 3rd line
    mEdit->setContent(text1);
    mEdit->setCaretXY({4,2});
    mEdit->searchReplace("int","replaced",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                         newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mReplaceAndExitProc,
                         nullptr);
    QCOMPARE(mEdit->content(),text2);
    QCOMPARE(newEndPos,CharPos(20,2));
    QCOMPARE(mEdit->caretXY(), CharPos(16,2));
    QCOMPARE(mEdit->selBegin(), CharPos(13,2));
    QCOMPARE(mEdit->selEnd(), CharPos(16,2));

    //no selection,caret at end of the second "int" on 3rd line
    mEdit->setContent(text1);
    mEdit->setCaretXY({7,2});
    mEdit->searchReplace("int","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                         newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mReplaceAndExitProc,
                         nullptr);
    QCOMPARE(mEdit->content(),text3);
    QCOMPARE(newEndPos,CharPos(12,2));
    QCOMPARE(mEdit->caretXY(), CharPos(12,2));
    QCOMPARE(mEdit->selBegin(), CharPos(9,2));
    QCOMPARE(mEdit->selEnd(), CharPos(12,2));

    //the second "int" on 3rd line selected, caret at selection begin
    mEdit->setContent(text1);
    mEdit->setCaretAndSelection(CharPos{4,2},CharPos{4,2},CharPos{7,2});
    mEdit->searchReplace("int","replaced1",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                         newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mReplaceAndExitProc,
                         nullptr);
    QCOMPARE(mEdit->content(),text4);
    QCOMPARE(newEndPos,CharPos(21,2));
    QCOMPARE(mEdit->caretXY(), CharPos(21,2));
    QCOMPARE(mEdit->selBegin(), CharPos(18,2));
    QCOMPARE(mEdit->selEnd(), CharPos(21,2));

    //the second "int" on 3rd line selected, caret at selection end
    mEdit->setContent(text1);
    mEdit->setCaretAndSelection(CharPos{7,2},CharPos{4,2},CharPos{7,2});
    mEdit->searchReplace("int","replaced2",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                         newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mReplaceAndExitProc,
                         nullptr);
    QCOMPARE(mEdit->content(),text5);
    QCOMPARE(newEndPos,CharPos(21,2));
    QCOMPARE(mEdit->caretXY(), CharPos(21,2));
    QCOMPARE(mEdit->selBegin(), CharPos(18,2));
    QCOMPARE(mEdit->selEnd(), CharPos(21,2));
}

void TestQSyneditCppSearchReplace::test_replace_forward_from_caret_include_selection()
{
    QStringList text1{
        "int int int int",
        "int int int int",
        "int int int int",
    };
    QStringList text2{
        "int int int int",
        "int int int int",
        "int replaced int int",
    };
    QStringList text3{
        "int int int int",
        "int int int int",
        "int int  int",
    };
    QStringList text4{
        "int int int int",
        "int int int int",
        "int replaced1 int int",
    };
    QStringList text5{
        "int int int int",
        "int int int int",
        "int replaced2 int int",
    };
    QStringList text6{
        "int int int int",
        "int int int replaced2",
        "int int int int",
    };
    SearchOptions options = ssoFromCaret | ssoIncludeCurrentSelection;
    CharPos newEndPos;

    //no selection,caret at begin of the second "int" on 3rd line
    mEdit->setContent(text1);
    mEdit->setCaretXY({4,2});
    mEdit->searchReplace("int","replaced",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                         newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mReplaceAndExitProc,
                         nullptr);
    QCOMPARE(mEdit->content(),text2);
    QCOMPARE(newEndPos,CharPos(20,2));
    QCOMPARE(mEdit->caretXY(), CharPos(16,2));
    QCOMPARE(mEdit->selBegin(), CharPos(13,2));
    QCOMPARE(mEdit->selEnd(), CharPos(16,2));


    //no selection,caret at end of the second "int" on 3rd line
    mEdit->setContent(text1);
    mEdit->setCaretXY({7,2});
    mEdit->searchReplace("int","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                         newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mReplaceAndExitProc,
                         nullptr);
    QCOMPARE(mEdit->content(),text3);
    QCOMPARE(newEndPos,CharPos(12,2));
    QCOMPARE(mEdit->caretXY(), CharPos(12,2));
    QCOMPARE(mEdit->selBegin(), CharPos(9,2));
    QCOMPARE(mEdit->selEnd(), CharPos(12,2));


    //the second "int" on 3rd line selected, caret at selection begin
    mEdit->setContent(text1);
    mEdit->setCaretAndSelection(CharPos{4,2},CharPos{4,2},CharPos{7,2});
    mEdit->searchReplace("int","replaced1",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                         newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mReplaceAndExitProc,
                         nullptr);
    QCOMPARE(mEdit->content(),text4);
    QCOMPARE(newEndPos,CharPos(21,2));
    QCOMPARE(mEdit->caretXY(), CharPos(17,2));
    QCOMPARE(mEdit->selBegin(), CharPos(14,2));
    QCOMPARE(mEdit->selEnd(), CharPos(17,2));

    //the second "int" on 3rd line selected, caret at selection end
    mEdit->setContent(text1);
    mEdit->setCaretAndSelection(CharPos{7,2},CharPos{4,2},CharPos{7,2});
    mEdit->searchReplace("int","replaced2",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                         newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mReplaceAndExitProc,
                         nullptr);
    QCOMPARE(mEdit->content(),text5);
    QCOMPARE(newEndPos,CharPos(21,2));
    QCOMPARE(mEdit->caretXY(), CharPos(17,2));
    QCOMPARE(mEdit->selBegin(), CharPos(14,2));
    QCOMPARE(mEdit->selEnd(), CharPos(17,2));

    //the forth "int" on 2rd line selected, caret at selection begin
    mEdit->setContent(text1);
    mEdit->setCaretAndSelection(CharPos{12,1},CharPos{12,1},CharPos{15,1});
    mEdit->searchReplace("int","replaced2",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                         newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mReplaceAndExitProc,
                         nullptr);
    QCOMPARE(mEdit->content(),text6);
    QCOMPARE(newEndPos,CharPos(15,2));
    QCOMPARE(mEdit->caretXY(), CharPos(3,2));
    QCOMPARE(mEdit->selBegin(), CharPos(0,2));
    QCOMPARE(mEdit->selEnd(), CharPos(3,2));
}

void TestQSyneditCppSearchReplace::test_replace_backward_from_caret()
{
    QStringList text1{
        "int int int int",
        "int int int int",
        "int int int int",
    };
    QStringList text2{
        "   ",
        " int  ",
        "   ",
    };

    QStringList text3{
        "   ",
        " int  ",
        "   ",
    };
    QStringList text4{
        "beautiful beautiful beautiful beautiful",
        "beautiful int beautiful beautiful",
        "beautiful beautiful beautiful beautiful",
    };

    SearchOptions options = ssoWrapAround | ssoFromCaret | ssoBackwards;
    CharPos newEndPos;

    auto matchProc = [this](const QString& sFound,
            const QString& sReplace, const CharPos& pos, int wordLen){
        mFoundStrings.append(sFound);
        mReplaces.append(sReplace);
        mFoundPositions.append(pos);
        mFoundLens.append(wordLen);
        if (pos == CharPos{4,1})
            return SearchAction::Skip;
        return SearchAction::Replace;
    };

    mEdit->setContent(text1);
    mEdit->setCaretXY({8,1});
    mEdit->searchReplace("int","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                         newEndPos,
                         options,
                         mBasicSearcher.get(),
                         matchProc,
                         nullptr);
    QCOMPARE(mEdit->content(),text2);
    QCOMPARE(newEndPos,CharPos(3,2));
    QCOMPARE(mEdit->caretXY(), CharPos(5,1));
    QCOMPARE(mEdit->selBegin(), CharPos(5,1));
    QCOMPARE(mEdit->selEnd(), CharPos(5,1));

    mEdit->setContent(text1);
    mEdit->setCaretXY({12,1});
    mEdit->searchReplace("int","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                         newEndPos,
                         options,
                         mBasicSearcher.get(),
                         matchProc,
                         nullptr);
    QCOMPARE(mEdit->content(),text3);
    QCOMPARE(newEndPos,CharPos(3,2));
    QCOMPARE(mEdit->caretXY(), CharPos(6,1));
    QCOMPARE(mEdit->selBegin(), CharPos(6,1));
    QCOMPARE(mEdit->selEnd(), CharPos(6,1));

    mEdit->setContent(text1);
    mEdit->setCaretXY({12,1});
    mEdit->searchReplace("int","beautiful",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                         newEndPos,
                         options,
                         mBasicSearcher.get(),
                         matchProc,
                         nullptr);
    QCOMPARE(mEdit->content(),text4);
    QCOMPARE(newEndPos,CharPos(39,2));
    QCOMPARE(mEdit->caretXY(), CharPos(24,1));
    QCOMPARE(mEdit->selBegin(), CharPos(24,1));
    QCOMPARE(mEdit->selEnd(), CharPos(24,1));
}

void TestQSyneditCppSearchReplace::test_replace_backward_from_caret_with_selection()
{
    QStringList text1{
        "int int int int",
        "int int int int",
        "int int int int",
    };
    QStringList text2{
        "int int int int",
        "int int int int",
        "int replaced int int",
    };
    QStringList text3{
        "int int int int",
        "int int int int",
        "int int  int",
    };
    QStringList text4{
        "int int int int",
        "int int int int",
        "int replaced1 int int",
    };
    QStringList text5{
        "int int int int",
        "int int int int",
        "int replaced2 int int",
    };
    SearchOptions options = ssoFromCaret | ssoBackwards;
    CharPos newEndPos;

    //no selection,caret at begin of the third "int" on 3rd line
    mEdit->setContent(text1);
    mEdit->setCaretXY({8,2});
    mEdit->searchReplace("int","replaced",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                         newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mReplaceAndExitProc,
                         nullptr);
    QCOMPARE(mEdit->content(),text2);
    QCOMPARE(newEndPos,CharPos(20,2));
    QCOMPARE(mEdit->caretXY(), CharPos(0,2));
    QCOMPARE(mEdit->selBegin(), CharPos(0,2));
    QCOMPARE(mEdit->selEnd(), CharPos(3,2));

    //no selection,caret at end of the third "int" on 3rd line
    mEdit->setContent(text1);
    mEdit->setCaretXY({11,2});
    mEdit->searchReplace("int","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                         newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mReplaceAndExitProc,
                         nullptr);
    QCOMPARE(mEdit->content(),text3);
    QCOMPARE(newEndPos,CharPos(12,2));
    QCOMPARE(mEdit->caretXY(), CharPos(4,2));
    QCOMPARE(mEdit->selBegin(), CharPos(4,2));
    QCOMPARE(mEdit->selEnd(), CharPos(7,2));

    //the thrid "int" on 3rd line selected, caret at selection begin
    mEdit->setContent(text1);
    mEdit->setCaretAndSelection(CharPos{8,2},CharPos{8,2},CharPos{11,2});
    mEdit->searchReplace("int","replaced1",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                         newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mReplaceAndExitProc,
                         nullptr);
    QCOMPARE(mEdit->content(),text4);
    QCOMPARE(newEndPos,CharPos(21,2));
    QCOMPARE(mEdit->caretXY(), CharPos(0,2));
    QCOMPARE(mEdit->selBegin(), CharPos(0,2));
    QCOMPARE(mEdit->selEnd(), CharPos(3,2));

    //the third "int" on 3rd line selected, caret at selection end
    mEdit->setContent(text1);
    mEdit->setCaretAndSelection(CharPos{11,2},CharPos{8,2},CharPos{11,2});
    mEdit->searchReplace("int","replaced2",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                         newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mReplaceAndExitProc,
                         nullptr);
    QCOMPARE(mEdit->content(),text5);
    QCOMPARE(newEndPos,CharPos(21,2));
    QCOMPARE(mEdit->caretXY(), CharPos(0,2));
    QCOMPARE(mEdit->selBegin(), CharPos(0,2));
    QCOMPARE(mEdit->selEnd(), CharPos(3,2));
}

void TestQSyneditCppSearchReplace::test_replace_backward_from_caret_include_selection()
{
    QStringList text1{
        "int int int int",
        "int int int int",
        "int int int int",
    };
    QStringList text2{
        "int int int int",
        "int int int int",
        "int replaced int int",
    };
    QStringList text3{
        "int int int int",
        "int int int int",
        "int int  int",
    };
    QStringList text4{
        "int int int int",
        "int int int int",
        "int int replaced1 int",
    };
    QStringList text5{
        "int int int int",
        "int int int int",
        "int int replaced2 int",
    };
    QStringList text6{
        "int int int int",
        "int int int int",
        "replaced2 int int int",
    };
    QStringList text7{
        "int int int int",
        "int int int replaced2",
        "int int int int",
    };
    SearchOptions options = ssoFromCaret | ssoBackwards | ssoIncludeCurrentSelection;
    CharPos newEndPos;

    //no selection,caret at begin of the third "int" on 3rd line
    mEdit->setContent(text1);
    mEdit->setCaretXY({8,2});
    mEdit->searchReplace("int","replaced",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                         newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mReplaceAndExitProc,
                         nullptr);
    QCOMPARE(mEdit->content(),text2);
    QCOMPARE(newEndPos,CharPos(20,2));
    QCOMPARE(mEdit->caretXY(), CharPos(0,2));
    QCOMPARE(mEdit->selBegin(), CharPos(0,2));
    QCOMPARE(mEdit->selEnd(), CharPos(3,2));

    //no selection,caret at end of the third "int" on 3rd line
    mEdit->setContent(text1);
    mEdit->setCaretXY({11,2});
    mEdit->searchReplace("int","",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                         newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mReplaceAndExitProc,
                         nullptr);
    QCOMPARE(mEdit->content(),text3);
    QCOMPARE(newEndPos,CharPos(12,2));
    QCOMPARE(mEdit->caretXY(), CharPos(4,2));
    QCOMPARE(mEdit->selBegin(), CharPos(4,2));
    QCOMPARE(mEdit->selEnd(), CharPos(7,2));

    //the thrid "int" on 3rd line selected, caret at selection begin
    mEdit->setContent(text1);
    mEdit->setCaretAndSelection(CharPos{8,2},CharPos{8,2},CharPos{11,2});
    mEdit->searchReplace("int","replaced1",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                         newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mReplaceAndExitProc,
                         nullptr);
    QCOMPARE(mEdit->content(),text4);
    QCOMPARE(newEndPos,CharPos(21,2));
    QCOMPARE(mEdit->caretXY(), CharPos(4,2));
    QCOMPARE(mEdit->selBegin(), CharPos(4,2));
    QCOMPARE(mEdit->selEnd(), CharPos(7,2));

    //the third "int" on 3rd line selected, caret at selection end
    mEdit->setContent(text1);
    mEdit->setCaretAndSelection(CharPos{11,2},CharPos{8,2},CharPos{11,2});
    mEdit->searchReplace("int","replaced2",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                         newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mReplaceAndExitProc,
                         nullptr);
    QCOMPARE(mEdit->content(),text5);
    QCOMPARE(newEndPos,CharPos(21,2));
    QCOMPARE(mEdit->caretXY(), CharPos(4,2));
    QCOMPARE(mEdit->selBegin(), CharPos(4,2));
    QCOMPARE(mEdit->selEnd(), CharPos(7,2));

    //the first "int" on 3rd line selected, caret at selection end
    mEdit->setContent(text1);
    mEdit->setCaretAndSelection(CharPos{3,2},CharPos{0,2},CharPos{3,2});
    mEdit->searchReplace("int","replaced2",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                         newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mReplaceAndExitProc,
                         nullptr);
    QCOMPARE(mEdit->content(),text6);
    QCOMPARE(newEndPos,CharPos(21,2));
    QCOMPARE(mEdit->caretXY(), CharPos(12,1));
    QCOMPARE(mEdit->selBegin(), CharPos(12,1));
    QCOMPARE(mEdit->selEnd(), CharPos(15,1));

    //the forth "int" on 2rd line selected, caret at selection end
    mEdit->setContent(text1);
    mEdit->setCaretAndSelection(CharPos{15,1},CharPos{12,1},CharPos{15,1});
    mEdit->searchReplace("int","replaced2",
                                  mEdit->fileBegin(),
                                  mEdit->fileEnd(),
                         newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mReplaceAndExitProc,
                         nullptr);
    QCOMPARE(mEdit->content(),text7);
    QCOMPARE(newEndPos,CharPos(15,2));
    QCOMPARE(mEdit->caretXY(), CharPos(8,1));
    QCOMPARE(mEdit->selBegin(), CharPos(8,1));
    QCOMPARE(mEdit->selEnd(), CharPos(11,1));
}

void TestQSyneditCppSearchReplace::test_replace_forward_scope()
{
    QStringList text1{
        "int int int int",
        "int int int int",
        "int int int int",
    };


    QStringList text2{
        "int   ",
        "   ",
        "  int int",
    };

    QStringList text3{
        "int replaced replaced replaced",
        "replaced replaced replaced replaced",
        "replaced replaced int int",
    };

    SearchOptions options = ssoNone;
    CharPos newEndPos;
    mEdit->setContent(text1);
    mEdit->searchReplace("int","",
                         CharPos{4,0},
                         CharPos{8,2},
                         newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mReplaceAndContinueProc,
                         nullptr);
    QCOMPARE(mEdit->content(),text2);
    QCOMPARE(newEndPos,CharPos(2,2));

    mEdit->setContent(text1);
    mEdit->searchReplace("int","replaced",
                         CharPos{4,0},
                         CharPos{8,2},
                         newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mReplaceAndContinueProc,
                         nullptr);
    QCOMPARE(mEdit->content(),text3);
    QCOMPARE(newEndPos,CharPos(18,2));
}

void TestQSyneditCppSearchReplace::test_replace_backward_scope()
{
    QStringList text1{
        "int int int int",
        "int int int int",
        "int int int int",
    };


    QStringList text2{
        "int   ",
        "   ",
        "  int int",
    };

    QStringList text3{
        "int replaced replaced replaced",
        "replaced replaced replaced replaced",
        "replaced replaced int int",
    };

    SearchOptions options = ssoBackwards;
    CharPos newEndPos;
    mEdit->setContent(text1);
    mEdit->searchReplace("int","",
                         CharPos{4,0},
                         CharPos{8,2},
                         newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mReplaceAndContinueProc,
                         nullptr);
    QCOMPARE(mEdit->content(),text2);
    QCOMPARE(newEndPos,CharPos(2,2));

    mEdit->setContent(text1);
    mEdit->searchReplace("int","replaced",
                         CharPos{4,0},
                         CharPos{8,2},
                         newEndPos,
                         options,
                         mBasicSearcher.get(),
                         mReplaceAndContinueProc,
                         nullptr);
    QCOMPARE(mEdit->content(),text3);
    QCOMPARE(newEndPos,CharPos(18,2));
}


namespace {
SearchAction replaceEveryMatch(const QString &, const QString &, const CharPos &, int)
{
    return SearchAction::ReplaceAll;
}

class FailingReplacementSearcher : public BasicSearcher
{
public:
    QString replace(const QString &occurrence, const QString &replacement) override
    {
        if (++mCalls == 2)
            throw std::runtime_error("Injected replacement failure");
        return BasicSearcher::replace(occurrence, replacement);
    }
private:
    int mCalls = 0;
};
}

void TestQSyneditCppSearchReplace::test_replace_all_whole_word_length_changes_data()
{
    QTest::addColumn<QString>("replacement");
    QTest::addColumn<bool>("backwards");
    for (bool backwards : {false, true}) {
        const QByteArray direction = backwards ? "backward-" : "forward-";
        QTest::newRow((direction + "shorter").constData()) << QString("x") << backwards;
        QTest::newRow((direction + "longer").constData()) << QString("replacement") << backwards;
        QTest::newRow((direction + "empty").constData()) << QString() << backwards;
    }
}

void TestQSyneditCppSearchReplace::test_replace_all_whole_word_length_changes()
{
    QFETCH(QString, replacement);
    QFETCH(bool, backwards);
    const QStringList original{"cat cat scatter cat"};
    mEdit->setContent(original);
    SearchOptions options = ssoWholeWord | ssoMatchCase;
    options.setFlag(ssoBackwards, backwards);
    CharPos scopeEnd;
    QCOMPARE(mEdit->searchReplace("cat", replacement, mEdit->fileBegin(),
                                  mEdit->fileEnd(), scopeEnd, options,
                                  mBasicSearcher.get(), replaceEveryMatch), 3);
    const QStringList expected{replacement + " " + replacement + " scatter " + replacement};
    QCOMPARE(mEdit->content(), expected);
    QCOMPARE(scopeEnd, mEdit->fileEnd());
    QVERIFY(!mEdit->editing());
    QVERIFY(mEdit->canUndo());
    mEdit->undo();
    QCOMPARE(mEdit->content(), original);
    QVERIFY(!mEdit->canUndo());
    QVERIFY(!mEdit->modified());
    mEdit->redo();
    QCOMPARE(mEdit->content(), expected);
    QVERIFY(!mEdit->canRedo());
}

void TestQSyneditCppSearchReplace::test_replace_all_multiline_data()
{
    QTest::addColumn<bool>("backwards");
    QTest::addColumn<bool>("scoped");
    QTest::addColumn<bool>("wrapped");
    for (bool backwards : {false, true}) {
        for (bool scoped : {false, true}) {
            for (bool wrapped : {false, true}) {
                const QByteArray name = QByteArray(backwards ? "backward" : "forward")
                        + (scoped ? "-scope" : "-file") + (wrapped ? "-wrap" : "-plain");
                QTest::newRow(name.constData()) << backwards << scoped << wrapped;
            }
        }
    }
}

void TestQSyneditCppSearchReplace::test_replace_all_multiline()
{
    QFETCH(bool, backwards);
    QFETCH(bool, scoped);
    QFETCH(bool, wrapped);
    const QStringList original{"outside foo", "foo foo", "foo", "outside foo"};
    mEdit->setContent(original);
    SearchOptions options = ssoMatchCase;
    options.setFlag(ssoBackwards, backwards);
    options.setFlag(ssoFromCaret, wrapped);
    options.setFlag(ssoWrapAround, wrapped);
    mEdit->setCaretXY(CharPos{4, 1});
    const CharPos begin = scoped ? CharPos{0, 1} : mEdit->fileBegin();
    const CharPos end = scoped ? CharPos{3, 2} : mEdit->fileEnd();
    CharPos scopeEnd;
    QCOMPARE(mEdit->searchReplace("foo", "A\nB", begin, end, scopeEnd,
                                  options, mBasicSearcher.get(), replaceEveryMatch), scoped ? 3 : 5);
    const QStringList expected = scoped
            ? QStringList{"outside foo", "A", "B A", "B", "A", "B", "outside foo"}
            : QStringList{"outside A", "B", "A", "B A", "B", "A", "B", "outside A", "B"};
    QCOMPARE(mEdit->content(), expected);
    QCOMPARE(scopeEnd, scoped ? CharPos(1, 5) : mEdit->fileEnd());
    QVERIFY(!mEdit->editing());
    mEdit->undo();
    QCOMPARE(mEdit->content(), original);
    QVERIFY(!mEdit->canUndo());
    mEdit->redo();
    QCOMPARE(mEdit->content(), expected);
}

void TestQSyneditCppSearchReplace::test_replace_all_unicode_undo_redo()
{
    const QString panda = QString::fromUtf8("\xF0\x9F\x90\xBC");
    const QStringList original{panda + " café " + panda, "café"};
    mEdit->setContent(original);
    CharPos scopeEnd;
    QCOMPARE(mEdit->searchReplace("café", panda + " italiano", mEdit->fileBegin(),
                                  mEdit->fileEnd(), scopeEnd, ssoMatchCase,
                                  mBasicSearcher.get(), replaceEveryMatch), 2);
    const QStringList expected{panda + " " + panda + " italiano " + panda, panda + " italiano"};
    QCOMPARE(mEdit->content(), expected);
    QCOMPARE(scopeEnd, mEdit->fileEnd());
    mEdit->undo();
    QCOMPARE(mEdit->content(), original);
    mEdit->redo();
    QCOMPARE(mEdit->content(), expected);
}

void TestQSyneditCppSearchReplace::test_read_only_replace_preserves_state()
{
    const QStringList original{"foo foo", "foo"};
    mEdit->setContent(original);
    mEdit->setReadOnly(true);
    const auto reset = qScopeGuard([this] { mEdit->setReadOnly(false); });
    CharPos scopeEnd;
    QCOMPARE(mEdit->searchReplace("foo", "longer\nreplacement", mEdit->fileBegin(),
                                  mEdit->fileEnd(), scopeEnd, ssoNone,
                                  mBasicSearcher.get(), replaceEveryMatch), 1);
    QCOMPARE(mEdit->content(), original);
    QCOMPARE(scopeEnd, mEdit->fileEnd());
    QVERIFY(!mEdit->canUndo());
    QVERIFY(!mEdit->modified());
    QVERIFY(!mEdit->editing());
    QCOMPARE(mEdit->searchReplace("foo", "", mEdit->fileBegin(), mEdit->fileEnd(),
                                  scopeEnd, ssoNone, mBasicSearcher.get(),
                                  [](const QString &, const QString &, const CharPos &, int) {
        return SearchAction::Skip;
    }), 3);
}

void TestQSyneditCppSearchReplace::test_invalid_scope_preserves_state()
{
    const QStringList original{"foo foo"};
    mEdit->setContent(original);
    mEdit->setCaretXY(CharPos{2, 0});
    const CharPos caret = mEdit->caretXY();
    CharPos scopeEnd;
    for (const auto &scope : QList<QPair<CharPos, CharPos>>{
             {CharPos{-1, 0}, mEdit->fileEnd()},
             {mEdit->fileBegin(), CharPos{0, 2}},
             {mEdit->fileEnd(), mEdit->fileBegin()}}) {
        QCOMPARE(mEdit->searchReplace("foo", "bar", scope.first, scope.second,
                                      scopeEnd, ssoNone, mBasicSearcher.get(), replaceEveryMatch), 0);
        QCOMPARE(mEdit->content(), original);
        QCOMPARE(mEdit->caretXY(), caret);
        QVERIFY(!mEdit->canUndo());
        QVERIFY(!mEdit->editing());
    }
    scopeEnd = CharPos{-1, -1};
    QCOMPARE(mEdit->searchReplace("", "", mEdit->fileBegin(), mEdit->fileEnd(),
                                  scopeEnd, ssoNone, mBasicSearcher.get()), 0);
    QCOMPARE(scopeEnd, mEdit->fileEnd());
    scopeEnd = CharPos{-1, -1};
    QCOMPARE(mEdit->searchReplace("foo", "", mEdit->fileBegin(), mEdit->fileEnd(),
                                  scopeEnd, ssoNone, nullptr), 0);
    QCOMPARE(scopeEnd, mEdit->fileEnd());
}

void TestQSyneditCppSearchReplace::test_search_failure_keeps_editor_usable()
{
    const QStringList original{"foo foo"};
    mEdit->setContent(original);
    FailingReplacementSearcher searcher;
    CharPos scopeEnd;
    bool failed = false;
    try {
        mEdit->searchReplace("foo", "bar", mEdit->fileBegin(), mEdit->fileEnd(),
                            scopeEnd, ssoNone, &searcher, replaceEveryMatch);
    } catch (const std::runtime_error &) {
        failed = true;
    }
    QVERIFY(failed);
    QVERIFY(!mEdit->editing());
    QVERIFY(mEdit->getOptions().testFlag(EditorOption::AutoIndent));
    QCOMPARE(mEdit->content(), QStringList{"bar foo"});
    mEdit->undo();
    QCOMPARE(mEdit->content(), original);
    mEdit->setCaretXY(mEdit->fileEnd());
    mEdit->processCommand(EditCommand::Input, QString("!"));
    QCOMPARE(mEdit->content(), QStringList{"foo foo!"});
    mEdit->undo();
    QCOMPARE(mEdit->content(), original);
}

void TestQSyneditCppSearchReplace::test_search_invalid_regex_preserves_document()
{
    const QStringList original{"foo foo"};
    mEdit->setContent(original);
    mEdit->setCaretXY(CharPos{2, 0});
    const CharPos caret = mEdit->caretXY();
    CharPos scopeEnd;
    QCOMPARE(mEdit->searchReplace("[", "bar", mEdit->fileBegin(), mEdit->fileEnd(),
                                  scopeEnd, ssoNone, mRegexSearcher.get(), replaceEveryMatch), 0);
    QCOMPARE(mEdit->content(), original);
    QCOMPARE(mEdit->caretXY(), caret);
    QCOMPARE(scopeEnd, mEdit->fileEnd());
    QVERIFY(!mEdit->canUndo());
    QVERIFY(!mEdit->editing());
}

void TestQSyneditCppSearchReplace::test_wrap_cancel_preserves_document()
{
    const QStringList original{"foo", "tail"};
    mEdit->setContent(original);
    mEdit->setCaretXY(mEdit->fileEnd());
    const CharPos caret = mEdit->caretXY();
    int wrapCalls = 0;
    CharPos scopeEnd;
    QCOMPARE(mEdit->searchReplace("foo", "bar", mEdit->fileBegin(), mEdit->fileEnd(),
                                  scopeEnd, ssoFromCaret | ssoWrapAround, mBasicSearcher.get(),
                                  replaceEveryMatch, [&wrapCalls] { ++wrapCalls; return false; }), 0);
    QCOMPARE(wrapCalls, 1);
    QCOMPARE(mEdit->content(), original);
    QCOMPARE(mEdit->caretXY(), caret);
    QVERIFY(!mEdit->canUndo());
    QVERIFY(!mEdit->editing());
}

void TestQSyneditCppSearchReplace::test_large_document_replace_undo_redo()
{
    QStringList original;
    original.reserve(3000);
    for (int line = 0; line < 3000; ++line)
        original.append(QString("int token_%1 = token + token;").arg(line));
    mEdit->setContent(original);
    CharPos scopeEnd;
    QCOMPARE(mEdit->searchReplace("token", "value_name", mEdit->fileBegin(), mEdit->fileEnd(),
                                  scopeEnd, ssoWholeWord | ssoMatchCase,
                                  mBasicSearcher.get(), replaceEveryMatch), 6000);
    QStringList expected;
    expected.reserve(original.size());
    for (int line = 0; line < original.size(); ++line)
        expected.append(QString("int token_%1 = value_name + value_name;").arg(line));
    QCOMPARE(mEdit->content(), expected);
    QCOMPARE(scopeEnd, mEdit->fileEnd());
    QVERIFY(!mEdit->editing());
    mEdit->undo();
    QCOMPARE(mEdit->content(), original);
    QVERIFY(!mEdit->canUndo());
    mEdit->redo();
    QCOMPARE(mEdit->content(), expected);
}


void TestQSyneditCppSearchReplace::test_regex_replacement_keeps_original_context_data()
{
    QTest::addColumn<QString>("source");
    QTest::addColumn<QString>("pattern");
    QTest::addColumn<QString>("replacement");
    QTest::addColumn<bool>("backwards");
    for (bool backwards : {false, true}) {
        const QByteArray prefix = backwards ? "backward-" : "forward-";
        QTest::newRow((prefix + "lookarounds").constData())
                << QString("prefix_12_suffix prefix_345_suffix")
                << QString("(?<=prefix_)(\\d+)(?=_suffix)") << QString("n\\1") << backwards;
        QTest::newRow((prefix + "capture-in-lookbehind").constData())
                << QString("prefix_12 prefix_345")
                << QString("(?<=(prefix_))(\\d+)") << QString("\\1(\\2)") << backwards;
        QTest::newRow((prefix + "zero-width-lookahead").constData())
                << QString("a cat cat") << QString("(?=cat)") << QString("X") << backwards;
        QTest::newRow((prefix + "beginning-anchor").constData())
                << QString("cat dog") << QString("^") << QString("start ") << backwards;
        QTest::newRow((prefix + "end-anchor").constData())
                << QString("cat dog") << QString("$") << QString(" end") << backwards;
        QTest::newRow((prefix + "optional-groups").constData())
                << QString("ab a ab") << QString("(a)(b)?") << QString("[\\2-\\1]") << backwards;
        QTest::newRow((prefix + "empty-nonempty-matches").constData())
                << QString("baab") << QString("a*") << QString("X") << backwards;
        QTest::newRow((prefix + "numbered-captures").constData())
                << QString("abcdefghijkl abcdefghijkl")
                << QString("(a)(b)(c)(d)(e)(f)(g)(h)(i)(j)(k)(l)")
                << QString("\\12/\\1/\\2/\\0/\\99/\\\\1") << backwards;
    }
}

void TestQSyneditCppSearchReplace::test_regex_replacement_keeps_original_context()
{
    QFETCH(QString, source);
    QFETCH(QString, pattern);
    QFETCH(QString, replacement);
    QFETCH(bool, backwards);
    const QRegularExpression regex(pattern);
    QVERIFY(regex.isValid());
    QString expected = source;
    expected.replace(regex, replacement);
    int count = 0;
    auto matches = regex.globalMatch(source);
    while (matches.hasNext()) {
        matches.next();
        ++count;
    }
    const QStringList original{source};
    mEdit->setContent(original);
    SearchOptions options = ssoMatchCase;
    options.setFlag(ssoBackwards, backwards);
    CharPos scopeEnd;
    QCOMPARE(mEdit->searchReplace(pattern, replacement, mEdit->fileBegin(), mEdit->fileEnd(),
                                  scopeEnd, options, mRegexSearcher.get(), replaceEveryMatch), count);
    QCOMPARE(mEdit->content(), QStringList{expected});
    QCOMPARE(scopeEnd, mEdit->fileEnd());
    QVERIFY(!mEdit->editing());
    mEdit->undo();
    QCOMPARE(mEdit->content(), original);
    QVERIFY(!mEdit->canUndo());
    mEdit->redo();
    QCOMPARE(mEdit->content(), QStringList{expected});
}

void TestQSyneditCppSearchReplace::test_regex_result_cache_cleared()
{
    RegexSearcher searcher;
    searcher.setPattern("(a)");
    QCOMPARE(searcher.findAll("a a"), 2);
    QCOMPARE(searcher.replaceResult(1, "a", "\\1!"), QString("a!"));
    searcher.setPattern("");
    QCOMPARE(searcher.findAll("a a"), 0);
    QCOMPARE(searcher.resultCount(), 0);
    QCOMPARE(searcher.replaceResult(0, "keep", "replace"), QString("keep"));
    searcher.setPattern("[");
    QCOMPARE(searcher.findAll("a a"), 0);
    QCOMPARE(searcher.resultCount(), 0);
    searcher.setPattern("(a)");
    QCOMPARE(searcher.findAll("a"), 1);
    searcher.setOptions(ssoMatchCase);
    QCOMPARE(searcher.resultCount(), 0);
}

}
