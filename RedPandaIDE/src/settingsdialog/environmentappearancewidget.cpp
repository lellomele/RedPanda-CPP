/*
 * Copyright (C) 2020-2022 Roy Qu (royqh1979@gmail.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "environmentappearancewidget.h"
#include "ui_environmentappearancewidget.h"

#include <QApplication>
#include <QMessageBox>
#include <QStyleFactory>
#include <QFrame>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QSaveFile>
#include <QJsonDocument>
#include <QInputDialog>
#include <QUuid>
#include "../widgets/coloredit.h"
#include "../settings.h"
#include "../mainwindow.h"
#include "../thememanager.h"
#include "../iconsmanager.h"

EnvironmentAppearanceWidget::EnvironmentAppearanceWidget(const QString& name, const QString& group,IconsManager *iconsManager,  QWidget *parent) :
    SettingsWidget(name,group,iconsManager,parent),
    ui(new Ui::EnvironmentAppearanceWidget)
{
    ui->setupUi(this);
    const int row = ui->gridLayout->rowCount();
    auto *tools = new QHBoxLayout;
    auto add = [this, tools](const QString &text, auto action) { auto *button = new QPushButton(text, this); tools->addWidget(button); connect(button, &QPushButton::clicked, this, action); };
    add(tr("Import theme..."), [this] { importTheme(); });
    add(tr("Export theme..."), [this] { exportTheme(); });
    add(tr("Edit palette..."), [this] { editPalette(); });
    tools->addStretch(); ui->gridLayout->addLayout(tools, row, 0, 1, 2);
    mPreview = new QFrame(this); mPreview->setObjectName("themePreview"); mPreview->setAutoFillBackground(true);
    auto *previewLayout = new QVBoxLayout(mPreview);
    previewLayout->addWidget(new QLabel(tr("Theme preview"), mPreview));
    auto *sample = new QLineEdit("int main() { return 0; }", mPreview); sample->setReadOnly(true); previewLayout->addWidget(sample);
    auto *selected = new QLabel(tr("Selected text"), mPreview); selected->setBackgroundRole(QPalette::Highlight); selected->setForegroundRole(QPalette::HighlightedText); selected->setAutoFillBackground(true); previewLayout->addWidget(selected);
    ui->gridLayout->addWidget(mPreview, row + 1, 0, 1, 2);
}

EnvironmentAppearanceWidget::~EnvironmentAppearanceWidget()
{
    delete ui;
}

void EnvironmentAppearanceWidget::doLoad()
{
    for (int i=0; i<ui->cbTheme->count();i++) {
        if (ui->cbTheme->itemData(i) == pSettings->environment().theme()) {
            ui->cbTheme->setCurrentIndex(i);
            break;
        }
    }
    ui->cbFont->setCurrentFont(QFont(pSettings->environment().interfaceFont()));
    ui->spinFontSize->setValue(pSettings->environment().interfaceFontSize());
    for (int i=0; i<ui->cbIconSet->count();i++) {
        if (ui->cbIconSet->itemData(i) == pSettings->environment().iconSet()) {
            ui->cbIconSet->setCurrentIndex(i);
            break;
        }
    }
    ui->spinZoomFactor->setValue(pSettings->environment().iconZoomFactor());
    ui->chkUseCustomIconSet->setChecked(pSettings->environment().useCustomIconSet());

    for (int i=0;i<ui->cbLanguage->count();i++) {
        if (ui->cbLanguage->itemData(i) == pSettings->environment().language()) {
            ui->cbLanguage->setCurrentIndex(i);
            break;
        }
    }
    ui->chkComboboxWheel->setChecked(pSettings->environment().comboboxWheel());
}

void EnvironmentAppearanceWidget::doSave()
{
    const bool languageChanged = pSettings->environment().language() != ui->cbLanguage->currentData().toString();
    if (pSettings->environment().theme()!=ui->cbTheme->currentData().toString()) {
        ThemeManager themeManager;
        PAppTheme appTheme = themeManager.theme(ui->cbTheme->currentData().toString());
        if (appTheme && !appTheme->defaultColorScheme().isEmpty()) {
            pSettings->editor().setColorScheme(appTheme->defaultColorScheme());
            pSettings->editor().save();
            pMainWindow->updateEditorColorSchemes();
        }
    }
    pSettings->environment().setTheme(ui->cbTheme->currentData().toString());
    pSettings->environment().setInterfaceFont(ui->cbFont->currentFont().family());
    pSettings->environment().setInterfaceFontSize(ui->spinFontSize->value());
    pSettings->environment().setLanguage(ui->cbLanguage->currentData().toString());
    pSettings->environment().setIconSet(ui->cbIconSet->currentData().toString());
    pSettings->environment().setIconZoomFactor(ui->spinZoomFactor->value());

    pSettings->environment().setUseCustomIconSet(ui->chkUseCustomIconSet->isChecked());
    pSettings->environment().setComboboxWheel(ui->chkComboboxWheel->isChecked());

    pSettings->environment().save();
    ThemeManager::invalidateCache();
    pMainWindow->applySettings();
    if (languageChanged) QMessageBox::information(this, tr("Restart required"), tr("Restart the IDE to apply the new interface language."));
}

void EnvironmentAppearanceWidget::updateIcons(const QSize & size)
{
    Q_UNUSED(size);
    iconsManager()->setIcon(ui->btnCustomize, IconsManager::ACTION_EDIT_COPY);
    iconsManager()->setIcon(ui->btnOpenCustomThemeFolder, IconsManager::ACTION_MISC_FOLDER);
    iconsManager()->setIcon(ui->btnRemoveCustomTheme, IconsManager::ACTION_MISC_REMOVE);
    iconsManager()->setIcon(ui->btnReload, IconsManager::ACTION_MISC_RESET);
}

void EnvironmentAppearanceWidget::init()
{
    ThemeManager themeManager;
    QList<PAppTheme> appThemes = themeManager.getThemes();
    foreach(const PAppTheme& appTheme, appThemes) {
        ui->cbTheme->addItem(appTheme->categoryLabel() + " " + appTheme->displayName(), appTheme->name());
    }
    ui->cbLanguage->addItem(tr("English"),"en");
    ui->cbLanguage->addItem(tr("Italian"),"it_IT");
    ui->cbLanguage->addItem(tr("Portuguese"),"pt_BR");
    ui->cbLanguage->addItem(tr("Simplified Chinese"),"zh_CN");
    ui->cbLanguage->addItem(tr("Traditional Chinese"),"zh_TW");
    ui->cbLanguage->addItem(tr("Russian"),"ru_RU");
    QList<PIconSet> iconSets = iconsManager()->listIconSets();
    foreach(const PIconSet& iconSet, iconSets) {
        ui->cbIconSet->addItem(iconSet->displayName,iconSet->name);
    }
    SettingsWidget::init();
}

void EnvironmentAppearanceWidget::on_cbTheme_currentIndexChanged(int /* index */)
{
    ThemeManager themeManager;
    PAppTheme appTheme = themeManager.theme(ui->cbTheme->currentData().toString());
    mPreview->setPalette(appTheme->palette());
    ui->btnCustomize->setVisible(appTheme->category() == AppTheme::ThemeCategory::BuiltIn);
    ui->btnOpenCustomThemeFolder->setVisible(appTheme->category() == AppTheme::ThemeCategory::Custom);
    ui->btnRemoveCustomTheme->setVisible(appTheme->category() == AppTheme::ThemeCategory::Custom);
    ui->btnReload->setVisible(appTheme->category() == AppTheme::ThemeCategory::Custom
                              && ui->cbTheme->currentData().toString() == pSettings->environment().theme());
    if(!appTheme->defaultIconSet().isEmpty()) {
        for (int i=0; i<ui->cbIconSet->count();i++) {
            if (ui->cbIconSet->itemData(i) == appTheme->defaultIconSet()) {
                ui->cbIconSet->setCurrentIndex(i);
                break;
            }
        }
    }
}


void EnvironmentAppearanceWidget::on_btnCustomize_clicked()
{
    ThemeManager themeManager;
    PAppTheme appTheme = themeManager.theme(ui->cbTheme->currentData().toString());
    QString customThemeFolder = pSettings->dirs().config(DirSettings::DataType::Theme);
    QDir dir{customThemeFolder};
    if (!dir.exists()) {
        dir.mkpath(customThemeFolder);
    }
    if (!appTheme->copyTo(customThemeFolder)) { QMessageBox::warning(this, tr("Theme"), tr("Could not save the custom theme.")); return; }
    ThemeManager::invalidateCache();
    refreshThemeList(appTheme->name());
}


void EnvironmentAppearanceWidget::on_btnOpenCustomThemeFolder_clicked()
{
    QString customThemeFolder = pSettings->dirs().config(DirSettings::DataType::Theme);
    openFileFolderInExplorer(customThemeFolder);
}

void EnvironmentAppearanceWidget::refreshThemeList(const QString &currentThemeName)
{
    ThemeManager themeManager;
    ui->cbTheme->clear();
    QList<PAppTheme> appThemes = themeManager.getThemes();
    for (int i=0; i<appThemes.count();i++) {
        const PAppTheme& appTheme =appThemes[i];
        ui->cbTheme->addItem(appTheme->categoryLabel() + " " + appTheme->displayName(), appTheme->name());
        if (appTheme->name() == currentThemeName)
            ui->cbTheme->setCurrentIndex(i);
    }
}


void EnvironmentAppearanceWidget::on_btnRemoveCustomTheme_clicked()
{
    ThemeManager themeManager;
    PAppTheme appTheme = themeManager.theme(ui->cbTheme->currentData().toString());
    if (appTheme->category() != AppTheme::ThemeCategory::Custom)
        return;
    if (!QFile::remove(appTheme->filename())) { QMessageBox::warning(this, tr("Theme"), tr("Could not remove the custom theme.")); return; }
    ThemeManager::invalidateCache();
    refreshThemeList(appTheme->name());
}


void EnvironmentAppearanceWidget::on_btnReload_clicked()
{
    if (ui->cbTheme->currentData().toString() != pSettings->environment().theme())
        return;
    ThemeManager::invalidateCache();
    pMainWindow->applySettings();
}


bool EnvironmentAppearanceWidget::saveCustomTheme(QJsonObject object)
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Custom theme"), tr("Theme name"), QLineEdit::Normal, object.value("name").toString(), &ok).trimmed();
    if (!ok || name.isEmpty()) return false;
    object.insert("name", name);
    for (const QString &key : object.keys()) if (key.startsWith("name_")) object.remove(key);
    const QString folder = pSettings->dirs().config(DirSettings::DataType::Theme);
    if (!QDir().mkpath(folder)) { QMessageBox::warning(this, tr("Theme"), tr("Could not create the custom theme folder.")); return false; }
    const QString id = "custom-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QSaveFile file(QDir(folder).filePath(id + ".json")); const auto bytes = QJsonDocument(object).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) { QMessageBox::warning(this, tr("Theme"), file.errorString()); return false; }
    ThemeManager::invalidateCache(); refreshThemeList(id); setSettingsChanged(); return true;
}
void EnvironmentAppearanceWidget::importTheme()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Import theme"), QString(), tr("JSON files (*.json)"));
    if (path.isEmpty()) return;
    try { AppTheme theme(path, AppTheme::ThemeType::JSON, AppTheme::ThemeCategory::Custom); saveCustomTheme(theme.toJson()); }
    catch (const FileError &error) { QMessageBox::warning(this, tr("Theme"), error.reason()); }
}
void EnvironmentAppearanceWidget::exportTheme()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Export theme"), "theme.json", tr("JSON files (*.json)"));
    if (path.isEmpty()) return;
    ThemeManager manager; auto theme = manager.theme(ui->cbTheme->currentData().toString());
    QSaveFile file(path); const auto bytes = QJsonDocument(theme->toJson()).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) QMessageBox::warning(this, tr("Theme"), file.errorString());
}
void EnvironmentAppearanceWidget::editPalette()
{
    ThemeManager manager; auto theme = manager.theme(ui->cbTheme->currentData().toString());
    QJsonObject object = theme->toJson(); QJsonObject colors = object.value("palette").toObject();
    QDialog dialog(this); dialog.setWindowTitle(tr("Edit palette")); auto *layout = new QFormLayout(&dialog);
    QMap<QString, ColorEdit *> controls;
    const QList<QPair<QString, QString>> roles = {{"PaletteWindow", tr("Window background")}, {"PaletteWindowText", tr("Window text")},
        {"PaletteBase", tr("Editor background")}, {"PaletteText", tr("Editor text")}, {"PaletteButton", tr("Button background")},
        {"PaletteButtonText", tr("Button text")}, {"PaletteHighlight", tr("Selection background")}, {"PaletteHighlightedText", tr("Selection text")},
        {"PaletteToolTipBase", tr("Tooltip background")}, {"PaletteToolTipText", tr("Tooltip text")}, {"PaletteLink", tr("Links")}};
    for (const auto &role : roles) { auto *control = new ColorEdit(&dialog); control->setColor(QColor(colors.value(role.first).toString())); controls.insert(role.first, control); layout->addRow(role.second, control); }
    auto *help = new QLabel(tr("This changes interface colors. Customize syntax colors in Editor > Color."), &dialog); help->setWordWrap(true); layout->addRow(help);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog); layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept); connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;
    for (auto it = controls.cbegin(); it != controls.cend(); ++it) { if (!it.value()->color().isValid()) { QMessageBox::warning(this, tr("Theme"), tr("All palette colors must be valid.")); return; } colors.insert(it.key(), it.value()->color().name(QColor::HexArgb)); }
    object.insert("palette", colors); saveCustomTheme(object);
}
