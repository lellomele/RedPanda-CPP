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
#include "codesnippetsmanager.h"
#include "snippetsstorage.h"
#include <QSaveFile>
#include "settings/dirsettings.h"
#include "systemconsts.h"
#include <qt_utils/utils.h>
#include <QMessageBox>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

CodeSnippetsManager::CodeSnippetsManager(DirSettings *dirSettings, QObject *parent) : QObject(parent)
{
    Q_ASSERT(dirSettings!=nullptr);
    mDirSettings = dirSettings;
}

void CodeSnippetsManager::load()
{
    const QString path = includeTrailingPathDelimiter(mDirSettings->config()) + "snippets-library.json";
    if (QFile::exists(path)) {
        QFile file(path); QString error; QList<PCodeSnippet> snippets;
        if (file.open(QIODevice::ReadOnly)) {
            const QByteArray bytes = file.read(SnippetStorage::MaxFileBytes + 1);
            const auto doc = QJsonDocument::fromJson(bytes);
            const auto templates = doc.object().value("templates").toObject();
            if (file.error() == QFile::NoError && SnippetStorage::decode(bytes, snippets, error) &&
                templates.value("cpp").isString() && templates.value("c").isString() && templates.value("gas").isString()) {
                mSnippets = snippets; mNewCppFileTemplate = templates.value("cpp").toString();
                mNewCFileTemplate = templates.value("c").toString(); mNewGASFileTemplate = templates.value("gas").toString(); return;
            }
        }
        auto *message = new QMessageBox(QMessageBox::Warning, tr("Snippet library recovered"),
            tr("The snippet file could not be loaded. The previous library or built-in defaults are still available. The original file was not changed.\n%1").arg(path), QMessageBox::Ok);
        message->setAttribute(Qt::WA_DeleteOnClose); message->open();
    }
    loadSnippets();
    mNewCppFileTemplate = loadNewFileTemplate(DEV_NEWFILETEMPLATES_FILE);
    mNewCFileTemplate = loadNewFileTemplate(DEV_NEWCFILETEMPLATES_FILE);
    mNewGASFileTemplate = loadNewFileTemplate(DEV_NEWGASFILETEMPLATES_FILE);
}

void CodeSnippetsManager::save()
{
    QString error;
    if (!SnippetStorage::validate(mSnippets, error)) throw FileError(error);
    QJsonObject root{{"version", 1}, {"snippets", QJsonDocument::fromJson(SnippetStorage::encode(mSnippets)).array()},
        {"templates", QJsonObject{{"cpp", mNewCppFileTemplate}, {"c", mNewCFileTemplate}, {"gas", mNewGASFileTemplate}}}};
    const auto bytes = QJsonDocument(root).toJson();
    if (bytes.size() > SnippetStorage::MaxFileBytes) throw FileError(tr("Snippet file is too large (maximum 8 MB)."));
    QSaveFile file(includeTrailingPathDelimiter(mDirSettings->config()) + "snippets-library.json"); file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        throw FileError(tr("Cannot save snippet library: %1").arg(file.errorString()));
}

QList<PCodeSnippet> CodeSnippetsManager::snippetsForLanguage(const QString &language) const
{
    QList<PCodeSnippet> result;
    for (const auto &snippet : mSnippets)
        if (SnippetStorage::matches(snippet, language)) result.append(snippet);
    return result;
}

void CodeSnippetsManager::loadSnippets()
{
    const QString filename = includeTrailingPathDelimiter(mDirSettings->config()) + DEV_CODESNIPPET_FILE;
    QList<PCodeSnippet> candidate; QString error;
    if (SnippetStorage::read(QFile::exists(filename) ? filename : ":/config/codesnippets.json", candidate, error)) {
        mSnippets = candidate; return;
    }
    const QString originalError = error;
    if (mSnippets.isEmpty()) SnippetStorage::read(":/config/codesnippets.json", candidate, error);
    if (!candidate.isEmpty()) mSnippets = candidate;
    auto *message = new QMessageBox(QMessageBox::Warning, tr("Snippet library recovered"),
        tr("The snippet file could not be loaded. The previous library or built-in defaults are still available. The original file was not changed.\n%1").arg(originalError), QMessageBox::Ok);
    message->setAttribute(Qt::WA_DeleteOnClose); message->open();
}

QString CodeSnippetsManager::loadNewFileTemplate(const QString &fn)
{
    QString filename = includeTrailingPathDelimiter(mDirSettings->config()) + fn;
    QFile file(filename);
    if (!file.exists()) {
        return "";
    }
    if (!file.open(QFile::ReadOnly)) {
        QMessageBox::critical(nullptr,
                              tr("Load new file template failed"),
                              tr("Can't open new file template file '%1' for read.")
                              .arg(filename));
        return "";
    }
    const auto bytes = file.read(1024 * 1024 + 1);
    if (bytes.size() > 1024 * 1024 || file.error() != QFile::NoError) { qWarning() << "File template could not be read:" << filename; return QString(); }
    return QString::fromUtf8(bytes);
}

QString CodeSnippetsManager::newGASFileTemplate() const
{
    return mNewGASFileTemplate;
}

void CodeSnippetsManager::setNewGASFileTemplate(const QString &newContent)
{
    mNewGASFileTemplate = newContent;
}

QString CodeSnippetsManager::newCFileTemplate() const
{
    return mNewCFileTemplate;
}

void CodeSnippetsManager::setNewCFileTemplate(const QString &newContent)
{
    mNewCFileTemplate = newContent;
}

const QList<PCodeSnippet> &CodeSnippetsManager::snippets() const
{
    return mSnippets;
}

void CodeSnippetsManager::setSnippets(const QList<PCodeSnippet> &newSnippets)
{
    mSnippets = SnippetStorage::clone(newSnippets);
}

const QString &CodeSnippetsManager::newCppFileTemplate() const
{
    return mNewCppFileTemplate;
}

void CodeSnippetsManager::setNewCppFileTemplate(const QString &content)
{
    mNewCppFileTemplate = content;
}

void CodeSnippetsModel::addSnippet(const QString &caption, const QString &prefix, const QString &code, const QString &description, int menuSection, const QString &language, bool enabled)
{
    beginInsertRows(QModelIndex(),mSnippets.count(),mSnippets.count());
    PCodeSnippet snippet = std::make_shared<CodeSnippet>();
    snippet->caption = caption;
    snippet->prefix = prefix;
    snippet->code = code;
    snippet->desc = description;
    snippet->section = menuSection;
    snippet->language = language;
    snippet->enabled = enabled;
    mSnippets.append(snippet);
    endInsertRows();
}

void CodeSnippetsModel::remove(int index)
{
    if (index < 0 || index >= mSnippets.count()) return;
    beginRemoveRows(QModelIndex(),index,index);
    mSnippets.removeAt(index);
    endRemoveRows();
}

void CodeSnippetsModel::clear()
{
    beginResetModel();
    mSnippets.clear();
    endResetModel();
}

QModelIndex CodeSnippetsModel::lastSnippetCaption()
{
    if (mSnippets.isEmpty()) return QModelIndex();
    return createIndex(mSnippets.count()-1,0);
}

int CodeSnippetsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : mSnippets.count();
}

int CodeSnippetsModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : 6;
}

QVariant CodeSnippetsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.model() != this || index.row() < 0 || index.row() >= mSnippets.size() || index.column() < 0 || index.column() >= 6) {
        return QVariant();
    }
    const auto &current = mSnippets[index.row()];
    if (role == Qt::UserRole) return current->caption + " " + current->prefix + " " + current->desc + " " + current->code + " " + current->language;
    if (role == Qt::CheckStateRole && index.column() == 5)
        return mSnippets[index.row()]->enabled ? Qt::Checked : Qt::Unchecked;
    if (role==Qt::DisplayRole
            || role == Qt::EditRole) {
        int row = index.row();
        PCodeSnippet snippet = mSnippets[row];
        switch(index.column()) {
        case 0:
            return snippet->caption;
        case 1:
            return snippet->prefix;
        case 2:
            return snippet->desc;
        case 3:
            return snippet->section;
        case 4:
            if (role == Qt::EditRole) return snippet->language;
            return snippet->language == "all" ? tr("All") : snippet->language == "cpp" ? QString("C++") : snippet->language == "c" ? QString("C") : tr("Assembly");
        case 5:
            return role == Qt::DisplayRole ? QVariant() : QVariant(snippet->enabled);
        }
    }
    return QVariant();
}

bool CodeSnippetsModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || index.model() != this || index.row() < 0 || index.row() >= mSnippets.size()) return false;
    auto &s = mSnippets[index.row()];
    if (index.column() == 5 && role == Qt::CheckStateRole) s->enabled = value.toInt() == Qt::Checked;
    else if (role == Qt::EditRole) {
        switch (index.column()) {
        case 0: s->caption = value.toString(); break;
        case 1: s->prefix = value.toString(); break;
        case 2: s->desc = value.toString(); break;
        case 3: { bool ok; int section = value.toInt(&ok); if (!ok || section < -1 || section > 999) return false; s->section = section; break; }
        case 4: { QString language = value.toString().toLower(); if (!QStringList{"all", "c", "cpp", "asm"}.contains(language)) return false; s->language = language; break; }
        default: return false;
        }
    } else return false;
    emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole, Qt::CheckStateRole});
    return true;
}

QVariant CodeSnippetsModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role ==  Qt::DisplayRole) {
        switch(section) {
        case 0:
            return tr("Caption");
        case 1:
            return tr("Completion Prefix");
        case 2:
            return tr("Description");
        case 3:
            return tr("Menu Section");
        case 4: return tr("Language");
        case 5: return tr("Enabled");
        }
    }
    return QVariant();
}

Qt::ItemFlags CodeSnippetsModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    if (index.column() == 5) return Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    return Qt::ItemIsEditable | Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

const QList<PCodeSnippet> &CodeSnippetsModel::snippets() const
{
    return mSnippets;
}

void CodeSnippetsModel::updateSnippets(const QList<PCodeSnippet> &snippets)
{
    beginResetModel();
    mSnippets = SnippetStorage::clone(snippets);
    endResetModel();
}
