// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef FROZENCOMPILEARGUMENTS_H
#define FROZENCOMPILEARGUMENTS_H
#include "compileconfiguration.h"
#include "../utils/parsearg.h"
#include "qt_utils/utils.h"
#include <limits>

namespace Compilation {
enum class ArgumentUse { C, Cpp, Linker };

inline QStringList optionArguments(const ConfigurationValues &config, ArgumentUse use,
                                   bool checkSyntax, bool suppressDebug = false)
{
    QStringList result;
    if (checkSyntax && use != ArgumentUse::Linker) result.append(QStringLiteral("-fsyntax-only"));
    const auto effective = config.effectiveCompileOptions(suppressDebug);
    for (auto it = effective.cbegin(); it != effective.cend(); ++it) {
        if (it.value().isEmpty()) continue;
        const auto option = config.toolchain.optionCatalogue.constFind(it.key());
        if (option == config.toolchain.optionCatalogue.cend()) continue;
        const bool applies = use == ArgumentUse::Linker ? option->isLinker
            : !option->isLinker && (use == ArgumentUse::Cpp ? option->isCpp : option->isC);
        if (!applies) continue;
        switch (option->type) {
        case CompilerOptionType::Checkbox: result.append(option->setting); break;
        case CompilerOptionType::Input: result += {option->setting, it.value()}; break;
        case CompilerOptionType::Number: {
            bool ok = false;
            const int parsed = it.value().toInt(&ok);
            if (ok && parsed > 0) {
                // Preserve valid historical flags without signed multiplication
                // overflow for a malformed/custom numeric compiler option.
                const qint64 scaled = qint64(option->scale) * parsed;
                if (scaled >= std::numeric_limits<int>::min() && scaled <= std::numeric_limits<int>::max())
                    result.append(option->setting + QString::number(scaled));
            }
            break;
        }
        case CompilerOptionType::Choice: result.append(option->setting + it.value()); break;
        }
    }
    if (use == ArgumentUse::Linker) {
        if (config.toolchain.useCustomLinkParams)
            result += parseArguments(config.toolchain.customLinkParams, config.macros, true);
    } else if (config.toolchain.useCustomCompileParams) {
        result += parseArguments(config.toolchain.customCompileParams, config.macros, true);
    }
    if (config.project && use != ArgumentUse::Linker) {
        QString extra = use == ArgumentUse::Cpp ? config.project->cppCompilerCommand : config.project->compilerCommand;
        extra.replace(QStringLiteral("_@@_"), QStringLiteral(" "));
        result += parseArguments(extra, config.macros, true);
    }
    return result;
}

inline QStringList unavailableToolchainMacros(const ConfigurationValues &config, ArgumentUse sourceUse, bool syntaxOnly)
{
    QStringList missing;
    const auto originalCompile = optionArguments(config, sourceUse, syntaxOnly);
    const auto originalLink = syntaxOnly ? QStringList{} : optionArguments(config, ArgumentUse::Linker, false);
    for (const QString &name : {QStringLiteral("INCLUDE"), QStringLiteral("LIB")}) {
        if (!config.macros.value(name).isEmpty()) continue;
        auto sentinel = config;
        sentinel.macros[name] = QStringLiteral("<unavailable-toolchain-directory>");
        // Use the actual argument parser, so quoted literals/escaped dollars
        // are not mistaken for macro expansion and silently rewritten.
        if (optionArguments(sentinel, sourceUse, syntaxOnly) != originalCompile
                || (!syntaxOnly && optionArguments(sentinel, ArgumentUse::Linker, false) != originalLink))
            missing.append(name);
    }
    return missing;
}

inline QStringList includeArguments(const QStringList &directories)
{
    QStringList result;
    for (const auto &directory : directories) result.append(QStringLiteral("-I") + directory);
    return result;
}

struct ArgumentPlan {
    QStringList arguments;
    PreparationResult preparation;
};
inline ArgumentPlan libraryArguments(const ConfigurationValues &config, const AutolinkResolution &autolink,
                                    bool applyAutolink, bool suppressDebug = false)
{
    QStringList result;
    for (const auto &directory : config.toolchain.libDirs) result.append(QStringLiteral("-L") + directory);
    if (config.project)
        for (const auto &directory : config.project->libDirs) result.append(QStringLiteral("-L") + directory);
    if (applyAutolink && config.autolink.enabled) {
        if (!autolink.complete)
            return {{}, PreparationResult::failure(autolink.error.isEmpty()
                         ? QStringLiteral("Dependency analysis is incomplete.") : autolink.error)};
        for (const auto &rule : autolink.linkOptions) result += parseArgumentsWithoutVariables(rule);
    }
    result += optionArguments(config, ArgumentUse::Linker, false, suppressDebug);
    if (config.project) {
        if (config.project->guiApplication) result.append(QStringLiteral("-mwindows"));
        QString extra = config.project->linkerCommand;
        extra.replace(QStringLiteral("_@@_"), QStringLiteral(" "));
        // Match the legacy command ordering and no project linker macro expansion.
        result += parseArguments(extra, {}, true);
        if (config.project->staticLink) result.append(QStringLiteral("-static"));
    } else if (config.toolchain.staticLink) result.append(QStringLiteral("-static"));
    return {result, {}};
}

inline QString charsetName(const QByteArray &encoding, const PreferencesValues &preferences)
{
    if (encoding == ENCODING_SYSTEM_DEFAULT) return QString::fromLatin1(preferences.systemEncoding);
    if (encoding == ENCODING_OEM_DEFAULT) return QString::fromLatin1(preferences.consoleEncoding);
    if (encoding == ENCODING_UTF8_BOM) return QStringLiteral("UTF-8");
    if (encoding == ENCODING_UTF16_BOM) return QStringLiteral("UTF-16");
    if (encoding == ENCODING_UTF32_BOM) return QStringLiteral("UTF-32");
    return QString::fromLatin1(encoding);
}

inline QStringList charsetArguments(const ConfigurationValues &config, const QByteArray &sourceEncoding,
                                    bool checkSyntax, bool forceExecUTF8)
{
    if ((!forceExecUTF8 && !config.toolchain.autoAddCharsetParams) || sourceEncoding == ENCODING_ASCII
            || !config.toolchain.supportConvertingCharset) return {};
    const auto input = charsetName(sourceEncoding, config.preferences);
    const auto exec = forceExecUTF8 ? QStringLiteral("UTF-8")
        : charsetName(config.toolchain.execCharset.isEmpty() ? QByteArray(ENCODING_SYSTEM_DEFAULT)
                                                           : config.toolchain.execCharset.toLatin1(), config.preferences);
    if (checkSyntax) return {QStringLiteral("-finput-charset=") + input};
    if (input.compare(exec, Qt::CaseInsensitive) == 0) return {};
    return {QStringLiteral("-finput-charset=") + input, QStringLiteral("-fexec-charset=") + exec};
}

inline QProcessEnvironment processEnvironment(const ConfigurationValues &config,
                                               const QString &executableDirectory, QChar pathSeparator)
{
    auto environment = config.environment;
    QString path = environment.value(QStringLiteral("PATH"));
#ifdef Q_OS_WIN
    if (!config.toolchain.binDirs.isEmpty()) {
        const QString bins = config.toolchain.binDirs.join(pathSeparator);
        path = path.isEmpty() ? bins : bins + pathSeparator + path;
    }
#endif
    if (!executableDirectory.isEmpty()) {
        path = path.isEmpty() ? executableDirectory : executableDirectory + pathSeparator + path;
        environment.insert(QStringLiteral("PATH"), path);
    }
    if (config.toolchain.supportNLS && config.toolchain.forceEnglishOutput) {
        environment.insert(QStringLiteral("LANGUAGE"), {});
        environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    }
    environment.insert(QStringLiteral("LDFLAGS"), {});
    environment.insert(QStringLiteral("CFLAGS"), {});
    environment.insert(QStringLiteral("CXXFLAGS"), {});
    return environment;
}
} // namespace Compilation
#endif
