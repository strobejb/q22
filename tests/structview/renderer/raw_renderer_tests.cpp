#include "../structview_testsupport.h"

class StructViewRawRendererTests : public QObject
{
    Q_OBJECT

private slots:
    void builderFormatsScalarsAndEndian();
    void builderRejectsTruncatedScalars();
    void builderFormatsLeb128ScalarsAndAdvancesByEncodedLength();
    void builderUsesLeb128ValuesInExpressions();
    void builderRendersBitflagsAsExpandableRows();
    void builderFormatsCharacterArraysAsStrings();
    void builderFormatsTaggedByteArraysAsStrings();
    void builderFormatsGenericDisplayTags();
    void builderAppliesTreePresentationTags();
    void builderRendersRaggedStringTables();
    void builderSupportsExtentBoundedRecursiveArrays();
    void builderFormatsScalarArraysAsPreviewLists();
    void builderAdvancesExactCountScalarArraysPastPreviewCap();
    void builderPopulatesCommentsFromTypeDeclarations();
    void builderUsesPackedLayoutByDefault();
    void builderAppliesStructAndFieldAlignment();
    void builderLetsOffsetOverrideAlignment();
    void builderKeepsUnionMembersAtAlignedBase();
    void builderUsesExtentToAdvancePastRenderedUnionSize();
    void builderPadsDeclarationsToAlignmentBoundaries();
    void builderSkipsAbsentOptionalDeclarations();
    void builderUsesSizeIsForUnsizedArrays();
    void builderUsesMaxCountAndTerminatorExpressions();
    void builderUsesNamedOffsetMapsAndValueAt();
    void builderUsesScopePrefixesForRootAndParent();
    void builderUsesCountAsForLogicalArraySlots();
    void builderEvaluatesTernaryExpressions();
    void builderUsesCommonUnionPrefixForSizeIs();
    void builderEvaluatesTernaryUnionMemberSizeAndOffset();
    void builderEvaluatesEndianAwareUnionMembers();
    void builderEvaluatesArrayIndexedUnionMembers();
    void builderUsesNameFieldForStructArrayElements();
    void builderAlignsFieldNamesWithinCompoundTypes();
    void builderKeepsSignedPrimitiveTypedefNamesInStorageMode();
    void builderBuildsNestedStructRowsAndOffsets();
    void builderSupportsArraysOffsetsEnumsAndSwitchCases();
    void builderExposesEnumChoicesAndEntrypoints();
    void builderRejectsOverflowingCodeAndOpenAsTargets();
    void builderExposesOpenAsTargets();
    void builderEvaluatesUnionSwitchSelectorsFromTypedLayout();
    void builderEvaluatesFieldsAndCorrectedExpressions();
    void builderEvaluatesFindSearchExpressions();
    void builderEvaluatesOctalStringExpressions();
    void builderSelectsUnionMembersFromStringExpressions();
    void builderSelectsUnionMembersFromFourCcExpressions();
    void builderUsesDynamicEndianExpressions();
    void builderEvaluatesEnumIndexedArraysInExpressions();
    void builderEvaluatesEnumIndexedUnionMembersInExpressions();
    void builderOptionallySortsTopLevelRowsByOffset();
    void builderResolvesUnionDiscriminatorFromCandidateOnlyField();
    void definitionManagerFlagsNonStaticFieldReferences();
    void definitionManagerFlagsRuntimeExpressionsInRootOffsets();
};

void StructViewRawRendererTests::builderFormatsScalarsAndEndian()
{
    // Scenario: a selected exported root contains ordinary scalar fields and a
    // declaration-level endian tag.
    // Expected: values are read from the supplied byte reader, little endian is
    // the default, and [endian("big")] only changes the tagged declaration.
    // Regression guard: the Structure View grid must show file data, not just
    // the parsed type outline.
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "[export]\n"
                        "struct Root { byte a; word b; [endian(\"big\")] word c; } root;\n"));

    const QByteArray bytes = QByteArray::fromHex("1201000102");
    auto rows = buildRows(&library, firstExported(&library), bytes);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->children.size(), size_t(3));
    QCOMPARE(rows[0]->name, QStringLiteral("struct Root root"));
    QCOMPARE(rows[0]->children[0]->name, QStringLiteral("byte a"));
    QCOMPARE(rows[0]->children[1]->name, QStringLiteral("word b"));
    QCOMPARE(rows[0]->children[0]->value, QStringLiteral("18"));
    QCOMPARE(rows[0]->children[1]->value, QStringLiteral("1"));
    QCOMPARE(rows[0]->children[2]->value, QStringLiteral("258"));
}

void StructViewRawRendererTests::builderRejectsTruncatedScalars()
{
    // Scenario: a file has the correct root selected but ends in the middle of
    // a scalar field. Expected: the renderer advances over the declared field
    // width without fabricating a partial value row. Regression guard:
    // malformed/truncated files must fail closed instead of showing bogus
    // values or feeding partial bytes into later expressions.
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "[export]\n"
                        "struct Root { word value; byte after; } root;\n"));

    QByteArray bytes;
    bytes.append(char(0x34));

    auto rows = buildRows(&library, firstExported(&library), bytes);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->children.size(), size_t(0));
    QCOMPARE(rows[0]->byteLength, uint64_t(3));
}

void StructViewRawRendererTests::builderFormatsLeb128ScalarsAndAdvancesByEncodedLength()
{
    // Scenario: LEB128 scalars have no fixed width; each field's encoded bytes
    // determine both its value and the next field offset.
    // Expected: uleb128/sleb128 values are decoded and following fields start
    // after the actual encoded byte count.
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "[export]\n"
                        "struct Root {\n"
                        "  uleb128 small;\n"
                        "  uleb128 medium;\n"
                        "  uleb128 large;\n"
                        "  sleb128 negOne;\n"
                        "  sleb128 negLarge;\n"
                        "  byte tail;\n"
                        "} root;\n"));

    auto rows = buildRows(&library,
                          firstExported(&library),
                          QByteArray::fromHex("7F8001E58E267F9BF15942"));
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->children.size(), size_t(6));
    QCOMPARE(rows[0]->children[0]->name, QStringLiteral("uleb128 small"));
    QCOMPARE(rows[0]->children[0]->value, QStringLiteral("127"));
    QCOMPARE(rows[0]->children[0]->byteLength, uint64_t(1));
    QCOMPARE(rows[0]->children[1]->value, QStringLiteral("128"));
    QCOMPARE(rows[0]->children[1]->absoluteOffset, uint64_t(1));
    QCOMPARE(rows[0]->children[1]->byteLength, uint64_t(2));
    QCOMPARE(rows[0]->children[2]->value, QStringLiteral("624485"));
    QCOMPARE(rows[0]->children[2]->absoluteOffset, uint64_t(3));
    QCOMPARE(rows[0]->children[2]->byteLength, uint64_t(3));
    QCOMPARE(rows[0]->children[3]->value, QStringLiteral("-1"));
    QCOMPARE(rows[0]->children[3]->absoluteOffset, uint64_t(6));
    QCOMPARE(rows[0]->children[4]->value, QStringLiteral("-624485"));
    QCOMPARE(rows[0]->children[4]->byteLength, uint64_t(3));
    QCOMPARE(rows[0]->children[5]->name, QStringLiteral("byte tail"));
    QCOMPARE(rows[0]->children[5]->absoluteOffset, uint64_t(10));
    QCOMPARE(rows[0]->children[5]->value, QStringLiteral("66"));
}

void StructViewRawRendererTests::builderUsesLeb128ValuesInExpressions()
{
    // Scenario: DEX/WASM-style structures commonly use a uleb128 field as the
    // count for the variable data that immediately follows.
    // Expected: expression evaluation sees the decoded integer, not the raw
    // encoded bytes.
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "[export]\n"
                        "struct Root {\n"
                        "  uleb128 count;\n"
                        "  [count(count)] byte values[];\n"
                        "  byte tail;\n"
                        "} root;\n"));

    auto rows = buildRows(&library, firstExported(&library), QByteArray::fromHex("030A0B0C2A"));
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->children.size(), size_t(3));
    QCOMPARE(rows[0]->children[0]->value, QStringLiteral("3"));
    QCOMPARE(rows[0]->children[1]->name, QStringLiteral("byte values[]"));
    QCOMPARE(rows[0]->children[1]->children.size(), size_t(3));
    QCOMPARE(rows[0]->children[1]->children[2]->value, QStringLiteral("12"));
    QCOMPARE(rows[0]->children[2]->absoluteOffset, uint64_t(4));
    QCOMPARE(rows[0]->children[2]->value, QStringLiteral("42"));
}

void StructViewRawRendererTests::builderRendersBitflagsAsExpandableRows()
{
    // Scenario: bitflag(EnumName) annotates an integer field whose bits map to
    // named masks.
    // Expected: the parent row shows the active flag names and expands to one
    // child row per set flag, preserving unknown bits instead of hiding them.
    // Regression guard: bitflag(...) should be more than a parsed no-op.
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "enum Flags { None = 0, Read = 1, Write = 2, Execute = 4 };\n"
                        "[export]\n"
                        "struct Root {\n"
                        "  [bitflag(Flags)] byte flags;\n"
                        "  [bitflag(Flags)] byte none;\n"
                        "  [bitflag(Flags)] byte unknown;\n"
                        "} root;\n"));

    StructureDisplayOptions options;
    options.hexadecimalValues = true;
    auto rows = buildRows(&library, firstExported(&library), QByteArray::fromHex("030008"), 0, options);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->children.size(), size_t(3));

    StructureRow *flags = rows[0]->children[0].get();
    QCOMPARE(flags->name, QStringLiteral("byte flags"));
    QCOMPARE(flags->value, QStringLiteral("Read | Write"));
    QCOMPARE(flags->children.size(), size_t(2));
    QCOMPARE(flags->children[0]->name, QStringLiteral("Read"));
    QCOMPARE(flags->children[0]->value, QStringLiteral("01"));
    QCOMPARE(flags->children[1]->name, QStringLiteral("Write"));
    QCOMPARE(flags->children[1]->value, QStringLiteral("02"));

    StructureRow *none = rows[0]->children[1].get();
    QCOMPARE(none->value, QStringLiteral("None"));
    QCOMPARE(none->children.size(), size_t(1));
    QCOMPARE(none->children[0]->name, QStringLiteral("None"));
    QCOMPARE(none->children[0]->value, QStringLiteral("00"));

    StructureRow *unknown = rows[0]->children[2].get();
    QCOMPARE(unknown->value, QStringLiteral("Unknown bits"));
    QCOMPARE(unknown->children.size(), size_t(1));
    QCOMPARE(unknown->children[0]->name, QStringLiteral("Unknown bits"));
    QCOMPARE(unknown->children[0]->value, QStringLiteral("08"));
}

void StructViewRawRendererTests::builderFormatsCharacterArraysAsStrings()
{
    // Scenario: a structure contains fixed-size char and wchar_t buffers.
    // Expected: the array rows still expand into elements, but the parent value
    // gives the useful quoted string preview instead of a generic {...}.
    // Regression guard: strings are a common binary-structure case and should be
    // readable without expanding every character cell.
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "[export]\n"
                        "struct Root { char label[5]; wchar_t wide[3]; } root;\n"));

    const QByteArray bytes = QByteArray("Hi\0X!", 5)
                             + QByteArray::fromHex("410042000000");
    auto rows = buildRows(&library, firstExported(&library), bytes);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->children.size(), size_t(2));
    QCOMPARE(rows[0]->children[0]->name, QStringLiteral("char label[]"));
    QCOMPARE(rows[0]->children[0]->value, QStringLiteral("\"Hi\""));
    QCOMPARE(rows[0]->children[0]->children.size(), size_t(5));
    QCOMPARE(rows[0]->children[1]->name, QStringLiteral("wchar_t wide[]"));
    QCOMPARE(rows[0]->children[1]->value, QStringLiteral("\"AB\""));
    QCOMPARE(rows[0]->children[1]->children.size(), size_t(3));
}

void StructViewRawRendererTests::builderFormatsTaggedByteArraysAsStrings()
{
    // Scenario: some formats store UTF-8 or ASCII bytes as byte arrays rather
    // than char arrays.
    // Expected: [string] opts byte[] into the quoted text display path, while
    // untagged byte arrays keep their scalar preview.
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "[export]\n"
                        "struct Root {\n"
                        "  [string, count(4)] byte text[];\n"
                        "  byte raw[4];\n"
                        "} root;\n"));

    auto rows = buildRows(&library, firstExported(&library), QByteArray::fromHex("4869C3A94869002A"));
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->children.size(), size_t(2));
    QCOMPARE(rows[0]->children[0]->name, QStringLiteral("byte text[]"));
    QCOMPARE(rows[0]->children[0]->value, QStringLiteral("\"Hié\""));
    QCOMPARE(rows[0]->children[1]->name, QStringLiteral("byte raw[]"));
    QCOMPARE(rows[0]->children[1]->value, QStringLiteral("{ 72, 105, 0, 42 }"));
}

void StructViewRawRendererTests::builderFormatsGenericDisplayTags()
{
    // Scenario: format("...") controls only the tagged row's value decoding.
    // Expected: FourCC/string legacy behavior is available through format(),
    // UTF-16 byte arrays and GUID/UUID values decode as text, and integer base
    // overrides do not leak to neighboring rows.
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "[export]\n"
                        "struct Root {\n"
                        "  [format(\"fourcc\")] dword tag;\n"
                        "  [format(\"string\"), count(4)] byte text[];\n"
                        "  [format(\"utf16le\"), count(6)] byte le[];\n"
                        "  [format(\"utf16be\"), count(6)] byte be[];\n"
                        "  [format(\"guid\")] byte guid[16];\n"
                        "  [format(\"uuid\")] byte uuid[16];\n"
                        "  [format(\"dec\")] byte decValue;\n"
                        "  byte normalValue;\n"
                        "} root;\n"));

    const QByteArray bytes = QByteArray("RIFF", 4)
                             + QByteArray::fromHex("4869002A")
                             + QByteArray::fromHex("410042000000")
                             + QByteArray::fromHex("005800590000")
                             + QByteArray::fromHex("33221100554477668899AABBCCDDEEFF")
                             + QByteArray::fromHex("00112233445566778899AABBCCDDEEFF")
                             + QByteArray::fromHex("1010");
    StructureDisplayOptions options;
    options.hexadecimalValues = true;
    auto rows = buildRows(&library, firstExported(&library), bytes, 0, options);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->children.size(), size_t(8));
    QCOMPARE(rows[0]->children[0]->value, QStringLiteral("\"RIFF\""));
    QCOMPARE(rows[0]->children[1]->value, QStringLiteral("\"Hi\""));
    QCOMPARE(rows[0]->children[2]->value, QStringLiteral("\"AB\""));
    QCOMPARE(rows[0]->children[2]->children.size(), size_t(6));
    QCOMPARE(rows[0]->children[3]->value, QStringLiteral("\"XY\""));
    QCOMPARE(rows[0]->children[4]->value, QStringLiteral("\"00112233-4455-6677-8899-aabbccddeeff\""));
    QCOMPARE(rows[0]->children[5]->value, QStringLiteral("\"00112233-4455-6677-8899-aabbccddeeff\""));
    QCOMPARE(rows[0]->children[6]->value, QStringLiteral("16"));
    QCOMPARE(rows[0]->children[7]->value, QStringLiteral("10"));
}

void StructViewRawRendererTests::builderAppliesTreePresentationTags()
{
    // Scenario: tree("...") controls only Structure View presentation.
    // Expected: hidden rows still advance layout, flatten promotes children, and
    // collapsed/expanded metadata is carried on the row for the panel.
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "typedef struct _Inner { byte a; byte b; } Inner;\n"
                        "[export]\n"
                        "struct Root {\n"
                        "  byte before;\n"
                        "  [tree(\"hidden\")] byte hidden;\n"
                        "  [tree(\"flatten\")] Inner flat;\n"
                        "  [tree(\"collapsed\")] Inner collapsed;\n"
                        "  [tree(\"expanded\")] Inner expanded;\n"
                        "  byte after;\n"
                        "} root;\n"));

    auto rows = buildRows(&library, firstExported(&library), QByteArray::fromHex("000102030405060708"));
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->children.size(), size_t(6));
    QCOMPARE(rows[0]->children[0]->name, QStringLiteral("byte before"));
    QCOMPARE(rows[0]->children[1]->name, QStringLiteral("byte a"));
    QCOMPARE(rows[0]->children[1]->absoluteOffset, uint64_t(2));
    QCOMPARE(rows[0]->children[2]->name, QStringLiteral("byte b"));
    QCOMPARE(rows[0]->children[2]->absoluteOffset, uint64_t(3));
    QVERIFY(!findChildNamed(rows[0].get(), QStringLiteral("byte hidden")));

    StructureRow *collapsed = findChildNamed(rows[0].get(), QStringLiteral("Inner collapsed"));
    QVERIFY(collapsed);
    QCOMPARE(collapsed->absoluteOffset, uint64_t(4));
    QCOMPARE(collapsed->treeMode, StructureRowTreeMode::Collapsed);

    StructureRow *expanded = findChildNamed(rows[0].get(), QStringLiteral("Inner expanded"));
    QVERIFY(expanded);
    QCOMPARE(expanded->absoluteOffset, uint64_t(6));
    QCOMPARE(expanded->treeMode, StructureRowTreeMode::Expanded);

    StructureRow *after = findChildNamed(rows[0].get(), QStringLiteral("byte after"));
    QVERIFY(after);
    QCOMPARE(after->absoluteOffset, uint64_t(8));
}

void StructViewRawRendererTests::builderRendersRaggedStringTables()
{
    // Scenario: a binary format stores a table as a flat byte extent containing
    // consecutive NUL-terminated strings.
    // Expected: nested flexible arrays render one expandable quoted string row
    // per entry, and the outer array stops at extent(...) rather than continuing
    // into padding or missing bytes.
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "[export]\n"
                        "struct Root {\n"
                        "  [count(16, 16), terminated_by(_, 0), extent(8)] char strings[][];\n"
                        "} root;\n"));

    auto rows = buildRows(&library, firstExported(&library), QByteArray("foo\0bar\0xxxx", 12));
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->children.size(), size_t(1));

    StructureRow *strings = rows[0]->children[0].get();
    QCOMPARE(strings->name, QStringLiteral("char strings[][]"));
    QCOMPARE(strings->value, QStringLiteral("{...}"));
    QCOMPARE(strings->children.size(), size_t(2));
    QCOMPARE(strings->children[0]->name, QStringLiteral("[0]"));
    QCOMPARE(strings->children[0]->value, QStringLiteral("\"foo\""));
    QCOMPARE(strings->children[0]->byteLength, uint64_t(4));
    QCOMPARE(strings->children[1]->name, QStringLiteral("[1]"));
    QCOMPARE(strings->children[1]->value, QStringLiteral("\"bar\""));
    QCOMPARE(strings->children[1]->byteLength, uint64_t(4));
}

void StructViewRawRendererTests::builderSupportsExtentBoundedRecursiveArrays()
{
    // Scenario: a container contains more instances of its own type inside a
    // byte-counted payload, as in MP4 boxes and RIFF LIST chunks.
    // Expected: only an array with both a count cap and an extent may introduce
    // the cycle; rendering advances by each variable-sized child and preserves
    // the enclosing extent for the following field.
    StrataLibrary rejectedLibrary;
    Parser rejectedParser(&rejectedLibrary);
    QVERIFY(!parseBuffer(rejectedParser,
                         "typedef struct _BAD { struct _BAD child; } BAD;\n"));

    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "typedef struct _NODE {\n"
                        "  byte payloadSize;\n"
                        "  [max_count(payloadSize), extent(payloadSize)] struct _NODE children[];\n"
                        "} NODE;\n"
                        "[export] typedef struct _ROOT { NODE node; byte tail; } ROOT;\n"));

    TypeDecl *rootType = exportedNamed(&library, QStringLiteral("ROOT"));
    QVERIFY(rootType);
    auto rows = buildRows(&library, rootType, QByteArray::fromHex("03010000AA"));
    QCOMPARE(rows.size(), size_t(1));

    StructureRow *node = findChildNamed(rows[0].get(), QStringLiteral("NODE node"));
    QVERIFY2(node, qPrintable(childNames(rows[0].get())));
    StructureRow *children = findChildNamed(node, QStringLiteral("struct _NODE children[]"));
    QVERIFY2(children, qPrintable(childNames(node)));
    QCOMPARE(children->byteLength, uint64_t(3));
    QCOMPARE(children->children.size(), size_t(2));
    QCOMPARE(children->children[0]->absoluteOffset, uint64_t(1));
    QCOMPARE(children->children[0]->byteLength, uint64_t(2));
    QCOMPARE(children->children[1]->absoluteOffset, uint64_t(3));
    QCOMPARE(children->children[1]->byteLength, uint64_t(1));

    StructureRow *grandchildren = findChildNamed(children->children[0].get(),
                                                  QStringLiteral("struct _NODE children[]"));
    QVERIFY2(grandchildren, qPrintable(childNames(children->children[0].get())));
    QCOMPARE(grandchildren->children.size(), size_t(1));
    QCOMPARE(grandchildren->children[0]->absoluteOffset, uint64_t(2));

    StructureRow *tail = findChildNamed(rows[0].get(), QStringLiteral("byte tail"));
    QVERIFY(tail);
    QCOMPARE(tail->absoluteOffset, uint64_t(4));
    QCOMPARE(tail->value, QStringLiteral("170"));

    // A malicious chain deeper than the renderer's recursion ceiling must
    // still consume its declared extents and leave the trailing field aligned.
    QByteArray deep;
    for (int remaining = 69; remaining >= 0; --remaining)
        deep.append(char(remaining));
    deep.append(char(0x55));
    auto deepRows = buildRows(&library, rootType, deep);
    QCOMPARE(deepRows.size(), size_t(1));
    StructureRow *deepTail = findChildNamed(deepRows[0].get(), QStringLiteral("byte tail"));
    QVERIFY(deepTail);
    QCOMPARE(deepTail->absoluteOffset, uint64_t(70));
    QCOMPARE(deepTail->value, QStringLiteral("85"));
}

void StructViewRawRendererTests::builderFormatsScalarArraysAsPreviewLists()
{
    // Scenario: a structure contains ordinary scalar arrays rather than strings.
    // Expected: the parent array row remains expandable, but its value previews
    // the first scalar elements and adds an ellipsis when the array is longer.
    // Regression guard: scalar arrays should be quickly readable without opening
    // every child row, while char/wchar arrays keep their string-specific path.
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "[export]\n"
                        "struct Root { byte small[4]; word large[10]; } root;\n"));

    const QByteArray bytes = QByteArray::fromHex(
        "00010203"
        "0000010002000300040005000600070008000900");
    auto rows = buildRows(&library, firstExported(&library), bytes);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->children.size(), size_t(2));
    QCOMPARE(rows[0]->children[0]->value, QStringLiteral("{ 0, 1, 2, 3 }"));
    QCOMPARE(rows[0]->children[0]->children.size(), size_t(4));
    QVERIFY(!rows[0]->children[0]->emphasizeName);
    QCOMPARE(rows[0]->children[1]->value, QStringLiteral("{ 0, 1, 2, 3, 4, 5, 6, 7, ... }"));
    QCOMPARE(rows[0]->children[1]->children.size(), size_t(10));
    QVERIFY(!rows[0]->children[1]->emphasizeName);
}

void StructViewRawRendererTests::builderAdvancesExactCountScalarArraysPastPreviewCap()
{
    // Scenario: a large exact-count scalar array renders as a capped preview.
    // Expected: the display cap does not become the consumed layout length.
    // Regression guard: archive payload arrays such as TAR data[] must not
    // desynchronize following fields just because only the first 100 bytes are
    // shown in the tree.
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "[export]\n"
                        "struct Root {\n"
                        "  word size;\n"
                        "  [count(size)] byte payload[];\n"
                        "  byte tail;\n"
                        "} root;\n"));

    QByteArray bytes(130, '\0');
    writeLe16(&bytes, 0, 127);
    bytes[129] = char(0x42);

    auto rows = buildRows(&library, firstExported(&library), bytes);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->children.size(), size_t(3));
    StructureRow *payload = rows[0]->children[1].get();
    QCOMPARE(payload->name, QStringLiteral("byte payload[]"));
    QCOMPARE(payload->children.size(), size_t(100));
    QCOMPARE(payload->byteLength, uint64_t(127));
    StructureRow *tail = rows[0]->children[2].get();
    QCOMPARE(tail->absoluteOffset, uint64_t(129));
    QCOMPARE(tail->value, QStringLiteral("66"));
}

void StructViewRawRendererTests::builderPopulatesCommentsFromTypeDeclarations()
{
    // Scenario: Strata definitions use ordinary C/C++ trailing comments to
    // document fields and structures.
    // Expected: Structure View displays a trimmed copy of those comments in the
    // Comment column, while the parser keeps the original whitespace refs for
    // round-tripping.
    // Regression guard: comments used to be captured in source whitespace only,
    // leaving rendered structure rows with an empty Comment column.
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "[export]\n"
                        "struct Root {\n"
                        "  dword signature; // file signature  \n"
                        "  word flags;      /*  flag bits  */\n"
                        "} root; //  root structure  \n"));

    auto rows = buildRows(&library, firstExported(&library), QByteArray::fromHex("000000000000"));
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->comment, QStringLiteral("root structure"));
    QCOMPARE(rows[0]->children.size(), size_t(2));
    QCOMPARE(rows[0]->children[0]->comment, QStringLiteral("file signature"));
    QCOMPARE(rows[0]->children[1]->comment, QStringLiteral("flag bits"));
}

void StructViewRawRendererTests::builderUsesPackedLayoutByDefault()
{
    // Scenario: a definition does not request any alignment.
    // Expected: Structure View renders fields back-to-back, matching the packed
    // default for this IDL dialect.
    // Regression guard: adding align support must not silently switch existing
    // definitions to compiler-like natural alignment.
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "[export]\n"
                        "struct Root { byte a; dword b; } root;\n"));
    TypeDecl *root = firstExported(&library);
    QVERIFY(root);

    QByteArray bytes(8, '\0');
    auto rows = buildRows(&library, root, bytes);

    QCOMPARE(rows[0]->children[0]->absoluteOffset, uint64_t(0));
    QCOMPARE(rows[0]->children[1]->absoluteOffset, uint64_t(1));
}

void StructViewRawRendererTests::builderAppliesStructAndFieldAlignment()
{
    // Scenario: a struct declares a default field alignment, and one member asks
    // for a stronger alignment.
    // Expected: the struct-level align applies to ordinary members, while the
    // field-level align overrides it for that member only.
    // Regression guard: align tags should work both as compound layout policy
    // and as a local field placement override.
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "[export, align(4)]\n"
                        "struct Root {\n"
                        "  byte a;\n"
                        "  dword b;\n"
                        "  [align(8)] dword c;\n"
                        "  byte d;\n"
                        "} root;\n"));
    TypeDecl *root = firstExported(&library);
    QVERIFY(root);

    QByteArray bytes(32, '\0');
    auto rows = buildRows(&library, root, bytes);

    QCOMPARE(rows[0]->children[0]->absoluteOffset, uint64_t(0));
    QCOMPARE(rows[0]->children[1]->absoluteOffset, uint64_t(4));
    QCOMPARE(rows[0]->children[2]->absoluteOffset, uint64_t(8));
    QCOMPARE(rows[0]->children[3]->absoluteOffset, uint64_t(12));
}

void StructViewRawRendererTests::builderLetsOffsetOverrideAlignment()
{
    // Scenario: a field has both an explicit offset and an alignment tag.
    // Expected: offset is treated as authoritative file placement and is not
    // rounded by align.
    // Regression guard: PE/ELF definitions use offset for exact locations; align
    // must not move those fields.
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "[export, align(8)]\n"
                        "struct Root { byte a; [offset(2), align(8)] dword b; } root;\n"));
    TypeDecl *root = firstExported(&library);
    QVERIFY(root);

    QByteArray bytes(16, '\0');
    auto rows = buildRows(&library, root, bytes);

    QCOMPARE(rows[0]->children[1]->absoluteOffset, uint64_t(2));
}

void StructViewRawRendererTests::builderKeepsUnionMembersAtAlignedBase()
{
    // Scenario: a union field is placed in an aligned struct.
    // Expected: the union itself is aligned as a field, but each union member
    // starts at the same base offset.
    // Regression guard: alignment must not accidentally serialize union members
    // as if they were struct fields.
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "[export, align(4)]\n"
                        "struct Root {\n"
                        "  byte a;\n"
                        "  union U { byte x; dword y; } u;\n"
                        "  byte z;\n"
                        "} root;\n"));
    TypeDecl *root = firstExported(&library);
    QVERIFY(root);

    QByteArray bytes(16, '\0');
    auto rows = buildRows(&library, root, bytes);
    StructureRow *u = rows[0]->children[1].get();

    QCOMPARE(u->absoluteOffset, uint64_t(4));
    QCOMPARE(u->children[0]->absoluteOffset, uint64_t(4));
    QCOMPARE(u->children[1]->absoluteOffset, uint64_t(4));
    QCOMPARE(rows[0]->children[2]->absoluteOffset, uint64_t(8));
}

void StructViewRawRendererTests::builderUsesExtentToAdvancePastRenderedUnionSize()
{
    // Scenario: a PE-style optional-header union renders one compact branch, but
    // the file header says the optional-header area consumes a larger byte span.
    // Expected: the union's visible child still renders at the union offset, and
    // the following field naturally starts after extent(expr), including scalar
    // sizeof(...) terms used inside that expression.
    // Regression guard: section-header style arrays should not need explicit
    // offset arithmetic just because the selected union member is shorter than
    // the on-disk reserved area.
    Parser parser;
    QVERIFY(parseBuffer(parser,
                        "typedef dword DWORD;\n"
                        "[export]\n"
                        "struct Root {\n"
                        "  byte span;\n"
                        "  [extent(span + sizeof(byte) + sizeof(DWORD))]\n"
                        "  union { byte tiny; };\n"
                        "  byte after;\n"
                        "} root;\n"));

    auto rows = buildRows(parser.GetStrataLibrary(),
                          firstExported(parser.GetStrataLibrary()),
                          QByteArray::fromHex("03AA000000000000000B"));

    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->children.size(), size_t(3));
    QCOMPARE(rows[0]->children[1]->absoluteOffset, uint64_t(1));
    QCOMPARE(rows[0]->children[2]->name, QStringLiteral("byte after"));
    QCOMPARE(rows[0]->children[2]->absoluteOffset, uint64_t(9));
    QCOMPARE(rows[0]->children[2]->value, QStringLiteral("11"));
}

void StructViewRawRendererTests::builderPadsDeclarationsToAlignmentBoundaries()
{
    // Scenario: variable payloads render their true byte counts, but the file
    // format pads each field end to a fixed boundary before the next
    // declaration.
    // Expected: pad_to(4) rounds only the consumed declaration length, so the
    // payload previews stay data-sized and following fields start at the
    // padded boundary.
    Parser parser;
    QVERIFY(parseBuffer(parser,
                        "[export]\n"
                        "struct Root {\n"
                        "  byte len;\n"
                        "  [string, count(len), pad_to(4)]\n"
                        "  byte data[];\n"
                        "  [count(8), terminated_by(0), pad_to(4)]\n"
                        "  char name[];\n"
                        "  byte afterName;\n"
                        "} root;\n"));

    auto rows = buildRows(parser.GetStrataLibrary(),
                          firstExported(parser.GetStrataLibrary()),
                          QByteArray::fromHex("02414200585900000B"));

    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->children.size(), size_t(4));
    QCOMPARE(rows[0]->children[1]->value, QStringLiteral("\"AB\""));
    QCOMPARE(rows[0]->children[2]->value, QStringLiteral("\"XY\""));
    QCOMPARE(rows[0]->children[3]->name, QStringLiteral("byte afterName"));
    QCOMPARE(rows[0]->children[3]->absoluteOffset, uint64_t(8));
    QCOMPARE(rows[0]->children[3]->value, QStringLiteral("11"));
}

void StructViewRawRendererTests::builderSkipsAbsentOptionalDeclarations()
{
    // Scenario: a PE-style NT header reports no optional-header bytes.
    // Expected: optional(expr) suppresses the optional-header union entirely, so
    // the section array starts naturally after the fixed signature/file-header
    // prefix instead of rendering both union branches or relying on extent(0).
    // Regression guard: a missing switch selector must not make a switched union
    // render every possible branch.
    Parser parser;
    QVERIFY(parseBuffer(parser,
                        "typedef struct _FileHeader { byte NumberOfSections; byte SizeOfOptionalHeader; } FileHeader;\n"
                        "typedef struct _Optional32 { word Magic; byte marker32; } Optional32;\n"
                        "typedef struct _Optional64 { word Magic; dword marker64; } Optional64;\n"
                        "typedef struct _Section { byte value; } Section;\n"
                        "typedef struct _NtHeaders {\n"
                        "  dword Signature;\n"
                        "  FileHeader FileHeader;\n"
                        "  [optional(FileHeader.SizeOfOptionalHeader != 0), switch_is(OptionalHeader32.Magic), extent(FileHeader.SizeOfOptionalHeader)]\n"
                        "  union {\n"
                        "    [case(0x10b)] Optional32 OptionalHeader32;\n"
                        "    [case(0x20b)] Optional64 OptionalHeader64;\n"
                        "  };\n"
                        "} NtHeaders;\n"
                        "[export]\n"
                        "struct Root {\n"
                        "  NtHeaders ntHeaders;\n"
                        "  [size_is(ntHeaders.FileHeader.NumberOfSections)] Section sections[];\n"
                        "} root;\n"));

    auto rows = buildRows(parser.GetStrataLibrary(),
                          firstExported(parser.GetStrataLibrary()),
                          QByteArray::fromHex("00000000" "02" "00" "0A0B"));

    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->children[0]->children.size(), size_t(2));
    QCOMPARE(rows[0]->children[1]->absoluteOffset, uint64_t(6));
    QCOMPARE(rows[0]->children[1]->children.size(), size_t(2));
    QCOMPARE(rows[0]->children[1]->children[1]->children[0]->value, QStringLiteral("11"));
}

void StructViewRawRendererTests::builderUsesSizeIsForUnsizedArrays()
{
    // Scenario: a file format stores an array count in an earlier field, and
    // the Strata declaration uses [] plus [size_is(...)] rather than a fixed
    // declarator bound.
    // Expected: the parser accepts the unsized array syntax and the renderer
    // expands exactly the count read from the already-rendered structure data.
    // Regression guard: PE section headers must not be capped by a placeholder
    // array size in pe.strata just because the count is data-driven.
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "typedef struct _Header { byte count; } Header;\n"
                        "typedef struct _Item { byte value; } Item;\n"
                        "[export]\n"
                        "struct Root {\n"
                        "  Header header;\n"
                        "  [size_is(header.count)] Item items[];\n"
                        "} root;\n"));

    const QByteArray bytes = QByteArray::fromHex("030A0B0C");
    auto rows = buildRows(&library, firstExported(&library), bytes);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->children.size(), size_t(2));
    QCOMPARE(rows[0]->children[1]->name, QStringLiteral("Item items[]"));
    QCOMPARE(rows[0]->children[1]->children.size(), size_t(3));
    QCOMPARE(rows[0]->children[1]->children[0]->children[0]->value, QStringLiteral("10"));
    QCOMPARE(rows[0]->children[1]->children[2]->children[0]->value, QStringLiteral("12"));
}

void StructViewRawRendererTests::builderUsesMaxCountAndTerminatorExpressions()
{
    // Scenario: sentinel arrays often have a safety cap rather than an exact
    // count, and their stop condition may inspect fields of a compound element
    // or a raw byte marker.
    // Expected: max_count(...) bounds the loop, terminated_by(...) evaluates
    // against the rendered element row, structured terminators are included by
    // default, and byte-sequence terminators are hidden but consumed as a whole.
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "typedef struct _Rec { byte kind; byte size; } Rec;\n"
                        "[export]\n"
                        "struct Root {\n"
                        "  [max_count(8), terminated_by(kind == 0 && size == 0)] Rec records[];\n"
                        "  byte afterRecords;\n"
                        "  [max_count(16), terminated_by({ 0xAA, 0xBB, 0xCC })] byte payload[];\n"
                        "  byte afterPayload;\n"
                        "} root;\n"));

    const QByteArray bytes = QByteArray::fromHex("01020000074849AABBCC55");
    auto rows = buildRows(&library, firstExported(&library), bytes);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->children.size(), size_t(4));

    StructureRow *records = rows[0]->children[0].get();
    QCOMPARE(records->name, QStringLiteral("Rec records[]"));
    QCOMPARE(records->children.size(), size_t(2));
    QCOMPARE(records->byteLength, uint64_t(4));
    QCOMPARE(records->children[0]->children[0]->value, QStringLiteral("1"));
    QCOMPARE(records->children[1]->children[0]->value, QStringLiteral("0"));
    QCOMPARE(rows[0]->children[1]->name, QStringLiteral("byte afterRecords"));
    QCOMPARE(rows[0]->children[1]->absoluteOffset, uint64_t(4));
    QCOMPARE(rows[0]->children[1]->value, QStringLiteral("7"));

    StructureRow *payload = rows[0]->children[2].get();
    QCOMPARE(payload->name, QStringLiteral("byte payload[]"));
    QCOMPARE(payload->children.size(), size_t(2));
    QCOMPARE(payload->byteLength, uint64_t(5));
    QCOMPARE(payload->children[0]->value, QStringLiteral("72"));
    QCOMPARE(payload->children[1]->value, QStringLiteral("73"));
    QCOMPARE(rows[0]->children[3]->name, QStringLiteral("byte afterPayload"));
    QCOMPARE(rows[0]->children[3]->absoluteOffset, uint64_t(10));
    QCOMPARE(rows[0]->children[3]->value, QStringLiteral("85"));
}

void StructViewRawRendererTests::builderUsesNamedOffsetMapsAndValueAt()
{
    // Scenario: definitions can name address spaces for base-relative and
    // range-mapped offsets, and can use value_at(...) for one-off scalar probes.
    // Expected: offset("space", expr) places fields through the named map, and
    // value_at(...) reads fixed-size scalars with the current endian.
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "typedef struct _Section { dword va; dword size; dword raw; } Section;\n"
                        "typedef struct _Entry { dword value; } Entry;\n"
                        "[export, offset_map(\"strings\", stringBase)]\n"
                        "struct Root {\n"
                        "  dword stringBase;\n"
                        "  dword nameOffset;\n"
                        "  dword targetRva;\n"
                        "  dword probeRva;\n"
                        "  [offset_map(\"rva\", va, size, raw), count(1)] Section sections[];\n"
                        "  [offset(\"strings\", nameOffset), string, max_count(16), terminated_by(0)] char name[];\n"
                        "  [offset(\"rva\", targetRva)] dword mappedValue;\n"
                        "  [optional(value_at(\"rva\", probeRva, word) == 0x1234), offset(28)] byte mappedProbe;\n"
                        "  [optional(value_at(28, word) == 0x6b5a), offset(29)] byte localProbe;\n"
                        "  [dynamic_array(name(Values), type(Entry), offset(\"rva\", targetRva), count(1)), offset(30)] byte owner;\n"
                        "} root;\n"));

    QByteArray bytes(0xa0, '\0');
    writeLe32(&bytes, 0, 0x40);   // stringBase
    writeLe32(&bytes, 4, 0x05);   // nameOffset
    writeLe32(&bytes, 8, 0x1020); // targetRva
    writeLe32(&bytes, 12, 0x1030);// probeRva
    writeLe32(&bytes, 16, 0x1000);// section va
    writeLe32(&bytes, 20, 0x80);  // section size
    writeLe32(&bytes, 24, 0x60);  // section raw
    writeLe32(&bytes, 0x80, 0xAABBCCDD); // rva 0x1020
    writeLe16(&bytes, 0x90, 0x1234);     // rva 0x1030
    bytes[0x45] = 'N';
    bytes[0x46] = 'a';
    bytes[0x47] = 'm';
    bytes[0x48] = 'e';
    bytes[0x49] = '\0';
    bytes[0x1c] = 0x5a;
    bytes[0x1d] = 0x6b;

    auto rows = buildRows(&library, firstExported(&library), bytes);
    QCOMPARE(rows.size(), size_t(1));

    StructureRow *name = findChildNamed(rows[0].get(), QStringLiteral("char name[]"));
    QVERIFY2(name, qPrintable(childNames(rows[0].get())));
    QCOMPARE(name->absoluteOffset, uint64_t(0x45));
    QCOMPARE(name->value, QStringLiteral("\"Name\""));

    StructureRow *mappedValue = findChildNamed(rows[0].get(), QStringLiteral("dword mappedValue"));
    QVERIFY2(mappedValue, qPrintable(childNames(rows[0].get())));
    QCOMPARE(mappedValue->absoluteOffset, uint64_t(0x80));
    QCOMPARE(mappedValue->value, QStringLiteral("2864434397"));

    StructureRow *mappedProbe = findChildNamed(rows[0].get(), QStringLiteral("byte mappedProbe"));
    QVERIFY2(mappedProbe, qPrintable(childNames(rows[0].get())));
    QCOMPARE(mappedProbe->value, QStringLiteral("90"));

    StructureRow *localProbe = findChildNamed(rows[0].get(), QStringLiteral("byte localProbe"));
    QVERIFY2(localProbe, qPrintable(childNames(rows[0].get())));
    QCOMPARE(localProbe->value, QStringLiteral("107"));

    StructureRow *owner = findChildNamed(rows[0].get(), QStringLiteral("byte owner"));
    QVERIFY2(owner, qPrintable(childNames(rows[0].get())));
    StructureRow *values = findChildNamed(owner, QStringLiteral("Entry Values[]"));
    QVERIFY2(values, qPrintable(childNames(owner)));
    QCOMPARE(values->absoluteOffset, uint64_t(0x80));
    QCOMPARE(values->children.size(), size_t(1));
    QCOMPARE(values->children[0]->children[0]->value, QStringLiteral("2864434397"));
}

void StructViewRawRendererTests::builderUsesScopePrefixesForRootAndParent()
{
    // Scenario: scope prefixes resolve against the enclosing row hierarchy
    // instead of being treated as literal field names.
    // Expected: parent:: resolves to the immediate container row and root::
    // resolves to the file root row.
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "typedef struct _Inner {\n"
                        "  byte innerValue;\n"
                        "  [optional(parent::header == 0x41)] byte parentHit;\n"
                        "  [optional(root::header == 0x41)] byte rootHit;\n"
                        "} Inner;\n"
                        "[export]\n"
                        "struct Root {\n"
                        "  byte header;\n"
                        "  Inner inner;\n"
                        "} root;\n"));

    QByteArray bytes(4, '\0');
    bytes[0] = 0x41;
    bytes[1] = 0x41;

    auto rows = buildRows(&library, firstExported(&library), bytes);
    QCOMPARE(rows.size(), size_t(1));

    StructureRow *inner = findChildNamed(rows[0].get(), QStringLiteral("Inner inner"));
    QVERIFY2(inner, qPrintable(childNames(rows[0].get())));
    QVERIFY2(findChildNamed(inner, QStringLiteral("byte parentHit")), qPrintable(childNames(inner)));
    QVERIFY2(findChildNamed(inner, QStringLiteral("byte rootHit")), qPrintable(childNames(inner)));
}

void StructViewRawRendererTests::builderUsesCountAsForLogicalArraySlots()
{
    // Scenario: some serialized array elements consume more than one logical
    // slot in the format's count field.
    // Expected: byte layout advances by the actual rendered element size, while
    // the array loop counter advances by count_as(expr).
    Parser parser;
    QVERIFY(parseBuffer(parser,
                        "[export]\n"
                        "struct Root {\n"
                        "  byte itemSlots;\n"
                        "  [count(itemSlots)]\n"
                        "  struct Entry {\n"
                        "    byte tag;\n"
                        "    [select(tag)]\n"
                        "    union {\n"
                        "      [case(2), count_as(2)] word wide;\n"
                        "      [default] byte narrow;\n"
                        "    };\n"
                        "  } entries[];\n"
                        "  byte after;\n"
                        "} root;\n"));

    auto rows = buildRows(parser.GetStrataLibrary(),
                          firstExported(parser.GetStrataLibrary()),
                          QByteArray::fromHex("0302123401990B"));

    QCOMPARE(rows.size(), size_t(1));
    StructureRow *entries = findChildNamed(rows[0].get(), QStringLiteral("struct Entry entries[]"));
    QVERIFY2(entries, qPrintable(childNames(rows[0].get())));
    QCOMPARE(entries->children.size(), size_t(2));
    QCOMPARE(entries->children[0]->name, QStringLiteral("[0]"));
    QCOMPARE(entries->children[1]->name, QStringLiteral("[2]"));

    StructureRow *after = findChildNamed(rows[0].get(), QStringLiteral("byte after"));
    QVERIFY2(after, qPrintable(childNames(rows[0].get())));
    QCOMPARE(after->absoluteOffset, uint64_t(6));
    QCOMPARE(after->value, QStringLiteral("11"));

    QByteArray capped;
    capped.append(char(101));
    for (int i = 0; i < 101; ++i)
    {
        capped.append(char(1));
        capped.append(char(0x80 + (i & 0x0f)));
    }
    capped.append(char(0x0b));

    rows = buildRows(parser.GetStrataLibrary(), firstExported(parser.GetStrataLibrary()), capped);
    QCOMPARE(rows.size(), size_t(1));
    entries = findChildNamed(rows[0].get(), QStringLiteral("struct Entry entries[]"));
    QVERIFY2(entries, qPrintable(childNames(rows[0].get())));
    QCOMPARE(entries->children.size(), size_t(100));
    QCOMPARE(entries->children.back()->name, QStringLiteral("[99]"));

    after = findChildNamed(rows[0].get(), QStringLiteral("byte after"));
    QVERIFY2(after, qPrintable(childNames(rows[0].get())));
    QCOMPARE(after->absoluteOffset, uint64_t(203));
    QCOMPARE(after->value, QStringLiteral("11"));
}

void StructViewRawRendererTests::builderEvaluatesTernaryExpressions()
{
    // Scenario: a structure definition uses a C-style conditional expression to
    // select between two possible array counts from already-rendered file data.
    // Expected: the renderer evaluates only the matching ternary branch and
    // expands the flexible array to that count.
    // Regression guard: ternary expressions should work in Structure View tags,
    // so simple data-driven choices do not need a semantic C++ interpreter.
    const char *definition =
        "[export]\n"
        "struct Root {\n"
        "  byte flag;\n"
        "  [size_is(flag ? 3 : 1)] byte values[];\n"
        "} root;\n";

    auto render = [definition](const QByteArray &bytes) {
        auto library = std::make_unique<StrataLibrary>();
        Parser parser(library.get());
        if (!parseBuffer(parser, definition))
            return std::vector<std::unique_ptr<StructureRow>>();
        return buildRows(library.get(), firstExported(library.get()), bytes);
    };

    auto rowsWhenTrue = render(QByteArray::fromHex("010A0B0C"));
    QCOMPARE(rowsWhenTrue.size(), size_t(1));
    QCOMPARE(rowsWhenTrue[0]->children[1]->children.size(), size_t(3));
    QCOMPARE(rowsWhenTrue[0]->children[1]->children[2]->value, QStringLiteral("12"));

    auto rowsWhenFalse = render(QByteArray::fromHex("000A0B0C"));
    QCOMPARE(rowsWhenFalse.size(), size_t(1));
    QCOMPARE(rowsWhenFalse[0]->children[1]->children.size(), size_t(1));
    QCOMPARE(rowsWhenFalse[0]->children[1]->children[0]->value, QStringLiteral("10"));
}

void StructViewRawRendererTests::builderUsesCommonUnionPrefixForSizeIs()
{
    // Scenario: a PE-style header has common fields followed by a 32/64-bit
    // union, while a later flexible array count lives in the common prefix.
    // Expected: [size_is(ntHeaders.FileHeader.NumberOfSections)] works for both
    // union branches and the selected branch still controls where the array
    // starts in the byte stream.
    // Regression guard: section counts must not depend on spelling a fake
    // ntHeaders32/ntHeaders64 path just because the optional header is a union.
    const char *definition =
        "typedef struct _FileHeader { byte NumberOfSections; } FileHeader;\n"
        "typedef struct _Optional32 { word Magic; byte marker32; } Optional32;\n"
        "typedef struct _Optional64 { word Magic; dword marker64; } Optional64;\n"
        "typedef struct _Section { byte value; } Section;\n"
        "typedef struct _NtHeaders {\n"
        "  dword Signature;\n"
        "  FileHeader FileHeader;\n"
        "  [switch_is(OptionalHeader32.Magic)] union {\n"
        "    [case(0x10b)] Optional32 OptionalHeader32;\n"
        "    [case(0x20b)] Optional64 OptionalHeader64;\n"
        "  };\n"
        "} NtHeaders;\n"
        "[export]\n"
        "struct Root {\n"
        "  NtHeaders ntHeaders;\n"
        "  [size_is(ntHeaders.FileHeader.NumberOfSections)] Section sections[];\n"
        "} root;\n";

    auto render = [definition](const QByteArray &bytes) {
        auto library = std::make_unique<StrataLibrary>();
        Parser parser(library.get());
        if (!parseBuffer(parser, definition))
            return std::vector<std::unique_ptr<StructureRow>>();
        return buildRows(library.get(), firstExported(library.get()), bytes);
    };

    const QByteArray pe32 = QByteArray::fromHex("00000000" "03" "0B01" "AA" "0A0B0C");
    auto rows32 = render(pe32);
    QCOMPARE(rows32.size(), size_t(1));
    QCOMPARE(rows32[0]->children[0]->children[2]->name, QStringLiteral("Optional32 OptionalHeader32"));
    QCOMPARE(rows32[0]->children[1]->children.size(), size_t(3));
    QCOMPARE(rows32[0]->children[1]->children[2]->children[0]->value, QStringLiteral("12"));

    const QByteArray pe64 = QByteArray::fromHex("00000000" "05" "0B02" "44332211" "0102030405");
    auto rows64 = render(pe64);
    QCOMPARE(rows64.size(), size_t(1));
    QCOMPARE(rows64[0]->children[0]->children[2]->name, QStringLiteral("Optional64 OptionalHeader64"));
    QCOMPARE(rows64[0]->children[1]->children.size(), size_t(5));
    QCOMPARE(rows64[0]->children[1]->children[4]->children[0]->value, QStringLiteral("5"));
}

void StructViewRawRendererTests::builderEvaluatesTernaryUnionMemberSizeAndOffset()
{
    // Scenario: a later array is described by fields inside whichever union
    // branch matches the current file, and both size_is and offset use ternary
    // expressions over explicit branch-member paths.
    // Expected: only the selected ternary branch is resolved, so PE/ELF-style
    // 32/64 layouts can share one declaration without probing the wrong member.
    // Regression guard: branch-specific union field lookup must be robust enough
    // for definitions such as header64.count/header32.count in render tags.
    const char *definition =
        "typedef struct _H32 { byte count; byte tableOffset; byte marker32; } H32;\n"
        "typedef struct _H64 { byte pad; byte count; byte tableOffset; byte marker64; } H64;\n"
        "typedef struct _Item { byte value; } Item;\n"
        "[export]\n"
        "struct Root {\n"
        "  byte is64;\n"
        "  [switch_is(is64)] union {\n"
        "    [case(0)] H32 header32;\n"
        "    [case(1)] H64 header64;\n"
        "  };\n"
        "  [offset(is64 ? header64.tableOffset : header32.tableOffset), size_is(is64 ? header64.count : header32.count)] Item items[];\n"
        "} root;\n";

    auto render = [definition](const QByteArray &bytes) {
        auto library = std::make_unique<StrataLibrary>();
        Parser parser(library.get());
        if (!parseBuffer(parser, definition))
            return std::vector<std::unique_ptr<StructureRow>>();
        return buildRows(library.get(), firstExported(library.get()), bytes);
    };

    auto rows32 = render(QByteArray::fromHex("00" "0206AA" "0000" "0A0B"));
    QCOMPARE(rows32.size(), size_t(1));
    QCOMPARE(rows32[0]->children[1]->name, QStringLiteral("H32 header32"));
    QCOMPARE(rows32[0]->children[2]->offset, QStringLiteral("00000006"));
    QCOMPARE(rows32[0]->children[2]->children.size(), size_t(2));
    QCOMPARE(rows32[0]->children[2]->children[1]->children[0]->value, QStringLiteral("11"));

    auto rows64 = render(QByteArray::fromHex("01" "FF030899" "000000" "111213"));
    QCOMPARE(rows64.size(), size_t(1));
    QCOMPARE(rows64[0]->children[1]->name, QStringLiteral("H64 header64"));
    QCOMPARE(rows64[0]->children[2]->offset, QStringLiteral("00000008"));
    QCOMPARE(rows64[0]->children[2]->children.size(), size_t(3));
    QCOMPARE(rows64[0]->children[2]->children[2]->children[0]->value, QStringLiteral("19"));
}

void StructViewRawRendererTests::builderEvaluatesEndianAwareUnionMembers()
{
    // Scenario: a file-level endian tag changes how numeric fields are read, and
    // an array count is selected from a branch-specific union member.
    // Expected: expression reads used by size_is respect the active endian state
    // before the array is expanded.
    // Regression guard: ELF big-endian headers must not turn a count like 0x0003
    // into 0x0300 just because the field is reached through a union branch.
    const char *definition =
        "typedef struct _Header { word count; } Header;\n"
        "[export, endian(bigEndian)]\n"
        "struct Root {\n"
        "  byte bigEndian;\n"
        "  [switch_is(bigEndian)] union {\n"
        "    [case(0)] Header headerLe;\n"
        "    [case(1)] Header headerBe;\n"
        "  };\n"
        "  [size_is(bigEndian ? headerBe.count : headerLe.count)] byte values[];\n"
        "} root;\n";

    auto render = [definition](const QByteArray &bytes) {
        auto library = std::make_unique<StrataLibrary>();
        Parser parser(library.get());
        if (!parseBuffer(parser, definition))
            return std::vector<std::unique_ptr<StructureRow>>();
        return buildRows(library.get(), firstExported(library.get()), bytes);
    };

    auto rowsLe = render(QByteArray::fromHex("00" "0300" "0A0B0C"));
    QCOMPARE(rowsLe.size(), size_t(1));
    QCOMPARE(rowsLe[0]->children[2]->children.size(), size_t(3));
    QCOMPARE(rowsLe[0]->children[2]->children[2]->value, QStringLiteral("12"));

    auto rowsBe = render(QByteArray::fromHex("01" "0003" "0A0B0C"));
    QCOMPARE(rowsBe.size(), size_t(1));
    QCOMPARE(rowsBe[0]->children[2]->children.size(), size_t(3));
    QCOMPARE(rowsBe[0]->children[2]->children[2]->value, QStringLiteral("12"));
}

void StructViewRawRendererTests::builderEvaluatesArrayIndexedUnionMembers()
{
    // Scenario: a render expression reaches through a selected union member and
    // indexes into an array field inside that member.
    // Expected: field resolution applies the array element offset before reading
    // the final field value.
    // Regression guard: offset/size/name expressions often grow from simple
    // fields into paths like header64.entries[1].count as format support matures.
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "typedef struct _Entry { byte count; byte marker; } Entry;\n"
                        "typedef struct _H64 { Entry entries[2]; } H64;\n"
                        "[export]\n"
                        "struct Root {\n"
                        "  byte is64;\n"
                        "  [switch_is(is64)] union {\n"
                        "    [case(1)] H64 header64;\n"
                        "  };\n"
                        "  [size_is(header64.entries[1].count)] byte values[];\n"
                        "} root;\n"));

    auto rows = buildRows(&library, firstExported(&library), QByteArray::fromHex("01" "01AA03BB" "0A0B0C"));
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->children[2]->children.size(), size_t(3));
    QCOMPARE(rows[0]->children[2]->children[2]->value, QStringLiteral("12"));
}

void StructViewRawRendererTests::builderUsesNameFieldForStructArrayElements()
{
    // Scenario: a PE-style array contains structured elements whose meaningful
    // label lives inside each element rather than in the array index.
    // Expected: [name(Name)] keeps the array expandable but appends the rendered
    // child field value to each element row, giving labels like "[0].text".
    // Regression guard: name tags used to work only for enum-indexed arrays, so
    // section headers could not surface their embedded Name field in the grid.
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "typedef struct _Section { char Name[8]; dword size; } Section;\n"
                        "[export]\n"
                        "struct Root { [name(Name)] Section sections[2]; } root;\n"));

    const QByteArray bytes = QByteArray(".text\0\0\0", 8)
                             + QByteArray::fromHex("10000000")
                             + QByteArray(".rdata\0\0", 8)
                             + QByteArray::fromHex("20000000");
    auto rows = buildRows(&library, firstExported(&library), bytes);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->children.size(), size_t(1));
    QCOMPARE(rows[0]->children[0]->name, QStringLiteral("Section sections[]"));
    QVERIFY(rows[0]->children[0]->emphasizeName);
    QCOMPARE(rows[0]->children[0]->children.size(), size_t(2));
    QCOMPARE(rows[0]->children[0]->children[0]->name, QStringLiteral("[0].text"));
    QCOMPARE(rows[0]->children[0]->children[1]->name, QStringLiteral("[1].rdata"));
    QVERIFY(!rows[0]->children[0]->children[0]->emphasizeName);
    QVERIFY(!rows[0]->children[0]->children[1]->emphasizeName);
    QCOMPARE(rows[0]->children[0]->children[0]->children[0]->value, QStringLiteral("\".text\""));
    QCOMPARE(rows[0]->children[0]->children[1]->children[0]->value, QStringLiteral("\".rdata\""));
}

void StructViewRawRendererTests::builderAlignsFieldNamesWithinCompoundTypes()
{
    // Scenario: sibling fields have type prefixes with different display widths,
    // such as "dword" followed by "signed word".
    // Expected: the model keeps normal display text but exposes split name
    // pieces, letting the delegate align identifiers with font metrics.
    // Regression guard: visual alignment must not be faked with spaces because
    // proportional UI fonts make character-count padding visibly wrong.
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "[export]\n"
                        "struct Root { dword a; signed word b; } root;\n"));

    const QByteArray bytes = QByteArray::fromHex("010000000200");
    auto rows = buildRows(&library, firstExported(&library), bytes);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->children.size(), size_t(2));
    QCOMPARE(rows[0]->children[0]->name, QStringLiteral("dword a"));
    QCOMPARE(rows[0]->children[0]->nameTypePrefix, QStringLiteral("dword"));
    QCOMPARE(rows[0]->children[0]->nameIdentifier, QStringLiteral("a"));
    QCOMPARE(rows[0]->children[1]->name, QStringLiteral("signed word b"));
    QCOMPARE(rows[0]->children[1]->nameTypePrefix, QStringLiteral("signed word"));
    QCOMPARE(rows[0]->children[1]->nameIdentifier, QStringLiteral("b"));
}

void StructViewRawRendererTests::builderKeepsSignedPrimitiveTypedefNamesInStorageMode()
{
    // Scenario: basetypes.strata defines "short"/"int"/"long"/etc. as ordinary
    // typedefs of a signed/unsigned primitive (e.g. typedef signed word short;)
    // -- there is no built-in TYPE for them, they are ordinary user-level
    // typedefs exactly like e32/DOSTIME.
    // Expected: "Storage type" display mode still unwraps a plain typedef of a
    // bare primitive (e.g. e32 -> dword), but a typedef of a signed/unsigned
    // primitive keeps its own name instead of unwrapping to "signed word".
    // Regression guard: short/int/long must not render as "signed word"/
    // "signed dword" once Storage mode is enabled (the panel's default).
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "typedef signed word short;\n"
                        "typedef dword e32;\n"
                        "[export]\n"
                        "struct Root { short a; e32 b; } root;\n"));

    const QByteArray bytes = QByteArray::fromHex("0100020000000000");
    auto rows = buildRows(&library, firstExported(&library), bytes);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->children.size(), size_t(2));
    QCOMPARE(rows[0]->children[0]->name, QStringLiteral("short a"));
    QCOMPARE(rows[0]->children[1]->name, QStringLiteral("e32 b"));

    StructureRow *rootRow = rows[0].get();
    std::vector<std::unique_ptr<StructureRow>> modelRows;
    modelRows.push_back(std::move(rows[0]));
    StructureTreeModel model;
    model.setRowsForTests(std::move(modelRows));
    StructureDisplayOptions storageOptions;
    storageOptions.typeNameMode = StructureTypeNameMode::Storage;
    model.applyDisplayOptions(storageOptions);

    QCOMPARE(rootRow->children[0]->name, QStringLiteral("short a"));
    QCOMPARE(rootRow->children[0]->nameTypePrefix, QStringLiteral("short"));
    QCOMPARE(rootRow->children[1]->name, QStringLiteral("dword b"));
    QCOMPARE(rootRow->children[1]->nameTypePrefix, QStringLiteral("dword"));
}

void StructViewRawRendererTests::builderBuildsNestedStructRowsAndOffsets()
{
    // Scenario: a root structure contains a nested structure value.
    // Expected: the nested row is expandable, child offsets advance inside it,
    // and offsets are displayed as zero-padded absolute hex addresses.
    // Regression guard: recursive rendering must not collapse nested structs
    // into a flat definition list or lose byte positions.
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "struct Inner { byte x; word y; };\n"
                        "[export]\n"
                        "struct Root { byte magic; struct Inner inner; } root;\n"));

    const QByteArray bytes = QByteArray::fromHex("00000000AA112233");
    auto rows = buildRows(&library, firstExported(&library), bytes, 4);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->children.size(), size_t(2));
    QCOMPARE(rows[0]->children[0]->offset, QStringLiteral("00000004"));
    QCOMPARE(rows[0]->children[1]->offset, QStringLiteral("00000005"));
    QCOMPARE(rows[0]->children[1]->children.size(), size_t(2));
    QCOMPARE(rows[0]->children[1]->children[0]->offset, QStringLiteral("00000005"));
    QCOMPARE(rows[0]->children[1]->children[1]->offset, QStringLiteral("00000006"));
}

void StructViewRawRendererTests::builderSupportsArraysOffsetsEnumsAndSwitchCases()
{
    // Scenario: Strata tags drive the visual interpretation: an offset jumps to
    // a later byte, enum values display labels, arrays use evaluated counts, and
    // a switch_is union chooses the matching case.
    // Expected: each of those legacy-core tags affects only the relevant rows.
    // Regression guard: the new engine must preserve the useful old TypeView
    // behaviour without keeping the Win32 grid dependency.
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "enum Kind { One = 1, Two = 2 };\n"
                        "[export]\n"
                        "struct Root {\n"
                        "  byte count;\n"
                        "  byte values[count];\n"
                        "  [offset(8), enum(\"Kind\")] word kind;\n"
                        "  [switch_is(kind)] union Choice {\n"
                        "    [case(1)] byte small;\n"
                        "    [case(2)] word large;\n"
                        "  } choice;\n"
                        "} root;\n"));

    const QByteArray bytes = QByteArray::fromHex("030A0B0C0000000002003412");
    auto rows = buildRows(&library, firstExported(&library), bytes);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->children.size(), size_t(4));
    QCOMPARE(rows[0]->children[1]->children.size(), size_t(3));
    QCOMPARE(rows[0]->children[2]->offset, QStringLiteral("00000008"));
    QCOMPARE(rows[0]->children[2]->value, QStringLiteral("Two"));
    QCOMPARE(rows[0]->children[3]->children.size(), size_t(1));
    QCOMPARE(rows[0]->children[1]->name, QStringLiteral("byte values[]"));
    QCOMPARE(rows[0]->children[3]->children[0]->name, QStringLiteral("word large"));
    QCOMPARE(rows[0]->children[3]->children[0]->value, QStringLiteral("4660"));
}

void StructViewRawRendererTests::builderExposesEnumChoicesAndEntrypoints()
{
    // Scenario: a rendered field has an enum display tag, and another field
    // identifies where executable code begins.
    // Expected: the value row keeps the enum label choices for a combo editor,
    // and the entrypoint row exposes a concrete file offset for UI integration.
    // Regression guard: dropdown editing and disassembler handoff should be
    // driven by renderer metadata, not by parsing display text in the delegate.
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "enum Kind { One = 1, Two = 2 };\n"
                        "[export]\n"
                        "struct Root {\n"
                        "  [enum(\"Kind\")] byte kind;\n"
                        "  [entrypoint] dword entryRva;\n"
                        "} root;\n"));

    const QByteArray bytes = QByteArray(4, '\0') + QByteArray::fromHex("02" "10000000");
    auto rows = buildRows(&library, firstExported(&library), bytes, 4);
    QCOMPARE(rows.size(), size_t(1));

    StructureRow *kind = rows[0]->children[0].get();
    QCOMPARE(kind->value, QStringLiteral("Two"));
    QCOMPARE(kind->valueChoices, QStringList({ QStringLiteral("One"), QStringLiteral("Two") }));

    StructureRow *entry = rows[0]->children[1].get();
    QVERIFY(entry->hasCodeTarget);
    QCOMPARE(entry->codeLogicalOffset, uint64_t(0x10));
    QCOMPARE(entry->codeTargetOffset, uint64_t(0x14));
}

void StructViewRawRendererTests::builderRejectsOverflowingCodeAndOpenAsTargets()
{
    // Scenario: malformed data drives declarative UI targets through negative
    // or overflowing expressions. Expected: the metadata is omitted rather
    // than wrapping into a plausible-looking file offset.
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "typedef struct _Child { byte magic; } Child;\n"
                        "[code(\"x86\", offset(-1), extent(4)), open_as(type(Child), offset(-1), extent(4), name(\"bad\"))]\n"
                        "typedef struct _Entry { byte marker; } Entry;\n"
                        "[export]\n"
                        "struct Root { Entry entry; } root;\n"));

    QByteArray bytes(1, '\0');
    auto rows = buildRows(&library, firstExported(&library), bytes);
    QCOMPARE(rows.size(), size_t(1));
    StructureRow *entry = findChildNamed(rows[0].get(), QStringLiteral("Entry entry"));
    QVERIFY(entry);
    QVERIFY(!entry->hasCodeTarget);
    QVERIFY(!entry->hasOpenAsTarget);
}

void StructViewRawRendererTests::builderExposesOpenAsTargets()
{
    // Scenario: a container element describes a physical byte range that can be
    // opened as another Strata root.
    // Expected: the renderer attaches one open_as target per rendered element,
    // evaluating offset/extent/name in that element's field scope.
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "typedef struct _Child { byte magic; } Child;\n"
                        "[open_as(type(Child), offset(dataOffset), extent(dataSize), name(fmt(\"slice {0}\", dataOffset)))]\n"
                        "typedef struct _Entry {\n"
                        "  dword dataOffset;\n"
                        "  dword dataSize;\n"
                        "} Entry;\n"
                        "[export]\n"
                        "struct Root {\n"
                        "  byte count;\n"
                        "  [count(count)] Entry entries[];\n"
                        "} root;\n"));

    QByteArray bytes(0x40, '\0');
    bytes[0] = char(2);
    writeLe32(&bytes, 1, 0x20);
    writeLe32(&bytes, 5, 0x04);
    writeLe32(&bytes, 9, 0x30);
    writeLe32(&bytes, 13, 0x08);
    auto rows = buildRows(&library, firstExported(&library), bytes);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->children.size(), size_t(2));

    StructureRow *entries = rows[0]->children[1].get();
    QCOMPARE(entries->children.size(), size_t(2));

    StructureRow *first = entries->children[0].get();
    QVERIFY(first->hasOpenAsTarget);
    QVERIFY(first->openAsRootType);
    QCOMPARE(first->openAsRootTypeName, QStringLiteral("Child"));
    QCOMPARE(first->openAsName, QStringLiteral("slice 32"));
    QCOMPARE(first->openAsOffset, uint64_t(0x20));
    QCOMPARE(first->openAsByteLength, uint64_t(4));

    StructureRow *second = entries->children[1].get();
    QVERIFY(second->hasOpenAsTarget);
    QCOMPARE(second->openAsName, QStringLiteral("slice 48"));
    QCOMPARE(second->openAsOffset, uint64_t(0x30));
    QCOMPARE(second->openAsByteLength, uint64_t(8));
}

void StructViewRawRendererTests::builderEvaluatesUnionSwitchSelectorsFromTypedLayout()
{
    // Scenario: a union selector references a field inside one possible union
    // member before that member has been rendered as a row.
    // Expected: switch_is can still read the selector from the typed layout at
    // the union offset, then render only the matching case.
    // Regression guard: PE uses ntHeaders32.OptionalHeader.Magic this way, so
    // row-context-only evaluation cannot decide the union case.
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "[export]\n"
                        "struct Root {\n"
                        "  byte prefix;\n"
                        "  [offset(4), switch_is(choice32.magic)] union Choice {\n"
                        "    [case(0x10b)] struct H32 { word magic; byte selected32; } choice32;\n"
                        "    [case(0x20b)] struct H64 { word magic; byte selected64; } choice64;\n"
                        "  };\n"
                        "} root;\n"));

    const QByteArray bytes = QByteArray::fromHex("AA0000000B0199");
    auto rows = buildRows(&library, firstExported(&library), bytes);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->children.size(), size_t(2));
    QCOMPARE(rows[0]->children[1]->name, QStringLiteral("struct H32 choice32"));
    QCOMPARE(rows[0]->children[1]->children.size(), size_t(2));
    QCOMPARE(rows[0]->children[1]->children[1]->name, QStringLiteral("byte selected32"));
    QCOMPARE(rows[0]->children[1]->children[1]->value, QStringLiteral("153"));
}

void StructViewRawRendererTests::builderEvaluatesFieldsAndCorrectedExpressions()
{
    // Scenario: an array bound references a field already rendered in the
    // current row.
    // Expected: field lookup reads from the row context rather than from UI text
    // or a process-global grid item.
    // Regression guard: expression evaluation is the most important separation
    // point between Strata syntax and file-data rendering.
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "[export]\n"
                        "struct Root {\n"
                        "  byte count;\n"
                        "  byte values[count];\n"
                        "} root;\n"));

    const QByteArray bytes = QByteArray::fromHex("02AABB");
    auto rows = buildRows(&library, firstExported(&library), bytes);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->children.size(), size_t(2));
    QCOMPARE(rows[0]->children[1]->children.size(), size_t(2));
    QCOMPARE(rows[0]->children[1]->children[0]->value, QStringLiteral("170"));
    QCOMPARE(rows[0]->children[1]->children[1]->value, QStringLiteral("187"));
}

void StructViewRawRendererTests::builderEvaluatesFindSearchExpressions()
{
    // Scenario: offset(...) uses byte-pattern search expressions to place
    // fields outside the sequential layout stream.
    // Expected: find_first/find_last return structure-relative offsets, and
    // bounded forms search only the first/last N bytes respectively.
    // Regression guard: ZIP central-directory discovery depends on reverse
    // trailer search, while failed searches must not render misleading inline
    // fallback rows.
    Parser parser;
    QVERIFY(parseBuffer(parser,
                        "[export]\n"
                        "struct Root {\n"
                        "  [offset(find_first({ 0xAA, 0xBB }))] byte first;\n"
                        "  [offset(find_last({ 0xAA, 0xBB }))] byte last;\n"
                        "  [offset(find_first({ 0xAA, 0xBB }, 4))] byte firstLimited;\n"
                        "  [offset(find_last({ 0xAA, 0xBB }, 4))] byte lastLimited;\n"
                        "  [offset(find_first({ 0xCC }))] byte missing;\n"
                        "} root;\n"));

    const QByteArray bytes = QByteArray::fromHex("00 AABB 11 AABB 22 AABB 33");
    auto rows = buildRows(parser.GetStrataLibrary(), firstExported(parser.GetStrataLibrary()), bytes);
    QCOMPARE(rows.size(), size_t(1));

    StructureRow *first = findChildNamed(rows[0].get(), QStringLiteral("byte first"));
    StructureRow *last = findChildNamed(rows[0].get(), QStringLiteral("byte last"));
    StructureRow *firstLimited = findChildNamed(rows[0].get(), QStringLiteral("byte firstLimited"));
    StructureRow *lastLimited = findChildNamed(rows[0].get(), QStringLiteral("byte lastLimited"));
    QVERIFY(first);
    QVERIFY(last);
    QVERIFY(firstLimited);
    QVERIFY(lastLimited);
    QCOMPARE(first->relativeOffset, uint64_t(1));
    QCOMPARE(last->relativeOffset, uint64_t(7));
    QCOMPARE(firstLimited->relativeOffset, uint64_t(1));
    QCOMPARE(lastLimited->relativeOffset, uint64_t(7));
    QVERIFY(findChildNamed(rows[0].get(), QStringLiteral("byte missing")) == nullptr);
}

void StructViewRawRendererTests::builderEvaluatesOctalStringExpressions()
{
    // Scenario: formats such as TAR store numeric fields as ASCII octal.
    // Expected: octal(str(field)) converts a rendered string field into an
    // integer expression usable by later layout tags.
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "[export]\n"
                        "struct Root {\n"
                        "  [string, count(12), terminated_by(0)] char size[];\n"
                        "  [count(octal(str(size)))] byte payload[];\n"
                        "} root;\n"));

    QByteArray bytes(17, '\0');
    writeAscii(&bytes, 0, "00000000005");
    bytes[12] = char(0x11);
    bytes[13] = char(0x22);
    bytes[14] = char(0x33);
    bytes[15] = char(0x44);
    bytes[16] = char(0x55);

    auto rows = buildRows(parser.GetStrataLibrary(), firstExported(parser.GetStrataLibrary()), bytes);
    QCOMPARE(rows.size(), size_t(1));
    StructureRow *payload = findChildNamed(rows[0].get(), QStringLiteral("byte payload[]"));
    QVERIFY2(payload, qPrintable(childNames(rows[0].get())));
    QCOMPARE(payload->value, QStringLiteral("{ 17, 34, 51, 68, 85 }"));
}

void StructViewRawRendererTests::builderSelectsUnionMembersFromStringExpressions()
{
    // Scenario: a nameless union discriminator is a NUL-terminated string found
    // through an offset expression, and cases are string literals rather than
    // numeric constants.
    // Expected: select(cstr_at(...)) chooses the matching string case, and
    // falls back to [default] when no case matches. Because the union has no
    // declarator, the selected child is flattened into the parent row.
    Parser parser;
    QVERIFY(parseBuffer(parser,
                        "[export]\n"
                        "struct Root {\n"
                        "  dword strings;\n"
                        "  byte nameoff;\n"
                        "  [select(cstr_at(strings + nameoff, 16))]\n"
                        "  union {\n"
                        "    [case(\"known\")] byte selected;\n"
                        "    [default] word fallback;\n"
                        "  };\n"
                        "} root;\n"));

    QByteArray known(32, '\0');
    writeLe32(&known, 0, 12);
    known[4] = char(0);
    known[5] = char(0x7f);
    writeAscii(&known, 12, "known");

    auto knownRows = buildRows(parser.GetStrataLibrary(), firstExported(parser.GetStrataLibrary()), known);
    QVERIFY(findChildNamed(knownRows[0].get(), QStringLiteral("byte selected")));
    QVERIFY(!findChildNamed(knownRows[0].get(), QStringLiteral("union value")));
    QVERIFY(!findChildNamed(knownRows[0].get(), QStringLiteral("word fallback")));

    QByteArray unknown(32, '\0');
    writeLe32(&unknown, 0, 12);
    unknown[4] = char(6);
    writeLe16(&unknown, 5, 0x1234);
    writeAscii(&unknown, 12, "known");
    writeAscii(&unknown, 18, "other");

    auto unknownRows = buildRows(parser.GetStrataLibrary(), firstExported(parser.GetStrataLibrary()), unknown);
    QVERIFY(!findChildNamed(unknownRows[0].get(), QStringLiteral("union value")));
    QVERIFY(!findChildNamed(unknownRows[0].get(), QStringLiteral("byte selected")));
    StructureRow *fallback = findChildNamed(unknownRows[0].get(), QStringLiteral("word fallback"));
    QVERIFY(fallback);
    QCOMPARE(fallback->value, QStringLiteral("4660"));
}

void StructViewRawRendererTests::builderSelectsUnionMembersFromFourCcExpressions()
{
    // Scenario: FourCC fields are scalar integers, but definitions should be
    // able to display them as text and use readable FourCC literals in case tags.
    // Expected: [format("fourcc")] renders the original four file bytes, and
    // fourcc("....") compares using the active endian context.
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "[export, endian(\"big\")]\n"
                        "typedef struct _Big {\n"
                        "  [format(\"fourcc\")] dword tag;\n"
                        "  [select(tag)] union {\n"
                        "    [case(fourcc(\"ftyp\"))] byte bigHit;\n"
                        "    [default] byte bigMiss;\n"
                        "  };\n"
                        "} Big;\n"
                        "[export, endian(\"little\")]\n"
                        "typedef struct _Little {\n"
                        "  [format(\"fourcc\")] dword tag;\n"
                        "  [select(tag)] union {\n"
                        "    [case(fourcc(\"RIFF\"))] byte littleHit;\n"
                        "    [default] byte littleMiss;\n"
                        "  };\n"
                        "} Little;\n"));

    TypeDecl *bigRoot = exportedNamed(&library, QStringLiteral("Big"));
    QVERIFY(bigRoot);
    auto bigRows = buildRows(&library, bigRoot, QByteArray("ftyp\x7f", 5));
    QCOMPARE(bigRows.size(), size_t(1));
    StructureRow *bigTag = findChildNamed(bigRows[0].get(), QStringLiteral("dword tag"));
    QVERIFY2(bigTag, qPrintable(childNames(bigRows[0].get())));
    QCOMPARE(bigTag->value, QStringLiteral("\"ftyp\""));
    StructureRow *bigHit = findChildNamed(bigRows[0].get(), QStringLiteral("byte bigHit"));
    QVERIFY2(bigHit, qPrintable(childNames(bigRows[0].get())));
    QCOMPARE(bigHit->value, QStringLiteral("127"));
    QVERIFY(!findChildNamed(bigRows[0].get(), QStringLiteral("byte bigMiss")));

    TypeDecl *littleRoot = exportedNamed(&library, QStringLiteral("Little"));
    QVERIFY(littleRoot);
    auto littleRows = buildRows(&library, littleRoot, QByteArray("RIFF\x42", 5));
    QCOMPARE(littleRows.size(), size_t(1));
    StructureRow *littleTag = findChildNamed(littleRows[0].get(), QStringLiteral("dword tag"));
    QVERIFY2(littleTag, qPrintable(childNames(littleRows[0].get())));
    QCOMPARE(littleTag->value, QStringLiteral("\"RIFF\""));
    StructureRow *littleHit = findChildNamed(littleRows[0].get(), QStringLiteral("byte littleHit"));
    QVERIFY2(littleHit, qPrintable(childNames(littleRows[0].get())));
    QCOMPARE(littleHit->value, QStringLiteral("66"));
    QVERIFY(!findChildNamed(littleRows[0].get(), QStringLiteral("byte littleMiss")));
}

void StructViewRawRendererTests::builderUsesDynamicEndianExpressions()
{
    // Scenario: a format stores byte order in the file header, and the exported
    // root TypeDecl uses endian(expr) to select how later numeric fields read.
    // Expected: the same definition renders big-endian and little-endian inputs
    // differently, and expression reads inherit that byte order too.
    // Regression guard: ELF must not require hard-coded C++ endian knowledge for
    // ordinary raw fields declared in Strata.
    const char *definition =
        "[export, endian(marker == 2)]\n"
        "struct Root { byte marker; word value; dword count; byte items[count]; } root;\n";

    auto render = [definition](const QByteArray &bytes) {
        auto library = std::make_unique<StrataLibrary>();
        Parser parser(library.get());
        if (!parseBuffer(parser, definition))
            return std::vector<std::unique_ptr<StructureRow>>();
        return buildRows(library.get(), firstExported(library.get()), bytes);
    };

    auto bigRows = render(QByteArray::fromHex("02" "0102" "00000002" "AABB"));
    QCOMPARE(bigRows.size(), size_t(1));
    QCOMPARE(bigRows[0]->children[1]->value, QStringLiteral("258"));
    QCOMPARE(bigRows[0]->children[3]->children.size(), size_t(2));

    auto littleRows = render(QByteArray::fromHex("01" "0102" "02000000" "AABB"));
    QCOMPARE(littleRows.size(), size_t(1));
    QCOMPARE(littleRows[0]->children[1]->value, QStringLiteral("513"));
    QCOMPARE(littleRows[0]->children[3]->children.size(), size_t(2));
}

void StructViewRawRendererTests::builderEvaluatesEnumIndexedArraysInExpressions()
{
    // Scenario: an ELF-style identifier array is indexed by enum constants
    // inside size_is and offset expressions.
    // Expected: enum identifiers resolve before array indexing, so e_ident slots
    // can drive normal Structure View rendering decisions.
    // Regression guard: earlier ELF experiments failed around expressions like
    // header.e_ident[EI_CLASS] even though both pieces worked independently.
    const char *definition =
        "enum Ident { EI_CLASS = 4, EI_DATA = 5, ELFCLASS32 = 1 };\n"
        "typedef struct _Header { byte e_ident[8]; byte count32; byte tableOffset32; } Header;\n"
        "typedef struct _Item { byte value; } Item;\n"
        "[export]\n"
        "struct Root {\n"
        "  Header header;\n"
        "  [offset(header.e_ident[EI_CLASS] == ELFCLASS32 ? header.tableOffset32 : 0), size_is(header.e_ident[EI_CLASS] == ELFCLASS32 ? header.count32 : 0)] Item items[];\n"
        "} root;\n";

    auto render = [definition](const QByteArray &bytes) {
        auto library = std::make_unique<StrataLibrary>();
        Parser parser(library.get());
        if (!parseBuffer(parser, definition))
            return std::vector<std::unique_ptr<StructureRow>>();
        return buildRows(library.get(), firstExported(library.get()), bytes);
    };

    auto rows32 = render(QByteArray::fromHex("00000000" "01" "00" "0000" "02" "0C" "0000" "0A0B"));
    QCOMPARE(rows32.size(), size_t(1));
    QCOMPARE(rows32[0]->children[1]->offset, QStringLiteral("0000000C"));
    QCOMPARE(rows32[0]->children[1]->children.size(), size_t(2));
    QCOMPARE(rows32[0]->children[1]->children[1]->children[0]->value, QStringLiteral("11"));

    auto rowsOther = render(QByteArray::fromHex("00000000" "02" "00" "0000" "02" "0C" "0000" "0A0B"));
    QCOMPARE(rowsOther.size(), size_t(1));
    QCOMPARE(rowsOther[0]->children.size(), size_t(1));
}

void StructViewRawRendererTests::builderEvaluatesEnumIndexedUnionMembersInExpressions()
{
    // Scenario: a not-yet-rendered union branch contains an e_ident array, and a
    // declaration tag indexes that array with enum constants.
    // Expected: typed-layout field resolution, enum lookup, array indexing, and
    // endian(expr) evaluation all compose for the branch path.
    // Regression guard: definitions such as endian(header32.e_ident[EI_DATA] ==
    // ELFDATA2MSB) should be reliable if we choose that spelling in ELF.
    const char *definition =
        "enum Ident { EI_DATA = 5, ELFDATA2MSB = 2 };\n"
        "typedef struct _Header32 { byte e_ident[8]; word count; } Header32;\n"
        "[export, endian(header32.e_ident[EI_DATA] == ELFDATA2MSB)]\n"
        "struct Root {\n"
        "  byte selector;\n"
        "  [switch_is(selector)] union {\n"
        "    [case(1)] Header32 header32;\n"
        "  };\n"
        "  [size_is(header32.count)] byte values[];\n"
        "} root;\n";

    auto render = [definition](const QByteArray &bytes) {
        auto library = std::make_unique<StrataLibrary>();
        Parser parser(library.get());
        if (!parseBuffer(parser, definition))
            return std::vector<std::unique_ptr<StructureRow>>();
        return buildRows(library.get(), firstExported(library.get()), bytes);
    };

    auto littleRows = render(QByteArray::fromHex("01" "0000000000010000" "0300" "0A0B0C"));
    QCOMPARE(littleRows.size(), size_t(1));
    QCOMPARE(littleRows[0]->children[2]->children.size(), size_t(3));
    QCOMPARE(littleRows[0]->children[2]->children[2]->value, QStringLiteral("12"));

    auto bigRows = render(QByteArray::fromHex("01" "0000000000020000" "0003" "0A0B0C"));
    QCOMPARE(bigRows.size(), size_t(1));
    QCOMPARE(bigRows[0]->children[2]->children.size(), size_t(3));
    QCOMPARE(bigRows[0]->children[2]->children[2]->value, QStringLiteral("12"));
}

void StructViewRawRendererTests::builderOptionallySortsTopLevelRowsByOffset()
{
    // Scenario: archive formats such as ZIP may need to evaluate rows in one
    // order while displaying top-level structures in physical file order.
    // Expected: the opt-in renderer flag sorts only the exported root's direct
    // children; nested C struct fields remain declaration-ordered.
    // Regression guard: PE/ELF and ordinary structs keep declaration order by
    // default because sortTopLevelRowsByOffset defaults to false.
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "typedef struct _Inner {\n"
                        "  [offset(4)] byte childLate;\n"
                        "  [offset(2)] byte childEarly;\n"
                        "} Inner;\n"
                        "[export]\n"
                        "struct Root {\n"
                        "  [offset(8)] byte late;\n"
                        "  [offset(0)] byte early;\n"
                        "  Inner inner;\n"
                        "} root;\n"));

    QByteArray bytes(16, '\0');
    auto rows = buildRows(&library, firstExported(&library), bytes);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->children.size(), size_t(3));
    QCOMPARE(rows[0]->children[0]->name, QStringLiteral("byte late"));
    QCOMPARE(rows[0]->children[1]->name, QStringLiteral("byte early"));
    QCOMPARE(rows[0]->children[2]->name, QStringLiteral("Inner inner"));

    StructureDisplayOptions sortedOptions;
    sortedOptions.sortTopLevelRowsByOffset = true;
    auto sortedRows = buildRows(&library, firstExported(&library), bytes, 0, sortedOptions);
    QCOMPARE(sortedRows.size(), size_t(1));
    QCOMPARE(sortedRows[0]->children.size(), size_t(3));
    QCOMPARE(sortedRows[0]->children[0]->name, QStringLiteral("byte early"));
    QCOMPARE(sortedRows[0]->children[1]->name, QStringLiteral("byte late"));
    QCOMPARE(sortedRows[0]->children[2]->name, QStringLiteral("Inner inner"));

    StructureRow *inner = sortedRows[0]->children[2].get();
    QCOMPARE(inner->children.size(), size_t(2));
    QCOMPARE(inner->children[0]->name, QStringLiteral("byte childLate"));
    QCOMPARE(inner->children[1]->name, QStringLiteral("byte childEarly"));
}

void StructViewRawRendererTests::builderResolvesUnionDiscriminatorFromCandidateOnlyField()
{
    // Scenario: a discriminated union's selector field exists only inside
    // each [case(...)] candidate's own struct (e.g. ELF's e_ident pattern),
    // not as a sibling field of the enclosing struct.
    // Expected: resolveDirectField's union-candidate fallback finds it by
    // trying every case(...) candidate and requiring them to agree, so plain
    // field syntax works without needing select_offset(...).
    // Regression guard: Option 3 of the select_offset design (see
    // EXPR_RAWOFFSET in causeway/expr.h) -- the generalization meant to make
    // select_offset unnecessary for fields that really are identical across
    // every candidate.
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "enum Sel { A = 1, B = 2 };\n"
                        "typedef struct _CandA { byte ident[4]; word valueA; } CandA;\n"
                        "typedef struct _CandB { byte ident[4]; dword valueB; } CandB;\n"
                        "[export]\n"
                        "struct Root {\n"
                        "  [select(ident[0])]\n"
                        "  union {\n"
                        "    [case(A)] CandA candA;\n"
                        "    [case(B)] CandB candB;\n"
                        "  };\n"
                        "} root;\n"));

    QByteArray bytes(16, '\0');
    bytes[0] = char(1);
    writeLe16(&bytes, 4, 0x1234);

    auto rows = buildRows(&library, firstExported(&library), bytes);
    QCOMPARE(rows.size(), size_t(1));
    StructureRow *valueA = findDescendantNamed(rows[0].get(), QStringLiteral("word valueA"));
    QVERIFY2(valueA, "candA branch (selected via the union-candidate discriminator fallback) not found");
    QCOMPARE(valueA->value, QStringLiteral("4660"));
}

void StructViewRawRendererTests::definitionManagerFlagsNonStaticFieldReferences()
{
    // Scenario: a select/switch_is/endian/offset/size_is/optional/extent
    // expression references a field that cannot be resolved without a live
    // file -- neither a sibling field nor declared identically by every
    // case(...) candidate of an enclosing union.
    // Expected: validateStaticFieldReferences reports it with a message
    // naming the bad reference; well-formed definitions (the real elf.strata
    // and pe.strata, and an equivalent valid synthetic struct) produce no
    // false positives.
    // Regression guard: this is meant to catch the exact ELF e_ident mistake
    // this session kept hitting as a silent render-time failure, at
    // definition-load time instead.
    StrataLibrary goodLibrary;
    Parser goodParser(&goodLibrary);
    QVERIFY(parseBuffer(goodParser,
                        "enum Sel { A = 1, B = 2 };\n"
                        "typedef struct _CandA { byte ident[4]; word valueA; } CandA;\n"
                        "typedef struct _CandB { byte ident[4]; dword valueB; } CandB;\n"
                        "[export]\n"
                        "struct Root {\n"
                        "  [select(ident[0])]\n"
                        "  union {\n"
                        "    [case(A)] CandA candA;\n"
                        "    [case(B)] CandB candB;\n"
                        "  };\n"
                        "} root;\n"));
    QVERIFY(StructureRenderEngine::validateStaticFieldReferences(&goodLibrary).isEmpty());

    StrataLibrary badLibrary;
    Parser badParser(&badLibrary);
    QVERIFY(parseBuffer(badParser,
                        "enum Sel { A = 1, B = 2 };\n"
                        "typedef struct _CandA { byte ident[4]; word valueA; } CandA;\n"
                        "typedef struct _CandB { byte ident[4]; dword valueB; } CandB;\n"
                        "[export]\n"
                        "struct Root {\n"
                        "  [select(nope[0])]\n"
                        "  union {\n"
                        "    [case(A)] CandA candA;\n"
                        "    [case(B)] CandB candB;\n"
                        "  };\n"
                        "} root;\n"));
    const QStringList badErrors = StructureRenderEngine::validateStaticFieldReferences(&badLibrary);
    QCOMPARE(badErrors.size(), 1);
    QVERIFY2(badErrors.first().contains(QStringLiteral("nope")), qPrintable(badErrors.first()));

    StrataLibrary elfLibrary;
    QVERIFY2(parseStandardElfDefinition(&elfLibrary), "elf.strata failed to parse");
    QVERIFY(StructureRenderEngine::validateStaticFieldReferences(&elfLibrary).isEmpty());

    StrataLibrary peLibrary;
    QVERIFY2(parseStandardDefinition(&peLibrary, QStringLiteral("pe.strata")), "pe.strata failed to parse");
    const QStringList peErrors = StructureRenderEngine::validateStaticFieldReferences(&peLibrary);
    QVERIFY2(peErrors.isEmpty(), qPrintable(peErrors.join(QLatin1Char('\n'))));

    StrataLibrary zipLibrary;
    QVERIFY2(parseStandardDefinition(&zipLibrary, QStringLiteral("zip.strata")), "zip.strata failed to parse");
    QVERIFY(StructureRenderEngine::validateStaticFieldReferences(&zipLibrary).isEmpty());

    StrataLibrary sfntLibrary;
    QVERIFY2(parseStandardDefinition(&sfntLibrary, QStringLiteral("sfnt.strata")), "sfnt.strata failed to parse");
    QVERIFY(StructureRenderEngine::validateStaticFieldReferences(&sfntLibrary).isEmpty());

    StrataLibrary pngLibrary;
    QVERIFY2(parseStandardDefinition(&pngLibrary, QStringLiteral("png.strata")), "png.strata failed to parse");
    QVERIFY(StructureRenderEngine::validateStaticFieldReferences(&pngLibrary).isEmpty());

    StrataLibrary bmpLibrary;
    QVERIFY2(parseStandardDefinition(&bmpLibrary, QStringLiteral("bmp.strata")), "bmp.strata failed to parse");
    QVERIFY(StructureRenderEngine::validateStaticFieldReferences(&bmpLibrary).isEmpty());

    StrataLibrary icoLibrary;
    QVERIFY2(parseStandardDefinition(&icoLibrary, QStringLiteral("ico.strata")), "ico.strata failed to parse");
    QVERIFY(StructureRenderEngine::validateStaticFieldReferences(&icoLibrary).isEmpty());

    StrataLibrary gifLibrary;
    QVERIFY2(parseStandardDefinition(&gifLibrary, QStringLiteral("gif.strata")), "gif.strata failed to parse");
    QVERIFY(StructureRenderEngine::validateStaticFieldReferences(&gifLibrary).isEmpty());

    StrataLibrary woffLibrary;
    QVERIFY2(parseStandardDefinition(&woffLibrary, QStringLiteral("woff.strata")), "woff.strata failed to parse");
    QVERIFY(StructureRenderEngine::validateStaticFieldReferences(&woffLibrary).isEmpty());

    StrataLibrary mp4Library;
    QVERIFY2(parseStandardDefinition(&mp4Library, QStringLiteral("mp4.strata")), "mp4.strata failed to parse");
    QVERIFY(StructureRenderEngine::validateStaticFieldReferences(&mp4Library).isEmpty());

    StrataLibrary riffLibrary;
    QVERIFY2(parseStandardDefinition(&riffLibrary, QStringLiteral("riff.strata")), "riff.strata failed to parse");
    QVERIFY(StructureRenderEngine::validateStaticFieldReferences(&riffLibrary).isEmpty());

    StrataLibrary tarLibrary;
    QVERIFY2(parseStandardDefinition(&tarLibrary, QStringLiteral("tar.strata")), "tar.strata failed to parse");
    QVERIFY(StructureRenderEngine::validateStaticFieldReferences(&tarLibrary).isEmpty());

    StrataLibrary gzipLibrary;
    QVERIFY2(parseStandardDefinition(&gzipLibrary, QStringLiteral("gzip.strata")), "gzip.strata failed to parse");
    QVERIFY(StructureRenderEngine::validateStaticFieldReferences(&gzipLibrary).isEmpty());

    StrataLibrary cabLibrary;
    QVERIFY2(parseStandardDefinition(&cabLibrary, QStringLiteral("cab.strata")), "cab.strata failed to parse");
    QVERIFY(StructureRenderEngine::validateStaticFieldReferences(&cabLibrary).isEmpty());

    StrataLibrary rawImgLibrary;
    QVERIFY2(parseStandardDefinition(&rawImgLibrary, QStringLiteral("rawimg.strata")), "rawimg.strata failed to parse");
    QVERIFY(StructureRenderEngine::validateStaticFieldReferences(&rawImgLibrary).isEmpty());

    StrataLibrary isoLibrary;
    QVERIFY2(parseStandardDefinition(&isoLibrary, QStringLiteral("iso.strata")), "iso.strata failed to parse");
    QVERIFY(StructureRenderEngine::validateStaticFieldReferences(&isoLibrary).isEmpty());

    StrataLibrary javaLibrary;
    QVERIFY2(parseStandardDefinition(&javaLibrary, QStringLiteral("java.strata")), "java.strata failed to parse");
    QVERIFY(StructureRenderEngine::validateStaticFieldReferences(&javaLibrary).isEmpty());

    StrataLibrary machoLibrary;
    QVERIFY2(parseStandardDefinition(&machoLibrary, QStringLiteral("macho.strata")), "macho.strata failed to parse");
    QVERIFY(StructureRenderEngine::validateStaticFieldReferences(&machoLibrary).isEmpty());
}

void StructViewRawRendererTests::definitionManagerFlagsRuntimeExpressionsInRootOffsets()
{
    StrataLibrary constantLibrary;
    Parser constantParser(&constantLibrary);
    QVERIFY(parseBuffer(constantParser,
                        "[export, offset(4 + 8)]\n"
                        "struct Root { byte value; } root;\n"));
    QVERIFY(StructureRenderEngine::validateStaticFieldReferences(&constantLibrary).isEmpty());

    StrataLibrary fileSizeLibrary;
    Parser fileSizeParser(&fileSizeLibrary);
    QVERIFY(parseBuffer(fileSizeParser,
                        "[export, offset(file_size() - 1)]\n"
                        "struct Root { byte value; } root;\n"));
    const QStringList fileSizeErrors = StructureRenderEngine::validateStaticFieldReferences(&fileSizeLibrary);
    QCOMPARE(fileSizeErrors.size(), 1);
    QVERIFY2(fileSizeErrors.first().contains(QStringLiteral("root offset(...)")), qPrintable(fileSizeErrors.first()));
    QVERIFY2(fileSizeErrors.first().contains(QStringLiteral("file_size(...)")), qPrintable(fileSizeErrors.first()));
    QVERIFY2(fileSizeErrors.first().contains(QStringLiteral("constant arithmetic")), qPrintable(fileSizeErrors.first()));

    StrataLibrary sizeofLibrary;
    Parser sizeofParser(&sizeofLibrary);
    QVERIFY(parseBuffer(sizeofParser,
                        "struct Header { byte value; };\n"
                        "[export, offset(sizeof(Header))]\n"
                        "struct Root { byte value; } root;\n"));
    const QStringList sizeofErrors = StructureRenderEngine::validateStaticFieldReferences(&sizeofLibrary);
    QCOMPARE(sizeofErrors.size(), 1);
    QVERIFY2(sizeofErrors.first().contains(QStringLiteral("sizeof(...)")), qPrintable(sizeofErrors.first()));
}

REGISTER_STRUCTVIEW_TEST(StructViewRawRendererTests)
#include "raw_renderer_tests.moc"
