#ifndef UI_CHECKS_H
#define UI_CHECKS_H
#include <QString>
class MainWindow; class SessionRecovery;
int runUiChecks(MainWindow &window, SessionRecovery &recovery, const QString &outputDirectory);
#endif
