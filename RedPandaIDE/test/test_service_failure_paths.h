#ifndef TEST_SERVICE_FAILURE_PATHS_H
#define TEST_SERVICE_FAILURE_PATHS_H
#include <QObject>
class TestServiceFailurePaths : public QObject {
    Q_OBJECT
private slots:
    void recovery_rejects_corrupt_inputs_without_replacing_state();
    void recovery_unicode_round_trip_and_many_documents();
    void recovery_coordinates_reject_bad_metadata();
    void recovery_budget_rejects_before_payload_and_keeps_previous_snapshot();
    void recovery_budget_accounts_for_json_escaping_and_unicode();
    void color_scheme_write_failure_is_reported();
    void color_scheme_failed_commit_preserves_previous_file();
    void snippet_read_write_failure_is_transactional();
    void deterministic_malformed_library_corpus();
    void snippet_replacement_ranges_reject_unsupported_layouts();
    void diagnostic_handler_reinstall_restores_previous_handler();
};
#endif
