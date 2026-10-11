#ifndef TEST_EDITOR_SYMBOL_COMPLETION_H
#define TEST_EDITOR_SYMBOL_COMPLETION_H
#include "test_editor_base.h"

class TestEditorSymbolCompletion : public TestEditorBase
{
    Q_OBJECT
public:
    TestEditorSymbolCompletion(QObject *parent=nullptr);
private slots:
    void test_input_double_quotes_in_string1();
    void test_input_double_quotes_in_string2();
    void test_input_double_quotes_in_string3();
    void test_input_double_quotes_in_string4();
    void test_input_double_quotes_in_string5();
    void test_input_double_quotes_on_selection();
    void test_input_double_quotes_in_raw_string1();
    void test_input_double_quotes_in_raw_string2();
    void test_input_double_quotes_in_raw_string3();
    void test_input_double_quotes_in_raw_string4();
    void test_input_double_quotes_in_char_literals1();
    void test_input_double_quotes_in_char_literals2();
    void test_input_double_quotes_in_comments();

    void test_input_single_quotes_in_char_literals1();
    void test_input_single_quotes_in_char_literals2();
    void test_input_single_quotes_in_char_literals3();
    void test_input_single_quotes_in_string_literals();
    void test_input_single_quotes_in_rawstring_not_escaping();
    void test_input_single_quotes_in_rawstring_start();
    void test_input_single_quotes_in_rawstring_end();
    void test_input_single_quotes_in_comments1();
    void test_input_single_quotes_in_comments2();
    void test_input_single_quotes_on_selection();
    void test_input_single_quotes_in_number();

    void test_input_parenthesis1();
    void test_input_parenthesis_in_string();
    void test_input_parenthesis_in_comment();
    void test_input_parenthesis_in_escaping_sequence();
    void test_input_parenthesis_on_selection();

    void test_input_brackets1();
    void test_input_brackets_in_string();
    void test_input_brackets_in_comment();
    void test_input_brackets_in_escaping_sequence();
    void test_input_brackets_on_selection();

    void test_input_braces1();
    void test_input_braces_in_string();
    void test_input_braces_in_comment();
    void test_input_braces_in_escaping_sequence();
    void test_input_braces_on_selection();
    void test_input_braces_with_indent();

    void test_input_asterisk_for_ansi_c_comments();
    void test_input_asterisk_in_comments();
    void test_input_asterisk_in_comments2();
    void test_input_asterisk_in_char_literal();
    void test_input_asterisk_in_string_literal();
    void test_input_asterisk_in_rawstring_begin();
    void test_input_asterisk_in_rawstring();
    void test_input_asterisk_in_rawstring_end();

    void test_input_periods1();
    void test_input_periods_in_comments();
    void test_input_periods_in_string();

    void test_input_docstring();
    void test_input_after_cpp_comment();

    void test_read_only_symbol_completion_data();
    void test_read_only_symbol_completion();
    void test_snippet_read_only_preserves_selection();
    void test_snippet_mixed_tab_stops_follow_source_order();
    void test_snippet_multiline_unicode_tab_stops();
    void test_snippet_failed_macro_preserves_document();
    void test_snippet_replacement_single_undo();
    void test_snippet_prefix_boundaries_and_stale_requests();
    void test_snippet_partial_prefix_requires_selected_candidate();
    void test_completion_popup_without_parser_data();
    void test_completion_popup_without_parser();
    void test_completion_popup_keywords_after_cancel();
    void test_completion_list_optional_callbacks();
    void test_header_completion_without_parser();
    void test_header_completion_preserves_include_precedence();

    void test_failed_save_as_preserves_editor_identity_data();
    void test_failed_save_as_preserves_editor_identity();
    void test_failed_encoding_conversion_preserves_document();
    void test_encoding_reload_cancel_preserves_edits();
    void test_encoding_reload_failure_preserves_state();
    void test_save_as_encoding_observer_sees_committed_identity();


};

#endif
