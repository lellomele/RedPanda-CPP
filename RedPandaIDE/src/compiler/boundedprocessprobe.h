// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef BOUNDEDPROCESSPROBE_H
#define BOUNDEDPROCESSPROBE_H
#include "compileconfiguration.h"
#include "processcancellation.h"
#include "processtreecompletion.h"
#include "qt_utils/utils.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QProcess>
#include <QThread>
#include <atomic>

namespace Compilation {
struct ProcessProbeRequest {
    QString executable;
    QStringList arguments;
    QString workingDirectory;
    QProcessEnvironment environment;
    int deadlineMilliseconds = 5000;
    qsizetype maximumOutputBytes = 256 * 1024;
};
struct ProcessProbeResult {
    PreparationResult preparation;
    QByteArray output;
    int exitCode = -1;
    ProcessOwnershipLevel ownership = ProcessOwnershipLevel::NoProcessStarted;
};

// A preparation worker owns the QProcess. The GUI sends only the shared atomic
// cancellation control; no application state, callback or GUI event loop enters
// the probe. Stop cleanup may take the existing bounded cancellation allowance
// after the execution deadline expires.
inline ProcessProbeResult runBoundedProcessProbe(const ProcessProbeRequest &request,
                                                 const std::atomic_bool &cancelled)
{
    ProcessProbeResult result;
    if (const auto *app = QCoreApplication::instance(); app && QThread::currentThread() == app->thread()) {
        result.preparation = PreparationResult::failure(QStringLiteral("Toolchain probes must run in a preparation worker."));
        return result;
    }
    if (cancelled.load(std::memory_order_acquire)) {
        result.preparation = {PreparationStatus::Cancelled, {}};
        return result;
    }
    if (request.executable.isEmpty() || request.deadlineMilliseconds <= 0 || request.maximumOutputBytes < 0) {
        result.preparation = PreparationResult::failure(QStringLiteral("Invalid toolchain probe request."));
        return result;
    }
    QElapsedTimer deadline;
    deadline.start();
    QProcess process;
    process.setProgram(request.executable);
    process.setArguments(request.arguments);
    process.setWorkingDirectory(request.workingDirectory);
    process.setProcessEnvironment(request.environment);
    process.setProcessChannelMode(QProcess::MergedChannels);
    bool outputLimitReached = false;
    const auto drain = [&] {
        const auto bytes = process.readAllStandardOutput();
        if (bytes.size() > request.maximumOutputBytes - result.output.size()) {
            outputLimitReached = true;
            process.kill(); // nonblocking; the owner drains the exit below
            return;
        }
        result.output.append(bytes);
    };
    QObject::connect(&process, &QProcess::readyReadStandardOutput, &process, drain);
#ifdef Q_OS_WIN
    ContainedProcess processTree;
    const auto containment = processTree.prepare(process);
    if (!containment.success) {
        result.preparation = PreparationResult::failure(containment.error);
        return result;
    }
    bool treeCompleted = false;
    QString treeFailure;
    const auto finishTree = [&](bool force) {
        if (treeCompleted) return;
        const auto completed = completeContainedProcess(process, processTree, cancelled, force, drain, {},
            qMax(0, request.deadlineMilliseconds - int(deadline.elapsed())));
        treeCompleted = true;
        treeFailure = completed.error;
        result.ownership = ProcessOwnershipLevel::WindowsTreeQuiescent;
    };
    const auto acknowledgeTreeBeforeExit = finally([&] { finishTree(true); });
#else
    // Preserve the non-Windows legacy driver policy without claiming a tree
    // acknowledgement. Official 3.5.4 process-tree qualification is Windows.
    result.ownership = ProcessOwnershipLevel::DriverOnlyUnverifiedTree;
#endif
    process.start();
    while (process.state() == QProcess::Starting && deadline.elapsed() < request.deadlineMilliseconds
           && !cancelled.load(std::memory_order_acquire)) {
        const int remaining = request.deadlineMilliseconds - static_cast<int>(deadline.elapsed());
        process.waitForStarted(qMin(remaining, ProcessCancellation::pollMilliseconds));
    }
    while (process.state() != QProcess::NotRunning && !outputLimitReached
           && deadline.elapsed() < request.deadlineMilliseconds && !cancelled.load(std::memory_order_acquire)) {
        const int remaining = request.deadlineMilliseconds - static_cast<int>(deadline.elapsed());
        process.waitForFinished(qMin(remaining, ProcessCancellation::pollMilliseconds));
        drain();
    }
    drain();
    bool wasCancelled = cancelled.load(std::memory_order_acquire);
    bool timedOut = process.state() != QProcess::NotRunning
        && deadline.elapsed() >= request.deadlineMilliseconds;
#ifdef Q_OS_WIN
    const auto creation = processTree.creationResult();
    finishTree(wasCancelled || timedOut || outputLimitReached || !creation.success
        || process.state() != QProcess::NotRunning || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0);
    wasCancelled = cancelled.load(std::memory_order_acquire);
    timedOut = timedOut || (!wasCancelled && deadline.elapsed() >= request.deadlineMilliseconds);
    if (!creation.success) {
        result.preparation = PreparationResult::failure(creation.error);
        return result;
    }
#else
    if (process.state() != QProcess::NotRunning && !ProcessCancellation::stop(process)) {
        result.preparation = PreparationResult::failure(QStringLiteral("The toolchain probe process could not be stopped."));
        return result;
    }
#endif
    if (wasCancelled) {
        result.preparation = {PreparationStatus::Cancelled, {}};
    } else if (outputLimitReached) {
        result.preparation = PreparationResult::failure(QStringLiteral("Toolchain probe output exceeds the size limit."));
    } else if (timedOut) {
        result.preparation = PreparationResult::failure(QStringLiteral("The toolchain probe deadline expired."));
#ifdef Q_OS_WIN
    } else if (!treeFailure.isEmpty()) {
        result.preparation = PreparationResult::failure(treeFailure);
#endif
    } else if (process.error() == QProcess::FailedToStart) {
        result.preparation = PreparationResult::failure(QStringLiteral("Could not start toolchain probe: %1").arg(process.errorString()));
    } else {
        result.exitCode = process.exitCode();
        if (process.exitStatus() != QProcess::NormalExit || result.exitCode != 0)
            result.preparation = PreparationResult::failure(QStringLiteral("The toolchain probe exited unsuccessfully (%1).").arg(result.exitCode));
    }
    return result;
}
} // namespace Compilation
#endif
