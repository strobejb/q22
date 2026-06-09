#include "dialogs/exportformat.h"
#include "dialogs/importformat.h"

#include <QBuffer>
#include <QByteArray>
#include <QTest>

// ── Test helpers ──────────────────────────────────────────────────────────────

// DataSink that collects everything written into a QByteArray.
struct ByteArraySink : DataSink
{
    QByteArray data;

    void write(const uint8_t *buf, size_t len) override
    {
        data.append(reinterpret_cast<const char *>(buf), (int)len);
    }

    // padToAddress has a default no-op — format tests don't exercise addressing
};

// Run one import format function against a text input, return collected bytes.
static QByteArray runImport(size_w (*fn)(ImportReader &, DataSink &, size_w, size_w, IMPEXP_OPTIONS *),
                            const QByteArray &input, IMPEXP_OPTIONS opts = {})
{
    QByteArray buf = input;
    QBuffer    dev(&buf);
    dev.open(QIODevice::ReadOnly);
    ImportReader  reader(&dev);
    ByteArraySink sink;
    fn(reader, sink, 0, 0, &opts);
    return sink.data;
}

class ImportFormatTests : public QObject
{
    Q_OBJECT

  private slots:
    // Codec decode primitives
    void base64_decodeRfcVectors();
    void base64_roundTrip();
    void uuDecode_knownInput();
    void uuDecode_emptyLine();
    void uu_roundTrip();
    void intelToBin_eofRecord();
    void intelToBin_dataRecord();
    void intelToBin_extendedLinearAddress();
    void intelToBin_rejectsInvalid();
    void intel_roundTrip();
    void motorolaToBin_s9Terminator();
    void motorolaToBin_s1DataRecord();
    void motorolaToBin_s3DataRecord();
    void motorolaToBin_rejectsInvalid();
    void motorola_roundTrip();

    // Format parsers
    void importRawHex_basicBytes();
    void importRawHex_withSpaces();
    void importBase64_knownInput();
    void importUUEncode_knownInput();
    void importIntelHex_singleByte();
    void importMotorola_singleByte();
    void importASM_byteMode();
    void importCPP_byteArray();
};

// ── base64_decode ─────────────────────────────────────────────────────────────

void ImportFormatTests::base64_decodeRfcVectors()
{
    uint8_t buf[64];
    auto    decode = [&](const char *s) -> QByteArray
    {
        size_t n = base64_decode(s, strlen(s), buf);
        return QByteArray(reinterpret_cast<const char *>(buf), (int)n);
    };

    QCOMPARE(decode("Zg=="), QByteArray("f"));
    QCOMPARE(decode("Zm8="), QByteArray("fo"));
    QCOMPARE(decode("Zm9v"), QByteArray("foo"));
    QCOMPARE(decode("Zm9vYg=="), QByteArray("foob"));
    QCOMPARE(decode("Zm9vYmE="), QByteArray("fooba"));
    QCOMPARE(decode("Zm9vYmFy"), QByteArray("foobar"));
}

void ImportFormatTests::base64_roundTrip()
{
    QByteArray original;
    original.resize(256);
    for (int i = 0; i < 256; i++)
        original[i] = (char)i;

    char   encoded[512];
    size_t encLen =
        base64_encode(reinterpret_cast<const uint8_t *>(original.constData()), (size_t)original.size(), encoded);

    uint8_t decoded[256];
    size_t  decLen = base64_decode(encoded, encLen, decoded);

    QCOMPARE((int)decLen, original.size());
    QCOMPARE(QByteArray(reinterpret_cast<const char *>(decoded), (int)decLen), original);
}

// ── uu_decode ────────────────────────────────────────────────────────────────

void ImportFormatTests::uuDecode_knownInput()
{
    uint8_t buf[8];
    size_t  n = uu_decode("#0V%T", 5, buf);
    QCOMPARE((int)n, 3);
    QCOMPARE(QByteArray(reinterpret_cast<const char *>(buf), (int)n), QByteArray("Cat"));
}

void ImportFormatTests::uuDecode_emptyLine()
{
    uint8_t buf[8];
    size_t  n = uu_decode(" ", 1, buf);
    QCOMPARE((int)n, 0);
}

void ImportFormatTests::uu_roundTrip()
{
    QByteArray original;
    original.resize(45);
    for (int i = 0; i < 45; i++)
        original[i] = (char)(i * 5);

    char   encoded[128];
    size_t encLen =
        uu_encode(reinterpret_cast<const uint8_t *>(original.constData()), (size_t)original.size(), encoded);

    uint8_t decoded[64];
    size_t  decLen = uu_decode(encoded, encLen, decoded);

    QCOMPARE((int)decLen, original.size());
    QCOMPARE(QByteArray(reinterpret_cast<const char *>(decoded), (int)decLen), original);
}

// ── intel_to_bin ─────────────────────────────────────────────────────────────

void ImportFormatTests::intelToBin_eofRecord()
{
    int           type = -1, count = -1;
    unsigned long addr = 0xdeadbeef;
    uint8_t       data[256];
    QVERIFY(intel_to_bin(":00000001FF", &type, &count, &addr, data));
    QCOMPARE(type, 1);
    QCOMPARE(count, 0);
    QCOMPARE(addr, 0UL);
}

void ImportFormatTests::intelToBin_dataRecord()
{
    int           type = -1, count = -1;
    unsigned long addr = 0xdeadbeef;
    uint8_t       data[256];
    QVERIFY(intel_to_bin(":01000000AA55", &type, &count, &addr, data));
    QCOMPARE(type, 0);
    QCOMPARE(count, 1);
    QCOMPARE(addr, 0UL);
    QCOMPARE(data[0], (uint8_t)0xAA);
}

void ImportFormatTests::intelToBin_extendedLinearAddress()
{
    int           type = -1, count = -1;
    unsigned long addr = 0xdeadbeef;
    uint8_t       data[256];
    QVERIFY(intel_to_bin(":020000040001F9", &type, &count, &addr, data));
    QCOMPARE(type, 4);
    QCOMPARE(count, 2);
    QCOMPARE(addr, 0UL);
    QCOMPARE(data[0], (uint8_t)0x00);
    QCOMPARE(data[1], (uint8_t)0x01);
}

void ImportFormatTests::intelToBin_rejectsInvalid()
{
    int           type, count;
    unsigned long addr;
    uint8_t       data[256];
    QVERIFY(!intel_to_bin("", &type, &count, &addr, data));
    QVERIFY(!intel_to_bin("00000001FF", &type, &count, &addr, data));
    QVERIFY(!intel_to_bin(":0000GG01FF", &type, &count, &addr, data));
}

void ImportFormatTests::intel_roundTrip()
{
    const uint8_t orig[] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
    char          rec[64];
    size_t        recLen = intel_frame(rec, 0, sizeof(orig), 0x1234, orig);
    rec[recLen]          = '\0';

    int           type, count;
    unsigned long addr;
    uint8_t       data[256];
    QVERIFY(intel_to_bin(rec, &type, &count, &addr, data));
    QCOMPARE(type, 0);
    QCOMPARE(count, (int)sizeof(orig));
    QCOMPARE(addr, 0x1234UL);
    QCOMPARE(QByteArray(reinterpret_cast<const char *>(data), count),
             QByteArray(reinterpret_cast<const char *>(orig), sizeof(orig)));
}

// ── motorola_to_bin ───────────────────────────────────────────────────────────

void ImportFormatTests::motorolaToBin_s9Terminator()
{
    int           type = -1, count = -1;
    unsigned long addr = 0xdeadbeef;
    uint8_t       data[256];
    QVERIFY(motorola_to_bin("S9030000FC", &type, &count, &addr, data));
    QCOMPARE(type, 9);
    QCOMPARE(count, 0);
    QCOMPARE(addr, 0UL);
}

void ImportFormatTests::motorolaToBin_s1DataRecord()
{
    int           type = -1, count = -1;
    unsigned long addr = 0xdeadbeef;
    uint8_t       data[256];
    QVERIFY(motorola_to_bin("S1040000AA51", &type, &count, &addr, data));
    QCOMPARE(type, 1);
    QCOMPARE(count, 1);
    QCOMPARE(addr, 0UL);
    QCOMPARE(data[0], (uint8_t)0xAA);
}

void ImportFormatTests::motorolaToBin_s3DataRecord()
{
    int           type = -1, count = -1;
    unsigned long addr = 0xdeadbeef;
    uint8_t       data[256];
    QVERIFY(motorola_to_bin("S30712345678ABCD6C", &type, &count, &addr, data));
    QCOMPARE(type, 3);
    QCOMPARE(count, 2);
    QCOMPARE(addr, 0x12345678UL);
    QCOMPARE(data[0], (uint8_t)0xAB);
    QCOMPARE(data[1], (uint8_t)0xCD);
}

void ImportFormatTests::motorolaToBin_rejectsInvalid()
{
    int           type, count;
    unsigned long addr;
    uint8_t       data[256];
    QVERIFY(!motorola_to_bin("", &type, &count, &addr, data));
    QVERIFY(!motorola_to_bin("S4030000FC", &type, &count, &addr, data));
    QVERIFY(!motorola_to_bin("X9030000FC", &type, &count, &addr, data));
}

void ImportFormatTests::motorola_roundTrip()
{
    const uint8_t orig[] = {0xDE, 0xAD, 0xBE, 0xEF};
    char          rec[64];
    size_t        recLen = motorola_frame(rec, 3, sizeof(orig), 0xCAFEBABEUL, orig);
    rec[recLen]          = '\0';

    int           type, count;
    unsigned long addr;
    uint8_t       data[256];
    QVERIFY(motorola_to_bin(rec, &type, &count, &addr, data));
    QCOMPARE(type, 3);
    QCOMPARE(count, (int)sizeof(orig));
    QCOMPARE(addr, 0xCAFEBABEUL);
    QCOMPARE(QByteArray(reinterpret_cast<const char *>(data), count),
             QByteArray(reinterpret_cast<const char *>(orig), sizeof(orig)));
}

// ── ImportRawHex ─────────────────────────────────────────────────────────────

void ImportFormatTests::importRawHex_basicBytes()
{
    // Contiguous hex digits → raw bytes
    QByteArray out = runImport(ImportRawHex, "414243");
    QCOMPARE(out, QByteArray("ABC"));
}

void ImportFormatTests::importRawHex_withSpaces()
{
    // Spaces between pairs are accepted; partial nibble at space is flushed
    QByteArray out = runImport(ImportRawHex, "41 42 43");
    QCOMPARE(out, QByteArray("ABC"));
}

// ── ImportBase64 ─────────────────────────────────────────────────────────────

void ImportFormatTests::importBase64_knownInput()
{
    // One base64 line (with trailing newline as ImportBase64 uses gets())
    QByteArray out = runImport(ImportBase64, "Zm9vYmFy\n");
    QCOMPARE(out, QByteArray("foobar"));
}

// ── ImportUUEncode ────────────────────────────────────────────────────────────

void ImportFormatTests::importUUEncode_knownInput()
{
    QByteArray input = "begin 666 test.bin\n#0V%T\n`\nend\n";
    QByteArray out   = runImport(ImportUUEncode, input);
    QCOMPARE(out, QByteArray("Cat"));
}

// ── ImportIntelHex ────────────────────────────────────────────────────────────

void ImportFormatTests::importIntelHex_singleByte()
{
    // Extended-address record (ignored for offset 0), one data byte, EOF.
    QByteArray     input = ":020000040000FA\n:01000000AA55\n:00000001FF\n";
    IMPEXP_OPTIONS opts;
    opts.fUseAddress = false; // no padToAddress in ByteArraySink
    QByteArray out   = runImport(ImportIntelHex, input, opts);
    QCOMPARE(out, QByteArray("\xAA", 1));
}

// ── ImportMotorola ────────────────────────────────────────────────────────────

void ImportFormatTests::importMotorola_singleByte()
{
    // S0 header (ignored), S1 data record, S9 terminator.
    QByteArray     input = "S00600004844521B\nS1040000AA51\nS9030000FC\n";
    IMPEXP_OPTIONS opts;
    opts.fUseAddress = false;
    QByteArray out   = runImport(ImportMotorola, input, opts);
    QCOMPARE(out, QByteArray("\xAA", 1));
}

// ── ImportASM ────────────────────────────────────────────────────────────────

void ImportFormatTests::importASM_byteMode()
{
    // Two-byte db line in hex suffix notation
    QByteArray     input = "db 041h 042h \n";
    IMPEXP_OPTIONS opts;
    opts.fBigEndian = false;
    QByteArray out  = runImport(ImportASM, input, opts);
    QCOMPARE(out, QByteArray("\x41\x42", 2));
}

// ── ImportCPP ────────────────────────────────────────────────────────────────

void ImportFormatTests::importCPP_byteArray()
{
    QByteArray     input = "uint8_t data[] = { 0x41, 0x42, 0x43, };";
    IMPEXP_OPTIONS opts;
    opts.fBigEndian = false;
    QByteArray out  = runImport(ImportCPP, input, opts);
    QCOMPARE(out, QByteArray("ABC"));
}

QTEST_APPLESS_MAIN(ImportFormatTests)
#include "importformat_tests.moc"
