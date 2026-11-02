// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef EDITORCLOSEPLAN_H
#define EDITORCLOSEPLAN_H
#include "operationresult.h"
#include <QList>
#include <QPointer>
#include <QStringList>
#include <functional>

class Editor;
class EditorManager;
enum class EditorCloseScope { PlannedEditors, AllEditors, ProjectEditors };

// GUI-controller adapter while DocumentStore ownership is being introduced.
// Decisions are staged; a Discard never marks a live document unmodified.
// The text snapshot is taken only at close preparation, not on every keystroke.
class EditorClosePlan {
public:
    using Decide = std::function<OperationResult(Editor *, bool &discard)>;
    OperationResult prepare(const QList<Editor *> &editors, const Decide &decide);
    OperationResult validate(const EditorManager &manager, EditorCloseScope scope) const;
    OperationResult validateEditor(Editor *editor) const;
    bool discards(Editor *editor) const;
    QList<QPointer<Editor>> editors() const;
private:
    struct Entry {
        QPointer<Editor> editor;
        QString filename;
        QStringList content;
        bool modified = false;
        bool discard = false;
    };
    QList<Entry> mEntries;
};
#endif
