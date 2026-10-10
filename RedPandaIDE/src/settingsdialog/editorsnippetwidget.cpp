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
#include <qsynedit/document.h>
#include "editorsnippetwidget.h"
#include "ui_editorsnippetwidget.h"
#include "../mainwindow.h"
#include "../codesnippetsmanager.h"
#include "../iconsmanager.h"
#include "../syntaxermanager.h"
#include "../settings.h"
#include "../utils/file.h"


#include <QItemSelectionModel>
#include <QHeaderView>
#include <QSortFilterProxyModel>
#include <QLineEdit>
#include <QPushButton>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QStyledItemDelegate>
#include <QComboBox>
#include <QLabel>
#include <QScopedValueRollback>
#include "../snippetsstorage.h"

class SnippetLanguageDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &, const QModelIndex &) const override {
        auto *box = new QComboBox(parent);
        box->addItem(QObject::tr("All"), "all"); box->addItem("C", "c"); box->addItem("C++", "cpp"); box->addItem(QObject::tr("Assembly"), "asm");
        return box;
    }
    void setEditorData(QWidget *editor, const QModelIndex &index) const override {
        auto *box = qobject_cast<QComboBox *>(editor); box->setCurrentIndex(box->findData(index.data(Qt::EditRole)));
    }
    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override {
        model->setData(index, qobject_cast<QComboBox *>(editor)->currentData(), Qt::EditRole);
    }
};

EditorSnippetWidget::EditorSnippetWidget(ColorManager *colorManager,const QString& name, const QString& group,
                                         IconsManager *iconsManager,
                                         QWidget *parent) :
    SettingsWidget(name,group,iconsManager,parent),
    ui(new Ui::EditorSnippetWidget)
{
    mUpdatingCode = false;
    ui->setupUi(this);
    QItemSelectionModel* m=ui->tblSnippets->selectionModel();
    mProxy = new QSortFilterProxyModel(this);
    mProxy->setSourceModel(&mModel); mProxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    mProxy->setFilterKeyColumn(0); mProxy->setFilterRole(Qt::UserRole);
    ui->tblSnippets->setModel(mProxy);
    auto *header = ui->tblSnippets->horizontalHeader(); header->setStretchLastSection(false);
    for (int column = 0; column < 6; ++column) header->setSectionResizeMode(column, column == 2 ? QHeaderView::Stretch : QHeaderView::ResizeToContents);
    header->setMinimumSectionSize(64);
    ui->tblSnippets->setItemDelegateForColumn(4, new SnippetLanguageDelegate(this));
    mSearch = new QLineEdit(this); mSearch->setClearButtonEnabled(true);
    mSearch->setPlaceholderText(tr("Search snippets by name, prefix, description or code"));
    mSearch->setObjectName("snippetSearch");
    ui->horizontalLayout_3->insertWidget(0, mSearch);
    connect(mSearch, &QLineEdit::textChanged, mProxy, &QSortFilterProxyModel::setFilterFixedString);
    auto *tools = new QHBoxLayout;
    auto addTool = [this, tools](const QString &text, auto action) {
        auto *button = new QPushButton(text, this); tools->addWidget(button);
        connect(button, &QPushButton::clicked, this, action);
    };
    addTool(tr("Duplicate"), [this] { duplicateSnippet(); });
    addTool(tr("Import..."), [this] { importSnippets(); });
    addTool(tr("Export..."), [this] { exportSnippets(); });
    addTool(tr("Restore defaults"), [this] {
        if (QMessageBox::question(this, tr("Restore defaults"), tr("Replace this snippet library with the built-in defaults?")) != QMessageBox::Yes) return;
        QList<PCodeSnippet> defaults; QString error;
        if (SnippetStorage::read(":/config/codesnippets.json", defaults, error)) { mModel.updateSnippets(defaults); mSearch->clear(); setSettingsChanged(); }
        else QMessageBox::warning(this, tr("Snippet library"), error);
    });
    addTool(tr("Placeholder"), [this] { ui->editCode->setSelText("%REPL_BEGIN%" + ui->editCode->selText() + "%REPL_END%"); });
    addTool(tr("Cursor stop"), [this] { ui->editCode->setSelText("%INSERT%"); });
    tools->addStretch(); ui->horizontalLayout_3->insertLayout(1, tools);
    auto *help = new QLabel(tr("Tab moves between placeholders. Language 'All' works in every supported editor."), this);
    help->setWordWrap(true); ui->horizontalLayout_3->insertWidget(2, help);
    delete m;
    connect(ui->editCode, &Editor::changed,
            [this] {
        if (mUpdatingCode)
            return;
        QModelIndex index = ui->tblSnippets->currentIndex();
        if (!index.isValid())
            return;
        PCodeSnippet snippet = mModel.snippets()[mProxy->mapToSource(index).row()];
        snippet->code = ui->editCode->text();
        const QModelIndex source = mProxy->mapToSource(index);
        emit mModel.dataChanged(source.sibling(source.row(), 0), source.sibling(source.row(), 5));
        setSettingsChanged();
    });
    connect(ui->tblSnippets->selectionModel(), &QItemSelectionModel::currentChanged,
            [this] {
        QModelIndex index = ui->tblSnippets->currentIndex();
        if (!index.isValid()) {
            ui->editCode->setEnabled(false);
            ui->editCode->clear();
        } else {
            mUpdatingCode = true;
            ui->editCode->setEnabled(true);
            PCodeSnippet snippet = mModel.snippets()[mProxy->mapToSource(index).row()];
            ui->editCode->setContent(snippet->code);
            mUpdatingCode = false;
        }
    });
    connect(&mModel, &QAbstractItemModel::dataChanged, this, &SettingsWidget::setSettingsChanged);
    connect(&mModel, &QAbstractItemModel::rowsInserted, this, &SettingsWidget::setSettingsChanged);
    connect(&mModel, &QAbstractItemModel::rowsRemoved, this, &SettingsWidget::setSettingsChanged);
    ui->editCode->setExtraKeystrokes();
    ui->editCode->setEditorSettings(&pSettings->editor());
    ui->editCode->setColorManager(colorManager);
    ui->editCode->applySettings();
    ui->editCppFileTemplate->setExtraKeystrokes();
    ui->editCppFileTemplate->setEditorSettings(&pSettings->editor());
    ui->editCppFileTemplate->setColorManager(colorManager);
    ui->editCppFileTemplate->applySettings();
    ui->editCFileTemplate->setExtraKeystrokes();
    ui->editCFileTemplate->setEditorSettings(&pSettings->editor());
    ui->editCFileTemplate->setColorManager(colorManager);
    ui->editCFileTemplate->applySettings();
    ui->editGASFileTemplate->setExtraKeystrokes();
    ui->editGASFileTemplate->setEditorSettings(&pSettings->editor());
    ui->editGASFileTemplate->setColorManager(colorManager);
    ui->editGASFileTemplate->applySettings();
    connect(ui->editCppFileTemplate,&Editor::changed,
            this, &SettingsWidget::setSettingsChanged);
    connect(ui->editCFileTemplate,&Editor::changed,
            this, &SettingsWidget::setSettingsChanged);
    connect(ui->editGASFileTemplate,&Editor::changed,
            this, &SettingsWidget::setSettingsChanged);
    ui->editCode->setFileType(FileType::CppSource);
    ui->editCppFileTemplate->setFileType(FileType::CppSource);
    ui->editCFileTemplate->setFileType(FileType::CSource);
    ui->editGASFileTemplate->setFileType(FileType::ATTASM);
    //ui->editGASFileTemplate->setSyntaxer(syntaxerManager.getSyntaxer(QSynedit::ProgrammingLanguage::ATTAssembly));
}

EditorSnippetWidget::~EditorSnippetWidget()
{
    delete ui;
}

void EditorSnippetWidget::doLoad()
{
    mModel.updateSnippets(pMainWindow->codeSnippetManager()->snippets());
    ui->editCppFileTemplate->setContent(pMainWindow->codeSnippetManager()->newCppFileTemplate());
    ui->editCFileTemplate->setContent(pMainWindow->codeSnippetManager()->newCFileTemplate());
    ui->editGASFileTemplate->setContent(pMainWindow->codeSnippetManager()->newGASFileTemplate());
}

void EditorSnippetWidget::doSave()
{
    QString error;
    if (!SnippetStorage::validate(mModel.snippets(), error)) throw FileError(error);
    auto *manager = pMainWindow->codeSnippetManager();
    const auto previous = SnippetStorage::clone(manager->snippets());
    const QString cpp = manager->newCppFileTemplate(), c = manager->newCFileTemplate(), gas = manager->newGASFileTemplate();
    manager->setSnippets(mModel.snippets());
    manager->setNewCppFileTemplate(ui->editCppFileTemplate->text());
    manager->setNewCFileTemplate(ui->editCFileTemplate->text());
    manager->setNewGASFileTemplate(ui->editGASFileTemplate->text());
    try { manager->save(); }
    catch (...) {
        manager->setSnippets(previous); manager->setNewCppFileTemplate(cpp); manager->setNewCFileTemplate(c); manager->setNewGASFileTemplate(gas);
        throw;
    }
}

void EditorSnippetWidget::on_btnAdd_clicked()
{
    mSearch->clear();
    const QString prefix = QString("snippet_%1").arg(getNewFileNumber());
    mModel.addSnippet(tr("New snippet"), prefix, "%INSERT%", "", 0);
    selectSnippet(mModel.lastSnippetCaption());
    ui->tblSnippets->edit(mProxy->mapFromSource(mModel.lastSnippetCaption()));
    setSettingsChanged();
}

void EditorSnippetWidget::updateIcons(const QSize &/*size*/)
{
    iconsManager()->setIcon(ui->btnAdd,IconsManager::ACTION_MISC_ADD);
    iconsManager()->setIcon(ui->btnRemove,IconsManager::ACTION_MISC_REMOVE);
}


void EditorSnippetWidget::on_btnRemove_clicked()
{
    QModelIndex index = ui->tblSnippets->currentIndex();
    if (!index.isValid())
        return;
    mModel.remove(mProxy->mapToSource(index).row());
    setSettingsChanged();
}

int EditorSnippetWidget::currentSnippetRow() const
{
    const QModelIndex index = mProxy->mapToSource(ui->tblSnippets->currentIndex());
    return index.isValid() ? index.row() : -1;
}

void EditorSnippetWidget::selectSnippet(const QModelIndex &source)
{
    ui->tblSnippets->setCurrentIndex(mProxy->mapFromSource(source));
}

void EditorSnippetWidget::duplicateSnippet()
{
    const int row = currentSnippetRow(); if (row < 0) return;
    const auto original = std::make_shared<CodeSnippet>(*mModel.snippets().at(row));
    QString prefix = original->prefix;
    if (!prefix.isEmpty()) {
        int suffix = 1;
        for (;;) {
            prefix = original->prefix + QString("_copy%1").arg(suffix++);
            bool exists = false;
            for (const auto &s : mModel.snippets()) if (s->prefix == prefix && s->language == original->language) exists = true;
            if (!exists) break;
        }
    }
    mSearch->clear();
    mModel.addSnippet(tr("%1 (copy)").arg(original->caption), prefix, original->code, original->desc, original->section, original->language, original->enabled);
    selectSnippet(mModel.lastSnippetCaption()); setSettingsChanged();
}

void EditorSnippetWidget::importSnippets()
{
    const QString file = QFileDialog::getOpenFileName(this, tr("Import snippet library"), QString(), tr("JSON files (*.json)"));
    if (file.isEmpty()) return;
    QList<PCodeSnippet> imported; QString error;
    if (!SnippetStorage::read(file, imported, error)) { QMessageBox::warning(this, tr("Snippet library"), error); return; }
    bool ok = false;
    const QString merge = tr("Merge (keep existing prefixes)");
    const QString mode = QInputDialog::getItem(this, tr("Import snippet library"), tr("Import mode"), {merge, tr("Replace library")}, 0, false, &ok);
    if (!ok) return;
    if (mode == merge) {
        auto combined = SnippetStorage::clone(mModel.snippets());
        for (const auto &candidate : imported) {
            bool duplicate = false;
            for (const auto &existing : combined)
                if (existing->language == candidate->language && ((!candidate->prefix.isEmpty() && existing->prefix == candidate->prefix) || (candidate->prefix.isEmpty() && existing->caption == candidate->caption))) duplicate = true;
            if (!duplicate) combined.append(candidate);
        }
        imported = combined;
    }
    if (!SnippetStorage::validate(imported, error)) { QMessageBox::warning(this, tr("Snippet library"), error); return; }
    mSearch->clear(); mModel.updateSnippets(imported); setSettingsChanged();
}

void EditorSnippetWidget::exportSnippets()
{
    const QString file = QFileDialog::getSaveFileName(this, tr("Export snippet library"), "snippets.json", tr("JSON files (*.json)"));
    if (file.isEmpty()) return;
    QString error;
    if (!SnippetStorage::write(file, mModel.snippets(), error)) QMessageBox::warning(this, tr("Snippet library"), error);
}
