// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef DIAGNOSTICCATALOG_H
#define DIAGNOSTICCATALOG_H
#include <QString>
namespace DiagnosticCatalog {
struct Translation { QString text; QString ruleId; bool matched() const { return !ruleId.isEmpty(); } };
Translation translate(const QString &original, const QString &language);
bool isItalianInterface();
int ruleCount();
}
#endif
