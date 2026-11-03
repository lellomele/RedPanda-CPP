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

OperationResult EditorClosePlan::prepare(const EditorManager &manager, const QList<Editor *> &editors, const Decide &decide)
{
    mEntries.clear();
    mEntryForEditor.clear();
    mEntries.reserve(editors.size());
    mEntryForEditor.reserve(editors.size());
    // Capture weak identities before the first dialog can run a nested loop.
    QList<QPointer<Editor>> candidates;
    for (Editor *editor : editors)
        candidates.append(editor);
    for (const auto &candidate : candidates) {
        if (!candidate)
            return changed();
        Entry entry{candidate, manager.documentId(candidate), manager.documentRevision(candidate),
            candidate->filename(), candidate->editorEncoding(), candidate->modified(), false};
        if (!entry.document || !entry.revision) return changed();
        if (entry.modified) {
            const OperationResult outcome = decide(candidate.data(), entry.discard);
            if (!outcome.succeeded())
                return outcome;
            if (!candidate || manager.documentId(candidate) != entry.document)
                return changed();
            if (!entry.discard) {
                if (candidate->modified())
                    return changed();
                // A confirmed Save As may change path and encoding. It is an
                // actual commit, not something a later cancellation rolls back.
                entry.filename = candidate->filename();
                entry.encoding = candidate->editorEncoding();
                entry.revision = manager.documentRevision(candidate);
                if (!entry.revision) return changed();
                entry.modified = false;
            }
        }
        mEntryForEditor.insert(candidate.data(), mEntries.size());
        mEntries.append(std::move(entry));
    }
    return OperationResult::success();
}

OperationResult EditorClosePlan::validateEditor(const EditorManager &manager, Editor *editor) const
{
    const auto found = mEntryForEditor.constFind(editor);
    if (!editor || found == mEntryForEditor.cend()) return changed();
    const auto &entry = mEntries.at(*found);
    if (entry.editor == editor && manager.documentId(editor) == entry.document &&
            manager.documentRevision(editor) == entry.revision && editor->filename() == entry.filename &&
            editor->editorEncoding() == entry.encoding && editor->modified() == entry.modified)
        return OperationResult::success();
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
        const auto result = validateEditor(manager, entry.editor.data());
        if (!result.succeeded())
            return result;
    }
    return OperationResult::success();
}

bool EditorClosePlan::discards(Editor *editor) const
{
    const auto found = mEntryForEditor.constFind(editor);
    return editor && found != mEntryForEditor.cend() && mEntries.at(*found).editor == editor &&
        mEntries.at(*found).discard;
}

QList<QPointer<Editor>> EditorClosePlan::editors() const
{
    QList<QPointer<Editor>> result;
    for (const Entry &entry : mEntries)
        result.append(entry.editor);
    return result;
}
