// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef EDITORCLOSEPLAN_H
#define EDITORCLOSEPLAN_H
#include "operationresult.h"
#include "documentstore.h"
#include <QList>
#include <QHash>
#include <QPointer>
#include <functional>

class Editor;
class EditorManager;
enum class EditorCloseScope { PlannedEditors, AllEditors, ProjectEditors };

// Close decisions name a registered document and its revision. A staged
// discard leaves the live document dirty until close commits.
class EditorClosePlan {
public:
    using Decide = std::function<OperationResult(Editor *, bool &discard)>;
    OperationResult prepare(const EditorManager &manager, const QList<Editor *> &editors, const Decide &decide);
    OperationResult validate(const EditorManager &manager, EditorCloseScope scope) const;
    OperationResult validateEditor(const EditorManager &manager, Editor *editor) const;
    bool discards(Editor *editor) const;
    QList<QPointer<Editor>> editors() const;
private:
    struct Entry {
        QPointer<Editor> editor;
        DocumentId document;
        DocumentRevision revision;
        QString filename;
        QByteArray encoding;
        bool modified = false;
        bool discard = false;
    };
    QList<Entry> mEntries;
    QHash<Editor *, qsizetype> mEntryForEditor;
};
#endif
