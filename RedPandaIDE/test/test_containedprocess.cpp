// SPDX-License-Identifier: GPL-3.0-or-later
#include "test_containedprocess.h"
#include "src/compiler/containedprocess.h"
#include "src/compiler/processcancellation.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>
#include <iostream>
#include <stdexcept>

using namespace Compilation;
namespace {
void configure(QProcess &process, const QStringList &arguments)
{
    process.setProgram(QCoreApplication::applicationFilePath());
    process.setArguments(arguments);
    process.setProcessChannelMode(QProcess::MergedChannels);
}
QByteArray readyLine(QProcess &process)
{
    QByteArray bytes;
    QElapsedTimer time; time.start();
    while (!bytes.contains('\n') && time.elapsed() < 5000) {
        process.waitForReadyRead(25); bytes += process.readAllStandardOutput();
    }
    return bytes.trimmed();
}
#ifdef Q_OS_WIN
bool spawnEarlyChild(const QString &marker, int delay, DWORD &pid)
{
    QString command = QLatin1Char('"') + QDir::toNativeSeparators(QCoreApplication::applicationFilePath())
        + QStringLiteral("\" --contained-child \"") + QDir::toNativeSeparators(marker)
        + QStringLiteral("\" ") + QString::number(delay);
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(nullptr, reinterpret_cast<wchar_t *>(command.data()), nullptr, nullptr,
                         FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child)) return false;
    pid = child.dwProcessId;
    CloseHandle(child.hThread); CloseHandle(child.hProcess);
    return true;
}
#endif
} // namespace

int runContainedProcessTestHelper()
{
    const auto args = QCoreApplication::arguments();
    if (args.size() < 2) return -1;
    if (args[1] == "--contained-echo") {
        std::cout << "std-handles-ok\n";
        return 0;
    }
    if (args[1] == "--contained-child") {
        if (args.size() != 4) return 4;
        QThread::msleep(args[3].toUInt());
        QFile marker(args[2]);
        if (!marker.open(QIODevice::WriteOnly) || marker.write("child wrote") != 11) return 5;
        marker.close();
        QThread::msleep(1000);
        return 0;
    }
    if (args[1] == "--contained-driver") {
#ifdef Q_OS_WIN
        if (args.size() != 5) return 6;
        DWORD child = 0;
        if (!spawnEarlyChild(args[2], args[4].toInt(), child)) return 7;
        std::cout << child << std::endl; // The child exists before any driver readiness notification.
        QThread::msleep(args[3].toUInt());
        return 0;
#else
        return 8;
#endif
    }
    if (args[1] == "--contained-nested-host") {
#ifdef Q_OS_WIN
        QTemporaryDir temporary;
        QProcess driver; configure(driver, {"--contained-driver", temporary.filePath("nested.txt"), "30000", "1000"});
        ContainedProcess inner;
        const auto prepared = inner.prepare(driver);
        BOOL inJob = FALSE; IsProcessInJob(GetCurrentProcess(), nullptr, &inJob);
        bool started = false, childReady = false, drained = false, active = false;
        if (prepared) {
            driver.start(); started = driver.waitForStarted(5000) && inner.creationResult().success;
            if (started) {
                childReady = !readyLine(driver).isEmpty(); active = inner.observe().activeProcesses >= 2;
                inner.terminate(); ProcessCancellation::stop(driver);
                drained = inner.waitForDrained(driver, 5000).drained();
            }
        }
        const QJsonObject result{{"hostInOuterJob", bool(inJob)}, {"prepared", prepared.success},
            {"started", started}, {"childReady", childReady}, {"observedTwoProcesses", active},
            {"drained", drained}, {"error", prepared.error}, {"nativeError", int(prepared.systemError)}};
        std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).constData();
        return prepared && started && childReady && active && drained ? 0 : 9;
#else
        return 8;
#endif
    }
    return -1;
}

void TestContainedProcess::cancellation_stops_early_child_before_next_job()
{
#ifdef Q_OS_WIN
    QTemporaryDir temporary; QVERIFY(temporary.isValid()); const auto marker = temporary.filePath("cancelled.txt");
    QProcess driver; configure(driver, {"--contained-driver", marker, "30000", "1000"});
    ContainedProcess contained; const auto prepared = contained.prepare(driver); QVERIFY2(prepared.success, qPrintable(prepared.error));
    driver.start(); QVERIFY2(driver.waitForStarted(5000), qPrintable(driver.errorString())); QVERIFY(contained.creationResult().success);
    bool pidValid = false; const auto childPid = readyLine(driver).toUInt(&pidValid); QVERIFY(pidValid);
    HANDLE child = OpenProcess(SYNCHRONIZE, FALSE, childPid); QVERIFY(child);
    QVERIFY(contained.observe().activeProcesses >= 2); QVERIFY(!contained.observe().drained());
    QVERIFY(contained.terminate().success); QVERIFY(ProcessCancellation::stop(driver));
    QVERIFY(contained.waitForDrained(driver, 5000).drained());
    QCOMPARE(WaitForSingleObject(child, 0), DWORD(WAIT_OBJECT_0)); CloseHandle(child);
    QThread::msleep(1100); QVERIFY(!QFile::exists(marker));
    // A fresh command may be admitted only after the previous tree ack.
    QProcess next; configure(next, {"--contained-echo"}); ContainedProcess second; QVERIFY(second.prepare(next).success);
    next.start(); QVERIFY(next.waitForStarted(5000)); QVERIFY(second.waitForDrained(next, 5000).drained());
#else
    QSKIP("Official containment target is Windows 10 / Qt 6.11.2");
#endif
}
void TestContainedProcess::driver_exit_is_not_tree_ack()
{
#ifdef Q_OS_WIN
    QTemporaryDir temporary; const auto marker = temporary.filePath("normal.txt");
    QProcess driver; configure(driver, {"--contained-driver", marker, "0", "400"}); ContainedProcess contained;
    QVERIFY(contained.prepare(driver).success); driver.start(); QVERIFY(driver.waitForStarted(5000));
    QVERIFY(driver.waitForFinished(5000)); QCOMPARE(driver.state(), QProcess::NotRunning);
    QVERIFY(!contained.observe().drained());
    QCOMPARE(contained.waitForDrained(driver, 25).status, ProcessTreeStatus::Active);
    QVERIFY(contained.waitForDrained(driver, 5000).drained()); QVERIFY(QFile::exists(marker));
#else
    QSKIP("Official containment target is Windows 10 / Qt 6.11.2");
#endif
}
void TestContainedProcess::nested_outer_job_is_supported()
{
#ifdef Q_OS_WIN
    QProcess host; configure(host, {"--contained-nested-host"}); ContainedProcess outer;
    QVERIFY(outer.prepare(host).success); host.start(); QVERIFY2(host.waitForStarted(5000), qPrintable(host.errorString()));
    QVERIFY(outer.waitForDrained(host, 15000).drained()); QCOMPARE(host.exitCode(), 0);
    const auto value = QJsonDocument::fromJson(host.readAllStandardOutput()).object();
    QVERIFY(value["hostInOuterJob"].toBool()); QVERIFY(value["prepared"].toBool());
    QVERIFY(value["started"].toBool()); QVERIFY(value["observedTwoProcesses"].toBool()); QVERIFY(value["drained"].toBool());
#else
    QSKIP("Official containment target is Windows 10 / Qt 6.11.2");
#endif
}
void TestContainedProcess::previous_modifier_and_standard_handles_are_preserved()
{
#ifdef Q_OS_WIN
    bool called = false;
    QProcess process; configure(process, {"--contained-echo"});
    process.setCreateProcessArgumentsModifier([&](QProcess::CreateProcessArguments *arguments) {
        called = true; arguments->flags |= CREATE_NO_WINDOW;
        arguments->startupInfo->dwFlags |= STARTF_USESHOWWINDOW; arguments->startupInfo->wShowWindow = SW_HIDE;
    });
    ContainedProcess contained; QVERIFY(contained.prepare(process).success); process.start();
    QVERIFY2(process.waitForStarted(5000), qPrintable(process.errorString())); QVERIFY(called);
    QVERIFY(contained.creationResult().success); QVERIFY(contained.waitForDrained(process, 5000).drained());
    QCOMPARE(process.readAllStandardOutput().trimmed(), QByteArray("std-handles-ok"));
#else
    QSKIP("Official containment target is Windows 10 / Qt 6.11.2");
#endif
}
void TestContainedProcess::incompatible_or_throwing_modifiers_fail_closed()
{
#ifdef Q_OS_WIN
    QTemporaryDir temporary;
    for (bool throws : {false, true}) {
        const auto marker = temporary.filePath(throws ? "throw.txt" : "extended.txt");
        QProcess process; configure(process, {"--contained-child", marker, "0"});
        process.setCreateProcessArgumentsModifier([throws](QProcess::CreateProcessArguments *arguments) {
            if (throws) throw std::runtime_error("injected modifier failure");
            arguments->flags |= EXTENDED_STARTUPINFO_PRESENT;
        });
        ContainedProcess contained; QVERIFY(contained.prepare(process).success); process.start();
        QVERIFY(!process.waitForStarted(5000)); QVERIFY(!contained.creationResult().success);
        QVERIFY(contained.waitForDrained(process, 5000).drained()); QVERIFY(!QFile::exists(marker));
    }
#else
    QSKIP("Official containment target is Windows 10 / Qt 6.11.2");
#endif
}
void TestContainedProcess::reused_process_launch_is_rejected()
{
#ifdef Q_OS_WIN
    QProcess process; configure(process, {"--contained-echo"}); ContainedProcess contained;
    QVERIFY(contained.prepare(process).success); process.start(); QVERIFY(process.waitForStarted(5000));
    QVERIFY(contained.waitForDrained(process, 5000).drained());
    process.start(); QVERIFY(!process.waitForStarted(5000)); QVERIFY(!contained.creationResult().success);
    QVERIFY(contained.observe().drained());
#else
    QSKIP("Official containment target is Windows 10 / Qt 6.11.2");
#endif
}
void TestContainedProcess::retained_lease_survives_process_object()
{
#ifdef Q_OS_WIN
    QTemporaryDir temporary; const auto marker = temporary.filePath("retained.txt"); ProcessTreeLease lease;
    {
        QProcess driver; configure(driver, {"--contained-driver", marker, "0", "1000"}); ContainedProcess contained;
        QVERIFY(contained.prepare(driver).success); lease = contained.lease(); driver.start();
        QVERIFY(driver.waitForStarted(5000)); QVERIFY(driver.waitForFinished(5000)); QVERIFY(!lease.observe().drained());
    }
    QVERIFY(lease.valid()); QVERIFY(!lease.observe().drained()); QVERIFY(lease.terminate().success);
    QElapsedTimer timeout; timeout.start();
    while (!lease.observe().drained() && timeout.elapsed() < 5000) QThread::msleep(10);
    QVERIFY(lease.observe().drained()); QThread::msleep(1100); QVERIFY(!QFile::exists(marker));
#else
    QSKIP("Official containment target is Windows 10 / Qt 6.11.2");
#endif
}
