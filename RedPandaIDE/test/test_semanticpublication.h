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
    void native_adapter_exact_paths_preserve_distinct_unicode_files();
    void outline_builds_labels_sorted_rows_and_owned_context();
    void exact_adapter_rejects_conflicting_duplicate_views();
    void model_rejects_obsolete_demands_and_stale_indexes();
    void model_indexes_large_sibling_sets_without_engine_reads();
    void cancelled_outline_never_publishes_partial_data();
    void publication_requires_same_certified_source_configuration_and_revision();
    void final_drop_defers_destruction_off_caller();
    void concurrent_reservation_admission_is_bounded();
    void invalid_or_unavailable_source_is_not_complete();
};
#endif
