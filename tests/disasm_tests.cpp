#include "HexView/hexview.h"
#include "disasm/codediscovery.h"

#include <QEventLoop>
#include <QTimer>
#include <QtTest>

#include <capstone/capstone.h>

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

} // namespace

class DisasmTests : public QObject
{
    Q_OBJECT

private slots:
    void capstoneDecodesWebAssemblyInstructions();
    void codeDiscoveryReadsInMemoryLogicalDocument();
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
}

QTEST_MAIN(DisasmTests)
#include "disasm_tests.moc"
