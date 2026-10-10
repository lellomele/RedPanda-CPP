#include "interfacechecks.h"
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
#include <QMimeData>
#include <QInputDialog>
#include <QKeyEvent>
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
    QDir().mkpath(outputDirectory); QJsonArray checks; int failures = 0;
    auto check = [&](const QString &name, bool passed) { checks.append(QJsonObject{{"name", name}, {"passed", passed}}); if (!passed) ++failures; };
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
            auto *savedClipboard = new QMimeData;
            if (const auto *previous = QGuiApplication::clipboard()->mimeData()) for (const auto &format : previous->formats()) savedClipboard->setData(format, previous->data(format));
            check("Copy original action invoked", QMetaObject::invokeMethod(&window, "onTableIssuesCopy", Qt::DirectConnection));
            check("Clipboard contains exact GCC message", QGuiApplication::clipboard()->text() == original);
            QGuiApplication::clipboard()->setMimeData(savedClipboard);
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
    QFile result(QDir(outputDirectory).filePath("ui-results.json"));
    if (!result.open(QIODevice::WriteOnly)) return 2; const auto report = QJsonDocument(QJsonObject{{"language", language}, {"checks", checks}, {"failures", failures}}).toJson(); if (result.write(report) != report.size()) return 2;
    for (int i = 0; i < window.editorManager()->pageCount(); ++i) if (auto *editor = (*window.editorManager())[i]) editor->setModified(false);
    return failures == 0 ? 0 : 1;
}
