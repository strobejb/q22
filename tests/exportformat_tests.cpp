#include "dialogs/exportformat.h"

#include <QBuffer>
#include <QByteArray>
#include <QTest>

// ── Test helpers ──────────────────────────────────────────────────────────────

// In-memory DataSource backed by a QByteArray.
struct ByteArraySource : DataSource
{
    QByteArray data;
    QString    path;

    explicit ByteArraySource(QByteArray d, QString p = QStringLiteral("test.bin"))
        : data(std::move(d)), path(std::move(p))
    {
    }

    void getData(size_w offset, uint8_t *buf, size_t len) const override
    {
        memcpy(buf, data.constData() + (int)offset, len);
    }

    QString filePath() const override
    {
        return path;
    }
};

// Run one export format function against `input` bytes, return raw output.
static QByteArray runExport(bool (*fn)(ExportWriter &, const DataSource &, size_w, size_w, IMPEXP_OPTIONS *),
                            const QByteArray &input, IMPEXP_OPTIONS opts = {})
{
    ByteArraySource src(input);
    QBuffer         buf;
    buf.open(QIODevice::WriteOnly);
    ExportWriter writer(&buf);
    fn(writer, src, 0, (size_w)input.size(), &opts);
    return buf.data();
}

class ExportFormatTests : public QObject
{
    Q_OBJECT

  private slots:
    // Codec primitives
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

    // Format writers
    void exportRawHex_basicBytes();
    void exportText_fullLine();
    void exportBase64_knownInput();
    void exportUUEncode_knownInput();
    void exportIntelHex_singleByte();
    void exportMotorola_singleByte();
    void exportASM_byteMode();
    void exportCPP_byteMode();
};

// ── toAscii ───────────────────────────────────────────────────────────────────

void ExportFormatTests::toAscii_printablePassThrough()
{
    QCOMPARE(toAscii(0x20), ' ');
    QCOMPARE(toAscii(0x41), 'A');
    QCOMPARE(toAscii(0x7E), '~');
}

void ExportFormatTests::toAscii_nonPrintableBecomeDot()
{
    QCOMPARE(toAscii(0x00), '.');
    QCOMPARE(toAscii(0x1F), '.');
    QCOMPARE(toAscii(0x7F), '.');
    QCOMPARE(toAscii(0xFF), '.');
}

// ── base64_encode ─────────────────────────────────────────────────────────────

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
    char   buf[4] = {};
    size_t n      = uu_encode(nullptr, 0, buf);
    QCOMPARE(n, size_t(1));
    QCOMPARE(buf[0], ' ');
}

void ExportFormatTests::uuEncode_knownInput()
{
    const uint8_t input[] = {'C', 'a', 't'};
    char          buf[8]  = {};
    size_t        n       = uu_encode(input, 3, buf);
    QCOMPARE(n, size_t(5));
    QCOMPARE(QByteArray(buf, (int)n), QByteArray("#0V%T"));
}

// ── intel_frame ──────────────────────────────────────────────────────────────

void ExportFormatTests::intelFrame_eofRecord()
{
    char   buf[32];
    size_t n = intel_frame(buf, 1, 0, 0, nullptr);
    QCOMPARE(QByteArray(buf, (int)n), QByteArray(":00000001FF"));
}

void ExportFormatTests::intelFrame_dataRecord()
{
    const uint8_t data[] = {0xAA};
    char          buf[32];
    size_t        n = intel_frame(buf, 0, 1, 0, data);
    QCOMPARE(QByteArray(buf, (int)n), QByteArray(":01000000AA55"));
}

void ExportFormatTests::intelFrame_extendedLinearAddress()
{
    const uint8_t data[] = {0x00, 0x01};
    char          buf[32];
    size_t        n = intel_frame(buf, 4, 2, 0, data);
    QCOMPARE(QByteArray(buf, (int)n), QByteArray(":020000040001F9"));
}

// ── motorola_frame ────────────────────────────────────────────────────────────

void ExportFormatTests::motorolaFrame_s9Terminator()
{
    char   buf[32];
    size_t n = motorola_frame(buf, 9, 0, 0, nullptr);
    QCOMPARE(QByteArray(buf, (int)n), QByteArray("S9030000FC"));
}

void ExportFormatTests::motorolaFrame_s1DataRecord()
{
    const uint8_t data[] = {0xAA};
    char          buf[32];
    size_t        n = motorola_frame(buf, 1, 1, 0, data);
    QCOMPARE(QByteArray(buf, (int)n), QByteArray("S1040000AA51"));
}

void ExportFormatTests::motorolaFrame_s3DataRecord()
{
    const uint8_t data[] = {0xAB, 0xCD};
    char          buf[32];
    size_t        n = motorola_frame(buf, 3, 2, 0x12345678UL, data);
    QCOMPARE(QByteArray(buf, (int)n), QByteArray("S30712345678ABCD6C"));
}

void ExportFormatTests::motorolaFrame_invalidType()
{
    char buf[32];
    QCOMPARE(motorola_frame(buf, -1, 0, 0, nullptr), size_t(0));
    QCOMPARE(motorola_frame(buf, 10, 0, 0, nullptr), size_t(0));
}

// ── ExportRawHex ─────────────────────────────────────────────────────────────

void ExportFormatTests::exportRawHex_basicBytes()
{
    // [0x41, 0x42, 0x43] with linelen covering all three → "414243"
    IMPEXP_OPTIONS opts;
    opts.linelen   = 16;
    QByteArray out = runExport(ExportRawHex, QByteArray("\x41\x42\x43", 3), opts);
    QCOMPARE(out, QByteArray("414243"));
}

// ── ExportText ────────────────────────────────────────────────────────────────

void ExportFormatTests::exportText_fullLine()
{
    // 16 bytes 0x00–0x0F fills exactly one line with no padding.
    // Expected: "00000000  00 01 02 03 04 05 06 07  08 09 0A 0B 0C 0D 0E 0F  |................|\n"
    QByteArray input(16, '\0');
    for (int i = 0; i < 16; i++)
        input[i] = (char)i;

    IMPEXP_OPTIONS opts;
    opts.linelen   = 16;
    QByteArray out = runExport(ExportText, input, opts);

    QVERIFY(out.startsWith("00000000  "));
    QVERIFY(out.contains("00 01 02 03 04 05 06 07  08 09 0A 0B 0C 0D 0E 0F"));
    QVERIFY(out.endsWith("|................|\n"));
}

// ── ExportBase64 ─────────────────────────────────────────────────────────────

void ExportFormatTests::exportBase64_knownInput()
{
    // "foobar" encodes as "Zm9vYmFy" in one 54-byte chunk → one line + newline
    QByteArray out = runExport(ExportBase64, QByteArray("foobar"));
    QCOMPARE(out, QByteArray("Zm9vYmFy\n"));
}

// ── ExportUUEncode ────────────────────────────────────────────────────────────

void ExportFormatTests::exportUUEncode_knownInput()
{
    // "Cat" → begin line, one data line "#0V%T", end line.
    // filePath() is "test.bin"; section('/') gives "test.bin".
    QByteArray out = runExport(ExportUUEncode, QByteArray("Cat"));
    QVERIFY(out.startsWith("begin 666 test.bin\n"));
    QVERIFY(out.contains("#0V%T\n"));
    QVERIFY(out.endsWith("end\n"));
}

// ── ExportIntelHex ────────────────────────────────────────────────────────────

void ExportFormatTests::exportIntelHex_singleByte()
{
    // One byte [0xAA] at offset 0 with fUseAddress=true.
    // Expected lines: extended-address(0), data(:01000000AA55), EOF.
    IMPEXP_OPTIONS opts;
    opts.fUseAddress = true;
    QByteArray out   = runExport(ExportIntelHex, QByteArray("\xAA", 1), opts);

    const QList<QByteArray> lines = out.split('\n');
    QVERIFY(lines.size() >= 3);
    QCOMPARE(lines[0], QByteArray(":020000040000FA")); // extended linear address: upper word = 0
    QCOMPARE(lines[1], QByteArray(":01000000AA55"));
    QCOMPARE(lines[2], QByteArray(":00000001FF"));
}

// ── ExportMotorola ────────────────────────────────────────────────────────────

void ExportFormatTests::exportMotorola_singleByte()
{
    // One byte [0xAA] at offset 0.
    // Expected: S0 header, S3 data record, S7 terminator.
    QByteArray out = runExport(ExportMotorola, QByteArray("\xAA", 1));

    const QList<QByteArray> lines = out.split('\n');
    QVERIFY(lines.size() >= 3);
    QCOMPARE(lines[0], QByteArray("S00600004844521B")); // S0 "HDR"
    QCOMPARE(lines[1], QByteArray("S30600000000AA4F")); // S3 data at addr 0
    QCOMPARE(lines[2], QByteArray("S70500000000FA"));   // S7 terminator
}

// ── ExportASM ────────────────────────────────────────────────────────────────

void ExportFormatTests::exportASM_byteMode()
{
    // [0x41, 0x42] with SEARCHTYPE_BYTE and linelen covering both bytes.
    IMPEXP_OPTIONS opts;
    opts.basetype  = SEARCHTYPE_BYTE;
    opts.linelen   = 16;
    QByteArray out = runExport(ExportASM, QByteArray("\x41\x42", 2), opts);

    QVERIFY(out.contains("; Generated by HexEdit\n"));
    QVERIFY(out.contains("db "));
    QVERIFY(out.contains("041h "));
    QVERIFY(out.contains("042h "));
}

// ── ExportCPP ────────────────────────────────────────────────────────────────

void ExportFormatTests::exportCPP_byteMode()
{
    // [0x41, 0x42] with SEARCHTYPE_BYTE.
    IMPEXP_OPTIONS opts;
    opts.basetype  = SEARCHTYPE_BYTE;
    opts.linelen   = 16;
    QByteArray out = runExport(ExportCPP, QByteArray("\x41\x42", 2), opts);

    QVERIFY(out.contains("uint8_t hexData[0x2]"));
    QVERIFY(out.contains("0x41, "));
    QVERIFY(out.contains("0x42, "));
    QVERIFY(out.endsWith("};\n"));
}

QTEST_APPLESS_MAIN(ExportFormatTests)
#include "exportformat_tests.moc"
