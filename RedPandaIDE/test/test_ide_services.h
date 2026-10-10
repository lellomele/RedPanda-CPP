#ifndef TEST_IDE_SERVICES_H
#define TEST_IDE_SERVICES_H
#include <QObject>
class TestIdeServices : public QObject {
    Q_OBJECT
private slots:
    void diagnostic_contrast_uses_srgb_and_alpha();
    void diagnostic_contrast_covers_rgb_backgrounds();
    void diagnostics_catalog_examples();
    void diagnostics_real_gcc_samples();
    void diagnostics_unicode_and_protected_tokens();
    void diagnostics_extended_regressions();
    void diagnostics_language_and_unknown_fallback();
    void artifacts_only_expected_outputs_are_selected();
    void artifacts_changes_after_preview_are_rejected();
    void snippets_round_trip_and_legacy();
    void snippets_invalid_input_keeps_previous_library();
    void snippets_editing_is_isolated();
    void snippets_reject_duplicate_prefix_and_bad_placeholders();
    void snippets_language_and_enabled_filter();
    void snippet_templates_commit_together();
    void recovery_round_trip_and_invalid_input();
    void recovery_failed_write_preserves_previous_snapshot();
    void document_failed_load_preserves_text();
    void document_failed_write_preserves_file();
    void recoverable_event_does_not_stop_dispatch();
};
#endif
