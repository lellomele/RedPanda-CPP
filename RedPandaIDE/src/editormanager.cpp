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
#include "editormanager.h"
#include "editor.h"
#include <QMessageBox>
#include <QVariant>
#include "mainwindow.h"
#include <QFileInfo>
#include "settings.h"
#include "project.h"
#include "systemconsts.h"
#include "documentfilecontroller.h"
#include "analysis/parserconfiguration.h"
#include "visithistorymanager.h"
#include "debugger/debugger.h"
#include "utils/parser.h"
#include <QApplication>
#include <QPointer>
#include <memory>
#include <qsynedit/document.h>

EditorManager::EditorManager(QTabWidget* leftPageWidget,
      QTabWidget* rightPageWidget,
      QSplitter* splitter,
      QWidget* panel,
      QObject* parent):
    QObject(parent),
    mLayout(LayoutShowType::lstLeft),
    mLeftPageWidget(leftPageWidget),
    mRightPageWidget(rightPageWidget),
    mSplitter(splitter),
    mPanel(panel),
    mUpdateCount(0)
{
    mDocumentFiles = std::make_unique<DocumentFileController>(*this, *pSystemConsts);
}

EditorManager::~EditorManager() = default;

DocumentSaveAsResult EditorManager::saveDocument(Editor *editor, const DocumentSaveRequest &request)
{
    auto operation = *mDocumentFiles; // Its modal lifetime is independent of this owner.
    return operation.save(editor, request);
}

UnitLocationCommitResult EditorManager::relinkProjectUnit(const std::shared_ptr<Project> &project,
                                                         const std::shared_ptr<ProjectUnit> &unit, const QString &name)
{
    auto operation = *mDocumentFiles;
    return operation.relink(project, unit, name);
}

Editor* EditorManager::newEditor(const QString& filename, const QByteArray& encoding,
                              FileType fileType, const QString& contextFile,
                              bool inProject, bool newFile,
                              QTabWidget* page) {
    const auto occupied = resolveOpenedEditor(filename);
    if (occupied.status != EditorPathStatus::NotOpen)
        throw FileError(occupied.error.isEmpty() ? tr("The file is already open: '%1'.").arg(filename) : occupied.error);
    QTabWidget * parentPageControl = nullptr;
    if (page == nullptr)
        parentPageControl = getNewEditorPageControl();
    else
        parentPageControl = page;
    if (!parentPageControl)
        return nullptr;

    // A failed load must not leave an invisible editor owned by the tab widget.
    // Transfer ownership only after the complete initialization succeeds.
    auto candidate = std::make_unique<Editor>(parentPageControl);
    Editor *e = candidate.get();
    e->setSaveOperation([owner = QPointer<EditorManager>(this)](Editor *source, const DocumentSaveRequest &request) {
        if (owner) return owner->saveDocument(source, request);
        DocumentSaveAsResult result;
        result.outcome = OperationResult::failure(QCoreApplication::translate("DocumentFileController", "The document manager is no longer available."));
        return result;
    });
    e->setEditorSettings(&pSettings->editor());
    e->setCodeCompletionSettings(&pSettings->codeCompletion());
    e->setColorManager(pMainWindow->colorManager());
    e->setIconsManager(pMainWindow->iconsManager());
//    e->setGetSharedParserFunc(std::bind(&EditorManager::sharedParser,this,std::placeholders::_1));
    e->setGetOpennedFunc(std::bind(&EditorManager::getOpenedEditor,this,std::placeholders::_1));
    e->setGetFileStreamCallBack(std::bind(
                                    &EditorManager::getContentFromOpenedEditor,this,
                                    std::placeholders::_1, std::placeholders::_2));
    e->setCanShowEvalTipFunc(std::bind(&EditorManager::debuggerReadyForEvalTip,this));
    e->setRequestEvalTipFunc(std::bind(&EditorManager::requestEvalTip,this,
                                       std::placeholders::_1, std::placeholders::_2));
    e->setEvalTipReadyCallback(std::bind(&EditorManager::onEditorTipEvalValueReady,
                                         this, std::placeholders::_1));
    e->setGetReformatterFunc(std::bind(&EditorManager::createReformatterForEditor,
                                       this, std::placeholders::_1));
    e->setGetMacroVarsFunc([window = QPointer<MainWindow>(pMainWindow)] {
        return window ? window->macroVariables() : QMap<QString, QString>{};
    });
    e->setGetCppParserFunc(std::bind(&EditorManager::createParserForEditor,
                                     this, std::placeholders::_1));
#ifdef ENABLE_SDCC
    e->setGetCompilerTypeForEditorFunc(std::bind(
                                           &EditorManager::getCompilerTypeForEditor,
                                           this, std::placeholders::_1));
#endif
    e->setCodeSnippetsManager(pMainWindow->codeSnippetManager());
    e->setFileSystemWatcher(pMainWindow->fileSystemWatcher());
    e->applySettings();
    e->setEditorEncoding(encoding);
    e->setFilename(filename);
    e->setInProject(inProject, false);
    e->setFileType(fileType, false);
    e->setContextFile(contextFile, false);
    if (!newFile) {
        e->loadFile(filename, false);
        e->checkSyntaxInBack();
        e->reparseTodo();
    }
    // A new-file template can request analysis while it is inserted. Register
    // the initialized buffer before attaching its parser or applying templates.
    observeDocument(e);
    e->setCppParser();
    //e->reparse();

    if (!newFile) {
        e->resetBookmarks(pMainWindow->bookmarkModel());
        e->resetBreakpoints(pMainWindow->debugger()->breakpointModel().get());
    }
    e->setStatementColors(pMainWindow->statementColors());
    if (newFile) {
        QString fileTemplate;
        switch (e->fileType()) {
        case FileType::CSource:
            fileTemplate = pMainWindow->codeSnippetManager()->newCFileTemplate();
            break;
        case FileType::CppSource:
            fileTemplate = pMainWindow->codeSnippetManager()->newCppFileTemplate();
            break;
        case FileType::ATTASM:
            fileTemplate = pMainWindow->codeSnippetManager()->newGASFileTemplate();
            break;
        default:
            break;
        }
        if (!fileTemplate.isEmpty()) {
            e->insertCodeSnippet(fileTemplate);
            e->setCaretPosition(e->fileBegin());
            e->setModified(false);
        }
    }
    e->setAutoBackupEnabled(true);
    if (fileExists(filename))
        pMainWindow->fileSystemWatcher()->addPath(filename);
    parentPageControl->addTab(e, e->caption());
    updateLayout();
    e->setFunctionTooltip(pMainWindow->functionTip());
    e->setCompletionPopup(pMainWindow->completionPopup());
    e->setHeaderCompletionPopup(pMainWindow->headerCompletionPopup());

    connect(e, &Editor::breakpointAdded, this, &EditorManager::onBreakpointAdded);
    connect(e, &Editor::breakpointRemoved, this, &EditorManager::onBreakpointRemoved);
    connect(e, &Editor::breakpointsCleared, this, &EditorManager::onBreakpointsCleared);
    connect(e, &Editor::showOccured, this, &EditorManager::onEditorShown);
    connect(e, &Editor::fileSaving, this, &EditorManager::onFileSaving);
    connect(e, &Editor::fileSaved, this, &EditorManager::onFileSaved);
    connect(e, &Editor::fileSaveAsed, this, &EditorManager::onFileSaveAsed);
    connect(e, &Editor::fileRenamed, this, &EditorManager::onFileRenamed);
    connect(e, &Editor::linesDeleted, this, &EditorManager::onEditorLinesRemoved);
    connect(e, &Editor::linesInserted, this, &EditorManager::onEditorLinesInserted);
    connect(e, &Editor::lineMoved, this, &EditorManager::onEditorLineMoved);
    connect(e, &Editor::statusChanged, this, &EditorManager::onEditorStatusChanged);
    connect(e, &Editor::fontSizeChangedByWheel, this, &EditorManager::onEditorFontSizeChangedByWheel);
    connect(e, &Editor::fileEncodingChanged, this, &EditorManager::onEditorFileEncodingChanged);

    connect(e, &Editor::syntaxCheckRequested, pMainWindow, &MainWindow::checkSyntaxInBack);
    connect(e, &Editor::parseTodoRequested, pMainWindow->todoParser().get(), &TodoParser::parseFile);
    connect(e, &Editor::fileEncodingChanged, pMainWindow, &MainWindow::updateForEncodingInfo);
    connect(e, &Editor::editorEncodingChanged, pMainWindow, &MainWindow::updateForEncodingInfo);
    connect(e, &Editor::focusInOccured, this, [this](Editor *editor) {
        mLastActivePageWidget = findPageControlForEditor(editor);
    });
    connect(e, &Editor::focusInOccured, pMainWindow, &MainWindow::refreshInfosForEditor);
    connect(e, &Editor::closeOccured, pMainWindow, &MainWindow::removeInfosForEditor);
    connect(e, &Editor::hideOccured, pMainWindow, &MainWindow::removeInfosForEditor);
    connect(e, &QWidget::customContextMenuRequested, pMainWindow, &MainWindow::onEditorContextMenu);
    connect(e, &Editor::openFileRequested, pMainWindow, &MainWindow::onOpenFileRequested);
    connect(e, &Editor::symbolChoosed, pMainWindow->symbolUsageManager(), &SymbolUsageManager::updateUsage);

    if (!pMainWindow->openingFiles()
            && !pMainWindow->openingProject()) {
        if (e->inProject()) {
            e->reparse();
            e->reparseTodo();
        }
        //checkSyntaxInBack();
    }
    return candidate.release();
}

QTabWidget*  EditorManager::getNewEditorPageControl() const {
    return getFocusedPageControl();
}

void EditorManager::observeDocument(Editor *editor)
{
    const QPointer<Editor> source(editor);
    const DocumentId id = mDocumentStore->add(editor->filename(), [source](QStringList &lines) {
        if (!source)
            return false;
        lines = source->content();
        return true;
    });
    if (!id)
        throw FileError(tr("The document could not be registered for analysis."));
    mDocumentIds.insert(editor, id);
    // Native commit precedes QSynEdit's changed/status notifications. During
    // changing(), captures fail explicitly instead of caching old text under
    // the revision of the edit that has not happened yet.
    connect(editor->document().get(), &QSynedit::Document::changeStarted, this,
            [this, id] { mDocumentStore->beginChange(id); });
    connect(editor->document().get(), &QSynedit::Document::contentCommitted, this,
            [this, id] { mDocumentStore->commitChange(id); });
    connect(editor, &Editor::filenameChanged, this,
            [this, id](Editor *, const QString &, const QString &filename) {
        mDocumentStore->renamed(id, filename);
    });
    connect(editor, &QObject::destroyed, this, [this, editor] { forgetDocument(editor); });
}

void EditorManager::forgetDocument(Editor *editor)
{
    const DocumentId id = mDocumentIds.take(editor);
    if (!id) return;
    mDocumentStore->remove(id);
    emit documentClosed(id);
}

DocumentId EditorManager::documentId(const Editor *editor) const
{
    if (QThread::currentThread() != thread() || !editor) return 0;
    return mDocumentIds.value(const_cast<Editor *>(editor));
}

DocumentRevision EditorManager::documentRevision(const Editor *editor) const
{
    if (QThread::currentThread() != thread()) return 0;
    return mDocumentStore->revisionFor(documentId(editor));
}

QString EditorManager::documentAbsolutePath(const Editor *editor) const
{
    if (QThread::currentThread() != thread()) return {};
    return mDocumentStore->absolutePathFor(documentId(editor));
}

quint64 EditorManager::documentInputGeneration() const
{
    return mDocumentStore->generation();
}

Editor *EditorManager::editorForDocument(DocumentId id) const
{
    if (QThread::currentThread() != thread() || !id) return nullptr;
    for (auto it = mDocumentIds.cbegin(); it != mDocumentIds.cend(); ++it)
        if (it.value() == id) return it.key();
    return nullptr;
}

InputSnapshotProvider EditorManager::inputSnapshotProvider() const
{
    const auto store = mDocumentStore;
    return [store] { return store->capture(); };
}

void EditorManager::bindParserInput(const PCppParser &parser)
{
    if (!parser)
        return;
    parser->setInputSnapshotProvider(inputSnapshotProvider());
    bool known = false;
    for (auto it = mKnownParsers.begin(); it != mKnownParsers.end();) {
        if (const auto item = it->lock()) {
            known |= item == parser;
            ++it;
        } else {
            it = mKnownParsers.erase(it);
        }
    }
    if (!known)
        mKnownParsers.append(parser);
}

QList<Editor *> EditorManager::editors() const
{
    QList<Editor *> result;
    if (QThread::currentThread() != thread())
        return result;
    for (QTabWidget *tabs : {mLeftPageWidget, mRightPageWidget}) {
        for (int index = 0; index < tabs->count(); ++index)
            if (auto *editor = qobject_cast<Editor *>(tabs->widget(index)))
                result.append(editor);
    }
    return result;
}

QList<PCppParser> EditorManager::allParsers() const
{
    QList<PCppParser> result;
    if (QThread::currentThread() != thread())
        return result;
    const auto add = [&result](const PCppParser &parser) {
        if (parser && !result.contains(parser))
            result.append(parser);
    };
    for (const auto &parser : mKnownParsers)
        add(parser.lock());
    for (const Editor *editor : editors())
        add(editor->parser());
    return result;
}

PDocumentSnapshot EditorManager::snapshotFor(Editor *editor)
{
    if (QThread::currentThread() != thread() || !editor)
        return {};
    const DocumentId id = mDocumentIds.value(editor);
    if (!id)
        return {};
    return mDocumentStore->snapshotFor(id);
}

QTabWidget* EditorManager::getFocusedPageControl() const {
    switch(mLayout) {
    case LayoutShowType::lstLeft:
        return mLeftPageWidget;
    case LayoutShowType::lstRight:
        return mRightPageWidget;
    case LayoutShowType::lstBoth: {
        Editor* rightEditor = dynamic_cast<Editor*>(mRightPageWidget->currentWidget());
        if (!rightEditor)
            return mLeftPageWidget;
        if (rightEditor->hasFocus())
            return mRightPageWidget;
        Editor *leftEditor = dynamic_cast<Editor*>(mLeftPageWidget->currentWidget());
        if (!leftEditor)
            return mRightPageWidget;
        if (leftEditor->hasFocus())
            return mLeftPageWidget;
        // Remember the pane rather than a particular tab's focus timestamp:
        // selecting an unfocused tab while using a tool panel must not make
        // commands and Structure jump to the opposite pane.
        if (mLastActivePageWidget == mRightPageWidget)
            return mRightPageWidget;
        if (mLastActivePageWidget == mLeftPageWidget)
            return mLeftPageWidget;
        if (rightEditor->lastFocusOutTime() > leftEditor->lastFocusOutTime())
            return mRightPageWidget;
        return mLeftPageWidget;
    }
    default:
        return nullptr;
    }
}

void EditorManager::showLayout(LayoutShowType layout)
{
    if (layout == mLayout)
        return;
    mLayout = layout;
    // Apply widths if layout does not change
    switch(mLayout) {
    case LayoutShowType::lstLeft:
        mLeftPageWidget->setVisible(true);
        mRightPageWidget->setVisible(false);
        break;
    case LayoutShowType::lstRight:
        mLeftPageWidget->setVisible(false);
        mRightPageWidget->setVisible(true);
        break;
    case LayoutShowType::lstBoth:
        mLeftPageWidget->setVisible(true);
        mRightPageWidget->setVisible(true);
        {
            QList<int> sizes=mSplitter->sizes();
            int total = sizes[0]+sizes[1];
            sizes[0] = total / 2;
            sizes[1] = total - sizes[0];
            mSplitter->setSizes(sizes);
        }
        break;
    }
}

void EditorManager::doRemoveEditor(Editor *e)
{
    const bool retireLater = mDocumentFiles->isSaving(documentId(e));
    forgetDocument(e);
    QTabWidget* parentPage=findPageControlForEditor(e);
    if (parentPage) {
        int index = parentPage->indexOf(e);
        parentPage->removeTab(index);
    }
    pMainWindow->fileSystemWatcher()->removePath(e->filename());
    pMainWindow->caretList().removeEditor(e);
    pMainWindow->updateCaretActions();
    if (retireLater) {
        // The save controller may still be dispatching an Editor signal or
        // unwinding a nested dialog. Remove its authority now and let Qt
        // destroy the retired view after the notification stack returns.
        e->hide();
        e->deleteLater();
    } else {
        e->setParent(nullptr);
        delete e;
    }
}

#ifdef ENABLE_SDCC
CompilerType EditorManager::getCompilerTypeForEditor(const Editor *e) const
{
    if (e) {
        PCompilerSet pSet;
        if (e->inProject() && pMainWindow->project()) {
            pSet = pSettings->compilerSets().getSet(pMainWindow->project()->options().compilerSet);
        } else if (!e->inProject()) {
            pSet = pSettings->compilerSets().defaultSet();
        }
        if (pSet)
            return pSet->compilerType();
    }
    return CompilerType::Unknown;
}
#endif

void EditorManager::updateEditorTabCaption(Editor* e)
{
    QTabWidget *parentWidget = mLeftPageWidget;
    int index = mLeftPageWidget->indexOf(e);
    if (index==-1) {
        index = mRightPageWidget->indexOf(e);
        parentWidget = mRightPageWidget;
    }
    if (index==-1)
        return;
    parentWidget->setTabText(index,e->caption());
    parentWidget->setTabToolTip(index, e->filename());
}

void EditorManager::onBreakpointAdded(const Editor *e, int line)
{
    pMainWindow->debugger()->addBreakpoint(line,e->filename(),e->inProject());
}

void EditorManager::onBreakpointRemoved(const Editor *e, int line)
{
    pMainWindow->debugger()->removeBreakpoint(line,e->filename(), e->inProject());
}

void EditorManager::onBreakpointsCleared(const Editor *e)
{
    pMainWindow->debugger()->deleteBreakpoints(e->filename(), e->inProject());
}

void EditorManager::onEditorShown(Editor *e)
{
    Q_ASSERT(e!=nullptr);
    if (e->parser() && !pMainWindow->isClosingAll()
            && !pMainWindow->isQuitting()) {
        if (!pMainWindow->openingFiles() && !pMainWindow->openingProject()) {
            if (pSettings->codeCompletion().clearWhenEditorHidden()
                && pSettings->codeCompletion().shareParser()
                && !e->inProject()) {
                if (e->needReparse()) {
                    const PCppParser parser = e->parser();
                    // A non-project header may use a project parser via its
                    // context source; clearing it would discard project state.
                    const auto project = pMainWindow->project();
                    if (project && parser == project->cppParser()) {
                        e->reparseIfNeeded();
                    } else {
                        QPointer<Editor> shown(e);
                        Editor *leftRaw = nullptr, *rightRaw = nullptr;
                        getVisibleEditors(leftRaw, rightRaw);
                        QPointer<Editor> left(leftRaw), right(rightRaw);
                        CppParser::resetParsedResultsNonBlocking(parser);
                        // Queue the selected and other visible file after the
                        // barrier, even when previously marked as scanned.
                        if (shown && shown->parser() == parser)
                            shown->reparse();
                        if (left && left.data() != shown.data() && left->parser() == parser)
                            left->reparse();
                        if (right && right.data() != shown.data() && right.data() != left.data()
                                && right->parser() == parser)
                            right->reparse();
                    }
                }
            } else
                e->reparseIfNeeded();
        }
    }
    pMainWindow->debugger()->setIsForProject(e->inProject());
    pMainWindow->bookmarkModel()->setIsForProject(e->inProject());
    pMainWindow->todoModel()->setIsForProject(e->inProject());

    if (!pMainWindow->isClosingAll()
                && !pMainWindow->isQuitting()
            && !pMainWindow->openingFiles()
            && !pMainWindow->openingProject()) {
        if (!e->inProject() || !pMainWindow->closingProject()) {
            e->checkSyntaxInBack();
            e->reparseTodo();
        }
    }
    if (e->inProject() && !pMainWindow->closingProject()) {
        pMainWindow->setProjectCurrentFile(e->filename());
    }
}

void EditorManager::onFileSaving(Editor *e, const QString &filename)
{
    Q_UNUSED(e);
    Q_UNUSED(filename);
}

void EditorManager::onFileSaved(Editor *e, const QString &filename)
{
    pMainWindow->onFileSaved(filename, e->inProject());
}

void EditorManager::onFileRenamed(Editor *e, const QString &oldFilename, const QString &newFilename)
{
    pMainWindow->getOJProblemSetModel()->updateProblemAnswerFilename(oldFilename, newFilename);
    if (!e->inProject()) {
        pMainWindow->bookmarkModel()->renameBookmarkFile(oldFilename,newFilename,false);
        pMainWindow->debugger()->breakpointModel()->renameBreakpointFilenames(oldFilename,newFilename,false);
    }
    updateEditorTabCaption(e);
}

void EditorManager::onFileSaveAsed(Editor *e, const QString &oldFilename, const QString &newFilename)
{
    pMainWindow->getOJProblemSetModel()->updateProblemAnswerFilename(oldFilename, newFilename);
    if (!e->inProject()) {
        pMainWindow->bookmarkModel()->renameBookmarkFile(oldFilename,newFilename,false);
        pMainWindow->debugger()->breakpointModel()->renameBreakpointFilenames(oldFilename,newFilename,false);
    }
}

void EditorManager::onFileSaveError(Editor *e, const QString& filename, const QString& reason)
{
    Q_UNUSED(e);
    Q_UNUSED(reason);
    Q_UNUSED(filename);
}

void EditorManager::onEditorLinesInserted(int startLine, int count)
{
    Editor * e = static_cast<Editor *>(sender());
    pMainWindow->caretList().onLinesInserted(e,startLine,count);
    pMainWindow->debugger()->breakpointModel()->onFileInsertLines(e->filename(), startLine,count, e->inProject());
    pMainWindow->bookmarkModel()->onFileInsertLines(e->filename(), startLine,count, e->inProject());
    e->resetBreakpoints(pMainWindow->debugger()->breakpointModel().get());
    e->resetBookmarks(pMainWindow->bookmarkModel());
}

void EditorManager::onEditorLinesRemoved(int startLine, int count)
{
    Editor * e = static_cast<Editor *>(sender());
    pMainWindow->caretList().onLinesDeleted(e,startLine,count);
    pMainWindow->debugger()->breakpointModel()->onFileDeleteLines(e->filename(),startLine,count,e->inProject());
    pMainWindow->bookmarkModel()->onFileDeleteLines(e->filename(),startLine,count,e->inProject());
    e->resetBreakpoints(pMainWindow->debugger()->breakpointModel().get());
    e->resetBookmarks(pMainWindow->bookmarkModel());
}

void EditorManager::onEditorLineMoved(int fromLine, int toLine)
{
    Editor * e = static_cast<Editor *>(sender());
    pMainWindow->caretList().onLinesMoved(e, fromLine, toLine);
    pMainWindow->debugger()->breakpointModel()->onFileLineMoved(e->filename(),fromLine,toLine,e->inProject());
    pMainWindow->bookmarkModel()->onFileDeleteLines(e->filename(),fromLine,toLine,e->inProject());

    e->resetBreakpoints(pMainWindow->debugger()->breakpointModel().get());
    e->resetBookmarks(pMainWindow->bookmarkModel());
}

void EditorManager::onEditorStatusChanged(QSynedit::StatusChanges changes)
{
    Editor *e = static_cast<Editor *>(sender());
    if (changes.testFlag(QSynedit::StatusChange::CaretX)
            || changes.testFlag(QSynedit::StatusChange::CaretY)) {
        pMainWindow->updateStatusbarForLineCol(e);
    }
    if (changes.testFlag(QSynedit::StatusChange::InsertMode) || changes.testFlag(QSynedit::StatusChange::ReadOnlyChanged)) {
        pMainWindow->updateForStatusbarModeInfo(e);
    }
    if (changes.testFlag(QSynedit::StatusChange::ModifyChanged)
            || changes.testFlag(QSynedit::StatusChange::ReadOnlyChanged)
            || changes.testFlag(QSynedit::StatusChange::Custom0)) {
        updateEditorTabCaption(e);
    }
    if (changes.testFlag(QSynedit::StatusChange::ModifyChanged)
        || changes.testFlag(QSynedit::StatusChange::Modified)
        || changes.testFlag(QSynedit::StatusChange::Selection)
        || changes.testFlag(QSynedit::StatusChange::ReadOnlyChanged)
            || changes.testFlag(QSynedit::StatusChange::Custom0)) {
        pMainWindow->updateEditorActions(e);
    }

    if (changes.testFlag(QSynedit::StatusChange::CaretY)) {
        pMainWindow->caretList().addCaret(e,e->caretY(),e->caretX());
        pMainWindow->updateCaretActions();
    }
}

void EditorManager::onEditorFontSizeChangedByWheel(int newSize)
{
    pSettings->editor().setFontSize(newSize);
    pSettings->editor().save();
    pMainWindow->updateEditorSettings();
}

void EditorManager::onEditorFileEncodingChanged(Editor *e)
{
    const DocumentId id = documentId(e);
    if (id) emit documentEncodingChanged(id, e->fileEncoding());
}

QTabWidget *EditorManager::rightPageWidget() const
{
    return mRightPageWidget;
}

PCppParser EditorManager::sharedParser(QHash<ParserLanguage,std::weak_ptr<CppParser>> & sharedParsers, ParserLanguage language)
{
    PCppParser parser;
    if (sharedParsers.contains(language)) {
        parser=sharedParsers[language].lock();
    }
    if (!parser) {
        parser = std::make_shared<CppParser>();
        parser->setSharedByFiles(true);
        bindParserInput(parser);
        Analysis::ParserConfigurationValues configuration; configuration.language = language;
        connectParserUi(parser);
        const auto submitted = configureCppParser(parser, -1, std::move(configuration), QDir::currentPath());
        Q_UNUSED(submitted)
        sharedParsers.insert(language,parser);
    }
    return parser;
}

void EditorManager::resetSharedParsers(QHash<ParserLanguage, std::weak_ptr<CppParser> > &sharedParsers)
{
    foreach(std::weak_ptr<CppParser> parser, sharedParsers) {
        resetCppParser(parser.lock());
    }
}


PCppParser EditorManager::sharedParser(ParserLanguage language, const QTabWidget *widget)
{
    if (widget == mLeftPageWidget)
        return sharedParser(mSharedParsersForLeft, language);
    else if (widget == mRightPageWidget)
        return sharedParser(mSharedParsersForRight, language);
    else
        return sharedParser(mSharedParsers, language);
}

void EditorManager::resetSharedParsers()
{
    resetSharedParsers(mSharedParsers);
    resetSharedParsers(mSharedParsersForLeft);
    resetSharedParsers(mSharedParsersForRight);
}


PCppParser EditorManager::createParserForEditor(Editor *editor)
{
    Q_ASSERT(editor!=nullptr);
    Q_ASSERT(editor->syntaxer()!=nullptr);
    if (editor->syntaxer()->language() != QSynedit::ProgrammingLanguage::CPP)
        return nullptr;
    if (pMainWindow->project() && pMainWindow->project()->inProject(editor)) {
        return pMainWindow->project()->cppParser();
    }
    if (editor->codeCompletionEnabled()) {
        if (!editor->contextFile().isEmpty())  {
            Editor * e = getOpenedEditor(editor->contextFile());
            if (e)
                return e->parser();
        }
        if (pSettings->codeCompletion().shareParser()) {
            return sharedParser(editor->calcParserLanguage(), findPageControlForEditor(editor));
        } else if (editor->syntaxer()->language() == QSynedit::ProgrammingLanguage::CPP) {
            PCppParser parser = std::make_shared<CppParser>();
            parser->setSharedByFiles(false);
            bindParserInput(parser);
            Analysis::ParserConfigurationValues configuration; configuration.language = editor->calcParserLanguage();
            connectParserUi(parser);
            const auto submitted = configureCppParser(parser, -1, std::move(configuration), QDir::currentPath());
            Q_UNUSED(submitted)
            return parser;
        }
    }
    return nullptr;
}

std::unique_ptr<BaseReformatter> EditorManager::createReformatterForEditor(Editor *)
{
    const QString &astyle = pSettings->environment().AStylePath();
    QStringList args = pSettings->codeFormatter().getArguments();
    return std::make_unique<AStyleReformatter>(astyle,args,
                                               std::bind(&MainWindow::logToolsOutput, pMainWindow, std::placeholders::_1));
}

QTabWidget *EditorManager::leftPageWidget() const
{
    return mLeftPageWidget;
}

Editor* EditorManager::getEditor(int index, QTabWidget* tabsWidget) const {
    QTabWidget* selectedWidget;
    if (tabsWidget == nullptr) {
        selectedWidget = getFocusedPageControl();
    } else {
        selectedWidget = tabsWidget;
    }
    if (!selectedWidget)
        return nullptr;
    if (index == -1) {
        index = selectedWidget->currentIndex();
    }
    if (index<0 || index >= selectedWidget->count()) {
        return nullptr;
    }
    return (Editor*)selectedWidget->widget(index);
}

bool EditorManager::closeEditor(Editor* editor, bool transferFocus, bool force, EditorCloseDecision decision) {
    if (QThread::currentThread() != thread() || !editor || !findPageControlForEditor(editor))
        return false;
    const QPointer<Editor> target(editor);
    if (force) {
        if (!editor->save(true,false) || !target || target->modified())
            return false;
    } else if (editor->modified() && decision == EditorCloseDecision::Ask) {
        const QString filename = editor->filename();
        const QStringList questionedContent = editor->content();
        // ask user if he wants to save
        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(editor,QObject::tr("Save"),
                                      QString(QObject::tr("Save changes to %1?")).arg(editor->filename()),
                                      QMessageBox::Yes|QMessageBox::No|QMessageBox::Cancel);
        if (!target || !findPageControlForEditor(target) || target->filename() != filename)
            return false;
        if (reply == QMessageBox::Yes) {
            if (!target->save(false,false) || !target || target->modified())
                return false;
        } else if (reply != QMessageBox::No) {
            // Only an explicit discard choice may close an unsaved buffer.
            return false;
        } else if (target->content() != questionedContent) {
            // The answer did not authorize discarding a different revision
            // introduced by a reentrant completion or external-file event.
            return false;
        }
    }

    if (!target || !findPageControlForEditor(target))
        return false;
    editor = target;

    beginUpdate();
    const auto finishUpdate = finally([this] { endUpdate(); });
//    if (transferFocus && (editor->pageControl()->currentWidget()==editor)) {
//        //todo: activate & focus the previous editor
//    }

    if (editor->inProject() && pMainWindow->project()) {
        if (fileExists(pMainWindow->project()->directory())) {
            PProjectUnit unit = pMainWindow->project()->findUnit(editor);
            pMainWindow->project()->closeUnit(unit);
        } else {
            editor->setInProject(false,false);
        }
    } else {
        QMutexLocker locker(&mMutex);
        if (!target || !findPageControlForEditor(target))
            return false;
        editor->setCppParser(nullptr);
        if (!editor->isNew() && pMainWindow->visitHistoryManager()->addFile(editor->filename())) {
            pMainWindow->rebuildOpenedFileHisotryMenu();
        }
        editor->clearBreakpoints();
        doRemoveEditor(editor);
    }
    if (target && findPageControlForEditor(target))
        return false;
    updateLayout();
    if (!force && transferFocus) {
        editor = getEditor();
        if (editor) {
            activeEditor(editor,true);
        }
    }
    if (!force) {
        editor = getEditor();
        if (!editor)
            pMainWindow->updateClassBrowserForEditor(nullptr);
    }
    emit editorClosed();
    return true;
}

bool EditorManager::swapEditor(Editor *editor)
{
    QMutexLocker locker(&mMutex);
    Q_ASSERT(editor!=nullptr);
    beginUpdate();
    auto action = finally([this](){
        endUpdate();
    });
    //remember old index
    QTabWidget* fromPageControl = findPageControlForEditor(editor);
    if (!editor->inProject() && pSettings->codeCompletion().shareParser()) {
        // setCppParser would invalidate file
        editor->setCppParser(nullptr);
    }
    if (fromPageControl == mLeftPageWidget) {
        mLeftPageWidget->removeTab(mLeftPageWidget->indexOf(editor));
        mRightPageWidget->addTab(editor, editor->caption());
    } else {
        mRightPageWidget->removeTab(mRightPageWidget->indexOf(editor));
        mLeftPageWidget->addTab(editor, editor->caption());
    }
    if (!editor->inProject() && pSettings->codeCompletion().shareParser()) {
        editor->setCppParser();
        editor->reparseIfNeeded();
    }
    updateLayout();
    activeEditor(editor, true);
    return true;
}

void EditorManager::saveAll()
{
    for (int i=0;i<pageCount();i++) {
        Editor * e= (*this)[i];
        if (e->modified())
            e->save();
    }
}

bool EditorManager::saveAllForProject()
{
    for (int i=0;i<pageCount();i++) {
        Editor * e= (*this)[i];
        if (e->modified() && e->inProject()) {
            if (!e->save())
                return false;
        }
    }
    return true;
}

bool EditorManager::projectEditorsModified()
{
    for (int i=0;i<pageCount();i++) {
        Editor * e= (*this)[i];
        if (e->modified() && e->inProject()) {
            return true;
        }
    }
    return false;
}

void EditorManager::clearProjectEditorsModified()
{
    for (int i=0;i<pageCount();i++) {
        Editor * e= (*this)[i];
        if (e->inProject()) {
            e->setModified(false);
        }
    }
}

void EditorManager::beginUpdate() {
    if (mUpdateCount==0) {
        mPanel->setUpdatesEnabled(false);
    }
    mUpdateCount++;
}

void EditorManager::endUpdate() {
    mUpdateCount--;
    if (mUpdateCount==0) {
        mPanel->setUpdatesEnabled(true);
        mPanel->update();
    }
}

void EditorManager::applySettings()
{
    for (int i=0;i<mLeftPageWidget->count();i++) {
        Editor* e = static_cast<Editor*>(mLeftPageWidget->widget(i));
        e->applySettings();
    }
    for (int i=0;i<mRightPageWidget->count();i++) {
        Editor* e = static_cast<Editor*>(mRightPageWidget->widget(i));
        e->applySettings();
    }
}

void EditorManager::applyColorSchemes(const QString& name)
{
    for (int i=0;i<mLeftPageWidget->count();i++) {
        Editor* e = static_cast<Editor*>(mLeftPageWidget->widget(i));
        e->applyColorScheme(name);
    }
    for (int i=0;i<mRightPageWidget->count();i++) {
        Editor* e = static_cast<Editor*>(mRightPageWidget->widget(i));
        e->applyColorScheme(name);
    }
}

bool EditorManager::isFileOpened(const QString &fullfilepath) const
{
    return resolveOpenedEditor(fullfilepath).status != EditorPathStatus::NotOpen;
}

bool EditorManager::hasFilename(const QString &filename) const
{
    for (int i=0;i<mLeftPageWidget->count();i++) {
        Editor* e = static_cast<Editor*>(mLeftPageWidget->widget(i));
        QFileInfo fileInfo(e->filename());
        QString name = fileInfo.fileName();
        if (NativePathKey(name) == NativePathKey(filename))
            return true;
    }
    for (int i=0;i<mRightPageWidget->count();i++) {
        Editor* e = static_cast<Editor*>(mRightPageWidget->widget(i));
        QFileInfo fileInfo(e->filename());
        QString name = fileInfo.fileName();
        if (NativePathKey(name) == NativePathKey(filename))
            return true;
    }
    return false;
}

int EditorManager::pageCount() const
{
    return mLeftPageWidget->count()+mRightPageWidget->count();
}

void EditorManager::selectNextPage()
{
    QTabWidget * pageControl = getFocusedPageControl();
    if (pageControl && pageControl->count()>0) {
        pageControl->setCurrentIndex(
                    (pageControl->currentIndex()+1) % pageControl->count()
                    );
    }
}

void EditorManager::selectPreviousPage()
{
    QTabWidget * pageControl = getFocusedPageControl();
    if (pageControl && pageControl->count()>0) {
        pageControl->setCurrentIndex(
                    (pageControl->currentIndex()+pageControl->count()-1) % pageControl->count()
                    );
    }
}

void EditorManager::showActiveEditorCaret()
{
    Editor *editor = getEditor();
    if (editor)
        editor->showCaret();
}

void EditorManager::activeEditor(Editor *e, bool focus)
{
    if (e==nullptr)
        return;
    QTabWidget * pageControl = findPageControlForEditor(e);
    if (pageControl!=nullptr) {
        if (focus)
            mLastActivePageWidget = pageControl;
        pageControl->setCurrentWidget(e);
        if (focus)
            e->setFocus();
    }
}

void EditorManager::activeEditorAndSetCaret(Editor *e, QSynedit::CharPos pos)
{
    if (e) {
        e->setCaretPosition(pos);
        activeEditor(e,true);
    }
}

Editor *EditorManager::operator[](int index)
{
    if (index>=0 && index<mLeftPageWidget->count()) {
        return static_cast<Editor*>(mLeftPageWidget->widget(index));
    }
    index -= mLeftPageWidget->count();
    if (index>=0 && index<mRightPageWidget->count()) {
        return static_cast<Editor*>(mRightPageWidget->widget(index));
    }
    return nullptr;
}

bool EditorManager::closeAll(bool force) {
//    beginUpdate();
//    auto end = finally([this] {
//        this->endUpdate();
//    });
    while (mLeftPageWidget->count()>0) {
        Editor* e = getEditor(0,mLeftPageWidget);
        if (!closeEditor(e,false,force)) {
            return false;
        }
    }
    while (mRightPageWidget->count()>0) {
        Editor* e = getEditor(0,mRightPageWidget);
        if (!closeEditor(e,false,force)) {
            return false;
        }
    }
    return true;
}

bool EditorManager::closeOthers(Editor *editor)
{
    QList<Editor*> editors;
    for (int i=0;i<mLeftPageWidget->count();i++) {
        editors.append(static_cast<Editor*>(mLeftPageWidget->widget(i)));
    }
    for (int i=0;i<mRightPageWidget->count();i++) {
        editors.append(static_cast<Editor*>(mRightPageWidget->widget(i)));
    }
    for (Editor* e: editors ) {
        if (e!=editor) {
            if (!closeEditor(e,false,false)) {
                return false;
            }
        }
    }
    return true;
}

void EditorManager::forceCloseEditor(Editor *editor)
{
    QMutexLocker locker(&mMutex);
    beginUpdate();
    doRemoveEditor(editor);
    // Force layout update when creating, destroying or moving editors
    updateLayout();
    endUpdate();
    emit editorClosed();
}

Editor* EditorManager::getOpenedEditor(const QString &filename) const
{
    return resolveOpenedEditor(filename).editor;
}

EditorPathResult EditorManager::resolveOpenedEditor(const QString &filename) const
{
    if (QThread::currentThread() != thread())
        return {EditorPathStatus::Conflict, nullptr, tr("Open documents must be resolved on their owner thread.")};
    if (filename.isEmpty()) return {};
    const NativePathKey key(filename);
    if (!key.valid()) return {EditorPathStatus::Conflict, nullptr, tr("Invalid document path.")};
    Editor *exact = nullptr;
    Editor *alias = nullptr;
    bool conflicting = false;
    for (auto it = mDocumentIds.cbegin(); it != mDocumentIds.cend(); ++it) {
        auto *editor = const_cast<Editor *>(it.key());
        const NativePathKey registered(editor->filename());
        const QString absolute = mDocumentStore->absolutePathFor(it.value());
        const NativePathKey stableAbsolute(absolute);
        if (key.spelling() == registered.spelling() || key.spelling() == stableAbsolute.spelling()) {
            if (exact && exact != editor) conflicting = true;
            exact = editor;
            continue;
        }
        if (!(key == registered) && !(key == stableAbsolute)) continue;
        const auto match = QDir::isAbsolutePath(key.spelling())
                ? sameExistingFile(key.spelling(), absolute) : ExistingFileMatch::Unknown;
        if (match == ExistingFileMatch::Same) {
            if (alias && alias != editor) conflicting = true;
            alias = editor;
        } else conflicting = true;
    }
    if (exact && !conflicting) return {EditorPathStatus::Found, exact, {}};
    if (conflicting || (exact && alias && exact != alias))
        return {EditorPathStatus::Conflict, nullptr,
            tr("The requested path conflicts with an open document and its identity could not be uniquely verified: '%1'.").arg(filename)};
    if (alias) return {EditorPathStatus::Found, alias, {}};
    return {};
}

bool EditorManager::getContentFromOpenedEditor(const QString &filename, QStringList &buffer) const
{
    // Compatibility for GUI-only consumers. Analysis jobs use DocumentStore
    // snapshots and never enter this widget lookup from a worker.
    if (QThread::currentThread() != thread())
        return false;
    if(mMutex.tryLock(0)){
        auto action = finally([this](){
            mMutex.unlock();
        });
        if (pMainWindow->isQuitting())
            return false;
        Editor * e= getOpenedEditor(filename);
        if (!e)
            return false;
        buffer = e->content();
        return true;
    } else
        return false;
}

void EditorManager::getVisibleEditors(Editor *&left, Editor *&right) const
{
    switch(mLayout) {
    case LayoutShowType::lstLeft:
        left = getEditor(-1,mLeftPageWidget);
        right = nullptr;
        break;
    case LayoutShowType::lstRight:
        left = nullptr;
        right = getEditor(-1,mRightPageWidget);
        break;
    case LayoutShowType::lstBoth:
        left = getEditor(-1,mLeftPageWidget);
        right = getEditor(-1,mRightPageWidget);
        break;
    }
}

void EditorManager::updateLayout()
{
    if (mRightPageWidget->count() == 0)
        showLayout(LayoutShowType::lstLeft);
    else if (mLeftPageWidget->count() ==0)
        showLayout(LayoutShowType::lstRight);
    else
        showLayout(LayoutShowType::lstBoth);
}

MainWindow *EditorManager::mainWindow()
{
    return qobject_cast<MainWindow *>(parent());
}

QTabWidget *EditorManager::findPageControlForEditor(Editor *e)
{
    if (mLeftPageWidget->indexOf(e)!=-1)
        return mLeftPageWidget;
    if (mRightPageWidget->indexOf(e)!=-1)
        return mRightPageWidget;
    return nullptr;
}

void EditorManager::updateEditorBookmarks()
{
    for (int i=0;i<mLeftPageWidget->count();i++) {
        Editor * e = static_cast<Editor*>(mLeftPageWidget->widget(i));
        e->resetBookmarks(pMainWindow->bookmarkModel());
    }
    for (int i=0;i<mRightPageWidget->count();i++) {
        Editor * e = static_cast<Editor*>(mRightPageWidget->widget(i));
        e->resetBookmarks(pMainWindow->bookmarkModel());
    }
}

void EditorManager::updateEditorBreakpoints()
{
    for (int i=0;i<mLeftPageWidget->count();i++) {
        Editor * e = static_cast<Editor*>(mLeftPageWidget->widget(i));
        e->resetBreakpoints(pMainWindow->debugger()->breakpointModel().get());
    }
    for (int i=0;i<mRightPageWidget->count();i++) {
        Editor * e = static_cast<Editor*>(mRightPageWidget->widget(i));
        e->resetBreakpoints(pMainWindow->debugger()->breakpointModel().get());
    }
}

bool EditorManager::debuggerReadyForEvalTip()
{
    return (pMainWindow->debugger()->executing() && !pMainWindow->debugger()->inferiorRunning());
}

bool EditorManager::requestEvalTip(Editor *e, const QString &s)
{
    if (pMainWindow->debugger()->commandRunning())
        return false;
    connect(pMainWindow->debugger(), &Debugger::evalValueReady,
               e, &Editor::onTipEvalValueReady);
    pMainWindow->debugger()->evalExpression(s);
    return true;
}

void EditorManager::onEditorTipEvalValueReady(Editor *e)
{
    disconnect(pMainWindow->debugger(), &Debugger::evalValueReady,
               e, &Editor::onTipEvalValueReady);
}
