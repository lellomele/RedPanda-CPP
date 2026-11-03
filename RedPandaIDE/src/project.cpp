/*
 * Copyright (C) 2020-2022 Roy Qu (royqh1979@gmail.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "project.h"
#include "projectfileoperations.h"
#include "editor.h"
#include "editormanager.h"
#include "qt_utils/utils.h"
#include "utils.h"
#include "systemconsts.h"
#include "parser/cppparser.h"
#include "utils/file.h"
#include "qt_utils/charsetinfo.h"
#include "projecttemplate.h"
#include "systemconsts.h"
#include "iconsmanager.h"

#include <QFileSystemWatcher>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QMessageBox>
#include <QDirIterator>
#include <QMimeDatabase>
#include <QDesktopServices>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QPointer>
#include "editorcloseplan.h"
#include "customfileiconprovider.h"
#include <QMimeData>
#include "settings.h"
#include "vcs/gitrepository.h"
#include "utils/parser.h"

namespace {
NativePathKey unitPathKey(const QString &path) {
    return NativePathKey(QFileInfo(path).absoluteFilePath());
}
OperationResult validateUnitPaths(const SimpleIni &ini, const QString &directory) {
    const long count = ini.GetLongValue("Project", "UnitCount", 0);
    if (count < 0 || count > 100000)
        return OperationResult::failure(Project::tr("Invalid or excessive project unit count."));
    QMap<NativePathKey, QString> paths;
    const QDir root(directory);
    for (long i = 1; i <= count; ++i) {
        const QByteArray group = toByteArray(QString("Unit%1").arg(i));
        const QString relative = fromByteArray(ini.GetValue(group, "FileName", ""));
        const NativePathKey key(relative.isEmpty() ? QString{} : root.absoluteFilePath(relative));
        if (relative.isEmpty() || !key.valid())
            return OperationResult::failure(Project::tr("Invalid file path in project unit %1.").arg(i));
        const auto existing = paths.constFind(key);
        if (existing != paths.cend())
            return OperationResult::conflict(Project::tr("Project paths '%1' and '%2' share the same native path namespace. Duplicate units and case-sensitive aliases are not supported; the project was not changed.")
                .arg(existing.value(), key.spelling()));
        paths.insert(key, key.spelling());
    }
    return OperationResult::success();
}
void requireProjectOwnerThread(EditorManager *editors, QFileSystemWatcher *watcher)
{
    if (!editors || !watcher || QThread::currentThread() != editors->thread()
            || QThread::currentThread() != watcher->thread())
        throw FileError(Project::tr("Projects must be created on the editor's owner thread."));
}

void deleteProjectOnOwnerThread(Project *project)
{
    if (QThread::currentThread() == project->thread())
        delete project;
    else
        project->deleteLater();
}
}

Project::Project(const QString &filename, const QString &name,
                 EditorManager* editorList,
                 IconsManager * iconsManager,
                 QFileSystemWatcher* fileSystemWatcher) :
    QObject(nullptr),
    mName(name),
    mModified(false),
    mEditorManager(editorList),
    mFileSystemWatcher(fileSystemWatcher)
{
    mModel = new ProjectModel(iconsManager, this);
    mFilename = QFileInfo(filename).absoluteFilePath();
    mParser = std::make_shared<CppParser>();
    mParser->setSharedByFiles(true);
    mEditorManager->bindParserInput(mParser);
    connect(mEditorManager, &EditorManager::documentClosed, this, [this](DocumentId id) {
        for (const auto &unit : mUnits)
            if (unit->mDocumentId == id) unit->mDocumentId = 0;
    });
    connect(mEditorManager, &EditorManager::documentEncodingChanged, this,
            [this](DocumentId id, const QByteArray &encoding) {
        for (const auto &unit : mUnits)
            if (unit->mDocumentId == id) unit->setRealEncoding(encoding);
    });
}

std::shared_ptr<Project> Project::load(const QString &filename, EditorManager *editorList,
                                       IconsManager * iconsManager,
                                       QFileSystemWatcher *fileSystemWatcher)
{
    requireProjectOwnerThread(editorList, fileSystemWatcher);
    std::shared_ptr<Project> project(new Project(filename, "", editorList, iconsManager, fileSystemWatcher),
                                     deleteProjectOnOwnerThread);
    project->open();
    project->mModified = false;
    connectParserUi(project->mParser);
    const auto submitted = configureCppParser(project->mParser, project->mOptions.compilerSet,
        project->parserConfigurationValues(), project->directory());
    Q_UNUSED(submitted)
    return project;
}

std::shared_ptr<Project> Project::create(
        const QString &filename, const QString &name,
        EditorManager *editorList,
        IconsManager * iconsManager, QFileSystemWatcher *fileSystemWatcher,
        const std::shared_ptr<ProjectTemplate> pTemplate,
        bool useCpp)
{
    requireProjectOwnerThread(editorList, fileSystemWatcher);
    std::shared_ptr<Project> project(new Project(filename, name, editorList, iconsManager, fileSystemWatcher),
                                     deleteProjectOnOwnerThread);
    SimpleIni ini;
    ini.SetValue("Project","filename", toByteArray(extractRelativePath(project->directory(),
                                                                       project->mFilename)));
    ini.SetValue("Project","name", toByteArray(project->mName));
    project->mParser->setEnabled(false);
    if (!project->assignTemplate(pTemplate,useCpp))
        return std::shared_ptr<Project>();
    connectParserUi(project->mParser);
    const auto submitted = configureCppParser(project->mParser, project->mOptions.compilerSet,
        project->parserConfigurationValues(), project->directory());
    Q_UNUSED(submitted)

    project->mModified = true;
    const auto result = ProjectFileOperations::saveIni(project->mFilename, ini);
    if (!result.succeeded())
        throw FileError(result.error);
    return project;
}

// Resource closure is an explicit controller operation. A delayed release by
// a compiler/task must never close a reopened editor or touch GUI services.
Project::~Project() = default;

QString Project::directory() const
{
    QFileInfo fileInfo(mFilename);
    return fileInfo.absolutePath();
}

QString Project::outputFilename() const
{
    QString exeFileName;
    if (mOptions.useCustomOutputFilename && !mOptions.customOutputFilename.isEmpty()) {
        exeFileName = mOptions.customOutputFilename;
    } else {
        switch(mOptions.type) {
#ifdef ENABLE_SDCC
        case ProjectType::MicroController: {
            PCompilerSet pSet=pSettings->compilerSets().getSet(mOptions.compilerSet);
            if (pSet)
                exeFileName = changeFileExt(extractFileName(mFilename),pSet->executableSuffix());
            else
                exeFileName = changeFileExt(extractFileName(mFilename),SDCC_HEX_SUFFIX);
            }
            break;
#endif
        case ProjectType::StaticLib:
            exeFileName = changeFileExt(extractFileName(mFilename),STATIC_LIB_EXT);
            if (!exeFileName.startsWith("lib"))
                exeFileName = "lib" + exeFileName;
            break;
        case ProjectType::DynamicLib:
            exeFileName = changeFileExt(extractFileName(mFilename),DYNAMIC_LIB_EXT);
            if (!exeFileName.startsWith("lib"))
                exeFileName = "lib" + exeFileName;
            break;
        default:
            exeFileName = changeFileExt(extractFileName(mFilename),DEFAULT_EXECUTABLE_SUFFIX);
        }
    }
    QString exePath;
    if (!mOptions.folderForOutput.isEmpty()) {
        QDir baseDir(directory());
        exePath = baseDir.filePath(mOptions.folderForOutput);
    } else {
        exePath = directory();
    }
    QDir exeDir(exePath);
    return QDir::cleanPath(exeDir.filePath(exeFileName));
}

QString Project::makeFileName()
{
    if (mOptions.useCustomMakefile)
        return mOptions.customMakefile;
    else
        return QDir(directory()).filePath(MAKEFILE_NAME);
}

QString Project::xmakeFileName()
{
    return QDir(directory()).filePath(XMAKEFILE_NAME);
}

bool Project::unitsModifiedSince(const QDateTime& time)
{
    foreach(const PProjectUnit& unit, mUnits) {
        QFileInfo info(unit->fileName());
        if (info.lastModified()>time) {
            //qDebug()<<info.lastModified()<<time;
            return true;
        }
        Editor * e=unitEditor(unit);
        if (e && e->modified())
            return true;
    }
    return false;
}

bool Project::modified() const
{
    return mModified;
}

bool Project::modifiedSince(const QDateTime &time)
{
    if (modified())
        return true;
    QFileInfo info(filename());
    return (info.lastModified()>time);
}

void Project::open()
{
    mModel->beginUpdate();
    auto action = finally([this]{
        mModel->endUpdate();
    });
//    QFile fileInfo(mFilename);
    SimpleIni ini;
    const auto loadResult = ProjectFileOperations::loadIni(mFilename, ini);
    if (!loadResult.succeeded())
        throw FileError(loadResult.error);
    const bool converted = checkProjectFileForUpdate(ini); // candidate only
    const auto validPaths = validateUnitPaths(ini, directory());
    if (!validPaths.succeeded()) throw FileError(validPaths.error);
    if (converted) {
        QString backupPath = mFilename + ".bak";
        if (QFileInfo::exists(backupPath))
            backupPath += QLatin1Char('.') + QUuid::createUuid().toString(QUuid::WithoutBraces);
        auto saved = ProjectFileOperations::copyFile(mFilename, backupPath);
        if (saved.succeeded()) saved = ProjectFileOperations::saveIni(mFilename, ini);
        if (!saved.succeeded()) throw FileError(saved.error);
        QMessageBox::information(nullptr, tr("Project Updated"),
            tr("Your project was succesfully updated to a newer file format!") + "<br />"
            + tr("If something has gone wrong, we kept a backup-file: '%1'...").arg(backupPath), QMessageBox::Ok);
    }
    loadOptions(ini);

    mRootNode = makeProjectNode();

    int uCount  = ini.GetLongValue("Project","UnitCount",0);
    if (mOptions.modelType==ProjectModelType::FileSystem) {
        createFileSystemFolderNodes();
    } else {
        createFolderNodes();
    }
    QDir dir(directory());
    for (int i=0;i<uCount;i++) {
        PProjectUnit newUnit = std::make_shared<ProjectUnit>(this);
        QByteArray groupName = toByteArray(QString("Unit%1").arg(i+1));
        newUnit->setFileName(
                    cleanPath(dir.absoluteFilePath(
                        fromByteArray(ini.GetValue(groupName,"FileName","")))));
//        if (!QFileInfo(newUnit->fileName()).exists()) {
//            QMessageBox::critical(nullptr,
//                                  tr("File Not Found"),
//                                  tr("Project file '%1' can't be found!")
//                                  .arg(newUnit->fileName()),
//                                  QMessageBox::Ok);
//            newUnit->setModified(true);
//        } else {
//        newUnit->setFileMissing(!QFileInfo(newUnit->fileName()).exists());
        newUnit->setFolder(fromByteArray(ini.GetValue(groupName,"Folder","")));
        newUnit->setCompile(ini.GetBoolValue(groupName,"Compile", true));
        newUnit->setCompileCpp(
                    ini.GetBoolValue(groupName,"CompileCpp",mOptions.isCpp));

        newUnit->setLink(ini.GetBoolValue(groupName,"Link", true));
        newUnit->setPriority(ini.GetLongValue(groupName,"Priority", 1000));
        newUnit->setOverrideBuildCmd(ini.GetBoolValue(groupName,"OverrideBuildCmd", false));
        newUnit->setBuildCmd(fromByteArray(ini.GetValue(groupName,"BuildCmd", "")));
        newUnit->setEncoding(ini.GetValue(groupName, "FileEncoding",ENCODING_PROJECT));
        if (newUnit->encoding()!=ENCODING_UTF16_BOM &&
                newUnit->encoding()!=ENCODING_UTF8_BOM &&
                newUnit->encoding()!=ENCODING_UTF32_BOM &&
                !isEncodingAvailable(newUnit->encoding())) {
            newUnit->setEncoding(ENCODING_PROJECT);
        }
        newUnit->setRealEncoding(ini.GetValue(groupName, "RealEncoding",""));
        const QByteArray relocationState = ini.GetValue(groupName, "RmeleRelocationState", "");
        if (!relocationState.isEmpty()) {
            const QString expectedPath = fromByteArray(ini.GetValue(groupName, "RmeleRelocationTarget", ""));
            if (relocationState != "conflict" || expectedPath.isEmpty())
                throw FileError(tr("Invalid saved relocation state for project file '%1'.").arg(newUnit->fileName()));
            QString diagnostic = fromByteArray(ini.GetValue(groupName, "RmeleRelocationDiagnostic", ""));
            if (diagnostic.isEmpty())
                diagnostic = tr("This project file has an unresolved rename conflict. Save detached buffers with Save As, then resolve or remove and re-add the unit.");
            newUnit->mRelocation = {ProjectUnit::RelocationState::Conflict,
                                   cleanPath(dir.absoluteFilePath(expectedPath)), diagnostic};
        }

        PProjectModelNode parentNode;
        if (mOptions.modelType==ProjectModelType::FileSystem) {
            parentNode = getParentFileSystemFolderNode(newUnit->fileName());
        } else {
            parentNode = getCustomeFolderNodeFromName(newUnit->folder());
        }
        PProjectModelNode node = makeNewFileNode(newUnit,
                                                 newUnit->priority(),
                                                 parentNode
                                                 );
        newUnit->setNode(node);
        mUnits.insert(unitPathKey(newUnit->fileName()),newUnit);
    }
}

//void Project::setFileName(QString value)
//{
//    value = QFileInfo(value).absoluteFilePath();
//    if (mFilename!=value) {
//        QFile::rename(mFilename,value);
//        mFilename = value;
//        setModified(true);
//    }
//}

void Project::setModified(bool value)
{
    if (mModified!=value) {
        mModified=value;
        emit modifyChanged(mModified);
    }
}

PProjectModelNode Project::makeNewFolderNode(
        const QString &folderName, PProjectModelNode newParent,
        ProjectModelNodeType nodeType,int priority)
{
    PProjectModelNode node = std::make_shared<ProjectModelNode>();
    if (!newParent) {
        newParent = mRootNode;
    }
    node->parent = newParent;
    node->text = folderName;
    if (newParent) {
        node->level = newParent->level+1;
    }
    node->isUnit=false;
    node->priority = priority;
    node->folderNodeType = nodeType;
    QModelIndex parentIndex=mModel->getNodeIndex(newParent.get());
    newParent->children.append(node);
    mModel->insertRow(newParent->children.count()-1,parentIndex);
    return node;
}

PProjectModelNode Project::makeNewFileNode(PProjectUnit unit,int priority, PProjectModelNode newParent)
{
    PProjectModelNode node = std::make_shared<ProjectModelNode>();
    if (!newParent) {
        newParent = mRootNode;
    }
    node->parent = newParent;
    node->text = extractFileName(unit->fileName());
    if (newParent) {
        node->level = newParent->level+1;
    }
    node->isUnit = true;
    node->pUnit = unit;
    node->priority = priority;
    node->folderNodeType = ProjectModelNodeType::File;

    newParent->children.append(node);
    QModelIndex parentIndex=mModel->getNodeIndex(newParent.get());
    mModel->insertRow(newParent->children.count()-1,parentIndex);
    return node;
}

PProjectModelNode Project::makeProjectNode()
{
    PProjectModelNode node = std::make_shared<ProjectModelNode>();
    node->text = mName;
    node->level = 0;
    node->isUnit = false;
    node->folderNodeType = ProjectModelNodeType::Folder;
    return node;
}

PProjectUnit Project::newUnit(PProjectModelNode parentNode, const QString& customFileName)
{
    // Select folder to add unit to
    if (!parentNode)
        parentNode = mRootNode; // project root node

    if (parentNode->isUnit) { //it's a file
        parentNode = mRootNode;
    }
    QString s;
    QDir dir(directory());
    // Find unused 'new' filename
    if (customFileName.isEmpty()) {
        do {
            s = cleanPath(dir.absoluteFilePath(QString("untitled%1").arg(getNewFileNumber())));
        } while (fileExists(s));
    } else {
        s = cleanPath(dir.absoluteFilePath(customFileName));
    }
    PProjectUnit newUnit = internalAddUnit(s,parentNode);
    if (newUnit) emit unitAdded(newUnit->fileName());
    return newUnit;
}

Editor* Project::openUnit(PProjectUnit& unit, bool forceOpen) {
    if (unit && mUnits.value(unitPathKey(unit->fileName())) != unit)
        throw FileError(tr("The project unit is no longer available."));
    if (!unit || unit->relocation().state != ProjectUnit::RelocationState::Resolved) return nullptr;
    if (!unit->fileName().isEmpty() && fileExists(unit->fileName())) {
        if (getFileType(unit->fileName())==FileType::Other) {
            if (forceOpen)
                QDesktopServices::openUrl(QUrl::fromLocalFile(unit->fileName()));
            return nullptr;
        }

        Editor * editor = mEditorManager->getOpenedEditor(unit->fileName());
        if (editor) {//already opened in the editors
            const auto associated = associateEditorToUnit(editor, unit);
            if (!associated.succeeded()) throw FileError(associated.error);
            mEditorManager->activeEditor(editor,true);
            return editor;
        }
        QByteArray encoding;
        encoding = unit->encoding();
        if (encoding==ENCODING_PROJECT)
            encoding=options().encoding;

        editor = mEditorManager->newEditor(unit->fileName(), encoding, FileType::None, QString(), true, false);
        const auto associated = associateEditorToUnit(editor, unit);
        if (!associated.succeeded()) throw FileError(associated.error);
        loadUnitLayout(editor);
        mEditorManager->activeEditor(editor,true);
        return editor;
    }
    return nullptr;
}

Editor *Project::openUnit(PProjectUnit &unit, const PProjectEditorLayout &layout)
{
    if (unit && mUnits.value(unitPathKey(unit->fileName())) != unit)
        throw FileError(tr("The project unit is no longer available."));
    if (!unit || unit->relocation().state != ProjectUnit::RelocationState::Resolved) return nullptr;
    if (!unit->fileName().isEmpty() && fileExists(unit->fileName())) {
        if (getFileType(unit->fileName())==FileType::Other) {
            return nullptr;
        }

        Editor * editor = mEditorManager->getOpenedEditor(unit->fileName());
        if (editor) {//already opened in the editors
            const auto associated = associateEditorToUnit(editor, unit);
            if (!associated.succeeded()) throw FileError(associated.error);
            mEditorManager->activeEditor(editor,true);
            return editor;
        }
        QByteArray encoding;
        encoding = unit->encoding();
        if (encoding==ENCODING_PROJECT)
            encoding=options().encoding;
        editor = mEditorManager->newEditor(unit->fileName(), encoding, FileType::None, QString(), true, false);
        const auto associated = associateEditorToUnit(editor, unit);
        if (!associated.succeeded()) throw FileError(associated.error);
        if (editor) {
            //editor->setInProject(true);
            editor->setCaretY(layout->caretY);
            editor->setCaretX(layout->caretX);
            editor->setTopPos(layout->top);
            editor->setLeftPos(layout->left);
            mEditorManager->activeEditor(editor,true);
            return editor;
        }
    }
    return nullptr;
}

Editor *Project::unitEditor(const PProjectUnit &unit) const
{
    if (QThread::currentThread() != thread()) return nullptr;
    if (!unit || mUnits.value(unitPathKey(unit->fileName())) != unit
            || unit->relocation().state != ProjectUnit::RelocationState::Resolved)
        return nullptr;
    return mEditorManager->editorForDocument(unit->documentId());
}

Editor *Project::unitEditor(const ProjectUnit *unit) const
{
    if (QThread::currentThread() != thread()) return nullptr;
    if (!unit || mUnits.value(unitPathKey(unit->fileName())).get() != unit
            || unit->relocation().state != ProjectUnit::RelocationState::Resolved)
        return nullptr;
    return mEditorManager->editorForDocument(unit->documentId());
}

QList<PProjectUnit> Project::unitList()
{
    QList<PProjectUnit> units;
    foreach(PProjectUnit unit, mUnits) {
        units.append(unit);
    }
    return units;
}

QStringList Project::unitFiles()
{
    QStringList units;
    foreach(PProjectUnit unit, mUnits) {
        units.append(unit->fileName());
    }
    return units;
}

void Project::rebuildNodes()
{
    mModel->beginUpdate();
    // Delete everything
    mRootNode->children.clear();
    mCustomFolderNodes.clear();
    mSpecialNodes.clear();
    mFileSystemFolderNodes.clear();

    // Recreate everything
    switch(mOptions.modelType) {
    case ProjectModelType::Custom:
        createFolderNodes();
        foreach (PProjectUnit pUnit, mUnits) {
            pUnit->setNode(
                        makeNewFileNode(
                            pUnit,
                            pUnit->priority(),
                            getCustomeFolderNodeFromName(pUnit->folder())
                            )
                        );
        }
        break;
    case ProjectModelType::FileSystem:
        createFileSystemFolderNodes();

        foreach (PProjectUnit pUnit, mUnits) {
            pUnit->setNode(
                        makeNewFileNode(
                            pUnit,
                            pUnit->priority(),
                            getParentFileSystemFolderNode(
                                pUnit->fileName())
                            )
                        );
        }

        break;
    }

    mModel->endUpdate();
}

OperationResult Project::removeUnit(PProjectUnit &unit, bool doClose, bool removeFile,
                                     const TrashFile &trashFile)
{
    if (!unit || mUnits.value(unitPathKey(unit->fileName())) != unit)
        return OperationResult::conflict(tr("The project unit changed before removal could be applied."));
    const QString fileName = unit->fileName();
    if (removeFile && !QFileInfo(fileName).isFile())
        return OperationResult::failure(tr("The project file cannot be moved to the Recycle Bin: '%1'.").arg(fileName));

    const QPointer<Project> lifetime(this);
    const QPointer<EditorManager> editors(mEditorManager);
    const QPointer<Editor> editor(unitEditor(unit));
    EditorClosePlan closePlan;
    if (editor && doClose) {
        // Temporary legacy controller adapter: collect the decision without
        // closing/detaching a buffer. The plan owns the accepted revision.
        const auto prepared = closePlan.prepare({editor.data()}, [](Editor *candidate, bool &discard) {
            const QPointer<Editor> live(candidate);
            const auto answer = QMessageBox::question(candidate, QObject::tr("Save"),
                QObject::tr("Save changes to %1?").arg(candidate->filename()),
                QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
            if (!live)
                return OperationResult::conflict(tr("A document was closed while preparing removal."));
            if (answer == QMessageBox::No) {
                discard = true;
                return OperationResult::success();
            }
            if (answer != QMessageBox::Yes)
                return OperationResult::cancelled();
            if (!live->save(false, false))
                return OperationResult::failure(tr("The document could not be saved. It was not removed from the project."));
            return OperationResult::success();
        });
        if (!prepared.succeeded()) return prepared;
        if (!lifetime || !editors)
            return OperationResult::conflict(tr("The project was closed while preparing removal."));
        const auto valid = closePlan.validate(*editors, EditorCloseScope::PlannedEditors);
        if (!valid.succeeded()) return valid;
    }
    if (!lifetime || !editors || !unit || unit->fileName() != fileName || mUnits.value(unitPathKey(fileName)) != unit
            || unitEditor(unit) != editor.data())
        return OperationResult::conflict(tr("The project unit changed before removal could be applied."));

    // A failure to trash is a failure to remove, not permission to permanently
    // delete the file. No editor/model changes have happened at this point.
    if (removeFile && !(trashFile ? trashFile(fileName) : QFile::moveToTrash(fileName)))
        return OperationResult::failure(tr("The project file could not be moved to the Recycle Bin: '%1'.").arg(fileName));
    bool committed = removeFile;
    QString followUpError;
    try {
        mModel->beginUpdate();
        {
            const auto finishModel = finally([this] { mModel->endUpdate(); });
            mUnits.remove(unitPathKey(fileName));
            unit->mDocumentId = 0;
            committed = true;
            setModified(true);
            rebuildNodes();
        }
        emit unitRemoved(fileName);
        if (!lifetime)
            return {OperationStatus::CommittedWithFollowUpPending,
                    tr("The project was closed after the file removal committed.")};
        if (editor) {
            if (doClose) {
                // Model-reset callbacks may have edited a buffer. An accepted
                // discard never authorizes destroying that different revision.
                const auto stillValid = closePlan.validate(*editors, EditorCloseScope::PlannedEditors);
                editor->setInProject(false, false);
                if (!stillValid.succeeded())
                    followUpError = stillValid.error;
                else
                    editors->forceCloseEditor(editor);
            } else {
                editor->setInProject(false, false);
                editor->setCppParser();
            }
        }
        if (!lifetime)
            return {OperationStatus::CommittedWithFollowUpPending,
                    tr("The project was closed after the file removal committed.")};
        if (mParser) {
            mParser->removeProjectFile(fileName);
            mParser->invalidateFile(fileName);
        }
    } catch (const BaseError &error) {
        return committed ? OperationResult{OperationStatus::CommittedWithFollowUpPending, error.reason()}
                         : OperationResult::failure(error.reason());
    }
    if (!followUpError.isEmpty())
        return {OperationStatus::CommittedWithFollowUpPending, followUpError};
    return OperationResult::success();
}

OperationResult Project::removeFolder(PProjectModelNode node)
{
    if (!node || node->isUnit || node->level < 1)
        return OperationResult::conflict(tr("The project folder is no longer available."));
    const QPointer<Project> lifetime(this);
    const QString folderPath = getNodePath(node);
    QList<PProjectUnit> units;
    const std::function<void(const PProjectModelNode &)> collect = [&](const PProjectModelNode &parent) {
        for (const auto &child : parent->children) {
            if (child->isUnit) {
                if (const auto unit = child->pUnit.lock()) units.append(unit);
            } else collect(child);
        }
    };
    collect(node);
    int removed = 0;
    for (auto &unit : units) {
        const auto result = removeUnit(unit, true);
        if (!lifetime) return {OperationStatus::CommittedWithFollowUpPending,
                              tr("The project was closed during folder removal.")};
        if (!result.succeeded()) {
            if (removed || result.committed())
                return {OperationStatus::CommittedWithFollowUpPending,
                        tr("Some project files were already removed. No further files were removed.") + QLatin1Char('\n') + result.error};
            return result;
        }
        ++removed;
    }
    mModel->beginUpdate();
    const auto finishModel = finally([this] { mModel->endUpdate(); });
    for (int i = mFolders.size() - 1; i >= 0; --i)
        if (mFolders[i] == folderPath || mFolders[i].startsWith(folderPath + QLatin1Char('/')))
            mFolders.removeAt(i);
    rebuildNodes();
    setModified(true);
    return OperationResult::success();
}

Analysis::ParserConfigurationValues Project::parserConfigurationValues() const
{
    auto values = mParser->requestedConfigurationValues();
    values.projectFiles.clear(); values.projectIncludePaths = mOptions.includeDirs;
    for (const auto &unit : mUnits)
        if (isCFile(unit->fileName()) || isHFile(unit->fileName())) values.projectFiles.insert(unit->fileName());
    return values;
}

void Project::resetParserProjectFiles()
{
    const auto submitted = configureCppParser(mParser, mOptions.compilerSet, parserConfigurationValues(), directory());
    Q_UNUSED(submitted)
}

OperationResult Project::saveMetadata()
{
    SimpleIni candidate;
    auto result = ProjectFileOperations::loadIni(mFilename, candidate);
    if (!result.succeeded()) {
        setModified(true);
        return result;
    }
    populateUnits(candidate);
    populateOptions(candidate);
    result = ProjectFileOperations::saveIni(mFilename, candidate);
    if (!result.succeeded())
        setModified(true);
    return result;
}

void Project::saveAll()
{
    // Legacy controller adapter. Metadata is one candidate/commit, not two
    // separate writes that can leave units and options from different saves.
    setModified(true);
    const auto result = saveMetadata();
    if (!result.succeeded())
        throw FileError(result.error);
    saveLayout();
    // Layout failure leaves the project dirty for a visible retry. A metadata
    // commit that already succeeded is retained; no false rollback is claimed.
    setModified(false);
}

void Project::saveLayout()
{
    if (!QFileInfo(directory()).isDir())
        throw FileError(tr("Project directory does not exist: '%1'.").arg(directory()));

    QHash<QString, PProjectEditorLayout> oldLayouts = loadLayout();

    QHash<QString,int> editorOrderSet;
    // Write list of open project files
    int order=0;
    for (int i=0;i<mEditorManager->pageCount();i++) {
        Editor* e=(*mEditorManager)[i];
        if (e && e->inProject() && !editorOrderSet.contains(e->filename())) {
            editorOrderSet.insert(e->filename(),order);
            order++;
        }
    }
//    layIni.SetValue("Editors","Order",sl.join(",").toUtf8());

    Editor *e, *e2;
    // Remember what files were visible
    mEditorManager->getVisibleEditors(e, e2);

    QJsonArray jsonLayouts;
    // save editor info
    foreach (const PProjectUnit& unit,mUnits) {
        Editor* editor = unitEditor(unit);
        if (editor) {
            QJsonObject jsonLayout;
            jsonLayout["filename"]=unit->fileName();
            jsonLayout["caretX"]=editor->caretX();
            jsonLayout["caretY"]=editor->caretY();
            jsonLayout["top"]=editor->topPos();
            jsonLayout["left"]=editor->leftPos();
            jsonLayout["isOpen"]=true;
            jsonLayout["focused"]=(editor==e);
            int order=editorOrderSet.value(editor->filename(),-1);
            if (order>=0) {
                jsonLayout["order"]=order;
            }
            jsonLayouts.append(jsonLayout);
        } else {
            PProjectEditorLayout oldLayout = oldLayouts.value(unit->fileName(),PProjectEditorLayout());
            if (oldLayout) {
                QJsonObject jsonLayout;
                jsonLayout["filename"]=unit->fileName();
                jsonLayout["caretX"]=oldLayout->caretX;
                jsonLayout["caretY"]=oldLayout->caretY;
                jsonLayout["top"]=oldLayout->top;
                jsonLayout["left"]=oldLayout->left;
                jsonLayout["isOpen"]=false;
                jsonLayout["focused"]=false;
                jsonLayouts.append(jsonLayout);
            }
        }
    }

    QString jsonFilename = changeFileExt(filename(), "layout");
    QSaveFile file(jsonFilename);
    file.setDirectWriteFallback(false);
    const QByteArray data = QJsonDocument(jsonLayouts).toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly))
        throw FileError(tr("Cannot save '%1': %2").arg(jsonFilename, file.errorString()));
    if (file.write(data) != data.size() || !file.commit())
        throw FileError(tr("Cannot save '%1': %2").arg(jsonFilename, file.errorString()));
}

OperationResult Project::reconcileUnitRename(PProjectUnit &unit, const QString &newFileName)
{
    if (!unit || mUnits.value(unitPathKey(unit->fileName())) != unit)
        return OperationResult::conflict(tr("The project unit changed before the rename could be applied."));
    const QString oldFileName = unit->fileName();
    if (newFileName == oldFileName && unit->relocation().state == ProjectUnit::RelocationState::Resolved)
        return OperationResult::success();
    if (!QFileInfo(newFileName).isFile())
        return OperationResult::failure(tr("The renamed project file does not exist: '%1'.").arg(newFileName));
    Editor *editor = unitEditor(unit);
    const auto targetDocument = mEditorManager->resolveOpenedEditor(newFileName);
    Editor *targetEditor = targetDocument.editor;
    const auto targetPath = resolveUnitPath(newFileName);
    const PProjectUnit target = targetPath.unit;
    const bool bufferConflict = targetEditor && targetEditor != editor;
    const auto unresolved = [&](const QString &diagnostic) {
        unit->mRelocation = {ProjectUnit::RelocationState::Conflict, newFileName, diagnostic};
        unit->mDocumentId = 0;
        if (editor) {
            editor->setInProject(false, false);
            editor->setModified(true);
        }
        setModified(true);
        return OperationResult{OperationStatus::CommittedWithFollowUpPending, diagnostic};
    };
    if (targetPath.status == ProjectPathStatus::Conflict)
        return unresolved(targetPath.error);
    if (targetDocument.status == EditorPathStatus::Conflict)
        return unresolved(targetDocument.error);
    if (target && target != unit)
        return unresolved(tr("The file was renamed on disk to '%1', but another project unit already owns that path. "
                             "Both units and open buffers were preserved. Save detached buffers with Save As, then remove and re-add the unresolved unit.")
                          .arg(newFileName));
    const QString contextFile = editor ? editor->contextFile() : QString();
    // Commit identity without replacing the ProjectUnit: encoding, compile
    // flags, custom command, priority and folder all remain attached to it.
    {
        mModel->beginUpdate();
        const auto finishModelUpdate = finally([this] { mModel->endUpdate(); });
        mUnits.remove(unitPathKey(oldFileName));
        unit->setFileName(newFileName);
        unit->mRelocation = bufferConflict
                ? ProjectUnit::Relocation{ProjectUnit::RelocationState::Conflict, newFileName,
                    tr("The file was renamed on disk, but its destination is open in another editor. "
                       "Both buffers were preserved. Save detached buffers with Save As, then resolve or remove and re-add the project unit.")}
                : ProjectUnit::Relocation{};
        if (bufferConflict) unit->mDocumentId = 0;
        mUnits.insert(unitPathKey(newFileName), unit);
        rebuildNodes();
    }
    setModified(true);

    // The file/model commit is already complete. A recoverable parser failure
    // is a follow-up failure, never permission to remove either disk file.
    try {
        if (bufferConflict) {
            if (editor) {
                editor->setInProject(false, false);
                editor->setModified(true);
            }
        } else if (editor) {
            editor->rename(newFileName);
            if (editor->filename() != newFileName)
                return unresolved(tr("The file and project were renamed, but the editor could not adopt the new path. "
                                     "Its contents were preserved. Save the detached buffer with Save As and resolve the project unit."));
        }
        emit unitRenamed(oldFileName, newFileName);
        emit nodeRenamed();
        if (mParser) {
            mParser->removeProjectFile(oldFileName);
            mParser->addProjectFile(newFileName, true);
            mParser->invalidateFile(oldFileName);
            CppParser::parseFileNonBlocking(mParser, newFileName, true, contextFile);
        }
    } catch (const BaseError &error) {
        return {OperationStatus::CommittedWithFollowUpPending, error.reason()};
    }
    if (bufferConflict)
        return {OperationStatus::CommittedWithFollowUpPending, unit->relocation().diagnostic};
    return OperationResult::success();
}

OperationResult Project::resolveUnitRelocation(PProjectUnit &unit, const QString &verifiedFileName,
                                               Editor *acceptedEditor)
{
    UnitLocationPlan::Pointer plan;
    const auto prepared = prepareUnitLocation(unit, UnitLocationCommand::RelinkExistingFile,
                                              verifiedFileName, acceptedEditor, plan);
    return prepared.succeeded() ? commitUnitLocation(*plan).outcome : prepared;
}

UnitLocationPlan::Settings Project::locationSettings(const PProjectUnit &unit)
{
    return {unit->compile(), unit->compileCpp(), unit->link(), unit->overrideBuildCmd(),
            unit->priority(), unit->folder(), unit->buildCmd(), unit->encoding()};
}

OperationResult Project::prepareUnitLocation(const PProjectUnit &unit, UnitLocationCommand command,
                                             const QString &target, Editor *acceptedEditor,
                                             UnitLocationPlan::Pointer &prepared) const
{
    prepared.reset();
    if (QThread::currentThread() != thread())
        return OperationResult::conflict(tr("Project file operations must run on the editor's owner thread."));
    if (!unit || findUnit(unit->fileName()) != unit || target.isEmpty())
        return OperationResult::conflict(tr("The project unit or selected recovery file is no longer available."));
    const QString destination = cleanPath(QFileInfo(target).absoluteFilePath());
    const QFileInfo info(destination);
    if (command != UnitLocationCommand::InspectSource &&
            ((command == UnitLocationCommand::RelinkExistingFile && !info.isFile()) || (info.exists() && !info.isFile())))
        return OperationResult::failure(tr("The selected destination is not an existing regular file: '%1'.").arg(destination));
    auto plan = std::shared_ptr<UnitLocationPlan>(new UnitLocationPlan);
    plan->mProject = this;
    plan->mUnit = unit;
    plan->mCommand = command;
    plan->mSourcePath = unit->fileName();
    plan->mTargetPath = destination;
    plan->mSettings = locationSettings(unit);
    plan->mProjectEncoding = mOptions.encoding;
    plan->mHadRelocationConflict = unit->relocation().state != ProjectUnit::RelocationState::Resolved;
    plan->mRelocationTarget = unit->relocation().expectedFileName;
    plan->mRelocationDiagnostic = unit->relocation().diagnostic;
    plan->mSourceDocument = unit->documentId();
    plan->mAcceptedDocument = mEditorManager->documentId(acceptedEditor);
    if (plan->mSourceDocument) {
        plan->mSourceRevision = mEditorManager->documentRevision(mEditorManager->editorForDocument(plan->mSourceDocument));
        if (!plan->mSourceRevision) return OperationResult::conflict(tr("The source document is changing or no longer available."));
    }
    if (acceptedEditor) {
        plan->mAcceptedRevision = mEditorManager->documentRevision(acceptedEditor);
        if (!plan->mAcceptedRevision) return OperationResult::conflict(tr("The selected document is changing or no longer available."));
    }
    if (command == UnitLocationCommand::SaveDocumentAs &&
            (!plan->mSourceDocument || plan->mSourceDocument != plan->mAcceptedDocument || plan->mHadRelocationConflict))
        return OperationResult::conflict(tr("The source document is not bound to this project file."));
    const auto valid = validateUnitLocation(*plan);
    if (!valid.succeeded()) return valid;
    prepared = std::move(plan);
    return OperationResult::success();
}

OperationResult Project::validateUnitLocation(const UnitLocationPlan &plan) const
{
    if (QThread::currentThread() != thread())
        return OperationResult::conflict(tr("Project file operations must run on the editor's owner thread."));
    const auto unit = plan.mUnit.lock();
    if (plan.mProject != this || !unit || findUnit(plan.mSourcePath) != unit || unit->fileName() != plan.mSourcePath
            || !(locationSettings(unit) == plan.mSettings) || mOptions.encoding != plan.mProjectEncoding
            || unit->documentId() != plan.mSourceDocument
            || (unit->relocation().state != ProjectUnit::RelocationState::Resolved) != plan.mHadRelocationConflict
            || unit->relocation().expectedFileName != plan.mRelocationTarget
            || unit->relocation().diagnostic != plan.mRelocationDiagnostic)
        return OperationResult::conflict(tr("The project file changed while preparing the operation. Please try again."));
    const auto validateDocument = [&](DocumentId id, DocumentRevision revision) {
        if (!id) return true;
        const auto editor = mEditorManager->editorForDocument(id);
        return editor && mEditorManager->documentId(editor) == id && mEditorManager->documentRevision(editor) == revision;
    };
    if (!validateDocument(plan.mSourceDocument, plan.mSourceRevision)
            || !validateDocument(plan.mAcceptedDocument, plan.mAcceptedRevision))
        return OperationResult::conflict(tr("An open document changed while preparing the operation. Please try again."));
    if (plan.mCommand == UnitLocationCommand::InspectSource) return OperationResult::success();
    const auto targetPath = resolveUnitPath(plan.mTargetPath);
    if (targetPath.status == ProjectPathStatus::Conflict) return OperationResult::conflict(targetPath.error);
    const auto targetUnit = targetPath.unit;
    if (targetUnit && targetUnit != unit)
        return OperationResult::conflict(tr("File '%1' is already in the project.").arg(plan.mTargetPath));
    Editor *accepted = mEditorManager->editorForDocument(plan.mAcceptedDocument);
    const auto destination = mEditorManager->resolveOpenedEditor(plan.mTargetPath);
    if (destination.status == EditorPathStatus::Conflict) return OperationResult::conflict(destination.error);
    Editor *occupant = destination.editor;
    if (occupant && occupant != accepted)
        return OperationResult::conflict(tr("The destination is open in another document."));
    if (plan.mCommand == UnitLocationCommand::RelinkExistingFile && occupant != accepted)
        return OperationResult::conflict(tr("Choose explicitly which open buffer belongs to the recovered project unit."));
    if (plan.mCommand == UnitLocationCommand::RelinkExistingFile && accepted
            && !verifiedPathAlias(mEditorManager->documentAbsolutePath(accepted), plan.mTargetPath))
        return OperationResult::conflict(tr("The selected document does not match the project file."));
    const auto acceptedUnit = findUnit(accepted);
    if (acceptedUnit && acceptedUnit != unit)
        return OperationResult::conflict(tr("The selected document already belongs to another project file."));
    return OperationResult::success();
}

UnitLocationCommitResult Project::commitUnitLocation(const UnitLocationPlan &plan)
{
    UnitLocationCommitResult result;
    result.metadata.path = mFilename;
    if (plan.mCommand == UnitLocationCommand::InspectSource) {
        result.outcome = OperationResult::conflict(tr("A source inspection cannot be committed."));
        return result;
    }
    result.outcome = validateUnitLocation(plan);
    if (!result.outcome.succeeded()) return result;
    if (!QFileInfo(plan.mTargetPath).isFile()) {
        result.outcome = OperationResult::conflict(tr("The selected recovery file is no longer available."));
        return result;
    }
    const auto unit = plan.mUnit.lock();
    SimpleIni candidate;
    result.outcome = ProjectFileOperations::loadIni(mFilename, candidate);
    if (!result.outcome.succeeded()) { result.metadata.state = ResourceCommitState::Failed; result.metadata.error = result.outcome.error; return result; }
    populateUnits(candidate, &plan);
    populateOptions(candidate);
    result.outcome = ProjectFileOperations::saveIni(mFilename, candidate);
    if (!result.outcome.succeeded()) { result.metadata.state = ResourceCommitState::Failed; result.metadata.error = result.outcome.error; return result; }
    result.metadata.state = ResourceCommitState::Committed;
    const QPointer<Project> lifetime(this);
    const QPointer<Editor> oldEditor = mEditorManager->editorForDocument(plan.mSourceDocument);
    const QPointer<Editor> accepted = mEditorManager->editorForDocument(plan.mAcceptedDocument);
    try {
        mModel->beginUpdate();
        {
            const auto finish = finally([model = QPointer<ProjectModel>(mModel)] { if (model) model->endUpdate(); });
            const auto stillValid = validateUnitLocation(plan);
            if (!stillValid.succeeded()) {
                result.outcome = deferUnitLocation(plan, stillValid.error);
                return result;
            }
            mUnits.remove(unitPathKey(plan.mSourcePath));
            unit->setFileName(plan.mTargetPath);
            unit->mRelocation = {};
            unit->mDocumentId = plan.mAcceptedDocument;
            unit->setRealEncoding(accepted ? accepted->fileEncoding() : QByteArray{});
            mUnits.insert(unitPathKey(plan.mTargetPath), unit);
            result.projection = UnitProjectionState::Applied;
            setModified(true);
            rebuildNodes();
        }
        if (!lifetime) {
            result.outcome = {OperationStatus::CommittedWithFollowUpPending, tr("The project was closed after its metadata was saved.")};
            return result;
        }
        if (oldEditor && oldEditor != accepted) {
            oldEditor->setInProject(false, false);
            if (!QFileInfo(oldEditor->filename()).isFile()) oldEditor->setModified(true);
        }
        if (accepted) accepted->setInProject(true, plan.mCommand == UnitLocationCommand::RelinkExistingFile);
        if (mParser) {
            mParser->removeProjectFile(plan.mSourcePath);
            mParser->addProjectFile(plan.mTargetPath, true);
            mParser->invalidateFile(plan.mSourcePath);
            const auto requested = CppParser::parseFileNonBlocking(mParser, plan.mTargetPath, true,
                                                                    accepted ? accepted->contextFile() : QString());
            if (requested == CppParser::RequestResult::InputUnavailable)
                result.outcome = {OperationStatus::CommittedWithFollowUpPending,
                    tr("The project reference was saved, but analysis input is temporarily unavailable. Please retry analysis.")};
        }
        if (plan.mSourcePath != plan.mTargetPath) emit unitRenamed(plan.mSourcePath, plan.mTargetPath);
        emit nodeRenamed();
    } catch (const BaseError &error) {
        result.outcome = {OperationStatus::CommittedWithFollowUpPending, error.reason()};
    }
    return result;
}

OperationResult Project::deferUnitLocation(const UnitLocationPlan &plan, const QString &diagnostic)
{
    const auto unit = plan.mUnit.lock();
    if (plan.mProject != this || !unit || findUnit(unit->fileName()) != unit)
        return {OperationStatus::CommittedWithFollowUpPending, diagnostic};
    const auto source = mEditorManager->editorForDocument(unit->documentId());
    unit->mRelocation = {ProjectUnit::RelocationState::Conflict, plan.mTargetPath, diagnostic};
    unit->mDocumentId = 0;
    if (source) {
        source->setInProject(false, false);
        source->setModified(true);
    }
    setModified(true);
    const auto saved = saveMetadata();
    return {OperationStatus::CommittedWithFollowUpPending, saved.succeeded() ? diagnostic
        : diagnostic + QLatin1Char('\n') + tr("The pending project state could not be saved: %1").arg(saved.error)};
}

OperationResult Project::validateBuildInputs() const
{
    for (const auto &unit : mUnits) {
        if (unit->relocation().state != ProjectUnit::RelocationState::Resolved) {
            const QString diagnostic = unit->relocation().diagnostic.isEmpty()
                    ? tr("This project file has an unresolved rename conflict. Save detached buffers with Save As, then resolve or remove and re-add the unit.")
                    : unit->relocation().diagnostic;
            return OperationResult::conflict(unit->fileName() + QLatin1Char('\n') + diagnostic);
        }
    }
    return OperationResult::success();
}

OperationResult Project::renameUnit(PProjectUnit &unit, const QString &newFileName)
{
    // Adapter for QFileSystemModel notifications: the filesystem rename has
    // already happened. Never copy/delete the old path here.
    const auto result = reconcileUnitRename(unit, newFileName);
    return result.committed() ? result
        : OperationResult{OperationStatus::CommittedWithFollowUpPending, result.error};
}

OperationResult Project::renameUnitFile(PProjectUnit &unit, const QString &newFileName, bool replaceExisting)
{
    if (!unit || mUnits.value(unitPathKey(unit->fileName())) != unit)
        return OperationResult::conflict(tr("The project unit changed before the rename could be applied."));
    const QString oldFileName = unit->fileName();
    if (oldFileName == newFileName)
        return OperationResult::success();
    const auto targetPath = resolveUnitPath(newFileName);
    if (targetPath.status == ProjectPathStatus::Conflict) return OperationResult::conflict(targetPath.error);
    const PProjectUnit replacedUnit = targetPath.unit;
    if (replacedUnit && replacedUnit != unit && !replaceExisting)
        return OperationResult::conflict(tr("File '%1' is already in the project.").arg(newFileName));
    const auto targetDocument = mEditorManager->resolveOpenedEditor(newFileName);
    if (targetDocument.status == EditorPathStatus::Conflict) return OperationResult::conflict(targetDocument.error);
    Editor *targetEditor = targetDocument.editor;
    if (targetEditor && targetEditor != unitEditor(unit))
        return OperationResult::conflict(tr("Close the destination editor before renaming the project file."));

    const bool watchedOld = mFileSystemWatcher->files().contains(oldFileName);
    const bool watchedTarget = mFileSystemWatcher->files().contains(newFileName);
    mFileSystemWatcher->removePath(oldFileName);
    mFileSystemWatcher->removePath(newFileName);
    ProjectFileOperations::RenameTransaction transaction(oldFileName, newFileName, replaceExisting);
    const auto diskResult = transaction.commit();
    if (!diskResult.succeeded()) {
        if (watchedOld) mFileSystemWatcher->addPath(oldFileName);
        if (watchedTarget) mFileSystemWatcher->addPath(newFileName);
        return diskResult;
    }
    // Nothing was removed from the model before the filesystem commit.
    if (replacedUnit && replacedUnit != unit) {
        mUnits.remove(unitPathKey(replacedUnit->fileName()));
    }
    const auto pendingResult = [&transaction](QString error) {
        if (!transaction.retainedBackup().isEmpty())
            error += QLatin1Char('\n') + tr("The previous destination is preserved at '%1'.").arg(transaction.retainedBackup());
        return OperationResult{OperationStatus::CommittedWithFollowUpPending, std::move(error)};
    };
    OperationResult modelResult;
    try {
        modelResult = reconcileUnitRename(unit, newFileName);
    } catch (const BaseError &error) {
        setModified(true);
        if (watchedOld || watchedTarget)
            mFileSystemWatcher->addPath(newFileName);
        return pendingResult(error.reason());
    }
    if (replacedUnit && replacedUnit != unit)
        emit unitRemoved(replacedUnit->fileName());
    if (watchedOld || watchedTarget || unitEditor(unit))
        mFileSystemWatcher->addPath(newFileName);
    if (!modelResult.succeeded()) {
        setModified(true);
        return pendingResult(modelResult.error);
    }
    return transaction.finish();
}

bool Project::saveUnits()
{
    SimpleIni candidate;
    auto result = ProjectFileOperations::loadIni(mFilename, candidate);
    if (result.succeeded()) {
        populateUnits(candidate);
        result = ProjectFileOperations::saveIni(mFilename, candidate);
    }
    if (!result.succeeded()) {
        setModified(true);
        throw FileError(result.error);
    }
    return true;
}

void Project::populateUnits(SimpleIni &ini, const UnitLocationPlan *location)
{
    int count = 0;
    int i=0;
    foreach (const PProjectUnit& unit, mUnits) {
        i++;
        QByteArray groupName = toByteArray(QString("Unit%1").arg(i));
        const bool relocated = location && location->mUnit.lock() == unit;
        const QString serializedName = relocated ? location->mTargetPath : unit->fileName();
//        if (!unit->FileMissing()) {
//            bool rd_only = false;
//            if (unit->modified() && fileExists(unit->fileName())
//                && isReadOnly(unit->fileName())) {
//                // file is read-only
//                QMessageBox::critical(nullptr,
//                                      tr("Can't save file"),
//                                      tr("Can't save file '%1'").arg(unit->fileName()),
//                                      QMessageBox::Ok
//                                      );
//                rd_only = true;
//            }
//            if (!rd_only) {
//                if (!unit->save() && unit->isNew())
//                    return false;
//            }
//        }

        // saved new file or an existing file add to project file
        ini.SetValue(
                    groupName,
                    "FileName",
                    toByteArray(
                        extractRelativePath(
                            directory(),
                            serializedName)));
        count++;
        // This is unit policy, independent of its current/next extension and
        // of the previous unit occupying this serialized section number.
        ini.SetLongValue(groupName,"CompileCpp", unit->compileCpp());
        ini.SetValue(groupName,"Folder", toByteArray(unit->folder()));
        ini.SetLongValue(groupName,"Compile", unit->compile());
        ini.SetLongValue(groupName,"Link", unit->link());
        ini.SetLongValue(groupName,"Priority", unit->priority());
        ini.SetLongValue(groupName,"OverrideBuildCmd", unit->overrideBuildCmd());
        ini.SetValue(groupName,"BuildCmd", toByteArray(unit->buildCmd()));
        //ini.SetLongValue(groupName,"DetectEncoding", unit->encoding()==ENCODING_AUTO_DETECT);
        ini.Delete(groupName,"DetectEncoding");
        ini.SetValue(groupName,"FileEncoding", unit->encoding());
        const auto accepted = relocated ? mEditorManager->editorForDocument(location->mAcceptedDocument) : nullptr;
        ini.SetValue(groupName,"RealEncoding", relocated ? (accepted ? accepted->fileEncoding() : QByteArray{}) : unit->realEncoding());
        if (!relocated && unit->relocation().state == ProjectUnit::RelocationState::Conflict) {
            ini.SetValue(groupName, "RmeleRelocationState", "conflict");
            ini.SetValue(groupName, "RmeleRelocationTarget",
                         toByteArray(extractRelativePath(directory(), unit->relocation().expectedFileName)));
            ini.SetValue(groupName, "RmeleRelocationDiagnostic", toByteArray(unit->relocation().diagnostic));
        } else {
            ini.Delete(groupName, "RmeleRelocationState");
            ini.Delete(groupName, "RmeleRelocationTarget");
            ini.Delete(groupName, "RmeleRelocationDiagnostic");
        }
    }
    ini.SetLongValue("Project","UnitCount",count);
}

PProjectUnit Project::findUnit(const QString &filename) const
{
    return resolveUnitPath(filename).unit;
}

ProjectPathResult Project::resolveUnitPath(const QString &filename) const
{
    if (filename.isEmpty()) return {};
    const NativePathKey key(QDir(directory()).absoluteFilePath(filename));
    if (!key.valid()) return {ProjectPathStatus::Conflict, {}, tr("Invalid project file path.")};
    const auto unit = mUnits.value(key);
    if (!unit) return {};
    const QString stored = exactPathSpelling(QFileInfo(unit->fileName()).absoluteFilePath());
    if (stored == key.spelling() || sameExistingFile(stored, key.spelling()) == ExistingFileMatch::Same)
        return {ProjectPathStatus::Found, unit, {}};
    return {ProjectPathStatus::Conflict, {},
        tr("The requested path conflicts with a project file and its identity could not be uniquely verified: '%1'.").arg(filename)};
}

PProjectUnit Project::findUnit(const Editor *editor) const
{
    if (!editor)
        return PProjectUnit();
    const DocumentId id = mEditorManager->documentId(editor);
    if (!id) return {};
    for (const auto &unit : mUnits)
        if (unit->documentId() == id && unit->relocation().state == ProjectUnit::RelocationState::Resolved)
            return unit;
    return {};
}

bool Project::inProject(const QString &filename) const
{
    return resolveUnitPath(filename).status == ProjectPathStatus::Found;
}

bool Project::inProject(const Editor *editor) const
{
    if (!editor)
        return false;
    return bool(findUnit(editor));
}

OperationResult Project::associateEditor(Editor *editor)
{
    PProjectUnit unit = findUnit(editor);
    if (!unit && editor) {
        const auto path = resolveUnitPath(editor->filename());
        if (path.status == ProjectPathStatus::Conflict) return OperationResult::conflict(path.error);
        unit = path.unit;
    }
    return associateEditorToUnit(editor,unit);
}

OperationResult Project::associateEditorToUnit(Editor *editor, PProjectUnit unit)
{
    if (QThread::currentThread() != thread())
        return OperationResult::conflict(tr("Project file operations must run on the editor's owner thread."));
    if (unit && unit->relocation().state != ProjectUnit::RelocationState::Resolved)
        return OperationResult::conflict(unit->relocation().diagnostic);
    if (!unit) {
        if (editor)
            editor->setInProject(false);
        return OperationResult::success();
    }
    if (mUnits.value(unitPathKey(unit->fileName())) != unit)
        return OperationResult::conflict(tr("The project unit is no longer available."));
    if (editor) {
        const DocumentId id = mEditorManager->documentId(editor);
        if (!id || !verifiedPathAlias(mEditorManager->documentAbsolutePath(editor), unit->fileName()))
            return OperationResult::conflict(tr("The selected document does not match the project file."));
        const auto otherUnit = findUnit(editor);
        if (otherUnit && otherUnit != unit)
            return OperationResult::conflict(tr("The selected document already belongs to another project file."));
        if (unit->documentId() && unit->documentId() != id && mEditorManager->editorForDocument(unit->documentId()))
            return OperationResult::conflict(tr("The project file already has another open document."));
        const auto parser = editor->syntaxer() && editor->syntaxer()->language() == QSynedit::ProgrammingLanguage::CPP
                ? mParser : PCppParser{};
        const bool parserChanged = editor->parser() != parser;
        unit->mDocumentId = id;
        editor->setInProject(true, false);
        // Registration happens after EditorManager initializes the candidate.
        // Resolve the parser from this canonical owner, not from the former
        // path/inProject flag or the MainWindow's currently active project.
        editor->setCppParser(parser);
        // Binding observes the file encoding; it does not change the unit's
        // configured encoding or its PROJECT inheritance policy.
        unit->setRealEncoding(editor->fileEncoding());
        if (parserChanged && parser) editor->reparse();
    }
    return OperationResult::success();
}

//bool Project::setCompileOption(const QString &key, int valIndex)
//{
//    PCompilerSet pSet = pSettings->compilerSets().getSet(mOptions.compilerSet);
//    if (!pSet)
//        return false;
//    PCompilerOption op = CompilerInfoManager::getCompilerOption(
//                pSet->compilerType(), key);
//    if (!op)
//        return false;
//    if (op->choices.isEmpty()) {
//        if (valIndex>0)
//            mOptions.compilerOptions.insert(key,COMPILER_OPTION_ON);
//        else
//            mOptions.compilerOptions.insert(key,"");
//    } else {
//        if (valIndex>0 && valIndex <= op->choices.length()) {
//            mOptions.compilerOptions.insert(key,op->choices[valIndex-1].second);
//        } else {
//            mOptions.compilerOptions.insert(key,"");
//        }
//    }
//    return true;
//}

bool Project::setCompileOption(const QString &key, const QString &value)
{
    PCompilerSet pSet = pSettings->compilerSets().getSet(mOptions.compilerSet);
    if (!pSet)
        return false;
    PCompilerOption op = CompilerInfoManager::getCompilerOption(
                pSet->compilerType(), key);
    if (!op)
        return false;
    mOptions.compilerOptions.insert(key,value);
    return true;
}

QString Project::getCompileOption(const QString &key) const
{
    return mOptions.compilerOptions.value(key,"");
}

void Project::updateFolders()
{
    mFolders.clear();
    updateFolderNode(mRootNode);
    foreach (PProjectUnit unit, mUnits) {
        unit->setFolder(
                    getNodePath(
                        unit->node()->parent.lock()
                        )
                    );
    }
    setModified(true);
}

PProjectModelNode Project::pointerToNode(ProjectModelNode *p, PProjectModelNode parent)
{
    if (!p)
        return PProjectModelNode();
    if (!parent) {
        parent = mRootNode;
    }
    if (p==mRootNode.get())
        return mRootNode;
    foreach (const PProjectModelNode& node , parent->children) {
        if (node.get()==p)
            return node;
        PProjectModelNode result = pointerToNode(p,node);
        if (result)
            return result;
    }
    return PProjectModelNode();
}

void Project::setCompilerSet(int compilerSetIndex)
{
    if (mOptions.compilerSet != compilerSetIndex) {
        mOptions.compilerSet = compilerSetIndex;
        updateCompilerSetting();
        setModified(true);
    }
}

bool Project::assignTemplate(const std::shared_ptr<ProjectTemplate> aTemplate, bool useCpp)
{
    if (!aTemplate) {
        return false;
    }

    mModel->beginUpdate();
    mRootNode = makeProjectNode();
    rebuildNodes();
    mOptions = aTemplate->options();
    mOptions.compilerSet = pSettings->compilerSets().defaultIndex();
    mOptions.isCpp = useCpp;
    updateCompilerSetting();
    mOptions.icon = aTemplate->icon();

    if (!isEncodingAvailable(mOptions.encoding))
        mOptions.encoding=ENCODING_SYSTEM_DEFAULT;
    if (!isEncodingAvailable(mOptions.execEncoding))
        mOptions.execEncoding=ENCODING_SYSTEM_DEFAULT;

    // Copy icon to project directory
    if (!mOptions.icon.isEmpty()) {
        QString originIcon = cleanPath(QFileInfo(aTemplate->fileName()).absoluteDir().absoluteFilePath(mOptions.icon));
        if (fileExists(originIcon)) {
            QString destIcon = cleanPath(QFileInfo(mFilename).absoluteDir().absoluteFilePath("app.ico"));
            QFile::copy(originIcon,destIcon);
            mOptions.icon = destIcon;
        } else {
            mOptions.icon = "";
        }
    }
    // Add list of files
    if (aTemplate->version() > 0) {
        QDir dir(aTemplate->folder());
        Editor *lastNewEditor=nullptr;
        for (int i=0;i<aTemplate->unitCount();i++) {
            // Pick file contents
            PTemplateUnit templateUnit = aTemplate->unit(i);
            if (!templateUnit)
                continue;
            if (!templateUnit->Source.isEmpty()) {
                QString target = templateUnit->Source;
                PProjectUnit unit;
                if (!templateUnit->Target.isEmpty())
                    target = templateUnit->Target;
                unit = newUnit(mRootNode, target);
                if (templateUnit->overwrite || !fileExists(unit->fileName()) ) {
                        QFile::copy(
                                    cleanPath(dir.absoluteFilePath(templateUnit->Source)),
                                    includeTrailingPathDelimiter(this->directory())+target);
                }

                FileType fileType=getFileType(unit->fileName());
                if ( isC_CPP_ASMSourceFile(fileType)
                        || isC_CPPHeaderFile(fileType)) {
                    Editor * editor = mEditorManager->newEditor(
                                unit->fileName(),
                                unit->encoding()==ENCODING_PROJECT?options().encoding:unit->encoding(),
                                FileType::None,
                                QString(),
                                true,
                                false);
                    const auto associated = associateEditorToUnit(editor, unit);
                    if (!associated.succeeded()) throw FileError(associated.error);
                    lastNewEditor = editor;
                }
            } else {
                QString s;
                PProjectUnit unit;
                if (mOptions.isCpp) {
                    s = templateUnit->CppText;
                    unit = newUnit(mRootNode, templateUnit->CppName);
                } else {
                    s = templateUnit->CText;
                    unit = newUnit(mRootNode,templateUnit->CName);
                }
                Editor * editor = mEditorManager->newEditor(
                            unit->fileName(),
                            unit->encoding()==ENCODING_PROJECT?options().encoding:unit->encoding(),
                            FileType::None,
                            QString(),
                            true,
                            true);

                const auto associated = associateEditorToUnit(editor, unit);
                if (!associated.succeeded()) throw FileError(associated.error);

                if (templateUnit->overwrite || !fileExists(unit->fileName()) ) {
                    QString s2 = cleanPath(dir.absoluteFilePath(s));
                    if (fileExists(s2) && !s.isEmpty()) {
                        try {
                            editor->loadFile(s2);
                        } catch(FileError& e) {
                            QMessageBox::critical(nullptr,
                                                  tr("Error Load File"),
                                                  e.reason());
                        }
                    } else {
                        s.replace("#13#10","\r\n");
                        editor->insertString(s,false);
                    }
                    editor->save(true,false);
                }
                lastNewEditor  = editor;
            }
        }
        mEditorManager->activeEditor(lastNewEditor,true);
    }
    mModel->endUpdate();
    return true;
}

bool Project::saveAsTemplate(const QString &templateFolder,
                             const QString& name,
                             const QString& description,
                             const QString& category)
{
    QDir dir(templateFolder);
    if (!dir.mkpath(templateFolder)) {
        QMessageBox::critical(nullptr,
                              tr("Error"),
                              tr("Can't create folder %1 ").arg(templateFolder),
                              QMessageBox::Ok);
        return false;
    }

    QString fileName = cleanPath(dir.absoluteFilePath(TEMPLATE_INFO_FILE));
    PSimpleIni ini = std::make_shared<SimpleIni>();

    ini->SetLongValue("Template","Ver",3);
    // template info
    ini->SetValue("Template", "Name", name.toUtf8());
    ini->SetValue("Template", "Category", category.toUtf8());
    ini->SetValue("Template", "Description", description.toUtf8());
    if (fileExists(mOptions.icon)) {
        QString iconName = extractFileName(mOptions.icon);
        const auto iconResult = ProjectFileOperations::copyFile(mOptions.icon, cleanPath(dir.absoluteFilePath(iconName)));
        if (!iconResult.succeeded()) {
            QMessageBox::warning(nullptr, tr("Warning"), iconResult.error);
            return false;
        }
        if (dir.exists(iconName))
            ini->SetValue("Template", "Icon", iconName.toUtf8());
    }

    ini->SetLongValue("Project", "Type", static_cast<int>(mOptions.type));
    if (!mOptions.includeDirs.isEmpty())
        ini->SetValue("Project", "Includes", relativePaths(mOptions.includeDirs).join(";").toUtf8());
    if (!mOptions.resourceIncludes.isEmpty())
        ini->SetValue("Project", "ResourceIncludes", relativePaths(mOptions.resourceIncludes).join(";").toUtf8());
    if (!mOptions.makeIncludes.isEmpty())
        ini->SetValue("Project", "MakeIncludes", relativePaths(mOptions.makeIncludes).join(";").toUtf8());
    if (!mOptions.binDirs.isEmpty())
        ini->SetValue("Project", "Bins", relativePaths(mOptions.binDirs).join(";").toUtf8());
    if (!mOptions.libDirs.isEmpty())
        ini->SetValue("Project", "Libs", relativePaths(mOptions.libDirs).join(";").toUtf8());
    if (!mOptions.compilerCmd.isEmpty())
        ini->SetValue("Project", "Compiler", textToLines(mOptions.compilerCmd).join(" ").toUtf8());
    if (!mOptions.cppCompilerCmd.isEmpty())
        ini->SetValue("Project", "CppCompiler", textToLines(mOptions.cppCompilerCmd).join(" ").toUtf8());
    if (!mOptions.linkerCmd.isEmpty())
        ini->SetValue("Project", "Linker",textToLines(mOptions.linkerCmd).join(" ").toUtf8());
    if (!mOptions.resourceCmd.isEmpty())
        ini->SetValue("Project", "ResourceCommand",textToLines(mOptions.resourceCmd).join(" ").toUtf8());
    ini->SetBoolValue("Project", "IsCpp", mOptions.isCpp);
    if (mOptions.includeVersionInfo)
        ini->SetBoolValue("Project", "IncludeVersionInfo", true);
    if (mOptions.supportXPThemes)
        ini->SetBoolValue("Project", "SupportXPThemes", true);
    if (!mOptions.folderForOutput.isEmpty())
        ini->SetValue("Project", "ExeOutput", extractRelativePath(directory(),mOptions.folderForOutput).toUtf8());
    if (!mOptions.folderForObjFiles.isEmpty())
        ini->SetValue("Project", "ObjectOutput", extractRelativePath(directory(),mOptions.folderForObjFiles).toUtf8());
    if (!mOptions.logFilename.isEmpty())
        ini->SetValue("Project", "LogOutput", extractRelativePath(directory(),mOptions.logFilename).toUtf8());
    if (mOptions.execEncoding!=ENCODING_SYSTEM_DEFAULT)
        ini->SetValue("Project","ExecEncoding", mOptions.execEncoding);

//    if (!mOptions.staticLink)
//        ini->SetBoolValue("Project", "StaticLink",false);
    if (!mOptions.addCharset)
        ini->SetBoolValue("Project", "AddCharset",false);
    if (mOptions.encoding!=ENCODING_AUTO_DETECT)
        ini->SetValue("Project","Encoding",mOptions.encoding);
    if (mOptions.modelType!=ProjectModelType::FileSystem)
        ini->SetLongValue("Project", "ModelType", (int)mOptions.modelType);
    ini->SetLongValue("Project","ClassBrowserType", (int)mOptions.classBrowserType);

    int i=0;
    foreach (const PProjectUnit &unit, mUnits) {
        QString unitName = extractFileName(unit->fileName());
        QByteArray section = toByteArray(QString("Unit%1").arg(i));
        const auto copyResult = ProjectFileOperations::copyFile(unit->fileName(), cleanPath(dir.absoluteFilePath(unitName)));
        if (!copyResult.succeeded()) {
            QMessageBox::warning(nullptr,
                                  tr("Warning"),
                                  tr("Can't save file %1").arg(cleanPath(dir.absoluteFilePath(unitName))),
                                  QMessageBox::Ok);
            return false;
        }
        switch(getFileType(unit->fileName())) {
        case FileType::CSource:
            ini->SetValue(section,"C", unitName.toUtf8());
            ini->SetValue(section,"CName", unitName.toUtf8());
            break;
        case FileType::CppSource:
            ini->SetValue(section,"Cpp", unitName.toUtf8());
            ini->SetValue(section,"CppName", unitName.toUtf8());
            break;
        case FileType::CCppHeader:
            ini->SetValue(section,"C", unitName.toUtf8());
            ini->SetValue(section,"CName", unitName.toUtf8());
            ini->SetValue(section,"Cpp", unitName.toUtf8());
            ini->SetValue(section,"CppName", unitName.toUtf8());
            break;
        default:
            ini->SetValue(section,"Source", unitName.toUtf8());
            ini->SetValue(section,"Target", unitName.toUtf8());
        }
        i++;
    }
    ini->SetLongValue("Project","UnitCount",mUnits.count());
    const auto templateResult = ProjectFileOperations::saveIni(fileName, *ini);
    if (!templateResult.succeeded()) {
        QMessageBox::critical(nullptr,
                              tr("Error"),
                              tr("Can't save file %1").arg(fileName),
                              QMessageBox::Ok);
        return false;
    }
    return true;
}

void Project::setEncoding(const QByteArray &encoding)
{
    if (encoding!=mOptions.encoding) {
        mOptions.encoding=encoding;
        foreach (const PProjectUnit& unit,mUnits) {
            if (unit->encoding()!=ENCODING_PROJECT)
                continue;
            Editor * e=unitEditor(unit);
            if (e) {
                e->setEditorEncoding(mOptions.encoding);
                unit->setEncoding(ENCODING_PROJECT);
            }
        }
    }
}

void Project::saveOptions()
{
    SimpleIni candidate;
    auto result = ProjectFileOperations::loadIni(mFilename, candidate);
    if (result.succeeded()) {
        populateOptions(candidate);
        result = ProjectFileOperations::saveIni(mFilename, candidate);
    }
    if (!result.succeeded()) {
        setModified(true);
        throw FileError(result.error);
    }
}

void Project::populateOptions(SimpleIni &ini)
{
    ini.SetValue("Project","FileName", toByteArray(extractRelativePath(directory(), mFilename)));
    ini.SetValue("Project","Name", toByteArray(mName));
    ini.SetLongValue("Project","Type", static_cast<int>(mOptions.type));
    ini.SetLongValue("Project","Ver", 3); // Is 3 as of Red Panda C++.0
    ini.SetValue("Project","Includes", toByteArray(relativePaths(mOptions.includeDirs).join(";")));
    ini.SetValue("Project","Libs", toByteArray(relativePaths(mOptions.libDirs).join(";")));
    ini.SetValue("Project","Bins", toByteArray(relativePaths(mOptions.binDirs).join(";")));
    ini.SetValue("Project","ResourceIncludes", toByteArray(relativePaths(mOptions.resourceIncludes).join(";")));
    ini.SetValue("Project","MakeIncludes", toByteArray(relativePaths(mOptions.makeIncludes).join(";")));
    ini.SetValue("Project","PrivateResource", toByteArray(mOptions.privateResource));
    ini.SetValue("Project","Compiler", toByteArray(textToLines(mOptions.compilerCmd).join(" ")));
    ini.SetValue("Project","CppCompiler", toByteArray(textToLines(mOptions.cppCompilerCmd).join(" ")));
    ini.SetValue("Project","Linker", toByteArray(textToLines(mOptions.linkerCmd).join(" ")));
    ini.SetValue("Project", "ResourceCommand", toByteArray(textToLines(mOptions.resourceCmd).join(" ")));
    ini.SetLongValue("Project","IsCpp", mOptions.isCpp);
    ini.SetValue("Project","Icon", toByteArray(extractRelativePath(directory(), mOptions.icon)));
    ini.SetValue("Project","ExeOutput", toByteArray(extractRelativePath(directory(),mOptions.folderForOutput)));
    ini.SetValue("Project","ObjectOutput", toByteArray(extractRelativePath(directory(),mOptions.folderForObjFiles)));
    ini.SetValue("Project","LogOutput", toByteArray(extractRelativePath(directory(),mOptions.logFilename)));
    ini.SetLongValue("Project","LogOutputEnabled", mOptions.logOutput);
    ini.SetLongValue("Project","OverrideOutput", mOptions.useCustomOutputFilename);
    ini.SetValue("Project","OverrideOutputName", toByteArray(mOptions.customOutputFilename));
    ini.SetValue("Project","HostApplication", toByteArray(extractRelativePath(directory(), mOptions.hostApplication)));
    ini.SetLongValue("Project","UseCustomMakefile", mOptions.useCustomMakefile);
    ini.SetValue("Project","CustomMakefile", toByteArray(extractRelativePath(directory(),mOptions.customMakefile)));
    ini.SetLongValue("Project","UsePrecompiledHeader", mOptions.usePrecompiledHeader);
    ini.SetValue("Project","PrecompiledHeader", toByteArray(extractRelativePath(directory(), mOptions.precompiledHeader)));
    ini.SetValue("Project","CommandLine", toByteArray(mOptions.cmdLineArgs));
    ini.SetValue("Project","Folders", toByteArray(mFolders.join(";")));
    ini.SetLongValue("Project","IncludeVersionInfo", mOptions.includeVersionInfo);
    ini.SetLongValue("Project","SupportXPThemes", mOptions.supportXPThemes);
    ini.SetLongValue("Project","CompilerSet", mOptions.compilerSet);
    ini.Delete("Project","CompilerSettings"); // remove old compiler settings
    ini.Delete("CompilerSettings",nullptr); // remove old compiler settings
    foreach (const QString& key, mOptions.compilerOptions.keys()) {
        ini.SetValue("CompilerSettings",toByteArray(key),toByteArray(mOptions.compilerOptions.value(key)));
    }
    ini.SetLongValue("Project","StaticLink", mOptions.staticLink);
    ini.SetLongValue("Project","AddCharset", mOptions.addCharset);
    ini.SetValue("Project","ExecEncoding", mOptions.execEncoding);
    ini.SetValue("Project","Encoding",mOptions.encoding);
    ini.SetLongValue("Project","ModelType", (int)mOptions.modelType);
    ini.SetLongValue("Project","ClassBrowserType", (int)mOptions.classBrowserType);
    ini.SetBoolValue("Project","AllowParallelBuilding",mOptions.allowParallelBuilding);
    ini.SetLongValue("Project","ParellelBuildingJobs",mOptions.parellelBuildingJobs);


    //for Red Panda Dev C++ 6 compatibility
    ini.SetLongValue("Project","UseUTF8",mOptions.encoding == ENCODING_UTF8);

    ini.SetLongValue("VersionInfo","Major", mOptions.versionInfo.major);
    ini.SetLongValue("VersionInfo","Minor", mOptions.versionInfo.minor);
    ini.SetLongValue("VersionInfo","Release", mOptions.versionInfo.release);
    ini.SetLongValue("VersionInfo","Build", mOptions.versionInfo.build);
    ini.SetLongValue("VersionInfo","LanguageID", mOptions.versionInfo.languageID);
    ini.SetLongValue("VersionInfo","CharsetID", mOptions.versionInfo.charsetID);
    ini.SetValue("VersionInfo","CompanyName", toByteArray(mOptions.versionInfo.companyName));
    ini.SetValue("VersionInfo","FileVersion", toByteArray(mOptions.versionInfo.fileVersion));
    ini.SetValue("VersionInfo","FileDescription", toByteArray(mOptions.versionInfo.fileDescription));
    ini.SetValue("VersionInfo","InternalName", toByteArray(mOptions.versionInfo.internalName));
    ini.SetValue("VersionInfo","LegalCopyright", toByteArray(mOptions.versionInfo.legalCopyright));
    ini.SetValue("VersionInfo","LegalTrademarks", toByteArray(mOptions.versionInfo.legalTrademarks));
    ini.SetValue("VersionInfo","OriginalFilename", toByteArray(mOptions.versionInfo.originalFilename));
    ini.SetValue("VersionInfo","ProductName", toByteArray(mOptions.versionInfo.productName));
    ini.SetValue("VersionInfo","ProductVersion", toByteArray(mOptions.versionInfo.productVersion));
    ini.SetLongValue("VersionInfo","AutoIncBuildNr", mOptions.versionInfo.autoIncBuildNr);
    ini.SetLongValue("VersionInfo","SyncProduct", mOptions.versionInfo.syncProduct);


    //delete outdated dev4 project options
    ini.Delete("Project","NoConsole");
    ini.Delete("Project","IsDLL");
    ini.Delete("Project","ResFiles");
    ini.Delete("Project","IncludeDirs");
    ini.Delete("Project","CompilerOptions");
    ini.Delete("Project","Use_GPP");

}

PProjectModelNode Project::addFolder(PProjectModelNode parentFolder,const QString &s)
{
    QString fullPath;
    QString path = getNodePath(parentFolder);
    if (path.isEmpty()) {
        fullPath = s;
    } else {
        fullPath = path + '/' +s;
    }
    if (mFolders.indexOf(fullPath)<0) {
        mModel->beginUpdate();
        auto action = finally([this]{
            mModel->endUpdate();
        });
        mFolders.append(fullPath);
        PProjectModelNode node = makeNewFolderNode(s,parentFolder);
        setModified(true);
        return node;
    }
    return PProjectModelNode();
}

PProjectUnit Project::addUnit(const QString &inFileName, PProjectModelNode parentNode)
{
    PProjectUnit newUnit=internalAddUnit(inFileName, parentNode);
    if (newUnit) {
        emit unitAdded(newUnit->fileName());
    }
    return newUnit;
}

PProjectUnit Project::internalAddUnit(const QString &inFileName, PProjectModelNode parentNode)
{
    const QString absolute = QDir(directory()).absoluteFilePath(inFileName);
    const auto existingUnit = resolveUnitPath(absolute);
    const auto existingDocument = mEditorManager->resolveOpenedEditor(absolute);
    if (existingUnit.status == ProjectPathStatus::Conflict || existingDocument.status == EditorPathStatus::Conflict) {
        QMessageBox::critical(nullptr, tr("File Exists"), existingUnit.status == ProjectPathStatus::Conflict
                              ? existingUnit.error : existingDocument.error, QMessageBox::Ok);
        return {};
    }
    // Don't add if it already exists
    if (existingUnit.unit) {
        QMessageBox::critical(nullptr,
                                 tr("File Exists"),
                                 tr("File '%1' is already in the project").arg(inFileName),
                              QMessageBox::Ok);
        return PProjectUnit();
    }
    if (mOptions.modelType == ProjectModelType::FileSystem) {
        // in file system mode, parentNode is determined by file's path
        parentNode = getParentFileSystemFolderNode(inFileName);
    } else {
        while (parentNode && (parentNode != rootNode()) && parentNode->isUnit) {
            parentNode = parentNode->parent.lock();
        }
    }
    PProjectUnit newUnit = std::make_shared<ProjectUnit>(this);

    // Set all properties
    newUnit->setFileName(absolute);
    Editor * e = existingDocument.editor;
    newUnit->setEncoding(e ? e->editorEncoding() : ENCODING_PROJECT);

  // Determine compilation flags
    switch(getFileType(inFileName)) {
    case FileType::GAS:
        newUnit->setCompile(true);
        newUnit->setCompileCpp(false);
        newUnit->setLink(true);
        break;
    case FileType::NASM:
        newUnit->setCompile(true);
        newUnit->setCompileCpp(false);
        newUnit->setLink(true);
        break;
    case FileType::CSource:
        newUnit->setCompile(true);
        newUnit->setCompileCpp(false);
        newUnit->setLink(true);
        break;
    case FileType::CppSource:
        newUnit->setCompile(true);
        newUnit->setCompileCpp(true);
        newUnit->setLink(true);
        break;
    case FileType::WindowsResourceSource:
        newUnit->setCompile(true);
        newUnit->setCompileCpp(mOptions.isCpp);
        newUnit->setLink(false);
        break;
    default:
        newUnit->setCompile(false);
        newUnit->setCompileCpp(false);
        newUnit->setLink(false);
    }
    if (mOptions.modelType == ProjectModelType::FileSystem)
        newUnit->setFolder(getNodePath(parentNode));
    newUnit->setPriority(1000);
    newUnit->setOverrideBuildCmd(false);
    newUnit->setBuildCmd("");

    PProjectModelNode node = makeNewFileNode(newUnit,
                                             newUnit->priority(), parentNode);
    newUnit->setNode(node);
    mUnits.insert(unitPathKey(newUnit->fileName()),newUnit);
    if (e) {
        const auto associated = associateEditorToUnit(e, newUnit);
        if (!associated.succeeded()) throw FileError(associated.error);
    }

    setModified(true);
    return newUnit;
}

QString Project::folder()
{
    return extractFileDir(filename());
}

void Project::buildPrivateResource()
{
    if (mOptions.type == ProjectType::MicroController)
        return;
    int comp = 0;
    foreach (const PProjectUnit& unit,mUnits) {
        if (
                (getFileType(unit->fileName()) == FileType::WindowsResourceSource)
                && unit->compile() )
            comp++;
    }

    // if project has no other resources included
    // and does not have an icon
    // and does not include the XP style manifest
    // and does not include version info
    // then do not create a private resource file
    if ((comp == 0) &&
            (! mOptions.supportXPThemes)
            && (! mOptions.includeVersionInfo)
            && (mOptions.icon == "")) {
        mOptions.privateResource="";
        return;
    }

    // change private resource from <project_filename>.res
    // to <project_filename>_private.res
    //
    // in many cases (like in importing a MSVC project)
    // the project's resource file has already the
    // <project_filename>.res filename.
    QString rcFile;
    if (!mOptions.privateResource.isEmpty()) {
        rcFile = QDir(directory()).filePath(mOptions.privateResource);
        if (changeFileExt(rcFile, DEV_PROJECT_EXT) == mFilename) {
            QFileInfo fileInfo(mFilename);
            rcFile = includeTrailingPathDelimiter(fileInfo.absolutePath())
                    + fileInfo.baseName()
                    + "_private."
                    + RC_EXT;
        }
    } else {
        QFileInfo fileInfo(mFilename);
        rcFile = includeTrailingPathDelimiter(fileInfo.absolutePath())
                + fileInfo.baseName()
                + "_private."
                + RC_EXT;
    }
    rcFile = extractRelativePath(mFilename, rcFile);
    rcFile.replace(' ','_');

    QStringList contents;
    contents.append("/* THIS FILE WILL BE OVERWRITTEN BY Red Panda C++ */");
    contents.append("/* DO NOT EDIT! */");
    contents.append("");

    if (mOptions.includeVersionInfo) {
      contents.append("#include <windows.h> // include for version info constants");
      contents.append("");
    }

    foreach (const PProjectUnit& unit, mUnits) {
        if (
                (getFileType(unit->fileName()) == FileType::WindowsResourceSource)
                && unit->compile() )
            contents.append("#include \"" + extractRelativePath(directory(), unit->fileName()) + "\"");
    }

    if (!mOptions.icon.isEmpty()) {
        contents.append("");
        QString icon = mOptions.icon;
        if (fileExists(icon)) {
            icon = extractRelativePath(mFilename, icon);
            icon.replace('\\', '/');
            contents.append("A ICON \"" + icon + '"');
        } else
            mOptions.icon = "";
    }

    if (mOptions.supportXPThemes) {
      contents.append("");
      contents.append("//");
      contents.append("// SUPPORT FOR WINDOWS XP THEMES:");
      contents.append("// THIS WILL MAKE THE PROGRAM USE THE COMMON CONTROLS");
      contents.append("// LIBRARY VERSION 6.0 (IF IT IS AVAILABLE)");
      contents.append("//");
      if (!mOptions.folderForOutput.isEmpty())
          contents.append(
                    "1 24 \"" + includeTrailingPathDelimiter(mOptions.folderForOutput)
                      + extractFileName(outputFilename()) + ".Manifest\"");
      else
          contents.append("1 24 \"" + extractFileName(outputFilename()) + ".Manifest\"");
    }

    if (mOptions.includeVersionInfo) {
        contents.append("");
        contents.append("//");
        contents.append("// TO CHANGE VERSION INFORMATION, EDIT PROJECT OPTIONS...");
        contents.append("//");
        contents.append("1 VERSIONINFO");
        contents.append("FILEVERSION " +
                       QString("%1,%2,%3,%4")
                       .arg(mOptions.versionInfo.major)
                       .arg(mOptions.versionInfo.minor)
                       .arg(mOptions.versionInfo.release)
                       .arg(mOptions.versionInfo.build));
        contents.append("PRODUCTVERSION " +
                       QString("%1,%2,%3,%4")
                       .arg(mOptions.versionInfo.major)
                       .arg(mOptions.versionInfo.minor)
                       .arg(mOptions.versionInfo.release)
                       .arg(mOptions.versionInfo.build));
        switch(mOptions.type) {
        case ProjectType::GUI:
        case ProjectType::Console:
            contents.append("FILETYPE VFT_APP");
            break;
        case ProjectType::StaticLib:
            contents.append("FILETYPE VFT_STATIC_LIB");
            break;
        case ProjectType::DynamicLib:
            contents.append("FILETYPE VFT_DLL");
            break;
        default:
            break;
        }
        contents.append("{");
        contents.append("  BLOCK \"StringFileInfo\"");
        contents.append("  {");
        contents.append("    BLOCK \"" +
                       QString("%1%2")
                       .arg(mOptions.versionInfo.languageID,4,16,QChar('0'))
                       .arg(mOptions.versionInfo.charsetID,4,16,QChar('0'))
                       + '"');
        contents.append("    {");
        contents.append("      VALUE \"CompanyName\", \""
                       + mOptions.versionInfo.companyName
                       + "\"");
        contents.append("      VALUE \"FileVersion\", \""
                       + mOptions.versionInfo.fileVersion
                       + "\"");
        contents.append("      VALUE \"FileDescription\", \""
                       + mOptions.versionInfo.fileDescription
                       + "\"");
        contents.append("      VALUE \"InternalName\", \""
                       + mOptions.versionInfo.internalName
                       + "\"");
        contents.append("      VALUE \"LegalCopyright\", \""
                       + mOptions.versionInfo.legalCopyright
                       + '"');
        contents.append("      VALUE \"LegalTrademarks\", \""
                       + mOptions.versionInfo.legalTrademarks
                       + "\"");
        contents.append("      VALUE \"OriginalFilename\", \""
                       + mOptions.versionInfo.originalFilename
                       + "\"");
        contents.append("      VALUE \"ProductName\", \""
                       + mOptions.versionInfo.productName + "\"");
        contents.append("      VALUE \"ProductVersion\", \""
                       + mOptions.versionInfo.productVersion + "\"");
        contents.append("    }");
        contents.append("  }");

        // additional block for windows 95->NT
        contents.append("  BLOCK \"VarFileInfo\"");
        contents.append("  {");
        contents.append("    VALUE \"Translation\", " +
                       QString("0x%1, %2")
                       .arg(mOptions.versionInfo.languageID,4,16,QChar('0'))
                       .arg(mOptions.versionInfo.charsetID));
        contents.append("  }");

        contents.append("}");
    }

    rcFile = generateAbsolutePath(directory(),rcFile);
    if (contents.count() > 3) {
        stringsToFile(contents,rcFile);
        mOptions.privateResource = extractRelativePath(directory(), rcFile);
    } else {
      if (fileExists(rcFile))
          QFile::remove(rcFile);
      QString resFile = changeFileExt(rcFile, RES_EXT);
      if (fileExists(resFile))
          QFile::remove(resFile);
      mOptions.privateResource = "";
    }
    // create XP manifest
    if (mOptions.supportXPThemes) {
        QStringList content;
        content.append("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>");
        content.append("<assembly");
        content.append("  xmlns=\"urn:schemas-microsoft-com:asm.v1\"");
        content.append("  manifestVersion=\"1.0\">");
        content.append("<assemblyIdentity");
        QString name = mName;
        name.replace(' ','_');
        content.append("    name=\"DevCpp.Apps." + name + '\"');
        content.append("    processorArchitecture=\"*\"");
        content.append("    version=\"1.0.0.0\"");
        content.append("    type=\"win32\"/>");
        content.append("<description>" + name + "</description>");
        content.append("<dependency>");
        content.append("    <dependentAssembly>");
        content.append("        <assemblyIdentity");
        content.append("            type=\"win32\"");
        content.append("            name=\"Microsoft.Windows.Common-Controls\"");
        content.append("            version=\"6.0.0.0\"");
        content.append("            processorArchitecture=\"*\"");
        content.append("            publicKeyToken=\"6595b64144ccf1df\"");
        content.append("            language=\"*\"");
        content.append("        />");
        content.append("    </dependentAssembly>");
        content.append("</dependency>");
        content.append("</assembly>");
        stringsToFile(content,outputFilename() + ".Manifest");
    }

    // create private header file
    QString hFile = changeFileExt(rcFile, H_EXT);
    contents.clear();
    QString def = extractFileName(hFile);
    def.replace(".","_");
    def = def.toUpper();
    if (def.front().isDigit())
        def = "PROJECT_"+def;
    contents.append("/* THIS FILE WILL BE OVERWRITTEN BY Red Panda C++ */");
    contents.append("/* DO NOT EDIT ! */");
    contents.append("");
    contents.append("#ifndef " + def);
    contents.append("#define " + def);
    contents.append("");
    contents.append("/* VERSION DEFINITIONS */");
    contents.append("#define VER_STRING\t" +
                   QString("\"%1.%2.%3.%4\"")
                   .arg(mOptions.versionInfo.major)
                   .arg(mOptions.versionInfo.minor)
                   .arg(mOptions.versionInfo.release)
                   .arg(mOptions.versionInfo.build));
    contents.append(QString("#define VER_MAJOR\t%1").arg(mOptions.versionInfo.major));
    contents.append(QString("#define VER_MINOR\t%1").arg(mOptions.versionInfo.minor));
    contents.append(QString("#define VER_RELEASE\t%1").arg(mOptions.versionInfo.release));
    contents.append(QString("#define VER_BUILD\t%1").arg(mOptions.versionInfo.build));
    contents.append(QString("#define COMPANY_NAME\t\"%1\"")
                   .arg(mOptions.versionInfo.companyName));
    contents.append(QString("#define FILE_VERSION\t\"%1\"")
                   .arg(mOptions.versionInfo.fileVersion));
    contents.append(QString("#define FILE_DESCRIPTION\t\"%1\"")
                   .arg(mOptions.versionInfo.fileDescription));
    contents.append(QString("#define INTERNAL_NAME\t\"%1\"")
                   .arg(mOptions.versionInfo.internalName));
    contents.append(QString("#define LEGAL_COPYRIGHT\t\"%1\"")
                   .arg(mOptions.versionInfo.legalCopyright));
    contents.append(QString("#define LEGAL_TRADEMARKS\t\"%1\"")
                   .arg(mOptions.versionInfo.legalTrademarks));
    contents.append(QString("#define ORIGINAL_FILENAME\t\"%1\"")
                   .arg(mOptions.versionInfo.originalFilename));
    contents.append(QString("#define PRODUCT_NAME\t\"%1\"")
                   .arg(mOptions.versionInfo.productName));
    contents.append(QString("#define PRODUCT_VERSION\t\"%1\"")
                   .arg(mOptions.versionInfo.productVersion));
    contents.append("");
    contents.append("#endif /*" + def + "*/");
    stringsToFile(contents,hFile);
}

bool Project::checkProjectFileForUpdate(SimpleIni &ini)
{
    bool cnvt = false;
    int uCount = ini.GetLongValue("Project","UnitCount", 0);
    if (uCount < 0 || uCount > 100000)
        throw FileError(tr("Invalid or excessive project unit count."));
    // check if using old way to store resources and fix it
    QString oldRes = QString::fromLocal8Bit(ini.GetValue("Project","Resources", ""));
    if (!oldRes.isEmpty()) {
        if (oldRes.count(';') + 1 > 100000 - uCount)
            throw FileError(tr("Invalid or excessive project unit count."));
        QStringList sl;
        sl = oldRes.split(';', Qt::SkipEmptyParts);
        for (int i=0;i<sl.count();i++){
            const QString& s = sl[i];
            QByteArray groupName = toByteArray(QString("Unit%1").arg(uCount+i+1));
            ini.SetValue(groupName,"Filename", toByteArray(s));
            ini.SetValue(groupName,"Folder", "Resources");
            ini.SetLongValue(groupName,"Compile",true);
        }
        ini.SetLongValue("Project","UnitCount",uCount+sl.count());
        QString folders = QString::fromLocal8Bit(ini.GetValue("Project","Folders",""));
        if (!folders.isEmpty())
            folders += ",Resources";
        else
            folders = "Resources";
        ini.SetValue("Project","Folders",toByteArray(folders));
        cnvt = true;
        ini.Delete("Project","Resources");
        ini.Delete("Project","Focused");
        ini.Delete("Project","Order");
        ini.Delete("Project","DebugInfo");
        ini.Delete("Project","ProfileInfo");
    }

    return cnvt;
}

void Project::closeUnit(PProjectUnit& unit)
{
    saveLayout();
    Editor * editor = unitEditor(unit);
    if (editor) {
        mEditorManager->forceCloseEditor(editor);
    }
}

void Project::createFolderNodes()
{
    for (int idx=0;idx<mFolders.count();idx++) {
        PProjectModelNode node = mRootNode;
        QString s = mFolders[idx];
        int i = s.indexOf('/');
        while (i>=0) {
            PProjectModelNode findnode;
            for (int c=0;c<node->children.count();c++) {
                if (node->children[c]->text == s.mid(0,i))
                    findnode = node->children[c];
            }
            if (!findnode)
                node = makeNewFolderNode(s.mid(0,i),node);
            else
                node = findnode;
            if (!node->isUnit) {
                qDebug()<<"node "<<node->text<<"is not a folder:"<<s;
                node = mRootNode;
            }
            s.remove(0,i+1);
            i = s.indexOf('/');
        }
        node = makeNewFolderNode(s, node);
        mCustomFolderNodes.append(node);
    }
}

static void addFolderRecursively(QSet<QString>& folders, QString folder) {
    if (folder.isEmpty())
        return;
    folders.insert(excludeTrailingPathDelimiter(folder));
    QString parentFolder = QFileInfo(folder).absolutePath();
    if (parentFolder==folder)
        return;
    addFolderRecursively(folders, parentFolder);
}

void Project::createFileSystemFolderNodes()
{
    QSet<QString> headerFolders;
    QSet<QString> sourceFolders;
    QSet<QString> otherFolders;
    mRootNode->children.clear();
    mSpecialNodes.clear();
    mFileSystemFolderNodes.clear();

    foreach (const PProjectUnit& unit, mUnits) {
        QFileInfo fileInfo(unit->fileName());
        if (isHFile(fileInfo.fileName())) {
            addFolderRecursively(headerFolders,fileInfo.absolutePath());
        } else if (isCFile(fileInfo.fileName())) {
            addFolderRecursively(sourceFolders,fileInfo.absolutePath());
        } else {
            addFolderRecursively(otherFolders,fileInfo.absolutePath());
        }
    }
    PProjectModelNode node = makeNewFolderNode(tr("Headers"),
                                               mRootNode,
                                               ProjectModelNodeType::DUMMY_HEADERS_FOLDER,
                                               1000);
    createFileSystemFolderNode(ProjectModelNodeType::DUMMY_HEADERS_FOLDER,folder(),node, headerFolders);
    mCustomFolderNodes.append(node);
    mSpecialNodes.insert(ProjectModelNodeType::DUMMY_HEADERS_FOLDER,node);

    node = makeNewFolderNode(tr("Sources"),
                             mRootNode,
                             ProjectModelNodeType::DUMMY_SOURCES_FOLDER,
                             900);
    createFileSystemFolderNode(ProjectModelNodeType::DUMMY_SOURCES_FOLDER,folder(),node, sourceFolders);
    mCustomFolderNodes.append(node);
    mSpecialNodes.insert(ProjectModelNodeType::DUMMY_SOURCES_FOLDER,node);

    node = makeNewFolderNode(tr("Others"),
                             mRootNode,
                             ProjectModelNodeType::DUMMY_OTHERS_FOLDER,
                             800);
    createFileSystemFolderNode(ProjectModelNodeType::DUMMY_OTHERS_FOLDER,folder(),node, otherFolders);
    mCustomFolderNodes.append(node);
    mSpecialNodes.insert(ProjectModelNodeType::DUMMY_OTHERS_FOLDER,node);
}

void Project::createFileSystemFolderNode(
        ProjectModelNodeType folderType,
        const QString &folderName,
        PProjectModelNode parent,
        const QSet<QString>& validFolders)
{
    QDirIterator iter(folderName);
    while (iter.hasNext()) {
        iter.next();
        QFileInfo fileInfo = iter.fileInfo();
        if (fileInfo.isHidden() || fileInfo.fileName().startsWith('.'))
            continue;
        if (fileInfo.isDir() && validFolders.contains(fileInfo.absoluteFilePath())) {
            PProjectModelNode node = makeNewFolderNode(fileInfo.fileName(),parent);
            mFileSystemFolderNodes.insert(QString("%1/%2").arg((int)folderType).arg(fileInfo.absoluteFilePath()),node);
            createFileSystemFolderNode(folderType,fileInfo.absoluteFilePath(), node, validFolders);
        }
    }
}

PProjectUnit Project::doAutoOpen()
{
    QHash<QString,PProjectEditorLayout> layouts = loadLayout();

    QHash<int,PProjectEditorLayout> opennedMap;

    QString focusedFilename;
    foreach (const PProjectEditorLayout &layout,layouts) {
        if (layout->isOpen && layout->order>=0) {
            if (layout->isFocused)
                focusedFilename = layout->filename;
            opennedMap.insert(layout->order,layout);
        }
    }

    for (int i=0;i<mUnits.count();i++) {
        PProjectEditorLayout editorLayout = opennedMap.value(i,PProjectEditorLayout());
        if (editorLayout) {
            PProjectUnit unit = findUnit(editorLayout->filename);
            openUnit(unit,editorLayout);
        }
    }

    if (!focusedFilename.isEmpty()) {
        PProjectUnit unit = findUnit(focusedFilename);
        if (unit) {
            Editor * editor = unitEditor(unit);
            mEditorManager->activeEditor(editor,true);
        }
        return unit;
    }
    return PProjectUnit();
}

bool Project::fileAlreadyExists(const QString &s)
{
    return resolveUnitPath(QDir(directory()).absoluteFilePath(s)).status != ProjectPathStatus::NotInProject;
}

PProjectModelNode Project::findFileSystemFolderNode(const QString &folderPath, ProjectModelNodeType nodeType)
{
    PProjectModelNode node = mFileSystemFolderNodes.value(QString("%1/%2").arg((int)nodeType).arg(folderPath),
                                                          PProjectModelNode());
    if (node)
        return node;
    PProjectModelNode parentNode = mSpecialNodes.value(nodeType,PProjectModelNode());
    if (parentNode) {
        QString projectFolder = includeTrailingPathDelimiter(directory());
        if (folderPath.startsWith(projectFolder)) {
            QString pathStr = folderPath.mid(projectFolder.length());
            QStringList paths = pathStr.split("/");
            PProjectModelNode currentParentNode = parentNode;
            QString currentFolderFullPath=directory();
            for (int i=0;i<paths.length();i++) {
                QString currentFolderName = paths[i];
                currentFolderFullPath = currentFolderFullPath+"/"+currentFolderName;
                bool found=false;
                foreach(PProjectModelNode tempNode, parentNode->children) {
                    if (tempNode->folderNodeType == ProjectModelNodeType::Folder
                            && tempNode->text == currentFolderName) {
                        found=true;
                        currentParentNode = tempNode;
                        break;
                    }
                }
                if (!found) {
                    PProjectModelNode newNode = makeNewFolderNode(currentFolderName,currentParentNode);
                    mFileSystemFolderNodes.insert(QString("%1/%2").arg((int)nodeType).arg(currentFolderFullPath),newNode);
                    currentParentNode = newNode;
                }
            }
            return currentParentNode;
        }
        return parentNode;
    }
    return mRootNode;
}

PProjectModelNode Project::getCustomeFolderNodeFromName(const QString &name)
{
    int index = mFolders.indexOf(name);
    if (index>=0) {
        return mCustomFolderNodes[index];
    }
    return mRootNode;
}

QString Project::getNodePath(PProjectModelNode node)
{
    QString result;
    if (!node)
        return result;

    if (node->isUnit) // not a folder
        return result;

    PProjectModelNode p = node;
    while (p && !p->isUnit && p!=mRootNode) {
        if (!result.isEmpty())
            result = p->text + "/" + result;
        else
            result = p->text;
        p = p->parent.lock();
    }
    return result;
}

PProjectModelNode Project::getParentFileSystemFolderNode(const QString &filename)
{
    QFileInfo fileInfo(filename);
    ProjectModelNodeType folderNodeType;
    FileType fileType = getFileType(filename);
    if (isC_CPPHeaderFile(fileType) && !fileInfo.suffix().isEmpty()) {
        folderNodeType = ProjectModelNodeType::DUMMY_HEADERS_FOLDER;
    } else if (isC_CPP_ASMSourceFile(fileType)) {
        folderNodeType = ProjectModelNodeType::DUMMY_SOURCES_FOLDER;
    } else {
        folderNodeType = ProjectModelNodeType::DUMMY_OTHERS_FOLDER;
    }
    return findFileSystemFolderNode(fileInfo.absolutePath(),folderNodeType);
}

void Project::incrementBuildNumber()
{
    mOptions.versionInfo.build++;
    mOptions.versionInfo.fileVersion = QString("%1.%2.%3.%4")
            .arg(mOptions.versionInfo.major)
            .arg(mOptions.versionInfo.minor)
            .arg(mOptions.versionInfo.release)
            .arg(mOptions.versionInfo.build);
    if (mOptions.versionInfo.syncProduct)
        mOptions.versionInfo.productVersion = mOptions.versionInfo.fileVersion;
    saveOptions();
}

QHash<QString, PProjectEditorLayout> Project::loadLayout()
{
    QHash<QString,PProjectEditorLayout> layouts;
    QString jsonFilename = changeFileExt(filename(), "layout");
    QFile file(jsonFilename);
    if (!file.open(QIODevice::ReadOnly))
        return layouts;
    QByteArray content = file.readAll().trimmed();
    if (content.isEmpty())
        return layouts;
    QJsonParseError parseError;
    QJsonDocument doc(QJsonDocument::fromJson(content,&parseError));
    file.close();
    if (parseError.error!=QJsonParseError::NoError || !doc.isArray())
        return layouts;

    QJsonArray jsonLayouts=doc.array();

    for (int i=0;i<jsonLayouts.size();i++) {
        QJsonObject jsonLayout = jsonLayouts[i].toObject();
        QString unitFilename = jsonLayout["filename"].toString();
        if (findUnit(unitFilename)) {
            PProjectEditorLayout editorLayout = std::make_shared<ProjectEditorLayout>();
            editorLayout->filename=unitFilename;
            editorLayout->top=jsonLayout["top"].toInt();
            editorLayout->left=jsonLayout["left"].toInt();
            editorLayout->caretX=jsonLayout["caretX"].toInt();
            editorLayout->caretY=jsonLayout["caretY"].toInt();
            editorLayout->order=jsonLayout["order"].toInt(-1);
            editorLayout->isFocused=jsonLayout["focused"].toBool();
            editorLayout->isOpen=jsonLayout["isOpen"].toBool();
            layouts.insert(unitFilename,editorLayout);
        }
    }

    return layouts;
}

void Project::loadOptions(SimpleIni& ini)
{
    mName = fromByteArray(ini.GetValue("Project","name", ""));
    QString icon = fromByteArray(ini.GetValue("Project", "icon", ""));
    if (icon.isEmpty()) {
        mOptions.icon = "";
    } else {
        mOptions.icon = generateAbsolutePath(directory(),icon);
    }
    mOptions.version = ini.GetLongValue("Project", "Ver", 0);
    if (mOptions.version > 0) { // ver > 0 is at least a v5 project
        if (mOptions.version < 3) {
            mOptions.version = 3;
            QMessageBox::information(nullptr,
                                     tr("Settings need update"),
                                     tr("The compiler settings format of Red Panda C++ has changed.")
                                     +"<BR /><BR />"
                                     +tr("Please update your settings at Project >> Project Options >> Compiler and save your project."),
                                     QMessageBox::Ok);
        }

        mOptions.type = static_cast<ProjectType>(ini.GetLongValue("Project", "type", 0));
        // ;CONFIG_LINE; is used in olded version config files (<2.17)
        // keep it for compatibility
        mOptions.compilerCmd = fromByteArray(ini.GetValue("Project", "Compiler", "")).replace(";CONFIG_LINE;","\n");
        mOptions.cppCompilerCmd = fromByteArray(ini.GetValue("Project", "CppCompiler", "")).replace(";CONFIG_LINE;","\n");
        mOptions.linkerCmd = fromByteArray(ini.GetValue("Project", "Linker", "")).replace(";CONFIG_LINE;","\n");
        mOptions.resourceCmd = fromByteArray(ini.GetValue("Project", "ResourceCommand", "")).replace(";CONFIG_LINE;","\n");
        mOptions.binDirs = absolutePaths(fromByteArray(ini.GetValue("Project", "Bins", "")).split(";", Qt::SkipEmptyParts));
        mOptions.libDirs = absolutePaths(fromByteArray(ini.GetValue("Project", "Libs", "")).split(";", Qt::SkipEmptyParts));
        mOptions.includeDirs = absolutePaths(fromByteArray(ini.GetValue("Project", "Includes", "")).split(";", Qt::SkipEmptyParts));
        mOptions.privateResource = fromByteArray(ini.GetValue("Project", "PrivateResource", ""));
        mOptions.resourceIncludes = absolutePaths(fromByteArray(ini.GetValue("Project", "ResourceIncludes", "")).split(";", Qt::SkipEmptyParts));
        mOptions.makeIncludes = absolutePaths(fromByteArray(ini.GetValue("Project", "MakeIncludes", "")).split(";", Qt::SkipEmptyParts));
        mOptions.isCpp = ini.GetBoolValue("Project", "IsCpp", false);
        mOptions.folderForOutput = generateAbsolutePath(directory(), fromByteArray(ini.GetValue("Project", "ExeOutput", "")));
        mOptions.folderForObjFiles =  generateAbsolutePath(directory(), fromByteArray(ini.GetValue("Project", "ObjectOutput", "")));
        mOptions.logFilename = generateAbsolutePath(directory(), fromByteArray(ini.GetValue("Project", "LogOutput", "")));
        mOptions.logOutput = ini.GetBoolValue("Project", "LogOutputEnabled", false);
        mOptions.useCustomOutputFilename = ini.GetBoolValue("Project", "OverrideOutput", false);
        mOptions.customOutputFilename = fromByteArray(ini.GetValue("Project", "OverrideOutputName", ""));
        mOptions.hostApplication = generateAbsolutePath(directory(), fromByteArray(ini.GetValue("Project", "HostApplication", "")));
        mOptions.useCustomMakefile = ini.GetBoolValue("Project", "UseCustomMakefile", false);
        mOptions.customMakefile = generateAbsolutePath(directory(),fromByteArray(ini.GetValue("Project", "CustomMakefile", "")));
        mOptions.usePrecompiledHeader = ini.GetBoolValue("Project", "UsePrecompiledHeader", false);
        mOptions.precompiledHeader = generateAbsolutePath(directory(),fromByteArray(ini.GetValue("Project", "PrecompiledHeader", "")));
        mOptions.cmdLineArgs = fromByteArray(ini.GetValue("Project", "CommandLine", ""));
        mFolders = fromByteArray(ini.GetValue("Project", "Folders", "")).split(";", Qt::SkipEmptyParts);
        mOptions.includeVersionInfo = ini.GetBoolValue("Project", "IncludeVersionInfo", false);
        mOptions.supportXPThemes = ini.GetBoolValue("Project", "SupportXPThemes", false);
        mOptions.compilerSet = ini.GetLongValue("Project", "CompilerSet", pSettings->compilerSets().defaultIndex());
        mOptions.modelType = (ProjectModelType)ini.GetLongValue("Project", "ModelType", (int)ProjectModelType::Custom);
        mOptions.classBrowserType = (ProjectClassBrowserType)ini.GetLongValue("Project", "ClassBrowserType", (int)ProjectClassBrowserType::CurrentFile);

        if (mOptions.compilerSet >= (int)pSettings->compilerSets().size()
                || mOptions.compilerSet < 0) { // TODO: change from indices to names
            QMessageBox::critical(
                        nullptr,
                        tr("Compiler not found"),
                        tr("The compiler set you have selected for this project, no longer exists.")
                        +"<BR />"
                        +tr("It will be substituted by the global compiler set."),
                        QMessageBox::Ok
                                  );
            setCompilerSet(pSettings->compilerSets().defaultIndex());
            saveOptions();
        }

        PCompilerSet pSet = pSettings->compilerSets().getSet(mOptions.compilerSet);
        if (pSet) {
            QByteArray oldCompilerOptions = ini.GetValue("Project", "CompilerSettings", "");
            if (!oldCompilerOptions.isEmpty()) {
                //version 2 compatibility
                // test if it is created by old dev-c++
                SimpleIni::TNamesDepend oKeys;
                ini.GetAllKeys("Project", oKeys);
                bool isNewDev=false;
                for(const SimpleIni::Entry& entry:oKeys) {
                    QString key(entry.pItem);
                    if (key=="UsePrecompiledHeader"
                            || key == "CompilerSetType"
                            || key == "StaticLink"
                            || key == "AddCharset"
                            || key == "ExecEncoding"
                            || key == "Encoding"
                            || key == "UseUTF8") {
                        isNewDev = true;
                        break;
                    }
                }
                if (!isNewDev && oldCompilerOptions.length()>=25) {
                    char t = oldCompilerOptions[18];
                    oldCompilerOptions[18]=oldCompilerOptions[21];
                    oldCompilerOptions[21]=t;
                }
                for (int i=0;i<oldCompilerOptions.length();i++) {
                    QString key = CompilerSets::getKeyFromCompilerCompatibleIndex(i);
                    PCompilerOption pOption = CompilerInfoManager::getCompilerOption(
                                pSet->compilerType(), key);
                    if (pOption) {
                        int val = CompilerSet::charToValue(oldCompilerOptions[i]);
                        if (pOption->choices.isEmpty()) {
                            if (val>0)
                                mOptions.compilerOptions.insert(key,COMPILER_OPTION_ON);
                            else
                                mOptions.compilerOptions.insert(key,"");
                        } else {
                            if (val>0 && val <= pOption->choices.length())
                                mOptions.compilerOptions.insert(key,pOption->choices[val-1].second);
                            else
                                mOptions.compilerOptions.insert(key,"");
                        }
                    }
                }
            } else {
                //version 3
                SimpleIni::TNamesDepend oKeys;
                ini.GetAllKeys("CompilerSettings", oKeys);
                for(const SimpleIni::Entry& entry:oKeys) {
                    QString key(entry.pItem);
                    mOptions.compilerOptions.insert(
                                    key,
                                    ini.GetValue("CompilerSettings", entry.pItem, ""));
                }
            }
        }

        mOptions.staticLink = ini.GetBoolValue("Project", "StaticLink", true);
        mOptions.execEncoding = ini.GetValue("Project","ExecEncoding", ENCODING_SYSTEM_DEFAULT);
        mOptions.addCharset = ini.GetBoolValue("Project", "AddCharset", true);

        bool useUTF8 = ini.GetBoolValue("Project", "UseUTF8", false);
        if (useUTF8) {
            mOptions.encoding = ini.GetValue("Project","Encoding", ENCODING_UTF8);
        } else {
            mOptions.encoding = ini.GetValue("Project","Encoding", pSettings->editor().defaultEncoding());
        }
        if (mOptions.encoding == ENCODING_AUTO_DETECT)
            mOptions.encoding = pSettings->editor().defaultEncoding();
        if (mOptions.encoding == ENCODING_AUTO_DETECT)
            mOptions.encoding = ENCODING_SYSTEM_DEFAULT;

        mOptions.allowParallelBuilding = ini.GetBoolValue("Project","AllowParallelBuilding");
        mOptions.parellelBuildingJobs = ini.GetLongValue("Project","ParellelBuildingJobs");


        mOptions.versionInfo.major = ini.GetLongValue("VersionInfo", "Major", 0);
        mOptions.versionInfo.minor = ini.GetLongValue("VersionInfo", "Minor", 1);
        mOptions.versionInfo.release = ini.GetLongValue("VersionInfo", "Release", 1);
        mOptions.versionInfo.build = ini.GetLongValue("VersionInfo", "Build", 1);
        mOptions.versionInfo.languageID = ini.GetLongValue("VersionInfo", "LanguageID", 0x0409);
        mOptions.versionInfo.charsetID = ini.GetLongValue("VersionInfo", "CharsetID", 0x04E4);
        mOptions.versionInfo.companyName = fromByteArray(ini.GetValue("VersionInfo", "CompanyName", ""));
        mOptions.versionInfo.fileVersion = fromByteArray(ini.GetValue("VersionInfo", "FileVersion", "0.1"));
        mOptions.versionInfo.fileDescription = fromByteArray(ini.GetValue("VersionInfo", "FileDescription",
          toByteArray(tr("Developed using the Red Panda C++ IDE"))));
        mOptions.versionInfo.internalName = fromByteArray(ini.GetValue("VersionInfo", "InternalName", ""));
        mOptions.versionInfo.legalCopyright = fromByteArray(ini.GetValue("VersionInfo", "LegalCopyright", ""));
        mOptions.versionInfo.legalTrademarks = fromByteArray(ini.GetValue("VersionInfo", "LegalTrademarks", ""));
        mOptions.versionInfo.originalFilename = fromByteArray(ini.GetValue("VersionInfo", "OriginalFilename",
                                                                           toByteArray(extractFileName(outputFilename()))));
        mOptions.versionInfo.productName = fromByteArray(ini.GetValue("VersionInfo", "ProductName", toByteArray(mName)));
        mOptions.versionInfo.productVersion = fromByteArray(ini.GetValue("VersionInfo", "ProductVersion", "0.1.1.1"));
        mOptions.versionInfo.autoIncBuildNr = ini.GetBoolValue("VersionInfo", "AutoIncBuildNr", false);
        mOptions.versionInfo.syncProduct = ini.GetBoolValue("VersionInfo", "SyncProduct", false);

    }
}

void Project::loadUnitLayout(Editor *e)
{
    if (!e)
        return;

    QHash<QString, PProjectEditorLayout> layouts = loadLayout();

    PProjectEditorLayout layout = layouts.value(e->filename(),PProjectEditorLayout());
    if (layout) {
        e->setCaretY(layout->caretY);
        e->setCaretX(layout->caretX);
        e->setTopPos(layout->top);
        e->setLeftPos(layout->left);
    }
}

QString Project::relativePath(const QString &filename)
{
    QString appPath = includeTrailingPathDelimiter(pSettings->dirs().appDir());
    QString projectPath = includeTrailingPathDelimiter(directory());
    if (filename.startsWith(appPath) && !filename.startsWith(projectPath)) {
        return "%APP_PATH%/"+filename.mid(appPath.length());
    }
    QDir projectDir(directory());
    QDir grandparentDir(projectDir.absoluteFilePath("../../"));
    QString grandparentPath=grandparentDir.absolutePath();
    if (grandparentDir.exists()
            && filename.startsWith(grandparentPath))
        return extractRelativePath(directory(),filename);
    return filename;
}

QStringList Project::relativePaths(const QStringList &files)
{
    QStringList lst;
    foreach(const QString& file,files) {
        lst.append(relativePath(file));
    }
    return lst;
}

QString Project::absolutePath(const QString &filename)
{
    QString appSuffix = "%APP_PATH%/";
    if (filename.startsWith(appSuffix)) {
        return includeTrailingPathDelimiter(pSettings->dirs().appDir()) + filename.mid(appSuffix.length());
    }
    return generateAbsolutePath(directory(),filename);
}

QStringList Project::absolutePaths(const QStringList &files)
{
    QStringList lst;
    foreach(const QString& file,files) {
        lst.append(absolutePath(file));
    }
    return lst;
}

PCppParser Project::cppParser()
{
    return mParser;
}

void Project::updateFolderNode(PProjectModelNode node)
{
    for (int i=0;i<node->children.count();i++){
        PProjectModelNode child = node->children[i];
        if (!child->isUnit) {
            mFolders.append(getNodePath(child));
            updateFolderNode(child);
        }
    }
}

void Project::updateCompilerSetting()
{
    PCompilerSet defaultSet = pSettings->compilerSets().getSet(mOptions.compilerSet);
    if (defaultSet) {
        mOptions.staticLink = defaultSet->staticLink();
        mOptions.compilerOptions = defaultSet->compileOptions();
    } else {
        mOptions.staticLink = false;
    }
}

QFileSystemWatcher *Project::fileSystemWatcher() const
{
    return mFileSystemWatcher;
}

QString Project::fileSystemNodeFolderPath(const PProjectModelNode &node)
{
    QString result;
    if (node != mRootNode) {
        PProjectModelNode pNode = node;
        while (pNode && pNode->folderNodeType == ProjectModelNodeType::Folder) {
            result = node->text + "/" +result;
            pNode = pNode->parent.lock();
        }
    }
    result = folder() + "/" + result;
    return result;
}

QStringList Project::binDirs()
{
    QStringList lst = options().binDirs;
    PCompilerSet compilerSet = pSettings->compilerSets().getSet(options().compilerSet);
    if (compilerSet) {
        lst.append(compilerSet->binDirs());
    }
    return lst;
}

void Project::renameFolderNode(PProjectModelNode node, const QString newName)
{
    if (!node)
        return;
    if (node->isUnit)
        return;
    node->text = newName;
    updateFolders();
    setModified(true);
    emit nodeRenamed();
}

EditorManager *Project::editorList() const
{
    return mEditorManager;
}

ProjectModelType Project::modelType() const
{
    return mOptions.modelType;
}

void Project::setModelType(ProjectModelType type)
{
    if (type!=mOptions.modelType) {
        mOptions.modelType = type;
        rebuildNodes();
    }
}

ProjectOptions &Project::options()
{
    return mOptions;
}

ProjectModel *Project::model()
{
    return mModel;
}

const PProjectModelNode &Project::rootNode() const
{
    return mRootNode;
}

const QString &Project::name() const
{
    return mName;
}

void Project::setName(const QString &newName)
{
    if (newName != mName) {
        mName = newName;
        mRootNode->text = newName;
        setModified(true);
    }
}

const QString &Project::filename() const
{
    return mFilename;
}

ProjectUnit::ProjectUnit(Project* parent)
{
    mNode = nullptr;
    mParent = parent;
//    mFileMissing = false;
    mPriority=0;
    mNew = true;
    mUnitEncoding=ENCODING_PROJECT;
    mFileEncoding="";
}

Project *ProjectUnit::parent() const
{
    return mParent;
}

const QString &ProjectUnit::fileName() const
{
    return mFileName;
}

void ProjectUnit::setFileName(QString newFileName)
{
    newFileName = QFileInfo(newFileName).absoluteFilePath();
    if (mFileName != newFileName) {
        mFileName = newFileName;
        if (mNode) {
            mNode->text = extractFileName(mFileName);
        }
    }
}

void ProjectUnit::setNew(bool newNew)
{
    mNew = newNew;
}

const QByteArray &ProjectUnit::realEncoding() const
{
    return mFileEncoding;
}

void ProjectUnit::setRealEncoding(const QByteArray &newRealEncoding)
{
    mFileEncoding = newRealEncoding;
}

const QString &ProjectUnit::folder() const
{
    return mFolder;
}

void ProjectUnit::setFolder(const QString &newFolder)
{
    mFolder = newFolder;
}

bool ProjectUnit::compile() const
{
    return mCompile;
}

void ProjectUnit::setCompile(bool newCompile)
{
    mCompile = newCompile;
}

bool ProjectUnit::compileCpp() const
{
    return mCompileCpp;
}

void ProjectUnit::setCompileCpp(bool newCompileCpp)
{
    mCompileCpp = newCompileCpp;
}

bool ProjectUnit::overrideBuildCmd() const
{
    return mOverrideBuildCmd;
}

void ProjectUnit::setOverrideBuildCmd(bool newOverrideBuildCmd)
{
    mOverrideBuildCmd = newOverrideBuildCmd;
}

const QString &ProjectUnit::buildCmd() const
{
    return mBuildCmd;
}

void ProjectUnit::setBuildCmd(const QString &newBuildCmd)
{
    mBuildCmd = newBuildCmd;
}

bool ProjectUnit::link() const
{
    return mLink;
}

void ProjectUnit::setLink(bool newLink)
{
    mLink = newLink;
}

int ProjectUnit::priority() const
{
    return mPriority;
}

void ProjectUnit::setPriority(int newPriority)
{
    if (mPriority!=newPriority) {
        mPriority = newPriority;
        if (mNode)
            mNode->priority = mPriority;
    }
}

const QByteArray &ProjectUnit::encoding() const
{
    return mUnitEncoding;
}

void ProjectUnit::setEncoding(const QByteArray &newEncoding)
{
    if (mUnitEncoding != newEncoding) {
        Editor * editor=mParent->unitEditor(this);
        if (editor) {
            editor->setEditorEncoding(newEncoding);
        }
        mUnitEncoding = newEncoding;
    }
}

PProjectModelNode &ProjectUnit::node()
{
    return mNode;
}

void ProjectUnit::setNode(const PProjectModelNode &newNode)
{
    mNode = newNode;
}

//bool ProjectUnit::FileMissing() const
//{
//    return mFileMissing;
//}

//void ProjectUnit::setFileMissing(bool newDontSave)
//{
//    mFileMissing = newDontSave;
//}

ProjectModel::ProjectModel(IconsManager * iconsManager, Project *project):
    QAbstractItemModel(project),
    mProject(project)
{
    mUpdateCount = 0;
    Q_ASSERT(iconsManager!=nullptr);
    //delete in the destructor
    mIconsManager = iconsManager;
    mIconProvider = std::make_unique<CustomFileIconProvider>(iconsManager);
}

void ProjectModel::beginUpdate()
{
    if (mUpdateCount==0) {
        beginResetModel();
    }
    mUpdateCount++;
}

void ProjectModel::endUpdate()
{
    mUpdateCount--;
    if (mUpdateCount==0) {
        mIconProvider->setRootFolder(mProject->folder());
        endResetModel();
    }
}

CustomFileIconProvider *ProjectModel::iconProvider() const
{
    return mIconProvider.get();
}

bool ProjectModel::insertRows(int row, int count, const QModelIndex &parent)
{
    beginInsertRows(parent,row,row+count-1);
    endInsertRows();
    return true;
}

bool ProjectModel::removeRows(int row, int count, const QModelIndex &parent)
{
    beginRemoveRows(parent,row,row+count-1);
    if (!parent.isValid())
        return false;
    ProjectModelNode* parentNode = static_cast<ProjectModelNode*>(parent.internalPointer());
    if (!parentNode)
        return false;

    parentNode->children.removeAt(row);

    endRemoveRows();
    return true;
}

Project *ProjectModel::project() const
{
    return mProject;
}

QModelIndex ProjectModel::index(int row, int column, const QModelIndex &parent) const
{
    if (!parent.isValid()) {
        return createIndex(row,column,mProject->rootNode().get());
    }
    ProjectModelNode* parentNode = static_cast<ProjectModelNode*>(parent.internalPointer());
    if (!parentNode) {
        return QModelIndex();
    }
    if (row<0 || row>=parentNode->children.count())
        return QModelIndex();
    return createIndex(row,column,parentNode->children[row].get());
}

QModelIndex ProjectModel::parent(const QModelIndex &child) const
{
    if (!child.isValid())
        return QModelIndex();
    ProjectModelNode * node = static_cast<ProjectModelNode*>(child.internalPointer());
    if (!node)
        return QModelIndex();
    return getParentIndex(node);
}

int ProjectModel::rowCount(const QModelIndex &parent) const
{
    if (!parent.isValid())
        return 1;
    ProjectModelNode* p = static_cast<ProjectModelNode*>(parent.internalPointer());
    if (p) {
        return p->children.count();
    } else {
        return mProject->rootNode()->children.count();
    }
}

int ProjectModel::columnCount(const QModelIndex &) const
{
    return 1;
}

QVariant ProjectModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();
    ProjectModelNode* p = static_cast<ProjectModelNode*>(index.internalPointer());
    if (!p)
        return QVariant();
    if (role == Qt::DisplayRole) {
#ifdef ENABLE_VCS
        if (p == mProject->rootNode().get()) {
            QString branch;
            if (mIconProvider->VCSRepository()->hasRepository(branch))
                return QString("%1 [%2]").arg(p->text,branch);
        }
#endif
        return p->text;
    } else if (role==Qt::EditRole) {
        return p->text;
    } else if (role == Qt::DecorationRole) {
        QIcon icon;
        if (p->isUnit) {
            PProjectUnit unit = p->pUnit.lock();
            if (unit)
                icon = mIconProvider->icon(QFileInfo(unit->fileName()));
        } else {
            if (p == mProject->rootNode().get()) {
#ifdef ENABLE_VCS
                QString branch;
                if (mIconProvider->VCSRepository()->hasRepository(branch))
                    icon = mIconsManager->getIcon(IconsManager::FILESYSTEM_GIT);
#endif
            } else {
                switch(p->folderNodeType) {
                case ProjectModelNodeType::DUMMY_HEADERS_FOLDER:
                    icon = mIconsManager->getIcon(IconsManager::FILESYSTEM_HEADERS_FOLDER);
                    break;
                case ProjectModelNodeType::DUMMY_SOURCES_FOLDER:
                    icon = mIconsManager->getIcon(IconsManager::FILESYSTEM_SOURCES_FOLDER);
                    break;
                default:
                    icon = mIconsManager->getIcon(IconsManager::FILESYSTEM_FOLDER);
                }
            }
            if (icon.isNull())
                icon = mIconProvider->icon(QFileIconProvider::Folder);
        }
        return icon;
    }
    return QVariant();
}

Qt::ItemFlags ProjectModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    ProjectModelNode* p = static_cast<ProjectModelNode*>(index.internalPointer());
    if (!p)
        return Qt::NoItemFlags;
    if (p==mProject->rootNode().get())
        return Qt::ItemIsEnabled | Qt::ItemIsDropEnabled | Qt::ItemIsEditable;
    if (mProject && mProject->modelType() == ProjectModelType::FileSystem) {
        Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
        if (p->isUnit)
            flags.setFlag(Qt::ItemIsEditable);
        return flags;
    } else {
        Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsEditable | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled;
        if (!p->isUnit) {
            flags.setFlag(Qt::ItemIsDropEnabled);
            flags.setFlag(Qt::ItemIsDragEnabled,false);
        }
        return flags;
    }
}

bool ProjectModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    QPointer<ProjectModel> modelLifetime(this);
    QPointer<Project> projectLifetime(mProject);
    if (!index.isValid())
        return false;
    ProjectModelNode* p = static_cast<ProjectModelNode*>(index.internalPointer());
    PProjectModelNode node = mProject->pointerToNode(p);
    if (!node)
        return false;
    if (role == Qt::EditRole) {
        if (node == mProject->rootNode()) {
            QString newName = value.toString().trimmed();
            if (newName.isEmpty())
                return false;
            mProject->setName(newName);
            emit dataChanged(index,index);
            return true;
        }
        PProjectUnit unit = node->pUnit.lock();
        if (unit) {
            //change unit name

            QString newName = value.toString().trimmed();
            if (newName.isEmpty())
                return false;
            if (newName ==  node->text)
                return false;
            QString oldName = unit->fileName();
            QString curDir = extractFilePath(oldName);
            newName = generateAbsolutePath(curDir,newName);
            // The model is the legacy UI adapter: collect decisions before
            // delegating disk/model commit to the project operation.
            if (!QFileInfo(oldName).isFile()) {
                QMessageBox::critical(nullptr, tr("Rename failed"),
                                      tr("Cannot rename missing project file '%1'.").arg(oldName));
                return false;
            }
            bool replaceExisting = false;
            if (QFileInfo::exists(newName) && sameExistingFile(newName, oldName) != ExistingFileMatch::Same) {
                if (!QFileInfo(newName).isFile()) {
                    QMessageBox::critical(nullptr, tr("Rename failed"),
                                          tr("The rename destination is not a file: '%1'.").arg(newName));
                    return false;
                }
                const auto replacementChoice = QMessageBox::question(nullptr, tr("File exists"),
                        tr("File '%1' already exists. Replace it?").arg(newName),
                        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
                if (!modelLifetime || !projectLifetime)
                    return false;
                if (replacementChoice != QMessageBox::Yes)
                    return false;
                if (mProject->findUnit(oldName) != unit || unit->fileName() != oldName || !QFileInfo(oldName).isFile())
                    return false;
                QPointer<Editor> targetEditor = mProject->editorList()->getOpenedEditor(newName);
                if (targetEditor && !mProject->editorList()->closeEditor(targetEditor))
                    return false;
                if (!modelLifetime || !projectLifetime)
                    return false;
                replaceExisting = true;
            }
            // A dialog can process other events. Validate the resource plan
            // again before any watcher, file or model is changed.
            if (mProject->findUnit(oldName) != unit || unit->fileName() != oldName)
                return false;
            const auto result = mProject->renameUnitFile(unit, newName, replaceExisting);
            if (!result.committed()) {
                QMessageBox::critical(nullptr, tr("Rename failed"), result.error);
                return false;
            }
            if (!result.succeeded()) {
                QMessageBox::warning(nullptr, tr("Rename completed with pending updates"), result.error);
                if (!modelLifetime || !projectLifetime)
                    return true; // the rename already committed
            }
            try {
                mProject->saveAll();
            } catch (const FileError &error) {
                // The rename is committed; keep the dirty project and state
                // which metadata still needs a retry instead of undoing UI.
                QMessageBox::warning(nullptr, tr("Save Error"), error.reason());
            }

            return true;
        } else {
            //change folder name
            QString newName = value.toString().trimmed();
            if (newName.isEmpty())
                return false;
            if (newName ==  node->text)
                return false;
            mProject->renameFolderNode(node,newName);

            emit dataChanged(index,index);

            mProject->saveAll();
            return true;
        }

    }
    return false;
}

void ProjectModel::refreshIcon(const QModelIndex &index, bool update)
{
    if (!index.isValid())
        return;
    if (update)
        mIconProvider->update();
    QVector<int> roles;
    roles.append(Qt::DecorationRole);
    emit dataChanged(index,index, roles);
}

void ProjectModel::refreshIcon(const QString &filename)
{
    PProjectUnit unit=mProject->findUnit(filename);
    if (!unit)
        return;
    PProjectModelNode node=unit->node();
    QModelIndex index = getNodeIndex(node.get());
    refreshIcon(index);
}

void ProjectModel::refreshIcons()
{
    mIconProvider->update();
    mProject->rootNode();
}

void ProjectModel::refreshNodeIconRecursive(PProjectModelNode node)
{
    QModelIndex index=getNodeIndex(node.get());
    refreshIcon(index,false);
    foreach( PProjectModelNode child, node->children) {
        refreshNodeIconRecursive(child);
    }
}

QModelIndex ProjectModel::getNodeIndex(ProjectModelNode *node) const
{
    if (!node)
        return QModelIndex();
    PProjectModelNode parent = node->parent.lock();
    if (!parent) // root node
        return createIndex(0,0,node);
    int row = -1;
    for (int i=0;i<parent->children.count();i++) {
        const PProjectModelNode& pNode=parent->children[i];
        if (pNode.get()==node) {
            row = i;
        }
    }
    if (row<0)
        return QModelIndex();
    return createIndex(row,0,node);
}

QModelIndex ProjectModel::getParentIndex(ProjectModelNode * node) const
{
    PProjectModelNode parent = node->parent.lock();
    if (!parent) // root node
        return QModelIndex();
    PProjectModelNode grand = parent->parent.lock();
    if (!grand) {
        return createIndex(0,0,parent.get());
    }

    int row = grand->children.indexOf(parent);
    if (row<0)
        return QModelIndex();
    return createIndex(row,0,parent.get());
}

QModelIndex ProjectModel::rootIndex() const
{
    return getNodeIndex(mProject->rootNode().get());
}

bool ProjectModel::canDropMimeData(const QMimeData * data, Qt::DropAction action, int /*row*/, int /*column*/, const QModelIndex &parent) const
{

    if (!data || action != Qt::MoveAction)
        return false;
    if (!parent.isValid())
        return false;
    // check if the format is supported
    QStringList types = mimeTypes();
    if (types.isEmpty())
        return false;
    QString format = types.at(0);
    if (!data->hasFormat(format))
        return false;

    QModelIndex idx = parent;
//    if (row >= rowCount(parent) || row < 0) {
//        return false;
//    } else {
//        idx= index(row,column,parent);
//    }
    ProjectModelNode* p= static_cast<ProjectModelNode*>(idx.internalPointer());
    PProjectModelNode node = mProject->pointerToNode(p);
    if (node->isUnit)
        return false;
    QByteArray encoded = data->data(format);
    QDataStream stream(&encoded, QIODevice::ReadOnly);
    while (!stream.atEnd()) {
        qint32 r, c;
        quintptr v;
        stream >> r >> c >> v;
        ProjectModelNode* droppedPointer= (ProjectModelNode*)(v);
        PProjectModelNode droppedNode = mProject->pointerToNode(droppedPointer);
        PProjectModelNode oldParent = droppedNode->parent.lock();
        if (oldParent == node)
            return false;
    }
    return true;
}

Qt::DropActions ProjectModel::supportedDropActions() const
{
    return Qt::MoveAction;
}

bool ProjectModel::dropMimeData(const QMimeData *data, Qt::DropAction action, int /*row*/, int /*column*/, const QModelIndex &parent)
{
    // check if the action is supported
    if (!data || action != Qt::MoveAction)
        return false;
    // check if the format is supported
    QStringList types = mimeTypes();
    if (types.isEmpty())
        return false;
    QString format = types.at(0);
    if (!data->hasFormat(format))
        return false;

    if (!parent.isValid())
        return false;
    ProjectModelNode* p= static_cast<ProjectModelNode*>(parent.internalPointer());
    PProjectModelNode node = mProject->pointerToNode(p);

    QByteArray encoded = data->data(format);
    QDataStream stream(&encoded, QIODevice::ReadOnly);
    QVector<int> rows,cols;
    QVector<intptr_t> pointers;
    while (!stream.atEnd()) {
        qint32 r, c;
        quintptr v;
        stream >> r >> c >> v;
        rows.append(r);
        cols.append(c);
        pointers.append(v);
    }
    for (int i=pointers.count()-1;i>=0;i--) {
        int r = rows[i];
        intptr_t v = pointers[i];
        ProjectModelNode* droppedPointer= (ProjectModelNode*)(v);
        PProjectModelNode droppedNode = mProject->pointerToNode(droppedPointer);
        PProjectModelNode oldParent = droppedNode->parent.lock();
        if (oldParent) {
            QModelIndex oldParentIndex = getNodeIndex(oldParent.get());
            beginRemoveRows(oldParentIndex,r,r);
            oldParent->children.removeAt(r);
            endRemoveRows();
        }
        QModelIndex newParentIndex = getNodeIndex(node.get());
        beginInsertRows(newParentIndex,node->children.count(),node->children.count());
        droppedNode->parent = node;
        node->children.append(droppedNode);
        if (droppedNode->isUnit) {
            PProjectUnit unit = droppedNode->pUnit.lock();
            unit->setFolder(mProject->getNodePath(node));
        }
        endInsertRows();
        mProject->saveAll();
        return true;
    }

    return false;
}

QMimeData *ProjectModel::mimeData(const QModelIndexList &indexes) const
{
    if (indexes.count() <= 0)
        return nullptr;
    QStringList types = mimeTypes();
    if (types.isEmpty())
        return nullptr;
    QMimeData *data = new QMimeData();
    QString format = types.at(0);
    QByteArray encoded;
    QDataStream stream(&encoded, QIODevice::WriteOnly);
    QModelIndexList::ConstIterator it = indexes.begin();
    QList<QUrl> urls;
    for (; it != indexes.end(); ++it) {
        stream << (qint32)((*it).row()) << (qint32)((*it).column()) << (quintptr)((*it).internalPointer());
        ProjectModelNode* p = static_cast<ProjectModelNode*>((*it).internalPointer());
        if (p && p->isUnit) {
            PProjectUnit unit = p->pUnit.lock();
            if (unit)
                urls.append(QUrl::fromLocalFile(unit->fileName()));
        }
    }
    if (!urls.isEmpty())
        data->setUrls(urls);
    data->setData(format, encoded);
    return data;
}

ProjectModelSortFilterProxy::ProjectModelSortFilterProxy(QObject *parent):
    QSortFilterProxyModel(parent)
{

}

bool ProjectModelSortFilterProxy::lessThan(const QModelIndex &source_left, const QModelIndex &source_right) const
{
    if (!sourceModel())
        return false;
    ProjectModelNode* pLeft=nullptr;
    if (source_left.isValid())
        pLeft = static_cast<ProjectModelNode*>(source_left.internalPointer());
    ProjectModelNode* pRight=nullptr;
    if (source_right.isValid())
        pRight = static_cast<ProjectModelNode*>(source_right.internalPointer());
    if (!pLeft)
        return true;
    if (!pRight)
        return false;
    if (!pLeft->isUnit && pRight->isUnit)
        return true;
    if (pLeft->isUnit && !pRight->isUnit)
        return false;
    if (pLeft->priority!=pRight->priority)
        return pLeft->priority>pRight->priority;
    return QString::compare(pLeft->text, pRight->text)<0;
}
