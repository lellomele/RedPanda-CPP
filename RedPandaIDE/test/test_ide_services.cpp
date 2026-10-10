#include "test_ide_services.h"
#include "src/buildartifacts.h"
#include "src/diagnosticcatalog.h"
#include "src/utils/colorcontrast.h"
#include <QDir>
#include "src/snippetsstorage.h"
#include "src/sessionstore.h"
#include "src/codesnippetsmanager.h"
#include "src/settings/dirsettings.h"
#include "src/thememanager.h"
#include "src/resilientapplication.h"
#include <qsynedit/document.h>
#include <qt_utils/utils.h>
#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QSaveFile>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

static PCodeSnippet exampleSnippet() {
    auto s = std::make_shared<CodeSnippet>(); s->caption = "Loop"; s->prefix = "loop";
    s->code = "for (%REPL_BEGIN%int i = 0%REPL_END%; ; ) { %INSERT% }"; s->section = 0; return s;
}
void TestIdeServices::snippets_round_trip_and_legacy() {
    QString error; QList<PCodeSnippet> decoded;
    auto s = exampleSnippet(); s->language = "cpp";
    QVERIFY2(SnippetStorage::decode(SnippetStorage::encode({s}), decoded, error), qPrintable(error));
    QCOMPARE(decoded.size(), 1); QCOMPARE(decoded[0]->language, QString("cpp")); QCOMPARE(decoded[0]->code, s->code);
    QVERIFY2(SnippetStorage::read(":/config/codesnippets.json", decoded, error), qPrintable(error));
    QVERIFY(decoded.size() >= 23);
    const QByteArray legacy = R"([{"caption":"Old","prefix":"old","code":"%INSERT%","description":"","section":-1}])";
    QVERIFY(SnippetStorage::decode(legacy, decoded, error)); QCOMPARE(decoded[0]->language, QString("all")); QVERIFY(decoded[0]->enabled);
}
void TestIdeServices::snippets_invalid_input_keeps_previous_library() {
    QList<PCodeSnippet> previous{exampleSnippet()}; const auto pointer = previous[0].get(); QString error;
    for (const QByteArray &data : {QByteArray("{"), QByteArray("{}"), QByteArray("[1]"), QByteArray("[{\"caption\":3}]")}) {
        QVERIFY(!SnippetStorage::decode(data, previous, error)); QCOMPARE(previous.size(), 1); QCOMPARE(previous[0].get(), pointer);
    }
}
void TestIdeServices::snippets_editing_is_isolated() {
    auto original = exampleSnippet(); CodeSnippetsModel model; model.updateSnippets({original});
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    QVERIFY(model.setData(model.index(0, 0), "Edited", Qt::EditRole)); QCOMPARE(original->caption, QString("Loop")); QCOMPARE(changed.size(), 1);
    const auto stale = model.index(0, 0); model.clear(); QVERIFY(!model.data(stale, Qt::DisplayRole).isValid()); QVERIFY(!model.setData(stale, "bad", Qt::EditRole));
    model.remove(-1); model.remove(100); QVERIFY(!model.lastSnippetCaption().isValid());
}
void TestIdeServices::snippets_reject_duplicate_prefix_and_bad_placeholders() {
    auto s = exampleSnippet(); auto duplicate = std::make_shared<CodeSnippet>(*s); QString error;
    QVERIFY(!SnippetStorage::validate({s, duplicate}, error));
    duplicate->language = "c"; QVERIFY(SnippetStorage::validate({s, duplicate}, error));
    s->code = "%REPL_END%broken%REPL_BEGIN%"; QVERIFY(!SnippetStorage::validate({s}, error));
    s->code = "%REPL_BEGIN%missing"; QVERIFY(!SnippetStorage::validate({s}, error));
}
void TestIdeServices::snippets_language_and_enabled_filter() {
    auto s = exampleSnippet(); s->language = "c";
    QVERIFY(SnippetStorage::matches(s, "c")); QVERIFY(!SnippetStorage::matches(s, "cpp"));
    s->language = "all"; QVERIFY(SnippetStorage::matches(s, "cpp")); s->enabled = false; QVERIFY(!SnippetStorage::matches(s, "cpp"));
    QCOMPARE(SnippetStorage::languageForFileType(FileType::GAS), QString("asm"));
    s->enabled = true; s->language = "c"; QVERIFY(SnippetStorage::matches(s, SnippetStorage::languageForFileType(FileType::CCppHeader)));
    QVERIFY(!SnippetStorage::matches(s, SnippetStorage::languageForFileType(FileType::Text)));
}
void TestIdeServices::snippet_templates_commit_together() {
    QTemporaryDir temporary; SettingsPersistor settings(temporary.filePath("test.ini")); DirSettings dirs(&settings);
    CodeSnippetsManager writer(&dirs); writer.setSnippets({exampleSnippet()}); writer.setNewCppFileTemplate("cpp template"); writer.setNewCFileTemplate("c template"); writer.setNewGASFileTemplate("asm template"); writer.save();
    CodeSnippetsManager reader(&dirs); reader.load(); QCOMPARE(reader.snippets().size(), 1); QCOMPARE(reader.newCppFileTemplate(), QString("cpp template")); QCOMPARE(reader.newCFileTemplate(), QString("c template")); QCOMPARE(reader.newGASFileTemplate(), QString("asm template"));
    QFile file(temporary.filePath("snippets-library.json")); QVERIFY(file.open(QIODevice::ReadOnly)); const auto previous = file.readAll(); file.close();
    auto bad = exampleSnippet(); bad->code = "%REPL_END%"; writer.setSnippets({bad});
    bool failed = false; try { writer.save(); } catch (const FileError &) { failed = true; } QVERIFY(failed);
    QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), previous);
}
void TestIdeServices::recovery_round_trip_and_invalid_input() {
    QTemporaryDir dir; QString error; const QString path = dir.filePath("session.json");
    QList<RecoveryDocument> docs{{"untitled.cpp", "// testo italiano\nint main() {}", 2, 3}};
    QVERIFY(SessionStore::write(path, docs, error)); QList<RecoveryDocument> read;
    QVERIFY(SessionStore::read(path, read, error)); QCOMPARE(read[0].text, docs[0].text); QCOMPARE(read[0].filename, docs[0].filename);
    QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("{}"); file.close();
    QVERIFY(!SessionStore::read(path, read, error)); QCOMPARE(read[0].text, docs[0].text);
}
void TestIdeServices::recovery_failed_write_preserves_previous_snapshot() {
    QTemporaryDir dir; QString error; const QString path = dir.filePath("session.json");
    QList<RecoveryDocument> docs{{"main.cpp", "old", 0, 0}}; QVERIFY(SessionStore::write(path, docs, error));
    QList<RecoveryDocument> excessive; for (int i = 0; i < 257; ++i) excessive.append({"x", "new", 0, 0});
    QVERIFY(!SessionStore::write(path, excessive, error)); QList<RecoveryDocument> read; QVERIFY(SessionStore::read(path, read, error)); QCOMPARE(read[0].text, QString("old"));
    QVERIFY(!SessionStore::write(dir.path(), docs, error));
}
void TestIdeServices::document_failed_load_preserves_text() {
    QSynedit::Document doc(QFont{}); doc.setText("keep this text"); const QString before = doc.text(); QByteArray encoding = "unchanged";
    QTemporaryDir dir; bool failed = false;
    try { doc.loadFromFile(dir.filePath("missing.cpp"), ENCODING_AUTO_DETECT, encoding); } catch (const FileError &) { failed = true; }
    QVERIFY(failed); QCOMPARE(doc.text(), before); QCOMPARE(encoding, QByteArray("unchanged"));
    QFile binary(dir.filePath("binary.cpp")); QVERIFY(binary.open(QIODevice::WriteOnly)); binary.write(QByteArray("text\n", 5) + QByteArray(10, '\0')); binary.close();
    failed = false; try { doc.loadFromFile(binary.fileName(), ENCODING_AUTO_DETECT, encoding); } catch (const FileError &) { failed = true; }
    QVERIFY(failed); QCOMPARE(doc.text(), before);
}
class FailingSaveFile : public QSaveFile {
public: using QSaveFile::QSaveFile;
protected: qint64 writeData(const char *, qint64) override { return -1; }
};
void TestIdeServices::document_failed_write_preserves_file() {
    QTemporaryDir dir; const QString path = dir.filePath("source.cpp"); QFile original(path); QVERIFY(original.open(QIODevice::WriteOnly)); original.write("old text"); original.close();
    QSynedit::Document doc(QFont{}); doc.setText("new text"); QByteArray encoding;
    for (const QByteArray &format : {QByteArray(ENCODING_UTF8), QByteArray(ENCODING_UTF16), QByteArray(ENCODING_UTF32)}) {
        FailingSaveFile target(path); QVERIFY(target.open(QIODevice::WriteOnly | QIODevice::Unbuffered)); bool failed = false;
        try { doc.saveToFile(target, format, encoding); } catch (const FileError &) { failed = true; } QVERIFY(failed); target.cancelWriting(); QVERIFY(!target.commit());
        QVERIFY(original.open(QIODevice::ReadOnly)); QCOMPARE(original.readAll(), QByteArray("old text")); original.close();
    }
}
class FailingReceiver : public QObject {
    bool event(QEvent *event) override { if (event->type() == QEvent::User) throw FileError("simulated read error"); return QObject::event(event); }
};
void TestIdeServices::recoverable_event_does_not_stop_dispatch() {
    auto *app = qobject_cast<ResilientApplication *>(qApp); QVERIFY(app);
    QSignalSpy errors(app, &ResilientApplication::recoverableError); FailingReceiver receiver; QEvent event(QEvent::User);
    QVERIFY(!QCoreApplication::sendEvent(&receiver, &event)); QTRY_COMPARE(errors.size(), 1);
    QObject healthy; QEvent next(QEvent::User); QCoreApplication::sendEvent(&healthy, &next);
    QCOMPARE(errors.size(), 1);
}

void TestIdeServices::artifacts_only_expected_outputs_are_selected() {
    QTemporaryDir project, outside;
    const QStringList names{"source.cpp", "app.exe", "source.o", "library.a", "dependency.a", "unrelated.obj", "notes.txt"};
    for (const auto &name : names) { QFile file(project.filePath(name)); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("keep"); }
    QFile foreign(outside.filePath("app.exe")); QVERIFY(foreign.open(QIODevice::WriteOnly)); foreign.write("outside"); foreign.close();
    const auto entries = BuildArtifacts::inspect({project.filePath("app.exe"), project.filePath("source.o"), project.filePath("library.a"), project.filePath("dependency.a"), project.filePath("source.cpp"), project.filePath("notes.txt"), project.filePath("sub/../app.exe"), outside.filePath("app.exe"), project.path()}, {project.path()}, {project.filePath("source.cpp"), project.filePath("dependency.a")});
    QCOMPARE(entries.size(), 3);
    QVERIFY(QFile::exists(project.filePath("unrelated.obj")));
    for (const auto &entry : entries) QVERIFY(BuildArtifacts::unchanged(entry, {project.path()}, {project.filePath("source.cpp"), project.filePath("dependency.a")}));
    QCOMPARE(BuildArtifacts::inspect({outside.filePath("app.exe")}, {project.path(), outside.path()}, {}).size(), 1);
}
void TestIdeServices::artifacts_changes_after_preview_are_rejected() {
    QTemporaryDir project;
    const QString path = project.filePath("result.o");
    QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("old"); file.close();
    const auto entries = BuildArtifacts::inspect({path}, {project.path()}, {}); QCOMPARE(entries.size(), 1);
    QVERIFY(file.open(QIODevice::WriteOnly)); file.write("changed contents"); file.close();
    QVERIFY(!BuildArtifacts::unchanged(entries[0], {project.path()}, {}));
    QVERIFY(file.remove()); QVERIFY(!BuildArtifacts::unchanged(entries[0], {project.path()}, {}));
}

void TestIdeServices::diagnostics_catalog_examples() {
    QCOMPARE(DiagnosticCatalog::ruleCount(), 162);
    QFile file(":/config/gcc-it-diagnostics.json"); QVERIFY(file.open(QIODevice::ReadOnly));
    const auto rules = QJsonDocument::fromJson(file.readAll()).object().value("rules").toArray();
    for (const auto &rule : rules) for (const auto &example : rule.toObject().value("examples").toArray()) {
        const QString original = example.toObject().value("original").toString();
        const auto result = DiagnosticCatalog::translate(original, "it_IT");
        QVERIFY2(result.matched(), qPrintable(original));
        QCOMPARE(result.ruleId, rule.toObject().value("id").toString());
        QCOMPARE(result.text, example.toObject().value("italian").toString());
        const auto warning = DiagnosticCatalog::translate("[Warning] " + original + " [-Werror=unused-variable]", "it");
        QCOMPARE(warning.text, "[Avvertimento] " + result.text + " [-Werror=unused-variable]");
    }
}
void TestIdeServices::diagnostics_real_gcc_samples() {
    QFile file(":/config/gcc-diagnostic-samples.json"); QVERIFY(file.open(QIODevice::ReadOnly));
    const auto samples = QJsonDocument::fromJson(file.readAll()).object().value("samples").toArray();
    QVERIFY(samples.size() >= 164);
    for (const auto &sample : samples) {
        const auto object = sample.toObject();
        const auto result = DiagnosticCatalog::translate(object.value("message").toString(), "it_IT");
        QVERIFY2(result.matched(), qPrintable(object.value("message").toString()));
        QCOMPARE(result.ruleId, object.value("ruleId").toString());
        QCOMPARE(result.text, object.value("italian").toString());
    }
}
void TestIdeServices::diagnostics_unicode_and_protected_tokens() {
    QCOMPARE(DiagnosticCatalog::translate("‘π_値’ was not declared in this scope", "it-IT").text, QString("‘π_値’ non è stato dichiarato in questo ambito."));
    const auto conversion = DiagnosticCatalog::translate("error: invalid conversion from ‘std::vector<std::pair<int, Widget>>’ to ‘const void*’ [-fpermissive]", "it_IT");
    QVERIFY(conversion.text.contains("‘std::vector<std::pair<int, Widget>>’")); QVERIFY(conversion.text.contains("‘const void*’")); QVERIFY(conversion.text.endsWith("[-fpermissive]"));
    QCOMPARE(DiagnosticCatalog::translate("unused variable '%2{1}'", "it").text, QString("La variabile '%2{1}' non viene usata."));
    QCOMPARE(DiagnosticCatalog::translate("C:/dir con spazi/私の.h: No such file or directory", "it").text, QString("File o cartella non trovato: C:/dir con spazi/私の.h."));
}
void TestIdeServices::diagnostics_extended_regressions() {
    const QString format = "error: format '%2{1}%d' expects argument of type 'int', but argument 12 has type 'std::vector<π>' [-Wformat=] [-Werror]";
    QCOMPARE(DiagnosticCatalog::translate(format, "it_IT").text, QString("Errore: Il formato '%2{1}%d' richiede un argomento di tipo 'int', ma l’argomento 12 è di tipo 'std::vector<π>'. [-Wformat=] [-Werror]"));
    const QString alias = "warning: conversion from 'size_t' {aka 'unsigned long'} to 'int' may change value [-Wconversion]";
    QCOMPARE(DiagnosticCatalog::translate(alias, "it_IT").text, QString("Avvertimento: La conversione da 'size_t' {aka 'unsigned long'} a 'int' può alterare il valore. [-Wconversion]"));
    QCOMPARE(DiagnosticCatalog::translate("warning: parameter ‘π_値’ set but not used [-Wunused-but-set-parameter=]", "it").text, QString("Avvertimento: Il parametro ‘π_値’ viene modificato, ma il suo valore non viene usato. [-Wunused-but-set-parameter=]"));
    QCOMPARE(DiagnosticCatalog::translate("wrong number of template arguments (2, should be at least 4)", "it").text, QString("Il template riceve 2 argomenti, ma ne richiede almeno 4."));
    for (const QString &malformed : {QString("format '%d' expects a matching 'int' argument; unknown context"), QString("pointer 'p' used after 'free'; new reason"), QString("expected ';' after struct definition in new context"), QString("warning: conversion from 'int' to 'float' may change value\nextra diagnostic"), QString("missing terminating x character")})
        QVERIFY2(!DiagnosticCatalog::translate(malformed, "it").matched(), qPrintable(malformed));
    QVERIFY(!DiagnosticCatalog::translate(format, "en").matched());
}
void TestIdeServices::diagnostics_language_and_unknown_fallback() {
    for (const auto &language : {"en", "en_US", "fr", "", "zh_CN"}) QVERIFY(!DiagnosticCatalog::translate("unused variable 'n'", language).matched());
    for (const auto &message : {"a new GCC diagnostic", "'x' was not declared in this scope; an unknown explanation", "unused variable 'unterminated", "prefix unused variable 'n'", "return-statement with no value, in function returning 'int'; unknown explanation", "return-statement with no value, in function returning 'unterminated", "valid return in void function"}) {
        const QString original = QString::fromUtf8(message); QVERIFY(!DiagnosticCatalog::translate(original, "it_IT").matched()); QCOMPARE(original, QString::fromUtf8(message));
    }
    QVERIFY(!DiagnosticCatalog::translate(QString(20000, 'x') + ": No such file or directory", "it").matched());
    for (int i = 0; i < 1000; ++i) QVERIFY(!DiagnosticCatalog::translate("custom diagnostic " + QString::number(i), "it").matched());
}

void TestIdeServices::diagnostic_contrast_uses_srgb_and_alpha() {
    QCOMPARE(UiContrast::ratio(QColor(Qt::black), QColor(Qt::white)), qreal(21));
    QCOMPARE(UiContrast::ratio(QColor(Qt::white), QColor(Qt::white)), qreal(1));
    QCOMPARE(UiContrast::ratio(QColor(), QColor(Qt::white)), qreal(0));
    const QColor background("#203047");
    const auto corrected = UiContrast::readableText(QColor(Qt::black), background);
    QVERIFY(UiContrast::ratio(corrected, background) >= 4.5);
    const QColor readable("#ff9b9b"); QCOMPARE(UiContrast::readableText(readable, background), readable);
    QCOMPARE(UiContrast::composite(QColor(255, 255, 255, 128), QColor(Qt::black)), QColor(128, 128, 128));
    QVERIFY(UiContrast::ratio(UiContrast::readableText(QColor(255, 0, 0, 0), background), background) >= 4.5);
}
void TestIdeServices::diagnostic_contrast_covers_rgb_backgrounds() {
    const QList<QColor> foregrounds{QColor(), QColor(Qt::black), QColor(Qt::white), QColor("#c74747"), QColor("#ff9b9b"), QColor("#a26c14"), QColor("#f0d18c"), QColor("#808080"), QColor(0, 0, 255, 70)};
    for (int r = 0; r <= 255; r += 51) for (int g = 0; g <= 255; g += 51) for (int b = 0; b <= 255; b += 51) {
        const QColor background(r, g, b);
        for (const auto &preferred : foregrounds) {
            const QColor resolved = UiContrast::readableText(preferred, background);
            QVERIFY(resolved.isValid()); QCOMPARE(resolved.alpha(), 255);
            QVERIFY2(UiContrast::ratio(resolved, background) >= 4.5, qPrintable(background.name() + " / " + preferred.name() + " -> " + resolved.name()));
            if (preferred.isValid() && preferred.alpha() == 255 && UiContrast::ratio(preferred, background) >= 4.5) QCOMPARE(resolved, preferred);
        }
    }
}
