// SPDX-License-Identifier: GPL-3.0-or-later
#include "documentfilechecks.h"
#include "src/mainwindow.h"
#include "src/project.h"
#include "src/projecttemplate.h"
#include "src/editormanager.h"
#include "src/editor.h"
#include <QApplication>
#include <QFileDialog>
#include <QMessageBox>
#include <QAbstractButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QPointer>
#include <QKeyEvent>

int runDocumentFileChecks(MainWindow &window, const QString &outputDirectory)
{
    if (window.project()) return 2; // Isolated QA instance, never a user's project.
    auto *documents = window.editorManager();
    QTemporaryDir fixture(QDir(outputDirectory).filePath("document-files-XXXXXX"));
    if (!fixture.isValid()) return 3;
    QJsonArray checks;
    int failures = 0, unexpectedDialogs = 0, outcomeMessages = 0;
    const auto trace = [&](const char *stage) {
        QFile file(QDir(outputDirectory).filePath("document-files-progress.log"));
        if (file.open(QIODevice::Append)) file.write(QByteArray(stage) + '\n');
    };
    const auto check = [&](const char *name, bool passed) {
        checks.append(QJsonObject{{"name", QString::fromLatin1(name)}, {"passed", passed}});
        if (!passed) ++failures;
        trace(name);
    };
    const auto write = [](const QString &path, const QByteArray &bytes) {
        QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    };
    const auto read = [](const QString &path) {
        QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
    };
    enum class Decision { None, CancelSaveAs, EditAndAcceptSaveAs, CloseProjectAndAcceptSaveAs, CancelRelink, AcceptRelink, ChangeUnitAndAcceptRelink, RejectNewUnit };
    Decision decision = Decision::None;
    QPointer<Editor> sourceEditor;
    PProjectUnit sourceUnit;
    QString selectedPath;
    bool setup = true;
    QTimer driver;
    QObject::connect(&driver, &QTimer::timeout, &window, [&] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (!widget->isVisible()) continue;
            if (auto *dialog = qobject_cast<QFileDialog *>(widget)) {
                if (dialog->objectName() == "documentSaveAsFileDialog" && !dialog->property("qaHandled").toBool()) {
                    dialog->setProperty("qaHandled", true);
                    if (decision == Decision::CancelSaveAs) dialog->reject();
                    else if (decision == Decision::EditAndAcceptSaveAs) {
                        sourceEditor->setContent("// New revision during Save As\n"); sourceEditor->setModified(true);
                        dialog->selectFile(selectedPath);
                        QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
                    } else if (decision == Decision::CloseProjectAndAcceptSaveAs) {
                        QMetaObject::invokeMethod(&window, "on_actionClose_Project_triggered", Qt::DirectConnection);
                        dialog->selectFile(selectedPath);
                        QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
                    }
                }
                continue;
            }
            auto *box = qobject_cast<QMessageBox *>(widget);
            if (!box) continue;
            if (box->objectName() == "documentFileOperationMessage") {
                ++outcomeMessages; box->button(QMessageBox::Ok)->click();
            } else if (box->objectName() == "projectRelinkConfirmation") {
                if (decision == Decision::CancelRelink) box->button(QMessageBox::Cancel)->click();
                else if (decision == Decision::AcceptRelink) box->button(QMessageBox::Yes)->click();
                else if (decision == Decision::ChangeUnitAndAcceptRelink) {
                    sourceUnit->setPriority(sourceUnit->priority() + 1);
                    box->button(QMessageBox::Yes)->click();
                }
            } else if (setup && box->standardButtons().testFlag(QMessageBox::Ok)
                       && (box->windowTitle() + box->text()).contains("compil", Qt::CaseInsensitive)) {
                box->button(QMessageBox::Ok)->click();
            } else if (decision == Decision::RejectNewUnit && box->standardButtons().testFlag(QMessageBox::Ok)
                       && box->text().contains(selectedPath)) {
                box->button(QMessageBox::Ok)->click();
            } else if (setup && box->standardButtons().testFlag(QMessageBox::Ok)
                       && box->text().contains("legacy-resources.dev.bak")) {
                box->button(QMessageBox::Ok)->click();
            } else {
                ++unexpectedDialogs;
                const auto buttons = box->standardButtons();
                if (buttons.testFlag(QMessageBox::Cancel)) box->button(QMessageBox::Cancel)->click();
                else if (buttons.testFlag(QMessageBox::No)) box->button(QMessageBox::No)->click();
                else if (buttons.testFlag(QMessageBox::Ok)) box->button(QMessageBox::Ok)->click();
            }
        }
    });
    driver.start(5);
    {
        const QString existingPath = fixture.filePath("save-existing.cpp");
        const QString protectedPath = fixture.filePath("save-protected.cpp");
        const QByteArray originalBytes = QString::fromUtf8("// caffè 😀\nint value = 1;").toUtf8();
        if (!write(existingPath, originalBytes) || !write(protectedPath, "// protected")) return 22;
        const QPointer<Editor> existing = documents->newEditor(existingPath, ENCODING_UTF8,
            FileType::CppSource, QString(), false, false);
        if (!existing) return 23;
        const DocumentSaveRequest saveExisting{DocumentSaveMode::CurrentPath, {}, {}, false};
        const auto typeAtEnd = [](Editor *editor, QChar character) {
            editor->setCaretXY(editor->fileEnd());
            QKeyEvent key(QEvent::KeyPress, character.toUpper().unicode(), Qt::NoModifier, QString(character));
            QApplication::sendEvent(editor, &key);
        };
        typeAtEnd(existing, QLatin1Char('x'));
        const auto before = documents->snapshotFor(existing);
        const auto originalEncoding = existing->fileEncoding();
        int failedSaves = 0;
        const auto failureObserver = QObject::connect(existing, &Editor::fileSaveError, &window,
            [&](Editor *, const QString &path, const QString &) { failedSaves += path == existingPath; });
        existing->convertToEncoding("ISO-8859-1");
        QObject::disconnect(failureObserver);
        check("Failed conversion preserves the current codec text Undo and original disk bytes",
              before && failedSaves == 1 && existing->editorEncoding() == ENCODING_UTF8 &&
              existing->fileEncoding() == originalEncoding && existing->content() == before->lines &&
              existing->modified() && existing->canUndo() && read(existingPath) == originalBytes);

        const auto postCommitEdit = QObject::connect(existing, &Editor::fileSaved, &window,
            [&](Editor *editor, const QString &) { typeAtEnd(editor, QLatin1Char('y')); });
        const auto withNewerEdit = existing->saveOperation(saveExisting);
        QObject::disconnect(postCommitEdit);
        check("Post-commit edits stay dirty and the receipt identifies the saved revision",
              before && withNewerEdit.outcome.status == OperationStatus::CommittedWithFollowUpPending &&
              withNewerEdit.textFile.state == ResourceCommitState::Committed &&
              withNewerEdit.savedRevision == before->revision && read(existingPath).contains("1;x") &&
              !read(existingPath).contains("1;xy") && existing->content().last().contains("1;xy") &&
              existing->modified() && existing->canUndo());
        trace("Begin undo");
        existing->undo();
        const bool savedRevisionIsClean = !existing->modified() && existing->content() == before->lines;
        trace("Begin redo");
        existing->redo();
        check("Undo returns to the committed revision and retry persists the newer edit",
              savedRevisionIsClean && existing->modified() && existing->saveOperation(saveExisting).outcome.succeeded() &&
              !existing->modified() && read(existingPath).contains("1;xy"));

        const auto preCommitEdit = QObject::connect(existing, &Editor::fileSaving, &window,
            [&](Editor *editor, const QString &) { typeAtEnd(editor, QLatin1Char('z')); });
        const auto rejectedEdit = existing->saveOperation(saveExisting);
        QObject::disconnect(preCommitEdit);
        check("Pre-commit edit invalidates the save plan without replacing disk bytes",
              rejectedEdit.outcome.status == OperationStatus::Conflict &&
              rejectedEdit.textFile.state == ResourceCommitState::NotAttempted &&
              existing->modified() && existing->content().last().contains("1;xyz") &&
              !read(existingPath).contains("1;xyz"));
        const auto preCommitRetarget = QObject::connect(existing, &Editor::fileSaving, &window,
            [&](Editor *editor, const QString &) { editor->setFilename(protectedPath); });
        const auto retargeted = existing->saveOperation(saveExisting);
        QObject::disconnect(preCommitRetarget);
        check("Pre-commit retarget cannot overwrite the newly selected destination",
              retargeted.outcome.status == OperationStatus::Conflict &&
              retargeted.textFile.state == ResourceCommitState::NotAttempted &&
              read(protectedPath) == "// protected" && !read(existingPath).contains("1;xyz") &&
              existing->modified());
        existing->setFilename(existingPath);
        existing->setModified(false);
        const auto oldId = documents->documentId(existing);
        QObject::connect(existing, &Editor::fileSaving, &window, [&](Editor *editor, const QString &) {
            documents->closeEditor(editor, false, false);
        });
        trace("Begin close from pre-save");
        const auto closedBeforeWrite = existing->saveOperation(saveExisting);
        check("Closing the view from a pre-commit observer returns without writing",
              !documents->documentId(existing) && !documents->editorForDocument(oldId) &&
              closedBeforeWrite.outcome.status == OperationStatus::Conflict &&
              closedBeforeWrite.textFile.state == ResourceCommitState::NotAttempted &&
              !read(existingPath).contains("1;xyz"));
    }

    const QString projectPath = fixture.filePath("fixture.dev");
    const QString protectedProjectPath = projectPath + ".protected";
    bool protectedMetadata = false;
    const auto cleanup = [&] {
        if (protectedMetadata) {
            QDir().rmdir(projectPath);
            QFile::rename(protectedProjectPath, projectPath);
            protectedMetadata = false;
        }
        const auto all = documents->editors();
        for (Editor *editor : all) {
            if (AnalysisInputSnapshot::pathKey(editor->filename()).startsWith(AnalysisInputSnapshot::pathKey(fixture.path()) + '/')) {
                editor->setModified(false);
                documents->closeEditor(editor, false, false, EditorCloseDecision::DiscardConfirmed);
            }
        }
        if (window.project() && window.project()->filename() == projectPath) {
            window.project()->setModified(false);
            QMetaObject::invokeMethod(&window, "on_actionClose_Project_triggered", Qt::DirectConnection);
        }
    };
    const auto fallbackCleanup = finally(cleanup);
    const QString templatePath = fixture.filePath("fixture.template");
    if (!write(templatePath,
        "[Template]\nVer=3\nName=Document fixture\n[Project]\nUnitCount=1\nType=1\nIsCpp=1\nModelType=1\nUseUTF8=1\nEncoding=UTF-8\n"
        "[Unit0]\nCppName=template.cpp\nCpp=class TemplateFixture { public: int value() const { return 7; } };#13#10\n")) return 4;
    auto projectTemplate = std::make_shared<ProjectTemplate>();
    projectTemplate->readTemplateFile(templatePath);
    auto created = Project::create(projectPath, "Document fixture", documents, window.iconsManager(),
                                   window.fileSystemWatcher(), projectTemplate, true);
    if (!created || created->unitList().size() != 1) return 5;
    auto createdUnit = created->unitList().first();
    const bool templateBound = createdUnit->documentId() != 0 && created->unitEditor(createdUnit)
            && created->unitEditor(createdUnit)->parser() == created->cppParser();
    created->saveAll();
    created.reset();
    window.openProject(projectPath, false);
    const auto project = window.project();
    if (!project || project->unitList().size() != 1) return 6;
    sourceUnit = project->unitList().first();
    sourceEditor = project->unitEditor(sourceUnit);
    if (!sourceEditor) return 7;
    setup = false;
    const DocumentId sourceId = documents->documentId(sourceEditor);
    check("Template project establishes one canonical unit document binding", templateBound && sourceId &&
          sourceUnit->documentId() == sourceId && project->findUnit(sourceEditor) == sourceUnit && sourceEditor->parser() == project->cppParser());

    // IO/notification contracts use the actual manager/controller. A bare
    // Editor intentionally refuses Save As because it has no registered owner.
    for (const bool newDocument : {false, true}) for (const bool directoryTarget : {true, false}) {
        const QString stem = QString("save-failure-%1-%2").arg(newDocument ? "new" : "existing", directoryTarget ? "directory" : "missing");
        const QString original = fixture.filePath(stem + ".cpp");
        const QByteArray originalBytes("int original = 1;");
        if (!newDocument && !write(original, originalBytes)) return 22;
        const QPointer<Editor> candidate = documents->newEditor(original, ENCODING_UTF8, FileType::CppSource, {}, false, newDocument);
        if (!candidate) return 23;
        candidate->setAutoBackupEnabled(false);
        if (newDocument) candidate->setContent("int original = 1;");
        candidate->setCaretXY(candidate->fileEnd());
        QKeyEvent typedX(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier, "x");
        QApplication::sendEvent(candidate, &typedX);
        const auto edited = candidate->content();
        const auto id = documents->documentId(candidate);
        const bool wasEditable = candidate->modified() && candidate->canUndo();
        QFileSystemWatcher watcher;
        if (!newDocument && !watcher.addPath(original)) return 24;
        candidate->setFileSystemWatcher(&watcher);
        int savedCount = 0, renamedCount = 0, failedCount = 0;
        QString reportedErrorPath;
        bool coherentSavedState = false;
        const QString retry = fixture.filePath(stem + "-retry.c");
        QObject::connect(candidate, &Editor::fileSaved, &window, [&](Editor *saved, const QString &name) {
            ++savedCount;
            coherentSavedState = saved == candidate && name == retry && candidate->filename() == retry &&
                candidate->fileType() == FileType::CSource && !candidate->isNew() && !candidate->modified();
        });
        QObject::connect(candidate, &Editor::fileSaveAsed, &window, [&](Editor *, const QString &, const QString &) { ++renamedCount; });
        QObject::connect(candidate, &Editor::fileSaveError, &window, [&](Editor *, const QString &name, const QString &) { ++failedCount; reportedErrorPath = name; });
        const QString target = directoryTarget ? fixture.path() : fixture.filePath(stem + "-missing/target.c");
        const auto failed = candidate->saveAsOperation(target, false);
        const bool failurePreserved = failed.outcome.status == OperationStatus::Failed && candidate &&
            candidate->filename() == original && candidate->fileType() == FileType::CppSource && candidate->isNew() == newDocument &&
            candidate->content() == edited && candidate->modified() && candidate->canUndo() &&
            documents->documentId(candidate) == id && savedCount == 0 && renamedCount == 0 && failedCount == 1 &&
            reportedErrorPath == target && watcher.files().contains(original) == !newDocument &&
            (newDocument ? !QFileInfo::exists(original) : read(original) == originalBytes);
        const auto retried = candidate->saveAsOperation(retry, false);
        const bool retryCoherent = retried.outcome.succeeded() && candidate && coherentSavedState &&
            savedCount == 1 && renamedCount == 1 && failedCount == 1 && documents->documentId(candidate) == id &&
            watcher.files().contains(retry) && !watcher.files().contains(original) &&
            read(retry).contains("int original = 1;x") && (newDocument ? !QFileInfo::exists(original) : read(original) == originalBytes);
        const QByteArray checkName = QString("Save As failure preserves %1 document for %2 and retries with coherent notifications")
            .arg(newDocument ? "new" : "existing", directoryTarget ? "directory target" : "missing parent").toLatin1();
        check(checkName.constData(), wasEditable && failurePreserved && retryCoherent);
        candidate->setFileSystemWatcher(nullptr);
        candidate->setModified(false);
        documents->closeEditor(candidate, false, false);
    }

    {
        const QString original = fixture.filePath("encoding-observer.cpp");
        if (!write(original, "int original = 1;")) return 25;
        const QPointer<Editor> candidate = documents->newEditor(original, ENCODING_UTF8, FileType::CppSource, {}, false, true);
        if (!candidate) return 26;
        candidate->setAutoBackupEnabled(false);
        candidate->setContent("int original = 1;"); candidate->setEditorEncoding(ENCODING_UTF8_BOM);
        candidate->setCaretXY(candidate->fileEnd());
        QKeyEvent typedX(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier, "x"); QApplication::sendEvent(candidate, &typedX);
        QFileSystemWatcher watcher;
        if (!watcher.addPath(original)) return 27;
        candidate->setFileSystemWatcher(&watcher);
        const QString destination = fixture.filePath("encoding-observer.c");
        int encodingCount = 0;
        bool coherentEncodingState = false;
        QObject::connect(candidate, &Editor::fileEncodingChanged, &window, [&](Editor *saved) {
            ++encodingCount;
            coherentEncodingState = saved == candidate && candidate->filename() == destination &&
                candidate->fileType() == FileType::CSource && !candidate->isNew() && !candidate->modified() &&
                watcher.files().contains(destination) && !watcher.files().contains(original) &&
                read(destination).startsWith(QByteArray::fromHex("efbbbf"));
        });
        const auto saved = candidate->saveAsOperation(destination, false);
        check("Save As encoding observers see committed identity type watcher and BOM bytes",
              saved.outcome.succeeded() && encodingCount == 1 && coherentEncodingState && candidate->fileEncoding() == ENCODING_UTF8_BOM &&
              read(original) == "int original = 1;");
        candidate->setFileSystemWatcher(nullptr); candidate->setModified(false);
        documents->closeEditor(candidate, false, false);
    }

    sourceUnit->setCompile(false); sourceUnit->setCompileCpp(false); sourceUnit->setLink(false);
    sourceUnit->setPriority(37); sourceUnit->setOverrideBuildCmd(true); sourceUnit->setBuildCmd("echo preserved");
    sourceUnit->setEncoding(ENCODING_PROJECT);
    const auto folder = project->addFolder(project->rootNode(), "kept");
    if (!folder) return 8;
    sourceUnit->setFolder("kept"); project->rebuildNodes();
    const auto flagsPreserved = [&] {
        return !sourceUnit->compile() && !sourceUnit->compileCpp() && !sourceUnit->link() &&
            sourceUnit->priority() == 37 && sourceUnit->overrideBuildCmd() && sourceUnit->buildCmd() == "echo preserved" &&
            sourceUnit->folder() == "kept" && sourceUnit->encoding() == ENCODING_PROJECT;
    };
    const QString originalPath = sourceUnit->fileName();
    const QByteArray originalBytes = read(originalPath);
    sourceEditor->setContent(QString::fromUtf8("// Città: caffè più\n")); sourceEditor->setModified(true);
    const QString utf8Path = fixture.filePath(QString::fromUtf8("città-UTF8.cpp"));
    const auto savedUtf8 = sourceEditor->saveAsOperation(utf8Path, false);
    check("Project Save As preserves unit identity document ID flags and inherited encoding",
          savedUtf8.outcome.succeeded() && savedUtf8.textFile.state == ResourceCommitState::Committed &&
          savedUtf8.projectMetadata.state == ResourceCommitState::Committed && sourceEditor->filename() == utf8Path &&
          sourceUnit->fileName() == utf8Path && project->findUnit(sourceEditor) == sourceUnit &&
          documents->documentId(sourceEditor) == sourceId && sourceUnit->documentId() == sourceId && flagsPreserved() &&
          read(originalPath) == originalBytes && read(utf8Path).contains(QString::fromUtf8("Città: caffè più").toUtf8()));

    const QString ansiPath = fixture.filePath("ANSI.cpp");
    if (!write(ansiPath, "// Initial ANSI file\n")) return 9;
    auto ansiUnit = project->addUnit(ansiPath, project->rootNode());
    const QPointer<Editor> ansiEditor = project->openUnit(ansiUnit, false);
    if (!ansiEditor) return 10;
    ansiUnit->setEncoding("WINDOWS-1252");
    ansiEditor->setContent(QString::fromUtf8("// Città: caffè più\n")); ansiEditor->setModified(true);
    const DocumentId ansiId = documents->documentId(ansiEditor);
    const QString ansiSavedPath = fixture.filePath("ANSI-saved.cpp");
    const auto savedAnsi = ansiEditor->saveAsOperation(ansiSavedPath, false);
    check("Project Save As keeps explicit ANSI encoding and the same document identity",
          savedAnsi.outcome.succeeded() && documents->documentId(ansiEditor) == ansiId && ansiUnit->documentId() == ansiId &&
          ansiUnit->encoding() == "WINDOWS-1252" && project->findUnit(ansiEditor) == ansiUnit &&
          read(ansiSavedPath).contains(QByteArray::fromHex("63616666e8")) && !read(ansiSavedPath).contains(QString::fromUtf8("caffè").toUtf8()));

    sourceEditor->setContent("// Before cancellation\n"); sourceEditor->setModified(true);
    const auto beforeCancel = documents->snapshotFor(sourceEditor);
    const auto metadataBeforeCancel = read(projectPath);
    decision = Decision::CancelSaveAs;
    const auto cancelled = sourceEditor->saveAsOperation(QString(), false);
    decision = Decision::None;
    const auto afterCancel = documents->snapshotFor(sourceEditor);
    check("Cancelled Save As preserves unit document revision metadata and unsaved text",
          cancelled.outcome.status == OperationStatus::Cancelled && sourceEditor->filename() == utf8Path &&
          beforeCancel && afterCancel && beforeCancel->id == afterCancel->id && beforeCancel->revision == afterCancel->revision &&
          read(projectPath) == metadataBeforeCancel && sourceEditor->modified() && flagsPreserved());

    selectedPath = fixture.filePath("stale-selection.cpp");
    decision = Decision::EditAndAcceptSaveAs;
    const auto changed = sourceEditor->saveAsOperation(QString(), false);
    decision = Decision::None;
    check("Save As rejects a document revision changed during its file dialog",
          changed.outcome.status == OperationStatus::Conflict && !QFileInfo::exists(selectedPath) &&
          sourceEditor->filename() == utf8Path && sourceUnit->fileName() == utf8Path &&
          sourceEditor->text().contains("New revision during Save As") && sourceEditor->modified() && flagsPreserved());

    const QString failedTextPath = fixture.filePath("missing-parent/file.cpp");
    const auto metadataBeforeTextFailure = read(projectPath);
    const auto failedText = sourceEditor->saveAsOperation(failedTextPath, false);
    check("Failed text commit leaves document binding project metadata and old disk file intact",
          failedText.outcome.status == OperationStatus::Failed && failedText.textFile.state == ResourceCommitState::Failed &&
          failedText.projectMetadata.state == ResourceCommitState::NotAttempted && sourceEditor->filename() == utf8Path &&
          sourceUnit->fileName() == utf8Path && sourceUnit->documentId() == sourceId &&
          read(projectPath) == metadataBeforeTextFailure && !QFileInfo::exists(failedTextPath) && flagsPreserved());

    const QString partialPath = fixture.filePath("partial-then-retry.cpp");
    const auto metadataBeforePartial = read(projectPath);
    if (!QFile::rename(projectPath, protectedProjectPath) || !QDir().mkpath(projectPath)) return 11;
    protectedMetadata = true;
    const auto partial = sourceEditor->saveAsOperation(partialPath, false);
    check("Metadata failure after text commit reports partial success without renaming the live document",
          partial.outcome.status == OperationStatus::CommittedWithFollowUpPending &&
          partial.textFile.state == ResourceCommitState::Committed && partial.projectMetadata.state == ResourceCommitState::Failed &&
          sourceEditor->filename() == utf8Path && sourceUnit->fileName() == utf8Path && sourceUnit->documentId() == sourceId &&
          sourceEditor->modified() && read(partialPath).contains("New revision during Save As") &&
          read(protectedProjectPath) == metadataBeforePartial && flagsPreserved());
    if (!QDir().rmdir(projectPath) || !QFile::rename(protectedProjectPath, projectPath)) return 12;
    protectedMetadata = false;
    const auto retried = sourceEditor->saveAsOperation(partialPath, false);
    check("Retry completes Save As while preserving the original unit and document identity",
          retried.outcome.succeeded() && retried.textFile.state == ResourceCommitState::Committed &&
          retried.projectMetadata.state == ResourceCommitState::Committed && sourceEditor->filename() == partialPath &&
          sourceUnit->fileName() == partialPath && project->findUnit(sourceEditor) == sourceUnit &&
          documents->documentId(sourceEditor) == sourceId && sourceUnit->documentId() == sourceId && flagsPreserved());

    const QString relinkPath = fixture.filePath("relink-existing.cpp");
    if (!write(relinkPath, "// Existing disk contents\n")) return 13;
    const QPointer<Editor> destination = documents->newEditor(relinkPath, ENCODING_UTF8, FileType::CppSource, QString(), false, false);
    if (!destination) return 14;
    destination->setContent("// Destination unsaved contents\n"); destination->setModified(true);
    sourceEditor->setContent("// Source unsaved contents kept independently\n"); sourceEditor->setModified(true);
    const QByteArray diskBeforeRelink = read(relinkPath), sourceDiskBeforeRelink = read(partialPath), metadataBeforeRelink = read(projectPath);
    decision = Decision::CancelRelink;
    const auto cancelledRelink = documents->relinkProjectUnit(project, sourceUnit, relinkPath);
    decision = Decision::None;
    check("Cancelled Relink preserves both documents unit metadata and source files",
          cancelledRelink.outcome.status == OperationStatus::Cancelled && sourceUnit->fileName() == partialPath &&
          sourceUnit->documentId() == sourceId && read(projectPath) == metadataBeforeRelink &&
          read(relinkPath) == diskBeforeRelink && read(partialPath) == sourceDiskBeforeRelink &&
          sourceEditor->modified() && destination->modified() && flagsPreserved());
    decision = Decision::ChangeUnitAndAcceptRelink;
    const auto staleRelink = documents->relinkProjectUnit(project, sourceUnit, relinkPath);
    decision = Decision::None;
    check("Relink rejects unit settings changed during confirmation without replacing current edits",
          staleRelink.outcome.status == OperationStatus::Conflict && sourceUnit->fileName() == partialPath &&
          sourceUnit->priority() == 38 && read(projectPath) == metadataBeforeRelink && sourceEditor->modified() && destination->modified());
    sourceUnit->setPriority(37);
    decision = Decision::AcceptRelink;
    const auto linked = documents->relinkProjectUnit(project, sourceUnit, relinkPath);
    decision = Decision::None;
    const DocumentId destinationId = documents->documentId(destination);
    check("Relink preserves the unit settings and binds only the explicitly accepted document",
          linked.outcome.succeeded() && linked.metadata.state == ResourceCommitState::Committed &&
          sourceUnit->fileName() == relinkPath && sourceUnit->documentId() == destinationId &&
          project->findUnit(destination) == sourceUnit && destination->parser() == project->cppParser() && !project->findUnit(sourceEditor) && flagsPreserved() &&
          documents->documentId(sourceEditor) == sourceId && sourceEditor->modified() &&
          sourceEditor->text().contains("Source unsaved contents kept independently") && destination->modified() &&
          destination->text().contains("Destination unsaved contents") && read(relinkPath) == diskBeforeRelink && read(partialPath) == sourceDiskBeforeRelink);

    setup = true;
    auto loadedProject = Project::load(projectPath, documents, window.iconsManager(), window.fileSystemWatcher());
    setup = false;
    const auto loadedUnit = loadedProject->findUnit(relinkPath);
    check("Relink metadata reload retains flags encoding and no runtime document identity",
          loadedUnit && loadedUnit->documentId() == 0 && !loadedUnit->compile() && !loadedUnit->compileCpp() && !loadedUnit->link() &&
          loadedUnit->priority() == 37 && loadedUnit->overrideBuildCmd() && loadedUnit->buildCmd() == "echo preserved" &&
          loadedUnit->folder() == "kept" && loadedUnit->encoding() == ENCODING_PROJECT &&
          loadedUnit->relocation().state == ProjectUnit::RelocationState::Resolved && !loadedProject->unitEditor(loadedUnit));
    loadedProject.reset();

    const QString distinctA = fixture.filePath("k.cpp"), distinctB = fixture.filePath(QString::fromUtf8("K.cpp"));
    if (!write(distinctA, "// K disk\n") || !write(distinctB, "// Kelvin disk\n")) return 18;
    auto distinctUnitA = project->addUnit(distinctA, project->rootNode());
    auto distinctUnitB = project->addUnit(distinctB, project->rootNode());
    const QPointer<Editor> distinctEditorA = project->openUnit(distinctUnitA, false);
    const QPointer<Editor> distinctEditorB = project->openUnit(distinctUnitB, false);
    if (!distinctEditorA || !distinctEditorB) return 19;
    distinctEditorA->setContent("// K unsaved\n"); distinctEditorA->setModified(true);
    distinctEditorB->setContent("// Kelvin unsaved\n"); distinctEditorB->setModified(true);
    check("Native path identity keeps distinct Unicode project files and documents separate",
          sameExistingFile(distinctA, distinctB) == ExistingFileMatch::Different && distinctUnitA != distinctUnitB &&
          distinctEditorA != distinctEditorB && distinctUnitA->documentId() != distinctUnitB->documentId() &&
          project->findUnit(distinctA) == distinctUnitA && project->findUnit(distinctB) == distinctUnitB &&
          documents->getOpenedEditor(distinctA) == distinctEditorA && documents->getOpenedEditor(distinctB) == distinctEditorB &&
          distinctEditorA->text().contains("K unsaved") && distinctEditorB->text().contains("Kelvin unsaved"));
    const auto unitCountBeforeRejectedNew = project->unitList().size();
    selectedPath = distinctA; decision = Decision::RejectNewUnit;
    const auto rejectedNew = project->newUnit(project->rootNode(), distinctA);
    decision = Decision::None;
    check("Rejected new project unit preserves existing membership and document without dereferencing null",
          !rejectedNew && project->unitList().size() == unitCountBeforeRejectedNew &&
          project->findUnit(distinctA) == distinctUnitA && project->unitEditor(distinctUnitA) == distinctEditorA &&
          distinctEditorA->text().contains("K unsaved") && read(distinctA) == "// K disk\n");

    const QString invalidProject = fixture.filePath("ambiguous-project.dev");
    const QByteArray invalidMetadata = "[Project]\nVer=3\nUnitCount=2\nResources=legacy.rc\n"
        "[Unit1]\nFileName=k.cpp\n[Unit2]\nFileName=K.cpp\n";
    if (!write(invalidProject, invalidMetadata)) return 20;
    bool rejectedCandidate = false;
    try { Project::load(invalidProject, documents, window.iconsManager(), window.fileSystemWatcher()); }
    catch (const FileError &) { rejectedCandidate = true; }
    check("Ambiguous project paths are rejected before legacy conversion metadata or live state changes",
          rejectedCandidate && read(invalidProject) == invalidMetadata && !QFileInfo::exists(invalidProject + ".bak") &&
          window.project() == project && project->findUnit(distinctA) == distinctUnitA && project->findUnit(distinctB) == distinctUnitB &&
          documents->documentId(distinctEditorA) == distinctUnitA->documentId() && documents->documentId(distinctEditorB) == distinctUnitB->documentId());

    const QString legacyProject = fixture.filePath("legacy-resources.dev");
    const QByteArray legacyMetadata = "[Project]\nVer=3\nType=1\nIsCpp=1\nModelType=1\nUnitCount=1\nResources=legacy.rc\n"
        "[Unit1]\nFileName=k.cpp\nCompileCpp=0\nPriority=27\n";
    if (!write(legacyProject, legacyMetadata) || !write(legacyProject + ".bak", "existing backup") ||
            !write(fixture.filePath("legacy.rc"), "// resource\n")) return 21;
    setup = true;
    auto convertedProject = Project::load(legacyProject, documents, window.iconsManager(), window.fileSystemWatcher());
    setup = false;
    const auto oldLegacyUnit = convertedProject->findUnit(distinctA);
    const auto newLegacyUnit = convertedProject->findUnit(fixture.filePath("legacy.rc"));
    bool preservedOriginalMetadata = false;
    for (const auto &name : QDir(fixture.path()).entryList({"legacy-resources.dev.bak.*"}, QDir::Files))
        preservedOriginalMetadata |= read(fixture.filePath(name)) == legacyMetadata;
    check("Validated legacy conversion preserves original units and existing backup files",
          convertedProject->unitList().size() == 2 && oldLegacyUnit && !oldLegacyUnit->compileCpp() && oldLegacyUnit->priority() == 27 &&
          newLegacyUnit && read(legacyProject + ".bak") == "existing backup" && preservedOriginalMetadata && window.project() == project);
    convertedProject.reset();
    sourceEditor->setModified(false);
    documents->closeEditor(sourceEditor, false, false);
    const QPointer<Editor> reopened = documents->newEditor(partialPath, ENCODING_UTF8, FileType::CppSource, QString(), false, false);
    check("Reopening an old path creates a new document and cannot steal the unit binding",
          reopened && documents->documentId(reopened) != sourceId && !project->findUnit(reopened) &&
          sourceUnit->documentId() == destinationId && project->unitEditor(sourceUnit) == destination);
    if (!reopened) return 16;
    destination->setModified(false);
    documents->closeEditor(destination, false, false);
    const bool closedBinding = sourceUnit->documentId() == 0;
    const auto closedTargetBytes = read(relinkPath);
    const auto refusedStandalone = reopened->saveAsOperation(relinkPath, false);
    check("Standalone Save As cannot overwrite a closed file owned by another project unit",
          refusedStandalone.outcome.status == OperationStatus::Conflict && sourceUnit->documentId() == 0 &&
          read(relinkPath) == closedTargetBytes && reopened->filename() == partialPath && !project->findUnit(reopened));
    const QPointer<Editor> reopenedUnit = window.openFile(sourceUnit->fileName(), false);
    check("Closing and reopening a project file replaces only its runtime document binding",
          closedBinding && reopenedUnit && sourceUnit->documentId() != destinationId && sourceUnit->documentId() != 0 &&
          project->unitEditor(sourceUnit) == reopenedUnit && project->findUnit(reopenedUnit) == sourceUnit &&
          reopenedUnit->parser() == project->cppParser() && flagsPreserved());
    if (!reopenedUnit) return 17;
    for (const auto &unit : project->unitList())
        if (auto *editor = project->unitEditor(unit)) editor->setModified(false);
    // Closing a project legitimately persists its current membership and
    // observed encoding. Start this authority test from that committed state.
    project->saveAll();
    const auto metadataBeforeClose = read(projectPath);
    const auto closingId = documents->documentId(reopenedUnit);
    selectedPath = fixture.filePath("closed-during-save-as.cpp");
    decision = Decision::CloseProjectAndAcceptSaveAs;
    const auto closedDuringSaveAs = reopenedUnit->saveAsOperation(QString(), false);
    decision = Decision::None;
    const QJsonObject closingFacts{{"outcome", int(closedDuringSaveAs.outcome.status)},
        {"error", closedDuringSaveAs.outcome.error}, {"editorAlive", bool(reopenedUnit)},
        {"registeredId", qint64(documents->documentId(reopenedUnit))}, {"oldIdStillRegistered", bool(documents->editorForDocument(closingId))},
        {"activeProject", bool(window.project())}, {"targetExists", QFileInfo::exists(selectedPath)},
        {"metadataEqual", read(projectPath) == metadataBeforeClose}};
    check("Closing the project during Save As preserves metadata and cancels stale document authority",
          closedDuringSaveAs.outcome.status == OperationStatus::Conflict && !documents->documentId(reopenedUnit) && !window.project() &&
          !documents->editorForDocument(closingId) && !QFileInfo::exists(selectedPath) && read(projectPath) == metadataBeforeClose);
    cleanup();
    check("Document file fixture closes its project without unexpected dialogs", !window.project() && unexpectedDialogs == 0 && outcomeMessages >= 4);
    driver.stop();
    QFile report(QDir(outputDirectory).filePath("document-files-result.json"));
    const auto bytes = QJsonDocument(QJsonObject{{"checks", checks}, {"failures", failures}, {"closingFacts", closingFacts}}).toJson();
    if (!report.open(QIODevice::WriteOnly) || report.write(bytes) != bytes.size()) return 15;
    return failures == 0 ? 0 : 1;
}
