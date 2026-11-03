/*
 * Copyright (C) 2020-2024 Roy Qu (royqh1979@gmail.com)
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
#include "competitivecompenionhandler.h"

#include <QJsonArray>
#include <QPointer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTextDocument>
#include "ojproblemset.h"
#include "../settings.h"
#include "../mainwindow.h"

CompetitiveCompanionHandler::CompetitiveCompanionHandler(QObject *parent):
    QObject(parent),
    mThread(nullptr)
{
}

void CompetitiveCompanionHandler::start()
{
    if (mStopping || mThread) return;
    if (!pSettings->executor().enableProblemSet())
        return;
    if (!pSettings->executor().enableCompetitiveCompanion())
        return;
    mThread = new CompetitiveCompanionThread(this);
    connect(mThread,
                &CompetitiveCompanionThread::newProblemReceived,
                this, &CompetitiveCompanionHandler::onNewProblemReceived);
    mThread->start();
    if (!mThread->waitStart()) {
        qWarning() << "Competitive Companion port is unavailable; service disabled for this attempt.";
        mThread->wait();
        delete mThread;
        mThread = nullptr;
    }
}

void CompetitiveCompanionHandler::stop()
{
    if (!mThread)
        return;
    auto *thread = mThread;
    mThread = nullptr;
    mAwaitingStop = false;
    thread->stop();
    // The semaphore acknowledges server shutdown, not the end of QThread::run().
    thread->wait();
    delete thread;
}

void CompetitiveCompanionHandler::requestShutdown()
{
    mStopping = true;
    if (!mThread) {
        emit shutdownComplete();
        return;
    }
    auto *thread = mThread;
    mRestartAfterStop = true;
    if (!mAwaitingStop) {
        mAwaitingStop = true;
        const QPointer<CompetitiveCompanionThread> lifetime(thread);
        connect(thread, &QThread::finished, this, [this, lifetime] {
            if (lifetime) finishShutdown(lifetime);
        });
    }
    thread->stop();
    if (thread->isFinished()) finishShutdown(thread);
}

void CompetitiveCompanionHandler::resumeAdmissions()
{
    mStopping = false;
    if (mRestartAfterStop && !mThread) {
        mRestartAfterStop = false;
        start();
    }
}

void CompetitiveCompanionHandler::finishShutdown(CompetitiveCompanionThread *thread)
{
    if (mThread != thread) return;
    mThread = nullptr;
    mAwaitingStop = false;
    thread->deleteLater();
    if (mStopping) emit shutdownComplete();
    else resumeAdmissions();
}

void CompetitiveCompanionHandler::onNewProblemReceived(int num, int total, POJProblem newProblem)
{
    if (!mStopping) emit newProblemReceived(num, total, newProblem);
}

void CompetitiveCompanionThread::onNewProblemConnection(QTcpSocket* clientConnection)
{
    QByteArray content;
    int unreadCount = 0;
    constexpr qsizetype maximumRequestBytes = 16 * 1024 * 1024;
    clientConnection->setReadBufferSize(64 * 1024);
    while (!mStop.load(std::memory_order_acquire) && clientConnection->state() == QTcpSocket::ConnectedState) {
        clientConnection->waitForReadyRead(100);
        QByteArray readed = clientConnection->readAll();
        if (readed.isEmpty()) {
            unreadCount ++;
            if (!content.isEmpty() || unreadCount>30)
                break;
        } else {
            unreadCount = 0;
        }
        if (readed.size() > maximumRequestBytes - content.size()) {
            clientConnection->abort();
            return;
        }
        content += readed;
    }
    if (mStop.load(std::memory_order_acquire) || clientConnection->bytesAvailable() > maximumRequestBytes - content.size()) {
        clientConnection->abort();
        return;
    }
    content += clientConnection->readAll();
    clientConnection->write("HTTP/1.1 200 OK\n");
    clientConnection->write("Connection: close\n");
    clientConnection->disconnectFromHost();
//    qDebug()<<"---------";
//    qDebug()<<content;
    content = getHTTPBody(content);
//    qDebug()<<"*********";
//    qDebug()<<content;
    if (content.isEmpty()) {
        return;
    }
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(content,&error);
    if (error.error!=QJsonParseError::NoError) {
        qDebug()<<"Read http content failed!";
        qDebug()<<error.errorString();
        return;
    }
    QJsonObject obj=doc.object();
    //qDebug()<<obj;
    QJsonObject batchObj = obj["batch"].toObject();
    QString batchId = batchObj["id"].toString();
    if (mBatchId!=batchId) {
        mBatchId = batchId;
        mBatchCount = batchObj["size"].toInt();
        mBatchProblemsRecieved = 0;
        //emit newBatchReceived(mBatchCount);
    }

    QString name = obj["name"].toString();

    POJProblem problem = std::make_shared<OJProblem>();
    problem->setName(name);
    problem->setUrl(obj["url"].toString());
    if (obj.contains("timeLimit")) {
        problem->setTimeLimit(obj["timeLimit"].toInt());
        problem->setTimeLimitUnit(ProblemTimeLimitUnit::Milliseconds);
    }
    if (obj.contains("memoryLimit")) {
        problem->setMemoryLimit(obj["memoryLimit"].toInt());
        problem->setMemoryLimitUnit(ProblemMemoryLimitUnit::MB);
    }
    QJsonArray caseArray = obj["tests"].toArray();
    for (const QJsonValue& val : caseArray) {
        QJsonObject caseObj = val.toObject();
        POJProblemCase problemCase = std::make_shared<OJProblemCase>();
        problemCase->testState = ProblemCaseTestState::NotTested;
        problemCase->setName( tr("Problem Case %1").arg(problem->cases().count()+1) );
        if (pSettings->executor().convertHTMLToTextForInput()) {
            QTextDocument doc;
            doc.setHtml(caseObj["input"].toString());
            problemCase->setInput( doc.toPlainText() );
        } else
            problemCase->setInput( caseObj["input"].toString() );
        if (pSettings->executor().convertHTMLToTextForExpected()) {
            QTextDocument doc;
            doc.setHtml(caseObj["output"].toString());
            problemCase->setExpected(doc.toPlainText());
        } else
            problemCase->setExpected(caseObj["output"].toString());
        problemCase->setModified(false);
        problem->addCase(problemCase);
    }
    problem->setModified(false);
    mBatchProblemsRecieved++;
    emit newProblemReceived(mBatchProblemsRecieved, mBatchCount, problem);
    // if (mBatchProblemsRecieved == mBatchCount) {
    //     emit batchFinished(mBatchCount);
    // }
}

CompetitiveCompanionThread::CompetitiveCompanionThread(QObject *parent):
    QThread{parent},
    mStop{false},
    mBatchProblemsRecieved{0},
    mStartSemaphore{0},
    mStopSemaphore{0},
    mStartOk{false}
{
}

void CompetitiveCompanionThread::stop()
{
    mStop = true;
}

bool CompetitiveCompanionThread::waitStart()
{
    mStartSemaphore.acquire(1);
    return mStartOk;
}

void CompetitiveCompanionThread::waitStop()
{
    stop();
    mStopSemaphore.acquire(1);
}

void CompetitiveCompanionThread::run()
{
    QTcpServer tcpServer;
    mStartOk = tcpServer.listen(QHostAddress::LocalHost,pSettings->executor().competivieCompanionPort());
    if (!mStartOk) {
        mStop=true;
    }
    mStartSemaphore.release(1);
    while(!mStop) {
        tcpServer.waitForNewConnection(1000);
        while (tcpServer.hasPendingConnections()) {
            QTcpSocket* clientConnection = tcpServer.nextPendingConnection();
            onNewProblemConnection(clientConnection);
            delete clientConnection;
        }
    }
    tcpServer.close();
    mStopSemaphore.release(1);
}
