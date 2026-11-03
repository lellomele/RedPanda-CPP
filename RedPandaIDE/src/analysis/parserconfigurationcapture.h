// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ANALYSIS_PARSERCONFIGURATIONCAPTURE_H
#define ANALYSIS_PARSERCONFIGURATIONCAPTURE_H
#include "parsertoolchainprobe.h"
class CompilerSet;

namespace Analysis {
struct ParserConfigurationCapture {
    Compilation::PreparationResult preparation;
    ParserToolchainProbeRequest::Pointer request;
};

// GUI-only adapter. All non-lazy settings reads finish here. The returned value
// has no CompilerSet, settings, Project, Editor, callback or QObject ownership.
ParserConfigurationCapture captureParserConfiguration(CompilerSet *compiler,
    ParserConfigurationValues base, const QString &workingDirectory,
    QProcessEnvironment environment, QList<QByteArray> outputEncodings = {QByteArrayLiteral("UTF-8")});
} // namespace Analysis
#endif
