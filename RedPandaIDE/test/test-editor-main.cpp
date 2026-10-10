#include <QTest>
#include <QGuiApplication>
#include "test_editor_symbol_completion.h"
#include "test_settingswidget.h"
#include "test_ide_services.h"
#include "src/resilientapplication.h"

int main(int argc, char *argv[]) {
    int status = 0;
    QTest::setMainSourcePath(__FILE__, QT_TESTCASE_BUILDDIR); // Optional: for source path resolution

    ResilientApplication app(argc,argv);
    { TestIdeServices tc; status |= QTest::qExec(&tc, argc, argv); }
    {
        TestSettingsWidget tc;
        status |= QTest::qExec(&tc, argc, argv);
    }
    //CharPos Test
    {
        TestEditorSymbolCompletion tc;
        status |= QTest::qExec(&tc, argc, argv);
    }

    return status;
}
