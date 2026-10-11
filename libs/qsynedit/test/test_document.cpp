#include <QTest>
#include <QCoreApplication>
#include "test_document.h"
#include "qsynedit/document.h"

namespace QSynedit {

TestDocument::TestDocument()
{
}

TestDocument::~TestDocument()
{

}

void TestDocument::init()
{
    mDoc=std::make_shared<Document>(QFont{"monospace"});
}

void TestDocument::cleanup()
{
    disconnect(this);
}

void TestDocument::initTestCase()
{
    {
        QFileInfo file("resources/test1.cpp");
        QVERIFY(file.exists());
    }
    {
        QFileInfo file("resources/emoji.txt");
        QVERIFY(file.exists());
    }
}

void TestDocument::cleanupTestCase()
{

}

void TestDocument::test_load_from_file()
{
    QByteArray encoding;

    mDoc->loadFromFile("resources/test1.cpp",ENCODING_AUTO_DETECT,encoding);
    QCOMPARE(mDoc->count(),6);
    QCOMPARE(mDoc->content(), QStringList({"#include <stdio.h>",
                                           "",
                                           "int main() {",
                                           "\tprintf(\"lala\\n\");",
                                           "\treturn 0;",
                                           "}"}));
    QCOMPARE(mDoc->charToGlyphIndex(0,0),0);
    QCOMPARE(mDoc->charToGlyphIndex(0,1),1);
    QCOMPARE(mDoc->charToGlyphIndex(0,2),2);
    QCOMPARE(mDoc->charToGlyphIndex(0,3),3);
    QCOMPARE(mDoc->charToGlyphIndex(0,4),4);
    QCOMPARE(mDoc->charToGlyphIndex(0,5),5);
    QCOMPARE(mDoc->charToGlyphIndex(0,6),6);
    QCOMPARE(mDoc->charToGlyphIndex(0,7),7);
    QCOMPARE(mDoc->charToGlyphIndex(0,8),8);
    QCOMPARE(mDoc->charToGlyphIndex(0,9),9);
    QCOMPARE(mDoc->charToGlyphIndex(0,10),10);
    QCOMPARE(mDoc->charToGlyphIndex(0,11),11);
    QCOMPARE(mDoc->charToGlyphIndex(0,12),12);
    QCOMPARE(mDoc->charToGlyphIndex(0,13),13);
    QCOMPARE(mDoc->charToGlyphIndex(0,14),14);
    QCOMPARE(mDoc->charToGlyphIndex(0,15),15);
    QCOMPARE(mDoc->charToGlyphIndex(0,16),16);
    QCOMPARE(mDoc->charToGlyphIndex(0,17),17);
    QCOMPARE(mDoc->charToGlyphIndex(0,18),18);
    QCOMPARE(mDoc->charToGlyphIndex(0,19),18);
    QCOMPARE(mDoc->charToGlyphIndex(1,0),0);
    QCOMPARE(mDoc->charToGlyphIndex(1,1),0);

}

void TestDocument::test_load_from_file2()
{
    QByteArray encoding;

    mDoc->loadFromFile("resources/queue1.cpp",ENCODING_AUTO_DETECT,encoding);

    QCOMPARE(mDoc->getLine(0), "#include <iostream>");
    QCOMPARE(mDoc->getGlyphStartCharListForTest(0),
             QList<int>({0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18}));
}

void TestDocument::test_load_from_empty_file()
{
    QByteArray encoding;

    mDoc->loadFromFile("resources/empty.txt",ENCODING_AUTO_DETECT,encoding);
    QCOMPARE(mDoc->count(),1);
    QCOMPARE(mDoc->content(), QStringList({""}));

    QVERIFY(mDoc->empty());
}

void TestDocument::test_emoji_glyphs()
{
    QByteArray encoding;
    mDoc->loadFromFile("resources/emoji.txt",ENCODING_AUTO_DETECT,encoding);
    QCOMPARE(encoding, ENCODING_UTF8);

    QCOMPARE(mDoc->glyphCount(0),11);
    QCOMPARE(mDoc->glyphCount(1),7);
    QCOMPARE(mDoc->glyphCount(2),7);

    QCOMPARE(mDoc->glyph(0,0),"🌴");
    QCOMPARE(mDoc->glyph(0,1),"🌳");
    QCOMPARE(mDoc->glyph(0,2),"🌵");
    QCOMPARE(mDoc->glyph(0,3),"🌶");

    QCOMPARE(mDoc->charToGlyphIndex(0,0),0);
    QCOMPARE(mDoc->charToGlyphIndex(0,1),0);
    QCOMPARE(mDoc->charToGlyphIndex(0,2),1);
    QCOMPARE(mDoc->charToGlyphIndex(0,3),1);
    QCOMPARE(mDoc->charToGlyphIndex(0,4),2);

    QCOMPARE(mDoc->glyphAt(0,0),"🌴");
    QCOMPARE(mDoc->glyphAt(0,1),"🌴");
    QCOMPARE(mDoc->glyphAt(0,2),"🌳");
    QCOMPARE(mDoc->glyphAt(0,3),"🌳");
    QCOMPARE(mDoc->glyphAt(0,4),"🌵");
    QCOMPARE(mDoc->glyphAt(0,5),"🌵");
    QCOMPARE(mDoc->glyphAt(0,6),"🌶");
    QCOMPARE(mDoc->glyphAt(0,7),"🌶");

}

void TestDocument::test_set_text()
{
    initSignalTest();
    mDoc->setText("int x;\nint y;\n");

    QCOMPARE(mDoc->count(),2);
    QCOMPARE(mDoc->getLine(0),"int x;");
    QCOMPARE(mDoc->getLine(1),"int y;");

    //signals
    QCOMPARE(mChangedCount,1);
}

void TestDocument::test_set_empty_text()
{
    initSignalTest();
    mDoc->setText("");

    QCOMPARE(mDoc->count(),1);
    QCOMPARE(mDoc->getLine(0),"");

    QVERIFY(mDoc->empty());

    //signals
    QCOMPARE(mChangedCount,1);
}

void TestDocument::test_set_contents()
{
    initSignalTest();
    mDoc->setContents({"int x1;","int y1;"});

    QCOMPARE(mDoc->count(),2);
    QCOMPARE(mDoc->getLine(0),"int x1;");
    QCOMPARE(mDoc->getLine(1),"int y1;");

    //signals
    QCOMPARE(mChangedCount,1);
}

void TestDocument::test_set_empty_contents()
{
    initSignalTest();
    mDoc->setContents({});

    QCOMPARE(mDoc->count(),1);
    QCOMPARE(mDoc->getLine(0),"");

    QVERIFY(mDoc->empty());

    //signals
    QCOMPARE(mChangedCount,1);
}

void TestDocument::test_add_line()
{
    mDoc->setContents({"int x1;","int y1;"});

    initSignalTest();
    mDoc->addLine("int z;");

    QCOMPARE(mDoc->count(),3);
    QCOMPARE(mDoc->content(),
             QStringList({"int x1;",
                          "int y1;",
                          "int z;"}));

    //signals
    QCOMPARE(mChangedCount,1);
}

void TestDocument::test_add_lines()
{
    mDoc->setContents({"int x1;","int y1;"});

    initSignalTest();
    mDoc->addLines({"int z;","int q;"});

    QCOMPARE(mDoc->count(),4);
    QCOMPARE(mDoc->content(),
             QStringList({"int x1;",
                          "int y1;",
                          "int z;",
                          "int q;"}));

    //signals
    QCOMPARE(mChangedCount,1);
}

void TestDocument::test_search_segment_index()
{
    QString line = "#include <iostream>";
    QList<int> glyphStartCharList({0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18});
    QCOMPARE(searchForSegmentIdx(glyphStartCharList,line.length(),0),0);

}

void TestDocument::test_insert_line()
{
    mDoc->setContents({"int x1;","int y1;"});

    initSignalTest();
    mDoc->insertLine(0,"int p;");

    QCOMPARE(mDoc->count(),3);
    QCOMPARE(mDoc->content(),
             QStringList({"int p;",
                          "int x1;",
                          "int y1;"}));

    //signals
    QCOMPARE(mChangedCount,1);
}

void TestDocument::test_insert_line1()
{
    mDoc->setContents({"int x1;","int y1;"});

    initSignalTest();
    mDoc->insertLine(1,"int p;");

    QCOMPARE(mDoc->count(),3);
    QCOMPARE(mDoc->content(),
             QStringList({"int x1;",
                          "int p;",
                          "int y1;"}));

    //signals
    QCOMPARE(mChangedCount,1);
}

void TestDocument::test_insert_line2()
{
    mDoc->setContents({"int x1;","int y1;"});

    initSignalTest();
    mDoc->insertLine(2,"int p;");

    QCOMPARE(mDoc->count(),3);
    QCOMPARE(mDoc->getLine(0),"int x1;");
    QCOMPARE(mDoc->getLine(1),"int y1;");
    QCOMPARE(mDoc->getLine(2),"int p;");

    //signals
    QCOMPARE(mChangedCount,1);
}

void TestDocument::test_insert_lines()
{
    mDoc->setContents({"int x1;","int y1;"});

    initSignalTest();
    mDoc->insertLines(0,2);

    QCOMPARE(mDoc->count(),4);
    QCOMPARE(mDoc->getLine(0),"");
    QCOMPARE(mDoc->getLine(1),"");
    QCOMPARE(mDoc->getLine(2),"int x1;");
    QCOMPARE(mDoc->getLine(3),"int y1;");

    //signals
    QCOMPARE(mChangedCount,1);
}

void TestDocument::test_insert_lines1()
{
    mDoc->setContents({"int x1;","int y1;"});

    initSignalTest();
    mDoc->insertLines(1,2);

    QCOMPARE(mDoc->count(),4);
    QCOMPARE(mDoc->getLine(0),"int x1;");
    QCOMPARE(mDoc->getLine(1),"");
    QCOMPARE(mDoc->getLine(2),"");
    QCOMPARE(mDoc->getLine(3),"int y1;");

    //signals
    QCOMPARE(mChangedCount,1);
}

void TestDocument::test_insert_lines2()
{
    mDoc->setContents({"int x1;","int y1;"});

    initSignalTest();
    mDoc->insertLines(2,2);

    QCOMPARE(mDoc->count(),4);
    QCOMPARE(mDoc->getLine(0),"int x1;");
    QCOMPARE(mDoc->getLine(1),"int y1;");
    QCOMPARE(mDoc->getLine(2),"");
    QCOMPARE(mDoc->getLine(3),"");

    //signals
    QCOMPARE(mChangedCount,1);
}

void TestDocument::test_delete_line()
{
    mDoc->setContents({"int x1;","int y1;","int z1;"});

    initSignalTest();
    mDoc->deleteLine(0);

    QCOMPARE(mDoc->count(),2);
    QCOMPARE(mDoc->getLine(0),"int y1;");
    QCOMPARE(mDoc->getLine(1),"int z1;");

    //signals
    QCOMPARE(mChangedCount,1);
}

void TestDocument::test_delete_line1()
{
    mDoc->setContents({"int x1;","int y1;","int z1;"});

    initSignalTest();
    mDoc->deleteLine(1);

    QCOMPARE(mDoc->count(),2);
    QCOMPARE(mDoc->getLine(0),"int x1;");
    QCOMPARE(mDoc->getLine(1),"int z1;");

    //signals
    QCOMPARE(mChangedCount,1);
}

void TestDocument::test_delete_line2()
{
    mDoc->setContents({"int x1;","int y1;","int z1;"});

    initSignalTest();
    mDoc->deleteLine(2);

    QCOMPARE(mDoc->count(),2);
    QCOMPARE(mDoc->getLine(0),"int x1;");
    QCOMPARE(mDoc->getLine(1),"int y1;");

    //signals
    QCOMPARE(mChangedCount,1);
}

void TestDocument::test_delete_lines()
{
    mDoc->setContents({"int x1;","int y1;","int z1;"});

    initSignalTest();
    mDoc->deleteLines(0,2);

    QCOMPARE(mDoc->count(),1);
    QCOMPARE(mDoc->getLine(0),"int z1;");

    //signals
    QCOMPARE(mChangedCount,1);
}

void TestDocument::test_delete_lines1()
{
    mDoc->setContents({"int x1;","int y1;","int z1;"});

    initSignalTest();
    mDoc->deleteLines(1,2);

    QCOMPARE(mDoc->count(),1);
    QCOMPARE(mDoc->getLine(0),"int x1;");

    //signals
    QCOMPARE(mChangedCount,1);
}

void TestDocument::test_delete_all()
{
    mDoc->setContents({"int x1;","int y1;","int z1;"});

    initSignalTest();
    mDoc->deleteLines(0,3);

    QCOMPARE(mDoc->count(),1);
    QCOMPARE(mDoc->getLine(0),"");

    QVERIFY(mDoc->empty());

    //signals
    QCOMPARE(mChangedCount,1);
}

void TestDocument::test_put_line()
{
    mDoc->setContents({"int x1;","int y1;","int z1;"});

    initSignalTest();
    mDoc->putLine(0,"test");

    QCOMPARE(mDoc->count(),3);
    QCOMPARE(mDoc->getLine(0),"test");
    QCOMPARE(mDoc->getLine(1),"int y1;");
    QCOMPARE(mDoc->getLine(2),"int z1;");

    //signals
    QCOMPARE(mChangedCount,1);
}

void TestDocument::test_put_line1()
{
    mDoc->setContents({"int x1;","int y1;","int z1;"});

    initSignalTest();
    mDoc->putLine(2,"test");

    QCOMPARE(mDoc->count(),3);
    QCOMPARE(mDoc->getLine(0),"int x1;");
    QCOMPARE(mDoc->getLine(1),"int y1;");
    QCOMPARE(mDoc->getLine(2),"test");
    //signals
    QCOMPARE(mChangedCount,1);
}

void TestDocument::test_move_line_to1()
{
    mDoc->setContents({"int a;","int b;","int c;","int d;","int e;"});

    initSignalTest();
    mDoc->moveLine(2,4);

    QCOMPARE(mDoc->count(),5);
    QCOMPARE(mDoc->content(),
             QStringList({"int a;",
                          "int b;",
                          "int d;",
                          "int e;",
                          "int c;"}));
    //signals
    QCOMPARE(mChangedCount,1);
}

void TestDocument::test_move_line_to2()
{
    mDoc->setContents({"int a;","int b;","int c;","int d;","int e;"});

    initSignalTest();
    mDoc->moveLine(4,2);

    QCOMPARE(mDoc->count(),5);
    QCOMPARE(mDoc->content(),
             QStringList({"int a;",
                          "int b;",
                          "int e;",
                          "int c;",
                          "int d;",
                          }));
    //signals
    QCOMPARE(mChangedCount,1);
}

void TestDocument::test_clear()
{
    QByteArray encoding;

    mDoc->loadFromFile("resources/test1.cpp",ENCODING_AUTO_DETECT,encoding);

    initSignalTest();
    mDoc->clear();

    QCOMPARE(1, mDoc->count());

    QVERIFY(mDoc->empty());

    QCOMPARE(mChangedCount, 1);
}

void TestDocument::test_find_last_line_by_seq()
{
    mDoc=std::make_shared<Document>(QFont{});
    QByteArray encoding;

    mDoc->loadFromFile("resources/test1.cpp",ENCODING_AUTO_DETECT,encoding);
    QCOMPARE(mDoc->count(),6);
    size_t seq = mDoc->getLineSeq(1);
    PDocumentLine line = mDoc->findLineBySeq(seq);
    QVERIFY(line!=nullptr);
    QCOMPARE(line->lineText(), mDoc->getLine(1));
    QCOMPARE(line->lineSeq(), seq);
}

void TestDocument::test_crash_on_debian_amd_64()
{
    mDoc=std::make_shared<Document>(QFont{});
    QByteArray encoding;

    mDoc->loadFromFile("resources/queue1.cpp",ENCODING_AUTO_DETECT,encoding);

    QCOMPARE(mDoc->getLine(0), "#include <iostream>");
    QCOMPARE(mDoc->charToGlyphStartPosition(0, mDoc->getLine(0), 0),0);
}

void TestDocument::initSignalTest()
{
    mChangedCount=0;
    connect(mDoc.get(), &Document::changed, this, &TestDocument::onChanged);
}

void TestDocument::onChanged()
{
    mChangedCount++;
}

void TestDocumentHelpers::test_calcGlyphStartCharList()
{
    QString s{"int 测试();"};
    QList<int> startChars = calcGlyphStartCharList(s);
    QList<int> expects{0,1,2,3,4,5,6,7,8};
    QCOMPARE(startChars, expects);
}

void TestDocumentHelpers::test_calcSegmentInterval()
{
    QList<int> segments{0,3,11,20};
    int maxVal=25;
    QList<int> expects{3,8,9,5};
    for(int i=0;i<segments.length();i++) {
        QCOMPARE(calcSegmentInterval(segments,maxVal,i), expects[i]);
    }
}

void TestDocumentHelpers::test_segmentIntervalStart()
{
    QList<int> segments{0,3,11,20};
    int maxVal=25;
    QList<int> expects{0,3,11,20,25};
    for(int i=0;i<expects.length();i++) {
        QCOMPARE(segmentIntervalStart(segments,maxVal,i), expects[i]);
    }
}

void TestDocumentHelpers::test_searchForSegmentIdx()
{
    QList<int> segments{0,3,11};
    int maxVal=15;
    QCOMPARE(searchForSegmentIdx(segments,maxVal,0), 0);
    QCOMPARE(searchForSegmentIdx(segments,maxVal,1), 0);
    QCOMPARE(searchForSegmentIdx(segments,maxVal,2), 0);
    QCOMPARE(searchForSegmentIdx(segments,maxVal,3), 1);
    QCOMPARE(searchForSegmentIdx(segments,maxVal,4), 1);
    QCOMPARE(searchForSegmentIdx(segments,maxVal,5), 1);
    QCOMPARE(searchForSegmentIdx(segments,maxVal,6), 1);
    QCOMPARE(searchForSegmentIdx(segments,maxVal,7), 1);
    QCOMPARE(searchForSegmentIdx(segments,maxVal,8), 1);
    QCOMPARE(searchForSegmentIdx(segments,maxVal,9), 1);
    QCOMPARE(searchForSegmentIdx(segments,maxVal,10), 1);
    QCOMPARE(searchForSegmentIdx(segments,maxVal,11), 2);
    QCOMPARE(searchForSegmentIdx(segments,maxVal,12), 2);
    QCOMPARE(searchForSegmentIdx(segments,maxVal,13), 2);
    QCOMPARE(searchForSegmentIdx(segments,maxVal,14), 2);
    QCOMPARE(searchForSegmentIdx(segments,maxVal,15), 3);
    QCOMPARE(searchForSegmentIdx(segments,maxVal,16), 3);
}

}

#include <QSaveFile>
#include <QSignalSpy>
#include <QTemporaryDir>

namespace QSynedit {
namespace {

bool writeFixture(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray readFixture(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

QByteArray encodeFixture(const QByteArray &codec, const QString &text)
{
    TextEncoder encoder(codec);
    const auto encoded = encoder.encode(text);
    if (!encoded.first)
        qFatal("Cannot generate document test fixture for %s", codec.constData());
    return encoded.second;
}

// Failure injection writes a real temporary file before failing. It tests
// short writes after a BOM or one or more complete lines, not just open errors.
class ShortWritingSaveFile final : public QSaveFile
{
public:
    ShortWritingSaveFile(const QString &path, qint64 limit)
        : QSaveFile(path), mRemaining(limit) {}

protected:
    qint64 writeData(const char *data, qint64 size) override
    {
        if (mRemaining <= 0)
            return -1;
        const qint64 accepted = qMin(size, mRemaining);
        const qint64 written = QSaveFile::writeData(data, accepted);
        if (written > 0)
            mRemaining -= written;
        return written;
    }

private:
    qint64 mRemaining;
};

} // namespace

void TestDocument::test_load_encoding_newline_matrix_data()
{
    QTest::addColumn<QByteArray>("bytes");
    QTest::addColumn<QByteArray>("requestedEncoding");
    QTest::addColumn<QByteArray>("detectedEncoding");
    QTest::addColumn<QStringList>("lines");
    QTest::addColumn<int>("newlineType");
    QTest::addColumn<bool>("finalNewline");
    const QByteArray autoDetect = ENCODING_AUTO_DETECT;
    const QStringList ascii{"int main() {", "\treturn 0;", "}"};
    const QStringList unicode{QString::fromUtf8("// città € 🌴 e\u0301"), "int n = 42;"};
    const QByteArray utf8Bom = QByteArray::fromHex("efbbbf");
    const QByteArray utf16LeBom = QByteArray::fromHex("fffe");
    const QByteArray utf16BeBom = QByteArray::fromHex("feff");
    const QByteArray utf32LeBom = QByteArray::fromHex("fffe0000");
    const QByteArray utf32BeBom = QByteArray::fromHex("0000feff");
    QTest::newRow("empty-auto") << QByteArray() << autoDetect << QByteArray(ENCODING_ASCII)
        << QStringList{""} << int(NewlineType::Windows) << false;
    QTest::newRow("ascii-crlf-final") << ascii.join("\r\n").toUtf8() + "\r\n"
        << autoDetect << QByteArray(ENCODING_ASCII) << ascii << int(NewlineType::Windows) << true;
    QTest::newRow("ascii-lf-no-final") << ascii.join("\n").toUtf8()
        << autoDetect << QByteArray(ENCODING_ASCII) << ascii << int(NewlineType::Unix) << false;
    QTest::newRow("ascii-cr-final") << ascii.join("\r").toUtf8() + "\r"
        << autoDetect << QByteArray(ENCODING_ASCII) << ascii << int(NewlineType::MacOld) << true;
    QTest::newRow("ascii-cr-no-final") << ascii.join("\r").toUtf8()
        << autoDetect << QByteArray(ENCODING_ASCII) << ascii << int(NewlineType::MacOld) << false;
    QTest::newRow("ascii-one-line") << QByteArray("int n;")
        << autoDetect << QByteArray(ENCODING_ASCII) << QStringList{"int n;"}
        << int(NewlineType::Windows) << false;
    QTest::newRow("utf8-unicode-auto") << unicode.join("\n").toUtf8() + "\n"
        << autoDetect << QByteArray(ENCODING_UTF8) << unicode << int(NewlineType::Unix) << true;
    QTest::newRow("utf8-unicode-explicit-crlf") << unicode.join("\r\n").toUtf8()
        << QByteArray(ENCODING_UTF8) << QByteArray(ENCODING_UTF8)
        << unicode << int(NewlineType::Windows) << false;
    QTest::newRow("utf8-bom-ascii") << utf8Bom + ascii.join("\n").toUtf8() + "\n"
        << autoDetect << QByteArray(ENCODING_UTF8_BOM) << ascii << int(NewlineType::Unix) << true;
    QTest::newRow("utf8-bom-empty") << utf8Bom
        << autoDetect << QByteArray(ENCODING_UTF8_BOM) << QStringList{""}
        << int(NewlineType::Windows) << false;
    QTest::newRow("utf8-bom-unicode-explicit") << utf8Bom + unicode.join("\r").toUtf8() + "\r"
        << QByteArray(ENCODING_UTF8_BOM) << QByteArray(ENCODING_UTF8_BOM)
        << unicode << int(NewlineType::MacOld) << true;
    const QStringList embeddedBom{"int a;", QString(QChar(0xfeff)) + "int b;"};
    QTest::newRow("utf8-bom-only-at-file-start") << utf8Bom + embeddedBom.join("\n").toUtf8()
        << autoDetect << QByteArray(ENCODING_UTF8_BOM)
        << embeddedBom << int(NewlineType::Unix) << false;
    QTest::newRow("utf8-embedded-bom-without-signature") << embeddedBom.join("\n").toUtf8()
        << autoDetect << QByteArray(ENCODING_UTF8)
        << embeddedBom << int(NewlineType::Unix) << false;
    for (const QByteArray &codec : {QByteArray("UTF-16LE"), QByteArray("UTF-16BE"),
                                  QByteArray("UTF-32LE"), QByteArray("UTF-32BE")}) {
        const bool utf16 = codec.startsWith("UTF-16");
        const bool little = codec.endsWith("LE");
        const QByteArray bom = utf16 ? (little ? utf16LeBom : utf16BeBom)
                                    : (little ? utf32LeBom : utf32BeBom);
        const QByteArray marked = utf16 ? ENCODING_UTF16_BOM : ENCODING_UTF32_BOM;
        for (const bool final : {false, true}) {
            const QByteArray name = codec + (final ? "-final" : "-no-final");
            const QString text = unicode.join("\n") + (final ? "\n" : "");
            QTest::newRow(name.constData()) << bom + encodeFixture(codec, text)
                << autoDetect << marked << unicode << int(NewlineType::Unix) << final;
        }
        const QByteArray explicitName = codec + "-explicit-no-bom";
        QTest::newRow(explicitName.constData()) << encodeFixture(codec, unicode.join("\r\n"))
            << codec << codec << unicode << int(NewlineType::Windows) << false;
    }
    QTest::newRow("utf16-bom-explicit")
        << utf16LeBom + encodeFixture("UTF-16LE", unicode.join("\n"))
        << QByteArray(ENCODING_UTF16_BOM) << QByteArray(ENCODING_UTF16_BOM)
        << unicode << int(NewlineType::Unix) << false;
    QTest::newRow("utf32-bom-explicit")
        << utf32LeBom + encodeFixture("UTF-32LE", unicode.join("\n"))
        << QByteArray(ENCODING_UTF32_BOM) << QByteArray(ENCODING_UTF32_BOM)
        << unicode << int(NewlineType::Unix) << false;
    const QStringList literalBomLines{QString(QChar(0xfeff)) + "int a;", QString(QChar(0xfeff)) + "int b;"};
    for (const QByteArray &codec : {QByteArray("UTF-16LE"), QByteArray("UTF-16BE"), QByteArray("UTF-32LE"), QByteArray("UTF-32BE")}) {
        const QByteArray signature = codec == "UTF-16LE" ? utf16LeBom : codec == "UTF-16BE" ? utf16BeBom
            : codec == "UTF-32LE" ? utf32LeBom : utf32BeBom;
        const QByteArray name = codec + "-literal-feff-in-body";
        QTest::newRow(name.constData()) << signature + encodeFixture(codec, literalBomLines.join("\n"))
            << autoDetect << QByteArray(codec.startsWith("UTF-16") ? ENCODING_UTF16_BOM : ENCODING_UTF32_BOM)
            << literalBomLines << int(NewlineType::Unix) << false;
    }
    const QString unicodeBoundaries = QString::fromUtf8(QByteArray::fromHex("ed9fbfee8080f48fbfbf"));
    QTest::newRow("utf32-be-valid-scalar-boundaries")
        << QByteArray::fromHex("0000feff0000d7ff0000e0000010ffff")
        << autoDetect << QByteArray(ENCODING_UTF32_BOM)
        << QStringList{unicodeBoundaries} << int(NewlineType::Windows) << false;
    QTest::newRow("utf32-le-valid-scalar-boundaries")
        << QByteArray::fromHex("fffe0000ffd7000000e00000ffff1000")
        << autoDetect << QByteArray(ENCODING_UTF32_BOM)
        << QStringList{unicodeBoundaries} << int(NewlineType::Windows) << false;
    QTest::newRow("latin1-explicit") << QByteArray::fromHex("63697474e00a696e74206e3b")
        << QByteArray("ISO-8859-1") << QByteArray("ISO-8859-1")
        << QStringList{QString::fromUtf8("città"), "int n;"} << int(NewlineType::Unix) << false;
    QTest::newRow("leading-and-trailing-empty-lines") << QByteArray("\nint n;\n\n")
        << autoDetect << QByteArray(ENCODING_ASCII) << QStringList{"", "int n;", ""}
        << int(NewlineType::Unix) << true;
    QTest::newRow("mixed-newlines-first-crlf") << QByteArray("a\r\nb\rc\n")
        << autoDetect << QByteArray(ENCODING_ASCII) << QStringList{"a", "b", "c"}
        << int(NewlineType::Windows) << true;
}

void TestDocument::test_load_encoding_newline_matrix()
{
    QFETCH(QByteArray, bytes);
    QFETCH(QByteArray, requestedEncoding);
    QFETCH(QByteArray, detectedEncoding);
    QFETCH(QStringList, lines);
    QFETCH(int, newlineType);
    QFETCH(bool, finalNewline);
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString path = temporary.filePath(QString::fromUtf8("sorgente città 🌴.cpp"));
    QVERIFY(writeFixture(path, bytes));
    mDoc->setContents({"unsaved original"});
    mDoc->setTabSize(8);
    QSignalSpy changing(mDoc.get(), &Document::changing);
    QSignalSpy changed(mDoc.get(), &Document::changed);
    QByteArray realEncoding = "unchanged";
    mDoc->loadFromFile(path, requestedEncoding, realEncoding);
    QCOMPARE(realEncoding, detectedEncoding);
    QCOMPARE(mDoc->content(), lines);
    QCOMPARE(int(mDoc->getNewlineType()), newlineType);
    QCOMPARE(mDoc->getAppendNewLineAtEOF(), finalNewline);
    QCOMPARE(mDoc->tabSize(), 8);
    QCOMPARE(changing.count(), 1);
    QCOMPARE(changed.count(), 1);
    for (int i = 0; i < mDoc->count(); ++i) {
        QVERIFY(mDoc->findLineBySeq(mDoc->getLineSeq(i)));
        QVERIFY(mDoc->lineWidth(i) >= 0);
    }
}

void TestDocument::test_load_failure_preserves_document_data()
{
    QTest::addColumn<QByteArray>("bytes");
    QTest::addColumn<QByteArray>("encoding");
    QTest::newRow("utf8-truncated-explicit") << QByteArray::fromHex("696e74206e3b0ac3") << QByteArray(ENCODING_UTF8);
    QTest::newRow("utf8-invalid-continuation") << QByteArray::fromHex("61f0288c2862") << QByteArray(ENCODING_UTF8);
    QTest::newRow("utf8-truncated-bom") << QByteArray::fromHex("efbbbf696e74206e3b0ac3") << QByteArray(ENCODING_AUTO_DETECT);
    QTest::newRow("utf16-truncated-byte") << QByteArray::fromHex("fffe610062") << QByteArray(ENCODING_AUTO_DETECT);
    QTest::newRow("utf16-isolated-high-surrogate") << QByteArray::fromHex("fffe610000d8") << QByteArray(ENCODING_AUTO_DETECT);
    QTest::newRow("utf16-isolated-low-surrogate") << QByteArray::fromHex("feff0061dc00") << QByteArray(ENCODING_AUTO_DETECT);
    QTest::newRow("utf32-truncated-scalar") << QByteArray::fromHex("fffe0000610000006200") << QByteArray(ENCODING_AUTO_DETECT);
    QTest::newRow("utf32-out-of-range-scalar") << QByteArray::fromHex("0000feff0000006100110000") << QByteArray(ENCODING_AUTO_DETECT);
    QTest::newRow("utf32-le-out-of-range-scalar") << QByteArray::fromHex("fffe00006100000000001100") << QByteArray(ENCODING_AUTO_DETECT);
    QTest::newRow("utf32-be-surrogate-scalar") << QByteArray::fromHex("0000feff000000610000d800") << QByteArray(ENCODING_AUTO_DETECT);
    QTest::newRow("utf32-le-surrogate-scalar") << QByteArray::fromHex("fffe00006100000000dc0000") << QByteArray(ENCODING_AUTO_DETECT);
    QTest::newRow("utf32-le-out-of-range-explicit-no-bom") << QByteArray::fromHex("6100000000001100") << QByteArray("UTF-32LE");
    QTest::newRow("utf32-be-surrogate-explicit-no-bom") << QByteArray::fromHex("000000610000dc00") << QByteArray("UTF-32BE");
    QTest::newRow("binary-nul-auto") << QByteArray("int a;\n\0secret", 14) << QByteArray(ENCODING_AUTO_DETECT);
    QTest::newRow("binary-nul-explicit") << QByteArray("int a;\n\0secret", 14) << QByteArray(ENCODING_UTF8);
    QTest::newRow("unknown-codec") << QByteArray("int n;\n") << QByteArray("UNKNOWN-CODEC-DOCUMENT-TEST");
}

void TestDocument::test_load_failure_preserves_document()
{
    QFETCH(QByteArray, bytes);
    QFETCH(QByteArray, encoding);
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString path = temporary.filePath("broken.cpp");
    QVERIFY(writeFixture(path, bytes));
    const QStringList original{QString::fromUtf8("// unsaved città 🌴"), "int work = 42;"};
    mDoc->setContents(original);
    mDoc->setNewlineType(NewlineType::MacOld);
    mDoc->setAppendNewLineAtEOF(false);
    const size_t sequence = mDoc->getLineSeq(0);
    const int width = mDoc->lineWidth(0);
    QSignalSpy changed(mDoc.get(), &Document::changed);
    QSignalSpy changing(mDoc.get(), &Document::changing);
    QByteArray realEncoding = "unchanged";
    QVERIFY_EXCEPTION_THROWN(mDoc->loadFromFile(path, encoding, realEncoding), FileError);
    QCOMPARE(mDoc->content(), original);
    QCOMPARE(mDoc->getNewlineType(), NewlineType::MacOld);
    QCOMPARE(mDoc->getAppendNewLineAtEOF(), false);
    QCOMPARE(mDoc->getLineSeq(0), sequence);
    QCOMPARE(mDoc->lineWidth(0), width);
    QCOMPARE(realEncoding, QByteArray("unchanged"));
    QCOMPARE(changed.count(), 0);
    QCOMPARE(changing.count(), 0);
    // A failed open must not leave an update lock stuck or prevent a retry.
    QVERIFY(writeFixture(path, QByteArray("recovered\n")));
    mDoc->loadFromFile(path, ENCODING_UTF8, realEncoding);
    QCOMPARE(mDoc->content(), QStringList{"recovered"});
    QCOMPARE(changed.count(), 1);
    QCOMPARE(changing.count(), 1);
}

void TestDocument::test_load_missing_file_preserves_document()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    mDoc->setContents({"unsaved"});
    const size_t sequence = mDoc->getLineSeq(0);
    QByteArray encoding = "unchanged";
    QSignalSpy changed(mDoc.get(), &Document::changed);
    QVERIFY_EXCEPTION_THROWN(mDoc->loadFromFile(temporary.filePath("absent.cpp"), ENCODING_AUTO_DETECT, encoding), FileError);
    QCOMPARE(mDoc->content(), QStringList{"unsaved"});
    QCOMPARE(mDoc->getLineSeq(0), sequence);
    QCOMPARE(encoding, QByteArray("unchanged"));
    QCOMPARE(changed.count(), 0);
}

void TestDocument::test_save_encoding_newline_matrix_data()
{
    QTest::addColumn<QByteArray>("encoding");
    QTest::addColumn<QStringList>("lines");
    QTest::addColumn<int>("newlineType");
    QTest::addColumn<bool>("appendNewline");
    QTest::addColumn<QByteArray>("expectedBytes");
    QTest::addColumn<QByteArray>("detectedEncoding");
    const QStringList unicode{QString::fromUtf8("// città € 🌴 e\u0301"), "int n = 42;"};
    for (const QByteArray &encoding : {QByteArray(ENCODING_UTF8), QByteArray(ENCODING_UTF8_BOM),
            QByteArray(ENCODING_UTF16), QByteArray(ENCODING_UTF16_BOM),
            QByteArray(ENCODING_UTF32), QByteArray(ENCODING_UTF32_BOM), QByteArray("UTF-16BE"), QByteArray("UTF-32BE")}) {
        QByteArray codec = encoding;
        QByteArray bom;
        if (encoding == ENCODING_UTF8_BOM) {
            codec = ENCODING_UTF8;
            bom = QByteArray::fromHex("efbbbf");
        } else if (encoding == ENCODING_UTF16 || encoding == ENCODING_UTF16_BOM) {
            codec = "UTF-16LE";
            if (encoding == ENCODING_UTF16_BOM)
                bom = QByteArray::fromHex("fffe");
        } else if (encoding == ENCODING_UTF32 || encoding == ENCODING_UTF32_BOM) {
            codec = "UTF-32LE";
            if (encoding == ENCODING_UTF32_BOM)
                bom = QByteArray::fromHex("fffe0000");
        }
        for (int newline = 0; newline < 3; ++newline) {
            const QString separator = newline == int(NewlineType::Windows) ? "\r\n"
                : newline == int(NewlineType::Unix) ? "\n" : "\r";
            for (bool append : {false, true}) {
                const QString text = unicode.join(separator) + (append ? separator : QString());
                const QByteArray name = encoding + "-newline-" + QByteArray::number(newline)
                    + (append ? "-final" : "-no-final");
                QTest::newRow(name.constData()) << encoding << unicode << newline << append
                    << bom + encodeFixture(codec, text) << encoding;
            }
        }
    }
    QTest::newRow("ascii-utf8") << QByteArray(ENCODING_UTF8) << QStringList{"int n;"}
        << int(NewlineType::Unix) << true << QByteArray("int n;\n") << QByteArray(ENCODING_ASCII);
    QTest::newRow("ascii-utf8-bom-retained") << QByteArray(ENCODING_UTF8_BOM) << QStringList{"int n;"}
        << int(NewlineType::Unix) << false << QByteArray::fromHex("efbbbf") + "int n;" << QByteArray(ENCODING_UTF8_BOM);
    QTest::newRow("empty-utf8") << QByteArray(ENCODING_UTF8) << QStringList{""}
        << int(NewlineType::Unix) << false << QByteArray() << QByteArray(ENCODING_ASCII);
    QTest::newRow("empty-utf8-bom") << QByteArray(ENCODING_UTF8_BOM) << QStringList{""}
        << int(NewlineType::Unix) << false << QByteArray::fromHex("efbbbf") << QByteArray(ENCODING_UTF8_BOM);
    QTest::newRow("latin1-non-ascii") << QByteArray("ISO-8859-1") << QStringList{QString::fromUtf8("città")}
        << int(NewlineType::Unix) << true << QByteArray::fromHex("63697474e00a") << QByteArray("ISO-8859-1");
}

void TestDocument::test_save_encoding_newline_matrix()
{
    QFETCH(QByteArray, encoding);
    QFETCH(QStringList, lines);
    QFETCH(int, newlineType);
    QFETCH(bool, appendNewline);
    QFETCH(QByteArray, expectedBytes);
    QFETCH(QByteArray, detectedEncoding);
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString path = temporary.filePath(QString::fromUtf8("save città 🌴.cpp"));
    const QByteArray original = "ORIGINAL FILE\r\n";
    QVERIFY(writeFixture(path, original));
    mDoc->setContents(lines);
    mDoc->setNewlineType(static_cast<NewlineType>(newlineType));
    mDoc->setAppendNewLineAtEOF(appendNewline);
    QSignalSpy changed(mDoc.get(), &Document::changed);
    QByteArray realEncoding = "unchanged";
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    QVERIFY(file.open(QIODevice::WriteOnly));
    mDoc->saveToFile(file, encoding, realEncoding);
    QCOMPARE(readFixture(path), original);
    QVERIFY(file.commit());
    QCOMPARE(readFixture(path), expectedBytes);
    QCOMPARE(realEncoding, detectedEncoding);
    QCOMPARE(mDoc->content(), lines);
    QCOMPARE(changed.count(), 0);
    Document reloaded(QFont{});
    QByteArray reloadedEncoding;
    reloaded.loadFromFile(path, encoding, reloadedEncoding);
    QCOMPARE(reloaded.content(), lines);
    QCOMPARE(reloaded.getAppendNewLineAtEOF(), appendNewline);
}

void TestDocument::test_save_failure_preserves_file_data()
{
    QTest::addColumn<QByteArray>("encoding");
    QTest::addColumn<qint64>("byteLimit");
    QTest::newRow("utf8-fails-before-first-byte") << QByteArray(ENCODING_UTF8) << qint64(0);
    QTest::newRow("utf8-short-first-line") << QByteArray(ENCODING_UTF8) << qint64(4);
    QTest::newRow("utf8-fails-after-first-line") << QByteArray(ENCODING_UTF8) << qint64(12);
    QTest::newRow("utf8-bom-short-signature") << QByteArray(ENCODING_UTF8_BOM) << qint64(1);
    QTest::newRow("utf16-bom-short-data") << QByteArray(ENCODING_UTF16_BOM) << qint64(6);
    QTest::newRow("utf32-bom-short-data") << QByteArray(ENCODING_UTF32_BOM) << qint64(10);
    QTest::newRow("utf32-short-data") << QByteArray(ENCODING_UTF32) << qint64(6);
}

void TestDocument::test_save_failure_preserves_file()
{
    QFETCH(QByteArray, encoding);
    QFETCH(qint64, byteLimit);
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString path = temporary.filePath("keep.cpp");
    const QByteArray original = "original user work must survive\r\n";
    QVERIFY(writeFixture(path, original));
    const QStringList lines{"first line;", QString::fromUtf8("// second città 🌴"), "last line;"};
    mDoc->setContents(lines);
    mDoc->setNewlineType(NewlineType::Unix);
    mDoc->setAppendNewLineAtEOF(true);
    QByteArray realEncoding = "unchanged";
    QSignalSpy changed(mDoc.get(), &Document::changed);
    {
        ShortWritingSaveFile file(path, byteLimit);
        file.setDirectWriteFallback(false);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Unbuffered));
        QVERIFY_EXCEPTION_THROWN(mDoc->saveToFile(file, encoding, realEncoding), FileError);
        file.cancelWriting();
        QVERIFY(!file.commit());
    }
    QCOMPARE(readFixture(path), original);
    QCOMPARE(realEncoding, QByteArray("unchanged"));
    QCOMPARE(mDoc->content(), lines);
    QCOMPARE(changed.count(), 0);
    QSaveFile retry(path);
    retry.setDirectWriteFallback(false);
    mDoc->saveToFile(retry, ENCODING_UTF8, realEncoding);
    QVERIFY(retry.commit());
    QCOMPARE(readFixture(path), lines.join("\n").toUtf8() + "\n");
}

void TestDocument::test_save_lossy_encoding_preserves_file_data()
{
    QTest::addColumn<QByteArray>("encoding");
    QTest::addColumn<QString>("text");
    QTest::newRow("latin1-euro") << QByteArray("ISO-8859-1") << QString::fromUtf8("price = €");
    QTest::newRow("latin1-emoji") << QByteArray("ISO-8859-1") << QString::fromUtf8("panda = 🐼");
    QTest::newRow("ascii-accent") << QByteArray(ENCODING_ASCII) << QString::fromUtf8("città");
    QTest::newRow("unknown-codec") << QByteArray("UNKNOWN-CODEC-DOCUMENT-TEST") << QString("int n;");
    QTest::newRow("auto-is-not-save-codec") << QByteArray(ENCODING_AUTO_DETECT) << QString("int n;");
    QTest::newRow("utf8-isolated-surrogate") << QByteArray(ENCODING_UTF8) << QString(QChar(0xd800));
    QTest::newRow("utf16-isolated-surrogate") << QByteArray(ENCODING_UTF16_BOM) << QString(QChar(0xdc00));
}

void TestDocument::test_save_lossy_encoding_preserves_file()
{
    QFETCH(QByteArray, encoding);
    QFETCH(QString, text);
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString path = temporary.filePath("keep.cpp");
    const QByteArray original = "preserve this file";
    QVERIFY(writeFixture(path, original));
    mDoc->setContents({"valid first line", text});
    QByteArray realEncoding = "unchanged";
    {
        QSaveFile file(path);
        file.setDirectWriteFallback(false);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY_EXCEPTION_THROWN(mDoc->saveToFile(file, encoding, realEncoding), FileError);
    }
    QCOMPARE(readFixture(path), original);
    QCOMPARE(realEncoding, QByteArray("unchanged"));
    QCOMPARE(mDoc->content(), QStringList({"valid first line", text}));
}

void TestDocument::test_save_unwritable_device()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString path = temporary.filePath("readonly.cpp");
    const QByteArray original = "keep";
    QVERIFY(writeFixture(path, original));
    mDoc->setContents({"changed"});
    QByteArray encoding = "unchanged";
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QVERIFY_EXCEPTION_THROWN(mDoc->saveToFile(file, ENCODING_UTF8, encoding), FileError);
    QCOMPARE(readFixture(path), original);
    QCOMPARE(encoding, QByteArray("unchanged"));
    QSaveFile impossible(temporary.filePath("missing/child.cpp"));
    impossible.setDirectWriteFallback(false);
    QVERIFY_EXCEPTION_THROWN(mDoc->saveToFile(impossible, ENCODING_UTF8, encoding), FileError);
    QCOMPARE(encoding, QByteArray("unchanged"));
    QCOMPARE(mDoc->content(), QStringList{"changed"});
}

void TestDocument::test_set_text_final_cr()
{
    mDoc->setText("first\rsecond\r");
    QCOMPARE(mDoc->content(), QStringList({"first", "second"}));
    mDoc->setText("\r");
    QCOMPARE(mDoc->content(), QStringList{""});
    mDoc->setText("a\r\nb\rc\n\r");
    QCOMPARE(mDoc->content(), QStringList({"a", "b", "c", ""}));
}

void TestDocument::test_loaded_file_round_trip_bytes()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QList<QByteArray> fixtures{
        QByteArray(), QByteArray("one"), QByteArray("one\n"), QByteArray("\n"),
        QByteArray("one\r\ntwo\r\n\r\n"), QByteArray("one\rtwo\r"),
        QByteArray::fromHex("efbbbf") + "one\n", QString::fromUtf8("città 🌴\nline 2").toUtf8(),
        QByteArray::fromHex("fffe") + encodeFixture("UTF-16LE", QString::fromUtf8("città 🌴\r\nline 2")),
        QByteArray::fromHex("fffe0000") + encodeFixture("UTF-32LE", QString::fromUtf8("città 🌴\nline 2\n"))
    };
    const QString path = temporary.filePath("round-trip.cpp");
    for (const QByteArray &original : fixtures) {
        QVERIFY(writeFixture(path, original));
        QByteArray encoding;
        mDoc->loadFromFile(path, ENCODING_AUTO_DETECT, encoding);
        QSaveFile file(path);
        file.setDirectWriteFallback(false);
        QByteArray savedEncoding;
        mDoc->saveToFile(file, encoding, savedEncoding);
        QVERIFY(file.commit());
        QCOMPARE(readFixture(path), original);
        QCOMPARE(savedEncoding, encoding);
    }
}

} // namespace QSynedit

namespace QSynedit {

void TestDocument::test_codec_encoder_stateless_data()
{
    QTest::addColumn<QByteArray>("codec");
    QTest::addColumn<QString>("text");
    QTest::addColumn<QByteArray>("bytes");
    QTest::addColumn<bool>("valid");
    const QString palm = QString::fromUtf8("🌴");
    QTest::newRow("utf8-ascii-no-implicit-bom") << QByteArray(ENCODING_UTF8) << QString("int a;\n") << QByteArray("int a;\n") << true;
    QTest::newRow("utf8-unicode-no-implicit-bom") << QByteArray(ENCODING_UTF8) << palm << QByteArray::fromHex("f09f8cb4") << true;
    QTest::newRow("utf16-le-scalar-no-bom") << QByteArray("UTF-16LE") << palm << QByteArray::fromHex("3cd834df") << true;
    QTest::newRow("utf16-be-scalar-no-bom") << QByteArray("UTF-16BE") << palm << QByteArray::fromHex("d83cdf34") << true;
    QTest::newRow("utf32-le-scalar-no-tail") << QByteArray("UTF-32LE") << palm << QByteArray::fromHex("34f30100") << true;
    QTest::newRow("utf32-be-scalar-no-tail") << QByteArray("UTF-32BE") << palm << QByteArray::fromHex("0001f334") << true;
    QTest::newRow("utf32-le-multiple-surrogate-pairs") << QByteArray("UTF-32LE") << "a" + palm + "b" + palm << QByteArray::fromHex("6100000034f301006200000034f30100") << true;
    QTest::newRow("utf32-be-multiple-surrogate-pairs") << QByteArray("UTF-32BE") << "a" + palm + "b" + palm << QByteArray::fromHex("000000610001f334000000620001f334") << true;
    const QString marker = QString(QChar(0xfeff)) + "x";
    QTest::newRow("utf8-literal-feff-is-content") << QByteArray(ENCODING_UTF8) << marker << QByteArray::fromHex("efbbbf78") << true;
    QTest::newRow("utf16-literal-feff-is-content") << QByteArray("UTF-16LE") << marker << QByteArray::fromHex("fffe7800") << true;
    QTest::newRow("utf32-literal-feff-is-content") << QByteArray("UTF-32BE") << marker << QByteArray::fromHex("0000feff00000078") << true;
    QTest::newRow("utf8-empty-no-bom") << QByteArray(ENCODING_UTF8) << QString() << QByteArray() << true;
    QTest::newRow("utf16-empty-no-bom") << QByteArray("UTF-16LE") << QString() << QByteArray() << true;
    QTest::newRow("utf32-empty-no-bom") << QByteArray("UTF-32LE") << QString() << QByteArray() << true;
    QTest::newRow("utf32-le-rejects-high-surrogate") << QByteArray("UTF-32LE") << QString(QChar(0xd800)) << QByteArray() << false;
    QTest::newRow("utf32-be-rejects-low-surrogate") << QByteArray("UTF-32BE") << QString(QChar(0xdc00)) << QByteArray() << false;
}

void TestDocument::test_codec_encoder_stateless()
{
    QFETCH(QByteArray, codec);
    QFETCH(QString, text);
    QFETCH(QByteArray, bytes);
    QFETCH(bool, valid);
    TextEncoder encoder(codec);
    QVERIFY(encoder.isValid());
    const auto first = encoder.encode(text);
    QCOMPARE(first.first, valid);
    QCOMPARE(first.second, bytes);
    const auto second = encoder.encode(text);
    QCOMPARE(second.first, valid);
    QCOMPARE(second.second, bytes);
    const auto retried = encoder.encode("A");
    QVERIFY(retried.first);
    const QByteArray expectedA = codec == "UTF-32LE" ? QByteArray::fromHex("41000000")
        : codec == "UTF-32BE" ? QByteArray::fromHex("00000041")
        : codec == "UTF-16LE" ? QByteArray::fromHex("4100")
        : codec == "UTF-16BE" ? QByteArray::fromHex("0041") : QByteArray("A");
    QCOMPARE(retried.second, expectedA);
}

void TestDocument::test_codec_decoder_truncated_data()
{
    QTest::addColumn<QByteArray>("codec");
    QTest::addColumn<QByteArray>("bytes");
    QTest::addColumn<QByteArray>("retryBytes");
    QTest::newRow("utf8-final-partial-sequence") << QByteArray(ENCODING_UTF8) << QByteArray::fromHex("61c3") << QByteArray("A");
    QTest::newRow("utf8-final-partial-emoji") << QByteArray(ENCODING_UTF8) << QByteArray::fromHex("61f09f8c") << QByteArray("A");
    QTest::newRow("utf16-final-partial-unit") << QByteArray("UTF-16LE") << QByteArray::fromHex("610061") << QByteArray::fromHex("4100");
    QTest::newRow("utf32-le-final-partial-unit") << QByteArray("UTF-32LE") << QByteArray::fromHex("610000") << QByteArray::fromHex("41000000");
    QTest::newRow("utf32-be-final-partial-unit") << QByteArray("UTF-32BE") << QByteArray::fromHex("000061") << QByteArray::fromHex("00000041");
}

void TestDocument::test_codec_decoder_truncated()
{
    QFETCH(QByteArray, codec);
    QFETCH(QByteArray, bytes);
    QFETCH(QByteArray, retryBytes);
    TextDecoder decoder(codec);
    QVERIFY(decoder.isValid());
    const auto broken = decoder.decode(bytes);
    QVERIFY(!broken.first);
    QVERIFY(broken.second.isEmpty());
    const auto retry = decoder.decode(retryBytes);
    QVERIFY(retry.first);
    QCOMPARE(retry.second, QString("A"));
}

} // namespace QSynedit
