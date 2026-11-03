#ifndef TEST_CPPPARSER_H
#define TEST_CPPPARSER_H
#include <QObject>
#include <memory>

class CppParser;

class TestCppParser: public QObject
{
    Q_OBJECT
public:
    TestCppParser(QObject *parent=nullptr);
private slots:
    void init_parser();
    void test_parse_var();
    void test_parse_vars();
    void test_struct();
    void test_structured_bindings();

    void test_include_directive_boundaries_data();
    void test_include_directive_boundaries();
    void test_reparse_replaces_obsolete_symbols();
    void test_frozen_parse_request_is_retried();
    void test_disabled_parser_cancels_queued_requests_data();
    void test_disabled_parser_cancels_queued_requests();
    void test_busy_parser_retains_distinct_requests();
    void test_frozen_project_file_list_is_retried();
    void test_frozen_background_request_is_retried();
    void test_frozen_requests_preserve_distinct_files_and_latest_content();
    void test_reset_barrier_nested_freeze_and_explicit_requests();
    void test_reset_barrier_busy_parser_retains_other_files();
    void test_reset_barrier_preserves_configuration_only();
    void test_reset_barrier_reports_synchronous_parse_busy();
    void test_sync_reservation_reset_excludes_parse();
    void test_sync_reservation_hard_defines_serialize();
    void test_reset_after_dispatch_before_parse_preserves_request();
    void test_reset_during_active_parse_preserves_request();
    void test_reset_replay_keeps_newer_context();
    void test_reset_during_active_project_list_replays_all();
    void test_reset_active_project_dependencies_data();
    void test_reset_active_project_dependencies();
    void test_expired_inheritance_entry_is_removed_once();
    void test_inheritance_records_stay_bounded_after_reparse();
    void test_split_expression_ignores_comments_data();
    void test_split_expression_ignores_comments();
    void test_document_store_lazy_immutable_revisions();
    void test_snapshot_input_failure_and_empty_buffer();
    void test_snapshot_source_includes_survive_provider_destruction();
    void test_snapshot_requests_coalesce_latest_revision();
    void test_snapshot_submission_owner_and_destroyed_parser();
    void test_snapshot_project_list_keeps_one_input_view();
    void test_source_manifest_revalidates_unsaved_included_header();
    void test_source_manifest_revalidates_closed_and_previously_missing_headers();
    void test_source_failure_is_distinct_from_an_empty_source();
    void test_source_request_keeps_accepted_directory();
    void test_source_change_during_parse_cannot_certify_ready();
    void test_source_relative_document_keeps_its_registration_root();

protected:
    std::shared_ptr<CppParser> mParser;
};

#endif
