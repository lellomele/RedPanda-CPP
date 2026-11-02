// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef GENERATEDTEXTFILE_H
#define GENERATEDTEXTFILE_H

#include "../operationresult.h"
#include <QSaveFile>

namespace Compilation {

// A generated makefile is visible only after every line has been written and
// the explicit commit succeeds. Never enable QSaveFile's direct-write fallback.
class GeneratedTextFile final {
public:
    explicit GeneratedTextFile(const QString &path, bool utf8,
                               qint64 byteBudget = 64LL * 1024 * 1024)
        : mFile(path), mUtf8(utf8), mBudget(byteBudget) {
        mFile.setDirectWriteFallback(false);
    }
    GeneratedTextFile(const GeneratedTextFile &) = delete;
    GeneratedTextFile &operator=(const GeneratedTextFile &) = delete;

    OperationResult open() {
        if (mBudget < 0 || !mFile.open(QIODevice::WriteOnly))
            return fail(QStringLiteral("Could not stage generated file %1: %2")
                        .arg(mFile.fileName(), mFile.errorString()));
        return OperationResult::success();
    }
    OperationResult appendLine(const QString &line) {
        if (!mFailure.isEmpty()) return OperationResult::failure(mFailure);
        if (!mFile.isOpen()) return fail(QStringLiteral("Generated file is not open."));
        if (line.size() > 1024 * 1024)
            return fail(QStringLiteral("Generated file line size limit exceeded: %1").arg(mFile.fileName()));
        // Check a conservative lower bound before allocating encoded bytes.
        // Newline is included in the budget and committed with the line.
        if (line.size() >= mBudget - mWritten)
            return fail(QStringLiteral("Generated file size limit exceeded: %1").arg(mFile.fileName()));
        auto bytes = mUtf8 ? line.toUtf8() : line.toLocal8Bit();
        if (bytes.size() >= mBudget - mWritten)
            return fail(QStringLiteral("Generated file size limit exceeded: %1").arg(mFile.fileName()));
        bytes.append('\n');
        if (mFile.write(bytes) != bytes.size())
            return fail(QStringLiteral("Could not write generated file %1: %2")
                        .arg(mFile.fileName(), mFile.errorString()));
        mWritten += bytes.size();
        return OperationResult::success();
    }
    OperationResult commit() {
        if (!mFailure.isEmpty()) return OperationResult::failure(mFailure);
        if (!mFile.isOpen()) return fail(QStringLiteral("Generated file is not open."));
        if (!mFile.commit())
            return fail(QStringLiteral("Could not commit generated file %1: %2")
                        .arg(mFile.fileName(), mFile.errorString()));
        return OperationResult::success();
    }
private:
    OperationResult fail(const QString &error) {
        mFailure = error;
        mFile.cancelWriting();
        return OperationResult::failure(error);
    }
    QSaveFile mFile;
    const bool mUtf8;
    const qint64 mBudget;
    qint64 mWritten = 0;
    QString mFailure;
};

} // namespace Compilation
#endif
