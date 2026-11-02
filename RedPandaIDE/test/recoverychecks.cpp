// SPDX-License-Identifier: GPL-3.0-or-later
#include "recoverychecks.h"
#include "atomic_replace_lock.h"
#include "src/mainwindow.h"
#include "src/editormanager.h"
#include "src/editor.h"
#include "src/sessionrecovery.h"
#include "src/sessionstore.h"
#include "src/settings.h"
#include <qsynedit/document.h>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTemporaryDir>
#include <QThread>

// This adapter is called only by the isolated GUI QA runner. Friendship avoids
// exposing mutable recovery state or a production-only-for-tests public API.
class RecoveryChecks {
public:
    static int run(MainWindow &window, SessionRecovery &recovery, const QString &outputDirectory)
    {
        QJsonArray checks;
        int failures = 0;
        const auto check = [&](const char *name, bool passed) {
            checks.append(QJsonObject{{"name", QString::fromLatin1(name)}, {"passed", passed}});
            if (!passed) ++failures;
        };
        const auto read = [](const QString &path) {
            QFile file(path);
            return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
        };
        const auto write = [](const QString &path, const QByteArray &bytes) {
            QFile file(path);
            return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
        };
        const auto waitIdle = [&] {
            // Event pumping belongs to this bounded test harness only. Wait for
            // the GUI result receipt too, not merely QFuture completion.
            QElapsedTimer deadline;
            deadline.start();
            while (recovery.isWriting() && deadline.elapsed() < 5000) {
                QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 10);
                QThread::msleep(1);
            }
            return !recovery.isWriting();
        };
        const auto snapshotContains = [&](const QString &filename, const QString &expected) {
            QList<RecoveryDocument> documents;
            QString error;
            if (!SessionStore::read(recovery.mPath, documents, error)) return false;
            for (const auto &document : documents)
                if (document.filename == filename)
                    return document.text == expected;
            return false;
        };

        const bool timerWasActive = recovery.mTimer.isActive();
        recovery.mTimer.stop();
        const bool enabled = pSettings->environment().recoveryEnabled();
        pSettings->environment().setRecoveryEnabled(true);
        QPointer<Editor> editor;
        QTemporaryDir fixture(QDir(outputDirectory).filePath("recovery-cache-XXXXXX"));
        const auto restore = finally([&] {
            waitIdle();
            if (editor) window.editorManager()->forceCloseEditor(editor);
            pSettings->environment().setRecoveryEnabled(enabled);
            if (timerWasActive && !recovery.mStopping) recovery.mTimer.start();
        });
        if (!waitIdle() || recovery.mStopping || recovery.mPath.isEmpty()) return 2;
        if (!fixture.isValid()) return 3;
        const QString source = fixture.filePath("recovery-source.cpp");
        if (!write(source, "struct InitialDisk {};\n")) return 4;
        editor = window.editorManager()->newEditor(source, ENCODING_UTF8, FileType::CppSource, {}, false, false);
        if (!editor) return 5;

        editor->setContent(QStringLiteral("struct FirstUnsaved {};\n"));
        editor->setModified(true);
        recovery.checkpoint();
        const bool firstScheduled = recovery.isWriting();
        const bool firstFinished = waitIdle();
        check("Initial real recovery checkpoint is scheduled and committed",
              firstScheduled && firstFinished && recovery.mLastError.isEmpty()
              && snapshotContains(source, editor->text()) && recovery.mTextCache.contains(editor));

        editor->setContent(QStringLiteral("struct ReplacedQString { int newer; };\n"));
        editor->setModified(true);
        recovery.checkpoint();
        const bool stringScheduled = recovery.isWriting();
        const bool stringFinished = waitIdle();
        check("setContent QString invalidates a cached dirty document in the real snapshot",
              stringScheduled && stringFinished && recovery.mLastError.isEmpty()
              && snapshotContains(source, editor->text()));

        editor->setContent(QStringList{QStringLiteral("struct ReplacedLines {"),
                                       QStringLiteral("    long newest;"), QStringLiteral("};")});
        editor->setModified(true);
        recovery.checkpoint();
        const bool linesScheduled = recovery.isWriting();
        const bool linesFinished = waitIdle();
        check("setContent QStringList invalidates the same cached dirty document",
              linesScheduled && linesFinished && recovery.mLastError.isEmpty()
              && snapshotContains(source, editor->text()));

        // The fixture owns this live editor on the GUI thread. Use its mutable
        // Document only here to inject an unfinished batch through the real API.
        const auto document = std::const_pointer_cast<QSynedit::Document>(editor->document());
        const QByteArray beforeBatch = read(recovery.mPath);
        const QString scheduledBeforeBatch = recovery.mScheduledState;
        {
            document->beginUpdate();
            const auto finish = finally([&] { document->endUpdate(); });
            document->putLine(0, QStringLiteral("struct InsideUncommittedBatch {"));
            const auto caret = editor->caretXY();
            editor->setCaretXY({caret.ch == 0 ? 1 : 0, caret.line});
            recovery.checkpoint();
            check("Inflight document batch cannot schedule or replace a confirmed snapshot",
                  document->updateInProgress() && !recovery.isWriting()
                  && recovery.mScheduledState == scheduledBeforeBatch && read(recovery.mPath) == beforeBatch);
        }
        editor->setModified(true);
        recovery.checkpoint();
        const bool batchFinished = waitIdle();
        check("Outermost document commit makes the complete batch recoverable",
              batchFinished && recovery.mLastError.isEmpty() && snapshotContains(source, editor->text()));

        // Avoid an unrelated external-file question while the fixture replaces
        // its own source; the reload under test still uses the real Editor API.
        window.fileSystemWatcher()->removePath(source);
        if (!write(source, "struct LoadedFromDisk { int diskRevision; };\n")) return 6;
        editor->loadFile(source, false);
        editor->setModified(true);
        recovery.checkpoint();
        const bool reloadScheduled = recovery.isWriting();
        const bool reloadFinished = waitIdle();
        check("loadFromFile followed by dirty state replaces cached text in the real snapshot",
              reloadScheduled && reloadFinished && recovery.mLastError.isEmpty()
              && editor->text().contains(QStringLiteral("LoadedFromDisk"))
              && snapshotContains(source, editor->text()));

#ifdef Q_OS_WIN
        const QByteArray confirmedBytes = read(recovery.mPath);
        const QString confirmedState = recovery.mLastState;
        AtomicReplaceLock lock(recovery.mPath);
        if (!lock.locked()) return 7;
        editor->setContent(QStringLiteral("struct RetryAfterCommitFailure { int retained; };\n"));
        editor->setModified(true);
        recovery.checkpoint();
        const bool failedFinished = waitIdle();
        check("A failed actual recovery replacement retains the last confirmed bytes and state",
              failedFinished && !recovery.mLastError.isEmpty() && !confirmedBytes.isEmpty()
              && read(recovery.mPath) == confirmedBytes && recovery.mLastState == confirmedState);
        lock.release();
        recovery.checkpoint();
        const bool retryScheduled = recovery.isWriting();
        const bool retryFinished = waitIdle();
        check("Retry after the replacement lock is released commits the latest full document",
              retryScheduled && retryFinished && recovery.mLastError.isEmpty()
              && snapshotContains(source, editor->text()));
#endif

        QFile output(QDir(outputDirectory).filePath("recovery-cache-result.json"));
        const QByteArray bytes = QJsonDocument(QJsonObject{{"failures", failures}, {"checks", checks}}).toJson();
        if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size()) return 8;
        return failures == 0 ? 0 : 1;
    }
};

int runRecoveryChecks(MainWindow &window, SessionRecovery &recovery, const QString &outputDirectory)
{
    return RecoveryChecks::run(window, recovery, outputDirectory);
}
