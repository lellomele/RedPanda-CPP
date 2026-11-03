// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef TEST_SEMANTICRETIREMENT_H
#define TEST_SEMANTICRETIREMENT_H
#include <QObject>
class TestSemanticRetirement : public QObject {
    Q_OBJECT
private slots:
    void gui_drop_reclaims_large_payload_on_one_worker();
    void old_pins_prevent_false_stopped_and_remain_charged();
    void saturation_rejects_admission_but_never_retirement();
    void allocation_and_constructor_failures_balance_registration();
    void control_block_failure_still_retires_constructed_payload();
    void concurrent_drops_use_the_same_worker();
    void last_pin_keeps_service_alive_without_gui_join();
    void weak_control_block_remains_accounted_after_payload_ack();
    void real_publication_and_pinned_shard_drain_in_stages();
};
#endif
