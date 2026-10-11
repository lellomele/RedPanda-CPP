#include <QTest>
#include <QApplication>
#include <QTemporaryDir>
#include <QFileSystemWatcher>
#include <QMessageBox>
#include <QSignalSpy>
#include <QTimer>
#include <QStringListModel>
#include <stdexcept>
#include "src/codesnippetsmanager.h"
#include "src/widgets/codecompletionpopup.h"
#include "src/widgets/headercompletionpopup.h"
#include "src/settings/codecompletionsettings.h"
#include "src/settings/dirsettings.h"
#include "src/settings/editorsettings.h"
#include "src/colorscheme.h"
#include "src/editor.h"
#include "test_editor_symbol_completion.h"
#include <qsynedit/syntaxer/cpp.h>

using QSynedit::CharPos;

TestEditorSymbolCompletion::TestEditorSymbolCompletion(QObject *parent):
    TestEditorBase{parent}
{
}

void TestEditorSymbolCompletion::test_input_double_quotes_in_string1()
{
    // auto add another '"' when input '"' and put the caret in the middle
    // auto skip the inputted '"' when caret before then enclosing '"' of string
    QStringList text{
        "const char *s = ",
    };
    QStringList text1{
        "const char *s = \"\"",
    };
    QStringList text2{
        "const char *s = \"abc\"",
    };
    QStringList text3{
        "const char *s = \"abc\";",
    };
    mEditor->setContent(text);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(),'"');
    QCOMPARE(mEditor->content(), text1);
    QTest::keyPress(mEditor.get(),'a');
    QTest::keyPress(mEditor.get(),'b');
    QTest::keyPress(mEditor.get(),'c');
    QCOMPARE(mEditor->content(), text2);
    QTest::keyPress(mEditor.get(),'"');
    QCOMPARE(mEditor->content(), text2);
    QTest::keyPress(mEditor.get(),';');
    QCOMPARE(mEditor->content(), text3);

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text2);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());

    //redo
    mEditor->redo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->redo();
    QCOMPARE(mEditor->content(), text2);
    mEditor->redo();
    QCOMPARE(mEditor->content(), text3);
    QVERIFY(!mEditor->canRedo());
    QVERIFY(mEditor->modified());

    //undo again
    mEditor->undo();
    QCOMPARE(mEditor->content(), text2);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());

}

void TestEditorSymbolCompletion::test_input_double_quotes_in_string2()
{
    //dont skip the inputted '"' when it's in a escaping sequence
    QStringList text{
        "const char *s = \"abcdef\"",
    };
    QStringList text1{
        "const char *s = \"ab\\\"cdef\"",
    };
    QStringList text2{
        "const char *s = \"ab\\\"-\"cdef\"",
    };
    mEditor->setContent(text);
    mEditor->setCaretXY(mEditor->fileEnd());
    mEditor->setCaretXY(CharPos{19,0});
    QTest::keyPress(mEditor.get(),'\\');
    QTest::keyPress(mEditor.get(),'"');
    QCOMPARE(mEditor->content(), text1);
    QTest::keyPress(mEditor.get(),Qt::Key_Left);
    QTest::keyPress(mEditor.get(),'"');
    QTest::keyPress(mEditor.get(),'-');
    QCOMPARE(mEditor->content(), text2);

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());

    //redo
    mEditor->redo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->redo();
    QCOMPARE(mEditor->content(), text2);
    QVERIFY(!mEditor->canRedo());
    QVERIFY(mEditor->modified());

    //undo again
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());

}

void TestEditorSymbolCompletion::test_input_double_quotes_in_string3()
{
    // correct input '"' in a un-enclosed string
    // correct input '"' after string
    QStringList text{
        "const char *s = \"abcdef",
    };
    QStringList text1{
        "const char *s = \"abcdef\"",
    };
    QStringList text2{
        "const char *s = \"abcdef\"\"\"",
    };
    mEditor->setContent(text);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(),'\"');
    QCOMPARE(mEditor->content(), text1);
    QTest::keyPress(mEditor.get(),'\"');
    QCOMPARE(mEditor->content(), text2);

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());

    //redo
    mEditor->redo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->redo();
    QCOMPARE(mEditor->content(), text2);
    QVERIFY(!mEditor->canRedo());
    QVERIFY(mEditor->modified());

    //undo again
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());
}

void TestEditorSymbolCompletion::test_input_double_quotes_in_string4()
{
    // correct input '"' before '"'
    QStringList text{
        "const char *s = \"\"",
    };
    QStringList text1{
        "const char *s = \"\"\"\"",
    };
    QStringList text2{
        "const char *s = \"ab\"\"\"",
    };
    mEditor->setContent(text);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(),Qt::Key_Left);
    QTest::keyPress(mEditor.get(),Qt::Key_Left);
    QTest::keyPress(mEditor.get(),'\"');
    QCOMPARE(mEditor->content(), text1);
    QTest::keyPress(mEditor.get(),'a');
    QTest::keyPress(mEditor.get(),'b');
    QCOMPARE(mEditor->content(), text2);

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());

    //redo
    mEditor->redo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->redo();
    QCOMPARE(mEditor->content(), text2);
    QVERIFY(!mEditor->canRedo());
    QVERIFY(mEditor->modified());

    //undo again
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());
}

void TestEditorSymbolCompletion::test_input_double_quotes_in_string5()
{
    // input successive '"'
    QStringList text{
        "const char *s = ",
    };
    QStringList text1{
        "const char *s = \"\"",
    };
    QStringList text2{
        "const char *s = \"\"\"\"",
    };
    mEditor->setContent(text);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(),'"');
    QCOMPARE(mEditor->content(), text1);
    QTest::keyPress(mEditor.get(),'"');
    QCOMPARE(mEditor->content(), text1); //auto skip '"'
    QTest::keyPress(mEditor.get(),'"');
    QCOMPARE(mEditor->content(), text2); //auto skip '"'

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());

    //redo
    mEditor->redo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->redo();
    QCOMPARE(mEditor->content(), text2);
    QVERIFY(!mEditor->canRedo());
    QVERIFY(mEditor->modified());

    //undo again
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());
}

void TestEditorSymbolCompletion::test_input_double_quotes_on_selection()
{
    QStringList text{
        "const char *s = abc",
    };
    QStringList text1{
        "const char *s = \"abc\"",
    };
    QStringList text2{
        "const char *s = \"abc\";",
    };
    mEditor->setContent(text);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(),Qt::Key_Left,Qt::ShiftModifier);
    QTest::keyPress(mEditor.get(),Qt::Key_Left,Qt::ShiftModifier);
    QTest::keyPress(mEditor.get(),Qt::Key_Left,Qt::ShiftModifier);
    QTest::keyPress(mEditor.get(),'"');
    QCOMPARE(mEditor->content(), text1);
    QTest::keyPress(mEditor.get(),';');
    QCOMPARE(mEditor->content(), text2);

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());

    //redo
    mEditor->redo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->redo();
    QCOMPARE(mEditor->content(), text2);
    QVERIFY(!mEditor->canRedo());
    QVERIFY(mEditor->modified());

    //undo again
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());
}

void TestEditorSymbolCompletion::test_input_double_quotes_in_raw_string1()
{
    // input '"' at start of raw string and
    // input '"' at end of raw string
    QStringList text{
        "const char *s = R",
    };
    QStringList text1{
        "const char *s = R\"\"",
    };
    QStringList text2{
        "const char *s = R\"()\"",
    };
    QStringList text3{
        "const char *s = R\"(ab)\"",
    };
    QStringList text4{
        "const char *s = R\"(ab)\"\"\"",
    };
    mEditor->setContent(text);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(),'"');
    QCOMPARE(mEditor->content(), text1);
    QTest::keyPress(mEditor.get(),'(');
    QCOMPARE(mEditor->content(), text2);
    QTest::keyPress(mEditor.get(),'a');
    QTest::keyPress(mEditor.get(),'b');
    QCOMPARE(mEditor->content(), text3);
    QTest::keyPress(mEditor.get(),')');
    QCOMPARE(mEditor->content(), text3); // auto skip ')'
    QTest::keyPress(mEditor.get(),'"');
    QCOMPARE(mEditor->content(), text3); // auto skip '"'
    QTest::keyPress(mEditor.get(),'"');
    QCOMPARE(mEditor->content(), text4);

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text3);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text2);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());

    //redo
    mEditor->redo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->redo();
    QCOMPARE(mEditor->content(), text2);
    mEditor->redo();
    QCOMPARE(mEditor->content(), text3);
    mEditor->redo();
    QCOMPARE(mEditor->content(), text4);
    QVERIFY(!mEditor->canRedo());
    QVERIFY(mEditor->modified());

    //undo again
    mEditor->undo();
    QCOMPARE(mEditor->content(), text3);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text2);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());
}

void TestEditorSymbolCompletion::test_input_double_quotes_in_raw_string2()
{
    // input '"' in a raw string
    QStringList text{
        "const char *s = R\"\""
    };
    QStringList text1{
        "const char *s = R\"\"\""
    };
    QStringList text2{
        "const char *s = R\"\"a\""
    };
    mEditor->setContent(text);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(),Qt::Key_Left);
    QTest::keyPress(mEditor.get(),'"');
    QCOMPARE(mEditor->content(), text1);
    QTest::keyPress(mEditor.get(),'a');
    QCOMPARE(mEditor->content(), text2);

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());

    //redo
    mEditor->redo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->redo();
    QCOMPARE(mEditor->content(), text2);
    QVERIFY(!mEditor->canRedo());
    QVERIFY(mEditor->modified());

    //undo again
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());
}

void TestEditorSymbolCompletion::test_input_double_quotes_in_raw_string3()
{
    // input '"' in a raw string (not escaping)
    QStringList text{
        "const char *s = R\"(\")\""
    };
    QStringList text1{
        "const char *s = R\"(\"\")\""
    };
    QStringList text2{
        "const char *s = R\"(\"a\")\""
    };
    mEditor->setContent(text);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(),Qt::Key_Left);
    QTest::keyPress(mEditor.get(),Qt::Key_Left);
    QTest::keyPress(mEditor.get(),Qt::Key_Left);
    QTest::keyPress(mEditor.get(),'"');
    QCOMPARE(mEditor->content(), text1);
    QTest::keyPress(mEditor.get(),'a');
    QCOMPARE(mEditor->content(), text2);

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());

    //redo
    mEditor->redo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->redo();
    QCOMPARE(mEditor->content(), text2);
    QVERIFY(!mEditor->canRedo());
    QVERIFY(mEditor->modified());

    //undo again
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());
}

void TestEditorSymbolCompletion::test_input_double_quotes_in_raw_string4()
{
    // input '"' before raw string's starting '"'
    QStringList text{
        "const char *s = R\"\""
    };
    QStringList text1{
        "const char *s = R\"\"\"\""
    };
    QStringList text2{
        "const char *s = R\"a\"\"\""
    };
    mEditor->setContent(text);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(),Qt::Key_Left);
    QTest::keyPress(mEditor.get(),Qt::Key_Left);
    QTest::keyPress(mEditor.get(),'"');
    QCOMPARE(mEditor->content(), text1);
    QTest::keyPress(mEditor.get(),'a');
    QCOMPARE(mEditor->content(), text2);


    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());

    //redo
    mEditor->redo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->redo();
    QCOMPARE(mEditor->content(), text2);
    QVERIFY(!mEditor->canRedo());
    QVERIFY(mEditor->modified());

    //undo again
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());
}

void TestEditorSymbolCompletion::test_input_double_quotes_in_char_literals1()
{
    // input '"' in char literal
    QStringList text{
        "char ch = "
    };
    QStringList text1{
        "char ch = ''"
    };
    QStringList text2{
        "char ch = '\"'"
    };
    QStringList text3{
        "char ch = '\"';"
    };
    mEditor->setContent(text);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(),'\'');
    QCOMPARE(mEditor->content(), text1);
    QTest::keyPress(mEditor.get(),'"');
    QCOMPARE(mEditor->content(), text2);
    QTest::keyPress(mEditor.get(),'\'');
    QCOMPARE(mEditor->content(), text2);  // auto skip '\''
    QTest::keyPress(mEditor.get(),';');
    QCOMPARE(mEditor->content(), text3);

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text2);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());

    //redo
    mEditor->redo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->redo();
    QCOMPARE(mEditor->content(), text2);
    mEditor->redo();
    QCOMPARE(mEditor->content(), text3);
    QVERIFY(!mEditor->canRedo());
    QVERIFY(mEditor->modified());

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text2);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());
}

void TestEditorSymbolCompletion::test_input_double_quotes_in_char_literals2()
{
    // input '\' '"' in char literal
    QStringList text{
        "char ch = "
    };
    QStringList text1{
        "char ch = ''"
    };
    QStringList text2{
        "char ch = '\\\"'"
    };
    QStringList text3{
        "char ch = '\\\"';"
    };
    mEditor->setContent(text);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(),'\'');
    QCOMPARE(mEditor->content(), text1);
    QTest::keyPress(mEditor.get(),'\\');
    QTest::keyPress(mEditor.get(),'\"');
    QCOMPARE(mEditor->content(), text2);
    QTest::keyPress(mEditor.get(),'\'');
    QCOMPARE(mEditor->content(), text2);  // auto skip '\''
    QTest::keyPress(mEditor.get(),';');
    QCOMPARE(mEditor->content(), text3);

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text2);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());

    //redo
    mEditor->redo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->redo();
    QCOMPARE(mEditor->content(), text2);
    mEditor->redo();
    QCOMPARE(mEditor->content(), text3);
    QVERIFY(!mEditor->canRedo());
    QVERIFY(mEditor->modified());

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text2);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());
}

void TestEditorSymbolCompletion::test_input_double_quotes_in_comments()
{
    QStringList text{
        "// const char *s = ",
    };
    QStringList text1{
        "// const char *s = \"",
    };
    QStringList text2{
        "// const char *s = \"\"",
    };
    QStringList text3{
        "// const char *s = \"abc\"",
    };
    mEditor->setContent(text);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(), '"');
    QCOMPARE(mEditor->content(), text1);
    QTest::keyPress(mEditor.get(), Qt::Key_Left);
    QTest::keyPress(mEditor.get(), '"');
    QCOMPARE(mEditor->content(), text2);
    QTest::keyPress(mEditor.get(), 'a');
    QTest::keyPress(mEditor.get(), 'b');
    QTest::keyPress(mEditor.get(), 'c');
    QCOMPARE(mEditor->content(), text3);

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text2);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());

    //redo
    mEditor->redo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->redo();
    QCOMPARE(mEditor->content(), text2);
    mEditor->redo();
    QCOMPARE(mEditor->content(), text3);
    QVERIFY(!mEditor->canRedo());
    QVERIFY(mEditor->modified());

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text2);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());
}

void TestEditorSymbolCompletion::test_input_single_quotes_in_char_literals1()
{
    //input '\'' before ''
    QStringList text{
        "char ch = ''"
    };
    QStringList text1{
        "char ch = ''''"
    };
    QStringList text2{
        "char ch = 'ab'''"
    };
    mEditor->setContent(text);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(), Qt::Key_Left);
    QTest::keyPress(mEditor.get(), Qt::Key_Left);
    QTest::keyPress(mEditor.get(), '\'');
    QCOMPARE(mEditor->content(), text1);
    QTest::keyPress(mEditor.get(), 'a');
    QTest::keyPress(mEditor.get(), 'b');
    QCOMPARE(mEditor->content(), text2);

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());

    //redo
    mEditor->redo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->redo();
    QCOMPARE(mEditor->content(), text2);
    QVERIFY(!mEditor->canRedo());
    QVERIFY(mEditor->modified());

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());
}

void TestEditorSymbolCompletion::test_input_single_quotes_in_char_literals2()
{
    //input '\'' in ''
    QStringList text{
        "char ch = "
    };
    QStringList text1{
        "char ch = ''"
    };
    QStringList text2{
        "char ch = ''''"
    };
    mEditor->setContent(text);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(), '\'');
    QCOMPARE(mEditor->content(), text1);
    QTest::keyPress(mEditor.get(), '\'');
    QCOMPARE(mEditor->content(), text1); //auto skip '\''
    QTest::keyPress(mEditor.get(), '\'');
    QCOMPARE(mEditor->content(), text2);

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());

    //redo
    mEditor->redo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->redo();
    QCOMPARE(mEditor->content(), text2);
    QVERIFY(!mEditor->canRedo());
    QVERIFY(mEditor->modified());

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());
}

void TestEditorSymbolCompletion::test_input_single_quotes_in_char_literals3()
{
    //input '\\' '\'' in ''
    QStringList text{
        "char ch = "
    };
    QStringList text1{
        "char ch = ''"
    };
    QStringList text2{
        "char ch = '\\\''"
    };
    mEditor->setContent(text);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(), '\'');
    QCOMPARE(mEditor->content(), text1);
    QTest::keyPress(mEditor.get(), '\\');
    QTest::keyPress(mEditor.get(), '\'');
    QCOMPARE(mEditor->content(), text2);

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());

    //redo
    mEditor->redo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->redo();
    QCOMPARE(mEditor->content(), text2);
    QVERIFY(!mEditor->canRedo());
    QVERIFY(mEditor->modified());

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());
}

void TestEditorSymbolCompletion::test_input_single_quotes_in_string_literals()
{
    QStringList text = {
        "\"\"",
    };
    QStringList text1 = {
        "\"\'\"",
    };
    QStringList text2 = {
        "\"\'\'\"",
    };
    QStringList text3 = {
        "\"\'ab\'\"",
    };
    mEditor->setContent(text);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(), Qt::Key_Left);
    QTest::keyPress(mEditor.get(), '\'');
    QCOMPARE(mEditor->content(), text1);
    QTest::keyPress(mEditor.get(), Qt::Key_Left);
    QTest::keyPress(mEditor.get(), '\'');
    QCOMPARE(mEditor->content(), text2);
    QTest::keyPress(mEditor.get(), 'a');
    QTest::keyPress(mEditor.get(), 'b');
    QCOMPARE(mEditor->content(), text3);

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text2);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());

    //redo
    mEditor->redo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->redo();
    QCOMPARE(mEditor->content(), text2);
    mEditor->redo();
    QCOMPARE(mEditor->content(), text3);
    QVERIFY(!mEditor->canRedo());
    QVERIFY(mEditor->modified());

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text2);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());
}

void TestEditorSymbolCompletion::test_input_single_quotes_in_rawstring_not_escaping()
{
    QStringList text = {
        "const char* s = R\"()\";",
    };
    QStringList text1 = {
        "const char* s = R\"(')\";",
    };
    mEditor->setContent(text);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(), Qt::Key_Left);
    QTest::keyPress(mEditor.get(), Qt::Key_Left);
    QTest::keyPress(mEditor.get(), Qt::Key_Left);
    QTest::keyPress(mEditor.get(), '\'');
    QCOMPARE(mEditor->content(), text1);
}

void TestEditorSymbolCompletion::test_input_single_quotes_in_rawstring_start()
{
    QStringList text = {
        "const char* s = R\"()\";",
    };
    QStringList text1 = {
        "const char* s = R\"'()\";",
    };
    mEditor->setContent(text);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(), Qt::Key_Left);
    QTest::keyPress(mEditor.get(), Qt::Key_Left);
    QTest::keyPress(mEditor.get(), Qt::Key_Left);
    QTest::keyPress(mEditor.get(), Qt::Key_Left);
    QTest::keyPress(mEditor.get(), '\'');
    QCOMPARE(mEditor->content(), text1);
}

void TestEditorSymbolCompletion::test_input_single_quotes_in_rawstring_end()
{
    QStringList text = {
        "const char* s = R\"()\";",
    };
    QStringList text1 = {
        "const char* s = R\"()'\";",
    };
    mEditor->setContent(text);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(), Qt::Key_Left);
    QTest::keyPress(mEditor.get(), Qt::Key_Left);
    QTest::keyPress(mEditor.get(), '\'');
    QCOMPARE(mEditor->content(), text1);
}

void TestEditorSymbolCompletion::test_input_single_quotes_in_comments1()
{
    QStringList text = {
        "// test",
    };
    QStringList text1 = {
        "// test\'",
    };
    QStringList text2 = {
        "// test\'\'",
    };
    QStringList text3 = {
        "// test\'ab\'",
    };
    mEditor->setContent(text);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(), '\'');
    QCOMPARE(mEditor->content(), text1);
    QTest::keyPress(mEditor.get(), Qt::Key_Left);
    QTest::keyPress(mEditor.get(), '\'');
    QCOMPARE(mEditor->content(), text2);
    QTest::keyPress(mEditor.get(), 'a');
    QTest::keyPress(mEditor.get(), 'b');
    QCOMPARE(mEditor->content(), text3);
}

void TestEditorSymbolCompletion::test_input_single_quotes_in_comments2()
{
    QStringList text = {
        "/* test",
    };
    QStringList text1 = {
        "/* test\'",
    };
    QStringList text2 = {
        "/* test\'\'",
    };
    QStringList text3 = {
        "/* test\'ab\'",
    };
    mEditor->setContent(text);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(), '\'');
    QCOMPARE(mEditor->content(), text1);
    QTest::keyPress(mEditor.get(), Qt::Key_Left);
    QTest::keyPress(mEditor.get(), '\'');
    QCOMPARE(mEditor->content(), text2);
    QTest::keyPress(mEditor.get(), 'a');
    QTest::keyPress(mEditor.get(), 'b');
    QCOMPARE(mEditor->content(), text3);

}

void TestEditorSymbolCompletion::test_input_single_quotes_on_selection()
{
    QStringList text{
        "char ch = \\n",
    };
    QStringList text1{
        "char ch = '\\n'",
    };
    QStringList text2{
        "char ch = '\\n';",
    };
    mEditor->setContent(text);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(),Qt::Key_Left,Qt::ShiftModifier);
    QTest::keyPress(mEditor.get(),Qt::Key_Left,Qt::ShiftModifier);
    QTest::keyPress(mEditor.get(),'\'');
    QCOMPARE(mEditor->content(), text1);
    QTest::keyPress(mEditor.get(),';');
    QCOMPARE(mEditor->content(), text2);

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());

    //redo
    mEditor->redo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->redo();
    QCOMPARE(mEditor->content(), text2);
    QVERIFY(!mEditor->canRedo());
    QVERIFY(mEditor->modified());

    //undo again
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());
}

void TestEditorSymbolCompletion::test_input_single_quotes_in_number()
{
    QStringList text{
        "double x = 2.13",
    };
    QStringList text1{
        "double x = 2.13'",
    };
    QStringList text2{
        "double x = 2.13'45",
    };
    QStringList text3{
        "double x = 2.13'45'",
    };
    mEditor->setContent(text);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(),'\'');
    QCOMPARE(mEditor->content(), text1);
    QTest::keyPress(mEditor.get(),'4');
    QTest::keyPress(mEditor.get(),'5');
    QCOMPARE(mEditor->content(), text2);
    QTest::keyPress(mEditor.get(),'\'');
    QCOMPARE(mEditor->content(), text3);

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text2);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());

    //redo
    mEditor->redo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->redo();
    QCOMPARE(mEditor->content(), text2);
    mEditor->redo();
    QCOMPARE(mEditor->content(), text3);
    QVERIFY(!mEditor->canRedo());
    QVERIFY(mEditor->modified());

    //undo again
    mEditor->undo();
    QCOMPARE(mEditor->content(), text2);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());
}

void TestEditorSymbolCompletion::test_input_parenthesis1()
{
    QStringList text{
        "int x=",
    };
    QStringList text1{
        "int x=()",
    };
    QStringList text2{
        "int x=(())",
    };
    QStringList text3{
        "int x=(())()",
    };
    QStringList text4{
        "int x=(())();",
    };
    mEditor->setContent(text);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(),'(');
    QCOMPARE(mEditor->content(), text1);
    QTest::keyPress(mEditor.get(),'(');
    QCOMPARE(mEditor->content(), text2);
    QTest::keyPress(mEditor.get(),')'); //auto skip ')'
    QCOMPARE(mEditor->content(), text2);
    QTest::keyPress(mEditor.get(),')');
    QCOMPARE(mEditor->content(), text2); //auto skip ')'
    QTest::keyPress(mEditor.get(),'(');
    QCOMPARE(mEditor->content(), text3);
    QTest::keyPress(mEditor.get(),')'); //auto skip ')'
    QCOMPARE(mEditor->content(), text3);
    QTest::keyPress(mEditor.get(),';');
    QCOMPARE(mEditor->content(), text4);

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text3);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text2);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());

    //redo
    mEditor->redo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->redo();
    QCOMPARE(mEditor->content(), text2);
    mEditor->redo();
    QCOMPARE(mEditor->content(), text3);
    mEditor->redo();
    QCOMPARE(mEditor->content(), text4);
    QVERIFY(!mEditor->canRedo());
    QVERIFY(mEditor->modified());


    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text3);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text2);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());
}

void TestEditorSymbolCompletion::test_input_parenthesis_in_string()
{
    QStringList text{
        "\"int x=\"",
    };
    QStringList text1{
        "\"int x=(\"",
    };
    QStringList text2{
        "\"int x=((\"",
    };
    QStringList text3{
        "\"int x=(()\"",
    };
    QStringList text4{
        "\"int x=(())\"",
    };
    mEditor->setContent(text);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(),Qt::Key_Left);
    QTest::keyPress(mEditor.get(),'(');
    QCOMPARE(mEditor->content(), text1);
    QTest::keyPress(mEditor.get(),'(');
    QCOMPARE(mEditor->content(), text2);
    QTest::keyPress(mEditor.get(),')');
    QCOMPARE(mEditor->content(), text3);
    QTest::keyPress(mEditor.get(),')');
    QCOMPARE(mEditor->content(), text4);

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());

    //redo
    mEditor->redo();
    QCOMPARE(mEditor->content(), text4);
    QVERIFY(!mEditor->canRedo());
    QVERIFY(mEditor->modified());


    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());
}

void TestEditorSymbolCompletion::test_input_parenthesis_in_comment()
{
    QStringList text{
        "/* int x=",
    };
    QStringList text1{
        "/* int x=(",
    };
    QStringList text2{
        "/* int x=((",
    };
    QStringList text3{
        "/* int x=(()",
    };
    QStringList text4{
        "/* int x=(())",
    };
    mEditor->setContent(text);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(),'(');
    QCOMPARE(mEditor->content(), text1);
    QTest::keyPress(mEditor.get(),'(');
    QCOMPARE(mEditor->content(), text2);
    QTest::keyPress(mEditor.get(),')');
    QCOMPARE(mEditor->content(), text3);
    QTest::keyPress(mEditor.get(),')');
    QCOMPARE(mEditor->content(), text4);

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());

    //redo
    mEditor->redo();
    QCOMPARE(mEditor->content(), text4);
    QVERIFY(!mEditor->canRedo());
    QVERIFY(mEditor->modified());

    //undo again
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());
}

void TestEditorSymbolCompletion::test_input_parenthesis_in_escaping_sequence()
{
    QStringList text{
        "\"int x=\\)",
    };
    QStringList text1{
        "\"int x=\\()",
    };
    QStringList text2{
        "\"int x=\\())",
    };
    QStringList text3{
        "\"int x=\\()ab)",
    };
    mEditor->setContent(text);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(),Qt::Key_Left);
    QTest::keyPress(mEditor.get(),'(');
    QCOMPARE(mEditor->content(), text1);
    QTest::keyPress(mEditor.get(),')');
    QCOMPARE(mEditor->content(), text2);
    QTest::keyPress(mEditor.get(),'a');
    QTest::keyPress(mEditor.get(),'b');
    QCOMPARE(mEditor->content(), text3);

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text2);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());

    //redo
    mEditor->redo();
    QCOMPARE(mEditor->content(), text2);
    mEditor->redo();
    QCOMPARE(mEditor->content(), text3);
    QVERIFY(!mEditor->canRedo());
    QVERIFY(mEditor->modified());

    //undo again
    mEditor->undo();
    QCOMPARE(mEditor->content(), text2);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());
}

void TestEditorSymbolCompletion::test_input_parenthesis_on_selection()
{
    QStringList text{
        "\"int x=abc;",
    };
    QStringList text1{
        "\"int x=(abc);",
    };
    QStringList text2{
        "\"int x=(abc)++;",
    };
    mEditor->setContent(text);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(), Qt::Key_Left);
    QTest::keyPress(mEditor.get(), Qt::Key_Left, Qt::ShiftModifier);
    QTest::keyPress(mEditor.get(), Qt::Key_Left, Qt::ShiftModifier);
    QTest::keyPress(mEditor.get(), Qt::Key_Left, Qt::ShiftModifier);
    QTest::keyPress(mEditor.get(), '(');
    QCOMPARE(mEditor->content(), text1);
    QTest::keyPress(mEditor.get(),'+');
    QTest::keyPress(mEditor.get(),'+');
    QCOMPARE(mEditor->content(), text2);

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());

    //redo
    mEditor->redo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->redo();
    QCOMPARE(mEditor->content(), text2);
    QVERIFY(!mEditor->canRedo());
    QVERIFY(mEditor->modified());

    //undo again
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());
}

void TestEditorSymbolCompletion::test_input_brackets1()
{
    QStringList text{
        "int x=",
    };
    QStringList text1{
        "int x=[]",
    };
    QStringList text2{
        "int x=[[]]",
    };
    QStringList text3{
        "int x=[[]][]",
    };
    QStringList text4{
        "int x=[[]][];",
    };
    mEditor->setContent(text);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(),'[');
    QCOMPARE(mEditor->content(), text1);
    QTest::keyPress(mEditor.get(),'[');
    QCOMPARE(mEditor->content(), text2);
    QTest::keyPress(mEditor.get(),']'); //auto skip ']'
    QCOMPARE(mEditor->content(), text2);
    QTest::keyPress(mEditor.get(),']');
    QCOMPARE(mEditor->content(), text2); //auto skip ']'
    QTest::keyPress(mEditor.get(),'[');
    QCOMPARE(mEditor->content(), text3);
    QTest::keyPress(mEditor.get(),']'); //auto skip ']'
    QCOMPARE(mEditor->content(), text3);
    QTest::keyPress(mEditor.get(),';');
    QCOMPARE(mEditor->content(), text4);

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text3);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text2);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());

    //redo
    mEditor->redo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->redo();
    QCOMPARE(mEditor->content(), text2);
    mEditor->redo();
    QCOMPARE(mEditor->content(), text3);
    mEditor->redo();
    QCOMPARE(mEditor->content(), text4);
    QVERIFY(!mEditor->canRedo());
    QVERIFY(mEditor->modified());


    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text3);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text2);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());
}


void TestEditorSymbolCompletion::test_input_brackets_in_string()
{
    QStringList text{
        "\"int x=\"",
    };
    QStringList text1{
        "\"int x=[\"",
    };
    QStringList text2{
        "\"int x=[[\"",
    };
    QStringList text3{
        "\"int x=[[]\"",
    };
    QStringList text4{
        "\"int x=[[]]\"",
    };
    mEditor->setContent(text);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(),Qt::Key_Left);
    QTest::keyPress(mEditor.get(),'[');
    QCOMPARE(mEditor->content(), text1);
    QTest::keyPress(mEditor.get(),'[');
    QCOMPARE(mEditor->content(), text2);
    QTest::keyPress(mEditor.get(),']');
    QCOMPARE(mEditor->content(), text3);
    QTest::keyPress(mEditor.get(),']');
    QCOMPARE(mEditor->content(), text4);

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());

    //redo
    mEditor->redo();
    QCOMPARE(mEditor->content(), text4);
    QVERIFY(!mEditor->canRedo());
    QVERIFY(mEditor->modified());


    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());
}

void TestEditorSymbolCompletion::test_input_brackets_in_comment()
{
    QStringList text{
        "/* int x=",
    };
    QStringList text1{
        "/* int x=[",
    };
    QStringList text2{
        "/* int x=[[",
    };
    QStringList text3{
        "/* int x=[[]",
    };
    QStringList text4{
        "/* int x=[[]]",
    };
    mEditor->setContent(text);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(),'[');
    QCOMPARE(mEditor->content(), text1);
    QTest::keyPress(mEditor.get(),'[');
    QCOMPARE(mEditor->content(), text2);
    QTest::keyPress(mEditor.get(),']');
    QCOMPARE(mEditor->content(), text3);
    QTest::keyPress(mEditor.get(),']');
    QCOMPARE(mEditor->content(), text4);

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());

    //redo
    mEditor->redo();
    QCOMPARE(mEditor->content(), text4);
    QVERIFY(!mEditor->canRedo());
    QVERIFY(mEditor->modified());

    //undo again
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());
}

void TestEditorSymbolCompletion::test_input_brackets_in_escaping_sequence()
{
    QStringList text{
        "\"int x=\\]",
    };
    QStringList text1{
        "\"int x=\\[]",
    };
    QStringList text2{
        "\"int x=\\[]]",
    };
    QStringList text3{
        "\"int x=\\[]ab]",
    };
    mEditor->setContent(text);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(),Qt::Key_Left);
    QTest::keyPress(mEditor.get(),'[');
    QCOMPARE(mEditor->content(), text1);
    QTest::keyPress(mEditor.get(),']');
    QCOMPARE(mEditor->content(), text2);
    QTest::keyPress(mEditor.get(),'a');
    QTest::keyPress(mEditor.get(),'b');
    QCOMPARE(mEditor->content(), text3);

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text2);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());

    //redo
    mEditor->redo();
    QCOMPARE(mEditor->content(), text2);
    mEditor->redo();
    QCOMPARE(mEditor->content(), text3);
    QVERIFY(!mEditor->canRedo());
    QVERIFY(mEditor->modified());

    //undo again
    mEditor->undo();
    QCOMPARE(mEditor->content(), text2);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());
}

void TestEditorSymbolCompletion::test_input_brackets_on_selection()
{
    QStringList text{
        "\"int x=abc;",
    };
    QStringList text1{
        "\"int x=[abc];",
    };
    QStringList text2{
        "\"int x=[abc]++;",
    };
    mEditor->setContent(text);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(), Qt::Key_Left);
    QTest::keyPress(mEditor.get(), Qt::Key_Left, Qt::ShiftModifier);
    QTest::keyPress(mEditor.get(), Qt::Key_Left, Qt::ShiftModifier);
    QTest::keyPress(mEditor.get(), Qt::Key_Left, Qt::ShiftModifier);
    QTest::keyPress(mEditor.get(), '[');
    QCOMPARE(mEditor->content(), text1);
    QTest::keyPress(mEditor.get(),'+');
    QTest::keyPress(mEditor.get(),'+');
    QCOMPARE(mEditor->content(), text2);

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());

    //redo
    mEditor->redo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->redo();
    QCOMPARE(mEditor->content(), text2);
    QVERIFY(!mEditor->canRedo());
    QVERIFY(mEditor->modified());

    //undo again
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());
}

void TestEditorSymbolCompletion::test_input_braces1()
{
    QStringList text{
        "int x=",
    };
    QStringList text1{
        "int x={};",
    };
    QStringList text2{
        "int x={{}};",
    };
    QStringList text3{
        "int x={{}}{};",
    };
    QStringList text4{
        "int x={{}}{};ab",
    };
    mEditor->setContent(text);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(),'{');
    QCOMPARE(mEditor->content(), text1);
    QTest::keyPress(mEditor.get(),'{');
    QCOMPARE(mEditor->content(), text2);
    QTest::keyPress(mEditor.get(),'}'); //auto skip '}'
    QCOMPARE(mEditor->content(), text2);
    QTest::keyPress(mEditor.get(),'}');
    QCOMPARE(mEditor->content(), text2); //auto skip '}'
    QTest::keyPress(mEditor.get(),'{');
    QCOMPARE(mEditor->content(), text3);
    QTest::keyPress(mEditor.get(),'}'); //auto skip '}'
    QCOMPARE(mEditor->content(), text3);
    QTest::keyPress(mEditor.get(),';');
    QCOMPARE(mEditor->content(), text3); //auto skip ';'
    QTest::keyPress(mEditor.get(),'a');
    QTest::keyPress(mEditor.get(),'b');
    QCOMPARE(mEditor->content(), text4);

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text3);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text2);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());

    //redo
    mEditor->redo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->redo();
    QCOMPARE(mEditor->content(), text2);
    mEditor->redo();
    QCOMPARE(mEditor->content(), text3);
    mEditor->redo();
    QCOMPARE(mEditor->content(), text4);
    QVERIFY(!mEditor->canRedo());
    QVERIFY(mEditor->modified());


    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text3);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text2);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());
}


void TestEditorSymbolCompletion::test_input_braces_in_string()
{
    QStringList text{
        "\"int x=\"",
    };
    QStringList text1{
        "\"int x={\"",
    };
    QStringList text2{
        "\"int x={{\"",
    };
    QStringList text3{
        "\"int x={{}\"",
    };
    QStringList text4{
        "\"int x={{}}\"",
    };
    mEditor->setContent(text);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(),Qt::Key_Left);
    QTest::keyPress(mEditor.get(),'{');
    QCOMPARE(mEditor->content(), text1);
    QTest::keyPress(mEditor.get(),'{');
    QCOMPARE(mEditor->content(), text2);
    QTest::keyPress(mEditor.get(),'}');
    QCOMPARE(mEditor->content(), text3);
    QTest::keyPress(mEditor.get(),'}');
    QCOMPARE(mEditor->content(), text4);

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());

    //redo
    mEditor->redo();
    QCOMPARE(mEditor->content(), text4);
    QVERIFY(!mEditor->canRedo());
    QVERIFY(mEditor->modified());


    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());
}

void TestEditorSymbolCompletion::test_input_braces_in_comment()
{
    QStringList text{
        "/* int x=",
    };
    QStringList text1{
        "/* int x={",
    };
    QStringList text2{
        "/* int x={{",
    };
    QStringList text3{
        "/* int x={{}",
    };
    QStringList text4{
        "/* int x={{}}",
    };
    mEditor->setContent(text);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(),'{');
    QCOMPARE(mEditor->content(), text1);
    QTest::keyPress(mEditor.get(),'{');
    QCOMPARE(mEditor->content(), text2);
    QTest::keyPress(mEditor.get(),'}');
    QCOMPARE(mEditor->content(), text3);
    QTest::keyPress(mEditor.get(),'}');
    QCOMPARE(mEditor->content(), text4);

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());

    //redo
    mEditor->redo();
    QCOMPARE(mEditor->content(), text4);
    QVERIFY(!mEditor->canRedo());
    QVERIFY(mEditor->modified());

    //undo again
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());
}

void TestEditorSymbolCompletion::test_input_braces_in_escaping_sequence()
{
    QStringList text{
        "\"int x=\\}",
    };
    QStringList text1{
        "\"int x=\\{}",
    };
    QStringList text2{
        "\"int x=\\{}}",
    };
    QStringList text3{
        "\"int x=\\{}ab}",
    };
    mEditor->setContent(text);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(),Qt::Key_Left);
    QTest::keyPress(mEditor.get(),'{');
    QCOMPARE(mEditor->content(), text1);
    QTest::keyPress(mEditor.get(),'}');
    QCOMPARE(mEditor->content(), text2);
    QTest::keyPress(mEditor.get(),'a');
    QTest::keyPress(mEditor.get(),'b');
    QCOMPARE(mEditor->content(), text3);

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text2);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());

    //redo
    mEditor->redo();
    QCOMPARE(mEditor->content(), text2);
    mEditor->redo();
    QCOMPARE(mEditor->content(), text3);
    QVERIFY(!mEditor->canRedo());
    QVERIFY(mEditor->modified());

    //undo again
    mEditor->undo();
    QCOMPARE(mEditor->content(), text2);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());
}

void TestEditorSymbolCompletion::test_input_braces_on_selection()
{
    QStringList text{
        "\"int x=abc;",
    };
    QStringList text1{
        "\"int x={abc};",
    };
    QStringList text2{
        "\"int x={abc}++;",
    };
    mEditor->setContent(text);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(), Qt::Key_Left);
    QTest::keyPress(mEditor.get(), Qt::Key_Left, Qt::ShiftModifier);
    QTest::keyPress(mEditor.get(), Qt::Key_Left, Qt::ShiftModifier);
    QTest::keyPress(mEditor.get(), Qt::Key_Left, Qt::ShiftModifier);
    QTest::keyPress(mEditor.get(), '{');
    QCOMPARE(mEditor->content(), text1);
    QTest::keyPress(mEditor.get(),'+');
    QTest::keyPress(mEditor.get(),'+');
    QCOMPARE(mEditor->content(), text2);

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());

    //redo
    mEditor->redo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->redo();
    QCOMPARE(mEditor->content(), text2);
    QVERIFY(!mEditor->canRedo());
    QVERIFY(mEditor->modified());

    //undo again
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());
}

void TestEditorSymbolCompletion::test_input_braces_with_indent()
{
    QStringList text{
        "int main() {",
        "\tif (x>0) ",
        "\t\tx=100;",
        "}"
    };
    QStringList text1{
        "int main() {",
        "\tif (x>0) ",
        "\t{}x=100;",
        "}"
    };
    QStringList text2{
        "int main() {",
        "\tif (x>0) ",
        "\t{ab}x=100;",
        "}"
    };
    mEditor->setContent(text);
    mEditor->setCaretXY({2,2});
    QTest::keyPress(mEditor.get(), '{');
    QCOMPARE(mEditor->content(), text1);
    QTest::keyPress(mEditor.get(),'a');
    QTest::keyPress(mEditor.get(),'b');
    QCOMPARE(mEditor->content(), text2);

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());

    //redo
    mEditor->redo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->redo();
    QCOMPARE(mEditor->content(), text2);
    QVERIFY(!mEditor->canRedo());
    QVERIFY(mEditor->modified());

    //undo again
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());
}

void TestEditorSymbolCompletion::test_input_asterisk_for_ansi_c_comments()
{
    QStringList text{
        "  /",
    };
    QStringList text1{
        "  /**/",
    };
    QStringList text2{
        "  /*++*/",
    };
    mEditor->setContent(text);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(),'*');
    QCOMPARE(mEditor->content(), text1);
    QTest::keyPress(mEditor.get(),'+');
    QTest::keyPress(mEditor.get(),'+');
    QCOMPARE(mEditor->content(), text2);

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());

    //redo
    mEditor->redo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->redo();
    QCOMPARE(mEditor->content(), text2);
    QVERIFY(!mEditor->canRedo());
    QVERIFY(mEditor->modified());

    //undo again
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());
}

void TestEditorSymbolCompletion::test_input_asterisk_in_comments()
{
    QStringList text{
        "\"  /",
    };
    QStringList text1{
        "\"  /*",
    };
    mEditor->setContent(text);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(),'*');
    QCOMPARE(mEditor->content(), text1);

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());

    //redo
    mEditor->redo();
    QCOMPARE(mEditor->content(), text1);
    QVERIFY(!mEditor->canRedo());
    QVERIFY(mEditor->modified());

    //undo again
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());
}

void TestEditorSymbolCompletion::test_input_asterisk_in_comments2()
{
    QStringList text{
        "/*  /",
    };
    QStringList text1{
        "/*  /*",
    };
    mEditor->setContent(text);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(),'*');
    QCOMPARE(mEditor->content(), text1);
}

void TestEditorSymbolCompletion::test_input_asterisk_in_char_literal()
{
    QStringList text{
        "'/",
    };
    QStringList text1{
        "'/*",
    };
    mEditor->setContent(text);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(),'*');
    QCOMPARE(mEditor->content(), text1);

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());

    //redo
    mEditor->redo();
    QCOMPARE(mEditor->content(), text1);
    QVERIFY(!mEditor->canRedo());
    QVERIFY(mEditor->modified());

    //undo again
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());
}

void TestEditorSymbolCompletion::test_input_asterisk_in_string_literal()
{
    QStringList text{
        "\"/",
    };
    QStringList text1{
        "\"/*",
    };
    mEditor->setContent(text);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(),'*');
    QCOMPARE(mEditor->content(), text1);

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());

    //redo
    mEditor->redo();
    QCOMPARE(mEditor->content(), text1);
    QVERIFY(!mEditor->canRedo());
    QVERIFY(mEditor->modified());

    //undo again
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());
}

void TestEditorSymbolCompletion::test_input_asterisk_in_rawstring_begin()
{
    QStringList text{
        "R\"/",
    };
    QStringList text1{
        "R\"/*",
    };
    mEditor->setContent(text);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(),'*');
    QCOMPARE(mEditor->content(), text1);

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());

    //redo
    mEditor->redo();
    QCOMPARE(mEditor->content(), text1);
    QVERIFY(!mEditor->canRedo());
    QVERIFY(mEditor->modified());

    //undo again
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());
}

void TestEditorSymbolCompletion::test_input_asterisk_in_rawstring()
{
    QStringList text{
        "R\"(/)\"",
    };
    QStringList text1{
        "R\"(/*)\"",
    };
    mEditor->setContent(text);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(),Qt::Key_Left);
    QTest::keyPress(mEditor.get(),Qt::Key_Left);
    QTest::keyPress(mEditor.get(),'*');
    QCOMPARE(mEditor->content(), text1);

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());

    //redo
    mEditor->redo();
    QCOMPARE(mEditor->content(), text1);
    QVERIFY(!mEditor->canRedo());
    QVERIFY(mEditor->modified());

    //undo again
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());

}

void TestEditorSymbolCompletion::test_input_asterisk_in_rawstring_end()
{
    QStringList text{
        "R\"/()/\"",
    };
    QStringList text1{
        "R\"/()/*\"",
    };
    mEditor->setContent(text);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(),Qt::Key_Left);
    QTest::keyPress(mEditor.get(),'*');
    QCOMPARE(mEditor->content(), text1);

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());

    //redo
    mEditor->redo();
    QCOMPARE(mEditor->content(), text1);
    QVERIFY(!mEditor->canRedo());
    QVERIFY(mEditor->modified());

    //undo again
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());
}

void TestEditorSymbolCompletion::test_input_periods1()
{
    QStringList text{
        "int x=10;",
    };
    QStringList text1{
        "int x=10;ab",
    };
    QStringList text2{
        "int x=10;ab;",
    };
    mEditor->setContent(text);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(),Qt::Key_Left);
    QTest::keyPress(mEditor.get(),';');
    QCOMPARE(mEditor->content(), text); // auto skip ;
    QTest::keyPress(mEditor.get(),'a');
    QTest::keyPress(mEditor.get(),'b');
    QCOMPARE(mEditor->content(), text1);
    QTest::keyPress(mEditor.get(),';');
    QCOMPARE(mEditor->content(), text2);

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());

    //redo
    mEditor->redo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->redo();
    QCOMPARE(mEditor->content(), text2);
    QVERIFY(!mEditor->canRedo());
    QVERIFY(mEditor->modified());

    //undo again
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());
}

void TestEditorSymbolCompletion::test_input_periods_in_comments()
{
    QStringList text{
        "// int x=10;",
    };
    QStringList text1{
        "// int x=10;;",
    };
    QStringList text2{
        "// int x=10;ab;",
    };
    mEditor->setContent(text);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(),Qt::Key_Left);
    QTest::keyPress(mEditor.get(),';');
    QCOMPARE(mEditor->content(), text1);
    QTest::keyPress(mEditor.get(),'a');
    QTest::keyPress(mEditor.get(),'b');
    QCOMPARE(mEditor->content(), text2);

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());

    //redo
    mEditor->redo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->redo();
    QCOMPARE(mEditor->content(), text2);
    QVERIFY(!mEditor->canRedo());
    QVERIFY(mEditor->modified());

    //undo again
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());
}

void TestEditorSymbolCompletion::test_input_periods_in_string()
{
    QStringList text{
        "\" int x=10;",
    };
    QStringList text1{
        "\" int x=10;;",
    };
    QStringList text2{
        "\" int x=10;ab;",
    };
    mEditor->setContent(text);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(),Qt::Key_Left);
    QTest::keyPress(mEditor.get(),';');
    QCOMPARE(mEditor->content(), text1);
    QTest::keyPress(mEditor.get(),'a');
    QTest::keyPress(mEditor.get(),'b');
    QCOMPARE(mEditor->content(), text2);

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());

    //redo
    mEditor->redo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->redo();
    QCOMPARE(mEditor->content(), text2);
    QVERIFY(!mEditor->canRedo());
    QVERIFY(mEditor->modified());

    //undo again
    mEditor->undo();
    QCOMPARE(mEditor->content(), text1);
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());
}

void TestEditorSymbolCompletion::test_input_docstring()
{
    QStringList text1{
        "/***/",
    };
    QStringList text2{
        "/**",
        " * ",
        " */",
    };
    QStringList text3{
        "/**",
        " * ",
        " * ",
        " */",
    };
    mEditor->clear();
    QTest::keyPress(mEditor.get(),'/');
    QTest::keyPress(mEditor.get(),'*');
    QTest::keyPress(mEditor.get(),'*');
    QCOMPARE(mEditor->content(),text1);
    QTest::keyPress(mEditor.get(),Qt::Key_Enter);
    QCOMPARE(mEditor->content(),text2);
    QTest::keyPress(mEditor.get(),Qt::Key_Escape);
    QTest::keyPress(mEditor.get(),Qt::Key_Enter);
    QCOMPARE(mEditor->content(),text3);

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(),text2);
    mEditor->undo();
    QCOMPARE(mEditor->content(),text1);
    mEditor->undo();
    mEditor->undo();
    mEditor->undo();
    QVERIFY(mEditor->empty());
    QVERIFY(!mEditor->canUndo());

    //redo
    mEditor->redo();
    mEditor->redo();
    mEditor->redo();
    QCOMPARE(mEditor->content(),text1);
    mEditor->redo();
    QCOMPARE(mEditor->content(),text2);
    mEditor->redo();
    QCOMPARE(mEditor->content(),text3);
    QVERIFY(!mEditor->canRedo());

    //undo
    mEditor->undo();
    QCOMPARE(mEditor->content(),text2);
    mEditor->undo();
    QCOMPARE(mEditor->content(),text1);
    mEditor->undo();
    mEditor->undo();
    mEditor->undo();
    QVERIFY(mEditor->empty());
    QVERIFY(!mEditor->canUndo());

}

void TestEditorSymbolCompletion::test_input_after_cpp_comment()
{
    QStringList text1{
        "//abc",
        "",
    };
    QStringList text2{
        "//abc",
        "{}",
    };
    mEditor->clear();
    mEditor->setContent(text1);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(),'{');
    QCOMPARE(mEditor->content(),text2);

}


void TestEditorSymbolCompletion::test_read_only_symbol_completion_data()
{
    QTest::addColumn<int>("key");
    for (int key : {int('('), int('['), int('{'), int('\''), int('"'), int('*'), int(Qt::Key_Delete), int(Qt::Key_Backspace)})
        QTest::newRow(qPrintable(QString::number(key))) << key;
}

void TestEditorSymbolCompletion::test_read_only_symbol_completion()
{
    QFETCH(int, key);
    init_editor();
    const QStringList text{QString::fromUtf8("int caffè = (42);")};
    mEditor->setContent(text);
    mEditor->setCaretXY({14, 0});
    mEditor->setReadOnly(true);
    const CharPos caret = mEditor->caretXY();
    QTest::keyPress(mEditor.get(), Qt::Key(key));
    QCOMPARE(mEditor->content(), text);
    QCOMPARE(mEditor->caretXY(), caret);
    QVERIFY(!mEditor->modified());
    QVERIFY(!mEditor->canUndo());
}

void TestEditorSymbolCompletion::test_snippet_read_only_preserves_selection()
{
    init_editor();
    const QStringList text{QString::fromUtf8("// αβγ 😀"), "value"};
    mEditor->setContent(text);
    mEditor->setSelBeginEnd({0, 0}, {5, 1});
    const QString selected = mEditor->selText();
    const CharPos caret = mEditor->caretXY();
    mEditor->setReadOnly(true);
    bool macrosCalled = false;
    mEditor->setGetMacroVarsFunc([&] { macrosCalled = true; return QMap<QString, QString>{}; });
    mEditor->insertCodeSnippet("%REPL_BEGIN%changed%REPL_END%%INSERT%");
    QCOMPARE(mEditor->content(), text);
    QCOMPARE(mEditor->selText(), selected);
    QCOMPARE(mEditor->caretXY(), caret);
    QVERIFY(!macrosCalled);
    QVERIFY(!mEditor->modified());
    QVERIFY(!mEditor->canUndo());
}

void TestEditorSymbolCompletion::test_snippet_mixed_tab_stops_follow_source_order()
{
    init_editor();
    mEditor->setContent(QStringList{""});
    mEditor->insertCodeSnippet("func(%REPL_BEGIN%left%REPL_END%, %INSERT%, %REPL_BEGIN%right%REPL_END%);");
    QCOMPARE(mEditor->content(), QStringList{"func(left, , right);"});
    QCOMPARE(mEditor->selText(), QString("left"));
    mEditor->setSelText("replacement");
    QTest::keyPress(mEditor.get(), Qt::Key_Tab);
    QVERIFY(!mEditor->selAvail());
    QCOMPARE(mEditor->caretX(), int(QString("func(replacement, ").size()));
    QTest::keyPress(mEditor.get(), 'x');
    QTest::keyPress(mEditor.get(), Qt::Key_Tab);
    QCOMPARE(mEditor->selText(), QString("right"));
    mEditor->setSelText("ok");
    QTest::keyPress(mEditor.get(), Qt::Key_Tab);
    QVERIFY(!mEditor->selAvail());
    QCOMPARE(mEditor->content(), QStringList{"func(replacement, x, ok);"});
}

void TestEditorSymbolCompletion::test_snippet_multiline_unicode_tab_stops()
{
    init_editor();
    mEditor->setContent(QStringList{""});
    mEditor->insertCodeSnippet(QString::fromUtf8("auto %REPL_BEGIN%caffè%REPL_END% = 0;\n  %INSERT%// 😀\n  %REPL_BEGIN%終わり%REPL_END%"));
    QCOMPARE(mEditor->selText(), QString::fromUtf8("caffè"));
    QTest::keyPress(mEditor.get(), Qt::Key_Tab);
    QCOMPARE(mEditor->caretY(), 1);
    QVERIFY(!mEditor->selAvail());
    QCOMPARE(mEditor->caretX(), mEditor->lineText().indexOf("//"));
    QTest::keyPress(mEditor.get(), Qt::Key_Tab);
    QCOMPARE(mEditor->caretY(), 2);
    QCOMPARE(mEditor->selText(), QString::fromUtf8("終わり"));
    mEditor->undo();
    QCOMPARE(mEditor->content(), QStringList{""});
    QVERIFY(!mEditor->canUndo());
    mEditor->redo();
    QVERIFY(mEditor->content().join('\n').contains(QString::fromUtf8("caffè")));
}

void TestEditorSymbolCompletion::test_snippet_failed_macro_preserves_document()
{
    init_editor();
    const QStringList text{"keep this text"};
    mEditor->setContent(text);
    mEditor->setSelBeginEnd({5, 0}, {9, 0});
    const QString selected = mEditor->selText();
    const CharPos caret = mEditor->caretXY();
    mEditor->setGetMacroVarsFunc([]() -> QMap<QString, QString> { throw std::runtime_error("macro provider failed"); });
    QVERIFY_EXCEPTION_THROWN(mEditor->insertCodeSnippet("<FILENAME>"), std::runtime_error);
    QCOMPARE(mEditor->content(), text);
    QCOMPARE(mEditor->selText(), selected);
    QCOMPARE(mEditor->caretXY(), caret);
    QVERIFY(!mEditor->modified());
    QVERIFY(!mEditor->canUndo());
    mEditor->setGetMacroVarsFunc(nullptr);
    QTest::keyPress(mEditor.get(), 'x');
    QCOMPARE(mEditor->content(), QStringList{"keep x text"});
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
}

void TestEditorSymbolCompletion::test_snippet_replacement_single_undo()
{
    init_editor();
    const QStringList text{QString::fromUtf8("// prima 😀"), "old value", "// after"};
    mEditor->setContent(text);
    mEditor->setSelBeginEnd({0, 1}, {9, 1});
    mEditor->insertCodeSnippet(QString::fromUtf8("const char* message = \"caffè 😀\";\n%INSERT%"));
    const QStringList expanded = mEditor->content();
    QVERIFY(expanded.join('\n').contains(QString::fromUtf8("caffè 😀")));
    QCOMPARE(expanded.first(), text.first());
    QCOMPARE(expanded.last(), text.last());
    mEditor->undo();
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->canUndo());
    QVERIFY(!mEditor->modified());
    mEditor->redo();
    QCOMPARE(mEditor->content(), expanded);
}

void TestEditorSymbolCompletion::test_snippet_prefix_boundaries_and_stale_requests()
{
    init_editor();
    mEditor->setFileType(FileType::CppSource, false);
    CodeSnippetsManager manager(mDirSettings.get());
    auto snippet = std::make_shared<CodeSnippet>();
    snippet->prefix = "std::";
    snippet->code = "std::vector<int> values;";
    snippet->language = "cpp";
    manager.setSnippets({snippet});
    mEditor->setCodeSnippetsManager(&manager);
    mEditor->setContent(QStringList{"identifierstd::"});
    mEditor->setCaretXY(mEditor->fileEnd());
    QVERIFY(mEditor->snippetPrefix() != "std::");
    QVERIFY(!mEditor->expandCodeSnippet(snippet, "std::"));
    QCOMPARE(mEditor->content(), QStringList{"identifierstd::"});
    mEditor->setContent(QStringList{"std::"});
    mEditor->setCaretXY(mEditor->fileEnd());
    QCOMPARE(mEditor->snippetPrefix(), QString("std::"));
    QVERIFY(!mEditor->expandCodeSnippet(snippet, "stale"));
    snippet->enabled = false;
    QVERIFY(!mEditor->expandCodeSnippet(snippet, "std::"));
    snippet->enabled = true;
    snippet->language = "asm";
    QVERIFY(!mEditor->expandCodeSnippet(snippet, "std::"));
    snippet->language = "cpp";
    QVERIFY(mEditor->expandCodeSnippet(snippet, "std::"));
    QCOMPARE(mEditor->content(), QStringList{snippet->code});
    mEditor->undo();
    QCOMPARE(mEditor->content(), QStringList{"std::"});
    QVERIFY(!mEditor->canUndo());
    mEditor->setCodeSnippetsManager(nullptr);
}

void TestEditorSymbolCompletion::test_snippet_partial_prefix_requires_selected_candidate()
{
    init_editor();
    mEditor->setFileType(FileType::CppSource, false);
    auto snippet = std::make_shared<CodeSnippet>();
    snippet->prefix = "class";
    snippet->code = "class Example {};";
    snippet->language = "cpp";
    mEditor->setContent(QStringList{"cl"});
    mEditor->setCaretXY(mEditor->fileEnd());
    QVERIFY(mEditor->expandCodeSnippet(snippet, "cl"));
    QCOMPARE(mEditor->content(), QStringList{snippet->code});
    mEditor->undo();
    QCOMPARE(mEditor->content(), QStringList{"cl"});
    mEditor->setContent(QStringList{QString::fromUtf8("caffè")});
    mEditor->setCaretXY(mEditor->fileEnd());
    QCOMPARE(mEditor->snippetPrefix(), QString::fromUtf8("caffè"));
    QVERIFY(!mEditor->expandCodeSnippet(snippet, QString::fromUtf8("caffè")));
    QCOMPARE(mEditor->content(), QStringList{QString::fromUtf8("caffè")});
}

void TestEditorSymbolCompletion::test_completion_popup_without_parser_data()
{
    QTest::addColumn<int>("type");
    for (auto type : {CodeCompletionType::Normal, CodeCompletionType::Types, CodeCompletionType::FunctionWithoutDefinition,
                      CodeCompletionType::Namespaces, CodeCompletionType::Macros, CodeCompletionType::LiteralOperators, CodeCompletionType::Labels})
        QTest::newRow(qPrintable(QString::number(int(type)))) << int(type);
}

void TestEditorSymbolCompletion::test_completion_popup_without_parser()
{
    QFETCH(int, type);
    CodeCompletionPopup popup(mColorManager.get(), nullptr);
    popup.setShowEditorCaretFunc([] {});
    popup.prepareSearch({}, {}, {}, {"int"}, "unsaved.cpp", 0, CodeCompletionType(type), {});
    QVERIFY(!popup.selectedStatement());
    QVERIFY(!popup.search("int", false, mEditorSettings->colorScheme()));
    QVERIFY(!popup.isVisible());
}

void TestEditorSymbolCompletion::test_completion_popup_keywords_after_cancel()
{
    CodeCompletionPopup popup(mColorManager.get(), nullptr);
    popup.setShowEditorCaretFunc([] {});
    popup.prepareSearch({}, {}, {}, {}, "unsaved.cpp", 0, CodeCompletionType::KeywordsOnly, {"keywordAlpha", "keywordBeta"});
    QVERIFY(popup.search("keywordAlpha", true, mEditorSettings->colorScheme()));
    QVERIFY(popup.selectedStatement());
    QCOMPARE(popup.selectedStatement()->command, QString("keywordAlpha"));
    auto view = popup.findChild<CodeCompletionListView*>();
    QVERIFY(view);
    auto model = qobject_cast<CodeCompletionListModel*>(view->model());
    QVERIFY(model);
    QVERIFY(model->statementIcon(model->index(0, 0), 16).isNull());
    popup.hide();
    QVERIFY(!popup.selectedStatement());
    popup.prepareSearch({}, {}, {}, {}, "unsaved.cpp", 0, CodeCompletionType::KeywordsOnly, {"keywordBeta"});
    QVERIFY(!popup.search("keywordAlpha", false, mEditorSettings->colorScheme()));
    QVERIFY(!popup.selectedStatement());
    QVERIFY(popup.search("keywordBeta", true, mEditorSettings->colorScheme()));
    QCOMPARE(popup.selectedStatement()->command, QString("keywordBeta"));
    popup.hide();
}

void TestEditorSymbolCompletion::test_completion_list_optional_callbacks()
{
    CodeCompletionListView view;
    QStringListModel model({"one", "two"});
    view.setModel(&model);
    view.show();
    QFocusEvent focus(QEvent::FocusIn);
    QApplication::sendEvent(&view, &focus);
    QTest::keyPress(&view, Qt::Key_A);
    view.setCurrentIndex(model.index(0));
    QTest::keyPress(&view, Qt::Key_Down);
    QCOMPARE(view.currentIndex().row(), 1);
    view.hide();
}

void TestEditorSymbolCompletion::test_header_completion_without_parser()
{
    HeaderCompletionPopup popup(mColorManager.get());
    popup.setShowEditorCaretFunc([] {});
    popup.prepareSearch("absent/", "unsaved.cpp");
    QVERIFY(!popup.search("absent/no-match", false, mEditorSettings->colorScheme()));
    QVERIFY(popup.selectedFilename(false).isEmpty());
}

void TestEditorSymbolCompletion::test_header_completion_preserves_include_precedence()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    QDir dir(temp.path());
    for (const QString &name : {QString("local"), QString("project"), QString("system")}) {
        QVERIFY(dir.mkdir(name));
        QFile file(dir.filePath(name + "/common.h"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("// header"), qint64(9));
    }
    auto parser = std::make_shared<CppParser>();
    parser->addProjectIncludePath(dir.filePath("project"));
    parser->addIncludePath(dir.filePath("system"));
    HeaderCompletionPopup popup(mColorManager.get());
    popup.setShowEditorCaretFunc([] {});
    popup.setSuggestionColor(Qt::red, Qt::green, Qt::blue, Qt::yellow);
    popup.setSearchLocal(true);
    popup.setParser(parser);
    popup.prepareSearch("common", dir.filePath("local/test.cpp"));
    QVERIFY(popup.search("common", true, mEditorSettings->colorScheme()));
    const auto view = popup.findChild<CodeCompletionListView*>();
    QVERIFY(view);
    QCOMPARE(view->model()->data(view->model()->index(0, 0), Qt::ForegroundRole).value<QColor>(), QColor(Qt::red));
    popup.hide();
    popup.setParser(parser);
    popup.setSearchLocal(false);
    popup.prepareSearch("common", dir.filePath("local/test.cpp"));
    QVERIFY(popup.search("common", true, mEditorSettings->colorScheme()));
    QCOMPARE(view->model()->data(view->model()->index(0, 0), Qt::ForegroundRole).value<QColor>(), QColor(Qt::green));
    popup.hide();
}


void TestEditorSymbolCompletion::test_failed_save_as_preserves_editor_identity_data()
{
    QTest::addColumn<bool>("newDocument");
    QTest::addColumn<bool>("directoryTarget");
    QTest::newRow("existing-directory") << false << true;
    QTest::newRow("existing-missing-parent") << false << false;
    QTest::newRow("new-directory") << true << true;
    QTest::newRow("new-missing-parent") << true << false;
}

void TestEditorSymbolCompletion::test_failed_save_as_preserves_editor_identity()
{
    QFETCH(bool, newDocument);
    QFETCH(bool, directoryTarget);
    init_editor();
    mEditorSettings->setAutoFormatWhenSaved(false);
    mEditorSettings->setRemoveTrailingSpacesWhenSaved(false);
    mEditorSettings->setSyntaxCheckWhenSave(false);
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString original = temp.filePath("original.cpp");
    const QByteArray originalBytes("int original = 1;");
    if (!newDocument) {
        QFile file(original);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(originalBytes), qint64(originalBytes.size()));
        file.close();
    }
    mEditor->setFilename(original);
    mEditor->setFileType(FileType::CppSource, false);
    if (!newDocument)
        mEditor->loadFile({}, false);
    else
        mEditor->setContent(QStringList{"int original = 1;"});
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(), 'x');
    const QStringList edited = mEditor->content();
    QVERIFY(mEditor->modified());
    QVERIFY(mEditor->canUndo());
    QFileSystemWatcher watcher;
    if (!newDocument)
        QVERIFY(watcher.addPath(original));
    mEditor->setFileSystemWatcher(&watcher);
    QSignalSpy renamed(mEditor.get(), &Editor::fileSaveAsed);
    QSignalSpy saved(mEditor.get(), &Editor::fileSaved);
    QSignalSpy failed(mEditor.get(), &Editor::fileSaveError);
    QTimer closeErrors;
    connect(&closeErrors, &QTimer::timeout, mEditor.get(), [] {
        if (auto box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
            box->accept();
    });
    closeErrors.start(10);
    const QString target = directoryTarget ? temp.path() : temp.filePath("missing/target.c");
    QVERIFY(!mEditor->saveAs(target, false));
    QCOMPARE(mEditor->filename(), original);
    QCOMPARE(mEditor->fileType(), FileType::CppSource);
    QCOMPARE(mEditor->isNew(), newDocument);
    QCOMPARE(mEditor->content(), edited);
    QVERIFY(mEditor->modified());
    QVERIFY(mEditor->canUndo());
    QCOMPARE(renamed.count(), 0);
    QCOMPARE(saved.count(), 0);
    QCOMPARE(failed.count(), 1);
    QCOMPARE(watcher.files().contains(original), !newDocument);
    if (!newDocument) {
        QFile file(original);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), originalBytes);
    }
    const QString retry = temp.filePath("retry.c");
    bool coherentSavedState = false;
    connect(mEditor.get(), &Editor::fileSaved, mEditor.get(), [&](Editor*, const QString &filename) {
        coherentSavedState = filename == retry && mEditor->filename() == retry
                && mEditor->fileType() == FileType::CSource && !mEditor->isNew()
                && !mEditor->modified();
    });
    QVERIFY(mEditor->saveAs(retry, false));
    QCOMPARE(renamed.count(), 1);
    QCOMPARE(saved.count(), 1);
    QCOMPARE(failed.count(), 1);
    QVERIFY(coherentSavedState);
    QVERIFY(watcher.files().contains(retry));
    QVERIFY(!watcher.files().contains(original));
    QFile file(retry);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QVERIFY(file.readAll().contains("int original = 1;x"));
    mEditor->setFileSystemWatcher(nullptr);
}

void TestEditorSymbolCompletion::test_failed_encoding_conversion_preserves_document()
{
    init_editor();
    mEditorSettings->setAutoFormatWhenSaved(false);
    mEditorSettings->setRemoveTrailingSpacesWhenSaved(false);
    mEditorSettings->setSyntaxCheckWhenSave(false);
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString original = temp.filePath("encoding.cpp");
    const QByteArray bytes = QString::fromUtf8("// caffè 😀\nint value = 1;").toUtf8();
    QFile file(original);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), qint64(bytes.size()));
    file.close();
    mEditor->setFilename(original);
    mEditor->setFileType(FileType::CppSource, false);
    mEditor->loadFile({}, false);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(), 'x');
    const QStringList text = mEditor->content();
    const QByteArray encoding = mEditor->editorEncoding();
    const QByteArray fileEncoding = mEditor->fileEncoding();
    QSignalSpy changed(mEditor.get(), &Editor::editorEncodingChanged);
    QSignalSpy failed(mEditor.get(), &Editor::fileSaveError);
    QTimer closeErrors;
    connect(&closeErrors, &QTimer::timeout, mEditor.get(), [] {
        if (auto box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
            box->accept();
    });
    closeErrors.start(10);
    mEditor->convertToEncoding("ISO-8859-1");
    QCOMPARE(failed.count(), 1);
    QCOMPARE(changed.count(), 0);
    QCOMPARE(mEditor->filename(), original);
    QCOMPARE(mEditor->editorEncoding(), encoding);
    QCOMPARE(mEditor->fileEncoding(), fileEncoding);
    QCOMPARE(mEditor->content(), text);
    QVERIFY(mEditor->modified());
    QVERIFY(mEditor->canUndo());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), bytes);
    file.close();
    mEditor->undo();
    QVERIFY(!mEditor->content().last().endsWith('x'));
    mEditor->redo();
    QCOMPARE(mEditor->content(), text);
    // Retry using the original codec after the recoverable conversion failure.
    QVERIFY(mEditor->save(false, false));
    QVERIFY(!mEditor->modified());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QVERIFY(file.readAll().contains("int value = 1;x"));
}


void TestEditorSymbolCompletion::test_encoding_reload_cancel_preserves_edits()
{
    init_editor();
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString original = temp.filePath("reload.cpp");
    QFile file(original);
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray bytes = QString::fromUtf8("// caffè 😀").toUtf8();
    QCOMPARE(file.write(bytes), qint64(bytes.size()));
    file.close();
    mEditor->setFilename(original);
    mEditor->setFileType(FileType::CppSource, false);
    mEditor->loadFile({}, false);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(), 'x');
    const QStringList text = mEditor->content();
    const QByteArray encoding = mEditor->editorEncoding();
    const QByteArray fileEncoding = mEditor->fileEncoding();
    const CharPos caret = mEditor->caretXY();
    QSignalSpy changed(mEditor.get(), &Editor::editorEncodingChanged);
    QTimer cancelReload;
    connect(&cancelReload, &QTimer::timeout, mEditor.get(), [] {
        if (auto box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
            box->done(QMessageBox::No);
    });
    cancelReload.start(10);
    mEditor->setEditorEncoding("ISO-8859-1");
    QCOMPARE(changed.count(), 0);
    QCOMPARE(mEditor->editorEncoding(), encoding);
    QCOMPARE(mEditor->fileEncoding(), fileEncoding);
    QCOMPARE(mEditor->content(), text);
    QCOMPARE(mEditor->caretXY(), caret);
    QVERIFY(mEditor->modified());
    QVERIFY(mEditor->canUndo());
    mEditor->undo();
    QVERIFY(!mEditor->modified());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), bytes);
}

void TestEditorSymbolCompletion::test_encoding_reload_failure_preserves_state()
{
    init_editor();
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString original = temp.filePath("reload.cpp");
    QFile file(original);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("int value = 1;"), qint64(14));
    file.close();
    mEditor->setFilename(original);
    mEditor->setFileType(FileType::CppSource, false);
    mEditor->loadFile({}, false);
    const QStringList text = mEditor->content();
    const QByteArray encoding = mEditor->editorEncoding();
    const QByteArray fileEncoding = mEditor->fileEncoding();
    mEditor->setFilename(temp.filePath("missing.cpp"));
    QSignalSpy changed(mEditor.get(), &Editor::editorEncodingChanged);
    QTimer closeErrors;
    connect(&closeErrors, &QTimer::timeout, mEditor.get(), [] {
        if (auto box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
            box->accept();
    });
    closeErrors.start(10);
    mEditor->setEditorEncoding("ISO-8859-1");
    QCOMPARE(changed.count(), 0);
    QCOMPARE(mEditor->editorEncoding(), encoding);
    QCOMPARE(mEditor->fileEncoding(), fileEncoding);
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->modified());
    QVERIFY(!mEditor->isNew());
    mEditor->setFilename(original);
    mEditor->setEditorEncoding(ENCODING_UTF8_BOM);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(mEditor->content(), text);
    QVERIFY(!mEditor->modified());
}


void TestEditorSymbolCompletion::test_save_as_encoding_observer_sees_committed_identity()
{
    init_editor();
    mEditorSettings->setAutoFormatWhenSaved(false);
    mEditorSettings->setRemoveTrailingSpacesWhenSaved(false);
    mEditorSettings->setSyntaxCheckWhenSave(false);
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString original = temp.filePath("identity.cpp");
    QFile source(original);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write("int original = 1;"), qint64(17));
    source.close();
    mEditor->setFilename(original);
    mEditor->setFileType(FileType::CppSource, false);
    // New documents choose an output codec without reloading an existing file.
    mEditor->setContent(QStringList{"int original = 1;"});
    QVERIFY(mEditor->isNew());
    mEditor->setEditorEncoding(ENCODING_UTF8_BOM);
    mEditor->setCaretXY(mEditor->fileEnd());
    QTest::keyPress(mEditor.get(), 'x');
    QFileSystemWatcher watcher;
    QVERIFY(watcher.addPath(original));
    mEditor->setFileSystemWatcher(&watcher);
    const QString destination = temp.filePath("identity.c");
    QSignalSpy changed(mEditor.get(), &Editor::fileEncodingChanged);
    bool coherent = false;
    connect(mEditor.get(), &Editor::fileEncodingChanged, mEditor.get(), [&](Editor*) {
        coherent = mEditor->filename() == destination && mEditor->fileType() == FileType::CSource
                && !mEditor->isNew() && !mEditor->modified()
                && watcher.files().contains(destination) && !watcher.files().contains(original);
        QFile committed(destination);
        coherent = coherent && committed.open(QIODevice::ReadOnly)
                && committed.read(3) == QByteArray::fromHex("efbbbf");
    });
    QVERIFY(mEditor->saveAs(destination, false));
    QCOMPARE(changed.count(), 1);
    QVERIFY(coherent);
    QCOMPARE(mEditor->fileEncoding(), QByteArray(ENCODING_UTF8_BOM));
    QVERIFY(source.open(QIODevice::ReadOnly));
    QCOMPARE(source.readAll(), QByteArray("int original = 1;"));
    mEditor->setFileSystemWatcher(nullptr);
}
