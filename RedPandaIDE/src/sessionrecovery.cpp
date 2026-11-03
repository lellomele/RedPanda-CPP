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
#include <qsynedit/document.h>
#include <exception>

SessionRecovery::SessionRecovery(MainWindow *window) : QObject(window), mWindow(window) {
    mDirectory = QDir(pSettings->dirs().config()).filePath("recovery");
    if (!QDir().mkpath(mDirectory)) { qWarning() << "Recovery directory unavailable:" << mDirectory; return; }
    mPath = QDir(mDirectory).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + ".json");
    mLock = std::make_unique<QLockFile>(mPath + ".lock");
    if (!mLock->tryLock(0)) { qWarning() << "Recovery lock unavailable"; mPath.clear(); return; }
    connect(&mWriter, &QFutureWatcher<QString>::finished, this, [this] {
        const QString error = mWriter.result();
        mWritePending = false;
        if (error.isEmpty()) {
            mLastError.clear(); mLastState = mScheduledState;
            for (const auto &path : mConsumed) QFile::remove(path);
            mConsumed.clear();
        } else reportFailure(error);
        if (mStopping) emit shutdownComplete();
    });
    mElapsed.start(); mTimer.setInterval(1000);
    connect(&mTimer, &QTimer::timeout, this, [this] {
        if (pSettings->environment().recoveryEnabled() && mElapsed.elapsed() >= pSettings->environment().recoveryInterval() * 1000)
            checkpoint();
    });
    mTimer.start();
}
SessionRecovery::~SessionRecovery() { stopScheduling(); mWriter.waitForFinished(); }
bool SessionRecovery::isWriting() const { return mWritePending || mWriter.isRunning(); }
void SessionRecovery::stopScheduling() { mStopping = true; mTimer.stop(); }
void SessionRecovery::requestShutdown()
{
    stopScheduling();
    if (!isWriting()) emit shutdownComplete();
}
void SessionRecovery::resumeScheduling()
{
    mStopping = false;
    if (!mPath.isEmpty()) mTimer.start();
}

void SessionRecovery::reportFailure(const QString &error) {
    if (error == mLastError) return;
    mLastError = error;
    qWarning() << "Recovery snapshot failed:" << error;
    mWindow->statusBar()->showMessage(tr("Recovery backup could not be saved: %1").arg(error), 15000);
}
void SessionRecovery::checkpoint() {
    if (mStopping || mPath.isEmpty() || isWriting() || !pSettings->environment().recoveryEnabled()) return;
    mElapsed.restart(); QString state = QString::number(mRevision);
    QList<Editor *> candidates;
    auto *manager = mWindow->editorManager();
    for (int i = 0; i < manager->pageCount(); ++i) {
        auto *editor = (*manager)[i];
        if (!editor) continue;
        if (!mObserved.contains(editor)) {
            mObserved.insert(editor);
            // Observe the document commit, not only editor undo commands.
            // setContent/loadFromFile also replace text without Editor::changed.
            connect(editor->document().get(), &QSynedit::Document::contentCommitted,
                    this, [this, editor] { mTextCache.remove(editor); ++mRevision; });
            connect(editor, &QObject::destroyed, this, [this, editor] { mTextCache.remove(editor); mObserved.remove(editor); ++mRevision; });
        }
        // A reentrant checkpoint must never publish an intermediate batch or
        // reuse the previous cache while a document mutation is in progress.
        if (editor->document()->updateInProgress())
            return;
        const auto caret = editor->caretXY();
        state += QString("|%1|%2|%3|%4|%5").arg(reinterpret_cast<quintptr>(editor)).arg(editor->filename()).arg(editor->modified()).arg(caret.line).arg(caret.ch);
        if (!editor->modified()) { mTextCache.remove(editor); continue; }
        candidates.append(editor);
    }
    if (state == mLastState && mConsumed.isEmpty()) return;
    // Preflight the complete candidate before joining or caching any new text.
    // These reads occur on the GUI thread; workers receive values only.
    SessionStore::SnapshotBudget budget;
    QString error;
    for (Editor *editor : candidates) {
        if (!budget.beginDocument(editor->filename(), error)) { reportFailure(error); return; }
        const auto cached = mTextCache.constFind(editor);
        if (cached != mTextCache.cend()) {
            if (!budget.appendText(QStringView{cached.value()}, error)) { reportFailure(error); return; }
        } else {
            const auto document = editor->document();
            const int count = document->count();
            const QString lineBreak = document->lineBreak();
            for (int line = 0; line < count; ++line) {
                const QString text = document->getLine(line);
                if (!budget.appendText(QStringView{text}, error)
                        || (line + 1 < count && !budget.appendText(QStringView{lineBreak}, error))) {
                    reportFailure(error);
                    return;
                }
            }
        }
    }
    QList<RecoveryDocument> documents;
    for (Editor *editor : candidates) {
        if (!mTextCache.contains(editor)) mTextCache.insert(editor, editor->text());
        const auto caret = editor->caretXY();
        documents.append({editor->filename(), mTextCache.value(editor), caret.line, caret.ch});
    }
    mScheduledState = state;
    const QString path = mPath;
    const auto future = QtConcurrent::run([path, documents] {
        // Serialization and file replacement only touch local candidate data.
        // A failed candidate must not replace the last committed snapshot.
        try { QString error; SessionStore::write(path, documents, error); return error; }
        catch (const std::exception &error) { return QString::fromLocal8Bit(error.what()); }
        catch (...) { return QStringLiteral("Recovery snapshot could not be prepared."); }
    });
    mWritePending = true;
    mWriter.setFuture(future);
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
    stopScheduling(); mWriter.waitForFinished();
    if (!mPath.isEmpty()) QFile::remove(mPath);
}
