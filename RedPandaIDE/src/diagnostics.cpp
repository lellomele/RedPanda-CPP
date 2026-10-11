#include "diagnostics.h"
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QDateTime>
#include <QLoggingCategory>
#include <QScopedValueRollback>
namespace {
QMutex logMutex; QString logFilename; QtMessageHandler previousHandler = nullptr; bool installed = false;
void handler(QtMsgType type, const QMessageLogContext &context, const QString &message) {
    static thread_local bool writing = false;
    if (writing) return;
    QScopedValueRollback<bool> guard(writing, true);
    QtMessageHandler forward = nullptr;
    {
        QMutexLocker lock(&logMutex);
        forward = previousHandler;
        if (installed && type != QtDebugMsg) {
            if (QFileInfo(logFilename).size() > 2 * 1024 * 1024) {
                const QString previous = logFilename + ".1";
                QFile::remove(previous); QFile::rename(logFilename, previous);
            }
            QFile file(logFilename);
            if (file.open(QIODevice::WriteOnly | QIODevice::Append)) {
                const auto line = (QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) + " [" + QString::number(type) + "] " + message + "\n").toUtf8();
                file.write(line);
            }
        }
    }
    if (forward) forward(type, context, message);
}
}
void Diagnostics::install(const QString &filename) {
    QMutexLocker lock(&logMutex);
    logFilename = filename;
    QFile file(filename);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append)) file.write("RedPanda session started\n");
    // Reconfiguration must retain the original handler, not install our handler
    // as its own predecessor and leave it installed after shutdown.
    if (!installed) { previousHandler = qInstallMessageHandler(handler); installed = true; }
}
void Diagnostics::uninstall() {
    QMutexLocker lock(&logMutex);
    if (installed) { qInstallMessageHandler(previousHandler); installed = false; }
}
