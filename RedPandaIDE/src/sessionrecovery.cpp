#include "sessionrecovery.h"
#include "sessionstore.h"
#include "mainwindow.h"
#include "editormanager.h"
#include "editor.h"
#include "settings.h"
#include <QtConcurrent/QtConcurrentRun>
#include <QDir>
#include <QUuid>
#include <QMessageBox>
#include <QStatusBar>

SessionRecovery::SessionRecovery(MainWindow *window) : QObject(window), mWindow(window) {
    mDirectory = QDir(pSettings->dirs().config()).filePath("recovery");
    if (!QDir().mkpath(mDirectory)) { qWarning() << "Recovery directory unavailable:" << mDirectory; return; }
    mPath = QDir(mDirectory).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + ".json");
    mLock = std::make_unique<QLockFile>(mPath + ".lock");
    if (!mLock->tryLock(0)) { qWarning() << "Recovery lock unavailable"; mPath.clear(); return; }
    connect(&mWriter, &QFutureWatcher<QString>::finished, this, [this] {
        const QString error = mWriter.result();
        if (error.isEmpty()) {
            mLastError.clear(); mLastState = mScheduledState;
            for (const auto &path : mConsumed) QFile::remove(path);
            mConsumed.clear();
        } else if (error != mLastError) {
            mLastError = error; qWarning() << "Recovery snapshot failed:" << error;
            mWindow->statusBar()->showMessage(tr("Recovery backup could not be saved: %1").arg(error), 15000);
        }
    });
    mElapsed.start(); mTimer.setInterval(1000);
    connect(&mTimer, &QTimer::timeout, this, [this] {
        if (pSettings->environment().recoveryEnabled() && mElapsed.elapsed() >= pSettings->environment().recoveryInterval() * 1000)
            checkpoint();
    });
    mTimer.start();
}
SessionRecovery::~SessionRecovery() { mStopping = true; mTimer.stop(); mWriter.waitForFinished(); }
void SessionRecovery::checkpoint() {
    if (mStopping || mPath.isEmpty() || mWriter.isRunning() || !pSettings->environment().recoveryEnabled()) return;
    mElapsed.restart(); QList<RecoveryDocument> documents; QString state = QString::number(mRevision);
    auto *manager = mWindow->editorManager();
    for (int i = 0; i < manager->pageCount(); ++i) {
        auto *editor = (*manager)[i];
        if (!editor) continue;
        if (!mObserved.contains(editor)) {
            mObserved.insert(editor);
            connect(editor, &Editor::changed, this, [this, editor] { mTextCache.remove(editor); ++mRevision; });
            connect(editor, &QObject::destroyed, this, [this, editor] { mTextCache.remove(editor); mObserved.remove(editor); ++mRevision; });
        }
        const auto caret = editor->caretXY();
        state += QString("|%1|%2|%3|%4|%5").arg(reinterpret_cast<quintptr>(editor)).arg(editor->filename()).arg(editor->modified()).arg(caret.line).arg(caret.ch);
        if (!editor->modified()) { mTextCache.remove(editor); continue; }
        if (!mTextCache.contains(editor)) mTextCache.insert(editor, editor->text());
        documents.append({editor->filename(), mTextCache.value(editor), caret.line, caret.ch});
    }
    if (state == mLastState && mConsumed.isEmpty()) return;
    mScheduledState = state;
    const QString path = mPath;
    mWriter.setFuture(QtConcurrent::run([path, documents] { QString error; SessionStore::write(path, documents, error); return error; }));
}
void SessionRecovery::restorePreviousSessions() {
    QDir dir(mDirectory);
    for (const QString &name : dir.entryList({"*.json"}, QDir::Files, QDir::Time)) {
        const QString path = dir.filePath(name); if (path == mPath) continue;
        QLockFile lock(path + ".lock"); if (!lock.tryLock(0)) continue;
        QList<RecoveryDocument> documents; QString error;
        if (!SessionStore::read(path, documents, error)) { qWarning() << "Invalid recovery snapshot retained:" << path << error; continue; }
        if (documents.isEmpty()) { QFile::remove(path); continue; }
        const auto choice = QMessageBox::question(mWindow, tr("Recover unsaved documents"),
            tr("%1 unsaved document(s) were found from a previous session. Recover them as new documents? Original files will not be overwritten.").arg(documents.size()),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes);
        if (choice == QMessageBox::Cancel) continue;
        if (choice == QMessageBox::No) { QFile::remove(path); continue; }
        bool complete = true;
        for (const auto &document : documents) {
            try {
                const QString name = QString("recovered-%1-%2").arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8), QFileInfo(document.filename).fileName());
                auto *editor = mWindow->editorManager()->newEditor(name, ENCODING_UTF8, FileType::None, document.filename, false, true);
                if (!editor) { complete = false; break; }
                editor->setContent(document.text); editor->setModified(true);
                editor->setCaretXY({document.column, document.line});
            } catch (const BaseError &error) { complete = false; QMessageBox::warning(mWindow, tr("Recovery error"), error.reason()); break; }
        }
        if (complete) mConsumed.append(path);
    }
    // Old snapshots are deleted only after a new snapshot has been committed successfully.
    if (!mConsumed.isEmpty()) checkpoint();
}
void SessionRecovery::finishCleanly() {
    mStopping = true; mTimer.stop(); mWriter.waitForFinished();
    if (!mPath.isEmpty()) QFile::remove(mPath);
}
