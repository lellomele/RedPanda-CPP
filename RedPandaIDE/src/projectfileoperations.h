// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PROJECTFILEOPERATIONS_H
#define PROJECTFILEOPERATIONS_H

#include "operationresult.h"
#include "pathidentity.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QUuid>
#include <functional>
#include <string>

// Filesystem/persistence portion of project operations. These functions do not
// show dialogs or mutate Project/Editor objects. Callers finish user decisions
// and validate model identity before entering a commit operation.
namespace ProjectFileOperations {
constexpr qint64 MaxMetadataBytes = 64 * 1024 * 1024;

inline QString message(const char *text) {
    return QCoreApplication::translate("Project", text);
}

template<class Ini>
OperationResult loadIni(const QString &path, Ini &candidate) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return OperationResult::failure(message("Cannot load project metadata '%1': %2").arg(path, file.errorString()));
    if (file.size() > MaxMetadataBytes)
        return OperationResult::failure(message("Project metadata exceeds the size limit: '%1'.").arg(path));
    const QByteArray bytes = file.read(MaxMetadataBytes + 1);
    if (file.error() != QFile::NoError || bytes.size() > MaxMetadataBytes)
        return OperationResult::failure(message("Cannot load project metadata '%1': %2").arg(path, file.errorString()));
    if (candidate.LoadData(bytes.constData(), static_cast<size_t>(bytes.size())) < 0)
        return OperationResult::failure(message("Invalid project metadata: '%1'.").arg(path));
    return OperationResult::success();
}

inline OperationResult commitMetadata(const QString &path, const QByteArray &candidate) {
    if (candidate.size() > MaxMetadataBytes)
        return OperationResult::failure(message("Project metadata exceeds the size limit: '%1'.").arg(path));
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(candidate) != candidate.size() || !file.commit())
        return OperationResult::failure(message("Cannot save project metadata '%1': %2").arg(path, file.errorString()));
    return OperationResult::success();
}

template<class Ini>
OperationResult saveIni(const QString &path, const Ini &candidate) {
    std::string bytes;
    if (candidate.Save(bytes) < 0)
        return OperationResult::failure(message("Cannot serialize project metadata: '%1'.").arg(path));
    if (bytes.size() > static_cast<size_t>(MaxMetadataBytes))
        return OperationResult::failure(message("Project metadata exceeds the size limit: '%1'.").arg(path));
    return commitMetadata(path, QByteArray(bytes.data(), static_cast<int>(bytes.size())));
}

inline OperationResult copyFile(const QString &sourcePath, const QString &targetPath) {
    QFile source(sourcePath);
    if (!QFileInfo(sourcePath).isFile() || !source.open(QIODevice::ReadOnly))
        return OperationResult::failure(message("Cannot copy project file '%1': %2").arg(sourcePath, source.errorString()));
    if (QFileInfo(sourcePath).canonicalFilePath() == QFileInfo(targetPath).canonicalFilePath())
        return OperationResult::success();
    QSaveFile target(targetPath);
    target.setDirectWriteFallback(false);
    if (!target.open(QIODevice::WriteOnly))
        return OperationResult::failure(message("Cannot copy project file to '%1': %2").arg(targetPath, target.errorString()));
    while (!source.atEnd()) {
        const QByteArray block = source.read(64 * 1024);
        if (source.error() != QFile::NoError || block.isEmpty())
            return OperationResult::failure(message("Cannot copy project file '%1': %2").arg(sourcePath, source.errorString()));
        if (target.write(block) != block.size())
            return OperationResult::failure(message("Cannot copy project file to '%1': %2").arg(targetPath, target.errorString()));
    }
    if (!target.commit())
        return OperationResult::failure(message("Cannot copy project file to '%1': %2").arg(targetPath, target.errorString()));
    return OperationResult::success();
}

// A replacement keeps the old destination until the caller confirms the model
// commit. If moving the source fails, the old destination is restored. A failed
// rollback retains the backup and reports its path rather than deleting data.
class RenameTransaction {
public:
    using MoveFile = std::function<bool(const QString &, const QString &)>;
    RenameTransaction(QString source, QString target, bool replaceExisting,
                      MoveFile moveFile = [](const QString &from, const QString &to) { return QFile::rename(from, to); }):
        mSource(std::move(source)), mTarget(std::move(target)), mReplaceExisting(replaceExisting), mMoveFile(std::move(moveFile)) {}
    RenameTransaction(const RenameTransaction &) = delete;
    RenameTransaction &operator=(const RenameTransaction &) = delete;

    OperationResult commit() {
        if (mCommitted)
            return OperationResult::conflict(message("The project file rename was already committed."));
        if (!QFileInfo(mSource).isFile())
            return OperationResult::failure(message("Cannot rename missing project file '%1'.").arg(mSource));
        const QFileInfo sourceInfo(mSource), targetInfo(mTarget);
        const bool sameResource = targetInfo.exists() && sameExistingFile(mSource, mTarget) == ExistingFileMatch::Same;
        if (targetInfo.exists() && !sameResource && !mReplaceExisting)
            return OperationResult::conflict(message("File '%1' already exists.").arg(mTarget));
        if (targetInfo.exists() && !sameResource && !targetInfo.isFile())
            return OperationResult::conflict(message("The rename destination is not a file: '%1'.").arg(mTarget));
        // The unique sibling also permits case-only renames on Windows.
        if (targetInfo.exists()) {
            mBackup = mTarget + QStringLiteral(".redpanda-rename-%1.backup").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
            if (!mMoveFile(sameResource ? mSource : mTarget, mBackup)) {
                mBackup.clear();
                return OperationResult::failure(message("Cannot prepare project file rename to '%1'.").arg(mTarget));
            }
            if (sameResource)
                mSourceMovedToBackup = true;
        }
        if (!mMoveFile(mSourceMovedToBackup ? mBackup : mSource, mTarget)) {
            const bool restored = mBackup.isEmpty() || mMoveFile(mBackup, mSourceMovedToBackup ? mSource : mTarget);
            if (!restored)
                return OperationResult::failure(message("Project file rename failed. The original file is preserved at '%1'.").arg(mBackup));
            mBackup.clear();
            return OperationResult::failure(message("Failed to rename file '%1' to '%2'.").arg(mSource, mTarget));
        }
        if (mSourceMovedToBackup)
            mBackup.clear();
        mCommitted = true;
        return OperationResult::success();
    }

    OperationResult finish() {
        if (!mCommitted)
            return OperationResult::failure(message("The project file rename has not been committed."));
        if (!mBackup.isEmpty() && !QFile::remove(mBackup))
            return {OperationStatus::CommittedWithFollowUpPending,
                    message("The file was renamed. The previous destination remains at '%1'.").arg(mBackup)};
        mBackup.clear();
        return OperationResult::success();
    }

    QString retainedBackup() const { return mBackup; }

private:
    QString mSource;
    QString mTarget;
    QString mBackup;
    bool mReplaceExisting = false;
    bool mSourceMovedToBackup = false;
    bool mCommitted = false;
    MoveFile mMoveFile;
};
}
#endif
