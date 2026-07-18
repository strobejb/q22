#include "../structview_testsupport.h"

class StructViewRootFormatTests : public QObject
{
    Q_OBJECT

private slots:
    void builderUsesSimpleRootNamesForBuiltinTypedefRoots();
};

void StructViewRootFormatTests::builderUsesSimpleRootNamesForBuiltinTypedefRoots()
{
    // Scenario: built-in PE and ELF definitions export their typedef root, not a
    // dummy global variable such as "ELF elf".
    // Expected: the rendered root node uses the simple structure alias shown to
    // users, matching PE's long-standing "PE" display.
    // Regression guard: moving tags between typedefs and variable declarations
    // should not reintroduce noisy root labels like "ELF elf".
    StrataLibrary peLibrary;
    QVERIFY2(parseStandardDefinition(&peLibrary, QStringLiteral("pe.strata")), "pe.strata failed to parse");
    TypeDecl *peRoot = exportedNamed(&peLibrary, QStringLiteral("PE"));
    QVERIFY(peRoot);
    auto peRows = buildRows(&peLibrary, peRoot, QByteArray(512, '\0'));
    QCOMPARE(peRows.size(), size_t(1));
    QCOMPARE(peRows[0]->name, QStringLiteral("PE"));

    StrataLibrary elfLibrary;
    QVERIFY2(parseStandardDefinition(&elfLibrary, QStringLiteral("elf.strata")), "elf.strata failed to parse");
    QByteArray elfBytes(128, '\0');
    elfBytes[0] = char(0x7f);
    elfBytes[1] = 'E';
    elfBytes[2] = 'L';
    elfBytes[3] = 'F';
    elfBytes[4] = char(1);
    elfBytes[5] = char(1);
    TypeDecl *elfRoot = exportedNamed(&elfLibrary, QStringLiteral("ELF"));
    QVERIFY(elfRoot);
    auto elfRows = buildRows(&elfLibrary, elfRoot, elfBytes);
    QCOMPARE(elfRows.size(), size_t(1));
    QCOMPARE(elfRows[0]->name, QStringLiteral("ELF"));
}

REGISTER_STRUCTVIEW_TEST(StructViewRootFormatTests)
#include "root_format_tests.moc"
