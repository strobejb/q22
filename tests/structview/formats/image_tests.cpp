#include "../structview_testsupport.h"

class StructViewImageTests : public QObject
{
    Q_OBJECT

private slots:
    void builderRendersPngChunks();
    void builderRendersBmpHeaderAndPixelPayload();
    void builderRendersIcoDirectoryAndImagePayload();
    void builderRendersGifBlocks();
};

void StructViewImageTests::builderRendersPngChunks()
{
    // Scenario: PNG is a big-endian signature plus a stream of length/type/data/CRC chunks.
    // Expected: the standard PNG definition names chunks by type and expands common
    // typed payloads such as IHDR while leaving compressed image data raw.
    StrataLibrary library;
    QVERIFY2(parseStandardDefinition(&library, QStringLiteral("png.strata")), "png.strata failed to parse");
    TypeDecl *pngRoot = exportedNamed(&library, QStringLiteral("PNG"));
    QVERIFY(pngRoot);

    QByteArray png;
    const auto appendBe32 = [&png](quint32 value) {
        png.append(char((value >> 24) & 0xff));
        png.append(char((value >> 16) & 0xff));
        png.append(char((value >> 8) & 0xff));
        png.append(char(value & 0xff));
    };

    png.append(QByteArray::fromHex("89504e470d0a1a0a")); // signature
    appendBe32(13);
    png.append("IHDR", 4);
    appendBe32(1);
    appendBe32(2);
    png.append(char(8));  // bit depth
    png.append(char(2));  // truecolor
    png.append(char(0));  // compression
    png.append(char(0));  // filter
    png.append(char(1));  // Adam7
    appendBe32(0x12345678);
    appendBe32(2);
    png.append("IDAT", 4);
    png.append(QByteArray::fromHex("789c"));
    appendBe32(0x90abcdef);
    appendBe32(0);
    png.append("IEND", 4);
    appendBe32(0xae426082);

    auto rows = buildRows(&library, pngRoot, png);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->name, QStringLiteral("PNG"));

    StructureRow *chunks = findChildNamed(rows[0].get(), QStringLiteral("PNG_CHUNK chunks[]"));
    QVERIFY2(chunks, qPrintable(childNames(rows[0].get())));
    QCOMPARE(chunks->children.size(), size_t(3));
    QCOMPARE(chunks->children[0]->name, QStringLiteral("[0]PNG_CHUNK_IHDR"));
    QCOMPARE(chunks->children[1]->name, QStringLiteral("[1]PNG_CHUNK_IDAT"));
    QCOMPARE(chunks->children[2]->name, QStringLiteral("[2]PNG_CHUNK_IEND"));

    StructureRow *ihdr = findChildNamed(chunks->children[0].get(), QStringLiteral("PNG_IHDR ihdr"));
    QVERIFY2(ihdr, qPrintable(childNames(chunks->children[0].get())));
    QCOMPARE(findChildNamed(ihdr, QStringLiteral("dword width"))->value, QStringLiteral("1"));
    QCOMPARE(findChildNamed(ihdr, QStringLiteral("dword height"))->value, QStringLiteral("2"));
    QCOMPARE(findChildNamed(ihdr, QStringLiteral("byte colorType"))->value, QStringLiteral("PNG_COLOR_TRUECOLOR"));
    QCOMPARE(findChildNamed(ihdr, QStringLiteral("byte interlaceMethod"))->value, QStringLiteral("PNG_INTERLACE_ADAM7"));

    StructureRow *idat = findChildNamed(chunks->children[1].get(), QStringLiteral("PNG_RAW_CHUNK_DATA raw"));
    QVERIFY2(idat, qPrintable(childNames(chunks->children[1].get())));
    QCOMPARE(findChildNamed(idat, QStringLiteral("byte data[]"))->value, QStringLiteral("{ 120, 156 }"));
}

void StructViewImageTests::builderRendersBmpHeaderAndPixelPayload()
{
    // Scenario: BMP is a little-endian file header followed by a DIB header
    // whose first field selects the concrete header variant.
    // Expected: the standard BMP definition expands the selected DIB header and
    // exposes the pixel payload at bfOffBits as dynamic data.
    StrataLibrary library;
    QVERIFY2(parseStandardDefinition(&library, QStringLiteral("bmp.strata")), "bmp.strata failed to parse");
    TypeDecl *bmpRoot = exportedNamed(&library, QStringLiteral("BMP"));
    QVERIFY(bmpRoot);

    QByteArray bmp(58, '\0');
    bmp[0] = 'B';
    bmp[1] = 'M';
    writeLe32(&bmp, 2, quint32(bmp.size()));
    writeLe32(&bmp, 10, 54);
    writeLe32(&bmp, 14, 40); // BITMAPINFOHEADER
    writeLe32(&bmp, 18, 1);  // width
    writeLe32(&bmp, 22, 1);  // height
    writeLe16(&bmp, 26, 1);  // planes
    writeLe16(&bmp, 28, 32); // bit count
    writeLe32(&bmp, 30, 0);  // BI_RGB
    bmp[54] = char(0x11);
    bmp[55] = char(0x22);
    bmp[56] = char(0x33);
    bmp[57] = char(0x44);

    auto rows = buildRows(&library, bmpRoot, bmp);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->name, QStringLiteral("BMP"));

    StructureRow *fileHeader = findChildNamed(rows[0].get(), QStringLiteral("BMP_FILE_HEADER fileHeader"));
    QVERIFY2(fileHeader, qPrintable(childNames(rows[0].get())));
    QCOMPARE(findChildNamed(fileHeader, QStringLiteral("word type"))->value, QStringLiteral("BMP_FILE_BM"));
    QCOMPARE(findChildNamed(fileHeader, QStringLiteral("dword pixelArrayOffset"))->value, QStringLiteral("54"));

    StructureRow *dibHeaderSize = findChildNamed(rows[0].get(), QStringLiteral("dword dibHeaderSize"));
    QVERIFY2(dibHeaderSize, qPrintable(childNames(rows[0].get())));
    QCOMPARE(dibHeaderSize->value, QStringLiteral("BMP_DIB_INFO"));

    StructureRow *infoHeader = findChildNamed(rows[0].get(), QStringLiteral("BMP_INFO_HEADER infoHeader"));
    QVERIFY2(infoHeader, qPrintable(childNames(rows[0].get())));
    QCOMPARE(findChildNamed(infoHeader, QStringLiteral("long width"))->value, QStringLiteral("1"));
    QCOMPARE(findChildNamed(infoHeader, QStringLiteral("word bitCount"))->value, QStringLiteral("32"));
    QCOMPARE(findChildNamed(infoHeader, QStringLiteral("dword compression"))->value, QStringLiteral("BMP_COMPRESSION_RGB"));

    StructureRow *pixelOffset = findChildNamed(fileHeader, QStringLiteral("dword pixelArrayOffset"));
    QVERIFY2(pixelOffset, qPrintable(childNames(fileHeader)));
    StructureRow *pixelData = findChildNamed(pixelOffset, QStringLiteral("BYTE PixelData[]"));
    QVERIFY2(pixelData, qPrintable(childNames(pixelOffset)));
    QCOMPARE(pixelData->offset, QStringLiteral("00000036"));
    QCOMPARE(pixelData->children.size(), size_t(4));
    QCOMPARE(pixelData->children[0]->value, QStringLiteral("17"));
    QCOMPARE(pixelData->children[3]->value, QStringLiteral("68"));
}

void StructViewImageTests::builderRendersIcoDirectoryAndImagePayload()
{
    // Scenario: ICO/CUR files contain a directory of image records whose
    // payloads live elsewhere in the file.
    // Expected: the standard ICO definition renders directory entries and
    // emits each bounded image payload under ICO Summary/Images.
    StrataLibrary library;
    QVERIFY2(parseStandardDefinition(&library, QStringLiteral("ico.strata")), "ico.strata failed to parse");
    TypeDecl *icoRoot = exportedNamed(&library, QStringLiteral("ICO"));
    QVERIFY(icoRoot);

    QByteArray ico(26, '\0');
    writeLe16(&ico, 0, 0);
    writeLe16(&ico, 2, 1);
    writeLe16(&ico, 4, 1);
    ico[6] = char(16); // width
    ico[7] = char(16); // height
    writeLe16(&ico, 10, 1);
    writeLe16(&ico, 12, 32);
    writeLe32(&ico, 14, 4);
    writeLe32(&ico, 18, 22);
    ico[22] = char(0x89);
    ico[23] = 'P';
    ico[24] = 'N';
    ico[25] = 'G';

    auto rows = buildRows(&library, icoRoot, ico);
    StructureRow *icoRow = findTopLevelNamed(rows, QStringLiteral("ICO"));
    QVERIFY(icoRow);
    QCOMPARE(findChildNamed(icoRow, QStringLiteral("word type"))->value, QStringLiteral("ICO_TYPE_ICON"));

    StructureRow *entries = findChildNamed(icoRow, QStringLiteral("ICO_DIRECTORY_ENTRY entries[]"));
    QVERIFY2(entries, qPrintable(childNames(icoRow)));
    QCOMPARE(entries->children.size(), size_t(1));

    StructureRow *entry = entries->children[0].get();
    QCOMPARE(findChildNamed(entry, QStringLiteral("byte width"))->value, QStringLiteral("16"));
    QCOMPARE(findChildNamed(entry, QStringLiteral("dword bytesInResource"))->value, QStringLiteral("4"));
    QCOMPARE(findChildNamed(entry, QStringLiteral("dword imageOffset"))->value, QStringLiteral("22"));

    StructureRow *imageOffset = findChildNamed(entry, QStringLiteral("dword imageOffset"));
    QVERIFY2(imageOffset, qPrintable(childNames(entry)));
    QVERIFY(imageOffset->children.empty());

    StructureRow *semantic = findSemanticRootChildNamed(rows, QStringLiteral("ICO Summary"));
    QVERIFY2(semantic, "ICO Summary semantic child row not found");
    StructureRow *images = findChildNamed(semantic, QStringLiteral("Images"));
    QVERIFY2(images, qPrintable(childNames(semantic)));
    QCOMPARE(images->children.size(), size_t(1));
    StructureRow *imageData = images->children[0].get();
    QCOMPARE(imageData->name, QStringLiteral("22"));
    QCOMPARE(imageData->offset, QStringLiteral("00000016"));
    QCOMPARE(imageData->children.size(), size_t(4));
    QCOMPARE(imageData->children[0]->value, QStringLiteral("137"));
    QCOMPARE(imageData->children[3]->value, QStringLiteral("71"));
}

void StructViewImageTests::builderRendersGifBlocks()
{
    // Scenario: GIF is a little-endian header, logical screen descriptor,
    // optional color table, and a sentinel-tagged stream of extensions/images.
    // Expected: the standard GIF definition expands color tables, extension
    // blocks, image descriptors, data sub-blocks, and the trailer.
    StrataLibrary library;
    QVERIFY2(parseStandardDefinition(&library, QStringLiteral("gif.strata")), "gif.strata failed to parse");
    TypeDecl *gifRoot = exportedNamed(&library, QStringLiteral("GIF"));
    QVERIFY(gifRoot);

    QByteArray gif;
    gif.append("GIF89a", 6);
    gif.append(char(1));
    gif.append(char(0));
    gif.append(char(1));
    gif.append(char(0));
    gif.append(char(0x80)); // global color table present, 2 entries
    gif.append(char(0));
    gif.append(char(0));
    gif.append(QByteArray::fromHex("000000ffffff"));
    gif.append(char(0x21)); // extension
    gif.append(char(0xf9)); // graphic control
    gif.append(char(4));
    gif.append(char(1));
    gif.append(char(5));
    gif.append(char(0));
    gif.append(char(0));
    gif.append(char(0));
    gif.append(char(0x2c)); // image
    gif.append(char(0));
    gif.append(char(0));
    gif.append(char(0));
    gif.append(char(0));
    gif.append(char(1));
    gif.append(char(0));
    gif.append(char(1));
    gif.append(char(0));
    gif.append(char(0)); // no local color table
    gif.append(char(2)); // LZW minimum code size
    gif.append(char(2));
    gif.append(char(0x4c));
    gif.append(char(0x01));
    gif.append(char(0)); // sub-block terminator
    gif.append(char(0x3b)); // trailer

    auto rows = buildRows(&library, gifRoot, gif);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->name, QStringLiteral("GIF"));

    StructureRow *header = findChildNamed(rows[0].get(), QStringLiteral("GIF_HEADER header"));
    QVERIFY2(header, qPrintable(childNames(rows[0].get())));
    StructureRow *gifSignature = findChildNamed(header, QStringLiteral("char signature[]"));
    QVERIFY2(gifSignature, qPrintable(childNames(header)));
    QCOMPARE(gifSignature->value, QStringLiteral("\"GIF\""));
    StructureRow *gifVersion = findChildNamed(header, QStringLiteral("char version[]"));
    QVERIFY2(gifVersion, qPrintable(childNames(header)));
    QCOMPARE(gifVersion->value, QStringLiteral("\"89a\""));

    StructureRow *globalColorTable = findChildNamed(rows[0].get(), QStringLiteral("GIF_COLOR_TABLE_ENTRY globalColorTable[]"));
    QVERIFY2(globalColorTable, qPrintable(childNames(rows[0].get())));
    QCOMPARE(globalColorTable->children.size(), size_t(2));

    StructureRow *blocks = findChildNamed(rows[0].get(), QStringLiteral("GIF_BLOCK blocks[]"));
    QVERIFY2(blocks, qPrintable(childNames(rows[0].get())));
    QCOMPARE(blocks->children.size(), size_t(2));
    QCOMPARE(blocks->children[0]->name, QStringLiteral("[0]"));
    StructureRow *extensionBlockType = findChildNamed(blocks->children[0].get(), QStringLiteral("byte blockType"));
    QVERIFY2(extensionBlockType, qPrintable(childNames(blocks->children[0].get())));
    QCOMPARE(extensionBlockType->value, QStringLiteral("GIF_BLOCK_EXTENSION"));
    StructureRow *extensionLabel = findDescendantNamed(blocks->children[0].get(), QStringLiteral("byte label"));
    QVERIFY2(extensionLabel, qPrintable(childNames(blocks->children[0].get())));
    QCOMPARE(extensionLabel->value, QStringLiteral("GIF_EXTENSION_GRAPHIC_CONTROL"));

    StructureRow *image = findDescendantNamed(blocks->children[1].get(), QStringLiteral("GIF_IMAGE_DESCRIPTOR image"));
    QVERIFY2(image, qPrintable(childNames(blocks->children[1].get())));
    StructureRow *lzwMinimumCodeSize = findChildNamed(image, QStringLiteral("byte lzwMinimumCodeSize"));
    QVERIFY2(lzwMinimumCodeSize, qPrintable(childNames(image)));
    QCOMPARE(lzwMinimumCodeSize->value, QStringLiteral("2"));
    StructureRow *subBlocks = findDescendantNamed(image, QStringLiteral("GIF_DATA_SUB_BLOCK blocks[]"));
    QVERIFY2(subBlocks, qPrintable(childNames(image)));
    QCOMPARE(subBlocks->children.size(), size_t(2));
    StructureRow *imageData = findChildNamed(subBlocks->children[0].get(), QStringLiteral("byte data[]"));
    QVERIFY2(imageData, qPrintable(childNames(subBlocks->children[0].get())));
    QCOMPARE(imageData->value, QStringLiteral("{ 76, 1 }"));
    QCOMPARE(findChildNamed(subBlocks->children[1].get(), QStringLiteral("byte size"))->value,
             QStringLiteral("0"));
    StructureRow *trailer = findChildNamed(rows[0].get(), QStringLiteral("byte trailer"));
    QVERIFY2(trailer, qPrintable(childNames(rows[0].get())));
    QCOMPARE(trailer->value, QStringLiteral("GIF_BLOCK_TRAILER"));
}

REGISTER_STRUCTVIEW_TEST(StructViewImageTests)
#include "image_tests.moc"
