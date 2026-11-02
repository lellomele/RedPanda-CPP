// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>
#include <QTemporaryDir>
#include <QDir>
#define SI_NO_CONVERSION
#include "src/SimpleIni.h"
#include "src/projectfileoperations.h"

namespace {
QByteArray read(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}
bool write(const QString &path, const QByteArray &data) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}
}

class TestProjectFileOperations : public QObject {
    Q_OBJECT
private slots:
    void missing_source_does_not_touch_replacement() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const auto target = dir.filePath("target.cpp");
        QVERIFY(write(target, "original destination"));
        ProjectFileOperations::RenameTransaction plan(dir.filePath("missing.cpp"), target, true);
        QVERIFY(!plan.commit().committed());
        QCOMPARE(read(target), QByteArray("original destination"));
        QVERIFY(plan.retainedBackup().isEmpty());
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files).size(), 1);
    }
    void missing_copy_source_preserves_existing_destination() {
        QTemporaryDir dir;
        const auto target = dir.filePath("target.cpp");
        QVERIFY(write(target, "original destination"));
        QVERIFY(!ProjectFileOperations::copyFile(dir.filePath("absent.cpp"), target).succeeded());
        QCOMPARE(read(target), QByteArray("original destination"));
    }
    void atomic_copy_handles_multiple_blocks_and_empty_file() {
        QTemporaryDir dir;
        const auto source = dir.filePath("source.cpp"), target = dir.filePath("target.cpp");
        QByteArray sourceBytes(200 * 1024 + 7, '\0');
        for (int i = 0; i < sourceBytes.size(); ++i) sourceBytes[i] = static_cast<char>(i % 251);
        QVERIFY(write(source, sourceBytes)); QVERIFY(write(target, "old"));
        QVERIFY(ProjectFileOperations::copyFile(source, target).succeeded());
        QCOMPARE(read(target), sourceBytes); QCOMPARE(read(source), sourceBytes);
        QVERIFY(write(source, {}));
        QVERIFY(ProjectFileOperations::copyFile(source, target).succeeded());
        QCOMPARE(read(target), QByteArray());
    }
    void destination_conflict_preserves_both_resources() {
        QTemporaryDir dir;
        const auto source = dir.filePath("source.cpp"), target = dir.filePath("target.cpp");
        QVERIFY(write(source, "source content")); QVERIFY(write(target, "target content"));
        ProjectFileOperations::RenameTransaction plan(source, target, false);
        QCOMPARE(plan.commit().status, OperationStatus::Conflict);
        QCOMPARE(read(source), QByteArray("source content"));
        QCOMPARE(read(target), QByteArray("target content"));
    }
    void missing_destination_directory_preserves_source() {
        QTemporaryDir dir;
        const auto source = dir.filePath("source.cpp");
        QVERIFY(write(source, "source content"));
        ProjectFileOperations::RenameTransaction plan(source, dir.filePath("absent/target.cpp"), false);
        QVERIFY(!plan.commit().committed());
        QCOMPARE(read(source), QByteArray("source content"));
    }
    void replacement_preserves_old_destination_until_model_commit() {
        QTemporaryDir dir;
        const auto source = dir.filePath("source.cpp"), target = dir.filePath("target.cpp");
        QVERIFY(write(source, "source content")); QVERIFY(write(target, "target content"));
        ProjectFileOperations::RenameTransaction plan(source, target, true);
        QVERIFY(plan.commit().succeeded());
        QVERIFY(!QFile::exists(source));
        QCOMPARE(read(target), QByteArray("source content"));
        const auto previousDestination = plan.retainedBackup();
        QVERIFY(!previousDestination.isEmpty());
        QCOMPARE(read(previousDestination), QByteArray("target content"));
        QVERIFY(plan.finish().succeeded());
        QVERIFY(!QFile::exists(previousDestination));
        QCOMPARE(read(target), QByteArray("source content"));
    }
    void unfinished_followup_retains_recoverable_destination() {
        QTemporaryDir dir;
        const auto source = dir.filePath("source.cpp"), target = dir.filePath("target.cpp");
        QVERIFY(write(source, "source content")); QVERIFY(write(target, "target content"));
        QString backup;
        {
            ProjectFileOperations::RenameTransaction plan(source, target, true);
            QVERIFY(plan.commit().succeeded());
            backup = plan.retainedBackup();
        }
        QCOMPARE(read(target), QByteArray("source content"));
        QCOMPARE(read(backup), QByteArray("target content"));
    }
    void rollback_restores_destination_when_source_move_fails() {
        QTemporaryDir dir;
        const auto source = dir.filePath("source.cpp"), target = dir.filePath("target.cpp");
        QVERIFY(write(source, "source content")); QVERIFY(write(target, "target content"));
        int calls = 0;
        ProjectFileOperations::RenameTransaction plan(source, target, true,
            [&calls](const QString &from, const QString &to) {
                if (++calls == 2) return false;
                return QFile::rename(from, to);
            });
        QVERIFY(!plan.commit().committed());
        QCOMPARE(calls, 3);
        QCOMPARE(read(source), QByteArray("source content"));
        QCOMPARE(read(target), QByteArray("target content"));
        QVERIFY(plan.retainedBackup().isEmpty());
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files).size(), 2);
    }
    void rollback_failure_retains_backup_and_reports_its_path() {
        QTemporaryDir dir;
        const auto source = dir.filePath("source.cpp"), target = dir.filePath("target.cpp");
        QVERIFY(write(source, "source content")); QVERIFY(write(target, "target content"));
        int calls = 0;
        QString backup;
        {
            ProjectFileOperations::RenameTransaction plan(source, target, true,
                [&calls](const QString &from, const QString &to) {
                    if (++calls >= 2) return false;
                    return QFile::rename(from, to);
                });
            const auto result = plan.commit();
            QVERIFY(!result.committed());
            backup = plan.retainedBackup();
            QVERIFY(!backup.isEmpty()); QVERIFY(result.error.contains(backup));
            QCOMPARE(read(source), QByteArray("source content"));
            QVERIFY(!QFile::exists(target));
        }
        QCOMPARE(read(backup), QByteArray("target content"));
    }
    void directory_destination_is_not_replaced() {
        QTemporaryDir dir;
        const auto source = dir.filePath("source.cpp"), target = dir.filePath("directory");
        QVERIFY(write(source, "source content")); QVERIFY(QDir().mkpath(target));
        ProjectFileOperations::RenameTransaction plan(source, target, true);
        QVERIFY(!plan.commit().committed());
        QCOMPARE(read(source), QByteArray("source content"));
        QVERIFY(QFileInfo(target).isDir());
    }
    void case_only_rename_keeps_contents() {
        QTemporaryDir dir;
        const auto source = dir.filePath("MixedCase.cpp"), target = dir.filePath("mixedcase.cpp");
        QVERIFY(write(source, "source content"));
        ProjectFileOperations::RenameTransaction plan(source, target, false);
        QVERIFY(plan.commit().succeeded());
        QVERIFY(plan.finish().succeeded());
        QCOMPARE(read(target), QByteArray("source content"));
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files), QStringList{"mixedcase.cpp"});
    }
    void unicode_metadata_roundtrip_preserves_other_sections() {
        QTemporaryDir dir;
        const auto path = dir.filePath(QString::fromUtf8("progetto è.dev"));
        CSimpleIniA ini;
        QVERIFY(ini.SetValue("Project", "Name", "example") >= 0);
        QVERIFY(ini.SetValue("Custom", "Keep", "unchanged") >= 0);
        QVERIFY(ProjectFileOperations::saveIni(path, ini).succeeded());
        CSimpleIniA candidate;
        QVERIFY(ProjectFileOperations::loadIni(path, candidate).succeeded());
        QVERIFY(candidate.SetValue("Project", "Name", "updated") >= 0);
        QVERIFY(ProjectFileOperations::saveIni(path, candidate).succeeded());
        CSimpleIniA actual;
        QVERIFY(ProjectFileOperations::loadIni(path, actual).succeeded());
        QCOMPARE(QByteArray(actual.GetValue("Project", "Name")), QByteArray("updated"));
        QCOMPARE(QByteArray(actual.GetValue("Custom", "Keep")), QByteArray("unchanged"));
    }
    void oversized_candidate_does_not_replace_valid_metadata() {
        QTemporaryDir dir;
        const auto path = dir.filePath("project.dev");
        const QByteArray before("[Project]\nName=original\n");
        QVERIFY(write(path, before));
        const QByteArray tooLarge(ProjectFileOperations::MaxMetadataBytes + 1, 'x');
        QVERIFY(!ProjectFileOperations::commitMetadata(path, tooLarge).succeeded());
        QCOMPARE(read(path), before);
    }
    void failed_metadata_commit_is_reported() {
        QTemporaryDir dir;
        const auto sentinel = dir.filePath("sentinel.dev");
        QVERIFY(write(sentinel, "unchanged"));
        const auto result = ProjectFileOperations::commitMetadata(dir.path(), "[Project]\nName=new\n");
        QVERIFY(!result.succeeded()); QVERIFY(!result.error.isEmpty());
        QCOMPARE(read(sentinel), QByteArray("unchanged"));
        QVERIFY(QFileInfo(dir.path()).isDir());
    }
};

QTEST_GUILESS_MAIN(TestProjectFileOperations)
#include "test_project_file_operations.moc"
