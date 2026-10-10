// SPDX-License-Identifier: GPL-3.0-or-later
#include "buildartifacts.h"
#include <QDir>
#include <QFileInfo>
#include <QSet>
namespace {
QString key(QString path) {
    path = QDir::cleanPath(path);
#ifdef Q_OS_WIN
    path = path.toCaseFolded();
#endif
    return path;
}
QString canonical(const QString &path) {
    const QFileInfo info(path);
    return key(info.exists() ? info.canonicalFilePath() : info.absoluteFilePath());
}
}
QList<BuildArtifacts::Entry> BuildArtifacts::inspect(const QStringList &candidates, const QStringList &allowedRoots, const QStringList &protectedFiles) {
    QList<Entry> result;
    QStringList roots; QSet<QString> protectedPaths, seen;
    for (const auto &root : allowedRoots) {
        const QFileInfo info(root);
        if (info.isDir()) roots.append(canonical(root));
    }
    for (const auto &path : protectedFiles) if (!path.isEmpty()) protectedPaths.insert(canonical(path));
    const QSet<QString> suffixes{"exe", "dll", "so", "dylib", "a", "lib", "o", "obj", "res", "gch", "pch", "ihx", "hex", ""};
    for (const auto &path : candidates) {
        const QFileInfo info(path);
        if (!info.isFile() || info.isSymLink() || !suffixes.contains(info.suffix().toLower())) continue;
        const QString identity = canonical(path);
        if (identity.isEmpty() || protectedPaths.contains(identity) || seen.contains(identity)) continue;
        bool inside = false;
        for (const auto &root : roots) if (identity.startsWith(root.endsWith('/') ? root : root + '/')) { inside = true; break; }
        if (!inside) continue;
        seen.insert(identity);
        result.append({info.absoluteFilePath(), identity, info.size(), info.lastModified()});
    }
    return result;
}
bool BuildArtifacts::unchanged(const Entry &entry, const QStringList &allowedRoots, const QStringList &protectedFiles) {
    const auto current = inspect({entry.path}, allowedRoots, protectedFiles);
    return current.size() == 1 && current[0].canonicalPath == entry.canonicalPath && current[0].size == entry.size && current[0].modified == entry.modified;
}
