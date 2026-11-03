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
#ifndef UTILS_PARSER_H
#define UTILS_PARSER_H
#include <memory>
#include <QString>
#include <QtGlobal>
namespace Analysis { struct ParserConfigurationValues; }
enum class ParserConfigurationSubmissionStatus { Accepted, Rejected };
struct ParserConfigurationSubmission {
    ParserConfigurationSubmissionStatus status;
    quint64 ticket;
    QString diagnostic;
};

class CppParser;
ParserConfigurationSubmission configureCppParser(std::shared_ptr<CppParser> parser, int compilerSetIndex,
    Analysis::ParserConfigurationValues base, const QString &workingDirectory);
void connectParserUi(const std::shared_ptr<CppParser> &parser);
void resetCppParser(std::shared_ptr<CppParser> parser, int compilerSetIndex=-1);

#endif // UTILS_PARSER_H
