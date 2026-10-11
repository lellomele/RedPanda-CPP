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
#include "regexsearcher.h"

#include <QRegularExpression>

namespace QSynedit {

RegexSearcher::RegexSearcher(QObject* parent):Searcher(parent)
{

}

int RegexSearcher::length(int aIndex)
{
    if (aIndex<0 || aIndex >= mMatches.length())
        return -1;
    return mMatches[aIndex].capturedLength();
}

int RegexSearcher::result(int aIndex)
{
    if (aIndex<0 || aIndex >= mMatches.length())
        return -1;
    return mMatches[aIndex].capturedStart();
}

int RegexSearcher::resultCount()
{
    return mMatches.size();
}

int RegexSearcher::findAll(const QString &text)
{
    mMatches.clear();
    if (pattern().isEmpty() || !mRegex.isValid())
        return 0;
    QRegularExpressionMatchIterator it = mRegex.globalMatch(text);
    while (it.hasNext())
        mMatches.append(it.next());
    return mMatches.size();
}

QString RegexSearcher::replace(const QString &aOccurrence, const QString &aReplacement)
{
    QString s=aOccurrence;
    return s.replace(mRegex,aReplacement);
}

QString RegexSearcher::replaceResult(int resultIndex, const QString &occurrence,
                                     const QString &replacement)
{
    if (resultIndex < 0 || resultIndex >= mMatches.size())
        return occurrence;
    const QRegularExpressionMatch &match = mMatches[resultIndex];
    const int captureCount = mRegex.captureCount();
    QString expanded;
    expanded.reserve(replacement.size());
    // Match QString::replace backreferences (\\1..\\99), but use captures
    // from the original line so lookarounds and anchors retain their context.
    for (int pos = 0; pos < replacement.size(); ++pos) {
        if (replacement[pos] == QChar('\\') && pos + 1 < replacement.size()) {
            int capture = replacement[pos + 1].digitValue();
            if (capture > 0 && capture <= captureCount) {
                int consumed = 1;
                if (pos + 2 < replacement.size()) {
                    const int second = replacement[pos + 2].digitValue();
                    if (second >= 0 && capture * 10 + second <= captureCount) {
                        capture = capture * 10 + second;
                        ++consumed;
                    }
                }
                expanded.append(match.captured(capture));
                pos += consumed;
                continue;
            }
        }
        expanded.append(replacement[pos]);
    }
    return expanded;
}

void RegexSearcher::setPattern(const QString &value)
{
    mMatches.clear();
    Searcher::setPattern(value);
    mRegex.setPattern(value);
    updateRegexOptions();
}

void RegexSearcher::setOptions(const SearchOptions &options)
{
    mMatches.clear();
    Searcher::setOptions(options);
    updateRegexOptions();
}

void RegexSearcher::updateRegexOptions()
{
    if (options().testFlag(SearchOption::ssoMatchCase)) {
        mRegex.setPatternOptions(
                    mRegex.patternOptions() &
                    ~QRegularExpression::CaseInsensitiveOption);
    } else {
        mRegex.setPatternOptions(
                    mRegex.patternOptions() |
                    QRegularExpression::CaseInsensitiveOption);
    }
}

}
