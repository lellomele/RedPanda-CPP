#include "src/cpprefacter.h"
#include "src/project.h"
#include "src/projecttemplate.h"
#include "src/syntaxermanager.h"
#include "src/parser/cppparser.h"
#include <QCheckBox>
#include <QTemporaryDir>
#include <QStandardPaths>
#ifdef ENABLE_VCS
#include "src/vcs/gitmanager.h"
#endif
#include <QFileSystemWatcher>
#include <qsynedit/syntaxer/cpp.h>
#include "interfacechecks.h"
#include "compilerworkflowchecks.h"
#include "closeplanchecks.h"
#include "documentstorechecks.h"
#include "documentfilechecks.h"
#include "compilerrequestchecks.h"
#include "recoverychecks.h"
#include "projectoperationchecks.h"
#include "src/utils/os.h"
#include "src/mainwindow.h"
#include "src/sessionrecovery.h"
#include "src/sessionstore.h"
#include "src/settingsdialog/settingsdialog.h"
#include "src/settingsdialog/settingswidget.h"
#include "src/codesnippetsmanager.h"
#include "src/editormanager.h"
#include "src/settings.h"
#include "src/thememanager.h"
#include "src/appidentity.h"
#include "src/widgets/aboutdialog.h"
#include "src/widgets/cpudialog.h"
#include "src/widgets/classbrowser.h"
#include "src/editor.h"
#include "src/settingsdialog/environmentshortcutwidget.h"
#include "src/shortcutmanager.h"
#include "src/diagnosticcatalog.h"
#include "src/compiler/compiler.h"
#include "src/widgets/issuestable.h"
#include "src/utils/colorcontrast.h"
#include <QStyleOptionViewItem>
#include <QClipboard>
#include <QStatusBar>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include <QMimeData>
#include <QInputDialog>
#include <QKeyEvent>
#include <QCloseEvent>
#include <QPointer>
#include <QItemSelectionModel>
#include <QFileSystemModel>
#include <QTabWidget>
#include <QTabBar>
#include <QMenu>
#include <QTreeWidget>
#include <QDialogButtonBox>
#include <QDirIterator>
#include <QSvgRenderer>
#include <QPainter>
#include <QLabel>
#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QTableView>
#include <QTreeView>
#include <QPersistentModelIndex>
#include <QMouseEvent>
#include <QPushButton>
#include <QMessageBox>
#include <QEventLoop>
#include <QTimer>
#include <QSemaphore>
#include <thread>
#include <atomic>
#include <chrono>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QDir>
#include <QFrame>
#include <QSet>
#include <QAction>
#include <QProcess>
#include <QProcessEnvironment>
#include <QAbstractButton>
#include <algorithm>

class DiagnosticDelegateProbe : public DiagnosticItemDelegate {
public:
    explicit DiagnosticDelegateProbe(QObject *parent) : DiagnosticItemDelegate(parent) {}
    using DiagnosticItemDelegate::initStyleOption;
};

class GccDiagnosticParserProbe : public Compiler {
public:
    GccDiagnosticParserProbe() : Compiler({}, true), mSet(std::make_shared<CompilerSet>()) { mErrorCount = 0; mWarningCount = 0; mSet->setCompilerType(CompilerType::GCC); }
    void feed(QString line) { processOutput(line); }
    void finish() { feed("---//END//----"); }
    void setFamily(CompilerType type) { mSet->setCompilerType(type); }
protected:
    PCompilerSet compilerSet() override { return mSet; }
    bool prepareForCompile() override { return false; }
    bool prepareForRebuild() override { return false; }
private:
    PCompilerSet mSet;
};

static QString prefText(const char *source) { return QCoreApplication::translate("SettingsDialog", source); }
static void waitForUi(int milliseconds = 150) { QEventLoop loop; QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit); loop.exec(); }
int runUiChecks(MainWindow &window, SessionRecovery &recovery, const QString &outputDirectory) {
    if (qEnvironmentVariableIsSet("REDPANDA_QA_COMPILER_WORKFLOW_ONLY")) {
        QJsonArray compilerChecks;
        int compilerFailures = 0;
        const bool completionEnabled = pSettings->codeCompletion().enabled();
        const bool autolinkEnabled = pSettings->editor().enableAutolink();
        runCompilerWorkflowChecks(window, outputDirectory, [&](const QString &name, bool passed) {
            compilerChecks.append(QJsonObject{{"name", name}, {"passed", passed}});
            if (!passed) ++compilerFailures;
        });
        const auto bytes = QJsonDocument(QJsonObject{{"checks", compilerChecks}, {"failures", compilerFailures},
            {"completionEnabled", completionEnabled}, {"autolinkEnabled", autolinkEnabled}}).toJson();
        QFile report(QDir(outputDirectory).filePath("compiler-workflow-result.json"));
        if (!report.open(QIODevice::WriteOnly) || report.write(bytes) != bytes.size()) return 2;
        return compilerFailures || compilerChecks.isEmpty() ? 1 : 0;
    }
    if (qEnvironmentVariableIsSet("REDPANDA_QA_COMPILER_REQUESTS"))
        return runCompilerRequestChecks(window, outputDirectory);
    if (qEnvironmentVariableIsSet("REDPANDA_QA_DOCUMENT_FILES"))
        return runDocumentFileChecks(window, outputDirectory);
    if (qEnvironmentVariableIsSet("REDPANDA_QA_RECOVERY_CACHE_ONLY"))
        return runRecoveryChecks(window, recovery, outputDirectory);
    if (qEnvironmentVariableIsSet("REDPANDA_QA_DOCUMENT_STORE_ONLY"))
        return runDocumentStoreChecks(window, outputDirectory);
    if (qEnvironmentVariableIsSet("REDPANDA_QA_CLOSE_PLAN_ONLY"))
        return runClosePlanChecks(window, outputDirectory);
    if (qEnvironmentVariableIsSet("REDPANDA_QA_PROJECT_OPERATIONS"))
        return runProjectOperationChecks(window, outputDirectory);
    QDir().mkpath(outputDirectory); QJsonArray checks; QJsonArray skipped; int failures = 0;
    QFile progress(QDir(outputDirectory).filePath("ui-progress.jsonl"));
    if (!progress.open(QIODevice::WriteOnly)) return 2;
    const auto trace = [&](const QJsonObject &record) {
        const QByteArray bytes = QJsonDocument(record).toJson(QJsonDocument::Compact) + '\n';
        return progress.write(bytes) == bytes.size() && progress.flush();
    };
    QTimer modalObserver;
    modalObserver.setInterval(1000);
    QPointer<QWidget> lastModal;
    QObject::connect(&modalObserver, &QTimer::timeout, &window, [&] {
        auto *modal = QApplication::activeModalWidget();
        if (!modal || modal == lastModal) return;
        lastModal = modal;
        QJsonObject record{{"modalClass", QString::fromLatin1(modal->metaObject()->className())},
            {"title", modal->windowTitle()}};
        if (const auto *message = qobject_cast<QMessageBox *>(modal)) record["message"] = message->text();
        trace(record);
    });
    modalObserver.start();
    auto check = [&](const QString &name, bool passed) {
        const QJsonObject record{{"name", name}, {"passed", passed}};
        checks.append(record);
        if (!passed || !trace(record)) ++failures;
    };
    auto skip = [&](const QString &name, const QString &reason) { skipped.append(QJsonObject{{"name", name}, {"reason", reason}}); };

    if (qEnvironmentVariableIsSet("REDPANDA_QA_RENAME_ONLY")) {
        QTemporaryDir fixture(QDir(outputDirectory).filePath("rename-fixture-XXXXXX"));
        check("Rename fixture is isolated", fixture.isValid());
        const QByteArray sentinel("file on disk is unchanged\n");
        for (const QString mode : {QStringLiteral("success"), QStringLiteral("conflict"),
                                   QStringLiteral("busy"), QStringLiteral("changed")}) {
            const QString path = QDir(fixture.path()).filePath(mode + ".cpp");
            QFile onDisk(path);
            const bool written = onDisk.open(QIODevice::WriteOnly)
                && onDisk.write(sentinel) == sentinel.size();
            onDisk.close();
            check(mode + ": isolated sentinel written", written);
            Editor *editor = window.editorManager()->newEditor(path, ENCODING_UTF8,
                FileType::CppSource, {}, false, true);
            check(mode + ": editor created", editor != nullptr);
            if (!editor) continue;
            const QString source = mode == "conflict"
                ? QStringLiteral("int before = 1;\nint after = 2;\nint f() { return before; }\n")
                : QStringLiteral("int before = 1;\nint f() { return before; }\n");
            editor->setContent(source);
            editor->setModified(true);
            auto lines = std::make_shared<QStringList>(source.split('\n'));
            auto parser = std::make_shared<CppParser>();
            auto reads = std::make_shared<std::atomic<int>>(0);
            parser->setEnabled(true);
            parser->setOnGetFileStream([path, lines, reads](const QString &file, QStringList &out) {
                ++*reads;
                if (file == path) out = *lines;
                else out = {QStringLiteral("int queued_analysis_marker;")};
                return true;
            });
            editor->setCppParser(parser);
            window.editorManager()->activeEditorAndSetCaret(editor, {4, 0});
            // Showing a tab may itself request analysis. Let it finish before
            // exercising rename so that Busy is introduced only by the case.
            waitForUi(200);
            QElapsedTimer initialDeadline; initialDeadline.start();
            while (parser->hasPendingWork() && initialDeadline.elapsed() < 10000) waitForUi(20);
            check(mode + ": initial editor requests settled", !parser->hasPendingWork());
            const auto initial = CppParser::parseFileBlocking(parser, path, false, {}, false, false);
            check(mode + ": initial parse succeeds", initial == CppParser::ParseResult::Parsed);
            const int readsBefore = reads->load();
            const QString before = editor->text();
            bool frozen = false;
            if (mode == "busy") {
                frozen = parser->freeze();
                CppParser::resetParsedResultsNonBlocking(parser);
            }
            int inputCount = 0;
            int messageCount = 0;
            bool backgroundQueued = false;
            QTimer dialogs;
            QObject::connect(&dialogs, &QTimer::timeout, [&] {
                if (auto *input = qobject_cast<QInputDialog *>(QApplication::activeModalWidget())) {
                    ++inputCount;
                    if (mode == "changed") {
                        editor->replaceLine(0, QStringLiteral("int different = 1;"));
                        *lines = editor->text().split('\n');
                    }
                    input->setTextValue(QStringLiteral("after"));
                    input->accept();
                } else if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                    if (mode == "conflict" && !backgroundQueued) {
                        ++messageCount;
                        backgroundQueued = true;
                        CppParser::parseFileNonBlocking(parser, QStringLiteral("queued-for-conflict.cpp"), false, {});
                        QTimer::singleShot(150, message, [message] {
                            message->accept();
                        });
                    } else if (mode != "conflict") {
                        ++messageCount;
                        message->accept();
                    }
                }
            });
            dialogs.start(10);
            const bool invoked = QMetaObject::invokeMethod(&window, "on_actionRename_Symbol_triggered", Qt::DirectConnection);
            dialogs.stop();
            check(mode + ": rename action invoked", invoked);
            if (mode == "success") {
                check("Successful rename updates declaration and reference", inputCount == 1
                      && editor->text().contains("int after = 1;")
                      && editor->text().contains("return after;"));
            } else if (mode == "conflict") {
                check("Rename conflict reports error and preserves document", inputCount == 1
                      && messageCount == 1 && editor->text() == before);
                check("Conflict dialog accepts an independent analysis request", backgroundQueued);
            } else if (mode == "busy") {
                check("Busy rename reports error before asking for a name", frozen && inputCount == 0
                      && messageCount == 1 && editor->text() == before && reads->load() == readsBefore);
            } else {
                check("Changed symbol aborts rename and preserves the intervening edit", inputCount == 1
                      && messageCount == 1 && editor->text().contains("int different = 1;")
                      && !editor->text().contains("int after = 1;"));
            }
            if (frozen) parser->unFreeze();
            QElapsedTimer deadline; deadline.start();
            while (parser->hasPendingWork() && deadline.elapsed() < 10000) waitForUi(20);
            check(mode + ": parser drains queued work", !parser->hasPendingWork());
            if (mode == "conflict")
                check("Queued analysis completes after rename conflict",
                      static_cast<bool>(parser->findStatement(QStringLiteral("queued_analysis_marker"))));
            if (mode == "busy")
                check("Busy rename did not secretly queue a parse", reads->load() == readsBefore);
            QFile diskCheck(path);
            check(mode + ": disk file is unchanged", diskCheck.open(QIODevice::ReadOnly)
                  && diskCheck.readAll() == sentinel);
            window.editorManager()->forceCloseEditor(editor);
        }
        QFile output(QDir(outputDirectory).filePath("rename-only-result.json"));
        const QByteArray data = QJsonDocument(QJsonObject{{"checks", checks}, {"failures", failures}}).toJson();
        if (!output.open(QIODevice::WriteOnly) || output.write(data) != data.size()) return 2;
        return failures == 0 ? 0 : 1;
    }


    // Verify the visible file after each genuine tab click, rather than only
    // returning to the original file before observing the final model.
    if (qEnvironmentVariableIsSet("REDPANDA_QA_TAB_SWITCH")) {
        QFile progress(QDir(outputDirectory).filePath("tab-switch-progress.jsonl"));
        if (!progress.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 2;
        const auto mark = [&](QJsonObject event) {
            progress.write(QJsonDocument(event).toJson(QJsonDocument::Compact));
            progress.write("\n");
            progress.flush();
        };
        mark(QJsonObject{{"phase", "start"}});
        auto *view = window.findChild<QTreeView *>("classBrowser");
        auto *explorer = window.findChild<QTabWidget *>("tabExplorer");
        auto *structure = window.findChild<QWidget *>("tabStructure");
        auto *model = view ? qobject_cast<ClassBrowserModel *>(view->model()) : nullptr;
        auto *manager = window.editorManager();
        const QString firstPath = QDir(outputDirectory).filePath("files/first.cpp");
        const QString primePath = QDir(outputDirectory).filePath("files/structure_concurrency.cpp");
        Editor *first = manager->getOpenedEditor(firstPath);
        Editor *prime = manager->getOpenedEditor(primePath);
        if (!view || !explorer || !structure || !model || !first || !prime ||
                !first->parser() || first->parser() != prime->parser()) return 3;
        explorer->setCurrentWidget(structure);
        window.resize(1200, 800);
        pSettings->codeCompletion().setShareParser(true);
        pSettings->codeCompletion().setClearWhenEditorHidden(true);
        const QString blankPath = QDir(outputDirectory).filePath("files/empty.cpp");
        if (!QFileInfo::exists(blankPath)) {
            QFile blankFile(blankPath);
            if (!blankFile.open(QIODevice::WriteOnly)) return 4;
        }
        mark(QJsonObject{{"phase", "open-blank-before"}});
        Editor *blank = window.openFile(blankPath);
        mark(QJsonObject{{"phase", "open-blank-after"}});
        if (!blank || blank->parser() != prime->parser()) return 5;
        mark(QJsonObject{{"phase", "activate-blank-before"}});
        manager->activeEditor(blank, true);
        mark(QJsonObject{{"phase", "activate-blank-after"},
                         {"leftTabCount", manager->leftPageWidget()->count()},
                         {"rightTabCount", manager->rightPageWidget()->count()}});
        const bool splitFixture = qEnvironmentVariableIsSet("REDPANDA_QA_TAB_SWITCH_SPLIT");
        if (splitFixture) {
            auto *leftTabs = manager->findPageControlForEditor(first);
            auto *rightTabs = manager->findPageControlForEditor(prime);
            check("Split fixture has two tabs on both sides", leftTabs && rightTabs &&
                  leftTabs != rightTabs && leftTabs->count() == 2 && rightTabs->count() == 2 &&
                  manager->findPageControlForEditor(blank) == rightTabs);
        }
        const auto roots = [&] {
            QJsonArray names;
            for (int row = 0; row < model->rowCount({}); ++row)
                names.append(model->data(model->index(row, 0, {}), Qt::DisplayRole).toString());
            return names;
        };
        const auto hasRoot = [&](const QString &expected) {
            const QJsonArray names = roots();
            for (const auto &name : names)
                if (name.toString() == expected || name.toString().startsWith(expected + " "))
                    return true;
            return false;
        };
        QJsonArray observations;
        const auto observe = [&](Editor *target, const QString &wanted, const QString &unwanted,
                                 int cycle, const QString &kind) {
            mark(QJsonObject{{"phase", "observe-before"}, {"file", target->filename()},
                             {"cycle", cycle}, {"kind", kind}});
            QElapsedTimer deadline;
            deadline.start();
            while (deadline.elapsed() < 10000 &&
                   (target->parser()->hasPendingWork() ||
                    QDir::cleanPath(model->currentFile()) != QDir::cleanPath(target->filename()) ||
                    (wanted.isEmpty() ? model->rowCount({}) != 0 : !hasRoot(wanted))))
                waitForUi(20);
            const bool titleMatches = window.windowTitle().contains(QDir::toNativeSeparators(target->filename())) ||
                window.windowTitle().contains(QDir::fromNativeSeparators(target->filename()));
            const bool correct = manager->getEditor() == target && titleMatches && model->parser() == target->parser() &&
                !target->parser()->hasPendingWork() &&
                QDir::cleanPath(model->currentFile()) == QDir::cleanPath(target->filename()) &&
                (wanted.isEmpty() ? model->rowCount({}) == 0 : hasRoot(wanted)) && !hasRoot(unwanted);
            observations.append(QJsonObject{{"cycle", cycle}, {"kind", kind},
                {"expectedSymbol", wanted}, {"currentFile", model->currentFile()},
                {"editorFile", target->filename()}, {"pending", target->parser()->hasPendingWork()},
                {"activeEditorFile", manager->getEditor() ? manager->getEditor()->filename() : QString()},
                {"windowTitle", window.windowTitle()}, {"titleMatches", titleMatches},
                {"leftTabCount", manager->leftPageWidget()->count()},
                {"rightTabCount", manager->rightPageWidget()->count()},
                {"rootNames", roots()}, {"passed", correct}, {"milliseconds", deadline.elapsed()}});
            check(QStringLiteral("Selected tab shows %1 after %2 (%3)").arg(wanted, kind).arg(cycle), correct);
            mark(QJsonObject{{"phase", "observe-after"}, {"observation", observations.last()}});
            return correct;
        };
        const auto clickTab = [&](Editor *target) {
            auto *tabs = manager->findPageControlForEditor(target);
            if (!tabs) return false;
            auto *bar = tabs->tabBar();
            const int index = tabs->indexOf(target);
            if (!bar || index < 0) return false;
            const QRect rect = bar->tabRect(index);
            const QPoint point = rect.center();
            if (rect.isEmpty() || !bar->rect().contains(point)) return false;
            const QPointF local(point), global(bar->mapToGlobal(point));
            mark(QJsonObject{{"phase", "click-before"}, {"file", target->filename()},
                             {"index", index}});
            QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(bar, &press);
            QMouseEvent release(QEvent::MouseButtonRelease, local, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(bar, &release);
            waitForUi(20);
            mark(QJsonObject{{"phase", "click-after"}, {"file", target->filename()}});
            return tabs->currentWidget() == target;
        };
        bool proceed = observe(blank, QString(), QStringLiteral("PrimeResult"), -2, "empty file");
        // Match the reported scenario: focus is in the tool panel when the
        // user selects a different editor tab, so focusIn is not guaranteed.
        view->setFocus(Qt::MouseFocusReason);
        if (proceed) {
            proceed = clickTab(prime);
            check(QStringLiteral("Click from empty file selects populated tab"), proceed);
            if (proceed) proceed = observe(prime, QStringLiteral("PrimeResult"), QStringLiteral("OtherFixture"), -1, "from tool panel");
        }
        for (int cycle = 0; proceed && cycle < 3; ++cycle) {
            proceed = clickTab(first);
            check(QStringLiteral("Actual mouse click selects alternate tab (%1)").arg(cycle), proceed);
            if (proceed) proceed = observe(first, QStringLiteral("OtherFixture"), QStringLiteral("PrimeResult"), cycle, "mouse click");
            if (!proceed) break;
            proceed = clickTab(prime);
            check(QStringLiteral("Actual mouse click selects original tab (%1)").arg(cycle), proceed);
            if (proceed) proceed = observe(prime, QStringLiteral("PrimeResult"), QStringLiteral("OtherFixture"), cycle, "mouse click");
        }
        if (proceed) {
            // Programmatic selection in the active pane must update the
            // information even when focus stays in the Structure view.
            view->setFocus(Qt::MouseFocusReason);
            auto *tabs = manager->findPageControlForEditor(prime);
            mark(QJsonObject{{"phase", "programmatic-blank-before"}});
            tabs->setCurrentWidget(blank);
            mark(QJsonObject{{"phase", "programmatic-blank-after"}});
            proceed = observe(blank, QString(), QStringLiteral("PrimeResult"), 3, "programmatic selection");
            if (proceed) {
                mark(QJsonObject{{"phase", "programmatic-prime-before"}});
                tabs->setCurrentWidget(prime);
                mark(QJsonObject{{"phase", "programmatic-prime-after"}});
                proceed = observe(prime, QStringLiteral("PrimeResult"), QStringLiteral("OtherFixture"), 3, "programmatic selection");
            }
        }
        if (proceed && splitFixture) {
            // A background pane changing its tab must not steal the active
            // editor, title or Structure selection from the focused pane.
            prime->setFocus(Qt::MouseFocusReason);
            auto *leftTabs = manager->findPageControlForEditor(first);
            Editor *helper = manager->getOpenedEditor(QDir(outputDirectory).filePath("files/left_helper.cpp"));
            if (!helper) return 7;
            mark(QJsonObject{{"phase", "background-helper-before"}});
            leftTabs->setCurrentWidget(helper);
            mark(QJsonObject{{"phase", "background-helper-after"}});
            proceed = observe(prime, QStringLiteral("PrimeResult"), QStringLiteral("OtherFixture"), 4, "background pane selection");
            if (proceed) {
                mark(QJsonObject{{"phase", "background-first-before"}});
                leftTabs->setCurrentWidget(first);
                mark(QJsonObject{{"phase", "background-first-after"}});
                proceed = observe(prime, QStringLiteral("PrimeResult"), QStringLiteral("OtherFixture"), 4, "background pane restoration");
            }
        }
        if (proceed && splitFixture) {
            first->setFocus(Qt::MouseFocusReason);
            proceed = observe(first, QStringLiteral("OtherFixture"), QStringLiteral("PrimeResult"), 3, "visible-pane focus");
            if (proceed) {
                prime->setFocus(Qt::MouseFocusReason);
                proceed = observe(prime, QStringLiteral("PrimeResult"), QStringLiteral("OtherFixture"), 3, "visible-pane focus");
            }
        }
        QFile output(QDir(outputDirectory).filePath("tab-switch-result.json"));
        const QByteArray data = QJsonDocument(QJsonObject{{"checks", checks}, {"failures", failures},
            {"observations", observations}}).toJson();
        if (!output.open(QIODevice::WriteOnly) || output.write(data) != data.size()) return 2;
        return failures == 0 && observations.size() == (splitFixture ? 14 : 10) ? 0 : 1;
    }

    // Opt-in isolated reproduction; progress is flushed before UI calls so
    // the supervising process can diagnose timeouts without touching user data.
    if (qEnvironmentVariableIsSet("REDPANDA_QA_PRIME_EXPAND")) {
        QFile progress(QDir(outputDirectory).filePath("prime-progress.jsonl"));
        if (!progress.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 2;
        const auto mark = [&](QJsonObject obj) {
            progress.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
            progress.write("\n");
            progress.flush();
        };
        const auto finish = [&](bool ok, const QString &reason, const QJsonArray &children,
                                qint64 maxDirectMs, qint64 maxClickMs) {
            const QJsonObject result{{"ok", ok}, {"reason", reason}, {"children", children},
                                     {"maxDirectMs", maxDirectMs}, {"maxClickMs", maxClickMs}};
            mark(QJsonObject{{"phase", "finished"}, {"ok", ok}, {"reason", reason}});
            QFile out(QDir(outputDirectory).filePath("prime-result.json"));
            const QByteArray bytes = QJsonDocument(result).toJson();
            if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size()) return 2;
            return ok ? 0 : 1;
        };
        mark(QJsonObject{{"phase", "start"}});
        const QString expected = QDir::cleanPath(qEnvironmentVariable("REDPANDA_QA_PRIME_EXPAND"));
        auto *view = window.findChild<QTreeView *>("classBrowser");
        auto *tabs = window.findChild<QTabWidget *>("tabExplorer");
        auto *structure = window.findChild<QWidget *>("tabStructure");
        auto *model = view ? qobject_cast<ClassBrowserModel *>(view->model()) : nullptr;
        auto *editor = window.editorManager()->getOpenedEditor(expected);
        if (!view || !tabs || !structure || !model || !editor || !editor->parser())
            return finish(false, "missing fixture, editor or Structure model", {}, -1, -1);
        tabs->setCurrentWidget(structure);
        window.resize(1200, 800);
        const auto findPrime = [&]() -> QModelIndex {
            for (int row = 0; row < model->rowCount({}); ++row) {
                const QModelIndex item = model->index(row, 0, {});
                const QString name = model->data(item, Qt::DisplayRole).toString();
                if (name == QStringLiteral("PrimeResult") || name.startsWith(QStringLiteral("PrimeResult ")))
                    return item;
            }
            return {};
        };
        QElapsedTimer deadline;
        deadline.start();
        while (deadline.elapsed() < 25000 &&
               (!editor->parser()->fileScanned(expected) || !findPrime().isValid()))
            waitForUi(50);
        QPersistentModelIndex prime(findPrime());
        if (!prime.isValid() || !editor->parser()->fileScanned(expected))
            return finish(false, "PrimeResult or parsed file not ready", {}, -1, -1);
        if (model->parser() != editor->parser() || QDir::cleanPath(model->currentFile()) != expected)
            return finish(false, "Structure refers to a different editor", {}, -1, -1);
        QJsonArray children;
        bool hasCount = false, hasSum = false;
        const int childCount = model->rowCount(prime);
        if (childCount < 0 || childCount > 256)
            return finish(false, "unexpected PrimeResult child count", children, -1, -1);
        for (int row = 0; row < childCount; ++row) {
            const QModelIndex child = model->index(row, 0, prime);
            if (!child.isValid() || child.internalPointer() == prime.internalPointer() || child.parent() != prime)
                return finish(false, "invalid or cyclic PrimeResult child", children, -1, -1);
            const QString value = model->data(child, Qt::DisplayRole).toString();
            children.append(value);
            hasCount |= value.startsWith(QStringLiteral("count"));
            hasSum |= value.startsWith(QStringLiteral("sum"));
        }
        if (!hasCount || !hasSum)
            return finish(false, "expected count and sum are absent", children, -1, -1);
        mark(QJsonObject{{"phase", "ready"}, {"children", childCount}});

        if (qEnvironmentVariableIsSet("REDPANDA_QA_PRIME_RACE")) {
            // Hold exactly one copied-file read per cycle, allowing the UI to
            // expand and switch while a real parser worker is demonstrably busy.
            auto *manager = window.editorManager();
            const QString alternateFile = QDir(outputDirectory).filePath("files/first.cpp");
            Editor *alternate = manager->getOpenedEditor(alternateFile);
            const PCppParser parser = editor->parser();
            if (!alternate || !parser || alternate->parser() != parser)
                return finish(false, "race fixture does not use one shared parser", children, -1, -1);
            auto requested = std::make_shared<std::atomic<bool>>(false);
            auto entered = std::make_shared<QSemaphore>();
            auto proceed = std::make_shared<QSemaphore>();
            bool frozen = false;
            QElapsedTimer idleWait; idleWait.start();
            while (idleWait.elapsed() < 5000 && !(frozen = parser->freeze())) waitForUi(10);
            if (!frozen) return finish(false, "parser did not become available for QA setup", children, -1, -1);
            parser->setSourceReadObserver([expected, requested, entered, proceed](const QString &filename) {
                if (QDir::cleanPath(filename) == expected && requested->exchange(false)) {
                    entered->release();
                    proceed->tryAcquire(1, 6000);
                }
            });
            parser->unFreeze();
            qint64 maxSwitchMs = 0;
            for (int cycle = 0; cycle < 3; ++cycle) {
                QElapsedTimer readyWait; readyWait.start();
                while (readyWait.elapsed() < 10000 &&
                       (parser->parsing() || model->currentFile() != expected || !findPrime().isValid()))
                    waitForUi(20);
                if (parser->parsing() || model->currentFile() != expected || !findPrime().isValid())
                    return finish(false, "race did not settle before cycle", children, maxSwitchMs, -1);
                prime = findPrime();
                requested->store(true);
                mark(QJsonObject{{"phase", "race-reparse-before"}, {"cycle", cycle}});
                editor->reparse();
                QElapsedTimer gateWait; gateWait.start();
                bool gateEntered = false;
                while (gateWait.elapsed() < 5000 && !(gateEntered = entered->tryAcquire(1, 0))) waitForUi(10);
                if (!gateEntered || !parser->parsing()) {
                    proceed->release();
                    return finish(false, "parser overlap could not be established", children, maxSwitchMs, -1);
                }
                mark(QJsonObject{{"phase", "race-parser-busy"}, {"cycle", cycle}});
                // Release independently even if the UI blocks in a tab switch.
                auto gateReleased = std::make_shared<std::atomic<bool>>(false);
                std::thread delayedRelease([proceed, gateReleased] {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
                    gateReleased->store(true, std::memory_order_release);
                    proceed->release();
                });
                struct JoinOnExit { std::thread &thread; ~JoinOnExit() { if (thread.joinable()) thread.join(); } };
                {
                    JoinOnExit join{delayedRelease};
                    if (!prime.isValid()) return finish(false, "PrimeResult snapshot invalid while busy", children, maxSwitchMs, -1);
                    mark(QJsonObject{{"phase", "race-expand-before"}, {"cycle", cycle}});
                    view->setCurrentIndex(prime);
                    view->scrollTo(prime, QAbstractItemView::PositionAtCenter);
                    view->expand(prime);
                    waitForUi(10);
                    mark(QJsonObject{{"phase", "race-expand-after"}, {"cycle", cycle},
                                     {"parserBusy", parser->parsing()}});
                    if (!prime.isValid())
                        return finish(false, "PrimeResult invalid during busy expansion", children, maxSwitchMs, -1);
                    view->collapse(prime);
                    const QRect rect = view->visualRect(prime);
                    const QPoint point(qMax(1, rect.left() - view->indentation() / 2), rect.center().y());
                    const bool visibleBranch = view->isVisible() && !rect.isEmpty()
                        && view->viewport()->rect().contains(point);
                    mark(QJsonObject{{"phase", "race-mouse-before"}, {"cycle", cycle},
                                     {"validGeometry", visibleBranch}, {"x", point.x()}, {"y", point.y()}});
                    if (!visibleBranch)
                        return finish(false, "race branch geometry not visible", children, maxSwitchMs, -1);
                    const QPointF local(point), global(view->viewport()->mapToGlobal(point));
                    QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                    QApplication::sendEvent(view->viewport(), &press);
                    QMouseEvent release(QEvent::MouseButtonRelease, local, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                    QApplication::sendEvent(view->viewport(), &release);
                    waitForUi(10);
                    mark(QJsonObject{{"phase", "race-mouse-after"}, {"cycle", cycle},
                                     {"parserBusy", parser->parsing()},
                                     {"expanded", prime.isValid() && view->isExpanded(prime)}});
                    if (!prime.isValid() || !view->isExpanded(prime))
                        return finish(false, "race branch click failed or model reset", children, maxSwitchMs, -1);
                    if (gateReleased->load(std::memory_order_acquire))
                        return finish(false, "parser gate released before switches; QA fixture inconclusive", children, -1, -1);
                    QElapsedTimer switchTimer; switchTimer.start();
                    mark(QJsonObject{{"phase", "race-switch-away-before"}, {"cycle", cycle}});
                    manager->activeEditor(alternate, true);
                    mark(QJsonObject{{"phase", "race-switch-away-after"}, {"cycle", cycle},
                                     {"gateReleased", gateReleased->load(std::memory_order_acquire)}});
                    manager->activeEditor(editor, true);
                    mark(QJsonObject{{"phase", "race-switch-back-after"}, {"cycle", cycle},
                                     {"gateReleased", gateReleased->load(std::memory_order_acquire)}});
                    // Further queued work tests coalescing while rapid tab changes occur.
                    alternate->reparse();
                    editor->reparse();
                    manager->activeEditor(alternate, true);
                    manager->activeEditor(editor, true);
                    const bool completedBeforeRelease = !gateReleased->load(std::memory_order_acquire);
                    maxSwitchMs = qMax(maxSwitchMs, switchTimer.elapsed());
                    mark(QJsonObject{{"phase", "race-switches-complete"}, {"cycle", cycle},
                                     {"milliseconds", switchTimer.elapsed()},
                                     {"completedBeforeRelease", completedBeforeRelease}});
                    if (!completedBeforeRelease)
                        return finish(false, "tab switching waited for the parser gate release", children, maxSwitchMs, -1);
                }
                if (maxSwitchMs > 5000)
                    return finish(false, "tab switching exceeded the QA responsiveness bound", children, maxSwitchMs, -1);
                QElapsedTimer settle; settle.start();
                while (settle.elapsed() < 10000 &&
                       (parser->parsing() || model->currentFile() != expected || !findPrime().isValid()))
                    waitForUi(20);
                prime = findPrime();
                if (parser->parsing() || model->currentFile() != expected || !prime.isValid())
                    return finish(false, "Structure did not recover after overlapping requests", children, maxSwitchMs, -1);
                mark(QJsonObject{{"phase", "race-cycle-complete"}, {"cycle", cycle},
                                 {"rows", model->rowCount(prime)}});
            }
        }
        qint64 maxDirectMs = 0, maxClickMs = 0;
        for (int iteration = 0; iteration < 10; ++iteration) {
            if (!prime.isValid()) return finish(false, "model reset during expansion", children, maxDirectMs, maxClickMs);
            view->setCurrentIndex(prime);
            view->scrollTo(prime, QAbstractItemView::PositionAtCenter);
            waitForUi(30);
            mark(QJsonObject{{"phase", "direct-before"}, {"iteration", iteration}});
            QElapsedTimer timer; timer.start();
            view->expand(prime);
            waitForUi(30);
            if (!prime.isValid() || !view->isExpanded(prime))
                return finish(false, "direct expansion failed or model reset", children, maxDirectMs, maxClickMs);
            maxDirectMs = qMax(maxDirectMs, timer.elapsed());
            view->collapse(prime);
            waitForUi(30);
            if (!prime.isValid()) return finish(false, "model reset after collapse", children, maxDirectMs, maxClickMs);
            const QRect rect = view->visualRect(prime);
            const QPoint point(qMax(1, rect.left() - view->indentation() / 2), rect.center().y());
            const bool geometryOk = view->isVisible() && !rect.isEmpty()
                && view->viewport()->rect().contains(point);
            mark(QJsonObject{{"phase", "mouse-before"}, {"iteration", iteration},
                {"visible", view->isVisible()}, {"validGeometry", geometryOk},
                {"x", point.x()}, {"y", point.y()}, {"rowLeft", rect.left()}});
            if (!geometryOk) return finish(false, "branch geometry is not visible", children, maxDirectMs, maxClickMs);
            timer.restart();
            const QPointF local(point), global(view->viewport()->mapToGlobal(point));
            QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(view->viewport(), &press);
            QMouseEvent release(QEvent::MouseButtonRelease, local, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(view->viewport(), &release);
            waitForUi(30);
            if (!prime.isValid() || !view->isExpanded(prime))
                return finish(false, "branch click did not expand or model reset", children, maxDirectMs, maxClickMs);
            maxClickMs = qMax(maxClickMs, timer.elapsed());
            view->collapse(prime);
            waitForUi(30);
            mark(QJsonObject{{"phase", "cycle-complete"}, {"iteration", iteration}});
        }
        return finish(true, QString(), children, maxDirectMs, maxClickMs);
    }

    // Only used by a bounded, isolated process to reproduce a frozen reader.
    if (qEnvironmentVariableIsSet("REDPANDA_QA_FREEZE_RESET")) {
        Editor *editor = window.editorManager()->getEditor();
        if (!editor) return 3;
        auto parser = std::make_shared<CppParser>();
        parser->setEnabled(true);
        editor->setCppParser(parser);
        editor->setFilename(QDir(outputDirectory).filePath("frozen-unscanned.cpp"));
        pSettings->codeCompletion().setShareParser(true);
        pSettings->codeCompletion().setClearWhenEditorHidden(true);
        const bool frozen = parser->freeze();
        const bool needsReparse = editor->needReparse();
        QFile ready(QDir(outputDirectory).filePath("freeze-reset-ready.json"));
        const QByteArray before = QJsonDocument(QJsonObject{{"frozen", frozen},
            {"needsReparse", needsReparse}, {"parserBusy", parser->parsing()}}).toJson();
        if (!ready.open(QIODevice::WriteOnly) || ready.write(before) != before.size()) {
            if (frozen) parser->unFreeze();
            return 4;
        }
        ready.close();
        if (!frozen || !needsReparse) {
            if (frozen) parser->unFreeze();
            return 5;
        }
        emit editor->showOccured(editor);
        parser->unFreeze();
        QFile result(QDir(outputDirectory).filePath("freeze-reset-result.json"));
        const QByteArray after = QJsonDocument(QJsonObject{{"returned", true}}).toJson();
        if (!result.open(QIODevice::WriteOnly) || result.write(after) != after.size()) return 6;
        return 0;
    }

    // Runs before the normal GUI checks, with an isolated restored-session fixture.
    // Unlike the ordinary UI suite, it leaves the restored editor unchanged.
    if (qEnvironmentVariableIsSet("REDPANDA_QA_RESTORED_FILE")) {
        const QString expected = QDir::cleanPath(qEnvironmentVariable("REDPANDA_QA_RESTORED_FILE"));
        auto *view = window.findChild<QTreeView *>("classBrowser");
        auto *model = view ? qobject_cast<ClassBrowserModel *>(view->model()) : nullptr;
        auto *editor = window.editorManager()->getOpenedEditor(expected);
        auto *editorPage = editor ? window.editorManager()->findPageControlForEditor(editor) : nullptr;
        const auto containsConfig = [&] {
            if (!model) return false;
            for (int row=0; row<model->rowCount({}); ++row)
                if (model->data(model->index(row, 0, {}), Qt::DisplayRole).toString().startsWith(QStringLiteral("Config")))
                    return true;
            return false;
        };
        for (int i=0; i<120; ++i) {
            if (editor && editor->parser() && editor->parser()->fileScanned(expected) && containsConfig())
                break;
            waitForUi(100);
        }
        const bool selected = editor && editorPage && editorPage->currentWidget() == editor
            && QDir::cleanPath(editor->filename()) == expected;
        const bool modelAttached = selected && model && model->parser() == editor->parser()
            && QDir::cleanPath(model->currentFile()) == expected;
        const bool parsed = selected && editor->parser() && editor->parser()->fileScanned(expected);
        const bool configVisible = containsConfig();
        const QJsonObject report{
            {"expectedFile", expected},
            {"selectedFile", editor ? editor->filename() : QString()},
            {"modelFile", model ? model->currentFile() : QString()},
            {"selected", selected}, {"modelAttached", modelAttached}, {"parsed", parsed},
            {"configVisible", configVisible}, {"rootRows", model ? model->rowCount({}) : -1},
            {"parserBusy", editor && editor->parser() && editor->parser()->parsing()}
        };
        QFile output(QDir(outputDirectory).filePath("startup-restore-result.json"));
        const QByteArray data = QJsonDocument(report).toJson();
        if (!output.open(QIODevice::WriteOnly) || output.write(data) != data.size()) return 2;
        return selected && modelAttached && parsed && configVisible ? 0 : 1;
    }
    const QString invalidTheme = QDir(outputDirectory).filePath("invalid-theme.json");
    for (const QByteArray &bytes : {QByteArray("[]"), QByteArray("{}"), QByteArray(R"({"palette":{"PaletteText":"bad color"}})")}) {
        QFile file(invalidTheme); if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) return 2; file.close(); bool rejected = false;
        try { AppTheme theme(invalidTheme, AppTheme::ThemeType::JSON, AppTheme::ThemeCategory::Custom); } catch (const FileError &) { rejected = true; }
        check("Invalid theme rejected", rejected);
    }
    const QString language = pSettings->environment().language();
    check("Application display name is language independent", QGuiApplication::applicationDisplayName() == AppIdentity::displayName());
    check("Internal name is fixed", QCoreApplication::applicationName() == AppIdentity::internalName());
    check("Window title contains canonical brand", window.windowTitle().contains(AppIdentity::displayName()) && !window.windowTitle().contains("Panda rosso", Qt::CaseInsensitive));
    {
        AboutDialog about(&window); about.show(); waitForUi(); const auto *title = about.findChild<QLabel *>("lblTitle");
        check("About dialog uses canonical brand", title && title->text().contains(AppIdentity::displayName()) && !title->text().contains("Panda rosso", Qt::CaseInsensitive));
        const auto *art = about.findChild<QLabel *>("lblAppArtwork");
        check("About artwork is visible at a useful size", art && art->isVisible() && art->width() >= 192 && !art->property("pixmap").value<QPixmap>().isNull());
        auto *copyright = about.findChild<QLabel *>("lblForkCopyright");
        auto *flag = about.findChild<QLabel *>("lblForkFlag");
        check("Fork copyright is exact and independent of interface language", copyright && copyright->text() == QStringLiteral("Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)") && copyright->textFormat() == Qt::PlainText);
        const auto *upstreamCopyright = about.findChild<QLabel *>("label");
        check("Original copyright remains visible", upstreamCopyright && upstreamCopyright->text().contains("royqh1979@gmail.com"));
        const auto *homepage = about.findChild<QLabel *>("lblHomepage");
        check("About website points to the fork in every interface language", homepage && homepage->text().contains(QStringLiteral("href=\"https://github.com/lellomele/RedPanda-CPP\"")) && !homepage->text().contains("sourceforge.net"));
        const auto *upstreamScope = about.findChild<QLabel *>("lblUpstreamScope");
        const auto *forkScope = about.findChild<QLabel *>("lblForkScope");
        check("About separates original and fork ownership", upstreamScope && upstreamScope->isVisible() && upstreamScope->text().contains("Roy Qu") && forkScope && forkScope->isVisible() && forkScope->text().contains("Raffaele Mele"));
        check("Italian flag has the height of the copyright characters", copyright && flag && flag->isVisible() && flag->height() == copyright->fontMetrics().capHeight());
        if (flag && copyright) {
            const QImage image = flag->property("pixmap").value<QPixmap>().toImage();
            check("Italian flag renders green white and red", !image.isNull() && image.pixelColor(image.width()/6, image.height()/2) == QColor("#009246") && image.pixelColor(image.width()/2, image.height()/2) == QColor("#ffffff") && image.pixelColor(5*image.width()/6, image.height()/2) == QColor("#ce2b37"));
            const QFont saved = copyright->font(); QFont enlarged = saved; enlarged.setPointSize(qMax(12, saved.pointSize()) + 4);
            copyright->setFont(enlarged); waitForUi(20);
            check("Italian flag follows copyright font size changes", flag->height() == copyright->fontMetrics().capHeight());
            copyright->setFont(saved); waitForUi(20);
        }
        check("About screenshot exported", about.grab().save(QDir(outputDirectory).filePath("about.png")));
        about.hide();
    }
    const QImage splash(QDir(outputDirectory).filePath("startup.png"));
    check("Startup artwork was displayed", !splash.isNull() && splash.width() >= 460 && splash.height() >= 340);
    if (language == "it_IT") {
        const QList<QPair<const char *, QString>> terms{{"Open...", "Apri..."}, {"About", "Informazioni"}, {"Clean", "Pulisci"}, {"Watch", "Watch"}, {"Add Watch...", "Aggiungi Watch..."}, {"Match Bracket", "Vai alla parentesi corrispondente"}, {"Generate Assembly", "Genera Assembly"}, {"Locals", "Variabili locali"}, {"TODO", "TODO"}, {"Commit Message", "Messaggio di Commit"}, {"Step Into", "Step Into"}, {"Encode in UTF-8 BOM", "Usa codifica UTF-8 con BOM"}};
        for (const auto &term : terms) check("Italian IDE terminology: " + QString::fromUtf8(term.first), QCoreApplication::translate("MainWindow", term.first) == term.second);
        const auto *open = window.findChild<QAction *>("actionOpen");
        check("Open action uses Apri and working shortcut", open && open->text() == "Apri..." && open->shortcut() == QKeySequence("Ctrl+O"));
    }
    {
        CPUDialog cpu(window.colorManager(), &window);
        const QStringList instructions{QStringLiteral("=> 0x401000: mov eax, 42"), QStringLiteral("   0x401005: ret")};
        cpu.setDisassembly(QStringLiteral("cpu-check.cpp"), QStringLiteral("main"), instructions, {});
        cpu.show();
        waitForUi();
        auto *code = cpu.findChild<Editor *>("txtCode");
        check("CPU dialog exposes its read-only disassembly editor", code && code->readOnly());
        if (code) {
            const QString original = code->text();
            QKeyEvent selectAll(QEvent::KeyPress, Qt::Key_A, Qt::ControlModifier);
            QApplication::sendEvent(code, &selectAll);
            const QString selected = code->selText();
            check("CPU dialog Ctrl+A selects every disassembly instruction", !selected.isEmpty() && selected == original);
            bool nativeClipboardDenied = false;
#ifdef Q_OS_WIN
            if (QGuiApplication::platformName() == "windows") {
                SetLastError(ERROR_SUCCESS);
                const bool available = OpenClipboard(nullptr);
                const DWORD error = GetLastError();
                if (available) CloseClipboard();
                nativeClipboardDenied = !available && error == ERROR_ACCESS_DENIED;
            }
#endif
            QMimeData *savedClipboard = nullptr;
            if (!nativeClipboardDenied) {
                savedClipboard = new QMimeData;
                if (const auto *previous = QGuiApplication::clipboard()->mimeData())
                    for (const auto &format : previous->formats()) savedClipboard->setData(format, previous->data(format));
                QGuiApplication::clipboard()->setText(QStringLiteral("CPU clipboard sentinel"));
            }
            QKeyEvent copy(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier);
            QApplication::sendEvent(code, &copy);
            if (!nativeClipboardDenied)
                check("CPU dialog Ctrl+C copies the exact selected disassembly", QGuiApplication::clipboard()->text() == selected);
            else
                check("CPU dialog copy preserves selection when native clipboard access is denied", code->selText() == selected && code->text() == original);
            QKeyEvent erase(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
            QApplication::sendEvent(code, &erase);
            QKeyEvent replace(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier, QStringLiteral("x"));
            QApplication::sendEvent(code, &replace);
            check("CPU dialog select copy delete and typing preserve disassembly", code->readOnly() && code->text() == original && code->selText() == selected);
            if (savedClipboard) QGuiApplication::clipboard()->setMimeData(savedClipboard);
        }
        cpu.hide();
    }
    {
        // Repeated traversal of parser scopes must preserve nested and reopened
        // namespaces, their members and their navigation attributes.
        const QString file = QStringLiteral("structure_nesting_fixture.cpp");
        auto parser = std::make_shared<CppParser>();
        parser->setOnGetFileStream([](const QString &name, QStringList &lines) {
            if (!name.endsWith(QStringLiteral("structure_nesting_fixture.cpp"))) return false;
            for (int i = 0; i < 80; ++i)
                lines << QStringLiteral("namespace Level%1 {").arg(i);
            lines << QStringLiteral("int deepest = 7;");
            for (int i = 0; i < 80; ++i) lines << QStringLiteral("}");
            lines << QStringLiteral("namespace Again { struct A { int x; }; }");
            lines << QStringLiteral("namespace Again { struct B { int y; }; }");
            return true;
        });
        CppParser::parseFileBlocking(parser, file, false, QString(), false, false);
        ClassBrowserModel model;
        model.beginUpdate(); model.setParser(parser); model.setCurrentFile(file); model.endUpdate();
        const auto childNamed = [&](const QModelIndex &parent, const QString &name) {
            for (int row = 0; row < model.rowCount(parent); ++row) {
                const auto index = model.index(row, 0, parent);
                if (model.data(index, Qt::DisplayRole).toString() == name) return index;
            }
            return QModelIndex();
        };
        QModelIndex nested;
        int levelsFound = 0;
        for (int i = 0; i < 80; ++i) {
            nested = childNamed(nested, QStringLiteral("Level%1").arg(i));
            if (!nested.isValid()) break;
            ++levelsFound;
        }
        check("Structure retains all deeply nested namespaces", levelsFound == 80 && nested.isValid()
              && model.rowCount(nested) == 1
              && model.data(model.index(0, 0, nested), Qt::DisplayRole).toString().startsWith(QStringLiteral("deepest")));
        const QModelIndex again = childNamed({}, QStringLiteral("Again"));
        const QModelIndex a = again.isValid() ? childNamed(again, QStringLiteral("A")) : QModelIndex();
        const QModelIndex b = again.isValid() ? childNamed(again, QStringLiteral("B")) : QModelIndex();
        const bool members = again.isValid() && model.rowCount(again) == 2 && a.isValid() && b.isValid()
                && model.rowCount(a) == 1 && model.rowCount(b) == 1
                && model.data(model.index(0, 0, a), Qt::DisplayRole).toString().startsWith(QStringLiteral("x"))
                && model.data(model.index(0, 0, b), Qt::DisplayRole).toString().startsWith(QStringLiteral("y"));
        check("Structure merges reopened namespaces without losing members", members);
        PStatement originalA, originalB;
        const bool frozen = parser->freeze();
        if (frozen) {
            const auto info = parser->findFileInfo(file);
            if (info) for (const PStatement &statement : info->statements()) {
                if (statement->fullName == QStringLiteral("Again::A")) originalA = statement;
                if (statement->fullName == QStringLiteral("Again::B")) originalB = statement;
            }
        }
        const auto sameAttributes = [](const PStatement &actual, const PStatement &original) {
            return actual && original && actual->kind == original->kind
                    && actual->fullName == original->fullName && actual->fileName == original->fileName
                    && actual->line == original->line && actual->args == original->args
                    && actual->accessibility == original->accessibility;
        };
        auto *aNode = a.isValid() ? static_cast<ClassBrowserNode *>(a.internalPointer()) : nullptr;
        auto *bNode = b.isValid() ? static_cast<ClassBrowserNode *>(b.internalPointer()) : nullptr;
        check("Structure preserves namespace member navigation attributes", frozen && aNode && bNode
              && sameAttributes(aNode->statement, originalA) && sameAttributes(bNode->statement, originalB));
        if (frozen) parser->unFreeze();
    }
    {
        // Exercise a real parser on an isolated model; the semaphore forces the
        // refresh attempt to overlap parsing without relying on timing luck.
        const QString originalFile = QStringLiteral("structure_fixture.cpp");
        auto parser = std::make_shared<CppParser>();
        parser->setOnGetFileStream([](const QString &file, QStringList &lines) {
            if (!file.endsWith(QStringLiteral("structure_fixture.cpp"))) return false;
            lines = {QStringLiteral("int original_symbol = 1;")};
            return true;
        });
        CppParser::parseFileBlocking(parser, originalFile, false, QString(), false, false);
        ClassBrowserModel model;
        model.beginUpdate();
        model.setParser(parser);
        model.setCurrentFile(originalFile);
        model.endUpdate();
        const auto contains = [&](const QString &name) {
            for (int row = 0; row < model.rowCount({}); ++row)
                if (model.data(model.index(row, 0, {}), Qt::DisplayRole).toString().contains(name))
                    return true;
            return false;
        };
        check("Structure model displays parsed file symbols", contains(QStringLiteral("original_symbol")));
        QSemaphore parsingStarted, permitFinish;
        parser->setOnGetFileStream([&](const QString &file, QStringList &lines) {
            if (!file.endsWith(QStringLiteral("structure_fixture.cpp"))) return false;
            parsingStarted.release();
            if (!permitFinish.tryAcquire(1, 5000)) return false;
            lines = {QStringLiteral("int refreshed_symbol = 2;")};
            return true;
        });
        std::thread parsing([&] { CppParser::parseFileBlocking(parser, originalFile, false, QString(), false, true); });
        const bool busy = parsingStarted.tryAcquire(1, 3000);
        check("Structure regression fixture reaches active parsing", busy);
        if (busy) {
            model.fillStatements();
            waitForUi(180); // includes queued parse signals and at least one retry
            check("Structure retains previous same-file snapshot while parser is busy",
                  contains(QStringLiteral("original_symbol")));
        }
        permitFinish.release();
        parsing.join();
        waitForUi(250);
        check("Structure automatically refreshes after parser finishes",
              contains(QStringLiteral("refreshed_symbol")) && !contains(QStringLiteral("original_symbol")));

        auto next = std::make_shared<CppParser>();
        next->setOnGetFileStream([](const QString &file, QStringList &lines) {
            if (!file.endsWith(QStringLiteral("structure_next.cpp"))) return false;
            lines = {QStringLiteral("int next_symbol = 3;")};
            return true;
        });
        const QString nextFile = QStringLiteral("structure_next.cpp");
        CppParser::parseFileBlocking(next, nextFile, false, QString(), false, false);
        // Queue a completion from the old parser, then switch before delivery.
        emit parser->parseFinished(0, 0);
        int refreshes = 0;
        QObject::connect(&model, &ClassBrowserModel::refreshStarted, &model, [&] { ++refreshes; });
        model.beginUpdate(); model.setParser(next); model.setCurrentFile(nextFile); model.endUpdate();
        const int afterSwitch = refreshes;
        waitForUi(150);
        check("Structure ignores queued completions from previous parser",
              refreshes == afterSwitch && contains(QStringLiteral("next_symbol")));
        model.beginUpdate(); model.setCurrentFile(QStringLiteral("structure_missing.cpp")); model.endUpdate();
        check("Structure does not show symbols of a different current file", model.rowCount({}) == 0);
        model.setParser(nullptr);
        check("Structure clears when the parser is detached", model.rowCount({}) == 0);
    }
    {
        // Block real parser workers deterministically so the indicator can be
        // observed throughout an analysis and during a parser switch.
        auto *view = window.findChild<QTreeView *>("classBrowser");
        auto *model = view ? qobject_cast<ClassBrowserModel *>(view->model()) : nullptr;
        auto *indicator = window.findChild<QLabel *>("structureUpdateIndicator");
        check("Structure analysis indicator and browser are available", model && indicator);
        if (model && indicator) {
            const PCppParser originalParser = model->parser();
            const QString originalFile = model->currentFile();
            const ProjectClassBrowserType originalType = model->classBrowserType();
            auto first = std::make_shared<CppParser>();
            auto second = std::make_shared<CppParser>();
            QSemaphore firstStarted, firstFinish, secondStarted, secondFinish;
            first->setOnGetFileStream([&](const QString &file, QStringList &lines) {
                if (!file.endsWith(QStringLiteral("indicator_first.cpp"))) return false;
                firstStarted.release();
                if (!firstFinish.tryAcquire(1, 10000)) return false;
                lines = QStringList{QStringLiteral("int first_indicator_symbol = 1;")};
                return true;
            });
            second->setOnGetFileStream([&](const QString &file, QStringList &lines) {
                if (!file.endsWith(QStringLiteral("indicator_second.cpp"))) return false;
                secondStarted.release();
                if (!secondFinish.tryAcquire(1, 10000)) return false;
                lines = QStringList{QStringLiteral("int second_indicator_symbol = 2;")};
                return true;
            });
            model->beginUpdate();
            model->setParser(first);
            model->setCurrentFile(QStringLiteral("indicator_first.cpp"));
            model->endUpdate();
            waitForUi(100);
            check("Structure indicator is hidden when analysis is idle", indicator->isHidden());
            std::thread firstWorker([&] {
                CppParser::parseFileBlocking(first, QStringLiteral("indicator_first.cpp"), false, QString(), false, true);
            });
            const bool firstBusy = firstStarted.tryAcquire(1, 3000);
            waitForUi(150);
            check("Structure indicator appears while the current parser is active",
                  firstBusy && model->parsingInProgress() && !indicator->isHidden());
            const QString blinkBefore = indicator->text();
            waitForUi(850);
            check("Structure indicator blinks during active analysis",
                  firstBusy && !indicator->isHidden() && indicator->text() != blinkBefore);
            model->beginUpdate();
            model->setParser(second);
            model->setCurrentFile(QStringLiteral("indicator_second.cpp"));
            model->endUpdate();
            waitForUi(100);
            check("Switching to an idle parser clears the structure indicator",
                  !model->parsingInProgress() && indicator->isHidden());
            std::thread secondWorker([&] {
                CppParser::parseFileBlocking(second, QStringLiteral("indicator_second.cpp"), false, QString(), false, true);
            });
            const bool secondBusy = secondStarted.tryAcquire(1, 3000);
            waitForUi(150);
            check("Structure indicator follows analysis of the new file",
                  secondBusy && model->parsingInProgress() && !indicator->isHidden());
            firstFinish.release();
            firstWorker.join();
            waitForUi(150);
            check("Old parser completion cannot hide the new parser indicator",
                  secondBusy && model->parsingInProgress() && !indicator->isHidden());
            secondFinish.release();
            secondWorker.join();
            waitForUi(200);
            check("Structure indicator disappears when analysis finishes",
                  !model->parsingInProgress() && indicator->isHidden());
            model->beginUpdate();
            model->setParser(originalParser);
            model->setClassBrowserType(originalType);
            model->setCurrentFile(originalFile);
            model->endUpdate();
        }
    }
    {
        // resetParser uses the parser's busy state but does not emit
        // parseFinished. Attach during onBusy to reproduce that race exactly.
        auto *view = window.findChild<QTreeView *>("classBrowser");
        auto *model = view ? qobject_cast<ClassBrowserModel *>(view->model()) : nullptr;
        auto *indicator = window.findChild<QLabel *>("structureUpdateIndicator");
        if (model && indicator) {
            const PCppParser originalParser = model->parser();
            const QString originalFile = model->currentFile();
            const ProjectClassBrowserType originalType = model->classBrowserType();
            auto parser = std::make_shared<CppParser>();
            bool observedReset = false;
            const auto resetConnection = QObject::connect(parser.get(), &CppParser::onBusy, &window, [&] {
                model->beginUpdate();
                model->setParser(parser);
                model->setCurrentFile(QStringLiteral("reset_indicator.cpp"));
                model->endUpdate();
                observedReset = model->parsingInProgress() && !indicator->isHidden();
            }, Qt::DirectConnection);
            parser->resetParser();
            // The parser destructor also resets it; do not reattach a parser
            // while its last shared owner is being destroyed.
            QObject::disconnect(resetConnection);
            check("Reset fixture observes the structure indicator during parser maintenance", observedReset);
            waitForUi(1500);
            check("Structure indicator clears after parser maintenance without parseFinished",
                  !parser->parsing() && !model->parsingInProgress() && indicator->isHidden());
            model->beginUpdate();
            model->setParser(originalParser);
            model->setClassBrowserType(originalType);
            model->setCurrentFile(originalFile);
            model->endUpdate();
        }
    }
    {
        // Keep one parser busy on another file while asking a restored editor
        // whether its already-scanned file needs a conservative reparse.
        auto parser = std::make_shared<CppParser>();
        parser->setEnabled(true);
        auto entered = std::make_shared<QSemaphore>();
        auto proceed = std::make_shared<QSemaphore>();
        parser->setOnGetFileStream([entered, proceed](const QString &file, QStringList &lines) {
            if (file.endsWith(QStringLiteral("readiness-target.cpp")))
                lines = QStringList{QStringLiteral("int readiness_target = 1;")};
            else if (file.endsWith(QStringLiteral("readiness-active.cpp"))) {
                entered->release();
                if (!proceed->tryAcquire(1, 10000)) return false;
                lines = QStringList{QStringLiteral("int readiness_active = 2;")};
            } else if (file.endsWith(QStringLiteral("readiness-pending.cpp")))
                lines = QStringList{QStringLiteral("int readiness_pending = 3;")};
            else return false;
            return true;
        });
        const QString target = QStringLiteral("readiness-target.cpp");
        CppParser::parseFileBlocking(parser, target, false, QString(), false, true);
        Editor fixture(&window);
        fixture.setFilename(target);
        fixture.setCppParser(parser);
        check("Editor knows an idle scanned file does not need reparse", !fixture.needReparse());
        std::thread worker([parser] {
            CppParser::parseFileBlocking(parser, QStringLiteral("readiness-active.cpp"), false, QString(), false, true);
        });
        const bool busy = entered->tryAcquire(1, 3000);
        const bool conservative = busy && fixture.needReparse();
        const bool hiddenWhileBusy = busy && !parser->fileScanned(target);
        if (busy)
            CppParser::parseFileNonBlocking(parser, QStringLiteral("readiness-pending.cpp"), false, QString());
        // Always unblock and join before assertions or fixture teardown.
        proceed->release();
        worker.join();
        check("Busy parser makes editor reparse check conservative", conservative);
        check("Busy parser does not expose scanned-file state", hiddenWhileBusy);
        QElapsedTimer deadline;
        deadline.start();
        while ((!parser->fileScanned(QStringLiteral("readiness-pending.cpp")) || parser->parsing())
               && deadline.elapsed() < 5000)
            waitForUi(20);
        check("Overlapping request completes after conservative readiness check",
              busy && parser->fileScanned(QStringLiteral("readiness-pending.cpp")));
        check("Editor readiness returns to idle after analysis", !fixture.needReparse());
    }
    {
        // Hold the reset worker immediately after cacheResetFinished. This
        // exposes the exact gap where parsing is false but a parse is queued.
        const auto parser = std::make_shared<CppParser>();
        parser->setEnabled(true);
        const auto latest = std::make_shared<std::atomic<bool>>(false);
        const auto resetEntered = std::make_shared<QSemaphore>();
        const auto releaseReset = std::make_shared<QSemaphore>();
        const auto resetOffGui = std::make_shared<std::atomic<bool>>(false);
        QThread *const guiThread = QCoreApplication::instance()->thread();
        parser->setOnGetFileStream([latest](const QString &filename, QStringList &lines) {
            if (!filename.endsWith(QStringLiteral("snapshot-reset-gap.cpp"))) return false;
            lines = QStringList{latest->load() ? QStringLiteral("int snapshot_new = 2;")
                                               : QStringLiteral("int snapshot_old = 1;")};
            return true;
        });
        const QString file = QStringLiteral("snapshot-reset-gap.cpp");
        CppParser::parseFileBlocking(parser, file, false, QString());
        ClassBrowserModel model;
        model.beginUpdate();
        model.setParser(parser);
        model.setCurrentFile(file);
        model.endUpdate();
        const auto contains = [&model](const QString &name) {
            for (int row = 0; row < model.rowCount({}); ++row)
                if (model.data(model.index(row, 0, {}), Qt::DisplayRole).toString().contains(name))
                    return true;
            return false;
        };
        check("Reset gap fixture starts with the previous structure snapshot",
              contains(QStringLiteral("snapshot_old")));
        QObject::connect(parser.get(), &CppParser::cacheResetFinished, &model,
                         [resetEntered, releaseReset, resetOffGui, guiThread] {
            resetOffGui->store(QThread::currentThread() != guiThread);
            resetEntered->release();
            releaseReset->tryAcquire(1, 10000);
        }, Qt::DirectConnection);
        auto unblock = finally([releaseReset] { releaseReset->release(); });
        const bool frozen = parser->freeze();
        if (frozen) {
            latest->store(true);
            CppParser::resetParsedResultsNonBlocking(parser);
            CppParser::parseFileNonBlocking(parser, file, false, QString());
            parser->unFreeze();
        }
        bool reached = false;
        QElapsedTimer deadline;
        deadline.start();
        while (frozen && !reached && deadline.elapsed() < 5000) {
            reached = resetEntered->tryAcquire(1, 0);
            if (!reached) waitForUi(20);
        }
        const bool pending = reached && parser->hasPendingWork();
        const bool idleGap = reached && !parser->parsing();
        if (reached) model.fillStatements();
        const bool oldKept = reached && contains(QStringLiteral("snapshot_old"));
        check("Reset gap is reached outside the GUI with analysis queued",
              reached && pending && idleGap && resetOffGui->load());
        check("Structure preserves the snapshot while post-reset analysis is queued", oldKept);
        releaseReset->release();
        deadline.restart();
        while (reached && !contains(QStringLiteral("snapshot_new")) && deadline.elapsed() < 10000)
            waitForUi(20);
        check("Structure refreshes after the queued post-reset analysis finishes",
              reached && contains(QStringLiteral("snapshot_new"))
                  && !contains(QStringLiteral("snapshot_old")));
    }
    if (qEnvironmentVariableIsSet("REDPANDA_QA_BUSY_ONLY")) {
        QFile output(QDir(outputDirectory).filePath("parser-busy-result.json"));
        const QByteArray data = QJsonDocument(QJsonObject{{"checks", checks}, {"failures", failures}}).toJson();
        if (!output.open(QIODevice::WriteOnly) || output.write(data) != data.size()) return 2;
        return failures == 0 ? 0 : 1;
    }
    const auto *aboutAction = window.findChild<QAction *>("actionAbout");
    check("About menu uses the current application artwork", aboutAction && aboutAction->icon().pixmap(32, 32).toImage() == QIcon(AppIdentity::iconResource()).pixmap(32, 32).toImage());
    auto *cleanAction = window.findChild<QAction *>("actionMakeClean");
    const auto *executeMenu = window.findChild<QMenu *>("menuExecute");
    const auto *projectMenu = window.findChild<QMenu *>("menuProject");
    check("Native artifact cleanup is in the compilation menu", cleanAction && executeMenu && executeMenu->actions().contains(cleanAction) && projectMenu && !projectMenu->actions().contains(cleanAction));
    const auto *toolsMenu = window.findChild<QMenu *>("menuTools");
    bool legacyTool = false, userTool = false;
    if (toolsMenu) for (const auto *action : toolsMenu->actions()) {
        legacyTool = legacyTool || action->text().contains("Rimuovi compilato", Qt::CaseInsensitive) || action->text().contains("Remove Compiled", Qt::CaseInsensitive);
        userTool = userTool || action->text().contains("QA custom tool");
    }
    check("Legacy cleanup tool is migrated", !legacyTool);
    check("User-defined tools are preserved", userTool);
    auto *snippetAction = window.findChild<QAction *>("actionInsert_Complete_Snippet");
    check("Snippet command has a dedicated default shortcut", snippetAction && snippetAction->shortcut() == QKeySequence("Ctrl+Alt+Space"));
    EnvironmentShortcutModel shortcutModel; shortcutModel.reload(); bool configurable = false;
    for (const auto &shortcut : shortcutModel.shortcuts()) if (shortcut->name == "actionInsert_Complete_Snippet") configurable = true;
    check("Snippet shortcut appears in preferences", configurable);
    if (snippetAction) if (auto *editor = window.editorManager()->getEditor()) {
        check("Dedicated snippets work with current code-completion setting", pSettings->environment().language() != "it_IT" || !pSettings->codeCompletion().enabled());
        const auto previous = window.codeSnippetManager()->snippets();
        auto sample = std::make_shared<CodeSnippet>(); sample->caption = "QA snippet"; sample->prefix = "qa_snip";
        sample->code = "int answer = %REPL_BEGIN%42%REPL_END%;\n%INSERT%"; sample->language = "cpp"; sample->enabled = true; sample->section = 0;
        window.codeSnippetManager()->setSnippets({sample});
        editor->setContent("qa_snip"); editor->setCaretXY({7, 0}); editor->setFocus(); window.activateWindow(); waitForUi(80);
        QKeyEvent key(QEvent::KeyPress, Qt::Key_Space, Qt::ControlModifier | Qt::AltModifier);
        QApplication::sendEvent(editor, &key);
        check("Keyboard shortcut expands the prefix", editor->text().contains("int answer = 42;") && !editor->text().contains("qa_snip"));
        check("Snippet replacement placeholder is selected", editor->selText() == "42");
        QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier); QApplication::sendEvent(editor, &tab);
        check("Tab advances to the snippet insertion stop", !editor->selAvail() && editor->caretY() == 1);
        editor->undo(); check("Snippet expansion is a single undo operation", editor->text().trimmed() == "qa_snip");
        editor->setContent("qa_sni"); editor->setCaretXY({6, 0}); snippetAction->trigger();
        check("Partial snippet prefix is completed", editor->text().contains("int answer = 42;"));
        editor->setContent("qa_snip"); editor->setCaretXY({7, 0}); editor->setReadOnly(true);
        check("Snippet expansion respects readonly documents", !editor->expandCodeSnippet(sample, "qa_snip") && editor->text().trimmed() == "qa_snip"); editor->setReadOnly(false);
        auto foreign = std::make_shared<CodeSnippet>(*sample); foreign->language = "c";
        check("Snippet expansion respects document language", !editor->expandCodeSnippet(foreign, "qa_snip"));
        sample->enabled = false; check("Disabled snippet is not inserted", !editor->expandCodeSnippet(sample, "qa_snip")); sample->enabled = true;
        editor->setContent("otherqa_snip"); editor->setCaretXY({12, 0}); check("Snippet prefix respects word boundaries", !editor->expandCodeSnippet(sample, "qa_snip"));
        auto legacy = std::make_shared<CodeSnippet>(*sample); legacy->prefix = "for(;;)"; window.codeSnippetManager()->setSnippets({legacy});
        editor->setContent("for(;;)"); editor->setCaretXY({7, 0}); snippetAction->trigger(); check("Legacy punctuation prefix is expanded", editor->text().contains("int answer = 42;"));
        window.codeSnippetManager()->setSnippets({sample}); editor->setContent(""); editor->setCaretXY({0, 0});
        QTimer::singleShot(0, &window, [] { for (auto *widget : QApplication::topLevelWidgets()) if (auto *picker = qobject_cast<QInputDialog *>(widget)) picker->reject(); });
        snippetAction->trigger(); check("Snippet picker cancellation preserves the document", editor->text().trimmed().isEmpty());
        editor->setContent("replace this block"); editor->setSelBeginEnd({0, 0}, {18, 0});
        QTimer::singleShot(0, &window, [] { for (auto *widget : QApplication::topLevelWidgets()) if (auto *picker = qobject_cast<QInputDialog *>(widget)) picker->accept(); });
        snippetAction->trigger(); check("Snippet picker replaces a selected code block", editor->text().contains("int answer = 42;") && !editor->text().contains("replace this block"));
        editor->undo(); check("Selected code block is restored by undo", editor->text().trimmed() == "replace this block");
        check("Snippet shortcut is scoped to the editor", snippetAction->shortcutContext() == Qt::WidgetWithChildrenShortcut);
        auto binding = std::make_shared<EnvironmentShortcut>(); binding->name = snippetAction->objectName(); binding->shortcut = "Ctrl+Alt+F10"; binding->isAction = true;
        ShortcutManager shortcuts; shortcuts.setShortcuts({binding}); shortcuts.save(); ShortcutManager reloaded; reloaded.load(); reloaded.applyTo(snippetAction);
        check("Custom snippet shortcut survives persistence", snippetAction->shortcut() == QKeySequence("Ctrl+Alt+F10"));
        editor->setContent("qa_snip"); editor->setCaretXY({7, 0}); editor->setFocus();
        QKeyEvent customKey(QEvent::KeyPress, Qt::Key_F10, Qt::ControlModifier | Qt::AltModifier); QApplication::sendEvent(editor, &customKey);
        check("Configured shortcut expands the snippet", editor->text().contains("int answer = 42;"));
        shortcuts.setShortcuts({}); shortcuts.save(); snippetAction->setShortcut(QKeySequence("Ctrl+Alt+Space"));
        window.codeSnippetManager()->setSnippets(previous); editor->setContent(""); editor->setModified(false);
    }
    {
        const QString sourcePath = QDir(outputDirectory).filePath("cleanup-sample.cpp");
        const QString executablePath = QDir(outputDirectory).filePath("cleanup-sample.exe");
        const QString objectPath = QDir(outputDirectory).filePath("cleanup-sample.o");
        const QString unrelated = QDir(outputDirectory).filePath("unrelated.o");
        for (const auto &path : {sourcePath, executablePath, objectPath, unrelated}) { QFile file(path); if (file.open(QIODevice::WriteOnly)) file.write("QA fake artifact, never compiled"); }
        window.openFile(sourcePath); waitForUi(80);
        check("Artifact cleanup supports a single source file", cleanAction && cleanAction->isEnabled());
        auto cancelCleanup = [&window, &check, &outputDirectory] {
            QTimer::singleShot(0, &window, [&check, &outputDirectory] {
                for (auto *widget : QApplication::topLevelWidgets()) if (widget->objectName() == "buildArtifactsDialog") {
                    auto *dialog = qobject_cast<QDialog *>(widget); auto *list = dialog->findChild<QTreeWidget *>("buildArtifactsList");
                    check("Cleanup preview lists only the expected executable and object", list && list->topLevelItemCount() == 2);
                    check("Cleanup preview screenshot exported", dialog->grab().save(QDir(outputDirectory).filePath("cleanup-preview.png")));
                    dialog->reject();
                }
            });
        };
        if (cleanAction && cleanAction->isEnabled()) { cancelCleanup(); cleanAction->trigger(); }
        check("Cancelled cleanup preserves every file", QFile::exists(sourcePath) && QFile::exists(executablePath) && QFile::exists(objectPath) && QFile::exists(unrelated));
        if (cleanAction && cleanAction->isEnabled()) {
            QTimer cleanupErrors;
            cleanupErrors.setInterval(5);
            QObject::connect(&cleanupErrors, &QTimer::timeout, &window, [&] {
                auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                if (!message || message->windowTitle() != QCoreApplication::translate("MainWindow", "Clean Build Artifacts")) return;
                trace(QJsonObject{{"cleanupError", message->text()}});
                if (auto *ok = message->button(QMessageBox::Ok)) ok->click();
            });
            cleanupErrors.start();
            QTimer::singleShot(0, &window, [] {
                for (auto *widget : QApplication::topLevelWidgets()) if (widget->objectName() == "buildArtifactsDialog") {
                    auto *dialog = qobject_cast<QDialog *>(widget); auto *list = dialog->findChild<QTreeWidget *>("buildArtifactsList");
                    for (int row = 0; list && row < list->topLevelItemCount(); ++row) if (list->topLevelItem(row)->text(0).endsWith(".o")) list->topLevelItem(row)->setCheckState(0, Qt::Unchecked);
                    dialog->accept();
                }
            });
            cleanAction->trigger();
            cleanupErrors.stop();
            check("Selected fake executable moved out of the build directory", !QFile::exists(executablePath));
            check("Cleanup preserves source, unchecked object and unrelated artifact", QFile::exists(sourcePath) && QFile::exists(objectPath) && QFile::exists(unrelated));
        }
    }
    const QStringList iconFamilies{"fluent-soft", "adwaita-symbolic", "breeze-lines", "cupertino-compact"};
    const auto availableIconSets = window.iconsManager()->listIconSets();
    const QList<QString> samples{"actions/01File-01New.svg", "actions/01File-02Open.svg", "actions/01File-03Save.svg", "actions/03Edit-08Search.svg", "actions/05Run-03Run.svg", "actions/05Run-06Debug.svg", "actions/00Misc-05Gear.svg", "actions/03Edit-04Copy.svg", "filesystem/cppfile.svg", "editor/breakpoint.svg"};
    const QPalette initialPalette = qApp->palette();
    for (const QString &mode : {"light", "dark"}) {
        QPalette palette = initialPalette; palette.setColor(QPalette::Window, mode == "dark" ? QColor("#182337") : QColor("#f5f7fb"));
        palette.setColor(QPalette::WindowText, mode == "dark" ? QColor("#f0f5ff") : QColor("#26354a")); qApp->setPalette(palette);
        QImage atlas(600, 248, QImage::Format_ARGB32_Premultiplied); atlas.fill(palette.color(QPalette::Window)); QPainter painter(&atlas);
        painter.setPen(palette.color(QPalette::WindowText));
        for (int row = 0; row < iconFamilies.size(); ++row) {
            const QString family = iconFamilies[row]; bool registered = false;
            for (const auto &item : availableIconSets) if (item->name == family) registered = true;
            check("Icon family is selectable: " + family, registered);
            const QString directory = ":/resources/iconsets/" + family;
            QDirIterator files(directory, {"*.svg"}, QDir::Files, QDirIterator::Subdirectories); int count = 0; bool valid = true;
            while (files.hasNext()) { const QString path = files.next(); QSvgRenderer renderer(path); valid = valid && renderer.isValid(); ++count; }
            check("Complete valid SVG family: " + family, count == 148 && valid);
            painter.drawText(12, row * 60 + 20, family);
            QSet<int> hues;
            for (int column = 0; column < samples.size(); ++column) {
                const auto icon = window.iconsManager()->createSVGIcon(directory + "/" + samples[column], 24, 24);
                check("Rendered adaptive icon: " + family + "/" + QString::number(column), icon && !icon->isNull());
                if (icon) {
                    painter.drawPixmap(210 + column * 37, row * 60 + 17, 24, 24, *icon);
                    const QImage pixels = icon->toImage();
                    for (int y = 0; y < pixels.height(); ++y) for (int x = 0; x < pixels.width(); ++x) {
                        const QColor color = pixels.pixelColor(x, y);
                        if (color.alpha() > 200 && color.hsvSaturation() > 100 && color.value() > 90) hues.insert(color.hsvHue() / 30);
                    }
                }
            }
            check("Multicolor icon family: " + family + "/" + mode, hues.size() >= 5);
        }
        painter.end(); check("Icon atlas exported", atlas.save(QDir(outputDirectory).filePath("iconsets-" + mode + ".png")));
    }
    qApp->setPalette(initialPalette);
    check("Italian catalog active", language != "it_IT" || prefText("Options") == "Preferenze");
    if (auto *editor = window.editorManager()->getEditor()) { editor->setContent("#include <iostream>\n\nint main() {\n    // Example used only by the isolated UI test\n    const auto answer = 42;\n    std::cout << answer << std::endl;\n    return 0;\n}\n"); editor->setModified(true); }
    auto dialog = SettingsDialog::optionDialog(window.colorManager(), window.iconsManager(), &window);
    dialog->resize(1120, 790); dialog->show(); waitForUi();
    check("Preference pages are created lazily", dialog->createdPageCount() == 1 && dialog->pageCount() > 20);
    auto *themes = dialog->findChild<QComboBox *>("cbTheme"); check("Theme selector available", themes != nullptr);
    if (themes) {
        for (const QString &name : {"dawn", "paper", "mist", "slate", "dusk", "nord", "midnight", "forest"}) {
            const int index = themes->findData(name); check("Theme exists: " + name, index >= 0); if (index < 0) continue;
            pSettings->environment().setTheme(name);
            ThemeManager manager; auto theme = manager.theme(name); pSettings->editor().setColorScheme(theme->defaultColorScheme());
            pSettings->environment().setIconSet(theme->defaultIconSet()); window.applySettings(); themes->setCurrentIndex(index); waitForUi(60);
            auto *preview = dialog->findChild<QFrame *>("themePreview");
            check("Preview palette: " + name, preview && preview->palette().color(QPalette::Window) == theme->palette().color(QPalette::Window));
            if (name == "dawn" || name == "slate" || name == "midnight") {
                check("Rendered IDE: " + name, window.grab().save(QDir(outputDirectory).filePath("ide-" + name + ".png")));
                check("Rendered preference preview: " + name, dialog->grab().save(QDir(outputDirectory).filePath("preferences-" + name + ".png")));
            }
        }
    }
    for (const QString &family : iconFamilies) {
        pSettings->environment().setIconSet(family); window.applySettings(); waitForUi(50);
        check("Icon family applied in IDE: " + family, window.grab().save(QDir(outputDirectory).filePath("ide-icons-" + family + ".png")));
    }
    auto *search = dialog->findChild<QLineEdit *>("preferencesSearch"); auto *tree = dialog->findChild<QTreeView *>("widgetsView");
    if (search && tree) { search->setText("recovery"); waitForUi(); check("Preference search filters categories", tree->model()->rowCount() > 0 && tree->model()->rowCount() < 3); search->clear(); waitForUi(); }
    auto answer = [&dialog](QMessageBox::StandardButton button) {
        QTimer::singleShot(0, dialog.get(), [button] { for (auto *w : QApplication::topLevelWidgets()) if (auto *m = qobject_cast<QMessageBox *>(w)) if (m->standardButtons().testFlag(button) && m->button(button)) m->button(button)->click(); });
    };
    answer(QMessageBox::Cancel);
    dialog->setCurrentWidget(prefText("Snippet"), prefText("Editor"));
    check("Cancel preserves current preference page", dialog->createdPageCount() == 1);
    answer(QMessageBox::Discard);
    check("Snippet preference page available", dialog->setCurrentWidget(prefText("Snippet"), prefText("Editor"))); waitForUi();
    auto *snippets = dialog->findChild<QTableView *>("tblSnippets"); check("Snippet table available", snippets != nullptr);
    if (snippets) {
        const int activeCount = window.codeSnippetManager()->snippets().size();
        snippets->setCurrentIndex(snippets->model()->index(0, 0));
        const QString duplicateLabel = QCoreApplication::translate("EditorSnippetWidget", "Duplicate");
        for (auto *button : dialog->findChildren<QPushButton *>()) if (button->text() == duplicateLabel) { button->click(); break; }
        check("Duplicated snippet remains unapplied", window.codeSnippetManager()->snippets().size() == activeCount);
        check("Duplicate adds one draft", snippets->model()->rowCount() == activeCount + 1);
        auto *filter = dialog->findChild<QLineEdit *>("snippetSearch");
        if (filter) { filter->setText("c_main"); waitForUi(40); check("Snippet search filters rows", snippets->model()->rowCount() == 1); filter->clear(); }
        check("Snippet controls rendered", dialog->grab().save(QDir(outputDirectory).filePath("snippets.png")));
    }
    dialog->close(); dialog.reset();
    const QString originalPath = QDir(outputDirectory).filePath("original.cpp"); QFile original(originalPath);
    if (!original.open(QIODevice::WriteOnly) || original.write("original disk text") != 18) return 2; original.close();
    const QString journal = QDir(pSettings->dirs().config()).filePath("recovery/simulated-interruption.json"); QString error;
    check("Simulated interrupted session saved", SessionStore::write(journal, {{originalPath, "recovered unsaved text", 0, 0}}, error));
    QTimer::singleShot(0, &window, [&window] {
        for (auto *widget : QApplication::topLevelWidgets()) if (auto *message = qobject_cast<QMessageBox *>(widget))
            if (message->parentWidget() == &window && message->standardButtons().testFlag(QMessageBox::Yes)) message->button(QMessageBox::Yes)->click();
    });
    recovery.restorePreviousSessions(); waitForUi(400);
    bool recovered = false;
    for (int i = 0; i < window.editorManager()->pageCount(); ++i) {
        auto *editor = (*window.editorManager())[i];
        if (editor && editor->text().contains("recovered unsaved text") && editor->isNew() && editor->modified()) recovered = true;
    }
    check("Unsaved text restored into a new document", recovered);
    check("Recovery original is readable", original.open(QIODevice::ReadOnly)); check("Recovery preserved original disk file", original.readAll() == "original disk text"); original.close();
    {
        auto *table = window.findChild<IssuesTable *>("tableIssues");
        check("Diagnostic catalog contains 162 reviewed rules", DiagnosticCatalog::ruleCount() == 162);
        check("Diagnostic activation follows the loaded interface", DiagnosticCatalog::isItalianInterface() == (language == "it_IT"));
        QFile corpus(":/config/gcc-diagnostic-samples.json"); check("Real GCC corpus available", corpus.open(QIODevice::ReadOnly));
        const auto samples = QJsonDocument::fromJson(corpus.readAll()).object().value("samples").toArray();
        const QString demoPath = QDir(outputDirectory).filePath("diagnostic-demo.cpp");
        QFile demo(demoPath); if (demo.open(QIODevice::WriteOnly)) demo.write(samples.first().toObject().value("source").toString().toUtf8()); demo.close();
        Editor *editor = window.openFile(demoPath); waitForUi(60);
        if (table && editor) {
            table->clearIssues(); auto *model = table->issuesModel();
            GccDiagnosticParserProbe probe;
            QVector<PCompileIssue> parsed;
            QString firstEditorHint;
            QObject::connect(&probe, &Compiler::compileIssue, &window, [&window, &parsed](const PCompileIssue &issue) { parsed.append(issue); window.onCompileIssue(issue); });
            for (const auto &sample : samples) {
                const auto object = sample.toObject();
                // Use each original source so tooltip positions cannot overlap previous fixtures.
                editor->setContent(object.value("source").toString()); editor->clearSyntaxIssues();
                const QString original = object.value("severity").toString() + ": " + object.value("message").toString();
                probe.feed(demoPath + ":" + QString::number(object.value("line").toInt()) + ":" + QString::number(object.value("column").toInt()) + ": " + original); probe.finish();
                const int row = parsed.size() - 1;
                check("GCC parser preserves original diagnostic: " + QString::number(row), row >= 0 && parsed[row]->originalDescription == original && model->originalDescription(row) == original);
                if (row < 0) continue;
                check("GCC parser preserves positions and severity: " + QString::number(row), parsed[row]->line == object.value("line").toInt() - 1 && parsed[row]->column == object.value("column").toInt() - 1 && parsed[row]->type == (object.value("severity").toString() == "warning" ? CompileIssueType::Warning : CompileIssueType::Error));
                check("GCC origin is recorded: " + QString::number(row), parsed[row]->gccDiagnostic);
                check("Diagnostic translated only in Italian: " + QString::number(row), language == "it_IT" ? !model->translatedDescription(row).isEmpty() : model->translatedDescription(row).isEmpty());
                const auto issue = parsed[row];
                const QString expected = (issue->type == CompileIssueType::Warning ? "[Avvertimento] " : "[Errore] ") + object.value("italian").toString();
                const QString panel = model->data(model->index(row, 3), Qt::DisplayRole).toString();
                check("Complete panel text matches reviewed translation: " + QString::number(row), panel == (language == "it_IT" ? expected : issue->description));
                const auto hint = editor->getSyntaxIssueAtPosition({std::min(issue->column, editor->lineText(issue->line).length()), issue->line});
                check("Editor and panel have identical full diagnostic hints: " + QString::number(row), hint && hint->hint == model->diagnosticHint(row));
                if (row == 0 && hint) firstEditorHint = hint->hint;
                const QString expectedTooltip = "<p>" + model->diagnosticHint(row).toHtmlEscaped().replace('\n', "<br>") + "</p>";
                check("Panel tooltip matches the editor hint safely: " + QString::number(row), model->data(model->index(row, 3), Qt::ToolTipRole).toString() == (language == "it_IT" ? expectedTooltip : issue->description));
                if (language == "it_IT") {
                    check("Original column preserves exact GCC message: " + QString::number(row), model->data(model->index(row, 4), Qt::DisplayRole).toString() == original);
                    check("Both full texts remain in each diagnostic hint: " + QString::number(row), hint && hint->hint.contains(expected) && hint->hint.contains(original));
                }
            }
            check("All real GCC samples displayed", parsed.size() == samples.size() && table->count() == samples.size());
            // Restore the first fixture for the navigation and preview checks.
            editor->setContent(samples.first().toObject().value("source").toString()); editor->clearSyntaxIssues();
            if (!parsed.isEmpty()) editor->addSyntaxIssues(parsed.first()->line, parsed.first()->column, parsed.first()->endColumn, parsed.first()->type, firstEditorHint);
            check("Original column is restricted to Italian UI", model->columnCount({}) == (language == "it_IT" ? 5 : 4));
            const QString original = model->originalDescription(0);
            table->setCurrentIndex(model->index(0, 3));
            bool nativeClipboardDenied = false;
#ifdef Q_OS_WIN
            if (QGuiApplication::platformName() == "windows") {
                SetLastError(ERROR_SUCCESS);
                const bool available = OpenClipboard(nullptr);
                const DWORD clipboardError = GetLastError();
                if (available) CloseClipboard();
                nativeClipboardDenied = !available && clipboardError == ERROR_ACCESS_DENIED;
            }
#endif
            // A denied native clipboard is an explicit environment limitation.
            // Never convert unrelated failures or a text mismatch into a skip.
            QMimeData *savedClipboard = nullptr;
            if (!nativeClipboardDenied) {
                savedClipboard = new QMimeData;
                if (const auto *previous = QGuiApplication::clipboard()->mimeData())
                    for (const auto &format : previous->formats()) savedClipboard->setData(format, previous->data(format));
            }
            check("Copy original action invoked", QMetaObject::invokeMethod(&window, "onTableIssuesCopy", Qt::DirectConnection));
            const bool copied = QGuiApplication::clipboard()->text() == original;
            if (!copied && nativeClipboardDenied) {
                skip("Clipboard contains exact GCC message", "Native Windows OpenClipboard(NULL) returned ERROR_ACCESS_DENIED (5) in the restricted QA process.");
                const QString feedback = QCoreApplication::translate("MainWindow", "Cannot copy the diagnostic to the clipboard. Please retry.");
                check("Denied native clipboard reports a recoverable translated copy failure", window.statusBar()->currentMessage() == feedback);
                check("Denied native clipboard preserves the original diagnostic and selection", model->originalDescription(0) == original && table->currentIndex() == model->index(0, 3));
                check("Denied native clipboard copy can be retried", QMetaObject::invokeMethod(&window, "onTableIssuesCopy", Qt::DirectConnection)
                      && window.statusBar()->currentMessage() == feedback && model->originalDescription(0) == original);
            } else {
                check("Clipboard contains exact GCC message", copied);
            }
            if (savedClipboard) QGuiApplication::clipboard()->setMimeData(savedClipboard);
            check("Text export includes the original", table->toTxt().contains(original));
            check("Exported positions match GCC and the displayed table", table->toTxt().contains("\t" + QString::number(parsed.first()->line + 1) + "\t" + QString::number(parsed.first()->column + 1) + "\t"));
            if (language == "it_IT") {
                check("Tooltip includes Italian and original", model->diagnosticHint(0).contains(original) && model->diagnosticHint(0).contains("non è stato dichiarato"));
                const auto first = parsed.first(); auto hint = editor->getSyntaxIssueAtPosition({first->column, first->line});
                check("Editor retains bilingual diagnostic hint", hint && hint->hint.contains("Originale:") && hint->hint.contains(original));
                const QString savedLanguage = pSettings->environment().language(); pSettings->environment().setLanguage("en");
                check("Pending preference language change does not change active diagnostics", DiagnosticCatalog::isItalianInterface() && model->italianDiagnostics()); pSettings->environment().setLanguage(savedLanguage);
            }
            check("Navigation to source still works", QMetaObject::invokeMethod(&window, "on_tableIssues_doubleClicked", Qt::DirectConnection, Q_ARG(QModelIndex, model->index(0, 3))));
            check("Caret uses original GCC coordinates", window.editorManager()->getEditor()->caretXY().ch == parsed.first()->column && window.editorManager()->getEditor()->caretXY().line == parsed.first()->line);
            probe.setFamily(CompilerType::Clang);
            probe.feed(demoPath + ":1:1: warning: unused variable 'other_compiler'"); probe.finish();
            check("Other compiler diagnostic is not translated", !parsed.last()->gccDiagnostic && model->translatedDescription(model->count() - 1).isEmpty());
            auto unknown = std::make_shared<CompileIssue>(); unknown->filename = demoPath; unknown->line = 0; unknown->column = 0; unknown->endColumn = -1; unknown->type = CompileIssueType::Error; unknown->gccDiagnostic = true;
            unknown->description = "A new GCC diagnostic with no catalog rule"; unknown->originalDescription = "error: " + unknown->description; window.onCompileIssue(unknown);
            check("Unknown GCC message remains unchanged", model->translatedDescription(model->count() - 1).isEmpty() && model->data(model->index(model->count() - 1, 3), Qt::DisplayRole).toString() == unknown->description);
            auto complex = std::make_shared<CompileIssue>(*unknown); complex->description = "invalid conversion from 'std::vector<int>' to 'const void*' [-fpermissive]"; complex->originalDescription = "error: " + complex->description; window.onCompileIssue(complex);
            const auto html = table->toHtml(); check("HTML export preserves template types safely", html.contains("std::vector&lt;int&gt;") && !html.contains("std::vector<int>"));
            QMetaObject::invokeMethod(&window, "on_actionIssues_triggered", Qt::DirectConnection); waitForUi(100);
            check("Bilingual diagnostic panel screenshot exported", window.grab().save(QDir(outputDirectory).filePath("gcc-diagnostics.png")));
            check("Bilingual table screenshot exported", table->grab().save(QDir(outputDirectory).filePath("gcc-diagnostics-table.png")));
            // Reproduce a value-less return independently of the other fixture positions.
            const auto returnSample = std::find_if(samples.cbegin(), samples.cend(), [](const QJsonValue &sample) {
                return sample.toObject().value("case").toString() == "return-no-value-cpp";
            });
            check("Value-less return GCC fixture exists", returnSample != samples.cend());
            if (returnSample != samples.cend()) {
                const auto sample = returnSample->toObject();
                const QString returnPath = QDir(outputDirectory).filePath("return-no-value.cpp");
                QFile source(returnPath);
                const auto sourceBytes = sample.value("source").toString().toUtf8();
                const bool written = source.open(QIODevice::WriteOnly) && source.write(sourceBytes) == sourceBytes.size(); source.close();
                check("Value-less return source written", written);
                Editor *returnEditor = written ? window.openFile(returnPath) : nullptr;
                check("Value-less return editor opened", returnEditor != nullptr);
                if (returnEditor) {
                    table->clearIssues(); GccDiagnosticParserProbe returnProbe;
                    QObject::connect(&returnProbe, &Compiler::compileIssue, &window, &MainWindow::onCompileIssue);
                    const QString raw = "error: " + sample.value("message").toString();
                    const QString translated = "[Errore] " + sample.value("italian").toString();
                    returnProbe.feed(returnPath + ":5:5: " + raw); returnProbe.finish();
                    check("Value-less return has one diagnostic and unchanged original", table->count() == 1 && model->originalDescription(0) == raw);
                    const auto item = model->issue(0);
                    check("Value-less return coordinates and severity preserved", item && item->line == 4 && item->column == 4 && item->type == CompileIssueType::Error);
                    const auto hint = returnEditor->getSyntaxIssueAtPosition({4, 4});
                    if (language == "it_IT") {
                        check("Value-less return Italian text appears in panel", model->data(model->index(0, 3), Qt::DisplayRole).toString() == translated);
                        check("Value-less return panel tooltip includes both texts", model->data(model->index(0, 3), Qt::ToolTipRole).toString().contains(translated.toHtmlEscaped()) && model->data(model->index(0, 3), Qt::ToolTipRole).toString().contains(raw.toHtmlEscaped()));
                        check("Value-less return editor tooltip includes both texts", hint && hint->hint.contains(translated) && hint->hint.contains(raw));
                    } else {
                        check("Value-less return stays untranslated in English", model->translatedDescription(0).isEmpty() && hint && hint->hint == item->description);
                    }
                    table->setColumnWidth(3, 650); if (language == "it_IT") table->setColumnWidth(4, 650);
                    waitForUi(100);
                    check("Value-less return panel screenshot exported", window.grab().save(QDir(outputDirectory).filePath("gcc-return-no-value.png")));
                    check("Value-less return table screenshot exported", table->grab().save(QDir(outputDirectory).filePath("gcc-return-no-value-table.png")));
                }
            }
            {
                IssuesTable preview; preview.resize(1120, 220); preview.setAlternatingRowColors(true);
                for (int row = 0; row < 4; ++row) {
                    auto item = std::make_shared<CompileIssue>(); item->filename = "demo.cpp"; item->line = row; item->column = 8; item->endColumn = -1;
                    item->type = row == 0 ? CompileIssueType::Info : row == 2 ? CompileIssueType::Warning : CompileIssueType::Error;
                    item->description = row == 0 ? "In function 'int main()':" : row == 2 ? "[Warning] unused variable 'numero' [-Wunused-variable]" : "[Error] invalid conversion from 'const char*' to 'int' [-fpermissive]";
                    item->originalDescription = item->description; item->gccDiagnostic = true; preview.addIssue(item);
                }
                preview.setAttribute(Qt::WA_ShowWithoutActivating); preview.show(); waitForUi(40);
                const auto state = QStyle::State_Enabled | QStyle::State_Active;
                DiagnosticDelegateProbe delegate(&preview);
                auto verifyPalette = [&](const QPalette &palette) {
                    bool contrast = true, rendered = true;
                    preview.setPalette(palette);
                    for (const auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) for (const bool selected : {false, true}) for (const bool alternate : {false, true}) for (int row = 0; row < 3; ++row) {
                        QStyleOptionViewItem option; option.initFrom(&preview); option.widget = &preview; option.rect = QRect(0, 0, 540, 48);
                        option.state = group == QPalette::Disabled ? QStyle::State_None : QStyle::State_Enabled;
                        if (group == QPalette::Active) option.state |= QStyle::State_Active;
                        if (selected) option.state |= QStyle::State_Selected;
                        if (alternate) option.features |= QStyleOptionViewItem::Alternate;
                        const QModelIndex cell = preview.issuesModel()->index(row, 3);
                        delegate.initStyleOption(&option, cell);
                        const QColor background = option.backgroundBrush.color();
                        const QColor foreground = option.palette.color(selected ? QPalette::HighlightedText : QPalette::Text);
                        contrast = contrast && UiContrast::ratio(foreground, background) >= 4.5;
                        QImage image(540, 48, QImage::Format_ARGB32_Premultiplied); image.fill(background); QPainter painter(&image); delegate.paint(&painter, option, cell); painter.end();
                        bool textFound = false;
                        for (int y = 5; y < image.height() - 5 && !textFound; ++y) for (int x = 5; x < image.width() - 5; ++x) if (image.pixelColor(x, y) == foreground) { textFound = true; break; }
                        rendered = rendered && textFound && UiContrast::ratio(foreground, image.pixelColor(4, 4)) >= 4.5;
                    }
                    return qMakePair(contrast, rendered);
                };
                ThemeManager allThemes;
                int checkedThemes = 0;
                for (const auto &theme : allThemes.getThemes()) {
                    const QPalette palette = theme->palette();
                    preview.setErrorColor(QColor(Qt::black)); preview.setWarningColor(QColor("#777777"));
                    const auto result = verifyPalette(palette);
                    const QString name = QFileInfo(theme->filename()).completeBaseName(); ++checkedThemes;
                    check("Diagnostic contrast for all row states: " + name, result.first);
                    check("Actual diagnostic painting is readable: " + name, result.second);
                    if (name == "midnight" || name == "dawn" || name == "slate") {
                        preview.setPalette(palette); preview.setErrorColor(name == "dawn" ? QColor("#c74747") : QColor("#ff9b9b")); preview.setWarningColor(name == "dawn" ? QColor("#a26c14") : QColor("#f0d18c"));
                        waitForUi(35); check("Contrast preview exported: " + name, preview.grab().save(QDir(outputDirectory).filePath("diagnostic-contrast-" + name + ".png")));
                        preview.selectRow(1); preview.activateWindow(); preview.setFocus(); waitForUi(35);
                        check("Selected diagnostic preview exported: " + name, preview.grab().save(QDir(outputDirectory).filePath("diagnostic-contrast-" + name + "-selected.png"))); preview.clearSelection();
                    }
                }
                check("All built-in theme families tested", checkedThemes >= 13);
                QPalette pathological; for (auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
                    pathological.setColor(group, QPalette::Window, QColor("#808080")); pathological.setColor(group, QPalette::Base, QColor("#808080")); pathological.setColor(group, QPalette::AlternateBase, QColor("#101828"));
                    pathological.setColor(group, QPalette::Text, QColor("#808080")); pathological.setColor(group, QPalette::Highlight, QColor("#808080")); pathological.setColor(group, QPalette::HighlightedText, QColor("#808080"));
                }
                preview.setErrorColor(QColor()); preview.setWarningColor(QColor(0, 0, 0, 0));
                const auto customResult = verifyPalette(pathological);
                check("Invalid colors and low-contrast custom palettes remain readable", customResult.first && customResult.second);
                auto model = preview.issuesModel(); int updates = 0;
                QObject::connect(model, &QAbstractItemModel::dataChanged, &preview, [&updates] { ++updates; });
                preview.setErrorColor(QColor("#ffffff")); preview.setWarningColor(QColor("#000000"));
                check("Diagnostic color changes repaint existing rows", updates == 2);
                preview.close();
            }

        }
    }

    // Real editor creation failure paths preserve tabs and destroy candidates.
    {
        const int pages = window.editorManager()->pageCount();
        const int children = window.findChildren<Editor *>().size();
        bool rejected = false;
        try { window.editorManager()->newEditor(QDir(outputDirectory).filePath("missing-parent/no-source.cpp"), ENCODING_AUTO_DETECT, FileType::CppSource, {}, false, false); }
        catch (const FileError &) { rejected = true; }
        check("Failed file open reports an error", rejected);
        check("Failed file open preserves tabs", window.editorManager()->pageCount() == pages);
        check("Failed file open destroys the candidate editor", window.findChildren<Editor *>().size() == children);
    }
    // Exercise compiler -> diagnostic parser -> panel -> editor with actual GCC output.
    const QString compiler = qEnvironmentVariable("REDPANDA_QA_COMPILER");
    if (!compiler.isEmpty()) {
        const QByteArray ideProcessPath = qgetenv("PATH");
        const bool ideProcessHasPath = qEnvironmentVariableIsSet("PATH");
        for (const bool valid : {true, false}) {
            const QString path = QDir(outputDirectory).filePath(valid ? "gcc-real-warning.cpp" : "gcc-real-error.cpp");
            QFile file(path); const QByteArray source = valid ? "int main() { int unused_value; return 0; }\n" : "int main() { return; }\n";
            const bool written = file.open(QIODevice::WriteOnly) && file.write(source) == source.size(); file.close();
            check("Real GCC fixture written", written);
            Editor *editor = written ? window.openFile(path) : nullptr;
            check("Real GCC source opened in editor", editor != nullptr);
            QProcess process;
            auto environment = QProcessEnvironment::systemEnvironment();
            environment.insert("LC_ALL", "C");
            const QString inheritedPath = environment.value("PATH");
            environment.insert("PATH", QFileInfo(compiler).absolutePath()
                               + (inheritedPath.isEmpty() ? QString() : QDir::listSeparator() + inheritedPath));
            process.setProcessEnvironment(environment);
            const QString object = QDir(outputDirectory).filePath(valid ? "gcc-real-warning.o" : "gcc-real-error.o");
            process.start(compiler, {"-std=c++17", "-Wall", "-Wextra", "-fdiagnostics-color=never", "-fmessage-length=0", "-c", path, "-o", object});
            const bool finished = process.waitForStarted(5000) && process.waitForFinished(15000);
            if (!finished) { process.kill(); process.waitForFinished(5000); }
            check("Real GCC process completes within timeout", finished);
            check("Real GCC exit status matches valid/invalid source", finished && process.exitStatus() == QProcess::NormalExit && (valid ? process.exitCode() == 0 : process.exitCode() != 0));
            check("Real GCC creates object only for valid source", QFile::exists(object) == valid);
            const QString diagnostics = QString::fromLocal8Bit(process.readAllStandardError());
            auto *table = window.findChild<IssuesTable *>();
            if (table && editor) {
                table->clearIssues(); GccDiagnosticParserProbe parser;
                QObject::connect(&parser, &Compiler::compileIssue, &window, &MainWindow::onCompileIssue);
                for (const QString &line : diagnostics.split('\n')) parser.feed(line); parser.finish();
                auto *model = table->issuesModel();
                check("Actual GCC diagnostic reaches panel", model->count() > 0);
                if (model->count() > 0) {
                    const auto issue = model->issue(model->count() - 1);
                    check("Actual GCC severity is preserved", issue && issue->type == (valid ? CompileIssueType::Warning : CompileIssueType::Error));
                    check("Actual GCC original remains available", model->originalDescription(model->count() - 1).contains(valid ? "unused" : "return"));
                    if (language == "it_IT") check("Actual GCC diagnostic has local Italian translation", !model->translatedDescription(model->count() - 1).isEmpty());
                }
            }
        }
        check("Direct GCC checks preserve the IDE process PATH", qgetenv("PATH") == ideProcessPath
              && qEnvironmentVariableIsSet("PATH") == ideProcessHasPath);
    }

{
    QTemporaryDir files(QDir(outputDirectory).filePath("document-workflows-XXXXXX"));
    check("Document workflows use an isolated directory", files.isValid());
    const auto writeBytes = [](const QString &path, const QByteArray &bytes) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    };
    const auto readBytes = [](const QString &path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    };
    struct Selection { int line; int start; int length; QString original; };
    const auto selectResults = [&](const QString &path, const QString &keyword,
                                   const QList<Selection> &selections, bool regex = false) {
        auto *model = window.searchResultModel();
        auto result = model->addSearchResults(keyword, QSynedit::ssoMatchCase, regex, SearchFileScope::Folder);
        auto parent = std::make_shared<SearchResultTreeItem>();
        parent->filename = path;
        parent->parent = nullptr;
        parent->selected = false;
        for (const auto &selected : selections) {
            auto child = std::make_shared<SearchResultTreeItem>();
            child->filename = path;
            child->line = selected.line;
            child->start = selected.start;
            child->len = selected.length;
            child->text = selected.original;
            child->text.replace('\t', ' ');
            child->selected = true;
            child->parent = parent.get();
            parent->results.append(child);
        }
        model->addResultToSearchResults(result, parent);
    };
    auto *openFiles = window.findChild<QCheckBox *>("chkOpenFileInEditors");
    auto *replacement = window.findChild<QComboBox *>("cbReplaceInHistory");
    check("Batch replacement controls exist", openFiles && replacement);
    if (openFiles && replacement && files.isValid()) {
        const bool oldOpenFiles = openFiles->isChecked();
        const QString oldReplacement = replacement->currentText();
        openFiles->setChecked(false);
        replacement->setEditText("bar");
        const QByteArray bom = QByteArray::fromHex("efbbbf");
        const QString closedPath = files.filePath(QString::fromUtf8("closed città.cpp"));
        check("Closed batch source fixture written", writeBytes(closedPath, bom + "foo foo\r\nint tail;"));
        // Deliberately reversed input order proves descending-coordinate application.
        selectResults(closedPath, "foo", {{0, 4, 3, "foo foo"}, {0, 0, 3, "foo foo"}});
        check("Closed batch replacement invocation", QMetaObject::invokeMethod(&window, "on_btnReplace_clicked", Qt::DirectConnection));
        check("Closed batch replacement uses zero-based coordinates and retains BOM CRLF EOF", readBytes(closedPath) == bom + "bar bar\r\nint tail;");
        check("Closed batch replacement does not open an editor", !window.editorManager()->getOpenedEditor(closedPath));

        const QString openedPath = files.filePath("opened.cpp");
        check("Opened batch source fixture written", writeBytes(openedPath, "foo foo\n"));
        Editor *opened = window.openFile(openedPath);
        check("Opened batch source editor exists", opened != nullptr);
        if (opened) {
            selectResults(openedPath, "foo", {{0, 4, 3, "foo foo"}, {0, 0, 3, "foo foo"}});
            check("Opened batch replacement invocation", QMetaObject::invokeMethod(&window, "on_btnReplace_clicked", Qt::DirectConnection));
            check("Opened batch replacement leaves disk unchanged", readBytes(openedPath) == "foo foo\n");
            check("Opened batch replacement is modified and undoable", opened->lineText(0) == "bar bar" && opened->modified() && opened->canUndo());
            opened->undo();
            check("Opened batch replacement is undone as one operation", opened->lineText(0) == "foo foo" && !opened->modified() && !opened->canUndo());
            const QString original = opened->text();
            const bool originalModified = opened->modified();
            const bool originalUndo = opened->canUndo();
            // First result is valid; second is stale. Nothing may be applied.
            selectResults(openedPath, "foo", {{0, 4, 3, "foo foo"}, {0, 0, 3, "stale line"}});
            int errors = 0;
            QTimer dismiss;
            QObject::connect(&dismiss, &QTimer::timeout, [&] {
                for (auto *widget : QApplication::topLevelWidgets()) {
                    if (auto *message = qobject_cast<QMessageBox *>(widget)) {
                        ++errors;
                        message->accept();
                    }
                }
            });
            dismiss.start(20);
            const bool invoked = QMetaObject::invokeMethod(&window, "on_btnReplace_clicked", Qt::DirectConnection);
            dismiss.stop();
            check("Stale batch replacement reports an error", invoked && errors == 1);
            check("Stale batch replacement preserves document undo and modified state", opened->text() == original && opened->modified() == originalModified && opened->canUndo() == originalUndo);
            check("Stale batch replacement preserves the disk file", readBytes(openedPath) == "foo foo\n");
            opened->setCaretXY({0, 0});
            opened->processCommand(QSynedit::EditCommand::Input, "X");
            check("Editing remains responsive after failed batch replacement", opened->lineText(0).startsWith("Xfoo"));
            opened->undo();
{
    const QString beforeReadOnly = opened->text();
    const bool beforeModified = opened->modified();
    const bool beforeUndo = opened->canUndo();
    const bool beforeRedo = opened->canRedo();
    const auto beforeCaret = opened->caretXY();
    replacement->setEditText("bar");
    selectResults(openedPath, "foo", {{0, 4, 3, "foo foo"}, {0, 0, 3, "foo foo"}});
    const bool previousReadOnly = opened->readOnly();
    opened->setReadOnly(true);
    int readOnlyErrors = 0;
    QTimer dismissReadOnly;
    QObject::connect(&dismissReadOnly, &QTimer::timeout, [&] {
        for (auto *widget : QApplication::topLevelWidgets()) {
            if (auto *message = qobject_cast<QMessageBox *>(widget)) {
                ++readOnlyErrors;
                message->accept();
            }
        }
    });
    dismissReadOnly.start(20);
    const bool invokedReadOnly = QMetaObject::invokeMethod(&window, "on_btnReplace_clicked", Qt::DirectConnection);
    dismissReadOnly.stop();
    check("Read-only batch replacement is reported before editing", invokedReadOnly && readOnlyErrors == 1);
    check("Read-only batch replacement preserves content caret modified and read-only state",
          opened->text() == beforeReadOnly && opened->modified() == beforeModified
          && opened->caretXY() == beforeCaret && opened->readOnly());
    // Availability of editing commands deliberately changes in read-only mode.
    check("Read-only mode disables undo and redo without editing", !opened->canUndo() && !opened->canRedo());
    check("Read-only batch replacement preserves the disk file", readBytes(openedPath) == "foo foo\n");
    opened->setReadOnly(previousReadOnly);
    check("Read-only batch replacement preserves content undo redo caret and modified state",
          opened->text() == beforeReadOnly && opened->modified() == beforeModified
          && opened->canUndo() == beforeUndo && opened->canRedo() == beforeRedo
          && opened->caretXY() == beforeCaret && opened->readOnly() == previousReadOnly);
    check("Read-only preservation fixture retains the prior redo operation", beforeRedo);
    if (beforeRedo) {
        opened->redo();
        check("A prior redo still applies after rejected read-only replacement", opened->lineText(0) == "Xfoo foo" && opened->modified());
        opened->undo();
        check("Undo after redo recovers the preserved read-only fixture", opened->text() == beforeReadOnly
              && opened->modified() == beforeModified && opened->canUndo() == beforeUndo && opened->canRedo() == beforeRedo);
    }
}

            window.editorManager()->forceCloseEditor(opened);
        }

        const QString regexPath = files.filePath("regex.cpp");
        check("Regex batch source fixture written", writeBytes(regexPath, "x1 x22\n"));
        replacement->setEditText("[\\1]");
        selectResults(regexPath, "(?<=x)(\\d+)", {{0, 4, 2, "x1 x22"}, {0, 1, 1, "x1 x22"}}, true);
        check("Regex batch replacement invocation", QMetaObject::invokeMethod(&window, "on_btnReplace_clicked", Qt::DirectConnection));
        check("Regex batch replacement keeps lookbehind context lengths and captures", readBytes(regexPath) == "x[1] x[22]\n");

        const QString invalidPath = files.filePath("invalid-replacement.cpp");
        const QByteArray invalidOriginal = bom + "foo\r\n";
        check("Failed batch save source fixture written", writeBytes(invalidPath, invalidOriginal));
        replacement->setEditText(QString(QChar(0xd800)));
        selectResults(invalidPath, "foo", {{0, 0, 3, "foo"}});
        int rejectedSaves = 0;
        QTimer dismissSave;
        QObject::connect(&dismissSave, &QTimer::timeout, [&] {
            for (auto *widget : QApplication::topLevelWidgets()) {
                if (auto *message = qobject_cast<QMessageBox *>(widget)) {
                    ++rejectedSaves;
                    message->accept();
                }
            }
        });
        dismissSave.start(20);
        const bool invokedInvalid = QMetaObject::invokeMethod(&window, "on_btnReplace_clicked", Qt::DirectConnection);
        dismissSave.stop();
        check("Batch save conversion failure is reported", invokedInvalid && rejectedSaves == 1);
        check("Batch save conversion failure preserves original bytes", readBytes(invalidPath) == invalidOriginal);
        replacement->setEditText("bar");
        check("Batch save retries successfully after conversion failure", QMetaObject::invokeMethod(&window, "on_btnReplace_clicked", Qt::DirectConnection) && readBytes(invalidPath) == bom + "bar\r\n");

        openFiles->setChecked(oldOpenFiles);
        replacement->setEditText(oldReplacement);
    }

    const QString renamePath = files.filePath("rename.cpp");
    const QByteArray beforeRename = "int before = 1;\nint f() { return before; }\n";
    check("Closed rename source fixture written", writeBytes(renamePath, beforeRename));
    auto parser = std::make_shared<CppParser>();
    parser->setOnGetFileStream(nullptr);
    CppParser::parseFileBlocking(parser, renamePath, false, QString(), false, false);
    Editor probe(nullptr);
    probe.setEditorSettings(&pSettings->editor());
    probe.setCodeCompletionSettings(&pSettings->codeCompletion());
    probe.setFilename(renamePath);
    probe.setSyntaxer(SyntaxerManager::getSyntaxer(QSynedit::ProgrammingLanguage::CPP));
    QByteArray encoding;
    probe.loadFromFile(renamePath, ENCODING_AUTO_DETECT, encoding);
    probe.setCppParser(parser);
    CppRefacter refactor(&window);
    refactor.renameSymbol(&probe, {4, 0}, "after");
    check("Closed symbol rename applies the calculated contents", readBytes(renamePath) == "int after = 1;\nint f() { return after; }\n");
    check("Closed symbol rename does not change the caller editor buffer", probe.lineText(0) == "int before = 1;");
    check("Closed symbol rename does not create a registered editor", !window.editorManager()->getOpenedEditor(renamePath));
    check("Symbol rename with no parser is a recoverable no-op", [&] {
        Editor noParser(nullptr);
        refactor.renameSymbol(&noParser, {0, 0}, "name");
        refactor.renameUndefinedLocalVariable(&noParser, {0, 0}, "name");
        return true;
    }());

    QFileSystemWatcher watcher;
    const QString layoutProjectPath = files.filePath("project.dev");
    auto layoutTemplate = std::make_shared<ProjectTemplate>();
    layoutTemplate->setVersion(0);
    ProjectOptions layoutOptions;
    layoutOptions.modelType = ProjectModelType::Custom;
    layoutOptions.icon.clear();
    layoutTemplate->setOptions(layoutOptions);
    auto layoutProject = Project::create(layoutProjectPath, "Document workflow test", window.editorManager(),
        window.iconsManager(), &watcher, layoutTemplate, true);
    check("Project layout fixture metadata written", layoutProject && QFile::exists(layoutProjectPath));
    if (!layoutProject) return 2;
    const QString layout = files.filePath("project.layout");
    layoutProject->saveLayout();
    check("Project layout saves valid JSON", QJsonDocument::fromJson(readBytes(layout)).isArray());
    // A directory is a deterministic unwritable destination on Windows and Linux.
    const QByteArray previousLayout = readBytes(layout);
    check("Project layout fault fixture prepared", QFile::rename(layout, layout + ".saved") && QDir().mkdir(layout));
    bool rejected = false;
    try { layoutProject->saveLayout(); } catch (const FileError &) { rejected = true; }
    check("Project layout failure is reported and original saved layout remains", rejected && readBytes(layout + ".saved") == previousLayout);
    check("Project layout retry target prepared", QDir().rmdir(layout) && QFile::rename(layout + ".saved", layout));
    layoutProject->saveLayout();

    check("Project layout can save after a previous failure", QJsonDocument::fromJson(readBytes(layout)).isArray());

    // Exercise the native close-event path, not merely Project::saveLayout.
    // A layout failure follows staged document decisions. A retry must still
    // request those decisions because no document-close commit occurred.
    const bool oldAutoLoad = pSettings->editor().autoLoadLastFiles();
    pSettings->editor().setAutoLoadLastFiles(false);
    auto restoreAutoLoad = finally([&] { pSettings->editor().setAutoLoadLastFiles(oldAutoLoad); });
    const QString closeProjectPath = files.filePath("close-workflow.dev");
    check("Close workflow project fixture written", writeBytes(closeProjectPath,
          "[Project]\nVer=3\nName=Close workflow\nUnitCount=0\nType=0\nModelType=0\n"));
    int stage = 0, layoutErrors = 0, cancelledDocumentPrompts = 0;
    bool nestedCloseChecked = false, nestedCloseRejected = false;
    bool parseNotificationDelivered = false;
    QTimer handleCloseMessages;
    QObject::connect(&handleCloseMessages, &QTimer::timeout, [&] {
        if (auto *message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            if (stage == 1 && message->text().contains("close-workflow.layout"))
                ++layoutErrors;
            if (stage == 1 && message->standardButtons().testFlag(QMessageBox::Cancel)) {
                // The discard is a prepared decision. A later layout failure
                // must leave the still-open buffer dirty and recoverable.
                message->button(QMessageBox::No)->click();
            } else if (stage == 2 && message->standardButtons().testFlag(QMessageBox::Cancel)) {
                ++cancelledDocumentPrompts;
                if (!nestedCloseChecked) {
                    nestedCloseChecked = true;
                    // Finish a background notification while the save dialog is
                    // pumping events, and deliver another native close event.
                    const bool started = QMetaObject::invokeMethod(&window, "onParseStarted", Qt::DirectConnection);
                    const bool finished = QMetaObject::invokeMethod(&window, "onParseFinished", Qt::DirectConnection,
                                                                  Q_ARG(int, 0), Q_ARG(int, 0));
                    parseNotificationDelivered = started && finished;
                    QCloseEvent nested;
                    QApplication::sendEvent(&window, &nested);
                    nestedCloseRejected = !nested.isAccepted() && window.isVisible();
                }
                message->button(QMessageBox::Cancel)->click();
            } else if (stage == 3 && message->standardButtons().testFlag(QMessageBox::Cancel)) {
                message->done(0); // No standard save/discard choice was made.
            } else
                message->accept();
        }
    });
    handleCloseMessages.start(10);
    window.openProject(closeProjectPath, false);
    const auto closeProject = window.project();
    check("Close workflow opens a real project", closeProject && closeProject->filename() == closeProjectPath);
    if (closeProject) {
        QPointer<Editor> detachedUnit = window.editorManager()->newEditor(files.filePath("unassociated-project.cpp"),
                ENCODING_UTF8, FileType::CppSource, QString(), false, true);
        if (detachedUnit) {
            detachedUnit->setContent("int unassociated_project_document = 1;");
            detachedUnit->setModified(false);
            detachedUnit->setInProject(true, false);
        }
        check("Close workflow includes an unassociated project editor", detachedUnit
              && detachedUnit->inProject() && !closeProject->findUnit(detachedUnit.data()));
        QPointer<Editor> extra = window.editorManager()->newEditor(files.filePath("unsaved-extra.cpp"),
                ENCODING_UTF8, FileType::CppSource, QString(), false, true);
        check("Close workflow has an independent unsaved editor", extra && !extra->inProject());
        if (extra) {
            extra->setContent(QStringList{"int keep_unsaved = 350;"});
            extra->setModified(true);
            const QString unsavedText = extra->text();
            const int originalEditorCount = window.editorManager()->pageCount();
            const QString closeLayout = files.filePath("close-workflow.layout");
            closeProject->saveLayout();
            check("Close workflow deterministic layout failure prepared",
                  QFile::rename(closeLayout, closeLayout + ".saved") && QDir().mkdir(closeLayout));
            stage = 1;
            QCloseEvent rejectedClose;
            QApplication::sendEvent(&window, &rejectedClose);
            check("Failed main-window close reports the layout error", layoutErrors == 1);
            check("Failed main-window close rejects the close event", !rejectedClose.isAccepted());
            check("Failed main-window close restores all shutdown flags",
                  !window.isQuitting() && !window.isClosingAll() && !window.closingProject());
            check("Failed main-window close preserves project editors and visibility",
                  window.project() == closeProject && window.isVisible()
                  && window.editorManager()->pageCount() == originalEditorCount);
            check("Failed main-window close preserves the independent unsaved buffer",
                  extra && extra->modified() && extra->text() == unsavedText
                  && window.editorManager()->getOpenedEditor(extra->filename()) == extra);
            check("Failed main-window close preserves the previous layout bytes",
                  !readBytes(closeLayout + ".saved").isEmpty());
            check("Close workflow retry destination restored",
                  QDir().rmdir(closeLayout) && QFile::rename(closeLayout + ".saved", closeLayout));
            stage = 2;
            QCloseEvent cancelledRetry;
            QApplication::sendEvent(&window, &cancelledRetry);
            check("Close retry reaches unsaved-document prompts instead of bypassing them", cancelledDocumentPrompts > 0);
            check("Parser completion during a save prompt cannot accept a nested close",
                  parseNotificationDelivered && nestedCloseChecked && nestedCloseRejected);
            check("Cancelling close retry keeps the window open and flags reset",
                  !cancelledRetry.isAccepted() && window.isVisible()
                  && !window.isQuitting() && !window.isClosingAll() && !window.closingProject());
            check("Cancelling close retry preserves the independent unsaved buffer",
                  extra && extra->modified() && extra->text() == unsavedText);
            check("Cancelling close retains the previous valid layout",
                  QJsonDocument::fromJson(readBytes(closeLayout)).isArray());
            stage = 3;
            QCloseEvent noChoice;
            QApplication::sendEvent(&window, &noChoice);
            check("Close without an explicit save or discard choice keeps the buffer",
                  !noChoice.isAccepted() && extra && extra->modified() && extra->text() == unsavedText);
            if (extra) {
                extra->setContent(QStringList{QString()});
                extra->setModified(true);
                QCloseEvent emptyModified;
                QApplication::sendEvent(&window, &emptyModified);
                check("Closing an empty modified document still requires a save or discard choice",
                      !emptyModified.isAccepted() && extra && extra->modified() && extra->empty());
            }
            if (extra)
                window.editorManager()->forceCloseEditor(extra);
        }
        if (window.project())
            QMetaObject::invokeMethod(&window, "on_actionClose_Project_triggered", Qt::DirectConnection);
        check("Project close retires planned project editors even without a unit association",
              !window.project() && detachedUnit.isNull());
    }
    handleCloseMessages.stop();
}


#ifdef ENABLE_VCS
    // Real Git commands run only in a throwaway offline repository. Disable
    // signing and hooks locally so a user's global Git settings cannot launch
    // external programs during the smoke checks.
    {
        QString git = qEnvironmentVariable("REDPANDA_QA_GIT");
        if (git.isEmpty())
            git = QStandardPaths::findExecutable("git");
        const bool usableGit = !git.isEmpty() && QFileInfo(git).isFile();
        check("VCS smoke has a real Git executable", usableGit);
        if (usableGit) {
            const QString previousGitPath = pSettings->vcs().gitPath();
            auto restoreGit = finally([&] { pSettings->vcs().setGitPath(previousGitPath); });
            pSettings->vcs().setGitPath(QFileInfo(git).absoluteFilePath());
            // A CI report directory may itself be inside the checked-out Git
            // repository. Use the system temp directory to avoid its parent
            // worktree, and clear repository overrides during this test only.
            const QList<QByteArray> gitOverrides{
                "GIT_DIR", "GIT_WORK_TREE", "GIT_COMMON_DIR", "GIT_INDEX_FILE",
                "GIT_NAMESPACE", "GIT_OBJECT_DIRECTORY", "GIT_ALTERNATE_OBJECT_DIRECTORIES",
                "GIT_CONFIG_PARAMETERS", "GIT_CONFIG_COUNT"};
            QHash<QByteArray, QByteArray> savedOverrides;
            QSet<QByteArray> presentOverrides;
            for (const QByteArray &name : gitOverrides) {
                if (qEnvironmentVariableIsSet(name.constData())) {
                    presentOverrides.insert(name);
                    savedOverrides.insert(name, qgetenv(name.constData()));
                }
                qunsetenv(name.constData());
            }
            auto restoreOverrides = finally([&] {
                for (const QByteArray &name : gitOverrides) {
                    if (presentOverrides.contains(name))
                        qputenv(name.constData(), savedOverrides.value(name));
                    else
                        qunsetenv(name.constData());
                }
            });
            QTemporaryDir repository(QDir::tempPath() + "/" +
                    QString::fromUtf8("RedPanda vcs spazi-\xC3\xA0-\xF0\x9F\x90\xBC-XXXXXX"));
            check("VCS smoke creates an isolated directory with spaces and Unicode", repository.isValid());
            if (repository.isValid()) {
                GitManager manager;
                QString output;
                bool initialized = false;
                const QByteArray preservedIgnore("# QA user's existing rules\n*.temporary-qa\n");
                const QString ignorePath = repository.filePath(".gitignore");
                {
                    QFile ignore(ignorePath);
                    check("VCS smoke existing ignore fixture written", ignore.open(QIODevice::WriteOnly)
                          && ignore.write(preservedIgnore) == preservedIgnore.size());
                }
                const auto readIgnore = [&] {
                    QFile ignore(ignorePath);
                    return ignore.open(QIODevice::ReadOnly) ? ignore.readAll() : QByteArray();
                };
                try {
                    const bool initSucceeded = manager.createRepository(repository.path(), output);
                    QString branch;
                    initialized = initSucceeded && manager.hasRepository(repository.path(), branch);
                    check("VCS smoke repository initialization preserves the user's existing ignore bytes", readIgnore() == preservedIgnore);
                    check("VCS smoke repository initialization does not stage an existing ignore", !manager.listStagedFiles(repository.path()).contains(".gitignore"));
                    check("VCS smoke initializes and recognizes an offline repository", initialized && !branch.isEmpty());
                } catch (const BaseError &) {
                    check("VCS smoke initializes and recognizes an offline repository", false);
                }
                if (initialized) {
                    // Isolate byte-level fixtures from Git for Windows' system
                    // autocrlf policy; these writes affect this temporary repo only.
                    const bool autocrlfConfigured = manager.setConfig(repository.path(), "core.autocrlf", "false", output);
                    const bool eolConfigured = manager.setConfig(repository.path(), "core.eol", "lf", output);
                    check("VCS smoke configures local core.autocrlf=false", autocrlfConfigured);
                    check("VCS smoke configures local core.eol=lf", eolConfigured);
                    const QString hooks = repository.filePath("disabled hooks");
                    const bool configured = autocrlfConfigured && eolConfigured && QDir().mkpath(hooks)
                            && manager.setConfig(repository.path(), "core.hooksPath", hooks, output)
                            && manager.setConfig(repository.path(), "commit.gpgSign", "false", output)
                            && manager.setUserName(repository.path(), "RedPanda GUI test", output)
                            && manager.setUserEmail(repository.path(), "redpanda-test@example.invalid", output);
                    check("VCS smoke configures local identity and disables hooks and signing", configured);
                    if (configured) {
                        const QString name = QString::fromUtf8("file prova-citt\xC3\xA0-\xF0\x9F\x90\xBC.cpp");
                        const QByteArray contents("int main() { return 0; }\n");
                        QFile file(repository.filePath(name));
                        const bool written = file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
                        file.close();
                        check("VCS smoke writes a source filename with spaces and Unicode", written);
                        const bool staged = written && manager.add(repository.path(), name, output);
                        check("VCS smoke stages a filename with spaces and Unicode", staged);
                        if (staged) {
                            check("VCS smoke lists the exact Unicode filename", manager.listStagedFiles(repository.path()).contains(name));
                            const QString message = "QA offline commit";
                            const bool committed = manager.commit(repository.path(), message, false, output);
                            check("VCS smoke commits the staged source offline", committed);
                            const auto history = manager.log(repository.path(), 0, 1);
                            check("VCS smoke reads HEAD log and commit title", committed && history.size() == 1
                                  && !history.first()->commitHash.isEmpty() && history.first()->title == message);
                            const QString head = history.isEmpty() ? QString() : history.first()->commitHash;
                            output.clear();
                            check("VCS smoke rejects a nonzero normal exit with no pending commit",
                                  !manager.commit(repository.path(), "must not create another commit", false, output)
                                  && output.contains("fatal:") && !output.trimmed().isEmpty());
                            output.clear();
                            const bool failed = !manager.add(repository.path(), "does-not-exist.cpp", output);
                            check("VCS smoke rejects missing paths with a nonempty diagnostic", failed && !output.trimmed().isEmpty());
                            const auto afterFailure = manager.log(repository.path(), 0, 1);
                            check("VCS smoke failed operation preserves HEAD", !head.isEmpty() && afterFailure.size() == 1
                                  && afterFailure.first()->commitHash == head);
                            {
                                auto *gitMenu = window.findChild<QMenu *>("menuGit");
                                struct GitActionSpec { const char *suffix; const char *english; const char *italian; };
                                const QList<GitActionSpec> actions{
                                    {"Create_Repository", "Create Repository...", "Crea repository..."},
                                    {"Add_Files", "Stage Selected Files", "Aggiungi file selezionati allo staging"},
                                    {"Commit", "Commit...", "Commit..."},
                                    {"Restore", "Restore Working Tree...", "Ripristina working tree..."},
                                    {"Branch", "Branches...", "Branch..."},
                                    {"Merge", "Merge...", "Merge..."},
                                    {"Log", "History...", "Cronologia..."},
                                    {"Remotes", "Remotes...", "Repository remoti..."},
                                    {"Fetch", "Fetch", "Fetch"},
                                    {"Pull", "Pull", "Pull"},
                                    {"Push", "Push", "Push"},
                                };
                                check("Git menu is available with VCS enabled", gitMenu && gitMenu->menuAction()->isVisible());
                                for (const auto &spec : actions) {
                                    const QString objectName = "actionGit_" + QString::fromLatin1(spec.suffix);
                                    auto *action = window.findChild<QAction *>(objectName);
                                    const QByteArray slot = "on_" + objectName.toLatin1() + "_triggered()";
                                    const QString expected = QString::fromUtf8(language == "it_IT" ? spec.italian : spec.english);
                                    check("Git menu action and implemented slot: " + QString::fromLatin1(spec.suffix),
                                          action && gitMenu && gitMenu->actions().contains(action)
                                          && action->text() == expected && !action->toolTip().isEmpty()
                                          && window.metaObject()->indexOfSlot(slot.constData()) >= 0);
                                }
                                check("Git menu omits unsupported reset and revert actions",
                                      !window.findChild<QAction *>("actionGit_Reset") && !window.findChild<QAction *>("actionGit_Revert"));
                                auto *files = window.findChild<QTreeView *>("treeFiles");
                                auto *explorer = window.findChild<QTabWidget *>("tabExplorer");
                                auto *filesPage = window.findChild<QWidget *>("tabFiles");
                                auto *stage = window.findChild<QAction *>("actionGit_Add_Files");
                                auto *restore = window.findChild<QAction *>("actionGit_Restore");
                                check("Git action smoke has the files view and working actions", files && explorer && filesPage && stage && restore && gitMenu);
                                if (files && explorer && filesPage && stage && restore && gitMenu) {
                                    const QString oldRoot = pSettings->environment().currentFolder();
                                    QPointer<QWidget> oldPage = explorer->currentWidget();
                                    QPointer<Editor> opened;
                                    auto cleanupGitUi = finally([&] {
                                        if (opened)
                                            window.editorManager()->forceCloseEditor(opened);
                                        window.setFilesViewRoot(oldRoot);
                                        if (oldPage)
                                            explorer->setCurrentWidget(oldPage);
                                    });
                                    explorer->setCurrentWidget(filesPage);
                                    window.setFilesViewRoot(repository.path());
                                    waitForUi(100);
                                    const QString path = repository.filePath(name);
                                    const auto writeGitBytes = [&](const QByteArray &bytes) {
                                        QFile target(path);
                                        return target.open(QIODevice::WriteOnly) && target.write(bytes) == bytes.size();
                                    };
                                    const auto readGitBytes = [&] {
                                        QFile source(path);
                                        return source.open(QIODevice::ReadOnly) ? source.readAll() : QByteArray();
                                    };
                                    auto *model = qobject_cast<QFileSystemModel *>(files->model());
                                    QModelIndex index;
                                    QElapsedTimer ready;
                                    ready.start();
                                    while (model && !index.isValid() && ready.elapsed() < 3000) {
                                        index = model->index(path);
                                        if (!index.isValid())
                                            waitForUi(40);
                                    }
                                    check("Git files view resolves the isolated Unicode filename", index.isValid());
                                    if (index.isValid()) {
                                        files->selectionModel()->clearSelection();
                                        QMetaObject::invokeMethod(gitMenu, "aboutToShow", Qt::DirectConnection);
                                        check("Git staging action is disabled with no selection", !stage->isEnabled());
                                        const QByteArray stagedBytes("int main() { return 7; }\n");
                                        const QByteArray changedBytes("int main() { return 9; }\n");
                                        check("Git action smoke writes a change for staging", writeGitBytes(stagedBytes));
                                        files->selectionModel()->select(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
                                        QMetaObject::invokeMethod(gitMenu, "aboutToShow", Qt::DirectConnection);
                                        check("Git staging action enables for a selected repository file", stage->isEnabled());
                                        stage->trigger();
                                        check("Git staging QAction invokes its connected slot", manager.listStagedFiles(repository.path()).contains(name));
                                        check("Git restore smoke writes an unstaged change", writeGitBytes(changedBytes));
                                        opened = window.openFile(path);
                                        check("Git restore smoke opens an independent buffer", !opened.isNull());
                                        if (opened) {
                                            const QString unsaved("// unsaved editor buffer\nint memory_only = 42;");
                                            opened->setContent(unsaved);
                                            opened->setModified(true);
                                            const QString expectedBuffer = opened->text();
                                            const auto expectedCaret = opened->caretXY();
                                            int restoreDialogs = 0;
                                            bool defaultsToNo = true;
                                            QMessageBox::StandardButton answer = QMessageBox::No;
                                            QTimer dismissRestore;
                                            QObject::connect(&dismissRestore, &QTimer::timeout, [&] {
                                                for (auto *widget : QApplication::topLevelWidgets()) {
                                                    if (auto *message = qobject_cast<QMessageBox *>(widget)) {
                                                        if (message->windowTitle() == QCoreApplication::translate("MainWindow", "Restore Working Tree")) {
                                                            ++restoreDialogs;
                                                            defaultsToNo = defaultsToNo && message->defaultButton() == message->button(QMessageBox::No);
                                                            if (auto *button = message->button(answer))
                                                                button->click();
                                                        } else if (auto *button = message->button(QMessageBox::No)) {
                                                            // File watcher reload prompts must keep the unsaved buffer.
                                                            button->click();
                                                        } else {
                                                            message->reject();
                                                        }
                                                    }
                                                }
                                            });
                                            dismissRestore.start(20);
                                            QMetaObject::invokeMethod(gitMenu, "aboutToShow", Qt::DirectConnection);
                                            restore->trigger();
                                            waitForUi(100);
                                            check("Canceled Git restore asks once with No as default", restoreDialogs == 1 && defaultsToNo);
                                            check("Canceled Git restore preserves working tree bytes", readGitBytes() == changedBytes);
                                            check("Canceled Git restore preserves unsaved editor and caret", opened && opened->text() == expectedBuffer && opened->modified() && opened->caretXY() == expectedCaret);
                                            answer = QMessageBox::Yes;
                                            restore->trigger();
                                            waitForUi(200);
                                            dismissRestore.stop();
                                            check("Confirmed Git restore asks once with No as default", restoreDialogs == 2 && defaultsToNo);
                                            check("Confirmed Git restore QAction invokes its connected slot", readGitBytes() == stagedBytes);
                                            check("Confirmed Git restore preserves unsaved editor and caret", opened && opened->text() == expectedBuffer && opened->modified() && opened->caretXY() == expectedCaret);
                                            check("Git restore preserves staged changes", manager.listStagedFiles(repository.path()).contains(name));
                                        }
                                    }
                                }
                            }
                            QProcess detach;
                            detach.setProgram(git);
                            detach.setArguments({"-C", repository.path(), "checkout", "--detach", head});
                            detach.start();
                            const bool detached = detach.waitForStarted(2000) && detach.waitForFinished(5000)
                                    && detach.exitStatus() == QProcess::NormalExit && detach.exitCode() == 0;
                            if (detach.state() != QProcess::NotRunning) { detach.kill(); detach.waitForFinished(2000); }
                            QString detachedBranch;
                            check("VCS smoke recognizes a detached HEAD repository", detached
                                  && manager.hasRepository(repository.path(), detachedBranch) && detachedBranch == "HEAD");
                            pSettings->vcs().setGitPath(repository.filePath("missing git.exe"));
                            output.clear();
                            check("VCS smoke failed initialization leaves existing ignore bytes unchanged",
                                  !manager.createRepository(repository.path(), output) && !output.trimmed().isEmpty() && readIgnore() == preservedIgnore);
                            QTemporaryDir notCreated;
                            output.clear();
                            check("VCS smoke failed initialization creates neither ignore nor repository",
                                  notCreated.isValid() && !manager.createRepository(notCreated.path(), output)
                                  && !output.trimmed().isEmpty() && !QFileInfo::exists(notCreated.filePath(".gitignore"))
                                  && !QFileInfo::exists(notCreated.filePath(".git")));
                            output.clear();
                            check("VCS smoke missing executable cannot report empty success",
                                  !manager.add(repository.path(), name, output) && output.startsWith("fatal:")
                                  && !output.trimmed().isEmpty());
                            const QString invalidGit = repository.filePath("invalid git.exe");
                            QFile invalid(invalidGit);
                            const bool invalidWritten = invalid.open(QIODevice::WriteOnly)
                                    && invalid.write("not an executable") == 17;
                            invalid.close();
                            check("VCS smoke prepares an invalid executable fixture", invalidWritten);
                            if (invalidWritten) {
                                pSettings->vcs().setGitPath(invalidGit);
                                output.clear();
                                check("VCS smoke reports process-start failure as an error",
                                      !manager.add(repository.path(), name, output) && output.startsWith("fatal:")
                                      && !output.trimmed().isEmpty());
                            }
                        }
                    }
                }
            }
        }
    }
#endif

    runCompilerWorkflowChecks(window, outputDirectory, check);
    QFile result(QDir(outputDirectory).filePath("ui-results.json"));
    if (!result.open(QIODevice::WriteOnly)) return 2;
    const auto report = QJsonDocument(QJsonObject{{"language", language}, {"appVersion", REDPANDA_CPP_VERSION},
        {"qtVersion", qVersion()}, {"portableConfig", usePortableConfigPath()}, {"checks", checks}, {"skipped", skipped}, {"failures", failures}}).toJson();
    if (result.write(report) != report.size()) return 2;
    for (int i = 0; i < window.editorManager()->pageCount(); ++i) if (auto *editor = (*window.editorManager())[i]) editor->setModified(false);
    return failures == 0 ? 0 : 1;
}
