// SPDX-License-Identifier: GPL-3.0-or-later
#include "parserconfigurationcapture.h"
#include "../settings/compilersetsettings.h"
#include "../compiler/compilerinfo.h"
#include "../utils/parsearg.h"
#include "../pathidentity.h"
#include <QDir>
#include <QCoreApplication>
#include <QThread>

namespace Analysis {
namespace {
bool driveRelative(const QString &path) {
#ifdef Q_OS_WIN
    return path.size() >= 2 && path[1] == ':'
        && ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z'))
        && (path.size() == 2 || (path[2] != '/' && path[2] != '\\'));
#else
    Q_UNUSED(path)
    return false;
#endif
}
bool qualifiedRoot(const QString &path) {
#ifdef Q_OS_WIN
    return !driveRelative(path) && ((path.size() >= 3 && path[1] == ':' && (path[2] == '/' || path[2] == '\\'))
        || path.startsWith("//") || path.startsWith("\\\\"));
#else
    return QDir::isAbsolutePath(path);
#endif
}
// Preflight before the legacy tokenizer allocates one QString per token. This
// checks quote/escape completeness without expanding variables or executing a
// shell. The actual argument validation still runs after tokenization.
bool boundedCustomText(const QString &text) {
    if (text.size() > ParserConfigurationLimits{}.maximumFieldUnits || !text.isValidUtf16() || text.contains(QChar::Null)) return false;
    enum class Quote { None, Single, Double, Ansi };
    Quote quote = Quote::None;
    bool token = false;
    qsizetype tokens = 0;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar ch = text[i];
        if (quote == Quote::Single) { if (ch == '\'') quote = Quote::None; continue; }
        if (quote == Quote::Ansi) {
            if (ch == '\\') { if (++i == text.size()) return false; }
            else if (ch == '\'') quote = Quote::None;
            continue;
        }
        if (quote == Quote::None) {
            if (ch == ' ' || ch == '\t' || ch == '\n') { token = false; continue; }
            if (!token && ch == '#') { while (i < text.size() && text[i] != '\n') ++i; continue; }
            if (!token) { token = true; if (++tokens > 65536) return false; }
            if (ch == '\'') { quote = Quote::Single; continue; }
            if (ch == '"') { quote = Quote::Double; continue; }
            if (ch == '\\') { if (++i == text.size()) return false; continue; }
        } else {
            if (ch == '"') { quote = Quote::None; continue; }
            if (ch == '\\') {
                if (i + 1 == text.size()) return false;
                const QChar next = text[i + 1];
                if (next == '$' || next == '`' || next == '"' || next == '\\' || next == '\n') ++i;
                continue;
            }
        }
        if (ch == '$' && i + 1 < text.size()) {
            const QChar next = text[i + 1];
            if (quote == Quote::None && next == '\'') { quote = Quote::Ansi; ++i; continue; }
            if (next == '{') {
                i += 2; while (i < text.size() && text[i] != '}') ++i;
                if (i == text.size()) return false;
            } else if (next == '(') {
                i += 2; int depth = 1;
                while (i < text.size() && depth) { if (text[i] == '(') ++depth; else if (text[i] == ')') --depth; if (depth) ++i; }
                if (depth) return false;
            }
        }
    }
    return quote == Quote::None;
}
struct FixedPathCapture {
    QDir root;
    qint64 remaining = ParserConfigurationLimits{}.maximumTotalUnits;
    bool unavailable = false;
    QString capture(const QString &path) {
        if (unavailable) return {};
        const auto maximumField = ParserConfigurationLimits{}.maximumFieldUnits;
        const qint64 upper = path.size() + (QDir::isAbsolutePath(path) ? 0 : qint64(root.path().size()) + 1);
        if (path.isEmpty() || driveRelative(path) || path.size() > maximumField || upper > remaining || upper > maximumField) {
            unavailable = true; return {};
        }
        const QString fixed = exactPathSpelling(root.absoluteFilePath(path));
        if (!qualifiedRoot(fixed) || !NativePathKey(fixed).valid()) { unavailable = true; return {}; }
        remaining -= fixed.size();
        return fixed;
    }
};
}
ParserConfigurationCapture captureParserConfiguration(CompilerSet *compiler, ParserConfigurationValues base,
    const QString &workingDirectory, QProcessEnvironment environment, QList<QByteArray> outputEncodings)
{
    const auto failed = [](const char *text) {
        return ParserConfigurationCapture{Compilation::PreparationResult::failure(
            QCoreApplication::translate("ParserConfiguration", text)), {}};
    };
    const auto *application = QCoreApplication::instance();
    if (!application || QThread::currentThread() != application->thread())
        return failed("Parser configuration must be captured on the GUI owner thread.");
    if (!qualifiedRoot(workingDirectory) || !NativePathKey(workingDirectory).valid())
        return failed("Parser configuration requires a fixed absolute working directory.");
    base.includePaths.clear(); base.hardDefineLines.clear(); base.compilerPreparationIdentity.clear();
    const auto initialError = ParserConfiguration::create(base)->validate();
    if (!initialError.isEmpty()) return {Compilation::PreparationResult::failure(initialError), {}};
    FixedPathCapture paths{QDir(workingDirectory)};
    const auto absolute = [&](const QString &path) { return paths.capture(path); };
    for (auto &path : base.projectIncludePaths) path = absolute(path);
    QSet<QString> files;
    for (const auto &path : base.projectFiles) files.insert(absolute(path));
    base.projectFiles = std::move(files);
    if (paths.unavailable) return failed("Parser paths exceed the preparation budget or have no fixed absolute identity.");
    ParserToolchainProbeValues values;
    values.base = std::move(base);
    values.outputEncodings = std::move(outputEncodings);
    if (!compiler || !values.base.enabled) {
        const auto error = ParserConfiguration::create(values.base)->validate();
        if (!error.isEmpty()) return {Compilation::PreparationResult::failure(error), {}};
        return {{}, ParserToolchainProbeRequest::create(std::move(values))};
    }
    values.configured = true;
    if (compiler->CppIncludeDirs().size() > 65536 || compiler->CIncludeDirs().size() > 65536 || compiler->binDirs().size() > 65536)
        return failed("Parser configuration exceeds its item limit.");
#ifdef ENABLE_SDCC
    if (compiler->compilerType() == CompilerType::SDCC) {
        values.kind = ParserToolchainKind::SDCC;
        values.base.language = ParserLanguage::SDCC;
    }
#endif
    if (compiler->useCustomCompileParams() && !boundedCustomText(compiler->customCompileParams()))
        return failed("Parser compiler options are malformed or exceed the preparation text or item budget.");
    if (compiler->CCompiler().isEmpty()) return failed("The configured compiler executable is unavailable.");
    const QString executable = absolute(compiler->CCompiler());
    if (paths.unavailable || !NativePathKey(executable).valid()) return failed("The configured compiler executable path is invalid.");
    if (values.base.language == ParserLanguage::CPlusPlus)
        for (const auto &path : compiler->CppIncludeDirs()) values.base.includePaths.append(absolute(path));
    for (const auto &path : compiler->CIncludeDirs()) values.base.includePaths.append(absolute(path));
    if (paths.unavailable) return failed("Parser paths exceed the preparation budget or have no fixed absolute identity.");
    values.base.includePaths.removeDuplicates();
    QStringList bins;
    for (const auto &path : compiler->binDirs()) bins.append(absolute(path));
    if (paths.unavailable) return failed("Parser paths exceed the preparation budget or have no fixed absolute identity.");
    if (!bins.isEmpty()) environment.insert("PATH", bins.join(QDir::listSeparator()) + QDir::listSeparator() + environment.value("PATH"));
    environment.insert("LC_ALL", "C"); environment.insert("LANG", "C");
    Compilation::ProcessProbeRequest command;
    command.executable = executable; command.workingDirectory = exactPathSpelling(workingDirectory);
    command.environment = std::move(environment); command.maximumOutputBytes = 2 * 1024 * 1024;
#ifdef Q_OS_WIN
    const QString nullFile = "NUL";
#else
    const QString nullFile = "/dev/null";
#endif
    const auto &options = compiler->compileOptions();
    const auto option = [&](const QString &key, bool includeValue) -> QStringList {
        const auto definition = CompilerInfoManager::getCompilerOption(compiler->compilerType(), key);
        if (!definition) return {};
        const QString value = options.value(key);
        if (includeValue) return value.isEmpty() ? QStringList{} : QStringList{definition->setting + value};
        return options.contains(key) ? QStringList{definition->setting} : QStringList{};
    };
    for (const auto &key : {QStringLiteral(CC_CMD_OPT_STD), QStringLiteral(C_CMD_OPT_STD),
                           QStringLiteral(SDCC_CMD_OPT_PROCESSOR), QStringLiteral(SDCC_CMD_OPT_STD)})
        if (options.value(key).size() > ParserConfigurationLimits{}.maximumFieldUnits)
            return failed("Parser compiler options exceed the preparation text budget.");
    QStringList languageFlags;
    if (values.kind == ParserToolchainKind::SDCC) {
        languageFlags += option(QStringLiteral(SDCC_CMD_OPT_PROCESSOR), true);
        languageFlags += option(QStringLiteral(SDCC_CMD_OPT_STD), true);
    } else {
        languageFlags += option(values.base.language == ParserLanguage::CPlusPlus ? QStringLiteral(CC_CMD_OPT_STD) : QStringLiteral(C_CMD_OPT_STD), true);
        languageFlags += option(QStringLiteral(CC_CMD_OPT_ENABLE_GCC_IMPORT_STD), false);
        languageFlags += option(QStringLiteral(CC_CMD_OPT_DEBUG_INFO), false);
    }
    if (compiler->useCustomCompileParams()) languageFlags += parseArgumentsWithoutVariables(compiler->customCompileParams());
    if (languageFlags.contains("-g3")) languageFlags.append("-D_DEBUG");
    const auto invalidArguments = validateParserProbeArguments(languageFlags);
    if (!invalidArguments.isEmpty()) return {Compilation::PreparationResult::failure(invalidArguments), {}};
    values.cIncludes = command;
    values.cIncludes.arguments = values.kind == ParserToolchainKind::SDCC
        ? QStringList{"--print-search-dirs"} + option(QStringLiteral(SDCC_CMD_OPT_PROCESSOR), true)
        : QStringList{"-E", "-x", "c", "-v", nullFile};
    values.cppIncludes = command;
    values.cppIncludes.arguments = {"-E", "-x", "c++", "-v", nullFile};
    values.hardDefines = command;
    values.hardDefines.arguments = {"-dM", "-E", "-x", values.base.language == ParserLanguage::CPlusPlus ? "c++" : "c"};
    if (values.kind == ParserToolchainKind::SDCC) values.hardDefines.arguments.append("-V");
    values.hardDefines.arguments += languageFlags; values.hardDefines.arguments.append(nullFile);
    const auto error = ParserConfiguration::create(values.base)->validate();
    if (!error.isEmpty()) return {Compilation::PreparationResult::failure(error), {}};
    return {{}, ParserToolchainProbeRequest::create(std::move(values))};
}
} // namespace Analysis
