// SPDX-License-Identifier: GPL-3.0-or-later
#include "test_application_lifecycle.h"
#include "src/lifecycle/applicationlifecycle.h"
#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <memory>
#include <stdexcept>
#include <thread>

using namespace Lifecycle;
namespace {
struct Receipt { CloseAttemptId attempt; ServiceId service; };

struct Harness {
    int prepared = 0;
    int validated = 0;
    int committed = 0;
    int followedUp = 0;
    int abandoned = 0;
    int resumed = 0;
    bool inPrepare = false;
    bool abandonedDuringPrepare = false;
    OperationResult prepareResult;
    OperationResult validateResult;
    OperationResult commitResult;
    OperationResult followUpResult;
    std::function<void()> prepareAction;
    std::function<void()> validateAction;
    std::function<void()> commitAction;
    std::function<void()> followUpAction;
    std::function<void()> abandonAction;
    QList<Receipt> requests;
    QList<Receipt> resumes;
    std::unique_ptr<ApplicationLifecycle> lifecycle;

    explicit Harness(bool followUp = true) {
        CloseCallbacks callbacks;
        callbacks.prepare = [this] {
            ++prepared;
            inPrepare = true;
            if (prepareAction) prepareAction();
            inPrepare = false;
            return prepareResult;
        };
        callbacks.validate = [this] {
            ++validated;
            if (validateAction) validateAction();
            return validateResult;
        };
        callbacks.commit = [this] {
            ++committed;
            if (commitAction) commitAction();
            return commitResult;
        };
        if (followUp) callbacks.finishCommitted = [this] {
            ++followedUp;
            if (followUpAction) followUpAction();
            return followUpResult;
        };
        callbacks.abandon = [this] {
            ++abandoned;
            abandonedDuringPrepare = abandonedDuringPrepare || inPrepare;
            if (abandonAction) abandonAction();
        };
        lifecycle = std::make_unique<ApplicationLifecycle>(std::move(callbacks));
    }

    ServiceId add(QString name, std::function<void(CloseAttemptId, ServiceId)> stop = {},
                  std::function<void(CloseAttemptId)> resume = {}) {
        ServiceId id = 0;
        ServiceHooks hooks;
        hooks.name = std::move(name);
        hooks.requestStop = [this, stop](CloseAttemptId attempt, ServiceId service) {
            requests.append({attempt, service});
            if (stop) stop(attempt, service);
        };
        // Capture the stable registration identifier through a small value holder.
        auto registeredId = std::make_shared<ServiceId>(0);
        hooks.resumeAfterAbortedClose = [this, resume, registeredId](CloseAttemptId attempt) {
            ++resumed;
            resumes.append({attempt, *registeredId});
            if (resume) resume(attempt);
        };
        const auto result = lifecycle->registerService(std::move(hooks), id);
        if (!result.succeeded()) return 0;
        *registeredId = id;
        return id;
    }
    void ack(Receipt receipt, StopState state = StopState::Quiescent,
             OperationResult task = {}) {
        lifecycle->acknowledgeStopped({receipt.attempt, receipt.service, state,
                                       state == StopState::FailedToStop ? QStringLiteral("stop failed") : QString{}, task});
    }
};
}

void TestApplicationLifecycle::empty_registry_commits_on_next_turn_and_permit_is_single_use()
{
    Harness h;
    QSignalSpy ready(h.lifecycle.get(), &ApplicationLifecycle::readyToClose);
    QVERIFY(!h.lifecycle->takeClosePermit());
    QVERIFY(h.lifecycle->requestClose().succeeded());
    QCOMPARE(h.lifecycle->state(), State::Stopping);
    QCOMPARE(h.committed, 0);
    QVERIFY(!h.lifecycle->takeClosePermit());
    QTRY_COMPARE(h.lifecycle->state(), State::ReadyToClose);
    QCOMPARE(h.prepared, 1);
    QCOMPARE(h.committed, 1);
    QCOMPARE(ready.count(), 1);
    QVERIFY(h.lifecycle->isCommitConfirmed());
    QVERIFY(h.lifecycle->takeClosePermit());
    QCOMPARE(h.lifecycle->state(), State::Closed);
    QVERIFY(!h.lifecycle->takeClosePermit());
    QVERIFY(!h.lifecycle->requestClose().succeeded());
    QCOMPARE(h.committed, 1);
}

void TestApplicationLifecycle::idle_native_close_can_take_the_permit_before_returning()
{
    Harness h;
    QVERIFY(h.lifecycle->requestClose(true).succeeded());
    QCOMPARE(h.lifecycle->state(), State::ReadyToClose);
    QCOMPARE(h.committed, 1);
    QVERIFY(h.lifecycle->takeClosePermit());
    QCoreApplication::processEvents();
    QCOMPARE(h.committed, 1);
    QVERIFY(!h.lifecycle->takeClosePermit());
}

void TestApplicationLifecycle::immediate_acknowledgements_wait_for_complete_broadcast()
{
    Harness h;
    bool premature = false;
    auto stop = [&](CloseAttemptId attempt, ServiceId service) {
        h.ack({attempt, service});
        // Even a nested test event loop must not publish a close permit before
        // the complete broadcast has returned. Production never pumps events.
        QCoreApplication::processEvents();
        premature = premature || h.committed != 0 || h.lifecycle->takeClosePermit();
    };
    QVERIFY(h.add(QStringLiteral("compiler"), stop));
    QVERIFY(h.add(QStringLiteral("recovery"), stop));
    QVERIFY(h.lifecycle->requestClose().succeeded());
    QVERIFY(!premature);
    QCOMPARE(h.requests.size(), 2);
    QCOMPARE(h.lifecycle->pendingServiceCount(), 0);
    QCOMPARE(h.committed, 0);
    QTRY_COMPARE(h.lifecycle->state(), State::ReadyToClose);
    QCOMPARE(h.committed, 1);
}

void TestApplicationLifecycle::acknowledgements_are_idempotent_and_attempt_scoped()
{
    Harness h;
    const auto first = h.add(QStringLiteral("compiler"));
    const auto second = h.add(QStringLiteral("parser"));
    QVERIFY(first && second);
    QVERIFY(h.lifecycle->requestClose().succeeded());
    const auto attempt = h.lifecycle->currentAttempt();
    h.ack({attempt, first});
    h.ack({attempt, first});
    h.ack({attempt + 1, second});
    h.ack({attempt, second + 100});
    QCOMPARE(h.lifecycle->pendingServiceCount(), 1);
    QCOMPARE(h.committed, 0);
    h.lifecycle->cancelClose();
    QVERIFY(h.lifecycle->requestClose().succeeded());
    const auto next = h.lifecycle->currentAttempt();
    QVERIFY(next > attempt);
    h.ack({attempt, second});
    QCOMPARE(h.lifecycle->pendingServiceCount(), 2);
    h.ack({next, first});
    h.ack({next, second});
    QTRY_COMPARE(h.lifecycle->state(), State::ReadyToClose);
    QCOMPARE(h.committed, 1);
}

void TestApplicationLifecycle::cancellation_retires_queued_commit_and_resumes_services()
{
    Harness h;
    QVERIFY(h.add(QStringLiteral("parser")));
    h.lifecycle->requestClose();
    const auto old = h.requests.last();
    h.ack(old);
    h.lifecycle->cancelClose();
    QCOMPARE(h.lifecycle->state(), State::Running);
    QCOMPARE(h.resumed, 1);
    QCOMPARE(h.abandoned, 1);
    h.lifecycle->requestClose();
    QCoreApplication::processEvents();
    QCOMPARE(h.committed, 0);
    QCOMPARE(h.lifecycle->pendingServiceCount(), 1);
    h.ack(h.requests.last());
    QTRY_COMPARE(h.lifecycle->state(), State::ReadyToClose);
    QCOMPARE(h.committed, 1);
    QCOMPARE(h.prepared, 2);
}

void TestApplicationLifecycle::reentrant_prepare_does_not_repeat_prompts_or_abandon_active_plan()
{
    Harness h;
    h.prepareAction = [&] {
        h.lifecycle->requestClose();
        h.lifecycle->cancelClose();
        QCOMPARE(h.abandoned, 0);
        h.lifecycle->requestClose();
    };
    h.lifecycle->requestClose();
    QCOMPARE(h.prepared, 1);
    QCOMPARE(h.abandoned, 1);
    QVERIFY(!h.abandonedDuringPrepare);
    QCOMPARE(h.lifecycle->state(), State::Running);
    QCOMPARE(h.committed, 0);
    h.prepareAction = {};
    h.lifecycle->requestClose();
    QTRY_COMPARE(h.lifecycle->state(), State::ReadyToClose);
    QCOMPARE(h.prepared, 2);
}

void TestApplicationLifecycle::preparation_failure_preserves_running_state()
{
    Harness h;
    h.prepareResult = OperationResult::cancelled();
    QCOMPARE(h.lifecycle->requestClose().status, OperationStatus::Cancelled);
    QCOMPARE(h.lifecycle->state(), State::Running);
    QCOMPARE(h.abandoned, 1);
    QCOMPARE(h.committed, 0);
    h.prepareResult = OperationResult::failure(QStringLiteral("save failed"));
    QCOMPARE(h.lifecycle->requestClose().status, OperationStatus::Failed);
    QCOMPARE(h.lifecycle->state(), State::Running);
    QCOMPARE(h.abandoned, 2);
}

void TestApplicationLifecycle::services_created_during_preparation_are_included()
{
    Harness h;
    ServiceId parser = 0;
    h.prepareAction = [&] { parser = h.add(QStringLiteral("parser created by Save As")); };
    h.lifecycle->requestClose();
    QVERIFY(parser);
    QCOMPARE(h.lifecycle->pendingServiceCount(), 1);
    QCOMPARE(h.requests.first().service, parser);
    h.ack(h.requests.first());
    QTRY_COMPARE(h.lifecycle->state(), State::ReadyToClose);
}

void TestApplicationLifecycle::registry_is_frozen_until_close_is_aborted()
{
    Harness h;
    const auto first = h.add(QStringLiteral("parser"));
    h.lifecycle->requestClose();
    QVERIFY(!h.lifecycle->unregisterService(first).succeeded());
    QCOMPARE(h.add(QStringLiteral("late parser")), ServiceId(0));
    h.lifecycle->cancelClose();
    QVERIFY(h.lifecycle->unregisterService(first).succeeded());
    const auto next = h.add(QStringLiteral("replacement parser"));
    QVERIFY(next > first);
}

void TestApplicationLifecycle::deadline_defers_without_blocking_and_late_ack_needs_retry()
{
    Harness h;
    QVERIFY(h.add(QStringLiteral("slow parser")));
    QVERIFY(h.lifecycle->setStopDeadline(20).succeeded());
    int beats = 0;
    QTimer heartbeat;
    connect(&heartbeat, &QTimer::timeout, [&] { ++beats; });
    heartbeat.start(1);
    QSignalSpy deferred(h.lifecycle.get(), &ApplicationLifecycle::closeDeferred);
    h.lifecycle->requestClose();
    const auto old = h.requests.last();
    QTRY_COMPARE(h.lifecycle->state(), State::Deferred);
    QVERIFY(beats > 0);
    QCOMPARE(deferred.count(), 1);
    QVERIFY(!h.lifecycle->deferredReason().isEmpty());
    h.ack(old);
    QTest::qWait(5);
    QCOMPARE(h.lifecycle->state(), State::Deferred);
    QCOMPARE(h.committed, 0);
    QVERIFY(!h.lifecycle->takeClosePermit());
    QVERIFY(h.lifecycle->retryClose().succeeded());
    QVERIFY(h.requests.last().attempt > old.attempt);
    QCOMPARE(h.prepared, 1);
    h.ack(old);
    QCOMPARE(h.lifecycle->pendingServiceCount(), 1);
    h.ack(h.requests.last());
    QTRY_COMPARE(h.lifecycle->state(), State::ReadyToClose);
}

void TestApplicationLifecycle::failed_stop_still_broadcasts_to_remaining_services()
{
    Harness h;
    QVERIFY(h.add(QStringLiteral("compiler"), [&](CloseAttemptId attempt, ServiceId service) {
        h.ack({attempt, service}, StopState::FailedToStop);
    }));
    QVERIFY(h.add(QStringLiteral("recovery")));
    h.lifecycle->requestClose();
    QCOMPARE(h.requests.size(), 2);
    QCOMPARE(h.lifecycle->state(), State::Deferred);
    h.ack(h.requests.last());
    QCoreApplication::processEvents();
    QCOMPARE(h.committed, 0);
    h.lifecycle->cancelClose();
    QCOMPARE(h.resumed, 2);
    QCOMPARE(h.lifecycle->state(), State::Running);
}

void TestApplicationLifecycle::task_failure_does_not_negate_service_quiescence()
{
    Harness h;
    QVERIFY(h.add(QStringLiteral("failed compiler")));
    h.lifecycle->requestClose();
    h.ack(h.requests.last(), StopState::Quiescent, OperationResult::failure(QStringLiteral("compiler exit 1")));
    QTRY_COMPARE(h.lifecycle->state(), State::ReadyToClose);
    QCOMPARE(h.committed, 1);
}

void TestApplicationLifecycle::validation_conflict_after_stop_aborts_before_commit()
{
    Harness h;
    QVERIFY(h.add(QStringLiteral("parser")));
    h.lifecycle->requestClose();
    h.validateResult = OperationResult::conflict(QStringLiteral("document changed"));
    h.ack(h.requests.last());
    QTRY_COMPARE(h.lifecycle->state(), State::Running);
    QCOMPARE(h.prepared, 1);
    QCOMPARE(h.resumed, 1);
    QCOMPARE(h.abandoned, 1);
    QCOMPARE(h.committed, 0);
}

void TestApplicationLifecycle::typed_failed_commit_can_retry_without_repeating_prepare()
{
    Harness h;
    h.commitResult = OperationResult::failure(QStringLiteral("session commit failed before replacement"));
    h.lifecycle->requestClose();
    QTRY_COMPARE(h.lifecycle->state(), State::Deferred);
    QCOMPARE(h.committed, 1);
    QVERIFY(!h.lifecycle->commitMayHaveApplied());
    h.commitResult = OperationResult::success();
    QVERIFY(h.lifecycle->retryClose().succeeded());
    QTRY_COMPARE(h.lifecycle->state(), State::ReadyToClose);
    QCOMPARE(h.committed, 2);
    QCOMPARE(h.prepared, 1);
    QCOMPARE(h.followedUp, 0);
}

void TestApplicationLifecycle::committed_followup_never_replays_commit_or_allows_fake_rollback()
{
    Harness h;
    h.commitResult = {OperationStatus::CommittedWithFollowUpPending, QStringLiteral("cleanup failed")};
    h.followUpResult = OperationResult::failure(QStringLiteral("cleanup still failed"));
    h.lifecycle->requestClose();
    QTRY_COMPARE(h.lifecycle->state(), State::Deferred);
    QVERIFY(h.lifecycle->commitMayHaveApplied());
    QVERIFY(h.lifecycle->isCommitConfirmed());
    QVERIFY(!h.lifecycle->cancelClose().succeeded());
    QCOMPARE(h.abandoned, 0);
    h.lifecycle->retryClose();
    QTRY_COMPARE(h.followedUp, 1);
    QCOMPARE(h.lifecycle->state(), State::Deferred);
    QCOMPARE(h.committed, 1);
    h.followUpResult = OperationResult::success();
    h.lifecycle->retryClose();
    QTRY_COMPARE(h.lifecycle->state(), State::ReadyToClose);
    QCOMPARE(h.committed, 1);
    QCOMPARE(h.followedUp, 2);
    QCOMPARE(h.prepared, 1);
}

void TestApplicationLifecycle::unknown_commit_outcome_requires_explicit_reconciliation()
{
    Harness h(false);
    h.commitAction = [] { throw std::runtime_error("commit state unknown"); };
    h.lifecycle->requestClose();
    QTRY_COMPARE(h.lifecycle->state(), State::Deferred);
    QVERIFY(h.lifecycle->commitMayHaveApplied());
    QVERIFY(!h.lifecycle->isCommitConfirmed());
    QVERIFY(!h.lifecycle->retryClose().succeeded());
    QVERIFY(!h.lifecycle->cancelClose().succeeded());
    QCOMPARE(h.committed, 1);
    QCOMPARE(h.abandoned, 0);
    QVERIFY(!h.lifecycle->takeClosePermit());

    Harness reconciled;
    reconciled.commitAction = [] { throw std::runtime_error("receipt lost"); };
    reconciled.lifecycle->requestClose();
    QTRY_COMPARE(reconciled.lifecycle->state(), State::Deferred);
    reconciled.lifecycle->retryClose();
    QTRY_COMPARE(reconciled.lifecycle->state(), State::ReadyToClose);
    QCOMPARE(reconciled.committed, 1);
    QCOMPARE(reconciled.followedUp, 1);
}

void TestApplicationLifecycle::followup_exception_preserves_confirmed_commit()
{
    Harness h;
    h.commitResult = {OperationStatus::CommittedWithFollowUpPending, QStringLiteral("cleanup pending")};
    h.followUpAction = [] { throw std::runtime_error("cleanup exception"); };
    h.lifecycle->requestClose();
    QTRY_COMPARE(h.lifecycle->state(), State::Deferred);
    h.lifecycle->retryClose();
    QTRY_COMPARE(h.followedUp, 1);
    QCOMPARE(h.lifecycle->state(), State::Deferred);
    QVERIFY(h.lifecycle->isCommitConfirmed());
    QCOMPARE(h.committed, 1);
    QVERIFY(!h.lifecycle->cancelClose().succeeded());
    h.followUpAction = {};
    h.lifecycle->retryClose();
    QTRY_COMPARE(h.lifecycle->state(), State::ReadyToClose);
    QCOMPARE(h.committed, 1);
    QCOMPARE(h.followedUp, 2);
}

void TestApplicationLifecycle::cancel_during_retry_resumes_old_and_new_stop_requests()
{
    Harness h;
    bool retrying = false;
    QVERIFY(h.add(QStringLiteral("compiler"), [&](CloseAttemptId, ServiceId) {
        if (retrying) h.lifecycle->cancelClose();
    }));
    QVERIFY(h.add(QStringLiteral("parser")));
    h.lifecycle->setStopDeadline(1);
    h.lifecycle->requestClose();
    const auto old = h.lifecycle->currentAttempt();
    QTRY_COMPARE(h.lifecycle->state(), State::Deferred);
    retrying = true;
    h.lifecycle->retryClose();
    QCOMPARE(h.lifecycle->state(), State::Running);
    QCOMPARE(h.requests.size(), 3);
    QCOMPARE(h.resumes.size(), 2);
    bool resumedOld = false, resumedNew = false;
    for (const auto &receipt: h.resumes) {
        resumedOld = resumedOld || receipt.attempt == old;
        resumedNew = resumedNew || receipt.attempt > old;
    }
    QVERIFY(resumedOld && resumedNew);
    QCOMPARE(h.committed, 0);
}

void TestApplicationLifecycle::failed_resume_can_retry_without_duplicate_abandon_or_reentry()
{
    Harness h;
    bool firstResume = true;
    QVERIFY(h.add(QStringLiteral("compiler"), {}, [&](CloseAttemptId) {
        h.lifecycle->cancelClose();
        QVERIFY(!h.lifecycle->requestClose().succeeded());
        if (firstResume) { firstResume = false; throw std::runtime_error("resume failed"); }
    }));
    h.lifecycle->requestClose();
    QVERIFY(!h.lifecycle->cancelClose().succeeded());
    QCOMPARE(h.lifecycle->state(), State::Deferred);
    QCOMPARE(h.abandoned, 1);
    QCOMPARE(h.resumed, 1);
    QVERIFY(!h.lifecycle->takeClosePermit());
    h.lifecycle->retryClose();
    QCOMPARE(h.lifecycle->state(), State::Stopping);
    QCOMPARE(h.abandoned, 1);
    QCOMPARE(h.resumed, 2);
    QCOMPARE(h.prepared, 2);
    h.ack(h.requests.last());
    QTRY_COMPARE(h.lifecycle->state(), State::ReadyToClose);
}

void TestApplicationLifecycle::acknowledgements_from_worker_are_queued_to_owner()
{
    Harness h;
    QVERIFY(h.add(QStringLiteral("worker service")));
    h.lifecycle->requestClose();
    const auto receipt = h.requests.last();
    std::thread worker([&] { h.lifecycle->acknowledgeStopped({receipt.attempt, receipt.service}); });
    worker.join();
    QCOMPARE(h.lifecycle->pendingServiceCount(), 1);
    QCOMPARE(h.committed, 0);
    QTRY_COMPARE(h.lifecycle->state(), State::ReadyToClose);
}

void TestApplicationLifecycle::wrong_thread_commands_do_not_mutate_state()
{
    Harness h;
    OperationResult result;
    std::thread worker([&] { result = h.lifecycle->requestClose(); });
    worker.join();
    QCOMPARE(result.status, OperationStatus::Failed);
    QCOMPARE(h.lifecycle->state(), State::Running);
    QCOMPARE(h.prepared, 0);
}
