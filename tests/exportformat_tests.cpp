#include "dialogs/exportformat.h"

#include <QByteArray>
#include <QTest>

class ExportFormatTests : public QObject
{
    Q_OBJECT

  private slots:
    void toAscii_printablePassThrough();
    void toAscii_nonPrintableBecomeDot();

    void base64_rfcVectors();
    void base64_emptyInput();

    void uuEncode_emptyInput();
    void uuEncode_knownInput();

    void intelFrame_eofRecord();
    void intelFrame_dataRecord();
    void intelFrame_extendedLinearAddress();

    void motorolaFrame_s9Terminator();
    void motorolaFrame_s1DataRecord();
    void motorolaFrame_s3DataRecord();
    void motorolaFrame_invalidType();
};

// ── toAscii ──────────────────────────────────────────────────────────────────

void ExportFormatTests::toAscii_printablePassThrough()
{
    QCOMPARE(toAscii(0x20), ' '); // space: lowest printable
    QCOMPARE(toAscii(0x41), 'A');
    QCOMPARE(toAscii(0x7E), '~'); // tilde: highest printable
}

void ExportFormatTests::toAscii_nonPrintableBecomeDot()
{
    QCOMPARE(toAscii(0x00), '.');
    QCOMPARE(toAscii(0x1F), '.');
    QCOMPARE(toAscii(0x7F), '.'); // DEL
    QCOMPARE(toAscii(0xFF), '.');
}

// ── base64_encode ─────────────────────────────────────────────────────────────
// RFC 4648 §10 test vectors.

void ExportFormatTests::base64_rfcVectors()
{
    char   buf[64];
    size_t n;
    auto   encode = [&](const char *s) -> QByteArray
    {
        n = base64_encode(reinterpret_cast<const uint8_t *>(s), strlen(s), buf);
        return QByteArray(buf, (int)n);
    };

    QCOMPARE(encode("f"), QByteArray("Zg=="));
    QCOMPARE(encode("fo"), QByteArray("Zm8="));
    QCOMPARE(encode("foo"), QByteArray("Zm9v"));
    QCOMPARE(encode("foob"), QByteArray("Zm9vYg=="));
    QCOMPARE(encode("fooba"), QByteArray("Zm9vYmE="));
    QCOMPARE(encode("foobar"), QByteArray("Zm9vYmFy"));
}

void ExportFormatTests::base64_emptyInput()
{
    char   buf[4] = {};
    size_t n      = base64_encode(nullptr, 0, buf);
    QCOMPARE(n, size_t(0));
}

// ── uu_encode ────────────────────────────────────────────────────────────────

void ExportFormatTests::uuEncode_emptyInput()
{
    // Zero-length input: output is just the UU length char for 0, which is ' ' (0x20).
    char   buf[4] = {};
    size_t n      = uu_encode(nullptr, 0, buf);
    QCOMPARE(n, size_t(1));
    QCOMPARE(buf[0], ' ');
}

void ExportFormatTests::uuEncode_knownInput()
{
    // "Cat" (classic UU example): length char '#' (uuetable[3]) + "0V%T"
    // C=0x43 a=0x61 t=0x74
    //   outbuf[0] = uuetable[0x43>>2]              = uuetable[16] = '0'
    //   outbuf[1] = uuetable[(3<<4)|(0x61>>4)]     = uuetable[54] = 'V'
    //   outbuf[2] = uuetable[(1<<2)|(0x74>>6)]     = uuetable[5]  = '%'
    //   outbuf[3] = uuetable[0x74 & 0x3f]          = uuetable[52] = 'T'
    const uint8_t input[] = {'C', 'a', 't'};
    char          buf[8]  = {};
    size_t        n       = uu_encode(input, 3, buf);
    QCOMPARE(n, size_t(5));
    QCOMPARE(QByteArray(buf, (int)n), QByteArray("#0V%T"));
}

// ── intel_frame ──────────────────────────────────────────────────────────────

void ExportFormatTests::intelFrame_eofRecord()
{
    // EOF record (type 1): :00000001FF
    //   count=0, addr=0, type=1 → checksum bytes: 00 00 00 01, sum=1
    //   checksum = (~1+1) & 0xFF = 0xFF
    char   buf[32];
    size_t n = intel_frame(buf, 1, 0, 0, nullptr);
    QCOMPARE(QByteArray(buf, (int)n), QByteArray(":00000001FF"));
}

void ExportFormatTests::intelFrame_dataRecord()
{
    // Data record (type 0): one byte 0xAA at addr 0 → :01000000AA55
    //   count=1, addr=0, type=0, data=[0xAA]
    //   checksum bytes: 01 00 00 00 AA, sum=0xAB
    //   checksum = (~0xAB+1) & 0xFF = 0x55
    const uint8_t data[] = {0xAA};
    char          buf[32];
    size_t        n = intel_frame(buf, 0, 1, 0, data);
    QCOMPARE(QByteArray(buf, (int)n), QByteArray(":01000000AA55"));
}

void ExportFormatTests::intelFrame_extendedLinearAddress()
{
    // Extended linear address record (type 4): upper word = 0x0001 → :020000040001F9
    //   count=2, addr=0, type=4, data=[0x00, 0x01]
    //   checksum bytes: 02 00 00 04 00 01, sum=7
    //   checksum = (~7+1) & 0xFF = 0xF9
    const uint8_t data[] = {0x00, 0x01};
    char          buf[32];
    size_t        n = intel_frame(buf, 4, 2, 0, data);
    QCOMPARE(QByteArray(buf, (int)n), QByteArray(":020000040001F9"));
}

// ── motorola_frame ───────────────────────────────────────────────────────────

void ExportFormatTests::motorolaFrame_s9Terminator()
{
    // S9 (terminator for S1 files): S9030000FC
    //   type=9, s_sizelook[9]=2, count=0
    //   byte-count field = 2+0+1 = 3, addr=0x0000
    //   checksum bytes: 03 00 00, sum=3 → ~3 & 0xFF = 0xFC
    char   buf[32];
    size_t n = motorola_frame(buf, 9, 0, 0, nullptr);
    QCOMPARE(QByteArray(buf, (int)n), QByteArray("S9030000FC"));
}

void ExportFormatTests::motorolaFrame_s1DataRecord()
{
    // S1 (data, 2-byte addr): one byte 0xAA at addr 0 → S1040000AA51
    //   type=1, s_sizelook[1]=2, count=1
    //   byte-count field = 2+1+1 = 4, addr=0x0000, data=[0xAA]
    //   checksum bytes: 04 00 00 AA, sum=0xAE → ~0xAE & 0xFF = 0x51
    const uint8_t data[] = {0xAA};
    char          buf[32];
    size_t        n = motorola_frame(buf, 1, 1, 0, data);
    QCOMPARE(QByteArray(buf, (int)n), QByteArray("S1040000AA51"));
}

void ExportFormatTests::motorolaFrame_s3DataRecord()
{
    // S3 (data, 4-byte addr): two bytes [0xAB, 0xCD] at addr 0x12345678 → S30712345678ABCD6C
    //   type=3, s_sizelook[3]=4, count=2
    //   byte-count field = 4+2+1 = 7
    //   checksum bytes: 07 12 34 56 78 AB CD, sum=0x293 → ~0x93 & 0xFF = 0x6C
    const uint8_t data[] = {0xAB, 0xCD};
    char          buf[32];
    size_t        n = motorola_frame(buf, 3, 2, 0x12345678UL, data);
    QCOMPARE(QByteArray(buf, (int)n), QByteArray("S30712345678ABCD6C"));
}

void ExportFormatTests::motorolaFrame_invalidType()
{
    // Types outside [0,9] must return 0.
    char buf[32];
    QCOMPARE(motorola_frame(buf, -1, 0, 0, nullptr), size_t(0));
    QCOMPARE(motorola_frame(buf, 10, 0, 0, nullptr), size_t(0));
}

QTEST_APPLESS_MAIN(ExportFormatTests)
#include "exportformat_tests.moc"
