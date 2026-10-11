#ifndef TEST_SEARCH_MODELS_H
#define TEST_SEARCH_MODELS_H
#include <QObject>

class TestSearchModels : public QObject
{
    Q_OBJECT
private slots:
    void history_combobox_reentrancy_has_balanced_resets();
    void tree_parents_use_file_rows();
    void removing_history_preserves_selection_data();
    void removing_history_preserves_selection();
    void invalid_indexes_and_clear_are_safe();
    void switching_history_invalidates_old_tree_indexes();
    void checkboxes_emit_change_notifications();
    void null_results_and_children_are_rejected();
    void history_limit_and_duplicate_replacement();
    void occurrence_history_replacement_is_consistent();
};

#endif
