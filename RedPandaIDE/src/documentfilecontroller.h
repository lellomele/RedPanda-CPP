// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef DOCUMENTFILECONTROLLER_H
#define DOCUMENTFILECONTROLLER_H
#include "documentfileoperation.h"
#include "unitlocationplan.h"
#include <QSet>
class Editor;
class EditorManager;
class SystemConsts;
class Project;
class ProjectUnit;

// One GUI-owned coordinator for document saves. Editor delegates; Project owns unit
// state; EditorManager/DocumentStore own document identities and revisions.
class DocumentFileController final {
public:
    DocumentFileController(EditorManager &documents, SystemConsts &systemConsts)
        : mDocuments(documents), mSystemConsts(systemConsts) {}
    DocumentSaveAsResult save(Editor *editor, const DocumentSaveRequest &request);
    bool isSaving(DocumentId id) const { return mSaving->contains(id); }
    UnitLocationCommitResult relink(const std::shared_ptr<Project> &project,
                                    const std::shared_ptr<ProjectUnit> &unit, const QString &requestedName = QString());
private:
    static QString message(const char *text);
    static void showOutcome(const QString &title, const QString &text);
    EditorManager &mDocuments;
    SystemConsts &mSystemConsts;
    // Operation copies may outlive the manager while a file dialog is open.
    std::shared_ptr<QSet<DocumentId>> mSaving = std::make_shared<QSet<DocumentId>>();
};
#endif
