// SPDX-License-Identifier: GPL-3.0-or-later
#include "pathidentity.h"
#include <QDir>
#include <limits>
#include <cstring>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#else
#include <sys/stat.h>
#include <cerrno>
#include <QFile>
#endif

QString exactPathSpelling(const QString &path)
{
    return path.isEmpty() ? QString{} : QDir::cleanPath(QDir::fromNativeSeparators(path));
}

NativePathKey::NativePathKey(const QString &path) : mSpelling(exactPathSpelling(path))
{
    mValid = !mSpelling.isEmpty() && mSpelling.size() <= std::numeric_limits<int>::max()
            && mSpelling.isValidUtf16() && !mSpelling.contains(QChar::Null);
}

int compareNativePathSpelling(const QString &left, const QString &right)
{
    return compareNativePathSpelling(QStringView{left}, QStringView{right});
}

int compareNativePathSpelling(QStringView left, QStringView right)
{
    if (left == right) return 0;
    if (left.size() > std::numeric_limits<int>::max() || right.size() > std::numeric_limits<int>::max()
            || !left.isValidUtf16() || !right.isValidUtf16() || left.contains(QChar::Null) || right.contains(QChar::Null))
        return left.compare(right, Qt::CaseSensitive);
#ifdef Q_OS_WIN
    // A view also supports bounded suffix comparison without allocating a copy.
    const int compared = CompareStringOrdinal(reinterpret_cast<LPCWCH>(left.data()), int(left.size()),
                                               reinterpret_cast<LPCWCH>(right.data()), int(right.size()), TRUE);
    if (compared == CSTR_LESS_THAN) return -1;
    if (compared == CSTR_EQUAL) return 0;
    if (compared == CSTR_GREATER_THAN) return 1;
    // Do not turn an unexpected native API failure into equality/overwrite.
    // Invalid inputs are normally separated by NativePathKey::valid().
#endif
    return left.compare(right, Qt::CaseSensitive);
}

NativeFileIdentity nativeFileIdentity(const QString &path)
{
    NativeFileIdentity result;
    const NativePathKey checked(path);
    if (!checked.valid() || checked.spelling().isEmpty()) return result;
#ifdef Q_OS_WIN
    const QString nativePath = QDir::toNativeSeparators(checked.spelling());
    const HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(nativePath.utf16()), 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        result.error = GetLastError();
        result.status = result.error == ERROR_FILE_NOT_FOUND || result.error == ERROR_PATH_NOT_FOUND
                ? NativeFileIdentityStatus::Missing : NativeFileIdentityStatus::Unavailable;
        return result;
    }
    FILE_ID_INFO identity{};
    if (GetFileInformationByHandleEx(handle, FileIdInfo, &identity, sizeof(identity))) {
        result.volume = identity.VolumeSerialNumber;
        std::memcpy(result.file.data(), identity.FileId.Identifier, result.file.size());
        bool nonzero = false;
        for (const auto value : result.file) nonzero |= value != 0;
        if (nonzero) result.status = NativeFileIdentityStatus::Known;
    } else result.error = GetLastError();
    CloseHandle(handle);
#else
    struct stat identity{};
    if (::stat(QFile::encodeName(checked.spelling()).constData(), &identity) == 0) {
        result.volume = quint64(identity.st_dev);
        const quint64 inode = quint64(identity.st_ino);
        std::memcpy(result.file.data(), &inode, sizeof(inode));
        result.status = NativeFileIdentityStatus::Known;
    } else {
        result.error = errno;
        result.status = errno == ENOENT || errno == ENOTDIR
                ? NativeFileIdentityStatus::Missing : NativeFileIdentityStatus::Unavailable;
    }
#endif
    return result;
}

ExistingFileMatch sameExistingFile(const QString &left, const QString &right)
{
    const auto first = nativeFileIdentity(left), second = nativeFileIdentity(right);
    if (first.status != NativeFileIdentityStatus::Known || second.status != NativeFileIdentityStatus::Known)
        return ExistingFileMatch::Unknown;
    return first.volume == second.volume && first.file == second.file
            ? ExistingFileMatch::Same : ExistingFileMatch::Different;
}

bool verifiedPathAlias(const QString &left, const QString &right)
{
    const NativePathKey first(left), second(right);
    if (!first.valid() || !second.valid() || !(first == second)) return false;
    return first.spelling() == second.spelling() || sameExistingFile(first.spelling(), second.spelling()) == ExistingFileMatch::Same;
}
