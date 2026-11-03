// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef TEST_CONTAINEDPROCESS_H
#define TEST_CONTAINEDPROCESS_H
#include <QObject>
// A private standalone test executable calls this before QTest::qExec. Returns
// -1 for an ordinary test invocation; helper modes never touch GUI/application state.
int runContainedProcessTestHelper();
class TestContainedProcess : public QObject {
    Q_OBJECT
private slots:
    void cancellation_stops_early_child_before_next_job();
    void driver_exit_is_not_tree_ack();
    void nested_outer_job_is_supported();
    void previous_modifier_and_standard_handles_are_preserved();
    void incompatible_or_throwing_modifiers_fail_closed();
    void reused_process_launch_is_rejected();
    void retained_lease_survives_process_object();
};
#endif
