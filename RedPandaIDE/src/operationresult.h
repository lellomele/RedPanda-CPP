// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef OPERATIONRESULT_H
#define OPERATIONRESULT_H

#include <QString>
#include <utility>

// Application operations report their outcome to the controller. Services do
// not turn a cancelled or failed operation into success by showing a dialog.
enum class OperationStatus {
    Succeeded,
    Cancelled,
    Failed,
    Conflict,
    CommittedWithFollowUpPending
};

struct OperationResult {
    OperationStatus status = OperationStatus::Succeeded;
    QString error;

    bool succeeded() const { return status == OperationStatus::Succeeded; }
    bool committed() const {
        return succeeded() || status == OperationStatus::CommittedWithFollowUpPending;
    }
    static OperationResult success() { return {}; }
    static OperationResult cancelled() { return {OperationStatus::Cancelled, {}}; }
    static OperationResult failure(QString reason) { return {OperationStatus::Failed, std::move(reason)}; }
    static OperationResult conflict(QString reason) { return {OperationStatus::Conflict, std::move(reason)}; }
};

#endif
