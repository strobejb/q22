#include "../structview_testsupport.h"

class StructViewMediaTests : public QObject
{
    Q_OBJECT

private slots:
    void builderRendersMp4Boxes();
    void builderRendersRiffWaveAndWebpChunks();
};

void StructViewMediaTests::builderRendersMp4Boxes()
{
    // Scenario: MP4/ISO BMFF files are big-endian FourCC box streams.
    // Expected: the standard MP4 definition labels boxes by type, decodes ftyp,
    // and keeps media/container payloads bounded.
    StrataLibrary library;
    QVERIFY2(parseStandardDefinition(&library, QStringLiteral("mp4.strata")), "mp4.strata failed to parse");
    TypeDecl *mp4Root = exportedNamed(&library, QStringLiteral("MP4"));
    QVERIFY(mp4Root);

    QByteArray mp4;
    const auto appendBe32 = [&mp4](quint32 value) {
        mp4.append(char((value >> 24) & 0xff));
        mp4.append(char((value >> 16) & 0xff));
        mp4.append(char((value >> 8) & 0xff));
        mp4.append(char(value & 0xff));
    };

    appendBe32(24);
    mp4.append("ftyp", 4);
    mp4.append("isom", 4);
    appendBe32(0x00000200);
    mp4.append("isom", 4);
    mp4.append("mp42", 4);

    appendBe32(116);
    mp4.append("moov", 4);
    appendBe32(108);
    mp4.append("mvhd", 4);
    mp4.append(char(0)); // version
    mp4.append(QByteArray(3, '\0'));
    appendBe32(1);       // creation_time
    appendBe32(2);       // modification_time
    appendBe32(1000);    // timescale
    appendBe32(5000);    // duration
    appendBe32(0x00010000);
    mp4.append(char(1));
    mp4.append(char(0));
    mp4.append(QByteArray(10, '\0'));
    appendBe32(0x00010000);
    appendBe32(0);
    appendBe32(0);
    appendBe32(0);
    appendBe32(0x00010000);
    appendBe32(0);
    appendBe32(0);
    appendBe32(0);
    appendBe32(0x40000000);
    mp4.append(QByteArray(24, '\0'));
    appendBe32(2);

    appendBe32(12);
    mp4.append("mdat", 4);
    mp4.append(QByteArray::fromHex("01020304"));

    auto rows = buildRows(&library, mp4Root, mp4);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->name, QStringLiteral("MP4"));

    StructureRow *boxes = findChildNamed(rows[0].get(), QStringLiteral("MP4_BOX boxes[]"));
    QVERIFY2(boxes, qPrintable(childNames(rows[0].get())));
    QCOMPARE(boxes->children.size(), size_t(3));
    QCOMPARE(boxes->children[0]->name, QStringLiteral("[0]ftyp"));
    QCOMPARE(boxes->children[1]->name, QStringLiteral("[1]moov"));
    QCOMPARE(boxes->children[2]->name, QStringLiteral("[2]mdat"));

    StructureRow *movie = findDescendantNamed(boxes->children[1].get(),
                                               QStringLiteral("struct _MP4_CONTAINER_BOX movie"));
    QVERIFY2(movie, qPrintable(childNames(boxes->children[1].get())));
    StructureRow *movieChildren = findChildNamed(movie, QStringLiteral("struct _MP4_BOX children[]"));
    QVERIFY2(movieChildren, qPrintable(childNames(movie)));
    QCOMPARE(movieChildren->byteLength, uint64_t(108));
    QCOMPARE(movieChildren->children.size(), size_t(1));
    QCOMPARE(movieChildren->children[0]->name, QStringLiteral("[0]mvhd"));
    QCOMPARE(movieChildren->children[0]->absoluteOffset, uint64_t(32));
    QCOMPARE(movieChildren->children[0]->byteLength, uint64_t(108));

    StructureRow *fileType = findDescendantNamed(boxes->children[0].get(), QStringLiteral("MP4_FILE_TYPE_BOX fileType"));
    QVERIFY2(fileType, qPrintable(childNames(boxes->children[0].get())));
    StructureRow *majorBrand = findChildNamed(fileType, QStringLiteral("char majorBrand[]"));
    QVERIFY2(majorBrand, qPrintable(childNames(fileType)));
    QCOMPARE(majorBrand->value, QStringLiteral("\"isom\""));
    StructureRow *compatibleBrands = findChildNamed(fileType, QStringLiteral("char compatibleBrands[][]"));
    QVERIFY2(compatibleBrands, qPrintable(childNames(fileType)));
    QCOMPARE(compatibleBrands->children.size(), size_t(2));

    StructureRow *moviePayload = findDescendantNamed(boxes->children[1].get(), QStringLiteral("MP4_RAW_BOX_PAYLOAD raw"));
    QVERIFY2(moviePayload, qPrintable(childNames(boxes->children[1].get())));
    StructureRow *movieData = findChildNamed(moviePayload, QStringLiteral("byte data[]"));
    QVERIFY2(movieData, qPrintable(childNames(moviePayload)));
    QCOMPARE(movieData->children.size(), size_t(100));

    StructureRow *mediaData = findDescendantNamed(boxes->children[2].get(), QStringLiteral("MP4_MEDIA_DATA_BOX mediaData"));
    QVERIFY2(mediaData, qPrintable(childNames(boxes->children[2].get())));
    StructureRow *data = findChildNamed(mediaData, QStringLiteral("byte data[]"));
    QVERIFY2(data, qPrintable(childNames(mediaData)));
    QCOMPARE(data->value, QStringLiteral("{ 1, 2, 3, 4 }"));
}

void StructViewMediaTests::builderRendersRiffWaveAndWebpChunks()
{
    // Scenario: RIFF-family files share a chunk stream but use their form type
    // and chunk FourCCs to interpret payloads.
    // Expected: WAVE and WebP roots render named chunks, typed known payloads,
    // and RIFF's even-byte payload padding without a synthetic _align field.
    StrataLibrary library;
    QVERIFY2(parseStandardDefinition(&library, QStringLiteral("riff.strata")), "riff.strata failed to parse");

    TypeDecl *waveRoot = exportedNamed(&library, QStringLiteral("WAVE"));
    QVERIFY(waveRoot);
    QByteArray wave(48, '\0');
    wave.replace(0, 4, "RIFF");
    writeLe32(&wave, 4, 40);
    wave.replace(8, 4, "WAVE");
    wave.replace(12, 4, "fmt ");
    writeLe32(&wave, 16, 16);
    writeLe16(&wave, 20, 1);
    writeLe16(&wave, 22, 2);
    writeLe32(&wave, 24, 44100);
    writeLe32(&wave, 28, 176400);
    writeLe16(&wave, 32, 4);
    writeLe16(&wave, 34, 16);
    wave.replace(36, 4, "data");
    writeLe32(&wave, 40, 3);
    wave[44] = char(0x01);
    wave[45] = char(0x02);
    wave[46] = char(0x03);

    auto waveRows = buildRows(&library, waveRoot, wave);
    QCOMPARE(waveRows.size(), size_t(1));
    QCOMPARE(waveRows[0]->name, QStringLiteral("WAVE"));

    StructureRow *waveFormType = findChildNamed(waveRows[0].get(), QStringLiteral("dword formType"));
    QVERIFY2(waveFormType, qPrintable(childNames(waveRows[0].get())));
    QCOMPARE(waveFormType->value, QStringLiteral("\"WAVE\""));

    StructureRow *waveChunks = findChildNamed(waveRows[0].get(), QStringLiteral("RIFF_CHUNK chunks[]"));
    QVERIFY2(waveChunks, qPrintable(childNames(waveRows[0].get())));
    QCOMPARE(waveChunks->children.size(), size_t(2));
    QCOMPARE(waveChunks->children[0]->name, QStringLiteral("[0]fmt "));
    QCOMPARE(waveChunks->children[1]->name, QStringLiteral("[1]data"));

    StructureRow *waveFormat = findDescendantNamed(waveChunks->children[0].get(), QStringLiteral("RIFF_WAVE_FORMAT_CHUNK waveFormat"));
    QVERIFY2(waveFormat, qPrintable(childNames(waveChunks->children[0].get())));
    StructureRow *audioFormat = findChildNamed(waveFormat, QStringLiteral("word audioFormat"));
    QVERIFY2(audioFormat, qPrintable(childNames(waveFormat)));
    QCOMPARE(audioFormat->value, QStringLiteral("RIFF_WAVE_FORMAT_PCM"));
    StructureRow *samplesPerSecond = findChildNamed(waveFormat, QStringLiteral("dword samplesPerSecond"));
    QVERIFY2(samplesPerSecond, qPrintable(childNames(waveFormat)));
    QCOMPARE(samplesPerSecond->value, QStringLiteral("44100"));

    StructureRow *waveDataRaw = findDescendantNamed(waveChunks->children[1].get(), QStringLiteral("RIFF_RAW_CHUNK_PAYLOAD raw"));
    QVERIFY2(waveDataRaw, qPrintable(childNames(waveChunks->children[1].get())));
    StructureRow *waveData = findChildNamed(waveDataRaw, QStringLiteral("byte data[]"));
    QVERIFY2(waveData, qPrintable(childNames(waveDataRaw)));
    QCOMPARE(waveData->value, QStringLiteral("{ 1, 2, 3 }"));

    TypeDecl *webpRoot = exportedNamed(&library, QStringLiteral("WEBP"));
    QVERIFY(webpRoot);
    QByteArray webp(42, '\0');
    webp.replace(0, 4, "RIFF");
    writeLe32(&webp, 4, 34);
    webp.replace(8, 4, "WEBP");
    webp.replace(12, 4, "VP8X");
    writeLe32(&webp, 16, 10);
    webp[20] = char(0x10);
    webp[24] = char(0x3f);
    webp[27] = char(0x1f);
    webp.replace(30, 4, "VP8 ");
    writeLe32(&webp, 34, 4);
    webp.replace(38, 4, QByteArray::fromHex("9d012a00"));

    auto webpRows = buildRows(&library, webpRoot, webp);
    QCOMPARE(webpRows.size(), size_t(1));
    QCOMPARE(webpRows[0]->name, QStringLiteral("WEBP"));

    StructureRow *webpFormType = findChildNamed(webpRows[0].get(), QStringLiteral("dword formType"));
    QVERIFY2(webpFormType, qPrintable(childNames(webpRows[0].get())));
    QCOMPARE(webpFormType->value, QStringLiteral("\"WEBP\""));

    StructureRow *webpChunks = findChildNamed(webpRows[0].get(), QStringLiteral("RIFF_CHUNK chunks[]"));
    QVERIFY2(webpChunks, qPrintable(childNames(webpRows[0].get())));
    QCOMPARE(webpChunks->children.size(), size_t(2));
    QCOMPARE(webpChunks->children[0]->name, QStringLiteral("[0]VP8X"));
    QCOMPARE(webpChunks->children[1]->name, QStringLiteral("[1]VP8 "));

    StructureRow *vp8x = findDescendantNamed(webpChunks->children[0].get(), QStringLiteral("WEBP_VP8X_CHUNK vp8x"));
    QVERIFY2(vp8x, qPrintable(childNames(webpChunks->children[0].get())));
    StructureRow *flags = findChildNamed(vp8x, QStringLiteral("byte flags"));
    QVERIFY2(flags, qPrintable(childNames(vp8x)));
    QCOMPARE(flags->value, QStringLiteral("16"));

    StructureRow *vp8Raw = findDescendantNamed(webpChunks->children[1].get(), QStringLiteral("RIFF_RAW_CHUNK_PAYLOAD raw"));
    QVERIFY2(vp8Raw, qPrintable(childNames(webpChunks->children[1].get())));
    StructureRow *vp8Data = findChildNamed(vp8Raw, QStringLiteral("byte data[]"));
    QVERIFY2(vp8Data, qPrintable(childNames(vp8Raw)));
    QCOMPARE(vp8Data->value, QStringLiteral("{ 157, 1, 42, 0 }"));
}

REGISTER_STRUCTVIEW_TEST(StructViewMediaTests)
#include "media_tests.moc"
