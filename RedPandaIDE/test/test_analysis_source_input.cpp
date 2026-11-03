// SPDX-License-Identifier: GPL-3.0-or-later
#include "src/analysis/sourceinput.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>

using namespace AnalysisInput;
namespace {
bool write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(bytes) == bytes.size() && file.flush();
}
ReadPolicy policyFor(const QString &directory) { ReadPolicy policy; policy.workingDirectory = directory; return policy; }
struct RestoreDirectory {
    QString path = QDir::currentPath();
    ~RestoreDirectory() { QDir::setCurrent(path); }
};
}
class AnalysisSourceInputChecks : public QObject {
    Q_OBJECT
private slots:
    void relativeOpenAliasCannotSupplyAnotherDirectoriesSource() {
        QTemporaryDir first, second; QVERIFY(first.isValid()); QVERIFY(second.isValid());
        RestoreDirectory restore; QVERIFY(QDir::setCurrent(first.path()));
        DocumentStore documents;
        documents.add("relative.h", [](QStringList &out) { out = {"UNSAVED A"}; return true; });
        const auto accepted = documents.capture(); QVERIFY(accepted.succeeded());
        QCOMPARE(accepted.snapshot->document("relative.h")->absoluteFilename, first.filePath("relative.h"));
        QVERIFY(write(second.filePath("relative.h"), "disk B\n"));
        SourceReadSession other(accepted.snapshot, policyFor(second.path()));
        const auto result = other.read("relative.h"); QVERIFY(result.ready());
        QCOMPARE(result.source->version.origin, SourceOrigin::Disk);
        QCOMPARE(result.source->version.path, second.filePath("relative.h"));
        QCOMPARE(result.source->lines, QStringList{"disk B"});
        QVERIFY(QFile::remove(second.filePath("relative.h")));
        SourceReadSession missing(accepted.snapshot, policyFor(second.path()));
        QCOMPARE(missing.probe("relative.h").presence, PathPresence::Missing);
        const auto registered = missing.readDocumentRoot("relative.h"); QVERIFY(registered.ready());
        QCOMPARE(registered.source->version.path, first.filePath("relative.h"));
        QCOMPARE(registered.source->lines, QStringList{"UNSAVED A"});
    }
    void includeSearchRecordsMissingPrecedenceAndUsesCapturedDirectory() {
        QTemporaryDir fixture, unrelated; QVERIFY(fixture.isValid()); QVERIFY(unrelated.isValid());
        QVERIFY(QDir(fixture.path()).mkdir("preferred")); QVERIFY(QDir(fixture.path()).mkdir("fallback"));
        const QString header = fixture.filePath("fallback/header.h");
        QVERIFY(write(header, "int selected;\n"));
        SourceReadSession session({}, policyFor(fixture.path()));
        RestoreDirectory restore; QVERIFY(QDir::setCurrent(unrelated.path()));
        QCOMPARE(session.resolveInclude("main.cpp", "<header.h>", {"fallback"}, {"preferred"}), header);
        QVERIFY(session.read(header).ready());
        QVERIFY(session.validateObservedVersions());
        QCOMPARE(session.pathProbes().value(fixture.filePath("preferred/header.h")), PathPresence::Missing);
        QVERIFY(write(fixture.filePath("preferred/header.h"), "int now_preferred;\n"));
        QString reason; QVERIFY(!session.validateObservedVersions(&reason)); QVERIFY(!reason.isEmpty());
    }
    void endValidationRejectsChangedDiskAndKeepsHistoricalOpenInput() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        const QString path = fixture.filePath("source.h"); QVERIFY(write(path, "int before;\n"));
        SourceReadSession disk({}, policyFor(fixture.path())); QVERIFY(disk.read(path).ready());
        QVERIFY(write(path, "int after_;\n")); QVERIFY(!disk.validateObservedVersions());
        DocumentStore documents; QStringList lines{"int accepted;"};
        const auto id = documents.add(path, [&](QStringList &out) { out = lines; return true; });
        const auto input = documents.capture(); QVERIFY(input.succeeded());
        SourceReadSession open(input.snapshot, policyFor(fixture.path())); QVERIFY(open.read(path).ready());
        lines = {"int newer;"}; QVERIFY(documents.changed(id)); QVERIFY(documents.remove(id));
        // Worker validation concerns its accepted historical input. Whether the
        // GUI may apply that result is a separate current-intent decision.
        QVERIFY(open.validateObservedVersions());
    }
    void includeProbeBudgetCannotSilentlySelectALaterHeader() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        auto policy = policyFor(fixture.path()); policy.maximumPathProbes = 1;
        SourceReadSession session({}, policy);
        QCOMPARE(session.probe("missing.h").presence, PathPresence::Missing);
        QCOMPARE(session.probe("other.h").presence, PathPresence::Unavailable);
        QVERIFY(!session.complete()); QVERIFY(!session.validateObservedVersions());
    }
    void verificationSharesRemainingSourceMemoryAdmission() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        QVERIFY(write(fixture.filePath("source.h"), QByteArray(4000, 'x')));
        auto policy = policyFor(fixture.path()); policy.maximumLiveSourceBytes = 46000;
        SourceReadSession session({}, policy);
        QVERIFY(session.read("source.h").ready());
        QString reason; QVERIFY(!session.validateObservedVersions(&reason));
        QVERIFY(reason.contains("budget", Qt::CaseInsensitive));
    }
    void emptyOpenBufferOverridesExistingDisk() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        const QString path = fixture.filePath("empty.h");
        QVERIFY(write(path, "int stale_disk_symbol;\n"));
        DocumentStore documents;
        const auto id = documents.add(path, [](QStringList &out) { out = {}; return true; });
        const auto captured = documents.capture(); QVERIFY(captured.succeeded());
        SourceReadSession session(captured.snapshot, policyFor(fixture.path()));
        const auto result = session.read(path);
        QVERIFY(result.ready()); QVERIFY(result.source->lines.isEmpty());
        QCOMPARE(result.source->version.origin, SourceOrigin::OpenDocument);
        QCOMPARE(result.source->version.documentId, id);
        QVERIFY(result.source->version.rawSha256.isEmpty());
        QCOMPARE(result.metrics.rawReadBytes, qint64(0));
        QVERIFY(session.complete());
    }
    void acceptedDocumentVersionSurvivesMutationAndRemoval() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        const QString path = fixture.filePath("mutable.h");
        QStringList text{"int accepted;"};
        DocumentStore documents;
        const auto id = documents.add(path, [&](QStringList &out) { out = text; return true; });
        const auto accepted = documents.capture(); QVERIFY(accepted.succeeded());
        text = {"int later;"}; QVERIFY(documents.changed(id));
        const auto later = documents.capture(); QVERIFY(later.succeeded());
        QVERIFY(documents.remove(id));
        SourceReadSession first(accepted.snapshot, policyFor(fixture.path()));
        SourceReadSession second(later.snapshot, policyFor(fixture.path()));
        const auto before = first.read(path), after = second.read(path);
        QVERIFY(before.ready()); QVERIFY(after.ready());
        QCOMPARE(before.source->lines, QStringList{"int accepted;"});
        QCOMPARE(after.source->lines, text);
        QCOMPARE(before.source->version.documentId, after.source->version.documentId);
        QVERIFY(before.source->version.revision < after.source->version.revision);
        QVERIFY(before.source->version.textSha256 != after.source->version.textSha256);
    }
    void diskReadHasOneViewAndCapturedWorkingDirectory() {
        QTemporaryDir first, other; QVERIFY(first.isValid()); QVERIFY(other.isValid());
        QVERIFY(write(first.filePath("same.h"), "alpha\n"));
        QVERIFY(write(other.filePath("same.h"), "wrong\n"));
        SourceReadSession session({}, policyFor(first.path()));
        RestoreDirectory restore; QVERIFY(QDir::setCurrent(other.path()));
        const auto initial = session.read("same.h"); QVERIFY(initial.ready());
        QCOMPARE(initial.source->lines, QStringList{"alpha"});
        QVERIFY(write(first.filePath("same.h"), "omega\n"));
        const auto repeated = session.read("same.h");
        QCOMPARE(repeated.source, initial.source);
        SourceReadSession fresh({}, policyFor(first.path()));
        const auto latest = fresh.read("same.h"); QVERIFY(latest.ready());
        QCOMPARE(latest.source->lines, QStringList{"omega"});
        QVERIFY(latest.source->version.rawSha256 != initial.source->version.rawSha256);
        QCOMPARE(session.actualReads().size(), 1);
    }
    void capturedDecodersHandleBomAndNewlineForms() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        const auto path = fixture.filePath("bom.h");
        const QByteArray raw = QByteArray::fromHex("efbbbf") + "A\r\nB\rC\n";
        QVERIFY(write(path, raw));
        SourceReadSession session({}, policyFor(fixture.path()));
        const auto result = session.read(path); QVERIFY(result.ready());
        QCOMPARE(result.source->lines, QStringList({"A", "B", "C"}));
        QCOMPARE(result.source->version.rawSha256, QCryptographicHash::hash(raw, QCryptographicHash::Sha256));
        QCOMPARE(result.metrics.decodedLines, qsizetype(3));
        QCOMPARE(result.metrics.materializedLines, qsizetype(3));
        const auto latin = fixture.filePath("latin.h"); QVERIFY(write(latin, QByteArray::fromHex("e90a")));
        SourceReadSession invalid({}, policyFor(fixture.path()));
        QCOMPARE(invalid.read(latin).status, ReadStatus::DecodeFailed);
        auto policy = policyFor(fixture.path()); policy.encodings.append("ISO-8859-1");
        SourceReadSession fallback({}, policy);
        const auto decoded = fallback.read(latin); QVERIFY(decoded.ready());
        QCOMPARE(decoded.source->lines, QStringList{QString(QChar(0xe9))});
        QCOMPARE(decoded.source->encoding, QByteArray("ISO-8859-1"));
    }
    void missingAndFailedInputsAreNotSuccessfulEmptyFiles() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        const auto path = fixture.filePath("new.h");
        SourceReadSession missing({}, policyFor(fixture.path()));
        QCOMPARE(missing.read(path).status, ReadStatus::Missing); QVERIFY(!missing.complete());
        QVERIFY(write(path, {}));
        QCOMPARE(missing.read(path).status, ReadStatus::Missing);
        SourceReadSession empty({}, policyFor(fixture.path()));
        const auto result = empty.read(path); QVERIFY(result.ready());
        QVERIFY(result.source->lines.isEmpty()); QVERIFY(!result.source->version.rawSha256.isEmpty());
        QVERIFY(empty.complete());
        SourceReadSession directory({}, policyFor(fixture.path()));
        QCOMPARE(directory.read(fixture.path()).status, ReadStatus::ReadFailed);
        QVERIFY(!directory.complete());
    }
    void unresolvedOpenAliasNeverFallsBackToDisk() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        const QString path = fixture.filePath("Virtual.h");
        DocumentStore documents;
        documents.add(path, [](QStringList &out) { out = {"int virtual_value;"}; return true; });
        const auto accepted = documents.capture(); QVERIFY(accepted.succeeded());
        SourceReadSession session(accepted.snapshot, policyFor(fixture.path()));
#ifdef Q_OS_WIN
        const auto conflict = session.read("VIRTUAL.H");
        QCOMPARE(conflict.status, ReadStatus::ConflictingPath);
        QVERIFY(!session.complete());
#endif
        const auto exact = session.read(path); QVERIFY(exact.ready());
        QCOMPARE(exact.source->version.origin, SourceOrigin::OpenDocument);
        QCOMPARE(exact.source->lines, QStringList{"int virtual_value;"});
    }
    void distinctNativeUnicodeFilesKeepBothOpenViews() {
#ifdef Q_OS_WIN
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        const QString first = fixture.filePath("k.h");
        const QString second = fixture.filePath(QString(QChar(0x212a)) + ".h");
        QVERIFY(write(first, "int disk_first;"));
        QFile distinct(second);
        if (!distinct.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            QCOMPARE(sameExistingFile(first, second), ExistingFileMatch::Same);
            return; // This filesystem's alias policy differs from the NTFS probe.
        }
        QCOMPARE(distinct.write("int disk_second;"), qint64(16)); distinct.close();
        QCOMPARE(sameExistingFile(first, second), ExistingFileMatch::Different);
        DocumentStore documents;
        const auto firstId = documents.add(first, [](QStringList &out) { out = {"int open_first;"}; return true; });
        const auto secondId = documents.add(second, [](QStringList &out) { out = {"int open_second;"}; return true; });
        const auto accepted = documents.capture();
        QVERIFY2(accepted.succeeded(), qPrintable(accepted.error));
        QCOMPARE(accepted.snapshot->documents().size(), 2);
        SourceReadSession session(accepted.snapshot, policyFor(fixture.path()));
        const auto a = session.read(first), b = session.read(second);
        QVERIFY(a.ready()); QVERIFY(b.ready());
        QCOMPARE(a.source->version.documentId, firstId);
        QCOMPARE(b.source->version.documentId, secondId);
        QCOMPARE(a.source->lines, QStringList{"int open_first;"});
        QCOMPARE(b.source->lines, QStringList{"int open_second;"});
        QCOMPARE(session.actualReads().size(), 2);
#endif
    }
    void sizeCountAndTotalAdmissionAreBounded() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        QVERIFY(write(fixture.filePath("first.h"), "first"));
        QVERIFY(write(fixture.filePath("other.h"), "other"));
        auto sizePolicy = policyFor(fixture.path()); sizePolicy.maximumRawFileBytes = 4;
        SourceReadSession size({}, sizePolicy);
        const auto tooLarge = size.read("first.h");
        QCOMPARE(tooLarge.status, ReadStatus::BudgetExceeded);
        QCOMPARE(tooLarge.metrics.rawReadBytes, qint64(0));
        auto countPolicy = policyFor(fixture.path()); countPolicy.maximumSources = 1;
        SourceReadSession count({}, countPolicy); QVERIFY(count.read("first.h").ready());
        QCOMPARE(count.read("other.h").status, ReadStatus::BudgetExceeded);
        QCOMPARE(count.sourceCount(), 1); QVERIFY(!count.complete());
        auto memoryPolicy = policyFor(fixture.path()); memoryPolicy.maximumLiveSourceBytes = 7000;
        SourceReadSession memory({}, memoryPolicy); QVERIFY(memory.read("first.h").ready());
        QCOMPARE(memory.read("other.h").status, ReadStatus::BudgetExceeded);
        QVERIFY(memory.retainedSourceBytes() <= memoryPolicy.maximumLiveSourceBytes);
    }
    void tinyFileDoesNotAllocateAFixedLargeReadChunk() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        QVERIFY(write(fixture.filePath("tiny.h"), "x"));
        auto policy = policyFor(fixture.path()); policy.maximumLiveSourceBytes = 16 * 1024;
        SourceReadSession session({}, policy);
        const auto result = session.read("tiny.h"); QVERIFY(result.ready());
        QCOMPARE(result.metrics.rawReadBytes, qint64(1));
        QVERIFY(result.metrics.maximumRawScratchBytes < 1024);
        QVERIFY(session.retainedSourceBytes() + result.metrics.maximumRawScratchBytes < policy.maximumLiveSourceBytes);
    }
    void denseNewlinesAreRejectedBeforeLineMaterialization() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        QVERIFY(write(fixture.filePath("dense.h"), QByteArray(60000, '\n')));
        auto policy = policyFor(fixture.path()); policy.maximumLiveSourceBytes = 1024 * 1024;
        SourceReadSession session({}, policy);
        const auto result = session.read("dense.h");
        QCOMPARE(result.status, ReadStatus::BudgetExceeded);
        QCOMPARE(result.metrics.rawReadBytes, qint64(60000));
        QCOMPARE(result.metrics.decodedLines, qsizetype(60000));
        QCOMPARE(result.metrics.materializedLines, qsizetype(0));
        QVERIFY(session.actualReads().isEmpty()); QVERIFY(!session.complete());
    }
    void cancellationAndExpiredReadDeadlineAreDistinct() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        QVERIFY(write(fixture.filePath("source.h"), "int value;"));
        auto cancel = std::make_shared<std::atomic_bool>(true);
        SourceReadSession cancelled({}, policyFor(fixture.path()), cancel);
        QCOMPARE(cancelled.read("source.h").status, ReadStatus::Cancelled);
        QVERIFY(!cancelled.complete());
        auto policy = policyFor(fixture.path()); policy.maximumReadMilliseconds = 0;
        SourceReadSession expired({}, policy);
        const auto result = expired.read("source.h");
        QCOMPARE(result.status, ReadStatus::TimedOut);
        QCOMPARE(result.metrics.rawReadBytes, qint64(0));
        QVERIFY(!expired.complete());
        cancel->store(false);
        SourceReadSession fenced({}, policyFor(fixture.path()), cancel);
        const auto accepted = fenced.read("source.h"); QVERIFY(accepted.ready());
        cancel->store(true);
        QCOMPARE(fenced.read("source.h").status, ReadStatus::Cancelled);
        QVERIFY(!fenced.complete());
        QCOMPARE(accepted.source->lines, QStringList{"int value;"});
    }
    void truncatedEncodedInputCannotBecomeReadyWithDroppedBytes() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        QVERIFY(write(fixture.filePath("utf8.h"), QByteArray("prefix") + QByteArray::fromHex("c3")));
        SourceReadSession utf8({}, policyFor(fixture.path()));
        QCOMPARE(utf8.read("utf8.h").status, ReadStatus::DecodeFailed);
        QVERIFY(!utf8.complete());
        QVERIFY(write(fixture.filePath("utf16.h"), QByteArray::fromHex("fffe610062")));
        auto policy = policyFor(fixture.path()); policy.encodings = {"UTF-16LE"};
        SourceReadSession utf16({}, policy);
        QCOMPARE(utf16.read("utf16.h").status, ReadStatus::DecodeFailed);
        QVERIFY(!utf16.complete());
    }
    void invalidPolicyAndPathsCannotCertifyACompleteRead() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        auto policy = policyFor("relative");
        SourceReadSession invalid({}, policy);
        QCOMPARE(invalid.read("source.h").status, ReadStatus::InvalidPolicy);
        QVERIFY(!invalid.complete());
        SourceReadSession badPath({}, policyFor(fixture.path()));
        QCOMPARE(badPath.read(QString("bad") + QChar::Null + "name").status, ReadStatus::ReadFailed);
        QVERIFY(!badPath.complete());
#ifdef Q_OS_WIN
        SourceReadSession driveRelative({}, policyFor(fixture.path()));
        QCOMPARE(driveRelative.read("C:relative.cpp").status, ReadStatus::ReadFailed);
        QVERIFY(!driveRelative.complete());
        const QString rootedPath = fixture.filePath("rooted.h"); QVERIFY(write(rootedPath, "int rooted;\n"));
        SourceReadSession currentDriveRelative({}, policyFor(fixture.path()));
        const auto rooted = currentDriveRelative.read(rootedPath.mid(2));
        QVERIFY(rooted.ready()); QCOMPARE(rooted.source->version.path, rootedPath);
        QCOMPARE(currentDriveRelative.probe(rootedPath.mid(2)).presence, PathPresence::Present);
        SourceReadSession badDirectory({}, policyFor("C:directory"));
        QCOMPARE(badDirectory.read("source.h").status, ReadStatus::InvalidPolicy);
        QVERIFY(!badDirectory.complete());
        SourceReadSession unqualifiedDirectory({}, policyFor("/directory"));
        QCOMPARE(unqualifiedDirectory.read("source.h").status, ReadStatus::InvalidPolicy);
#endif
    }
    void digestDistinguishesLineBoundariesAndLoneSurrogates() {
        QVERIFY(SourceReadSession::textDigest({"ab", "c"}) != SourceReadSession::textDigest({"a", "bc"}));
        QVERIFY(SourceReadSession::textDigest({QString(QChar(0xd800))})
                != SourceReadSession::textDigest({QString(QChar(0xd801))}));
        QVERIFY(SourceReadSession::textDigest({}) != SourceReadSession::textDigest({""}));
    }
};
QTEST_GUILESS_MAIN(AnalysisSourceInputChecks)
#include "test_analysis_source_input.moc"
