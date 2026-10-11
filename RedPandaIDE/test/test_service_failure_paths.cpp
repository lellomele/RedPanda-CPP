#include "test_service_failure_paths.h"
#include "src/sessionstore.h"
#include "src/snippetsstorage.h"
#include "src/diagnostics.h"
#include <QTest>
#include <QFile>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <random>

static QByteArray readFile(const QString &path) {
    QFile f(path); if (!f.open(QIODevice::ReadOnly)) return {}; return f.readAll();
}
void TestServiceFailurePaths::recovery_rejects_corrupt_inputs_without_replacing_state() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    QList<RecoveryDocument> previous{{"keep.cpp", "unsaved text", 3, 7}};
    const QString path=dir.filePath("recovery.json");
    const QList<QByteArray> cases{"", "{", "[]", "{}", "null", R"({"version":2,"documents":[]})", R"({"version":1,"documents":[null]})", R"({"version":1,"documents":[{"filename":0,"text":"x"}]})", R"({"version":1,"documents":[{"filename":"x","text":false}]})"};
    for (const auto &bytes:cases) {
        QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly)); QCOMPARE(f.write(bytes), qint64(bytes.size())); f.close();
        QString error; QVERIFY(!SessionStore::read(path,previous,error)); QVERIFY(!error.isEmpty());
        QCOMPARE(previous.size(),1); QCOMPARE(previous[0].filename,QString("keep.cpp")); QCOMPARE(previous[0].text,QString("unsaved text")); QCOMPARE(previous[0].line,3); QCOMPARE(previous[0].column,7);
    }
}
void TestServiceFailurePaths::recovery_unicode_round_trip_and_many_documents() {
    QTemporaryDir dir; QList<RecoveryDocument> docs;
    for (int i=0;i<256;++i) docs.append({QString::fromUtf8("cartella è/文件-%1.cpp").arg(i),QString::fromUtf8("αβ🙂\r\n\t\"\\\n")+QString::number(i),i,i+1});
    QString error="stale"; const auto path=dir.filePath("session.json");
    QVERIFY2(SessionStore::write(path,docs,error),qPrintable(error)); QVERIFY(error.isEmpty());
    QList<RecoveryDocument> actual; QVERIFY(SessionStore::read(path,actual,error)); QCOMPARE(actual.size(),docs.size());
    for(int i=0;i<docs.size();++i) { QCOMPARE(actual[i].filename,docs[i].filename); QCOMPARE(actual[i].text,docs[i].text); QCOMPARE(actual[i].line,docs[i].line); QCOMPARE(actual[i].column,docs[i].column); }
    const auto before=readFile(path); docs.append({"overflow","",0,0});
    QVERIFY(!SessionStore::write(path,docs,error)); QCOMPARE(readFile(path),before);
    QVERIFY(!SessionStore::write(dir.path(),{},error)); QCOMPARE(readFile(path),before);
}
void TestServiceFailurePaths::recovery_coordinates_reject_bad_metadata() {
    QTemporaryDir dir; const auto path=dir.filePath("session.json");
    for (const QJsonValue &value : {QJsonValue("3"),QJsonValue(true),QJsonValue(1.5),QJsonValue(1e30),QJsonValue(QJsonValue::Null)}) {
        for (const auto &field : {QString("line"),QString("column")}) {
            QJsonObject doc{{"filename","main.cpp"},{"text","keep"},{"line",0},{"column",0}}; doc[field]=value;
            const auto bytes=QJsonDocument(QJsonObject{{"version",1},{"documents",QJsonArray{doc}}}).toJson();
            QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly)); QCOMPARE(f.write(bytes),qint64(bytes.size())); f.close();
            QList<RecoveryDocument> previous{{"old","old text",0,0}}; QString error;
            QVERIFY(!SessionStore::read(path,previous,error)); QCOMPARE(previous[0].text,QString("old text"));
        }
    }
}
void TestServiceFailurePaths::snippet_read_write_failure_is_transactional() {
    QTemporaryDir dir; auto snippet=std::make_shared<CodeSnippet>(); snippet->caption="Example"; snippet->prefix="example"; snippet->code="int value = %REPL_BEGIN%42%REPL_END%;"; snippet->section=0;
    QList<PCodeSnippet> library{snippet}; QString error; const auto path=dir.filePath("snippets.json");
    QVERIFY(SnippetStorage::write(path,library,error)); const auto before=readFile(path);
    const auto original=library[0].get();
    QVERIFY(!SnippetStorage::read(dir.filePath("missing.json"),library,error)); QCOMPARE(library[0].get(),original);
    QVERIFY(!SnippetStorage::write(dir.path(),library,error)); QCOMPARE(readFile(path),before);
    snippet->code="%REPL_END%"; QVERIFY(!SnippetStorage::write(path,library,error)); QCOMPARE(readFile(path),before);
    const auto oversized=QByteArray(SnippetStorage::MaxFileBytes+1,'x'); QVERIFY(!SnippetStorage::decode(oversized,library,error)); QCOMPARE(library[0].get(),original);
}
void TestServiceFailurePaths::deterministic_malformed_library_corpus() {
    auto snippet=std::make_shared<CodeSnippet>(); snippet->caption="Keep"; snippet->prefix="keep"; snippet->code="keep"; snippet->section=0;
    QList<PCodeSnippet> previous{snippet}; std::mt19937 random(0x350);
    for(int i=0;i<400;++i) {
        QByteArray bytes; const int length=1+random()%256;
        for(int j=0;j<length;++j) bytes.append(char(random()%256));
        bytes.prepend('{'); // Never a valid complete versioned library.
        QString error; QVERIFY(!SnippetStorage::decode(bytes,previous,error)); QVERIFY(!error.isEmpty()); QCOMPARE(previous.size(),1); QCOMPARE(previous[0].get(),snippet.get());
    }
}
static int forwardedMessages=0;
static void previousHandler(QtMsgType,const QMessageLogContext &,const QString &) { ++forwardedMessages; }
void TestServiceFailurePaths::diagnostic_handler_reinstall_restores_previous_handler() {
    QTemporaryDir dir; auto previous=qInstallMessageHandler(previousHandler); forwardedMessages=0;
    Diagnostics::install(dir.filePath("first.log")); qWarning("first diagnostic");
    Diagnostics::install(dir.filePath("second.log")); qWarning("second diagnostic");
    Diagnostics::uninstall(); const int before=forwardedMessages; qWarning("after uninstall");
    const bool forwarded=forwardedMessages==before+1;
    const auto second=readFile(dir.filePath("second.log"));
    Diagnostics::uninstall(); qInstallMessageHandler(previous);
    QVERIFY(forwarded); QVERIFY(second.contains("second diagnostic")); QVERIFY(!second.contains("after uninstall"));
}

void TestServiceFailurePaths::snippet_replacement_ranges_reject_unsupported_layouts() {
    auto snippet = std::make_shared<CodeSnippet>(); snippet->caption="Keep"; snippet->prefix="keep"; snippet->section=0;
    for (const auto &code : {QString("%REPL_BEGIN%line1\nline2%REPL_END%"),QString("%REPL_BEGIN%line1\rline2%REPL_END%"),QString("%REPL_BEGIN%value%INSERT%%REPL_END%")}) {
        snippet->code = code; QString error; QVERIFY(!SnippetStorage::validate({snippet},error)); QVERIFY(!error.isEmpty());
    }
    snippet->code="%REPL_BEGIN%value%REPL_END%\n%INSERT%"; QString error; QVERIFY(SnippetStorage::validate({snippet},error));
}
