// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef RECOVERYCHECKS_H
#define RECOVERYCHECKS_H
#include <QString>
class MainWindow;
class SessionRecovery;
int runRecoveryChecks(MainWindow &window, SessionRecovery &recovery, const QString &outputDirectory);
#endif
