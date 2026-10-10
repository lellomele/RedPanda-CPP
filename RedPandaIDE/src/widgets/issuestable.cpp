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
#include "issuestable.h"
#include "../utils.h"
#include <QHeaderView>
#include "../settings.h"
#include "../mainwindow.h"
#include "../editor.h"
#include "../editormanager.h"
#include "../utils/colorcontrast.h"
#include <QStyleOptionViewItem>


DiagnosticItemDelegate::DiagnosticItemDelegate(QObject *parent) : QStyledItemDelegate(parent), mTextColors(256) {}

void DiagnosticItemDelegate::initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const {
    QStyledItemDelegate::initStyleOption(option, index);
    const auto group = !(option->state & QStyle::State_Enabled) ? QPalette::Disabled
        : (option->state & QStyle::State_Active) ? QPalette::Active : QPalette::Inactive;
    option->palette.setCurrentColorGroup(group);
    const QColor window = UiContrast::composite(option->palette.color(group, QPalette::Window), QColor(Qt::white));
    const QColor base = UiContrast::composite(option->palette.color(group, QPalette::Base), window);
    const bool alternate = option->features & QStyleOptionViewItem::Alternate;
    QColor background = alternate ? UiContrast::composite(option->palette.color(group, QPalette::AlternateBase), base) : base;
    if (option->backgroundBrush.style() != Qt::NoBrush) background = UiContrast::composite(option->backgroundBrush.color(), background);
    const bool selected = option->state & QStyle::State_Selected;
    if (selected) background = UiContrast::composite(option->palette.color(group, QPalette::Highlight), background);
    const QVariant foreground = index.data(Qt::ForegroundRole);
    QColor preferred = foreground.canConvert<QColor>() ? foreground.value<QColor>()
        : option->palette.color(group, selected ? QPalette::HighlightedText : QPalette::Text);
    QColor text;
    if (preferred.isValid()) {
        const quint64 key = (quint64(preferred.rgba()) << 32) | quint64(background.rgba());
        if (const auto *cached = mTextColors.object(key)) text = *cached;
        else { text = UiContrast::readableText(preferred, background); mTextColors.insert(key, new QColor(text)); }
    } else text = UiContrast::readableText(preferred, background);
    // Paint the exact opaque background used for the contrast check, including
    // alternate rows, alpha colors, selection and inactive/disabled palettes.
    option->backgroundBrush = QBrush(background);
    option->palette.setColor(selected ? QPalette::Highlight : alternate ? QPalette::AlternateBase : QPalette::Base, background);
    option->palette.setColor(QPalette::Text, text);
    option->palette.setColor(QPalette::HighlightedText, text);
}

IssuesTable::IssuesTable(QWidget *parent):
    QTableView(parent)
{
    mModel = new IssuesModel(this);
    QItemSelectionModel *m=this->selectionModel();
    this->setModel(mModel);
    setItemDelegate(new DiagnosticItemDelegate(this));
    delete m;
    this->setColumnWidth(0,200);
    this->setColumnWidth(1,45);
    this->setColumnWidth(2,45);
    if (mModel->italianDiagnostics()) { setColumnWidth(3, 320); setColumnWidth(4, 360); }
}

const QVector<PCompileIssue> &IssuesTable::issues() const
{
    return mModel->issues();
}

IssuesModel *IssuesTable::issuesModel()
{
    return mModel;
}

void IssuesTable::setErrorColor(QColor color)
{
    mModel->setErrorColor(color);
}

void IssuesTable::setWarningColor(QColor color)
{
    mModel->setWarningColor(color);
}

QString IssuesTable::toHtml()
{
    QString result = "<table><thead><tr>";
    for (int column = 0; column < mModel->columnCount({}); ++column)
        result += "<th>" + mModel->headerData(column, Qt::Horizontal, Qt::DisplayRole).toString().toHtmlEscaped() + "</th>";
    result += "</tr></thead><tbody>";
    for (int row = 0; row < mModel->count(); ++row) {
        const auto issue = mModel->issue(row);
        QStringList cells{issue->filename, issue->line >= 0 ? QString::number(issue->line + 1) : QString{}, issue->column >= 0 ? QString::number(issue->column + 1) : QString{},
                          mModel->italianDiagnostics() ? (mModel->translatedDescription(row).isEmpty() ? issue->description : mModel->translatedDescription(row)) : mModel->originalDescription(row)};
        if (mModel->italianDiagnostics()) cells.append(mModel->originalDescription(row));
        result += "<tr>";
        for (const auto &cell : cells) result += "<td>" + cell.toHtmlEscaped() + "</td>";
        result += "</tr>\n";
    }
    return result + "</tbody></table>";
}

QString IssuesTable::toTxt()
{
    QString result;
    for (int row = 0; row < mModel->count(); ++row) {
        const auto issue = mModel->issue(row);
        QStringList cells{issue->filename, issue->line >= 0 ? QString::number(issue->line + 1) : QString{}, issue->column >= 0 ? QString::number(issue->column + 1) : QString{},
                          mModel->italianDiagnostics() ? (mModel->translatedDescription(row).isEmpty() ? issue->description : mModel->translatedDescription(row)) : mModel->originalDescription(row)};
        if (mModel->italianDiagnostics()) cells.append(mModel->originalDescription(row));
        result += cells.join('\t') + '\n';
    }
    return result;
}

IssuesModel::IssuesModel(QObject *parent):
    QAbstractTableModel(parent),
    mItalianDiagnostics(DiagnosticCatalog::isItalianInterface()),
    mErrorColor("#c74747"), mWarningColor("#a26c14")
{

}

void IssuesModel::addIssue(PCompileIssue issue)
{
    if (!issue) return;
    const auto translation = mItalianDiagnostics && issue->gccDiagnostic && (issue->type == CompileIssueType::Error || issue->type == CompileIssueType::Warning || issue->type == CompileIssueType::Other)
        ? DiagnosticCatalog::translate(issue->description, "it") : DiagnosticCatalog::Translation{};
    beginInsertRows(QModelIndex(),mIssues.size(),mIssues.size());
    mIssues.push_back(issue);
    mTranslations.push_back(translation);
    endInsertRows();
}

void IssuesModel::clearIssues()
{
    QSet<QString> issueFiles;
    foreach(const PCompileIssue& issue, mIssues) {
        if (!(issue->filename.isEmpty())){
            issueFiles.insert(issue->filename);
        }
    }
    foreach (const QString& filename, issueFiles) {
        Editor *e=pMainWindow->editorManager()->getOpenedEditor(filename);
        if (e)
            e->clearSyntaxIssues();
    }
    if (mIssues.size()>0) {
        beginResetModel();
        mIssues.clear();
        mTranslations.clear();
        endResetModel();
    }
}

void IssuesModel::setErrorColor(QColor color)
{
    const QColor effective = color.isValid() && color.alpha() > 0 ? color : QColor("#c74747");
    if (mErrorColor == effective) return;
    mErrorColor = effective;
    if (!mIssues.isEmpty()) emit dataChanged(index(0, 0), index(count() - 1, columnCount({}) - 1), {Qt::ForegroundRole});
}

void IssuesModel::setWarningColor(QColor color)
{
    const QColor effective = color.isValid() && color.alpha() > 0 ? color : QColor("#a26c14");
    if (mWarningColor == effective) return;
    mWarningColor = effective;
    if (!mIssues.isEmpty()) emit dataChanged(index(0, 0), index(count() - 1, columnCount({}) - 1), {Qt::ForegroundRole});
}

PCompileIssue IssuesModel::issue(int row)
{
    if (row<0 || row>=static_cast<int>(mIssues.size())) {
        return PCompileIssue();
    }

    return mIssues[row];
}

const QVector<PCompileIssue> &IssuesModel::issues() const
{
    return mIssues;
}

int IssuesModel::count()
{
    return mIssues.size();
}

void IssuesTable::addIssue(PCompileIssue issue)
{
    mModel->addIssue(issue);
}

PCompileIssue IssuesTable::issue(const QModelIndex &index)
{
    if (!index.isValid())
        return PCompileIssue();
    return issue(index.row());
}

PCompileIssue IssuesTable::issue(const int row)
{
    return mModel->issue(row);
}

int IssuesTable::count()
{
    return mModel->count();
}

void IssuesTable::clearIssues()
{
    mModel->clearIssues();
}

int IssuesModel::rowCount(const QModelIndex &) const
{
    return mIssues.size();
}

int IssuesModel::columnCount(const QModelIndex &) const
{
    return mItalianDiagnostics ? 5 : 4;
}

QVariant IssuesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();
    if (index.row()<0 || index.row() >= static_cast<int>(mIssues.size()))
        return QVariant();
    PCompileIssue issue = mIssues[index.row()];
    if (!issue)
        return QVariant();
    switch (role) {
    case Qt::DisplayRole:
    case Qt::ToolTipRole:
        switch (index.column()) {
        case 0: {
            if (role == Qt::DisplayRole)
                return extractFileName(issue->filename);
            else
                return issue->filename;
        }
        case 1:
            if (issue->line>=0)
                return issue->line+1;
            else
                return "";
        case 2:
            if (issue->column>=0)
                return issue->column+1;
            else
                return "";
        case 3:
            if (role == Qt::ToolTipRole && !translatedDescription(index.row()).isEmpty())
                return "<p>" + diagnosticHint(index.row()).toHtmlEscaped().replace('\n', "<br>") + "</p>";
            return translatedDescription(index.row()).isEmpty() ? issue->description : translatedDescription(index.row());
        case 4:
            return role == Qt::ToolTipRole ? "<p>" + originalDescription(index.row()).toHtmlEscaped() + "</p>" : originalDescription(index.row());
        default:
            return QVariant();
        }
    case Qt::ForegroundRole:
        switch(issue->type) {
        case CompileIssueType::Error:
            return mErrorColor;
        case CompileIssueType::Warning:
            return mWarningColor;
        default:
            return QVariant();
        }
    case Qt::FontRole: {
        QFont newFont=((IssuesTable *)parent())->font();
        switch(issue->type) {
        case CompileIssueType::Error:
        case CompileIssueType::Warning:
        {
            newFont.setBold(true);
            break;
        }
        default:
            newFont.setBold(issue->line == 0);
        }
        return newFont;
    }
    default:
        return QVariant();
    }
}

QVariant IssuesModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal ) {
        switch(role) {
        case Qt::DisplayRole:
            switch(section) {
            case 0:
                return tr("Filename");
            case 1:
                return tr("Line");
            case 2:
                return tr("Col");
            case 3:
                return mItalianDiagnostics ? tr("Italian description") : tr("Description");
            case 4:
                return tr("Original message");
            }
            break;
        }
    }
    return QVariant();
}

QString IssuesModel::translatedDescription(int row) const {
    return row >= 0 && row < mTranslations.size() ? mTranslations[row].text : QString{};
}
QString IssuesModel::originalDescription(int row) const {
    if (row < 0 || row >= mIssues.size() || !mIssues[row]) return {};
    const auto &issue = mIssues[row];
    return issue->originalDescription.isEmpty() ? issue->description : issue->originalDescription;
}
QString IssuesModel::diagnosticHint(int row) const {
    if (row < 0 || row >= mIssues.size() || !mIssues[row]) return {};
    if (translatedDescription(row).isEmpty()) return mIssues[row]->description;
    return tr("Italian:") + "\n" + translatedDescription(row) + "\n\n" + tr("Original:") + "\n" + originalDescription(row);
}
bool IssuesModel::italianDiagnostics() const { return mItalianDiagnostics; }
