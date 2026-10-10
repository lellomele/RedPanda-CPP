#include "test_settingswidget.h"
#include "src/settingsdialog/settingswidget.h"
#include <QCheckBox>
#include <QSignalSpy>
#include <QTest>

namespace {
class CheckBoxSettingsWidget : public SettingsWidget
{
public:
    CheckBoxSettingsWidget() : SettingsWidget("Test", "Test", nullptr), checkbox(this) {}
    QCheckBox checkbox;
protected:
    void doLoad() override { checkbox.setChecked(!checkbox.isChecked()); }
    void doSave() override {}
};
}

void TestSettingsWidget::test_checkbox_disconnect()
{
    CheckBoxSettingsWidget widget;
    widget.connectInputs();
    QSignalSpy changes(&widget, &SettingsWidget::settingsChanged);
    QVERIFY(changes.isValid());
    widget.checkbox.setChecked(true);
    QCOMPARE(changes.count(), 1);
    changes.clear();
    widget.disconnectInputs();
    widget.checkbox.setChecked(false);
    QCOMPARE(changes.count(), 0);
}

void TestSettingsWidget::test_repeated_load_does_not_duplicate_connections()
{
    CheckBoxSettingsWidget widget;
    widget.connectInputs();
    QSignalSpy changes(&widget, &SettingsWidget::settingsChanged);
    QVERIFY(changes.isValid());
    for (int i = 0; i < 3; ++i) {
        changes.clear();
        widget.load();
        QCOMPARE(changes.count(), 1);
        QCOMPARE(changes.at(0).at(0).toBool(), false);
        QVERIFY(!widget.isSettingsChanged());
    }
    changes.clear();
    widget.checkbox.setChecked(!widget.checkbox.isChecked());
    QCOMPARE(changes.count(), 1);
    QCOMPARE(changes.at(0).at(0).toBool(), true);
    QVERIFY(widget.isSettingsChanged());
}
