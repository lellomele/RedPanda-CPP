#include <QTest>
#include <QApplication>
#include "test_cppparser.h"
#include "test_parser_configuration_queue.h"
#include "test_semanticsnapshot.h"
#include "test_semanticpublication.h"
#include "test_cpppreprocessor.h"
#include "test_cpptokenizer.h"
#include "../../tests/qtest_suite_runner.h"
int main(int argc, char **argv) {
    QTest::setMainSourcePath(__FILE__, QT_TESTCASE_BUILDDIR);
    QApplication app(argc, argv);
    if (const int helper = runParserConfigurationQueueHelper(); helper >= 0)
        return helper;
    initParser();
    using namespace TestSuites;
    return run(argc,argv,{suite<TestCppPreprocessor>("preprocessor"),suite<TestCppTokenizer>("tokenizer"),suite<TestCppParser>("parser"),suite<TestParserConfigurationQueue>("parser-configuration-queue"),suite<TestSemanticSnapshot>("semantic-snapshot"),suite<TestSemanticPublication>("semantic-publication")});
}
