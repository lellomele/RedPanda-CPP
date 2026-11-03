#include "test_cpppreprocessor.h"
#include "src/parser/cpppreprocessor.h"
#include "qt_utils/utils.h"
#include <QTest>
#include <QTemporaryDir>

TestCppPreprocessor::TestCppPreprocessor(QObject *parent):
    QObject{parent}
{

}

void TestCppPreprocessor::test_combine_lines_ending_with_backslash()
{
    QStringList text1 = readFileToLines("resources/preprocessor-backslashes-1.cpp");
    QStringList text2 = readFileToLines("resources/preprocessor-backslashes-1-result.cpp");
    QVERIFY(!text1.isEmpty());
    QVERIFY(!text2.isEmpty());
    CppPreprocessor::combineLinesEndingWithBackslash(text1);
    QCOMPARE(text1,text2);

}

void TestCppPreprocessor::test_replace_comments_with_space_char()
{
    QStringList text1 = readFileToLines("resources/preprocessor-comments-1.cpp");
    QStringList text2 = readFileToLines("resources/preprocessor-comments-1-result.cpp");
    QVERIFY(!text1.isEmpty());
    QVERIFY(!text2.isEmpty());
    CppPreprocessor::replaceCommentsBySpaceChar(text1);
    QCOMPARE(text1,text2);
}

void TestCppPreprocessor::test_macro_replace_1()
{
    CppPreprocessor preprocessor;
    QFileInfo info("resources/preprocessor-macros-1.cpp");
    preprocessor.preprocess(info.absoluteFilePath());
    QStringList text1 = filterIncludes(preprocessor.result());
    QStringList text2 = readFileToLines("resources/preprocessor-macros-1-result.cpp");
    QVERIFY(!text1.isEmpty());
    QVERIFY(!text2.isEmpty());
    QCOMPARE(text1,text2);
}

void TestCppPreprocessor::test_macro_replace_2()
{
    CppPreprocessor preprocessor;
    preprocessor.addHardDefineByLine("#define EMPTY");
    preprocessor.addHardDefineByLine("#define SCAN(x)     x");
    preprocessor.addHardDefineByLine("#define EXAMPLE_()  EXAMPLE");
    preprocessor.addHardDefineByLine("#define EXAMPLE(n)  EXAMPLE_ EMPTY()(n-1) (n)");
    QCOMPARE("EXAMPLE_ ()(5-1) (5)",
             preprocessor.expandMacros("EXAMPLE(5)"));
    QCOMPARE("EXAMPLE_ ()(5-1-1) (5-1) (5)",
             preprocessor.expandMacros("SCAN(EXAMPLE(5))"));
}

void TestCppPreprocessor::test_macro_replace_3()
{
    CppPreprocessor preprocessor;
    preprocessor.addHardDefineByLine("#define TEST 123");
    QCOMPARE("ttt 123 123",
             preprocessor.expandMacros("ttt TEST TEST"));
}

void TestCppPreprocessor::test_macro_replace_4()
{
    CppPreprocessor preprocessor;
    preprocessor.addHardDefineByLine("#define N TEST");
    preprocessor.addHardDefineByLine("#define TEST N");
    QCOMPARE("ttt TEST TEST",
             preprocessor.expandMacros("ttt TEST TEST"));
    QCOMPARE("ttt N N",
             preprocessor.expandMacros("ttt N N"));
}

void TestCppPreprocessor::test_macro_replace_5()
{
    CppPreprocessor preprocessor;
    preprocessor.addHardDefineByLine("#define NNN 123");
    preprocessor.addHardDefineByLine("#define TEST NNN");
    QCOMPARE("ttt 123 123",
             preprocessor.expandMacros("ttt TEST TEST"));
}

void TestCppPreprocessor::test_macro_replace_6()
{
    CppPreprocessor preprocessor;
    QFileInfo info("resources/preprocessor-macros-2.cpp");
    preprocessor.preprocess(info.absoluteFilePath());
    QStringList text1 = filterIncludes(preprocessor.result());
    QStringList text2 = readFileToLines("resources/preprocessor-macros-2-result.cpp");
    QVERIFY(!text1.isEmpty());
    QVERIFY(!text2.isEmpty());
    QCOMPARE(text1,text2);
}

void TestCppPreprocessor::test_macro_replace_7()
{
    CppPreprocessor preprocessor;
    QFileInfo info("resources/preprocessor-macros-3.cpp");
    preprocessor.preprocess(info.absoluteFilePath());
    QStringList text1 = filterIncludes(preprocessor.result());
    QStringList text2 = readFileToLines("resources/preprocessor-macros-3-result.cpp");
    QVERIFY(!text1.isEmpty());
    QVERIFY(!text2.isEmpty());
    QCOMPARE(text1,text2);
}

void TestCppPreprocessor::test_macro_replace_8()
{
    CppPreprocessor preprocessor;
    QFileInfo info("resources/preprocessor-macros-4.cpp");
    preprocessor.preprocess(info.absoluteFilePath());
    QStringList text1 = filterIncludes(preprocessor.result());
    QStringList text2 = readFileToLines("resources/preprocessor-macros-4-result.cpp");
    QVERIFY(!text1.isEmpty());
    QVERIFY(!text2.isEmpty());
    QCOMPARE(text1,text2);
}

void TestCppPreprocessor::test_replace_macros_in_constant_expression_1()
{
    CppPreprocessor preprocessor;
    preprocessor.addHardDefineByLine("#define __cplusplus 201703L");
    preprocessor.addHardDefineByLine("#define __has_builtin(x) 0");
    preprocessor.addHardDefineByLine("#define __GNUC__ 15");
    preprocessor.addHardDefineByLine("#define __GNUC_MINOR__ 3");
    preprocessor.addHardDefineByLine("#define __MINGW_GNUC_PREREQ(major, minor) (__GNUC__ > (major) || (__GNUC__ == (major) && __GNUC_MINOR__ >= (minor)))");
    QCOMPARE(preprocessor.expandMacrosInConditioningExpression("__cplusplus"),
             "201703L");
    QCOMPARE(preprocessor.expandMacrosInConditioningExpression("__cplusplus > 201703L"),
             "201703L > 201703L");
    QCOMPARE(preprocessor.expandMacrosInConditioningExpression("defined(__cplusplus)"),
             "1");
    QCOMPARE(preprocessor.expandMacrosInConditioningExpression("defined __cplusplus"),
             "1");
    QCOMPARE(preprocessor.expandMacrosInConditioningExpression("defined(__cplus)"),
             "0");
    QCOMPARE(preprocessor.expandMacrosInConditioningExpression("defined __cplus"),
             "0");

    QCOMPARE(preprocessor.expandMacrosInConditioningExpression("__has_builtin(__builtin_is_constant_evaluated)"),
             "0");


}

void TestCppPreprocessor::test_replace_macros_in_constant_expression_2()
{
    CppPreprocessor preprocessor;
    preprocessor.addHardDefineByLine("#define EMPTY");
    preprocessor.addHardDefineByLine("#define SCAN(x)     x");
    preprocessor.addHardDefineByLine("#define EXAMPLE_()  EXAMPLE");
    preprocessor.addHardDefineByLine("#define EXAMPLE(n)  EXAMPLE_ EMPTY()(n-1) (n)");
    QCOMPARE(preprocessor.expandMacrosInConditioningExpression("EXAMPLE(5)"),
             "EXAMPLE_ ()(5-1) (5)");
    QCOMPARE(preprocessor.expandMacrosInConditioningExpression("SCAN(EXAMPLE(5))"),
             "EXAMPLE_ ()(5-1-1) (5-1)  (5)");
}

void TestCppPreprocessor::test_evaluate_constant_expression_1()
{
    CppPreprocessor preprocessor;
    QCOMPARE(preprocessor.evaluateIf("0(0)"),false);
    QCOMPARE(preprocessor.evaluateIf("1 && !1 && !0    && !0"),false);
    QCOMPARE(preprocessor.evaluateIf("1&& (8* 4) <= 32"), true);
    QCOMPARE(preprocessor.evaluateIf("1&& (8* 4) == 32"), true);
    QCOMPARE(preprocessor.evaluateIf("0 || 1"), true);
    QCOMPARE(preprocessor.evaluateIf("1 || 0"), true);
    QCOMPARE(preprocessor.evaluateIf("1 || 1"), true);
    QCOMPARE(preprocessor.evaluateIf("0 || 0"), false);
    QCOMPARE(preprocessor.evaluateIf("(0 || 1) && 1"), true);
    QCOMPARE(preprocessor.evaluateIf("(1 || 0) && 1"), true);
    QCOMPARE(preprocessor.evaluateIf("(1 || 1) && 1"), true);
    QCOMPARE(preprocessor.evaluateIf("(0 || 0) && 1"), false);
    QCOMPARE(preprocessor.evaluateIf("1 && (0 || 1)"), true);
    QCOMPARE(preprocessor.evaluateIf("1 && (1 || 0)"), true);
    QCOMPARE(preprocessor.evaluateIf("1 && (1 || 1)"), true);
    QCOMPARE(preprocessor.evaluateIf("1 && (0 || 0)"), false);
    QCOMPARE(preprocessor.evaluateIf("0 && 1"), false);
    QCOMPARE(preprocessor.evaluateIf("1 && 0"), false);
    QCOMPARE(preprocessor.evaluateIf("1 && 1"), true);
    QCOMPARE(preprocessor.evaluateIf("0 && 0"), false);
    QCOMPARE(preprocessor.evaluateIf("(0 && 1) || 0"), false);
    QCOMPARE(preprocessor.evaluateIf("(1 && 0) || 0"), false);
    QCOMPARE(preprocessor.evaluateIf("(1 && 1) || 0"), true);
    QCOMPARE(preprocessor.evaluateIf("(0 && 0) || 0"), false);
    QCOMPARE(preprocessor.evaluateIf("0 || (0 && 1)"), false);
    QCOMPARE(preprocessor.evaluateIf("0 || (1 && 0)"), false);
    QCOMPARE(preprocessor.evaluateIf("0 || (1 && 1)"), true);
    QCOMPARE(preprocessor.evaluateIf("0 || (0 && 0)"), false);
    QCOMPARE(preprocessor.evaluateIf("!0"), true);
    QCOMPARE(preprocessor.evaluateIf("!1"), false);
    QCOMPARE(preprocessor.evaluateIf("8*4 == 32"), true);
    QCOMPARE(preprocessor.evaluateIf("8*4 >= 32"), true);
    QCOMPARE(preprocessor.evaluateIf("8*4 <= 32"), true);
    QCOMPARE(preprocessor.evaluateIf("8*4 < 32"), false);
    QCOMPARE(preprocessor.evaluateIf("8*4 > 32"), false);
    QCOMPARE(preprocessor.evaluateIf("8*4 != 32"), false);
    QCOMPARE(preprocessor.evaluateIf("0x10 > 15"), true);
    QCOMPARE(preprocessor.evaluateIf("0x10 == 16"), true);
    QCOMPARE(preprocessor.evaluateIf("0x10 < 17"), true);
    QCOMPARE(preprocessor.evaluateIf("0x10L == 16"), true);
    QCOMPARE(preprocessor.evaluateIf("0x10UL == 16"), true);
    QCOMPARE(preprocessor.evaluateIf("0x10LL == 16"), true);
    QCOMPARE(preprocessor.evaluateIf("0x10ULL == 16"), true);
    QCOMPARE(preprocessor.evaluateIf("010 > 7"), true);
    QCOMPARE(preprocessor.evaluateIf("010 == 8"), true);
    QCOMPARE(preprocessor.evaluateIf("010 < 9"), true);
    QCOMPARE(preprocessor.evaluateIf("010L == 8"), true);
    QCOMPARE(preprocessor.evaluateIf("010UL == 8"), true);
    QCOMPARE(preprocessor.evaluateIf("010LL == 8"), true);
    QCOMPARE(preprocessor.evaluateIf("010ULL == 8"), true);
    QCOMPARE(preprocessor.evaluateIf("(15> (14) || (15== (14) && 3>= (15)))"), true);
    QCOMPARE(preprocessor.evaluateIf("(0 > 3 || (0 == 3 && 0 >= 1)) &&     !1  "), false);
    QCOMPARE(preprocessor.evaluateIf("1?0:1"), false);
    QCOMPARE(preprocessor.evaluateIf("0?0:1"), true);
    QCOMPARE(preprocessor.evaluateIf("1?1:0"), true);
    QCOMPARE(preprocessor.evaluateIf("0?1:0"), false);

}

void TestCppPreprocessor::test_evaluate_constant_expression_2()
{
    CppPreprocessor preprocessor;
    preprocessor.addHardDefineByLine("#define __cplusplus 202002L");
    preprocessor.addHardDefineByLine("#define __has_builtin(x) 0");
    preprocessor.addHardDefineByLine("#define __GNUC__ 15");
    preprocessor.addHardDefineByLine("#define __GNUC_MINOR__ 3");
    preprocessor.addHardDefineByLine("#define __MINGW_GNUC_PREREQ(major, minor) (__GNUC__ > (major) || (__GNUC__ == (major) && __GNUC_MINOR__ >= (minor)))");
    QCOMPARE(preprocessor.evaluateIf("__MINGW_GNUC_PREREQ(14,15)"), true);
    QCOMPARE(preprocessor.evaluateIf("__MINGW_GNUC_PREREQ(15,2)"), true);
    QCOMPARE(preprocessor.evaluateIf("__MINGW_GNUC_PREREQ(15,3)"), true);
    QCOMPARE(preprocessor.evaluateIf("__MINGW_GNUC_PREREQ(15,4)"), false);
    QCOMPARE(preprocessor.evaluateIf("__MINGW_GNUC_PREREQ(16,0)"), false);
}

void TestCppPreprocessor::test_compare_cpp_version()
{
    CppPreprocessor preprocessor;
    preprocessor.clear();
    preprocessor.addHardDefineByLine("#define __cplusplus 199711L");
    QCOMPARE(preprocessor.evaluateIf("defined(__cplusplus)"), true);
    QCOMPARE(preprocessor.evaluateIf("__cplusplus >= 201103L"), false);
    QCOMPARE(preprocessor.evaluateIf("__cplusplus >= 201402L"), false);
    QCOMPARE(preprocessor.evaluateIf("__cplusplus >= 201703L"), false);
    QCOMPARE(preprocessor.evaluateIf("__cplusplus >= 202002L"), false);
    QCOMPARE(preprocessor.evaluateIf("__cplusplus > 202002L"), false);
    QCOMPARE(preprocessor.evaluateIf("__cplusplus > 202302L"), false);

    preprocessor.clear();
    preprocessor.addHardDefineByLine("#define __cplusplus 201103L");
    QCOMPARE(preprocessor.evaluateIf("defined(__cplusplus)"), true);
    QCOMPARE(preprocessor.evaluateIf("__cplusplus >= 201103L"), true);
    QCOMPARE(preprocessor.evaluateIf("__cplusplus >= 201402L"), false);
    QCOMPARE(preprocessor.evaluateIf("__cplusplus >= 201703L"), false);
    QCOMPARE(preprocessor.evaluateIf("__cplusplus >= 202002L"), false);
    QCOMPARE(preprocessor.evaluateIf("__cplusplus > 202002L"), false);
    QCOMPARE(preprocessor.evaluateIf("__cplusplus > 202302L"), false);

    preprocessor.clear();
    preprocessor.addHardDefineByLine("#define __cplusplus 201402L");
    QCOMPARE(preprocessor.evaluateIf("defined(__cplusplus)"), true);
    QCOMPARE(preprocessor.evaluateIf("__cplusplus >= 201103L"), true);
    QCOMPARE(preprocessor.evaluateIf("__cplusplus >= 201402L"), true);
    QCOMPARE(preprocessor.evaluateIf("__cplusplus >= 201703L"), false);
    QCOMPARE(preprocessor.evaluateIf("__cplusplus >= 202002L"), false);
    QCOMPARE(preprocessor.evaluateIf("__cplusplus > 202002L"), false);
    QCOMPARE(preprocessor.evaluateIf("__cplusplus > 202302L"), false);

    preprocessor.clear();
    preprocessor.addHardDefineByLine("#define __cplusplus 201703L");
    QCOMPARE(preprocessor.evaluateIf("defined(__cplusplus)"), true);
    QCOMPARE(preprocessor.evaluateIf("__cplusplus >= 201103L"), true);
    QCOMPARE(preprocessor.evaluateIf("__cplusplus >= 201402L"), true);
    QCOMPARE(preprocessor.evaluateIf("__cplusplus >= 201703L"), true);
    QCOMPARE(preprocessor.evaluateIf("__cplusplus >= 202002L"), false);
    QCOMPARE(preprocessor.evaluateIf("__cplusplus > 202002L"), false);
    QCOMPARE(preprocessor.evaluateIf("__cplusplus > 202302L"), false);


    preprocessor.clear();
    preprocessor.addHardDefineByLine("#define __cplusplus 202002L");
    QCOMPARE(preprocessor.evaluateIf("defined(__cplusplus)"), true);
    QCOMPARE(preprocessor.evaluateIf("__cplusplus >= 201103L"), true);
    QCOMPARE(preprocessor.evaluateIf("__cplusplus >= 201402L"), true);
    QCOMPARE(preprocessor.evaluateIf("__cplusplus >= 201703L"), true);
    QCOMPARE(preprocessor.evaluateIf("__cplusplus >= 202002L"), true);
    QCOMPARE(preprocessor.evaluateIf("__cplusplus > 202002L"), false);
    QCOMPARE(preprocessor.evaluateIf("__cplusplus > 202302L"), false);

    preprocessor.clear();
    preprocessor.addHardDefineByLine("#define __cplusplus 202302L");
    QCOMPARE(preprocessor.evaluateIf("defined(__cplusplus)"), true);
    QCOMPARE(preprocessor.evaluateIf("__cplusplus >= 201103L"), true);
    QCOMPARE(preprocessor.evaluateIf("__cplusplus >= 201402L"), true);
    QCOMPARE(preprocessor.evaluateIf("__cplusplus >= 201703L"), true);
    QCOMPARE(preprocessor.evaluateIf("__cplusplus >= 202002L"), true);
    QCOMPARE(preprocessor.evaluateIf("__cplusplus > 202002L"), true);
    QCOMPARE(preprocessor.evaluateIf("__cplusplus > 202302L"), false);

    preprocessor.clear();
    preprocessor.addHardDefineByLine("#define __cplusplus 202400L");
    QCOMPARE(preprocessor.evaluateIf("defined(__cplusplus)"), true);
    QCOMPARE(preprocessor.evaluateIf("__cplusplus >= 201103L"), true);
    QCOMPARE(preprocessor.evaluateIf("__cplusplus >= 201402L"), true);
    QCOMPARE(preprocessor.evaluateIf("__cplusplus >= 201703L"), true);
    QCOMPARE(preprocessor.evaluateIf("__cplusplus >= 202002L"), true);
    QCOMPARE(preprocessor.evaluateIf("__cplusplus > 202002L"), true);
    QCOMPARE(preprocessor.evaluateIf("__cplusplus > 202302L"), true);

}

QStringList TestCppPreprocessor::filterIncludes(const QStringList &text)
{
    QStringList result;
    foreach(const QString& s, text) {
        if (!s.startsWith("#include"))
            result.append(s);
    }
    return result;
}


namespace {
bool writeFixture(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
}

void TestCppPreprocessor::test_dependency_scan_uses_active_directives()
{
    QTemporaryDir files;
    QVERIFY(files.isValid());
    const auto root = files.filePath("main.cpp");
    const auto bridge = files.filePath("bridge.h");
    const auto leaf = files.filePath("leaf.h");
    const auto missing = files.filePath("missing.h");
    QVERIFY(writeFixture(root, R"test(
#define BRIDGE "bridge.h"
const char *text = R"delim(
#include "raw-string-is-not-an-include.h"
)delim";
#if __has_include(BRIDGE)
#include BRIDGE
#endif
#if __has_include("missing.h")
#include "disabled-include.h"
#endif
)test"));
    QVERIFY(writeFixture(bridge, "#ifndef BRIDGE_H\n#define BRIDGE_H\n#include \"leaf.h\"\n#endif\n"));
    QVERIFY(writeFixture(leaf, "// leaf\n"));
    CppPreprocessor::DependencyRequest request;
    request.root = root;
    request.readPolicy.workingDirectory = files.path();
    const auto result = CppPreprocessor::scanDependencies(request);
    QVERIFY2(result.complete(), qPrintable(result.error));
    QCOMPARE(result.directIncludes.value(root), QStringList{bridge});
    QCOMPARE(result.directIncludes.value(bridge), QStringList{leaf});
    QCOMPARE(result.reads.size(), 3);
    QVERIFY(result.reads.contains(leaf));
    QVERIFY(result.probes.contains(missing));
}

void TestCppPreprocessor::test_dependency_scan_rejects_incomplete_results()
{
    QTemporaryDir files;
    QVERIFY(files.isValid());
    CppPreprocessor::DependencyRequest request;
    request.root = files.filePath("main.cpp");
    request.readPolicy.workingDirectory = files.path();
    QVERIFY(writeFixture(request.root, "#include \"bridge.h\"\n"));
    QVERIFY(writeFixture(files.filePath("bridge.h"), "#include \"leaf.h\"\n"));
    QVERIFY(writeFixture(files.filePath("leaf.h"), "// leaf\n"));
    request.limits.edges = 1;
    const auto limited = CppPreprocessor::scanDependencies(request);
    QCOMPARE(limited.status, CppPreprocessor::DependencyStatus::LimitExceeded);
    QVERIFY(limited.directIncludes.isEmpty());
    request.limits = {};
    request.cancelled = std::make_shared<const std::atomic_bool>(true);
    const auto cancelled = CppPreprocessor::scanDependencies(request);
    QCOMPARE(cancelled.status, CppPreprocessor::DependencyStatus::Cancelled);
    QVERIFY(cancelled.directIncludes.isEmpty());
    request.cancelled.reset();
    QVERIFY(QFile::remove(files.filePath("leaf.h")));
    const auto missing = CppPreprocessor::scanDependencies(request);
    QCOMPARE(missing.status, CppPreprocessor::DependencyStatus::UnresolvedInclude);
    QVERIFY(missing.directIncludes.isEmpty());
}
