/*
 * Copyright (C) 2020-2026 Roy Qu (royqh1979@gmail.com)
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
#include "parser.h"
#include "../parser/cppparser.h"
#include "../settings/compilersetsettings.h"
#include "../settings/codecompletionsettings.h"
#include "../settings.h"
#include "../mainwindow.h"


#include "../analysis/parserconfigurationcapture.h"
#include "qt_utils/charsetinfo.h"
#include <QDir>
#include <QThread>
#include <QProcessEnvironment>

ParserConfigurationSubmission configureCppParser(std::shared_ptr<CppParser> parser, int compilerSetIndex,
    Analysis::ParserConfigurationValues base, const QString &workingDirectory)
{
    if (!parser || QThread::currentThread() != parser->thread())
        return {ParserConfigurationSubmissionStatus::Rejected, 0,
            QCoreApplication::translate("ParserConfiguration", "Parser configuration must be submitted on its owner thread.")};
    const auto ticket = parser->beginConfigurationPreparation();
    if (!ticket)
        return {ParserConfigurationSubmissionStatus::Rejected, 0,
            QCoreApplication::translate("ParserConfiguration", "The parser service is retiring and cannot accept configuration.")};
    base.enabled = true;
    base.includeOnce = pSettings->codeCompletion().includedHeaderFileOnlyParseOnce();
    if (compilerSetIndex < 0) compilerSetIndex = pSettings->compilerSets().defaultIndex();
    const auto compiler = pSettings->compilerSets().getSet(compilerSetIndex);
    QList<QByteArray> encodings{QByteArrayLiteral("UTF-8")};
    if (pCharsetInfoManager) {
        const auto system = pCharsetInfoManager->getDefaultSystemEncoding();
        if (!system.isEmpty() && !encodings.contains(system)) encodings.append(system);
    }
    const auto captured = Analysis::captureParserConfiguration(compiler.get(), std::move(base),
        workingDirectory, QProcessEnvironment::systemEnvironment(), std::move(encodings));
    if (!captured.preparation.ready() || !captured.request) {
        parser->failConfigurationPreparation(ticket, captured.preparation.reason);
        return {ParserConfigurationSubmissionStatus::Rejected, ticket, captured.preparation.reason};
    }
    if (!parser->prepareConfigurationAsync(ticket, captured.request)) {
        const auto reason = QCoreApplication::translate("ParserConfiguration", "Parser configuration preparation could not be submitted.");
        parser->failConfigurationPreparation(ticket, reason);
        return {ParserConfigurationSubmissionStatus::Rejected, ticket, reason};
    }
    return {ParserConfigurationSubmissionStatus::Accepted, ticket, {}};
}

void connectParserUi(const std::shared_ptr<CppParser> &parser)
{
    if (!parser || !pMainWindow) return;
    QObject::connect(parser.get(), &CppParser::parseStarted, pMainWindow, &MainWindow::onParseStarted, Qt::UniqueConnection);
    QObject::connect(parser.get(), &CppParser::progress, pMainWindow, &MainWindow::onParserProgress, Qt::UniqueConnection);
    QObject::connect(parser.get(), &CppParser::parseFinished, pMainWindow, &MainWindow::onParseFinished, Qt::UniqueConnection);
    QObject::connect(parser.get(), &CppParser::cacheResetStarted, pMainWindow, &MainWindow::onParseStarted, Qt::UniqueConnection);
    QObject::connect(parser.get(), &CppParser::cacheResetFinished, pMainWindow, &MainWindow::onCacheResetFinished, Qt::UniqueConnection);
    QObject::connect(parser.get(), &CppParser::inputSnapshotUnavailable, pMainWindow, &MainWindow::onAnalysisInputUnavailable, Qt::UniqueConnection);
}

void resetCppParser(std::shared_ptr<CppParser> parser, int compilerSetIndex)
{
    if (!parser) return;
    connectParserUi(parser);
    // Explicit legacy command adapter: acceptance is not synchronous analysis
    // completion. Failed preparation is published by the parser's typed state.
    const auto submitted = configureCppParser(parser, compilerSetIndex,
        parser->requestedConfigurationValues(), QDir::currentPath());
    Q_UNUSED(submitted)
}
