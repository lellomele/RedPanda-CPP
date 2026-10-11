/*
 * Copyright (C) 2020-2022 Roy Qu (royqh1979@gmail.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "aboutdialog.h"
#include "../appidentity.h"
#include "ui_aboutdialog.h"
#include "../systemconsts.h"
#include "../utils/os.h"
#include "../utils.h"
#include "../settings.h"
#include <QDebug>
#include <QEvent>
#include <QPainter>
#include <QSvgRenderer>

AboutDialog::AboutDialog(QWidget *parent) :
    QDialog{parent},
    ui{new Ui::AboutDialog}
{
    setWindowFlag(Qt::WindowContextHelpButtonHint,false);
    ui->setupUi(this);
    mUiReady = true;
    ui->lblForkCopyright->setText(AppIdentity::forkCopyright());
    ui->lblUpstreamScope->setText(tr("Original Red Panda C++ / QSynEdit: Roy Qu (royqh1979) and contributors. Inherited editor, C/C++ support and shared Qt infrastructure."));
    ui->lblForkScope->setText(tr("Fork enhancements by Raffaele Mele: Italian localization, themes and icons, snippets, diagnostic translation, document recovery and stability improvements. Third-party components retain their own copyrights and licenses."));
    ui->lblForkCopyright->installEventFilter(this);
    ui->lblForkFlag->setAccessibleName(tr("Italian flag"));
    updateForkFlag();
    const qreal ratio = devicePixelRatioF();
    QPixmap artwork(AppIdentity::iconResource());
    artwork = artwork.scaled(qRound(192 * ratio), qRound(192 * ratio), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    artwork.setDevicePixelRatio(ratio);
    ui->lblAppArtwork->setPixmap(artwork);
    ui->lblAppArtwork->setFixedSize(208, 208);
    ui->lblAppArtwork->setAccessibleName(AppIdentity::displayName());
    ui->lblTitle->setText(QString("<h1>%1</h1>%2%3").arg(AppIdentity::displayName(), tr("Version: "), QStringLiteral(REDPANDA_CPP_VERSION)));

    QString buildArch = appArch();
#if defined(__x86_64__) && defined(ENABLE_GLIBC_HWCAPS)
# if defined(__AVX512__)
    buildArch += tr(", μarch level v4");
# elif defined(__AVX2__)
    buildArch += tr(", μarch level v3");
# elif defined(__SSE4_2__)
    buildArch += tr(", μarch level v2");
# else
    buildArch += tr(", baseline");
# endif
#endif

#if defined(__clang__) // Clang always pretends to be GCC/MSVC. Check it first.
# if defined(_MSC_VER)
    QString templ = "Clang %1.%2.%3 %4 MSVC ABI";
# elif defined(__apple_build_version__)
    QString templ = "Apple Clang %1.%2.%3 %4";
# else
    QString templ = "Clang %1.%2.%3 %4";
# endif
    ui->lblQt->setText(ui->lblQt->text()
                       .arg(qVersion())
                       .arg(templ
                            .arg(__clang_major__)
                            .arg(__clang_minor__)
                            .arg(__clang_patchlevel__)
                            .arg(buildArch))
                       .arg(osArch()));
#elif defined(__GNUC__)
    ui->lblQt->setText(ui->lblQt->text()
            .arg(qVersion())
            .arg(QString("GCC %1.%2.%3 %4")
                 .arg(__GNUC__)
                 .arg(__GNUC_MINOR__)
                 .arg(__GNUC_PATCHLEVEL__)
                 .arg(buildArch))
            .arg(osArch()));
#elif defined(_MSC_VER)
    ui->lblQt->setText(ui->lblQt->text()
            .arg(qVersion())
            .arg(QStringLiteral("MSVC %1.%2 %3")
                .arg(_MSC_VER / 100)
                .arg(_MSC_VER % 100)
                .arg(buildArch))
            .arg(osArch()));
#else
    ui->lblQt->setText(ui->lblQt->text()
            .arg(qVersion())
            .arg(tr("unknown compiler"))
            .arg(osArch()));
#endif
    ui->lblCompileTime->setText(ui->lblCompileTime->text()
                                .arg(__DATE__, __TIME__));

    ui->lblHomepage->setText(tr("Website: <a href=\"%1\">%1</a>").arg(AppIdentity::website().toHtmlEscaped()));
}

void AboutDialog::updateForkFlag()
{
    if (!mUiReady) return;
    const int height = qMax(1, ui->lblForkCopyright->fontMetrics().capHeight());
    const int width = qMax(1, qRound(height * 1.5));
    const qreal ratio = devicePixelRatioF();
    QPixmap flag(qMax(1, qRound(width * ratio)), qMax(1, qRound(height * ratio)));
    flag.setDevicePixelRatio(ratio);
    flag.fill(Qt::transparent);
    QPainter painter(&flag);
    QSvgRenderer renderer(QStringLiteral(":/icons/app/italian-flag.svg"));
    if (!renderer.isValid()) {
        // Keep the copyright readable if its decorative resource is unavailable.
        ui->lblForkFlag->clear();
        ui->lblForkFlag->hide();
        return;
    }
    renderer.render(&painter, QRectF(0, 0, width, height));
    painter.end();
    ui->lblForkFlag->setPixmap(flag);
    ui->lblForkFlag->setFixedSize(width, height);
    ui->lblForkFlag->show();
}

bool AboutDialog::event(QEvent *event)
{
    const bool handled = QDialog::event(event);
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    if (event->type() == QEvent::DevicePixelRatioChange) updateForkFlag();
#endif
    return handled;
}

void AboutDialog::changeEvent(QEvent *event)
{
    QDialog::changeEvent(event);
    if (event->type() == QEvent::FontChange || event->type() == QEvent::ApplicationFontChange)
        updateForkFlag();
}

bool AboutDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (mUiReady && watched == ui->lblForkCopyright && event->type() == QEvent::FontChange)
        updateForkFlag();
    return QDialog::eventFilter(watched, event);
}

AboutDialog::~AboutDialog()
{
    mUiReady = false;
    delete ui;
}
