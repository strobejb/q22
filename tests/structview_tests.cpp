#include "structview/structuredefinitionmanager.h"
#include "structview/structuresemanticview.h"
#include "structview/structuretreemodel.h"
#include "structview/structurevaluebuilder.h"

#include <QFile>
#include <QDir>
#include <QSignalSpy>
#include <QStringList>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <cstring>

class StructViewTests : public QObject
{
    Q_OBJECT

private slots:
    void managerCreatesUserStructsDirectory();
    void managerDiscoversBuiltinAndUserDefinitionFiles();
    void reloadSwapsInParsedTypeLibrary();
    void failedReloadPreservesPreviousLibrary();
    void exportedTypesUseExplicitExportTagsOnly();
    void exportedTypesExposeAssocExtensions();
    void builderFormatsScalarsAndEndian();
    void builderFormatsCharacterArraysAsStrings();
    void builderFormatsScalarArraysAsPreviewLists();
    void builderUsesSizeIsForUnsizedArrays();
    void builderEvaluatesTernaryExpressions();
    void builderUsesCommonUnionPrefixForSizeIs();
    void builderEvaluatesTernaryUnionMemberSizeAndOffset();
    void builderEvaluatesEndianAwareUnionMembers();
    void builderEvaluatesArrayIndexedUnionMembers();
    void builderUsesNameFieldForStructArrayElements();
    void builderAlignsFieldNamesWithinCompoundTypes();
    void builderBuildsNestedStructRowsAndOffsets();
    void builderSupportsArraysOffsetsEnumsAndSwitchCases();
    void builderEvaluatesUnionSwitchSelectorsFromTypedLayout();
    void builderEvaluatesFieldsAndCorrectedExpressions();
    void builderUsesDynamicEndianExpressions();
    void builderEvaluatesEnumIndexedArraysInExpressions();
    void builderEvaluatesEnumIndexedUnionMembersInExpressions();
    void builderRendersElf32AndElf64Tables();
    void builderPlacesDynamicStructsUnderNamedDynamicContainers();
    void semanticRegistryRunsKnownViewsAndIgnoresUnknownViews();
    void builderRunsSemanticViewsAfterDynamicPlacement();
    void builderKeepsRawDynamicRowsWhenSemanticImportDataIsTruncated();
    void builderAddsElfSectionAndSymbolSemanticRows();
    void builderKeepsRawElfRowsWhenSemanticDataIsTruncated();
    void modelHeadersMatchStructureGridColumns();
    void modelSupportsHierarchyAndEditableCells();
    void modelAppliesTypeDisplayOptionsWithoutResettingRows();
    void modelBuildsExpandableRowsForStructFields();
};

static void writeTextFile(const QString &path, const QByteArray &text)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write(text), qint64(text.size()));
}

static bool parseBuffer(Parser &parser, const char *text)
{
    parser.Init(text, strlen(text));
    return parser.Parse() != 0;
}

static TypeDecl *firstExported(TypeLibrary *library)
{
    if (!library)
        return nullptr;

    for (TypeDecl *decl : library->globalTypeDeclList)
        if (decl && FindTag(decl->tagList, TOK_EXPORT, nullptr))
            return decl;

    return nullptr;
}

static std::vector<std::unique_ptr<StructureRow>> buildRows(TypeLibrary *library,
                                                            TypeDecl *root,
                                                            const QByteArray &bytes,
                                                            uint64_t baseOffset = 0)
{
    StructureValueBuilder builder;
    return builder.build(library,
                         root,
                         baseOffset,
                         [&bytes](uint64_t offset, uint8_t *buffer, size_t length) -> size_t {
                             if (offset >= static_cast<uint64_t>(bytes.size()))
                                 return 0;

                             const size_t available = static_cast<size_t>(bytes.size() - static_cast<int>(offset));
                             const size_t copied = qMin(length, available);
                             memcpy(buffer, bytes.constData() + offset, copied);
                             return copied;
                         });
}

static bool parseStandardElfDefinition(TypeLibrary *library)
{
    if (!library)
        return false;

    Parser parser(library);
    const QString path = QDir(QStringLiteral(TYPELIB_TEST_DATA_DIR)).filePath(QStringLiteral("elf.txt"));
    return parser.Ooof(qPrintable(path));
}

static StructureRow *findChildNamed(StructureRow *parent, const QString &name)
{
    if (!parent)
        return nullptr;

    for (const auto &child : parent->children)
        if (child->name == name)
            return child.get();

    return nullptr;
}

static void verifyBranchIconsPresent(const StructureRow *row)
{
    QVERIFY(row != nullptr);
    QVERIFY(!row->branchIconPath.isEmpty());
    QVERIFY(!row->branchOpenIconPath.isEmpty());
    QVERIFY(!row->branchEmptyIconPath.isEmpty());
}

static void writeLe32(QByteArray *bytes, qsizetype offset, quint32 value)
{
    QVERIFY(bytes != nullptr);
    QVERIFY(offset >= 0);
    QVERIFY(offset + 4 <= bytes->size());
    (*bytes)[offset + 0] = char(value & 0xff);
    (*bytes)[offset + 1] = char((value >> 8) & 0xff);
    (*bytes)[offset + 2] = char((value >> 16) & 0xff);
    (*bytes)[offset + 3] = char((value >> 24) & 0xff);
}

static void writeLe16(QByteArray *bytes, qsizetype offset, quint16 value)
{
    QVERIFY(bytes != nullptr);
    QVERIFY(offset >= 0);
    QVERIFY(offset + 2 <= bytes->size());
    (*bytes)[offset + 0] = char(value & 0xff);
    (*bytes)[offset + 1] = char((value >> 8) & 0xff);
}

static void writeLe64(QByteArray *bytes, qsizetype offset, quint64 value)
{
    QVERIFY(bytes != nullptr);
    QVERIFY(offset >= 0);
    QVERIFY(offset + 8 <= bytes->size());
    for (int i = 0; i < 8; ++i)
        (*bytes)[offset + i] = char((value >> (i * 8)) & 0xff);
}

static void writeBe16(QByteArray *bytes, qsizetype offset, quint16 value)
{
    QVERIFY(bytes != nullptr);
    QVERIFY(offset >= 0);
    QVERIFY(offset + 2 <= bytes->size());
    (*bytes)[offset + 0] = char((value >> 8) & 0xff);
    (*bytes)[offset + 1] = char(value & 0xff);
}

static void writeBe32(QByteArray *bytes, qsizetype offset, quint32 value)
{
    QVERIFY(bytes != nullptr);
    QVERIFY(offset >= 0);
    QVERIFY(offset + 4 <= bytes->size());
    (*bytes)[offset + 0] = char((value >> 24) & 0xff);
    (*bytes)[offset + 1] = char((value >> 16) & 0xff);
    (*bytes)[offset + 2] = char((value >> 8) & 0xff);
    (*bytes)[offset + 3] = char(value & 0xff);
}

static void writeAscii(QByteArray *bytes, qsizetype offset, const char *text)
{
    QVERIFY(bytes != nullptr);
    QVERIFY(text != nullptr);
    const qsizetype length = qsizetype(strlen(text)) + 1;
    QVERIFY(offset >= 0);
    QVERIFY(offset + length <= bytes->size());
    memcpy(bytes->data() + offset, text, size_t(length));
}

void StructViewTests::managerCreatesUserStructsDirectory()
{
    // Scenario: the Structure View panel is opened on a fresh profile.
    // Expected: the user-editable structs directory is created lazily beside the
    // settings/palette area, so users have a stable place to drop definitions.
    // Regression guard: first-open loading must not fail just because the custom
    // definitions directory has not existed before.
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString userDir = temp.filePath(QStringLiteral("structs"));
    StructureDefinitionManager manager;
    manager.setBuiltinStructDirsForTests({});
    manager.setUserStructsDirForTests(userDir);

    QVERIFY2(manager.reload(), qPrintable(manager.lastError()));
    QVERIFY(QDir(userDir).exists());
}

void StructViewTests::managerDiscoversBuiltinAndUserDefinitionFiles()
{
    // Scenario: shipped definitions and user definitions are both available.
    // Expected: built-ins and user files are discovered, with the current .txt
    // extension and future .bstruct extension both accepted.
    // Regression guard: adding the user watched directory must not hide the
    // runtime definitions that ship with the app.
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString builtinDir = temp.filePath(QStringLiteral("typelib"));
    const QString userDir = temp.filePath(QStringLiteral("structs"));
    QVERIFY(QDir().mkpath(builtinDir));
    QVERIFY(QDir().mkpath(userDir));
    writeTextFile(QDir(builtinDir).filePath(QStringLiteral("builtin.txt")), "typedef dword BuiltinType;\n");
    writeTextFile(QDir(userDir).filePath(QStringLiteral("user.bstruct")), "typedef word UserType;\n");

    StructureDefinitionManager manager;
    manager.setBuiltinStructDirsForTests({ builtinDir });
    manager.setUserStructsDirForTests(userDir);

    QVERIFY2(manager.reload(), qPrintable(manager.lastError()));
    QCOMPARE(manager.definitionFiles().size(), 2);
}

void StructViewTests::reloadSwapsInParsedTypeLibrary()
{
    // Scenario: the watcher notices a valid definition set and triggers reload.
    // Expected: the manager publishes a fresh TypeLibrary containing parsed
    // declarations from that definition set.
    // Regression guard: reload must not merely rescan filenames while leaving
    // the old parser result in place.
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString userDir = temp.filePath(QStringLiteral("structs"));
    QVERIFY(QDir().mkpath(userDir));
    writeTextFile(QDir(userDir).filePath(QStringLiteral("first.txt")), "typedef dword FirstType;\n");

    StructureDefinitionManager manager;
    manager.setBuiltinStructDirsForTests({});
    manager.setUserStructsDirForTests(userDir);

    QVERIFY2(manager.reload(), qPrintable(manager.lastError()));
    QVERIFY(manager.library());
    QCOMPARE(manager.library()->globalTypeDeclList.size(), size_t(1));
}

void StructViewTests::failedReloadPreservesPreviousLibrary()
{
    // Scenario: a user saves a broken definition while the panel is open.
    // Expected: the error is reported, but the previous valid TypeLibrary stays
    // alive so the Structure View does not blank itself during an editing typo.
    // Regression guard: failed reloads must not replace good parse results with
    // an empty or half-built library.
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString userDir = temp.filePath(QStringLiteral("structs"));
    QVERIFY(QDir().mkpath(userDir));
    const QString filePath = QDir(userDir).filePath(QStringLiteral("types.txt"));
    writeTextFile(filePath, "typedef dword StableType;\n");

    StructureDefinitionManager manager;
    manager.setBuiltinStructDirsForTests({});
    manager.setUserStructsDirForTests(userDir);

    QVERIFY(manager.reload());
    TypeLibrary *stableLibrary = manager.library();
    QVERIFY(stableLibrary);
    QCOMPARE(stableLibrary->globalTypeDeclList.size(), size_t(1));

    writeTextFile(filePath, "this is not valid TypeLib syntax\n");
    QVERIFY(!manager.reload());
    QCOMPARE(manager.library(), stableLibrary);
    QCOMPARE(manager.library()->globalTypeDeclList.size(), size_t(1));
    QVERIFY(!manager.lastError().isEmpty());
}

void StructViewTests::exportedTypesUseExplicitExportTagsOnly()
{
    // Scenario: a definition file contains many parseable declarations, but only
    // one is marked as user-facing with the TypeLib [export] tag.
    // Expected: the Structure View root selector lists only the tagged type.
    // Regression guard: Parser::exported remains intentionally permissive for
    // round-tripping, so the UI must filter on the real TOK_EXPORT tag instead.
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString userDir = temp.filePath(QStringLiteral("structs"));
    QVERIFY(QDir().mkpath(userDir));
    writeTextFile(QDir(userDir).filePath(QStringLiteral("types.txt")),
                  "[export]\n"
                  "struct ExportedRoot { dword magic; } exportedRoot;\n"
                  "struct HiddenHelper { word flags; } hiddenHelper;\n");

    StructureDefinitionManager manager;
    manager.setBuiltinStructDirsForTests({});
    manager.setUserStructsDirForTests(userDir);

    QVERIFY2(manager.reload(), qPrintable(manager.lastError()));
    const QList<ExportedStructureType> exported = manager.exportedTypes();
    QCOMPARE(exported.size(), 1);
    QVERIFY(exported[0].typeDecl != nullptr);
}

void StructViewTests::exportedTypesExposeAssocExtensions()
{
    // Scenario: an exported TypeLib declaration declares file associations.
    // Expected: the Structure View loader exposes normalized lowercase suffixes
    // so the panel can auto-select the matching root type for the current file.
    // Regression guard: PE/ELF-style definitions should not require the user to
    // manually pick the root structure every time the panel reloads.
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString userDir = temp.filePath(QStringLiteral("structs"));
    QVERIFY(QDir().mkpath(userDir));
    writeTextFile(QDir(userDir).filePath(QStringLiteral("types.txt")),
                  "[export, assoc(\".EXE\", \"dll\")]\n"
                  "struct PeRoot { byte magic; } pe;\n");

    StructureDefinitionManager manager;
    manager.setBuiltinStructDirsForTests({});
    manager.setUserStructsDirForTests(userDir);

    QVERIFY2(manager.reload(), qPrintable(manager.lastError()));
    const QList<ExportedStructureType> exported = manager.exportedTypes();
    QCOMPARE(exported.size(), 1);
    QCOMPARE(exported[0].assocExtensions, QStringList({ QStringLiteral(".exe"), QStringLiteral(".dll") }));
}

void StructViewTests::builderFormatsScalarsAndEndian()
{
    // Scenario: a selected exported root contains ordinary scalar fields and a
    // declaration-level endian tag.
    // Expected: values are read from the supplied byte reader, little endian is
    // the default, and [endian("big")] only changes the tagged declaration.
    // Regression guard: the Structure View grid must show file data, not just
    // the parsed type outline.
    TypeLibrary library;
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

void StructViewTests::builderFormatsCharacterArraysAsStrings()
{
    // Scenario: a structure contains fixed-size char and wchar_t buffers.
    // Expected: the array rows still expand into elements, but the parent value
    // gives the useful quoted string preview instead of a generic {...}.
    // Regression guard: strings are a common binary-structure case and should be
    // readable without expanding every character cell.
    TypeLibrary library;
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

void StructViewTests::builderFormatsScalarArraysAsPreviewLists()
{
    // Scenario: a structure contains ordinary scalar arrays rather than strings.
    // Expected: the parent array row remains expandable, but its value previews
    // the first scalar elements and adds an ellipsis when the array is longer.
    // Regression guard: scalar arrays should be quickly readable without opening
    // every child row, while char/wchar arrays keep their string-specific path.
    TypeLibrary library;
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

void StructViewTests::builderUsesSizeIsForUnsizedArrays()
{
    // Scenario: a file format stores an array count in an earlier field, and
    // the TypeLib declaration uses [] plus [size_is(...)] rather than a fixed
    // declarator bound.
    // Expected: the parser accepts the unsized array syntax and the renderer
    // expands exactly the count read from the already-rendered structure data.
    // Regression guard: PE section headers must not be capped by a placeholder
    // array size in pe.txt just because the count is data-driven.
    TypeLibrary library;
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

void StructViewTests::builderEvaluatesTernaryExpressions()
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
        auto library = std::make_unique<TypeLibrary>();
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

void StructViewTests::builderUsesCommonUnionPrefixForSizeIs()
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
        auto library = std::make_unique<TypeLibrary>();
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

void StructViewTests::builderEvaluatesTernaryUnionMemberSizeAndOffset()
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
        auto library = std::make_unique<TypeLibrary>();
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

void StructViewTests::builderEvaluatesEndianAwareUnionMembers()
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
        auto library = std::make_unique<TypeLibrary>();
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

void StructViewTests::builderEvaluatesArrayIndexedUnionMembers()
{
    // Scenario: a render expression reaches through a selected union member and
    // indexes into an array field inside that member.
    // Expected: field resolution applies the array element offset before reading
    // the final field value.
    // Regression guard: offset/size/name expressions often grow from simple
    // fields into paths like header64.entries[1].count as format support matures.
    TypeLibrary library;
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

void StructViewTests::builderUsesNameFieldForStructArrayElements()
{
    // Scenario: a PE-style array contains structured elements whose meaningful
    // label lives inside each element rather than in the array index.
    // Expected: [name(Name)] keeps the array expandable but appends the rendered
    // child field value to each element row, giving labels like "[0].text".
    // Regression guard: name tags used to work only for enum-indexed arrays, so
    // section headers could not surface their embedded Name field in the grid.
    TypeLibrary library;
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

void StructViewTests::builderAlignsFieldNamesWithinCompoundTypes()
{
    // Scenario: sibling fields have type prefixes with different display widths,
    // such as "dword" followed by "signed word".
    // Expected: the model keeps normal display text but exposes split name
    // pieces, letting the delegate align identifiers with font metrics.
    // Regression guard: visual alignment must not be faked with spaces because
    // proportional UI fonts make character-count padding visibly wrong.
    TypeLibrary library;
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

void StructViewTests::builderBuildsNestedStructRowsAndOffsets()
{
    // Scenario: a root structure contains a nested structure value.
    // Expected: the nested row is expandable, child offsets advance inside it,
    // and offsets are displayed as zero-padded absolute hex addresses.
    // Regression guard: recursive rendering must not collapse nested structs
    // into a flat definition list or lose byte positions.
    TypeLibrary library;
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

void StructViewTests::builderSupportsArraysOffsetsEnumsAndSwitchCases()
{
    // Scenario: TypeLib tags drive the visual interpretation: an offset jumps to
    // a later byte, enum values display labels, arrays use evaluated counts, and
    // a switch_is union chooses the matching case.
    // Expected: each of those legacy-core tags affects only the relevant rows.
    // Regression guard: the new engine must preserve the useful old TypeView
    // behaviour without keeping the Win32 grid dependency.
    TypeLibrary library;
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

void StructViewTests::builderEvaluatesUnionSwitchSelectorsFromTypedLayout()
{
    // Scenario: a union selector references a field inside one possible union
    // member before that member has been rendered as a row.
    // Expected: switch_is can still read the selector from the typed layout at
    // the union offset, then render only the matching case.
    // Regression guard: PE uses ntHeaders32.OptionalHeader.Magic this way, so
    // row-context-only evaluation cannot decide the union case.
    TypeLibrary library;
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

void StructViewTests::builderEvaluatesFieldsAndCorrectedExpressions()
{
    // Scenario: an array bound references a field already rendered in the
    // current row.
    // Expected: field lookup reads from the row context rather than from UI text
    // or a process-global grid item.
    // Regression guard: expression evaluation is the most important separation
    // point between TypeLib syntax and file-data rendering.
    TypeLibrary library;
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

void StructViewTests::builderUsesDynamicEndianExpressions()
{
    // Scenario: a format stores byte order in the file header, and the exported
    // root TypeDecl uses endian(expr) to select how later numeric fields read.
    // Expected: the same definition renders big-endian and little-endian inputs
    // differently, and expression reads inherit that byte order too.
    // Regression guard: ELF must not require hard-coded C++ endian knowledge for
    // ordinary raw fields declared in TypeLib.
    const char *definition =
        "[export, endian(marker == 2)]\n"
        "struct Root { byte marker; word value; dword count; byte items[count]; } root;\n";

    auto render = [definition](const QByteArray &bytes) {
        auto library = std::make_unique<TypeLibrary>();
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

void StructViewTests::builderEvaluatesEnumIndexedArraysInExpressions()
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
        auto library = std::make_unique<TypeLibrary>();
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

void StructViewTests::builderEvaluatesEnumIndexedUnionMembersInExpressions()
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
        auto library = std::make_unique<TypeLibrary>();
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

void StructViewTests::builderRendersElf32AndElf64Tables()
{
    // Scenario: ELF stores the 32/64-bit layout choice in e_ident[EI_CLASS].
    // Expected: TypeLib switch_is selects the matching header branch, and the
    // program/section table arrays use offsets and counts from that branch.
    // Regression guard: ELF support must not assume PE-like fixed offsets or a
    // single word size.
    TypeLibrary library32;
    QVERIFY2(parseStandardElfDefinition(&library32), "elf.txt failed to parse");
    QByteArray elf32(0x180, '\0');
    elf32[0] = char(0x7f);
    elf32[1] = 'E';
    elf32[2] = 'L';
    elf32[3] = 'F';
    elf32[4] = char(1);
    elf32[5] = char(1);
    elf32[6] = char(1);
    writeLe16(&elf32, 16, 2);
    writeLe16(&elf32, 18, 3);
    writeLe32(&elf32, 20, 1);
    writeLe32(&elf32, 24, 0x12345678);
    writeLe32(&elf32, 28, 0x80);
    writeLe32(&elf32, 32, 0x100);
    writeLe16(&elf32, 42, 32);
    writeLe16(&elf32, 44, 1);
    writeLe16(&elf32, 46, 40);
    writeLe16(&elf32, 48, 2);

    auto rows32 = buildRows(&library32, firstExported(&library32), elf32);
    QCOMPARE(rows32.size(), size_t(1));
    QStringList childNames32;
    for (const auto &child : rows32[0]->children)
        childNames32.push_back(child->name);
    const QByteArray childNames32Message = childNames32.join(QStringLiteral(", ")).toLocal8Bit();
    StructureRow *header32 = findChildNamed(rows32[0].get(), QStringLiteral("Elf32_EhdrTail header32"));
    QVERIFY2(header32, childNames32Message.constData());
    QCOMPARE(header32->children[9]->value, QStringLiteral("1"));
    QVERIFY2(findChildNamed(rows32[0].get(), QStringLiteral("Elf32_Phdr programHeaders32[]")),
             childNames32Message.constData());
    StructureRow *sections32 = findChildNamed(rows32[0].get(), QStringLiteral("Elf32_Shdr sectionHeaders32[]"));
    QVERIFY2(sections32, childNames32Message.constData());
    QCOMPARE(sections32->children.size(), size_t(2));
    QCOMPARE(header32->children[3]->value, QStringLiteral("305419896"));

    TypeLibrary library32be;
    QVERIFY2(parseStandardElfDefinition(&library32be), "elf.txt failed to parse");
    QByteArray elf32be(0x180, '\0');
    elf32be[0] = char(0x7f);
    elf32be[1] = 'E';
    elf32be[2] = 'L';
    elf32be[3] = 'F';
    elf32be[4] = char(1);
    elf32be[5] = char(2);
    elf32be[6] = char(1);
    writeBe16(&elf32be, 16, 2);
    writeBe16(&elf32be, 18, 3);
    writeBe32(&elf32be, 20, 1);
    writeBe32(&elf32be, 24, 0x01020304);
    writeBe32(&elf32be, 28, 0x80);
    writeBe32(&elf32be, 32, 0x100);
    writeBe16(&elf32be, 42, 32);
    writeBe16(&elf32be, 44, 1);
    writeBe16(&elf32be, 46, 40);
    writeBe16(&elf32be, 48, 1);

    auto rows32be = buildRows(&library32be, firstExported(&library32be), elf32be);
    QCOMPARE(rows32be.size(), size_t(1));
    StructureRow *header32be = findChildNamed(rows32be[0].get(), QStringLiteral("Elf32_EhdrTail header32"));
    QVERIFY(header32be);
    QCOMPARE(header32be->children[3]->value, QStringLiteral("16909060"));

    TypeLibrary library64;
    QVERIFY2(parseStandardElfDefinition(&library64), "elf.txt failed to parse");
    QByteArray elf64(0x180, '\0');
    elf64[0] = char(0x7f);
    elf64[1] = 'E';
    elf64[2] = 'L';
    elf64[3] = 'F';
    elf64[4] = char(2);
    elf64[5] = char(1);
    elf64[6] = char(1);
    writeLe16(&elf64, 16, 3);
    writeLe16(&elf64, 18, 62);
    writeLe32(&elf64, 20, 1);
    writeLe64(&elf64, 24, 0x1122334455667788ull);
    writeLe64(&elf64, 32, 0x80);
    writeLe64(&elf64, 40, 0x100);
    writeLe16(&elf64, 54, 56);
    writeLe16(&elf64, 56, 1);
    writeLe16(&elf64, 58, 64);
    writeLe16(&elf64, 60, 1);

    auto rows64 = buildRows(&library64, firstExported(&library64), elf64);
    QCOMPARE(rows64.size(), size_t(1));
    QVERIFY(findChildNamed(rows64[0].get(), QStringLiteral("Elf64_EhdrTail header64")));
    QVERIFY(findChildNamed(rows64[0].get(), QStringLiteral("Elf64_Phdr programHeaders64[]")));
    StructureRow *sections64 = findChildNamed(rows64[0].get(), QStringLiteral("Elf64_Shdr sectionHeaders64[]"));
    QVERIFY(sections64);
    QCOMPARE(sections64->children.size(), size_t(1));
}

void StructViewTests::builderPlacesDynamicStructsUnderNamedDynamicContainers()
{
    // Scenario: a PE-style data-directory entry names a logical RVA, while a
    // section-header array declares named dynamic containers and RVA mapping.
    // Expected: SECTION rows are rendered at the root using [name] aliases, and
    // the dynamic structure appears under the SECTION whose range contains it.
    // Regression guard: optional PE structures must be declared in TypeLib data,
    // not hard-coded into the Structure View renderer.
    TypeLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "enum Dir { Export = 0, Import = 1 };\n"
                        "typedef struct _DataDir { dword VirtualAddress; dword Size; } DataDir;\n"
                        "typedef struct _Section { char Name[8]; dword VirtualAddress; dword SizeOfRawData; dword PointerToRawData; } Section;\n"
                        "typedef struct _SectionBucket { } SECTION;\n"
                        "typedef struct _ImportDesc { dword thunk; } ImportDesc;\n"
                        "typedef struct _ExportDir { dword flags; } ExportDir;\n"
                        "[export]\n"
                        "struct Root {\n"
                        "  [dynamic_struct(Export, ExportDir, VirtualAddress, Size != 0), dynamic_struct(Import, ImportDesc, VirtualAddress, Size != 0)] DataDir dirs[2];\n"
                        "  [name(Name), dynamic_container(SECTION), offset_map(VirtualAddress, SizeOfRawData, PointerToRawData)] Section sections[2];\n"
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
    verifyBranchIconsPresent(rows[0]->children[2].get());
    QCOMPARE(rows[0]->children[2]->children.size(), size_t(0));
    QCOMPARE(rows[0]->children[3]->name, QStringLiteral("SECTION .idata"));
    verifyBranchIconsPresent(rows[0]->children[3].get());
    QCOMPARE(rows[0]->children[3]->offset, QStringLiteral("00000080"));

    StructureRow *dynamicImport = rows[0]->children[3]->children[0].get();
    QCOMPARE(dynamicImport->name, QStringLiteral("ImportDesc"));
    QCOMPARE(dynamicImport->offset, QStringLiteral("00000080"));
    QCOMPARE(static_cast<int>(rows[0]->children[3]->kind), static_cast<int>(StructureRowKind::Dynamic));
    QCOMPARE(static_cast<int>(dynamicImport->kind), static_cast<int>(StructureRowKind::Dynamic));
    verifyBranchIconsPresent(dynamicImport);
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
    QVERIFY(model.hasChildren(emptySectionIndex));
    QCOMPARE(model.rowCount(emptySectionIndex), 0);
    QVERIFY(!(model.flags(sectionIndex) & Qt::ItemIsEditable));
    QVERIFY(!(model.flags(dynamicIndex) & Qt::ItemIsEditable));
}

void StructViewTests::semanticRegistryRunsKnownViewsAndIgnoresUnknownViews()
{
    // Scenario: semantic rendering is an optional interpreter layer selected by
    // string ids in TypeLib definitions.
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

void StructViewTests::builderRunsSemanticViewsAfterDynamicPlacement()
{
    // Scenario: a PE import directory is declared as a dynamic structure and
    // marked with view("pe.imports").
    // Expected: raw IMAGE_IMPORT_DESCRIPTOR fields stay visible, then semantic
    // DLL/function rows are appended beneath the dynamically placed import row.
    // Regression guard: PE knowledge must augment dynamic rows after RVA mapping
    // has found the containing section, not replace the raw TypeLib rendering.
    TypeLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "enum Dir { Import = 0 };\n"
                        "typedef struct _DataDir { dword VirtualAddress; dword Size; } DataDir;\n"
                        "typedef struct _Section { char Name[8]; dword VirtualAddress; dword SizeOfRawData; dword PointerToRawData; } Section;\n"
                        "typedef struct _SectionBucket { } SECTION;\n"
                        "[view(\"pe.imports\")]\n"
                        "typedef struct _ImportDesc { dword OriginalFirstThunk; dword TimeDateStamp; dword ForwarderChain; dword Name; dword FirstThunk; } ImportDesc;\n"
                        "[export]\n"
                        "struct Root {\n"
                        "  [dynamic_struct(Import, ImportDesc, VirtualAddress, Size != 0)] DataDir dirs[1];\n"
                        "  [name(Name), dynamic_container(SECTION), offset_map(VirtualAddress, SizeOfRawData, PointerToRawData)] Section sections[1];\n"
                        "} root;\n"));

    QByteArray bytes(0x140, '\0');
    writeLe32(&bytes, 0, 0x1200);
    writeLe32(&bytes, 4, 0x80);
    writeAscii(&bytes, 8, ".idata");
    writeLe32(&bytes, 16, 0x1200);
    writeLe32(&bytes, 20, 0x100);
    writeLe32(&bytes, 24, 0x80);
    writeLe32(&bytes, 0x80, 0x1240);
    writeLe32(&bytes, 0x8c, 0x1260);
    writeLe32(&bytes, 0x90, 0x1240);
    writeLe32(&bytes, 0xc0, 0x1270);
    writeAscii(&bytes, 0xe0, "KERNEL32.dll");
    writeLe16(&bytes, 0xf0, 0x1234);
    writeAscii(&bytes, 0xf2, "CreateFileW");

    auto rows = buildRows(&library, firstExported(&library), bytes);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->children.size(), size_t(3));
    QCOMPARE(rows[0]->children[2]->name, QStringLiteral("SECTION .idata"));

    StructureRow *dynamicImport = rows[0]->children[2]->children[0].get();
    QCOMPARE(dynamicImport->name, QStringLiteral("ImportDesc"));
    QCOMPARE(dynamicImport->children.size(), size_t(6));
    QCOMPARE(dynamicImport->children[0]->name, QStringLiteral("dword OriginalFirstThunk"));
    QCOMPARE(static_cast<int>(dynamicImport->children[0]->kind), static_cast<int>(StructureRowKind::Raw));

    StructureRow *dllRow = dynamicImport->children[5].get();
    QCOMPARE(static_cast<int>(dllRow->kind), static_cast<int>(StructureRowKind::Semantic));
    QCOMPARE(dllRow->name, QStringLiteral("KERNEL32.dll"));
    verifyBranchIconsPresent(dllRow);
    QCOMPARE(dllRow->children.size(), size_t(1));
    QCOMPARE(static_cast<int>(dllRow->children[0]->kind), static_cast<int>(StructureRowKind::Semantic));
    QCOMPARE(dllRow->children[0]->name, QStringLiteral("Import CreateFileW"));
    QCOMPARE(dllRow->children[0]->value, QStringLiteral("hint 4660"));

    std::vector<std::unique_ptr<StructureRow>> modelRows;
    modelRows.push_back(std::move(rows[0]));
    StructureTreeModel model;
    model.setRowsForTests(std::move(modelRows));
    const QModelIndex rootIndex = model.index(0, StructureTreeModel::NameColumn);
    const QModelIndex sectionIndex = model.index(2, StructureTreeModel::NameColumn, rootIndex);
    const QModelIndex importIndex = model.index(0, StructureTreeModel::NameColumn, sectionIndex);
    const QModelIndex dllIndex = model.index(5, StructureTreeModel::NameColumn, importIndex);
    QVERIFY(dllIndex.isValid());
    QVERIFY(!(model.flags(dllIndex) & Qt::ItemIsEditable));
}

void StructViewTests::builderKeepsRawDynamicRowsWhenSemanticImportDataIsTruncated()
{
    // Scenario: a PE import directory row is present, but the imported DLL/name
    // tables are incomplete or outside the mapped bytes.
    // Expected: semantic interpretation stops quietly while the raw dynamic
    // IMAGE_IMPORT_DESCRIPTOR row and its fields remain available.
    // Regression guard: educational views must never make the base structure
    // renderer brittle when a file is malformed or partially loaded.
    TypeLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "enum Dir { Import = 0 };\n"
                        "typedef struct _DataDir { dword VirtualAddress; dword Size; } DataDir;\n"
                        "typedef struct _Section { char Name[8]; dword VirtualAddress; dword SizeOfRawData; dword PointerToRawData; } Section;\n"
                        "typedef struct _SectionBucket { } SECTION;\n"
                        "[view(\"pe.imports\")]\n"
                        "typedef struct _ImportDesc { dword OriginalFirstThunk; dword TimeDateStamp; dword ForwarderChain; dword Name; dword FirstThunk; } ImportDesc;\n"
                        "[export]\n"
                        "struct Root {\n"
                        "  [dynamic_struct(Import, ImportDesc, VirtualAddress, Size != 0)] DataDir dirs[1];\n"
                        "  [name(Name), dynamic_container(SECTION), offset_map(VirtualAddress, SizeOfRawData, PointerToRawData)] Section sections[1];\n"
                        "} root;\n"));

    QByteArray bytes(0xa0, '\0');
    writeLe32(&bytes, 0, 0x1200);
    writeLe32(&bytes, 4, 0x80);
    writeAscii(&bytes, 8, ".idata");
    writeLe32(&bytes, 16, 0x1200);
    writeLe32(&bytes, 20, 0x100);
    writeLe32(&bytes, 24, 0x80);
    writeLe32(&bytes, 0x80, 0x1240);
    writeLe32(&bytes, 0x8c, 0x1260);
    writeLe32(&bytes, 0x90, 0x1240);

    auto rows = buildRows(&library, firstExported(&library), bytes);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->children.size(), size_t(3));
    QVERIFY(!rows[0]->children[2]->children.empty());

    StructureRow *dynamicImport = rows[0]->children[2]->children[0].get();
    QCOMPARE(dynamicImport->name, QStringLiteral("ImportDesc"));
    QVERIFY(dynamicImport->children.size() >= size_t(5));
    QCOMPARE(dynamicImport->children[0]->name, QStringLiteral("dword OriginalFirstThunk"));
    QCOMPARE(dynamicImport->children[0]->value, QStringLiteral("4672"));
    QCOMPARE(static_cast<int>(dynamicImport->children[0]->kind), static_cast<int>(StructureRowKind::Raw));
}

void StructViewTests::builderAddsElfSectionAndSymbolSemanticRows()
{
    // Scenario: an ELF file has a section-header string table plus a symbol
    // table linked to its own string table.
    // Expected: the raw TypeLib tables remain visible, and the semantic pass
    // appends named SECTION rows with resolved SYMBOL children.
    // Regression guard: ELF domain knowledge belongs in elfsemanticview.cpp,
    // augmenting the declarative structs rather than replacing them.
    TypeLibrary library;
    QVERIFY2(parseStandardElfDefinition(&library), "elf.txt failed to parse");

    QByteArray bytes(0x320, '\0');
    bytes[0] = char(0x7f);
    bytes[1] = 'E';
    bytes[2] = 'L';
    bytes[3] = 'F';
    bytes[4] = char(1);
    bytes[5] = char(1);
    bytes[6] = char(1);
    writeLe16(&bytes, 16, 2);
    writeLe16(&bytes, 18, 3);
    writeLe32(&bytes, 20, 1);
    writeLe32(&bytes, 32, 0x100);
    writeLe16(&bytes, 46, 40);
    writeLe16(&bytes, 48, 5);
    writeLe16(&bytes, 50, 4);

    auto writeSection = [&bytes](qsizetype index,
                                 quint32 name,
                                 quint32 type,
                                 quint32 offset,
                                 quint32 size,
                                 quint32 link,
                                 quint32 entrySize) {
        const qsizetype base = 0x100 + index * 40;
        writeLe32(&bytes, base + 0, name);
        writeLe32(&bytes, base + 4, type);
        writeLe32(&bytes, base + 16, offset);
        writeLe32(&bytes, base + 20, size);
        writeLe32(&bytes, base + 24, link);
        writeLe32(&bytes, base + 36, entrySize);
    };

    const QByteArray shstr("\0.text\0.symtab\0.strtab\0.shstrtab\0", 33);
    memcpy(bytes.data() + 0x280, shstr.constData(), size_t(shstr.size()));
    const QByteArray strtab("\0main\0", 6);
    memcpy(bytes.data() + 0x260, strtab.constData(), size_t(strtab.size()));

    writeSection(1, 1, 1, 0x200, 4, 0, 0);
    writeSection(2, 7, 2, 0x220, 32, 3, 16);
    writeSection(3, 15, 3, 0x260, 6, 0, 0);
    writeSection(4, 23, 3, 0x280, quint32(shstr.size()), 0, 0);

    writeLe32(&bytes, 0x220 + 16, 1);
    writeLe32(&bytes, 0x220 + 20, 0x1000);
    writeLe32(&bytes, 0x220 + 24, 4);
    bytes[0x220 + 28] = char(0x12);
    writeLe16(&bytes, 0x220 + 30, 1);

    auto rows = buildRows(&library, firstExported(&library), bytes);
    QCOMPARE(rows.size(), size_t(1));
    QVERIFY(findChildNamed(rows[0].get(), QStringLiteral("Elf32_Shdr sectionHeaders32[]")));

    StructureRow *text = findChildNamed(rows[0].get(), QStringLiteral("SECTION .text"));
    QVERIFY(text);
    QCOMPARE(text->offset, QStringLiteral("00000200"));
    QCOMPARE(text->byteLength, uint64_t(4));

    StructureRow *symtab = findChildNamed(rows[0].get(), QStringLiteral("SECTION .symtab"));
    QVERIFY(symtab);
    StructureRow *symbol = findChildNamed(symtab, QStringLiteral("SYMBOL main"));
    QVERIFY(symbol);
    QCOMPARE(symbol->value, QStringLiteral("value 0x1000, size 4"));
}

void StructViewTests::builderKeepsRawElfRowsWhenSemanticDataIsTruncated()
{
    // Scenario: an ELF header points at a section table, but the string table
    // data needed by the educational semantic pass is missing.
    // Expected: semantic rows are skipped quietly while raw header/table rows
    // from TypeLib remain available.
    // Regression guard: malformed ELF files must not make Structure View blank
    // or fail just because name resolution cannot complete.
    TypeLibrary library;
    QVERIFY2(parseStandardElfDefinition(&library), "elf.txt failed to parse");

    QByteArray bytes(0x160, '\0');
    bytes[0] = char(0x7f);
    bytes[1] = 'E';
    bytes[2] = 'L';
    bytes[3] = 'F';
    bytes[4] = char(1);
    bytes[5] = char(1);
    bytes[6] = char(1);
    writeLe16(&bytes, 16, 2);
    writeLe16(&bytes, 18, 3);
    writeLe32(&bytes, 20, 1);
    writeLe32(&bytes, 32, 0x100);
    writeLe16(&bytes, 46, 40);
    writeLe16(&bytes, 48, 2);
    writeLe16(&bytes, 50, 1);
    writeLe32(&bytes, 0x100 + 40 + 0, 1);
    writeLe32(&bytes, 0x100 + 40 + 4, 3);
    writeLe32(&bytes, 0x100 + 40 + 16, 0x300);
    writeLe32(&bytes, 0x100 + 40 + 20, 0x20);

    auto rows = buildRows(&library, firstExported(&library), bytes);
    QCOMPARE(rows.size(), size_t(1));
    QVERIFY(findChildNamed(rows[0].get(), QStringLiteral("Elf32_EhdrTail header32")));
    QVERIFY(findChildNamed(rows[0].get(), QStringLiteral("Elf32_Shdr sectionHeaders32[]")));
    QVERIFY(!findChildNamed(rows[0].get(), QStringLiteral("SECTION .text")));
}

void StructViewTests::modelHeadersMatchStructureGridColumns()
{
    // Scenario: the view is rendered as a real tree-grid with stable columns.
    // Expected: headers use the exact user-facing labels from the UI plan.
    // Regression guard: later model work must not reorder or rename columns
    // underneath the delegate/header styling.
    StructureTreeModel model;

    QCOMPARE(model.columnCount(), int(StructureTreeModel::ColumnCount));
    QCOMPARE(model.headerData(StructureTreeModel::NameColumn, Qt::Horizontal).toString(), QStringLiteral("Name"));
    QCOMPARE(model.headerData(StructureTreeModel::ValueColumn, Qt::Horizontal).toString(), QStringLiteral("Value"));
    QCOMPARE(model.headerData(StructureTreeModel::OffsetColumn, Qt::Horizontal).toString(), QStringLiteral("Offset"));
    QCOMPARE(model.headerData(StructureTreeModel::CommentColumn, Qt::Horizontal).toString(), QStringLiteral("Comment"));
}

void StructViewTests::modelSupportsHierarchyAndEditableCells()
{
    // Scenario: a structured type expands into nested fields and the user edits
    // a visible cell through the tree-grid delegate.
    // Expected: parent/child indexes are stable, and editing updates only the
    // model text for that cell in this first UI slice.
    // Regression guard: full-row selection must not imply a read-only list; cell
    // editing is part of the Structure View contract even before byte write-back.
    auto parent = std::make_unique<StructureRow>();
    parent->name = QStringLiteral("struct PE");
    parent->value = QStringLiteral("{...}");
    parent->offset = QStringLiteral("00000000");

    auto child = std::make_unique<StructureRow>(parent.get());
    child->name = QStringLiteral("word Machine");
    child->value = QStringLiteral("0x8664");
    child->offset = QStringLiteral("00000004");
    parent->children.push_back(std::move(child));

    std::vector<std::unique_ptr<StructureRow>> rows;
    rows.push_back(std::move(parent));

    StructureTreeModel model;
    model.setRowsForTests(std::move(rows));

    const QModelIndex parentIndex = model.index(0, StructureTreeModel::NameColumn);
    QVERIFY(parentIndex.isValid());
    QCOMPARE(model.rowCount(parentIndex), 1);

    const QModelIndex childValue = model.index(0, StructureTreeModel::ValueColumn, parentIndex);
    QVERIFY(childValue.isValid());
    QVERIFY(model.flags(childValue) & Qt::ItemIsEditable);
    QVERIFY(model.setData(childValue, QStringLiteral("0x14C")));
    QCOMPARE(model.data(childValue).toString(), QStringLiteral("0x14C"));
}

void StructViewTests::modelAppliesTypeDisplayOptionsWithoutResettingRows()
{
    // Scenario: the user right-clicks Structure View and changes only the type
    // display lens, for example from TypeLib aliases to primitive storage types.
    // Expected: visible name text updates in place, preserving the existing row
    // objects so expansion, scroll position, and selection do not jump.
    // Regression guard: the first implementation rebuilt the whole tree for a
    // cosmetic setting, causing expanded structures to collapse unnecessarily.
    Parser parser;
    QVERIFY(parseBuffer(parser,
                        "typedef dword e32_addr;\n"
                        "[export]\n"
                        "struct Root { byte pad[4]; e32_addr entry; } root;\n"));
    TypeDecl *root = firstExported(parser.GetTypeLibrary());
    QVERIFY(root != nullptr);

    auto rows = buildRows(parser.GetTypeLibrary(), root, QByteArray(32, '\0'), 0x10);
    StructureTreeModel model;
    model.setRowsForTests(std::move(rows));

    const QModelIndex rootIndex = model.index(0, StructureTreeModel::NameColumn);
    const QModelIndex fieldIndex = model.index(1, StructureTreeModel::NameColumn, rootIndex);
    const QModelIndex fieldOffset = model.index(1, StructureTreeModel::OffsetColumn, rootIndex);
    QVERIFY(fieldIndex.isValid());
    QVERIFY(fieldOffset.isValid());
    StructureRow *fieldRow = model.rowForIndex(fieldIndex);
    QCOMPARE(model.data(fieldIndex).toString(), QStringLiteral("e32_addr entry"));
    QCOMPARE(model.data(fieldOffset).toString(), QStringLiteral("00000014"));

    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
    QSignalSpy changedSpy(&model, &QAbstractItemModel::dataChanged);
    StructureDisplayOptions options;
    options.typeNameMode = StructureTypeNameMode::Storage;
    options.hexadecimalOffsets = false;
    model.applyDisplayOptions(options);

    QCOMPARE(resetSpy.count(), 0);
    QVERIFY(changedSpy.count() > 0);
    QCOMPARE(model.rowForIndex(fieldIndex), fieldRow);
    QCOMPARE(model.data(fieldIndex).toString(), QStringLiteral("dword entry"));
    QCOMPARE(model.data(fieldOffset).toString(), QStringLiteral("20"));

    options.hexadecimalOffsets = true;
    options.relativeOffsets = true;
    model.applyDisplayOptions(options);
    QCOMPARE(model.rowForIndex(fieldIndex), fieldRow);
    QCOMPARE(model.data(fieldOffset).toString(), QStringLiteral("+0004"));

    options.hexadecimalOffsets = false;
    model.applyDisplayOptions(options);
    QCOMPARE(model.data(fieldOffset).toString(), QStringLiteral("+4"));
}

void StructViewTests::modelBuildsExpandableRowsForStructFields()
{
    // Scenario: an exported structure contains ordinary member declarations.
    // Expected: the model exposes those members as child rows, giving QTreeView
    // something real to expand with the disclosure indicator.
    // Regression guard: the first Structure View UI was tree-capable but flat,
    // so clicking exported structures did nothing.
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString userDir = temp.filePath(QStringLiteral("structs"));
    QVERIFY(QDir().mkpath(userDir));
    writeTextFile(QDir(userDir).filePath(QStringLiteral("types.txt")),
                  "[export]\n"
                  "struct Root { dword magic; word flags; } root;\n");

    StructureDefinitionManager manager;
    manager.setBuiltinStructDirsForTests({});
    manager.setUserStructsDirForTests(userDir);
    QVERIFY2(manager.reload(), qPrintable(manager.lastError()));

    QList<TypeDecl *> exportedDecls;
    for (const ExportedStructureType &type : manager.exportedTypes())
        exportedDecls.push_back(type.typeDecl);

    StructureTreeModel model;
    model.setTypeDecls(exportedDecls);

    const QModelIndex rootIndex = model.index(0, StructureTreeModel::NameColumn);
    QVERIFY(rootIndex.isValid());
    QCOMPARE(model.rowCount(rootIndex), 2);
    QCOMPARE(model.data(model.index(0, StructureTreeModel::NameColumn, rootIndex)).toString(),
             QStringLiteral("dword magic"));
}

QTEST_MAIN(StructViewTests)
#include "structview_tests.moc"
