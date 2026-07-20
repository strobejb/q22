#include "../structview_testsupport.h"

class StructViewFontTests : public QObject
{
    Q_OBJECT

private slots:
    void builderRendersSfntTableDirectory();
    void builderRendersWoffDirectoryAndPayloads();
};

namespace
{

QByteArray utf16be(const QString &text)
{
    QByteArray bytes;
    bytes.reserve(text.size() * 2);
    for (QChar ch : text)
    {
        const ushort value = ch.unicode();
        bytes.append(char((value >> 8) & 0xff));
        bytes.append(char(value & 0xff));
    }
    return bytes;
}

void writeSfntTableRecord(QByteArray *font,
                          qsizetype offset,
                          const char tag[4],
                          quint32 checksum,
                          quint32 tableOffset,
                          quint32 tableLength)
{
    font->replace(offset, 4, QByteArray(tag, 4));
    writeBe32(font, offset + 4, checksum);
    writeBe32(font, offset + 8, tableOffset);
    writeBe32(font, offset + 12, tableLength);
}

void writeMinimalHeadTable(QByteArray *font, qsizetype offset)
{
    writeBe16(font, offset + 0, 1);
    writeBe16(font, offset + 2, 0);
    writeBe32(font, offset + 4, 0x00010000);
    writeBe32(font, offset + 8, 0);
    writeBe32(font, offset + 12, 0x5f0f3cf5);
    writeBe16(font, offset + 16, 0x000b);
    writeBe16(font, offset + 18, 2048);
    writeBe64(font, offset + 20, 0);
    writeBe64(font, offset + 28, 0);
    writeBe16(font, offset + 36, 0xff38); // xMin = -200
    writeBe16(font, offset + 38, 0xff9c); // yMin = -100
    writeBe16(font, offset + 40, 1200);
    writeBe16(font, offset + 42, 1600);
    writeBe16(font, offset + 44, 0);
    writeBe16(font, offset + 46, 9);
    writeBe16(font, offset + 48, 2);
    writeBe16(font, offset + 50, 1);
    writeBe16(font, offset + 52, 0);
}

void writeMinimalHheaTable(QByteArray *font, qsizetype offset)
{
    writeBe16(font, offset + 0, 1);
    writeBe16(font, offset + 2, 0);
    writeBe16(font, offset + 4, 1500);
    writeBe16(font, offset + 6, 0xff38); // descender = -200
    writeBe16(font, offset + 8, 0);
    writeBe16(font, offset + 10, 1300);
    writeBe16(font, offset + 12, 0xffce);
    writeBe16(font, offset + 14, 0xffce);
    writeBe16(font, offset + 16, 1250);
    writeBe16(font, offset + 18, 1);
    writeBe16(font, offset + 20, 0);
    writeBe16(font, offset + 22, 0);
    writeBe16(font, offset + 32, 0);
    writeBe16(font, offset + 34, 3);
}

void writeMinimalMaxpTable(QByteArray *font, qsizetype offset)
{
    writeBe32(font, offset + 0, 0x00010000);
    writeBe16(font, offset + 4, 42);
    writeBe16(font, offset + 6, 12);
    writeBe16(font, offset + 8, 2);
    writeBe16(font, offset + 10, 24);
    writeBe16(font, offset + 12, 4);
    writeBe16(font, offset + 14, 2);
}

void writeMinimalNameTable(QByteArray *font, qsizetype offset, const QString &name)
{
    const QByteArray nameBytes = utf16be(name);
    writeBe16(font, offset + 0, 0);
    writeBe16(font, offset + 2, 1);
    writeBe16(font, offset + 4, 18);
    writeBe16(font, offset + 6, 3);  // Windows
    writeBe16(font, offset + 8, 1);  // Unicode BMP
    writeBe16(font, offset + 10, 0x0409);
    writeBe16(font, offset + 12, 4); // Full font name
    writeBe16(font, offset + 14, quint16(nameBytes.size()));
    writeBe16(font, offset + 16, 0);
    font->replace(offset + 18, nameBytes.size(), nameBytes);
}

void writeMinimalCmapTable(QByteArray *font, qsizetype offset)
{
    writeBe16(font, offset + 0, 0);
    writeBe16(font, offset + 2, 1);
    writeBe16(font, offset + 4, 3);
    writeBe16(font, offset + 6, 1);
    writeBe32(font, offset + 8, 12);
}

} // namespace

void StructViewFontTests::builderRendersSfntTableDirectory()
{
    // Scenario: TTF/OTF fonts share the big-endian SFNT table-directory
    // container. Table records carry FourCC tags plus offsets and lengths.
    // Expected: the standard SFNT definition names table-record rows by tag,
    // keeps raw table-directory fields intact, and emits table bytes under
    // SFNT Summary/FontTables.
    StrataLibrary library;
    QVERIFY2(parseStandardDefinition(&library, QStringLiteral("sfnt.strata")), "sfnt.strata failed to parse");
    TypeDecl *sfntRoot = exportedNamed(&library, QStringLiteral("SFNT"));
    QVERIFY(sfntRoot);

    QByteArray font(264, '\0');
    writeBe32(&font, 0, 0x00010000); // TrueType sfntVersion
    writeBe16(&font, 4, 5);          // numTables
    writeBe16(&font, 6, 64);         // searchRange
    writeBe16(&font, 8, 2);          // entrySelector
    writeBe16(&font, 10, 16);        // rangeShift

    writeSfntTableRecord(&font, 12, "head", 0x11111111, 92, 54);
    writeSfntTableRecord(&font, 28, "hhea", 0x22222222, 148, 36);
    writeSfntTableRecord(&font, 44, "maxp", 0x33333333, 184, 32);
    writeSfntTableRecord(&font, 60, "name", 0x44444444, 216, 26);
    writeSfntTableRecord(&font, 76, "cmap", 0x55555555, 244, 12);

    writeMinimalHeadTable(&font, 92);
    writeMinimalHheaTable(&font, 148);
    writeMinimalMaxpTable(&font, 184);
    writeMinimalNameTable(&font, 216, QStringLiteral("Test"));
    writeMinimalCmapTable(&font, 244);

    auto rows = buildRows(&library, sfntRoot, font);
    StructureRow *sfntRow = findTopLevelNamed(rows, QStringLiteral("SFNT"));
    QVERIFY(sfntRow);

    StructureRow *signature = findChildNamed(sfntRow, QStringLiteral("dword signature"));
    QVERIFY2(signature, qPrintable(childNames(sfntRow)));
    QCOMPARE(signature->value, QStringLiteral("SFNT_TRUETYPE_1_0"));

    StructureRow *offsetTable = findChildNamed(sfntRow, QStringLiteral("SFNT_OFFSET_TABLE font"));
    QVERIFY2(offsetTable, qPrintable(childNames(sfntRow)));
    QCOMPARE(findChildNamed(offsetTable, QStringLiteral("word numTables"))->value, QStringLiteral("5"));

    StructureRow *tables = findChildNamed(offsetTable, QStringLiteral("SFNT_TABLE_RECORD tables[]"));
    QVERIFY2(tables, qPrintable(childNames(offsetTable)));
    QCOMPARE(tables->children.size(), size_t(5));
    QCOMPARE(tables->children[0]->name, QStringLiteral("[0]head"));
    QCOMPARE(tables->children[3]->name, QStringLiteral("[3]name"));

    StructureRow *headTag = findChildNamed(tables->children[0].get(), QStringLiteral("dword tag"));
    QVERIFY2(headTag, qPrintable(childNames(tables->children[0].get())));
    QCOMPARE(headTag->value, QStringLiteral("\"head\""));
    QCOMPARE(findChildNamed(tables->children[0].get(), QStringLiteral("dword offset"))->value, QStringLiteral("92"));
    QCOMPARE(findChildNamed(tables->children[3].get(), QStringLiteral("dword length"))->value, QStringLiteral("26"));

    StructureRow *decodedHead = findDescendantNamed(tables->children[0].get(), QStringLiteral("Decoded head"));
    QVERIFY2(decodedHead, qPrintable(childNames(tables->children[0].get())));
    QCOMPARE(findChildNamed(decodedHead, QStringLiteral("word unitsPerEm"))->value, QStringLiteral("2048"));
    QCOMPARE(findChildNamed(decodedHead, QStringLiteral("SFNT_FWORD indexToLocFormat"))->value,
             QStringLiteral("SFNT_LOCA_LONG_OFFSETS"));

    StructureRow *decodedMaxp = findDescendantNamed(tables->children[2].get(), QStringLiteral("Decoded maxp"));
    QVERIFY2(decodedMaxp, qPrintable(childNames(tables->children[2].get())));
    QCOMPARE(findChildNamed(decodedMaxp, QStringLiteral("word numGlyphs"))->value, QStringLiteral("42"));

    StructureRow *decodedName = findDescendantNamed(tables->children[3].get(), QStringLiteral("Decoded name"));
    QVERIFY2(decodedName, qPrintable(childNames(tables->children[3].get())));
    StructureRow *nameRecords = findChildNamed(decodedName, QStringLiteral("SFNT_NAME_RECORD records[]"));
    QVERIFY2(nameRecords, qPrintable(childNames(decodedName)));
    QCOMPARE(nameRecords->children.size(), size_t(1));
    StructureRow *nameRecord = nameRecords->children[0].get();
    QCOMPARE(findChildNamed(nameRecord, QStringLiteral("word nameID"))->value,
             QStringLiteral("SFNT_NAME_FULL_FONT_NAME"));
    StructureRow *text = findChildNamed(nameRecord, QStringLiteral("SFNT_UTF16BE_BYTE TextUtf16Be[]"));
    QVERIFY2(text, qPrintable(childNames(nameRecord)));
    QCOMPARE(text->value, QStringLiteral("\"Test\""));

    StructureRow *decodedCmap = findDescendantNamed(tables->children[4].get(), QStringLiteral("Decoded cmap"));
    QVERIFY2(decodedCmap, qPrintable(childNames(tables->children[4].get())));
    StructureRow *encodings = findChildNamed(decodedCmap, QStringLiteral("SFNT_CMAP_ENCODING_RECORD encodings[]"));
    QVERIFY2(encodings, qPrintable(childNames(decodedCmap)));
    QCOMPARE(encodings->children.size(), size_t(1));
    QCOMPARE(findChildNamed(encodings->children[0].get(), QStringLiteral("word platformID"))->value,
             QStringLiteral("SFNT_PLATFORM_WINDOWS"));

    StructureRow *semantic = findSemanticRootChildNamed(rows, QStringLiteral("SFNT Summary"));
    QVERIFY2(semantic, "SFNT Summary semantic child row not found");
    StructureRow *fontTables = findChildNamed(semantic, QStringLiteral("FontTables"));
    QVERIFY2(fontTables, qPrintable(childNames(semantic)));
    QCOMPARE(fontTables->children.size(), size_t(5));
    QCOMPARE(fontTables->children[0]->name, QStringLiteral("head"));
    QCOMPARE(fontTables->children[0]->offset, QStringLiteral("0000005C"));
    QCOMPARE(fontTables->children[0]->children.size(), size_t(54));
    QCOMPARE(fontTables->children[3]->name, QStringLiteral("name"));
    QCOMPARE(fontTables->children[3]->offset, QStringLiteral("000000D8"));
    QCOMPARE(fontTables->children[3]->children.size(), size_t(26));
}

void StructViewFontTests::builderRendersWoffDirectoryAndPayloads()
{
    // Scenario: WOFF 1.0 is a big-endian wrapper around sfnt table data with a
    // fixed header and table directory.
    // Expected: the standard WOFF definition renders the directory entries by
    // tag, emits compressed tables under WOFF Summary/FontTables, and still exposes
    // metadata payloads as dynamic bytes.
    StrataLibrary library;
    QVERIFY2(parseStandardDefinition(&library, QStringLiteral("woff.strata")), "woff.strata failed to parse");
    TypeDecl *woffRoot = exportedNamed(&library, QStringLiteral("WOFF"));
    QVERIFY(woffRoot);

    QByteArray woff(120, '\0');
    woff.replace(0, 4, "wOFF");
    writeBe32(&woff, 4, 0x00010000); // TrueType flavor
    writeBe32(&woff, 8, quint32(woff.size()));
    writeBe16(&woff, 12, 2);
    writeBe16(&woff, 14, 0);
    writeBe32(&woff, 16, 32); // reconstructed sfnt size
    writeBe16(&woff, 20, 1);
    writeBe16(&woff, 22, 0);
    writeBe32(&woff, 24, 116);
    writeBe32(&woff, 28, 4);
    writeBe32(&woff, 32, 8);
    writeBe32(&woff, 36, 0);
    writeBe32(&woff, 40, 0);
    woff.replace(44, 4, "name");
    writeBe32(&woff, 48, 84);
    writeBe32(&woff, 52, 26);
    writeBe32(&woff, 56, 26);
    writeBe32(&woff, 60, 0x12345678);
    woff.replace(64, 4, "head");
    writeBe32(&woff, 68, 112);
    writeBe32(&woff, 72, 4);
    writeBe32(&woff, 76, 54);
    writeBe32(&woff, 80, 0x87654321);

    writeMinimalNameTable(&woff, 84, QStringLiteral("Test"));
    woff.replace(112, 4, QByteArray::fromHex("01020304"));
    woff.replace(116, 4, QByteArray::fromHex("05060708"));

    auto rows = buildRows(&library, woffRoot, woff);
    StructureRow *woffRow = findTopLevelNamed(rows, QStringLiteral("WOFF"));
    QVERIFY(woffRow);

    StructureRow *header = findChildNamed(woffRow, QStringLiteral("WOFF_HEADER header"));
    QVERIFY2(header, qPrintable(childNames(woffRow)));
    StructureRow *signature = findChildNamed(header, QStringLiteral("char signature[]"));
    QVERIFY2(signature, qPrintable(childNames(header)));
    QCOMPARE(signature->value, QStringLiteral("\"wOFF\""));
    StructureRow *flavor = findChildNamed(header, QStringLiteral("dword flavor"));
    QVERIFY2(flavor, qPrintable(childNames(header)));
    QCOMPARE(flavor->value, QStringLiteral("WOFF_SFNT_TRUETYPE_1_0"));
    StructureRow *numTables = findChildNamed(header, QStringLiteral("word numTables"));
    QVERIFY2(numTables, qPrintable(childNames(header)));
    QCOMPARE(numTables->value, QStringLiteral("2"));

    StructureRow *tables = findChildNamed(woffRow, QStringLiteral("WOFF_TABLE_DIRECTORY_ENTRY tables[]"));
    QVERIFY2(tables, qPrintable(childNames(woffRow)));
    QCOMPARE(tables->children.size(), size_t(2));
    QCOMPARE(tables->children[0]->name, QStringLiteral("[0]name"));
    StructureRow *tag = findChildNamed(tables->children[0].get(), QStringLiteral("dword tag"));
    QVERIFY2(tag, qPrintable(childNames(tables->children[0].get())));
    QCOMPARE(tag->value, QStringLiteral("\"name\""));

    StructureRow *tableOffset = findChildNamed(tables->children[0].get(), QStringLiteral("dword offset"));
    QVERIFY2(tableOffset, qPrintable(childNames(tables->children[0].get())));

    StructureRow *decodedName = findDescendantNamed(tables->children[0].get(), QStringLiteral("Decoded name"));
    QVERIFY2(decodedName, qPrintable(childNames(tables->children[0].get())));
    StructureRow *nameRecords = findChildNamed(decodedName, QStringLiteral("SFNT_NAME_RECORD records[]"));
    QVERIFY2(nameRecords, qPrintable(childNames(decodedName)));
    StructureRow *text = findChildNamed(nameRecords->children[0].get(), QStringLiteral("SFNT_UTF16BE_BYTE TextUtf16Be[]"));
    QVERIFY2(text, qPrintable(childNames(nameRecords->children[0].get())));
    QCOMPARE(text->value, QStringLiteral("\"Test\""));

    QCOMPARE(tables->children[1]->name, QStringLiteral("[1]head"));
    QVERIFY2(!findDescendantNamed(tables->children[1].get(), QStringLiteral("Decoded head")),
             qPrintable(childNames(tables->children[1].get())));

    StructureRow *semantic = findSemanticRootChildNamed(rows, QStringLiteral("WOFF Summary"));
    QVERIFY2(semantic, "WOFF Summary semantic child row not found");
    StructureRow *fontTables = findChildNamed(semantic, QStringLiteral("FontTables"));
    QVERIFY2(fontTables, qPrintable(childNames(semantic)));
    QCOMPARE(fontTables->children.size(), size_t(2));
    StructureRow *tableData = fontTables->children[0].get();
    QCOMPARE(tableData->name, QStringLiteral("name"));
    QCOMPARE(tableData->offset, QStringLiteral("00000054"));
    QCOMPARE(tableData->children.size(), size_t(26));
    QCOMPARE(fontTables->children[1]->name, QStringLiteral("head"));
    QCOMPARE(fontTables->children[1]->offset, QStringLiteral("00000070"));
    QCOMPARE(fontTables->children[1]->children.size(), size_t(4));

    StructureRow *metaOffset = findChildNamed(header, QStringLiteral("dword metaOffset"));
    QVERIFY2(metaOffset, qPrintable(childNames(header)));
    StructureRow *metadata = findChildNamed(metaOffset, QStringLiteral("BYTE Metadata[]"));
    QVERIFY2(metadata, qPrintable(childNames(metaOffset)));
    QCOMPARE(metadata->offset, QStringLiteral("00000074"));
    QCOMPARE(metadata->children[0]->value, QStringLiteral("5"));
}

REGISTER_STRUCTVIEW_TEST(StructViewFontTests)
#include "font_tests.moc"
