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
#include "document.h"
#include "qt_utils/utils.h"
#include <QDataStream>
#include <QFile>
#include <QMutexLocker>
#include <stdexcept>
#include <QMessageBox>
#include <cmath>
#include <optional>
#include "qt_utils/charsetinfo.h"
#include <QDateTime>
#include <QDebug>

namespace QSynedit {

Document::Document(const QFont& font, QObject *parent):
    QObject{parent},
    mSetLineWidthLockCount{0},
    mMaxLineChangedInSetLinesWidth{false},
    mMutex{},
    mGlyphCalculator{font}
{
    mAppendNewLineAtEOF = true;
    mNewlineType = NewlineType::Windows;
    mIndexOfLongestLine = -1;
    mUpdateCount = 0;
    mUpdateDocumentLineWidthFunc = std::bind(&GlyphCalculator::calcLineWidth,
        &mGlyphCalculator,
        std::placeholders::_1,
        std::placeholders::_2,
        std::placeholders::_3);
    ensureHasLine();
}

int Document::parenthesisLevel(int line) const
{
    QMutexLocker locker(&mMutex);
    if (line>=0 && line < mLines.size()) {
        return mLines[line]->syntaxState()->parenthesisLevel;
    } else
        return 0;
}

int Document::bracketLevel(int line) const
{
    QMutexLocker locker(&mMutex);
    if (line>=0 && line < mLines.size()) {
        return mLines[line]->syntaxState()->bracketLevel;
    } else
        return 0;
}

int Document::braceLevel(int line) const
{
    QMutexLocker locker(&mMutex);
    if (line>=0 && line < mLines.size()) {
        return mLines[line]->syntaxState()->braceLevel;
    } else
        return 0;
}

int Document::lineWidth(int line) const
{
    QMutexLocker locker(&mMutex);
    if (line>=0 && line < mLines.size()) {
        int width = mLines[line]->width();
        //updateLongestLineWidth(width);
        return width;
    } else
        return 0;
}

int Document::lineWidth(int line, const QString &newText) const
{
    QMutexLocker locker(&mMutex);
    if (line<0 || line >= mLines.size())
        return 0;
    QString lineText = mLines[line]->lineText();
    if (lineText==newText) {
        return mLines[line]->width();
    } else {
        return mGlyphCalculator.stringWidth(newText,0);
    }
}

int Document::blockLevel(int line) const
{
    QMutexLocker locker(&mMutex);
    if (line>=0 && line < mLines.size()) {
        return mLines[line]->syntaxState()->blockLevel;
    } else
        return 0;
}

int Document::blockStarted(int line) const
{
    QMutexLocker locker(&mMutex);
    if (line>=0 && line < mLines.size()) {
        return mLines[line]->syntaxState()->blockStarted;
    } else
        return 0;
}

int Document::blockEnded(int line) const
{
    QMutexLocker locker(&mMutex);
    if (line>=0 && line < mLines.size()) {
        int result = mLines[line]->syntaxState()->blockEnded;
//        if (index+1 < mLines.size())
//            result += mLines[index+1]->syntaxState.blockEndedLastLine;
        return result;
    } else
        return 0;
}

int Document::maxLineWidth() const {
    QMutexLocker locker(&mMutex);
    if (mIndexOfLongestLine >= 0) {
        return mLines[mIndexOfLongestLine]->width();
    } else if (mLines.isEmpty()) {
        return 0;
    } else
        return -1;
}

QString Document::lineBreak() const
{
    switch(mNewlineType) {
    case NewlineType::Unix:
        return "\n";
    case NewlineType::Windows:
        return "\r\n";
    case NewlineType::MacOld:
        return "\r";
    }
    return "\n";
}

PSyntaxState Document::getSyntaxState(int line) const
{
    QMutexLocker locker(&mMutex);
    Q_ASSERT(line>=0 && line < mLines.size());
    // Crash Prevention
    if (line<0 || line>=count())
        return std::make_shared<SyntaxState>();
    return mLines[line]->syntaxState();
}

void Document::insertItem(int line, const QString &s)
{
    beginUpdate();
    PDocumentLine documentLine = std::make_shared<DocumentLine>(
                mUpdateDocumentLineWidthFunc);
    documentLine->setLineText(s);
    mLines.insert(line,documentLine);
    mLineSeqIndice.insert(documentLine->lineSeq(), documentLine);
    Q_ASSERT(mLineSeqIndice.count() == mLines.count());
    mIndexOfLongestLine = -1;
    endUpdate();
}

void Document::addItem(const QString &s)
{
    beginUpdate();
    PDocumentLine line = std::make_shared<DocumentLine>(mUpdateDocumentLineWidthFunc);
    line->setLineText(s);
    mLines.append(line);
    mLineSeqIndice.insert(line->lineSeq(), line);
    Q_ASSERT(mLineSeqIndice.count() == mLines.count());
    endUpdate();
}

bool Document::getAppendNewLineAtEOF() const
{
    QMutexLocker locker(&mMutex);
    return mAppendNewLineAtEOF;
}

void Document::setAppendNewLineAtEOF(bool appendNewLineAtEOF)
{
    QMutexLocker locker(&mMutex);
    mAppendNewLineAtEOF = appendNewLineAtEOF;
}

void Document::setSyntaxState(int line, const PSyntaxState& state)
{
    QMutexLocker locker(&mMutex);
    Q_ASSERT(line >=0 && line < mLines.count());
    mLines[line]->setSyntaxState(state);
}

QString Document::getLine(int line) const
{
    QMutexLocker locker(&mMutex);
    if (line<0 || line>=count())
        return 0;
    return mLines[line]->lineText();
}

size_t Document::getLineSeq(int line) const
{
    QMutexLocker locker(&mMutex);
    if (line<0 || line>=count())
        return 0;
    return mLines[line]->lineSeq();
}

// QList<int> Document::getGlyphPositions(int index)
// {
//     QMutexLocker locker(&mMutex);
//     if (index<0 || index>=mLines.count()) {
//         return QList<int>{};
//     }
//     return mLines[index]->glyphStartCharList();
// }

int Document::count() const
{
    QMutexLocker locker(&mMutex);
    return mLines.count();
}

QString Document::text() const
{
    QMutexLocker locker(&mMutex);
    return getTextStr();
}

void Document::setText(const QString &text)
{
    QMutexLocker locker(&mMutex);
    beginUpdate();
    auto action = finally([this]{
        endUpdate();
    });
    internalClear();
    if (!text.isEmpty()) {
        int pos = 0;
        int start;
        while (pos < text.length()) {
            start = pos;
            while (pos<text.length()) {
                if (text[pos] == '\r' || text[pos] == '\n') {
                    break;
                }
                pos++;
            }
            addItem(text.mid(start,pos-start));
            if (pos>=text.length())
                break;
            if (text[pos] == '\r')
                pos++;
            if (pos<text.length() && text[pos] == '\n')
                pos++;
        }
        mIndexOfLongestLine = -1;
    }
    ensureHasLine();
}

void Document::setContents(const QStringList &text)
{
    QMutexLocker locker(&mMutex);
    beginUpdate();
    internalClear();
    if (text.count() > 0) {
        foreach (const QString& s,text) {
            addItem(s);
        }
        mIndexOfLongestLine = -1;
    }
    ensureHasLine();
    endUpdate();
}

QStringList Document::content() const
{
    QMutexLocker locker(&mMutex);
    QStringList result;
    DocumentLines list = mLines;
    foreach (const PDocumentLine& line, list) {
        result.append(line->lineText());
    }
    return result;
}

void Document::beginUpdate()
{
    if (mUpdateCount == 0) {
        emit changing();
        beginSetLinesWidth();
    }
    mUpdateCount++;
}

void Document::endUpdate()
{
    mUpdateCount--;
    if (mUpdateCount == 0) {
        emit changed();
        endSetLinesWidth();
    }
}


int Document::addLine(const QString &s)
{
    QMutexLocker locker(&mMutex);
    beginUpdate();
    int Result = mLines.count();
    insertItem(Result, s);
    endUpdate();
    return Result;
}

void Document::addLines(const QStringList &strings)
{
    QMutexLocker locker(&mMutex);
    if (strings.count() > 0) {
        beginUpdate();
        auto action = finally([this]{
            endUpdate();
        });
        for (const QString& s:strings) {
            addItem(s);
        }
        mIndexOfLongestLine = -1;
    }
}

int Document::getTextLength() const
{
    QMutexLocker locker(&mMutex);
    int Result = 0;
    foreach (const PDocumentLine& line, mLines ) {
        Result += line->glyphsCount();
        if (mNewlineType == NewlineType::Windows) {
            Result += 2;
        } else {
            Result += 1;
        }
    }
    return Result;
}

void Document::clear()
{
    QMutexLocker locker(&mMutex);
    beginUpdate();
    internalClear();
    ensureHasLine();
    endUpdate();
}

void Document::deleteLines(int index, int numLines)
{
    QMutexLocker locker(&mMutex);
    Q_ASSERT(numLines>0);
    Q_ASSERT(index >= 0 && index < mLines.count());
    beginUpdate();
    if (mIndexOfLongestLine>=index) {
        if (mIndexOfLongestLine <index+numLines) {
            mIndexOfLongestLine = -1;
        } else {
            mIndexOfLongestLine -= numLines;
        }
    }
    int LinesAfter = mLines.count() - (index + numLines);
    if (LinesAfter < 0) {
       numLines = mLines.count() - index;
    }
    for (int i=0;i<numLines;i++) {
        mLineSeqIndice.remove(mLines[index+i]->lineSeq());
    }
    mLines.remove(index,numLines);
    Q_ASSERT(mLineSeqIndice.count() == mLines.count());
    ensureHasLine();
    endUpdate();
}

void Document::moveLine(int from, int to)
{
    QMutexLocker locker(&mMutex);
    Q_ASSERT(from >= 0 && from < mLines.count());
    Q_ASSERT(to >= 0 && to < mLines.count());
    beginUpdate();
    PDocumentLine temp = mLines[from];
    mLines.remove(from);
    mLines.insert(to, temp);
    Q_ASSERT(mLineSeqIndice.count() == mLines.count());
    if (from<to) {
        if (mIndexOfLongestLine == from) {
            mIndexOfLongestLine = to;
        } else if (from < mIndexOfLongestLine && mIndexOfLongestLine <= to) {
            --mIndexOfLongestLine;
        }
    } else {
        if (mIndexOfLongestLine == from) {
            mIndexOfLongestLine = to;
        } else if (to < mIndexOfLongestLine && mIndexOfLongestLine <= from) {
            ++mIndexOfLongestLine;
        }
    }
    endUpdate();
}

void Document::insertLine(int index, const QString &s)
{
    QMutexLocker locker(&mMutex);
    Q_ASSERT(index>=0 && index<=mLines.count());
    beginUpdate();
    insertItem(index, s);
    endUpdate();
}

void Document::deleteLine(int index)
{
    QMutexLocker locker(&mMutex);
    Q_ASSERT(index>=0 && index<mLines.count());
    beginUpdate();
    if (mIndexOfLongestLine == index)
        mIndexOfLongestLine = -1;
    else if (mIndexOfLongestLine>index)
        mIndexOfLongestLine -= 1;
    mLineSeqIndice.remove(mLines[index]->lineSeq());
    mLines.removeAt(index);
    Q_ASSERT(mLineSeqIndice.count() == mLines.count());
    ensureHasLine();
    endUpdate();
}

QString Document::getTextStr() const
{
    QString result;
    for (int i=0;i<mLines.count()-1;i++) {
        const PDocumentLine& line = mLines[i];
        result.append(line->lineText());
        result.append(lineBreak());
    }
    if (mLines.length()>0) {
        result.append(mLines.back()->lineText());
    }
    return result;
}

void Document::putLine(int index, const QString &s) {
    QMutexLocker locker(&mMutex);
    Q_ASSERT(index>=0 && index < mLines.count());
    if (index == mLines.count()) {
        addLine(s);
    } else {
        beginUpdate();
        mLines[index]->setLineText(s);
        if (mIndexOfLongestLine == index) {
            // width is invalidated, so we must recalculate longest line
            mIndexOfLongestLine = -1;
        }
        endUpdate();
    }
}

void Document::insertLines(int index, int numLines)
{
    QMutexLocker locker(&mMutex);
    Q_ASSERT(index>=0 && index<=mLines.count());
    Q_ASSERT(numLines>0);
    beginUpdate();
    auto action = finally([this]{
        endUpdate();
    });
    PDocumentLine line;
    mLines.insert(index,numLines,line);
    for (int i=index;i<index+numLines;i++) {
        mLines[i] = std::make_shared<DocumentLine>(mUpdateDocumentLineWidthFunc);
        mLineSeqIndice.insert(mLines[i]->lineSeq(), mLines[i]);
    }
    Q_ASSERT(mLineSeqIndice.count() == mLines.count());
    mIndexOfLongestLine = -1;
}

// A decoded document must never contain isolated UTF-16 surrogate halves.
// Some codecs can accept them without reporting a conversion error.
static bool hasValidUtf16(const QString &text)
{
    for (int i = 0; i < text.length(); ++i) {
        if (text[i].isHighSurrogate()) {
            if (i + 1 >= text.length() || !text[i + 1].isLowSurrogate())
                return false;
            ++i;
        } else if (text[i].isLowSurrogate()) {
            return false;
        }
    }
    return true;
}

void Document::addDecodedLines(const QString &text, bool &foundLineBreak)
{
    int start = 0;
    int pos = 0;
    while (pos < text.length()) {
        if (text[pos] != '\r' && text[pos] != '\n') {
            ++pos;
            continue;
        }
        addItem(text.mid(start, pos - start));
        const bool cr = text[pos] == '\r';
        const bool crlf = cr && pos + 1 < text.length() && text[pos + 1] == '\n';
        if (!foundLineBreak) {
            mNewlineType = crlf ? NewlineType::Windows
                : cr ? NewlineType::MacOld : NewlineType::Unix;
            foundLineBreak = true;
        }
        pos += crlf ? 2 : 1;
        start = pos;
    }
    if (start < text.length())
        addItem(text.mid(start));
    mAppendNewLineAtEOF = !text.isEmpty()
        && (text.endsWith('\n') || text.endsWith('\r'));
}

bool Document::tryLoadFileByEncoding(const QByteArray &encodingName, QFile &file,
                                     qint64 offset, bool *allAscii)
{
    TextDecoder decoder(encodingName);
    if (!decoder.isValid())
        return false;
    if (!file.seek(offset))
        throw FileError(tr("Failed to read file: %1").arg(file.fileName()));
    internalClear();
    mNewlineType = NewlineType::Windows;
    mAppendNewLineAtEOF = false;
    bool foundLineBreak = false;
    const bool utf8Codec = encodingName.compare(ENCODING_UTF8, Qt::CaseInsensitive) == 0;
    const QByteArray utf8Signature = QByteArray::fromHex("efbbbf");
    if (allAscii)
        *allAscii = true;
    while (!file.atEnd()) {
        const QByteArray line = file.readLine();
        if (file.error() != QFile::NoError || (line.isEmpty() && !file.atEnd()))
            throw FileError(tr("Failed to read file: %1").arg(file.fileName()));
        bool asciiLine = true;
        for (unsigned char byte : line) {
            if (byte == 0)
                throw BinaryFileError(tr("'%1' is a binaray File!").arg(file.fileName()));
            if (byte > 127)
                asciiLine = false;
        }
        QString decoded;
        if (utf8Codec && asciiLine) {
            // ASCII cannot contain malformed UTF-8 or surrogate halves.
            // Avoid repeating Unicode validation for ordinary source lines.
            decoded = QString::fromLatin1(line);
        } else {
            auto result = decoder.decode(line);
            if (!result.first || !hasValidUtf16(result.second))
                return false;
            decoded = std::move(result.second);
            // The file signature is skipped via offset. U+FEFF at the start
            // of a later line is content, not another file signature.
            if (utf8Codec && line.startsWith(utf8Signature))
                decoded.prepend(QChar(0xfeff));
        }
        if (allAscii && *allAscii)
            *allAscii = asciiLine;
        addDecodedLines(decoded, foundLineBreak);
    }
    return true;
}

void Document::loadUnicodeFile(QFile &file, const QByteArray &encodingName, int unitSize)
{
    TextDecoder decoder(encodingName);
    if (!decoder.isValid())
        throw FileError(tr("Can't load codec '%1'!").arg(QString::fromLatin1(encodingName)));
    if (!file.seek(0))
        throw FileError(tr("Failed to read file: %1").arg(file.fileName()));
    const QByteArray data = file.readAll();
    if (file.error() != QFile::NoError)
        throw FileError(tr("Failed to read file: %1").arg(file.fileName()));
    const auto invalidData = [&file, &encodingName] {
        throw FileError(tr("File '%1' contains invalid or incomplete data for codec '%2'.")
                            .arg(file.fileName(), QString::fromLatin1(encodingName)));
    };
    if (data.size() % unitSize != 0)
        invalidData();
    if (unitSize == 4) {
        // Qt/ICU may silently discard invalid UTF-32 scalars without reporting
        // a decoder error. Validate the original units before decoding.
        const QByteArray codec = encodingName.toUpper();
        bool bigEndian = Q_BYTE_ORDER == Q_BIG_ENDIAN;
        if (codec.endsWith("BE"))
            bigEndian = true;
        else if (codec.endsWith("LE"))
            bigEndian = false;
        else if (data.startsWith(QByteArray::fromHex("0000feff")))
            bigEndian = true;
        else if (data.startsWith(QByteArray::fromHex("fffe0000")))
            bigEndian = false;
        for (int i = 0; i < data.size(); i += 4) {
            const auto byte = [&data](int offset) { return quint32(quint8(data[offset])); };
            const quint32 scalar = bigEndian
                ? (byte(i) << 24) | (byte(i + 1) << 16) | (byte(i + 2) << 8) | byte(i + 3)
                : byte(i) | (byte(i + 1) << 8) | (byte(i + 2) << 16) | (byte(i + 3) << 24);
            if (scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff))
                invalidData();
        }
    }
    auto [ok, decoded] = decoder.decode(data);
    if (!ok || !hasValidUtf16(decoded))
        invalidData();
    if (decoded.contains(QChar(0)))
        throw BinaryFileError(tr("'%1' is a binaray File!").arg(file.fileName()));
    internalClear();
    mNewlineType = NewlineType::Windows;
    bool foundLineBreak = false;
    addDecodedLines(decoded, foundLineBreak);
}

PDocumentLine Document::findLineBySeq(size_t lineSeq) const
{
    QMutexLocker locker(&mMutex);
    return mLineSeqIndice.value(lineSeq);
}

void Document::setTabSize(int newTabSize)
{
    if (tabSize()!=newTabSize) {
        mGlyphCalculator.setTabSize(newTabSize);
        invalidateAllLineWidth();
    }
}

void Document::setForceMonospace(bool newForceMonospace)
{
    bool oldValue = forceMonospace();    // (fix): forceMonospace() returns bool, not int
    mGlyphCalculator.setForceMonospace(newForceMonospace);
    if (oldValue != newForceMonospace)
        invalidateAllLineWidth();
}

#ifdef QSYNEDIT_TEST
QList<int> Document::getGlyphStartCharListForTest(int line)
{
    QMutexLocker locker(&mMutex);
    return mLines[line]->glyphStartCharList();;
}
#endif
void Document::loadFromFile(const QString& filename, const QByteArray& encoding, QByteArray& realEncoding)
{
    Document candidate(QFont{});
    QByteArray detectedEncoding;
    candidate.loadFromFileInPlace(filename, encoding, detectedEncoding);
    replaceWith(candidate);
    realEncoding = detectedEncoding;
}

void Document::replaceWith(Document &source)
{
    if (&source == this)
        return;
    QMutexLocker locker(&mMutex);
    QMutexLocker sourceLocker(&source.mMutex);
    // Rebind lazy width calculation before transferring ownership of the lines.
    for (const auto &line : source.mLines) {
        line->mUpdateWidthFunc = mUpdateDocumentLineWidthFunc;
        line->invalidateWidth();
    }
    beginUpdate();
    auto cleanup = finally([this] { endUpdate(); });
    mLines.swap(source.mLines);
    mLineSeqIndice.swap(source.mLineSeqIndice);
    mNewlineType = source.mNewlineType;
    mAppendNewLineAtEOF = source.mAppendNewLineAtEOF;
    mIndexOfLongestLine = -1;
    ensureHasLine();
}

void Document::loadFromFileInPlace(const QString &filename, const QByteArray &encoding,
                                   QByteArray &realEncoding)
{
    QMutexLocker locker(&mMutex);
    QFile file(filename);
    if (!file.open(QFile::ReadOnly))
        throw FileError(tr("Can't open file '%1' for read!").arg(file.fileName()));
    beginUpdate();
    internalClear();
    auto action = finally([this] {
        ensureHasLine();
        endUpdate();
    });
    const QByteArray prefix = file.peek(4);
    if (file.error() != QFile::NoError)
        throw FileError(tr("Failed to read file: %1").arg(filename));
    const bool utf8Bom = prefix.startsWith(QByteArray::fromHex("efbbbf"));
    const bool utf32Bom = prefix.startsWith(QByteArray::fromHex("fffe0000"))
        || prefix.startsWith(QByteArray::fromHex("0000feff"));
    const bool utf16Bom = prefix.startsWith(QByteArray::fromHex("fffe"))
        || prefix.startsWith(QByteArray::fromHex("feff"));
    QByteArray detectedEncoding = encoding;
    if (encoding == ENCODING_AUTO_DETECT) {
        if (utf32Bom)
            detectedEncoding = ENCODING_UTF32_BOM;
        else if (utf16Bom)
            detectedEncoding = ENCODING_UTF16_BOM;
        else if (utf8Bom)
            detectedEncoding = ENCODING_UTF8_BOM;
        else {
            bool allAscii = false;
            if (tryLoadFileByEncoding(ENCODING_UTF8, file, 0, &allAscii)) {
                realEncoding = allAscii ? ENCODING_ASCII : ENCODING_UTF8;
                return;
            }
            // A BOM fixes the codec. Only files without a BOM may be tried
            // with the system's legacy encodings after UTF-8 validation fails.
            if (!pCharsetInfoManager)
                throw FileError(tr("File '%1' contains invalid or incomplete data for codec '%2'.")
                                    .arg(filename, QString::fromLatin1(ENCODING_UTF8)));
            detectedEncoding = pCharsetInfoManager->getDefaultSystemEncoding();
            if (tryLoadFileByEncoding(detectedEncoding, file)) {
                realEncoding = detectedEncoding;
                return;
            }
            const QList<PCharsetInfo> charsets =
                pCharsetInfoManager->findCharsetByLocale(pCharsetInfoManager->localeName());
            QSet<QByteArray> tried{detectedEncoding, ENCODING_UTF8};
            for (const auto &charset : charsets) {
                if (tried.contains(charset->name))
                    continue;
                tried.insert(charset->name);
                if (tryLoadFileByEncoding(charset->name, file)) {
                    realEncoding = charset->name;
                    return;
                }
            }
            throw FileError(tr("File '%1' contains invalid or incomplete data for codec '%2'.")
                                .arg(filename, QString::fromLatin1(detectedEncoding)));
        }
    }
    if (detectedEncoding == ENCODING_SYSTEM_DEFAULT) {
        if (!pCharsetInfoManager)
            throw FileError(tr("Can't load codec '%1'!").arg(QString::fromLatin1(detectedEncoding)));
        detectedEncoding = pCharsetInfoManager->getDefaultSystemEncoding();
    }
    QByteArray codecName = detectedEncoding;
    if (detectedEncoding == ENCODING_UTF16_BOM)
        codecName = ENCODING_UTF16;
    else if (detectedEncoding == ENCODING_UTF32_BOM)
        codecName = ENCODING_UTF32;
    else if (detectedEncoding == ENCODING_UTF8_BOM)
        codecName = ENCODING_UTF8;
    TextDecoder decoder(codecName);
    if (!decoder.isValid())
        throw FileError(tr("Can't load codec '%1'!").arg(QString::fromLatin1(codecName)));
    if (codecName.toUpper().startsWith("UTF-16")) {
        loadUnicodeFile(file, codecName, 2);
    } else if (codecName.toUpper().startsWith("UTF-32")) {
        loadUnicodeFile(file, codecName, 4);
    } else if (!tryLoadFileByEncoding(codecName, file,
                    codecName.compare(ENCODING_UTF8, Qt::CaseInsensitive) == 0 && utf8Bom ? 3 : 0)) {
        throw FileError(tr("File '%1' contains invalid or incomplete data for codec '%2'.")
                            .arg(filename, QString::fromLatin1(codecName)));
    }
    realEncoding = detectedEncoding;
}

void Document::saveToFile(QFileDevice &file, const QByteArray &encoding,
                          QByteArray &realEncoding) const
{
    QMutexLocker locker(&mMutex);
    if (encoding == ENCODING_AUTO_DETECT)
        throw FileError(tr("Can't load codec '%1'!").arg(QString::fromLatin1(encoding)));
    QByteArray detectedEncoding = encoding;
    QByteArray codecName = encoding;
    QByteArray bom;
    if (encoding == ENCODING_UTF16 || encoding == ENCODING_UTF16_BOM) {
        codecName = "UTF-16LE";
        if (encoding == ENCODING_UTF16_BOM)
            bom = QByteArray::fromHex("fffe");
    } else if (encoding == ENCODING_UTF32 || encoding == ENCODING_UTF32_BOM) {
        codecName = "UTF-32LE";
        if (encoding == ENCODING_UTF32_BOM)
            bom = QByteArray::fromHex("fffe0000");
    } else if (encoding == ENCODING_UTF8_BOM) {
        codecName = ENCODING_UTF8;
        bom = QByteArray::fromHex("efbbbf");
    } else if (encoding == ENCODING_SYSTEM_DEFAULT) {
        if (!pCharsetInfoManager)
            throw FileError(tr("Can't load codec '%1'!").arg(QString::fromLatin1(encoding)));
        codecName = pCharsetInfoManager->getDefaultSystemEncoding();
        detectedEncoding = codecName;
    }
    TextEncoder encoder(codecName);
    if (!encoder.isValid())
        throw FileError(tr("Can't load codec '%1'!").arg(QString::fromLatin1(codecName)));
    TextDecoder roundTripDecoder(codecName);
    const bool unicodeCodec = codecName.toUpper().startsWith("UTF-");
    if (!file.isOpen() && !file.open(QIODevice::WriteOnly))
        throw FileError(tr("Can't open file '%1' for save!").arg(file.fileName()));
    if (!file.isWritable())
        throw FileError(tr("Can't open file '%1' for save!").arg(file.fileName()));
    if (!bom.isEmpty() && file.write(bom) != bom.size())
        throw FileError(tr("Failed to write file: %1").arg(file.fileName()));
    bool allAscii = true;
    for (int i = 0; i < mLines.count(); ++i) {
        QString text = mLines[i]->lineText();
        if (i + 1 < mLines.count() || mAppendNewLineAtEOF)
            text += lineBreak();
        auto [ok, data] = encoder.encode(text);
        if (ok && !unicodeCodec) {
            // ICU codecs can replace unrepresentable characters without
            // setting hasError(). Verify the actual encoded result too.
            const auto decoded = roundTripDecoder.decode(data);
            ok = decoded.first && decoded.second == text;
        }
        if (!ok || !hasValidUtf16(text))
            throw FileError(tr("Text cannot be represented by codec '%1' without losing data.")
                                .arg(QString::fromLatin1(codecName)));
        if (file.write(data) != data.size())
            throw FileError(tr("Failed to write file: %1").arg(file.fileName()));
        if (allAscii)
            allAscii = isTextAllAscii(text);
    }
    // Preserve explicitly selected BOM/wide/legacy codecs. ASCII is only a
    // refinement of plain UTF-8, never a reason to discard a requested BOM.
    if (allAscii && encoding == ENCODING_UTF8)
        detectedEncoding = ENCODING_ASCII;
    realEncoding = detectedEncoding;
}

QString Document::glyph(int line, int glyphIdx) const
{
    QMutexLocker locker(&mMutex);
    return mLines[line]->glyph(glyphIdx);
}

QString Document::glyphAt(int line, int charPos) const
{
    QMutexLocker locker(&mMutex);
    Q_ASSERT(line>=0 && line<count());
    // Crash Prevention
    if (line<0 || line>=count())
        return 0;
    QList<int> glyphStartCharList = mLines[line]->glyphStartCharList();
    int glyphIdx = charToGlyphIndex(mLines[line]->lineText(), glyphStartCharList, charPos);
    return mLines[line]->glyph(glyphIdx);
}

int Document::charToGlyphStartChar(int line, int charPos) const
{
    QMutexLocker locker(&mMutex);
    Q_ASSERT(line>=0 && line<count());
    // Crash Prevention
    if (line<0 || line>=count())
        return 0;
    QList<int> glyphStartCharList = mLines[line]->glyphStartCharList();
    int glyphIdx = charToGlyphIndex(mLines[line]->lineText(), glyphStartCharList, charPos);
    return mLines[line]->glyphStartChar(glyphIdx);
}

// int Document::columnToGlyphStartColumn(int line, int charPos)
// {
//     QMutexLocker locker(&mMutex);
//     QList<int> glyphColumnsList = mLines[line]->glyphColumnsList();
//     int glyphIdx = columnToGlyphIndex(glyphColumnsList, charPos);
//     return mLines[line]->glyphStartColumn(glyphIdx);
// }

QList<int> calcGlyphStartCharList(const QString &text)
{
    QList<int> glyphStartCharList;
    //parse mGlyphs
    int i=0;
    bool consecutive = false;
    while (i<text.length()) {
        QChar ch = text[i];
        if (ch.isHighSurrogate() && i+1<text.length() && QChar::isLowSurrogate(text[i+1].unicode())) {
            //character that larger than 0xffff
            uint ucs4 = QChar::surrogateToUcs4(ch, text[i+1]);
            if (QChar::combiningClass(ucs4)!=0 && !glyphStartCharList.isEmpty()) {
                //a Combining character
            } else if (ucs4>=0xE0100 && ucs4 <= 0xE01EF) {
                //variation selector
            } else if (ucs4>=0x1F3FB && ucs4 <= 0x1F3FF) {
                //Emoji modifiers for skin tone
            } else if (!consecutive) {
                glyphStartCharList.append(i);
                if (ucs4>=0x1F1E6 && ucs4<=0x1F1FF) // National Flags emoji
                    consecutive = true;
                else
                    consecutive = false;
            } else {
                consecutive = false;
            }
            i+=2;
            continue;
        } else if (ch.unicode() == 0x200D || ch.unicode() == 0x200C ) {
            //ZWJ && ZWNJ
            consecutive = true;
        } else if (ch.combiningClass()!=0 && !glyphStartCharList.isEmpty()) {
            //a Combining character
        } else if (ch.unicode()>=0xFE00 && ch.unicode()<=0xFE0F) {
            //variation selector
        } else {
            //qDebug("%x %d", ch.unicode(), ch.combiningClass());
            if (!consecutive)
                glyphStartCharList.append(i);
            consecutive = false;
        }
        i++;
    }
    //qDebug()<<text<<glyphStartCharList;
    // if (text=="...}")
    //     qDebug()<<"where it comes?";
    if (!text.isEmpty() && glyphStartCharList.isEmpty())
        glyphStartCharList.append(0);
    return glyphStartCharList;
}

int GlyphCalculator::stringWidth(const QString &str, int left) const
{
    QList<int> glyphStartCharList = calcGlyphStartCharList(str);
    int right;
    calcGlyphPositionList(str, glyphStartCharList, left, right);
    return right - left;
}

int GlyphCalculator::stringWidth(const QString &str, int left, const QFontMetrics &fontMetrics)
{
    QList<int> glyphStartCharList = calcGlyphStartCharList(str);
    int right;
    calcGlyphPositionList(str, glyphStartCharList, fontMetrics, left, right);
    return right - left;
}

int Document::glyphCount(int line) const
{
    QMutexLocker locker(&mMutex);
    Q_ASSERT(line>=0 && line<count());
    // Crash Prevention
    if (line<0 || line>=count())
        return 0;
    return mLines[line]->glyphsCount();
}

int Document::glyphStartChar(int line, int glyphIdx) const
{
    QMutexLocker locker(&mMutex);
    Q_ASSERT(line>=0 && line<count());
    // Crash Prevention
    if (line<0 || line>=count())
        return 0;
    return mLines[line]->glyphStartChar(glyphIdx);
}

int Document::glyphEndChar(int line, int glyphIdx) const
{
    QMutexLocker locker(&mMutex);
    Q_ASSERT(line>=0 && line<count());
    // Crash Prevention
    if (line<0 || line>=count())
        return 0;
    return mLines[line]->glyphStartChar(glyphIdx)+mLines[line]->glyphLength(glyphIdx);
}

int Document::glyphLength(int line, int glyphIdx) const
{
    QMutexLocker locker(&mMutex);
    Q_ASSERT(line>=0 && line<count());
    // Crash Prevention
    if (line<0 || line>=count())
        return 0;
    return mLines[line]->glyphLength(glyphIdx);
}

int Document::glyphStartPostion(int line, int glyphIdx) const
{
    QMutexLocker locker(&mMutex);
    Q_ASSERT(line>=0 && line<count());
    // Crash Prevention
    if (line<0 || line>=count())
        return 0;
    return mLines[line]->glyphStartPosition(glyphIdx);
}

int Document::glyphWidth(int line, int glyphIdx) const
{
    QMutexLocker locker(&mMutex);
    Q_ASSERT(line>=0 && line<count());
    // Crash Prevention
    if (line<0 || line>=count())
        return 0;
    return mLines[line]->glyphWidth(glyphIdx);
}

int Document::charToGlyphIndex(int line, int charIdx) const
{
    QMutexLocker locker(&mMutex);
    Q_ASSERT(line>=0 && line<count());
    // Crash Prevention
    if (line<0 || line>=count())
        return 0;
    QList<int> glyphStartCharList = mLines[line]->glyphStartCharList();
    return charToGlyphIndex(mLines[line]->lineText(), glyphStartCharList, charIdx);
}

int Document::charToGlyphIndex(const QString& str, const QList<int> &glyphStartCharList, int charIdx) const
{
    return searchForSegmentIdx(glyphStartCharList, str.length(), charIdx);
}

QList<int> GlyphCalculator::calcGlyphPositionList(const QString &lineText, const QList<int> &glyphStartCharList, const QFontMetrics &fontMetrics, int left, int &right) const
{
    right = std::max(0,left);
    int start,end;
    QList<int> glyphPostionList;
    for (int i=0;i<glyphStartCharList.length();i++) {
        start = glyphStartCharList[i];
        if (i+1<glyphStartCharList.length()) {
            end = glyphStartCharList[i+1];
        } else {
            end = lineText.length();
        }
        QString glyph = lineText.mid(start,end-start);
        int gWidth = glyphWidth(glyph, right, fontMetrics, mForceMonospace);
        glyphPostionList.append(right);
        right += gWidth;
    }
    return glyphPostionList;
}

int Document::xposToGlyphIndex(int line, int xpos) const
{
    QMutexLocker locker(&mMutex);
    if (line<0 || line>=count())
        return 0;
    QList<int> glyphPositionList = mLines[line]->glyphStartPositionList();
    return xposToGlyphIndex(mLines[line]->width(), glyphPositionList, xpos);
}

int Document::xposToGlyphIndex(int strWidth, const QList<int> &glyphPositionList, int xpos) const
{
    return searchForSegmentIdx(glyphPositionList,strWidth,xpos);
}

int Document::charToGlyphStartPosition(int line, int charPos) const
{
    QMutexLocker locker(&mMutex);
    if (line<0 || line>=count())
        return 0;
    QList<int> glyphStartCharList = mLines[line]->glyphStartCharList();
    int glyphIdx = charToGlyphIndex(mLines[line]->lineText(), glyphStartCharList, charPos);
    return mLines[line]->glyphStartPosition(glyphIdx);
}

int Document::xposToGlyphStartChar(int line, int xpos) const
{
    QMutexLocker locker(&mMutex);
    if (line<0 || line>=count())
        return 0;
    QList<int> glyphPositionList = mLines[line]->glyphStartPositionList();
    int glyphIdx = xposToGlyphIndex(mLines[line]->width(), glyphPositionList, xpos);
    return mLines[line]->glyphStartChar(glyphIdx);
}

int Document::charToGlyphStartPosition(int line, const QString newStr, int charPos) const
{
    QMutexLocker locker(&mMutex);
    if (line>=0 && line<count() && mLines[line]->lineText() == newStr) {
        return charToGlyphStartPosition(line,charPos);
    } else {
        QList<int> glyphStartCharList = calcGlyphStartCharList(newStr);
        int glyphIdx = charToGlyphIndex(newStr, glyphStartCharList, charPos);
        int width;
        QList<int> glyphStartPositionList = mGlyphCalculator.calcGlyphPositionList(newStr, width);
        if (glyphIdx<glyphStartCharList.length())
            return glyphStartPositionList[glyphIdx];
        else
            return width;
    }
}

int Document::xposToGlyphStartChar(int line, const QString newStr, int xpos) const
{
    QMutexLocker locker(&mMutex);
    if (line<0 || line>=count())
        return 0;
    QList<int> glyphPositionList;
    int width;
    if (mLines[line]->lineText() == newStr) {
        glyphPositionList = mLines[line]->glyphStartPositionList();
        width = mLines[line]->width();
    } else {
        glyphPositionList = mGlyphCalculator.calcGlyphPositionList(mLines[line]->lineText(), width);
    }
    int glyphIdx = xposToGlyphIndex(width, glyphPositionList, xpos);
    return mLines[line]->glyphStartChar(glyphIdx);
}

// int Document::charToColumn(int line, int charPos)
// {
//     QMutexLocker locker(&mMutex);
//     QList<int> glyphPositions = mLines[line]->glyphPositions();

// }

// int Document::charToColumn(const QString &lineText, int charPos) const
// {
//     QMutexLocker locker(&mMutex);
//     QList<int> glyphPositions = calcGlyphPositions(lineText);
//     return charToColumn(lineText, glyphPositions, charPos);
// }

// int Document::charToColumn(const QString &lineText, const QList<int> &glyphPositions, int charPos) const
// {
//     Q_ASSERT(charPos<lineText.length() && charPos>=0);
//     int glyphIdx;
//     for (glyphIdx=0;glyphIdx<glyphPositions.length();glyphIdx++) {
//         if (charPos<glyphPositions[glyphIdx])
//             break;
//     }
//     int cols=0;
// }

// int Document::columnToChar(const QString &lineText, int column) const
// {
//     QList<int> glyphPositions = calcGlyphPositions(lineText);
//     return columnToChar(lineText, glyphPositions, column);
// }

// int Document::columnToChar(const QString &lineText, const QList<int> &glyphPositions, int column) const
// {

// }


void Document::internalClear()
{
    Q_ASSERT(mLines.count() == mLineSeqIndice.count());
    beginUpdate();
    mLines.clear();
    mLineSeqIndice.clear();
    mIndexOfLongestLine = -1;
    endUpdate();
}

void Document::ensureHasLine()
{
    if (mLines.isEmpty())
        insertItem(0,"");
}

bool Document::lineWidthValid(int line)
{
    if (line<0 || line>=mLines.count())
        return false;
    return mLines[line]->mWidth>=0 && !mLines[line]->mIsTempWidth;
}

void Document::beginSetLinesWidth()
{
    if (mSetLineWidthLockCount == 0) {
        mMaxLineChangedInSetLinesWidth = false;
    }
    mSetLineWidthLockCount++;
}

void Document::endSetLinesWidth()
{
    mSetLineWidthLockCount--;
    if (mSetLineWidthLockCount == 0) {
        if (mMaxLineChangedInSetLinesWidth)
            updateMaxLineWidthAndNotify();
    }
}

void Document::setLineWidth(int line, int newWidth, const QList<int> &glyphStartPositionList)
{
    QMutexLocker locker(&mMutex);
    if (line<0 || line>=count())
        return ;
    int oldWidth = mLines[line]->mWidth;
    // qDebug()<<line<<oldWidth<<newWidth;
    mLines[line]->mWidth = newWidth;
    mLines[line]->mIsTempWidth = false;
    mLines[line]->mGlyphStartPositionList = glyphStartPositionList;
    if (mIndexOfLongestLine<0) {
        mIndexOfLongestLine = line;
        updateMaxLineWidthChanged();
    } else if (mIndexOfLongestLine == line) {
        if (oldWidth > newWidth) {
            mIndexOfLongestLine = -1;
            updateMaxLineWidthChanged();
        } else if (oldWidth < newWidth) {
            updateMaxLineWidthChanged();
        }
    } else if (mLines[mIndexOfLongestLine]->mWidth < newWidth) {
        mIndexOfLongestLine = line;
        updateMaxLineWidthChanged();
    }
    Q_ASSERT(mLines[line]->mGlyphStartPositionList.length() == mLines[line]->mGlyphStartCharList.length());
}

void Document::updateMaxLineWidthChanged()
{
    if (mSetLineWidthLockCount>0) {
        mMaxLineChangedInSetLinesWidth = true;
    } else {
        //updateMaxLineWidthAndNotify();
        emit maxLineWidthChanged();
    }
}

void Document::updateMaxLineWidthAndNotify()
{
    int MaxLen = -1;
    mIndexOfLongestLine = -1;
    if (mLines.count() > 0 ) {
        for (int i=0;i<mLines.size();i++) {
            int len = mLines[i]->mWidth;
            if (len > MaxLen) {
                MaxLen = len;
                mIndexOfLongestLine = i;
            }
        }
    }
    if (mIndexOfLongestLine>=0)
        emit maxLineWidthChanged();
}

QList<int> GlyphCalculator::calcGlyphPositionList(const QString &lineText, int &width) const
{
    QList<int> glyphStartCharList = calcGlyphStartCharList(lineText);
    return calcGlyphPositionList(lineText,glyphStartCharList,0,width);
}

QList<int> Document::getGlyphStartCharList(int line, const QString &lineText)
{
    if (line<0 || line>=count() || mLines[line]->lineText()!=lineText)
        return calcGlyphStartCharList(lineText);
    return mLines[line]->glyphStartCharList();
}

QList<int> Document::getGlyphStartCharList(int line)
{
    if (line<0 || line>=count())
        return QList<int>();
    return mLines[line]->glyphStartCharList();
}

QList<int> Document::getGlyphStartPositionList(int line)
{
    return mLines[line]->glyphStartPositionList();
}

NewlineType Document::getNewlineType() const
{
    QMutexLocker locker(&mMutex);
    return mNewlineType;
}

void Document::setNewlineType(const NewlineType &fileEndingType)
{
    QMutexLocker locker(&mMutex);
    mNewlineType = fileEndingType;
}

bool Document::empty() const
{
    QMutexLocker locker(&mMutex);
    return mLines.count()==1 && mLines[0]->lineText().length()==0;
}

void Document::invalidateAllLineWidth()
{
    QMutexLocker locker(&mMutex);
    for (PDocumentLine& line:mLines) {
        line->invalidateWidth();
    }
    mIndexOfLongestLine = -1;
}

void Document::invalidateAllNonTempLineWidth()
{
    QMutexLocker locker(&mMutex);
    for (PDocumentLine& line:mLines) {
        if (!line->mIsTempWidth)
            line->mIsTempWidth = true;
    }
}

//Reserve 0
size_t DocumentLine::seqCounter = 1;

DocumentLine::DocumentLine(DocumentLine::UpdateWidthFunc updateWidthFunc):
    mSyntaxState{},
    mWidth{-1},
    mIsTempWidth{true},
    mUpdateWidthFunc{updateWidthFunc},
    mLineSeq{seqCounter++}
{

}

int DocumentLine::glyphLength(int i) const
{
    return calcSegmentInterval(mGlyphStartCharList, mLineText.length(), i);
}

QString DocumentLine::glyph(int i) const
{
   if (i<0 || i>=mGlyphStartCharList.length())
       return QString();
   return mLineText.mid(glyphStartChar(i),glyphLength(i));
}

int DocumentLine::glyphStartPosition(int i)
{
    if (i<0)
       return 0;
    if (mWidth<0)
        updateWidth();
    if (i>=mGlyphStartPositionList.length())
       return mWidth;
    return mGlyphStartPositionList[i];
}

int DocumentLine::glyphWidth(int i)
{
    if (mWidth <0)
        updateWidth();
    return calcSegmentInterval(mGlyphStartPositionList, mWidth, i);
}

int DocumentLine::width()
{
    if(mWidth<0)
        updateWidth();
    return mWidth;
}

void DocumentLine::setLineText(const QString &newLineText)
{
    mLineText = newLineText;
    mGlyphStartCharList = calcGlyphStartCharList(newLineText);
    invalidateWidth();
}

void DocumentLine::updateWidth()
{
    Q_ASSERT(mUpdateWidthFunc!=nullptr);
    mGlyphStartPositionList = mUpdateWidthFunc(mLineText, mGlyphStartCharList, mWidth);
//    qDebug()<<"Update Width"<<mLineText<<mWidth<<mGlyphPositionList;
}

const QList<int> &DocumentLine::glyphStartPositionList()
{
    if(mWidth<0)
        updateWidth();
    return mGlyphStartPositionList;
}

int DocumentLine::glyphStartChar(int i) const
{
   if (i<0)
       return 0;
   if (i>=mGlyphStartCharList.length())
       return mLineText.length();
   return mGlyphStartCharList[i];
}

UndoList::UndoList():QObject()
{
    mNextChangeNumber = 1;
    mInsideRedo = false;

    mBlockChangeNumber=0;
    mBlockLock=0;
    mFullUndoImposible=false;
    mLastPoppedItemChangeNumber=0;
    mInitialChangeNumber = 0;
    mLastRestoredItemChangeNumber=0;
}

PUndoItem UndoList::addChange(ChangeReason reason, const CharPos &startPos,
                                const CharPos &endPos, const QStringList& changeText,
                                SelectionMode selMode)
{
    Q_ASSERT(inBlock());
    PUndoItem  newItem = std::make_shared<UndoItem>(
                reason,
                selMode,startPos,endPos,changeText,
                mBlockChangeNumber);
    mItems.append(newItem);

    if (reason!=ChangeReason::GroupBreak && !inBlock()) {
        emit addedUndo();
    }
    return newItem;
}

void UndoList::restoreChange(ChangeReason reason, const CharPos &startPos,
                             const CharPos &endPos, const QStringList &changeText,
                             SelectionMode selMode, size_t changeNumber)
{
    PUndoItem  newItem = std::make_shared<UndoItem>(reason,
                                                    selMode,startPos,endPos,changeText,
                                                    changeNumber);
    restoreChange(newItem);
}

void UndoList::restoreChange(PUndoItem item)
{
    size_t changeNumber = item->changeNumber();
    mItems.append(item);
    if (changeNumber>mNextChangeNumber)
        mNextChangeNumber=changeNumber;
    if (changeNumber!=mLastRestoredItemChangeNumber) {
        emit addedUndo();
    }
    mLastRestoredItemChangeNumber=changeNumber;
}

void UndoList::addGroupBreak()
{
    if (!canUndo())
        return;

    if (lastChangeReason() != ChangeReason::GroupBreak) {
        addChange(ChangeReason::GroupBreak, {0,0}, {0,0}, QStringList(), SelectionMode::Normal);
    }
}

void UndoList::beginBlock(const CharPos &caret, const CharPos &selBegin, const CharPos &selEnd, SelectionMode selMode)
{
//    qDebug()<<"begin block";
    if (mBlockLock==0) {
        mBlockChangeNumber = getNextChangeNumber();
        mCaretInfoBeforeChange.insert(
                    mBlockChangeNumber,
                    std::make_shared<CaretAndSelectionInfo>(
                        caret,
                        selBegin,
                        selEnd,
                        selMode
                        ));
    }
    mBlockLock++;

}

void UndoList::clear()
{
    mItems.clear();
    mFullUndoImposible = false;
    mInitialChangeNumber=0;
    mLastPoppedItemChangeNumber=0;
    mLastRestoredItemChangeNumber=0;
    mBlockLock=0;
    mCaretInfoAfterChange.clear();
    mCaretInfoBeforeChange.clear();
}

void UndoList::endBlock(const CharPos &caret, const CharPos &selBegin, const CharPos &selEnd, SelectionMode selMode)
{
//    qDebug()<<"end block";
    if (mBlockLock > 0) {
        mBlockLock--;
        if (mBlockLock == 0)  {
            size_t iBlockID = mBlockChangeNumber;
            mBlockChangeNumber = 0;
            if (mItems.count() > 0 && peekItem()->changeNumber() == iBlockID) {
                mCaretInfoAfterChange.insert(
                            iBlockID,
                            std::make_shared<CaretAndSelectionInfo>(
                                caret,
                                selBegin,
                                selEnd,
                                selMode
                                ));
                emit addedUndo();
            }
        }
    }
}

PCaretAndSelectionInfo UndoList::caretAndSelBeforeChange(size_t changeNumber)
{
    PCaretAndSelectionInfo info = mCaretInfoBeforeChange.value(changeNumber);
    Q_ASSERT(info!=nullptr);
    return info;
}

PCaretAndSelectionInfo UndoList::caretAndSelAfterChange(size_t changeNumber)
{
    PCaretAndSelectionInfo info = mCaretInfoAfterChange.value(changeNumber);
    Q_ASSERT(info!=nullptr);
    return info;
}

void UndoList::restoreCaretAndSelInfos(size_t changeNumber, const PCaretAndSelectionInfo &beforeInfo, const PCaretAndSelectionInfo &afterInfo)
{
    mCaretInfoBeforeChange.insert(changeNumber, beforeInfo);
    mCaretInfoAfterChange.insert(changeNumber, afterInfo);
}

void UndoList::removeCaretAndSelInfo(size_t changeNumber)
{
    mCaretInfoAfterChange.remove(changeNumber);
    mCaretInfoBeforeChange.remove(changeNumber);
}

bool UndoList::inBlock()
{
    return mBlockLock>0;
}

unsigned int UndoList::getNextChangeNumber()
{
    return mNextChangeNumber++;
}

ChangeReason UndoList::lastChangeReason()
{
    if (mItems.count() == 0)
        return ChangeReason::Nothing;
    else
        return mItems.last()->changeReason();
}

bool UndoList::isEmpty()
{
    return mItems.count()==0;
}

PUndoItem UndoList::peekItem()
{
    if (mItems.count() == 0)
        return PUndoItem();
    else
        return mItems.last();
}

PUndoItem UndoList::popItem()
{
    if (mItems.count() == 0)
        return PUndoItem();
    else {
        PUndoItem item = mItems.last();
//        qDebug()<<"popped"<<item->changeNumber()<<item->changeText()<<(int)item->changeReason()<<mLastPoppedItemChangeNumber;
        mLastPoppedItemChangeNumber =  item->changeNumber();
        mItems.removeLast();
        return item;
    }
}

bool UndoList::canUndo()
{
    return mItems.count()>0;
}

int UndoList::itemCount()
{
    return mItems.count();
}

bool UndoList::initialState()
{
    if (itemCount() == 0) {
        return mInitialChangeNumber==0;
    } else {
        return peekItem()->changeNumber() == mInitialChangeNumber;
    }
}

void UndoList::setInitialState()
{
    if (itemCount() == 0)
        mInitialChangeNumber = 0;
    else
        mInitialChangeNumber = peekItem()->changeNumber();
}

bool UndoList::insideRedo() const
{
    return mInsideRedo;
}

void UndoList::setInsideRedo(bool insideRedo)
{
    mInsideRedo = insideRedo;
}

bool UndoList::fullUndoImposible() const
{
    return mFullUndoImposible;
}

SelectionMode UndoItem::changeSelMode() const
{
    return mChangeSelMode;
}

CharPos UndoItem::changeStartPos() const
{
    return mChangeStartPos;
}

CharPos UndoItem::changeEndPos() const
{
    return mChangeEndPos;
}

QStringList UndoItem::changeText() const
{
    return mChangeText;
}

size_t UndoItem::changeNumber() const
{
    return mChangeNumber;
}

UndoItem::UndoItem(ChangeReason reason, SelectionMode selMode,
                                 CharPos startPos, CharPos endPos,
                                 const QStringList& text, int number)
{
    mChangeReason = reason;
    mChangeSelMode = selMode;
    mChangeStartPos = startPos;
    mChangeEndPos = endPos;
    mChangeText = text;
    mChangeNumber = number;
    int length=0;
    foreach (const QString& s, text) {
        length+=s.length();
    }
}

ChangeReason UndoItem::changeReason() const
{
    return mChangeReason;
}

RedoList::RedoList()
{

}

void RedoList::addRedo(ChangeReason reason, const CharPos &startPos, const CharPos &endPos, const QStringList &changeText, SelectionMode SelMode, size_t changeNumber)
{
    PUndoItem  newItem = std::make_shared<UndoItem>(
                reason,
                SelMode,startPos,endPos,changeText,
                changeNumber);
    mItems.append(newItem);
}

void RedoList::addRedo(PUndoItem item)
{
    mItems.append(item);
}

void RedoList::addCaretAndSelectionInfo(size_t changeNumber, const PCaretAndSelectionInfo &beforeInfo, const PCaretAndSelectionInfo &afterInfo)
{
    mCaretInfoBeforeChange.insert(changeNumber, beforeInfo);
    mCaretInfoAfterChange.insert(changeNumber, afterInfo);
}

PCaretAndSelectionInfo RedoList::caretAndSelBeforeChange(size_t changeNumber)
{
    PCaretAndSelectionInfo info = mCaretInfoBeforeChange.value(changeNumber);
    Q_ASSERT(info!=nullptr);
    return info;
}

PCaretAndSelectionInfo RedoList::caretAndSelAfterChange(size_t changeNumber)
{
    PCaretAndSelectionInfo info = mCaretInfoAfterChange.value(changeNumber);
    Q_ASSERT(info!=nullptr);
    return info;
}

void RedoList::removeCaretAndSelInfo(size_t changeNumber)
{
    mCaretInfoBeforeChange.remove(changeNumber);
    mCaretInfoAfterChange.remove(changeNumber);
}

void RedoList::clear()
{
    mItems.clear();
    mCaretInfoAfterChange.clear();
    mCaretInfoBeforeChange.clear();
}

ChangeReason RedoList::lastChangeReason()
{
    if (mItems.count() == 0)
        return ChangeReason::Nothing;
    else
        return mItems.last()->changeReason();
}

bool RedoList::isEmpty()
{
    return mItems.isEmpty();
}

PUndoItem RedoList::peekItem()
{
    if (mItems.count() == 0)
        return PUndoItem();
    else
        return mItems.last();
}

PUndoItem RedoList::popItem()
{
    if (mItems.count() == 0)
        return PUndoItem();
    else {
        PUndoItem item = mItems.last();
        mItems.removeLast();
        return item;
    }
}

bool RedoList::canRedo()
{
    return mItems.count()>0;
}

int RedoList::itemCount()
{
    return mItems.count();
}

BinaryFileError::BinaryFileError(const QString& reason):
    FileError(reason)
{

}

int searchForSegmentIdx(const QList<int> &segList, int maxVal, int value)
{
    if (value<0)
        return 0;
    if (value>=maxVal)
        return segList.length();
    int start = 0;
    int end = segList.length()-1;
    while ( start<=end ) {
        int mid = (start+end) / 2;
        if (segList[mid]==value) {
            //skip zero length segs
            while(mid+1<segList.length() && segList[mid+1]==value)
                mid++;
            return mid;
        } else if (segList[mid]>value) {
            end = mid-1;
        } else if (mid+1>=segList.length() || segList[mid+1]>value ) {
            return mid;
        } else {
            start = mid+1;
        }
    }
    //Not found, should not happen
    Q_ASSERT(false);
    return -1;
}

int GlyphCalculator::updateGlyphStartPositionList(
        const QString &lineText,
        const QList<int> &glyphStartCharList, int startChar, int endChar,
        const QFontMetrics &fontMetrics,
        QList<int> &glyphStartPositionList, int left, int &right, int &startGlyph, int &endGlyph) const
{
    right = std::max(0,left);
    startGlyph = searchForSegmentIdx(glyphStartCharList,lineText.length(),startChar);
    endGlyph = searchForSegmentIdx(glyphStartCharList,lineText.length(),endChar);
    for (int i=startGlyph;i<endGlyph;i++) {
        int start = glyphStartCharList[i];
        int end;
        if (i+1<glyphStartCharList.length()) {
            end = glyphStartCharList[i+1];
        } else {
            end = lineText.length();
        }
        QString glyph = lineText.mid(start,end-start);
        int gWidth = glyphWidth(glyph, right, fontMetrics, mForceMonospace);
        glyphStartPositionList[i] = right;
        right += gWidth;
    }
    if (endGlyph<glyphStartPositionList.length())
        glyphStartPositionList[endGlyph] = right;
    return right-left;
}

int GlyphCalculator::glyphWidth(const QString &glyph, int left, const QFontMetrics &fontMetrics, bool forceMonospace) const
{
    int glyphWidth;
    if (glyph.length()==0)
        return 0;
    QChar ch = glyph[0];
    if (ch == '\t') {
        glyphWidth = tabWidth() - left % tabWidth();
    } else {
        glyphWidth = fontMetrics.horizontalAdvance(glyph);
        //qDebug()<<glyph<<glyphCols<<width<<mCharWidth;
    }
    if (forceMonospace) {
        int cols = std::ceil(glyphWidth / (double)mCharWidth);
        glyphWidth = cols * mCharWidth;
    }
    return glyphWidth;
}

void expandGlyphStartCharList(const QString &strAdded, int oldStrLen, QList<int> &glyphStartCharList)
{
    QList<int> addedList = calcGlyphStartCharList(strAdded);
    for (int i=0;i<addedList.length();i++) {
        glyphStartCharList.append(addedList[i]+oldStrLen);
    }
}

int calcSegmentInterval(const QList<int> &segList, int maxVal, int idx)
{
    if (idx<0 || idx>=segList.length())
        return 0;
    if (idx == segList.length()-1)
        return maxVal - segList[idx];
    return segList[idx+1]-segList[idx];
}

int segmentIntervalStart(const QList<int> &segList, int maxVal, int idx)
{
    if (idx<0)
        return 0;
    if (idx>=segList.length())
        return maxVal;
    return segList[idx];
}

GlyphCalculator::GlyphCalculator(const QFont &font):
    mFontMetrics{font},
    mTabSize{4},
    mForceMonospace{false}
{
    mCharWidth =  mFontMetrics.horizontalAdvance("M");
    mSpaceWidth = mFontMetrics.horizontalAdvance(" ");
}

void GlyphCalculator::setFont(const QFont &newFont)
{
    mFontMetrics = QFontMetrics(newFont);
    mCharWidth =  mFontMetrics.horizontalAdvance("M");
    mSpaceWidth = mFontMetrics.horizontalAdvance(" ");
}

CaretAndSelectionInfo::CaretAndSelectionInfo(const CharPos &caret, const CharPos &selBegin, const CharPos &selEnd, SelectionMode selMode)
{
    this->caret=caret;
    this->selBegin=selBegin;
    this->selEnd=selEnd;
    this->selMode = selMode;
}

}
