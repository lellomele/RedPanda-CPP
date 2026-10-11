#include <QTest>
#include <QApplication>
#include "test_charpos.h"
#include "test_keystrokes.h"
#include "test_cppsyntaxer.h"
#include "test_document.h"
#include "test_qsynedit_cpp.h"
#include "test_qsynedit_emoji.h"
#include "test_qsynedit_cpp_search_replace.h"
#include "../../../tests/qtest_suite_runner.h"
int main(int argc, char **argv) {
    QTest::setMainSourcePath(__FILE__, QT_TESTCASE_BUILDDIR);
    QApplication app(argc,argv);
    using namespace TestSuites;
    return run(argc,argv,{suite<QSynedit::TestKeyStrokes>("keystrokes"),suite<QSynedit::TestCharPos>("charpos"),suite<QSynedit::TestDocumentHelpers>("document-helpers"),suite<QSynedit::TestDocument>("document"),suite<TestCppSyntaxer>("syntaxer"),suite<QSynedit::TestQSyneditCpp>("cpp-editing"),suite<QSynedit::TestQSyneditEmoji>("emoji"),suite<QSynedit::TestQSyneditCppSearchReplace>("search-replace")});
}
