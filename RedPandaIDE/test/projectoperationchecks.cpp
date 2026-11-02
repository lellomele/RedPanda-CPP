// SPDX-License-Identifier: GPL-3.0-or-later
#include "projectoperationchecks.h"
#include "src/mainwindow.h"
#include "src/editormanager.h"
#include "src/editor.h"
#include "src/project.h"
#include "src/projecttemplate.h"
#include "src/parser/cppparser.h"
#include "src/projectfileoperations.h"
#include "src/compiler/compilermanager.h"
#include <QApplication>
#include <QMessageBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QElapsedTimer>
#include <QThread>
#include <QPointer>
#include <thread>

int runProjectOperationChecks(MainWindow &window, const QString &outputDirectory)
{
    QJsonArray checks;
    int failures = 0;
    const auto check = [&](const char *name, bool passed) {
        checks.append(QJsonObject{{"name", QString::fromLatin1(name)}, {"passed", passed}});
        if (!passed) ++failures;
    };
    const auto write = [](const QString &path, const QByteArray &text) {
        QFile f(path); return f.open(QIODevice::WriteOnly) && f.write(text) == text.size();
    };
    const auto read = [](const QString &path) {
        QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    };
    const auto waitParser = [](const PCppParser &parser) {
        QElapsedTimer deadline; deadline.start();
        while (parser && parser->hasPendingWork() && deadline.elapsed() < 10000) {
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 10);
            QThread::msleep(1);
        }
        return !parser || !parser->hasPendingWork();
    };
    QTemporaryDir fixture(QDir(outputDirectory).filePath("project-transactions-XXXXXX"));
    if (!fixture.isValid()) return 2;
    // This watcher belongs to the fixture/controller, not to any Project.
    auto *fixtureWatcher = window.fileSystemWatcher();
    const bool alreadyWatched = fixtureWatcher->directories().contains(fixture.path());
    if (!alreadyWatched && !fixtureWatcher->addPath(fixture.path())) return 15;
    const auto releaseFixtureWatch = finally([&] {
        if (!alreadyWatched) fixtureWatcher->removePath(fixture.path());
    });

    const QString failedFolder = fixture.filePath("uncommitted-project");
    if (!QDir().mkpath(failedFolder)) return 16;
    bool failedLoadReported = false;
    try {
        Project::load(QDir(failedFolder).filePath("missing.dev"), window.editorManager(),
                      window.iconsManager(), fixtureWatcher);
    } catch (const FileError &) { failedLoadReported = true; }
    check("Failed project load does not register a folder watcher", failedLoadReported &&
          !fixtureWatcher->directories().contains(failedFolder));
    auto failedCreate = Project::create(QDir(failedFolder).filePath("template.dev"), "NoTemplate",
        window.editorManager(), window.iconsManager(), fixtureWatcher, {}, true);
    check("Failed project create does not register a folder watcher", !failedCreate &&
          !fixtureWatcher->directories().contains(failedFolder));
    const QString projectPath = fixture.filePath("fixture.dev");
    auto emptyTemplate = std::make_shared<ProjectTemplate>();
    emptyTemplate->setVersion(0);
    ProjectOptions options;
    options.modelType = ProjectModelType::Custom;
    options.icon.clear();
    emptyTemplate->setOptions(options);
    auto project = Project::create(projectPath, "Fixture", window.editorManager(),
                                    window.iconsManager(), window.fileSystemWatcher(), emptyTemplate, true);
    if (!project) return 3;
    QPointer<Editor> targetEditor;
    QPointer<Editor> reopenedEditor;
    QPointer<Editor> caseEditor;
    QPointer<Editor> removalEditor;
    QList<QPointer<Editor>> relocationEditors;
    const auto cleanup = finally([&] {
        for (const auto &editor : relocationEditors) {
            if (editor) {
                editor->setModified(false);
                window.editorManager()->closeEditor(editor, false, false);
            }
        }
        if (removalEditor) {
            removalEditor->setModified(false);
            window.editorManager()->closeEditor(removalEditor, false, false);
        }
        if (caseEditor) {
            caseEditor->setModified(false);
            window.editorManager()->closeEditor(caseEditor, false, false);
        }
        if (reopenedEditor) {
            reopenedEditor->setModified(false);
            window.editorManager()->closeEditor(reopenedEditor, false, false);
        }
        if (targetEditor) {
            targetEditor->setModified(false);
            window.editorManager()->closeEditor(targetEditor, false, false);
        }
        waitParser(project->cppParser());
        project->cppParser()->setEnabled(false);
    });

    const QString sourcePath = fixture.filePath("source.cpp"), renamedPath = fixture.filePath("renamed.cpp");
    const QString targetPath = fixture.filePath("target.cpp");
    if (!write(sourcePath, "struct Source {};\n") || !write(targetPath, "struct Target {};\n")) return 4;
    auto source = project->addUnit(sourcePath, project->rootNode());
    auto target = project->addUnit(targetPath, project->rootNode());
    if (!source || !target) return 5;
    source->setPriority(7); source->setOverrideBuildCmd(true); source->setBuildCmd("custom-command");
    source->setCompile(false); source->setEncoding(ENCODING_UTF8);
    ProjectUnit *sourceIdentity = source.get();
    const auto renamed = project->renameUnitFile(source, renamedPath);
    check("Closed project unit rename commits without an Editor", renamed.committed() &&
          !QFile::exists(sourcePath) && read(renamedPath) == QByteArray("struct Source {};\n") &&
          project->findUnit(renamedPath) == source && !project->findUnit(sourcePath));
    check("Project rename preserves unit identity and settings", source.get() == sourceIdentity &&
          source->priority() == 7 && source->overrideBuildCmd() && source->buildCmd() == "custom-command" &&
          !source->compile() && source->encoding() == ENCODING_UTF8);
    check("Closed unit analysis completes", waitParser(project->cppParser()));

    const QString caseSourcePath = fixture.filePath("CaseOpen.cpp");
    const QString caseTargetPath = fixture.filePath("caseopen.cpp");
    if (!write(caseSourcePath, "struct CaseOpen {};\n")) return 17;
    auto caseUnit = project->addUnit(caseSourcePath, project->rootNode());
    caseEditor = window.editorManager()->newEditor(caseSourcePath, ENCODING_UTF8,
                                                   FileType::CppSource, QString(), false, false);
    if (!caseUnit || !caseEditor) return 18;
    caseEditor->setContent("struct CaseOpenUnsaved {};\n");
    caseEditor->setModified(true);
    const auto caseResult = project->renameUnitFile(caseUnit, caseTargetPath);
    check("Case-only open unit rename preserves editor association and unsaved buffer",
          caseResult.committed() && caseEditor && caseEditor->filename() == caseTargetPath &&
          project->findUnit(caseEditor) == caseUnit && project->unitEditor(caseUnit) == caseEditor &&
          caseEditor->modified() && caseEditor->text().contains("CaseOpenUnsaved") &&
          read(caseTargetPath) == QByteArray("struct CaseOpen {};\n"));

    targetEditor = window.editorManager()->newEditor(targetPath, ENCODING_UTF8, FileType::CppSource, QString(), false, false);
    if (!targetEditor) return 6;
    targetEditor->setContent("struct UnsavedTarget {};\n"); targetEditor->setModified(true);
    const QByteArray sourceBefore = read(renamedPath), targetBefore = read(targetPath);
    int replacementQuestions = 0, saveCancellations = 0;
    QTimer answers;
    QObject::connect(&answers, &QTimer::timeout, &window, [&] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            auto *box = qobject_cast<QMessageBox *>(widget);
            if (!box || !box->isVisible()) continue;
            if (box->standardButtons().testFlag(QMessageBox::Cancel)) {
                ++saveCancellations; box->button(QMessageBox::Cancel)->click();
            } else if (box->standardButtons().testFlag(QMessageBox::Yes)) {
                ++replacementQuestions; box->button(QMessageBox::Yes)->click();
            }
        }
    });
    answers.start(5);
    const bool accepted = project->model()->setData(project->model()->getNodeIndex(source->node().get()),
                                                    QFileInfo(targetPath).fileName(), Qt::EditRole);
    answers.stop();
    check("Project replacement honors destination-save cancellation", !accepted && replacementQuestions > 0 && saveCancellations > 0);
    check("Cancelled project rename preserves files and unit associations", read(renamedPath) == sourceBefore &&
          read(targetPath) == targetBefore && project->findUnit(renamedPath) == source && project->findUnit(targetPath) == target);
    check("Cancelled project rename preserves unsaved destination editor", targetEditor && targetEditor->modified() &&
          targetEditor->text().contains("UnsavedTarget") && targetEditor->filename() == targetPath);

    const QString removalPath = fixture.filePath("removal.cpp");
    const QByteArray removalDisk("struct RemovalDisk {};\n");
    if (!write(removalPath, removalDisk)) return 19;
    auto removalUnit = project->addUnit(removalPath, project->rootNode());
    removalEditor = window.editorManager()->newEditor(removalPath, ENCODING_UTF8,
                                                       FileType::CppSource, QString(), false, false);
    if (!removalUnit || !removalEditor) return 20;
    removalEditor->setInProject(true, false);
    removalEditor->setContent("struct RemovalUnsaved {};\n"); removalEditor->setModified(true);
    const auto answerRemoval = [&](QMessageBox::StandardButton choice, const std::function<OperationResult()> &operation,
                                   int &questions, int &saveErrors) {
        QTimer driver;
        QObject::connect(&driver, &QTimer::timeout, &window, [&] {
            for (QWidget *widget : QApplication::topLevelWidgets()) {
                auto *box = qobject_cast<QMessageBox *>(widget);
                if (!box || !box->isVisible()) continue;
                if (box->standardButtons().testFlag(QMessageBox::Cancel)) {
                    ++questions; box->button(choice)->click();
                } else if (box->standardButtons().testFlag(QMessageBox::Ok)) {
                    ++saveErrors; box->button(QMessageBox::Ok)->click();
                }
            }
        });
        driver.start(5);
        const auto result = operation();
        driver.stop();
        return result;
    };
    int removalQuestions = 0, removalSaveErrors = 0, removalEvents = 0;
    const auto removalConnection = QObject::connect(project.get(), &Project::unitRemoved, &window,
        [&](const QString &path) { if (path == removalPath) ++removalEvents; });
    const auto releaseRemovalConnection = finally([&] { QObject::disconnect(removalConnection); });
    project->setModified(false);
    const auto cancelledRemoval = answerRemoval(QMessageBox::Cancel,
        [&] { return project->removeUnit(removalUnit, true, true); }, removalQuestions, removalSaveErrors);
    check("Cancelled unit removal preserves membership dirty buffer flags and disk",
          cancelledRemoval.status == OperationStatus::Cancelled && removalQuestions > 0 && removalEvents == 0 &&
          project->findUnit(removalPath) == removalUnit && removalEditor && removalEditor->inProject() &&
          removalEditor->modified() && removalEditor->text().contains("RemovalUnsaved") &&
          read(removalPath) == removalDisk && !project->modified());

    const QString removalSavedPath = removalPath + ".protected";
    const bool watcherWasBlocked = window.fileSystemWatcher()->blockSignals(true);
    const auto restoreWatcherSignals = finally([&] { window.fileSystemWatcher()->blockSignals(watcherWasBlocked); });
    if (!QFile::rename(removalPath, removalSavedPath) || !QDir().mkpath(removalPath)) return 21;
    const auto failedRemovalSave = answerRemoval(QMessageBox::Yes,
        [&] { return project->removeUnit(removalUnit, true, false); }, removalQuestions, removalSaveErrors);
    check("Failed save during unit removal preserves membership and unsaved document",
          !failedRemovalSave.committed() && removalSaveErrors > 0 && removalEvents == 0 &&
          project->findUnit(removalPath) == removalUnit && removalEditor && removalEditor->inProject() &&
          removalEditor->modified() && removalEditor->text().contains("RemovalUnsaved") &&
          read(removalSavedPath) == removalDisk && QFileInfo(removalPath).isDir() && !project->modified());
    if (!QDir().rmdir(removalPath) || !QFile::rename(removalSavedPath, removalPath)) return 22;
    int trashCalls = 0;
    const Project::TrashFile refusedTrash = [&](const QString &path) { ++trashCalls; return path != removalPath; };
    const auto failedTrash = answerRemoval(QMessageBox::No,
        [&] { return project->removeUnit(removalUnit, true, true, refusedTrash); }, removalQuestions, removalSaveErrors);
    check("Failed trash never falls back to delete or discard the dirty editor",
          failedTrash.status == OperationStatus::Failed && trashCalls == 1 && removalEvents == 0 &&
          project->findUnit(removalPath) == removalUnit && removalEditor && removalEditor->inProject() &&
          removalEditor->modified() && removalEditor->text().contains("RemovalUnsaved") &&
          read(removalPath) == removalDisk && !project->modified());
    const int questionsBeforeDiscard = removalQuestions;
    const auto acceptedRemoval = answerRemoval(QMessageBox::No,
        [&] { return project->removeUnit(removalUnit, true, false); }, removalQuestions, removalSaveErrors);
    check("Confirmed discard removes one unit without saving or a second close dialog",
          acceptedRemoval.succeeded() && removalQuestions == questionsBeforeDiscard + 1 && removalEvents == 1 &&
          !project->findUnit(removalPath) && !removalEditor && project->modified() && read(removalPath) == removalDisk);
    if (!write(removalPath, "struct ReplacementAfterRemoval {};\n")) return 23;
    const auto staleRemoval = project->removeUnit(removalUnit, true, true, refusedTrash);
    check("Replayed removal cannot delete a replacement file or emit another removal",
          staleRemoval.status == OperationStatus::Conflict && trashCalls == 1 && removalEvents == 1 &&
          !project->findUnit(removalPath) && read(removalPath) == QByteArray("struct ReplacementAfterRemoval {};\n"));

    const QString externalSourcePath = fixture.filePath("external-source.cpp");
    const QString externalTargetPath = fixture.filePath("external-target.cpp");
    if (!write(externalSourcePath, "struct ExternalSourceDisk {};\n") ||
            !write(externalTargetPath, "struct ExternalTargetDisk {};\n")) return 24;
    auto externalSourceUnit = project->addUnit(externalSourcePath, project->rootNode());
    auto externalTargetUnit = project->addUnit(externalTargetPath, project->rootNode());
    const QPointer<Editor> externalSourceEditor = window.editorManager()->newEditor(externalSourcePath, ENCODING_UTF8,
        FileType::CppSource, QString(), false, false);
    const QPointer<Editor> externalTargetEditor = window.editorManager()->newEditor(externalTargetPath, ENCODING_UTF8,
        FileType::CppSource, QString(), false, false);
    relocationEditors << externalSourceEditor << externalTargetEditor;
    if (!externalSourceUnit || !externalTargetUnit || !externalSourceEditor || !externalTargetEditor) return 25;
    externalSourceEditor->setInProject(true, false);
    externalTargetEditor->setInProject(true, false);
    externalSourceEditor->setContent("struct ExternalSourceUnsaved {};\n"); externalSourceEditor->setModified(true);
    externalTargetEditor->setContent("struct ExternalTargetUnsaved {};\n"); externalTargetEditor->setModified(true);
    if (!QFile::remove(externalTargetPath) || !QFile::rename(externalSourcePath, externalTargetPath)) return 26;
    const auto externalConflict = project->renameUnit(externalSourceUnit, externalTargetPath);
    const auto refusedMerge = project->resolveUnitRelocation(externalSourceUnit, externalTargetPath, externalTargetEditor);
    check("External rename collision preserves both unit identities and original buffers",
          externalConflict.status == OperationStatus::CommittedWithFollowUpPending && !externalConflict.error.isEmpty() &&
          refusedMerge.status == OperationStatus::Conflict &&
          externalSourceUnit->fileName() == externalSourcePath && externalTargetUnit->fileName() == externalTargetPath &&
          project->findUnit(externalSourcePath) == externalSourceUnit && project->findUnit(externalTargetPath) == externalTargetUnit &&
          externalSourceUnit->relocation().state == ProjectUnit::RelocationState::Conflict &&
          externalSourceUnit->relocation().expectedFileName == externalTargetPath &&
          !externalSourceUnit->relocation().diagnostic.isEmpty() && !project->unitEditor(externalSourceUnit) &&
          !project->findUnit(externalSourceEditor) && !externalSourceEditor->inProject() && externalSourceEditor->modified() &&
          externalSourceEditor->text().contains("ExternalSourceUnsaved") && externalTargetEditor->inProject() &&
          externalTargetEditor->modified() && externalTargetEditor->text().contains("ExternalTargetUnsaved") &&
          project->unitEditor(externalTargetUnit) == externalTargetEditor &&
          read(externalTargetPath) == QByteArray("struct ExternalSourceDisk {};\n"));

    const QString soloSourcePath = fixture.filePath("solo-source.cpp"), soloTargetPath = fixture.filePath("solo-target.cpp");
    if (!write(soloSourcePath, "struct SoloSourceDisk {};\n") || !write(soloTargetPath, "struct SoloTargetDisk {};\n")) return 27;
    auto soloUnit = project->addUnit(soloSourcePath, project->rootNode());
    const QPointer<Editor> soloSourceEditor = window.editorManager()->newEditor(soloSourcePath, ENCODING_UTF8,
        FileType::CppSource, QString(), false, false);
    const QPointer<Editor> soloTargetEditor = window.editorManager()->newEditor(soloTargetPath, ENCODING_UTF8,
        FileType::CppSource, QString(), false, false);
    relocationEditors << soloSourceEditor << soloTargetEditor;
    if (!soloUnit || !soloSourceEditor || !soloTargetEditor) return 28;
    soloSourceEditor->setInProject(true, false);
    soloSourceEditor->setContent("struct SoloSourceUnsaved {};\n"); soloSourceEditor->setModified(true);
    soloTargetEditor->setContent("struct SoloTargetUnsaved {};\n"); soloTargetEditor->setModified(true);
    if (!QFile::remove(soloTargetPath) || !QFile::rename(soloSourcePath, soloTargetPath)) return 29;
    const auto soloConflict = project->renameUnit(soloUnit, soloTargetPath);
    check("External rename never binds a project unit to an unrelated destination buffer",
          soloConflict.status == OperationStatus::CommittedWithFollowUpPending && soloUnit->fileName() == soloTargetPath &&
          project->findUnit(soloTargetPath) == soloUnit && !project->findUnit(soloSourcePath) &&
          soloUnit->relocation().state == ProjectUnit::RelocationState::Conflict && !project->unitEditor(soloUnit) &&
          !project->findUnit(soloSourceEditor) && !project->findUnit(soloTargetEditor) &&
          !soloSourceEditor->inProject() && soloSourceEditor->modified() && soloSourceEditor->text().contains("SoloSourceUnsaved") &&
          !soloTargetEditor->inProject() && soloTargetEditor->modified() && soloTargetEditor->text().contains("SoloTargetUnsaved"));
    const auto soloResolved = project->resolveUnitRelocation(soloUnit, soloTargetPath, soloTargetEditor);
    check("Explicit relocation recovery selects one buffer without discarding either document",
          soloResolved.succeeded() && soloUnit->relocation().state == ProjectUnit::RelocationState::Resolved &&
          project->unitEditor(soloUnit) == soloTargetEditor && project->findUnit(soloTargetEditor) == soloUnit &&
          soloTargetEditor->inProject() && soloTargetEditor->modified() && soloTargetEditor->text().contains("SoloTargetUnsaved") &&
          !soloSourceEditor->inProject() && soloSourceEditor->modified() && soloSourceEditor->text().contains("SoloSourceUnsaved") &&
          read(soloTargetPath) == QByteArray("struct SoloSourceDisk {};\n"));

    const auto metadata = project->saveMetadata();
    check("Project metadata candidate commits", metadata.succeeded());
    std::shared_ptr<Project> restoredProject;
    const auto loaded = answerRemoval(QMessageBox::No, [&] {
        try {
            restoredProject = Project::load(projectPath, window.editorManager(), window.iconsManager(), window.fileSystemWatcher());
            return OperationResult::success();
        } catch (const FileError &error) { return OperationResult::failure(error.reason()); }
    }, removalQuestions, removalSaveErrors);
    const auto restoredSource = restoredProject ? restoredProject->findUnit(externalSourcePath) : PProjectUnit{};
    const auto restoredTarget = restoredProject ? restoredProject->findUnit(externalTargetPath) : PProjectUnit{};
    const auto restoredResolved = restoredProject ? restoredProject->findUnit(soloTargetPath) : PProjectUnit{};
    check("Atomic metadata reload preserves unresolved relocation and suspended association",
          loaded.succeeded() && restoredSource && restoredTarget && restoredResolved &&
          restoredSource != restoredTarget && restoredSource->fileName() == externalSourcePath &&
          restoredTarget->fileName() == externalTargetPath &&
          restoredSource->relocation().state == ProjectUnit::RelocationState::Conflict &&
          restoredSource->relocation().expectedFileName == externalTargetPath &&
          !restoredSource->relocation().diagnostic.isEmpty() && !restoredProject->unitEditor(restoredSource) &&
          !restoredProject->findUnit(externalSourceEditor) &&
          restoredProject->validateBuildInputs().status == OperationStatus::Conflict &&
          !restoredProject->validateBuildInputs().error.isEmpty() &&
          restoredProject->unitEditor(restoredTarget) == externalTargetEditor &&
          restoredResolved->relocation().state == ProjectUnit::RelocationState::Resolved);
    if (!restoredProject) return 31;
    auto *compilerManager = window.findChild<CompilerManager *>();
    if (!compilerManager) return 30;
    const int trackedBeforeConflict = compilerManager->trackedWorkerCount();
    const bool compilingBeforeConflict = compilerManager->compiling();
    int conflictWarnings = 0;
    QTimer conflictDriver;
    QObject::connect(&conflictDriver, &QTimer::timeout, &window, [&] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            auto *box = qobject_cast<QMessageBox *>(widget);
            if (!box || !box->isVisible() || !box->standardButtons().testFlag(QMessageBox::Ok)) continue;
            if (box->text().contains(externalSourcePath) && box->text().contains(externalTargetPath))
                ++conflictWarnings;
            box->button(QMessageBox::Ok)->click();
        }
    });
    conflictDriver.start(5);
    compilerManager->compileProject(restoredProject, false);
    conflictDriver.stop();
    check("Compiler manager rejects unresolved project inputs before worker submission",
          !compilingBeforeConflict && trackedBeforeConflict >= 0 && conflictWarnings == 1 &&
          !compilerManager->compiling() && compilerManager->trackedWorkerCount() == trackedBeforeConflict &&
          !QFileInfo(project->makeFileName()).exists());
    if (restoredProject) {
        waitParser(restoredProject->cppParser());
        restoredProject->cppParser()->setEnabled(false);
        restoredProject.reset();
    }
    const QByteArray goodMetadata = read(projectPath);
    const QString protectedPath = projectPath + ".saved";
    if (!QFile::rename(projectPath, protectedPath) || !QDir().mkpath(projectPath)) return 7;
    project->setModified(false);
    const auto failed = project->saveMetadata();
    check("Failed project metadata save retains dirty state and previous file", !failed.succeeded() &&
          project->modified() && read(protectedPath) == goodMetadata);
    if (!QDir().rmdir(projectPath) || !QFile::rename(protectedPath, projectPath)) return 8;

    const QString layoutPath = changeFileExt(projectPath, "layout");
    if (!QDir().mkpath(layoutPath)) return 9;
    bool layoutFailed = false;
    project->setModified(false);
    try { project->saveAll(); } catch (const FileError &) { layoutFailed = true; }
    check("Layout failure cannot mark a partially saved project clean", layoutFailed && project->modified() &&
          !read(projectPath).isEmpty() && QFileInfo(layoutPath).isDir());
    QDir().rmdir(layoutPath);
    check("Project parser drains before teardown", waitParser(project->cppParser()));

    // Simulate a controller releasing a project while a background task still
    // holds it. A user can reopen and edit an identically named file before
    // that task finally releases the old project.
    const QString latePath = fixture.filePath("late.cpp");
    if (!write(latePath, "struct OriginalLate {};\n")) return 11;
    auto delayedProject = Project::create(fixture.filePath("delayed.dev"), "Delayed", window.editorManager(),
                                         window.iconsManager(), window.fileSystemWatcher(), emptyTemplate, true);
    if (!delayedProject || !delayedProject->addUnit(latePath, delayedProject->rootNode())) return 12;
    auto backgroundOwner = delayedProject;
    QPointer<Project> delayedLifetime(delayedProject.get());
    delayedProject.reset();
    reopenedEditor = window.editorManager()->newEditor(latePath, ENCODING_UTF8, FileType::CppSource, QString(), false, false);
    if (!reopenedEditor) return 13;
    reopenedEditor->setContent("struct ReopenedUnsaved {};\n"); reopenedEditor->setModified(true);
    check("Background project reference delays only object destruction", delayedLifetime && backgroundOwner.use_count() == 1);
    backgroundOwner.reset();
    check("Late project release does not close a reopened unsaved editor", !delayedLifetime && reopenedEditor &&
          reopenedEditor->modified() && reopenedEditor->text().contains("ReopenedUnsaved") &&
          read(latePath) == QByteArray("struct OriginalLate {};\n"));
    check("Project destruction does not remove another workspace watcher", window.fileSystemWatcher()->directories().contains(fixture.path()));

    auto threadedProject = Project::create(fixture.filePath("threaded.dev"), "Threaded", window.editorManager(),
                                          window.iconsManager(), window.fileSystemWatcher(), emptyTemplate, true);
    if (!threadedProject) return 14;
    QThread *ownerThread = threadedProject->thread();
    QThread *destructionThread = nullptr;
    QPointer<Project> threadLifetime(threadedProject.get());
    check("Shared project factory has no QObject parent ownership", threadedProject->parent() == nullptr);
    const auto destroyedConnection = QObject::connect(threadedProject.get(), &QObject::destroyed, &window,
        [&] { destructionThread = QThread::currentThread(); }, Qt::DirectConnection);
    std::thread releaseThread([held = std::move(threadedProject)]() mutable { held.reset(); });
    releaseThread.join();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QElapsedTimer destructionDeadline; destructionDeadline.start();
    while (threadLifetime && destructionDeadline.elapsed() < 5000) {
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 10);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    QObject::disconnect(destroyedConnection);
    check("Worker final release destroys Project on its owner thread", !threadLifetime && destructionThread == ownerThread);

    QFile report(QDir(outputDirectory).filePath("project-operations-result.json"));
    const auto bytes = QJsonDocument(QJsonObject{{"checks", checks}, {"failures", failures}}).toJson();
    if (!report.open(QIODevice::WriteOnly) || report.write(bytes) != bytes.size()) return 10;
    return failures == 0 ? 0 : 1;
}
