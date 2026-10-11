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
    void test_split_expression_ignores_comments_data();
    void test_split_expression_ignores_comments();

protected:
    std::shared_ptr<CppParser> mParser;
};

#endif
