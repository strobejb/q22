#include "../structview_testsupport.h"

class StructViewSemanticRendererTests : public QObject
{
    Q_OBJECT

private slots:
    void builderEmitsSemanticRowsUnderAttachedSchema();
    void rendererDefersSemanticOverlayUntilRequested();
    void builderEmitsSemanticRowsThroughNamedOffsetSpaces();
    void builderEmitsAndMergesSemanticNodes();
    void builderRejectsUnterminatedCstrSemanticNames();
    void builderEmitsIntoMappedSemanticContainers();
    void builderElidesImplicitScalarArrayEmitRow();
    void builderStopsSemanticEmitRowsWhenElementsAreUnreadable();
    void builderAddressesPositionalSemanticCollections();
    void builderUsesPositionalCollectionsForParallelTables();
    void definitionManagerFlagsUnknownSemanticDestinations();
    void definitionManagerFlagsUnknownSemanticNodeFields();
    void definitionManagerValidatesPositionalSemanticDestinations();
    void semanticRegistryRunsKnownViewsAndIgnoresUnknownViews();
};

void StructViewSemanticRendererTests::builderEmitsSemanticRowsUnderAttachedSchema()
{
    // Scenario: a raw directory row points at byte payloads elsewhere in the
    // file and declares a semantic destination schema.
    // Expected: the raw tree is unchanged, while a top-level Semantic/Payloads
    // branch contains one labeled byte-backed row per emitting raw row.
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "typedef byte PayloadByte;\n"
                        "typedef struct _Entry {"
                        "  dword id;"
                        "  [emit(dest(Payloads), label(id), type(PayloadByte), offset(payloadOffset), count(payloadSize), optional(payloadSize != 0))] dword payloadOffset;"
                        "  dword payloadSize;"
                        "} Entry;\n"
                        "[semantic] typedef struct _RootView { PayloadByte Payloads[]; } RootView;\n"
                        "[export, semantic(RootView)]\n"
                        "typedef struct _Root { dword count; [count(count)] Entry entries[]; } Root;\n"));

    QByteArray bytes(0x40, '\0');
    writeLe32(&bytes, 0, 2);
    writeLe32(&bytes, 4, 17);
    writeLe32(&bytes, 8, 0x30);
    writeLe32(&bytes, 12, 3);
    writeLe32(&bytes, 16, 23);
    writeLe32(&bytes, 20, 0x34);
    writeLe32(&bytes, 24, 0);
    bytes[0x30] = char(0xaa);
    bytes[0x31] = char(0xbb);
    bytes[0x32] = char(0xcc);

    auto rows = buildRows(&library, firstExported(&library), bytes);
    StructureRow *rootRow = findTopLevelNamed(rows, QStringLiteral("Root"));
    QVERIFY(rootRow);

    StructureRow *entries = findChildNamed(rootRow, QStringLiteral("Entry entries[]"));
    QVERIFY(entries);
    QCOMPARE(entries->children.size(), size_t(2));
    QVERIFY(!findChildNamed(entries->children[0].get(), QStringLiteral("17")));

    StructureRow *semantic = findSemanticRootChildNamed(rows, QStringLiteral("Semantic"));
    QVERIFY(semantic);
    StructureRow *payloads = findChildNamed(semantic, QStringLiteral("Payloads"));
    QVERIFY(payloads);
    QCOMPARE(payloads->branchIconPath, QString::fromLatin1(StructureBranchIcons::kBlueElementArray));
    QCOMPARE(payloads->branchOpenIconPath, QString::fromLatin1(StructureBranchIcons::kBlueElementArray));
    QCOMPARE(payloads->branchEmptyIconPath, QString::fromLatin1(StructureBranchIcons::kGrayElementArray));
    QCOMPARE(payloads->children.size(), size_t(1));

    StructureRow *payload = payloads->children[0].get();
    QCOMPARE(payload->name, QStringLiteral("17"));
    QCOMPARE(payload->branchIconPath, QString::fromLatin1(StructureBranchIcons::kBlueElementArray));
    QCOMPARE(payload->branchOpenIconPath, QString::fromLatin1(StructureBranchIcons::kBlueElementArray));
    QCOMPARE(payload->branchEmptyIconPath, QString::fromLatin1(StructureBranchIcons::kGrayElementArray));
    QCOMPARE(payload->offset, QStringLiteral("00000030"));
    QCOMPARE(payload->byteLength, uint64_t(3));
    QCOMPARE(static_cast<int>(payload->kind), static_cast<int>(StructureRowKind::Semantic));
    QCOMPARE(payload->children.size(), size_t(3));
    QVERIFY(payload->children[0]->branchIconPath.isEmpty());
    QVERIFY(payload->children[0]->branchOpenIconPath.isEmpty());
    QVERIFY(payload->children[0]->branchEmptyIconPath.isEmpty());
    QCOMPARE(payload->children[0]->value, QStringLiteral("170"));
    QCOMPARE(payload->children[2]->value, QStringLiteral("204"));
}

void StructViewSemanticRendererTests::rendererDefersSemanticOverlayUntilRequested()
{
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "typedef byte PayloadByte;\n"
                        "typedef struct _Entry {"
                        "  [emit(dest(Payloads), label(\"payload\"), type(PayloadByte), offset(payloadOffset), count(payloadSize))] dword payloadOffset;"
                        "  dword payloadSize;"
                        "} Entry;\n"
                        "[semantic] typedef struct _RootView { PayloadByte Payloads[]; } RootView;\n"
                        "[export, semantic(RootView)] typedef struct _Root { Entry entry; } Root;\n"));

    QByteArray bytes(0x20, '\0');
    writeLe32(&bytes, 0, 0x10);
    writeLe32(&bytes, 4, 1);
    bytes[0x10] = char(0xaa);
    const auto reader = [&bytes](uint64_t offset, uint8_t *buffer, size_t length) -> size_t {
        if (offset >= static_cast<uint64_t>(bytes.size()))
            return 0;
        const size_t available = static_cast<size_t>(bytes.size() - static_cast<int>(offset));
        const size_t copied = qMin(length, available);
        memcpy(buffer, bytes.constData() + offset, copied);
        return copied;
    };

    StructureRenderEngine renderer(&library, firstExported(&library), 0, reader, {});
    QVERIFY(renderer.hasSemanticOverlay());
    auto rawRows = renderer.buildRaw();
    QCOMPARE(rawRows.size(), size_t(1));
    QVERIFY(!findTopLevelNamed(rawRows, QStringLiteral("Semantic")));

    auto loadingRow = std::make_unique<StructureRow>();
    loadingRow->kind = StructureRowKind::Semantic;
    loadingRow->name = QStringLiteral("Semantic");
    loadingRow->value = QStringLiteral("Loading...");
    loadingRow->branchIconPath = QStringLiteral(":/icons/actions/circle-repeat.svg");
    loadingRow->branchOpenIconPath = loadingRow->branchIconPath;
    loadingRow->branchEmptyIconPath = loadingRow->branchIconPath;
    loadingRow->parent = rawRows.front().get();
    rawRows.front()->children.push_back(std::move(loadingRow));

    auto semanticRows = renderer.buildSemanticOverlay(rawRows.front().get());
    QCOMPARE(semanticRows.size(), size_t(1));
    QCOMPARE(semanticRows.front()->name, QStringLiteral("Semantic"));
    QCOMPARE(semanticRows.front()->value, QStringLiteral("{...}"));
    QVERIFY(semanticRows.front()->branchIconPath != QStringLiteral(":/icons/actions/circle-repeat.svg"));
    QVERIFY(findChildNamed(semanticRows.front().get(), QStringLiteral("Payloads")));
}

void StructViewSemanticRendererTests::builderEmitsSemanticRowsThroughNamedOffsetSpaces()
{
    // Scenario: semantic emit uses offset("space", expr) to target a named
    // address space rather than a direct file offset.
    // Expected: the emitted semantic row points at the mapped file bytes and
    // terminator/max_count behavior matches dynamic arrays.
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "typedef byte PayloadByte;\n"
                        "typedef struct _Entry {"
                        "  dword logical;"
                        "  [emit(dest(Strings), label(\"name\"), type(PayloadByte), offset(\"strings\", logical), max_count(8), terminated_by(0), terminator(\"hidden\"))] byte marker;"
                        "} Entry;\n"
                        "[semantic] typedef struct _RootView { PayloadByte Strings[]; } RootView;\n"
                        "[export, semantic(RootView), offset_map(\"strings\", stringBase)]\n"
                        "typedef struct _Root { dword stringBase; Entry entry; } Root;\n"));

    QByteArray bytes(0x40, '\0');
    writeLe32(&bytes, 0, 0x20);
    writeLe32(&bytes, 4, 3);
    bytes[0x23] = char(0x41);
    bytes[0x24] = char(0x42);
    bytes[0x25] = char(0);
    bytes[0x26] = char(0x43);

    auto rows = buildRows(&library, firstExported(&library), bytes);
    QVERIFY(findTopLevelNamed(rows, QStringLiteral("Root")));

    StructureRow *semantic = findSemanticRootChildNamed(rows, QStringLiteral("Semantic"));
    QVERIFY(semantic);
    StructureRow *strings = findChildNamed(semantic, QStringLiteral("Strings"));
    QVERIFY(strings);
    QCOMPARE(strings->children.size(), size_t(1));

    StructureRow *name = strings->children[0].get();
    QCOMPARE(name->name, QStringLiteral("name"));
    QCOMPARE(name->offset, QStringLiteral("00000023"));
    QCOMPARE(name->children.size(), size_t(2));
    QCOMPARE(name->children[0]->value, QStringLiteral("65"));
    QCOMPARE(name->children[1]->value, QStringLiteral("66"));
}

void StructViewSemanticRendererTests::builderEmitsAndMergesSemanticNodes()
{
    // Scenario: multiple raw rows emit lightweight semantic facts to the same
    // destination/key.
    // Expected: the renderer creates one semantic node, uses string helpers for
    // names/attributes, and updates attrs on repeated emits instead of adding a
    // duplicate node or rendering the raw source subtree again.
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "typedef struct _Entry {"
                        "  byte key;"
                        "  byte size;"
                        "  [emit_node(dest(Items, key(key)), field(Key, key), field(Size, size))] byte marker;"
                        "  [emit_node(dest(Items, key(key)), field(Label, fmt(\"entry {0}\", key)))] byte marker2;"
                        "} Entry;\n"
                        "[semantic] typedef struct _RootView {\n"
                        "  [element(name(concat(\"item \", Key)))]\n"
                        "  struct { byte Key; byte Size; char Label[]; } Items[];\n"
                        "} RootView;\n"
                        "[export, semantic(RootView)] typedef struct _Root { Entry entry; } Root;\n"));

    QByteArray bytes;
    bytes.append(char(7));
    bytes.append(char(12));
    bytes.append(char(0xaa));
    bytes.append(char(0xbb));

    auto rows = buildRows(&library, firstExported(&library), bytes);
    QVERIFY(findTopLevelNamed(rows, QStringLiteral("Root")));

    StructureRow *semantic = findSemanticRootChildNamed(rows, QStringLiteral("Semantic"));
    QVERIFY(semantic);
    StructureRow *items = findChildNamed(semantic, QStringLiteral("Items"));
    QVERIFY(items);
    QCOMPARE(items->children.size(), size_t(1));

    StructureRow *item = findChildNamed(items, QStringLiteral("item 7"));
    QVERIFY2(item, qPrintable(childNames(items)));
    QCOMPARE(item->offset, QStringLiteral("00000002"));
    QCOMPARE(item->children.size(), size_t(3));
    QCOMPARE(item->children[0]->name, QStringLiteral("Key"));
    QCOMPARE(item->children[0]->value, QStringLiteral("7"));
    QCOMPARE(item->children[1]->name, QStringLiteral("Size"));
    QCOMPARE(item->children[1]->value, QStringLiteral("12"));
    QCOMPARE(item->children[2]->name, QStringLiteral("Label"));
    QCOMPARE(item->children[2]->value, QStringLiteral("entry 7"));
    QVERIFY(!findDescendantNamed(item, QStringLiteral("byte marker")));
}

void StructViewSemanticRendererTests::builderRejectsUnterminatedCstrSemanticNames()
{
    // Scenario: cstr(...) is used as a semantic row key/name but the target
    // bytes are not NUL-terminated within the requested cap.
    // Expected: the semantic row is suppressed instead of showing garbage text.
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "typedef byte PayloadByte;\n"
                        "typedef struct _Entry {"
                        "  dword good;"
                        "  dword bad;"
                        "  [emit_row(dest(Strings, key(cstr(good, 8)), name(cstr(good, 8))), offset(good))] byte goodMarker;"
                        "  [emit_row(dest(Strings, key(cstr(bad, 4)), name(cstr(bad, 4))), offset(bad))] byte badMarker;"
                        "} Entry;\n"
                        "[semantic] typedef struct _RootView { PayloadByte Strings[]; } RootView;\n"
                        "[export, semantic(RootView)] typedef struct _Root { Entry entry; } Root;\n"));

    QByteArray bytes(0x40, '\0');
    writeLe32(&bytes, 0, 0x20);
    writeLe32(&bytes, 4, 0x30);
    writeAscii(&bytes, 0x20, "OK");
    bytes[0x30] = 'B';
    bytes[0x31] = 'A';
    bytes[0x32] = 'D';
    bytes[0x33] = '!';

    auto rows = buildRows(&library, firstExported(&library), bytes);
    QVERIFY(findTopLevelNamed(rows, QStringLiteral("Root")));

    StructureRow *semantic = findSemanticRootChildNamed(rows, QStringLiteral("Semantic"));
    QVERIFY(semantic);
    StructureRow *strings = findChildNamed(semantic, QStringLiteral("Strings"));
    QVERIFY(strings);
    QCOMPARE(strings->children.size(), size_t(1));
    QCOMPARE(strings->children[0]->name, QStringLiteral("OK"));
}

void StructViewSemanticRendererTests::builderEmitsIntoMappedSemanticContainers()
{
    // Scenario: a PE-like format emits section payloads as semantic containers
    // with an RVA map, then emits a data-directory table by RVA.
    // Expected: the table is attached under the semantic section whose map owns
    // the target RVA, not under a flat root-level destination.
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "typedef byte PayloadByte;\n"
                        "enum DirKind { ExportDir = 0, ImportDir = 1 };\n"
                        "typedef struct _ImportDesc { dword thunk; } ImportDesc;\n"
                        "[semantic] typedef struct _SectionView { ImportDesc Imports[]; PayloadByte Bytes[]; } SectionView;\n"
                        "[semantic(\"Image\")] typedef struct _RootView { [element(tree(\"flatten\"))] SectionView Sections[]; } RootView;\n"
                        "typedef struct _Dir { dword va; dword size; } Dir;\n"
                        "[offset_map(va, size, raw),\n"
                        " emit_row(dest(Sections, key(Name), name(Name)), offset(raw), map(\"rva\", va, size, raw)),\n"
                        " emit(dest(Sections.Bytes), type(PayloadByte), offset(\"rva\", va), count(size))]\n"
                        "typedef struct _Section { char Name[8]; dword va; dword size; dword raw; } Section;\n"
                        "[export, semantic(RootView)]\n"
                        "struct Root {\n"
                        "  [element(name(DirKind), emit(case(ImportDir), dest(Sections.Imports), label(\"Import Descriptors\"), type(ImportDesc), offset(\"rva\", va), max_count(size / sizeof(ImportDesc)), terminated_by(thunk == 0), terminator(\"hidden\")))] Dir dirs[2];\n"
                        "  [count(2)] Section sections[];\n"
                        "} root;\n"));

    QByteArray bytes(0xa0, '\0');
    writeLe32(&bytes, 8, 0x1200);  // import directory RVA
    writeLe32(&bytes, 12, 8);      // one descriptor plus hidden terminator
    writeAscii(&bytes, 16, ".text");
    writeLe32(&bytes, 24, 0x1000);
    writeLe32(&bytes, 28, 0x20);
    writeLe32(&bytes, 32, 0x40);
    writeAscii(&bytes, 36, ".idata");
    writeLe32(&bytes, 44, 0x1200);
    writeLe32(&bytes, 48, 0x20);
    writeLe32(&bytes, 52, 0x80);
    writeLe32(&bytes, 0x80, 0x12345678);
    writeLe32(&bytes, 0x84, 0);

    auto rows = buildRows(&library, firstExported(&library), bytes);
    QVERIFY(findTopLevelNamed(rows, QStringLiteral("struct Root root")));

    StructureRow *image = findSemanticRootChildNamed(rows, QStringLiteral("Image"));
    QVERIFY2(image, "Image semantic child row not found");
    QVERIFY(!findChildNamed(image, QStringLiteral("Sections")));
    QCOMPARE(image->children.size(), size_t(2));
    QCOMPARE(image->children[0]->name, QStringLiteral(".text"));
    QCOMPARE(image->children[0]->offset, QStringLiteral("00000040"));
    QCOMPARE(image->children[0]->byteLength, uint64_t(0x20));
    QCOMPARE(image->children[0]->branchIconPath, QString::fromLatin1(StructureBranchIcons::kGrayStructure));
    QCOMPARE(image->children[1]->name, QStringLiteral(".idata"));
    QCOMPARE(image->children[1]->offset, QStringLiteral("00000080"));
    QCOMPARE(image->children[1]->byteLength, uint64_t(0x20));
    QCOMPARE(image->children[1]->branchIconPath, QString::fromLatin1(StructureBranchIcons::kBlueStructure));
    QCOMPARE(image->children[1]->branchOpenIconPath, QString::fromLatin1(StructureBranchIcons::kBlueStructureOpen));
    QCOMPARE(image->children[1]->branchEmptyIconPath, QString::fromLatin1(StructureBranchIcons::kGrayStructure));

    QCOMPARE(image->children[1]->children.size(), size_t(2));
    QCOMPARE(image->children[1]->children[0]->name, QStringLiteral("Imports"));
    QCOMPARE(image->children[1]->children[1]->name, QStringLiteral("Bytes"));
    StructureRow *bytesGroup = findChildNamed(image->children[1].get(), QStringLiteral("Bytes"));
    QVERIFY2(bytesGroup, qPrintable(childNames(image->children[1].get())));
    QCOMPARE(bytesGroup->branchIconPath, QString::fromLatin1(StructureBranchIcons::kBlueElementArray));
    QCOMPARE(bytesGroup->branchOpenIconPath, QString::fromLatin1(StructureBranchIcons::kBlueElementArray));
    QCOMPARE(bytesGroup->branchEmptyIconPath, QString::fromLatin1(StructureBranchIcons::kGrayElementArray));
    QCOMPARE(bytesGroup->children.size(), size_t(32));
    QCOMPARE(bytesGroup->children[0]->name, QStringLiteral("[0]"));
    QVERIFY(bytesGroup->children[0]->branchIconPath.isEmpty());

    StructureRow *importsGroup = findChildNamed(image->children[1].get(), QStringLiteral("Imports"));
    QVERIFY2(importsGroup, qPrintable(childNames(image->children[1].get())));
    QCOMPARE(importsGroup->branchIconPath, QString::fromLatin1(StructureBranchIcons::kBlueStructure));
    StructureRow *imports = findChildNamed(importsGroup, QStringLiteral("Import Descriptors"));
    QVERIFY2(imports, qPrintable(childNames(importsGroup)));
    QCOMPARE(imports->offset, QStringLiteral("00000080"));
    QCOMPARE(imports->children.size(), size_t(1));
    QCOMPARE(imports->children[0]->branchIconPath, QString::fromLatin1(StructureBranchIcons::kBlueEntity));
    StructureRow *thunk = findChildNamed(imports->children[0].get(), QStringLiteral("dword thunk"));
    QVERIFY(thunk);
    QCOMPARE(thunk->value, QStringLiteral("305419896"));
}

void StructViewSemanticRendererTests::builderElidesImplicitScalarArrayEmitRow()
{
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "typedef byte PayloadByte;\n"
                        "typedef struct _Entry {\n"
                        "  dword offset;\n"
                        "  dword size;\n"
                        "  [emit(dest(Bytes), type(PayloadByte), offset(offset), count(size))] byte marker;\n"
                        "} Entry;\n"
                        "[semantic] typedef struct _RootView { PayloadByte Bytes[]; } RootView;\n"
                        "[export, semantic(RootView)] typedef struct _Root { Entry entry; } Root;\n"));

    QByteArray bytes(0x20, '\0');
    writeLe32(&bytes, 0, 0x10);
    writeLe32(&bytes, 4, 3);
    bytes[0x10] = char(0xaa);
    bytes[0x11] = char(0xbb);
    bytes[0x12] = char(0xcc);

    auto rows = buildRows(&library, firstExported(&library), bytes);
    StructureRow *semantic = findSemanticRootChildNamed(rows, QStringLiteral("Semantic"));
    QVERIFY(semantic);
    StructureRow *bytesGroup = findChildNamed(semantic, QStringLiteral("Bytes"));
    QVERIFY(bytesGroup);
    QCOMPARE(bytesGroup->children.size(), size_t(3));
    QCOMPARE(bytesGroup->children[0]->name, QStringLiteral("[0]"));
    QCOMPARE(bytesGroup->children[0]->value, QStringLiteral("170"));
    QVERIFY(!findChildNamed(bytesGroup, QStringLiteral("PayloadByte")));
}

void StructViewSemanticRendererTests::builderStopsSemanticEmitRowsWhenElementsAreUnreadable()
{
    // Scenario: a file matches the selected root, but later fields claim a
    // semantic payload lives outside readable data with a huge count. Expected:
    // the semantic array contributes no children and the renderer returns
    // promptly. Regression guard: semantic overlays need the same malformed
    // data protections as raw and dynamic arrays.
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "typedef byte PayloadByte;\n"
                        "typedef struct _Entry {"
                        "  dword offset;"
                        "  dword size;"
                        "  [emit(dest(Bytes), type(PayloadByte), offset(offset), count(size))] byte marker;"
                        "} Entry;\n"
                        "[semantic] typedef struct _RootView { PayloadByte Bytes[]; } RootView;\n"
                        "[export, semantic(RootView)] typedef struct _Root { Entry entry; } Root;\n"));

    QByteArray bytes(9, '\0');
    writeLe32(&bytes, 0, 0x100000);
    writeLe32(&bytes, 4, 1000000);

    auto rows = buildRows(&library, firstExported(&library), bytes);
    StructureRow *semantic = findSemanticRootChildNamed(rows, QStringLiteral("Semantic"));
    QVERIFY(semantic);
    StructureRow *bytesGroup = findChildNamed(semantic, QStringLiteral("Bytes"));
    QVERIFY(bytesGroup);
    QCOMPARE(bytesGroup->children.size(), size_t(0));
}

void StructViewSemanticRendererTests::builderAddressesPositionalSemanticCollections()
{
    // Scenario: two filtered append sequences allocate one shared destination,
    // while an earlier source row contributes through absolute and
    // sequence-relative indexes.
    // Expected: append order defines the complete array, optional filtering
    // keeps sequence indexes dense, forward item(...) contributions merge
    // fields, and invalid indexes or missing sequences add nothing.
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "["
                        " emit_node(dest(Items, item(absoluteIndex)), field(Absolute, absoluteValue)),"
                        " emit_node(dest(Items, item(\"left\", sequenceIndex)), field(Relative, sequenceValue)),"
                        " emit_node(dest(Items, item(\"right\", 0)), field(Other, rightValue)),"
                        " emit_node(dest(Items, item(-1)), field(Other, 251)),"
                        " emit_node(dest(Items, item(99)), field(Other, 252)),"
                        " emit_node(dest(Items, item(\"missing\", 0)), field(Other, 253))"
                        "] typedef struct _Contribution {"
                        " byte absoluteIndex; byte sequenceIndex; byte absoluteValue; byte sequenceValue; byte rightValue;"
                        "} Contribution;\n"
                        "["
                        " emit_node(dest(Items, append(\"left\")), optional(kind == 1), field(Id, id), field(Source, \"left\")),"
                        " emit_node(dest(Items, append(\"right\")), optional(kind == 2), field(Id, id), field(Source, \"right\"))"
                        "] typedef struct _Seed { byte kind; byte id; } Seed;\n"
                        "[semantic] typedef struct _View {"
                        " [element(name(fmt(\"item {0}\", Id)))] struct {"
                        "  byte Id; char Source[]; byte Absolute; byte Relative; byte Other;"
                        " } Items[];"
                        "} View;\n"
                        "[export, semantic(View)] typedef struct _Root { Contribution contributions[1]; Seed seeds[4]; } Root;\n"));
    QVERIFY2(StructureRenderEngine::validateStaticFieldReferences(&library).isEmpty(),
             qPrintable(StructureRenderEngine::validateStaticFieldReferences(&library).join(QLatin1Char('\n'))));

    QByteArray bytes;
    bytes.append(char(2));  // absolute item 2
    bytes.append(char(1));  // left-sequence item 1
    bytes.append(char(77));
    bytes.append(char(88));
    bytes.append(char(66));
    bytes.append(char(1)); bytes.append(char(10));
    bytes.append(char(0)); bytes.append(char(99)); // filtered out of both sequences
    bytes.append(char(2)); bytes.append(char(20));
    bytes.append(char(1)); bytes.append(char(30));

    auto rows = buildRows(&library, firstExported(&library), bytes);
    StructureRow *semantic = findSemanticRootChildNamed(rows, QStringLiteral("Semantic"));
    QVERIFY(semantic);
    StructureRow *items = findChildNamed(semantic, QStringLiteral("Items"));
    QVERIFY(items);
    QCOMPARE(items->children.size(), size_t(3));

    StructureRow *item10 = items->children[0].get();
    StructureRow *item20 = items->children[1].get();
    StructureRow *item30 = items->children[2].get();
    QCOMPARE(item10->name, QStringLiteral("item 10"));
    QCOMPARE(item20->name, QStringLiteral("item 20"));
    QCOMPARE(item30->name, QStringLiteral("item 30"));
    QCOMPARE(findChildNamed(item10, QStringLiteral("Source"))->value, QStringLiteral("left"));
    QCOMPARE(findChildNamed(item20, QStringLiteral("Source"))->value, QStringLiteral("right"));
    StructureRow *other = findChildNamed(item20, QStringLiteral("Other"));
    QVERIFY2(other, qPrintable(childNames(item20)));
    QCOMPARE(other->value, QStringLiteral("66"));
    StructureRow *absolute = findChildNamed(item30, QStringLiteral("Absolute"));
    QVERIFY2(absolute, qPrintable(childNames(item30)));
    QCOMPARE(absolute->value, QStringLiteral("77"));
    StructureRow *relative = findChildNamed(item30, QStringLiteral("Relative"));
    QVERIFY2(relative, qPrintable(childNames(item30)));
    QCOMPARE(relative->value, QStringLiteral("88"));
    QVERIFY(!findChildNamed(item10, QStringLiteral("Other")));
    QVERIFY(!findChildNamed(item30, QStringLiteral("Other")));
}

void StructViewSemanticRendererTests::builderUsesPositionalCollectionsForParallelTables()
{
    // Scenario: a generic binary stores a name table before the records it
    // describes, with each name carrying a record ordinal.
    // Expected: the early parallel table updates later allocations without any
    // format-specific renderer support.
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "[emit_node(dest(Records, item(target)), field(Name, fmt(\"name {0}\", code)))]"
                        " typedef struct _NameEntry { byte target; byte code; } NameEntry;\n"
                        "[emit_node(dest(Records, append(\"records\")), field(Id, id), field(Size, size))]"
                        " typedef struct _Record { byte id; byte size; } Record;\n"
                        "[semantic] typedef struct _View {"
                        " [element(name(Name))] struct { byte Id; byte Size; char Name[]; } Records[];"
                        "} View;\n"
                        "[export, semantic(View)] typedef struct _Root { NameEntry names[2]; Record records[2]; } Root;\n"));

    QByteArray bytes;
    bytes.append(char(1)); bytes.append(char(20));
    bytes.append(char(0)); bytes.append(char(10));
    bytes.append(char(7)); bytes.append(char(3));
    bytes.append(char(9)); bytes.append(char(5));

    auto rows = buildRows(&library, firstExported(&library), bytes);
    StructureRow *semantic = findSemanticRootChildNamed(rows, QStringLiteral("Semantic"));
    QVERIFY(semantic);
    StructureRow *records = findChildNamed(semantic, QStringLiteral("Records"));
    QVERIFY(records);
    QCOMPARE(records->children.size(), size_t(2));
    QCOMPARE(records->children[0]->name, QStringLiteral("name 10"));
    QCOMPARE(records->children[1]->name, QStringLiteral("name 20"));
    QCOMPARE(findChildNamed(records->children[0].get(), QStringLiteral("Id"))->value, QStringLiteral("7"));
    QCOMPARE(findChildNamed(records->children[1].get(), QStringLiteral("Size"))->value, QStringLiteral("5"));
}

void StructViewSemanticRendererTests::definitionManagerFlagsUnknownSemanticDestinations()
{
    // Scenario: an emit destination is misspelled relative to the root's
    // attached semantic schema.
    // Expected: the static definition validator reports the bad destination.
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "typedef byte PayloadByte;\n"
                        "typedef struct _Entry { [emit(dest(Missing), type(PayloadByte), offset(0), count(1))] byte marker; } Entry;\n"
                        "[semantic] typedef struct _RootView { PayloadByte Payloads[]; } RootView;\n"
                        "[export, semantic(RootView)] struct Root { Entry entry; } root;\n"));

    const QStringList errors = StructureRenderEngine::validateStaticFieldReferences(&library);
    QVERIFY(errors.join(QLatin1Char('\n')).contains(QStringLiteral("emit(dest(Missing))")));
}

void StructViewSemanticRendererTests::definitionManagerFlagsUnknownSemanticNodeFields()
{
    // Scenario: emit_node(field(...)) names a field that is not declared in the
    // destination element schema.
    // Expected: the static definition validator reports the bad semantic field.
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "typedef struct _Entry { [emit_node(dest(Items), field(Missing, marker))] byte marker; } Entry;\n"
                        "[semantic] typedef struct _RootView { struct { byte Name; } Items[]; } RootView;\n"
                        "[export, semantic(RootView)] struct Root { Entry entry; } root;\n"));

    const QStringList errors = StructureRenderEngine::validateStaticFieldReferences(&library);
    QVERIFY2(errors.join(QLatin1Char('\n')).contains(QStringLiteral("field(Missing")),
             qPrintable(errors.join(QLatin1Char('\n'))));
}

void StructViewSemanticRendererTests::definitionManagerValidatesPositionalSemanticDestinations()
{
    // Valid positional forms coexist on one destination.
    StrataLibrary validLibrary;
    Parser validParser(&validLibrary);
    QVERIFY(parseBuffer(validParser,
                        "[semantic] typedef struct _View { struct { byte Value; } Items[]; } View;\n"
                        "[export, semantic(View)] typedef struct _Root {"
                        " [emit_node(dest(Items, append(\"rows\")), field(Value, value))] byte value;"
                        " [emit_node(dest(Items, item(\"rows\", index)), field(Value, value))] byte index;"
                        " [emit_node(dest(Items, item(index)), field(Value, value))] byte marker;"
                        "} Root;\n"));
    const QStringList validErrors = StructureRenderEngine::validateStaticFieldReferences(&validLibrary);
    QVERIFY2(validErrors.isEmpty(), qPrintable(validErrors.join(QLatin1Char('\n'))));

    // Malformed counts, non-literal/empty sequence names, and a string absolute
    // index are rejected when definitions load.
    StrataLibrary malformedLibrary;
    Parser malformedParser(&malformedLibrary);
    QVERIFY(parseBuffer(malformedParser,
                        "[semantic] typedef struct _View { struct { byte Value; } Items[]; } View;\n"
                        "[export, semantic(View)] typedef struct _Root {"
                        " [emit_node(dest(Items, append(1)), field(Value, value))] byte value;"
                        " [emit_node(dest(Items, append(\"\")), field(Value, value))] byte a;"
                        " [emit_node(dest(Items, item(\"rows\")), field(Value, value))] byte b;"
                        " [emit_node(dest(Items, item(\"rows\", 0, 1)), field(Value, value))] byte c;"
                        "} Root;\n"));
    const QString malformedErrors = StructureRenderEngine::validateStaticFieldReferences(&malformedLibrary)
                                        .join(QLatin1Char('\n'));
    QVERIFY2(malformedErrors.contains(QStringLiteral("malformed")), qPrintable(malformedErrors));

    // Once a destination is positional, keyed and ordinary row creation would
    // make its indexes ambiguous and is therefore rejected.
    StrataLibrary mixedLibrary;
    Parser mixedParser(&mixedLibrary);
    QVERIFY(parseBuffer(mixedParser,
                        "[semantic] typedef struct _View { struct { byte Value; } Items[]; } View;\n"
                        "[export, semantic(View)] typedef struct _Root {"
                        " [emit_node(dest(Items, append(\"rows\")), field(Value, value))] byte value;"
                        " [emit_node(dest(Items, key(value)), field(Value, value))] byte keyed;"
                        " [emit_node(dest(Items), field(Value, value))] byte ordinary;"
                        "} Root;\n"));
    const QString mixedErrors = StructureRenderEngine::validateStaticFieldReferences(&mixedLibrary)
                                    .join(QLatin1Char('\n'));
    QVERIFY2(mixedErrors.contains(QStringLiteral("mixes positional")), qPrintable(mixedErrors));

    // Positional destination addresses belong only to emit_node(...).
    StrataLibrary wrongTagLibrary;
    Parser wrongTagParser(&wrongTagLibrary);
    QVERIFY(parseBuffer(wrongTagParser,
                        "typedef byte Payload;\n"
                        "[semantic] typedef struct _View { Payload Items[]; } View;\n"
                        "[export, semantic(View)] typedef struct _Root {"
                        " [emit(dest(Items, append(\"rows\")), type(Payload), offset(0), count(1))] byte emitted;"
                        " [emit_row(dest(Items, item(0)))] byte row;"
                        "} Root;\n"));
    const QString wrongTagErrors = StructureRenderEngine::validateStaticFieldReferences(&wrongTagLibrary)
                                       .join(QLatin1Char('\n'));
    QVERIFY2(wrongTagErrors.contains(QStringLiteral("only valid on emit_node")), qPrintable(wrongTagErrors));
}

void StructViewSemanticRendererTests::semanticRegistryRunsKnownViewsAndIgnoresUnknownViews()
{
    // Scenario: semantic rendering is an optional interpreter layer selected by
    // string ids in Strata definitions.
    // Expected: known ids can append semantic rows through the shared context,
    // while unknown ids are ignored without affecting the raw row tree.
    // Regression guard: new semantic views must be pluggable and safe, not a
    // fragile type-name switch buried inside StructureRenderEngine.
    StructureSemanticViewRegistry &registry = StructureSemanticViewRegistry::instance();
    registry.registerInterpreter(QStringLiteral("test.semantic"), [](StructureSemanticContext &context) {
        context.appendSemanticRow(context.currentRow(), QStringLiteral("semantic row"), QStringLiteral("value"));
    });

    StructureRow root;
    root.name = QStringLiteral("root");
    std::vector<StructureOffsetMap> maps;
    StructureSemanticContext context(nullptr, &root, &root, 0, {}, maps);

    QVERIFY(!registry.run(QStringLiteral("missing.semantic"), context));
    QCOMPARE(root.children.size(), size_t(0));

    QVERIFY(registry.run(QStringLiteral("test.semantic"), context));
    QCOMPARE(root.children.size(), size_t(1));
    QCOMPARE(static_cast<int>(root.children[0]->kind), static_cast<int>(StructureRowKind::Semantic));
    QCOMPARE(root.children[0]->name, QStringLiteral("semantic row"));
}

REGISTER_STRUCTVIEW_TEST(StructViewSemanticRendererTests)
#include "semantic_renderer_tests.moc"
