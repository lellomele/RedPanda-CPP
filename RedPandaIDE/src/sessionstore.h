#ifndef SESSIONSTORE_H
#define SESSIONSTORE_H
#include <QList>
#include <QString>
struct RecoveryDocument { QString filename; QString text; int line = 0; int column = 0; };
namespace SessionStore {
constexpr qint64 MaxBytes = 128 * 1024 * 1024;
bool write(const QString &path, const QList<RecoveryDocument> &documents, QString &error);
bool read(const QString &path, QList<RecoveryDocument> &documents, QString &error);
}
#endif
