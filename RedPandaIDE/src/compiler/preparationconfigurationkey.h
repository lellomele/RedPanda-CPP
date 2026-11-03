// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PREPARATIONCONFIGURATIONKEY_H
#define PREPARATIONCONFIGURATIONKEY_H
#include "compileconfiguration.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QIODevice>

namespace Compilation {
// Transitional preparation guard, not a parser freshness certificate. Derived
// probe facts, dependency graphs and display labels are deliberately excluded.
// The caller supplies the original captured macros, so clock/cursor changes do
// not falsely invalidate an accepted job while it waits for dependencies.
inline QByteArray preparationConfigurationKey(const ConfigurationValues &values)
{
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_0);
    const auto &t = values.toolchain;
    out << t.identity << t.generation << t.cCompiler << t.cppCompiler << t.make << t.resourceCompiler
        << t.target << t.dumpMachine << t.version << qint32(t.compilerType)
        << t.binDirs << t.cIncludeDirs << t.cppIncludeDirs << t.libDirs << t.compileOptions
        << t.useCustomCompileParams << t.useCustomLinkParams << t.customCompileParams << t.customLinkParams
        << t.autoAddCharsetParams << t.staticLink << t.execCharset << t.executableSuffix
        << t.preprocessingSuffix << t.compilationProperSuffix << t.assemblingSuffix << t.forceEnglishOutput
        << t.additionalPrograms << quint64(t.optionCatalogue.size());
    for (auto it = t.optionCatalogue.cbegin(); it != t.optionCatalogue.cend(); ++it) {
        out << it.key() << it->isC << it->isCpp << it->isLinker << it->setting << qint32(it->type);
        if (it->type == CompilerOptionType::Number) out << it->scale;
    }
    const auto &p = values.preferences;
    out << p.noDebugDirectivesWhenGenerateASM << p.noSEHDirectivesWhenGenerateASM << p.intelASMDialect
        << p.gasLinkCStandardLib << p.nasmLinkCStandardLib << p.nasmPath << p.systemEncoding << p.consoleEncoding;
    out << bool(values.project);
    if (values.project) {
        const auto &project = *values.project;
        out << project.identity << project.compilerOptions << project.compilerCommand << project.cppCompilerCommand
            << project.linkerCommand << project.includeDirs << project.libDirs << project.guiApplication << project.staticLink;
    }
    out << values.autolink.enabled << qint64(values.autolink.nodeBudget) << qint64(values.autolink.edgeBudget)
        << qint64(values.autolink.ruleMatchBudget) << qint64(values.autolink.ruleSuffixCodeUnitBudget)
        << quint64(values.autolink.rules.size());
    for (auto it = values.autolink.rules.cbegin(); it != values.autolink.rules.cend(); ++it)
        out << it.key() << it->linkOption << it->execUseUTF8;
    auto environment = values.environment.toStringList();
    environment.sort();
    out << values.macros << environment;
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}
} // namespace Compilation
#endif
