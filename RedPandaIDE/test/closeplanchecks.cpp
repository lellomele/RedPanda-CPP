// SPDX-License-Identifier: GPL-3.0-or-later
#include "closeplanchecks.h"
#include "src/mainwindow.h"
#include "src/editormanager.h"
#include "src/editorcloseplan.h"
#include "src/editor.h"
#include "src/settings.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QAbstractButton>
#include <QTimer>

int runClosePlanChecks(MainWindow &window, const QString &outputDirectory)
{
    QJsonArray checks;
    int failures = 0;
    const auto check = [&](const char *name, bool passed) {
        checks.append(QJsonObject{{"name", name}, {"passed", passed}});
        if (!passed) ++failures;
    };
    auto *manager = window.editorManager();
    Editor *first = manager->newEditor("close-first.cpp", ENCODING_UTF8, FileType::CppSource, {}, false, true);
    Editor *second = manager->newEditor("close-second.cpp", ENCODING_UTF8, FileType::CppSource, {}, false, true);
    if (!first || !second)
        return 3;
    first->setContent("int first_unsaved = 1;\n"); first->setModified(true);
    second->setContent("int second_unsaved = 2;\n"); second->setModified(true);
    const auto firstText = first->content(), secondText = second->content();
    EditorClosePlan plan;
    int choices = 0;
    const auto cancelled = plan.prepare({first, second}, [&](Editor *, bool &discard) {
        discard = true;
        return ++choices == 1 ? OperationResult::success() : OperationResult::cancelled();
    });
    check("Close plan stages discard without clearing a live document", cancelled.status == OperationStatus::Cancelled
          && first->modified() && second->modified() && first->content() == firstText && second->content() == secondText);
    const auto prepared = plan.prepare({first, second}, [](Editor *, bool &discard) {
        discard = true;
        return OperationResult::success();
    });
    check("Prepared close plan preserves both dirty buffers", prepared.succeeded() && first->modified()
          && second->modified() && plan.discards(first) && plan.discards(second)
          && plan.validate(*manager, EditorCloseScope::PlannedEditors).succeeded());
    second->setContent("int changed_during_close = 3;\n"); second->setModified(true);
    check("Close plan rejects a changed revision before removal", plan.validate(*manager, EditorCloseScope::PlannedEditors).status == OperationStatus::Conflict);
    second->setContent(secondText); second->setModified(true);

    first->setInProject(true, false);
    const auto projectPrepared = plan.prepare({first}, [](Editor *, bool &discard) {
        discard = true;
        return OperationResult::success();
    });
    auto *unplanned = manager->newEditor("close-new-project.cpp", ENCODING_UTF8, FileType::CppSource, {}, false, true);
    if (!unplanned) return 5;
    unplanned->setContent("int new_project_document = 9;"); unplanned->setModified(true);
    unplanned->setInProject(true, false);
    check("Project close rejects a document added after the decision set", projectPrepared.succeeded()
          && plan.validate(*manager, EditorCloseScope::ProjectEditors).status == OperationStatus::Conflict
          && first->modified() && unplanned->modified());
    first->setInProject(false, false);
    unplanned->setInProject(false, false); manager->forceCloseEditor(unplanned);

    // Exercise the real MainWindow close controller: first No, then Cancel.
    // This must not mutate the first buffer's dirty flag or stop the window.
    QTimer answers;
    answers.setInterval(5);
    int dialogCount = 0;
    QJsonArray answeredDialogs;
    QObject::connect(&answers, &QTimer::timeout, &window, [&] {
        auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (!message || !message->standardButtons().testFlag(QMessageBox::Cancel))
            return;
        const auto answer = dialogCount++ == 0 ? QMessageBox::No : QMessageBox::Cancel;
        answeredDialogs.append(message->text());
        // Use the actual button path; done() alone does not select a standard
        // button for the static QMessageBox::question return value.
        message->button(answer)->click();
    });
    answers.start();
    const bool closed = window.close();
    answers.stop();
    const QJsonObject closeAttempt{{"accepted", closed}, {"dialogs", dialogCount},
        {"quitting", window.isQuitting()}, {"visible", window.isVisible()},
        {"answeredDialogs", answeredDialogs}};
    check("Production close No then Cancel keeps the window usable", !closed && dialogCount == 2
          && !window.isQuitting() && window.isVisible());
    check("Production cancelled close retains unsaved text and dirty markers", first->modified() && second->modified()
          && first->content() == firstText && second->content() == secondText);
    check("Session serialization does not discard an untitled buffer", window.saveLastOpens()
          && first->modified() && second->modified());

    // A vanished candidate must be rejected without dereferencing a raw editor.
    auto *vanishing = manager->newEditor("close-vanishing.cpp", ENCODING_UTF8, FileType::CppSource, {}, false, true);
    if (!vanishing) return 4;
    vanishing->setContent("int live = 4;"); vanishing->setModified(true);
    const auto vanished = plan.prepare({vanishing}, [&](Editor *editor, bool &discard) {
        discard = true;
        manager->forceCloseEditor(editor);
        return OperationResult::success();
    });
    check("Close plan rejects a document destroyed during a decision", vanished.status == OperationStatus::Conflict);
    first->setModified(false); second->setModified(false);
    QFile output(QDir(outputDirectory).filePath("close-plan-result.json"));
    const QByteArray bytes = QJsonDocument(QJsonObject{{"failures", failures}, {"checks", checks},
        {"closeAttempt", closeAttempt}}).toJson();
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size())
        return 2;
    return failures == 0 ? 0 : 1;
}
