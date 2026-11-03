// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "classbrowser.h"
#include "../parser/cppparser.h"
#include <QPointer>
#include <functional>

class ClassBrowserController final : public QObject {
    Q_OBJECT
public:
    enum class State { Empty, Reading, Ready, Unavailable, Partial, ResourceLimit };
    Q_ENUM(State)
    struct SourceIdentity {
        quint64 generation = 0;
        quint64 document = 0;
        quint64 revision = 0;
        QString logical;
        QString physical;
    };
    struct Context {
        PCppParser parser;
        quint64 workspace = 0;
        bool wholeProject = false;
        QString contextFile;
        SourceIdentity source;
        Analysis::OutlineOptions options;
    };
    using CurrentSource = std::function<SourceIdentity()>;

    ClassBrowserController(std::shared_ptr<const Analysis::OutlineResources> resources,
                           CurrentSource currentSource, QObject *parent = nullptr);

    ClassBrowserModel *model() { return &mModel; }
    State state() const { return mState; }
    const Context &context() const { return mContext; }
    void setContext(Context context);
    void refresh();
    void stop();

signals:
    void stateChanged(ClassBrowserController::State state);

private:
    void update();
    void receive(const Analysis::OutlineJobResult &result);
    void changeState(State state);
    void connectParser();

    ClassBrowserModel mModel;
    const std::shared_ptr<const Analysis::OutlineResources> mResources;
    const CurrentSource mCurrentSource;
    Context mContext;
    State mState = State::Empty;
    Analysis::OutlineJobRequest mRequest;
    quint64 mSourceJobAtRequest = 0;
    std::optional<Analysis::OutlineStamp> mWaitingForSource;
    std::optional<Analysis::OutlineStamp> mAttemptedProjectScan;
    QList<QMetaObject::Connection> mConnections;
    quint64 mSequence = 0;
    bool mUpdateQueued = false;
    bool mStopped = false;
};
