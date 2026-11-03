// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ANALYSIS_SOURCEINPUT_H
#define ANALYSIS_SOURCEINPUT_H

#include "../documentstore.h"
#include <QByteArray>
#include <QHash>
#include <QStringList>
#include <atomic>
#include <limits>
#include <memory>

namespace AnalysisInput {

enum class SourceOrigin { OpenDocument, Disk };
enum class ReadStatus { Ready, Missing, ReadFailed, DecodeFailed, ChangedWhileReading, ConflictingPath,
                        Cancelled, TimedOut, BudgetExceeded, InvalidPolicy };
enum class PathPresence { Present, Missing, Unavailable };
struct PathProbe { PathPresence presence = PathPresence::Unavailable; QString path; QString error; };
struct DiskStamp {
    qint64 size = -1;
    qint64 modifiedMilliseconds = -1;
    bool operator==(const DiskStamp &other) const {
        return size == other.size && modifiedMilliseconds == other.modifiedMilliseconds;
    }
};
struct SourceVersion {
    QString path;
    SourceOrigin origin = SourceOrigin::Disk;
    DocumentId documentId = 0;
    DocumentRevision revision = 0;
    DiskStamp stamp;
    QByteArray rawSha256;
    QByteArray textSha256;
};
struct SourceContents {
    SourceVersion version;
    QStringList lines;
    QByteArray encoding;
};
using SourcePtr = std::shared_ptr<const SourceContents>;
struct ReadMetrics {
    qint64 rawReadBytes = 0;
    qint64 maximumRawScratchBytes = 0;
    qint64 decodedUnits = 0;
    qsizetype decodedLines = 0;
    qsizetype materializedLines = 0;
    qsizetype borrowedLines = 0;
};
struct ReadResult {
    ReadStatus status = ReadStatus::Ready;
    SourcePtr source;
    QString error;
    ReadMetrics metrics;
    bool ready() const { return status == ReadStatus::Ready && bool(source); }
};
struct ReadPolicy {
    QString workingDirectory;
    QList<QByteArray> encodings{QByteArrayLiteral("UTF-8")};
    qint64 maximumRawFileBytes = 32 * 1024 * 1024;
    qint64 maximumLiveSourceBytes = 256 * 1024 * 1024;
    int maximumSources = 8192;
    int maximumReadMilliseconds = 10000;
    int maximumPathProbes = 32768;
};

// Copied at acceptance in the document owner's thread. Keeping it separate
// from the worker's mutable session prevents queued work from changing its
// directory or decoder policy when another editor becomes active.
struct SourceReadRequest { PAnalysisInputSnapshot documents; ReadPolicy policy; };
using SourceReadRequestPtr = std::shared_ptr<const SourceReadRequest>;

// Job-owned, single-threaded reader. The GUI supplies directory/codec values and
// immutable open buffers once. No GUI callback, settings or charset singleton is
// consulted. Each path, including a failed read, is captured once. Cancellation
// refuses further reads even when the path already has a captured result.
class SourceReadSession {
public:
    SourceReadSession(PAnalysisInputSnapshot openDocuments, ReadPolicy policy,
                      std::shared_ptr<const std::atomic_bool> cancelled = {});
    ReadResult read(const QString &filename);
    // A root submitted by an editor uses its stable registration path. Generic
    // relative reads/includes instead resolve against the captured job directory.
    ReadResult readDocumentRoot(const QString &logicalName);
    PathProbe probe(const QString &filename);
    QString resolveInclude(const QString &relativeTo, const QString &directive,
                           const QStringList &includePaths, const QStringList &projectPaths);
    QHash<QString, PathPresence> pathProbes() const { return mProbes; }
    QHash<QString, SourceVersion> actualReads() const { return mActualReads; }
    QHash<QString, ReadResult> outcomes() const { return mReads; }
    int sourceCount() const { return mReads.size(); }
    qint64 retainedSourceBytes() const { return mRetainedSourceBytes; }
    bool complete() const { return mComplete; }
    static QByteArray textDigest(const QStringList &lines);
    static bool sameVersion(const SourceVersion &, const SourceVersion &);
    // Re-read the captured inputs/probes in a new session. It certifies the
    // observed start/end versions, not an atomic filesystem-wide snapshot.
    bool validateObservedVersions(QString *reason = nullptr, const std::function<void()> &checkpoint = {}) const;
private:
    QString resolvedPath(const QString &filename) const;
    ReadResult readUncached(const QString &path);
    bool stopped() const;
    bool canRetain(qint64 bytes, qint64 scratchBytes = 0) const;
    static qint64 contentCost(const QStringList &lines, qint64 ceiling = std::numeric_limits<qint64>::max());
    PAnalysisInputSnapshot mOpenDocuments;
    const ReadPolicy mPolicy;
    const std::shared_ptr<const std::atomic_bool> mCancelled;
    QHash<QString, ReadResult> mReads;
    QHash<QString, SourceVersion> mActualReads;
    QHash<QString, PathPresence> mProbes;
    qint64 mRetainedSourceBytes = 0;
    bool mComplete = true;
    ReadMetrics mAttempt;
};
}
#endif
