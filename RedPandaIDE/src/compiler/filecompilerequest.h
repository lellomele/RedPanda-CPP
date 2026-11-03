// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef FILECOMPILEREQUEST_H
#define FILECOMPILEREQUEST_H
#include "compileconfiguration.h"
#include "../common.h"
#include "../utils/file.h"
#include <QDeadlineTimer>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QWaitCondition>
#include <atomic>

namespace Analysis { class ParserToolchainProbeRequest; }

namespace Compilation {
enum class FileInputMode { SavedDiskFile, StandardInput };
struct CompileIdentity {
    quint64 jobId = 0;
    quint64 workspaceSessionId = 0;
    quint64 workspaceGeneration = 0;
    quint64 documentId = 0;
    quint64 textRevision = 0;
    quint64 configurationGeneration = 0;
};
struct FileCompileValues {
    CompileIdentity identity;
    // Metadata only. The GUI resolves a diagnostic filename to DocumentId and
    // verifies its accepted revision without copying source text per message.
    QHash<quint64, quint64> openDocumentRevisions;
    QStringList unavailableToolchainMacros;
    FileInputMode inputMode = FileInputMode::SavedDiskFile;
    QString filename;
    QString sourcePath;
    QString workingDirectory;
    FileType fileType = FileType::None;
    QByteArray sourceEncoding;
    QByteArray originalSourceEncoding;
    QString standardInput;
    CppCompileType compileType = CppCompileType::Normal;
    bool syntaxOnly = false;
    bool rebuild = false;
    // Disk builds still let the external compiler read current disk paths.
    // This is provenance, not a frozen filesystem or prevention of TOCTOU.
    qint64 diskSize = -1;
    qint64 diskModifiedMilliseconds = 0;
    FrozenConfiguration::Pointer configuration;
    std::shared_ptr<const Analysis::ParserToolchainProbeRequest> dependencyProbe;
    QList<QByteArray> dependencyEncodings;
    QString dependencyPreparationError;
    bool capabilitiesKnown = false;
    qint64 maximumDiagnosticBytes = 8LL * 1024 * 1024;
    qint64 maximumConvertedOutputBytes = 64LL * 1024 * 1024;
};
class FileCompileRequest final {
public:
    using Pointer = std::shared_ptr<const FileCompileRequest>;
    static Pointer capture(const FileCompileValues &values) {
        return Pointer(new FileCompileRequest(values));
    }
    const FileCompileValues &values() const { return mValues; }
private:
    explicit FileCompileRequest(const FileCompileValues &values): mValues(values) {}
    const FileCompileValues mValues;
};

struct PreparationDelivery {
    PreparationResult preparation;
    FrozenConfiguration::Pointer configuration;
};
// The GUI owner delivers one immutable result; a registered compiler worker
// waits without touching a parser, GUI objects or application settings. This
// mailbox is control state, not another source of mutable request values.
class PreparationMailbox final {
public:
    bool deliver(PreparationDelivery delivery) {
        QMutexLocker locker(&mMutex);
        if (mTerminal) return false;
        mDelivery = std::move(delivery);
        mTerminal = true;
        mChanged.wakeAll();
        return true;
    }
    void cancel() { deliver({{PreparationStatus::Cancelled, {}}, {}}); }
    bool terminal() const { QMutexLocker locker(&mMutex); return mTerminal; }
    PreparationDelivery wait(int timeoutMilliseconds, const std::atomic_bool &cancelled) {
        QMutexLocker locker(&mMutex);
        if (timeoutMilliseconds <= 0)
            return {{PreparationStatus::Failed, QStringLiteral("The compilation preparation deadline is invalid.")}, {}};
        QDeadlineTimer deadline(timeoutMilliseconds);
        while (!mTerminal) {
            if (cancelled.load(std::memory_order_acquire)) {
                mTerminal = true;
                mDelivery = {{PreparationStatus::Cancelled, {}}, {}};
                break;
            }
            if (!mChanged.wait(&mMutex, deadline)) {
                mTerminal = true;
                mDelivery = {PreparationResult::failure(QStringLiteral("Code analysis preparation did not finish before the deadline.")), {}};
                break;
            }
        }
        if (mDelivery.preparation.ready() && !mDelivery.configuration)
            return {PreparationResult::failure(QStringLiteral("Compilation preparation did not deliver a configuration.")), {}};
        return mDelivery;
    }
private:
    mutable QMutex mMutex;
    QWaitCondition mChanged;
    bool mTerminal = false;
    PreparationDelivery mDelivery;
};
} // namespace Compilation
#endif
