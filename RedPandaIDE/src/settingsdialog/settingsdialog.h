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
#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QAbstractItemModel>
#include <QDialog>
#include <QStandardItemModel>
#include <memory>
#include <functional>
#include <QSet>
class QSortFilterProxyModel;
class QLineEdit;
#include "src/iconsmanager.h"

class SettingsWidget;
namespace Ui {
class SettingsDialog;
}

class ColorManager;
class SettingsDialog;
using PSettingsDialog = std::shared_ptr<SettingsDialog>;
class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    enum {
        GetWidgetIndexRole = Qt::UserRole + 1, OriginalGroupRole, SearchKeywordsRole
    };
    explicit SettingsDialog(QWidget *parent = nullptr);
    ~SettingsDialog();

    void addWidget(SettingsWidget* pWidget);
    void addPage(const QString &name, const QString &group, std::function<SettingsWidget *()> factory, const QString &keywords = QString());

    int createdPageCount() const;
    int pageCount() const { return mSettingWidgets.size(); }
    void selectFirstWidget();

    static PSettingsDialog optionDialog(ColorManager *colorManager, IconsManager *iconsManager, QWidget *parent);
    static PSettingsDialog projectOptionDialog(IconsManager *iconsManager, QWidget *parent);

    bool setCurrentWidget(const QString &widgetName, const QString &groupName);

    bool appShouldQuit() const;

private slots:
    void closeAndQuit();
    bool showWidget(const QModelIndex &index);

    void widget_settings_changed(bool value);

    void onWidgetsViewCurrentChanged(const QModelIndex &index, const QModelIndex &previous);

    void on_btnCancel_pressed();

    void on_btnApply_pressed();

    void on_btnOk_pressed();
private:
    bool saveCurrentPageSettings(bool confirm);
private:
    Ui::SettingsDialog *ui;
    QList<SettingsWidget*> mSettingWidgets;
    QStandardItemModel model;
    QSortFilterProxyModel *mFilter;
    QLineEdit *mSearch;
    QVector<std::function<SettingsWidget *()>> mFactories;
    QSet<SettingsWidget *> mInitialized;
    bool mAppShouldQuit;

//    CompilerSetOptionWidget *pCompilerSetOptionWidget;
//    CompilerAutolinkWidget *pCompilerAutolinkWidget;
//    EditorGeneralWidget *pEditorGeneralWidget;
//    EditorFontWidget *pEditorFontWidget;
//    EditorClipboardWidget *pEditorClipboardWidget;
//    EditorColorSchemeWidget *pEditorColorSchemeWidget;
//    EnvironmentAppearanceWidget *pEnvironmentAppearanceWidget;
//    EditorSymbolCompletionWidget *pEditorSymbolCompletionWidget;
//    EditorCodeCompletionWidget *pEditorCodeCompletionWidget;
//    EditorSyntaxCheckWidget *pEditorSyntaxCheckWidget;
//    EditorAutoSaveWidget *pEditorAutoSaveWidget;
//    EditorMiscWidget *pEditorMiscWidget;
//    ExecutorGeneralWidget  *pExecutorGeneralWidget;
//    DebugGeneralWidget *pDebugGeneralWidget;
//    FormatterGeneralWidget  *pFormatterGeneralWidget;


    // QWidget interface
protected:
    void closeEvent(QCloseEvent *event) override;
};

#endif // SETTINGSDIALOG_H
