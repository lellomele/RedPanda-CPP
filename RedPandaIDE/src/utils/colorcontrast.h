// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef UTILS_COLORCONTRAST_H
#define UTILS_COLORCONTRAST_H
#include <QColor>
namespace UiContrast {
constexpr qreal MinimumTextRatio = 4.5;
QColor composite(const QColor &foreground, const QColor &background);
qreal ratio(const QColor &foreground, const QColor &background);
QColor readableText(const QColor &preferred, const QColor &background);
}
#endif
