#include "sessionstore.h"
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>
#include <QCoreApplication>
#include <limits>
#include <algorithm>
namespace SessionStore {
static QString message(const char *s) { return QCoreApplication::translate("SessionStore", s); }
static bool validCoordinate(const QJsonObject &object, const char *name) {
    if (!object.contains(name)) return true; // Legacy snapshots may omit caret metadata.
    const auto value = object.value(name);
    if (!value.isDouble()) return false;
    const double number = value.toDouble();
    return number >= std::numeric_limits<int>::min() && number <= std::numeric_limits<int>::max() && number == value.toInt();
}
// A conservative UTF-8 JSON string size, computed without encoding or building
// a QJson container. Invalid UTF-16 units are charged as escaped \uXXXX.
static qint64 encodedStringSize(QStringView text, qint64 remaining) {
    qint64 bytes = 0;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const ushort c = text.at(i).unicode();
        qint64 add = 1;
        if (c == '"' || c == '\\' || c == '/') add = 2;
        else if (c == '\b' || c == '\f' || c == '\n' || c == '\r' || c == '\t') add = 2;
        else if (c < 0x20 || c == 0x2028 || c == 0x2029) add = 6;
        else if (QChar::isHighSurrogate(c) && i + 1 < text.size() && QChar::isLowSurrogate(text.at(i + 1).unicode())) {
            add = 4;
            ++i;
        } else if (QChar::isSurrogate(c)) add = 6;
        else if (c >= 0x800) add = 3;
        else if (c >= 0x80) add = 2;
        if (add > remaining - bytes) return -1;
        bytes += add;
    }
    return bytes;
}

SnapshotBudget::SnapshotBudget(qint64 maximumBytes, qint64 maximumTextCharacters):
    mMaximumBytes(std::max<qint64>(0, maximumBytes)),
    mMaximumTextCharacters(std::max<qint64>(0, maximumTextCharacters)) {}

bool SnapshotBudget::beginDocument(const QString &filename, QString &error) {
    if (mDocuments >= MaxDocuments) {
        error = message("Too many documents for one recovery snapshot (maximum 256).");
        return false;
    }
    if (filename.size() > 4096) {
        error = message("A document exceeds the recovery size limit.");
        return false;
    }
    // Includes document keys, two signed integer coordinates, punctuation and
    // the comma between entries. No allocation is needed to reject the input.
    constexpr qint64 documentOverhead = 96;
    const qint64 remaining = mMaximumBytes - mEncodedBytes;
    const qint64 filenameBytes = remaining >= documentOverhead
            ? encodedStringSize(QStringView{filename}, remaining - documentOverhead) : -1;
    if (filenameBytes < 0) {
        error = message("Recovery snapshot exceeds 128 MB.");
        return false;
    }
    mEncodedBytes += documentOverhead + filenameBytes;
    mDocumentCharacters = 0;
    ++mDocuments;
    error.clear();
    return true;
}

bool SnapshotBudget::appendText(QStringView text, QString &error) {
    if (mDocuments == 0) {
        error = message("Invalid recovery snapshot budget state.");
        return false;
    }
    if (text.size() > MaxDocumentCharacters - mDocumentCharacters) {
        error = message("A document exceeds the recovery size limit.");
        return false;
    }
    if (text.size() > mMaximumTextCharacters - mTextCharacters) {
        error = message("Recovery snapshot exceeds the total text size limit (32 Mi characters).");
        return false;
    }
    const qint64 bytes = encodedStringSize(text, mMaximumBytes - mEncodedBytes);
    if (bytes < 0) {
        error = message("Recovery snapshot exceeds 128 MB.");
        return false;
    }
    mEncodedBytes += bytes;
    mDocumentCharacters += text.size();
    mTextCharacters += text.size();
    error.clear();
    return true;
}

bool write(const QString &path, const QList<RecoveryDocument> &documents, QString &error) {
    if (documents.size() > MaxDocuments) { error = message("Too many documents for one recovery snapshot (maximum 256)."); return false; }
    SnapshotBudget budget;
    for (const auto &d : documents) {
        if (!budget.beginDocument(d.filename, error) || !budget.appendText(QStringView{d.text}, error)) return false;
    }
    QJsonArray entries;
    for (const auto &d : documents) {
        entries.append(QJsonObject{{"filename", d.filename}, {"text", d.text}, {"line", d.line}, {"column", d.column}});
    }
    const QByteArray bytes = QJsonDocument(QJsonObject{{"version", 1}, {"savedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}, {"documents", entries}}).toJson(QJsonDocument::Compact);
    if (bytes.size() > MaxBytes) { error = message("Recovery snapshot exceeds 128 MB."); return false; }
    QSaveFile file(path); file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) { error = file.errorString(); return false; }
    error.clear(); return true;
}
bool read(const QString &path, QList<RecoveryDocument> &documents, QString &error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return false; }
    if (file.size() > MaxBytes) { error = message("Recovery snapshot exceeds 128 MB."); return false; }
    const auto bytes = file.read(MaxBytes + 1);
    if (bytes.size() > MaxBytes || file.error() != QFile::NoError) { error = message("Could not read recovery snapshot."); return false; }
    QJsonParseError parse; const auto doc = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject() || doc.object().value("version").toInt() != 1 || !doc.object().value("documents").isArray()) {
        error = message("Invalid recovery snapshot."); return false;
    }
    const auto entries = doc.object().value("documents").toArray();
    if (entries.size() > MaxDocuments) { error = message("Too many documents in recovery snapshot."); return false; }
    QList<RecoveryDocument> candidate;
    for (const auto &value : entries) {
        if (!value.isObject()) { error = message("Invalid recovery document."); return false; }
        auto o = value.toObject();
        if (!validCoordinate(o, "line") || !validCoordinate(o, "column") || !o.value("filename").isString() || !o.value("text").isString() || o.value("text").toString().size() > MaxDocumentCharacters || o.value("filename").toString().size() > 4096) {
            error = message("Invalid recovery document."); return false;
        }
        candidate.append({o.value("filename").toString(), o.value("text").toString(), qMax(0, o.value("line").toInt()), qMax(0, o.value("column").toInt())});
    }
    documents = candidate; error.clear(); return true;
}
}
