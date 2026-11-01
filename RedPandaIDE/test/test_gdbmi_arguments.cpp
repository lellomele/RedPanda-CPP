// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)
#include <QTest>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QProcess>
#include <QTemporaryDir>
#include "src/debugger/gdbmiarguments.h"
#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

class TestGdbmiArguments : public QObject {
    Q_OBJECT
private slots:
    void expression_arguments_data();
    void expression_arguments();
    void real_gdb_watch_workflow();
};

void TestGdbmiArguments::expression_arguments_data()
{
    QTest::addColumn<QByteArray>("input");
    QTest::addColumn<QByteArray>("expected");
    QTest::newRow("empty") << QByteArray() << QByteArray("\"\"");
    QTest::newRow("spaces") << QByteArray("array[2] + 1") << QByteArray("\"array[2] + 1\"");
    QTest::newRow("quotes") << QByteArray("text == \"hello\"") << QByteArray("\"text == \\\"hello\\\"\"");
    QTest::newRow("backslashes") << QByteArray("path == \"C:\\tools\\\"") << QByteArray("\"path == \\\"C:\\\\tools\\\\\\\"\"");
    QTest::newRow("newlines") << QByteArray("a\r\n +\t1") << QByteArray("\"a\\015\\012 +\\0111\"");
    QTest::newRow("nul-and-digit") << QByteArray("a\0" "123",5) << QByteArray("\"a\\000123\"");
    QTest::newRow("delete") << QByteArray(1,char(127)) << QByteArray("\"\\177\"");
    const QByteArray utf8 = QString::fromUtf8("variabile_è + 1").toUtf8();
    QTest::newRow("utf8-preserved") << utf8 << '"' + utf8 + '"';
}

void TestGdbmiArguments::expression_arguments()
{
    QFETCH(QByteArray,input);
    QFETCH(QByteArray,expected);
    QCOMPARE(GDBMI::quoteArgument(input), expected);
    QCOMPARE(GDBMI::watchCreationArguments(input,false), QByteArray(" - @ ")+expected);
    QCOMPARE(GDBMI::watchCreationArguments(input,true), QByteArray(" - * ")+expected);
    QVERIFY(!GDBMI::quoteArgument(input).contains('\n'));
    QVERIFY(!GDBMI::quoteArgument(input).contains('\r'));
}

namespace {
struct GdbSession {
    QProcess process;
    QByteArray output;
    int nextToken = 1;
#ifdef Q_OS_WIN
    HANDLE job = nullptr;
    bool contain()
    {
        job=CreateJobObjectW(nullptr,nullptr);
        if (!job) return false;
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))) return false;
        HANDLE child=OpenProcess(PROCESS_SET_QUOTA|PROCESS_TERMINATE,FALSE,DWORD(process.processId()));
        if (!child) return false;
        const bool assigned=AssignProcessToJobObject(job,child);
        CloseHandle(child);
        return assigned;
    }
#else
    bool contain() {return true;}
#endif
    ~GdbSession()
    {
        if (process.state()!=QProcess::NotRunning) {
            process.write("-gdb-set confirm off\n-interpreter-exec console \"kill\"\n-gdb-exit\n");
            if (!process.waitForFinished(3000)) {
                process.kill();
                process.waitForFinished(3000);
            }
        }
#ifdef Q_OS_WIN
        if (job) CloseHandle(job);
#endif
    }
    QByteArray command(const QByteArray &command)
    {
        const QByteArray token=QByteArray::number(nextToken++);
        if (process.write(token+command+'\n')<0) return {};
        QElapsedTimer timer;timer.start();
        while (timer.elapsed()<5000) {
            process.waitForReadyRead(100);
            output+=process.readAllStandardOutput();
            const int begin=output.indexOf(token+'^');
            if (begin>=0) {
                const int end=output.indexOf('\n',begin);
                if (end>=0) {
                    const auto result=output.mid(begin,end-begin).trimmed();
                    output.remove(0,end+1);
                    return result;
                }
            }
            if (process.state()==QProcess::NotRunning) break;
        }
        return {};
    }
    bool stopped()
    {
        QElapsedTimer timer;timer.start();
        while (timer.elapsed()<5000) {
            output+=process.readAllStandardOutput();
            if (output.contains("*stopped")) return true;
            if (process.state()==QProcess::NotRunning) return false;
            process.waitForReadyRead(100);
        }
        return false;
    }
};
}

void TestGdbmiArguments::real_gdb_watch_workflow()
{
    const QString cxx=qEnvironmentVariable("REDPANDA_TEST_CXX");
    const QString gdb=qEnvironmentVariable("REDPANDA_TEST_GDB");
    if (cxx.isEmpty() || gdb.isEmpty())
        QSKIP("Set REDPANDA_TEST_CXX and REDPANDA_TEST_GDB to absolute tool paths for the real debugger workflow.");
    QVERIFY(QFileInfo(cxx).isAbsolute() && QFileInfo(cxx).isExecutable());
    QVERIFY(QFileInfo(gdb).isAbsolute() && QFileInfo(gdb).isExecutable());
    QTemporaryDir directory(QDir::tempPath()+"/redpanda-gdb watch-XXXXXX");
    QVERIFY(directory.isValid());
    const QString source=directory.filePath("watch.cpp");
#ifdef Q_OS_WIN
    const QString executable=directory.filePath("watch.exe");
#else
    const QString executable=directory.filePath("watch");
#endif
    QFile file(source);QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray code=R"(int globalValue=41;
char sample[]="a\"b\\c";
int main() { return globalValue == 41 ? 0 : 1; }
)";
    QCOMPARE(file.write(code),qint64(code.size()));file.close();
    QProcess compiler;
    compiler.start(cxx,{"-g","-O0",source,"-o",executable});
    QVERIFY(compiler.waitForStarted(5000));
    const bool built=compiler.waitForFinished(30000);
    if (!built) {compiler.kill();compiler.waitForFinished(3000);}
    QVERIFY2(built,qPrintable(compiler.errorString()));
    QCOMPARE(compiler.exitCode(),0);

    GdbSession session;
    session.process.start(gdb,{"--quiet","--nx","--interpreter=mi2"});
    QVERIFY(session.process.waitForStarted(5000));
    QVERIFY2(session.contain(),"Could not contain the debugger and its inferior in an owned Windows job.");
    QVERIFY(session.command("-gdb-set confirm off").contains("^done"));
    QVERIFY(session.command("-file-exec-and-symbols "+GDBMI::quoteArgument(executable.toUtf8())).contains("^done"));
    QVERIFY(session.command("-break-insert main").contains("^done"));
    QVERIFY(session.command("-exec-run").contains("^running"));
    QVERIFY(session.stopped());
    const QByteArray simple=session.command("-var-create"+GDBMI::watchCreationArguments("globalValue",false));
    QVERIFY2(simple.contains("^done") && simple.contains("value=\"41\""),simple.constData());
    const QByteArray result=session.command("-var-create"+GDBMI::watchCreationArguments("globalValue + 1",false));
    QVERIFY2(result.contains("^done") && result.contains("value=\"42\""),result.constData());
    for (const QByteArray expression : {QByteArray("(int)(sample[1] == '\"')"),QByteArray("(int)(sample[3] == '\\\\')")}) {
        const auto response=session.command("-var-create"+GDBMI::watchCreationArguments(expression,false));
        QVERIFY2(response.contains("^done") && response.contains("value=\"1\""),response.constData());
        const auto evaluated=session.command("-data-evaluate-expression "+GDBMI::quoteArgument(expression));
        QVERIFY2(evaluated.contains("^done") && evaluated.contains("value=\"1\""),evaluated.constData());
    }
    const int nameStart=simple.indexOf("name=\"")+6;
    const QByteArray variable=simple.mid(nameStart,simple.indexOf('"',nameStart)-nameStart);
    QVERIFY(!variable.isEmpty());
    const auto assignment=session.command("-var-assign "+variable+" 52");
    QVERIFY2(assignment.contains("^done") && assignment.contains("value=\"52\""),assignment.constData());
    const auto refreshed=session.command("-var-update --all-values *");
    QVERIFY2(refreshed.contains("^done") && refreshed.contains("value=\"53\""),refreshed.constData());
    QVERIFY(session.command("-var-delete "+variable).contains("^done"));
}

QTEST_GUILESS_MAIN(TestGdbmiArguments)
#include "test_gdbmi_arguments.moc"
