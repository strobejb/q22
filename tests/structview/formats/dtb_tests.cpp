#include "../structview_testsupport.h"

class StructViewDtbTests : public QObject
{
    Q_OBJECT

private slots:
    void builderRendersDtbHeaderAndBlocks();
};

void StructViewDtbTests::builderRendersDtbHeaderAndBlocks()
{
    // Scenario: DTB/FDT files use a big-endian header that points at the memory
    // reservation map, structure token stream, and strings block.
    // Expected: the standard DTB definition renders those regions from header
    // offsets without pretending the variable-width structure token stream is a
    // fixed C struct.
    StrataLibrary library;
    QVERIFY2(parseStandardDefinition(&library, QStringLiteral("dtb.strata")), "dtb.strata failed to parse");
    TypeDecl *dtbRoot = exportedNamed(&library, QStringLiteral("FDT"));
    QVERIFY(dtbRoot);

    QByteArray dtb(0x90, '\0');
    writeBe32(&dtb, 0x00, 0xd00dfeed); // magic
    writeBe32(&dtb, 0x04, quint32(dtb.size()));
    writeBe32(&dtb, 0x08, 0x48);       // off_dt_struct
    writeBe32(&dtb, 0x0c, 0x78);       // off_dt_strings
    writeBe32(&dtb, 0x10, 0x28);       // off_mem_rsvmap
    writeBe32(&dtb, 0x14, 17);         // version
    writeBe32(&dtb, 0x18, 16);         // last_comp_version
    writeBe32(&dtb, 0x1c, 0);          // boot_cpuid_phys
    writeBe32(&dtb, 0x20, 15);         // size_dt_strings
    writeBe32(&dtb, 0x24, 0x30);       // size_dt_struct

    writeBe64(&dtb, 0x28, 0x0000000000001000);
    writeBe64(&dtb, 0x30, 0x0000000000000100);
    writeBe64(&dtb, 0x38, 0);
    writeBe64(&dtb, 0x40, 0);

    writeBe32(&dtb, 0x48, 0x00000001); // FDT_BEGIN_NODE
    writeBe32(&dtb, 0x4c, 0x00000000); // root node name: ""
    writeBe32(&dtb, 0x50, 0x00000003); // FDT_PROP
    writeBe32(&dtb, 0x54, 3);          // len
    writeBe32(&dtb, 0x58, 0);          // nameoff: "compatible"
    writeAscii(&dtb, 0x5c, "abc");
    writeBe32(&dtb, 0x60, 0x00000003); // FDT_PROP
    writeBe32(&dtb, 0x64, 4);          // len
    writeBe32(&dtb, 0x68, 11);         // nameoff: "foo"
    dtb[0x6c] = char(1);
    dtb[0x6d] = char(2);
    dtb[0x6e] = char(3);
    dtb[0x6f] = char(4);
    writeBe32(&dtb, 0x70, 0x00000002); // FDT_END_NODE
    writeBe32(&dtb, 0x74, 0x00000009); // FDT_END
    writeAscii(&dtb, 0x78, "compatible");
    writeAscii(&dtb, 0x83, "foo");

    auto rows = buildRows(&library, dtbRoot, dtb);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->name, QStringLiteral("FDT"));

    StructureRow *header = findChildNamed(rows[0].get(), QStringLiteral("FDT_HEADER header"));
    QVERIFY(header);
    QCOMPARE(findChildNamed(header, QStringLiteral("dword magic"))->value, QStringLiteral("3490578157"));
    QCOMPARE(findChildNamed(header, QStringLiteral("dword off_dt_struct"))->value, QStringLiteral("72"));
    QCOMPARE(findChildNamed(header, QStringLiteral("dword size_dt_struct"))->value, QStringLiteral("48"));

    StructureRow *reserveMap = findChildNamed(rows[0].get(), QStringLiteral("FDT_RESERVE_ENTRY reserveMap[]"));
    QVERIFY(reserveMap);
    QCOMPARE(reserveMap->absoluteOffset, uint64_t(0x28));
    QCOMPARE(reserveMap->children.size(), size_t(2));
    QCOMPARE(findChildNamed(reserveMap->children[0].get(), QStringLiteral("qword address"))->value,
             QStringLiteral("4096"));
    QCOMPARE(findChildNamed(reserveMap->children[0].get(), QStringLiteral("qword size"))->value,
             QStringLiteral("256"));
    QCOMPARE(findChildNamed(reserveMap->children[1].get(), QStringLiteral("qword address"))->value,
             QStringLiteral("0"));
    QCOMPARE(findChildNamed(reserveMap->children[1].get(), QStringLiteral("qword size"))->value,
             QStringLiteral("0"));

    StructureRow *structureBlock = findChildNamed(rows[0].get(), QStringLiteral("FDT_STRUCT_ITEM structureBlock[]"));
    QVERIFY(structureBlock);
    QCOMPARE(structureBlock->absoluteOffset, uint64_t(0x48));
    QCOMPARE(structureBlock->children.size(), size_t(5));
    QCOMPARE(findChildNamed(structureBlock->children[0].get(), QStringLiteral("dword token"))->value,
             QStringLiteral("FDT_BEGIN_NODE"));
    QVERIFY(!findChildNamed(structureBlock->children[0].get(), QStringLiteral("union payload")));
    StructureRow *beginNode = findChildNamed(structureBlock->children[0].get(), QStringLiteral("FDT_BEGIN_NODE_PAYLOAD beginNode"));
    QVERIFY(beginNode);
    QCOMPARE(findChildNamed(beginNode, QStringLiteral("char name[]"))->value, QStringLiteral("\"\""));

    QCOMPARE(findChildNamed(structureBlock->children[1].get(), QStringLiteral("dword token"))->value,
             QStringLiteral("FDT_PROP"));
    QVERIFY(!findChildNamed(structureBlock->children[1].get(), QStringLiteral("union payload")));
    StructureRow *compatibleProp = findChildNamed(structureBlock->children[1].get(), QStringLiteral("FDT_PROP_PAYLOAD prop"));
    QVERIFY(compatibleProp);
    StructureRow *compatible = findChildNamed(compatibleProp, QStringLiteral("byte compatible[]"));
    QVERIFY(compatible);
    QCOMPARE(compatible->value, QStringLiteral("\"abc\""));
    QVERIFY(!findChildNamed(compatibleProp, QStringLiteral("union data")));
    QVERIFY(!findChildNamed(compatibleProp, QStringLiteral("byte raw[]")));

    QCOMPARE(findChildNamed(structureBlock->children[2].get(), QStringLiteral("dword token"))->value,
             QStringLiteral("FDT_PROP"));
    QVERIFY(!findChildNamed(structureBlock->children[2].get(), QStringLiteral("union payload")));
    StructureRow *rawProp = findChildNamed(structureBlock->children[2].get(), QStringLiteral("FDT_PROP_PAYLOAD prop"));
    QVERIFY(rawProp);
    StructureRow *raw = findChildNamed(rawProp, QStringLiteral("byte raw[]"));
    QVERIFY(raw);
    QCOMPARE(raw->value, QStringLiteral("{ 1, 2, 3, 4 }"));
    QVERIFY(!findChildNamed(rawProp, QStringLiteral("union data")));
    QVERIFY(!findChildNamed(rawProp, QStringLiteral("byte compatible[]")));

    QCOMPARE(findChildNamed(structureBlock->children[3].get(), QStringLiteral("dword token"))->value,
             QStringLiteral("FDT_END_NODE"));
    QCOMPARE(findChildNamed(structureBlock->children[4].get(), QStringLiteral("dword token"))->value,
             QStringLiteral("FDT_END"));

    StructureRow *stringsBlock = findChildNamed(rows[0].get(), QStringLiteral("char stringsBlock[][]"));
    QVERIFY(stringsBlock);
    QCOMPARE(stringsBlock->absoluteOffset, uint64_t(0x78));
    QCOMPARE(stringsBlock->children.size(), size_t(2));
    QCOMPARE(stringsBlock->children[0]->value, QStringLiteral("\"compatible\""));
    QCOMPARE(stringsBlock->children[1]->value, QStringLiteral("\"foo\""));
}

REGISTER_STRUCTVIEW_TEST(StructViewDtbTests)
#include "dtb_tests.moc"
