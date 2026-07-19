#include "../structview_testsupport.h"

class StructViewMachOTests : public QObject
{
    Q_OBJECT

private slots:
    void builderRendersMachOHeaderAndLoadCommands();
};

void StructViewMachOTests::builderRendersMachOHeaderAndLoadCommands()
{
    // Scenario: 64-bit Mach-O files carry a header followed by typed load commands.
    // Expected: the standard Mach-O definition selects the 64-bit header and
    // decodes common load command payloads such as UUID and main entry point.
    StrataLibrary library;
    QVERIFY2(parseStandardDefinition(&library, QStringLiteral("macho.strata")), "macho.strata failed to parse");
    TypeDecl *machoRoot = exportedNamed(&library, QStringLiteral("MACHO"));
    QVERIFY(machoRoot);

    QByteArray macho(232, '\0');
    writeLe32(&macho, 0, 0xfeedfacf);
    writeLe32(&macho, 4, 0x01000007); // x86_64
    writeLe32(&macho, 8, 3);
    writeLe32(&macho, 12, 2); // execute
    writeLe32(&macho, 16, 3);
    writeLe32(&macho, 20, 200);
    writeLe32(&macho, 24, 0x00200004); // dyldlink | pie
    writeLe32(&macho, 28, 0);

    writeLe32(&macho, 32, 0x19); // LC_SEGMENT_64
    writeLe32(&macho, 36, 152);
    writeAscii(&macho, 40, "__TEXT");
    writeLe64(&macho, 56, 0x100000000);
    writeLe64(&macho, 64, 0x1000);
    writeLe64(&macho, 72, 0);
    writeLe64(&macho, 80, 0x1000);
    writeLe32(&macho, 88, 7);
    writeLe32(&macho, 92, 5);
    writeLe32(&macho, 96, 1);
    writeAscii(&macho, 104, "__text");
    writeAscii(&macho, 120, "__TEXT");
    writeLe64(&macho, 136, 0x100000f50);
    writeLe64(&macho, 144, 0x50);
    writeLe32(&macho, 152, 0xf50);
    writeLe32(&macho, 156, 4);
    writeLe32(&macho, 160, 0);
    writeLe32(&macho, 164, 0);
    writeLe32(&macho, 168, 0x80000400);

    writeLe32(&macho, 184, 0x1b); // LC_UUID
    writeLe32(&macho, 188, 24);
    for (int i = 0; i < 16; ++i)
        macho[192 + i] = char(i);
    writeLe32(&macho, 208, 0x80000028); // LC_MAIN
    writeLe32(&macho, 212, 24);
    writeLe64(&macho, 216, 0x1000);
    writeLe64(&macho, 224, 0);

    auto rows = buildRows(&library, machoRoot, macho);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->name, QStringLiteral("MACHO"));

    StructureRow *header = findDescendantNamed(rows[0].get(), QStringLiteral("MACHO_HEADER64 header64"));
    QVERIFY2(header, qPrintable(childNames(rows[0].get())));
    QCOMPARE(findChildNamed(rows[0].get(), QStringLiteral("dword magic"))->value, QStringLiteral("MACHO_MAGIC_64"));
    QCOMPARE(findChildNamed(header, QStringLiteral("dword cpuType"))->value, QStringLiteral("MACHO_CPU_TYPE_X86_64"));
    QCOMPARE(findChildNamed(header, QStringLiteral("dword commandCount"))->value, QStringLiteral("3"));

    StructureRow *commands = findChildNamed(rows[0].get(), QStringLiteral("MACHO_LOAD_COMMAND loadCommands[]"));
    QVERIFY2(commands, qPrintable(childNames(rows[0].get())));
    QCOMPARE(commands->children.size(), size_t(3));
    QCOMPARE(commands->children[0]->name, QStringLiteral("[0]MACHO_LC_SEGMENT_64"));
    QCOMPARE(commands->children[1]->name, QStringLiteral("[1]MACHO_LC_UUID"));
    QCOMPARE(commands->children[2]->name, QStringLiteral("[2]MACHO_LC_MAIN"));

    StructureRow *segment = findDescendantNamed(commands->children[0].get(), QStringLiteral("MACHO_SEGMENT64_COMMAND segment64"));
    QVERIFY2(segment, qPrintable(childNames(commands->children[0].get())));
    StructureRow *sections = findChildNamed(segment, QStringLiteral("MACHO_SECTION64 sections[]"));
    QVERIFY2(sections, qPrintable(childNames(segment)));
    QCOMPARE(sections->children.size(), size_t(1));
    StructureRow *sectionFlags = findDescendantNamed(sections->children[0].get(), QStringLiteral("dword flags"));
    QVERIFY2(sectionFlags, qPrintable(childNames(sections->children[0].get())));
    QCOMPARE(sectionFlags->value, QStringLiteral("MACHO_S_ATTR_PURE_INSTRUCTIONS | MACHO_S_ATTR_SOME_INSTRUCTIONS"));
    QCOMPARE(findChildNamed(sectionFlags, QStringLiteral("Section type"))->value, QStringLiteral("MACHO_S_REGULAR"));
    QCOMPARE(findChildNamed(sectionFlags, QStringLiteral("MACHO_S_ATTR_PURE_INSTRUCTIONS"))->value, QStringLiteral("1"));
    QCOMPARE(findChildNamed(sectionFlags, QStringLiteral("MACHO_S_ATTR_SOME_INSTRUCTIONS"))->value, QStringLiteral("1"));

    StructureRow *uuid = findDescendantNamed(commands->children[1].get(), QStringLiteral("MACHO_UUID_COMMAND uuid"));
    QVERIFY2(uuid, qPrintable(childNames(commands->children[1].get())));
    StructureRow *uuidBytes = findChildNamed(uuid, QStringLiteral("byte uuid[]"));
    QVERIFY2(uuidBytes, qPrintable(childNames(uuid)));
    QCOMPARE(uuidBytes->value, QStringLiteral("\"00010203-0405-0607-0809-0a0b0c0d0e0f\""));

    StructureRow *main = findDescendantNamed(commands->children[2].get(), QStringLiteral("MACHO_MAIN_COMMAND main"));
    QVERIFY2(main, qPrintable(childNames(commands->children[2].get())));
    QCOMPARE(findChildNamed(main, QStringLiteral("qword entryOffset"))->value, QStringLiteral("4096"));
}

REGISTER_STRUCTVIEW_TEST(StructViewMachOTests)
#include "macho_tests.moc"
