// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef DOCUMENTSTORECHECKS_H
#define DOCUMENTSTORECHECKS_H
#include <QString>
class MainWindow;
int runDocumentStoreChecks(MainWindow &window, const QString &outputDirectory);
#endif
