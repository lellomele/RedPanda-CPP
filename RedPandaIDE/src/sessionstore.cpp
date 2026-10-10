#include "sessionstore.h"
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>
#include <QCoreApplication>
namespace SessionStore {
static QString message(const char *s) { return QCoreApplication::translate("SessionStore", s); }
bool write(const QString &path, const QList<RecoveryDocument> &documents, QString &error) {
    if (documents.size() > 256) { error = message("Too many documents for one recovery snapshot (maximum 256)."); return false; }
    QJsonArray entries;
    for (const auto &d : documents) {
        if (d.text.size() > 32 * 1024 * 1024 || d.filename.size() > 4096) { error = message("A document exceeds the recovery size limit."); return false; }
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
    if (entries.size() > 256) { error = message("Too many documents in recovery snapshot."); return false; }
    QList<RecoveryDocument> candidate;
    for (const auto &value : entries) {
        if (!value.isObject()) { error = message("Invalid recovery document."); return false; }
        auto o = value.toObject();
        if (!o.value("filename").isString() || !o.value("text").isString() || o.value("text").toString().size() > 32 * 1024 * 1024 || o.value("filename").toString().size() > 4096) {
            error = message("Invalid recovery document."); return false;
        }
        candidate.append({o.value("filename").toString(), o.value("text").toString(), qMax(0, o.value("line").toInt()), qMax(0, o.value("column").toInt())});
    }
    documents = candidate; error.clear(); return true;
}
}
