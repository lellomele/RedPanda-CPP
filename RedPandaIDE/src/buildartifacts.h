// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef BUILDARTIFACTS_H
#define BUILDARTIFACTS_H
#include <QStringList>
#include <QDateTime>
namespace BuildArtifacts {
struct Entry { QString path; QString canonicalPath; qint64 size; QDateTime modified; };
QList<Entry> inspect(const QStringList &candidates, const QStringList &allowedRoots, const QStringList &protectedFiles);
bool unchanged(const Entry &entry, const QStringList &allowedRoots, const QStringList &protectedFiles);
}
#endif
