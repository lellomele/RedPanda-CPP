// SPDX-License-Identifier: GPL-3.0-or-later
#include "parsertoolchainprobe.h"
#include "../pathidentity.h"
#include "../utils/parsearg.h"
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QStringDecoder>
#include <algorithm>
#include <optional>

namespace Analysis {
namespace {
QString message(const char *text) { return QCoreApplication::translate("ParserConfiguration", text); }
std::optional<QString> decoded(const QByteArray &bytes, const QList<QByteArray> &encodings) {
    for (const auto &encoding : encodings) {
        QStringDecoder decoder(encoding.constData(), QStringConverter::Flag::Stateless);
        if (!decoder.isValid()) continue;
        const QString text = decoder.decode(bytes);
        if (!decoder.hasError() && text.isValidUtf16() && !text.contains(QChar::Null)) return text;
    }
    return {};
}
void appendUnique(QStringList &target, const QStringList &source) {
    QSet<QString> seen(target.cbegin(), target.cend());
    for (const auto &path : source) if (!seen.contains(path)) { target.append(path); seen.insert(path); }
}
std::optional<QStringList> includeDirectories(const QString &text, ParserToolchainKind kind, const QString &root) {
    if (text.count('\n') > 65536) return {};
    QStringList paths;
    bool inList = false, complete = false;
    for (const auto &line : text.split('\n')) {
        const QString trimmed = line.trimmed();
        if (kind == ParserToolchainKind::SDCC) {
            if (trimmed == "includedir:") { inList = true; continue; }
            if (inList && trimmed == "libdir:") { complete = true; break; }
        } else {
            if (trimmed == "#include <...> search starts here:") { inList = true; continue; }
            if (inList && trimmed == "End of search list.") { complete = true; break; }
        }
        if (!inList || trimmed.isEmpty()) continue;
        QString path = trimmed;
        if (path.endsWith(" (framework directory)")) path.chop(QStringLiteral(" (framework directory)").size());
        if (!NativePathKey(path).valid()) return {};
        paths.append(exactPathSpelling(QDir(root).absoluteFilePath(path)));
    }
    return complete ? std::optional<QStringList>{std::move(paths)} : std::nullopt;
}
std::optional<QStringList> definitions(const QString &text, ParserToolchainKind kind) {
    if (text.count('\n') > 65536) return {};
    QStringList result;
    if (kind == ParserToolchainKind::SDCC) {
        for (const auto &line : text.split('\n')) {
            if (!line.trimmed().startsWith('+')) continue;
            for (const auto &argument : parseArgumentsWithoutVariables(line.trimmed().mid(1))) {
                if (!argument.startsWith("-D")) continue;
                const QString definition = argument.mid(2);
                if (definition.isEmpty()) return {};
                const auto equals = definition.indexOf('=');
                result.append(equals < 0 ? "#define " + definition
                    : "#define " + definition.left(equals) + ' ' + definition.mid(equals + 1));
            }
            return result.isEmpty() ? std::nullopt : std::optional<QStringList>{std::move(result)};
        }
        return {};
    }
    for (const auto &line : text.split('\n')) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) continue;
        if (!trimmed.startsWith("#define ")) return {};
        result.append(trimmed);
    }
    return result.isEmpty() ? std::nullopt : std::optional<QStringList>{std::move(result)};
}
} // namespace

QString validateParserProbeArguments(const QStringList &arguments)
{
    if (arguments.size() > 65536)
        return message("Parser assistance is unavailable because compiler arguments exceed the item budget.");
    qint64 remainingArgumentUnits = 4LL * 1024 * 1024;
    for (const auto &argument : arguments) {
        if (!argument.isValidUtf16() || argument.contains(QChar::Null) || argument.size() > 1024 * 1024
                || argument.size() > remainingArgumentUnits)
            return message("Parser assistance is unavailable because compiler arguments are invalid or exceed the text budget.");
        remainingArgumentUnits -= argument.size();
    }
    const QSet<QString> operands{"-D", "-U", "-I", "-L", "-l", "-x", "-B", "-isystem", "-iquote",
        "-idirafter", "-include", "-imacros", "-std", "--std", "--param", "-o", "--output", "-MF", "-MT", "-MQ", "-MJ"};
    const auto fileOutput = [](const QString &arg) {
        return arg.startsWith("-o") || arg == "--output" || arg.startsWith("--output=")
            || arg.startsWith("-MF") || arg.startsWith("-MT") || arg.startsWith("-MQ") || arg.startsWith("-MJ")
            || arg == "-M" || arg == "-MM" || arg == "-MD" || arg == "-MMD" || arg == "-MP" || arg == "-MG"
            || arg == "-save-temps" || arg.startsWith("-save-temps=") || arg.startsWith("-fdump-")
            || arg.startsWith("-ftime-trace") || arg.startsWith("-fcallgraph-info") || arg == "-fstack-usage"
            || arg.startsWith("-fdiagnostics-add-output") || arg.startsWith("-fdiagnostics-set-output")
            || arg.startsWith("-fdiagnostics-file") || arg == "-fdiagnostics-format=json-file" || arg == "-fdiagnostics-format=sarif-file";
    };
    for (qsizetype i = 0; i < arguments.size(); ++i) {
        const auto &arg = arguments[i];
        if (operands.contains(arg) && (i + 1 == arguments.size() || arguments[i + 1].isEmpty() || arguments[i + 1].startsWith('-')))
            return message("Parser assistance is unavailable because compiler flag '%1' has a missing or invalid operand. Normal compilation keeps the configured options.").arg(arg);
        bool unsafe = fileOutput(arg);
        if (arg.startsWith("-Wp,")) {
            const auto forwarded = arg.mid(4).split(',');
            for (const auto &item : forwarded) unsafe |= fileOutput(item);
        }
        if (unsafe)
            return message("Parser assistance is unavailable because compiler flag '%1' may write files during discovery. Remove that flag from this context to enable assistance; normal compilation remains available.").arg(arg);
        if (operands.contains(arg)) ++i;
    }
    return {};
}

QByteArray ParserToolchainProbeRequest::identity() const
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArrayView("RedPanda.ParserToolchainProbe.v1"));
    const auto add = [&](const QByteArray &value) {
        const QByteArray length = QByteArray::number(value.size()) + ':';
        hash.addData(length); hash.addData(value);
    };
    add(base->digest()); add(QByteArray::number(configured)); add(QByteArray::number(int(kind)));
    for (const auto *command : {&cIncludes, &cppIncludes, &hardDefines}) {
        add(command->executable.toUtf8()); add(command->workingDirectory.toUtf8());
        add(QByteArray::number(command->arguments.size()));
        for (const auto &argument : command->arguments) add(argument.toUtf8());
        QStringList variables = command->environment.toStringList(); std::sort(variables.begin(), variables.end());
        add(QByteArray::number(variables.size()));
        for (const auto &variable : variables) add(variable.toUtf8());
    }
    for (const auto &encoding : outputEncodings) add(encoding);
    return hash.result();
}

ParserToolchainDiscovery discoverParserToolchain(const ParserToolchainProbeRequest &request, const std::atomic_bool &cancelled)
{
    ParserToolchainDiscovery result;
    const auto failed = [&](QString text) {
        result.preparation = Compilation::PreparationResult::failure(std::move(text));
        result.configuration.reset(); return result;
    };
    if (cancelled.load(std::memory_order_acquire)) { result.preparation.status = Compilation::PreparationStatus::Cancelled; return result; }
    const auto invalid = request.base->validate();
    if (!invalid.isEmpty()) return failed(invalid);
    if (request.maximumTotalMilliseconds <= 0 || request.maximumTotalMilliseconds > 60000
            || request.outputEncodings.isEmpty() || request.outputEncodings.size() > 4)
        return failed(message("Invalid parser toolchain preparation limits."));
    if (!request.configured || !request.base->enabled) { result.configuration = request.base; return result; }
    for (const auto *command : {&request.cIncludes, &request.cppIncludes, &request.hardDefines}) {
        const auto invalidArguments = validateParserProbeArguments(command->arguments);
        if (!invalidArguments.isEmpty()) return failed(invalidArguments);
    }
    ParserConfigurationValues config = request.base->values();
    QStringList cIncludes, cppIncludes;
    QElapsedTimer elapsed; elapsed.start();
    const auto probe = [&](const Compilation::ProcessProbeRequest &command) -> std::optional<QString> {
        auto bounded = command;
        bounded.deadlineMilliseconds = qMin(command.deadlineMilliseconds,
            qMax(0, request.maximumTotalMilliseconds - int(elapsed.elapsed())));
        if (bounded.deadlineMilliseconds <= 0) {
            result.preparation = Compilation::PreparationResult::failure(message("Parser toolchain preparation deadline expired."));
            return {};
        }
        if (bounded.maximumOutputBytes < 0 || bounded.maximumOutputBytes > 2 * 1024 * 1024) {
            result.preparation = Compilation::PreparationResult::failure(message("Invalid parser toolchain output limit.")); return {};
        }
        const auto queried = Compilation::runBoundedProcessProbe(bounded, cancelled);
        if (queried.ownership != Compilation::ProcessOwnershipLevel::NoProcessStarted)
            result.ownership = queried.ownership;
        result.preparation = queried.preparation;
        if (!queried.preparation.ready()) return {};
        const auto text = decoded(queried.output, request.outputEncodings);
        if (!text) result.preparation = Compilation::PreparationResult::failure(message("Parser toolchain output could not be decoded."));
        return text;
    };
    const auto cText = probe(request.cIncludes);
    if (!cText) return result;
    const auto cPaths = includeDirectories(*cText, request.kind, request.cIncludes.workingDirectory);
    if (!cPaths) return failed(message("The compiler did not provide a complete include search list."));
    cIncludes = *cPaths;
    if (config.language == ParserLanguage::CPlusPlus) {
        const auto cppText = probe(request.cppIncludes); if (!cppText) return result;
        const auto cppPaths = includeDirectories(*cppText, request.kind, request.cppIncludes.workingDirectory);
        if (!cppPaths) return failed(message("The compiler did not provide a complete C++ include search list."));
        cppIncludes = *cppPaths;
    }
    const auto defineText = probe(request.hardDefines); if (!defineText) return result;
    const auto defines = definitions(*defineText, request.kind);
    if (!defines) return failed(message("The compiler did not provide a complete predefined macro list."));
    appendUnique(config.includePaths, cppIncludes); appendUnique(config.includePaths, cIncludes);
    config.hardDefineLines = *defines;
    config.hardDefineLines.append({"#define __FILE__  1", "#define __LINE__  1", "#define __DATE__  1", "#define __TIME__  1"});
    config.compilerPreparationIdentity = request.identity();
    auto frozen = ParserConfiguration::create(std::move(config));
    const auto error = frozen->validate(); if (!error.isEmpty()) return failed(error);
    if (cancelled.load(std::memory_order_acquire)) { result.preparation.status = Compilation::PreparationStatus::Cancelled; return result; }
    result.configuration = std::move(frozen);
    return result;
}
} // namespace Analysis
