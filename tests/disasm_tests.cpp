#include "HexView/hexview.h"
#include "disasm/codediscovery.h"
#include "disasm/pemetadata.h"

#include <QEventLoop>
#include <QDir>
#include <QTemporaryFile>
#include <QTimer>
#include <QtTest>

#include <capstone/capstone.h>

#include <cstring>
#include <utility>

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
    data[offset + 0] = static_cast<char>(value & 0xFF);
    data[offset + 1] = static_cast<char>((value >> 8) & 0xFF);
    data[offset + 2] = static_cast<char>((value >> 16) & 0xFF);
    data[offset + 3] = static_cast<char>((value >> 24) & 0xFF);
}

QByteArray minimalPe64WithRetEntry()
{
    QByteArray data(0x400, '\0');
    data[0] = 'M';
    data[1] = 'Z';
    writeU32(data, 0x3C, 0x80);

    const int nt = 0x80;
    data[nt + 0] = 'P';
    data[nt + 1] = 'E';
    writeU16(data, nt + 4, 0x8664);
    writeU16(data, nt + 6, 1);
    writeU16(data, nt + 20, 0xF0);

    const int opt = nt + 24;
    writeU16(data, opt, 0x20B);
    writeU32(data, opt + 0x10, 0x1000);

    const int section = opt + 0xF0;
    memcpy(data.data() + section, ".text", 5);
    writeU32(data, section + 8, 0x10);
    writeU32(data, section + 12, 0x1000);
    writeU32(data, section + 16, 0x200);
    writeU32(data, section + 20, 0x200);
    writeU32(data, section + 36, 0x60000020);

    data[0x200] = static_cast<char>(0xC3);
    return data;
}

QByteArray pe32DllWithLargeTextSectionRetEntry()
{
    const int entryFileOffset = 0x36a0b5;
    QByteArray data(entryFileOffset + 1, '\0');
    data[0] = 'M';
    data[1] = 'Z';
    writeU32(data, 0x3C, 0x80);

    const int nt = 0x80;
    data[nt + 0] = 'P';
    data[nt + 1] = 'E';
    writeU16(data, nt + 4, 0x014c);
    writeU16(data, nt + 6, 4);
    writeU16(data, nt + 20, 0xE0);
    writeU16(data, nt + 22, 0x2102);

    const int opt = nt + 24;
    writeU16(data, opt, 0x10B);
    writeU32(data, opt + 0x10, 0x36acb5);
    writeU32(data, opt + 0x1c, 0x10000000);
    writeU32(data, opt + 0x5c, 16);

    const int section = opt + 0xE0;
    memcpy(data.data() + section, ".text", 5);
    writeU32(data, section + 8, 0x3e4def);
    writeU32(data, section + 12, 0x1000);
    writeU32(data, section + 16, 0x3e4e00);
    writeU32(data, section + 20, 0x400);
    writeU32(data, section + 36, 0x60000020);

    data[entryFileOffset] = static_cast<char>(0xC3);
    return data;
}

PeByteReader byteArrayReader(const QByteArray &data)
{
    return [&data](uint64_t offset, uint8_t *buf, size_t len) -> size_t {
        if (!buf || offset >= static_cast<uint64_t>(data.size()))
            return 0;
        const size_t available = static_cast<size_t>(static_cast<uint64_t>(data.size()) - offset);
        const size_t copied = qMin(len, available);
        memcpy(buf, data.constData() + offset, copied);
        return copied;
    };
}

} // namespace

class DisasmTests : public QObject
{
    Q_OBJECT

private slots:
    void capstoneDecodesWebAssemblyInstructions();
    void codeDiscoveryReadsInMemoryLogicalDocument();
    void peMetadataMapsPe32DllEntrypointRva();
    void peMetadataReadsOpenedPe32DllEntrypointRva();
    void codeDiscoveryMapsPe32DllEntryPoint();
    void codeDiscoveryMapsOpenedPe32DllEntryPoint();
};

void DisasmTests::capstoneDecodesWebAssemblyInstructions()
{
    QVERIFY2(cs_support(CS_ARCH_WASM), "q22 must build Capstone with WebAssembly support");

    csh handle = 0;
    QCOMPARE(cs_open(CS_ARCH_WASM, static_cast<cs_mode>(0), &handle), CS_ERR_OK);

    const uint8_t code[] = {0x41, 0x2a, 0x0b}; // i32.const 42; end
    cs_insn *instructions = nullptr;
    const size_t count = cs_disasm(handle, code, sizeof(code), 0x24, 0, &instructions);

    QCOMPARE(count, size_t(2));
    QCOMPARE(QByteArray(instructions[0].mnemonic), QByteArray("i32.const"));
    QCOMPARE(QByteArray(instructions[0].op_str), QByteArray("0x2a"));
    QCOMPARE(instructions[0].address, uint64_t(0x24));
    QCOMPARE(instructions[0].size, uint16_t(2));
    QCOMPARE(QByteArray(instructions[1].mnemonic), QByteArray("end"));

    cs_free(instructions, count);
    cs_close(&handle);
}

void DisasmTests::codeDiscoveryReadsInMemoryLogicalDocument()
{
    const QByteArray data = minimalPe64WithRetEntry();
    HexView hv;
    QVERIFY(hv.initBuf(reinterpret_cast<const uint8_t *>(data.constData()),
                       static_cast<size_t>(data.size()),
                       true,
                       true));

    CodeDiscoveryEngine engine;
    QList<DiscoveredFunction> functions;
    bool finished = false;
    QEventLoop loop;
    QObject::connect(&engine, &CodeDiscoveryEngine::finished,
                     &loop,
                     [&](QList<DiscoveredFunction> discovered)
                     {
                         functions = std::move(discovered);
                         finished = true;
                         loop.quit();
                     });
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);

    engine.scan(&hv);
    loop.exec();

    QVERIFY(finished);
    QCOMPARE(functions.size(), 1);
    QCOMPARE(functions[0].startOffset, uint64_t(0x200));
    QCOMPARE(functions[0].endOffset, uint64_t(0x201));
    QCOMPARE(functions[0].source, FunctionSource::EntryPoint);
    QCOMPARE(functions[0].name, QStringLiteral("entrypoint"));
}

void DisasmTests::peMetadataMapsPe32DllEntrypointRva()
{
    const QByteArray data = pe32DllWithLargeTextSectionRetEntry();
    const PeMetadata pe = readPeMetadata(byteArrayReader(data), static_cast<uint64_t>(data.size()));

    QVERIFY(pe.isValid);
    QVERIFY(!pe.is64Bit);
    QCOMPARE(pe.entryPointRva, uint64_t(0x36acb5));
    QCOMPARE(rvaToFileOffset(pe, pe.entryPointRva), std::optional<uint64_t>(0x36a0b5));
}

void DisasmTests::peMetadataReadsOpenedPe32DllEntrypointRva()
{
    const QByteArray data = pe32DllWithLargeTextSectionRetEntry();

    QTemporaryFile file;
    file.setFileTemplate(QDir::tempPath() + QStringLiteral("/q22-pe32-entrypoint-XXXXXX.dll"));
    QVERIFY(file.open());
    QCOMPARE(file.write(data), qint64(data.size()));
    file.close();

    HexView hv;
    QVERIFY(hv.openFile(file.fileName()));

    const PeMetadata pe = readPeMetadata(&hv);
    QVERIFY(pe.isValid);
    QVERIFY(!pe.is64Bit);
    QCOMPARE(pe.entryPointRva, uint64_t(0x36acb5));
    QCOMPARE(rvaToFileOffset(pe, pe.entryPointRva), std::optional<uint64_t>(0x36a0b5));
}

void DisasmTests::codeDiscoveryMapsPe32DllEntryPoint()
{
    const QByteArray data = pe32DllWithLargeTextSectionRetEntry();
    HexView hv;
    QVERIFY(hv.initBuf(reinterpret_cast<const uint8_t *>(data.constData()),
                       static_cast<size_t>(data.size()),
                       true,
                       true));

    CodeDiscoveryEngine engine;
    QList<DiscoveredFunction> functions;
    bool finished = false;
    QEventLoop loop;
    QObject::connect(&engine, &CodeDiscoveryEngine::finished,
                     &loop,
                     [&](QList<DiscoveredFunction> discovered)
                     {
                         functions = std::move(discovered);
                         finished = true;
                         loop.quit();
                     });
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);

    engine.scan(&hv);
    loop.exec();

    QVERIFY(finished);
    QCOMPARE(functions.size(), 1);
    QCOMPARE(functions[0].startOffset, uint64_t(0x36a0b5));
    QCOMPARE(functions[0].endOffset, uint64_t(0x36a0b6));
    QCOMPARE(functions[0].source, FunctionSource::EntryPoint);
    QCOMPARE(functions[0].name, QStringLiteral("entrypoint"));
}

void DisasmTests::codeDiscoveryMapsOpenedPe32DllEntryPoint()
{
    const QByteArray data = pe32DllWithLargeTextSectionRetEntry();

    QTemporaryFile file;
    file.setFileTemplate(QDir::tempPath() + QStringLiteral("/q22-pe32-entrypoint-XXXXXX.dll"));
    QVERIFY(file.open());
    QCOMPARE(file.write(data), qint64(data.size()));
    file.close();

    HexView hv;
    QVERIFY(hv.openFile(file.fileName()));

    CodeDiscoveryEngine engine;
    QList<DiscoveredFunction> functions;
    bool finished = false;
    QEventLoop loop;
    QObject::connect(&engine, &CodeDiscoveryEngine::finished,
                     &loop,
                     [&](QList<DiscoveredFunction> discovered)
                     {
                         functions = std::move(discovered);
                         finished = true;
                         loop.quit();
                     });
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);

    engine.scan(&hv);
    loop.exec();

    QVERIFY(finished);
    QCOMPARE(functions.size(), 1);
    QCOMPARE(functions[0].startOffset, uint64_t(0x36a0b5));
    QCOMPARE(functions[0].endOffset, uint64_t(0x36a0b6));
    QCOMPARE(functions[0].source, FunctionSource::EntryPoint);
    QCOMPARE(functions[0].name, QStringLiteral("entrypoint"));
}

QTEST_MAIN(DisasmTests)
#include "disasm_tests.moc"
