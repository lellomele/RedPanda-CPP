// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef TEST_SEMANTICSNAPSHOT_H
#define TEST_SEMANTICSNAPSHOT_H
#include <QObject>

class TestSemanticSnapshot : public QObject {
    Q_OBJECT
private slots:
    void cycles_are_bounded_and_output_is_acyclic();
    void reopened_namespaces_and_parent_placeholders();
    void inherited_visibility_and_navigation_are_values();
    void publication_survives_source_mutation_and_destruction();
    void macros_and_include_configuration_are_immutable();
    void build_limits_report_partial_results();
    void dense_shared_graph_has_a_cumulative_work_budget();
    void pending_and_macro_scans_have_independent_bounds();
    void input_identity_copies_revision_manifest();
};
#endif
