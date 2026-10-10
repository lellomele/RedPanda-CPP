#include "diagnostics.h"
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QDateTime>
#include <QLoggingCategory>
namespace {
QMutex logMutex; QString logFilename; QtMessageHandler previousHandler = nullptr;
void handler(QtMsgType type, const QMessageLogContext &context, const QString &message) {
    static thread_local bool writing = false;
    if (writing) return;
    writing = true;
    if (type != QtDebugMsg) {
        QMutexLocker lock(&logMutex);
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
    if (previousHandler) previousHandler(type, context, message);
    writing = false;
}
}
void Diagnostics::install(const QString &filename) {
    logFilename = filename; QFile file(filename); if (file.open(QIODevice::WriteOnly | QIODevice::Append)) file.write("RedPanda session started\n");
    previousHandler = qInstallMessageHandler(handler);
}
void Diagnostics::uninstall() { qInstallMessageHandler(previousHandler); }
