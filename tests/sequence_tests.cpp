#include "sequence.h"

#include <QtTest/QtTest>
#include <QVector>

namespace {

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
    for(const seqchar_info &item : info)
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

QTEST_APPLESS_MAIN(SequenceTests)

#include "sequence_tests.moc"
