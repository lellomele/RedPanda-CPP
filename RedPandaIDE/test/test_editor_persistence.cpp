#include "test_editor_persistence.h"
#include "atomic_replace_lock.h"
#include "src/editor.h"
#include "src/settings/editorsettings.h"
#include <qt_utils/utils.h>
#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>

namespace {
QByteArray backupContents(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
}

void TestEditorPersistence::enabling_backup_creates_one_snapshot_and_disable_cleans_it() {
    init_editor();
    QTemporaryDir dir; QVERIFY(dir.isValid());
    auto stop = finally([this] { mEditor->setAutoBackupEnabled(false); });
    mEditorSettings->setEnableEditTempBackup(true);
    mEditor->setFilename(dir.filePath("source.cpp"));
    mEditor->setContent(QStringLiteral("int confirmed = 1;"));
    mEditor->setAutoBackupEnabled(true);
    QVERIFY(mEditor->mBackupFile);
    const QString path = mEditor->mBackupFile->fileName();
    QCOMPARE(backupContents(path), mEditor->text().toUtf8());
    QVERIFY(mEditor->mAutoBackupTimer.isActive());
    mEditor->initAutoBackup();
    QCOMPARE(mEditor->mBackupFile->fileName(), path);
    QCOMPARE(QDir(dir.path()).entryList({"*.editbackup"}, QDir::Files).size(), 1);
    mEditor->setAutoBackupEnabled(false);
    QVERIFY(!mEditor->mBackupFile);
    QVERIFY(!QFile::exists(path));
}

void TestEditorPersistence::failed_backup_commit_keeps_confirmed_snapshot_and_can_retry() {
#ifdef Q_OS_WIN
    init_editor();
    QTemporaryDir dir; QVERIFY(dir.isValid());
    auto stop = finally([this] { mEditor->setAutoBackupEnabled(false); });
    mEditorSettings->setEnableEditTempBackup(true);
    mEditor->setFilename(dir.filePath("source.cpp"));
    mEditor->setContent(QStringLiteral("int confirmed = 1;"));
    mEditor->setAutoBackupEnabled(true);
    QVERIFY(mEditor->mBackupFile);
    const QString path = mEditor->mBackupFile->fileName();
    const QByteArray before = backupContents(path);
    const QDateTime confirmedTime = mEditor->mBackupTime;
    AtomicReplaceLock lock(path); QVERIFY(lock.locked());
    mEditor->setContent(QStringLiteral("int candidate = 2;"));
    mEditor->saveAutoBackup();
    QCOMPARE(backupContents(path), before);
    QCOMPARE(mEditor->mBackupTime, confirmedTime);
    QVERIFY(!mEditor->mAutoBackupTimer.isActive());
    lock.release();
    mEditor->setAutoBackupEnabled(true); // explicit retry of stopped writer
    QCOMPARE(mEditor->mBackupFile->fileName(), path);
    QCOMPARE(backupContents(path), mEditor->text().toUtf8());
    QVERIFY(mEditor->mAutoBackupTimer.isActive());
#else
    QSKIP("Commit-failure test requires Windows file-sharing semantics");
#endif
}

void TestEditorPersistence::failed_backup_move_keeps_previous_snapshot_until_new_commit() {
    init_editor();
    QTemporaryDir dir; QVERIFY(dir.isValid());
    auto stop = finally([this] { mEditor->setAutoBackupEnabled(false); });
    mEditorSettings->setEnableEditTempBackup(true);
    mEditor->setFilename(dir.filePath("source.cpp"));
    mEditor->setContent(QStringLiteral("int confirmed = 1;"));
    mEditor->setAutoBackupEnabled(true);
    QVERIFY(mEditor->mBackupFile);
    const QString oldPath = mEditor->mBackupFile->fileName();
    const QByteArray before = backupContents(oldPath);
    mEditor->setFilename(dir.filePath("missing/new.cpp"));
    mEditor->initAutoBackup();
    QCOMPARE(mEditor->mBackupFile->fileName(), oldPath);
    QCOMPARE(backupContents(oldPath), before);
    QVERIFY(!mEditor->mAutoBackupTimer.isActive());
    QVERIFY(QDir().mkpath(dir.filePath("missing")));
    mEditor->setAutoBackupEnabled(true);
    QVERIFY(mEditor->mBackupFile->fileName() != oldPath);
    QCOMPARE(backupContents(mEditor->mBackupFile->fileName()), mEditor->text().toUtf8());
    QVERIFY(!QFile::exists(oldPath));
}
