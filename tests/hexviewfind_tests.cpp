#include <QtTest/QtTest>

#include "hexview.h"

#include <QBuffer>
#include <QMenu>

void themeMenu(QMenu *)
{
}

namespace
{

template <typename Tag, typename Tag::type Member> struct PrivateAccess
{
    friend typename Tag::type get(Tag)
    {
        return Member;
    }
};

struct FindDeviceTag
{
    using type = bool (HexView::*)(QIODevice &, size_w, size_w *, uint);
    friend type get(FindDeviceTag);
};

template struct PrivateAccess<FindDeviceTag, &HexView::findNext>;

bool findDeviceForTest(HexView &hv, QIODevice &source, size_w sourceSize, size_w *result, uint options = 0)
{
    return (hv.*get(FindDeviceTag{}))(source, sourceSize, result, options);
}

void initHexView(HexView &hv, const QByteArray &data)
{
    QVERIFY(
        hv.initBuf(reinterpret_cast<const uint8_t *>(data.constData()), static_cast<size_t>(data.size()), true, false));
    hv.resize(1000, 600);
    hv.setLineLen(16);
    hv.scrollHStart();
    hv.scrollTop(0);
}

void compilePattern(HexView &hv, const QByteArray &pattern)
{
    QVERIFY(hv.findInit(reinterpret_cast<const uint8_t *>(pattern.constData()), static_cast<size_t>(pattern.size())));
}

} // namespace

class HexViewFindTests : public QObject
{
    Q_OBJECT

  private slots:
    void deviceSearchReadsQBuffer();
    void deviceSearchFindsMatchAcrossChunkBoundary();
    void publicSearchSeesUnsavedInsertion();
    void publicSearchUsesStableSnapshotDuringProgressCallback();
};

void HexViewFindTests::deviceSearchReadsQBuffer()
{
    HexView hv;
    compilePattern(hv, QByteArray("needle"));

    QBuffer source;
    source.setData("xxneedlezz");
    QVERIFY(source.open(QIODevice::ReadOnly));

    size_w result = 0;
    QVERIFY(findDeviceForTest(hv, source, static_cast<size_w>(source.size()), &result));
    QCOMPARE(result, static_cast<size_w>(2));
}

void HexViewFindTests::deviceSearchFindsMatchAcrossChunkBoundary()
{
    QByteArray data(999, 'x');
    data.append("needle");
    data.append("tail");

    HexView hv;
    compilePattern(hv, QByteArray("needle"));

    QBuffer source;
    source.setData(data);
    QVERIFY(source.open(QIODevice::ReadOnly));

    size_w result = 0;
    QVERIFY(findDeviceForTest(hv, source, static_cast<size_w>(source.size()), &result));
    QCOMPARE(result, static_cast<size_w>(999));
}

void HexViewFindTests::publicSearchSeesUnsavedInsertion()
{
    HexView hv;
    initHexView(hv, QByteArray("abcdef"));

    QVERIFY(hv.setCurSel(3, 3));
    hv.setEditMode(HVMODE_INSERT);
    const QByteArray inserted("XYZ");
    QCOMPARE(
        hv.writeAtCursor(reinterpret_cast<const uint8_t *>(inserted.constData()), static_cast<size_t>(inserted.size())),
        static_cast<size_w>(inserted.size()));

    QVERIFY(hv.setCurSel(0, 0));
    compilePattern(hv, QByteArray("cXYZd"));

    size_w result = 0;
    QVERIFY(hv.findNext(&result));
    QCOMPARE(result, static_cast<size_w>(2));
}

void HexViewFindTests::publicSearchUsesStableSnapshotDuringProgressCallback()
{
    static constexpr int kChunkCount  = 1030;
    static constexpr int kChunkSize   = 1000;
    const int            needleOffset = 1025 * kChunkSize;

    QByteArray data(kChunkCount * kChunkSize, 'A');
    data.replace(needleOffset, 6, "needle");

    HexView hv;
    initHexView(hv, data);
    hv.setCurSel(0, 0);
    compilePattern(hv, QByteArray("needle"));

    bool   editedLiveDocument = false;
    size_t replacementResult  = 0;
    QObject::connect(&hv, &HexView::findProgress, &hv,
                     [&]()
                     {
                         if (editedLiveDocument)
                             return;
                         editedLiveDocument = true;
                         QByteArray replacement("xxxxxx");
                         replacementResult = hv.setData(static_cast<size_w>(needleOffset),
                                                        reinterpret_cast<uint8_t *>(replacement.data()),
                                                        static_cast<size_t>(replacement.size()));
                     });

    size_w result = 0;
    QVERIFY(hv.findNext(&result));
    QVERIFY(editedLiveDocument);
    QVERIFY(replacementResult > 0);
    QCOMPARE(result, static_cast<size_w>(needleOffset));
}

QTEST_MAIN(HexViewFindTests)

#include "hexviewfind_tests.moc"
