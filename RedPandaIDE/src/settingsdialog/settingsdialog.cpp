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
#include "../mainwindow.h"
#include "settingsdialog.h"
#include "ui_settingsdialog.h"
#include "settingswidget.h"
#include "compilersetoptionwidget.h"
#include "compilerautolinkwidget.h"
#include "compilernasmwidget.h"
#include "compilergaswidget.h"
#include "editorgeneralwidget.h"
#include "editorfontwidget.h"
#include "editorclipboardwidget.h"
#include "editorcolorschemewidget.h"
#include "editorcodecompletionwidget.h"
#include "editorsyntaxcheckwidget.h"
#include "editorsymbolcompletionwidget.h"
#include "editortooltipswidget.h"
#include "editorautosavewidget.h"
#include "editorsnippetwidget.h"
#include "editorcustomctypekeywords.h"
#include "editormiscwidget.h"
#include "environmentappearancewidget.h"
#include "environmentshortcutwidget.h"
#include "environmentfolderswidget.h"
#include "environmentperformancewidget.h"
#include "environmentprogramswidget.h"
#include "executorgeneralwidget.h"
#include "executorproblemsetwidget.h"
#include "debuggeneralwidget.h"
#include "formattergeneralwidget.h"
#include "formatterpathwidget.h"
#include "languageasmgenerationwidget.h"
#include "projectgeneralwidget.h"
#include "projectfileswidget.h"
#include "projectcompilerwidget.h"
#include "projectcompileparamaterswidget.h"
#include "projectdirectorieswidget.h"
#include "projectprecompilewidget.h"
#include "projectoutputwidget.h"
#include "projectmakefilewidget.h"
#include "projectdllhostwidget.h"
#include "toolsgeneralwidget.h"
#ifdef ENABLE_VCS
#include "toolsgitwidget.h"
#endif
#ifdef Q_OS_WIN
#include "environmentfileassociationwidget.h"
#include "projectversioninfowidget.h"
#endif
#include <QDebug>
#include <QMessageBox>
#include <QModelIndex>
#include <QSortFilterProxyModel>
#include <QLineEdit>
#include <QTimer>
#include <QShortcut>
#include <QSignalBlocker>
#include "environmentrecoverywidget.h"

class PreferencesFilter : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override {
        if (parent.isValid() && parent.data(SettingsDialog::SearchKeywordsRole).toString().contains(filterRegularExpression())) return true;
        return QSortFilterProxyModel::filterAcceptsRow(row, parent);
    }
};

SettingsDialog::SettingsDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::SettingsDialog)
{
    setWindowFlag(Qt::WindowContextHelpButtonHint,false);
    ui->setupUi(this);

    QItemSelectionModel *m=ui->widgetsView->selectionModel();
    mFilter = new PreferencesFilter(this);
    mFilter->setSourceModel(&model); mFilter->setRecursiveFilteringEnabled(true);
    mFilter->setFilterRole(SearchKeywordsRole); mFilter->setFilterCaseSensitivity(Qt::CaseInsensitive);
    ui->widgetsView->setModel(mFilter);
    ui->widgetsView->setMinimumWidth(240);
    ui->widgetsView->setTextElideMode(Qt::ElideNone);
    mSearch = new QLineEdit(this); mSearch->setClearButtonEnabled(true);
    mSearch->setPlaceholderText(tr("Search preferences (Ctrl+F)"));
    mSearch->setAccessibleName(tr("Search preferences"));
    mSearch->setObjectName("preferencesSearch");
    ui->verticalLayout->insertWidget(0, mSearch);
    auto *delay = new QTimer(this); delay->setSingleShot(true); delay->setInterval(80);
    connect(mSearch, &QLineEdit::textChanged, delay, [delay] { delay->start(); });
    connect(delay, &QTimer::timeout, this, [this] { const QSignalBlocker selection(ui->widgetsView->selectionModel()); mFilter->setFilterFixedString(mSearch->text().trimmed()); ui->widgetsView->expandAll(); });
    auto *find = new QShortcut(QKeySequence::Find, this);
    connect(find, &QShortcut::activated, mSearch, qOverload<>(&QWidget::setFocus));
    delete m;

    connect(ui->widgetsView->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &SettingsDialog::onWidgetsViewCurrentChanged);

    model.setHorizontalHeaderLabels(QStringList());

    ui->btnApply->setEnabled(false);

    mAppShouldQuit = false;
    resize(pSettings->ui().settingsDialogWidth(),pSettings->ui().settingsDialogHeight());

    QList<int> sizes = ui->splitter->sizes();
    int tabWidth = qMax(260, pSettings->ui().settingsDialogSplitterPos());
    int totalSize = sizes[0] + sizes[1];
    sizes[0] = tabWidth;
    sizes[1] = std::max(1,totalSize - sizes[0]);
    ui->splitter->setSizes(sizes);
}

SettingsDialog::~SettingsDialog()
{
    foreach (SettingsWidget* p, mSettingWidgets) {
        if (p) { p->setParent(nullptr); delete p; }
    }
    delete ui;
}

void SettingsDialog::addWidget(SettingsWidget *widget)
{
    addPage(widget->name(), widget->group(), [widget] { return widget; });
    mSettingWidgets.last() = widget;
}

void SettingsDialog::addPage(const QString &name, const QString &group, std::function<SettingsWidget *()> factory, const QString &keywords)
{
    QStandardItem *groupItem = nullptr;
    for (int i = 0; i < model.rowCount(); ++i)
        if (model.item(i)->data(OriginalGroupRole).toString() == group) groupItem = model.item(i);
    if (!groupItem) {
        QString label = group;
        if (group == tr("Environment")) label = tr("Interface and workspace");
        if (group == tr("Editor")) label = tr("Editor and snippets");
        if (group == tr("Compiler")) label = tr("Build and toolchains");
        if (group == tr("Program Runner")) label = tr("Run and testing");
        groupItem = new QStandardItem(label); groupItem->setData(-1, GetWidgetIndexRole);
        groupItem->setData(group, OriginalGroupRole); groupItem->setData(group + " " + label, SearchKeywordsRole);
        model.appendRow(groupItem);
    }
    mSettingWidgets.append(nullptr); mFactories.append(std::move(factory));
    auto *item = new QStandardItem(name); item->setToolTip(group + " > " + name); item->setData(mSettingWidgets.size() - 1, GetWidgetIndexRole);
    item->setData(name + " " + group + " " + keywords, SearchKeywordsRole); groupItem->appendRow(item);
}

int SettingsDialog::createdPageCount() const { int count = 0; for (auto *widget : mSettingWidgets) if (widget) ++count; return count; }

void SettingsDialog::selectFirstWidget()
{
    ui->widgetsView->expandAll();
    //select the first widget of the first group
    auto groupIndex = ui->widgetsView->model()->index(0,0);
    auto widgetIndex = ui->widgetsView->model()->index(0,0, groupIndex);
    ui->widgetsView->selectionModel()->setCurrentIndex(
                widgetIndex,
                QItemSelectionModel::Select
                );
}

PSettingsDialog SettingsDialog::optionDialog(ColorManager *colorManager, IconsManager *iconsManager, QWidget *parent)
{
    PSettingsDialog dialog = std::make_shared<SettingsDialog>(parent);

    dialog->setWindowTitle(tr("Options"));


    dialog->addPage(tr("Appearance"), tr("Environment"), [colorManager, iconsManager] { return new EnvironmentAppearanceWidget(tr("Appearance"),tr("Environment"),iconsManager); }, "theme light dark colors language themes tema colori lingua chiaro scuro italiano english font");

#ifdef Q_OS_WIN
    dialog->addPage(tr("File Association"), tr("Environment"), [colorManager, iconsManager] { return new EnvironmentFileAssociationWidget(tr("File Association"),tr("Environment"),iconsManager); }, "");
#endif

    dialog->addPage(tr("Shortcuts"), tr("Environment"), [colorManager, iconsManager] { return new EnvironmentShortcutWidget(tr("Shortcuts"),tr("Environment"),iconsManager); }, "shortcuts keyboard tastiera scorciatoie");

    dialog->addPage(tr("Terminal"), tr("Environment"), [colorManager, iconsManager] { return new EnvironmentProgramsWidget(tr("Terminal"),tr("Environment"),iconsManager); }, "");

    dialog->addPage(tr("Performance"), tr("Environment"), [colorManager, iconsManager] { return new EnvironmentPerformanceWidget(tr("Performance"),tr("Environment"),iconsManager); }, "performance memory parser cache velocità memoria prestazioni");

    dialog->addPage(tr("Folders / Restore Default Settings"), tr("Environment"), [iconsManager, owner = dialog.get()] {
        auto *widget = new EnvironmentFoldersWidget(tr("Folders / Restore Default Settings"), tr("Environment"), iconsManager);
        connect(widget, &EnvironmentFoldersWidget::shouldQuitApp, owner, &SettingsDialog::closeAndQuit);
        return widget;
    }, "folders cartelle reset ripristino");

    dialog->addPage(tr("Compiler Set"), tr("Compiler"), [colorManager, iconsManager] { return new CompilerSetOptionWidget(tr("Compiler Set"),tr("Compiler"),iconsManager); }, "compiler gcc clang msvc toolchain compilatore");

    dialog->addPage(tr("Auto Link"), tr("Compiler"), [colorManager, iconsManager] { return new CompilerAutolinkWidget(tr("Auto Link"),tr("Compiler"),iconsManager); }, "");

    dialog->addPage(tr("GNU Assembler"), tr("Compiler"), [colorManager, iconsManager] { return new CompilerGASWidget(tr("GNU Assembler"),tr("Compiler"),iconsManager); }, "");

    dialog->addPage(tr("NASM"), tr("Compiler"), [colorManager, iconsManager] { return new CompilerNASMWidget(tr("NASM"),tr("Compiler"),iconsManager); }, "");

    dialog->addPage(tr("General"), tr("Editor"), [colorManager, iconsManager] { return new EditorGeneralWidget(tr("General"),tr("Editor"),iconsManager); }, "general encoding codifica indent tabs tabulazioni");

    dialog->addPage(tr("Font"), tr("Editor"), [colorManager, iconsManager] { return new EditorFontWidget(tr("Font"),tr("Editor"),iconsManager); }, "");

    dialog->addPage(tr("Copy & Export"), tr("Editor"), [colorManager, iconsManager] { return new EditorClipboardWidget(colorManager, tr("Copy & Export"),tr("Editor"),iconsManager); }, "");

    dialog->addPage(tr("Color"), tr("Editor"), [colorManager, iconsManager] { return new EditorColorSchemeWidget(colorManager, tr("Color"),tr("Editor"),iconsManager); }, "colors schemes palette colori evidenziazione");

    dialog->addPage(tr("Code Completion"), tr("Editor"), [colorManager, iconsManager] { return new EditorCodeCompletionWidget(tr("Code Completion"),tr("Editor"),iconsManager); }, "");

    dialog->addPage(tr("Symbol Completion"), tr("Editor"), [colorManager, iconsManager] { return new EditorSymbolCompletionWidget(tr("Symbol Completion"),tr("Editor"),iconsManager); }, "");

    dialog->addPage(tr("Snippet"), tr("Editor"), [colorManager, iconsManager] { return new EditorSnippetWidget(colorManager, tr("Snippet"),tr("Editor"),iconsManager); }, "snippets templates code import export frammenti modelli codice");

    dialog->addPage(tr("Auto Syntax Checking"), tr("Editor"), [colorManager, iconsManager] { return new EditorSyntaxCheckWidget(tr("Auto Syntax Checking"),tr("Editor"),iconsManager); }, "");

    dialog->addPage(tr("Tooltips"), tr("Editor"), [colorManager, iconsManager] { return new EditorTooltipsWidget(tr("Tooltips"),tr("Editor"),iconsManager); }, "");

    dialog->addPage(tr("Auto save"), tr("Editor"), [colorManager, iconsManager] { return new EditorAutoSaveWidget(tr("Auto save"),tr("Editor"),iconsManager); }, "save recovery backup crash salvataggio recupero");

    dialog->addPage(tr("Misc"), tr("Editor"), [colorManager, iconsManager] { return new EditorMiscWidget(tr("Misc"),tr("Editor"),iconsManager); }, "");

    dialog->addPage(tr("Custom C/C++ Keywords"), tr("Languages"), [colorManager, iconsManager] { return new EditorCustomCTypeKeywordsWidget(tr("Custom C/C++ Keywords"),tr("Languages"),iconsManager); }, "");

//    widget = new LanguageCFormatWidget(tr("C/C++ Format"),tr("Languages"),iconsManager);
//    dialog->addWidget(widget);
    dialog->addPage(tr("ASM Generation"), tr("Languages"), [colorManager, iconsManager] { return new LanguageAsmGenerationWidget(tr("ASM Generation"),tr("Languages"),iconsManager); }, "");

    dialog->addPage(tr("General"), tr("Program Runner"), [colorManager, iconsManager] { return new ExecutorGeneralWidget(tr("General"),tr("Program Runner"),iconsManager); }, "general encoding codifica indent tabs tabulazioni");

    dialog->addPage(tr("Problem Set"), tr("Program Runner"), [colorManager, iconsManager] { return new ExecutorProblemSetWidget(tr("Problem Set"),tr("Program Runner"),iconsManager); }, "");

    dialog->addPage(tr("General"), tr("Debugger"), [colorManager, iconsManager] { return new DebugGeneralWidget(tr("General"),tr("Debugger"),iconsManager); }, "general encoding codifica indent tabs tabulazioni");

    dialog->addPage(tr("General"), tr("Code Formatter"), [colorManager, iconsManager] { return new FormatterGeneralWidget(colorManager, tr("General"),tr("Code Formatter"),iconsManager); }, "general encoding codifica indent tabs tabulazioni");

    dialog->addPage(tr("Program"), tr("Code Formatter"), [colorManager, iconsManager] { return new FormatterPathWidget(tr("Program"),tr("Code Formatter"),iconsManager); }, "");

    dialog->addPage(tr("General"), tr("Tools"), [colorManager, iconsManager] { return new ToolsGeneralWidget(tr("General"),tr("Tools"),iconsManager); }, "general encoding codifica indent tabs tabulazioni");

#ifdef ENABLE_VCS
    dialog->addPage(tr("Git"), tr("Tools"), [colorManager, iconsManager] { return new ToolsGitWidget(tr("Git"),tr("Tools"),iconsManager); }, "");
#endif

    dialog->addPage(tr("Recovery and resilience"), tr("Environment"), [iconsManager] { return new EnvironmentRecoveryWidget(tr("Recovery and resilience"), tr("Environment"), iconsManager); }, "recovery backup crash recover recupero resilienza salvataggio");

    dialog->selectFirstWidget();

    return dialog;
}

PSettingsDialog SettingsDialog::projectOptionDialog(IconsManager *iconsManager, QWidget *parent)
{
    PSettingsDialog dialog = std::make_shared<SettingsDialog>(parent);


    bool isMicroControllerProject=false;
    std::shared_ptr<Project> project = pMainWindow->project();
#ifdef ENABLE_SDCC
    if (project)
        isMicroControllerProject=(project->options().type==ProjectType::MicroController);
#endif

    dialog->setWindowTitle(tr("Project Options"));

    SettingsWidget* widget = new ProjectGeneralWidget(tr("General"),tr("Project"),iconsManager);
    dialog->addWidget(widget);

    widget = new ProjectFilesWidget(tr("Files"),tr("Project"),iconsManager);
    dialog->addWidget(widget);

    widget = new ProjectCompilerWidget(tr("Compiler Set"),tr("Project"),iconsManager);
    dialog->addWidget(widget);

    widget = new ProjectCompileParamatersWidget(tr("Custom Compile options"),tr("Project"),iconsManager);
    dialog->addWidget(widget);

    widget = new ProjectDirectoriesWidget(tr("Directories"),tr("Project"),iconsManager);
    dialog->addWidget(widget);

    if (!isMicroControllerProject) {
        widget = new ProjectPreCompileWidget(tr("Precompiled Header"),tr("Project"),iconsManager);
        dialog->addWidget(widget);
    }

    widget = new ProjectMakefileWidget(tr("Makefile"),tr("Project"),iconsManager);
    dialog->addWidget(widget);

    widget = new ProjectOutputWidget(tr("Output"),tr("Project"),iconsManager);
    dialog->addWidget(widget);

    if (!isMicroControllerProject) {
        widget = new ProjectDLLHostWidget(tr("DLL host"),tr("Project"),iconsManager);
        dialog->addWidget(widget);
    }

#ifdef Q_OS_WIN
    if (!isMicroControllerProject) {
        widget = new ProjectVersionInfoWidget(tr("Version info"),tr("Project"),iconsManager);
        dialog->addWidget(widget);
    }
#endif

    dialog->selectFirstWidget();

    return dialog;
}

bool SettingsDialog::setCurrentWidget(const QString &widgetName, const QString &groupName)
{
    { const QSignalBlocker selection(ui->widgetsView->selectionModel()); mSearch->clear(); mFilter->setFilterFixedString(QString()); }
    for (int row = 0; row < model.rowCount(); ++row) {
        auto *group = model.item(row);
        if (group->data(OriginalGroupRole).toString() != groupName) continue;
        for (int i = 0; i < group->rowCount(); ++i) {
            auto *item = group->child(i);
            if (item->text() == widgetName) { ui->widgetsView->setCurrentIndex(mFilter->mapFromSource(item->index())); return true; }
        }
    }
    return false;
}

void SettingsDialog::widget_settings_changed(bool value)
{
    ui->btnApply->setEnabled(value);
}

void SettingsDialog::onWidgetsViewCurrentChanged(const QModelIndex &index, const QModelIndex &previous)
{
    if (!showWidget(index)) {
        const QSignalBlocker blocked(ui->widgetsView->selectionModel());
        ui->widgetsView->setCurrentIndex(previous);
    }
}

void SettingsDialog::on_btnCancel_pressed()
{
    this->close();
}

void SettingsDialog::on_btnApply_pressed()
{
    saveCurrentPageSettings(false);
}

void SettingsDialog::on_btnOk_pressed()
{
    if (saveCurrentPageSettings(false)) this->close();
}

bool SettingsDialog::saveCurrentPageSettings(bool confirm)
{
    auto *widget = qobject_cast<SettingsWidget *>(ui->scrollArea->widget());
    if (!widget || !widget->isSettingsChanged()) return true;
    if (confirm) {
        const auto choice = QMessageBox::question(this, tr("Save Changes"), tr("Save changes before switching to another preference page?"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
        if (choice == QMessageBox::Cancel) return false;
        if (choice == QMessageBox::Discard) { widget->load(); return true; }
    }
    if (!widget->save()) return false;
    if (pSettings->sync() != QSettings::NoError) {
        widget->setSettingsChanged();
        QMessageBox::warning(this, tr("Save Error"), tr("Preferences could not be written to disk. Your changes are still available; check the configuration folder permissions and retry."));
        return false;
    }
    return true;
}

void SettingsDialog::closeEvent(QCloseEvent *event)
{
    pSettings->ui().setSettingsDialogWidth(width());
    pSettings->ui().setSettingsDialogHeight(height());

    QList<int> sizes = ui->splitter->sizes();
    pSettings->ui().setSettingsDialogSplitterPos(sizes[0]);

    QDialog::closeEvent(event);
}

bool SettingsDialog::appShouldQuit() const
{
    return mAppShouldQuit;
}

void SettingsDialog::closeAndQuit()
{
    mAppShouldQuit = true;
    close();
}

bool SettingsDialog::showWidget(const QModelIndex &index)
{
    if (!index.isValid()) return true;
    const auto source = mFilter->mapToSource(index);
    const int i = source.data(GetWidgetIndexRole).toInt();
    if (i < 0) {
        ui->widgetsView->expand(index);
        const auto child = mFilter->index(0, 0, index);
        if (child.isValid()) ui->widgetsView->setCurrentIndex(child);
        return true;
    }
    if (i >= mSettingWidgets.size() || ui->scrollArea->widget() == mSettingWidgets[i]) return true;
    if (!saveCurrentPageSettings(true)) return false;
    try {
        if (!mSettingWidgets[i]) mSettingWidgets[i] = mFactories[i]();
        auto *widget = mSettingWidgets[i];
        if (!mInitialized.contains(widget)) {
            widget->init(); mInitialized.insert(widget);
            connect(widget, &SettingsWidget::settingsChanged, this, &SettingsDialog::widget_settings_changed);
        }
        if (auto *old = ui->scrollArea->takeWidget()) {
            if (old == ui->scrollAreaWidgetContents) { delete old; ui->scrollAreaWidgetContents = nullptr; }
            else old->setParent(nullptr);
        }
        ui->scrollArea->setWidget(widget);
        ui->lblWidgetCaption->setText(QString("%1 > %2").arg(widget->group(), widget->name()));
        ui->btnApply->setEnabled(false);
        return true;
    } catch (const FileError &error) {
        QMessageBox::warning(this, tr("Load Error"), error.reason()); return false;
    }
}
