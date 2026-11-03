// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef TEST_PARSER_CONFIGURATION_QUEUE_H
#define TEST_PARSER_CONFIGURATION_QUEUE_H
#include <QObject>
#include <memory>
class CppParser;
int runParserConfigurationQueueHelper(); // >=0 handled; -1 ordinary QTest invocation.
class TestParserConfigurationQueue : public QObject {
    Q_OBJECT
private slots:
    void active_A_keeps_its_configuration_and_source_until_B_applies();
    void superseded_B_never_commits_after_C_is_requested();
    void failed_preparation_rejects_fallback_and_equal_values_get_new_authority();
    void coalescing_preserves_latest_source_and_configuration_partition();
    void direct_callbacks_are_outside_control_mutex_and_terminal_precedes_next_writer();
    void legacy_setters_seal_one_batch_before_immediate_parse();
    void stop_is_nonblocking_without_event_pump_and_waits_for_worker_ack();
    void async_toolchain_probe_prepares_and_applies_complete_values();
    void cancelled_owned_probe_keeps_service_until_early_child_is_quiescent();
    void invalid_configuration_and_wrong_thread_calls_never_become_ready();
    void queued_outline_uses_latest_certified_demand();
    void outline_controller_switches_files_with_same_generation();
    void project_outline_completes_a_single_file_manifest();
private:
    static bool controlMutexAvailable(CppParser *parser);
};
#endif
