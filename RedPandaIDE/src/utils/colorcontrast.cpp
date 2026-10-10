// SPDX-License-Identifier: GPL-3.0-or-later
#include "colorcontrast.h"
#include <algorithm>
#include <cmath>
namespace {
qreal luminance(const QColor &color) {
    auto linear = [](int channel) { const qreal value = channel / 255.0; return value <= .04045 ? value / 12.92 : std::pow((value + .055) / 1.055, 2.4); };
    return .2126 * linear(color.red()) + .7152 * linear(color.green()) + .0722 * linear(color.blue());
}
QColor blend(const QColor &start, const QColor &end, qreal amount) {
    return QColor(qRound(start.red() + (end.red() - start.red()) * amount),
                  qRound(start.green() + (end.green() - start.green()) * amount),
                  qRound(start.blue() + (end.blue() - start.blue()) * amount));
}
int distance(const QColor &a, const QColor &b) {
    const int r = a.red() - b.red(), g = a.green() - b.green(), blue = a.blue() - b.blue();
    return r * r + g * g + blue * blue;
}
}
QColor UiContrast::composite(const QColor &foreground, const QColor &background) {
    const QColor base = background.isValid() ? background : QColor(Qt::white);
    if (!foreground.isValid()) return QColor(base.red(), base.green(), base.blue());
    return blend(QColor(base.red(), base.green(), base.blue()), QColor(foreground.red(), foreground.green(), foreground.blue()), foreground.alphaF());
}
qreal UiContrast::ratio(const QColor &foreground, const QColor &background) {
    if (!foreground.isValid() || !background.isValid()) return 0;
    const QColor base = composite(background, QColor(Qt::white));
    const qreal a = luminance(composite(foreground, base)), b = luminance(base);
    return (std::max(a, b) + .05) / (std::min(a, b) + .05);
}
QColor UiContrast::readableText(const QColor &preferred, const QColor &background) {
    const QColor base = composite(background, QColor(Qt::white));
    const QColor black(Qt::black), white(Qt::white);
    if (!preferred.isValid()) return ratio(black, base) >= ratio(white, base) ? black : white;
    const QColor start = composite(preferred, base);
    if (ratio(start, base) >= MinimumTextRatio) return start;
    QColor best; int bestDistance = 3 * 255 * 255 + 1;
    for (const QColor &end : {black, white}) {
        if (ratio(end, base) < MinimumTextRatio) continue;
        qreal lower = 0, upper = 1;
        for (int iteration = 0; iteration < 20; ++iteration) {
            const qreal middle = (lower + upper) / 2;
            if (ratio(blend(start, end, middle), base) >= MinimumTextRatio) upper = middle;
            else lower = middle;
        }
        QColor candidate = blend(start, end, upper);
        if (ratio(candidate, base) < MinimumTextRatio) candidate = end;
        const int change = distance(start, candidate);
        if (change < bestDistance) { bestDistance = change; best = candidate; }
    }
    // One of opaque black/white always meets 4.5:1 for an opaque sRGB background.
    return best.isValid() ? best : (ratio(black, base) >= ratio(white, base) ? black : white);
}
