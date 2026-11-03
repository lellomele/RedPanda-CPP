// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef CONTAINEDPROCESS_H
#define CONTAINEDPROCESS_H
#include <QElapsedTimer>
#include <QProcess>
#include <QThread>
#include <QString>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Compilation {
struct ProcessContainmentResult {
    bool success = false;
    QString error;
    quint32 systemError = 0;
    explicit operator bool() const { return success; }
};
enum class ProcessTreeStatus { Drained, Active, Failed, Unsupported };
struct ProcessTreeObservation {
    ProcessTreeStatus status = ProcessTreeStatus::Unsupported;
    quint32 activeProcesses = 0;
    QString error;
    quint32 systemError = 0;
    bool drained() const { return status == ProcessTreeStatus::Drained; }
};
namespace ProcessContainmentDetail {
#ifdef Q_OS_WIN
enum class CreationFailure { None, PreviousModifierThrew, UnsupportedStartupInfo, ReusedLaunch };
struct WindowsJob {
    HANDLE job = nullptr;
    STARTUPINFOEXW startup{};
    std::vector<unsigned char> attributeStorage;
    bool attributesInitialized = false;
    std::atomic_bool modifierApplied{false};
    std::atomic<CreationFailure> creationFailure{CreationFailure::None};
    ~WindowsJob() {
        if (attributesInitialized) DeleteProcThreadAttributeList(startup.lpAttributeList);
        // KILL_ON_JOB_CLOSE is only a last-owner safety net. Closing the handle
        // is not an acknowledgement that all descendants have already exited.
        if (job) CloseHandle(job);
    }
    void reject(QProcess::CreateProcessArguments *arguments, CreationFailure reason) noexcept {
        creationFailure.store(reason, std::memory_order_release);
        // Qt's Windows modifier has no failure-return API. An invalid extended
        // startup cb makes CreateProcess reject this launch before any code runs.
        startup.StartupInfo.cb = 0;
        arguments->startupInfo = &startup.StartupInfo;
        arguments->flags |= EXTENDED_STARTUPINFO_PRESENT;
    }
};
#endif
} // namespace ProcessContainmentDetail

// Native job ownership may be retained by a task/supervisor after QProcess is
// gone. New jobs/shutdown must remain gated while observe() is Active or Failed;
// only a successful ActiveProcesses==0 observation acknowledges quiescence.
class ProcessTreeLease {
public:
    bool valid() const {
#ifdef Q_OS_WIN
        return mJob && mJob->job;
#else
        return false;
#endif
    }
    ProcessTreeObservation observe() const {
#ifdef Q_OS_WIN
        if (!valid()) return {ProcessTreeStatus::Failed, 0, QStringLiteral("Process containment was not prepared."), 0};
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION information{};
        if (!QueryInformationJobObject(mJob->job, JobObjectBasicAccountingInformation,
                                       &information, sizeof(information), nullptr))
            return {ProcessTreeStatus::Failed, 0, QStringLiteral("Could not query the contained process tree."), GetLastError()};
        return {information.ActiveProcesses ? ProcessTreeStatus::Active : ProcessTreeStatus::Drained,
                information.ActiveProcesses, {}, 0};
#else
        return {ProcessTreeStatus::Unsupported, 0, QStringLiteral("Process-tree containment is supported by this implementation on Windows 10 or later."), 0};
#endif
    }
    ProcessContainmentResult terminate() const {
#ifdef Q_OS_WIN
        const auto state = observe();
        if (state.drained()) return {true, {}, 0};
        if (!valid()) return {false, state.error, state.systemError};
        if (!TerminateJobObject(mJob->job, ERROR_CANCELLED))
            return {false, QStringLiteral("Could not terminate the contained process tree."), GetLastError()};
        return {true, {}, 0}; // Request accepted; observe().drained() is the ack.
#else
        return {false, QStringLiteral("Process-tree containment is not available on this platform."), 0};
#endif
    }
private:
    friend class ContainedProcess;
#ifdef Q_OS_WIN
    std::shared_ptr<ProcessContainmentDetail::WindowsJob> mJob;
#endif
};

class ContainedProcess final {
public:
    // Call once from the owning preparation/compiler worker before start().
    // The existing public modifier is invoked first. Its ordinary startup
    // handles/flags/environment modifications are preserved. An existing opaque
    // extended attribute list cannot be copied safely and is rejected explicitly
    // rather than silently discarding its restrictions or starting uncontained.
    ProcessContainmentResult prepare(QProcess &process) {
        if (process.thread() != QThread::currentThread() || process.state() != QProcess::NotRunning)
            return {false, QStringLiteral("Process containment must be prepared on the process owner before launch."), 0};
        if (mLease.valid()) return {false, QStringLiteral("Process containment is already prepared."), 0};
#ifdef Q_OS_WIN
        try {
            auto job = std::make_shared<ProcessContainmentDetail::WindowsJob>();
            job->job = CreateJobObjectW(nullptr, nullptr);
            if (!job->job) return {false, QStringLiteral("Could not create the compiler process job."), GetLastError()};
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if (!SetInformationJobObject(job->job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
                return {false, QStringLiteral("Could not configure compiler process containment."), GetLastError()};
            SIZE_T required = 0;
            InitializeProcThreadAttributeList(nullptr, 1, 0, &required);
            if (required == 0 || required > 64 * 1024)
                return {false, QStringLiteral("Invalid process containment attribute capacity."), GetLastError()};
            job->attributeStorage.resize(required);
            job->startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(job->attributeStorage.data());
            if (!InitializeProcThreadAttributeList(job->startup.lpAttributeList, 1, 0, &required))
                return {false, QStringLiteral("Could not initialize process containment attributes."), GetLastError()};
            job->attributesInitialized = true;
            if (!UpdateProcThreadAttribute(job->startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST,
                                           &job->job, sizeof(job->job), nullptr, nullptr))
                return {false, QStringLiteral("Atomic process-job assignment is unavailable."), GetLastError()};
            const auto previous = process.createProcessArgumentsModifier();
            // Capture order keeps the job/attribute values alive until after
            // any earlier modifier's owned resources have been destroyed.
            process.setCreateProcessArgumentsModifier([job, previous](QProcess::CreateProcessArguments *arguments) noexcept {
                if (job->modifierApplied.exchange(true, std::memory_order_acq_rel)) {
                    job->reject(arguments, ProcessContainmentDetail::CreationFailure::ReusedLaunch);
                    return;
                }
                if (previous) {
                    try { previous(arguments); }
                    catch (...) { job->reject(arguments, ProcessContainmentDetail::CreationFailure::PreviousModifierThrew); return; }
                }
                if (!arguments->startupInfo || arguments->startupInfo->cb != sizeof(STARTUPINFOW)
                        || (arguments->flags & EXTENDED_STARTUPINFO_PRESENT)) {
                    job->reject(arguments, ProcessContainmentDetail::CreationFailure::UnsupportedStartupInfo);
                    return;
                }
                // The standard streams and window flags were assembled by Qt
                // (and possibly the previous modifier); only add our attribute.
                job->startup.StartupInfo = *arguments->startupInfo;
                job->startup.StartupInfo.cb = sizeof(STARTUPINFOEXW);
                arguments->startupInfo = &job->startup.StartupInfo;
                arguments->flags |= EXTENDED_STARTUPINFO_PRESENT;
            });
            mLease.mJob = std::move(job);
            return {true, {}, 0};
        } catch (const std::bad_alloc &) {
            return {false, QStringLiteral("Process containment bookkeeping could not be allocated."), 0};
        }
#else
        Q_UNUSED(process)
        return {false, QStringLiteral("Process-tree containment is supported by this implementation on Windows 10 or later."), 0};
#endif
    }
    ProcessTreeLease lease() const { return mLease; }
    ProcessTreeObservation observe() const { return mLease.observe(); }
    ProcessContainmentResult terminate() const { return mLease.terminate(); }
    // Call after waitForStarted(). A failed launch still requires the tree ack
    // before dropping registration, because prepare() and launch are separate.
    ProcessContainmentResult creationResult() const {
#ifdef Q_OS_WIN
        if (!mLease.valid()) return {false, QStringLiteral("Process containment was not prepared."), 0};
        const auto failure = mLease.mJob->creationFailure.load(std::memory_order_acquire);
        if (failure == ProcessContainmentDetail::CreationFailure::PreviousModifierThrew)
            return {false, QStringLiteral("The existing process creation modifier failed."), 0};
        if (failure == ProcessContainmentDetail::CreationFailure::UnsupportedStartupInfo)
            return {false, QStringLiteral("The existing extended process attributes cannot be safely combined with containment."), 0};
        if (failure == ProcessContainmentDetail::CreationFailure::ReusedLaunch)
            return {false, QStringLiteral("A contained process instance cannot be launched more than once."), 0};
        if (!mLease.mJob->modifierApplied.load(std::memory_order_acquire))
            return {false, QStringLiteral("Atomic process containment was not applied during launch."), 0};
        return {true, {}, 0};
#else
        return {false, QStringLiteral("Process containment is not available on this platform."), 0};
#endif
    }
    // Optional owner-worker convenience. A timeout returns Active, never a
    // terminal success. The caller retains the lease/task as StoppingDeferred,
    // requests termination as policy requires, and continues to observe the ack.
    ProcessTreeObservation waitForDrained(QProcess &process, int timeoutMilliseconds,
                                         const std::function<void()> &drain = {}) const {
        if (process.thread() != QThread::currentThread() || timeoutMilliseconds < 0)
            return {ProcessTreeStatus::Failed, 0, QStringLiteral("Invalid contained-process drain request."), 0};
        QElapsedTimer elapsed; elapsed.start();
        while (true) {
            if (drain) drain();
            auto state = observe();
            if (state.status != ProcessTreeStatus::Active && !state.drained()) return state;
            if (state.drained() && process.state() == QProcess::NotRunning) return state;
            const qint64 remaining = qint64(timeoutMilliseconds) - elapsed.elapsed();
            if (remaining <= 0) {
                // The kernel tree may already be empty while QProcess still
                // needs to process the driver's exit notification.
                if (state.drained()) { process.waitForFinished(0); if (drain) drain(); }
                if (state.drained() && process.state() != QProcess::NotRunning) state.status = ProcessTreeStatus::Active;
                return state;
            }
            const int slice = int(qMin<qint64>(remaining, 25));
            if (process.state() != QProcess::NotRunning) process.waitForFinished(slice);
            else QThread::msleep(static_cast<unsigned long>(slice));
        }
    }
private:
    ProcessTreeLease mLease;
};
} // namespace Compilation
#endif
