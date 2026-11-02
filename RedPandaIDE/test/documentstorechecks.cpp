// SPDX-License-Identifier: GPL-3.0-or-later
#include "documentstorechecks.h"
#include "src/mainwindow.h"
#include "src/editormanager.h"
#include "src/editor.h"
#include "src/parser/cppparser.h"
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMimeData>
#include <QPointer>
#include <QTemporaryDir>
#include <qsynedit/document.h>

int runDocumentStoreChecks(MainWindow &window, const QString &outputDirectory)
{
    QDir().mkpath(outputDirectory);
    QTemporaryDir temporary(QDir(outputDirectory).filePath("document-store-XXXXXX"));
    if (!temporary.isValid())
        return 2;
    QJsonArray checks;
    int failures = 0;
    const auto check = [&](const char *name, bool passed) {
        checks.append(QJsonObject{{"name", name}, {"passed", passed}});
        if (!passed) ++failures;
    };
    auto *manager = window.editorManager();
    const auto provider = manager->inputSnapshotProvider();
    QList<QPointer<Editor>> owned;
    QList<QMetaObject::Connection> observers;
    const auto cleanup = finally([&] {
        for (const auto &connection : observers)
            QObject::disconnect(connection);
        for (const auto &editor : owned)
            if (editor) manager->forceCloseEditor(editor);
    });
    const auto create = [&](const QString &filename, bool newFile = true, QTabWidget *pane = nullptr) {
        auto *editor = manager->newEditor(filename, ENCODING_UTF8, FileType::CppSource, {}, false, newFile, pane);
        if (editor) {
            owned.append(editor);
            editor->setAutoBackupEnabled(false);
            editor->setGetCppParserFunc([](Editor *) { return PCppParser{}; });
            editor->setCppParser(nullptr); // These checks control acceptance explicitly.
        }
        return editor;
    };
    const auto sourceMatches = [&](Editor *editor) {
        const auto document = manager->snapshotFor(editor);
        const auto captured = provider();
        const auto source = captured.succeeded() ? captured.snapshot->document(editor->filename()) : PDocumentSnapshot{};
        return document && source && source->id == document->id && source->revision == document->revision
            && document->filename == editor->filename() && document->lines == editor->content()
            && source->lines == editor->content();
    };
    const auto write = [](const QString &filename, const QByteArray &bytes) {
        QFile file(filename);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.flush();
    };
    const auto probe = std::make_shared<CppParser>();
    probe->setEnabled(true);
    manager->bindParserInput(probe);
    auto *editor = create(temporary.filePath("changes.cpp"));
    if (!editor)
        return 3;
    check("A new editor is registered with its initialized template", sourceMatches(editor));
    editor->setContent("int typed_value = ;");
    const auto initial = manager->snapshotFor(editor);
    bool changingRejected = true, nativeChanging = true, committedConsistent = true, statusConsistent = true;
    int changingCalls = 0, committedCalls = 0, statusCalls = 0;
    CppParser::ParseResult inFlightJob = CppParser::ParseResult::NotParsed;
    observers.append(QObject::connect(editor->document().get(), &QSynedit::Document::changing, editor, [&] {
        ++changingCalls;
        nativeChanging &= editor->document()->updateInProgress();
        const auto captured = provider();
        changingRejected &= !manager->snapshotFor(editor) && !captured.succeeded()
            && captured.status == InputCaptureStatus::DocumentChanging;
        if (changingCalls == 1)
            inFlightJob = CppParser::parseFileBlocking(probe, editor->filename(), false, {});
    }));
    observers.append(QObject::connect(editor->document().get(), &QSynedit::Document::contentCommitted, editor, [&] {
        ++committedCalls;
        committedConsistent &= !editor->document()->updateInProgress() && sourceMatches(editor);
    }));
    observers.append(QObject::connect(editor, &Editor::statusChanged, editor, [&] {
        ++statusCalls;
        statusConsistent &= sourceMatches(editor);
    }));
    editor->setCaretXY({int(editor->lineText().size()) - 1, 0});
    QKeyEvent key(QEvent::KeyPress, Qt::Key_1, Qt::NoModifier, "1");
    QApplication::sendEvent(editor, &key);
    const auto typed = manager->snapshotFor(editor);
    check("Typing publishes complete text with a newer committed revision", typed && initial
          && typed->id == initial->id && typed->revision > initial->revision
          && typed->lines == QStringList{"int typed_value = 1;"} && sourceMatches(editor));
    check("Analysis rejects a capture inside changing before reading the old buffer", changingCalls > 0
          && changingRejected && nativeChanging && inFlightJob == CppParser::ParseResult::InputUnavailable);
    check("An actual parser job reads the committed open editor", CppParser::parseFileBlocking(probe, editor->filename(), false, {})
          == CppParser::ParseResult::Parsed && bool(probe->findStatement("typed_value")));

    editor->setCaretXY(editor->fileEnd());
    const auto beforePaste = manager->snapshotFor(editor);
    auto previousClipboard = std::make_unique<QMimeData>();
    if (const QMimeData *previous = QApplication::clipboard()->mimeData())
        for (const QString &format : previous->formats())
            previousClipboard->setData(format, previous->data(format));
    const auto restoreClipboard = finally([&] { QApplication::clipboard()->setMimeData(previousClipboard.release()); });
    QApplication::clipboard()->setText("\nint pasted_value = 2;\nstruct Pasted { int field; };");
    editor->pasteFromClipboard();
    const auto pasted = manager->snapshotFor(editor);
    check("Multiline paste snapshots the final buffer", pasted && beforePaste && pasted->revision > beforePaste->revision
          && pasted->lines.join('\n').contains("pasted_value") && pasted->lines.size() == 3 && sourceMatches(editor));
    editor->undo();
    const auto undone = manager->snapshotFor(editor);
    check("Undo publishes restored text at a new revision", undone && pasted && beforePaste
          && undone->lines == beforePaste->lines && undone->revision > pasted->revision && sourceMatches(editor));
    editor->redo();
    const auto redone = manager->snapshotFor(editor);
    check("Redo publishes reapplied text at a new revision", redone && undone && pasted
          && redone->lines == pasted->lines && redone->revision > undone->revision && sourceMatches(editor));
    editor->selectAll();
    editor->setSelText("struct Replaced { int member; };\nint replacement_value = 3;");
    check("Multiline replacement reaches both snapshot consumers", sourceMatches(editor)
          && CppParser::parseFileBlocking(probe, editor->filename(), false, {}) == CppParser::ParseResult::Parsed
          && bool(probe->findStatement("replacement_value")) && !probe->findStatement("typed_value"));
    check("Commit notifications precede every status-driven capture", committedCalls > 0 && statusCalls > 0
          && committedConsistent && statusConsistent);

    check("Disk fixture exists for the empty open-buffer test", write(editor->filename(), "int stale_disk_value;\n"));
    editor->selectAll();
    editor->setSelText("");
    const auto empty = provider();
    QStringList emptyLines;
    check("An empty open buffer remains present and never falls back to disk", empty.succeeded()
          && empty.snapshot->read(editor->filename(), emptyLines) && emptyLines.join('\n').isEmpty()
          && sourceMatches(editor) && CppParser::parseFileBlocking(probe, editor->filename(), false, {})
              == CppParser::ParseResult::Parsed && !probe->findStatement("stale_disk_value"));

    const QString loadPath = temporary.filePath("loaded.cpp");
    check("Load fixture is written", write(loadPath, "int first_loaded_value;\n"));
    auto *loaded = create(loadPath, false);
    if (!loaded)
        return 4;
    const auto loadBefore = manager->snapshotFor(loaded);
    check("File loading registers the decoded text", sourceMatches(loaded)
          && loadBefore && loadBefore->lines.join('\n').contains("first_loaded_value"));
    check("Reload fixture is written", write(loadPath, "struct Reloaded { int field; };\nint second_loaded_value;\n"));
    loaded->loadFile({}, false);
    const auto loadAfter = manager->snapshotFor(loaded);
    check("Reload commits replacement text while preserving document identity", loadBefore && loadAfter
          && loadAfter->id == loadBefore->id && loadAfter->revision > loadBefore->revision
          && loadAfter->lines.join('\n').contains("second_loaded_value") && sourceMatches(loaded));
    bool failedLoad = false;
    try { loaded->loadFile(temporary.filePath("missing.cpp"), false); }
    catch (const FileError &) { failedLoad = true; }
    const auto loadFailed = manager->snapshotFor(loaded);
    check("A failed load keeps the previously committed revision and source", failedLoad && loadFailed && loadAfter
          && loadFailed->revision == loadAfter->revision && loadFailed->lines == loadAfter->lines && sourceMatches(loaded));

    loaded->setContent("int unsaved_renamed_value;");
    loaded->setModified(true);
    const auto beforeRename = manager->snapshotFor(loaded);
    const QString renamedPath = temporary.filePath("renamed.c");
    bool renameBoundarySeen = false, renameBoundaryConsistent = true;
    const auto renameStatus = QObject::connect(loaded, &Editor::statusChanged, loaded, [&] {
        if (loaded->filename() == renamedPath) {
            renameBoundarySeen = true;
            const auto captured = provider();
            const auto source = captured.succeeded() ? captured.snapshot->document(renamedPath) : PDocumentSnapshot{};
            renameBoundaryConsistent &= source && source->lines == loaded->content()
                && !captured.snapshot->document(loadPath);
        }
    });
    loaded->rename(renamedPath); // CPP -> C triggers setFileType/status before legacy fileRenamed.
    QObject::disconnect(renameStatus);
    const auto renamed = manager->snapshotFor(loaded);
    check("Rename registers the new path before language/status side effects", renameBoundarySeen && renameBoundaryConsistent
          && renamed && beforeRename && renamed->id == beforeRename->id
          && renamed->revision > beforeRename->revision && loaded->modified() && sourceMatches(loaded));

    const QString savedPath = temporary.filePath("saved-as.cpp");
    bool saveBoundarySeen = false, saveBoundaryConsistent = true;
    const auto saveStatus = QObject::connect(loaded, &Editor::statusChanged, loaded, [&] {
        if (loaded->filename() == savedPath) {
            saveBoundarySeen = true;
            const auto captured = provider();
            const auto source = captured.succeeded() ? captured.snapshot->document(savedPath) : PDocumentSnapshot{};
            saveBoundaryConsistent &= source && source->lines == loaded->content()
                && !captured.snapshot->document(renamedPath);
        }
    });
    const bool saved = loaded->saveAs(savedPath, false);
    QObject::disconnect(saveStatus);
    const auto savedAs = manager->snapshotFor(loaded);
    check("Save As registers its committed path before status and parser work", saved && saveBoundarySeen && saveBoundaryConsistent
          && savedAs && renamed && savedAs->id == renamed->id && savedAs->revision > renamed->revision
          && !loaded->modified() && sourceMatches(loaded));
    check("Parser provider uses the renamed open source", CppParser::parseFileBlocking(probe, savedPath, false, {})
          == CppParser::ParseResult::Parsed && bool(probe->findStatement("unsaved_renamed_value")));

#ifdef Q_OS_WIN
    const QString casePath = QFileInfo(savedPath).path() + "/SAVED-AS.cpp";
    const auto beforeCaseRename = manager->snapshotFor(loaded);
    loaded->rename(casePath);
    const auto afterCaseRename = manager->snapshotFor(loaded);
    check("Case-only rename preserves the same open document and exact display path", loaded->filename() == casePath
          && beforeCaseRename && afterCaseRename && beforeCaseRename->id == afterCaseRename->id
          && afterCaseRename->revision > beforeCaseRename->revision && sourceMatches(loaded));
    auto *right = create(temporary.filePath("RightPane.cpp"), true, manager->rightPageWidget());
    check("Both panes recognize Windows case-insensitive paths", right
          && manager->getOpenedEditor(right->filename().toUpper()) == right
          && manager->isFileOpened(right->filename().toUpper()) && manager->getOpenedEditor(casePath.toLower()) == loaded);
#endif
    const auto retained = manager->snapshotFor(loaded);
    const QString reopenPath = loaded->filename();
    manager->forceCloseEditor(loaded);
    const auto closed = provider();
    check("Closing removes the live source while accepted snapshots remain valid", retained && closed.succeeded()
          && !closed.snapshot->document(reopenPath) && retained->lines.join('\n').contains("unsaved_renamed_value"));
    auto *reopened = create(reopenPath, false);
    const auto reopenedSnapshot = reopened ? manager->snapshotFor(reopened) : PDocumentSnapshot{};
    check("Reopening the same path receives a new document identity", retained && reopenedSnapshot
          && reopenedSnapshot->id != retained->id && reopenedSnapshot->lines == retained->lines && sourceMatches(reopened));

    QFile output(QDir(outputDirectory).filePath("document-store-result.json"));
    const QByteArray bytes = QJsonDocument(QJsonObject{{"failures", failures}, {"checks", checks},
        {"changingCalls", changingCalls}, {"committedCalls", committedCalls}, {"statusCalls", statusCalls}}).toJson();
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size())
        return 5;
    return failures == 0 ? 0 : 1;
}
