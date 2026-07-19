#include "../structview_testsupport.h"

class StructViewArchiveTests : public QObject
{
    Q_OBJECT

private slots:
    void builderRendersTarEntries();
    void builderAdvancesTarEntriesByFullPayloadExtent();
    void builderRendersGzipHeaderAndTrailer();
    void builderRendersCabinetHeaderFilesAndData();
    void builderRendersZipCentralDirectoryFromEocd();
};

void StructViewArchiveTests::builderRendersTarEntries()
{
    // Scenario: TAR stores records as 512-byte headers followed by payloads
    // whose size is encoded as ASCII octal.
    // Expected: the standard TAR definition names entries from the header,
    // converts the octal size field, and advances over the padded payload.
    StrataLibrary library;
    QVERIFY2(parseStandardDefinition(&library, QStringLiteral("tar.strata")), "tar.strata failed to parse");
    TypeDecl *tarRoot = exportedNamed(&library, QStringLiteral("TAR"));
    QVERIFY(tarRoot);

    QByteArray tar(2048, '\0');
    writeAscii(&tar, 0, "hello.txt");
    writeAscii(&tar, 100, "0000644");
    writeAscii(&tar, 108, "0000000");
    writeAscii(&tar, 116, "0000000");
    writeAscii(&tar, 124, "00000000005");
    writeAscii(&tar, 136, "00000000000");
    writeAscii(&tar, 148, "        ");
    tar[156] = char('0');
    writeAscii(&tar, 257, "ustar");
    writeAscii(&tar, 263, "00");
    tar.replace(512, 5, "hello");

    auto rows = buildRows(&library, tarRoot, tar);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->name, QStringLiteral("TAR"));

    StructureRow *entries = findChildNamed(rows[0].get(), QStringLiteral("TAR_ENTRY entries[]"));
    QVERIFY2(entries, qPrintable(childNames(rows[0].get())));
    QCOMPARE(entries->children.size(), size_t(2));
    QCOMPARE(entries->children[0]->name, QStringLiteral("[0]hello.txt"));
    QCOMPARE(entries->children[1]->name, QStringLiteral("[1]"));

    StructureRow *header = findChildNamed(entries->children[0].get(), QStringLiteral("TAR_HEADER header"));
    QVERIFY2(header, qPrintable(childNames(entries->children[0].get())));
    StructureRow *size = findChildNamed(header, QStringLiteral("char size[]"));
    QVERIFY2(size, qPrintable(childNames(header)));
    QCOMPARE(size->value, QStringLiteral("\"00000000005\""));

    StructureRow *data = findChildNamed(entries->children[0].get(), QStringLiteral("byte data[]"));
    QVERIFY2(data, qPrintable(childNames(entries->children[0].get())));
    QCOMPARE(data->offset, QStringLiteral("00000200"));
    QCOMPARE(data->value, QStringLiteral("{ 104, 101, 108, 108, 111 }"));
}

void StructViewArchiveTests::builderAdvancesTarEntriesByFullPayloadExtent()
{
    // Scenario: TAR entries may contain large payloads such as WASM files.
    // Expected: the entry consumes the full padded payload extent even though
    // the byte array preview only renders the first display-capped elements.
    // Regression guard: without extent(...) on TAR_ENTRY.data, the following
    // header is parsed from inside the payload and Structure View can appear to
    // hang while walking garbage records.
    StrataLibrary library;
    QVERIFY2(parseStandardDefinition(&library, QStringLiteral("tar.strata")), "tar.strata failed to parse");
    TypeDecl *tarRoot = exportedNamed(&library, QStringLiteral("TAR"));
    QVERIFY(tarRoot);

    QByteArray tar(4096, '\0');
    writeAscii(&tar, 0, "module.wasm");
    writeAscii(&tar, 100, "0000644");
    writeAscii(&tar, 108, "0000000");
    writeAscii(&tar, 116, "0000000");
    writeAscii(&tar, 124, "00000002000"); // 1024 bytes
    writeAscii(&tar, 136, "00000000000");
    writeAscii(&tar, 148, "        ");
    tar[156] = char('0');
    writeAscii(&tar, 257, "ustar");
    writeAscii(&tar, 263, "00");
    for (int i = 0; i < 1024; ++i)
        tar[512 + i] = char(i & 0xff);

    const int secondHeader = 512 + 1024;
    writeAscii(&tar, secondHeader, "next.bin");
    writeAscii(&tar, secondHeader + 100, "0000644");
    writeAscii(&tar, secondHeader + 108, "0000000");
    writeAscii(&tar, secondHeader + 116, "0000000");
    writeAscii(&tar, secondHeader + 124, "00000000001");
    writeAscii(&tar, secondHeader + 136, "00000000000");
    writeAscii(&tar, secondHeader + 148, "        ");
    tar[secondHeader + 156] = char('0');
    writeAscii(&tar, secondHeader + 257, "ustar");
    writeAscii(&tar, secondHeader + 263, "00");
    tar[secondHeader + 512] = char('x');

    auto rows = buildRows(&library, tarRoot, tar);
    QCOMPARE(rows.size(), size_t(1));
    StructureRow *entries = findChildNamed(rows[0].get(), QStringLiteral("TAR_ENTRY entries[]"));
    QVERIFY2(entries, qPrintable(childNames(rows[0].get())));
    QVERIFY(entries->children.size() >= size_t(3));
    QCOMPARE(entries->children[0]->name, QStringLiteral("[0]module.wasm"));
    QCOMPARE(entries->children[1]->name, QStringLiteral("[1]next.bin"));

    StructureRow *secondHeaderRow = findChildNamed(entries->children[1].get(), QStringLiteral("TAR_HEADER header"));
    QVERIFY2(secondHeaderRow, qPrintable(childNames(entries->children[1].get())));
    QCOMPARE(secondHeaderRow->absoluteOffset, uint64_t(secondHeader));
}

void StructViewArchiveTests::builderRendersGzipHeaderAndTrailer()
{
    // Scenario: GZip has a fixed header followed by optional NUL-terminated
    // filename/comment fields, compressed bytes, and a fixed trailer.
    // Expected: the standard GZip definition renders the optional strings and
    // bounds compressedData before the trailer.
    StrataLibrary library;
    QVERIFY2(parseStandardDefinition(&library, QStringLiteral("gzip.strata")), "gzip.strata failed to parse");
    TypeDecl *gzipRoot = exportedNamed(&library, QStringLiteral("GZIP"));
    QVERIFY(gzipRoot);

    QByteArray gzip;
    gzip.append(char(0x1f));
    gzip.append(char(0x8b));
    gzip.append(char(8));    // deflate
    gzip.append(char(0x18)); // FNAME | FCOMMENT
    gzip.append(QByteArray::fromHex("00000000"));
    gzip.append(char(0));
    gzip.append(char(3));    // Unix
    gzip.append("hello.txt", 9);
    gzip.append(char(0));
    gzip.append("comment", 7);
    gzip.append(char(0));
    gzip.append(QByteArray::fromHex("01020304"));
    gzip.append(QByteArray::fromHex("78563412"));
    gzip.append(QByteArray::fromHex("05000000"));

    auto rows = buildRows(&library, gzipRoot, gzip);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->name, QStringLiteral("GZIP"));

    StructureRow *header = findChildNamed(rows[0].get(), QStringLiteral("GZIP_HEADER header"));
    QVERIFY2(header, qPrintable(childNames(rows[0].get())));
    QCOMPARE(findChildNamed(header, QStringLiteral("byte compressionMethod"))->value,
             QStringLiteral("GZIP_COMPRESSION_DEFLATE"));
    QCOMPARE(findChildNamed(header, QStringLiteral("byte operatingSystem"))->value,
             QStringLiteral("GZIP_OS_UNIX"));
    StructureRow *originalName = findChildNamed(header, QStringLiteral("char originalName[]"));
    QVERIFY2(originalName, qPrintable(childNames(header)));
    QCOMPARE(originalName->value, QStringLiteral("\"hello.txt\""));
    StructureRow *comment = findChildNamed(header, QStringLiteral("char comment[]"));
    QVERIFY2(comment, qPrintable(childNames(header)));
    QCOMPARE(comment->value, QStringLiteral("\"comment\""));

    StructureRow *compressedData = findChildNamed(rows[0].get(), QStringLiteral("byte compressedData[]"));
    QVERIFY2(compressedData, qPrintable(childNames(rows[0].get())));
    QCOMPARE(compressedData->value, QStringLiteral("{ 1, 2, 3, 4 }"));

    StructureRow *trailer = findChildNamed(rows[0].get(), QStringLiteral("GZIP_TRAILER trailer"));
    QVERIFY2(trailer, qPrintable(childNames(rows[0].get())));
    QCOMPARE(findChildNamed(trailer, QStringLiteral("dword crc32"))->value, QStringLiteral("305419896"));
    QCOMPARE(findChildNamed(trailer, QStringLiteral("dword inputSize"))->value, QStringLiteral("5"));
}

void StructViewArchiveTests::builderRendersCabinetHeaderFilesAndData()
{
    // Scenario: CAB files contain a fixed header, folder table, file table, and
    // compressed data blocks referenced by each folder.
    // Expected: the standard CAB definition renders the folder's data blocks at
    // their target offset and names files from CFFILE.fileName.
    StrataLibrary library;
    QVERIFY2(parseStandardDefinition(&library, QStringLiteral("cab.strata")), "cab.strata failed to parse");
    TypeDecl *cabRoot = exportedNamed(&library, QStringLiteral("CAB"));
    QVERIFY(cabRoot);

    QByteArray cab(83, '\0');
    cab.replace(0, 4, "MSCF");
    writeLe32(&cab, 8, quint32(cab.size()));
    writeLe32(&cab, 16, 44); // filesOffset
    cab[24] = char(3);       // versionMinor
    cab[25] = char(1);       // versionMajor
    writeLe16(&cab, 26, 1);  // folderCount
    writeLe16(&cab, 28, 1);  // fileCount
    writeLe16(&cab, 32, 7);  // setId

    writeLe32(&cab, 36, 70); // firstDataBlockOffset
    writeLe16(&cab, 40, 1);  // dataBlockCount
    writeLe16(&cab, 42, 0);  // no compression

    writeLe32(&cab, 44, 5);  // uncompressedSize
    writeLe32(&cab, 48, 0);  // uncompressedFolderOffset
    writeLe16(&cab, 52, 0);  // folderIndex
    writeLe16(&cab, 58, 0x0020); // archive attribute
    writeAscii(&cab, 60, "hello.txt");

    writeLe32(&cab, 70, 0);  // checksum
    writeLe16(&cab, 74, 5);  // compressedSize
    writeLe16(&cab, 76, 5);  // uncompressedSize
    cab.replace(78, 5, "hello");

    auto rows = buildRows(&library, cabRoot, cab);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->name, QStringLiteral("CAB"));

    StructureRow *header = findChildNamed(rows[0].get(), QStringLiteral("CAB_CFHEADER header"));
    QVERIFY2(header, qPrintable(childNames(rows[0].get())));
    QCOMPARE(findChildNamed(header, QStringLiteral("dword signature"))->value,
             QStringLiteral("CAB_SIGNATURE_MSCF"));
    QCOMPARE(findChildNamed(header, QStringLiteral("word folderCount"))->value, QStringLiteral("1"));
    QCOMPARE(findChildNamed(header, QStringLiteral("word fileCount"))->value, QStringLiteral("1"));

    StructureRow *folders = findChildNamed(rows[0].get(), QStringLiteral("CAB_CFFOLDER folders[]"));
    QVERIFY2(folders, qPrintable(childNames(rows[0].get())));
    QCOMPARE(folders->children.size(), size_t(1));
    StructureRow *dataBlocks = findChildNamed(folders->children[0].get(), QStringLiteral("CAB_CFDATA dataBlocks[]"));
    QVERIFY2(dataBlocks, qPrintable(childNames(folders->children[0].get())));
    QCOMPARE(dataBlocks->children.size(), size_t(1));
    QCOMPARE(dataBlocks->children[0]->offset, QStringLiteral("00000046"));
    StructureRow *compressedData = findChildNamed(dataBlocks->children[0].get(), QStringLiteral("byte compressedData[]"));
    QVERIFY2(compressedData, qPrintable(childNames(dataBlocks->children[0].get())));
    QCOMPARE(compressedData->value, QStringLiteral("{ 104, 101, 108, 108, 111 }"));

    StructureRow *files = findChildNamed(rows[0].get(), QStringLiteral("CAB_CFFILE files[]"));
    QVERIFY2(files, qPrintable(childNames(rows[0].get())));
    QCOMPARE(files->children.size(), size_t(1));
    QCOMPARE(files->children[0]->name, QStringLiteral("[0]hello.txt"));
    StructureRow *fileName = findChildNamed(files->children[0].get(), QStringLiteral("char fileName[]"));
    QVERIFY2(fileName, qPrintable(childNames(files->children[0].get())));
    QCOMPARE(fileName->value, QStringLiteral("\"hello.txt\""));
}

void StructViewArchiveTests::builderRendersZipCentralDirectoryFromEocd()
{
    // Scenario: ZIP local headers may defer CRC/sizes to data descriptors, so
    // walking local headers can stop after the first payload.
    // Expected: the standard ZIP definition finds EOCD from the trailer and
    // renders all central-directory entries instead.
    StrataLibrary library;
    QVERIFY2(parseStandardDefinition(&library, QStringLiteral("zip.strata")), "zip.strata failed to parse");
    TypeDecl *zipRoot = exportedNamed(&library, QStringLiteral("ZIP"));
    QVERIFY(zipRoot);

    QByteArray zip;
    auto appendLe16 = [&zip](quint16 value) {
        zip.append(char(value & 0xff));
        zip.append(char((value >> 8) & 0xff));
    };
    auto appendLe32 = [&zip](quint32 value) {
        zip.append(char(value & 0xff));
        zip.append(char((value >> 8) & 0xff));
        zip.append(char((value >> 16) & 0xff));
        zip.append(char((value >> 24) & 0xff));
    };
    auto appendLocal = [&](const QByteArray &name, const QByteArray &data) -> quint32 {
        const quint32 offset = static_cast<quint32>(zip.size());
        appendLe32(0x04034b50);
        appendLe16(20);
        appendLe16(0x0008); // data descriptor follows payload
        appendLe16(8);
        appendLe16(0);
        appendLe16(0);
        appendLe32(0);
        appendLe32(0);
        appendLe32(0);
        appendLe16(static_cast<quint16>(name.size()));
        appendLe16(0);
        zip.append(name);
        zip.append(data);
        appendLe32(0x08074b50);
        appendLe32(0);
        appendLe32(static_cast<quint32>(data.size()));
        appendLe32(static_cast<quint32>(data.size()));
        return offset;
    };
    auto appendCentral = [&](const QByteArray &name, quint32 localOffset, quint32 size) {
        appendLe32(0x02014b50);
        appendLe16(20);
        appendLe16(20);
        appendLe16(0x0008);
        appendLe16(8);
        appendLe16(0);
        appendLe16(0);
        appendLe32(0);
        appendLe32(size);
        appendLe32(size);
        appendLe16(static_cast<quint16>(name.size()));
        appendLe16(0);
        appendLe16(0);
        appendLe16(0);
        appendLe16(0);
        appendLe32(0);
        appendLe32(localOffset);
        zip.append(name);
    };

    const quint32 firstOffset = appendLocal("a.txt", "alpha");
    const quint32 secondOffset = appendLocal("b.txt", "bravo");
    const quint32 centralOffset = static_cast<quint32>(zip.size());
    appendCentral("a.txt", firstOffset, 5);
    appendCentral("b.txt", secondOffset, 5);
    const quint32 centralSize = static_cast<quint32>(zip.size()) - centralOffset;
    appendLe32(0x06054b50);
    appendLe16(0);
    appendLe16(0);
    appendLe16(2);
    appendLe16(2);
    appendLe32(centralSize);
    appendLe32(centralOffset);
    appendLe16(0);

    auto rows = buildRows(&library, zipRoot, zip);
    QCOMPARE(rows.size(), size_t(1));

    StructureRow *locals = nullptr;
    StructureRow *central = nullptr;
    for (const auto &child : rows[0]->children)
    {
        if (child->name.contains(QStringLiteral("localFileHeaders")))
        {
            locals = child.get();
            continue;
        }

        if (child->name.contains(QStringLiteral("centralDirectory")))
        {
            central = child.get();
            break;
        }
    }

    QVERIFY(locals);
    QCOMPARE(locals->children.size(), size_t(1));
    QVERIFY(locals->children[0]->name.contains(QStringLiteral("a.txt")));

    QVERIFY(central);
    QCOMPARE(central->children.size(), size_t(2));
    QVERIFY(central->children[0]->name.contains(QStringLiteral("a.txt")));
    QVERIFY(central->children[1]->name.contains(QStringLiteral("b.txt")));

    StructureDisplayOptions sortedOptions;
    sortedOptions.sortTopLevelRowsByOffset = true;
    auto sortedRows = buildRows(&library, zipRoot, zip, 0, sortedOptions);
    QCOMPARE(sortedRows.size(), size_t(1));
    QCOMPARE(sortedRows[0]->children.size(), size_t(3));
    QVERIFY(sortedRows[0]->children[0]->name.contains(QStringLiteral("localFileHeaders")));
    QVERIFY(sortedRows[0]->children[1]->name.contains(QStringLiteral("centralDirectory")));
    QCOMPARE(sortedRows[0]->children[2]->name, QStringLiteral("ZIP_END_OF_CENTRAL_DIRECTORY_RECORD eocd"));

    QByteArray storedZip;
    auto appendStoredLe16 = [&storedZip](quint16 value) {
        storedZip.append(char(value & 0xff));
        storedZip.append(char((value >> 8) & 0xff));
    };
    auto appendStoredLe32 = [&storedZip](quint32 value) {
        storedZip.append(char(value & 0xff));
        storedZip.append(char((value >> 8) & 0xff));
        storedZip.append(char((value >> 16) & 0xff));
        storedZip.append(char((value >> 24) & 0xff));
    };
    auto appendStoredLocal = [&](const QByteArray &name, qsizetype size) -> quint32 {
        const quint32 offset = static_cast<quint32>(storedZip.size());
        appendStoredLe32(0x04034b50);
        appendStoredLe16(20);
        appendStoredLe16(0);
        appendStoredLe16(0);
        appendStoredLe16(0);
        appendStoredLe16(0);
        appendStoredLe32(0);
        appendStoredLe32(static_cast<quint32>(size));
        appendStoredLe32(static_cast<quint32>(size));
        appendStoredLe16(static_cast<quint16>(name.size()));
        appendStoredLe16(0);
        storedZip.append(name);
        storedZip.append(QByteArray(size, '\0'));
        return offset;
    };
    auto appendStoredCentral = [&](const QByteArray &name, quint32 localOffset, qsizetype size) {
        appendStoredLe32(0x02014b50);
        appendStoredLe16(20);
        appendStoredLe16(20);
        appendStoredLe16(0);
        appendStoredLe16(0);
        appendStoredLe16(0);
        appendStoredLe16(0);
        appendStoredLe32(0);
        appendStoredLe32(static_cast<quint32>(size));
        appendStoredLe32(static_cast<quint32>(size));
        appendStoredLe16(static_cast<quint16>(name.size()));
        appendStoredLe16(0);
        appendStoredLe16(0);
        appendStoredLe16(0);
        appendStoredLe16(0);
        appendStoredLe32(0);
        appendStoredLe32(localOffset);
        storedZip.append(name);
    };

    const quint32 largeOffset = appendStoredLocal("large.bin", 5000);
    const quint32 nextOffset = appendStoredLocal("next.bin", 1);
    const quint32 storedCentralOffset = static_cast<quint32>(storedZip.size());
    appendStoredCentral("large.bin", largeOffset, 5000);
    appendStoredCentral("next.bin", nextOffset, 1);
    const quint32 storedCentralSize = static_cast<quint32>(storedZip.size()) - storedCentralOffset;
    appendStoredLe32(0x06054b50);
    appendStoredLe16(0);
    appendStoredLe16(0);
    appendStoredLe16(2);
    appendStoredLe16(2);
    appendStoredLe32(storedCentralSize);
    appendStoredLe32(storedCentralOffset);
    appendStoredLe16(0);

    auto storedRows = buildRows(&library, zipRoot, storedZip);
    QCOMPARE(storedRows.size(), size_t(1));
    StructureRow *storedLocals = findChildNamed(storedRows[0].get(), QStringLiteral("ZIP_LOCAL_FILE_HEADER localFileHeaders[]"));
    QVERIFY(storedLocals);
    QCOMPARE(storedLocals->children.size(), size_t(2));
    QVERIFY(storedLocals->children[0]->name.contains(QStringLiteral("large.bin")));
    QVERIFY(storedLocals->children[1]->name.contains(QStringLiteral("next.bin")));
}

REGISTER_STRUCTVIEW_TEST(StructViewArchiveTests)
#include "archive_tests.moc"
