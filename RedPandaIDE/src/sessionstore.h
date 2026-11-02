#ifndef SESSIONSTORE_H
#define SESSIONSTORE_H
#include <QList>
#include <QString>
#include <QStringView>
struct RecoveryDocument { QString filename; QString text; int line = 0; int column = 0; };
namespace SessionStore {
constexpr qint64 MaxBytes = 128 * 1024 * 1024;
constexpr qint64 MaxDocumentCharacters = 32 * 1024 * 1024;
// Bound the input retained by one snapshot as well as the encoded file. The
// per-document limit alone allowed hundreds of large documents to accumulate.
constexpr qint64 MaxTotalTextCharacters = 32 * 1024 * 1024;
constexpr int MaxDocuments = 256;

class SnapshotBudget {
public:
    explicit SnapshotBudget(qint64 maximumBytes = MaxBytes,
                            qint64 maximumTextCharacters = MaxTotalTextCharacters);
    bool beginDocument(const QString &filename, QString &error);
    bool appendText(QStringView text, QString &error);
    qint64 estimatedBytes() const { return mEncodedBytes; }
    qint64 textCharacters() const { return mTextCharacters; }
private:
    qint64 mMaximumBytes;
    qint64 mMaximumTextCharacters;
    qint64 mEncodedBytes = 128; // envelope, timestamp and separators
    qint64 mTextCharacters = 0;
    qint64 mDocumentCharacters = 0;
    int mDocuments = 0;
};

bool write(const QString &path, const QList<RecoveryDocument> &documents, QString &error);
bool read(const QString &path, QList<RecoveryDocument> &documents, QString &error);
}
#endif
