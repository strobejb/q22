#include "../structview_testsupport.h"

class StructViewDynamicPlacementTests : public QObject
{
    Q_OBJECT

private slots:
    void builderPlacesDynamicStructsUnderNamedDynamicContainers();
    void builderPlacesDirectDynamicStructsUnderOwningRows();
    void builderRendersDynamicArraysAtReferencedOffsets();
    void builderStopsDynamicAndInlineArraysAtTerminators();
    void builderStopsDynamicArraysWhenElementsAreUnreadable();
    void builderRunsSemanticViewsOnceForDynamicArrayTables();
};

void StructViewDynamicPlacementTests::builderPlacesDynamicStructsUnderNamedDynamicContainers()
{
    // Scenario: a PE-style data-directory entry names a logical RVA, while a
    // section-header array declares named dynamic containers and RVA mapping.
    // Expected: SECTION rows are rendered at the root using [name] aliases, and
    // the dynamic structure appears under the SECTION whose range contains it.
    // Regression guard: optional PE structures must be declared in Strata data,
    // not hard-coded into the Structure View renderer.
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "enum Dir { Export = 0, Import = 1 };\n"
                        "typedef struct _DataDir { dword VirtualAddress; dword Size; } DataDir;\n"
                        "typedef struct _SectionBucket { } SECTION;\n"
                        "[dynamic_container(type(SECTION)), offset_map(VirtualAddress, SizeOfRawData, PointerToRawData)]\n"
                        "typedef struct _Section { char Name[8]; dword VirtualAddress; dword SizeOfRawData; dword PointerToRawData; } Section;\n"
                        "typedef struct _ImportDesc { dword thunk; } ImportDesc;\n"
                        "typedef struct _ExportDir { dword flags; } ExportDir;\n"
                        "[export]\n"
                        "struct Root {\n"
                        "  [element(dynamic_struct(case(Export), type(ExportDir), offset(VirtualAddress), mapper(offset_map), optional(Size != 0)), dynamic_struct(case(Import), type(ImportDesc), offset(VirtualAddress), mapper(offset_map), optional(Size != 0)))] DataDir dirs[2];\n"
                        "  [element(name(Name))] Section sections[2];\n"
                        "} root;\n"));

    QByteArray bytes(0x90, '\0');
    auto put32 = [&bytes](qsizetype offset, quint32 value) {
        bytes[offset + 0] = char(value & 0xff);
        bytes[offset + 1] = char((value >> 8) & 0xff);
        bytes[offset + 2] = char((value >> 16) & 0xff);
        bytes[offset + 3] = char((value >> 24) & 0xff);
    };
    put32(8, 0x1200);
    put32(12, 4);
    memcpy(bytes.data() + 16, ".text\0\0\0", 8);
    put32(24, 0x1000);
    put32(28, 0x100);
    put32(32, 0x40);
    memcpy(bytes.data() + 36, ".idata\0\0", 8);
    put32(44, 0x1200);
    put32(48, 0x100);
    put32(52, 0x80);
    put32(0x80, 0x12345678);

    auto rows = buildRows(&library, firstExported(&library), bytes);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->children.size(), size_t(4));
    QVERIFY(rows[0]->branchIconPath.isEmpty());
    QVERIFY(rows[0]->branchOpenIconPath.isEmpty());
    QVERIFY(rows[0]->branchEmptyIconPath.isEmpty());

    StructureRow *sections = rows[0]->children[1].get();
    QVERIFY(rows[0]->children[0]->branchIconPath.isEmpty());
    QVERIFY(sections->branchIconPath.isEmpty());
    QCOMPARE(sections->children.size(), size_t(2));
    QCOMPARE(sections->children[0]->children.size(), size_t(4));
    QCOMPARE(sections->children[1]->name, QStringLiteral("[1].idata"));
    QCOMPARE(sections->children[1]->children.size(), size_t(4));
    QCOMPARE(rows[0]->children[2]->name, QStringLiteral("SECTION .text"));
    QVERIFY(!rows[0]->children[2]->name.startsWith(QStringLiteral("SECTION - ")));
    QVERIFY(rows[0]->children[2]->branchIconPath.isEmpty());
    QCOMPARE(rows[0]->children[2]->children.size(), size_t(0));
    QCOMPARE(rows[0]->children[3]->name, QStringLiteral("SECTION .idata"));
    QVERIFY(!rows[0]->children[3]->name.startsWith(QStringLiteral("SECTION - ")));
    QVERIFY(rows[0]->children[3]->branchIconPath.isEmpty());
    QCOMPARE(rows[0]->children[3]->offset, QStringLiteral("00000080"));

    StructureRow *dynamicImport = rows[0]->children[3]->children[0].get();
    QCOMPARE(dynamicImport->name, QStringLiteral("ImportDesc"));
    QCOMPARE(dynamicImport->offset, QStringLiteral("00000080"));
    QCOMPARE(static_cast<int>(rows[0]->children[3]->kind), static_cast<int>(StructureRowKind::Dynamic));
    QCOMPARE(static_cast<int>(dynamicImport->kind), static_cast<int>(StructureRowKind::Dynamic));
    QVERIFY(dynamicImport->branchIconPath.isEmpty());
    QCOMPARE(dynamicImport->children.size(), size_t(1));
    QCOMPARE(dynamicImport->children[0]->name, QStringLiteral("dword thunk"));
    QCOMPARE(dynamicImport->children[0]->value, QStringLiteral("305419896"));

    std::vector<std::unique_ptr<StructureRow>> modelRows;
    modelRows.push_back(std::move(rows[0]));
    StructureTreeModel model;
    model.setRowsForTests(std::move(modelRows));
    const QModelIndex rootIndex = model.index(0, StructureTreeModel::NameColumn);
    const QModelIndex emptySectionIndex = model.index(2, StructureTreeModel::NameColumn, rootIndex);
    const QModelIndex sectionIndex = model.index(3, StructureTreeModel::NameColumn, rootIndex);
    const QModelIndex dynamicIndex = model.index(0, StructureTreeModel::NameColumn, sectionIndex);
    QVERIFY(emptySectionIndex.isValid());
    QVERIFY(sectionIndex.isValid());
    QVERIFY(dynamicIndex.isValid());
    QVERIFY(!model.hasChildren(emptySectionIndex));
    QCOMPARE(model.rowCount(emptySectionIndex), 0);
    QVERIFY(model.hasChildren(sectionIndex));
    QVERIFY(model.hasChildren(dynamicIndex));
    QVERIFY(!(model.flags(sectionIndex) & Qt::ItemIsEditable));
    QVERIFY(!(model.flags(dynamicIndex) & Qt::ItemIsEditable));
}

void StructViewDynamicPlacementTests::builderPlacesDirectDynamicStructsUnderOwningRows()
{
    // Scenario: a non-PE format stores an absolute file offset in a record and
    // wants the pointed-to structure displayed as related data.
    // Expected: the default direct mapper uses that file offset and attaches
    // the dynamic row under the compound declaration carrying dynamic_struct(...).
    // Regression guard: dynamic_struct must not require array selectors or
    // PE-style offset_map containers.
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "typedef struct _Payload { byte value; } Payload;\n"
                        "[dynamic_struct(name(RelatedPayload), type(Payload), offset(payloadOffset))]\n"
                        "typedef struct _Entry { dword payloadOffset; dword padding; } Entry;\n"
                        "[export]\n"
                        "struct Root {\n"
                        "  Entry entry;\n"
                        "} root;\n"));

    QByteArray bytes(16, '\0');
    writeLe32(&bytes, 0, 12);
    bytes[12] = char(0x5a);

    auto rows = buildRows(&library, firstExported(&library), bytes);
    QCOMPARE(rows.size(), size_t(1));

    StructureRow *entry = findChildNamed(rows[0].get(), QStringLiteral("Entry entry"));
    QVERIFY(entry);
    QCOMPARE(entry->children.size(), size_t(3));

    StructureRow *payload = findChildNamed(entry, QStringLiteral("RelatedPayload"));
    QVERIFY(payload);
    QCOMPARE(payload->name, QStringLiteral("RelatedPayload"));
    QCOMPARE(payload->offset, QStringLiteral("0000000C"));
    QCOMPARE(static_cast<int>(payload->kind), static_cast<int>(StructureRowKind::Dynamic));
    QCOMPARE(payload->children.size(), size_t(1));
    QCOMPARE(payload->children[0]->name, QStringLiteral("byte value"));
    QCOMPARE(payload->children[0]->value, QStringLiteral("90"));
}

void StructViewDynamicPlacementTests::builderRendersDynamicArraysAtReferencedOffsets()
{
    // Scenario: a rendered directory row points at a table elsewhere in the
    // mapped file image, just like PE export address/name/ordinal tables.
    // Expected: the raw table is displayed beneath the owning row without
    // pretending it is an inline field of the C structure.
    // Regression guard: dynamic_array must stay generic and reuse offset_map,
    // not rely on PE-specific semantic interpreter code.
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "typedef dword Entry;\n"
                        "typedef struct _Section { char Name[8]; dword VirtualAddress; dword SizeOfRawData; dword PointerToRawData; } Section;\n"
                        "typedef struct _Directory {\n"
                        "  dword AddressOfEntries;\n"
                        "  dword NumberOfEntries;\n"
                        "} Directory;\n"
                        "typedef struct _SectionBucket { } SECTION;\n"
                        "[export]\n"
                        "struct Root {\n"
                        "  [dynamic_array(name(Entries), type(Entry), offset(AddressOfEntries), count(NumberOfEntries), mapper(offset_map))] Directory dir;\n"
                        "  [element(dynamic_container(type(SECTION)), offset_map(VirtualAddress, SizeOfRawData, PointerToRawData))] Section section[1];\n"
                        "} root;\n"));

    QByteArray bytes(0x80, '\0');
    writeLe32(&bytes, 0, 0x1020);
    writeLe32(&bytes, 4, 2);
    writeLe32(&bytes, 16, 0x1000);
    writeLe32(&bytes, 20, 0x80);
    writeLe32(&bytes, 24, 0x40);
    writeLe32(&bytes, 0x60, 0x11111111);
    writeLe32(&bytes, 0x64, 0x22222222);

    auto rows = buildRows(&library, firstExported(&library), bytes);
    QCOMPARE(rows.size(), size_t(1));

    StructureRow *dir = findChildNamed(rows[0].get(), QStringLiteral("Directory dir"));
    QVERIFY(dir);
    StructureRow *entries = findChildNamed(dir, QStringLiteral("Entry Entries[]"));
    QVERIFY(entries);
    QCOMPARE(static_cast<int>(entries->kind), static_cast<int>(StructureRowKind::Dynamic));
    QCOMPARE(entries->offset, QStringLiteral("00000060"));
    QCOMPARE(entries->children.size(), size_t(2));
    QCOMPARE(entries->children[0]->name, QStringLiteral("[0]"));
    QCOMPARE(entries->children[0]->value, QStringLiteral("286331153"));
    QCOMPARE(entries->children[1]->value, QStringLiteral("572662306"));
}

void StructViewDynamicPlacementTests::builderStopsDynamicAndInlineArraysAtTerminators()
{
    // Scenario: binary formats often use sentinel-terminated arrays for strings
    // and descriptor tables, with size_is acting only as the safety cap.
    // Expected: string terminators hide by default; explicit
    // terminator("hidden") hides descriptor-table sentinels while still
    // consuming them for layout so the following field appears at the correct
    // offset.
    // Regression guard: C strings and null descriptor arrays should not require
    // one-off PE/import semantic code just to stop at zero.
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "typedef struct _Desc { dword Value; } Desc;\n"
                        "typedef struct _Section { char Name[8]; dword VirtualAddress; dword SizeOfRawData; dword PointerToRawData; } Section;\n"
                        "typedef struct _SectionBucket { } SECTION;\n"
                        "[export]\n"
                        "struct Root {\n"
                        "  [size_is(8), terminated_by(0)] char title[];\n"
                        "  byte afterTitle;\n"
                        "  [dynamic_array(name(Descs), type(Desc), offset(tableRva), count(4), mapper(offset_map), terminated_by(Value == 0), terminator(\"hidden\"))] dword tableRva;\n"
                        "  [element(dynamic_container(type(SECTION)), offset_map(VirtualAddress, SizeOfRawData, PointerToRawData))] Section section[1];\n"
                        "} root;\n"));

    QByteArray bytes(0x90, '\0');
    writeAscii(&bytes, 0, "ABC");
    bytes[4] = char(0x7f);
    writeLe32(&bytes, 5, 0x1040);
    writeLe32(&bytes, 17, 0x1000);
    writeLe32(&bytes, 21, 0x80);
    writeLe32(&bytes, 25, 0x40);
    writeLe32(&bytes, 0x80, 7);
    writeLe32(&bytes, 0x84, 0);
    writeLe32(&bytes, 0x88, 9);

    auto rows = buildRows(&library, firstExported(&library), bytes);
    QCOMPARE(rows.size(), size_t(1));

    StructureRow *title = findChildNamed(rows[0].get(), QStringLiteral("char title[]"));
    QVERIFY(title);
    QCOMPARE(title->value, QStringLiteral("\"ABC\""));
    QCOMPARE(title->children.size(), size_t(3));

    StructureRow *afterTitle = findChildNamed(rows[0].get(), QStringLiteral("byte afterTitle"));
    QVERIFY(afterTitle);
    QCOMPARE(afterTitle->offset, QStringLiteral("00000004"));
    QCOMPARE(afterTitle->value, QStringLiteral("127"));

    StructureRow *table = findChildNamed(rows[0].get(), QStringLiteral("dword tableRva"));
    QVERIFY(table);
    StructureRow *descs = findChildNamed(table, QStringLiteral("Desc Descs[]"));
    QVERIFY(descs);
    QCOMPARE(descs->children.size(), size_t(1));
    QCOMPARE(descs->children[0]->children[0]->value, QStringLiteral("7"));
}

void StructViewDynamicPlacementTests::builderStopsDynamicArraysWhenElementsAreUnreadable()
{
    // Scenario: the user manually selects a format that does not match the
    // file. Offset/count fields can then describe a huge dynamic table outside
    // the readable data. Expected: the dynamic array stops before rendering any
    // element instead of walking the bogus count. Regression guard: selecting
    // ISO for a TAR file used this path via directory-entry dynamic arrays.
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "typedef struct _Item { [count_as(1)] byte value; } Item;\n"
                        "[export]\n"
                        "struct Root {\n"
                        "  [dynamic_array(name(Items), type(Item), offset(tableOffset), count(itemCount))] dword tableOffset;\n"
                        "  dword itemCount;\n"
                        "} root;\n"));

    QByteArray bytes(8, '\0');
    writeLe32(&bytes, 0, 0x1000);
    writeLe32(&bytes, 4, 1000000);

    auto rows = buildRows(&library, firstExported(&library), bytes);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->children.size(), size_t(2));
    QVERIFY(!findDescendantNamed(rows[0].get(), QStringLiteral("Item Items[]")));
}

void StructViewDynamicPlacementTests::builderRunsSemanticViewsOnceForDynamicArrayTables()
{
    // Scenario: PE import descriptors are now rendered as a raw dynamic array,
    // while their type still carries a semantic view hook for the friendly
    // import-name overlay.
    // Expected: the semantic view runs once on the generated table row, not on
    // every generated [0], [1], ... descriptor element.
    // Regression guard: running pe.imports per descriptor made real PE files
    // effectively hang by repeatedly walking the same import table.
    auto runCount = std::make_shared<int>(0);
    StructureSemanticViewRegistry::instance().registerInterpreter(QStringLiteral("test.dynamic_array.once"),
                                                                  [runCount](StructureSemanticContext &context) {
                                                                      ++(*runCount);
                                                                      context.appendSemanticRow(context.currentRow(), QStringLiteral("semantic marker"), QString());
                                                                  });

    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "[native_view(\"test.dynamic_array.once\")]\n"
                        "typedef struct _Viewed { dword Value; } Viewed;\n"
                        "typedef struct _Section { char Name[8]; dword VirtualAddress; dword SizeOfRawData; dword PointerToRawData; } Section;\n"
                        "typedef struct _SectionBucket { } SECTION;\n"
                        "[export]\n"
                        "struct Root {\n"
                        "  [dynamic_array(name(Entries), type(Viewed), offset(tableRva), count(4), mapper(offset_map), terminated_by(Value == 0), terminator(\"hidden\"))] dword tableRva;\n"
                        "  [element(dynamic_container(type(SECTION)), offset_map(VirtualAddress, SizeOfRawData, PointerToRawData))] Section section[1];\n"
                        "} root;\n"));

    QByteArray bytes(0x90, '\0');
    writeLe32(&bytes, 0, 0x1040);
    writeAscii(&bytes, 4, ".idata");
    writeLe32(&bytes, 12, 0x1000);
    writeLe32(&bytes, 16, 0x80);
    writeLe32(&bytes, 20, 0x40);
    writeLe32(&bytes, 0x80, 7);
    writeLe32(&bytes, 0x84, 8);
    writeLe32(&bytes, 0x88, 0);

    auto rows = buildRows(&library, firstExported(&library), bytes);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(*runCount, 1);

    StructureRow *tableRva = findChildNamed(rows[0].get(), QStringLiteral("dword tableRva"));
    QVERIFY(tableRva);
    StructureRow *entries = findChildNamed(tableRva, QStringLiteral("Viewed Entries[]"));
    QVERIFY(entries);
    QCOMPARE(entries->children.size(), size_t(3));
    QCOMPARE(entries->children[0]->name, QStringLiteral("[0]"));
    QCOMPARE(entries->children[1]->name, QStringLiteral("[1]"));
    QCOMPARE(static_cast<int>(entries->children[2]->kind), static_cast<int>(StructureRowKind::Semantic));
    QCOMPARE(entries->children[2]->name, QStringLiteral("semantic marker"));
    QCOMPARE(entries->children[0]->children.size(), size_t(1));
    QCOMPARE(entries->children[1]->children.size(), size_t(1));
    QCOMPARE(static_cast<int>(entries->children[0]->children[0]->kind), static_cast<int>(StructureRowKind::Raw));
    QCOMPARE(static_cast<int>(entries->children[1]->children[0]->kind), static_cast<int>(StructureRowKind::Raw));
}

REGISTER_STRUCTVIEW_TEST(StructViewDynamicPlacementTests)
#include "dynamic_placement_tests.moc"
