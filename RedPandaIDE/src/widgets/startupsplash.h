// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef STARTUPSPLASH_H
#define STARTUPSPLASH_H
#include "../appidentity.h"
#include <QSplashScreen>
#include <QPainter>
#include <QApplication>
#include <QEvent>

// Uses the same artwork as the application and About; no external file access.
class StartupSplash : public QSplashScreen {
public:
    StartupSplash() : QSplashScreen(artwork(), Qt::WindowStaysOnTopHint) { qApp->installEventFilter(this); }
    void finish(QWidget *mainWindow) {
        qApp->removeEventFilter(this);
        QSplashScreen::finish(mainWindow);
    }
protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        // Startup warnings and recovery prompts must remain immediately accessible.
        if (event->type() == QEvent::Show) {
            auto *widget = qobject_cast<QWidget *>(object);
            if (widget && widget != this && widget->isWindow() && widget->isModal()) hide();
        }
        return QSplashScreen::eventFilter(object, event);
    }
private:
    static QPixmap artwork() {
        const qreal ratio = qApp->devicePixelRatio();
        QPixmap canvas(qRound(460 * ratio), qRound(340 * ratio));
        canvas.setDevicePixelRatio(ratio);
        canvas.fill(QColor("#142438"));
        QPainter painter(&canvas);
        QPixmap panda(AppIdentity::iconResource());
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawPixmap(QRect(125, 18, 210, 210), panda);
        painter.setPen(QColor("#f5f8ff"));
        QFont font = qApp->font(); font.setPointSize(22); font.setBold(true);
        painter.setFont(font);
        painter.drawText(QRect(12, 242, 436, 42), Qt::AlignCenter, AppIdentity::displayName());
        font.setPointSize(11); font.setBold(false); painter.setFont(font);
        painter.setPen(QColor("#b6d2ec"));
        painter.drawText(QRect(12, 294, 436, 28), Qt::AlignCenter,
                         QCoreApplication::translate("StartupSplash", "Loading development environment…"));
        return canvas;
    }
};
#endif
