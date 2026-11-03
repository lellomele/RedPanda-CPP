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
#ifndef EDITORMANAGER_H
#define EDITORMANAGER_H

#include <QTabWidget>
#include <QSplitter>
#include <QWidget>
#include <QRecursiveMutex>
#include <QPointer>
#include "utils.h"
#include "editor.h"
#include "documentstore.h"
#include "unitlocationplan.h"
#include "reformatter/astyleformatter.h"

class MainWindow;
class Project;
class ProjectUnit;
class DocumentFileController;
enum class EditorPathStatus { Found, NotOpen, Conflict };
struct EditorPathResult {
    EditorPathStatus status = EditorPathStatus::NotOpen;
    Editor *editor = nullptr;
    QString error;
};
enum class EditorCloseDecision { Ask, DiscardConfirmed };
class EditorManager : public QObject
{
    Q_OBJECT
public:
    enum class LayoutShowType{
        lstLeft,
        lstRight,
        lstBoth
    };

    explicit EditorManager(QTabWidget* leftPageWidget,
                        QTabWidget* rightPageWidget,
                        QSplitter* splitter,
                        QWidget* panel, QObject* parent = nullptr);
    ~EditorManager() override;

    Editor* newEditor(const QString& filename, const QByteArray& encoding,
                      FileType fileType, const QString& contextFile,
                     bool inProject, bool newFile,
                     QTabWidget* page=nullptr);

    Editor* getEditor(int index=-1, QTabWidget* tabsWidget=nullptr) const;

    bool closeEditor(Editor* editor, bool transferFocus=true, bool force=false,
                     EditorCloseDecision decision=EditorCloseDecision::Ask);

    // GUI-owned inventory: selection/focus never changes the resource set.
    QList<Editor *> editors() const;
    QList<PCppParser> allParsers() const;
    void bindParserInput(const PCppParser &parser);
    InputSnapshotProvider inputSnapshotProvider() const;
    PDocumentSnapshot snapshotFor(Editor *editor);
    DocumentId documentId(const Editor *editor) const;
    DocumentRevision documentRevision(const Editor *editor) const;
    QString documentAbsolutePath(const Editor *editor) const;
    quint64 documentInputGeneration() const;
    Editor *editorForDocument(DocumentId id) const;
    DocumentSaveAsResult saveDocument(Editor *editor, const DocumentSaveRequest &request);
    UnitLocationCommitResult relinkProjectUnit(const std::shared_ptr<Project> &project,
                                               const std::shared_ptr<ProjectUnit> &unit, const QString &name = QString());

    bool swapEditor(Editor* editor);
    void activeEditor(Editor *e, bool focus);
    void activeEditorAndSetCaret(Editor *e, QSynedit::CharPos pos);

    void saveAll();
    bool saveAllForProject();

    bool projectEditorsModified();
    void clearProjectEditorsModified();

    bool closeAll(bool force = false);
    bool closeOthers(Editor* editor);

    void forceCloseEditor(Editor* editor);

    Editor* getOpenedEditor(const QString &filename) const;
    EditorPathResult resolveOpenedEditor(const QString &filename) const;

    bool getContentFromOpenedEditor(const QString& filename, QStringList& buffer) const;

    void getVisibleEditors(Editor*& left, Editor*& right) const;
    void updateLayout();

    MainWindow * mainWindow();


    QTabWidget *findPageControlForEditor(Editor *e);

    void updateEditorBookmarks();
    void updateEditorBreakpoints();

    bool debuggerReadyForEvalTip();
    bool requestEvalTip(Editor *e, const QString& s);
    void onEditorTipEvalValueReady(Editor *e);

    void beginUpdate();
    void endUpdate();
    void applySettings();
    void applyColorSchemes(const QString& name);
    bool isFileOpened(const QString& fullfilepath) const;
    bool hasFilename(const QString& filename) const;
    int pageCount() const;
    void selectNextPage();
    void selectPreviousPage();
    void showActiveEditorCaret();

    Editor* operator[](int index);

    QTabWidget *leftPageWidget() const;

    QTabWidget *rightPageWidget() const;

    PCppParser sharedParser(ParserLanguage language, const QTabWidget *widget);
    void resetSharedParsers();

    PCppParser createParserForEditor(Editor *editor);

    std::unique_ptr<BaseReformatter> createReformatterForEditor(Editor *);

signals:
    void documentClosed(DocumentId id);
    void documentEncodingChanged(DocumentId id, const QByteArray &encoding);
    void editorClosed();
    void editorOpenned();
private:
    PCppParser sharedParser(QHash<ParserLanguage,std::weak_ptr<CppParser>> & sharedParsers, ParserLanguage language);
    void resetSharedParsers(QHash<ParserLanguage,std::weak_ptr<CppParser>> & sharedParsers);

    QTabWidget* getNewEditorPageControl() const;
    QTabWidget* getFocusedPageControl() const;
    void showLayout(LayoutShowType layout);
    void doRemoveEditor(Editor* e);
    void observeDocument(Editor *editor);
    void forgetDocument(Editor *editor);
#ifdef ENABLE_SDCC
    CompilerType getCompilerTypeForEditor(const Editor *e) const;
#endif
private slots:
    void updateEditorTabCaption(Editor* e);
    void onBreakpointAdded(const Editor* e, int line);
    void onBreakpointRemoved(const Editor* e, int line);
    void onBreakpointsCleared(const Editor* e);
    void onEditorShown(Editor *e);
    void onFileSaving(Editor *e, const QString& filename);
    void onFileSaved(Editor *e, const QString& filename);
    void onFileRenamed(Editor *e, const QString &oldFilename, const QString &newFilename);
    void onFileSaveAsed(Editor *e, const QString &oldFilename, const QString &newFilename);
    void onFileSaveError(Editor *e, const QString& filename, const QString& reason);
    void onEditorLinesInserted(int startLine, int count);
    void onEditorLinesRemoved(int startLine, int count);
    void onEditorLineMoved(int fromLine, int toLine);
    void onEditorStatusChanged(QSynedit::StatusChanges changes);
    void onEditorFontSizeChangedByWheel(int newSize);
    void onEditorFileEncodingChanged(Editor *e);
private:
    LayoutShowType mLayout;
    QTabWidget *mLeftPageWidget;
    QTabWidget *mRightPageWidget;
    QPointer<QTabWidget> mLastActivePageWidget;
    QSplitter *mSplitter;
    QWidget *mPanel;
    int mUpdateCount;
    QHash<ParserLanguage,std::weak_ptr<CppParser>> mSharedParsers;
    QHash<ParserLanguage,std::weak_ptr<CppParser>> mSharedParsersForRight;
    QHash<ParserLanguage,std::weak_ptr<CppParser>> mSharedParsersForLeft;
    mutable QRecursiveMutex mMutex;
    std::shared_ptr<DocumentStore> mDocumentStore = std::make_shared<DocumentStore>();
    QHash<Editor *, DocumentId> mDocumentIds;
    std::unique_ptr<DocumentFileController> mDocumentFiles;
    QList<std::weak_ptr<CppParser>> mKnownParsers;
};

#endif // EDITORLIST_H
