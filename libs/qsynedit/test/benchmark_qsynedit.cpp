// Repeatable editor measurements. Timings are observations, not CI pass/fail gates.
#include <QApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QDir>
#include <QSysInfo>
#include <QTextStream>
#include <algorithm>
#include <memory>
#include "qsynedit/qsynedit.h"
#include "qsynedit/searcher/basicsearcher.h"
#include "qsynedit/syntaxer/cpp.h"
#include "qsynedit/formatter/cppformatter.h"
#include "qt_utils/utils.h"

using namespace QSynedit;

namespace {
QStringList sourceLines(int count)
{
    QStringList source;
    source.reserve(count);
    for (int line = 0; line < count; ++line) {
        if (line % 6 == 0)
            source.append(QString("void method_%1() {").arg(line));
        else if (line % 6 == 5)
            source.append("}");
        else if (line % 512 == 1)
            source.append("    int benchmark_token = 42;");
        else
            source.append(QString("    int value_%1 = %1; // independent declaration").arg(line));
    }
    return source;
}

QJsonObject measurement(const QString &name, const QList<qint64> &samples)
{
    QList<qint64> sorted = samples;
    std::sort(sorted.begin(), sorted.end());
    QJsonArray raw;
    for (qint64 value : samples)
        raw.append(static_cast<double>(value) / 1000000.0);
    return {{"scenario", name}, {"milliseconds", raw},
            {"median_ms", static_cast<double>(sorted[sorted.size() / 2]) / 1000000.0},
            {"minimum_ms", static_cast<double>(sorted.first()) / 1000000.0},
            {"maximum_ms", static_cast<double>(sorted.last()) / 1000000.0}};
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QList<int> sizes{3000, 30000, 100000};
    int sampleCount = 5;
    int edits = 40;
    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (args[i] == "--lines" && i + 1 < args.size()) {
            sizes.clear();
            const QStringList values = args[++i].split(',');
            for (const QString &value : values) {
                bool valid = false;
                const int count = value.toInt(&valid);
                if (!valid || count < 12 || count > 500000)
                    return 2;
                sizes.append(count);
            }
        } else if ((args[i] == "--samples" || args[i] == "--edits") && i + 1 < args.size()) {
            const bool isSamples = args[i] == "--samples";
            bool valid = false;
            const int count = args[++i].toInt(&valid);
            if (!valid || count < 1 || count > (isSamples ? 15 : 1000))
                return 2;
            if (isSamples)
                sampleCount = count;
            else
                edits = count;
        } else {
            return 2;
        }
    }
    QJsonArray documents;
    for (int count : sizes) {
        const QStringList source = sourceLines(count);
        const QString sourceHash = QString::fromLatin1(QCryptographicHash::hash(
                source.join('\n').toUtf8(), QCryptographicHash::Sha256).toHex());
        QList<qint64> loadTimes, editTimes, searchTimes, replaceTimes, undoTimes, redoTimes;
        for (int sample = 0; sample < sampleCount; ++sample) {
            QSynEdit edit;
            edit.setSyntaxer(std::make_shared<CppSyntaxer>());
            edit.setFormatter(std::make_shared<CppFormatter>());
            edit.setOptions(EditorOption::GroupUndo | EditorOption::AutoIndent);
            edit.resize(1000, 700);
            QElapsedTimer timer;
            timer.start();
            edit.setContent(source);
            loadTimes.append(timer.nsecsElapsed());
            if (edit.content() != source)
                return 3;
            int middleLine = (count / 2 / 6) * 6 + 2;
            edit.setCaretXY(CharPos{4, middleLine});
            timer.restart();
            for (int step = 0; step < edits; ++step) {
                edit.processCommand(EditCommand::Input, QString("x"));
                edit.processCommand(EditCommand::DeleteLastChar);
            }
            editTimes.append(timer.nsecsElapsed());
            if (edit.content() != source || edit.editing())
                return 3;
            edit.setContent(source);
            BasicSearcher searcher;
            CharPos scopeEnd;
            timer.restart();
            const int absent = edit.searchReplace("definitely_missing_needle", "",
                    edit.fileBegin(), edit.fileEnd(), scopeEnd, ssoMatchCase, &searcher,
                    [](const QString &, const QString &, const CharPos &, int) { return SearchAction::Skip; });
            searchTimes.append(timer.nsecsElapsed());
            if (absent != 0 || edit.content() != source)
                return 3;
            timer.restart();
            const int replaced = edit.searchReplace("benchmark_token", "result_token",
                    edit.fileBegin(), edit.fileEnd(), scopeEnd, ssoMatchCase, &searcher,
                    [](const QString &, const QString &, const CharPos &, int) { return SearchAction::ReplaceAll; });
            replaceTimes.append(timer.nsecsElapsed());
            QStringList expected = source;
            int expectedMatches = 0;
            for (QString &line : expected) {
                expectedMatches += line.count("benchmark_token");
                line.replace("benchmark_token", "result_token");
            }
            if (replaced != expectedMatches || edit.content() != expected || edit.editing())
                return 3;
            timer.restart();
            edit.undo();
            undoTimes.append(timer.nsecsElapsed());
            if (edit.content() != source || edit.canUndo())
                return 3;
            timer.restart();
            edit.redo();
            redoTimes.append(timer.nsecsElapsed());
            if (edit.content() != expected || edit.canRedo())
                return 3;
            QCoreApplication::sendPostedEvents();
        }
        QJsonArray scenarios;
        scenarios.append(measurement("load_cpp_document", loadTimes));
        scenarios.append(measurement("local_type_backspace_pairs", editTimes));
        scenarios.append(measurement("search_absent_whole_document", searchTimes));
        scenarios.append(measurement("replace_all_sparse_tokens", replaceTimes));
        scenarios.append(measurement("undo_replace_all", undoTimes));
        scenarios.append(measurement("redo_replace_all", redoTimes));
        // File IO is measured separately; the six in-memory scenarios and
        // their input hash remain comparable with earlier benchmark reports.
        QStringList ioSource = source;
        const QString unicodeComment = QString::fromUtf8(" // caf\xC3\xA8 \xF0\x9F\x90\xBC");
        for (int line = 0; line < ioSource.size(); line += 128)
            ioSource[line].append(unicodeComment);
        const QByteArray ioBytes = QByteArray::fromHex("efbbbf")
                + ioSource.join('\n').toUtf8() + '\n';
        QTemporaryDir ioDirectory(QDir::tempPath() + "/RedPanda editing IO-XXXXXX");
        if (!ioDirectory.isValid())
            return 3;
        const QString inputPath = ioDirectory.filePath(QString::fromUtf8("file di prova-\xF0\x9F\x90\xBC.cpp"));
        const QString outputPath = ioDirectory.filePath("atomic output.cpp");
        for (const QString &path : {inputPath, outputPath}) {
            QFile initial(path);
            if (!initial.open(QIODevice::WriteOnly) || initial.write(ioBytes) != ioBytes.size())
                return 3;
        }
        QList<qint64> ioLoadTimes, ioSaveTimes;
        for (int sample = 0; sample < sampleCount; ++sample) {
            QSynEdit fileEditor;
            fileEditor.setSyntaxer(std::make_shared<CppSyntaxer>());
            fileEditor.setFormatter(std::make_shared<CppFormatter>());
            QByteArray detectedEncoding;
            QElapsedTimer timer;
            timer.start();
            fileEditor.loadFromFile(inputPath, ENCODING_AUTO_DETECT, detectedEncoding);
            ioLoadTimes.append(timer.nsecsElapsed());
            if (fileEditor.content() != ioSource || detectedEncoding != ENCODING_UTF8_BOM
                    || fileEditor.canUndo() || fileEditor.editing())
                return 3;
            QSaveFile output(outputPath);
            output.setDirectWriteFallback(false);
            QByteArray outputEncoding;
            timer.restart();
            fileEditor.saveToFile(output, ENCODING_UTF8_BOM, outputEncoding);
            if (!output.commit())
                return 3;
            ioSaveTimes.append(timer.nsecsElapsed());
            QFile saved(outputPath);
            if (outputEncoding != ENCODING_UTF8_BOM || !saved.open(QIODevice::ReadOnly)
                    || saved.readAll() != ioBytes || fileEditor.content() != ioSource)
                return 3;
        }
        const QJsonArray ioScenarios{
                measurement("utf8_bom_file_load_cpp", ioLoadTimes),
                measurement("utf8_bom_atomic_file_save_commit", ioSaveTimes)};
        documents.append(QJsonObject{{"lines", count}, {"source_sha256", sourceHash},
                                     {"scenarios", scenarios}, {"io_scenarios", ioScenarios},
                                     {"io_bytes", static_cast<double>(ioBytes.size())},
                                     {"io_sha256", QString::fromLatin1(QCryptographicHash::hash(
                                              ioBytes, QCryptographicHash::Sha256).toHex())}});
    }
    const QJsonObject result{{"schema", 1}, {"qt_version", qVersion()},
                            {"platform", QSysInfo::prettyProductName()},
                            {"architecture", QSysInfo::currentCpuArchitecture()},
                            {"samples", sampleCount}, {"type_backspace_pairs", edits},
                            {"integrity_checked", true},
                            {"io_cache_policy", "Warm temporary local files; OS cache is not evicted"},
                            {"documents", documents}};
    QTextStream(stdout) << QJsonDocument(result).toJson(QJsonDocument::Indented);
    return 0;
}
