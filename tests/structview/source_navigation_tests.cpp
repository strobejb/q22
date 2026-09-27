#include "structview/structuresourcecoordinates.h"
#include "structview/structview_testsupport.h"

class StructViewSourceNavigationTests : public QObject
{
    Q_OBJECT

private slots:
    void rawSliceMapsPrimaryOffsetsIntoZeroBasedView();
    void isolatedSourceKeepsItsOwnOffsets();
    void rawSliceRejectsOffsetsBeforeItsBase();
};

void StructViewSourceNavigationTests::rawSliceMapsPrimaryOffsetsIntoZeroBasedView()
{
    uint64_t viewOffset = 0;
    QVERIFY(StructureSourceCoordinates::toViewOffset(101060656, true, false, 48, &viewOffset));
    QCOMPARE(viewOffset, uint64_t(101060608));

    QVERIFY(StructureSourceCoordinates::toViewOffset(101297821, true, false, 101297821, &viewOffset));
    QCOMPARE(viewOffset, uint64_t(0));
}

void StructViewSourceNavigationTests::isolatedSourceKeepsItsOwnOffsets()
{
    uint64_t viewOffset = 0;
    QVERIFY(StructureSourceCoordinates::toViewOffset(101060608, true, true, 0, &viewOffset));
    QCOMPARE(viewOffset, uint64_t(101060608));

    QVERIFY(StructureSourceCoordinates::toViewOffset(73, false, false, 0, &viewOffset));
    QCOMPARE(viewOffset, uint64_t(73));
}

void StructViewSourceNavigationTests::rawSliceRejectsOffsetsBeforeItsBase()
{
    uint64_t viewOffset = 0;
    QVERIFY(!StructureSourceCoordinates::toViewOffset(47, true, false, 48, &viewOffset));
}

REGISTER_STRUCTVIEW_TEST(StructViewSourceNavigationTests)

#include "source_navigation_tests.moc"
