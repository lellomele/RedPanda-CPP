// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include "src/documentstore.h"
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

class PathIdentityTests : public QObject {
    Q_OBJECT
private slots:
    void invalidPathsCannotBecomeNamespaceKeys();
    void lexicalSpellingDoesNotFoldUnicode();
    void distinctUnicodeFilesRemainDistinct_data();
    void distinctUnicodeFilesRemainDistinct();
    void nativeAliasesNeedExistingFileIdentity();
    void missingVirtualAliasIsConflict();
    void duplicateCapturePreservesPublishedInput();
    void relativeRegistrationRetainsItsOriginalRoot();
    void caseSensitiveNamespaceConflictPreservesDocuments();
};

static bool writeNew(const QString &path, const QByteArray &data) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::NewOnly) && file.write(data) == data.size();
}

void PathIdentityTests::invalidPathsCannotBecomeNamespaceKeys() {
    QVERIFY(!NativePathKey(QString{}).valid());
    QVERIFY(!NativePathKey(QString(QChar(0xd800))).valid());
    QVERIFY(!NativePathKey(QStringLiteral("a") + QChar::Null + QStringLiteral("b")).valid());
    QVERIFY(NativePathKey(QString::fromUtf8("città.cpp")).valid());
}

void PathIdentityTests::lexicalSpellingDoesNotFoldUnicode() {
    QCOMPARE(exactPathSpelling(QString::fromUtf8("a/../K.cpp")), QString::fromUtf8("K.cpp"));
#ifdef Q_OS_WIN
    QVERIFY(NativePathKey("Mixed.cpp") == NativePathKey("mixed.cpp"));
    QVERIFY(NativePathKey(QString::fromUtf8("ä.cpp")) == NativePathKey(QString::fromUtf8("Ä.cpp")));
#else
    QVERIFY(!(NativePathKey("Mixed.cpp") == NativePathKey("mixed.cpp")));
#endif
}

void PathIdentityTests::distinctUnicodeFilesRemainDistinct_data() {
    QTest::addColumn<QString>("left");
    QTest::addColumn<QString>("right");
    QTest::newRow("sharp-s") << QString::fromUtf8("ß.cpp") << QString::fromUtf8("ẞ.cpp");
    QTest::newRow("kelvin") << QString::fromUtf8("k.cpp") << QString::fromUtf8("K.cpp");
    QTest::newRow("angstrom") << QString::fromUtf8("å.cpp") << QString::fromUtf8("Å.cpp");
    QTest::newRow("ohm") << QString::fromUtf8("ω.cpp") << QString::fromUtf8("Ω.cpp");
    QTest::newRow("final-sigma") << QString::fromUtf8("σ.cpp") << QString::fromUtf8("ς.cpp");
    QTest::newRow("long-s") << QString::fromUtf8("s.cpp") << QString::fromUtf8("ſ.cpp");
    QTest::newRow("micro") << QString::fromUtf8("µ.cpp") << QString::fromUtf8("μ.cpp");
    QTest::newRow("deseret") << QString::fromUtf8("𐐀.cpp") << QString::fromUtf8("𐐨.cpp");
}

void PathIdentityTests::distinctUnicodeFilesRemainDistinct() {
    QFETCH(QString, left); QFETCH(QString, right);
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const QString a = directory.filePath(left), b = directory.filePath(right);
    QVERIFY(writeNew(a, "disk A"));
    QVERIFY(writeNew(b, "disk B"));
    QCOMPARE(sameExistingFile(a, b), ExistingFileMatch::Different);
    QVERIFY(!(NativePathKey(a) == NativePathKey(b)));
    QVERIFY(!verifiedPathAlias(a, b));
    DocumentStore store;
    const auto idA = store.add(a, [](QStringList &text) { text = {"unsaved A"}; return true; });
    const auto idB = store.add(b, [](QStringList &text) { text = {"unsaved B"}; return true; });
    const auto capture = store.capture(); QVERIFY2(capture.succeeded(), qPrintable(capture.error));
    QCOMPARE(capture.snapshot->documents().size(), 2);
    QCOMPARE(capture.snapshot->resolveDocument(a).document->id, idA);
    QCOMPARE(capture.snapshot->resolveDocument(b).document->id, idB);
    QCOMPARE(capture.snapshot->document(a)->lines, QStringList{"unsaved A"});
    QCOMPARE(capture.snapshot->document(b)->lines, QStringList{"unsaved B"});
}

void PathIdentityTests::nativeAliasesNeedExistingFileIdentity() {
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const QString a = directory.filePath("Mixed.cpp"), b = directory.filePath("mixed.cpp");
    QVERIFY(writeNew(a, "disk"));
#ifdef Q_OS_WIN
    QCOMPARE(sameExistingFile(a, b), ExistingFileMatch::Same);
    QVERIFY(verifiedPathAlias(a, b));
    DocumentStore store;
    const auto id = store.add(a, [](QStringList &text) { text = {"unsaved"}; return true; });
    const auto capture = store.capture(); QVERIFY(capture.succeeded());
    const auto alias = capture.snapshot->resolveDocument(b);
    QCOMPARE(alias.status, DocumentPathStatus::Found); QCOMPARE(alias.document->id, id);
#else
    QCOMPARE(sameExistingFile(a, b), ExistingFileMatch::Unknown);
    QVERIFY(!verifiedPathAlias(a, b));
#endif
}

void PathIdentityTests::missingVirtualAliasIsConflict() {
    QTemporaryDir directory; QVERIFY(directory.isValid());
    DocumentStore store;
    const QString a = directory.filePath("Virtual.cpp"), b = directory.filePath("virtual.cpp");
    const auto id = store.add(a, [](QStringList &text) { text = {}; return true; });
    const auto capture = store.capture(); QVERIFY(capture.succeeded());
    QCOMPARE(capture.snapshot->document(a)->id, id);
#ifdef Q_OS_WIN
    QCOMPARE(capture.snapshot->resolveDocument(b).status, DocumentPathStatus::Conflict);
#else
    QCOMPARE(capture.snapshot->resolveDocument(b).status, DocumentPathStatus::NotOpen);
#endif
}

void PathIdentityTests::duplicateCapturePreservesPublishedInput() {
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const QString a = directory.filePath("Source.cpp");
    DocumentStore store;
    const auto first = store.add(a, [](QStringList &text) { text = {"first"}; return true; });
    const auto previous = store.capture(); QVERIFY(previous.succeeded());
    const auto second = store.add(a, [](QStringList &text) { text = {"second"}; return true; });
    QVERIFY(first && second && first != second);
    const auto conflict = store.capture();
    QCOMPARE(conflict.status, InputCaptureStatus::ConflictingPath); QVERIFY(!conflict.snapshot);
    QCOMPARE(previous.snapshot->document(a)->id, first);
    QCOMPARE(previous.snapshot->document(a)->lines, QStringList{"first"});
    QVERIFY(store.remove(second));
    const auto recovered = store.capture(); QVERIFY(recovered.succeeded());
    QCOMPARE(recovered.snapshot->document(a)->id, first);
}

void PathIdentityTests::relativeRegistrationRetainsItsOriginalRoot() {
    QTemporaryDir first, second; QVERIFY(first.isValid() && second.isValid());
    struct RestoreDirectory { QString value = QDir::currentPath(); ~RestoreDirectory() { QDir::setCurrent(value); } } restore;
    QVERIFY(QDir::setCurrent(first.path()));
    QVERIFY(writeNew("Relative.cpp", "A"));
    DocumentStore store;
    const auto id = store.add("Relative.cpp", [](QStringList &text) { text = {"A unsaved"}; return true; });
    const QString absolute = store.absolutePathFor(id);
    const auto capture = store.capture(); QVERIFY(capture.succeeded());
    QVERIFY(QDir::setCurrent(second.path()));
    QVERIFY(writeNew("Relative.cpp", "B"));
    QCOMPARE(capture.snapshot->document("Relative.cpp")->id, id);
    QCOMPARE(capture.snapshot->document(absolute)->id, id);
#ifdef Q_OS_WIN
    QCOMPARE(capture.snapshot->resolveDocument("relative.cpp").status, DocumentPathStatus::Conflict);
#endif
    QCOMPARE(store.absolutePathFor(id), absolute);
}

void PathIdentityTests::caseSensitiveNamespaceConflictPreservesDocuments() {
#ifdef Q_OS_WIN
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const QString native = QDir::toNativeSeparators(directory.path());
    const HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()),
        FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    QVERIFY2(handle != INVALID_HANDLE_VALUE, qPrintable(QString("Open directory failed: %1").arg(GetLastError())));
    FILE_CASE_SENSITIVE_INFORMATION requested{}; requested.Flags = 1;
    const bool enabled = SetFileInformationByHandle(handle, FileCaseSensitiveInfo, &requested, sizeof(requested));
    const DWORD enableError = enabled ? 0 : GetLastError();
    FILE_CASE_SENSITIVE_INFORMATION actual{};
    const bool queried = GetFileInformationByHandleEx(handle, FileCaseSensitiveInfo, &actual, sizeof(actual));
    CloseHandle(handle);
    if (!enabled)
        QSKIP(qPrintable(QString("Native case-sensitive directory fixture unavailable; Windows error %1. This guard was not exercised.").arg(enableError)));
    QVERIFY(queried); QVERIFY(actual.Flags & 1);
    const QString first = directory.filePath("Same.h"), second = directory.filePath("same.h");
    QVERIFY(writeNew(first, "disk A")); QVERIFY(writeNew(second, "disk B"));
    QVERIFY(NativePathKey(first) == NativePathKey(second));
    QCOMPARE(sameExistingFile(first, second), ExistingFileMatch::Different);
    QVERIFY(!verifiedPathAlias(first, second));
    DocumentStore store;
    const auto idA = store.add(first, [](QStringList &text) { text = {"unsaved A"}; return true; });
    const auto previous = store.capture(); QVERIFY(previous.succeeded());
    QCOMPARE(previous.snapshot->resolveDocument(second).status, DocumentPathStatus::NotOpen);
    const auto idB = store.add(second, [](QStringList &text) { text = {"unsaved B"}; return true; });
    const auto conflict = store.capture();
    QCOMPARE(conflict.status, InputCaptureStatus::ConflictingPath); QVERIFY(!conflict.snapshot);
    QCOMPARE(previous.snapshot->document(first)->id, idA);
    QCOMPARE(previous.snapshot->document(first)->lines, QStringList{"unsaved A"});
    QVERIFY(store.remove(idA));
    const auto recovered = store.capture(); QVERIFY(recovered.succeeded());
    QCOMPARE(recovered.snapshot->document(second)->id, idB);
    QCOMPARE(recovered.snapshot->document(second)->lines, QStringList{"unsaved B"});
    QCOMPARE(recovered.snapshot->resolveDocument(first).status, DocumentPathStatus::NotOpen);
#else
    QSKIP("Windows native-ordinal-equivalent case-sensitive namespace fixture is Windows-specific.");
#endif
}

QTEST_GUILESS_MAIN(PathIdentityTests)
#include "test_path_identity.moc"
