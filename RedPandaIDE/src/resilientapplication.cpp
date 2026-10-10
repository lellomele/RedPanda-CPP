#include "resilientapplication.h"
#include <qt_utils/utils.h>
#include <QTimer>
#include <QThread>
#include <QDebug>

bool ResilientApplication::notify(QObject *receiver, QEvent *event) {
    try { return QApplication::notify(receiver, event); }
    catch (const BaseError &error) {
        const QString reason = error.reason(); qWarning() << "Operation cancelled:" << reason;
        // Report after unwinding the failed event, rather than reentering a partly completed operation.
        QTimer::singleShot(0, this, [this, reason] { emit recoverableError(reason); });
        return false;
    }
    // Unknown exceptions and native faults cannot safely be classified as recoverable.
}
