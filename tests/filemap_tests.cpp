#include "filemap/filemapbuilder.h"
#include "structview/strataroot.h"
#include "structview/structuredefinitionmanager.h"

#include <QDir>
#include <QRandomGenerator>
#include <QtTest>

#include <cstring>

class QMenu;
void themeMenu(QMenu *) {}

namespace
{

void writeU16(QByteArray &data, int offset, quint16 value)
{
    data[offset + 0] = static_cast<char>(value & 0xFF);
    data[offset + 1] = static_cast<char>((value >> 8) & 0xFF);
}

void writeU32(QByteArray &data, int offset, quint32 value)
{
    for (int i = 0; i < 4; ++i)
        data[offset + i] = static_cast<char>((value >> (8 * i)) & 0xFF);
}

void writeU32BE(QByteArray &data, int offset, quint32 value)
{
    for (int i = 0; i < 4; ++i)
        data[offset + i] = static_cast<char>((value >> (8 * (3 - i))) & 0xFF);
}

QByteArray randomBytes(int length)
{
    QByteArray bytes(length, Qt::Uninitialized);
    QRandomGenerator generator(22);
    for (char &byte : bytes)
        byte = static_cast<char>(generator.bounded(256));
    return bytes;
}

// PE32+ with .text (holding the entry point) and an all-zero .data, followed
// by 1 KiB of high-entropy overlay. The DOS stub (0x40-0x80) is never parsed.
QByteArray peWithOverlay()
{
    QByteArray data(0xA00, '\0');
    data[0] = 'M';
    data[1] = 'Z';
    writeU32(data, 0x3C, 0x80);

    const int nt = 0x80;
    data[nt + 0] = 'P';
    data[nt + 1] = 'E';
    writeU16(data, nt + 4, 0x8664);
    writeU16(data, nt + 6, 2);
    writeU16(data, nt + 20, 0xF0);

    const int opt = nt + 24;
    writeU16(data, opt, 0x20B);
    writeU32(data, opt + 0x10, 0x1000); // AddressOfEntryPoint
    writeU32(data, opt + 0x6C, 16);     // NumberOfRvaAndSizes

    const int section = opt + 0xF0;
    memcpy(data.data() + section, ".text", 5);
    writeU32(data, section + 8, 0x400);
    writeU32(data, section + 12, 0x1000);
    writeU32(data, section + 16, 0x400);
    writeU32(data, section + 20, 0x400);
    writeU32(data, section + 36, 0x60000020);

    memcpy(data.data() + section + 40, ".data", 5);
    writeU32(data, section + 40 + 8, 0x200);
    writeU32(data, section + 40 + 12, 0x2000);
    writeU32(data, section + 40 + 16, 0x200);
    writeU32(data, section + 40 + 20, 0x800);
    writeU32(data, section + 40 + 36, 0xC0000040);

    const QByteArray prologue = QByteArray::fromHex("554889e54883ec20e8000000004889c75dc3");
    for (int offset = 0x400; offset + prologue.size() <= 0x800; offset += prologue.size())
        memcpy(data.data() + offset, prologue.constData(), size_t(prologue.size()));

    data.append(randomBytes(0x400));
    return data;
}

QByteArray pngChunk(const char type[4], const QByteArray &payload)
{
    QByteArray chunk(8, '\0');
    writeU32BE(chunk, 0, quint32(payload.size()));
    memcpy(chunk.data() + 4, type, 4);
    chunk.append(payload);
    chunk.append(4, '\0'); // CRC is not validated by the map
    return chunk;
}

QByteArray pngWithLargeIdat()
{
    QByteArray png = QByteArray::fromHex("89504e470d0a1a0a");
    QByteArray ihdr(13, '\0');
    writeU32BE(ihdr, 0, 64);
    writeU32BE(ihdr, 4, 64);
    ihdr[8] = 8;
    ihdr[9] = 6;
    png.append(pngChunk("IHDR", ihdr));
    png.append(pngChunk("IDAT", randomBytes(8192)));
    png.append(pngChunk("IEND", QByteArray()));
    return png;
}

QByteArray minimalNamedWasmFunction()
{
    QByteArray wasm;
    wasm.append(QByteArray::fromHex("0061736d01000000"));
    wasm.append(QByteArray::fromHex("0010"));
    wasm.append(char(0x04));
    wasm.append("name", 4);
    wasm.append(QByteArray::fromHex("0109"));
    wasm.append(QByteArray::fromHex("010006"));
    wasm.append("answer", 6);
    wasm.append(QByteArray::fromHex("0105016000017f"));
    wasm.append(QByteArray::fromHex("03020100"));
    wasm.append(QByteArray::fromHex("070a0106616e737765720000"));
    wasm.append(QByteArray::fromHex("0a06010400412a0b")); // body: i32.const 42; end
    return wasm;
}

filemap::ByteReader byteArrayReader(const QByteArray &data)
{
    return [&data](uint64_t offset, uint8_t *buf, size_t len) -> size_t {
        if (offset >= uint64_t(data.size()))
            return 0;
        const size_t copied = std::min<size_t>(len, size_t(uint64_t(data.size()) - offset));
        memcpy(buf, data.constData() + offset, copied);
        return copied;
    };
}

const filemap::Region *regionAt(const filemap::FileMap &map, uint64_t offset)
{
    for (const filemap::Region &region : map.content)
        if (region.start <= offset && offset < region.end)
            return &region;
    return nullptr;
}

void verifyTiling(const filemap::FileMap &map)
{
    uint64_t cursor = 0;
    for (const filemap::Region &region : map.content)
    {
        QCOMPARE(region.start, cursor);
        QVERIFY(region.end > region.start);
        cursor = region.end;
    }
    QCOMPARE(cursor, map.fileSize);
}

} // namespace

class FileMapTests : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void peSectionsGapsAndOverlay();
    void pngChunksAreRecordSequence();
    void wasmCodeTargetIsCode();
    void unknownFormatIsRefinedGap();
    void strataRootSelectionOrder();

private:
    filemap::FileMap build(const QString &fileName, const QByteArray &data);

    StructureDefinitionManager m_definitions;
};

void FileMapTests::initTestCase()
{
    m_definitions.setBuiltinStructDirsForTests({QStringLiteral(CAUSEWAY_TEST_DATA_DIR)});
    m_definitions.setUserStrataDirForTests(QDir::temp().filePath(QStringLiteral("q22-filemap-tests-strata")));
    m_definitions.ensureLoaded();
    QVERIFY(m_definitions.library());
}

filemap::FileMap FileMapTests::build(const QString &fileName, const QByteArray &data)
{
    return filemap::buildFileMap(m_definitions, fileName, uint64_t(data.size()), byteArrayReader(data));
}

void FileMapTests::peSectionsGapsAndOverlay()
{
    const QByteArray pe = peWithOverlay();
    const filemap::FileMap map = build(QStringLiteral("sample.exe"), pe);
    verifyTiling(map);
    QCOMPARE(map.rootName, QStringLiteral("PE"));

    // Headers are described exactly.
    QCOMPARE(regionAt(map, 0x00)->kind, filemap::RegionKind::Metadata);
    QCOMPARE(regionAt(map, 0x00)->source, filemap::RegionSource::Structure);

    // The DOS stub is never parsed, so it is a gap -- not "part of the root".
    QCOMPARE(regionAt(map, 0x40)->source, filemap::RegionSource::Gap);

    // The section table is an array of payload-free records.
    const filemap::Region *table = regionAt(map, 0x188);
    QCOMPARE(table->kind, filemap::RegionKind::Table);

    // .text holds the entry point, so it is (heuristically) code.
    const filemap::Region *text = regionAt(map, 0x500);
    QCOMPARE(text->kind, filemap::RegionKind::Code);
    QCOMPARE(text->source, filemap::RegionSource::Heuristic);
    QCOMPARE(text->start, uint64_t(0x400));
    QCOMPARE(text->end, uint64_t(0x800));

    // .data is all zeros.
    QCOMPARE(regionAt(map, 0x900)->kind, filemap::RegionKind::Padding);

    // Bytes after the last described section are an overlay, classified by content.
    const filemap::Region *overlay = regionAt(map, 0xA00);
    QCOMPARE(overlay->source, filemap::RegionSource::Overlay);
    QCOMPARE(overlay->kind, filemap::RegionKind::Compressed);

    QCOMPARE(map.markers.size(), 1);
    QCOMPARE(map.markers.first().offset, uint64_t(0x400));
}

void FileMapTests::pngChunksAreRecordSequence()
{
    const QByteArray png = pngWithLargeIdat();
    const filemap::FileMap map = build(QStringLiteral("image.png"), png);
    verifyTiling(map);

    // Chunks carry payloads, so they appear as individual structure entries.
    QStringList labels;
    for (const filemap::Section &section : map.structure)
        labels << section.label;
    QCOMPARE(labels.size(), 4);
    QVERIFY(labels[2].contains(QStringLiteral("IDAT")));

    // The opaque IDAT payload is high entropy; its chunk header is metadata.
    const uint64_t idatPayload = 8 + 25 + 8;
    QCOMPARE(regionAt(map, idatPayload)->kind, filemap::RegionKind::Compressed);
    QCOMPARE(regionAt(map, idatPayload)->source, filemap::RegionSource::Heuristic);
    QCOMPARE(regionAt(map, idatPayload - 1)->kind, filemap::RegionKind::Metadata);
}

void FileMapTests::wasmCodeTargetIsCode()
{
    const QByteArray wasm = minimalNamedWasmFunction();
    const filemap::FileMap map = build(QStringLiteral("answer.wasm"), wasm);
    verifyTiling(map);

    // [code] on the function body gives an exact range: i32.const 42; end.
    const filemap::Region *code = regionAt(map, uint64_t(wasm.size() - 1));
    QCOMPARE(code->kind, filemap::RegionKind::Code);
    QCOMPARE(code->source, filemap::RegionSource::Structure);
    QCOMPARE(code->end - code->start, uint64_t(3));
}

void FileMapTests::unknownFormatIsRefinedGap()
{
    QByteArray data(8192, '\0');
    data.append(QByteArray("plain text that no definition describes\n").repeated(100));
    const filemap::FileMap map = build(QStringLiteral("blob.unknown-ext"), data);
    verifyTiling(map);

    QVERIFY(map.rootName.isEmpty());
    QCOMPARE(regionAt(map, 0)->kind, filemap::RegionKind::Padding);
    QCOMPARE(regionAt(map, 0)->source, filemap::RegionSource::Gap);
    QCOMPARE(regionAt(map, 8192)->kind, filemap::RegionKind::Text);
}

// Structure View, code discovery and the file map all pick roots through
// strataroot.h; this pins down the precedence they share.
void FileMapTests::strataRootSelectionOrder()
{
    auto assoc = [](const QString &extension) {
        ExportedStructureType type;
        type.assocExtensions = {extension};
        return type;
    };
    ExportedStructureType mz;
    mz.magicSignatures = {StructureMagicSignature{0, QByteArray("MZ")}};
    const QList<ExportedStructureType> types{assoc(QStringLiteral("tar")), assoc(QStringLiteral(".gz")),
                                             mz, assoc(QStringLiteral("exe"))};

    QByteArray data("MZ\x90\x00", 4);
    const StrataRootByteReader reader = [&data](uint64_t offset, uint8_t *buf, size_t len) -> size_t {
        if (offset >= uint64_t(data.size()))
            return 0;
        const size_t got = std::min(len, size_t(data.size() - offset));
        std::memcpy(buf, data.constData() + offset, got);
        return got;
    };

    // Exact suffix beats compound; compound matching ignores case.
    QCOMPARE(associatedStrataRootIndex(types, QStringLiteral("x.tar.gz"), reader), 1);
    QCOMPARE(associatedStrataRootIndex(types, QStringLiteral("x.TAR.bz2"), reader), 0);
    // Any name match beats magic; magic is the fallback.
    QCOMPARE(associatedStrataRootIndex(types, QStringLiteral("prog.exe"), reader), 3);
    QCOMPARE(associatedStrataRootIndex(types, QStringLiteral("prog"), reader), 2);
    QCOMPARE(strataRootIndexForFileName(types, QStringLiteral("prog")), -1);

    // A short read never matches.
    data.truncate(1);
    QCOMPARE(associatedStrataRootIndex(types, QStringLiteral("prog"), reader), -1);
    QVERIFY(!associatedStrataRoot(types, QStringLiteral("prog"), reader));

    uint64_t offset = 99;
    QVERIFY(!strataRootExplicitOffset(nullptr, &offset));
    QCOMPARE(strataRootBaseOffset(nullptr), uint64_t(0));
}

QTEST_MAIN(FileMapTests)
#include "filemap_tests.moc"
