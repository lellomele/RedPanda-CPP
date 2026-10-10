#ifndef ENVIRONMENTRECOVERYWIDGET_H
#define ENVIRONMENTRECOVERYWIDGET_H
#include "settingswidget.h"
class QCheckBox; class QSpinBox;
class EnvironmentRecoveryWidget : public SettingsWidget {
    Q_OBJECT
public:
    EnvironmentRecoveryWidget(const QString &name, const QString &group, IconsManager *icons, QWidget *parent = nullptr);
protected:
    void doLoad() override;
    void doSave() override;
private:
    QCheckBox *mEnabled;
    QSpinBox *mInterval;
};
#endif
