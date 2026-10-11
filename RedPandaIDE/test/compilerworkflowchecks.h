// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef COMPILER_WORKFLOW_CHECKS_H
#define COMPILER_WORKFLOW_CHECKS_H
#include <QString>
#include <functional>
class MainWindow;
void runCompilerWorkflowChecks(MainWindow &window, const QString &outputDirectory,
                               const std::function<void(const QString &, bool)> &check);
#endif
