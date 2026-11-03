// SPDX-License-Identifier: GPL-3.0-or-later
#include "parserconfiguration.h"
#include <QCryptographicHash>
#include <QCoreApplication>
#include <algorithm>
#include <limits>

namespace Analysis {
ParserConfigurationValues ParserConfiguration::values() const
{
    return {language, enabled, includeOnce, includePaths, projectIncludePaths,
            hardDefineLines, projectFiles, compilerPreparationIdentity};
}

QString ParserConfiguration::validate(const ParserConfigurationLimits &limits) const
{
    const auto error = [](const char *text) { return QCoreApplication::translate("ParserConfiguration", text); };
    switch (language) {
    case ParserLanguage::None:
        if (enabled) return error("An enabled parser requires a supported language.");
        break;
    case ParserLanguage::C:
    case ParserLanguage::CPlusPlus:
#ifdef ENABLE_SDCC
    case ParserLanguage::SDCC:
#endif
        break;
    default: return error("An enabled parser requires a supported language.");
    }
    if (limits.maximumIncludePaths < 0 || limits.maximumHardDefines < 0 || limits.maximumProjectFiles < 0
            || limits.maximumFieldUnits < 0 || limits.maximumTotalUnits < 0)
        return error("Invalid parser configuration limits.");
    if (includePaths.size() > limits.maximumIncludePaths || projectIncludePaths.size() > limits.maximumIncludePaths
            || hardDefineLines.size() > limits.maximumHardDefines || projectFiles.size() > limits.maximumProjectFiles)
        return error("Parser configuration exceeds its item limit.");
    qint64 remaining = limits.maximumTotalUnits;
    const auto accepts = [&](const QString &text) {
        if (text.size() > limits.maximumFieldUnits || text.size() > remaining
                || !text.isValidUtf16() || text.contains(QChar::Null)) return false;
        remaining -= text.size();
        return true;
    };
    for (const auto &path : includePaths) if (!accepts(path)) return error("Invalid or excessive parser configuration text.");
    for (const auto &path : projectIncludePaths) if (!accepts(path)) return error("Invalid or excessive parser configuration text.");
    for (const auto &line : hardDefineLines) if (!accepts(line)) return error("Invalid or excessive parser configuration text.");
    for (const auto &path : projectFiles) if (!accepts(path)) return error("Invalid or excessive parser configuration text.");
    if (compilerPreparationIdentity.size() > remaining) return error("Invalid or excessive parser configuration text.");
    return {};
}

QByteArray ParserConfiguration::digest() const
{
    if (!validate().isEmpty()) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArrayView("RedPanda.ParserConfiguration.v1"));
    const auto integer = [&](quint64 value) {
        char bytes[8];
        for (int i = 0; i < 8; ++i) bytes[i] = char(value >> (56 - i * 8));
        hash.addData(QByteArrayView(bytes, 8));
    };
    const auto bytes = [&](QByteArrayView value) { integer(value.size()); hash.addData(value); };
    const auto strings = [&](const QStringList &list) {
        integer(list.size());
        for (const auto &item : list) { const QByteArray utf8 = item.toUtf8(); bytes(utf8); }
    };
    integer(static_cast<quint64>(language)); integer(enabled); integer(includeOnce);
    strings(includePaths); strings(projectIncludePaths); strings(hardDefineLines);
    QStringList sortedFiles(projectFiles.cbegin(), projectFiles.cend());
    std::sort(sortedFiles.begin(), sortedFiles.end());
    strings(sortedFiles); bytes(compilerPreparationIdentity);
    return hash.result();
}
} // namespace Analysis
