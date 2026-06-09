#include "filestats/stringscan.h"

#include <QTest>

using namespace stringscan;

// Run a complete scan over `data` in chunks of `chunkSize`, returning all results.
// Leaves the resultLimit at kMaxStringResultBatchLimit so adaptive-limit growth
// never masks a cap before all expected results are collected.
struct ScanHit
{
    QString    text;
    qulonglong offset = 0;
    qulonglong length = 0;
};

static QVector<ScanHit> runScan(const QByteArray &data, int minLength,
                                StringScanMode mode = StringScanMode::PrintableAscii, bool includeWhitespace = false,
                                int chunkSize = 8)
{
    StringScanState state;
    state.resultLimit = kMaxStringResultBatchLimit;
    state.elapsed.start();

    int pos = 0;
    while (pos < data.size() && !state.capped)
    {
        const QByteArray chunk = data.mid(pos, chunkSize);
        scanAsciiChunk(state, chunk, minLength, mode, includeWhitespace, nullptr, false);
        pos += chunk.size();
    }
    if (!state.capped)
        flushAsciiRun(state, minLength, state.offset, mode != StringScanMode::CIdentifiers, nullptr, false);

    QVector<ScanHit> hits;
    hits.reserve(state.results.size());
    for (const QVariantMap &row : state.results)
    {
        hits.append({row.value(QStringLiteral("text")).toString(), row.value(QStringLiteral("offset")).toULongLong(),
                     row.value(QStringLiteral("length")).toULongLong()});
    }
    return hits;
}

class StringScanTests : public QObject
{
    Q_OBJECT

  private slots:
    void stringSpansChunkBoundary();
    void stringEndsAtChunkBoundary();
    void shortRunSuppressedByMinLength();
    void stringExactlyMinLengthAtBoundary();
    void offsetTrackingAcrossChunks();
    void alphanumericModeFiltersNonAlpha();
    void cIdentifierModeRequiresNullTerminator();
    void includeWhitespaceToggle();
};

// A run of 10 'x' bytes split across two 6-byte chunks: [\0xxxxx][xxxxx\0]
// The run is assembled across the boundary and reported as one hit.
void StringScanTests::stringSpansChunkBoundary()
{
    QByteArray data("\x00xxxxxyyyyy\x00", 12);
    const auto hits = runScan(data, 4, StringScanMode::PrintableAscii, false, 6);
    QCOMPARE(hits.size(), 1);
    QCOMPARE(hits[0].text, QStringLiteral("xxxxxyyyyy"));
    QCOMPARE(hits[0].offset, 1ULL);
    QCOMPARE(hits[0].length, 10ULL);
}

// Run of 8 'x' bytes whose terminator lands in the next chunk: [xxxxxxxx][?\0...]
// The run must be flushed with the correct length and offset.
void StringScanTests::stringEndsAtChunkBoundary()
{
    // chunk 1 (9 bytes): \0 xxxxxxxx   — run of 8 starts at offset 1
    // chunk 2 (3 bytes): \0 x \0       — \0 at offset 9 terminates the run
    QByteArray data("\x00xxxxxxxx\x00x\x00", 12);
    const auto hits = runScan(data, 4, StringScanMode::PrintableAscii, false, 9);
    QVERIFY(hits.size() >= 1);
    QCOMPARE(hits[0].text, QStringLiteral("xxxxxxxx"));
    QCOMPARE(hits[0].offset, 1ULL);
    QCOMPARE(hits[0].length, 8ULL);
}

// Runs of 3 bytes on each side of a boundary, min=4 — nothing should be reported.
void StringScanTests::shortRunSuppressedByMinLength()
{
    // chunk 1: xxx\0   chunk 2: xxx\0  — each run is 3 bytes, below min=4
    QByteArray data("xxx\x00xxx\x00", 8);
    const auto hits = runScan(data, 4, StringScanMode::PrintableAscii, false, 4);
    QCOMPARE(hits.size(), 0);
}

// A 4-byte run (exactly min=4) split 2+2 across a 6-byte boundary: [\0\0xx][xx\0\0]
void StringScanTests::stringExactlyMinLengthAtBoundary()
{
    QByteArray data("\x00\x00xxxx\x00\x00", 8);
    const auto hits = runScan(data, 4, StringScanMode::PrintableAscii, false, 4);
    QCOMPARE(hits.size(), 1);
    QCOMPARE(hits[0].text, QStringLiteral("xxxx"));
    QCOMPARE(hits[0].offset, 2ULL);
    QCOMPARE(hits[0].length, 4ULL);
}

// 512 null bytes followed by "hello" — offset must be 512 regardless of chunk size.
void StringScanTests::offsetTrackingAcrossChunks()
{
    QByteArray data(512, '\x00');
    data.append("hello");
    const auto hits = runScan(data, 4, StringScanMode::PrintableAscii, false, 64);
    QCOMPARE(hits.size(), 1);
    QCOMPARE(hits[0].text, QStringLiteral("hello"));
    QCOMPARE(hits[0].offset, 512ULL);
    QCOMPARE(hits[0].length, 5ULL);
}

// Alphanumeric mode: spaces and punctuation are not accepted, digits and letters are.
void StringScanTests::alphanumericModeFiltersNonAlpha()
{
    // "hello world" contains a space — should split into "hello" and "world" in alphanumeric mode
    QByteArray data("hello world", 11);
    const auto hits = runScan(data, 4, StringScanMode::Alphanumeric, false, 6);
    QCOMPARE(hits.size(), 2);
    QCOMPARE(hits[0].text, QStringLiteral("hello"));
    QCOMPARE(hits[1].text, QStringLiteral("world"));

    // With includeWhitespace=true the space is accepted and it's one hit
    const auto hitsWs = runScan(data, 4, StringScanMode::Alphanumeric, true, 6);
    QCOMPARE(hitsWs.size(), 1);
    QCOMPARE(hitsWs[0].text, QStringLiteral("hello world"));
}

// CIdentifiers: first byte must be [A-Za-z_], only emitted on null terminator.
// Digits and non-null terminators do not flush.
void StringScanTests::cIdentifierModeRequiresNullTerminator()
{
    // "123abc\0" — digits are not a valid first byte, so the run starts at 'a'
    {
        QByteArray data("123abc\x00", 7);
        const auto hits = runScan(data, 3, StringScanMode::CIdentifiers, false, 4);
        QCOMPARE(hits.size(), 1);
        QCOMPARE(hits[0].text, QStringLiteral("abc"));
        QCOMPARE(hits[0].offset, 3ULL);
    }

    // "_myVar\0" — underscore is a valid first byte
    {
        QByteArray data("_myVar\x00", 7);
        const auto hits = runScan(data, 3, StringScanMode::CIdentifiers, false, 4);
        QCOMPARE(hits.size(), 1);
        QCOMPARE(hits[0].text, QStringLiteral("_myVar"));
    }

    // "hello" with no null terminator — must not be flushed at end of data
    {
        QByteArray data("hello", 5);
        const auto hits = runScan(data, 3, StringScanMode::CIdentifiers, false, 8);
        QCOMPARE(hits.size(), 0);
    }

    // Non-null terminator (' ') does not flush a C identifier run
    {
        // "foo bar\0" — 'foo' is not null-terminated (space terminates it non-null), 'bar' is
        QByteArray data("foo bar\x00", 8);
        const auto hits = runScan(data, 3, StringScanMode::CIdentifiers, false, 8);
        QCOMPARE(hits.size(), 1);
        QCOMPARE(hits[0].text, QStringLiteral("bar"));
    }
}

// AsciiText mode: whitespace toggle controls whether space/tab extend a run.
void StringScanTests::includeWhitespaceToggle()
{
    // "foo bar" — without whitespace it's split; with whitespace it's one run
    QByteArray data("\x01"
                    "foo bar\x01",
                    9); // \x01 is non-printable, acts as terminator
    {
        const auto hits = runScan(data, 3, StringScanMode::AsciiText, false, 5);
        QCOMPARE(hits.size(), 2);
        QCOMPARE(hits[0].text, QStringLiteral("foo"));
        QCOMPARE(hits[1].text, QStringLiteral("bar"));
    }
    {
        const auto hits = runScan(data, 3, StringScanMode::AsciiText, true, 5);
        QCOMPARE(hits.size(), 1);
        QCOMPARE(hits[0].text, QStringLiteral("foo bar"));
    }
}

QTEST_APPLESS_MAIN(StringScanTests)
#include "stringscan_tests.moc"
