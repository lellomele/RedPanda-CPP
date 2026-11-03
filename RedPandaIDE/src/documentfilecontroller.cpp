// SPDX-License-Identifier: GPL-3.0-or-later
#include "documentfilecontroller.h"
#include "editor.h"
#include "editormanager.h"
#include "mainwindow.h"
#include "project.h"
#include "systemconsts.h"
#include "settings/editorsettings.h"
#include <qsynedit/document.h>
#include <QFileDialog>
#include <QMessageBox>
#include <QFileSystemWatcher>
#include <QPointer>
#include <QSaveFile>
#include <QScopeGuard>
#include <QAbstractButton>
#include <optional>

namespace {
struct DocumentVersion { DocumentId id; DocumentRevision revision; QString filename; };
std::optional<DocumentVersion> versionOf(const EditorManager &documents, const Editor *editor)
{
    const auto id = documents.documentId(editor);
    const auto revision = documents.documentRevision(editor);
    if (!id || !revision) return {};
    return DocumentVersion{id, revision, editor->filename()};
}
}

QString DocumentFileController::message(const char *text)
{
    return QCoreApplication::translate("DocumentFileController", text);
}

void DocumentFileController::showOutcome(const QString &title, const QString &text)
{
    QMessageBox box(QMessageBox::Warning, title, text, QMessageBox::Ok, nullptr);
    box.setObjectName("documentFileOperationMessage");
    box.setWindowModality(Qt::ApplicationModal);
    box.exec();
}

DocumentSaveAsResult DocumentFileController::save(Editor *candidate, const DocumentSaveRequest &request)
{
    DocumentSaveAsResult result;
    const bool choosePath = request.mode == DocumentSaveMode::ChoosePath;
    const auto &requestedName = request.destination;
    const auto title = choosePath ? message("Save As") : QCoreApplication::translate("Editor", "Save Error");
    const QPointer<EditorManager> documents(&mDocuments);
    const QPointer<Editor> editor(candidate);
    const QPointer<MainWindow> window(mDocuments.mainWindow());
    const auto original = versionOf(mDocuments, candidate);
    const auto project = window ? window->project() : std::shared_ptr<Project>{};
    const auto unit = project ? project->findUnit(candidate) : PProjectUnit{};
    const auto isRegistered = [&] {
        return documents && editor && documents->documentId(editor) != 0
            && (!result.document || documents->documentId(editor) == result.document);
    };
    const auto stop = [&](OperationResult outcome) {
        result.outcome = std::move(outcome);
        if (isRegistered() && result.outcome.status == OperationStatus::Failed) {
            const QString failedPath = result.textFile.path.isEmpty() ? editor->filename() : result.textFile.path;
            emit editor->fileSaveError(editor, failedPath, result.outcome.error);
        }
        // An observer may close the document synchronously on fileSaveError.
        // Recheck its guarded lifetime before entering a modal interaction.
        if (isRegistered() && !result.outcome.error.isEmpty())
            showOutcome(title, result.outcome.error);
        return result;
    };
    if (!documents || !window || !editor || !original)
        return stop(OperationResult::conflict(message("The document is changing or no longer available.")));
    result.document = original->id;
    const auto saving = mSaving;
    if (saving->contains(result.document)) {
        result.outcome = OperationResult::conflict(message("This document is already being saved."));
        return result;
    }
    saving->insert(result.document);
    const auto release = qScopeGuard([saving, id = result.document] { saving->remove(id); });
    const QString originalPath = documents->documentAbsolutePath(editor);
    const QByteArray originalCodec = editor->mEditorEncoding;
    const QByteArray codec = request.convertTo.isEmpty() ? originalCodec : request.convertTo;
    const auto validateSource = [&](DocumentRevision revision) {
        if (!documents || !window || !editor)
            return OperationResult::conflict(message("The document is no longer available."));
        const auto source = versionOf(*documents, editor);
        if (!source || source->id != original->id || source->revision != revision || source->filename != original->filename ||
                documents->documentAbsolutePath(editor) != originalPath || editor->mEditorEncoding != originalCodec)
            return OperationResult::conflict(choosePath ? message("The document changed while preparing Save As. Please try again.")
                                              : message("The document changed while preparing the save. Please try again."));
        const auto activeProject = window->project();
        const auto activeUnit = activeProject ? activeProject->findUnit(editor) : PProjectUnit{};
        if (activeUnit != unit || activeProject != project)
            return OperationResult::conflict(choosePath ? message("Project membership changed while preparing Save As. Please try again.")
                                              : message("Project membership changed while preparing the save. Please try again."));
        return OperationResult::success();
    };
    UnitLocationPlan::Pointer originalUnit;
    if (unit) {
        const auto prepared = project->prepareUnitLocation(unit, UnitLocationCommand::InspectSource,
                                                           unit->fileName(), editor, originalUnit);
        if (!prepared.succeeded()) return stop(prepared);
    }
    QString newName = choosePath ? requestedName : originalPath;
    if (choosePath && newName.isEmpty()) {
        QString suffix = QFileInfo(original->filename).suffix();
        QString filter;
        if (suffix.isEmpty()) {
            suffix = editor->mEditorSettings->defaultFileCpp() ? "cpp" : "c";
            filter = editor->mEditorSettings->defaultFileCpp() ? mSystemConsts.defaultCPPFileFilter() : mSystemConsts.defaultCFileFilter();
        } else filter = mSystemConsts.fileFilterFor(suffix);
        QFileDialog dialog(nullptr, message("Save As"), extractFilePath(original->filename),
                           mSystemConsts.defaultFileFilters().join(";;"));
        // A source editor can be closed by a reentrant operation. The stack
        // dialog must not be a QObject child that dies with that editor.
        dialog.setWindowModality(Qt::ApplicationModal);
        dialog.selectNameFilter(filter);
        dialog.selectFile(original->filename);
        dialog.setFileMode(QFileDialog::AnyFile);
        dialog.setAcceptMode(QFileDialog::AcceptSave);
        dialog.setDefaultSuffix(suffix);
        dialog.setObjectName("documentSaveAsFileDialog");
        if (!QCoreApplication::instance()->property("uiTestConfigPath").toString().isEmpty())
            dialog.setOption(QFileDialog::DontUseNativeDialog);
        if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) {
            result.outcome = OperationResult::cancelled();
            return result;
        }
        newName = dialog.selectedFiles().first();
    }
    if (newName.isEmpty())
        return stop(OperationResult::conflict(message("The document has no save destination.")));
    newName = cleanPath(QFileInfo(newName).absoluteFilePath());
    result.textFile.path = newName;
    auto valid = validateSource(original->revision);
    if (!valid.succeeded()) return stop(valid);
    if (originalUnit) {
        valid = project->validateUnitLocation(*originalUnit);
        if (!valid.succeeded()) return stop(valid);
    }
    const auto targetPath = project ? project->resolveUnitPath(newName) : ProjectPathResult{};
    if (targetPath.status == ProjectPathStatus::Conflict) return stop(OperationResult::conflict(targetPath.error));
    const auto targetUnit = targetPath.unit;
    if (targetUnit && targetUnit != unit)
        return stop(OperationResult::conflict(message("The destination already belongs to another project file. Choose a different path.")));
    auto occupiedPath = documents->resolveOpenedEditor(newName);
    if (occupiedPath.status == EditorPathStatus::Conflict) return stop(OperationResult::conflict(occupiedPath.error));
    Editor *occupied = occupiedPath.editor;
    if (occupied && occupied != editor)
        return stop(OperationResult::conflict(message("The destination is open in another document.")));
    const QFileInfo destinationBefore(newName);
    const bool destinationExists = destinationBefore.exists();
    const qint64 destinationSize = destinationBefore.size();
    const auto destinationTime = destinationBefore.lastModified();
    const QString destinationCanonical = destinationBefore.canonicalFilePath();
    if (destinationExists && !destinationBefore.isFile())
        return stop(OperationResult::failure(message("The destination is not a regular file.")));

    try {
        if (editor->mEditorSettings->autoFormatWhenSaved()) editor->reformat(false);
        else if (editor->mEditorSettings->removeTrailingSpacesWhenSaved()) editor->trimTrailingSpaces();
    } catch (const BaseError &error) {
        return stop(OperationResult::failure(error.reason()));
    }
    valid = validateSource(documents && editor ? documents->documentRevision(editor) : 0);
    if (!valid.succeeded()) return stop(valid);
    const auto saved = documents->snapshotFor(editor);
    if (!saved || saved->id != original->id)
        return stop(OperationResult::conflict(message("The document is changing or no longer available.")));
    result.savedRevision = saved->revision;
    UnitLocationPlan::Pointer location;
    if (choosePath && unit) {
        valid = project->prepareUnitLocation(unit, UnitLocationCommand::SaveDocumentAs, newName, editor, location);
        if (!valid.succeeded()) return stop(valid);
        result.projectMetadata.path = project->filename();
    }
    const QByteArray oldEncoding = editor->mFileEncoding;
    const auto textDocument = editor->document();
    const auto separator = textDocument->lineBreak();
    const bool appendFinalSeparator = textDocument->appendNewLineAtEOF();
    const QPointer<QFileSystemWatcher> watcher(editor->mFileSystemWatcher);
    emit editor->fileSaving(editor, newName);
    valid = validateSource(saved->revision);
    if (valid.succeeded() && (textDocument->lineBreak() != separator ||
            textDocument->appendNewLineAtEOF() != appendFinalSeparator))
        valid = OperationResult::conflict(message("The document line ending changed while preparing the save."));
    if (valid.succeeded() && location) valid = project->validateUnitLocation(*location);
    if (valid.succeeded() && !location && originalUnit) valid = project->validateUnitLocation(*originalUnit);
    if (valid.succeeded()) {
        const auto target = documents->resolveOpenedEditor(newName);
        if (target.status == EditorPathStatus::Conflict)
            valid = OperationResult::conflict(target.error);
        else if (target.editor && target.editor != editor)
            valid = OperationResult::conflict(message("The destination is open in another document."));
    }
    const QFileInfo destinationNow(newName);
    if (valid.succeeded() && (destinationNow.exists() != destinationExists || destinationNow.size() != destinationSize
            || destinationNow.lastModified() != destinationTime || destinationNow.canonicalFilePath() != destinationCanonical))
        valid = OperationResult::conflict(choosePath ? message("The destination changed while preparing Save As. Please try again.")
                                                    : message("The destination changed while preparing the save. Please try again."));
    if (!valid.succeeded()) return stop(valid);
    const bool targetWatched = watcher && watcher->files().contains(newName) && watcher->removePath(newName);
    const auto restoreTargetWatch = qScopeGuard([&] {
        if (watcher && targetWatched && isRegistered() && editor->filename() == original->filename &&
                QFileInfo(newName).exists() && !watcher->files().contains(newName))
            watcher->addPath(newName);
    });

    QByteArray writtenEncoding;
    try {
        QSaveFile file(newName);
        file.setDirectWriteFallback(false);
        if (!file.open(QIODevice::WriteOnly))
            throw FileError(QCoreApplication::translate("Editor", "Cannot save '%1': %2").arg(newName, file.errorString()));
        QSynedit::Document::writeText(file, saved->lines, separator, appendFinalSeparator, codec, writtenEncoding);
        if (!file.commit())
            throw FileError(QCoreApplication::translate("Editor", "Cannot save '%1': %2").arg(newName, file.errorString()));
        result.textFile.state = ResourceCommitState::Committed;
    } catch (const FileError &error) {
        result.textFile.state = ResourceCommitState::Failed;
        result.textFile.error = error.reason();
        return stop(OperationResult::failure(error.reason()));
    }

    OperationResult completion = OperationResult::success();
    if (location) {
        const auto committed = project->commitUnitLocation(*location);
        result.projectMetadata = committed.metadata;
        completion = committed.outcome;
        if (committed.projection != UnitProjectionState::Applied) {
            if (committed.metadata.state == ResourceCommitState::Committed) {
                completion = project->deferUnitLocation(*location, completion.error);
                return stop({OperationStatus::CommittedWithFollowUpPending,
                    message("The file and project metadata were saved, but the document could not adopt the new path. Its contents were preserved; resolve the project file before building.\n%1")
                        .arg(completion.error)});
            }
            return stop({OperationStatus::CommittedWithFollowUpPending,
                message("The file was saved to '%1', but the project reference was not updated. The current document keeps its original path.\n%2")
                    .arg(newName, completion.error)});
        }
    }
    if (!documents || !editor)
        return stop({OperationStatus::CommittedWithFollowUpPending,
                     message("The file was saved, but its document was closed before the new path could be applied.")});
    occupiedPath = documents->resolveOpenedEditor(newName);
    occupied = occupiedPath.editor;
    if (occupiedPath.status == EditorPathStatus::Conflict || (occupied && occupied != editor)) {
        const QString error = occupiedPath.status == EditorPathStatus::Conflict ? occupiedPath.error
            : message("The file was saved, but its destination was opened in another document. Both buffers were preserved.");
        if (location) project->deferUnitLocation(*location, error);
        return stop({OperationStatus::CommittedWithFollowUpPending, error});
    }
    const auto pending = [&](QString error) {
        result.outcome = {OperationStatus::CommittedWithFollowUpPending, std::move(error)};
        return result;
    };
    const auto interrupted = [&] {
        return pending(message("The file was saved, but the document changed before the operation finished."));
    };
    const auto hasPath = [&](const QString &name) {
        return documents && editor && documents->documentId(editor) == result.document && editor->filename() == name;
    };
    if (!hasPath(original->filename) || editor->mEditorEncoding != originalCodec)
        return interrupted();
    const QString adoptedName = choosePath ? newName : original->filename;
    const auto expectedRevision = saved->revision + (adoptedName != original->filename ? 1 : 0);
    editor->mFileEncoding = writtenEncoding;
    editor->mEditorEncoding = codec;
    editor->mIsNew = false;
    if (choosePath) {
        editor->clearSyntaxIssues();
        if (!hasPath(original->filename)) return interrupted();
        if (editor->mCodeCompletionEnabled && editor->mParser && !editor->inProject())
            editor->mParser->invalidateFile(original->filename);
        editor->setFilename(adoptedName);
        if (!hasPath(adoptedName)) return interrupted();
        editor->setFileType(getFileType(adoptedName), false);
        if (!hasPath(adoptedName)) return interrupted();
        if (!editor->syntaxer() || editor->syntaxer()->language() != QSynedit::ProgrammingLanguage::CPP)
            editor->mSyntaxIssues.clear();
        if (!editor->shouldOpenInReadonly()) editor->setReadOnly(false);
        if (!hasPath(adoptedName)) return interrupted();
        if (requestedName.isEmpty()) QDir::setCurrent(extractFileDir(newName));
    }
    if (watcher) {
        if (choosePath && originalPath != newName && watcher->files().contains(originalPath))
            watcher->removePath(originalPath);
        if (!watcher->files().contains(newName))
            watcher->addPath(newName);
    }
    if (documents->documentRevision(editor) == expectedRevision)
        editor->setModified(false);
    if (!hasPath(adoptedName)) return interrupted();
    editor->setStatusChanged(QSynedit::StatusChange::Custom0);
    if (!hasPath(adoptedName)) return interrupted();
    if (oldEncoding != writtenEncoding) emit editor->fileEncodingChanged(editor);
    if (!hasPath(adoptedName)) return interrupted();
    if (codec != originalCodec) emit editor->editorEncodingChanged(editor);
    if (!hasPath(adoptedName)) return interrupted();
    if (choosePath) emit editor->fileSaveAsed(editor, original->filename, newName);
    if (!hasPath(adoptedName)) return interrupted();
    emit editor->fileSaved(editor, newName);
    if (!hasPath(adoptedName)) return interrupted();

    if (choosePath) editor->setCppParser();
    if (!hasPath(adoptedName)) return interrupted();
    if (choosePath || (request.checkSyntax && editor->isVisible())) {
        editor->reparse();
        if (!hasPath(adoptedName)) return interrupted();
        if (request.checkSyntax && editor->mEditorSettings->syntaxCheckWhenSave()) editor->checkSyntaxInBack();
        if (!hasPath(adoptedName)) return interrupted();
        editor->reparseTodo();
        if (!hasPath(adoptedName)) return interrupted();
    }
    if (choosePath) editor->initAutoBackup();
    if (!hasPath(adoptedName)) return interrupted();
    if (editor->modified() || documents->documentRevision(editor) != expectedRevision || editor->mEditorEncoding != codec)
        return pending(message("The file was saved, but newer edits remain unsaved in the document."));
    return stop(completion);
}

UnitLocationCommitResult DocumentFileController::relink(const std::shared_ptr<Project> &project,
                                                        const std::shared_ptr<ProjectUnit> &unit,
                                                        const QString &requestedName)
{
    UnitLocationCommitResult result;
    const QPointer<EditorManager> documents(&mDocuments);
    const QPointer<MainWindow> window(mDocuments.mainWindow());
    const auto stop = [&](OperationResult outcome) {
        result.outcome = std::move(outcome);
        if (window && !result.outcome.error.isEmpty())
            showOutcome(message("Relink file"), result.outcome.error);
        return result;
    };
    if (!documents || !window || !project || window->project() != project || !unit)
        return stop(OperationResult::conflict(message("The selected project file is no longer available.")));
    UnitLocationPlan::Pointer source;
    auto valid = project->prepareUnitLocation(unit, UnitLocationCommand::InspectSource,
                                              unit->fileName(), project->unitEditor(unit), source);
    if (!valid.succeeded()) return stop(valid);
    QString selected = requestedName;
    if (selected.isEmpty()) {
        const QString suggested = unit->relocation().expectedFileName.isEmpty()
                ? unit->fileName() : unit->relocation().expectedFileName;
        QFileDialog dialog(nullptr, message("Relink file"), suggested, mSystemConsts.defaultFileFilters().join(";;"));
        dialog.setWindowModality(Qt::ApplicationModal);
        dialog.setObjectName("projectRelinkFileDialog");
        dialog.setFileMode(QFileDialog::ExistingFile);
        dialog.setAcceptMode(QFileDialog::AcceptOpen);
        if (!QCoreApplication::instance()->property("uiTestConfigPath").toString().isEmpty())
            dialog.setOption(QFileDialog::DontUseNativeDialog);
        if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) {
            result.outcome = OperationResult::cancelled();
            return result;
        }
        selected = dialog.selectedFiles().first();
    }
    if (!documents || !window || window->project() != project)
        return stop(OperationResult::conflict(message("The project changed while choosing the recovery file.")));
    valid = project->validateUnitLocation(*source);
    if (!valid.succeeded()) return stop(valid);
    const auto destination = documents->resolveOpenedEditor(selected);
    if (destination.status == EditorPathStatus::Conflict) return stop(OperationResult::conflict(destination.error));
    const QPointer<Editor> accepted = destination.editor;
    UnitLocationPlan::Pointer plan;
    valid = project->prepareUnitLocation(unit, UnitLocationCommand::RelinkExistingFile, selected, accepted, plan);
    if (!valid.succeeded()) return stop(valid);
    QMessageBox confirmation(QMessageBox::Question, message("Relink file"),
        message("Relink '%1' to the existing file '%2'?\nThe project's build and encoding settings for this file will be kept.")
            .arg(source->sourcePath(), plan->targetPath()), QMessageBox::Yes | QMessageBox::Cancel, nullptr);
    confirmation.setWindowModality(Qt::ApplicationModal);
    confirmation.setObjectName("projectRelinkConfirmation");
    confirmation.setDefaultButton(QMessageBox::Cancel);
    confirmation.button(QMessageBox::Yes)->setText(accepted ? message("Relink and use open document") : message("Relink"));
    if (accepted)
        confirmation.setInformativeText(accepted->modified()
            ? message("The selected file is open with unsaved changes. Its text will be preserved; a later Save will write those changes to the selected file.")
            : message("The selected file is already open. That document will be associated with this project file."));
    if (confirmation.exec() != QMessageBox::Yes) {
        result.outcome = OperationResult::cancelled();
        return result;
    }
    if (!documents || !window || window->project() != project)
        return stop(OperationResult::conflict(message("The project changed while confirming the recovery file.")));
    valid = project->validateUnitLocation(*plan);
    if (!valid.succeeded()) return stop(valid);
    result = project->commitUnitLocation(*plan);
    if (!result.outcome.succeeded()) return stop(result.outcome);
    if (!accepted) {
        auto recovered = unit;
        try { project->openUnit(recovered, false); }
        catch (const FileError &error) {
            return stop({OperationStatus::CommittedWithFollowUpPending, error.reason()});
        }
    }
    return result;
}
