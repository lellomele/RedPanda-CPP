#include "environmentrecoverywidget.h"
#include "../settings.h"
#include <QCheckBox>
#include <QSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QDesktopServices>
#include <QPushButton>
#include <QUrl>
#include <QDir>
EnvironmentRecoveryWidget::EnvironmentRecoveryWidget(const QString &name, const QString &group, IconsManager *icons, QWidget *parent)
    : SettingsWidget(name, group, icons, parent) {
    auto *layout = new QFormLayout(this);
    mEnabled = new QCheckBox(tr("Keep recovery snapshots of unsaved documents"), this);
    mEnabled->setObjectName("recoveryEnabled"); layout->addRow(mEnabled);
    mInterval = new QSpinBox(this); mInterval->setObjectName("recoveryInterval"); mInterval->setRange(10, 300); mInterval->setSuffix(tr(" seconds"));
    layout->addRow(tr("Snapshot interval"), mInterval);
    auto *description = new QLabel(tr("Snapshots do not overwrite source files. After an unexpected shutdown, recovered text opens in new documents. Save regularly: native faults and memory corruption cannot always be recovered while the process is running."), this);
    description->setWordWrap(true); layout->addRow(description);
    auto *open = new QPushButton(tr("Open recovery folder"), this); layout->addRow(open);
    auto *log = new QPushButton(tr("Open diagnostic log"), this); layout->addRow(log);
    connect(log, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl::fromLocalFile(QDir(pSettings->dirs().config()).filePath("diagnostics.log"))); });
    connect(open, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl::fromLocalFile(QDir(pSettings->dirs().config()).filePath("recovery"))); });
}
void EnvironmentRecoveryWidget::doLoad() { mEnabled->setChecked(pSettings->environment().recoveryEnabled()); mInterval->setValue(pSettings->environment().recoveryInterval()); }
void EnvironmentRecoveryWidget::doSave() {
    pSettings->environment().setRecoveryEnabled(mEnabled->isChecked());
    pSettings->environment().setRecoveryInterval(mInterval->value());
    pSettings->environment().save();
}
