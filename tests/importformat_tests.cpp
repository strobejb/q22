#include "dialogs/exportformat.h"
#include "dialogs/importformat.h"

#include <QByteArray>
#include <QTest>

class ImportFormatTests : public QObject
{
    Q_OBJECT

  private slots:
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
};

// ── base64_decode ─────────────────────────────────────────────────────────────
// RFC 4648 §10 test vectors (inverse of the encode tests).

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
    // Encode then decode — result must equal the original for all byte values.
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
    // "#0V%T" is the UU encoding of "Cat" — length char '#' (=3) + encoded data.
    uint8_t buf[8];
    size_t  n = uu_decode("#0V%T", 5, buf);
    QCOMPARE((int)n, 3);
    QCOMPARE(QByteArray(reinterpret_cast<const char *>(buf), (int)n), QByteArray("Cat"));
}

void ImportFormatTests::uuDecode_emptyLine()
{
    // The UU end-of-data marker is a line containing only '`' (uuetable[0]=space,
    // but the encoder uses space for length-0; the decoder accepts either).
    // A single space character encodes zero bytes.
    uint8_t buf[8];
    size_t  n = uu_decode(" ", 1, buf);
    QCOMPARE((int)n, 0);
}

void ImportFormatTests::uu_roundTrip()
{
    // Encode then decode for a block of bytes (45 bytes = one full UU line).
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
// Parse the same records built by intel_frame in exportformat_tests.

void ImportFormatTests::intelToBin_eofRecord()
{
    // :00000001FF → type=1, count=0, addr=0
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
    // :01000000AA55 → type=0, count=1, addr=0, data=[0xAA]
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
    // :020000040001F9 → type=4, count=2, addr=0, data=[0x00, 0x01]
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
    QVERIFY(!intel_to_bin("", &type, &count, &addr, data));            // empty
    QVERIFY(!intel_to_bin("00000001FF", &type, &count, &addr, data));  // missing ':'
    QVERIFY(!intel_to_bin(":0000GG01FF", &type, &count, &addr, data)); // bad hex
}

void ImportFormatTests::intel_roundTrip()
{
    // Build a data record with intel_frame, parse it back, check round-trip.
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
    // S9030000FC → type=9, count=0, addr=0
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
    // S1040000AA51 → type=1, count=1, addr=0, data=[0xAA]
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
    // S30712345678ABCD6C → type=3, count=2, addr=0x12345678, data=[0xAB, 0xCD]
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
    QVERIFY(!motorola_to_bin("", &type, &count, &addr, data));           // empty
    QVERIFY(!motorola_to_bin("S4030000FC", &type, &count, &addr, data)); // type 4 unsupported
    QVERIFY(!motorola_to_bin("X9030000FC", &type, &count, &addr, data)); // wrong leader
}

void ImportFormatTests::motorola_roundTrip()
{
    // Build an S3 record with motorola_frame, parse it back, check round-trip.
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

QTEST_APPLESS_MAIN(ImportFormatTests)
#include "importformat_tests.moc"
