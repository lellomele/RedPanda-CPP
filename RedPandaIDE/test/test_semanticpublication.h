// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef TEST_SEMANTICPUBLICATION_H
#define TEST_SEMANTICPUBLICATION_H
#include <QObject>
class TestSemanticPublication : public QObject {
    Q_OBJECT
private slots:
    void union_shares_payload_and_scans_children_once();
    void exact_ids_and_ambiguous_selection();
    void old_pins_and_retirement_stay_charged();
    void allocation_cap_precedes_payload_and_scratch_growth();
    void cycles_and_traversal_limits_are_explicit_partial_results();
    void native_adapter_preserves_outline_values();
    void final_drop_defers_destruction_off_caller();
    void concurrent_reservation_admission_is_bounded();
    void invalid_or_unavailable_source_is_not_complete();
};
#endif
