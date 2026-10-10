#ifndef TEST_SETTINGSWIDGET_H
#define TEST_SETTINGSWIDGET_H
#include <QObject>

class TestSettingsWidget : public QObject
{
    Q_OBJECT
private slots:
    void test_checkbox_disconnect();
    void test_repeated_load_does_not_duplicate_connections();
};
#endif
