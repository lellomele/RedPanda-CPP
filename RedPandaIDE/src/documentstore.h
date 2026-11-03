// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef DOCUMENTSTORE_H
#define DOCUMENTSTORE_H

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QMap>
#include "pathidentity.h"
#include <QThread>
#include <QStringList>
#include <functional>
#include <limits>
#include <memory>
#include <utility>

using DocumentId = quint64;
using DocumentRevision = quint64;

struct DocumentSnapshot {
    DocumentId id = 0;
    DocumentRevision revision = 0;
    QString filename;
    QString absoluteFilename; // Registration/rename root, never a later process CWD.
    QStringList lines;
};
using PDocumentSnapshot = std::shared_ptr<const DocumentSnapshot>;
enum class DocumentPathStatus { Found, NotOpen, Conflict };
struct DocumentPathResult {
    DocumentPathStatus status = DocumentPathStatus::NotOpen;
    PDocumentSnapshot document;
    QString error;
};

// This object is immutable once published. A job keeps its complete source view
// even when a document is edited, renamed or closed while that job is running.
class AnalysisInputSnapshot {
public:
    static QString pathKey(const QString &filename) {
        // Legacy lexical utility; not an identity/equivalence key.
        return exactPathSpelling(filename);
    }
    DocumentPathResult resolveDocument(const QString &filename) const {
        const NativePathKey key(filename);
        if (!key.valid())
            return {DocumentPathStatus::Conflict, {}, QCoreApplication::translate("DocumentStore", "Invalid document path.")};
        const auto entry = mByPath.constFind(key);
        if (entry == mByPath.cend()) return {};
        if (entry->spellings.contains(key.spelling()))
            return {DocumentPathStatus::Found, entry->document, {}};
        // Relative aliasing has no stable filesystem root after capture. Exact
        // registered relative spellings remain valid; other aliases fail closed.
        const auto match = QDir::isAbsolutePath(key.spelling())
                ? sameExistingFile(key.spelling(), entry->identityPath) : ExistingFileMatch::Unknown;
        if (match == ExistingFileMatch::Same) return {DocumentPathStatus::Found, entry->document, {}};
        if (match == ExistingFileMatch::Different) return {};
        return {DocumentPathStatus::Conflict, {}, QCoreApplication::translate("DocumentStore",
                    "The identity of an open document path alias could not be verified: %1").arg(filename)};
    }
    PDocumentSnapshot document(const QString &filename) const {
        return resolveDocument(filename).document;
    }
    bool read(const QString &filename, QStringList &lines) const {
        const auto source = document(filename);
        if (!source)
            return false; // This path was not an open document in this job.
        lines = source->lines; // An empty open buffer is still a successful read.
        return true;
    }
    const QHash<DocumentId, PDocumentSnapshot> &documents() const { return mDocuments; }
    quint64 generation() const { return mGeneration; }
private:
    friend class DocumentStore;
    struct PathEntry { PDocumentSnapshot document; QString identityPath; QStringList spellings; };
    quint64 mGeneration = 0;
    QMap<NativePathKey, PathEntry> mByPath;
    QHash<DocumentId, PDocumentSnapshot> mDocuments;
};
using PAnalysisInputSnapshot = std::shared_ptr<const AnalysisInputSnapshot>;

enum class InputCaptureStatus { Captured, WrongThread, DocumentChanging, DocumentUnavailable, ConflictingPath };
struct InputCaptureResult {
    InputCaptureStatus status = InputCaptureStatus::Captured;
    PAnalysisInputSnapshot snapshot;
    QString error;
    bool succeeded() const { return status == InputCaptureStatus::Captured && bool(snapshot); }
};
using InputSnapshotProvider = std::function<InputCaptureResult()>;

// Mutable registrations and readers belong exclusively to the creating GUI
// thread. The parser calls capture() only when accepting a request on that
// thread; no reader is transferred to a worker. Dirty/revision updates are O(1)
// and text is materialized only for dirty documents at an accepted submission.
class DocumentStore {
public:
    using Reader = std::function<bool(QStringList &)>;
    DocumentStore(): mOwner(QThread::currentThread()) {}

    DocumentId add(QString filename, Reader reader) {
        if (!onOwnerThread() || mNextId == std::numeric_limits<DocumentId>::max())
            return 0;
        const DocumentId id = ++mNextId;
        Entry entry;
        entry.filename = std::move(filename);
        entry.absoluteFilename = QFileInfo(entry.filename).absoluteFilePath();
        entry.reader = std::move(reader);
        mEntries.insert(id, std::move(entry));
        invalidate();
        return id;
    }
    bool changed(DocumentId id) {
        if (!onOwnerThread())
            return false;
        auto it = mEntries.find(id);
        if (it == mEntries.end() || it->revision == std::numeric_limits<DocumentRevision>::max())
            return false;
        ++it->revision;
        it->dirty = true;
        invalidate();
        return true;
    }
    // Revisions identify committed text, never a pre-mutation notification.
    // These hooks are O(1); text remains lazy until an accepted job captures it.
    bool beginChange(DocumentId id) {
        if (!onOwnerThread())
            return false;
        auto it = mEntries.find(id);
        if (it == mEntries.end() || it->changing)
            return false;
        it->changing = true;
        ++mChangingCount;
        return true;
    }
    bool commitChange(DocumentId id) {
        if (!onOwnerThread())
            return false;
        auto it = mEntries.find(id);
        if (it == mEntries.end() || !it->changing)
            return false;
        it->changing = false;
        --mChangingCount;
        return changed(id);
    }
    bool renamed(DocumentId id, const QString &filename) {
        if (!onOwnerThread())
            return false;
        auto it = mEntries.find(id);
        if (it == mEntries.end())
            return false;
        if (it->filename == filename)
            return true;
        it->filename = filename;
        it->absoluteFilename = QFileInfo(filename).absoluteFilePath();
        return changed(id);
    }
    bool remove(DocumentId id) {
        if (!onOwnerThread())
            return false;
        const auto it = mEntries.find(id);
        if (it == mEntries.end())
            return false;
        if (it->changing)
            --mChangingCount;
        mEntries.erase(it);
        invalidate();
        return true;
    }
    InputCaptureResult capture() {
        if (!onOwnerThread())
            return {InputCaptureStatus::WrongThread, {}, message("Document input must be captured in its owning thread.")};
        if (mChangingCount != 0)
            return {InputCaptureStatus::DocumentChanging, {}, message("An open document is being modified.")};
        if (mPublished)
            return {InputCaptureStatus::Captured, mPublished, {}};
        auto candidate = std::make_shared<AnalysisInputSnapshot>();
        candidate->mGeneration = mGeneration;
        for (auto it = mEntries.begin(); it != mEntries.end(); ++it) {
            const auto source = materialize(it.key(), *it);
            if (!source)
                return {InputCaptureStatus::DocumentUnavailable, {}, message("An open document could not be captured: %1").arg(it->filename)};
            const QStringList aliases{it->filename, it->absoluteFilename};
            for (const QString &alias : aliases) {
                const NativePathKey key(alias);
                if (!key.valid())
                    return {InputCaptureStatus::DocumentUnavailable, {}, message("Invalid document path.")};
                auto existing = candidate->mByPath.find(key);
                if (existing != candidate->mByPath.end()) {
                    if (existing->document->id != source->id) {
                        const auto match = sameExistingFile(it->absoluteFilename, existing->identityPath);
                        const QString reason = match == ExistingFileMatch::Different
                            ? message("Distinct files share an unsupported case-sensitive path namespace: %1").arg(alias)
                            : message("Several open documents have the same or unverifiable path: %1").arg(alias);
                        return {InputCaptureStatus::ConflictingPath, {}, reason};
                    }
                    if (!existing->spellings.contains(key.spelling())) existing->spellings.append(key.spelling());
                } else candidate->mByPath.insert(key, {source, it->absoluteFilename, {key.spelling()}});
            }
            candidate->mDocuments.insert(source->id, source);
        }
        mPublished = std::move(candidate);
        return {InputCaptureStatus::Captured, mPublished, {}};
    }
    PDocumentSnapshot snapshotFor(DocumentId id) {
        if (!onOwnerThread())
            return {};
        const auto entry = mEntries.find(id);
        return entry == mEntries.end() || entry->changing ? PDocumentSnapshot{} : materialize(id, *entry);
    }
    DocumentRevision revisionFor(DocumentId id) const {
        if (!onOwnerThread()) return 0;
        const auto entry = mEntries.constFind(id);
        return entry == mEntries.cend() || entry->changing ? 0 : entry->revision;
    }
    QString absolutePathFor(DocumentId id) const {
        if (!onOwnerThread()) return {};
        const auto entry = mEntries.constFind(id);
        return entry == mEntries.cend() ? QString{} : entry->absoluteFilename;
    }
    bool onOwnerThread() const { return QThread::currentThread() == mOwner; }
private:
    struct Entry {
        QString filename;
        QString absoluteFilename;
        Reader reader;
        DocumentRevision revision = 1;
        bool dirty = true;
        bool changing = false;
        PDocumentSnapshot snapshot;
    };
    static PDocumentSnapshot materialize(DocumentId id, Entry &entry) {
        if (entry.dirty || !entry.snapshot) {
            QStringList lines;
            if (!entry.reader || !entry.reader(lines))
                return {};
            auto document = std::make_shared<DocumentSnapshot>();
            document->id = id;
            document->revision = entry.revision;
            document->filename = entry.filename;
            document->absoluteFilename = entry.absoluteFilename;
            document->lines = std::move(lines);
            entry.snapshot = std::move(document);
            entry.dirty = false;
        }
        return entry.snapshot;
    }
    static QString message(const char *text) { return QCoreApplication::translate("DocumentStore", text); }
    void invalidate() { ++mGeneration; mPublished.reset(); }
    QThread *const mOwner;
    DocumentId mNextId = 0;
    quint64 mGeneration = 0;
    quint64 mChangingCount = 0;
    QHash<DocumentId, Entry> mEntries;
    PAnalysisInputSnapshot mPublished;
};

#endif
