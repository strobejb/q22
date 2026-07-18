#include <QtTest>

#include <capstone/capstone.h>

class DisasmTests : public QObject
{
    Q_OBJECT

private slots:
    void capstoneDecodesWebAssemblyInstructions();
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

QTEST_APPLESS_MAIN(DisasmTests)
#include "disasm_tests.moc"
