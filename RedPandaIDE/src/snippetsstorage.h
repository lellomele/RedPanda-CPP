#ifndef SNIPPETSSTORAGE_H
#define SNIPPETSSTORAGE_H
#include "parser/parserutils.h"
#include "utils/file.h"
#include <QByteArray>
namespace SnippetStorage {
constexpr qint64 MaxFileBytes = 8 * 1024 * 1024;
bool decode(const QByteArray &bytes, QList<PCodeSnippet> &result, QString &error);
bool validate(const QList<PCodeSnippet> &snippets, QString &error);
QByteArray encode(const QList<PCodeSnippet> &snippets);
bool read(const QString &path, QList<PCodeSnippet> &result, QString &error);
bool write(const QString &path, const QList<PCodeSnippet> &snippets, QString &error);
QList<PCodeSnippet> clone(const QList<PCodeSnippet> &snippets);
bool matches(const PCodeSnippet &snippet, const QString &language);
QString languageForFileType(FileType fileType);
}
#endif
