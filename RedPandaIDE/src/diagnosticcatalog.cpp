// SPDX-License-Identifier: GPL-3.0-or-later
#include "diagnosticcatalog.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <QVector>
#include <QVariant>
namespace {
struct Rule { QString id; QRegularExpression pattern; QString italian; };
const QVector<Rule> &rules() {
    // Immutable after first load: compiled once, shared safely across callers.
    static const QVector<Rule> catalog = [] {
        QFile file(":/config/gcc-it-diagnostics.json");
        if (!file.open(QIODevice::ReadOnly) || file.size() > 512 * 1024) return QVector<Rule>{};
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) return QVector<Rule>{};
        const auto root = document.object();
        if (root.value("schemaVersion").toInt() != 1 || root.value("language").toString() != "it" || root.value("engine").toString() != "gcc" || !root.value("rules").isArray()) return QVector<Rule>{};
        const auto entries = root.value("rules").toArray();
        if (entries.isEmpty() || entries.size() > 512) return QVector<Rule>{};
        QVector<Rule> result; QSet<QString> identifiers;
        const QRegularExpression placeholder(QStringLiteral(R"(\{([1-9][0-9]*)\})"));
        for (const auto &entry : entries) {
            if (!entry.isObject()) return QVector<Rule>{};
            const auto object = entry.toObject();
            const QString id = object.value("id").toString();
            const QString pattern = object.value("pattern").toString();
            const QString italian = object.value("italian").toString();
            if (id.isEmpty() || identifiers.contains(id) || pattern.size() > 2048 || italian.isEmpty() || italian.size() > 2048 || !pattern.startsWith('^') || !pattern.endsWith('$')) return QVector<Rule>{};
            QRegularExpression regex(pattern, QRegularExpression::UseUnicodePropertiesOption);
            if (!regex.isValid()) return QVector<Rule>{};
            QSet<int> used; auto placeholders = placeholder.globalMatch(italian);
            while (placeholders.hasNext()) {
                const int capture = placeholders.next().captured(1).toInt();
                if (capture < 1 || capture > regex.captureCount()) return QVector<Rule>{};
                used.insert(capture);
            }
            // Every captured identifier/type must survive the translation.
            if (used.size() != regex.captureCount()) return QVector<Rule>{};
            regex.optimize(); identifiers.insert(id); result.append({id, regex, italian});
        }
        return result;
    }();
    return catalog;
}
QString substitute(const QString &text, const QRegularExpressionMatch &match) {
    static const QRegularExpression placeholder(QStringLiteral(R"(\{([1-9][0-9]*)\})"));
    QString result; int position = 0; auto placeholders = placeholder.globalMatch(text);
    while (placeholders.hasNext()) {
        const auto token = placeholders.next();
        result += text.mid(position, token.capturedStart() - position);
        // Never reinterpret percent/braces appearing in a compiler's quoted text.
        result += match.captured(token.captured(1).toInt());
        position = token.capturedEnd();
    }
    return result + text.mid(position);
}
}
bool DiagnosticCatalog::isItalianInterface() {
    if (!QCoreApplication::instance()) return false;
    const QString language = QCoreApplication::instance()->property("interfaceLanguage").toString();
    return language == "it" || language.startsWith("it_") || language.startsWith("it-");
}
int DiagnosticCatalog::ruleCount() { return rules().size(); }
DiagnosticCatalog::Translation DiagnosticCatalog::translate(const QString &original, const QString &language) {
    if (!(language == "it" || language.startsWith("it_") || language.startsWith("it-")) || original.size() > 16384) return {};
    QString body = original.trimmed(), prefix, suffix;
    static const QRegularExpression severity(QStringLiteral(R"(^(\[(?:Error|Warning|Note|Errore|Avvertimento|Nota|Info)\]|fatal error:|error:|warning:|note:)\s*)"));
    const auto severityMatch = severity.match(body);
    if (severityMatch.hasMatch()) {
        const QString token = severityMatch.captured(1);
        if (token == "[Error]") prefix = "[Errore] ";
        else if (token == "[Warning]") prefix = "[Avvertimento] ";
        else if (token == "[Note]") prefix = "[Nota] ";
        else if (token == "error:") prefix = "Errore: ";
        else if (token == "fatal error:") prefix = "Errore fatale: ";
        else if (token == "warning:") prefix = "Avvertimento: ";
        else if (token == "note:") prefix = "Nota: ";
        else prefix = token + ' ';
        body.remove(0, severityMatch.capturedLength());
    }
    static const QRegularExpression flags(QStringLiteral(R"((\s+(?:\[-[^\]\r\n]+\]|\[enabled by default\]))+$)"));
    const auto options = flags.match(body);
    if (options.hasMatch()) { suffix = options.captured(0); body.truncate(options.capturedStart()); }
    for (const auto &rule : rules()) {
        const auto match = rule.pattern.match(body);
        if (match.hasMatch()) return {prefix + substitute(rule.italian, match) + suffix, rule.id};
    }
    return {};
}
