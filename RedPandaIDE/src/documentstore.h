// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef DOCUMENTSTORE_H
#define DOCUMENTSTORE_H

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHash>
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
    QStringList lines;
};
using PDocumentSnapshot = std::shared_ptr<const DocumentSnapshot>;

// This object is immutable once published. A job keeps its complete source view
// even when a document is edited, renamed or closed while that job is running.
class AnalysisInputSnapshot {
public:
    static QString pathKey(const QString &filename) {
        QString key = QDir::cleanPath(QDir::fromNativeSeparators(filename));
#ifdef Q_OS_WIN
        key = key.toCaseFolded();
#endif
        return key;
    }
    PDocumentSnapshot document(const QString &filename) const {
        return mByPath.value(pathKey(filename));
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
    quint64 mGeneration = 0;
    QHash<QString, PDocumentSnapshot> mByPath;
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
            if (it->dirty || !it->snapshot) {
                QStringList lines;
                if (!it->reader || !it->reader(lines))
                    return {InputCaptureStatus::DocumentUnavailable, {}, message("An open document could not be captured: %1").arg(it->filename)};
                auto document = std::make_shared<DocumentSnapshot>();
                document->id = it.key();
                document->revision = it->revision;
                document->filename = it->filename;
                document->lines = std::move(lines);
                it->snapshot = std::move(document);
                it->dirty = false;
            }
            const auto source = it->snapshot;
            const QStringList aliases{it->filename, it->absoluteFilename};
            for (const QString &alias : aliases) {
                const QString key = AnalysisInputSnapshot::pathKey(alias);
                const auto existing = candidate->mByPath.value(key);
                if (existing && existing->id != source->id)
                    return {InputCaptureStatus::ConflictingPath, {}, message("Several open documents have the same path: %1").arg(alias)};
                candidate->mByPath.insert(key, source);
            }
            candidate->mDocuments.insert(source->id, source);
        }
        mPublished = std::move(candidate);
        return {InputCaptureStatus::Captured, mPublished, {}};
    }
    PDocumentSnapshot snapshotFor(DocumentId id) {
        const auto result = capture();
        return result.succeeded() ? result.snapshot->documents().value(id) : PDocumentSnapshot{};
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
