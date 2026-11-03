// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ANALYSIS_PARSERCONFIGURATION_H
#define ANALYSIS_PARSERCONFIGURATION_H
#include "../parser/parserutils.h"
#include <QByteArray>
#include <QStringList>
#include <QSet>
#include <memory>
#include <utility>

namespace Analysis {

// Construction input is an ordinary value. Publication copies/moves these
// containers into const fields; no mutable shared alias can update a job.
struct ParserConfigurationValues {
    ParserLanguage language = ParserLanguage::CPlusPlus;
    bool enabled = true;
    bool includeOnce = false;
    QStringList includePaths;
    QStringList projectIncludePaths;
    QStringList hardDefineLines;
    QSet<QString> projectFiles;
    QByteArray compilerPreparationIdentity;
};

struct ParserConfigurationLimits {
    qsizetype maximumIncludePaths = 65536;
    qsizetype maximumHardDefines = 65536;
    qsizetype maximumProjectFiles = 100000;
    qsizetype maximumFieldUnits = 1024 * 1024;
    qint64 maximumTotalUnits = 32LL * 1024 * 1024;
};

class ParserConfiguration final {
public:
    using Pointer = std::shared_ptr<const ParserConfiguration>;
    static Pointer create(ParserConfigurationValues values) {
        return Pointer(new ParserConfiguration(std::move(values)));
    }
    const ParserLanguage language;
    const bool enabled;
    const bool includeOnce;
    const QStringList includePaths;
    const QStringList projectIncludePaths;
    const QStringList hardDefineLines;
    const QSet<QString> projectFiles;
    const QByteArray compilerPreparationIdentity;

    // A digest proves value equivalence, never request temporal authority.
    QByteArray digest() const;
    QString validate(const ParserConfigurationLimits &limits = {}) const;
    ParserConfigurationValues values() const;
private:
    explicit ParserConfiguration(ParserConfigurationValues values)
        : language(values.language), enabled(values.enabled), includeOnce(values.includeOnce),
          includePaths(std::move(values.includePaths)), projectIncludePaths(std::move(values.projectIncludePaths)),
          hardDefineLines(std::move(values.hardDefineLines)), projectFiles(std::move(values.projectFiles)),
          compilerPreparationIdentity(std::move(values.compilerPreparationIdentity)) {}
};
using PParserConfiguration = ParserConfiguration::Pointer;

} // namespace Analysis
#endif
