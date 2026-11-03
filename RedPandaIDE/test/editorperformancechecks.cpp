// SPDX-License-Identifier: GPL-3.0-or-later
#include "editorperformancechecks.h"
#include "src/mainwindow.h"
#include "src/editor.h"
#include "src/editormanager.h"
#include "src/widgets/classbrowsercontroller.h"
#include "src/widgets/codecompletionpopup.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QThread>
#include <functional>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QPointer>
#include <QSaveFile>
#include <QTabWidget>
#include <QTimer>
#include <QTreeView>
#include <algorithm>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <psapi.h>
#endif

namespace {

double milliseconds(const QElapsedTimer &timer)
{
    return double(timer.nsecsElapsed()) / 1000000.0;
}

QJsonObject memorySample()
{
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    if (K32GetProcessMemoryInfo(GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory), sizeof(memory))) {
        constexpr double mib = 1024.0 * 1024.0;
        return {{"workingSetMiB", memory.WorkingSetSize / mib},
                {"peakWorkingSetMiB", memory.PeakWorkingSetSize / mib},
                {"privateCommitMiB", memory.PrivateUsage / mib}};
    }
#endif
    return {};
}

bool waitFor(const std::function<bool()> &predicate, int timeout)
{
    QElapsedTimer time;
    time.start();
    while (!predicate() && time.elapsed() < timeout) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(1);
    }
    return predicate();
}

void sendKey(QWidget *target, int key, const QString &text = {})
{
    QKeyEvent down(QEvent::KeyPress, key, Qt::NoModifier, text);
    QCoreApplication::sendEvent(target, &down);
    QKeyEvent up(QEvent::KeyRelease, key, Qt::NoModifier, text);
    QCoreApplication::sendEvent(target, &up);
}

}

int runEditorPerformanceChecks(MainWindow &window, const QString &outputDirectory)
{
    const auto path = qEnvironmentVariable("REDPANDA_QA_EDITOR_BENCHMARK");
    auto *tree = window.findChild<QTreeView *>("classBrowser");
    auto *tabs = window.findChild<QTabWidget *>("tabExplorer");
    auto *structure = window.findChild<QWidget *>("tabStructure");
    auto *completionAction = window.findChild<QAction *>("actionCode_Completion");
    auto *popup = window.findChild<CodeCompletionPopup *>();
    auto *controller = window.classBrowserController();
    QJsonObject data{{"fixture", QFileInfo(path).fileName()}, {"initialMemory", memorySample()}};
    const auto finish = [&](int exitCode) {
        data.insert("finalMemory", memorySample());
        data.insert("passed", exitCode == 0);
        QSaveFile report(QDir(outputDirectory).filePath("editor-performance.json"));
        const auto bytes = QJsonDocument(data).toJson();
        if (!report.open(QIODevice::WriteOnly) || report.write(bytes) != bytes.size() || !report.commit())
            return 8;
        return exitCode;
    };
    if (path.isEmpty() || !tree || !tabs || !structure || !completionAction || !popup || !controller)
        return finish(2);

    tabs->setCurrentWidget(structure);
    QElapsedTimer heartbeatClock;
    heartbeatClock.start();
    double largestGap = 0;
    QTimer heartbeat;
    heartbeat.setTimerType(Qt::PreciseTimer);
    heartbeat.setInterval(20);
    QObject::connect(&heartbeat, &QTimer::timeout, &window, [&] {
        largestGap = std::max(largestGap, milliseconds(heartbeatClock));
        heartbeatClock.restart();
    });
    heartbeat.start();
    QElapsedTimer time;
    time.start();
    const QPointer<Editor> editor = window.openFile(path);
    data.insert("openMs", milliseconds(time));
    if (!editor) return finish(3);
    data.insert("lines", editor->lineCount());
    const bool ready = waitFor([&] {
        return editor && controller->state() == ClassBrowserController::State::Ready
            && controller->context().source.logical == editor->filename() && tree->model()->rowCount() > 0;
    }, 45000);
    data.insert("structureReadyMsFromOpen", milliseconds(time));
    data.insert("maximumHeartbeatGapMsDuringOpenAndAnalysis", largestGap);
    data.insert("structureState", int(controller->state()));
    data.insert("structureFile", controller->context().source.logical);
    data.insert("parserPendingWork", editor && editor->parser() && editor->parser()->hasPendingWork());
    if (editor && editor->parser()) {
        if (const auto source = editor->parser()->sourceJobReport()) {
            data.insert("sourceReads", source->reads.size());
            data.insert("sourceComplete", source->complete());
            data.insert("sourceError", source->error);
        }
    }
    if (!ready) return finish(4);
    data.insert("afterAnalysisMemory", memorySample());

    auto *model = tree->model();
    QPersistentModelIndex root;
    data.insert("structureRoots", model->rowCount());
    for (int row = 0; row < model->rowCount(); ++row) {
        const auto candidate = model->index(row, 0);
        if (model->rowCount(candidate) > 0) { root = candidate; break; }
    }
    if (!root.isValid()) return finish(5);
    const auto childCount = model->rowCount(root);
    data.insert("firstRootChildren", childCount);
    QJsonArray expansion;
    for (int index = 0; index < 5; ++index) {
        time.restart();
        tree->expand(root);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        if (!root.isValid() || model->rowCount(root) != childCount) return finish(5);
        expansion.append(milliseconds(time));
        tree->collapse(root);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    data.insert("expandMs", expansion);

    const QString original = editor->lineText(1);
    editor->setCaretXY({int(original.size()), 1});
    const auto beforeEdit = window.editorManager()->documentRevision(editor);
    QJsonArray edit;
    for (int run = 0; run < 5; ++run) {
        time.restart();
        for (int index = 0; index < 40; ++index) {
            sendKey(editor, Qt::Key_Z, QStringLiteral("z"));
            sendKey(editor, Qt::Key_Backspace);
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        edit.append(milliseconds(time));
    }
    data.insert("edit40PairsMs", edit);
    if (editor->lineText(1) != original || window.editorManager()->documentRevision(editor) == beforeEdit) return finish(6);
    // The fixture is generated for this run and each keystroke was reversed.
    editor->setModified(false);
    const int completionLine = editor->lineCount() - 1;
    const QString completionText = editor->lineText(completionLine);
    if (completionLine < 0 || !completionText.endsWith("BenchCandidate")) return finish(7);
    editor->setCaretXY({int(completionText.size()), completionLine});
    if (!waitFor([&] { return !editor->parser()->hasPendingWork(); }, 20000)) return finish(7);
    auto *choices = popup->findChild<CodeCompletionListView *>();
    if (!choices) return finish(7);
    QJsonArray completion;
    for (int run = 0; run < 5; ++run) {
        time.restart();
        completionAction->trigger();
        if (!popup->isVisible() && !waitFor([&] { return popup->isVisible(); }, 2000)) return finish(7);
        if (!choices->model() || choices->model()->rowCount() < 2) return finish(7);
        completion.append(milliseconds(time));
        popup->hide();
    }
    data.insert("completionMs", completion);
    data.insert("afterEditingMemory", memorySample());
    window.editorManager()->forceCloseEditor(editor);
    return finish(0);
}
