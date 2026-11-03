// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef COMPILATIONCAPTURE_H
#define COMPILATIONCAPTURE_H
#include "compileconfiguration.h"
#include "../settings.h"
#include "../project.h"
#include "../autolinkmanager.h"
#include "../documentstore.h"
#include "../systemconsts.h"
#include "qt_utils/charsetinfo.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QThread>

namespace Compilation {
struct ConfigurationCapture {
    PreparationResult preparation;
    ConfigurationValues values;
};

// The sole GUI adapter: copy all mutable settings/catalogues now. Capability
// getters are deliberately absent because they launch lazy external probes.
inline ConfigurationCapture captureCompilerConfiguration(const PCompilerSet &set, Project *project,
                                                          const QMap<QString, QString> &macros, bool autolinkUsed = true)
{
    ConfigurationCapture result;
    const auto *app = QCoreApplication::instance();
    if (!app || QThread::currentThread() != app->thread()) {
        result.preparation = PreparationResult::failure(QStringLiteral("Compiler configuration must be captured on the GUI owner thread."));
        return result;
    }
    if (!set) {
        result.preparation = PreparationResult::failure(QStringLiteral("The selected compiler configuration is no longer available."));
        return result;
    }
    auto &values = result.values;
    auto &toolchain = values.toolchain;
    toolchain.name = set->name(); toolchain.cCompiler = set->CCompiler(); toolchain.cppCompiler = set->cppCompiler();
    toolchain.make = set->make(); toolchain.resourceCompiler = set->resourceCompiler();
    for (QString *program : {&toolchain.cCompiler, &toolchain.cppCompiler, &toolchain.make, &toolchain.resourceCompiler})
        if (!program->isEmpty()) *program = QFileInfo(*program).absoluteFilePath();
    toolchain.target = set->target(); toolchain.dumpMachine = set->dumpMachine(); toolchain.version = set->version();
    toolchain.compilerType = set->compilerType(); toolchain.binDirs = set->binDirs();
    toolchain.cIncludeDirs = set->CIncludeDirs(); toolchain.cppIncludeDirs = set->CppIncludeDirs(); toolchain.libDirs = set->libDirs();
    toolchain.compileOptions = set->compileOptions();
    for (const auto &option : CompilerInfoManager::getCompilerOptions(toolchain.compilerType))
        if (option) toolchain.optionCatalogue.insert(option->key, *option);
    toolchain.useCustomCompileParams = set->useCustomCompileParams(); toolchain.useCustomLinkParams = set->useCustomLinkParams();
    toolchain.customCompileParams = set->customCompileParams(); toolchain.customLinkParams = set->customLinkParams();
    toolchain.autoAddCharsetParams = set->autoAddCharsetParams(); toolchain.staticLink = set->staticLink();
    toolchain.execCharset = set->execCharset(); toolchain.executableSuffix = set->executableSuffix();
    toolchain.preprocessingSuffix = set->preprocessingSuffix(); toolchain.compilationProperSuffix = set->compilationProperSuffix();
    toolchain.assemblingSuffix = set->assemblingSuffix(); toolchain.forceEnglishOutput = set->forceEnglishOutput();
#ifdef ENABLE_SDCC
    if (toolchain.compilerType == CompilerType::SDCC) {
        toolchain.additionalPrograms.insert(PACKIHX_PROGRAM, set->findProgramInBinDirs(PACKIHX_PROGRAM));
        toolchain.additionalPrograms.insert(MAKEBIN_PROGRAM, set->findProgramInBinDirs(MAKEBIN_PROGRAM));
    }
#endif
    // Cache identity is tied to the selected paths and observable binary stamps.
    // It is not a source/configuration freshness certificate for the parser.
    QByteArray fingerprint;
    for (const QString &path : {toolchain.cCompiler, toolchain.cppCompiler}) {
        const QFileInfo info(path);
        fingerprint += info.absoluteFilePath().toUtf8() + '\0' + QByteArray::number(info.size()) + ':'
            + QByteArray::number(info.lastModified().toMSecsSinceEpoch()) + '\0';
    }
    toolchain.identity = QString::fromLatin1(QCryptographicHash::hash(fingerprint, QCryptographicHash::Sha256).toHex());
    auto &preferences = values.preferences;
    preferences.noDebugDirectivesWhenGenerateASM = pSettings->languages().noDebugDirectivesWhenGenerateASM();
    preferences.noSEHDirectivesWhenGenerateASM = pSettings->languages().noSEHDirectivesWhenGenerateASM();
    preferences.intelASMDialect = pSettings->languages().x86DialectOfASMGenerated() == LanguageSettings::X86ASMDialect::Intel;
    preferences.gasLinkCStandardLib = pSettings->compile().GASLinkCStandardLib();
    preferences.nasmLinkCStandardLib = pSettings->compile().NASMLinkCStandardLib(); preferences.nasmPath = pSettings->compile().NASMPath();
    if (!preferences.nasmPath.isEmpty()) preferences.nasmPath = QFileInfo(preferences.nasmPath).absoluteFilePath();
    preferences.systemEncoding = pCharsetInfoManager->getDefaultSystemEncoding();
    preferences.consoleEncoding = pCharsetInfoManager->getDefaultConsoleEncoding();
    values.macros = macros;
    values.environment = QProcessEnvironment::systemEnvironment();
    if (project) {
        const auto &source = project->options();
        values.project.emplace();
        values.project->identity = project->filename(); values.project->compilerOptions = source.compilerOptions;
        values.project->compilerCommand = source.compilerCmd; values.project->cppCompilerCommand = source.cppCompilerCmd;
        values.project->linkerCommand = source.linkerCmd; values.project->includeDirs = source.includeDirs;
        values.project->libDirs = source.libDirs; values.project->guiApplication = source.type == ProjectType::GUI;
        values.project->staticLink = source.staticLink;
    }
    values.autolink.enabled = autolinkUsed && pSettings->editor().enableAutolink();
    values.autolink.complete = !values.autolink.enabled;
    if (values.autolink.enabled && pAutolinkManager) {
        qint64 characters = 0;
        for (auto it = pAutolinkManager->links().cbegin(); it != pAutolinkManager->links().cend(); ++it) {
            if (!it.value()) continue;
            if (values.autolink.rules.size() >= 4096 || it.key().size() + it.value()->linkOption.size() > 4 * 1024 * 1024 - characters) {
                result.preparation = PreparationResult::failure(QStringLiteral("Autolink configuration exceeds the preparation budget."));
                return result;
            }
            characters += it.key().size() + it.value()->linkOption.size();
            const auto key = QDir::isAbsolutePath(it.key()) ? AnalysisInputSnapshot::pathKey(it.key()) : QDir::fromNativeSeparators(it.key());
            values.autolink.rules.insert(key, {it.value()->linkOption, it.value()->execUseUTF8});
        }
    }
    return result;
}
} // namespace Compilation
#endif
