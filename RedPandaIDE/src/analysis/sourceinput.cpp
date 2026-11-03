// SPDX-License-Identifier: GPL-3.0-or-later
#include "sourceinput.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QStringConverter>
#include <limits>

namespace AnalysisInput {
namespace {
constexpr qint64 RecordAllowance = 4096;
constexpr qint64 ReadChunk = 64 * 1024;
bool driveRelative(const QString &path) {
#ifdef Q_OS_WIN
    if (path.size() < 2 || path[1] != ':') return false;
    const ushort letter = path[0].unicode();
    if (!((letter >= 'A' && letter <= 'Z') || (letter >= 'a' && letter <= 'z'))) return false;
    return path.size() == 2 || (path[2] != '/' && path[2] != '\\');
#else
    Q_UNUSED(path)
    return false;
#endif
}
bool qualifiedWorkingDirectory(const QString &path) {
#ifdef Q_OS_WIN
    if (driveRelative(path)) return false;
    return (path.size() >= 3 && path[1] == ':' && (path[2] == '/' || path[2] == '\\'))
        || path.startsWith("//") || path.startsWith("\\\\");
#else
    return QDir::isAbsolutePath(path);
#endif
}
ReadResult failed(ReadStatus status, const QString &message) { return {status, {}, message}; }
DiskStamp stamp(const QFileInfo &info) {
    return {info.size(), info.lastModified().isValid() ? info.lastModified().toMSecsSinceEpoch() : -1};
}
qsizetype lineCount(const QString &text) {
    qsizetype count = 0, begin = 0;
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (text[i] != '\r' && text[i] != '\n') continue;
        ++count;
        if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') ++i;
        begin = i + 1;
    }
    return count + (begin < text.size() ? 1 : 0);
}
QStringList splitLines(const QString &text, qsizetype count) {
    QStringList result;
    result.reserve(count);
    qsizetype begin = 0;
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (text[i] != '\r' && text[i] != '\n') continue;
        result.append(text.sliced(begin, i - begin));
        if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') ++i;
        begin = i + 1;
    }
    if (begin < text.size()) result.append(text.sliced(begin));
    return result;
}
}

SourceReadSession::SourceReadSession(PAnalysisInputSnapshot documents, ReadPolicy policy,
        std::shared_ptr<const std::atomic_bool> cancelled):
    mOpenDocuments(std::move(documents)), mPolicy(std::move(policy)), mCancelled(std::move(cancelled)) {}

ReadResult SourceReadSession::readDocumentRoot(const QString &logicalName) {
    if (stopped()) {
        mComplete = false;
        return failed(ReadStatus::Cancelled, QStringLiteral("Analysis source capture was cancelled."));
    }
    if (mOpenDocuments) {
        const auto root = mOpenDocuments->resolveDocument(logicalName);
        if (root.status == DocumentPathStatus::Conflict) {
            mComplete = false;
            return failed(ReadStatus::ConflictingPath, root.error);
        }
        if (root.status == DocumentPathStatus::Found) return read(root.document->absoluteFilename);
    }
    return read(logicalName);
}

bool SourceReadSession::stopped() const { return mCancelled && mCancelled->load(std::memory_order_relaxed); }
QString SourceReadSession::resolvedPath(const QString &filename) const {
    if (!qualifiedWorkingDirectory(mPolicy.workingDirectory) || filename.isEmpty()
            || filename.size() > 32767 || filename.contains(QChar::Null) || driveRelative(filename)
            || mPolicy.workingDirectory.size() > 32767 || mPolicy.workingDirectory.contains(QChar::Null)) return {};
    const QString path = QDir::cleanPath(QDir(mPolicy.workingDirectory).absoluteFilePath(filename));
    return qualifiedWorkingDirectory(path) && path.size() <= 32767 ? path : QString{};
}

PathProbe SourceReadSession::probe(const QString &filename) {
    const QString path = resolvedPath(filename);
    if (path.isEmpty() || stopped() || mPolicy.maximumPathProbes < 0) {
        mComplete = false;
        return {PathPresence::Unavailable, path, QStringLiteral("Invalid or cancelled analysis include lookup.")};
    }
    const auto prior = mProbes.constFind(path);
    if (prior != mProbes.cend()) return {prior.value(), path, {}};
    const qint64 bytes = RecordAllowance + qint64(path.size()) * 8;
    if (mProbes.size() >= mPolicy.maximumPathProbes || !canRetain(bytes)) {
        mComplete = false;
        return {PathPresence::Unavailable, path, QStringLiteral("Analysis include lookup limit reached.")};
    }
    DocumentPathResult open;
    if (mOpenDocuments) open = mOpenDocuments->resolveDocument(path);
    PathPresence presence;
    if (open.status == DocumentPathStatus::Conflict) {
        mComplete = false;
        presence = PathPresence::Unavailable;
    } else if (open.status == DocumentPathStatus::Found) presence = PathPresence::Present;
    else {
        const QFileInfo info(path);
        presence = info.isFile() ? PathPresence::Present : PathPresence::Missing;
        // An existing directory or unreadable/nonregular source must not be
        // treated as a proven absent header and silently select another one.
        if (info.exists() && !info.isFile()) { presence = PathPresence::Unavailable; mComplete = false; }
        else if (presence == PathPresence::Missing
                 && nativeFileIdentity(path).status != NativeFileIdentityStatus::Missing) {
            presence = PathPresence::Unavailable;
            mComplete = false;
        }
    }
    mProbes.insert(path, presence);
    mRetainedSourceBytes += bytes;
    return {presence, path, open.error};
}

QString SourceReadSession::resolveInclude(const QString &relativeTo, const QString &directive,
        const QStringList &includePaths, const QStringList &projectPaths) {
    int begin = directive.indexOf('<');
    const bool system = begin >= 0;
    if (!system) begin = directive.indexOf('"');
    if (begin < 0) return {};
    const int end = directive.indexOf(system ? '>' : '"', begin + 1);
    if (end <= begin + 1) return {};
    const QString name = directive.mid(begin + 1, end - begin - 1);
    QStringList directories;
    if (!system) {
        const QString including = resolvedPath(relativeTo);
        if (including.isEmpty()) { mComplete = false; return {}; }
        directories.append(QFileInfo(including).absolutePath());
    }
    directories.append(projectPaths);
    directories.append(includePaths);
    for (const QString &directory : directories) {
        const QString base = resolvedPath(directory);
        if (base.isEmpty()) { mComplete = false; return {}; }
        const auto candidate = probe(QDir(base).filePath(name));
        if (candidate.presence == PathPresence::Present) return candidate.path;
        if (candidate.presence == PathPresence::Unavailable) return {};
    }
    return {};
}

bool SourceReadSession::sameVersion(const SourceVersion &a, const SourceVersion &b) {
    if (a.path != b.path || a.origin != b.origin) return false;
    if (a.origin == SourceOrigin::OpenDocument)
        return a.documentId == b.documentId && a.revision == b.revision && a.textSha256 == b.textSha256;
    return a.rawSha256 == b.rawSha256 && a.textSha256 == b.textSha256 && a.stamp == b.stamp;
}

bool SourceReadSession::validateObservedVersions(QString *reason, const std::function<void()> &checkpoint) const {
    if (!mComplete || stopped()) {
        if (reason) *reason = QStringLiteral("Analysis input capture is incomplete or cancelled.");
        return false;
    }
    // Keep only one verification file alive at a time; this does not duplicate
    // the complete source set in a second retaining reader.
    ReadPolicy verificationPolicy = mPolicy;
    verificationPolicy.maximumLiveSourceBytes = qMax<qint64>(0, mPolicy.maximumLiveSourceBytes - mRetainedSourceBytes);
    for (const auto &version : mActualReads) {
        if (checkpoint) checkpoint();
        SourceReadSession verification(mOpenDocuments, verificationPolicy, mCancelled);
        const auto current = verification.read(version.path);
        if (checkpoint) checkpoint();
        if (!current.ready()) {
            if (reason) *reason = QStringLiteral("Analysis source verification failed: %1 (%2)").arg(version.path, current.error);
            return false;
        }
        if (!sameVersion(version, current.source->version)) {
            if (reason) *reason = QStringLiteral("Analysis source changed during processing: %1").arg(version.path);
            return false;
        }
    }
    for (auto it = mProbes.cbegin(); it != mProbes.cend(); ++it) {
        if (checkpoint) checkpoint();
        SourceReadSession verification(mOpenDocuments, verificationPolicy, mCancelled);
        const auto current = verification.probe(it.key());
        if (checkpoint) checkpoint();
        if (current.presence != it.value()) {
            if (reason) *reason = QStringLiteral("Analysis include lookup changed during processing: %1").arg(it.key());
            return false;
        }
    }
    return true;
}
bool SourceReadSession::canRetain(qint64 bytes, qint64 scratch) const {
    const auto maximum = mPolicy.maximumLiveSourceBytes;
    return bytes >= 0 && scratch >= 0 && mRetainedSourceBytes <= maximum
        && bytes <= maximum - mRetainedSourceBytes && scratch <= maximum - mRetainedSourceBytes - bytes;
}
qint64 SourceReadSession::contentCost(const QStringList &lines, qint64 ceiling) {
    qint64 result = RecordAllowance;
    for (const auto &line : lines) {
        const qint64 units = line.capacity();
        constexpr qint64 overhead = sizeof(QString) + 64;
        if (units > (std::numeric_limits<qint64>::max() - result - overhead) / 2)
            return std::numeric_limits<qint64>::max();
        result += 2 * units + overhead;
        if (result > ceiling) return result;
    }
    return result;
}
QByteArray SourceReadSession::textDigest(const QStringList &lines) {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArrayLiteral("rmele-normalized-lines-utf16le-v1:"));
    for (const auto &line : lines) {
        hash.addData(QByteArray::number(line.size()) + ':');
        for (qsizetype offset = 0; offset < line.size(); offset += 32768) {
            const int bytes = int(qMin<qsizetype>(32768, line.size() - offset) * 2);
            const auto raw = QByteArray::fromRawData(reinterpret_cast<const char *>(line.constData() + offset), bytes);
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
            hash.addData(raw);
#else
            QByteArray swapped(raw);
            for (int i = 0; i < bytes; i += 2) std::swap(swapped[i], swapped[i + 1]);
            hash.addData(swapped);
#endif
        }
    }
    return hash.result();
}

ReadResult SourceReadSession::read(const QString &filename) {
    if (!qualifiedWorkingDirectory(mPolicy.workingDirectory) || mPolicy.encodings.isEmpty()
            || mPolicy.maximumSources < 0 || mPolicy.maximumRawFileBytes < 0
            || mPolicy.maximumLiveSourceBytes < 0 || mPolicy.maximumReadMilliseconds < 0
            || mPolicy.workingDirectory.size() > 32767 || mPolicy.workingDirectory.contains(QChar::Null)
            || mPolicy.encodings.size() > 64) {
        mComplete = false;
        return failed(ReadStatus::InvalidPolicy, QStringLiteral("Invalid analysis source policy."));
    }
    for (const auto &encoding : mPolicy.encodings) {
        if (encoding.isEmpty() || encoding.size() > 256 || encoding.contains('\0')) {
            mComplete = false;
            return failed(ReadStatus::InvalidPolicy, QStringLiteral("Invalid analysis source encoding policy."));
        }
    }
    if (filename.size() > 32767 || filename.contains(QChar::Null) || driveRelative(filename)) {
        mComplete = false;
        return failed(ReadStatus::ReadFailed, QStringLiteral("Invalid analysis source path."));
    }
    if (stopped()) {
        mComplete = false;
        return failed(ReadStatus::Cancelled, QStringLiteral("Analysis source capture was cancelled."));
    }
    const QString path = resolvedPath(filename);
    if (path.isEmpty()) {
        mComplete = false;
        return failed(ReadStatus::ReadFailed, QStringLiteral("Unqualified analysis source paths are not supported."));
    }
    const QString key = AnalysisInputSnapshot::pathKey(path);
    const auto previous = mReads.constFind(key);
    if (previous != mReads.cend()) return previous.value();
    const qint64 recordBytes = RecordAllowance + qint64(path.size()) * 8;
    if (mReads.size() >= mPolicy.maximumSources || !canRetain(recordBytes)) {
        mComplete = false;
        return failed(ReadStatus::BudgetExceeded, QStringLiteral("Analysis source count/memory limit reached."));
    }
    ReadResult result = readUncached(path);
    result.metrics = mAttempt;
    result.error = result.error.left(1024);
    if (result.ready()) {
        mRetainedSourceBytes += contentCost(result.source->lines) + recordBytes - RecordAllowance;
        mActualReads.insert(key, result.source->version);
    } else {
        mRetainedSourceBytes += recordBytes;
        mComplete = false;
    }
    mReads.insert(key, result);
    return result;
}

ReadResult SourceReadSession::readUncached(const QString &path) {
    mAttempt = {};
    const qint64 pathAllowance = qint64(path.size()) * 8;
    DocumentPathResult lookup;
    if (mOpenDocuments) {
        lookup = mOpenDocuments->resolveDocument(path);
        if (lookup.status == DocumentPathStatus::Conflict)
            return failed(ReadStatus::ConflictingPath, lookup.error);
    }
    const auto document = lookup.document;
    if (document) {
        if (!canRetain(contentCost(document->lines, mPolicy.maximumLiveSourceBytes), pathAllowance))
            return failed(ReadStatus::BudgetExceeded, QStringLiteral("Open analysis source exceeds the job budget."));
        auto value = std::make_shared<SourceContents>();
        value->version = {path, SourceOrigin::OpenDocument, document->id, document->revision, {}, {}, textDigest(document->lines)};
        value->lines = document->lines;
        mAttempt.borrowedLines = document->lines.size();
        return {ReadStatus::Ready, std::move(value), {}};
    }
    QFileInfo before(path);
    if (!before.exists()) return failed(ReadStatus::Missing, QStringLiteral("Analysis source does not exist: %1").arg(path));
    if (!before.isFile()) return failed(ReadStatus::ReadFailed, QStringLiteral("Analysis source is not a regular file: %1").arg(path));
    if (before.size() < 0) return failed(ReadStatus::ReadFailed, QStringLiteral("Analysis source size is unavailable."));
    if (before.size() > mPolicy.maximumRawFileBytes || before.size() > (std::numeric_limits<qint64>::max() - RecordAllowance - pathAllowance) / 8
            || !canRetain(RecordAllowance + pathAllowance, before.size() * 8))
        return failed(ReadStatus::BudgetExceeded, QStringLiteral("Disk analysis source exceeds the job budget: %1").arg(path));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Unbuffered)) return failed(ReadStatus::ReadFailed, file.errorString());
    QElapsedTimer elapsed; elapsed.start();
    QByteArray raw;
    raw.reserve(before.size());
    while (!file.atEnd()) {
        if (stopped()) return failed(ReadStatus::Cancelled, QStringLiteral("Analysis source capture was cancelled."));
        // QFile can block in the operating system. This deadline is checked
        // between reads; it is not a promise to interrupt a blocked device.
        if (elapsed.elapsed() >= mPolicy.maximumReadMilliseconds)
            return failed(ReadStatus::TimedOut, QStringLiteral("Analysis source read deadline exceeded."));
        const qint64 remaining = before.size() - raw.size();
        if (remaining <= 0)
            return failed(ReadStatus::ChangedWhileReading, QStringLiteral("Analysis source grew while being read: %1").arg(path));
        const QByteArray chunk = file.read(qMin(ReadChunk, remaining));
        mAttempt.maximumRawScratchBytes = qMax(mAttempt.maximumRawScratchBytes,
            qint64(raw.capacity()) + qint64(chunk.capacity()));
        if (file.error() != QFileDevice::NoError || chunk.isEmpty())
            return failed(ReadStatus::ReadFailed, file.errorString());
        const qint64 nextSize = qint64(raw.size()) + chunk.size();
        if (nextSize > mPolicy.maximumRawFileBytes || nextSize > (std::numeric_limits<qint64>::max() - RecordAllowance - pathAllowance) / 8
                || !canRetain(RecordAllowance + pathAllowance, nextSize * 8))
            return failed(ReadStatus::BudgetExceeded, QStringLiteral("Growing analysis source exceeds the job budget."));
        raw.append(chunk);
        mAttempt.rawReadBytes = raw.size();
    }
    QFileInfo after(path);
    if (!(stamp(before) == stamp(after)) || raw.size() != before.size())
        return failed(ReadStatus::ChangedWhileReading, QStringLiteral("Analysis source changed while being read: %1").arg(path));
    for (const auto &encoding : mPolicy.encodings) {
        if (stopped()) return failed(ReadStatus::Cancelled, QStringLiteral("Analysis source capture was cancelled."));
        // The entire byte snapshot is decoded in one call. Stateless flushes
        // incomplete final sequences rather than retaining undecoded EOF bytes.
        QStringDecoder decoder(encoding.constData(), QStringConverter::Flag::Stateless);
        if (!decoder.isValid()) continue;
        const auto kind = QStringConverter::encodingForName(decoder.name());
        if (kind) {
            const bool utf16 = *kind == QStringConverter::Utf16 || *kind == QStringConverter::Utf16LE
                || *kind == QStringConverter::Utf16BE;
            const bool utf32 = *kind == QStringConverter::Utf32 || *kind == QStringConverter::Utf32LE
                || *kind == QStringConverter::Utf32BE;
            // Qt's UTF-16 converter may ignore a final odd byte even in
            // Stateless mode. Enforce complete fixed-width code units first.
            if ((utf16 && raw.size() % 2 != 0) || (utf32 && raw.size() % 4 != 0)) continue;
        }
        const qint64 requiredUnits = decoder.requiredSpace(raw.size());
        if (requiredUnits < 0 || requiredUnits > (std::numeric_limits<qint64>::max() - raw.capacity()) / 4
                || !canRetain(RecordAllowance + pathAllowance, raw.capacity() + requiredUnits * 4))
            return failed(ReadStatus::BudgetExceeded, QStringLiteral("Analysis decoder allocation exceeds the job budget."));
        const QString text = decoder.decode(raw);
        if (decoder.hasError()) continue;
        const qsizetype count = lineCount(text);
        mAttempt.decodedUnits = text.size();
        mAttempt.decodedLines = count;
        constexpr qint64 lineOverhead = sizeof(QString) + 64;
        if (count > (std::numeric_limits<qint64>::max() - RecordAllowance - pathAllowance) / lineOverhead)
            return failed(ReadStatus::BudgetExceeded, QStringLiteral("Analysis source has too many lines."));
        const qint64 estimated = RecordAllowance + pathAllowance + qint64(count) * lineOverhead;
        if (!canRetain(estimated, qint64(raw.capacity()) + qint64(text.capacity()) * 4))
            return failed(ReadStatus::BudgetExceeded, QStringLiteral("Analysis source line allocation exceeds the job budget."));
        QStringList lines = splitLines(text, count);
        mAttempt.materializedLines = lines.size();
        const qint64 cost = contentCost(lines);
        if (!canRetain(cost + pathAllowance, qint64(raw.capacity()) + qint64(text.capacity()) * 2))
            return failed(ReadStatus::BudgetExceeded, QStringLiteral("Decoded analysis source exceeds the job budget."));
        auto value = std::make_shared<SourceContents>();
        value->version = {path, SourceOrigin::Disk, 0, 0, stamp(after),
            QCryptographicHash::hash(raw, QCryptographicHash::Sha256), textDigest(lines)};
        value->lines = std::move(lines);
        value->encoding = encoding;
        return {ReadStatus::Ready, std::move(value), {}};
    }
    return failed(ReadStatus::DecodeFailed, QStringLiteral("Analysis source cannot be decoded with the captured encodings: %1").arg(path));
}
}
