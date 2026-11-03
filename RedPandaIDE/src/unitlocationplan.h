// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef UNITLOCATIONPLAN_H
#define UNITLOCATIONPLAN_H
#include "documentstore.h"
#include "documentfileoperation.h"
#include <memory>

class Project;
class ProjectUnit;
enum class UnitLocationCommand { InspectSource, SaveDocumentAs, RelinkExistingFile };
enum class UnitProjectionState { NotApplied, Applied };
struct UnitLocationCommitResult {
    OperationResult outcome;
    ResourceCommit metadata;
    UnitProjectionState projection = UnitProjectionState::NotApplied;
};

// Ephemeral immutable GUI command plan, never an authoritative project copy.
// Only Project can create it; every commit revalidates its identities/versions.
class UnitLocationPlan final {
public:
    using Pointer = std::shared_ptr<const UnitLocationPlan>;
    UnitLocationCommand command() const { return mCommand; }
    const QString &sourcePath() const { return mSourcePath; }
    const QString &targetPath() const { return mTargetPath; }
    DocumentId acceptedDocument() const { return mAcceptedDocument; }
private:
    friend class Project;
    struct Settings {
        bool compile = false, compileCpp = false, link = false, overrideCommand = false;
        int priority = 0;
        QString folder, command;
        QByteArray encoding;
        bool operator==(const Settings &other) const {
            return compile == other.compile && compileCpp == other.compileCpp && link == other.link
                && overrideCommand == other.overrideCommand && priority == other.priority
                && folder == other.folder && command == other.command && encoding == other.encoding;
        }
    };
    UnitLocationPlan() = default;
    const Project *mProject = nullptr; // Identity only; never dereferenced.
    std::weak_ptr<ProjectUnit> mUnit;
    UnitLocationCommand mCommand = UnitLocationCommand::RelinkExistingFile;
    QString mSourcePath, mTargetPath, mRelocationTarget, mRelocationDiagnostic;
    bool mHadRelocationConflict = false;
    Settings mSettings;
    QByteArray mProjectEncoding;
    DocumentId mSourceDocument = 0, mAcceptedDocument = 0;
    DocumentRevision mSourceRevision = 0, mAcceptedRevision = 0;
};
#endif
