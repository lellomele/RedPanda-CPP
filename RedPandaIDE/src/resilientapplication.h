#ifndef RESILIENTAPPLICATION_H
#define RESILIENTAPPLICATION_H
#include <QApplication>
class ResilientApplication : public QApplication {
    Q_OBJECT
public:
    using QApplication::QApplication;
    bool notify(QObject *receiver, QEvent *event) override;
signals:
    void recoverableError(const QString &message);
};
#endif
