// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PATHIDENTITY_H
#define PATHIDENTITY_H
#include <QString>
#include <QStringView>
#include <QtGlobal>
#include <array>

// Lexical normalization is not filesystem identity. No Unicode folding here.
QString exactPathSpelling(const QString &path);
int compareNativePathSpelling(const QString &left, const QString &right);
int compareNativePathSpelling(QStringView left, QStringView right);

// A stable ordered namespace key. Its comparator never performs filesystem I/O.
// Windows native ordinal comparison differs from QString Unicode case folding.
class NativePathKey final {
public:
    NativePathKey() = default;
    explicit NativePathKey(const QString &path);
    const QString &spelling() const { return mSpelling; }
    bool valid() const { return mValid; }
    friend bool operator<(const NativePathKey &left, const NativePathKey &right) {
        if (left.mValid != right.mValid) return left.mValid < right.mValid;
        return left.mValid ? compareNativePathSpelling(left.mSpelling, right.mSpelling) < 0
                          : left.mSpelling < right.mSpelling;
    }
    friend bool operator==(const NativePathKey &left, const NativePathKey &right) {
        return left.mValid == right.mValid && (left.mValid
            ? compareNativePathSpelling(left.mSpelling, right.mSpelling) == 0
            : left.mSpelling == right.mSpelling);
    }
private:
    QString mSpelling;
    bool mValid = false;
};

enum class NativeFileIdentityStatus { Known, Missing, Unavailable };
struct NativeFileIdentity {
    NativeFileIdentityStatus status = NativeFileIdentityStatus::Unavailable;
    quint64 volume = 0;
    std::array<quint8, 16> file{};
    quint32 error = 0;
};
enum class ExistingFileMatch { Same, Different, Unknown };
NativeFileIdentity nativeFileIdentity(const QString &path);
ExistingFileMatch sameExistingFile(const QString &left, const QString &right);

// Used only when an ordered key matches but its exact spelling differs.
// Missing/inaccessible files are not proof of aliasing and never select a buffer.
bool verifiedPathAlias(const QString &left, const QString &right);
#endif
