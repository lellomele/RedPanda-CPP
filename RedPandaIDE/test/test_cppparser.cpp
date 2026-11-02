#include <QTest>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <utility>
#include <QSignalSpy>
#include <QElapsedTimer>
#include <atomic>
#include <QSemaphore>
#include <thread>
#include "src/parser/cppparser.h"
#include "test_cppparser.h"

TestCppParser::TestCppParser(QObject *parent):
    QObject{parent}
{
    init_parser();
}

void TestCppParser::init_parser()
{
    mParser = std::make_shared<CppParser>();
}

void TestCppParser::test_parse_var()
{
    mParser->setOnGetFileStream([](const QString& filename, QStringList& buffer){
        buffer=QStringList({
                               "int xxx=10;"
                           });
           return true;
                                });
    CppParser::parseFileBlocking(mParser,"text.cpp",false,"");
    QCOMPARE(mParser->statementList().count(),1);
    PStatement statement = mParser->findStatement("xxx");
    QVERIFY(statement!=nullptr);
    QCOMPARE(statement->fullName,"xxx");
    QCOMPARE(statement->kind,StatementKind::Variable);

}

void TestCppParser::test_parse_vars()
{
    mParser->setOnGetFileStream(nullptr);
    QFileInfo file("resources/vars.cpp");
    QVERIFY(file.exists());
    CppParser::parseFileBlocking(mParser,file.absoluteFilePath(),false,"");
    PStatement statement;
    statement = mParser->findStatement("a1");
    QVERIFY(statement!=nullptr);
    QCOMPARE(statement->fullName,"a1");
    QCOMPARE(statement->kind,StatementKind::Variable);
    QCOMPARE(statement->type,"int");
    QCOMPARE(statement->args,"");

    statement = mParser->findStatement("a2");
    QVERIFY(statement!=nullptr);
    QCOMPARE(statement->fullName,"a2");
    QCOMPARE(statement->kind,StatementKind::Variable);
    QCOMPARE(statement->type,"const int");
    QCOMPARE(statement->args,"");

    statement = mParser->findStatement("a3");
    QVERIFY(statement!=nullptr);
    QCOMPARE(statement->fullName,"a3");
    QCOMPARE(statement->kind,StatementKind::Variable);
    QCOMPARE(statement->type,"static const int");
    QCOMPARE(statement->args,"");

    statement = mParser->findStatement("a4");
    QVERIFY(statement!=nullptr);
    QCOMPARE(statement->fullName,"a4");
    QCOMPARE(statement->kind,StatementKind::Variable);
    QCOMPARE(statement->type,"extern const int");
    QCOMPARE(statement->args,"");

    statement = mParser->findStatement("a5");
    QVERIFY(statement!=nullptr);
    QCOMPARE(statement->fullName,"a5");
    QCOMPARE(statement->kind,StatementKind::Variable);
    QCOMPARE(statement->type,"static const int");
    QCOMPARE(statement->args,"[4]");

    statement = mParser->findStatement("a6");
    QVERIFY(statement!=nullptr);
    QCOMPARE(statement->fullName,"a6");
    QCOMPARE(statement->kind,StatementKind::Variable);
    QCOMPARE(statement->type,"static const int *");
    QCOMPARE(statement->args,"");

    statement = mParser->findStatement("a7");
    QVERIFY(statement!=nullptr);
    QCOMPARE(statement->fullName,"a7");
    QCOMPARE(statement->kind,StatementKind::Variable);
    QCOMPARE(statement->type,"static const int &");
    QCOMPARE(statement->args,"");

    statement = mParser->findStatement("a8");
    QVERIFY(statement!=nullptr);
    QCOMPARE(statement->fullName,"a8");
    QCOMPARE(statement->kind,StatementKind::Variable);
    QCOMPARE(statement->type,"static const int **");
    QCOMPARE(statement->args,"");

    statement = mParser->findStatement("a8");
    QVERIFY(statement!=nullptr);
    QCOMPARE(statement->fullName,"a8");
    QCOMPARE(statement->kind,StatementKind::Variable);
    QCOMPARE(statement->type,"static const int **");
    QCOMPARE(statement->args,"");

    statement = mParser->findStatement("a9");
    QVERIFY(statement!=nullptr);
    QCOMPARE(statement->fullName,"a9");
    QCOMPARE(statement->kind,StatementKind::Variable);
    QCOMPARE(statement->type,"static const int");
    QCOMPARE(statement->args,"");

    statement = mParser->findStatement("a10");
    QVERIFY(statement!=nullptr);
    QCOMPARE(statement->fullName,"a10");
    QCOMPARE(statement->kind,StatementKind::Variable);
    QCOMPARE(statement->type,"static const int *");
    QCOMPARE(statement->args,"");

    statement = mParser->findStatement("a11");
    QVERIFY(statement!=nullptr);
    QCOMPARE(statement->fullName,"a11");
    QCOMPARE(statement->kind,StatementKind::Variable);
    QCOMPARE(statement->type,"static const int &");
    QCOMPARE(statement->args,"");

    statement = mParser->findStatement("b1");
    QVERIFY(statement!=nullptr);
    QCOMPARE(statement->fullName,"b1");
    QCOMPARE(statement->kind,StatementKind::Variable);
    QCOMPARE(statement->type,"unsigned int");
    QCOMPARE(statement->args,"");

    statement = mParser->findStatement("b2");
    QVERIFY(statement!=nullptr);
    QCOMPARE(statement->fullName,"b2");
    QCOMPARE(statement->kind,StatementKind::Variable);
    QCOMPARE(statement->type,"const unsigned int");
    QCOMPARE(statement->args,"");

    statement = mParser->findStatement("b3");
    QVERIFY(statement!=nullptr);
    QCOMPARE(statement->fullName,"b3");
    QCOMPARE(statement->kind,StatementKind::Variable);
    QCOMPARE(statement->type,"static const unsigned int");
    QCOMPARE(statement->args,"");

    statement = mParser->findStatement("b4");
    QVERIFY(statement!=nullptr);
    QCOMPARE(statement->fullName,"b4");
    QCOMPARE(statement->kind,StatementKind::Variable);
    QCOMPARE(statement->type,"extern const unsigned int");
    QCOMPARE(statement->args,"");

    statement = mParser->findStatement("b5");
    QVERIFY(statement!=nullptr);
    QCOMPARE(statement->fullName,"b5");
    QCOMPARE(statement->kind,StatementKind::Variable);
    QCOMPARE(statement->type,"static unsigned int");
    QCOMPARE(statement->args,"[4]");

    statement = mParser->findStatement("b6");
    QVERIFY(statement!=nullptr);
    QCOMPARE(statement->fullName,"b6");
    QCOMPARE(statement->kind,StatementKind::Variable);
    QCOMPARE(statement->type,"static unsigned int *");
    QCOMPARE(statement->args,"");

    statement = mParser->findStatement("b7");
    QVERIFY(statement!=nullptr);
    QCOMPARE(statement->fullName,"b7");
    QCOMPARE(statement->kind,StatementKind::Variable);
    QCOMPARE(statement->type,"static unsigned int *const");
    QCOMPARE(statement->args,"");

    statement = mParser->findStatement("b8");
    QVERIFY(statement!=nullptr);
    QCOMPARE(statement->fullName,"b8");
    QCOMPARE(statement->kind,StatementKind::Variable);
    QCOMPARE(statement->type,"static unsigned int **");
    QCOMPARE(statement->args,"");

    statement = mParser->findStatement("s1");
    QVERIFY(statement!=nullptr);
    QCOMPARE(statement->fullName,"s1");
    QCOMPARE(statement->kind,StatementKind::Variable);
    QCOMPARE(statement->type,"std::string");
    QCOMPARE(statement->args,"");

    statement = mParser->findStatement("s2");
    QVERIFY(statement!=nullptr);
    QCOMPARE(statement->fullName,"s2");
    QCOMPARE(statement->kind,StatementKind::Variable);
    QCOMPARE(statement->type,"const std::string");
    QCOMPARE(statement->args,"");

    statement = mParser->findStatement("s3");
    QVERIFY(statement!=nullptr);
    QCOMPARE(statement->fullName,"s3");
    QCOMPARE(statement->kind,StatementKind::Variable);
    QCOMPARE(statement->type,"static const std::string");
    QCOMPARE(statement->args,"");

    statement = mParser->findStatement("s4");
    QVERIFY(statement!=nullptr);
    QCOMPARE(statement->fullName,"s4");
    QCOMPARE(statement->kind,StatementKind::Variable);
    QCOMPARE(statement->type,"static const std::string");
    QCOMPARE(statement->args,"");

    statement = mParser->findStatement("s5");
    QVERIFY(statement!=nullptr);
    QCOMPARE(statement->fullName,"s5");
    QCOMPARE(statement->kind,StatementKind::Variable);
    QCOMPARE(statement->type,"static const std::string");
    QCOMPARE(statement->args,"[4]");

    statement = mParser->findStatement("s6");
    QVERIFY(statement!=nullptr);
    QCOMPARE(statement->fullName,"s6");
    QCOMPARE(statement->kind,StatementKind::Variable);
    QCOMPARE(statement->type,"static const std::string *");
    QCOMPARE(statement->args,"");

    statement = mParser->findStatement("s7");
    QVERIFY(statement!=nullptr);
    QCOMPARE(statement->fullName,"s7");
    QCOMPARE(statement->kind,StatementKind::Variable);
    QCOMPARE(statement->type,"static const std::string &");
    QCOMPARE(statement->args,"");

    statement = mParser->findStatement("f1");
    QVERIFY(statement!=nullptr);
    QCOMPARE(statement->fullName,"f1");
    QCOMPARE(statement->kind,StatementKind::Variable);
    QCOMPARE(statement->type,"static const struct FILE *");
    QCOMPARE(statement->args,"");

    statement = mParser->findStatement("f2");
    QVERIFY(statement!=nullptr);
    QCOMPARE(statement->fullName,"f2");
    QCOMPARE(statement->kind,StatementKind::Variable);
    QCOMPARE(statement->type,"static const struct FILE");
    QCOMPARE(statement->args,"");

    statement = mParser->findStatement("ff1");
    QVERIFY(statement!=nullptr);
    QCOMPARE(statement->fullName,"ff1");
    QCOMPARE(statement->kind,StatementKind::Variable);
    QVERIFY(statement->properties & StatementProperty::FunctionPointer);
    QCOMPARE(statement->type,"int");
    QCOMPARE(statement->args,"( )");

    statement = mParser->findStatement("ff2");
    QVERIFY(statement!=nullptr);
    QCOMPARE(statement->fullName,"ff2");
    QCOMPARE(statement->kind,StatementKind::Variable);
    QVERIFY(statement->properties & StatementProperty::FunctionPointer);
    QCOMPARE(statement->type,"static const int");
    QCOMPARE(statement->args,"( )");

    statement = mParser->findStatement("ff3");
    QVERIFY(statement!=nullptr);
    QCOMPARE(statement->fullName,"ff3");
    QCOMPARE(statement->kind,StatementKind::Variable);
    QVERIFY(statement->properties & StatementProperty::FunctionPointer);
    QCOMPARE(statement->type,"static const std::string");
    QCOMPARE(statement->args,"( )");

    //TODO:: ff4 ff5

    statement = mParser->findStatement("ff6");
    QVERIFY(statement!=nullptr);
    QCOMPARE(statement->fullName,"ff6");
    QCOMPARE(statement->kind,StatementKind::Variable);
    QVERIFY(statement->properties & StatementProperty::FunctionPointer);
    QCOMPARE(statement->type,"int *");
    QCOMPARE(statement->args,"( )");

    statement = mParser->findStatement("aa1");
    QVERIFY(statement!=nullptr);
    QCOMPARE(statement->fullName,"aa1");
    QCOMPARE(statement->kind,StatementKind::Variable);
    QVERIFY(!(statement->properties & StatementProperty::FunctionPointer));
    QCOMPARE(statement->type,"int (*)");
    QCOMPARE(statement->args,"[5]");

    statement = mParser->findStatement("aa2");
    QVERIFY(statement!=nullptr);
    QCOMPARE(statement->fullName,"aa2");
    QCOMPARE(statement->kind,StatementKind::Variable);
    QVERIFY(!(statement->properties & StatementProperty::FunctionPointer));
    QCOMPARE(statement->type,"static const std::string (*)");
    QCOMPARE(statement->args,"[5]");

}

void TestCppParser::test_struct()
{
    mParser->setOnGetFileStream(nullptr);
    QFileInfo file("resources/struct.cpp");
    QVERIFY(file.exists());
    CppParser::parseFileBlocking(mParser,file.absoluteFilePath(),false,"");
    PStatement statement;
    statement = mParser->findStatement("TTT");
    QVERIFY(statement!=nullptr);
    QCOMPARE(statement->command, "TTT");
    QCOMPARE(statement->kind, StatementKind::Class);
    QCOMPARE(statement->publicProperties.count(), 3);
    PStatement child;
    child = statement->publicProperties[0];
    QCOMPARE(child->command,"xxx");
    QCOMPARE(child->kind,StatementKind::Variable);
    QCOMPARE(child->fullName,"TTT::xxx");
    QCOMPARE(child->type, "int");
    QCOMPARE(child->accessibility, StatementAccessibility::Public);
    child = statement->publicProperties[1];
    QCOMPARE(child->command,"yyy");
    QCOMPARE(child->kind,StatementKind::Variable);
    QCOMPARE(child->fullName,"TTT::yyy");
    QCOMPARE(child->type, "short");
    QCOMPARE(child->accessibility, StatementAccessibility::Public);
    child = statement->publicProperties[2];
    QCOMPARE(child->command,"zzz");
    QCOMPARE(child->kind,StatementKind::Variable);
    QCOMPARE(child->fullName,"TTT::zzz");
    QCOMPARE(child->type, "double");
    QCOMPARE(child->accessibility, StatementAccessibility::Public);

    QCOMPARE(statement->children.count(),4);
    child = statement->children.value("ppp");
    QCOMPARE(child->command,"ppp");
    QCOMPARE(child->kind,StatementKind::Variable);
    QCOMPARE(child->fullName,"TTT::ppp");
    QCOMPARE(child->type, "int");
    QCOMPARE(child->accessibility, StatementAccessibility::Private);

}

void TestCppParser::test_structured_bindings()
{
    mParser->setOnGetFileStream(nullptr);
    QFileInfo file("resources/structured-bindings.cpp");
    QVERIFY(file.exists());
    CppParser::parseFileBlocking(mParser,file.absoluteFilePath(),false,"");
    PStatement statement;
    statement = mParser->findStatement("a");
    QVERIFY(statement!=nullptr);
    QCOMPARE(statement->fullName,"a");
    QCOMPARE(statement->kind,StatementKind::Variable);
    QCOMPARE(statement->type,"int");
    QCOMPARE(statement->args,"");

    statement = mParser->findStatement("b");
    QVERIFY(statement!=nullptr);
    QCOMPARE(statement->fullName,"b");
    QCOMPARE(statement->kind,StatementKind::Variable);
    QCOMPARE(statement->type,"short");
    QCOMPARE(statement->args,"");

    statement = mParser->findStatement("c");
    QVERIFY(statement!=nullptr);
    QCOMPARE(statement->fullName,"c");
    QCOMPARE(statement->kind,StatementKind::Variable);
    QCOMPARE(statement->type,"double");
    QCOMPARE(statement->args,"");

    statement = mParser->findStatement("d");
    QVERIFY(statement!=nullptr);
    QCOMPARE(statement->fullName,"d");
    QCOMPARE(statement->kind,StatementKind::Variable);
    QCOMPARE(statement->type,"int");
    QCOMPARE(statement->args,"");

    statement = mParser->findStatement("e");
    QVERIFY(statement!=nullptr);
    QCOMPARE(statement->fullName,"e");
    QCOMPARE(statement->kind,StatementKind::Variable);
    QCOMPARE(statement->type,"short");
    QCOMPARE(statement->args,"");

    statement = mParser->findStatement("g");
    QVERIFY(statement!=nullptr);
    QCOMPARE(statement->fullName,"g");
    QCOMPARE(statement->kind,StatementKind::Variable);
    QCOMPARE(statement->type,"double");
    QCOMPARE(statement->args,"");
}


void TestCppParser::test_include_directive_boundaries_data()
{
    QTest::addColumn<QString>("line");
    QTest::addColumn<bool>("include");
    QTest::addColumn<bool>("includeNext");
    QTest::newRow("angle") << QString("#include <vector>") << true << false;
    QTest::newRow("quoted-without-space") << QString("#include\"local.h\"") << true << false;
    QTest::newRow("space-after-hash") << QString("  # include <vector>") << true << false;
    QTest::newRow("macro") << QString("#include HEADER") << true << false;
    QTest::newRow("unfinished") << QString("#include") << true << false;
    QTest::newRow("next") << QString("#include_next <vector>") << true << true;
    QTest::newRow("next-unfinished") << QString("#include_next") << true << true;
    QTest::newRow("identifier-prefix") << QString("#include_something <vector>") << false << false;
    QTest::newRow("suffix-digit") << QString("#include2 <vector>") << false << false;
    QTest::newRow("next-suffix") << QString("#include_next2 <vector>") << false << false;
    QTest::newRow("inside-comment") << QString("// #include <vector>") << false << false;
    QTest::newRow("ordinary-code") << QString("include(vector);") << false << false;
}

void TestCppParser::test_include_directive_boundaries()
{
    QFETCH(QString, line);
    QFETCH(bool, include);
    QFETCH(bool, includeNext);
    QCOMPARE(mParser->isIncludeLine(line), include);
    QCOMPARE(mParser->isIncludeNextLine(line), includeNext);
}

void TestCppParser::test_reparse_replaces_obsolete_symbols()
{
    init_parser();
    QStringList contents{"struct Holder { int first; };", "int before = 1;"};
    mParser->setOnGetFileStream([&](const QString&, QStringList& buffer) { buffer = contents; return true; });
    CppParser::parseFileBlocking(mParser, "incremental-edit.cpp", false, "");
    QVERIFY(mParser->findStatement("before"));
    QVERIFY(mParser->findStatement("Holder::first"));
    contents = QStringList{"struct Holder { double second; };", "int after = 2;"};
    mParser->invalidateFile("incremental-edit.cpp");
    CppParser::parseFileBlocking(mParser, "incremental-edit.cpp", false, "");
    QVERIFY(!mParser->findStatement("before"));
    QVERIFY(!mParser->findStatement("Holder::first"));
    QVERIFY(mParser->findStatement("after"));
    QVERIFY(mParser->findStatement("Holder::second"));
    QCOMPARE(mParser->findStatement("Holder::second")->type, QString("double"));
    QVERIFY(!mParser->parsing());
}


void TestCppParser::test_frozen_parse_request_is_retried()
{
    init_parser();
    mParser->setEnabled(true);
    mParser->setOnGetFileStream([](const QString &, QStringList &buffer) {
        buffer = QStringList{"int deferred_symbol = 42;"};
        return true;
    });
    QSignalSpy finished(mParser.get(), &CppParser::parseFinished);
    QVERIFY(finished.isValid());
    QVERIFY(mParser->freeze());
    // The asynchronous entry point enqueues synchronously while frozen.
    CppParser::parseFileNonBlocking(mParser, "deferred-one.cpp", false, "");
    QCOMPARE(finished.count(), 0);
    mParser->unFreeze();
    QTRY_VERIFY_WITH_TIMEOUT(finished.count() >= 1, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(mParser->findStatement("deferred_symbol"), 3000);
    QVERIFY(mParser->findFileInfo("deferred-one.cpp"));
}

void TestCppParser::test_disabled_parser_cancels_queued_requests_data()
{
    QTest::addColumn<bool>("initiallyFrozen");
    QTest::newRow("dispatch-pending") << false;
    QTest::newRow("queued-during-freeze") << true;
}

void TestCppParser::test_disabled_parser_cancels_queued_requests()
{
    QFETCH(bool, initiallyFrozen);
    init_parser();
    mParser->setEnabled(true);
    const auto cancelledInvocations = std::make_shared<std::atomic<int>>(0);
    mParser->setOnGetFileStream([cancelledInvocations](const QString &filename, QStringList &buffer) {
        if (filename.endsWith("cancelled-queued.cpp")) {
            ++*cancelledInvocations;
            buffer = QStringList{"int must_not_run = 1;"};
        } else if (filename.endsWith("fresh-after-enable.cpp")) {
            buffer = QStringList{"int fresh_after_enable = 2;"};
        } else return false;
        return true;
    });
    QSignalSpy finished(mParser.get(), &CppParser::parseFinished);
    QVERIFY(finished.isValid());
    if (initiallyFrozen) QVERIFY(mParser->freeze());
    CppParser::parseFileNonBlocking(mParser, "cancelled-queued.cpp", false, "");
    if (initiallyFrozen) mParser->unFreeze();
    // No event-loop dispatch between queuing, unfreeze, and disable.
    mParser->setEnabled(false);
    QCoreApplication::processEvents();
    QCOMPARE(cancelledInvocations->load(), 0);
    QCOMPARE(finished.count(), 0);
    QVERIFY(!mParser->findFileInfo("cancelled-queued.cpp"));
    mParser->setEnabled(true);
    CppParser::parseFileNonBlocking(mParser, "fresh-after-enable.cpp", false, "");
    QTRY_VERIFY_WITH_TIMEOUT(mParser->findStatement("fresh_after_enable"), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(finished.count() >= 1, 3000);
    QCOMPARE(cancelledInvocations->load(), 0);
    QVERIFY(!mParser->findStatement("must_not_run"));
    QVERIFY(!mParser->findFileInfo("cancelled-queued.cpp"));
}

void TestCppParser::test_busy_parser_retains_distinct_requests()
{
    init_parser();
    mParser->setEnabled(true);
    const auto entered = std::make_shared<QSemaphore>();
    const auto release = std::make_shared<QSemaphore>();
    const auto latest = std::make_shared<std::atomic<bool>>(false);
    mParser->setOnGetFileStream([entered, release, latest](const QString &filename, QStringList &buffer) {
        if (filename.endsWith("active-a.cpp")) {
            entered->release();
            if (!release->tryAcquire(1, 5000)) return false;
            buffer = QStringList{"int active_symbol = 1;"};
        } else if (filename.endsWith("pending-b.cpp")) {
            buffer = QStringList{latest->load() ? "int newest_b = 2;" : "int obsolete_b = 2;"};
        } else if (filename.endsWith("pending-c.cpp")) {
            buffer = QStringList{"int pending_c = 3;"};
        } else return false;
        return true;
    });
    QSignalSpy finished(mParser.get(), &CppParser::parseFinished);
    QVERIFY(finished.isValid());
    const auto parser = mParser;
    std::thread first([parser] { CppParser::parseFileBlocking(parser, "active-a.cpp", false, ""); });
    const bool started = entered->tryAcquire(1, 3000);
    if (started) {
        CppParser::parseFileNonBlocking(parser, "pending-b.cpp", false, "");
        CppParser::parseFileNonBlocking(parser, "pending-c.cpp", false, "");
        latest->store(true);
        CppParser::parseFileNonBlocking(parser, "pending-b.cpp", false, "");
    }
    // Always release and join, including the error path, before any assertion.
    release->release();
    first.join();
    QVERIFY2(started, "First parse never reached the controlled file callback");
    QTRY_VERIFY_WITH_TIMEOUT(mParser->findStatement("active_symbol"), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(mParser->findStatement("newest_b"), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(mParser->findStatement("pending_c"), 3000);
    QVERIFY(!mParser->findStatement("obsolete_b"));
    QTRY_VERIFY_WITH_TIMEOUT(finished.count() >= 3, 3000);
}

void TestCppParser::test_frozen_project_file_list_is_retried()
{
    init_parser();
    mParser->setEnabled(true);
    mParser->setOnGetFileStream([](const QString &filename, QStringList &buffer) {
        if (!filename.endsWith("queued-project.cpp")) return false;
        buffer = QStringList{"int queued_project_symbol = 8;"};
        return true;
    });
    mParser->addProjectFile("queued-project.cpp", true);
    QSignalSpy finished(mParser.get(), &CppParser::parseFinished);
    QVERIFY(finished.isValid());
    QVERIFY(mParser->freeze());
    CppParser::parseFileListNonBlocking(mParser, true);
    QCOMPARE(finished.count(), 0);
    mParser->unFreeze();
    QTRY_VERIFY_WITH_TIMEOUT(finished.count() >= 1, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(mParser->findStatement("queued_project_symbol"), 3000);
    QVERIFY(mParser->findFileInfo("queued-project.cpp"));
}

void TestCppParser::test_frozen_background_request_is_retried()
{
    init_parser();
    mParser->setEnabled(true);
    mParser->setOnGetFileStream([](const QString &, QStringList &buffer) {
        buffer = QStringList{"int background_symbol = 7;"};
        return true;
    });
    QSignalSpy parsed(mParser.get(), &CppParser::parseFinished);
    QVERIFY(parsed.isValid());
    QVERIFY(mParser->freeze());
    auto *worker = new CppFileParserThread(mParser, "background-while-frozen.cpp", false, "");
    QSignalSpy workerFinished(worker, &QThread::finished);
    worker->start();
    // Wait until the request has actually reached the frozen parser before
    // releasing it, avoiding a race with worker-thread scheduling.
    const bool completed = workerFinished.count() > 0 || workerFinished.wait(3000);
    mParser->unFreeze();
    QVERIFY2(completed, "Background request must be queued without waiting for unFreeze");
    QTRY_VERIFY_WITH_TIMEOUT(parsed.count() >= 1, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(mParser->findStatement("background_symbol"), 3000);
}

void TestCppParser::test_frozen_requests_preserve_distinct_files_and_latest_content()
{
    init_parser();
    mParser->setEnabled(true);
    const auto latest = std::make_shared<std::atomic<bool>>(false);
    mParser->setOnGetFileStream([latest](const QString &filename, QStringList &buffer) {
        if (filename.endsWith("deferred-first.cpp"))
            buffer = QStringList{latest->load() ? "int first_latest = 3;" : "int first_old = 1;"};
        else if (filename.endsWith("deferred-second.cpp")) buffer = QStringList{"int second_symbol = 2;"};
        else return false;
        return true;
    });
    QSignalSpy finished(mParser.get(), &CppParser::parseFinished);
    QVERIFY(finished.isValid());
    QVERIFY(mParser->freeze());
    QVERIFY(mParser->freeze());
    CppParser::parseFileNonBlocking(mParser, "deferred-first.cpp", false, "");
    CppParser::parseFileNonBlocking(mParser, "deferred-second.cpp", false, "");
    latest->store(true);
    CppParser::parseFileNonBlocking(mParser, "deferred-first.cpp", false, "");
    mParser->unFreeze(); // still frozen by the outer holder
    QCOMPARE(finished.count(), 0);
    QVERIFY(!mParser->findFileInfo("deferred-first.cpp"));
    mParser->unFreeze();
    QTRY_VERIFY_WITH_TIMEOUT(mParser->findStatement("first_latest"), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(mParser->findStatement("second_symbol"), 3000);
    QVERIFY(!mParser->findStatement("first_old"));
    QVERIFY(mParser->findFileInfo("deferred-first.cpp"));
    QVERIFY(mParser->findFileInfo("deferred-second.cpp"));
    QTRY_VERIFY_WITH_TIMEOUT(finished.count() >= 2, 3000);
}

void TestCppParser::test_split_expression_ignores_comments_data()
{
    QTest::addColumn<QString>("expression");
    QTest::addColumn<QStringList>("tokens");
    QTest::newRow("member") << QString("object /* .fake */ . value") << QStringList{"object", ".", "value"};
    QTest::newRow("multiline") << QString("object // .fake\n -> value") << QStringList{"object", "->", "value"};
    QTest::newRow("nested-template") << QString("vector<vector<int>>") << QStringList{"vector", "<", "vector", "<", "int", ">", ">"};
    QTest::newRow("unicode") << QString::fromUtf8("caffè.値") << QStringList{QString::fromUtf8("caffè"), ".", QString::fromUtf8("値")};
    QTest::newRow("unfinished-comment") << QString("object /* unfinished") << QStringList{"object"};
    QTest::newRow("empty") << QString("") << QStringList{};
}

void TestCppParser::test_split_expression_ignores_comments()
{
    QFETCH(QString, expression);
    QFETCH(QStringList, tokens);
    QCOMPARE(CppParser::splitExpression(expression), tokens);
}
// Candidate regression tests for TestCppParser. Add the two declarations to
// TestCppParser's private slots and these definitions to test_cppparser.cpp.
// TestCppParser is already a friend of CppParser: no public API is needed.

void TestCppParser::test_expired_inheritance_entry_is_removed_once()
{
    auto parser = std::make_shared<CppParser>();
    auto expiredDerived = std::make_shared<Statement>();
    auto liveDerived = std::make_shared<Statement>();
    auto expired = std::make_shared<ClassInheritanceInfo>();
    auto retained = std::make_shared<ClassInheritanceInfo>();
    expired->derivedClass = expiredDerived;
    retained->derivedClass = liveDerived;
    retained->handled = true; // No lookup is necessary for this live entry.
    parser->mClassInheritances = {expired, retained};
    expiredDerived.reset();
    QVERIFY(expired->derivedClass.expired());

    parser->handleInheritances();

    QCOMPARE(parser->mClassInheritances.size(), 1);
    QCOMPARE(parser->mClassInheritances.front().get(), retained.get());
    // A second cleanup must remain idempotent and not duplicate live records.
    parser->handleInheritances();
    QCOMPARE(parser->mClassInheritances.size(), 1);
}

void TestCppParser::test_inheritance_records_stay_bounded_after_reparse()
{
    auto parser = std::make_shared<CppParser>();
    parser->setOnGetFileStream([](const QString &filename, QStringList &lines) {
        if (filename.endsWith(QStringLiteral("inherited_old.cpp"))) {
            lines = QStringList{QStringLiteral("class FirstBase {};"),
                                QStringLiteral("class FirstDerived : public FirstBase {};")};
            return true;
        }
        if (filename.endsWith(QStringLiteral("inherited_anchor.cpp"))) {
            lines = QStringList{QStringLiteral("class AnchorBase {};"),
                                QStringLiteral("class AnchorDerived : public AnchorBase {};")};
            return true;
        }
        return false;
    });
    const QString oldFile = QStringLiteral("inherited_old.cpp");
    const QString anchorFile = QStringLiteral("inherited_anchor.cpp");
    CppParser::parseFileBlocking(parser, oldFile, false, QString());
    CppParser::parseFileBlocking(parser, anchorFile, false, QString());
    QCOMPARE(parser->mClassInheritances.size(), 2);
    for (int i = 0; i < 12; ++i) {
        CppParser::parseFileBlocking(parser, oldFile, false, QString());
        QCOMPARE(parser->mClassInheritances.size(), 2);
    }
}


// Concrete regression cases for CppParser::resetParsedResultsNonBlocking.
// Include <QTemporaryDir>, <QFile>, <QDir>, <QSignalSpy>, <QSemaphore>,
// <atomic>, and <thread> in the enclosing test_cppparser.cpp.

namespace {
class ScopedParserFreeze {
public:
    explicit ScopedParserFreeze(PCppParser parser) : parser(std::move(parser)) {}
    bool acquire() {
        if (!parser->freeze()) return false;
        ++held;
        return true;
    }
    void releaseOne() {
        if (held > 0) {
            --held;
            parser->unFreeze();
        }
    }
    ~ScopedParserFreeze() { while (held > 0) releaseOne(); }
private:
    PCppParser parser;
    int held = 0;
};

bool writeUtf8(const QString &path, const QByteArray &contents) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}
}

void TestCppParser::test_reset_barrier_nested_freeze_and_explicit_requests()
{
    init_parser();
    mParser->setEnabled(true);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString header = QDir::fromNativeSeparators(dir.filePath("queued_b.h"));
    const QString source = QDir::fromNativeSeparators(dir.filePath("queued_c.cpp"));
    QVERIFY(writeUtf8(header, "// The test supplies current contents through the callback.\n"));
    const auto headerReads = std::make_shared<std::atomic<int>>(0);
    const auto resetDone = std::make_shared<std::atomic<bool>>(false);
    const auto parseBeforeReset = std::make_shared<std::atomic<bool>>(false);
    QObject::connect(mParser.get(), &CppParser::cacheResetFinished, mParser.get(),
                     [resetDone] { resetDone->store(true); }, Qt::DirectConnection);
    mParser->setOnGetFileStream([headerReads, resetDone, parseBeforeReset]
                                (const QString &filename, QStringList &lines) {
        if (filename.endsWith(QStringLiteral("old-before-reset.cpp"))) {
            lines = QStringList{QStringLiteral("int stale_before_barrier = 1;")};
        } else if (filename.endsWith(QStringLiteral("queued_c.cpp"))) {
            if (!resetDone->load()) parseBeforeReset->store(true);
            lines = QStringList{QStringLiteral("#include \"queued_b.h\""),
                                QStringLiteral("int queued_c_symbol = 2;")};
        } else if (filename.endsWith(QStringLiteral("queued_b.h"))) {
            if (!resetDone->load()) parseBeforeReset->store(true);
            const int n = headerReads->fetch_add(1);
            lines = QStringList{n == 0 ? QStringLiteral("int queued_b_old = 1;")
                                       : QStringLiteral("int queued_b_latest = 2;")};
        } else return false;
        return true;
    });
    CppParser::parseFileBlocking(mParser, QStringLiteral("old-before-reset.cpp"), false, QString());
    {
        ScopedParserFreeze snapshot(mParser);
        QVERIFY(snapshot.acquire());
        QVERIFY(mParser->findStatement(QStringLiteral("stale_before_barrier")));
    }
    QSignalSpy finished(mParser.get(), &CppParser::parseFinished);
    QVERIFY(finished.isValid());
    QSignalSpy reset(mParser.get(), &CppParser::cacheResetFinished);
    QVERIFY(reset.isValid());
    QSignalSpy resetStarted(mParser.get(), &CppParser::cacheResetStarted);
    QVERIFY(resetStarted.isValid());
    ScopedParserFreeze frozen(mParser);
    QVERIFY(frozen.acquire());
    QVERIFY(frozen.acquire());

    // The first queued parse includes B.  B is then requested both with the
    // skip-if-parsed flag and explicitly.  The merged explicit request must
    // re-read B after C has already scanned the header.
    CppParser::resetParsedResultsNonBlocking(mParser);
    CppParser::resetParsedResultsNonBlocking(mParser);
    CppParser::parseFileNonBlocking(mParser, source, false, QString());
    CppParser::parseFileNonBlocking(mParser, header, false, QString(), true, false);
    CppParser::parseFileNonBlocking(mParser, header, false, QString(), false, true);
    QCoreApplication::processEvents();
    QCOMPARE(finished.count(), 0);
    QCOMPARE(reset.count(), 0);
    QCOMPARE(resetStarted.count(), 0);
    QVERIFY(mParser->findStatement(QStringLiteral("stale_before_barrier")));
    frozen.releaseOne();
    QCoreApplication::processEvents();
    QCOMPARE(finished.count(), 0); // the outer reader still owns the parser
    QCOMPARE(reset.count(), 0);
    QCOMPARE(resetStarted.count(), 0);
    frozen.releaseOne();

    QTRY_VERIFY_WITH_TIMEOUT(reset.count() >= 1, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(finished.count() >= 2, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(!mParser->hasPendingWork(), 10000);
    {
        ScopedParserFreeze snapshot(mParser);
        QVERIFY(snapshot.acquire());
        QVERIFY(mParser->findStatement(QStringLiteral("queued_c_symbol")));
        QVERIFY(mParser->findStatement(QStringLiteral("queued_b_latest")));
        QVERIFY(!mParser->findStatement(QStringLiteral("stale_before_barrier")));
        QVERIFY(!mParser->findStatement(QStringLiteral("queued_b_old")));
    }
    QCOMPARE(headerReads->load(), 2);
    QCOMPARE(reset.count(), 1); // repeated reset requests coalesced
    QCOMPARE(resetStarted.count(), 1);
    QVERIFY2(!parseBeforeReset->load(), "A pending parse ran before the reset barrier completed");
}

void TestCppParser::test_reset_barrier_busy_parser_retains_other_files()
{
    init_parser();
    mParser->setEnabled(true);
    const auto entered = std::make_shared<QSemaphore>();
    const auto resume = std::make_shared<QSemaphore>();
    const auto completedActive = std::make_shared<std::atomic<bool>>(false);
    const auto newest = std::make_shared<std::atomic<bool>>(false);
    const auto resetDone = std::make_shared<std::atomic<bool>>(false);
    const auto parseBeforeReset = std::make_shared<std::atomic<bool>>(false);
    QObject::connect(mParser.get(), &CppParser::cacheResetFinished, mParser.get(),
                     [resetDone] { resetDone->store(true); }, Qt::DirectConnection);
    mParser->setOnGetFileStream([entered, resume, completedActive, newest, resetDone, parseBeforeReset]
                                (const QString &filename, QStringList &lines) {
        if (filename.endsWith(QStringLiteral("barrier_active.cpp"))) {
            if (!completedActive->load()) {
                entered->release();
                if (!resume->tryAcquire(1, 15000)) return false;
            }
            lines = QStringList{QStringLiteral("int active_before_reset = 1;")};
            completedActive->store(true);
        } else if (filename.endsWith(QStringLiteral("barrier_b.cpp"))) {
            if (!resetDone->load()) parseBeforeReset->store(true);
            lines = QStringList{newest->load() ? QStringLiteral("int barrier_b_latest = 2;")
                                              : QStringLiteral("int barrier_b_old = 2;")};
        } else if (filename.endsWith(QStringLiteral("barrier_c.cpp"))) {
            if (!resetDone->load()) parseBeforeReset->store(true);
            lines = QStringList{QStringLiteral("int barrier_c = 3;")};
        } else return false;
        return true;
    });
    QSignalSpy finished(mParser.get(), &CppParser::parseFinished);
    QVERIFY(finished.isValid());
    QSignalSpy reset(mParser.get(), &CppParser::cacheResetFinished);
    QVERIFY(reset.isValid());
    QSignalSpy resetStarted(mParser.get(), &CppParser::cacheResetStarted);
    QVERIFY(resetStarted.isValid());
    const auto parser = mParser;
    std::thread active([parser] {
        CppParser::parseFileBlocking(parser, QStringLiteral("barrier_active.cpp"), false, QString());
    });
    const bool started = entered->tryAcquire(1, 5000);
    if (started) {
        CppParser::resetParsedResultsNonBlocking(parser);
        CppParser::resetParsedResultsNonBlocking(parser);
        CppParser::parseFileNonBlocking(parser, QStringLiteral("barrier_b.cpp"), false, QString());
        CppParser::parseFileNonBlocking(parser, QStringLiteral("barrier_c.cpp"), false, QString());
        newest->store(true);
        CppParser::parseFileNonBlocking(parser, QStringLiteral("barrier_b.cpp"), false, QString());
    }
    // Release and join before using any QTest assertion, including error paths.
    resume->release();
    active.join();
    QVERIFY2(started, "Active parse never entered the controlled callback");
    QVERIFY2(completedActive->load(), "Reset unexpectedly interrupted the active callback");
    QTRY_VERIFY_WITH_TIMEOUT(reset.count() >= 1, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(finished.count() >= 4, 10000); // A + reset replay + B + C
    QTRY_VERIFY_WITH_TIMEOUT(!mParser->hasPendingWork(), 10000);
    {
        ScopedParserFreeze snapshot(mParser);
        QVERIFY(snapshot.acquire());
        QVERIFY(mParser->findStatement(QStringLiteral("barrier_b_latest")));
        QVERIFY(mParser->findStatement(QStringLiteral("barrier_c")));
        QVERIFY(!mParser->findStatement(QStringLiteral("barrier_b_old")));
        QVERIFY(mParser->findStatement(QStringLiteral("active_before_reset")));
    }
    QCOMPARE(reset.count(), 1);
    QCOMPARE(resetStarted.count(), 1);
    QVERIFY2(!parseBeforeReset->load(), "A pending parse ran before the reset barrier completed");
}

void TestCppParser::test_reset_barrier_preserves_configuration_only()
{
    init_parser();
    mParser->setEnabled(true);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString first = QDir::fromNativeSeparators(dir.filePath("first"));
    const QString second = QDir::fromNativeSeparators(dir.filePath("second"));
    QVERIFY(QDir().mkpath(first));
    QVERIFY(QDir().mkpath(second));
    QVERIFY(writeUtf8(first + QStringLiteral("/choice.h"), "int config_header_first = 1;\n"));
    QVERIFY(writeUtf8(second + QStringLiteral("/choice.h"), "int config_header_second = 2;\n"));
    mParser->addIncludePath(first);
    mParser->addIncludePath(second);
    const auto includeOrder = mParser->mPreprocessor.includePathList();
    mParser->addHardDefineByLine(QStringLiteral("#define RESET_KEEP_HARD 73"));
    mParser->parseHardDefines();
    const QString oldFile = QDir::fromNativeSeparators(dir.filePath("before.cpp"));
    const QString newFile = QDir::fromNativeSeparators(dir.filePath("after.cpp"));
    mParser->setOnGetFileStream([](const QString &filename, QStringList &lines) {
        if (filename.endsWith(QStringLiteral("before.cpp"))) {
            lines = QStringList{QStringLiteral("#define RESET_SOFT_LEAK 1"),
                                QStringLiteral("int stale_config_result = 1;")};
        } else if (filename.endsWith(QStringLiteral("after.cpp"))) {
            lines = QStringList{QStringLiteral("#include <choice.h>"),
                                QStringLiteral("#ifdef RESET_KEEP_HARD"),
                                QStringLiteral("int config_hard_define_kept = 1;"),
                                QStringLiteral("#endif"),
                                QStringLiteral("#ifdef RESET_SOFT_LEAK"),
                                QStringLiteral("int config_soft_define_leaked = 1;"),
                                QStringLiteral("#else"),
                                QStringLiteral("int config_soft_define_removed = 1;"),
                                QStringLiteral("#endif")};
        } else return false; // include contents are read from the temporary files
        return true;
    });
    CppParser::parseFileBlocking(mParser, oldFile, false, QString());
    {
        ScopedParserFreeze snapshot(mParser);
        QVERIFY(snapshot.acquire());
        QVERIFY(mParser->findStatement(QStringLiteral("stale_config_result")));
    }
    QSignalSpy finished(mParser.get(), &CppParser::parseFinished);
    QVERIFY(finished.isValid());
    QSignalSpy reset(mParser.get(), &CppParser::cacheResetFinished);
    QVERIFY(reset.isValid());
    QSignalSpy resetStarted(mParser.get(), &CppParser::cacheResetStarted);
    QVERIFY(resetStarted.isValid());
    CppParser::resetParsedResultsNonBlocking(mParser);
    CppParser::parseFileNonBlocking(mParser, newFile, false, QString());
    QTRY_VERIFY_WITH_TIMEOUT(reset.count() >= 1, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(finished.count() >= 1, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(!mParser->hasPendingWork(), 10000);
    {
        ScopedParserFreeze snapshot(mParser);
        QVERIFY(snapshot.acquire());
        QVERIFY(mParser->findStatement(QStringLiteral("config_hard_define_kept")));
        QVERIFY(mParser->findStatement(QStringLiteral("config_soft_define_removed")));
        QVERIFY(mParser->findStatement(QStringLiteral("config_header_first")));
        QVERIFY(!mParser->findStatement(QStringLiteral("stale_config_result")));
        QVERIFY(!mParser->findStatement(QStringLiteral("config_soft_define_leaked")));
        QVERIFY(!mParser->findStatement(QStringLiteral("config_header_second")));
        QCOMPARE(mParser->mPreprocessor.includePathList(), includeOrder);
        QVERIFY(mParser->mPreprocessor.hardDefines().contains(QStringLiteral("RESET_KEEP_HARD")));
    }
    QCOMPARE(reset.count(), 1);
    QCOMPARE(resetStarted.count(), 1);
}

// TestCppParser is a friend of CppParser. These tests intentionally use its
// mutex and dispatch entry point to control the precise worker-start race,
// without introducing a production hook. Run with a process-level watchdog.

void TestCppParser::test_reset_after_dispatch_before_parse_preserves_request()
{
    init_parser();
    mParser->setEnabled(true);
    const auto reads = std::make_shared<std::atomic<int>>(0);
    const auto resetDone = std::make_shared<std::atomic<bool>>(false);
    const auto readBeforeReset = std::make_shared<std::atomic<bool>>(false);
    QObject::connect(mParser.get(), &CppParser::cacheResetFinished, mParser.get(),
                     [resetDone] { resetDone->store(true); }, Qt::DirectConnection);
    mParser->setOnGetFileStream([reads, resetDone, readBeforeReset]
                                (const QString &filename, QStringList &lines) {
        if (!filename.endsWith(QStringLiteral("selected-before-reset.cpp"))) return false;
        ++*reads;
        if (!resetDone->load()) readBeforeReset->store(true);
        lines = QStringList{QStringLiteral("int selected_after_reset = 1;")};
        return true;
    });
    QSignalSpy reset(mParser.get(), &CppParser::cacheResetFinished);
    QVERIFY(reset.isValid());
    bool dispatched = false;
    {
        // Worker may start but cannot enter parseFile's mutex-protected check.
        // The recursive mutex lets this GUI/test thread enqueue A, dispatch it,
        // and enqueue the barrier while still holding the same lock.
        QMutexLocker lock(&mParser->mMutex);
        CppParser::parseFileNonBlocking(mParser, QStringLiteral("selected-before-reset.cpp"),
                                        false, QString());
        mParser->startPendingParseCommands();
        dispatched = mParser->mPendingWorkerActive;
        CppParser::resetParsedResultsNonBlocking(mParser);
    }
    QVERIFY2(dispatched, "The file worker was not dispatched while the mutex was held");
    QTRY_COMPARE_WITH_TIMEOUT(reset.count(), 1, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(!mParser->hasPendingWork(), 10000);
    QVERIFY2(!readBeforeReset->load(), "The selected file ran before the reset barrier");
    QCOMPARE(reads->load(), 1);
    ScopedParserFreeze snapshot(mParser);
    QVERIFY(snapshot.acquire());
    QVERIFY2(mParser->findStatement(QStringLiteral("selected_after_reset")),
             "The selected explicit file was lost across the reset");
}

void TestCppParser::test_reset_during_active_parse_preserves_request()
{
    init_parser();
    mParser->setEnabled(true);
    const auto entered = std::make_shared<QSemaphore>();
    const auto resume = std::make_shared<QSemaphore>();
    const auto reads = std::make_shared<std::atomic<int>>(0);
    const auto resetDone = std::make_shared<std::atomic<bool>>(false);
    const auto secondReadBeforeReset = std::make_shared<std::atomic<bool>>(false);
    QObject::connect(mParser.get(), &CppParser::cacheResetFinished, mParser.get(),
                     [resetDone] { resetDone->store(true); }, Qt::DirectConnection);
    mParser->setOnGetFileStream([entered, resume, reads, resetDone, secondReadBeforeReset]
                                (const QString &filename, QStringList &lines) {
        if (!filename.endsWith(QStringLiteral("active-during-reset.cpp"))) return false;
        const int read = reads->fetch_add(1);
        if (read == 0) {
            entered->release();
            if (!resume->tryAcquire(1, 10000)) return false;
        } else if (!resetDone->load()) {
            secondReadBeforeReset->store(true);
        }
        lines = QStringList{QStringLiteral("int active_after_reset = 1;")};
        return true;
    });
    struct Unblock {
        std::shared_ptr<QSemaphore> semaphore;
        ~Unblock() { semaphore->release(); }
    } unblock{resume};
    QSignalSpy reset(mParser.get(), &CppParser::cacheResetFinished);
    QVERIFY(reset.isValid());
    CppParser::parseFileNonBlocking(mParser, QStringLiteral("active-during-reset.cpp"),
                                    false, QString());
    QTRY_VERIFY_WITH_TIMEOUT(entered->available() > 0, 5000);
    CppParser::resetParsedResultsNonBlocking(mParser);
    resume->release();
    QTRY_COMPARE_WITH_TIMEOUT(reset.count(), 1, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(!mParser->hasPendingWork(), 10000);
    QCOMPARE(reads->load(), 2); // in-flight result plus the post-barrier replay
    QVERIFY2(!secondReadBeforeReset->load(), "The replay ran before the reset completed");
    ScopedParserFreeze snapshot(mParser);
    QVERIFY(snapshot.acquire());
    QVERIFY2(mParser->findStatement(QStringLiteral("active_after_reset")),
             "The in-flight explicit request was lost across the reset");
}

void TestCppParser::test_reset_replay_keeps_newer_context()
{
    init_parser();
    mParser->setEnabled(true);
    const auto entered = std::make_shared<QSemaphore>();
    const auto resume = std::make_shared<QSemaphore>();
    const auto oldReads = std::make_shared<std::atomic<int>>(0);
    mParser->setOnGetFileStream([entered, resume, oldReads](const QString &filename, QStringList &lines) {
        if (filename.endsWith(QStringLiteral("old_context.cpp"))) {
            if (oldReads->fetch_add(1) == 0) {
                entered->release();
                if (!resume->tryAcquire(1, 10000)) return false;
            }
            lines = {QStringLiteral("int old_context_symbol = 1;")};
        } else if (filename.endsWith(QStringLiteral("new_context.cpp"))) {
            lines = {QStringLiteral("int new_context_symbol = 1;")};
        } else if (filename.endsWith(QStringLiteral("same_header.h"))) {
            lines = {QStringLiteral("int shared_header_symbol = 1;")};
        } else return false;
        return true;
    });
    struct Unblock { std::shared_ptr<QSemaphore> s; ~Unblock() { s->release(); } } unblock{resume};
    CppParser::parseFileNonBlocking(mParser, QStringLiteral("same_header.h"), false,
                                    QStringLiteral("old_context.cpp"));
    QTRY_VERIFY_WITH_TIMEOUT(entered->available() > 0, 5000);
    CppParser::parseFileNonBlocking(mParser, QStringLiteral("same_header.h"), false,
                                    QStringLiteral("new_context.cpp"));
    CppParser::resetParsedResultsNonBlocking(mParser);
    resume->release();
    QTRY_VERIFY_WITH_TIMEOUT(!mParser->hasPendingWork(), 10000);
    QCOMPARE(oldReads->load(), 1);
    ScopedParserFreeze snapshot(mParser);
    QVERIFY(snapshot.acquire());
    QVERIFY(mParser->findStatement(QStringLiteral("new_context_symbol")));
    QVERIFY(mParser->findStatement(QStringLiteral("shared_header_symbol")));
    QVERIFY(!mParser->findStatement(QStringLiteral("old_context_symbol")));
}

void TestCppParser::test_reset_active_project_dependencies_data()
{
    QTest::addColumn<bool>("changeProjectFiles");
    QTest::newRow("keep-dependent") << false;
    QTest::newRow("remove-dependent-and-add-file") << true;
}

void TestCppParser::test_reset_active_project_dependencies()
{
    QFETCH(bool, changeProjectFiles);
    init_parser();
    mParser->setEnabled(true);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString header = QDir::fromNativeSeparators(dir.filePath("a.h"));
    const QString dependent = QDir::fromNativeSeparators(dir.filePath("b.cpp"));
    const QString added = QDir::fromNativeSeparators(dir.filePath("c.cpp"));
    QVERIFY(writeUtf8(header, "int header_symbol = 1;\n"));
    QVERIFY(writeUtf8(dependent, "#include \"a.h\"\nint dependent_symbol = 1;\n"));
    QVERIFY(writeUtf8(added, "int added_symbol = 1;\n"));
    const auto entered = std::make_shared<QSemaphore>();
    const auto resume = std::make_shared<QSemaphore>();
    const auto armed = std::make_shared<std::atomic<bool>>(false);
    mParser->setOnGetFileStream([entered, resume, armed](const QString &filename, QStringList &lines) {
        if (filename.endsWith(QStringLiteral("a.h"))) {
            if (armed->exchange(false)) {
                entered->release();
                if (!resume->tryAcquire(1, 10000)) return false;
            }
            lines = {QStringLiteral("int header_symbol = 1;")};
        } else if (filename.endsWith(QStringLiteral("b.cpp"))) {
            lines = {QStringLiteral("#include \"a.h\""), QStringLiteral("int dependent_symbol = 1;")};
        } else if (filename.endsWith(QStringLiteral("c.cpp"))) {
            lines = {QStringLiteral("int added_symbol = 1;")};
        } else return false;
        return true;
    });
    struct Unblock { std::shared_ptr<QSemaphore> s; ~Unblock() { s->release(); } } unblock{resume};
    mParser->addProjectFile(header, true);
    mParser->addProjectFile(dependent, true);
    CppParser::parseFileListNonBlocking(mParser);
    QTRY_VERIFY_WITH_TIMEOUT(!mParser->hasPendingWork(), 10000);
    {
        ScopedParserFreeze snapshot(mParser);
        QVERIFY(snapshot.acquire());
        QVERIFY(mParser->findStatement(QStringLiteral("header_symbol")));
        QVERIFY(mParser->findStatement(QStringLiteral("dependent_symbol")));
        QVERIFY2(mParser->calculateFilesToBeReparsed(header).contains(dependent),
                 "The fixture must establish that B includes A");
    }

    armed->store(true);
    CppParser::parseFileNonBlocking(mParser, header, true, QString());
    QTRY_VERIFY_WITH_TIMEOUT(entered->available() > 0, 5000);
    CppParser::resetParsedResultsNonBlocking(mParser);
    if (changeProjectFiles) {
        mParser->removeProjectFile(dependent);
        mParser->addProjectFile(added, true);
    }
    resume->release();
    QTRY_VERIFY_WITH_TIMEOUT(!mParser->hasPendingWork(), 10000);
    ScopedParserFreeze snapshot(mParser);
    QVERIFY(snapshot.acquire());
    QVERIFY(mParser->findStatement(QStringLiteral("header_symbol")));
    if (changeProjectFiles) {
        QVERIFY(!mParser->findStatement(QStringLiteral("dependent_symbol")));
        QVERIFY(mParser->findStatement(QStringLiteral("added_symbol")));
    } else {
        QVERIFY(mParser->findStatement(QStringLiteral("dependent_symbol")));
        QVERIFY(!mParser->findStatement(QStringLiteral("added_symbol")));
    }
}


void TestCppParser::test_reset_during_active_project_list_replays_all()
{
    init_parser();
    mParser->setEnabled(true);
    const auto entered = std::make_shared<QSemaphore>();
    const auto resume = std::make_shared<QSemaphore>();
    const auto reads = std::make_shared<std::atomic<int>>(0);
    mParser->setOnGetFileStream([entered, resume, reads](const QString &filename, QStringList &lines) {
        if (!filename.endsWith(QStringLiteral("project_a.cpp")) &&
            !filename.endsWith(QStringLiteral("project_b.cpp"))) return false;
        if (reads->fetch_add(1) == 0) {
            entered->release();
            if (!resume->tryAcquire(1, 10000)) return false;
        }
        lines = {filename.endsWith(QStringLiteral("project_a.cpp"))
                     ? QStringLiteral("int project_symbol_a = 1;")
                     : QStringLiteral("int project_symbol_b = 1;")};
        return true;
    });
    struct Unblock { std::shared_ptr<QSemaphore> s; ~Unblock() { s->release(); } } unblock{resume};
    mParser->addProjectFile(QStringLiteral("project_a.cpp"), true);
    mParser->addProjectFile(QStringLiteral("project_b.cpp"), true);
    CppParser::parseFileListNonBlocking(mParser);
    QTRY_VERIFY_WITH_TIMEOUT(entered->available() > 0, 5000);
    CppParser::resetParsedResultsNonBlocking(mParser);
    resume->release();
    QTRY_VERIFY_WITH_TIMEOUT(!mParser->hasPendingWork(), 10000);
    QVERIFY(reads->load() >= 4);
    ScopedParserFreeze snapshot(mParser);
    QVERIFY(snapshot.acquire());
    QVERIFY(mParser->findStatement(QStringLiteral("project_symbol_a")));
    QVERIFY(mParser->findStatement(QStringLiteral("project_symbol_b")));
}

void TestCppParser::test_reset_barrier_reports_synchronous_parse_busy()
{
    init_parser();
    mParser->setEnabled(true);
    const auto reads = std::make_shared<std::atomic<int>>(0);
    mParser->setOnGetFileStream([reads](const QString &filename, QStringList &lines) {
        if (!filename.endsWith(QStringLiteral("blocking-after-reset.cpp")))
            return false;
        ++*reads;
        lines = QStringList{QStringLiteral("int blocking_after_reset = 1;")};
        return true;
    });
    QSignalSpy reset(mParser.get(), &CppParser::cacheResetFinished);
    QSignalSpy started(mParser.get(), &CppParser::parseStarted);
    QVERIFY(reset.isValid());
    QVERIFY(started.isValid());
    ScopedParserFreeze frozen(mParser);
    QVERIFY(frozen.acquire());
    CppParser::resetParsedResultsNonBlocking(mParser);
    QElapsedTimer elapsed;
    elapsed.start();
    const auto result = CppParser::parseFileBlocking(
        mParser, QStringLiteral("blocking-after-reset.cpp"), false, QString());
    QVERIFY(result == CppParser::ParseResult::Busy);
    QVERIFY2(elapsed.elapsed() < 2000, "Busy must return promptly without a nested event loop");
    QCOMPARE(reads->load(), 0);
    QCOMPARE(started.count(), 0);
    QVERIFY(!mParser->freezeStable());
    frozen.releaseOne();
    const auto pendingResult = CppParser::parseFileBlocking(
        mParser, QStringLiteral("blocking-after-reset.cpp"), false, QString());
    QVERIFY(pendingResult == CppParser::ParseResult::Busy);
    QCOMPARE(reads->load(), 0);
    QCOMPARE(started.count(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(reset.count(), 1, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(!mParser->hasPendingWork(), 10000);
    QCOMPARE(reads->load(), 0);
    QCOMPARE(started.count(), 0);
    const auto retry = CppParser::parseFileBlocking(
        mParser, QStringLiteral("blocking-after-reset.cpp"), false, QString());
    QVERIFY(retry == CppParser::ParseResult::Parsed);
    QCOMPARE(reads->load(), 1);
    ScopedParserFreeze snapshot(mParser);
    QVERIFY(snapshot.acquire());
    QVERIFY(mParser->findStatement(QStringLiteral("blocking_after_reset")));
}

// A reset may enter after the sync reservation but before parseFile consumes it.
void TestCppParser::test_sync_reservation_reset_excludes_parse()
{
    init_parser();
    mParser->setEnabled(true);
    auto parser = mParser;
    auto entered = std::make_shared<QSemaphore>();
    auto resume = std::make_shared<QSemaphore>();
    auto reads = std::make_shared<std::atomic<int>>(0);
    parser->setOnGetFileStream([reads](const QString &, QStringList &lines) {
        ++*reads;
        lines = {QStringLiteral("int reset_reservation_symbol;")};
        return true;
    });
    const auto connection = QObject::connect(parser.get(), &CppParser::onBusy, parser.get(), [entered, resume] {
        entered->release();
        resume->acquire();
    }, Qt::DirectConnection);
    {
        QMutexLocker locker(&parser->mMutex);
        parser->mSyncParseReserved = true;
    }
    std::thread reset([parser] { parser->resetParser(); });
    struct ReleaseAndJoin {
        std::shared_ptr<QSemaphore> resume;
        std::thread &thread;
        QMetaObject::Connection connection;
        ~ReleaseAndJoin() {
            QObject::disconnect(connection);
            resume->release();
            if (thread.joinable()) thread.join();
        }
    } cleanup{resume, reset, connection};
    QVERIFY2(entered->tryAcquire(1, 5000), "Reset did not enter while the reservation was held");
    const bool parsed = parser->parseFile(QStringLiteral("reset_reservation.cpp"), false,
                                          QString(), false, false, true);
    QVERIFY(!parsed);
    QCOMPARE(reads->load(), 0);
    {
        QMutexLocker locker(&parser->mMutex);
        QVERIFY(parser->mParsing);
        QVERIFY(!parser->mSyncParseReserved);
    }
}

// Hard defines currently run before a reserved parse, but under the same mutex.
void TestCppParser::test_sync_reservation_hard_defines_serialize()
{
    init_parser();
    mParser->setEnabled(true);
    auto parser = mParser;
    parser->addHardDefineByLine(QStringLiteral("#define RESERVED_HARD_DEFINE 1"));
    parser->setOnGetFileStream([](const QString &, QStringList &lines) {
        lines = {QStringLiteral("int after_reserved_define;")};
        return true;
    });
    {
        QMutexLocker locker(&parser->mMutex);
        parser->mSyncParseReserved = true;
    }
    std::thread defines([parser] { parser->parseHardDefines(); });
    defines.join();
    QVERIFY(parser->findStatement(QStringLiteral("RESERVED_HARD_DEFINE")));
    {
        QMutexLocker locker(&parser->mMutex);
        QVERIFY(parser->mSyncParseReserved);
        QVERIFY(!parser->mParsing);
    }
    QVERIFY(parser->parseFile(QStringLiteral("hard_define_reservation.cpp"), false,
                              QString(), false, false, true));
    QVERIFY(parser->findStatement(QStringLiteral("after_reserved_define")));
}
