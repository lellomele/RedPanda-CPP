// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef APPLICATIONLIFECYCLE_H
#define APPLICATIONLIFECYCLE_H
#include "../operationresult.h"
#include <QObject>
#include <QHash>
#include <QMap>
#include <QSet>
#include <QTimer>
#include <functional>

namespace Lifecycle {
using CloseAttemptId = quint64;
using ServiceId = quint64;
enum class State { Running, Preparing, Stopping, Deferred, Committing, ReadyToClose, Closed };
enum class StopState { Quiescent, FailedToStop };
struct StopAck {
    CloseAttemptId attempt = 0;
    ServiceId service = 0;
    StopState state = StopState::Quiescent;
    QString diagnostic;
    OperationResult lastTaskOutcome;
};
struct ServiceHooks {
    QString name;
    // GUI-affine adapters send nonblocking commands. Quiescent means there
    // can be no later workspace access/publication, not just stop requested.
    std::function<void(CloseAttemptId, ServiceId)> requestStop;
    // Optional only for services whose stop does not fence future admissions.
    // Failed cleanup may retry this hook, so it must be idempotent per attempt.
    std::function<void(CloseAttemptId)> resumeAfterAbortedClose;
};
struct CloseCallbacks {
    std::function<OperationResult()> prepare;
    std::function<OperationResult()> validate;
    std::function<OperationResult()> commit;
    // Reconcile/finalize an already committed or unknown outcome. The original
    // commit is never replayed after reaching an irreversible commit point.
    std::function<OperationResult()> finishCommitted;
    // Release the prepared plan, without reverting any already saved data.
    // If it throws, an explicit retry may call it again.
    std::function<void()> abandon;
};

// One application/GUI owner mutates this controller. Receipts are queued from
// other threads. No QWidget, parser, document or global settings dependency.
class ApplicationLifecycle : public QObject {
    Q_OBJECT
public:
    explicit ApplicationLifecycle(CloseCallbacks callbacks, QObject *parent = nullptr);
    // State inspection, like commands, belongs to the owner thread.
    State state() const { return mState; }
    CloseAttemptId currentAttempt() const { return mAttempt; }
    int pendingServiceCount() const { return mPending.size(); }
    bool commitMayHaveApplied() const { return mCommitState != CommitState::NotCommitted; }
    bool isCommitConfirmed() const { return mCommitState == CommitState::Committed; }
    QString deferredReason() const { return mDeferredReason; }
    OperationResult registerService(ServiceHooks hooks, ServiceId &id);
    OperationResult unregisterService(ServiceId id);
    OperationResult setStopDeadline(int milliseconds);
    OperationResult requestClose(bool commitIfQuiescent = false);
    OperationResult retryClose();
    OperationResult cancelClose();
    bool takeClosePermit();
public slots:
    void acknowledgeStopped(Lifecycle::StopAck ack);
signals:
    void stateChanged(Lifecycle::State state);
    void closeDeferred(Lifecycle::CloseAttemptId attempt, QString reason);
    void closeAborted(Lifecycle::CloseAttemptId attempt, QString reason);
    void readyToClose(Lifecycle::CloseAttemptId attempt);
private:
    enum class CommitState { NotCommitted, Committed, OutcomeUnknown };
    struct CallbackOutcome { OperationResult result; bool threw = false; };
    bool onOwnerThread() const;
    CloseAttemptId nextAttempt();
    CallbackOutcome invoke(const std::function<OperationResult()> &callback, const char *phase);
    OperationResult invokeVoid(const std::function<void()> &callback, const char *phase);
    void setState(State state);
    void beginStopping();
    void scheduleAdvance();
    void advance(CloseAttemptId attempt);
    void enterDeferred(QString reason);
    OperationResult abortAttempt(QString reason);
    void deadlineExpired();
    CloseCallbacks mCallbacks;
    QMap<ServiceId, ServiceHooks> mRegistry;
    QMap<ServiceId, ServiceHooks> mAttemptServices;
    QHash<ServiceId, CloseAttemptId> mStopRequests;
    QSet<ServiceId> mPending;
    QHash<ServiceId, QString> mFailures;
    QTimer mDeadline;
    State mState = State::Running;
    CloseAttemptId mGeneration = 0;
    CloseAttemptId mAttempt = 0;
    CloseAttemptId mQueuedAdvance = 0;
    ServiceId mNextService = 0;
    CommitState mCommitState = CommitState::NotCommitted;
    bool mPrepared = false;
    bool mCancelDuringPrepare = false;
    bool mBroadcasting = false;
    bool mAborting = false;
    bool mPlanAbandoned = false;
    int mDeadlineMilliseconds = 5000;
    QString mDeferredReason;
};
}
Q_DECLARE_METATYPE(Lifecycle::State)
Q_DECLARE_METATYPE(Lifecycle::StopAck)
#endif
