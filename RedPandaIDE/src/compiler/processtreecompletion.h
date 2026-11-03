// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PROCESSTREECOMPLETION_H
#define PROCESSTREECOMPLETION_H
#include "containedprocess.h"
#include <QCoreApplication>
#include <atomic>

namespace Compilation {
struct ProcessTreeCompletion {
    bool forcedAfterGrace = false;
    QString error;
};

// The registered worker retains every owner until a positive kernel tree ack
// AND QProcess exit. A failed/late termination is a deferred stop, never a
// completed cancellation. This function intentionally has no GUI/event pump.
inline ProcessTreeCompletion completeContainedProcess(QProcess &process, const ContainedProcess &tree,
    const std::atomic_bool &cancelled, bool terminateImmediately, const std::function<void()> &drain = {},
    const std::function<void(const QString &)> &reportDeferred = {}, int normalGraceMilliseconds = 2000)
{
    Q_ASSERT(process.thread() == QThread::currentThread());
    Q_ASSERT(!QCoreApplication::instance() || QThread::currentThread() != QCoreApplication::instance()->thread());
    ProcessTreeCompletion result;
    QElapsedTimer elapsed; elapsed.start();
    QElapsedTimer lastTermination;
    bool terminationRequested = terminateImmediately;
    bool reported = false;
    while (true) {
        if (drain) drain();
        const auto state = tree.observe();
        if (state.drained() && process.state() == QProcess::NotRunning) return result;
        const bool unverified = state.status == ProcessTreeStatus::Failed || state.status == ProcessTreeStatus::Unsupported;
        if (cancelled.load(std::memory_order_acquire)) terminationRequested = true;
        if (unverified) terminationRequested = true;
        if (!terminationRequested && elapsed.elapsed() >= normalGraceMilliseconds) {
            terminationRequested = true;
            result.forcedAfterGrace = true;
            result.error = QStringLiteral("Compiler child processes did not exit after the driver finished.");
        }
        if (terminationRequested && (!lastTermination.isValid() || lastTermination.elapsed() >= 250)) {
            const auto requested = tree.terminate();
            lastTermination.restart();
            if (!requested.success && result.error.isEmpty()) result.error = requested.error;
        }
        if (!reported && (unverified || elapsed.elapsed() >= normalGraceMilliseconds)) {
            reported = true;
            if (reportDeferred) reportDeferred(QStringLiteral("Stopping the compiler and its background processes..."));
        }
        if (process.state() != QProcess::NotRunning) process.waitForFinished(25);
        else QThread::msleep(25);
    }
}
} // namespace Compilation
#endif
