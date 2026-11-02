// SPDX-License-Identifier: GPL-3.0-or-later
#include "editorcloseplan.h"
#include "editor.h"
#include "editormanager.h"
#include <QCoreApplication>
#include <QSet>

namespace {
OperationResult changed() {
    return OperationResult::conflict(QCoreApplication::translate("EditorClosePlan",
        "Open documents changed while preparing to close. No further documents were closed. Please try again."));
}
}

OperationResult EditorClosePlan::prepare(const QList<Editor *> &editors, const Decide &decide)
{
    mEntries.clear();
    // Capture weak identities before the first dialog can run a nested loop.
    QList<QPointer<Editor>> candidates;
    for (Editor *editor : editors)
        candidates.append(editor);
    for (const auto &candidate : candidates) {
        if (!candidate)
            return changed();
        Entry entry{candidate, candidate->filename(), candidate->content(), candidate->modified(), false};
        if (entry.modified) {
            const OperationResult outcome = decide(candidate.data(), entry.discard);
            if (!outcome.succeeded())
                return outcome;
            if (!candidate)
                return changed();
            if (!entry.discard) {
                if (candidate->modified())
                    return changed();
                // A confirmed Save As may change path and encoding. It is an
                // actual commit, not something a later cancellation rolls back.
                entry.filename = candidate->filename();
                entry.content = candidate->content();
                entry.modified = false;
            }
        }
        mEntries.append(std::move(entry));
    }
    return OperationResult::success();
}

OperationResult EditorClosePlan::validateEditor(Editor *editor) const
{
    for (const Entry &entry : mEntries) {
        if (entry.editor.data() == editor && editor) {
            if (editor->filename() == entry.filename && editor->modified() == entry.modified
                    && editor->content() == entry.content)
                return OperationResult::success();
            return changed();
        }
    }
    return changed();
}

OperationResult EditorClosePlan::validate(const EditorManager &manager, EditorCloseScope scope) const
{
    const auto editors = manager.editors();
    QSet<Editor *> current;
    for (Editor *editor : editors)
        current.insert(editor);
    if (scope == EditorCloseScope::AllEditors && editors.size() != mEntries.size())
        return changed();
    if (scope == EditorCloseScope::ProjectEditors) {
        QSet<Editor *> planned;
        for (const Entry &entry : mEntries)
            if (entry.editor)
                planned.insert(entry.editor.data());
        for (Editor *editor : editors)
            if (editor->inProject() && !planned.contains(editor))
                return changed();
    }
    for (const Entry &entry : mEntries) {
        if (!entry.editor)
            return changed();
        if (!current.contains(entry.editor.data()))
            return changed();
        const auto result = validateEditor(entry.editor.data());
        if (!result.succeeded())
            return result;
    }
    return OperationResult::success();
}

bool EditorClosePlan::discards(Editor *editor) const
{
    for (const Entry &entry : mEntries)
        if (entry.editor.data() == editor)
            return entry.discard;
    return false;
}

QList<QPointer<Editor>> EditorClosePlan::editors() const
{
    QList<QPointer<Editor>> result;
    for (const Entry &entry : mEntries)
        result.append(entry.editor);
    return result;
}
