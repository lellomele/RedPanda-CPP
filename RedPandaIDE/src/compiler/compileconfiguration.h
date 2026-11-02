// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef COMPILECONFIGURATION_H
#define COMPILECONFIGURATION_H

#include "compilerinfo.h"
#include <QMap>
#include <QSet>
#include <QStringList>
#include <QProcessEnvironment>
#include <memory>
#include <optional>
#include <utility>

// Plain values only. This boundary deliberately has no Project, Editor,
// CompilerSet, parser, settings, callback or QObject ownership.
namespace Compilation {

enum class PreparationStatus { Ready, Pending, Conflict, Failed, Cancelled };
struct PreparationResult {
    PreparationStatus status = PreparationStatus::Ready;
    QString reason;
    bool ready() const { return status == PreparationStatus::Ready; }
    static PreparationResult pending(QString reason) { return {PreparationStatus::Pending, std::move(reason)}; }
    static PreparationResult conflict(QString reason) { return {PreparationStatus::Conflict, std::move(reason)}; }
    static PreparationResult failure(QString reason) { return {PreparationStatus::Failed, std::move(reason)}; }
};

struct ToolchainValues {
    QString identity;
    quint64 generation = 0;
    QString name, cCompiler, cppCompiler, make, resourceCompiler;
    QString target, dumpMachine, version;
    CompilerType compilerType = CompilerType::Unknown;
    QStringList binDirs, cIncludeDirs, cppIncludeDirs, libDirs;
    QMap<QString, QString> compileOptions;
    QMap<QString, CompilerOption> optionCatalogue;
    bool useCustomCompileParams = false, useCustomLinkParams = false;
    QString customCompileParams, customLinkParams;
    bool autoAddCharsetParams = false, staticLink = false;
    QString execCharset;
    QString executableSuffix, preprocessingSuffix, compilationProperSuffix, assemblingSuffix;
    bool supportNLS = false, supportConvertingCharset = false;
    bool compilerUsingUTF8 = true, makerUsingUTF8 = true;
    bool forceEnglishOutput = false;
    QMap<QString, QString> additionalPrograms;
};

struct PreferencesValues {
    bool noDebugDirectivesWhenGenerateASM = false;
    bool noSEHDirectivesWhenGenerateASM = false;
    bool intelASMDialect = false;
    bool gasLinkCStandardLib = true, nasmLinkCStandardLib = true;
    QString nasmPath;
    QByteArray systemEncoding, consoleEncoding;
};

// This is the subset used by single-file/stdin argument planning. Project build
// unit and dependency snapshots are a separate migration, not a fake Project.
struct ProjectOptionsValues {
    QString identity;
    QMap<QString, QString> compilerOptions;
    QString compilerCommand, cppCompilerCommand, linkerCommand;
    QStringList includeDirs, libDirs;
    bool guiApplication = false, staticLink = false;
};

struct AutolinkRuleValues {
    QString linkOption;
    bool execUseUTF8 = false;
};

struct AutolinkResolution {
    bool complete = true;
    QString error;
    QStringList linkOptions; // Keep rule order; parsing/expansion is a later step.
    bool forceExecUTF8 = false;
};

struct AutolinkValues {
    bool enabled = false;
    bool complete = true;
    QString analysisIdentity;
    QMap<QString, AutolinkRuleValues> rules;
    QMap<QString, QStringList> directIncludes;
    qsizetype nodeBudget = 100000;
    qsizetype edgeBudget = 1000000;

    AutolinkResolution resolve(const QString &root) const {
        AutolinkResolution result;
        if (!enabled) return result;
        if (!complete) {
            result.complete = false;
            result.error = QStringLiteral("Dependency analysis is not ready.");
            return result;
        }
        QSet<QString> visited;
        QStringList pending{root};
        qsizetype edges = 0;
        while (!pending.isEmpty()) {
            const QString file = pending.takeLast();
            if (visited.contains(file)) continue;
            if (visited.size() >= nodeBudget) {
                result.complete = false;
                result.error = QStringLiteral("Autolink dependency limit exceeded.");
                break;
            }
            visited.insert(file);
            auto rule = rules.constFind(file);
            if (rule == rules.cend()) {
                // Match the existing manager's exact-path, then suffix-key order.
                for (auto it = rules.cbegin(); it != rules.cend(); ++it) {
#ifdef Q_OS_WIN
                    constexpr auto sensitivity = Qt::CaseInsensitive;
#else
                    constexpr auto sensitivity = Qt::CaseSensitive;
#endif
                    if (file.endsWith(QLatin1Char('/') + it.key(), sensitivity)) {
                        rule = it;
                        break;
                    }
                }
            }
            if (rule != rules.cend()) {
                if (!rule->linkOption.isEmpty()) result.linkOptions.append(rule->linkOption);
                result.forceExecUTF8 = result.forceExecUTF8 || rule->execUseUTF8;
            }
            const auto children = directIncludes.constFind(file);
            if (children == directIncludes.cend()) continue;
            if (children->size() > edgeBudget - edges) {
                result.complete = false;
                result.error = QStringLiteral("Autolink dependency limit exceeded.");
                break;
            }
            edges += children->size();
            // Existing recursive behavior visits the last include first.
            // Iteration uses the stack to keep that order without call-stack risk.
            for (const auto &child : *children)
                if (!visited.contains(child)) pending.append(child);
        }
        if (!result.complete) {
            // A partial link plan must never look like a usable success.
            result.linkOptions.clear();
            result.forceExecUTF8 = false;
        }
        return result;
    }
};

struct ConfigurationValues {
    ToolchainValues toolchain;
    PreferencesValues preferences;
    std::optional<ProjectOptionsValues> project;
    AutolinkValues autolink;
    QMap<QString, QString> macros;
    QProcessEnvironment environment;

    QMap<QString, QString> effectiveCompileOptions(bool suppressDebug = false) const {
        auto result = project && !project->compilerOptions.isEmpty()
                ? project->compilerOptions : toolchain.compileOptions;
        if (suppressDebug) result.remove(QStringLiteral(CC_CMD_OPT_DEBUG_INFO));
        return result;
    }
};

class FrozenConfiguration final {
public:
    using Pointer = std::shared_ptr<const FrozenConfiguration>;
    static Pointer capture(const ConfigurationValues &source) {
        // Copy the value graph. Copying a shared_ptr<CompilerSet> or a map of
        // shared_ptr<Autolink> here would leave mutable aliases across threads.
        return Pointer(new FrozenConfiguration(source));
    }
    const ConfigurationValues &values() const { return mValues; }
private:
    explicit FrozenConfiguration(const ConfigurationValues &source) : mValues(source) {}
    const ConfigurationValues mValues;
};

} // namespace Compilation
#endif
