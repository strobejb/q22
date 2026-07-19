#include "../structview_testsupport.h"

class StructViewDexWasmTests : public QObject
{
    Q_OBJECT

private slots:
    void builderRendersDexHeaderAndTables();
    void builderAddsDexSemanticSummaryPastArrayRenderCap();
    void builderRendersWasmHeaderAndSections();
    void builderKeepsWasmCodeTargetsPastRawArrayCap();
    void builderRendersWasmStartTagsAndElements();
};

void StructViewDexWasmTests::builderRendersDexHeaderAndTables()
{
    // Scenario: DEX files are mostly a header plus offset-counted ID tables.
    // Expected: the standard DEX definition renders the fixed header, ID tables,
    // class definitions, and map list from the offsets in the header.
    StrataLibrary library;
    QVERIFY2(parseStandardDefinition(&library, QStringLiteral("dex.strata")), "dex.strata failed to parse");
    TypeDecl *dexRoot = exportedNamed(&library, QStringLiteral("DEX"));
    QVERIFY(dexRoot);

    QByteArray dex(0x110, '\0');
    dex[0] = 'd';
    dex[1] = 'e';
    dex[2] = 'x';
    dex[3] = '\n';
    dex[4] = '0';
    dex[5] = '3';
    dex[6] = '5';

    writeLe32(&dex, 0x20, 0x110);      // file_size
    writeLe32(&dex, 0x24, 0x70);       // header_size
    writeLe32(&dex, 0x28, 0x12345678); // endian_tag
    writeLe32(&dex, 0x34, 0xb4);       // map_off
    writeLe32(&dex, 0x38, 1);          // string_ids_size
    writeLe32(&dex, 0x3c, 0x70);       // string_ids_off
    writeLe32(&dex, 0x40, 1);          // type_ids_size
    writeLe32(&dex, 0x44, 0x74);       // type_ids_off
    writeLe32(&dex, 0x48, 1);          // proto_ids_size
    writeLe32(&dex, 0x4c, 0x78);       // proto_ids_off
    writeLe32(&dex, 0x50, 1);          // field_ids_size
    writeLe32(&dex, 0x54, 0x84);       // field_ids_off
    writeLe32(&dex, 0x58, 1);          // method_ids_size
    writeLe32(&dex, 0x5c, 0x8c);       // method_ids_off
    writeLe32(&dex, 0x60, 1);          // class_defs_size
    writeLe32(&dex, 0x64, 0x94);       // class_defs_off
    writeLe32(&dex, 0x68, 0x5c);       // data_size
    writeLe32(&dex, 0x6c, 0xb4);       // data_off

    writeLe32(&dex, 0x70, 0xc8);       // string_data_off
    writeLe32(&dex, 0x74, 0);          // descriptor_idx
    writeLe32(&dex, 0x78, 0);          // shorty_idx
    writeLe32(&dex, 0x7c, 0);          // return_type_idx
    writeLe32(&dex, 0x80, 0xd0);       // parameters_off
    writeLe16(&dex, 0x84, 0);          // field class_idx
    writeLe16(&dex, 0x86, 0);          // field type_idx
    writeLe32(&dex, 0x88, 0);          // field name_idx
    writeLe16(&dex, 0x8c, 0);          // method class_idx
    writeLe16(&dex, 0x8e, 0);          // method proto_idx
    writeLe32(&dex, 0x90, 0);          // method name_idx
    writeLe32(&dex, 0x94, 0);          // class_idx
    writeLe32(&dex, 0x98, 0x00000401); // public abstract
    writeLe32(&dex, 0x9c, 0xffffffff); // no superclass

    writeLe32(&dex, 0xb4, 7);          // map_list size
    qsizetype map = 0xb8;
    auto writeMapItem = [&](quint16 type, quint32 size, quint32 offset) {
        writeLe16(&dex, map + 0, type);
        writeLe16(&dex, map + 2, 0);
        writeLe32(&dex, map + 4, size);
        writeLe32(&dex, map + 8, offset);
        map += 12;
    };
    writeMapItem(0x0000, 1, 0x00);
    writeMapItem(0x0001, 1, 0x70);
    writeMapItem(0x0002, 1, 0x74);
    writeMapItem(0x0003, 1, 0x78);
    writeMapItem(0x0004, 1, 0x84);
    writeMapItem(0x0005, 1, 0x8c);
    writeMapItem(0x0006, 1, 0x94);
    dex[0xc8] = char(2); // utf16_size as uleb128
    dex[0xc9] = 'H';
    dex[0xca] = 'i';
    dex[0xcb] = '\0';

    auto rows = buildRows(&library, dexRoot, dex);
    StructureRow *dexRow = findTopLevelNamed(rows, QStringLiteral("DEX"));
    QVERIFY(dexRow);

    StructureRow *header = findChildNamed(dexRow, QStringLiteral("DEX_HEADER header"));
    QVERIFY(header);
    QVERIFY(findChildNamed(header, QStringLiteral("dword file_size")));
    QCOMPARE(findChildNamed(header, QStringLiteral("dword file_size"))->value, QStringLiteral("272"));
    QCOMPARE(findChildNamed(header, QStringLiteral("dword endian_tag"))->value,
             QStringLiteral("DEX_ENDIAN_CONSTANT"));

    StructureRow *stringIds = findChildNamed(dexRow, QStringLiteral("DEX_STRING_ID_ITEM stringIds[]"));
    QVERIFY(stringIds);
    QCOMPARE(stringIds->absoluteOffset, uint64_t(0x70));
    QCOMPARE(stringIds->children.size(), size_t(1));
    QCOMPARE(findChildNamed(stringIds->children[0].get(), QStringLiteral("dword string_data_off"))->value,
             QStringLiteral("200"));
    QVERIFY(!findChildNamed(dexRow, QStringLiteral("DEX Strings")));

    StructureRow *typeIds = findChildNamed(dexRow, QStringLiteral("DEX_TYPE_ID_ITEM typeIds[]"));
    QVERIFY(typeIds);
    QCOMPARE(typeIds->children.size(), size_t(1));
    QVERIFY(!findChildNamed(dexRow, QStringLiteral("DEX Types")));

    StructureRow *protoIds = findChildNamed(dexRow, QStringLiteral("DEX_PROTO_ID_ITEM protoIds[]"));
    QVERIFY(protoIds);
    QCOMPARE(protoIds->children.size(), size_t(1));
    QVERIFY(findChildNamed(protoIds->children[0].get(), QStringLiteral("dword shorty_idx")));
    QVERIFY(!findChildNamed(dexRow, QStringLiteral("DEX Protos")));

    StructureRow *fieldIds = findChildNamed(dexRow, QStringLiteral("DEX_FIELD_ID_ITEM fieldIds[]"));
    QVERIFY(fieldIds);
    QCOMPARE(fieldIds->children.size(), size_t(1));
    QVERIFY(!findChildNamed(dexRow, QStringLiteral("DEX Fields")));

    StructureRow *methodIds = findChildNamed(dexRow, QStringLiteral("DEX_METHOD_ID_ITEM methodIds[]"));
    QVERIFY(methodIds);
    QCOMPARE(methodIds->children.size(), size_t(1));
    QVERIFY(!findChildNamed(dexRow, QStringLiteral("DEX Methods")));

    StructureRow *classDefs = findChildNamed(dexRow, QStringLiteral("DEX_CLASS_DEF_ITEM classDefs[]"));
    QVERIFY(classDefs);
    QCOMPARE(classDefs->absoluteOffset, uint64_t(0x94));
    StructureRow *accessFlags = findChildNamed(classDefs->children[0].get(), QStringLiteral("dword access_flags"));
    QVERIFY(accessFlags);
    QVERIFY(findChildNamed(accessFlags, QStringLiteral("DEX_ACC_PUBLIC")));
    QVERIFY(findChildNamed(accessFlags, QStringLiteral("DEX_ACC_ABSTRACT")));
    QVERIFY(!findChildNamed(dexRow, QStringLiteral("DEX Classes")));

    StructureRow *mapList = findChildNamed(dexRow, QStringLiteral("DEX_MAP_LIST mapList"));
    QVERIFY(mapList);
    QCOMPARE(mapList->absoluteOffset, uint64_t(0xb4));
    StructureRow *mapItems = findChildNamed(mapList, QStringLiteral("DEX_MAP_ITEM list[]"));
    QVERIFY(mapItems);
    QCOMPARE(mapItems->children.size(), size_t(7));
    QCOMPARE(findChildNamed(mapItems->children[1].get(), QStringLiteral("word type"))->value,
             QStringLiteral("DEX_TYPE_STRING_ID_ITEM"));

    StructureRow *summary = findSemanticRootChildNamed(rows, QStringLiteral("DEX Summary"));
    QVERIFY(summary);
    StructureRow *decodedStrings = findChildNamed(summary, QStringLiteral("Decoded Strings"));
    QVERIFY(decodedStrings);
    QVERIFY(findChildNamed(decodedStrings, QStringLiteral("String[0] Hi")));
    StructureRow *decodedTypes = findChildNamed(summary, QStringLiteral("Decoded Types"));
    QVERIFY(decodedTypes);
    QVERIFY(findChildNamed(decodedTypes, QStringLiteral("Type[0] Hi")));
    StructureRow *decodedFields = findChildNamed(summary, QStringLiteral("Decoded Fields"));
    QVERIFY(decodedFields);
    StructureRow *field = findChildNamed(decodedFields, QStringLiteral("Field[0] Hi"));
    QVERIFY(field);
    QCOMPARE(field->value, QStringLiteral("Hi : Hi"));
    StructureRow *decodedMethods = findChildNamed(summary, QStringLiteral("Decoded Methods"));
    QVERIFY(decodedMethods);
    QVERIFY(findChildNamed(decodedMethods, QStringLiteral("Method[0] Hi")));
    StructureRow *decodedClasses = findChildNamed(summary, QStringLiteral("Decoded Classes"));
    QVERIFY(decodedClasses);
    StructureRow *classDef = findChildNamed(decodedClasses, QStringLiteral("Class[0] Hi"));
    QVERIFY(classDef);
    QVERIFY(classDef->value.contains(QStringLiteral("FLAGS 0X00000401")));
    QVERIFY(classDef->value.contains(QStringLiteral("source Hi")));
}

void StructViewDexWasmTests::builderAddsDexSemanticSummaryPastArrayRenderCap()
{
    // Scenario: large real-world DEX files may have readable names well after
    // the raw renderer's array preview cap.
    // Expected: the semantic DEX summary walks the header tables directly and
    // exposes decoded names even when their string_id item was not rendered.
    // Regression guard: DEX name discovery must not depend on the first 100 raw
    // stringIds[] children being useful human-readable text.
    StrataLibrary library;
    QVERIFY2(parseStandardDefinition(&library, QStringLiteral("dex.strata")), "dex.strata failed to parse");
    TypeDecl *dexRoot = exportedNamed(&library, QStringLiteral("DEX"));
    QVERIFY(dexRoot);

    constexpr quint32 kStringCount = 121;
    constexpr quint32 kReadableIndex = 120;
    constexpr qsizetype kStringIdsOff = 0x70;
    constexpr qsizetype kTypeIdsOff = kStringIdsOff + kStringCount * 4;
    constexpr qsizetype kMethodIdsOff = kTypeIdsOff + 4;
    constexpr qsizetype kStringDataOff = 0x300;

    QByteArray dex(0x430, '\0');
    dex[0] = 'd';
    dex[1] = 'e';
    dex[2] = 'x';
    dex[3] = '\n';
    dex[4] = '0';
    dex[5] = '3';
    dex[6] = '9';

    writeLe32(&dex, 0x20, quint32(dex.size()));
    writeLe32(&dex, 0x24, 0x70);
    writeLe32(&dex, 0x28, 0x12345678);
    writeLe32(&dex, 0x38, kStringCount);
    writeLe32(&dex, 0x3c, kStringIdsOff);
    writeLe32(&dex, 0x40, 1);
    writeLe32(&dex, 0x44, kTypeIdsOff);
    writeLe32(&dex, 0x58, 1);
    writeLe32(&dex, 0x5c, kMethodIdsOff);
    writeLe32(&dex, 0x68, quint32(dex.size() - kStringDataOff));
    writeLe32(&dex, 0x6c, kStringDataOff);

    for (quint32 i = 0; i < kStringCount; ++i)
    {
        const quint32 dataOffset = quint32(kStringDataOff + i * 2);
        writeLe32(&dex, kStringIdsOff + qsizetype(i) * 4, dataOffset);
        dex[dataOffset] = char(0);     // utf16_size
        dex[dataOffset + 1] = char(0); // terminator
    }

    const qsizetype readableDataOff = kStringDataOff + qsizetype(kReadableIndex) * 2;
    writeLe32(&dex, kStringIdsOff + qsizetype(kReadableIndex) * 4, quint32(readableDataOff));
    dex[readableDataOff] = char(13);
    memcpy(dex.data() + readableDataOff + 1, "   NormalName", 13);
    dex[readableDataOff + 14] = char(0);

    writeLe32(&dex, kTypeIdsOff, kReadableIndex);
    writeLe16(&dex, kMethodIdsOff, 0);
    writeLe16(&dex, kMethodIdsOff + 2, 0);
    writeLe32(&dex, kMethodIdsOff + 4, kReadableIndex);

    auto rows = buildRows(&library, dexRoot, dex);
    StructureRow *dexRow = findTopLevelNamed(rows, QStringLiteral("DEX"));
    QVERIFY(dexRow);

    StructureRow *stringIds = findChildNamed(dexRow, QStringLiteral("DEX_STRING_ID_ITEM stringIds[]"));
    QVERIFY(stringIds);
    QVERIFY(stringIds->children.size() < kStringCount);
    QVERIFY(!findDescendantNamed(stringIds, QStringLiteral("String[120] NormalName")));

    StructureRow *summary = findSemanticRootChildNamed(rows, QStringLiteral("DEX Summary"));
    QVERIFY(summary);
    StructureRow *decodedStrings = findChildNamed(summary, QStringLiteral("Decoded Strings"));
    QVERIFY(decodedStrings);
    QVERIFY(findChildNamed(decodedStrings, QStringLiteral("String[120] NormalName")));
    StructureRow *decodedTypes = findChildNamed(summary, QStringLiteral("Decoded Types"));
    QVERIFY(decodedTypes);
    QVERIFY(findChildNamed(decodedTypes, QStringLiteral("Type[0] NormalName")));
    StructureRow *decodedMethods = findChildNamed(summary, QStringLiteral("Decoded Methods"));
    QVERIFY(decodedMethods);
    QVERIFY(findChildNamed(decodedMethods, QStringLiteral("Method[0] NormalName")));
}

void StructViewDexWasmTests::builderRendersWasmHeaderAndSections()
{
    // Scenario: WebAssembly modules have a fixed header followed by an
    // EOF-terminated stream of size-prefixed sections.
    // Expected: the standard Wasm definition renders only the real sections,
    // names known section IDs, decodes custom section names, and expands the
    // common typed payload sections used by a tiny exported function module.
    StrataLibrary library;
    QVERIFY2(parseStandardDefinition(&library, QStringLiteral("wasm.strata")), "wasm.strata failed to parse");
    TypeDecl *wasmRoot = exportedNamed(&library, QStringLiteral("WASM"));
    QVERIFY(wasmRoot);

    QByteArray wasm;
    wasm.append(QByteArray::fromHex("0061736d"));     // magic
    wasm.append(QByteArray::fromHex("01000000"));     // version 1
    wasm.append(char(0x00));                          // custom section
    wasm.append(char(0x20));                          // size
    wasm.append(char(0x04));                          // name size
    wasm.append("name", 4);
    wasm.append(QByteArray::fromHex("0005"));         // module name subsection, size 5
    wasm.append(char(0x04));                          // module name length
    wasm.append("demo", 4);
    wasm.append(QByteArray::fromHex("0109"));         // function names subsection, size 9
    wasm.append(QByteArray::fromHex("0101"));         // one entry, function index 1
    wasm.append(char(0x06));                          // function name length
    wasm.append("answer", 6);
    wasm.append(QByteArray::fromHex("0407"));         // type names subsection, size 7
    wasm.append(QByteArray::fromHex("010004"));       // one entry, type index 0, name length 4
    wasm.append("sig0", 4);
    wasm.append(char(0x01));                          // type section
    wasm.append(char(0x05));                          // size
    wasm.append(QByteArray::fromHex("016000017f"));   // () -> i32
    wasm.append(char(0x02));                          // import section
    wasm.append(char(0x0d));                          // size
    wasm.append(char(0x01));                          // one import
    wasm.append(char(0x03));                          // module length
    wasm.append("env", 3);
    wasm.append(char(0x05));                          // import name length
    wasm.append("print", 5);
    wasm.append(char(0x00));                          // function import
    wasm.append(char(0x00));                          // type index 0
    wasm.append(char(0x03));                          // function section
    wasm.append(char(0x02));                          // size
    wasm.append(QByteArray::fromHex("0100"));         // one function, type 0
    wasm.append(char(0x05));                          // memory section
    wasm.append(char(0x03));                          // size
    wasm.append(QByteArray::fromHex("010001"));       // one min-only memory, min 1 page
    wasm.append(char(0x07));                          // export section
    wasm.append(char(0x0a));                          // size
    wasm.append(char(0x01));                          // one export
    wasm.append(char(0x06));                          // name length
    wasm.append("answer", 6);
    wasm.append(char(0x00));                          // function export
    wasm.append(char(0x01));                          // function index 1
    wasm.append(char(0x0a));                          // code section
    wasm.append(char(0x06));                          // size
    wasm.append(QByteArray::fromHex("010400412a0b")); // one body: i32.const 42; end
    wasm.append(char(0x0b));                          // data section
    wasm.append(char(0x06));                          // size
    wasm.append(char(0x01));                          // one data segment
    wasm.append(char(0x01));                          // passive segment
    wasm.append(char(0x03));                          // byte count
    wasm.append("abc", 3);

    auto rows = buildRows(&library, wasmRoot, wasm);
    StructureRow *wasmRow = findTopLevelNamed(rows, QStringLiteral("WASM"));
    QVERIFY(wasmRow);

    StructureRow *header = findChildNamed(wasmRow, QStringLiteral("WASM_HEADER header"));
    QVERIFY(header);
    StructureRow *version = findChildNamed(header, QStringLiteral("dword version"));
    QVERIFY2(version, qPrintable(childNames(header)));
    QCOMPARE(version->value, QStringLiteral("1"));

    StructureRow *sections = findChildNamed(wasmRow, QStringLiteral("WASM_SECTION sections[]"));
    QVERIFY(sections);
    QCOMPARE(sections->children.size(), size_t(8));

    StructureRow *customSection = sections->children[0].get();
    QCOMPARE(customSection->name, QStringLiteral("[0]WASM_SECTION_CUSTOM"));
    StructureRow *customId = findChildNamed(customSection, QStringLiteral("byte id"));
    QVERIFY2(customId, qPrintable(childNames(customSection)));
    QCOMPARE(customId->value, QStringLiteral("WASM_SECTION_CUSTOM"));
    StructureRow *customSize = findChildNamed(customSection, QStringLiteral("uleb128 size"));
    QVERIFY2(customSize, qPrintable(childNames(customSection)));
    QCOMPARE(customSize->value, QStringLiteral("32"));
    StructureRow *custom = findChildNamed(customSection, QStringLiteral("WASM_CUSTOM_SECTION custom"));
    QVERIFY2(custom, qPrintable(childNames(customSection)));
    StructureRow *customNameSize = findChildNamed(custom, QStringLiteral("uleb128 nameSize"));
    QVERIFY2(customNameSize, qPrintable(childNames(custom)));
    QCOMPARE(customNameSize->value, QStringLiteral("4"));
    StructureRow *customName = findChildNamed(custom, QStringLiteral("byte name[]"));
    QVERIFY2(customName, qPrintable(childNames(custom)));
    QCOMPARE(customName->value, QStringLiteral("\"name\""));
    StructureRow *nameSubsections = findChildNamed(custom, QStringLiteral("WASM_NAME_SUBSECTION nameSubsections[]"));
    QVERIFY2(nameSubsections, qPrintable(childNames(custom)));
    QCOMPARE(nameSubsections->children.size(), size_t(3));
    StructureRow *moduleSubsection = nameSubsections->children[0].get();
    QCOMPARE(findChildNamed(moduleSubsection, QStringLiteral("byte id"))->value, QStringLiteral("WASM_NAME_MODULE"));
    StructureRow *moduleName = findChildNamed(moduleSubsection, QStringLiteral("WASM_NAME module"));
    QVERIFY2(moduleName, qPrintable(childNames(moduleSubsection)));
    QCOMPARE(findChildNamed(moduleName, QStringLiteral("byte bytes[]"))->value, QStringLiteral("\"demo\""));
    StructureRow *functionNameSubsection = nameSubsections->children[1].get();
    QCOMPARE(findChildNamed(functionNameSubsection, QStringLiteral("byte id"))->value, QStringLiteral("WASM_NAME_FUNCTION"));
    StructureRow *functionNameMap = findChildNamed(functionNameSubsection, QStringLiteral("WASM_FUNCTION_NAME_MAP functions"));
    QVERIFY2(functionNameMap, qPrintable(childNames(functionNameSubsection)));
    StructureRow *functionNames = findChildNamed(functionNameMap, QStringLiteral("WASM_FUNCTION_NAME_ASSOC names[]"));
    QVERIFY2(functionNames, qPrintable(childNames(functionNameMap)));
    QCOMPARE(functionNames->children.size(), size_t(1));
    QCOMPARE(functionNames->children[0]->name, QStringLiteral("[0]answer"));
    QCOMPARE(findChildNamed(functionNames->children[0].get(), QStringLiteral("uleb128 index"))->value, QStringLiteral("1"));
    StructureRow *functionDebugName = findChildNamed(functionNames->children[0].get(), QStringLiteral("WASM_NAME name"));
    QVERIFY2(functionDebugName, qPrintable(childNames(functionNames->children[0].get())));
    QCOMPARE(findChildNamed(functionDebugName, QStringLiteral("byte bytes[]"))->value, QStringLiteral("\"answer\""));

    StructureRow *typeSection = sections->children[1].get();
    QCOMPARE(typeSection->name, QStringLiteral("[1]WASM_SECTION_TYPE"));
    StructureRow *typeId = findChildNamed(typeSection, QStringLiteral("byte id"));
    QVERIFY2(typeId, qPrintable(childNames(typeSection)));
    QCOMPARE(typeId->value, QStringLiteral("WASM_SECTION_TYPE"));
    StructureRow *typeSize = findChildNamed(typeSection, QStringLiteral("uleb128 size"));
    QVERIFY2(typeSize, qPrintable(childNames(typeSection)));
    QCOMPARE(typeSize->value, QStringLiteral("5"));
    StructureRow *typePayload = findChildNamed(typeSection, QStringLiteral("WASM_TYPE_SECTION type"));
    QVERIFY2(typePayload, qPrintable(childNames(typeSection)));
    StructureRow *typeCount = findChildNamed(typePayload, QStringLiteral("uleb128 count"));
    QVERIFY2(typeCount, qPrintable(childNames(typePayload)));
    QCOMPARE(typeCount->value, QStringLiteral("1"));
    StructureRow *functionTypes = findChildNamed(typePayload, QStringLiteral("WASM_FUNCTION_TYPE types[]"));
    QVERIFY2(functionTypes, qPrintable(childNames(typePayload)));
    QCOMPARE(functionTypes->children.size(), size_t(1));
    StructureRow *typeForm = findChildNamed(functionTypes->children[0].get(), QStringLiteral("byte form"));
    QVERIFY2(typeForm, qPrintable(childNames(functionTypes->children[0].get())));
    QCOMPARE(typeForm->value, QStringLiteral("WASM_TYPE_FORM_FUNC"));

    StructureRow *importSection = sections->children[2].get();
    QCOMPARE(importSection->name, QStringLiteral("[2]WASM_SECTION_IMPORT"));
    StructureRow *importId = findChildNamed(importSection, QStringLiteral("byte id"));
    QVERIFY2(importId, qPrintable(childNames(importSection)));
    QCOMPARE(importId->value, QStringLiteral("WASM_SECTION_IMPORT"));
    StructureRow *importPayload = findChildNamed(importSection, QStringLiteral("WASM_IMPORT_SECTION import"));
    QVERIFY2(importPayload, qPrintable(childNames(importSection)));
    StructureRow *imports = findChildNamed(importPayload, QStringLiteral("WASM_IMPORT imports[]"));
    QVERIFY2(imports, qPrintable(childNames(importPayload)));
    QCOMPARE(imports->children[0]->name, QStringLiteral("[0]print"));
    StructureRow *importModule = findChildNamed(imports->children[0].get(), QStringLiteral("WASM_NAME module"));
    QVERIFY2(importModule, qPrintable(childNames(imports->children[0].get())));
    QCOMPARE(findChildNamed(importModule, QStringLiteral("byte bytes[]"))->value, QStringLiteral("\"env\""));
    StructureRow *importName = findChildNamed(imports->children[0].get(), QStringLiteral("WASM_NAME name"));
    QVERIFY2(importName, qPrintable(childNames(imports->children[0].get())));
    QCOMPARE(findChildNamed(importName, QStringLiteral("byte bytes[]"))->value, QStringLiteral("\"print\""));

    StructureRow *functionSection = sections->children[3].get();
    QCOMPARE(functionSection->name, QStringLiteral("[3]WASM_SECTION_FUNCTION"));
    StructureRow *functionId = findChildNamed(functionSection, QStringLiteral("byte id"));
    QVERIFY2(functionId, qPrintable(childNames(functionSection)));
    QCOMPARE(functionId->value, QStringLiteral("WASM_SECTION_FUNCTION"));
    StructureRow *functionPayload = findChildNamed(functionSection, QStringLiteral("WASM_FUNCTION_SECTION function"));
    QVERIFY2(functionPayload, qPrintable(childNames(functionSection)));
    StructureRow *typeIndices = findChildNamed(functionPayload, QStringLiteral("WASM_DEFINED_FUNCTION typeIndices[]"));
    QVERIFY2(typeIndices, qPrintable(childNames(functionPayload)));
    QCOMPARE(typeIndices->children.size(), size_t(1));
    QCOMPARE(findChildNamed(typeIndices->children[0].get(), QStringLiteral("uleb128 typeIndex"))->value, QStringLiteral("0"));

    StructureRow *memorySection = sections->children[4].get();
    QCOMPARE(memorySection->name, QStringLiteral("[4]WASM_SECTION_MEMORY"));
    StructureRow *memoryId = findChildNamed(memorySection, QStringLiteral("byte id"));
    QVERIFY2(memoryId, qPrintable(childNames(memorySection)));
    QCOMPARE(memoryId->value, QStringLiteral("WASM_SECTION_MEMORY"));
    StructureRow *memoryPayload = findChildNamed(memorySection, QStringLiteral("WASM_MEMORY_SECTION memory"));
    QVERIFY2(memoryPayload, qPrintable(childNames(memorySection)));
    StructureRow *memories = findChildNamed(memoryPayload, QStringLiteral("WASM_MEMORY_TYPE memories[]"));
    QVERIFY2(memories, qPrintable(childNames(memoryPayload)));
    StructureRow *memoryMin = findDescendantNamed(memories->children[0].get(), QStringLiteral("uleb128 min"));
    QVERIFY2(memoryMin, qPrintable(childNames(memories->children[0].get())));
    QCOMPARE(memoryMin->value, QStringLiteral("1"));

    StructureRow *exportSection = sections->children[5].get();
    QCOMPARE(exportSection->name, QStringLiteral("[5]WASM_SECTION_EXPORT"));
    StructureRow *exportId = findChildNamed(exportSection, QStringLiteral("byte id"));
    QVERIFY2(exportId, qPrintable(childNames(exportSection)));
    QCOMPARE(exportId->value, QStringLiteral("WASM_SECTION_EXPORT"));
    StructureRow *exportPayload = findChildNamed(exportSection, QStringLiteral("WASM_EXPORT_SECTION export"));
    QVERIFY2(exportPayload, qPrintable(childNames(exportSection)));
    StructureRow *exports = findChildNamed(exportPayload, QStringLiteral("WASM_EXPORT exports[]"));
    QVERIFY2(exports, qPrintable(childNames(exportPayload)));
    QCOMPARE(exports->children[0]->name, QStringLiteral("[0]answer"));
    StructureRow *exportName = findChildNamed(exports->children[0].get(), QStringLiteral("WASM_NAME name"));
    QVERIFY2(exportName, qPrintable(childNames(exports->children[0].get())));
    StructureRow *exportNameBytes = findChildNamed(exportName, QStringLiteral("byte bytes[]"));
    QVERIFY2(exportNameBytes, qPrintable(childNames(exportName)));
    QCOMPARE(exportNameBytes->value, QStringLiteral("\"answer\""));
    StructureRow *exportTarget = findChildNamed(exports->children[0].get(), QStringLiteral("WASM_EXTERN_INDEX target"));
    QVERIFY2(exportTarget, qPrintable(childNames(exports->children[0].get())));
    StructureRow *exportKind = findChildNamed(exportTarget, QStringLiteral("byte kind"));
    QVERIFY2(exportKind, qPrintable(childNames(exportTarget)));
    QCOMPARE(exportKind->value, QStringLiteral("WASM_EXTERN_FUNC"));
    StructureRow *exportIndex = findChildNamed(exportTarget, QStringLiteral("uleb128 index"));
    QVERIFY2(exportIndex, qPrintable(childNames(exportTarget)));
    QCOMPARE(exportIndex->value, QStringLiteral("1"));

    StructureRow *codeSection = sections->children[6].get();
    QCOMPARE(codeSection->name, QStringLiteral("[6]WASM_SECTION_CODE"));
    StructureRow *codeId = findChildNamed(codeSection, QStringLiteral("byte id"));
    QVERIFY2(codeId, qPrintable(childNames(codeSection)));
    QCOMPARE(codeId->value, QStringLiteral("WASM_SECTION_CODE"));
    StructureRow *codePayload = findChildNamed(codeSection, QStringLiteral("WASM_CODE_SECTION code"));
    QVERIFY2(codePayload, qPrintable(childNames(codeSection)));
    StructureRow *functions = findChildNamed(codePayload, QStringLiteral("WASM_CODE_ENTRY functions[]"));
    QVERIFY2(functions, qPrintable(childNames(codePayload)));
    StructureRow *body = findChildNamed(functions->children[0].get(), QStringLiteral("WASM_CODE_BODY body"));
    QVERIFY2(body, qPrintable(childNames(functions->children[0].get())));
    StructureRow *localDeclCount = findChildNamed(body, QStringLiteral("uleb128 localDeclCount"));
    QVERIFY2(localDeclCount, qPrintable(childNames(body)));
    QCOMPARE(localDeclCount->value, QStringLiteral("0"));
    StructureRow *instructions = findChildNamed(body, QStringLiteral("byte instructions[]"));
    QVERIFY2(instructions, qPrintable(childNames(body)));
    QCOMPARE(instructions->value, QStringLiteral("{ 65, 42, 11 }"));
    QVERIFY(typeIndices->children[0]->hasCodeTarget);
    QCOMPARE(typeIndices->children[0]->codeArchitecture, QStringLiteral("wasm"));
    QCOMPARE(typeIndices->children[0]->codeTargetOffset, functions->children[0]->codeTargetOffset);

    StructureRow *dataSection = sections->children[7].get();
    QCOMPARE(dataSection->name, QStringLiteral("[7]WASM_SECTION_DATA"));
    StructureRow *dataId = findChildNamed(dataSection, QStringLiteral("byte id"));
    QVERIFY2(dataId, qPrintable(childNames(dataSection)));
    QCOMPARE(dataId->value, QStringLiteral("WASM_SECTION_DATA"));
    StructureRow *dataPayload = findChildNamed(dataSection, QStringLiteral("WASM_DATA_SECTION data"));
    QVERIFY2(dataPayload, qPrintable(childNames(dataSection)));
    StructureRow *dataSegments = findChildNamed(dataPayload, QStringLiteral("WASM_DATA_SEGMENT data[]"));
    QVERIFY2(dataSegments, qPrintable(childNames(dataPayload)));
    StructureRow *passiveData = findChildNamed(dataSegments->children[0].get(), QStringLiteral("struct passive"));
    QVERIFY2(passiveData, qPrintable(childNames(dataSegments->children[0].get())));
    QCOMPARE(findChildNamed(passiveData, QStringLiteral("byte bytes[]"))->value, QStringLiteral("{ 97, 98, 99 }"));

    StructureRow *summary = findSemanticRootChildNamed(rows, QStringLiteral("WASM Summary"));
    QVERIFY2(summary, "WASM Summary semantic child row not found");

    StructureRow *summarySections = findChildNamed(summary, QStringLiteral("Sections"));
    QVERIFY2(summarySections, qPrintable(childNames(summary)));
    StructureRow *summaryImportSection = findChildNamed(summarySections, QStringLiteral("WASM_SECTION_IMPORT"));
    QVERIFY2(summaryImportSection, qPrintable(childNames(summarySections)));
    QCOMPARE(summaryImportSection->offset, importSection->offset);
    QCOMPARE(findChildNamed(summaryImportSection, QStringLiteral("Size"))->value, QStringLiteral("13"));

    StructureRow *summaryTypes = findChildNamed(summary, QStringLiteral("Types"));
    QVERIFY2(summaryTypes, qPrintable(childNames(summary)));
    StructureRow *summaryType0 = findChildNamed(summaryTypes, QStringLiteral("sig0"));
    QVERIFY2(summaryType0, qPrintable(childNames(summaryTypes)));
    QCOMPARE(summaryType0->offset, functionTypes->children[0]->offset);
    QCOMPARE(findChildNamed(summaryType0, QStringLiteral("Form"))->value, QStringLiteral("WASM_TYPE_FORM_FUNC"));
    QCOMPARE(findChildNamed(summaryType0, QStringLiteral("Params"))->value, QStringLiteral("0"));
    QCOMPARE(findChildNamed(summaryType0, QStringLiteral("Results"))->value, QStringLiteral("1"));
    QCOMPARE(findChildNamed(summaryType0, QStringLiteral("Name"))->value, QStringLiteral("sig0"));

    StructureRow *summaryMemories = findChildNamed(summary, QStringLiteral("Memories"));
    QVERIFY2(summaryMemories, qPrintable(childNames(summary)));
    StructureRow *summaryMemory0 = findChildNamed(summaryMemories, QStringLiteral("memory 0"));
    QVERIFY2(summaryMemory0, qPrintable(childNames(summaryMemories)));
    QCOMPARE(summaryMemory0->offset, memories->children[0]->offset);
    QCOMPARE(findChildNamed(summaryMemory0, QStringLiteral("Min"))->value, QStringLiteral("1"));

    StructureRow *summaryImports = findChildNamed(summary, QStringLiteral("Imports"));
    QVERIFY2(summaryImports, qPrintable(childNames(summary)));
    StructureRow *summaryImport = findChildNamed(summaryImports, QStringLiteral("env.print"));
    QVERIFY2(summaryImport, qPrintable(childNames(summaryImports)));
    QCOMPARE(summaryImport->offset, imports->children[0]->offset);
    QCOMPARE(findChildNamed(summaryImport, QStringLiteral("Module"))->value, QStringLiteral("env"));
    QCOMPARE(findChildNamed(summaryImport, QStringLiteral("Name"))->value, QStringLiteral("print"));
    QCOMPARE(findChildNamed(summaryImport, QStringLiteral("Kind"))->value, QStringLiteral("WASM_EXTERN_FUNC"));
    QCOMPARE(findChildNamed(summaryImport, QStringLiteral("TypeIndex"))->value, QStringLiteral("0"));

    StructureRow *summaryExports = findChildNamed(summary, QStringLiteral("Exports"));
    QVERIFY2(summaryExports, qPrintable(childNames(summary)));
    StructureRow *summaryExport = findChildNamed(summaryExports, QStringLiteral("answer"));
    QVERIFY2(summaryExport, qPrintable(childNames(summaryExports)));
    QCOMPARE(summaryExport->offset, exports->children[0]->offset);
    QCOMPARE(findChildNamed(summaryExport, QStringLiteral("Kind"))->value, QStringLiteral("WASM_EXTERN_FUNC"));
    QCOMPARE(findChildNamed(summaryExport, QStringLiteral("Index"))->value, QStringLiteral("1"));

    StructureRow *summaryData = findChildNamed(summary, QStringLiteral("Data"));
    QVERIFY2(summaryData, qPrintable(childNames(summary)));
    StructureRow *summaryData0 = findChildNamed(summaryData, QStringLiteral("data 0"));
    QVERIFY2(summaryData0, qPrintable(childNames(summaryData)));
    QCOMPARE(summaryData0->offset, dataSegments->children[0]->offset);
    QCOMPARE(findChildNamed(summaryData0, QStringLiteral("Mode"))->value, QStringLiteral("1"));

    StructureRow *summaryFunctions = findChildNamed(summary, QStringLiteral("Functions"));
    QVERIFY2(summaryFunctions, qPrintable(childNames(summary)));
    QCOMPARE(summaryFunctions->children.size(), size_t(2));

    StructureRow *summaryImportedFunction = summaryFunctions->children[0].get();
    QCOMPARE(summaryImportedFunction->name, QStringLiteral("print"));
    QCOMPARE(summaryImportedFunction->offset, imports->children[0]->offset);
    QCOMPARE(findChildNamed(summaryImportedFunction, QStringLiteral("Module"))->value, QStringLiteral("env"));
    QCOMPARE(findChildNamed(summaryImportedFunction, QStringLiteral("Name"))->value, QStringLiteral("print"));
    QCOMPARE(findChildNamed(summaryImportedFunction, QStringLiteral("TypeIndex"))->value, QStringLiteral("0"));
    QVERIFY(!findChildNamed(summaryImportedFunction, QStringLiteral("Export")));
    QVERIFY(!findChildNamed(summaryImportedFunction, QStringLiteral("CodeSize")));

    StructureRow *summaryDefinedFunction = summaryFunctions->children[1].get();
    QCOMPARE(summaryDefinedFunction->name, QStringLiteral("answer"));
    QCOMPARE(summaryDefinedFunction->offset, typeIndices->children[0]->offset);
    QCOMPARE(findChildNamed(summaryDefinedFunction, QStringLiteral("Name"))->value, QStringLiteral("answer"));
    QCOMPARE(findChildNamed(summaryDefinedFunction, QStringLiteral("TypeIndex"))->value, QStringLiteral("0"));
    QCOMPARE(findChildNamed(summaryDefinedFunction, QStringLiteral("Export"))->value, QStringLiteral("answer"));
    QCOMPARE(findChildNamed(summaryDefinedFunction, QStringLiteral("CodeSize"))->value, QStringLiteral("4"));
    QVERIFY(summaryDefinedFunction->hasCodeTarget);
    QCOMPARE(summaryDefinedFunction->codeArchitecture, QStringLiteral("wasm"));
    QCOMPARE(summaryDefinedFunction->codeByteLength, uint64_t(3));
    QVERIFY(functions->children[0]->hasCodeTarget);
    QCOMPARE(functions->children[0]->codeTargetOffset, summaryDefinedFunction->codeTargetOffset);
}

void StructViewDexWasmTests::builderKeepsWasmCodeTargetsPastRawArrayCap()
{
    StrataLibrary library;
    QVERIFY2(parseStandardDefinition(&library, QStringLiteral("wasm.strata")), "wasm.strata failed to parse");
    TypeDecl *wasmRoot = exportedNamed(&library, QStringLiteral("WASM"));
    QVERIFY(wasmRoot);

    constexpr int kFunctionCount = 117;
    QByteArray wasm = QByteArray::fromHex("0061736d01000000");

    // One () -> () type.
    wasm.append(QByteArray::fromHex("010401600000"));
    // Every defined function has type index zero.
    wasm.append(char(0x03));
    wasm.append(char(0x76)); // 118-byte payload: count + 117 type indices.
    wasm.append(char(kFunctionCount));
    wasm.append(QByteArray(kFunctionCount, '\0'));
    // Every body is: local-decl-count 0, nop, end.
    wasm.append(char(0x0a));
    wasm.append(QByteArray::fromHex("d503")); // 469-byte payload.
    wasm.append(char(kFunctionCount));
    for (int i = 0; i < kFunctionCount; ++i)
        wasm.append(QByteArray::fromHex("0300010b"));

    auto rows = buildRows(&library, wasmRoot, wasm);
    StructureRow *summary = findSemanticRootChildNamed(rows, QStringLiteral("WASM Summary"));
    QVERIFY(summary);
    StructureRow *functions = findChildNamed(summary, QStringLiteral("Functions"));
    QVERIFY(functions);
    QCOMPARE(functions->children.size(), size_t(kFunctionCount));

    StructureRow *lastFunction = functions->children.back().get();
    QCOMPARE(lastFunction->name, QStringLiteral("defined function 116"));
    QVERIFY(lastFunction->hasCodeTarget);
    QCOMPARE(lastFunction->codeArchitecture, QStringLiteral("wasm"));
    QCOMPARE(lastFunction->codeByteLength, uint64_t(2));
}

void StructViewDexWasmTests::builderRendersWasmStartTagsAndElements()
{
    StrataLibrary library;
    QVERIFY2(parseStandardDefinition(&library, QStringLiteral("wasm.strata")), "wasm.strata failed to parse");
    TypeDecl *wasmRoot = exportedNamed(&library, QStringLiteral("WASM"));
    QVERIFY(wasmRoot);

    QByteArray wasm = QByteArray::fromHex("0061736d01000000");
    wasm.append(QByteArray::fromHex("080100"));       // start function index 0
    wasm.append(QByteArray::fromHex("09050101000100")); // one passive element, function 0
    wasm.append(QByteArray::fromHex("0d03010000"));    // one tag, attribute/type index 0

    auto rows = buildRows(&library, wasmRoot, wasm);
    StructureRow *wasmRow = findTopLevelNamed(rows, QStringLiteral("WASM"));
    QVERIFY(wasmRow);
    StructureRow *sections = findChildNamed(wasmRow, QStringLiteral("WASM_SECTION sections[]"));
    QVERIFY(sections);
    QCOMPARE(sections->children.size(), size_t(3));
    QVERIFY(findChildNamed(sections->children[1].get(), QStringLiteral("WASM_ELEMENT_SECTION element")));
    QVERIFY(findChildNamed(sections->children[2].get(), QStringLiteral("WASM_TAG_SECTION tag")));

    StructureRow *summary = findSemanticRootChildNamed(rows, QStringLiteral("WASM Summary"));
    QVERIFY(summary);
    StructureRow *start = findChildNamed(summary, QStringLiteral("Start"));
    QVERIFY(start);
    QCOMPARE(findChildNamed(start, QStringLiteral("start"))->children[0]->value, QStringLiteral("0"));
    StructureRow *elements = findChildNamed(summary, QStringLiteral("Elements"));
    QVERIFY(elements);
    StructureRow *rawElements = findChildNamed(sections->children[1].get(), QStringLiteral("WASM_ELEMENT_SECTION element"));
    QVERIFY(rawElements);
    StructureRow *rawElement = findChildNamed(rawElements, QStringLiteral("WASM_ELEMENT_SEGMENT elements[]"));
    QVERIFY(rawElement);
    QCOMPARE(findChildNamed(elements, QStringLiteral("element 0"))->offset, rawElement->children[0]->offset);
    StructureRow *tags = findChildNamed(summary, QStringLiteral("Tags"));
    QVERIFY(tags);
    StructureRow *rawTags = findChildNamed(sections->children[2].get(), QStringLiteral("WASM_TAG_SECTION tag"));
    QVERIFY(rawTags);
    StructureRow *rawTag = findChildNamed(rawTags, QStringLiteral("WASM_TAG tags[]"));
    QVERIFY(rawTag);
    QCOMPARE(findChildNamed(tags, QStringLiteral("tag 0"))->offset, rawTag->children[0]->offset);
}

REGISTER_STRUCTVIEW_TEST(StructViewDexWasmTests)
#include "dex_wasm_tests.moc"
