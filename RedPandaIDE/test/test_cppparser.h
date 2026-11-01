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
    void test_split_expression_ignores_comments_data();
    void test_split_expression_ignores_comments();

protected:
    std::shared_ptr<CppParser> mParser;
};

#endif
