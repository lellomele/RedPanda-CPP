#include <QTest>
#include "test_editor_symbol_completion.h"
#include "test_settingswidget.h"
#include "test_ide_services.h"
#include "test_service_failure_paths.h"
#include "test_search_models.h"
#include "src/resilientapplication.h"
#include "../../tests/qtest_suite_runner.h"
int main(int argc, char **argv) {
    QTest::setMainSourcePath(__FILE__, QT_TESTCASE_BUILDDIR);
    ResilientApplication app(argc, argv);
    using namespace TestSuites;
    return run(argc,argv,{suite<TestSearchModels>("search-models"),suite<TestIdeServices>("ide-services"),suite<TestServiceFailurePaths>("failure-paths"),suite<TestSettingsWidget>("settings"),suite<TestEditorSymbolCompletion>("smart-editing")});
}
