#ifndef SESSIONRECOVERY_H
#define SESSIONRECOVERY_H
#include <QObject>
#include <QTimer>
#include <QFutureWatcher>
#include <QElapsedTimer>
#include <QLockFile>
#include <memory>
#include <QHash>
#include <QSet>
class Editor;
class MainWindow;
class RecoveryChecks;
class SessionRecovery : public QObject {
    Q_OBJECT
public:
    explicit SessionRecovery(MainWindow *window);
    ~SessionRecovery();
    void restorePreviousSessions();
    void checkpoint();
    void finishCleanly();
    bool isWriting() const;
    void stopScheduling();
    void requestShutdown();
    void resumeScheduling();
signals:
    void shutdownComplete();
private:
    friend class RecoveryChecks;
    void reportFailure(const QString &error);
    MainWindow *mWindow;
    QString mDirectory;
    QString mPath;
    std::unique_ptr<QLockFile> mLock;
    QTimer mTimer;
    QElapsedTimer mElapsed;
    QFutureWatcher<QString> mWriter;
    QStringList mConsumed;
    QString mLastError;
    QHash<Editor *, QString> mTextCache;
    QSet<Editor *> mObserved;
    quint64 mRevision = 0;
    QString mLastState;
    QString mScheduledState;
    bool mStopping = false;
    bool mWritePending = false; // cleared only after the GUI consumes the result
};
#endif
