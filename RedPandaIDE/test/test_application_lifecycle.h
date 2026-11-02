// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef TEST_APPLICATION_LIFECYCLE_H
#define TEST_APPLICATION_LIFECYCLE_H
#include <QObject>

class TestApplicationLifecycle : public QObject {
    Q_OBJECT
private slots:
    void empty_registry_commits_on_next_turn_and_permit_is_single_use();
    void immediate_acknowledgements_wait_for_complete_broadcast();
    void acknowledgements_are_idempotent_and_attempt_scoped();
    void cancellation_retires_queued_commit_and_resumes_services();
    void reentrant_prepare_does_not_repeat_prompts_or_abandon_active_plan();
    void preparation_failure_preserves_running_state();
    void services_created_during_preparation_are_included();
    void registry_is_frozen_until_close_is_aborted();
    void deadline_defers_without_blocking_and_late_ack_needs_retry();
    void failed_stop_still_broadcasts_to_remaining_services();
    void task_failure_does_not_negate_service_quiescence();
    void validation_conflict_after_stop_aborts_before_commit();
    void typed_failed_commit_can_retry_without_repeating_prepare();
    void committed_followup_never_replays_commit_or_allows_fake_rollback();
    void followup_exception_preserves_confirmed_commit();
    void unknown_commit_outcome_requires_explicit_reconciliation();
    void cancel_during_retry_resumes_old_and_new_stop_requests();
    void failed_resume_can_retry_without_duplicate_abandon_or_reentry();
    void acknowledgements_from_worker_are_queued_to_owner();
    void wrong_thread_commands_do_not_mutate_state();
};
#endif
