#include <QTest>
#include <QSignalSpy>
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
    // Blocking entry point reaches the parser synchronously, so the request is
    // definitely made while frozen; there is no reliance on worker scheduling.
    CppParser::parseFileBlocking(mParser, "deferred-one.cpp", false, "");
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
    CppParser::parseFileBlocking(mParser, "deferred-first.cpp", false, "");
    CppParser::parseFileBlocking(mParser, "deferred-second.cpp", false, "");
    latest->store(true);
    CppParser::parseFileBlocking(mParser, "deferred-first.cpp", false, "");
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
