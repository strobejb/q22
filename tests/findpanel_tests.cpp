#include "panels/findpattern.h"

#include <QTest>

class FindPanelTests : public QObject
{
    Q_OBJECT

private slots:
    void endianOptionControlsParsing();
    void signedOptionControlsNegativeIntegerParsing();
};

void FindPanelTests::endianOptionControlsParsing()
{
    // UTF byte order and integer byte order are separate search options.
    QCOMPARE(buildFindPattern(QStringLiteral("A"), SearchUTF16,
                              /*textBigEndian=*/false, /*integerBigEndian=*/false, /*signedIntegers=*/false),
             QByteArray("\x41\x00", 2));
    QCOMPARE(buildFindPattern(QStringLiteral("A"), SearchUTF16,
                              /*textBigEndian=*/true, /*integerBigEndian=*/false, /*signedIntegers=*/false),
             QByteArray("\x00\x41", 2));
    QCOMPARE(buildFindPattern(QStringLiteral("258"), SearchWord,
                              /*textBigEndian=*/true, /*integerBigEndian=*/false, /*signedIntegers=*/false),
             QByteArray("\x02\x01", 2));
    QCOMPARE(buildFindPattern(QStringLiteral("258"), SearchWord,
                              /*textBigEndian=*/false, /*integerBigEndian=*/true, /*signedIntegers=*/false),
             QByteArray("\x01\x02", 2));
}

void FindPanelTests::signedOptionControlsNegativeIntegerParsing()
{
    // Negative integer searches are accepted only when Signed is enabled.
    QVERIFY(buildFindPattern(QStringLiteral("-1"), SearchByte,
                             /*textBigEndian=*/false, /*integerBigEndian=*/false, /*signedIntegers=*/false).isEmpty());
    QCOMPARE(buildFindPattern(QStringLiteral("-1"), SearchByte,
                              /*textBigEndian=*/false, /*integerBigEndian=*/false, /*signedIntegers=*/true),
             QByteArray("\xff", 1));
    QCOMPARE(buildFindPattern(QStringLiteral("-1"), SearchWord,
                              /*textBigEndian=*/false, /*integerBigEndian=*/false, /*signedIntegers=*/true),
             QByteArray("\xff\xff", 2));
    QVERIFY(buildFindPattern(QStringLiteral("-129"), SearchByte,
                             /*textBigEndian=*/false, /*integerBigEndian=*/false, /*signedIntegers=*/true).isEmpty());
}

QTEST_MAIN(FindPanelTests)
#include "findpanel_tests.moc"
