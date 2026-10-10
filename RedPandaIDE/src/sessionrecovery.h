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
class SessionRecovery : public QObject {
    Q_OBJECT
public:
    explicit SessionRecovery(MainWindow *window);
    ~SessionRecovery();
    void restorePreviousSessions();
    void checkpoint();
    void finishCleanly();
private:
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
};
#endif
