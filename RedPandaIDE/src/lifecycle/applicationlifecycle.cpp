// SPDX-License-Identifier: GPL-3.0-or-later
#include "applicationlifecycle.h"
#include <QCoreApplication>
#include <QThread>
#include <QMetaObject>
#include <exception>
#include <limits>

namespace Lifecycle {
namespace {
OperationResult wrongThread() {
    return OperationResult::failure(QCoreApplication::translate("ApplicationLifecycle",
        "Lifecycle commands must run on the owner thread."));
}
QString unexpected(const char *phase, const QString &detail = {}) {
    const QString message = QCoreApplication::translate("ApplicationLifecycle",
        "The %1 lifecycle callback failed.").arg(QString::fromLatin1(phase));
    return detail.isEmpty() ? message : message + QLatin1Char(' ') + detail;
}
}

ApplicationLifecycle::ApplicationLifecycle(CloseCallbacks callbacks, QObject *parent):
    QObject(parent), mCallbacks(std::move(callbacks))
{
    qRegisterMetaType<Lifecycle::State>();
    qRegisterMetaType<Lifecycle::StopAck>();
    qRegisterMetaType<Lifecycle::CloseAttemptId>("Lifecycle::CloseAttemptId");
    mDeadline.setSingleShot(true);
    connect(&mDeadline, &QTimer::timeout, this, &ApplicationLifecycle::deadlineExpired);
}

bool ApplicationLifecycle::onOwnerThread() const { return QThread::currentThread() == thread(); }

OperationResult ApplicationLifecycle::registerService(ServiceHooks hooks, ServiceId &id)
{
    if (!onOwnerThread()) return wrongThread();
    if (mState != State::Running && mState != State::Preparing)
        return OperationResult::conflict(tr("Services cannot be added during shutdown."));
    if (hooks.name.isEmpty() || !hooks.requestStop)
        return OperationResult::failure(tr("A service needs a name and a nonblocking stop callback."));
    if (mNextService == std::numeric_limits<ServiceId>::max())
        return OperationResult::failure(tr("The lifecycle service identifier space is exhausted."));
    id = ++mNextService;
    mRegistry.insert(id, std::move(hooks));
    return OperationResult::success();
}

OperationResult ApplicationLifecycle::unregisterService(ServiceId id)
{
    if (!onOwnerThread()) return wrongThread();
    if (mState != State::Running && mState != State::Preparing)
        return OperationResult::conflict(tr("Services cannot be removed during shutdown."));
    if (!mRegistry.remove(id))
        return OperationResult::conflict(tr("The lifecycle service is not registered."));
    return OperationResult::success();
}

OperationResult ApplicationLifecycle::setStopDeadline(int milliseconds)
{
    if (!onOwnerThread()) return wrongThread();
    if (mState != State::Running || milliseconds < 0)
        return OperationResult::conflict(tr("The stop deadline can only change while running."));
    mDeadlineMilliseconds = milliseconds;
    return OperationResult::success();
}

CloseAttemptId ApplicationLifecycle::nextAttempt()
{
    return mGeneration == std::numeric_limits<CloseAttemptId>::max() ? 0 : ++mGeneration;
}

ApplicationLifecycle::CallbackOutcome ApplicationLifecycle::invoke(
    const std::function<OperationResult()> &callback, const char *phase)
{
    if (!callback)
        return {OperationResult::failure(tr("The %1 lifecycle callback is missing.")
                                        .arg(QString::fromLatin1(phase))), false};
    try { return {callback(), false}; }
    catch (const std::exception &error) { return {OperationResult::failure(unexpected(phase, QString::fromLocal8Bit(error.what()))), true}; }
    catch (...) { return {OperationResult::failure(unexpected(phase)), true}; }
}

OperationResult ApplicationLifecycle::invokeVoid(const std::function<void()> &callback, const char *phase)
{
    if (!callback) return OperationResult::success();
    return invoke([callback] { callback(); return OperationResult::success(); }, phase).result;
}

void ApplicationLifecycle::setState(State state)
{
    if (mState == state) return;
    mState = state;
    emit stateChanged(state);
}

OperationResult ApplicationLifecycle::requestClose()
{
    if (!onOwnerThread()) return wrongThread();
    if (mAborting) return OperationResult::conflict(tr("The previous close attempt is still being abandoned."));
    if (mState == State::Closed)
        return OperationResult::conflict(tr("The application is already closed."));
    if (mState == State::Deferred)
        return OperationResult::conflict(tr("Retry or cancel the deferred close attempt."));
    if (mState != State::Running) return OperationResult::success();
    if (!mCallbacks.prepare || !mCallbacks.validate || !mCallbacks.commit)
        return OperationResult::failure(tr("The close callbacks are incomplete."));
    mAttempt = nextAttempt();
    if (!mAttempt)
        return OperationResult::failure(tr("The close-attempt identifier space is exhausted."));
    mPrepared = false;
    mCommitState = CommitState::NotCommitted;
    mCancelDuringPrepare = false;
    mPlanAbandoned = false;
    mDeferredReason.clear();
    setState(State::Preparing);
    if (mCancelDuringPrepare) return abortAttempt({});
    const auto prepared = invoke(mCallbacks.prepare, "prepare");
    if (mCancelDuringPrepare) return abortAttempt({});
    if (!prepared.result.succeeded()) {
        if (prepared.threw) enterDeferred(prepared.result.error);
        else abortAttempt(prepared.result.error);
        return prepared.result;
    }
    const auto valid = invoke(mCallbacks.validate, "validate");
    if (mCancelDuringPrepare) return abortAttempt({});
    if (!valid.result.succeeded()) {
        if (valid.threw) enterDeferred(valid.result.error);
        else abortAttempt(valid.result.error);
        return valid.result;
    }
    mPrepared = true;
    mAttemptServices = mRegistry;
    beginStopping();
    return OperationResult::success();
}

void ApplicationLifecycle::beginStopping()
{
    const auto attempt = mAttempt;
    mPending.clear();
    mFailures.clear();
    for (auto it = mAttemptServices.cbegin(); it != mAttemptServices.cend(); ++it)
        mPending.insert(it.key());
    setState(State::Stopping);
    if (mAttempt != attempt || mState != State::Stopping) return;
    if (!mPending.isEmpty()) mDeadline.start(mDeadlineMilliseconds);
    const auto services = mAttemptServices;
    mBroadcasting = true;
    for (auto it = services.cbegin(); it != services.cend(); ++it) {
        if (mAttempt != attempt || (mState != State::Stopping && mState != State::Deferred)) break;
        mStopRequests.insert(it.key(), attempt);
        try { it.value().requestStop(attempt, it.key()); }
        catch (const std::exception &error) {
            acknowledgeStopped({attempt, it.key(), StopState::FailedToStop,
                                unexpected("stop", QString::fromLocal8Bit(error.what())), {}});
        } catch (...) {
            acknowledgeStopped({attempt, it.key(), StopState::FailedToStop, unexpected("stop"), {}});
        }
    }
    mBroadcasting = false;
    scheduleAdvance();
}

void ApplicationLifecycle::acknowledgeStopped(StopAck ack)
{
    if (!onOwnerThread()) {
        QMetaObject::invokeMethod(this, [this, ack] { acknowledgeStopped(ack); }, Qt::QueuedConnection);
        return;
    }
    if (!ack.attempt || ack.attempt != mAttempt || !mPending.contains(ack.service)
            || (mState != State::Stopping && mState != State::Deferred)) return;
    mPending.remove(ack.service);
    if (ack.state == StopState::FailedToStop) {
        const QString reason = ack.diagnostic.isEmpty() ? tr("A service could not stop.") : ack.diagnostic;
        mFailures.insert(ack.service, reason);
        enterDeferred(reason);
        return;
    }
    // Task failure and service quiescence are independent outcomes.
    if (mPending.isEmpty()) mDeadline.stop();
    scheduleAdvance();
}

void ApplicationLifecycle::scheduleAdvance()
{
    if (mBroadcasting || !mAttempt || mState != State::Stopping || !mPending.isEmpty() || !mFailures.isEmpty()
            || mQueuedAdvance == mAttempt) return;
    const auto attempt = mAttempt;
    mQueuedAdvance = attempt;
    QTimer::singleShot(0, this, [this, attempt] {
        if (mQueuedAdvance == attempt) mQueuedAdvance = 0;
        advance(attempt);
    });
}

void ApplicationLifecycle::advance(CloseAttemptId attempt)
{
    if (!attempt || mAttempt != attempt || mState != State::Stopping
            || !mPending.isEmpty() || !mFailures.isEmpty()) return;
    mDeadline.stop();
    if (mCommitState == CommitState::NotCommitted) {
        const auto valid = invoke(mCallbacks.validate, "validate");
        if (mAttempt != attempt || mState != State::Stopping) return;
        if (!valid.result.succeeded()) {
            if (valid.threw) enterDeferred(valid.result.error);
            else abortAttempt(valid.result.error);
            return;
        }
    }
    setState(State::Committing);
    const bool followingUp = mCommitState != CommitState::NotCommitted;
    const auto outcome = invoke(followingUp ? mCallbacks.finishCommitted : mCallbacks.commit,
                                followingUp ? "finish committed close" : "commit");
    if (outcome.threw) {
        // A follow-up exception cannot erase an already confirmed commit.
        if (mCommitState == CommitState::NotCommitted)
            mCommitState = CommitState::OutcomeUnknown;
        enterDeferred(outcome.result.error);
        return;
    }
    if (outcome.result.succeeded()) {
        mCommitState = CommitState::Committed;
        setState(State::ReadyToClose);
        if (mState == State::ReadyToClose && mAttempt == attempt)
            emit readyToClose(attempt);
    } else if (outcome.result.status == OperationStatus::CommittedWithFollowUpPending) {
        mCommitState = CommitState::Committed;
        enterDeferred(outcome.result.error.isEmpty() ? tr("The committed close has follow-up work pending.") : outcome.result.error);
    } else if (followingUp) {
        enterDeferred(outcome.result.error.isEmpty() ? tr("The committed close still needs reconciliation.") : outcome.result.error);
    } else if (outcome.result.status == OperationStatus::Cancelled || outcome.result.status == OperationStatus::Conflict) {
        abortAttempt(outcome.result.error);
    } else enterDeferred(outcome.result.error);
}

void ApplicationLifecycle::enterDeferred(QString reason)
{
    mDeadline.stop();
    mDeferredReason = reason.isEmpty() ? tr("The close attempt could not finish.") : std::move(reason);
    const auto attempt = mAttempt;
    setState(State::Deferred);
    if (mState == State::Deferred && mAttempt == attempt)
        emit closeDeferred(attempt, mDeferredReason);
}

void ApplicationLifecycle::deadlineExpired()
{
    if (mState != State::Stopping) return;
    if (mPending.isEmpty() && mFailures.isEmpty()) { scheduleAdvance(); return; }
    enterDeferred(tr("Background services are still stopping. Documents remain open."));
}

OperationResult ApplicationLifecycle::retryClose()
{
    if (!onOwnerThread()) return wrongThread();
    if (mAborting) return OperationResult::conflict(tr("The previous close attempt is still being abandoned."));
    if (mState != State::Deferred)
        return OperationResult::conflict(tr("There is no deferred close to retry."));
    if (mCommitState != CommitState::NotCommitted) {
        if (!mCallbacks.finishCommitted)
            return OperationResult::conflict(tr("A committed or unknown outcome needs an explicit follow-up handler."));
        setState(State::Stopping);
        scheduleAdvance();
        return OperationResult::success();
    }
    if (!mPrepared) {
        const auto abandoned = abortAttempt({});
        if (mState != State::Running) return abandoned;
        return requestClose();
    }
    const auto priorAttempt = mAttempt;
    const auto valid = invoke(mCallbacks.validate, "validate");
    if (mState != State::Deferred || mAttempt != priorAttempt) return OperationResult::cancelled();
    if (!valid.result.succeeded()) {
        if (valid.threw) enterDeferred(valid.result.error);
        else abortAttempt(valid.result.error);
        return valid.result;
    }
    const auto attempt = nextAttempt();
    if (!attempt) return OperationResult::failure(tr("The close-attempt identifier space is exhausted."));
    mAttempt = attempt;
    mDeferredReason.clear();
    beginStopping();
    return OperationResult::success();
}

OperationResult ApplicationLifecycle::abortAttempt(QString reason)
{
    if (mAborting) return OperationResult::success();
    mAborting = true;
    const auto retired = mAttempt;
    mAttempt = 0;
    mQueuedAdvance = 0;
    mDeadline.stop();
    mPending.clear();
    mFailures.clear();
    QStringList errors;
    const auto stopped = mStopRequests;
    for (auto it = stopped.cbegin(); it != stopped.cend(); ++it) {
        const auto service = mAttemptServices.constFind(it.key());
        if (service == mAttemptServices.cend()) continue;
        const auto resumed = invokeVoid([hook = service.value().resumeAfterAbortedClose, epoch = it.value()] {
            if (hook) hook(epoch);
        }, "resume");
        if (resumed.succeeded()) mStopRequests.remove(it.key());
        else errors.append(resumed.error);
    }
    if (!mPlanAbandoned) {
        const auto abandoned = invokeVoid(mCallbacks.abandon, "abandon");
        if (!abandoned.succeeded()) errors.append(abandoned.error);
        else mPlanAbandoned = true;
    }
    mPrepared = false;
    mCancelDuringPrepare = false;
    if (!errors.isEmpty()) {
        enterDeferred(errors.join(QLatin1Char('\n')));
        mAborting = false;
        return OperationResult::failure(mDeferredReason);
    }
    mAttemptServices.clear();
    mDeferredReason.clear();
    setState(State::Running);
    emit closeAborted(retired, reason);
    mAborting = false;
    return OperationResult::cancelled();
}

OperationResult ApplicationLifecycle::cancelClose()
{
    if (!onOwnerThread()) return wrongThread();
    if (mAborting) return OperationResult::success();
    if (mState == State::Running) return OperationResult::success();
    if (mState == State::Preparing) { mCancelDuringPrepare = true; return OperationResult::success(); }
    if (mState == State::Committing || mState == State::ReadyToClose || mState == State::Closed
            || mCommitState != CommitState::NotCommitted)
        return OperationResult::conflict(tr("The close has reached its commit point and cannot be rolled back."));
    return abortAttempt({});
}

bool ApplicationLifecycle::takeClosePermit()
{
    if (!onOwnerThread() || mState != State::ReadyToClose) return false;
    mAttempt = 0;
    mStopRequests.clear();
    mAttemptServices.clear();
    setState(State::Closed);
    return true;
}
}
