#include "filestats/checksumscan.h"
#include "filestats/entropyscan.h"
#include "filestats/stringscan.h"
#include "sequence.h"
#include "sequencedevice.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QTemporaryFile>
#include <QVector>
#include <QtTest/QtTest>

using namespace filestats;
using namespace stringscan;

namespace
{

void init(sequence &seq, const QByteArray &bytes)
{
    QVERIFY(seq.init(reinterpret_cast<const seqchar *>(bytes.constData()),
                     static_cast<size_t>(bytes.size()),
                     true));
}

QByteArray renderContent(const sequence &seq)
{
    QByteArray actual(static_cast<int>(seq.size()), Qt::Uninitialized);
    const size_t rendered = seq.render(0,
                                       reinterpret_cast<seqchar *>(actual.data()),
                                       static_cast<size_t>(actual.size()));
    Q_ASSERT(rendered == static_cast<size_t>(actual.size()));
    return actual;
}

void expectContent(const sequence &seq, const QByteArray &expected)
{
    QCOMPARE(seq.size(), static_cast<size_w>(expected.size()));

    QByteArray actual = renderContent(seq);
    QCOMPARE(actual, expected);
}

QVector<size_t> renderFlags(const sequence &seq)
{
    QByteArray actual(static_cast<int>(seq.size()), Qt::Uninitialized);
    QVector<seqchar_info> info(static_cast<int>(seq.size()));
    const size_t rendered = seq.render(0,
                                       reinterpret_cast<seqchar *>(actual.data()),
                                       static_cast<size_t>(actual.size()),
                                       info.data());
    Q_ASSERT(rendered == static_cast<size_t>(actual.size()));

    QVector<size_t> flags;
    flags.reserve(info.size());
    for (const seqchar_info &item : info)
        flags.append(item.flags);
    return flags;
}

QVector<size_t> repeatedFlags(size_t flags, int count)
{
    return QVector<size_t>(count, flags);
}

bool insertBytes(sequence &seq, size_w index, const QByteArray &bytes)
{
    return seq.insert(index,
                      reinterpret_cast<const seqchar *>(bytes.constData()),
                      static_cast<size_w>(bytes.size()));
}

bool replaceBytes(sequence &seq, size_w index, const QByteArray &bytes, size_w eraseLength)
{
    return seq.replace(index,
                       reinterpret_cast<const seqchar *>(bytes.constData()),
                       static_cast<size_w>(bytes.size()),
                       eraseLength);
}

struct ScanHit
{
    QString text;
    qulonglong offset = 0;
    qulonglong length = 0;
};

QVector<ScanHit> scanSequence(const sequence &seq, int minLength = 4, int chunkSize = 8, bool includeUnicode = false)
{
    SequenceDevice device(seq);
    if (!device.isValid() || !device.open(QIODevice::ReadOnly))
        return {};

    StringScanState state;
    state.resultLimit = kMaxStringResultBatchLimit;
    state.elapsed.start();
    const UnicodeScanMode unicodeMode = includeUnicode ? UnicodeScanMode::Latin : UnicodeScanMode::Off;
    if (!scanDevice(device, state, minLength,
                    StringScanOptions{StringScanMode::PrintableAscii, false, unicodeMode, false}, nullptr,
                    chunkSize))
        return {};

    QVector<ScanHit> hits;
    hits.reserve(state.results.size());
    for (const QVariantMap &row : state.results)
    {
        hits.append({row.value(QStringLiteral("text")).toString(), row.value(QStringLiteral("offset")).toULongLong(),
                     row.value(QStringLiteral("length")).toULongLong()});
    }
    return hits;
}

QHash<QString, QString> checksumSequence(const sequence &seq, const QStringList &algorithms)
{
    SequenceDevice device(seq);
    if (!device.isValid() || !device.open(QIODevice::ReadOnly))
        return {};
    return calculateChecksums(device, algorithms);
}

QVector<quint8> hilbertSequence(const sequence &seq, qulonglong startOffset, qulonglong byteCount,
                                qulonglong &scopeSize, int &sampleCount)
{
    SequenceDevice device(seq);
    if (!device.isValid() || !device.open(QIODevice::ReadOnly))
        return {};
    return calculateHilbert(device, startOffset, byteCount, scopeSize, sampleCount, 64);
}

QString md5Hex(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Md5).toHex());
}

class TestSequenceSlice final : public sequence
{
  public:
    TestSequenceSlice(sequence *source, size_w baseOffset, size_w length)
        : m_source(source), m_baseOffset(baseOffset), m_length(length)
    {
    }

    bool isreadonly() override
    {
        return true;
    }
    bool save(const std::string & = std::string()) override
    {
        return false;
    }
    bool clear() override
    {
        return false;
    }
    bool insert(size_w, const seqchar *, size_w) override
    {
        return false;
    }
    bool replace(size_w, const seqchar *, size_w, size_w) override
    {
        return false;
    }
    bool replace(size_w, const seqchar *, size_w) override
    {
        return false;
    }
    bool erase(size_w, size_w) override
    {
        return false;
    }
    bool append(const seqchar *, size_w) override
    {
        return false;
    }
    bool undo() override
    {
        return false;
    }
    bool redo() override
    {
        return false;
    }
    bool canundo() const override
    {
        return false;
    }
    bool canredo() const override
    {
        return false;
    }

    size_w size() const override
    {
        return m_length;
    }

    size_t render(size_w index, seqchar *buf, size_t len, seqchar_info *infobuf = nullptr) const override
    {
        if (!m_source || !buf || index >= m_length)
            return 0;

        len = static_cast<size_t>(std::min<size_w>(static_cast<size_w>(len), m_length - index));
        return m_source->render(m_baseOffset + index, buf, len, infobuf);
    }

  private:
    void resolveDeviceSource(const sequence *&source, size_w &baseOffset, size_w &length) const override
    {
        source = m_source;
        baseOffset = m_baseOffset;
        length = m_length;
    }

    sequence *m_source = nullptr;
    size_w m_baseOffset = 0;
    size_w m_length = 0;
};

QVector<sequence::span_desc> takeSnapshot(sequence &seq, size_w index, size_w length)
{
    size_t count = 0;
    const bool counted = seq.takesnapshot(index, length, nullptr, &count);
    Q_ASSERT(counted);
    if (!counted)
        return {};

    QVector<sequence::span_desc> spans(static_cast<int>(count));
    const bool captured = seq.takesnapshot(index, length, spans.data(), &count);
    Q_ASSERT(captured);
    if (!captured)
        return {};
    Q_ASSERT(count == static_cast<size_t>(spans.size()));
    return spans;
}

QByteArray renderSnapshot(sequence &seq,
                          QVector<sequence::span_desc> spans,
                          size_w offset,
                          size_t length)
{
    QByteArray actual(static_cast<int>(length), Qt::Uninitialized);
    const bool rendered = seq.rendersnapshot(static_cast<size_t>(spans.size()),
                                             spans.data(),
                                             offset,
                                             reinterpret_cast<seqchar *>(actual.data()),
                                             length);
    Q_ASSERT(rendered);
    if (!rendered)
        return {};
    return actual;
}

} // namespace

class SequenceTests : public QObject
{
    Q_OBJECT

  private slots:
    void insertAtBeginning();
    void insertAtEnd();
    void insertOnSpanBoundary();
    void insertInMiddleOfSpan();

    void deleteStartsOnSpanBoundary();
    void deleteEndsOnSpanBoundary();
    void deleteEntireFileWithMultipleSpans();
    void deleteStartsAndStopsWithinSingleSpan();
    void deleteAcrossMultipleSpans();

    void replaceAtBeginning();
    void replaceAtEnd();
    void replaceOnSpanBoundary();
    void replaceInMiddleOfSpan();
    void replaceAcrossMultipleSpans();

    void consecutiveInsertUndoCoalesces();
    void breakoptPreventsInsertCoalescing();
    void forwardDeleteUndoCoalesces();
    void backwardDeleteUndoCoalesces();

    void replaceOverrunClampsToEnd();
    void replaceShorterThanErased();
    void replaceLongerThanErased();

    void undoRedoInsertMiddleSplit();
    void undoRedoDeleteWithinSplitSpan();
    void undoRedoReplaceAcrossSpans();
    void groupedOperationsUndoRedoTogether();
    void redoInvalidatedByNewEdit();

    void snapshotWithinSingleSpanRenders();
    void snapshotAcrossMultipleSpansRenders();
    void insertSnapshotRoundTripContent();
    void replaceSnapshotRoundTripContent();
    void initialDataRendersUnmodified();
    void insertedDataRendersModified();
    void snapshotInsertedOriginalDataRendersModified();
    void snapshotReplacementRendersModified();
    void splitOriginalSpansStayUnmodified();
    void splitModifiedSpansStayModified();
    void undoRedoPreservesModifiedFlags();

    void invalidOperationsDoNotModifyContent();
    void renderPastEndReturnsAvailableBytes();
    void sequenceDeviceReadsMemoryBackedDocument();
    void checksumScanReadsQBuffer();
    void checksumScanSeesUnsavedLogicalEdits();
    void entropyScanReadsQBuffer();
    void entropyScanSeesUnsavedLogicalEdits();
    void entropyScanRespectsLogicalScope();
    void stringsScanSeesUnsavedInsertion();
    void stringsScanSeesUnsavedOverwrite();
    void stringsScanOmitsDeletedContent();
    void stringsScanCrossesSequenceSpanBoundaries();
    void stringsScanSeesUtf16MemoryBackedDocument();
    void sequenceDeviceSnapshotSurvivesEdits();
    void sequenceDeviceSeekAcrossSpans();
    void sequenceDeviceReadsFileBackedSource();
    void sequenceDeviceReadsViewSlice();
};

void SequenceTests::insertAtBeginning()
{
    sequence seq;
    init(seq, "abcdef");

    QVERIFY(insertBytes(seq, 0, "XX"));
    expectContent(seq, "XXabcdef");
}

void SequenceTests::insertAtEnd()
{
    sequence seq;
    init(seq, "abcdef");

    QVERIFY(insertBytes(seq, seq.size(), "XX"));
    expectContent(seq, "abcdefXX");
}

void SequenceTests::insertOnSpanBoundary()
{
    sequence seq;
    init(seq, "abcdef");

    QVERIFY(insertBytes(seq, 3, "XX"));
    expectContent(seq, "abcXXdef");

    QVERIFY(insertBytes(seq, 5, "YY"));
    expectContent(seq, "abcXXYYdef");
}

void SequenceTests::insertInMiddleOfSpan()
{
    sequence seq;
    init(seq, "abcdef");

    QVERIFY(insertBytes(seq, 2, "XX"));
    expectContent(seq, "abXXcdef");
}

void SequenceTests::deleteStartsOnSpanBoundary()
{
    sequence seq;
    init(seq, "abcdef");
    QVERIFY(insertBytes(seq, 3, "XX"));
    expectContent(seq, "abcXXdef");

    QVERIFY(seq.erase(3, 3));
    expectContent(seq, "abcef");
}

void SequenceTests::deleteEndsOnSpanBoundary()
{
    sequence seq;
    init(seq, "abcdef");
    QVERIFY(insertBytes(seq, 3, "XX"));
    expectContent(seq, "abcXXdef");

    QVERIFY(seq.erase(1, 2));
    expectContent(seq, "aXXdef");
}

void SequenceTests::deleteEntireFileWithMultipleSpans()
{
    sequence seq;
    init(seq, "abcdef");
    QVERIFY(insertBytes(seq, 0, "S"));
    QVERIFY(insertBytes(seq, 4, "M"));
    QVERIFY(insertBytes(seq, seq.size(), "E"));
    expectContent(seq, "SabcMdefE");

    QVERIFY(seq.erase(0, seq.size()));
    expectContent(seq, "");
}

void SequenceTests::deleteStartsAndStopsWithinSingleSpan()
{
    sequence seq;
    init(seq, "abcdef");

    QVERIFY(seq.erase(2, 2));
    expectContent(seq, "abef");
}

void SequenceTests::deleteAcrossMultipleSpans()
{
    sequence seq;
    init(seq, "abcdefgh");
    QVERIFY(insertBytes(seq, 3, "XX"));
    expectContent(seq, "abcXXdefgh");

    QVERIFY(seq.erase(2, 5));
    expectContent(seq, "abfgh");
}

void SequenceTests::replaceAtBeginning()
{
    sequence seq;
    init(seq, "abcdef");

    QVERIFY(replaceBytes(seq, 0, "XX", 2));
    expectContent(seq, "XXcdef");
}

void SequenceTests::replaceAtEnd()
{
    sequence seq;
    init(seq, "abcdef");

    QVERIFY(replaceBytes(seq, seq.size(), "XX", 2));
    expectContent(seq, "abcdefXX");
}

void SequenceTests::replaceOnSpanBoundary()
{
    sequence seq;
    init(seq, "abcdef");
    QVERIFY(insertBytes(seq, 3, "XX"));
    expectContent(seq, "abcXXdef");

    QVERIFY(replaceBytes(seq, 3, "YY", 2));
    expectContent(seq, "abcYYdef");
}

void SequenceTests::replaceInMiddleOfSpan()
{
    sequence seq;
    init(seq, "abcdef");

    QVERIFY(replaceBytes(seq, 2, "XYZ", 3));
    expectContent(seq, "abXYZf");
}

void SequenceTests::replaceAcrossMultipleSpans()
{
    sequence seq;
    init(seq, "abcdef");
    QVERIFY(insertBytes(seq, 3, "XX"));
    expectContent(seq, "abcXXdef");

    QVERIFY(replaceBytes(seq, 2, "QQ", 5));
    expectContent(seq, "abQQf");
}

void SequenceTests::consecutiveInsertUndoCoalesces()
{
    sequence seq;
    init(seq, "abc");

    QVERIFY(insertBytes(seq, seq.size(), "X"));
    QVERIFY(insertBytes(seq, seq.size(), "Y"));
    expectContent(seq, "abcXY");

    QVERIFY(seq.undo());
    expectContent(seq, "abc");

    QVERIFY(seq.redo());
    expectContent(seq, "abcXY");
}

void SequenceTests::breakoptPreventsInsertCoalescing()
{
    sequence seq;
    init(seq, "abc");

    QVERIFY(insertBytes(seq, seq.size(), "X"));
    seq.breakopt();
    QVERIFY(insertBytes(seq, seq.size(), "Y"));
    expectContent(seq, "abcXY");

    QVERIFY(seq.undo());
    expectContent(seq, "abcX");

    QVERIFY(seq.undo());
    expectContent(seq, "abc");

    QVERIFY(seq.redo());
    expectContent(seq, "abcX");

    QVERIFY(seq.redo());
    expectContent(seq, "abcXY");
}

void SequenceTests::forwardDeleteUndoCoalesces()
{
    sequence seq;
    init(seq, "abcdef");

    QVERIFY(seq.erase(2, 1));
    QVERIFY(seq.erase(2, 1));
    expectContent(seq, "abef");

    QVERIFY(seq.undo());
    expectContent(seq, "abcdef");

    QVERIFY(seq.redo());
    expectContent(seq, "abef");
}

void SequenceTests::backwardDeleteUndoCoalesces()
{
    sequence seq;
    init(seq, "abcdef");

    QVERIFY(seq.erase(3, 1));
    QVERIFY(seq.erase(2, 1));
    expectContent(seq, "abef");

    QVERIFY(seq.undo());
    expectContent(seq, "abcdef");

    QVERIFY(seq.redo());
    expectContent(seq, "abef");
}

void SequenceTests::replaceOverrunClampsToEnd()
{
    sequence seq;
    init(seq, "abcdef");

    QVERIFY(replaceBytes(seq, 4, "XYZ", 99));
    expectContent(seq, "abcdXYZ");
}

void SequenceTests::replaceShorterThanErased()
{
    sequence seq;
    init(seq, "abcdef");

    QVERIFY(replaceBytes(seq, 1, "Z", 4));
    expectContent(seq, "aZf");
}

void SequenceTests::replaceLongerThanErased()
{
    sequence seq;
    init(seq, "abcdef");

    QVERIFY(replaceBytes(seq, 2, "WXYZ", 1));
    expectContent(seq, "abWXYZdef");
}

void SequenceTests::undoRedoInsertMiddleSplit()
{
    sequence seq;
    init(seq, "abcdef");

    QVERIFY(insertBytes(seq, 3, "XX"));
    expectContent(seq, "abcXXdef");

    QVERIFY(seq.undo());
    expectContent(seq, "abcdef");

    QVERIFY(seq.redo());
    expectContent(seq, "abcXXdef");
}

void SequenceTests::undoRedoDeleteWithinSplitSpan()
{
    sequence seq;
    init(seq, "abcdef");

    QVERIFY(seq.erase(2, 2));
    expectContent(seq, "abef");

    QVERIFY(seq.undo());
    expectContent(seq, "abcdef");

    QVERIFY(seq.redo());
    expectContent(seq, "abef");
}

void SequenceTests::undoRedoReplaceAcrossSpans()
{
    sequence seq;
    init(seq, "abcdef");

    QVERIFY(insertBytes(seq, 3, "XX"));
    seq.breakopt();
    QVERIFY(replaceBytes(seq, 2, "QQ", 5));
    expectContent(seq, "abQQf");

    QVERIFY(seq.undo());
    expectContent(seq, "abcXXdef");

    QVERIFY(seq.redo());
    expectContent(seq, "abQQf");
}

void SequenceTests::groupedOperationsUndoRedoTogether()
{
    sequence seq;
    init(seq, "abcdef");

    seq.group();
    QVERIFY(insertBytes(seq, 1, "X"));
    QVERIFY(seq.erase(4, 1));
    QVERIFY(replaceBytes(seq, 0, "Q", 1));
    seq.ungroup();
    expectContent(seq, "QXbcef");

    QVERIFY(seq.undo());
    expectContent(seq, "abcdef");

    QVERIFY(seq.redo());
    expectContent(seq, "QXbcef");
}

void SequenceTests::redoInvalidatedByNewEdit()
{
    sequence seq;
    init(seq, "abc");

    QVERIFY(insertBytes(seq, seq.size(), "X"));
    expectContent(seq, "abcX");

    QVERIFY(seq.undo());
    expectContent(seq, "abc");
    QVERIFY(seq.canredo());

    QVERIFY(insertBytes(seq, seq.size(), "Y"));
    QVERIFY(!seq.canredo());
    expectContent(seq, "abcY");
}

void SequenceTests::snapshotWithinSingleSpanRenders()
{
    sequence seq;
    init(seq, "abcdef");

    QVector<sequence::span_desc> spans = takeSnapshot(seq, 2, 3);
    QCOMPARE(renderSnapshot(seq, spans, 0, 3), QByteArray("cde"));
    QCOMPARE(renderSnapshot(seq, spans, 1, 2), QByteArray("de"));
}

void SequenceTests::snapshotAcrossMultipleSpansRenders()
{
    sequence seq;
    init(seq, "abcdef");
    QVERIFY(insertBytes(seq, 3, "XX"));
    expectContent(seq, "abcXXdef");

    QVector<sequence::span_desc> spans = takeSnapshot(seq, 2, 5);
    QCOMPARE(renderSnapshot(seq, spans, 0, 5), QByteArray("cXXde"));
    QCOMPARE(renderSnapshot(seq, spans, 2, 3), QByteArray("Xde"));
}

void SequenceTests::insertSnapshotRoundTripContent()
{
    sequence seq;
    init(seq, "abcdef");
    QVERIFY(insertBytes(seq, 3, "XX"));

    QVector<sequence::span_desc> spans = takeSnapshot(seq, 2, 5);
    QVERIFY(seq.insert_snapshot(seq.size(), 5, spans.data(), static_cast<size_t>(spans.size())));
    expectContent(seq, "abcXXdefcXXde");
}

void SequenceTests::replaceSnapshotRoundTripContent()
{
    sequence seq;
    init(seq, "abcdef");
    QVERIFY(insertBytes(seq, 3, "XX"));

    QVector<sequence::span_desc> spans = takeSnapshot(seq, 2, 5);
    QVERIFY(seq.replace_snapshot(0, 5, spans.data(), static_cast<size_t>(spans.size())));
    expectContent(seq, "cXXdedef");
}

void SequenceTests::initialDataRendersUnmodified()
{
    sequence seq;
    init(seq, "abcdef");

    QCOMPARE(renderFlags(seq), repeatedFlags(0, 6));
}

void SequenceTests::insertedDataRendersModified()
{
    sequence seq;
    init(seq, "abcdef");

    QVERIFY(insertBytes(seq, 3, "XX"));
    expectContent(seq, "abcXXdef");
    QCOMPARE(renderFlags(seq),
             (QVector<size_t>{0, 0, 0, SEQCHAR_MODIFIED, SEQCHAR_MODIFIED, 0, 0, 0}));
}

void SequenceTests::snapshotInsertedOriginalDataRendersModified()
{
    sequence seq;
    init(seq, "abcdef");

    QVector<sequence::span_desc> spans = takeSnapshot(seq, 2, 3);
    QVERIFY(seq.insert_snapshot(seq.size(), 3, spans.data(), static_cast<size_t>(spans.size())));
    expectContent(seq, "abcdefcde");
    QCOMPARE(renderFlags(seq),
             (QVector<size_t>{0, 0, 0, 0, 0, 0,
                              SEQCHAR_MODIFIED, SEQCHAR_MODIFIED, SEQCHAR_MODIFIED}));
}

void SequenceTests::snapshotReplacementRendersModified()
{
    sequence seq;
    init(seq, "abcdef");

    QVector<sequence::span_desc> spans = takeSnapshot(seq, 2, 3);
    QVERIFY(seq.replace_snapshot(0, 3, spans.data(), static_cast<size_t>(spans.size())));
    expectContent(seq, "cdedef");
    QCOMPARE(renderFlags(seq),
             (QVector<size_t>{SEQCHAR_MODIFIED, SEQCHAR_MODIFIED, SEQCHAR_MODIFIED, 0, 0, 0}));
}

void SequenceTests::splitOriginalSpansStayUnmodified()
{
    sequence seq;
    init(seq, "abcdef");

    QVERIFY(insertBytes(seq, 3, "XX"));
    QVERIFY(seq.erase(3, 2));
    expectContent(seq, "abcdef");
    QCOMPARE(renderFlags(seq), repeatedFlags(0, 6));
}

void SequenceTests::splitModifiedSpansStayModified()
{
    sequence seq;
    init(seq, "abcdef");

    QVERIFY(insertBytes(seq, 3, "XXXX"));
    QVERIFY(seq.erase(4, 2));
    expectContent(seq, "abcXXdef");
    QCOMPARE(renderFlags(seq),
             (QVector<size_t>{0, 0, 0, SEQCHAR_MODIFIED, SEQCHAR_MODIFIED, 0, 0, 0}));
}

void SequenceTests::undoRedoPreservesModifiedFlags()
{
    sequence seq;
    init(seq, "abcdef");

    QVERIFY(insertBytes(seq, 3, "XX"));
    QCOMPARE(renderFlags(seq),
             (QVector<size_t>{0, 0, 0, SEQCHAR_MODIFIED, SEQCHAR_MODIFIED, 0, 0, 0}));

    QVERIFY(seq.undo());
    QCOMPARE(renderFlags(seq), repeatedFlags(0, 6));

    QVERIFY(seq.redo());
    QCOMPARE(renderFlags(seq),
             (QVector<size_t>{0, 0, 0, SEQCHAR_MODIFIED, SEQCHAR_MODIFIED, 0, 0, 0}));
}

void SequenceTests::invalidOperationsDoNotModifyContent()
{
    sequence seq;
    init(seq, "abc");

    QVERIFY(!insertBytes(seq, 4, "X"));
    QVERIFY(!seq.erase(1, 0));
    QVERIFY(!seq.erase(2, 5));
    QVERIFY(!replaceBytes(seq, 4, "X", 1));
    expectContent(seq, "abc");
}

void SequenceTests::renderPastEndReturnsAvailableBytes()
{
    sequence seq;
    init(seq, "abc");

    QByteArray actual(5, '\0');
    const size_t rendered = seq.render(2,
                                       reinterpret_cast<seqchar *>(actual.data()),
                                       static_cast<size_t>(actual.size()));
    QCOMPARE(rendered, static_cast<size_t>(1));
    QCOMPARE(actual.left(1), QByteArray("c"));

    QCOMPARE(seq.render(3,
                        reinterpret_cast<seqchar *>(actual.data()),
                        static_cast<size_t>(actual.size())),
             static_cast<size_t>(0));
}

void SequenceTests::sequenceDeviceReadsMemoryBackedDocument()
{
    sequence seq;
    init(seq, "memory-backed");

    SequenceDevice device(seq);
    QVERIFY2(device.isValid(), qPrintable(device.errorString()));
    QVERIFY(device.open(QIODevice::ReadOnly));
    QCOMPARE(device.readAll(), QByteArray("memory-backed"));
}

void SequenceTests::checksumScanReadsQBuffer()
{
    QBuffer source;
    source.setData("checksum-buffer");
    QVERIFY(source.open(QIODevice::ReadOnly));

    const QHash<QString, QString> hashes = calculateChecksums(source, {QStringLiteral("MD5")});
    QCOMPARE(hashes.value(QStringLiteral("MD5")), md5Hex("checksum-buffer"));
}

void SequenceTests::checksumScanSeesUnsavedLogicalEdits()
{
    sequence seq;
    init(seq, "abcDELETEdef");
    QVERIFY(seq.erase(3, 6));
    QVERIFY(insertBytes(seq, 3, "XYZ"));
    expectContent(seq, "abcXYZdef");

    const QHash<QString, QString> hashes = checksumSequence(seq, {QStringLiteral("MD5")});
    QCOMPARE(hashes.value(QStringLiteral("MD5")), md5Hex("abcXYZdef"));
    QVERIFY(hashes.value(QStringLiteral("MD5")) != md5Hex("abcDELETEdef"));
}

void SequenceTests::entropyScanReadsQBuffer()
{
    QBuffer source;
    source.setData(QByteArray("\x00\x01\x02\x03", 4));
    QVERIFY(source.open(QIODevice::ReadOnly));

    qulonglong scopeSize = 0;
    const QVector<float> entropy = calculateEntropy(source, 4, 0, 0, scopeSize);
    QCOMPARE(scopeSize, 4ULL);
    QCOMPARE(entropy.size(), 1);
    QVERIFY(qAbs(entropy[0] - 0.25f) < 0.0001f);
}

void SequenceTests::entropyScanSeesUnsavedLogicalEdits()
{
    sequence seq;
    init(seq, "abcdef");
    QVERIFY(replaceBytes(seq, 2, "XYZ", 3));
    expectContent(seq, "abXYZf");

    qulonglong scopeSize = 0;
    int sampleCount = 0;
    const QVector<quint8> bytes = hilbertSequence(seq, 0, 0, scopeSize, sampleCount);
    QCOMPARE(scopeSize, 6ULL);
    QCOMPARE(sampleCount, 6);
    QCOMPARE(QByteArray(reinterpret_cast<const char *>(bytes.constData()), bytes.size()), QByteArray("abXYZf"));
}

void SequenceTests::entropyScanRespectsLogicalScope()
{
    sequence seq;
    init(seq, "0123456789");
    QVERIFY(insertBytes(seq, 4, "AB"));
    expectContent(seq, "0123AB456789");

    qulonglong scopeSize = 0;
    int sampleCount = 0;
    const QVector<quint8> bytes = hilbertSequence(seq, 3, 5, scopeSize, sampleCount);
    QCOMPARE(scopeSize, 5ULL);
    QCOMPARE(sampleCount, 5);
    QCOMPARE(QByteArray(reinterpret_cast<const char *>(bytes.constData()), bytes.size()), QByteArray("3AB45"));
}

void SequenceTests::stringsScanSeesUnsavedInsertion()
{
    sequence seq;
    init(seq, QByteArray("\0hello\0", 7));
    QVERIFY(insertBytes(seq, 3, "WORLD"));

    const auto hits = scanSequence(seq, 4, 3);
    QCOMPARE(hits.size(), 1);
    QCOMPARE(hits[0].text, QStringLiteral("heWORLDllo"));
    QCOMPARE(hits[0].offset, 1ULL);
    QCOMPARE(hits[0].length, 10ULL);
}

void SequenceTests::stringsScanSeesUnsavedOverwrite()
{
    sequence seq;
    init(seq, QByteArray("\0abcdef\0", 8));
    QVERIFY(replaceBytes(seq, 3, "XYZ", 3));

    const auto hits = scanSequence(seq, 4, 4);
    QCOMPARE(hits.size(), 1);
    QCOMPARE(hits[0].text, QStringLiteral("abXYZf"));
    QCOMPARE(hits[0].offset, 1ULL);
    QCOMPARE(hits[0].length, 6ULL);
}

void SequenceTests::stringsScanOmitsDeletedContent()
{
    sequence seq;
    init(seq, QByteArray("\0abcDELETEdef\0", 14));
    QVERIFY(seq.erase(4, 6));

    const auto hits = scanSequence(seq, 4, 5);
    QCOMPARE(hits.size(), 1);
    QCOMPARE(hits[0].text, QStringLiteral("abcdef"));
    QVERIFY(!hits[0].text.contains(QStringLiteral("DELETE")));
}

void SequenceTests::stringsScanCrossesSequenceSpanBoundaries()
{
    sequence seq;
    init(seq, QByteArray("\0abcd\0", 6));
    QVERIFY(insertBytes(seq, 3, "EFGH"));

    const auto hits = scanSequence(seq, 4, 2);
    QCOMPARE(hits.size(), 1);
    QCOMPARE(hits[0].text, QStringLiteral("abEFGHcd"));
    QCOMPARE(hits[0].offset, 1ULL);
}

void SequenceTests::stringsScanSeesUtf16MemoryBackedDocument()
{
    QByteArray data;
    const QString text = QStringLiteral("C:\\src\\loxberry-plugin-mqttwestin");
    for (QChar ch : text)
    {
        data.append(static_cast<char>(ch.unicode() & 0xFF));
        data.append(static_cast<char>((ch.unicode() >> 8) & 0xFF));
    }
    data.append('\0');
    data.append('\0');

    sequence seq;
    init(seq, data);

    const auto hits = scanSequence(seq, 5, 9, true);
    QCOMPARE(hits.size(), 1);
    QCOMPARE(hits[0].text, text);
    QCOMPARE(hits[0].offset, 0ULL);
    QCOMPARE(hits[0].length, static_cast<qulonglong>(text.size() * 2));
}

void SequenceTests::sequenceDeviceSnapshotSurvivesEdits()
{
    sequence seq;
    init(seq, "abcdef");
    QVERIFY(insertBytes(seq, 3, "XX"));

    SequenceDevice device(seq);
    QVERIFY2(device.isValid(), qPrintable(device.errorString()));
    QVERIFY(device.open(QIODevice::ReadOnly));

    QVERIFY(replaceBytes(seq, 3, "YY", 2));
    QVERIFY(seq.erase(0, 1));
    expectContent(seq, "bcYYdef");

    QCOMPARE(device.readAll(), QByteArray("abcXXdef"));
}

void SequenceTests::sequenceDeviceSeekAcrossSpans()
{
    sequence seq;
    init(seq, "abcdef");
    QVERIFY(insertBytes(seq, 3, "XX"));

    SequenceDevice device(seq);
    QVERIFY2(device.isValid(), qPrintable(device.errorString()));
    QVERIFY(device.open(QIODevice::ReadOnly));
    QVERIFY(device.seek(2));
    QCOMPARE(device.read(5), QByteArray("cXXde"));
}

void SequenceTests::sequenceDeviceReadsFileBackedSource()
{
    QTemporaryFile temp;
    QVERIFY(temp.open());
    const QByteArray contents("\0file-backed-string\0", 20);
    QCOMPARE(temp.write(contents), static_cast<qint64>(contents.size()));
    QVERIFY(temp.flush());
    const QString path = temp.fileName();
    temp.close();

    sequence seq;
    QVERIFY(seq.open(path.toStdString(), true, true));

    SequenceDevice device(seq);
    QVERIFY2(device.isValid(), qPrintable(device.errorString()));
    QVERIFY(device.open(QIODevice::ReadOnly));
    QCOMPARE(device.readAll(), contents);
}

void SequenceTests::sequenceDeviceReadsViewSlice()
{
    sequence seq;
    init(seq, QByteArray("\0prefix\0slice-value\0suffix\0", 28));
    TestSequenceSlice slice(&seq, 8, 12);

    SequenceDevice device(slice);
    QVERIFY2(device.isValid(), qPrintable(device.errorString()));
    QVERIFY(device.open(QIODevice::ReadOnly));
    QCOMPARE(device.readAll(), QByteArray("slice-value\x00", 12));
}

QTEST_APPLESS_MAIN(SequenceTests)

#include "sequence_tests.moc"
