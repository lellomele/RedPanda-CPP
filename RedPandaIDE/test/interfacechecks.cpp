#include "src/cpprefacter.h"
#include "src/project.h"
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
#include <QPushButton>
#include <QMessageBox>
#include <QEventLoop>
#include <QTimer>
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
    QDir().mkpath(outputDirectory); QJsonArray checks; QJsonArray skipped; int failures = 0;
    auto check = [&](const QString &name, bool passed) { checks.append(QJsonObject{{"name", name}, {"passed", passed}}); if (!passed) ++failures; };
    auto skip = [&](const QString &name, const QString &reason) { skipped.append(QJsonObject{{"name", name}, {"reason", reason}}); };
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
        check("Fork copyright is exact and independent of interface language", copyright && copyright->text() == QStringLiteral("© 2006 Ing. prof. Raffaele Mele") && copyright->textFormat() == Qt::PlainText);
        const auto *upstreamCopyright = about.findChild<QLabel *>("label");
        check("Original copyright remains visible", upstreamCopyright && upstreamCopyright->text().contains("royqh1979@gmail.com"));
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
            QTimer::singleShot(0, &window, [] {
                for (auto *widget : QApplication::topLevelWidgets()) if (widget->objectName() == "buildArtifactsDialog") {
                    auto *dialog = qobject_cast<QDialog *>(widget); auto *list = dialog->findChild<QTreeWidget *>("buildArtifactsList");
                    for (int row = 0; list && row < list->topLevelItemCount(); ++row) if (list->topLevelItem(row)->text(0).endsWith(".o")) list->topLevelItem(row)->setCheckState(0, Qt::Unchecked);
                    dialog->accept();
                }
            });
            cleanAction->trigger();
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
        for (const bool valid : {true, false}) {
            const QString path = QDir(outputDirectory).filePath(valid ? "gcc-real-warning.cpp" : "gcc-real-error.cpp");
            QFile file(path); const QByteArray source = valid ? "int main() { int unused_value; return 0; }\n" : "int main() { return; }\n";
            const bool written = file.open(QIODevice::WriteOnly) && file.write(source) == source.size(); file.close();
            check("Real GCC fixture written", written);
            Editor *editor = written ? window.openFile(path) : nullptr;
            check("Real GCC source opened in editor", editor != nullptr);
            QProcess process; auto environment = QProcessEnvironment::systemEnvironment(); environment.insert("LC_ALL", "C"); process.setProcessEnvironment(environment);
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
    Project project(files.filePath("project.dev"), "Document workflow test", window.editorManager(), window.iconsManager(), &watcher);
    const QString layout = files.filePath("project.layout");
    project.saveLayout();
    check("Project layout saves valid JSON", QJsonDocument::fromJson(readBytes(layout)).isArray());
    // A directory is a deterministic unwritable destination on Windows and Linux.
    const QByteArray previousLayout = readBytes(layout);
    check("Project layout fault fixture prepared", QFile::rename(layout, layout + ".saved") && QDir().mkdir(layout));
    bool rejected = false;
    try { project.saveLayout(); } catch (const FileError &) { rejected = true; }
    check("Project layout failure is reported and original saved layout remains", rejected && readBytes(layout + ".saved") == previousLayout);
    check("Project layout retry target prepared", QDir().rmdir(layout) && QFile::rename(layout + ".saved", layout));
    project.saveLayout();

    check("Project layout can save after a previous failure", QJsonDocument::fromJson(readBytes(layout)).isArray());

    // Exercise the native close-event path, not merely Project::saveLayout.
    // A project-layout failure occurs before closeAll can prompt for a separate
    // unsaved editor; retries must still run those document prompts.
    const bool oldAutoLoad = pSettings->editor().autoLoadLastFiles();
    pSettings->editor().setAutoLoadLastFiles(false);
    auto restoreAutoLoad = finally([&] { pSettings->editor().setAutoLoadLastFiles(oldAutoLoad); });
    const QString closeProjectPath = files.filePath("close-workflow.dev");
    check("Close workflow project fixture written", writeBytes(closeProjectPath,
          "[Project]\nVer=3\nName=Close workflow\nUnitCount=0\nType=0\nModelType=0\n"));
    int stage = 0, layoutErrors = 0, cancelledDocumentPrompts = 0;
    QTimer handleCloseMessages;
    QObject::connect(&handleCloseMessages, &QTimer::timeout, [&] {
        if (auto *message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            if (stage == 1 && message->text().contains("close-workflow.layout"))
                ++layoutErrors;
            if (stage == 2 && message->standardButtons().testFlag(QMessageBox::Cancel)) {
                ++cancelledDocumentPrompts;
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
            check("Cancelling close retry keeps the window open and flags reset",
                  !cancelledRetry.isAccepted() && window.isVisible()
                  && !window.isQuitting() && !window.isClosingAll() && !window.closingProject());
            check("Cancelling close retry preserves the independent unsaved buffer",
                  extra && extra->modified() && extra->text() == unsavedText);
            check("Close retry commits a valid layout before document cancellation",
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
