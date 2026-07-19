#include "../structview_testsupport.h"

class StructViewFontTests : public QObject
{
    Q_OBJECT

private slots:
    void builderRendersSfntTableDirectory();
    void builderRendersWoffDirectoryAndPayloads();
};

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

    QByteArray font(54, '\0');
    writeBe32(&font, 0, 0x00010000); // TrueType sfntVersion
    writeBe16(&font, 4, 2);          // numTables
    writeBe16(&font, 6, 32);         // searchRange
    writeBe16(&font, 8, 1);          // entrySelector
    writeBe16(&font, 10, 0);         // rangeShift
    font.replace(12, 4, "head");
    writeBe32(&font, 16, 0x11111111);
    writeBe32(&font, 20, 44);
    writeBe32(&font, 24, 4);
    font.replace(28, 4, "name");
    writeBe32(&font, 32, 0x22222222);
    writeBe32(&font, 36, 48);
    writeBe32(&font, 40, 6);
    font.replace(44, 4, QByteArray::fromHex("01020304"));
    font.replace(48, 6, QByteArray("font\0\0", 6));

    auto rows = buildRows(&library, sfntRoot, font);
    StructureRow *sfntRow = findTopLevelNamed(rows, QStringLiteral("SFNT"));
    QVERIFY(sfntRow);

    StructureRow *signature = findChildNamed(sfntRow, QStringLiteral("dword signature"));
    QVERIFY2(signature, qPrintable(childNames(sfntRow)));
    QCOMPARE(signature->value, QStringLiteral("SFNT_TRUETYPE_1_0"));

    StructureRow *offsetTable = findChildNamed(sfntRow, QStringLiteral("SFNT_OFFSET_TABLE font"));
    QVERIFY2(offsetTable, qPrintable(childNames(sfntRow)));
    QCOMPARE(findChildNamed(offsetTable, QStringLiteral("word numTables"))->value, QStringLiteral("2"));

    StructureRow *tables = findChildNamed(offsetTable, QStringLiteral("SFNT_TABLE_RECORD tables[]"));
    QVERIFY2(tables, qPrintable(childNames(offsetTable)));
    QCOMPARE(tables->children.size(), size_t(2));
    QCOMPARE(tables->children[0]->name, QStringLiteral("[0]head"));
    QCOMPARE(tables->children[1]->name, QStringLiteral("[1]name"));

    StructureRow *headTag = findChildNamed(tables->children[0].get(), QStringLiteral("dword tag"));
    QVERIFY2(headTag, qPrintable(childNames(tables->children[0].get())));
    QCOMPARE(headTag->value, QStringLiteral("\"head\""));
    QCOMPARE(findChildNamed(tables->children[0].get(), QStringLiteral("dword offset"))->value, QStringLiteral("44"));
    QCOMPARE(findChildNamed(tables->children[1].get(), QStringLiteral("dword length"))->value, QStringLiteral("6"));

    StructureRow *semantic = findSemanticRootChildNamed(rows, QStringLiteral("SFNT Summary"));
    QVERIFY2(semantic, "SFNT Summary semantic child row not found");
    StructureRow *fontTables = findChildNamed(semantic, QStringLiteral("FontTables"));
    QVERIFY2(fontTables, qPrintable(childNames(semantic)));
    QCOMPARE(fontTables->children.size(), size_t(2));
    QCOMPARE(fontTables->children[0]->name, QStringLiteral("head"));
    QCOMPARE(fontTables->children[0]->offset, QStringLiteral("0000002C"));
    QCOMPARE(fontTables->children[0]->children.size(), size_t(4));
    QCOMPARE(fontTables->children[1]->name, QStringLiteral("name"));
    QCOMPARE(fontTables->children[1]->offset, QStringLiteral("00000030"));
    QCOMPARE(fontTables->children[1]->children.size(), size_t(6));
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

    QByteArray woff(68, '\0');
    woff.replace(0, 4, "wOFF");
    writeBe32(&woff, 4, 0x00010000); // TrueType flavor
    writeBe32(&woff, 8, quint32(woff.size()));
    writeBe16(&woff, 12, 1);
    writeBe16(&woff, 14, 0);
    writeBe32(&woff, 16, 32); // reconstructed sfnt size
    writeBe16(&woff, 20, 1);
    writeBe16(&woff, 22, 0);
    writeBe32(&woff, 24, 64);
    writeBe32(&woff, 28, 4);
    writeBe32(&woff, 32, 8);
    writeBe32(&woff, 36, 0);
    writeBe32(&woff, 40, 0);
    woff.replace(44, 4, "name");
    writeBe32(&woff, 48, 64);
    writeBe32(&woff, 52, 4);
    writeBe32(&woff, 56, 8);
    writeBe32(&woff, 60, 0x12345678);
    woff.replace(64, 4, QByteArray::fromHex("01020304"));

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
    QCOMPARE(numTables->value, QStringLiteral("1"));

    StructureRow *tables = findChildNamed(woffRow, QStringLiteral("WOFF_TABLE_DIRECTORY_ENTRY tables[]"));
    QVERIFY2(tables, qPrintable(childNames(woffRow)));
    QCOMPARE(tables->children.size(), size_t(1));
    QCOMPARE(tables->children[0]->name, QStringLiteral("[0]name"));
    StructureRow *tag = findChildNamed(tables->children[0].get(), QStringLiteral("dword tag"));
    QVERIFY2(tag, qPrintable(childNames(tables->children[0].get())));
    QCOMPARE(tag->value, QStringLiteral("\"name\""));

    StructureRow *tableOffset = findChildNamed(tables->children[0].get(), QStringLiteral("dword offset"));
    QVERIFY2(tableOffset, qPrintable(childNames(tables->children[0].get())));
    QVERIFY(tableOffset->children.empty());

    StructureRow *semantic = findSemanticRootChildNamed(rows, QStringLiteral("WOFF Summary"));
    QVERIFY2(semantic, "WOFF Summary semantic child row not found");
    StructureRow *fontTables = findChildNamed(semantic, QStringLiteral("FontTables"));
    QVERIFY2(fontTables, qPrintable(childNames(semantic)));
    QCOMPARE(fontTables->children.size(), size_t(1));
    StructureRow *tableData = fontTables->children[0].get();
    QCOMPARE(tableData->name, QStringLiteral("name"));
    QCOMPARE(tableData->offset, QStringLiteral("00000040"));
    QCOMPARE(tableData->children.size(), size_t(4));

    StructureRow *metaOffset = findChildNamed(header, QStringLiteral("dword metaOffset"));
    QVERIFY2(metaOffset, qPrintable(childNames(header)));
    StructureRow *metadata = findChildNamed(metaOffset, QStringLiteral("BYTE Metadata[]"));
    QVERIFY2(metadata, qPrintable(childNames(metaOffset)));
    QCOMPARE(metadata->offset, QStringLiteral("00000040"));
    QCOMPARE(metadata->children[0]->value, QStringLiteral("1"));
}

REGISTER_STRUCTVIEW_TEST(StructViewFontTests)
#include "font_tests.moc"
