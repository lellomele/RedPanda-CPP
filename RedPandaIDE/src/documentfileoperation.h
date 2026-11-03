// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef DOCUMENTFILEOPERATION_H
#define DOCUMENTFILEOPERATION_H
#include "operationresult.h"
#include "documentstore.h"

enum class ResourceCommitState { NotAttempted, Committed, Failed };
struct ResourceCommit {
    ResourceCommitState state = ResourceCommitState::NotAttempted;
    QString path;
    QString error;
};
enum class DocumentSaveMode { CurrentPath, ChoosePath };
struct DocumentSaveRequest {
    DocumentSaveMode mode = DocumentSaveMode::CurrentPath;
    QString destination;
    QByteArray convertTo;
    bool checkSyntax = true;
};
struct DocumentSaveAsResult {
    OperationResult outcome;
    DocumentId document = 0;
    DocumentRevision savedRevision = 0;
    ResourceCommit textFile;
    ResourceCommit projectMetadata;
};
#endif
