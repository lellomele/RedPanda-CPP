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
#include "todoparser.h"
#include "mainwindow.h"
#include "editor.h"
#include "editormanager.h"
#include "utils/file.h"
#include "qt_utils/charsetinfo.h"
#include <QDir>

#include <QRegularExpression>

Q_GLOBAL_STATIC_WITH_ARGS(
    QRegularExpression,
    todoReg,
    (QString("\\b(todo|fixme)\\b"), QRegularExpression::CaseInsensitiveOption)
)

TodoParser::TodoParser(QObject *parent) : QObject(parent)
{
}

TodoParser::~TodoParser()
{
    if (mThread) {
        mThread->stop();
        mThread->wait();
    }
}

void TodoParser::parseFile(const QString &filename, bool isForProject)
{
    if (isForProject && pMainWindow->closingProject()) return;
    start({filename}, !isForProject);
}

void TodoParser::parseFiles(const QStringList &files)
{
    start(files, true);
}

void TodoParser::start(QStringList files, bool clearAll)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (mStopping || mThread || !pMainWindow || pMainWindow->isQuitting() || pMainWindow->isClosingAll())
        return;
    const auto capture = pMainWindow->editorManager()->inputSnapshotProvider()();
    if (!capture.succeeded()) return;
    AnalysisInput::SourceReadRequest request;
    request.documents = capture.snapshot;
    request.policy.workingDirectory = QDir::currentPath();
    if (pCharsetInfoManager) {
        const auto add = [&](const QByteArray &encoding) {
            if (!encoding.isEmpty() && !request.policy.encodings.contains(encoding) &&
                    request.policy.encodings.size() < 16)
                request.policy.encodings.append(encoding);
        };
        add(pCharsetInfoManager->getDefaultSystemEncoding());
        for (const auto &entry : pCharsetInfoManager->findCharsetByLocale(pCharsetInfoManager->localeName()))
            add(entry->name);
    }
    auto *task = new TodoThread(std::move(files), std::move(request), this);
    mThread = task;
    if (clearAll)
        connect(task, &TodoThread::parseStarted, this, [this, task] {
            if (!mStopping && mThread == task) pMainWindow->onTodoParseStarted();
        });
    connect(task, &TodoThread::parsingFile, this, [this, task](const QString &file) {
        if (!mStopping && mThread == task) pMainWindow->onTodoParsingFile(file);
    });
    connect(task, &TodoThread::todoFound, this, [this, task](const QString &file, int line, int column, const QString &text) {
        if (!mStopping && mThread == task) pMainWindow->onTodoFound(file, line, column, text);
    });
    connect(task, &TodoThread::parseFinished, this, [this, task] {
        if (!mStopping && mThread == task) pMainWindow->onTodoParseFinished();
    });
    connect(task, &QThread::finished, this, [this, task] {
        if (mThread == task) mThread = nullptr;
        task->deleteLater();
        if (mStopping) emit shutdownComplete();
    });
    task->start();
}

bool TodoParser::parsing() const
{
    Q_ASSERT(QThread::currentThread() == thread());
    return mThread != nullptr;
}

void TodoParser::requestShutdown()
{
    Q_ASSERT(QThread::currentThread() == thread());
    mStopping = true;
    if (mThread) mThread->stop();
    else emit shutdownComplete();
}

void TodoParser::resumeAdmissions()
{
    Q_ASSERT(QThread::currentThread() == thread());
    mStopping = false;
}

TodoThread::TodoThread(QStringList files, AnalysisInput::SourceReadRequest request, QObject *parent)
    : QThread(parent), mFiles(std::move(files)), mRequest(std::move(request))
{
}

void TodoThread::stop()
{
    mCancelled->store(true, std::memory_order_release);
}

void TodoThread::parseSource(const QString &filename, AnalysisInput::SourceReadSession &reader)
{
    const FileType fileType = getFileType(filename);
    QSynedit::PSyntaxer syntaxer = SyntaxerManager::getSyntaxer(fileType);
    if (!syntaxer || syntaxer->language() == QSynedit::ProgrammingLanguage::Textfile)
        return;
    const auto source = reader.readDocumentRoot(filename);
    if (!source.ready()) return;
    emit parsingFile(filename);
    const QStringList &lines = source.source->lines;
    syntaxer->resetState();
    for (int i = 0; i < lines.count() && !mCancelled->load(std::memory_order_acquire); ++i) {
        syntaxer->setLine(i, lines[i], 0);
        while (!syntaxer->eol()) {
            QSynedit::PTokenAttribute attr;
            attr = syntaxer->getTokenAttribute();
            if (attr && attr->tokenType() == QSynedit::TokenType::Comment) {
                QString token = syntaxer->getToken();
                int pos = token.indexOf(*todoReg);
                if (pos>=0) {
                    emit todoFound(
                                filename,
                                i,
                                pos+syntaxer->getTokenPos(),
                                lines[i].trimmed()
                                );
                    break;
                }
            }
            syntaxer->next();
        }
    }

}

void TodoThread::run()
{
    AnalysisInput::SourceReadSession reader(mRequest.documents, mRequest.policy, mCancelled);
    emit parseStarted();
    for (const auto &file : mFiles) {
        if (mCancelled->load(std::memory_order_acquire)) break;
        parseSource(file, reader);
    }
    emit parseFinished();
}

TodoModel::TodoModel(QObject *parent) : QAbstractListModel(parent)
{
    mIsForProject=false;
}

void TodoModel::addItem(const QString &filename, int line, int ch, const QString &lineText)
{
    QList<PTodoItem> &items=getItems(mIsForProject);
    int pos=-1;
    for (int i=0;i<items.count();i++) {
        int comp=QString::compare(filename,items[i]->filename);
        if (comp<0) {
            pos=i;
            break;
        } else if (comp==0) {
            if (line<items[i]->line)  {
                pos=i;
                break;
            }
        }
    }
    if (pos<0) {
        pos=items.count();
    }
    beginInsertRows(QModelIndex(),pos,pos);
    PTodoItem item = std::make_shared<TodoItem>();
    item->filename = filename;
    item->line = line;
    item->ch = ch;
    item->lineText = lineText;
    items.insert(pos,item);
    endInsertRows();
}

void TodoModel::removeTodosForFile(const QString &filename)
{
    QList<PTodoItem> &items=getItems(mIsForProject);
    for(int i=items.count()-1;i>=0;i--) {
        PTodoItem item = items[i];
        if (item->filename==filename) {
            beginRemoveRows(QModelIndex(),i,i);
            items.removeAt(i);
            endRemoveRows();
        }
    }
}

void TodoModel::clear()
{
    beginResetModel();
    QList<PTodoItem> &items=getItems(mIsForProject);
    items.clear();
    endResetModel();
}

void TodoModel::clear(bool forProject)
{
    if (mIsForProject == forProject)
        beginResetModel();
    QList<PTodoItem> &items=getItems(forProject);
    items.clear();
    if (mIsForProject == forProject)
        endResetModel();
}

PTodoItem TodoModel::getItem(const QModelIndex &index)
{
    if (!index.isValid())
        return PTodoItem();
    return getItems(mIsForProject)[index.row()];
}

QList<PTodoItem> &TodoModel::getItems(bool forProject)
{
    return forProject?mProjectItems:mItems;
}

const QList<PTodoItem> &TodoModel::getConstItems(bool forProject) const
{
    return forProject?mProjectItems:mItems;
}

bool TodoModel::isForProject() const
{
    return mIsForProject;
}

void TodoModel::setIsForProject(bool newIsForProject)
{
    if (mIsForProject!=newIsForProject) {
        beginResetModel();
        mIsForProject = newIsForProject;
        endResetModel();
    }
}

int TodoModel::rowCount(const QModelIndex &) const
{
    const QList<PTodoItem> &items=getConstItems(mIsForProject);
    return items.count();
}

QVariant TodoModel::data(const QModelIndex &index, int role) const
{
    const QList<PTodoItem> &items=getConstItems(mIsForProject);
    if (!index.isValid())
        return QVariant();
    if (role==Qt::DisplayRole) {
        PTodoItem item = items[index.row()];
        switch(index.column()) {
        case 0:
            return item->filename;
        case 1:
            return item->line+1;
        case 2:
            return item->lineText;
        }
    }
    return QVariant();
}

QVariant TodoModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        switch(section) {
        case 0:
            return tr("Filename");
        case 1:
            return tr("Line");
        case 2:
            return tr("Content");
        }
    }
    return QVariant();
}

int TodoModel::columnCount(const QModelIndex &) const
{
    return 3;
}
