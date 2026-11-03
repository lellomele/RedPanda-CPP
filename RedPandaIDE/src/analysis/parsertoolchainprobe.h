// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ANALYSIS_PARSERTOOLCHAINPROBE_H
#define ANALYSIS_PARSERTOOLCHAINPROBE_H
#include "parserconfiguration.h"
#include "../compiler/boundedprocessprobe.h"

namespace Analysis {
enum class ParserToolchainKind { GccLike, SDCC };
struct ParserToolchainProbeValues {
    ParserConfigurationValues base;
    bool configured = false;
    ParserToolchainKind kind = ParserToolchainKind::GccLike;
    Compilation::ProcessProbeRequest cIncludes, cppIncludes, hardDefines;
    QList<QByteArray> outputEncodings{QByteArrayLiteral("UTF-8")};
    int maximumTotalMilliseconds = 10000;
};
class ParserToolchainProbeRequest final {
public:
    using Pointer = std::shared_ptr<const ParserToolchainProbeRequest>;
    static Pointer create(ParserToolchainProbeValues values) {
        return Pointer(new ParserToolchainProbeRequest(std::move(values)));
    }
    const PParserConfiguration base;
    const bool configured;
    const ParserToolchainKind kind;
    const Compilation::ProcessProbeRequest cIncludes, cppIncludes, hardDefines;
    const QList<QByteArray> outputEncodings;
    const int maximumTotalMilliseconds;
    QByteArray identity() const;
private:
    explicit ParserToolchainProbeRequest(ParserToolchainProbeValues values)
        : base(ParserConfiguration::create(std::move(values.base))), configured(values.configured), kind(values.kind),
          cIncludes(std::move(values.cIncludes)), cppIncludes(std::move(values.cppIncludes)),
          hardDefines(std::move(values.hardDefines)), outputEncodings(std::move(values.outputEncodings)),
          maximumTotalMilliseconds(values.maximumTotalMilliseconds) {}
};
struct ParserToolchainDiscovery {
    Compilation::PreparationResult preparation;
    PParserConfiguration configuration;
    Compilation::ProcessOwnershipLevel ownership = Compilation::ProcessOwnershipLevel::NoProcessStarted;
};

// Preparation is unavailable for known file-output/dependency flags and
// malformed operands. Normal compilation retains the user's original flags.
QString validateParserProbeArguments(const QStringList &arguments);

// Called only by a bounded preparation worker. It owns no application objects.
ParserToolchainDiscovery discoverParserToolchain(const ParserToolchainProbeRequest &request,
                                               const std::atomic_bool &cancelled);
} // namespace Analysis
#endif
