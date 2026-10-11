#include "snippetsstorage.h"
#include <QCoreApplication>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

static QString snippetError(const char *text) { return QCoreApplication::translate("SnippetStorage", text); }
namespace SnippetStorage {
QList<PCodeSnippet> clone(const QList<PCodeSnippet> &snippets) {
    QList<PCodeSnippet> result;
    result.reserve(snippets.size());
    for (const auto &snippet : snippets)
        if (snippet) result.append(std::make_shared<CodeSnippet>(*snippet));
    return result;
}
bool validate(const QList<PCodeSnippet> &snippets, QString &error) {
    if (snippets.size() > 5000) { error = snippetError("Too many snippets (maximum 5000)."); return false; }
    QSet<QString> prefixes;
    for (int i = 0; i < snippets.size(); ++i) {
        const auto &s = snippets[i];
        if (!s || s->caption.trimmed().isEmpty() || s->code.isEmpty() ||
            s->caption.size() > 256 || s->prefix.size() > 256 || s->desc.size() > 8192 || s->code.size() > 256 * 1024 ||
            s->section < -1 || s->section > 999 ||
            !QStringList{"all", "c", "cpp", "asm"}.contains(s->language) ||
            (s->prefix.trimmed().isEmpty() && s->section < 0)) {
            error = snippetError("Invalid snippet at row %1. Check name, prefix, code, language and menu section.").arg(i + 1); return false;
        }
        if (!s->prefix.isEmpty() && s->enabled) {
            const QString key = s->language + QChar(0x1f) + s->prefix;
            if (prefixes.contains(key)) { error = snippetError("Duplicate completion prefix: %1").arg(s->prefix); return false; }
            prefixes.insert(key);
        }
        static const QRegularExpression replacements("%REPL_BEGIN%|%REPL_END%");
        auto tokens = replacements.globalMatch(s->code); int depth = 0; int replacementStart = 0;
        while (tokens.hasNext()) {
            const auto match = tokens.next();
            const bool opening = match.captured() == "%REPL_BEGIN%";
            depth += opening ? 1 : -1;
            if (depth < 0 || depth > 1) { error = snippetError("Invalid replacement placeholders in snippet: %1").arg(s->caption); return false; }
            if (opening) replacementStart = match.capturedEnd();
            else {
                const QString value = s->code.mid(replacementStart, match.capturedStart() - replacementStart);
                if (value.contains('\n') || value.contains('\r') || value.contains("%INSERT%")) {
                    error = snippetError("Invalid replacement placeholders in snippet: %1").arg(s->caption); return false;
                }
            }
        }
        if (depth != 0) { error = snippetError("Invalid replacement placeholders in snippet: %1").arg(s->caption); return false; }
    }
    error.clear(); return true;
}
bool decode(const QByteArray &bytes, QList<PCodeSnippet> &result, QString &error) {
    if (bytes.size() > MaxFileBytes) { error = snippetError("Snippet file is too large (maximum 8 MB)."); return false; }
    QJsonParseError parse; auto document = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError) { error = parse.errorString(); return false; }
    QJsonArray array;
    if (document.isArray()) array = document.array();
    else if (document.isObject() && document.object().value("version").toInt() == 1 && document.object().value("snippets").isArray())
        array = document.object().value("snippets").toArray();
    else { error = snippetError("Expected a snippet array or a version 1 snippet library."); return false; }
    if (array.size() > 5000) { error = snippetError("Too many snippets (maximum 5000)."); return false; }
    QList<PCodeSnippet> candidate;
    for (const auto &value : array) {
        if (!value.isObject()) { error = snippetError("Each snippet must be an object."); return false; }
        auto object = value.toObject();
        for (const auto &key : {"caption", "prefix", "code", "description"}) {
            if (!object.value(key).isString()) { error = snippetError("Snippet text fields must be strings."); return false; }
        }
        if ((object.contains("enabled") && !object.value("enabled").isBool()) ||
            (object.contains("language") && !object.value("language").isString()) ||
            !object.value("section").isDouble() || object.value("section").toDouble() != object.value("section").toInt()) {
            error = snippetError("Invalid snippet metadata."); return false;
        }
        auto s = std::make_shared<CodeSnippet>();
        s->caption = object.value("caption").toString(); s->prefix = object.value("prefix").toString();
        s->code = object.value("code").toString(); s->desc = object.value("description").toString();
        s->section = object.value("section").toInt(); s->language = object.value("language").toString("all");
        s->enabled = object.value("enabled").toBool(true); candidate.append(s);
    }
    if (!validate(candidate, error)) return false;
    result = candidate; return true;
}
QByteArray encode(const QList<PCodeSnippet> &snippets) {
    QJsonArray array;
    for (const auto &s : snippets) {
        if (!s) continue;
        array.append(QJsonObject{{"caption", s->caption}, {"prefix", s->prefix}, {"code", s->code},
            {"description", s->desc}, {"section", s->section}, {"language", s->language}, {"enabled", s->enabled}});
    }
    return QJsonDocument(array).toJson();
}
bool read(const QString &path, QList<PCodeSnippet> &result, QString &error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return false; }
    if (file.size() > MaxFileBytes) { error = snippetError("Snippet file is too large (maximum 8 MB)."); return false; }
    const auto bytes = file.read(MaxFileBytes + 1);
    if (file.error() != QFile::NoError) { error = file.errorString(); return false; }
    return decode(bytes, result, error);
}
bool write(const QString &path, const QList<PCodeSnippet> &snippets, QString &error) {
    if (!validate(snippets, error)) return false;
    const QByteArray bytes = encode(snippets);
    if (bytes.size() > MaxFileBytes) { error = snippetError("Snippet file is too large (maximum 8 MB)."); return false; }
    QSaveFile file(path); file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) { error = file.errorString(); return false; }
    return true;
}
bool matches(const PCodeSnippet &s, const QString &language) {
    return s && s->enabled && (s->language == "all" || s->language == language || (language == "c-cpp" && (s->language == "c" || s->language == "cpp")));
}
QString languageForFileType(FileType type) {
    if (type == FileType::CSource) return "c";
    if (type == FileType::ATTASM || type == FileType::INTELASM || type == FileType::GAS || type == FileType::NASM) return "asm";
    if (type == FileType::CCppHeader) return "c-cpp";
    if (type == FileType::CppSource || type == FileType::PreprocessedSource) return "cpp";
    return "other";
}
}
