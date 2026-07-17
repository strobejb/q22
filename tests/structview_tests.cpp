#include "structview/structuredefinitionmanager.h"
#include "structview/structurerenderengine.h"
#include "structview/structuresemanticview.h"
#include "structview/structuretreemodel.h"
#include "structview/structurevaluebuilder.h"

#include <QFile>
#include <QDir>
#include <QSignalSpy>
#include <QStringList>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <algorithm>
#include <cstring>
#include <memory>

class StructViewTests : public QObject
{
    Q_OBJECT

private slots:
    void managerCreatesUserStrataDirectory();
    void managerDiscoversBuiltinAndUserDefinitionFiles();
    void reloadSwapsInParsedStrataLibrary();
    void managerReportsChangedDefinitionsWithoutAutoReload();
    void brokenDefinitionFileIsReportedWithoutDroppingValidOnes();
    void fixingABrokenDefinitionFileRestoresItsTypes();
    void partiallyParsedFileDoesNotExposeStaleExportedType();
    void exportedTypesUseExplicitExportTagsOnly();
    void exportedTypesExposeAssocExtensions();
    void exportedTypesExposeMagicSignatures();
    void exportedTypesExposeDescriptions();
    void exportedTypesExposeCategories();
    void exportedTypesResolveDuplicateVersionsAndLogDecision();
    void userDefinitionOverridesBuiltinWithSameBaseName();
    void brokenUserDefinitionOverrideBlocksBuiltinFallback();
    void builderFormatsScalarsAndEndian();
    void builderFormatsLeb128ScalarsAndAdvancesByEncodedLength();
    void builderUsesLeb128ValuesInExpressions();
    void builderRendersBitflagsAsExpandableRows();
    void builderFormatsCharacterArraysAsStrings();
    void builderFormatsTaggedByteArraysAsStrings();
    void builderFormatsGenericDisplayTags();
    void builderAppliesTreePresentationTags();
    void builderRendersRaggedStringTables();
    void builderFormatsScalarArraysAsPreviewLists();
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
    void builderEvaluatesUnionSwitchSelectorsFromTypedLayout();
    void builderEvaluatesFieldsAndCorrectedExpressions();
    void builderEvaluatesFindSearchExpressions();
    void builderEvaluatesOctalStringExpressions();
    void builderSelectsUnionMembersFromStringExpressions();
    void builderSelectsUnionMembersFromFourCcExpressions();
    void builderUsesDynamicEndianExpressions();
    void builderEvaluatesEnumIndexedArraysInExpressions();
    void builderEvaluatesEnumIndexedUnionMembersInExpressions();
    void builderUsesSimpleRootNamesForBuiltinTypedefRoots();
    void builderOptionallySortsTopLevelRowsByOffset();
    void builderRendersDexHeaderAndTables();
    void builderAddsDexSemanticSummaryPastArrayRenderCap();
    void builderRendersDtbHeaderAndBlocks();
    void builderRendersWasmHeaderAndSections();
    void builderRendersJavaClassConstantPool();
    void builderRendersMachOHeaderAndLoadCommands();
    void builderRendersSfntTableDirectory();
    void builderRendersPngChunks();
    void builderRendersBmpHeaderAndPixelPayload();
    void builderRendersIcoDirectoryAndImagePayload();
    void builderRendersGifBlocks();
    void builderRendersWoffDirectoryAndPayloads();
    void builderRendersMp4Boxes();
    void builderRendersRiffWaveAndWebpChunks();
    void builderRendersTarEntries();
    void builderRendersGzipHeaderAndTrailer();
    void builderRendersCabinetHeaderFilesAndData();
    void builderRendersRawImgMbrPartitions();
    void builderRendersIsoVolumeDescriptors();
    void builderRendersZipCentralDirectoryFromEocd();
    void builderRendersElf32AndElf64Tables();
    void builderPlacesDynamicStructsUnderNamedDynamicContainers();
    void builderPlacesDirectDynamicStructsUnderOwningRows();
    void builderRendersDynamicArraysAtReferencedOffsets();
    void builderStopsDynamicAndInlineArraysAtTerminators();
    void builderEmitsSemanticRowsUnderAttachedSchema();
    void builderEmitsAndMergesSemanticNodes();
    void builderEmitsSemanticRowsThroughNamedOffsetSpaces();
    void builderRejectsUnterminatedCstrSemanticNames();
    void builderEmitsIntoMappedSemanticContainers();
    void definitionManagerFlagsUnknownSemanticDestinations();
    void definitionManagerFlagsUnknownSemanticNodeFields();
    void builderRunsSemanticViewsOnceForDynamicArrayTables();
    void builderNamesPeDynamicSectionsFromStandardDefinition();
    void builderPeSemanticModeCppSkipsDeclarativeRows();
    void builderNamesPeImportDescriptorsFromStandardDefinition();
    void builderEmitsPeImportDllsFromStandardDefinition();
    void builderResolvesEntryPointRvaThroughSectionOffsetMap();
    void builderResolvesUnionDiscriminatorFromCandidateOnlyField();
    void definitionManagerFlagsNonStaticFieldReferences();
    void definitionManagerFlagsRuntimeExpressionsInRootOffsets();
    void semanticRegistryRunsKnownViewsAndIgnoresUnknownViews();
    void builderRunsSemanticViewsAfterDynamicPlacement();
    void builderPeSemanticModeDeclarativeSkipsCppViews();
    void builderKeepsRawDynamicRowsWhenSemanticImportDataIsTruncated();
    void semanticPeImportsWalksPe32PlusThunkTables();
    void semanticPeImportsRespectDynamicArrayDescriptorCount();
    void builderAddsElfSectionAndSymbolSemanticRows();
    void builderKeepsRawElfRowsWhenSemanticDataIsTruncated();
    void modelHeadersMatchStructureGridColumns();
    void modelFetchesLazyChildrenOnceAndFormatsOffsets();
    void modelSupportsHierarchyAndEditableCells();
    void modelAppliesTypeDisplayOptionsWithoutResettingRows();
    void modelBuildsExpandableRowsForStructFields();
};

class ScopedEnvironmentVariable
{
public:
    ScopedEnvironmentVariable(const char *name, const QByteArray &value)
        : m_name(name)
        , m_hadValue(qEnvironmentVariableIsSet(name))
        , m_previous(qgetenv(name))
    {
        qputenv(name, value);
    }

    ~ScopedEnvironmentVariable()
    {
        if (m_hadValue)
            qputenv(m_name, m_previous);
        else
            qunsetenv(m_name);
    }

private:
    const char *m_name = nullptr;
    bool m_hadValue = false;
    QByteArray m_previous;
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

static TypeDecl *firstExported(StrataLibrary *library)
{
    if (!library)
        return nullptr;

    for (TypeDecl *decl : library->globalTypeDeclList)
        if (decl && FindTag(decl->tagList, TOK_EXPORT, nullptr))
            return decl;

    return nullptr;
}

static QString exportedName(TypeDecl *decl)
{
    if (!decl)
        return {};

    for (Type *type : decl->declList)
        if (type && type->sym)
            return QString::fromLocal8Bit(type->sym->name);

    if (decl->baseType && decl->baseType->ty == typeSTRUCT && decl->baseType->sptr && decl->baseType->sptr->symbol)
        return QString::fromLocal8Bit(decl->baseType->sptr->symbol->name);

    return {};
}

static TypeDecl *exportedNamed(StrataLibrary *library, const QString &name)
{
    if (!library)
        return nullptr;

    for (TypeDecl *decl : library->globalTypeDeclList)
        if (decl && FindTag(decl->tagList, TOK_EXPORT, nullptr) && exportedName(decl) == name)
            return decl;

    return nullptr;
}

static TypeDecl *typeNamed(StrataLibrary *library, const QString &name)
{
    if (!library)
        return nullptr;

    const QByteArray nameBytes = name.toLocal8Bit();
    for (TypeDecl *decl : library->globalTypeDeclList)
    {
        if (!decl)
            continue;

        for (Type *type : decl->declList)
        {
            if ((type->ty == typeTYPEDEF || type->ty == typeIDENTIFIER) && type->sym
                && nameBytes == type->sym->name)
            {
                return decl;
            }
        }

        Type *base = BaseNode(decl->baseType);
        if (base && (base->ty == typeSTRUCT || base->ty == typeUNION) && base->sptr
            && base->sptr->symbol && nameBytes == base->sptr->symbol->name)
        {
            return decl;
        }
    }

    return nullptr;
}

static std::vector<std::unique_ptr<StructureRow>> buildRows(StrataLibrary *library,
                                                            TypeDecl *root,
                                                            const QByteArray &bytes,
                                                            uint64_t baseOffset = 0,
                                                            const StructureDisplayOptions &options = StructureDisplayOptions())
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
                         },
                         options);
}

static bool parseStandardElfDefinition(StrataLibrary *library)
{
    if (!library)
        return false;

    Parser parser(library);
    const QString path = QDir(QStringLiteral(CAUSEWAY_TEST_DATA_DIR)).filePath(QStringLiteral("elf.strata"));
    return parser.Ooof(qPrintable(path));
}

static bool parseStandardDefinition(StrataLibrary *library, const QString &fileName)
{
    if (!library)
        return false;

    Parser parser(library);
    const QString path = QDir(QStringLiteral(CAUSEWAY_TEST_DATA_DIR)).filePath(fileName);
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

static StructureRow *findDescendantNamed(StructureRow *parent, const QString &name)
{
    if (!parent)
        return nullptr;
    if (parent->name == name)
        return parent;

    for (const auto &child : parent->children)
        if (StructureRow *found = findDescendantNamed(child.get(), name))
            return found;

    return nullptr;
}

static QString childNames(StructureRow *parent)
{
    if (!parent)
        return QStringLiteral("<null>");

    QStringList names;
    for (const auto &child : parent->children)
        names.push_back(child->name);
    return names.join(QStringLiteral(", "));
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

static void writeBe64(QByteArray *bytes, qsizetype offset, quint64 value)
{
    QVERIFY(bytes != nullptr);
    QVERIFY(offset >= 0);
    QVERIFY(offset + 8 <= bytes->size());
    for (int i = 0; i < 8; ++i)
        (*bytes)[offset + i] = char((value >> ((7 - i) * 8)) & 0xff);
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

void StructViewTests::managerCreatesUserStrataDirectory()
{
    // Scenario: the Structure View panel is opened on a fresh profile.
    // Expected: the user-editable strata directory is created lazily beside the
    // settings/palette area, so users have a stable place to drop definitions.
    // Regression guard: first-open loading must not fail just because the custom
    // definitions directory has not existed before.
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString userDir = temp.filePath(QStringLiteral("user-strata"));
    StructureDefinitionManager manager;
    manager.setBuiltinStructDirsForTests({});
    manager.setUserStrataDirForTests(userDir);

    QVERIFY2(manager.reload(), qPrintable(manager.lastError()));
    QVERIFY(QDir(userDir).exists());
}

void StructViewTests::managerDiscoversBuiltinAndUserDefinitionFiles()
{
    // Scenario: shipped definitions and user definitions are both available.
    // Expected: built-ins and user files are discovered, with the canonical
    // .strata extension, .struct compatibility extension, and legacy
    // .txt/.bstruct extensions all accepted.
    // Regression guard: adding the user watched directory must not hide the
    // runtime definitions that ship with the app.
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString builtinDir = temp.filePath(QStringLiteral("strata"));
    const QString userDir = temp.filePath(QStringLiteral("user-strata"));
    QVERIFY(QDir().mkpath(builtinDir));
    QVERIFY(QDir().mkpath(userDir));
    writeTextFile(QDir(builtinDir).filePath(QStringLiteral("builtin.strata")), "typedef dword BuiltinType;\n");
    writeTextFile(QDir(builtinDir).filePath(QStringLiteral("builtin.struct")), "typedef dword StaleBuiltinType;\n");
    writeTextFile(QDir(userDir).filePath(QStringLiteral("user.struct")), "typedef dword StructCompatType;\n");
    writeTextFile(QDir(userDir).filePath(QStringLiteral("user.txt")), "typedef byte LegacyType;\n");
    writeTextFile(QDir(userDir).filePath(QStringLiteral("user.bstruct")), "typedef word UserType;\n");

    StructureDefinitionManager manager;
    manager.setBuiltinStructDirsForTests({ builtinDir });
    manager.setUserStrataDirForTests(userDir);

    QVERIFY2(manager.reload(), qPrintable(manager.lastError()));
    QCOMPARE(manager.definitionFiles().size(), 4);
}

void StructViewTests::reloadSwapsInParsedStrataLibrary()
{
    // Scenario: the user accepts a valid definition set by reloading it.
    // Expected: the manager publishes a fresh StrataLibrary containing parsed
    // declarations from that definition set.
    // Regression guard: reload must not merely rescan filenames while leaving
    // the old parser result in place.
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString userDir = temp.filePath(QStringLiteral("user-strata"));
    QVERIFY(QDir().mkpath(userDir));
    writeTextFile(QDir(userDir).filePath(QStringLiteral("first.txt")), "typedef dword FirstType;\n");

    StructureDefinitionManager manager;
    manager.setBuiltinStructDirsForTests({});
    manager.setUserStrataDirForTests(userDir);

    QVERIFY2(manager.reload(), qPrintable(manager.lastError()));
    QVERIFY(manager.library());
    QCOMPARE(manager.library()->globalTypeDeclList.size(), size_t(1));
}

void StructViewTests::managerReportsChangedDefinitionsWithoutAutoReload()
{
    // Scenario: a watched .struct file is saved while the Structure View is open.
    // Expected: the manager reports that definitions changed, but leaves the
    // active StrataLibrary alone until the user explicitly clicks Reload.
    // Regression guard: file watching used to auto-reload immediately, which
    // could surprise users while they were still editing a broken definition.
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString userDir = temp.filePath(QStringLiteral("user-strata"));
    QVERIFY(QDir().mkpath(userDir));
    const QString filePath = QDir(userDir).filePath(QStringLiteral("types.struct"));
    writeTextFile(filePath, "typedef dword FirstType;\n");

    StructureDefinitionManager manager;
    manager.setBuiltinStructDirsForTests({});
    manager.setUserStrataDirForTests(userDir);

    QVERIFY2(manager.reload(), qPrintable(manager.lastError()));
    StrataLibrary *activeLibrary = manager.library();
    QVERIFY(activeLibrary);

    QSignalSpy changedSpy(&manager, &StructureDefinitionManager::definitionFilesChanged);
    QSignalSpy reloadedSpy(&manager, &StructureDefinitionManager::definitionsReloaded);
    writeTextFile(filePath, "typedef byte SecondType;\n");

    QTRY_VERIFY_WITH_TIMEOUT(!changedSpy.isEmpty(), 3000);
    QCOMPARE(reloadedSpy.size(), 0);
    QCOMPARE(manager.library(), activeLibrary);
}

void StructViewTests::brokenDefinitionFileIsReportedWithoutDroppingValidOnes()
{
    // Scenario: one definition file has a syntax error (e.g. the user just saved a
    // typo) while a sibling file is perfectly valid.
    // Expected: reload() still parses the valid file into the library and exposes
    // the broken one via failedFiles(), instead of discarding everything.
    // Regression guard: a single broken file used to abort the whole batch, so even
    // unrelated, already-valid files vanished from the dropdown until fixed.
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString userDir = temp.filePath(QStringLiteral("user-strata"));
    QVERIFY(QDir().mkpath(userDir));
    const QString stableFilePath = QDir(userDir).filePath(QStringLiteral("stable.struct"));
    const QString brokenFilePath = QDir(userDir).filePath(QStringLiteral("broken.struct"));
    writeTextFile(stableFilePath, "typedef dword StableType;\n");
    writeTextFile(brokenFilePath, "this is not valid Strata syntax\n");

    StructureDefinitionManager manager;
    manager.setBuiltinStructDirsForTests({});
    manager.setUserStrataDirForTests(userDir);

    QVERIFY(!manager.reload());
    QVERIFY(manager.library());
    QCOMPARE(manager.library()->globalTypeDeclList.size(), size_t(1));

    const QList<FailedStructureFile> failures = manager.failedFiles();
    QCOMPARE(failures.size(), 1);
    QCOMPARE(failures.first().fileName, QStringLiteral("broken.struct"));
    QCOMPARE(failures.first().filePath, brokenFilePath);
    QVERIFY(failures.first().message.contains(QStringLiteral("broken.struct(1) : error")));

    QVERIFY(!manager.lastError().isEmpty());
    QCOMPARE(manager.lastError(), QStringLiteral("Failed: broken.struct"));

    const QString log = manager.loadLog();
    QVERIFY(log.contains(QStringLiteral("Failed: broken.struct")));
    QVERIFY(log.contains(QStringLiteral("broken.struct(1) : error")));
}

void StructViewTests::fixingABrokenDefinitionFileRestoresItsTypes()
{
    // Scenario: the user corrects the syntax error and saves again.
    // Expected: the next reload() picks the file back up and its type reappears,
    // with no leftover entry in failedFiles().
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString userDir = temp.filePath(QStringLiteral("user-strata"));
    QVERIFY(QDir().mkpath(userDir));
    const QString filePath = QDir(userDir).filePath(QStringLiteral("types.struct"));
    writeTextFile(filePath, "this is not valid Strata syntax\n");

    StructureDefinitionManager manager;
    manager.setBuiltinStructDirsForTests({});
    manager.setUserStrataDirForTests(userDir);

    QVERIFY(!manager.reload());
    QCOMPARE(manager.failedFiles().size(), 1);
    QCOMPARE(manager.library()->globalTypeDeclList.size(), size_t(0));

    writeTextFile(filePath, "typedef dword FixedType;\n");
    QVERIFY(manager.reload());
    QVERIFY(manager.failedFiles().isEmpty());
    QCOMPARE(manager.library()->globalTypeDeclList.size(), size_t(1));
}

void StructViewTests::partiallyParsedFileDoesNotExposeStaleExportedType()
{
    // Scenario: a file exports a type, then the user introduces a syntax error
    // further down (e.g. while adding a second struct) and saves. The declaration
    // before the error still parses fine and stays in the shared library.
    // Expected: exportedTypes() does not keep listing that now-stale declaration --
    // the file shows up only via failedFiles(), not as a confusing extra "good"
    // entry alongside its own "failed to load" entry.
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString userDir = temp.filePath(QStringLiteral("user-strata"));
    QVERIFY(QDir().mkpath(userDir));
    const QString filePath = QDir(userDir).filePath(QStringLiteral("types.struct"));
    writeTextFile(filePath,
                  "[export]\n"
                  "struct Good { byte b; } good;\n");

    StructureDefinitionManager manager;
    manager.setBuiltinStructDirsForTests({});
    manager.setUserStrataDirForTests(userDir);

    QVERIFY2(manager.reload(), qPrintable(manager.lastError()));
    QCOMPARE(manager.exportedTypes().size(), 1);

    writeTextFile(filePath,
                  "[export]\n"
                  "struct Good { byte b; } good;\n"
                  "this is not valid Strata syntax\n");
    QVERIFY(!manager.reload());
    QCOMPARE(manager.failedFiles().size(), 1);
    QCOMPARE(manager.library()->globalTypeDeclList.size(), size_t(1));
    QVERIFY(manager.exportedTypes().isEmpty());
}

void StructViewTests::exportedTypesUseExplicitExportTagsOnly()
{
    // Scenario: a definition file contains many parseable declarations, but only
    // one is marked as user-facing with the Strata [export] tag.
    // Expected: the Structure View root selector lists only the tagged type.
    // Regression guard: Parser::exported remains intentionally permissive for
    // round-tripping, so the UI must filter on the real TOK_EXPORT tag instead.
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString userDir = temp.filePath(QStringLiteral("user-strata"));
    QVERIFY(QDir().mkpath(userDir));
    writeTextFile(QDir(userDir).filePath(QStringLiteral("types.txt")),
                  "[export]\n"
                  "struct ExportedRoot { dword magic; } exportedRoot;\n"
                  "struct HiddenHelper { word flags; } hiddenHelper;\n");

    StructureDefinitionManager manager;
    manager.setBuiltinStructDirsForTests({});
    manager.setUserStrataDirForTests(userDir);

    QVERIFY2(manager.reload(), qPrintable(manager.lastError()));
    const QList<ExportedStructureType> exported = manager.exportedTypes();
    QCOMPARE(exported.size(), 1);
    QVERIFY(exported[0].typeDecl != nullptr);
}

void StructViewTests::exportedTypesExposeAssocExtensions()
{
    // Scenario: an exported Strata declaration declares file associations.
    // Expected: the Structure View loader exposes normalized lowercase suffixes
    // so the panel can auto-select the matching root type for the current file.
    // Regression guard: PE/ELF-style definitions should not require the user to
    // manually pick the root structure every time the panel reloads.
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString userDir = temp.filePath(QStringLiteral("user-strata"));
    QVERIFY(QDir().mkpath(userDir));
    writeTextFile(QDir(userDir).filePath(QStringLiteral("types.txt")),
                  "[export, assoc(\".EXE\", \"dll\")]\n"
                  "struct PeRoot { byte magic; } pe;\n");

    StructureDefinitionManager manager;
    manager.setBuiltinStructDirsForTests({});
    manager.setUserStrataDirForTests(userDir);

    QVERIFY2(manager.reload(), qPrintable(manager.lastError()));
    const QList<ExportedStructureType> exported = manager.exportedTypes();
    QCOMPARE(exported.size(), 1);
    QCOMPARE(exported[0].assocExtensions, QStringList({ QStringLiteral(".exe"), QStringLiteral(".dll") }));
}

void StructViewTests::exportedTypesExposeMagicSignatures()
{
    // Scenario: an exported Structure View root declares byte signatures for
    // files that may not have a useful extension.
    // Expected: the manager exposes normalized magic bytes, including numeric
    // byte fragments, without asking the panel to understand Strata syntax.
    // Regression guard: auto-selection for extensionless ELF-style binaries
    // should be data-driven by definitions rather than hard-coded in the UI.
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString userDir = temp.filePath(QStringLiteral("user-strata"));
    QVERIFY(QDir().mkpath(userDir));
    writeTextFile(QDir(userDir).filePath(QStringLiteral("types.txt")),
                  "[export, magic({ 'A', 'B' }, 2), magic({ 0x7F, 'E', 'L', 'F' }, 4)]\n"
                  "struct Root { byte magic; } root;\n");

    StructureDefinitionManager manager;
    manager.setBuiltinStructDirsForTests({});
    manager.setUserStrataDirForTests(userDir);

    QVERIFY2(manager.reload(), qPrintable(manager.lastError()));
    const QList<ExportedStructureType> exported = manager.exportedTypes();
    QCOMPARE(exported.size(), 1);
    QCOMPARE(exported[0].magicSignatures.size(), 2);
    QCOMPARE(exported[0].magicSignatures[0].offset, uint64_t(4));
    QCOMPARE(exported[0].magicSignatures[0].bytes, QByteArray("\x7f""ELF", 4));
    QCOMPARE(exported[0].magicSignatures[1].offset, uint64_t(2));
    QCOMPARE(exported[0].magicSignatures[1].bytes, QByteArray("AB", 2));
}

void StructViewTests::exportedTypesExposeDescriptions()
{
    // Scenario: exported roots can carry a friendly description for the
    // Structure View dropdown, while older definitions may omit it.
    // Expected: the manager exposes the string description when present and an
    // empty string when absent, leaving the panel to use its existing fallback.
    // Regression guard: root labels should be Strata metadata, not hard-coded
    // PE/ELF special cases in the combo box.
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString userDir = temp.filePath(QStringLiteral("user-strata"));
    QVERIFY(QDir().mkpath(userDir));
    writeTextFile(QDir(userDir).filePath(QStringLiteral("types.txt")),
                  "[export(\"Friendly Root\")]\n"
                  "struct Friendly { byte magic; } friendly;\n"
                  "[export]\n"
                  "struct Plain { word flags; } plain;\n");

    StructureDefinitionManager manager;
    manager.setBuiltinStructDirsForTests({});
    manager.setUserStrataDirForTests(userDir);

    QVERIFY2(manager.reload(), qPrintable(manager.lastError()));
    const QList<ExportedStructureType> exported = manager.exportedTypes();
    QCOMPARE(exported.size(), 2);
    QCOMPARE(exported[0].description, QStringLiteral("Friendly Root"));
    QCOMPARE(exported[1].description, QString());
}

void StructViewTests::exportedTypesExposeCategories()
{
    // Scenario: exported roots can declare a coarse Structure View category for
    // grouped dropdown display.
    // Expected: the manager exposes the normalized category string and leaves
    // uncategorized roots empty so the panel can place them in its fallback
    // section.
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString userDir = temp.filePath(QStringLiteral("user-strata"));
    QVERIFY(QDir().mkpath(userDir));
    writeTextFile(QDir(userDir).filePath(QStringLiteral("types.txt")),
                  "[export(\"Image Root\"), category(\"Media\")]\n"
                  "struct ImageRoot { byte magic; } imageRoot;\n"
                  "[export(\"Plain Root\")]\n"
                  "struct PlainRoot { word flags; } plainRoot;\n");

    StructureDefinitionManager manager;
    manager.setBuiltinStructDirsForTests({});
    manager.setUserStrataDirForTests(userDir);

    QVERIFY2(manager.reload(), qPrintable(manager.lastError()));
    const QList<ExportedStructureType> exported = manager.exportedTypes();
    QCOMPARE(exported.size(), 2);
    QCOMPARE(exported[0].category, QStringLiteral("media"));
    QCOMPARE(exported[1].category, QString());
}

void StructViewTests::exportedTypesResolveDuplicateVersionsAndLogDecision()
{
    // Scenario: a user definition intentionally replaces a bundled exported
    // format by using the same exported display name with a higher version.
    // Expected: both files parse, only the newer user export is listed, and the
    // load log names the selected and ignored candidates.
    // Regression guard: stale config copies should not silently shadow built-ins
    // and intentional overrides should not need to reuse built-in C symbols.
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString builtinDir = temp.filePath(QStringLiteral("strata"));
    const QString userDir = temp.filePath(QStringLiteral("user-strata"));
    QVERIFY(QDir().mkpath(builtinDir));
    QVERIFY(QDir().mkpath(userDir));

    writeTextFile(QDir(builtinDir).filePath(QStringLiteral("zip.strata")),
                  "[export(\"ZIP Archive\"), version(1), assoc(\".zip\")]\n"
                  "struct BuiltinZipRoot { byte builtin; } builtinZip;\n");
    writeTextFile(QDir(userDir).filePath(QStringLiteral("zip_override.struct")),
                  "[export(\"ZIP Archive\"), version(2), assoc(\".zip\")]\n"
                  "struct UserZipRoot { byte user; } userZip;\n");

    StructureDefinitionManager manager;
    manager.setBuiltinStructDirsForTests({ builtinDir });
    manager.setUserStrataDirForTests(userDir);

    QVERIFY2(manager.reload(), qPrintable(manager.lastError()));
    const QList<ExportedStructureType> exported = manager.exportedTypes();
    QCOMPARE(exported.size(), 1);
    QCOMPARE(exported[0].description, QStringLiteral("ZIP Archive"));
    QCOMPARE(exported[0].version, 2);
    QVERIFY(exported[0].userDefinition);
    QCOMPARE(exported[0].fileName, QStringLiteral("zip_override.struct"));

    const QString log = manager.loadLog();
    QVERIFY(log.contains(QStringLiteral("Export ZIP Archive: picked: user zip_override.struct version 2")));
    QVERIFY(log.contains(QStringLiteral("Export ZIP Archive: ignored: built-in zip.strata version 1")));
    QVERIFY(log.contains(QStringLiteral("Exported type(s): 1")));
}

void StructViewTests::userDefinitionOverridesBuiltinWithSameBaseName()
{
    // Scenario: the user saves an editable copy of a bundled definition under
    // the same basename.
    // Expected: the user copy replaces the built-in before parsing, so shared
    // helper names can be reused without causing redefinition errors.
    // Regression guard: loading both files into the same StrataLibrary collides
    // on common type names such as JAVA_MAGIC.
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString builtinDir = temp.filePath(QStringLiteral("strata"));
    const QString userDir = temp.filePath(QStringLiteral("user-strata"));
    QVERIFY(QDir().mkpath(builtinDir));
    QVERIFY(QDir().mkpath(userDir));

    const QString builtinPath = QDir(builtinDir).filePath(QStringLiteral("java.strata"));
    const QString userPath = QDir(userDir).filePath(QStringLiteral("java.strata"));
    writeTextFile(builtinPath,
                  "enum JAVA_MAGIC { BUILTIN_MAGIC = 1 };\n"
                  "[export(\"Java Class\"), version(1)]\n"
                  "struct BuiltinJavaRoot { byte builtin; } builtinJava;\n");
    writeTextFile(userPath,
                  "enum JAVA_MAGIC { USER_MAGIC = 2 };\n"
                  "[export(\"Java Class\"), version(2)]\n"
                  "struct UserJavaRoot { byte user; } userJava;\n");

    StructureDefinitionManager manager;
    manager.setBuiltinStructDirsForTests({ builtinDir });
    manager.setUserStrataDirForTests(userDir);

    QVERIFY2(manager.reload(), qPrintable(manager.lastError()));
    QVERIFY(!manager.definitionFiles().contains(builtinPath));
    QVERIFY(manager.definitionFiles().contains(userPath));

    const QList<ExportedStructureType> exported = manager.exportedTypes();
    QCOMPARE(exported.size(), 1);
    QCOMPARE(exported[0].description, QStringLiteral("Java Class"));
    QCOMPARE(exported[0].version, 2);
    QVERIFY(exported[0].userDefinition);
    QCOMPARE(exported[0].filePath, userPath);
}

void StructViewTests::brokenUserDefinitionOverrideBlocksBuiltinFallback()
{
    // Scenario: a same-basename user override is currently broken.
    // Expected: the override still replaces the built-in, and the parse failure
    // is reported without falling back to a second, incompatible definition set.
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString builtinDir = temp.filePath(QStringLiteral("strata"));
    const QString userDir = temp.filePath(QStringLiteral("user-strata"));
    QVERIFY(QDir().mkpath(builtinDir));
    QVERIFY(QDir().mkpath(userDir));

    const QString builtinPath = QDir(builtinDir).filePath(QStringLiteral("zip.strata"));
    const QString userPath = QDir(userDir).filePath(QStringLiteral("zip.struct"));
    writeTextFile(builtinPath,
                  "[export(\"ZIP Archive\"), version(1)]\n"
                  "struct BuiltinZipRoot { byte builtin; } builtinZip;\n");
    writeTextFile(userPath,
                  "[export(\"ZIP Archive\"), version(2)]\n"
                  "struct UserZipRoot { Word broken; } userZip;\n");

    StructureDefinitionManager manager;
    manager.setBuiltinStructDirsForTests({ builtinDir });
    manager.setUserStrataDirForTests(userDir);

    QVERIFY(!manager.reload());
    QVERIFY(!manager.definitionFiles().contains(builtinPath));
    QVERIFY(manager.definitionFiles().contains(userPath));

    const QString log = manager.loadLog();
    QVERIFY(log.contains(QStringLiteral("Failed: zip.struct")));
    QVERIFY(!log.contains(QStringLiteral("built-in(picked):")));
    QVERIFY(manager.exportedTypes().isEmpty());
}

void StructViewTests::builderFormatsScalarsAndEndian()
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

void StructViewTests::builderFormatsLeb128ScalarsAndAdvancesByEncodedLength()
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

void StructViewTests::builderUsesLeb128ValuesInExpressions()
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

void StructViewTests::builderRendersBitflagsAsExpandableRows()
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

void StructViewTests::builderFormatsCharacterArraysAsStrings()
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

void StructViewTests::builderFormatsTaggedByteArraysAsStrings()
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

void StructViewTests::builderFormatsGenericDisplayTags()
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

void StructViewTests::builderAppliesTreePresentationTags()
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

void StructViewTests::builderRendersRaggedStringTables()
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

void StructViewTests::builderFormatsScalarArraysAsPreviewLists()
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

void StructViewTests::builderPopulatesCommentsFromTypeDeclarations()
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

void StructViewTests::builderUsesPackedLayoutByDefault()
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

void StructViewTests::builderAppliesStructAndFieldAlignment()
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

void StructViewTests::builderLetsOffsetOverrideAlignment()
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

void StructViewTests::builderKeepsUnionMembersAtAlignedBase()
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

void StructViewTests::builderUsesExtentToAdvancePastRenderedUnionSize()
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

void StructViewTests::builderPadsDeclarationsToAlignmentBoundaries()
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

void StructViewTests::builderSkipsAbsentOptionalDeclarations()
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

void StructViewTests::builderUsesSizeIsForUnsizedArrays()
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

void StructViewTests::builderUsesMaxCountAndTerminatorExpressions()
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

void StructViewTests::builderUsesNamedOffsetMapsAndValueAt()
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

void StructViewTests::builderUsesCountAsForLogicalArraySlots()
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

void StructViewTests::builderEvaluatesArrayIndexedUnionMembers()
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

void StructViewTests::builderUsesNameFieldForStructArrayElements()
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

void StructViewTests::builderAlignsFieldNamesWithinCompoundTypes()
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

void StructViewTests::builderKeepsSignedPrimitiveTypedefNamesInStorageMode()
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

void StructViewTests::builderBuildsNestedStructRowsAndOffsets()
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

void StructViewTests::builderSupportsArraysOffsetsEnumsAndSwitchCases()
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

void StructViewTests::builderExposesEnumChoicesAndEntrypoints()
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

void StructViewTests::builderEvaluatesUnionSwitchSelectorsFromTypedLayout()
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

void StructViewTests::builderEvaluatesFieldsAndCorrectedExpressions()
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

void StructViewTests::builderEvaluatesFindSearchExpressions()
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

void StructViewTests::builderEvaluatesOctalStringExpressions()
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

void StructViewTests::builderSelectsUnionMembersFromStringExpressions()
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

void StructViewTests::builderSelectsUnionMembersFromFourCcExpressions()
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

void StructViewTests::builderUsesDynamicEndianExpressions()
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

void StructViewTests::builderUsesSimpleRootNamesForBuiltinTypedefRoots()
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

void StructViewTests::builderOptionallySortsTopLevelRowsByOffset()
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

void StructViewTests::builderRendersDexHeaderAndTables()
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
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->name, QStringLiteral("DEX"));

    StructureRow *header = findChildNamed(rows[0].get(), QStringLiteral("DEX_HEADER header"));
    QVERIFY(header);
    QVERIFY(findChildNamed(header, QStringLiteral("dword file_size")));
    QCOMPARE(findChildNamed(header, QStringLiteral("dword file_size"))->value, QStringLiteral("272"));
    QCOMPARE(findChildNamed(header, QStringLiteral("dword endian_tag"))->value,
             QStringLiteral("DEX_ENDIAN_CONSTANT"));

    StructureRow *stringIds = findChildNamed(rows[0].get(), QStringLiteral("DEX_STRING_ID_ITEM stringIds[]"));
    QVERIFY(stringIds);
    QCOMPARE(stringIds->absoluteOffset, uint64_t(0x70));
    QCOMPARE(stringIds->children.size(), size_t(1));
    QCOMPARE(findChildNamed(stringIds->children[0].get(), QStringLiteral("dword string_data_off"))->value,
             QStringLiteral("200"));
    QVERIFY(!findChildNamed(rows[0].get(), QStringLiteral("DEX Strings")));

    StructureRow *typeIds = findChildNamed(rows[0].get(), QStringLiteral("DEX_TYPE_ID_ITEM typeIds[]"));
    QVERIFY(typeIds);
    QCOMPARE(typeIds->children.size(), size_t(1));
    QVERIFY(!findChildNamed(rows[0].get(), QStringLiteral("DEX Types")));

    StructureRow *protoIds = findChildNamed(rows[0].get(), QStringLiteral("DEX_PROTO_ID_ITEM protoIds[]"));
    QVERIFY(protoIds);
    QCOMPARE(protoIds->children.size(), size_t(1));
    QVERIFY(findChildNamed(protoIds->children[0].get(), QStringLiteral("dword shorty_idx")));
    QVERIFY(!findChildNamed(rows[0].get(), QStringLiteral("DEX Protos")));

    StructureRow *fieldIds = findChildNamed(rows[0].get(), QStringLiteral("DEX_FIELD_ID_ITEM fieldIds[]"));
    QVERIFY(fieldIds);
    QCOMPARE(fieldIds->children.size(), size_t(1));
    QVERIFY(!findChildNamed(rows[0].get(), QStringLiteral("DEX Fields")));

    StructureRow *methodIds = findChildNamed(rows[0].get(), QStringLiteral("DEX_METHOD_ID_ITEM methodIds[]"));
    QVERIFY(methodIds);
    QCOMPARE(methodIds->children.size(), size_t(1));
    QVERIFY(!findChildNamed(rows[0].get(), QStringLiteral("DEX Methods")));

    StructureRow *classDefs = findChildNamed(rows[0].get(), QStringLiteral("DEX_CLASS_DEF_ITEM classDefs[]"));
    QVERIFY(classDefs);
    QCOMPARE(classDefs->absoluteOffset, uint64_t(0x94));
    StructureRow *accessFlags = findChildNamed(classDefs->children[0].get(), QStringLiteral("dword access_flags"));
    QVERIFY(accessFlags);
    QVERIFY(findChildNamed(accessFlags, QStringLiteral("DEX_ACC_PUBLIC")));
    QVERIFY(findChildNamed(accessFlags, QStringLiteral("DEX_ACC_ABSTRACT")));
    QVERIFY(!findChildNamed(rows[0].get(), QStringLiteral("DEX Classes")));

    StructureRow *mapList = findChildNamed(rows[0].get(), QStringLiteral("DEX_MAP_LIST mapList"));
    QVERIFY(mapList);
    QCOMPARE(mapList->absoluteOffset, uint64_t(0xb4));
    StructureRow *mapItems = findChildNamed(mapList, QStringLiteral("DEX_MAP_ITEM list[]"));
    QVERIFY(mapItems);
    QCOMPARE(mapItems->children.size(), size_t(7));
    QCOMPARE(findChildNamed(mapItems->children[1].get(), QStringLiteral("word type"))->value,
             QStringLiteral("DEX_TYPE_STRING_ID_ITEM"));

    StructureRow *summary = findChildNamed(rows[0].get(), QStringLiteral("DEX Summary"));
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

void StructViewTests::builderAddsDexSemanticSummaryPastArrayRenderCap()
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
    QCOMPARE(rows.size(), size_t(1));

    StructureRow *stringIds = findChildNamed(rows[0].get(), QStringLiteral("DEX_STRING_ID_ITEM stringIds[]"));
    QVERIFY(stringIds);
    QVERIFY(stringIds->children.size() < kStringCount);
    QVERIFY(!findDescendantNamed(stringIds, QStringLiteral("String[120] NormalName")));

    StructureRow *summary = findChildNamed(rows[0].get(), QStringLiteral("DEX Summary"));
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

void StructViewTests::builderRendersDtbHeaderAndBlocks()
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

void StructViewTests::builderRendersWasmHeaderAndSections()
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
    wasm.append(char(0x17));                          // size
    wasm.append(char(0x04));                          // name size
    wasm.append("name", 4);
    wasm.append(QByteArray::fromHex("0005"));         // module name subsection, size 5
    wasm.append(char(0x04));                          // module name length
    wasm.append("demo", 4);
    wasm.append(QByteArray::fromHex("0109"));         // function names subsection, size 9
    wasm.append(QByteArray::fromHex("0100"));         // one entry, function index 0
    wasm.append(char(0x06));                          // function name length
    wasm.append("answer", 6);
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
    wasm.append(char(0x00));                          // function index 0
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
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->name, QStringLiteral("WASM"));

    StructureRow *header = findChildNamed(rows[0].get(), QStringLiteral("WASM_HEADER header"));
    QVERIFY(header);
    StructureRow *version = findChildNamed(header, QStringLiteral("dword version"));
    QVERIFY2(version, qPrintable(childNames(header)));
    QCOMPARE(version->value, QStringLiteral("1"));

    StructureRow *sections = findChildNamed(rows[0].get(), QStringLiteral("WASM_SECTION sections[]"));
    QVERIFY(sections);
    QCOMPARE(sections->children.size(), size_t(8));

    StructureRow *customSection = sections->children[0].get();
    QCOMPARE(customSection->name, QStringLiteral("[0]WASM_SECTION_CUSTOM"));
    StructureRow *customId = findChildNamed(customSection, QStringLiteral("byte id"));
    QVERIFY2(customId, qPrintable(childNames(customSection)));
    QCOMPARE(customId->value, QStringLiteral("WASM_SECTION_CUSTOM"));
    StructureRow *customSize = findChildNamed(customSection, QStringLiteral("uleb128 size"));
    QVERIFY2(customSize, qPrintable(childNames(customSection)));
    QCOMPARE(customSize->value, QStringLiteral("23"));
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
    QCOMPARE(nameSubsections->children.size(), size_t(2));
    StructureRow *moduleSubsection = nameSubsections->children[0].get();
    QCOMPARE(findChildNamed(moduleSubsection, QStringLiteral("byte id"))->value, QStringLiteral("WASM_NAME_MODULE"));
    StructureRow *moduleName = findChildNamed(moduleSubsection, QStringLiteral("WASM_NAME module"));
    QVERIFY2(moduleName, qPrintable(childNames(moduleSubsection)));
    QCOMPARE(findChildNamed(moduleName, QStringLiteral("byte bytes[]"))->value, QStringLiteral("\"demo\""));
    StructureRow *functionNameSubsection = nameSubsections->children[1].get();
    QCOMPARE(findChildNamed(functionNameSubsection, QStringLiteral("byte id"))->value, QStringLiteral("WASM_NAME_FUNCTION"));
    StructureRow *functionNameMap = findChildNamed(functionNameSubsection, QStringLiteral("WASM_NAME_MAP functions"));
    QVERIFY2(functionNameMap, qPrintable(childNames(functionNameSubsection)));
    StructureRow *functionNames = findChildNamed(functionNameMap, QStringLiteral("WASM_NAME_ASSOC names[]"));
    QVERIFY2(functionNames, qPrintable(childNames(functionNameMap)));
    QCOMPARE(functionNames->children.size(), size_t(1));
    QCOMPARE(functionNames->children[0]->name, QStringLiteral("[0]answer"));
    QCOMPARE(findChildNamed(functionNames->children[0].get(), QStringLiteral("uleb128 index"))->value, QStringLiteral("0"));
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
    StructureRow *typeIndices = findChildNamed(functionPayload, QStringLiteral("uleb128 typeIndices[]"));
    QVERIFY2(typeIndices, qPrintable(childNames(functionPayload)));
    QCOMPARE(typeIndices->value, QStringLiteral("{ 0 }"));

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
    QCOMPARE(exportIndex->value, QStringLiteral("0"));

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

    StructureRow *summary = findChildNamed(rows[0].get(), QStringLiteral("WASM Summary"));
    QVERIFY2(summary, qPrintable(childNames(rows[0].get())));

    StructureRow *summarySections = findChildNamed(summary, QStringLiteral("Sections"));
    QVERIFY2(summarySections, qPrintable(childNames(summary)));
    StructureRow *summaryImportSection = findChildNamed(summarySections, QStringLiteral("WASM_SECTION_IMPORT"));
    QVERIFY2(summaryImportSection, qPrintable(childNames(summarySections)));
    QCOMPARE(summaryImportSection->offset, importSection->offset);
    QCOMPARE(findChildNamed(summaryImportSection, QStringLiteral("Size"))->value, QStringLiteral("13"));

    StructureRow *summaryTypes = findChildNamed(summary, QStringLiteral("Types"));
    QVERIFY2(summaryTypes, qPrintable(childNames(summary)));
    StructureRow *summaryType0 = findChildNamed(summaryTypes, QStringLiteral("type 0"));
    QVERIFY2(summaryType0, qPrintable(childNames(summaryTypes)));
    QCOMPARE(summaryType0->offset, functionTypes->children[0]->offset);
    QCOMPARE(findChildNamed(summaryType0, QStringLiteral("Form"))->value, QStringLiteral("WASM_TYPE_FORM_FUNC"));
    QCOMPARE(findChildNamed(summaryType0, QStringLiteral("Params"))->value, QStringLiteral("0"));
    QCOMPARE(findChildNamed(summaryType0, QStringLiteral("Results"))->value, QStringLiteral("1"));

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
    QCOMPARE(findChildNamed(summaryExport, QStringLiteral("Index"))->value, QStringLiteral("0"));

    StructureRow *summaryCode = findChildNamed(summary, QStringLiteral("Code"));
    QVERIFY2(summaryCode, qPrintable(childNames(summary)));
    StructureRow *summaryCode0 = findChildNamed(summaryCode, QStringLiteral("function 0"));
    QVERIFY2(summaryCode0, qPrintable(childNames(summaryCode)));
    QCOMPARE(summaryCode0->offset, functions->children[0]->offset);
    QCOMPARE(findChildNamed(summaryCode0, QStringLiteral("Size"))->value, QStringLiteral("4"));

    StructureRow *summaryData = findChildNamed(summary, QStringLiteral("Data"));
    QVERIFY2(summaryData, qPrintable(childNames(summary)));
    StructureRow *summaryData0 = findChildNamed(summaryData, QStringLiteral("data 0"));
    QVERIFY2(summaryData0, qPrintable(childNames(summaryData)));
    QCOMPARE(summaryData0->offset, dataSegments->children[0]->offset);
    QCOMPARE(findChildNamed(summaryData0, QStringLiteral("Mode"))->value, QStringLiteral("1"));
}

void StructViewTests::builderRendersJavaClassConstantPool()
{
    // Scenario: Java class files are big-endian and start with a constant pool
    // whose entries are tagged variants.
    // Expected: the standard Java definition renders the class header and
    // selects a UTF-8 constant pool payload by tag.
    StrataLibrary library;
    QVERIFY2(parseStandardDefinition(&library, QStringLiteral("java.strata")), "java.strata failed to parse");
    TypeDecl *javaRoot = exportedNamed(&library, QStringLiteral("JAVA_CLASS"));
    QVERIFY(javaRoot);

    QByteArray klass;
    const auto appendBe16 = [&klass](quint16 value) {
        klass.append(char((value >> 8) & 0xff));
        klass.append(char(value & 0xff));
    };
    const auto appendBe32 = [&klass](quint32 value) {
        klass.append(char((value >> 24) & 0xff));
        klass.append(char((value >> 16) & 0xff));
        klass.append(char((value >> 8) & 0xff));
        klass.append(char(value & 0xff));
    };
    const auto appendBe64 = [&klass, &appendBe32](quint64 value) {
        appendBe32(quint32((value >> 32) & 0xffffffff));
        appendBe32(quint32(value & 0xffffffff));
    };

    appendBe32(0xcafebabe);
    appendBe16(0);  // minor
    appendBe16(52); // Java 8 major
    appendBe16(4);  // long entry, its reserved slot, then one UTF-8 entry
    klass.append(char(5));
    appendBe64(0x0102030405060708);
    klass.append(char(1));
    appendBe16(4);
    klass.append("Test", 4);
    appendBe16(0x0021); // public | super
    appendBe16(0);
    appendBe16(0);
    appendBe16(0);
    appendBe16(0);
    appendBe16(0);
    appendBe16(0);

    auto rows = buildRows(&library, javaRoot, klass);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->name, QStringLiteral("JAVA_CLASS"));
    QCOMPARE(findChildNamed(rows[0].get(), QStringLiteral("dword magic"))->value,
             QStringLiteral("JAVA_MAGIC_CLASS"));
    QCOMPARE(findChildNamed(rows[0].get(), QStringLiteral("word majorVersion"))->value,
             QStringLiteral("52"));

    StructureRow *constantPool = findChildNamed(rows[0].get(), QStringLiteral("JAVA_CP_INFO constantPool[]"));
    QVERIFY2(constantPool, qPrintable(childNames(rows[0].get())));
    QCOMPARE(constantPool->children.size(), size_t(2));
    QCOMPARE(constantPool->children[0]->name, QStringLiteral("[0]JAVA_CONSTANT_Long"));
    QCOMPARE(constantPool->children[1]->name, QStringLiteral("[2]JAVA_CONSTANT_Utf8"));

    StructureRow *utf8 = findDescendantNamed(constantPool->children[1].get(), QStringLiteral("JAVA_CONSTANT_Utf8_INFO utf8"));
    QVERIFY2(utf8, qPrintable(childNames(constantPool->children[1].get())));
    StructureRow *bytes = findChildNamed(utf8, QStringLiteral("byte bytes[]"));
    QVERIFY2(bytes, qPrintable(childNames(utf8)));
    QCOMPARE(bytes->value, QStringLiteral("\"Test\""));

    StructureRow *accessFlags = findChildNamed(rows[0].get(), QStringLiteral("word accessFlags"));
    QVERIFY2(accessFlags, qPrintable(childNames(rows[0].get())));
    QCOMPARE(accessFlags->value, QStringLiteral("JAVA_ACC_PUBLIC | JAVA_ACC_SUPER"));

    QByteArray largeKlass;
    const auto appendLargeBe16 = [&largeKlass](quint16 value) {
        largeKlass.append(char((value >> 8) & 0xff));
        largeKlass.append(char(value & 0xff));
    };
    const auto appendLargeBe32 = [&largeKlass](quint32 value) {
        largeKlass.append(char((value >> 24) & 0xff));
        largeKlass.append(char((value >> 16) & 0xff));
        largeKlass.append(char((value >> 8) & 0xff));
        largeKlass.append(char(value & 0xff));
    };

    appendLargeBe32(0xcafebabe);
    appendLargeBe16(0);
    appendLargeBe16(52);
    appendLargeBe16(102); // 101 physical UTF-8 constants, beyond the display cap.
    for (int i = 0; i < 101; ++i)
    {
        largeKlass.append(char(1));
        appendLargeBe16(1);
        largeKlass.append(char('A' + (i % 26)));
    }
    appendLargeBe16(0x0021);
    appendLargeBe16(0);
    appendLargeBe16(0);
    appendLargeBe16(0);
    appendLargeBe16(0);
    appendLargeBe16(0);
    appendLargeBe16(0);

    rows = buildRows(&library, javaRoot, largeKlass);
    QCOMPARE(rows.size(), size_t(1));
    constantPool = findChildNamed(rows[0].get(), QStringLiteral("JAVA_CP_INFO constantPool[]"));
    QVERIFY2(constantPool, qPrintable(childNames(rows[0].get())));
    QCOMPARE(constantPool->children.size(), size_t(100));
    QCOMPARE(constantPool->children.back()->name, QStringLiteral("[99]JAVA_CONSTANT_Utf8"));
    accessFlags = findChildNamed(rows[0].get(), QStringLiteral("word accessFlags"));
    QVERIFY2(accessFlags, qPrintable(childNames(rows[0].get())));
    QCOMPARE(accessFlags->absoluteOffset, uint64_t(414));
    QCOMPARE(accessFlags->value, QStringLiteral("JAVA_ACC_PUBLIC | JAVA_ACC_SUPER"));
}

void StructViewTests::builderRendersMachOHeaderAndLoadCommands()
{
    // Scenario: 64-bit Mach-O files carry a header followed by typed load commands.
    // Expected: the standard Mach-O definition selects the 64-bit header and
    // decodes common load command payloads such as UUID and main entry point.
    StrataLibrary library;
    QVERIFY2(parseStandardDefinition(&library, QStringLiteral("macho.strata")), "macho.strata failed to parse");
    TypeDecl *machoRoot = exportedNamed(&library, QStringLiteral("MACHO"));
    QVERIFY(machoRoot);

    QByteArray macho(80, '\0');
    writeLe32(&macho, 0, 0xfeedfacf);
    writeLe32(&macho, 4, 0x01000007); // x86_64
    writeLe32(&macho, 8, 3);
    writeLe32(&macho, 12, 2); // execute
    writeLe32(&macho, 16, 2);
    writeLe32(&macho, 20, 48);
    writeLe32(&macho, 24, 0x00200004); // dyldlink | pie
    writeLe32(&macho, 28, 0);
    writeLe32(&macho, 32, 0x1b); // LC_UUID
    writeLe32(&macho, 36, 24);
    for (int i = 0; i < 16; ++i)
        macho[40 + i] = char(i);
    writeLe32(&macho, 56, 0x80000028); // LC_MAIN
    writeLe32(&macho, 60, 24);
    writeLe64(&macho, 64, 0x1000);
    writeLe64(&macho, 72, 0);

    auto rows = buildRows(&library, machoRoot, macho);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->name, QStringLiteral("MACHO"));

    StructureRow *header = findDescendantNamed(rows[0].get(), QStringLiteral("MACHO_HEADER64 header64"));
    QVERIFY2(header, qPrintable(childNames(rows[0].get())));
    QCOMPARE(findChildNamed(rows[0].get(), QStringLiteral("dword magic"))->value, QStringLiteral("MACHO_MAGIC_64"));
    QCOMPARE(findChildNamed(header, QStringLiteral("dword cpuType"))->value, QStringLiteral("MACHO_CPU_TYPE_X86_64"));
    QCOMPARE(findChildNamed(header, QStringLiteral("dword commandCount"))->value, QStringLiteral("2"));

    StructureRow *commands = findChildNamed(rows[0].get(), QStringLiteral("MACHO_LOAD_COMMAND loadCommands[]"));
    QVERIFY2(commands, qPrintable(childNames(rows[0].get())));
    QCOMPARE(commands->children.size(), size_t(2));
    QCOMPARE(commands->children[0]->name, QStringLiteral("[0]MACHO_LC_UUID"));
    QCOMPARE(commands->children[1]->name, QStringLiteral("[1]MACHO_LC_MAIN"));

    StructureRow *uuid = findDescendantNamed(commands->children[0].get(), QStringLiteral("MACHO_UUID_COMMAND uuid"));
    QVERIFY2(uuid, qPrintable(childNames(commands->children[0].get())));
    StructureRow *uuidBytes = findChildNamed(uuid, QStringLiteral("byte uuid[]"));
    QVERIFY2(uuidBytes, qPrintable(childNames(uuid)));
    QCOMPARE(uuidBytes->value, QStringLiteral("\"00010203-0405-0607-0809-0a0b0c0d0e0f\""));

    StructureRow *main = findDescendantNamed(commands->children[1].get(), QStringLiteral("MACHO_MAIN_COMMAND main"));
    QVERIFY2(main, qPrintable(childNames(commands->children[1].get())));
    QCOMPARE(findChildNamed(main, QStringLiteral("qword entryOffset"))->value, QStringLiteral("4096"));
}

void StructViewTests::builderRendersSfntTableDirectory()
{
    // Scenario: TTF/OTF fonts share the big-endian SFNT table-directory
    // container. Table records carry FourCC tags plus offsets and lengths.
    // Expected: the standard SFNT definition names table-record rows by tag,
    // keeps raw table-directory fields intact, and emits table bytes under
    // SFNT Data/FontTables.
    StrataLibrary library;
    QVERIFY2(parseStandardDefinition(&library, QStringLiteral("sfnt.strata")), "sfnt.strata failed to parse");
    TypeDecl *sfntRoot = exportedNamed(&library, QStringLiteral("SFNT"));
    QVERIFY(sfntRoot);

    QByteArray font(54, '\0');
    writeBe32(&font, 0, 0x00010000); // TrueType sfntVersion
    writeBe16(&font, 4, 2);          // numTables
    writeBe16(&font, 6, 32);         // searchRange
    writeBe16(&font, 8, 1);          // entrySelector
    writeBe16(&font, 10, 0);         // rangeShift
    font.replace(12, 4, "head");
    writeBe32(&font, 16, 0x11111111);
    writeBe32(&font, 20, 44);
    writeBe32(&font, 24, 4);
    font.replace(28, 4, "name");
    writeBe32(&font, 32, 0x22222222);
    writeBe32(&font, 36, 48);
    writeBe32(&font, 40, 6);
    font.replace(44, 4, QByteArray::fromHex("01020304"));
    font.replace(48, 6, QByteArray("font\0\0", 6));

    auto rows = buildRows(&library, sfntRoot, font);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->name, QStringLiteral("SFNT"));

    StructureRow *signature = findChildNamed(rows[0].get(), QStringLiteral("dword signature"));
    QVERIFY2(signature, qPrintable(childNames(rows[0].get())));
    QCOMPARE(signature->value, QStringLiteral("SFNT_TRUETYPE_1_0"));

    StructureRow *offsetTable = findChildNamed(rows[0].get(), QStringLiteral("SFNT_OFFSET_TABLE font"));
    QVERIFY2(offsetTable, qPrintable(childNames(rows[0].get())));
    QCOMPARE(findChildNamed(offsetTable, QStringLiteral("word numTables"))->value, QStringLiteral("2"));

    StructureRow *tables = findChildNamed(offsetTable, QStringLiteral("SFNT_TABLE_RECORD tables[]"));
    QVERIFY2(tables, qPrintable(childNames(offsetTable)));
    QCOMPARE(tables->children.size(), size_t(2));
    QCOMPARE(tables->children[0]->name, QStringLiteral("[0]head"));
    QCOMPARE(tables->children[1]->name, QStringLiteral("[1]name"));

    StructureRow *headTag = findChildNamed(tables->children[0].get(), QStringLiteral("dword tag"));
    QVERIFY2(headTag, qPrintable(childNames(tables->children[0].get())));
    QCOMPARE(headTag->value, QStringLiteral("\"head\""));
    QCOMPARE(findChildNamed(tables->children[0].get(), QStringLiteral("dword offset"))->value, QStringLiteral("44"));
    QCOMPARE(findChildNamed(tables->children[1].get(), QStringLiteral("dword length"))->value, QStringLiteral("6"));

    StructureRow *semantic = findChildNamed(rows[0].get(), QStringLiteral("SFNT Data"));
    QVERIFY2(semantic, qPrintable(childNames(rows[0].get())));
    StructureRow *fontTables = findChildNamed(semantic, QStringLiteral("FontTables"));
    QVERIFY2(fontTables, qPrintable(childNames(semantic)));
    QCOMPARE(fontTables->children.size(), size_t(2));
    QCOMPARE(fontTables->children[0]->name, QStringLiteral("head"));
    QCOMPARE(fontTables->children[0]->offset, QStringLiteral("0000002C"));
    QCOMPARE(fontTables->children[0]->children.size(), size_t(4));
    QCOMPARE(fontTables->children[1]->name, QStringLiteral("name"));
    QCOMPARE(fontTables->children[1]->offset, QStringLiteral("00000030"));
    QCOMPARE(fontTables->children[1]->children.size(), size_t(6));
}

void StructViewTests::builderRendersPngChunks()
{
    // Scenario: PNG is a big-endian signature plus a stream of length/type/data/CRC chunks.
    // Expected: the standard PNG definition names chunks by type and expands common
    // typed payloads such as IHDR while leaving compressed image data raw.
    StrataLibrary library;
    QVERIFY2(parseStandardDefinition(&library, QStringLiteral("png.strata")), "png.strata failed to parse");
    TypeDecl *pngRoot = exportedNamed(&library, QStringLiteral("PNG"));
    QVERIFY(pngRoot);

    QByteArray png;
    const auto appendBe32 = [&png](quint32 value) {
        png.append(char((value >> 24) & 0xff));
        png.append(char((value >> 16) & 0xff));
        png.append(char((value >> 8) & 0xff));
        png.append(char(value & 0xff));
    };

    png.append(QByteArray::fromHex("89504e470d0a1a0a")); // signature
    appendBe32(13);
    png.append("IHDR", 4);
    appendBe32(1);
    appendBe32(2);
    png.append(char(8));  // bit depth
    png.append(char(2));  // truecolor
    png.append(char(0));  // compression
    png.append(char(0));  // filter
    png.append(char(1));  // Adam7
    appendBe32(0x12345678);
    appendBe32(2);
    png.append("IDAT", 4);
    png.append(QByteArray::fromHex("789c"));
    appendBe32(0x90abcdef);
    appendBe32(0);
    png.append("IEND", 4);
    appendBe32(0xae426082);

    auto rows = buildRows(&library, pngRoot, png);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->name, QStringLiteral("PNG"));

    StructureRow *chunks = findChildNamed(rows[0].get(), QStringLiteral("PNG_CHUNK chunks[]"));
    QVERIFY2(chunks, qPrintable(childNames(rows[0].get())));
    QCOMPARE(chunks->children.size(), size_t(3));
    QCOMPARE(chunks->children[0]->name, QStringLiteral("[0]PNG_CHUNK_IHDR"));
    QCOMPARE(chunks->children[1]->name, QStringLiteral("[1]PNG_CHUNK_IDAT"));
    QCOMPARE(chunks->children[2]->name, QStringLiteral("[2]PNG_CHUNK_IEND"));

    StructureRow *ihdr = findChildNamed(chunks->children[0].get(), QStringLiteral("PNG_IHDR ihdr"));
    QVERIFY2(ihdr, qPrintable(childNames(chunks->children[0].get())));
    QCOMPARE(findChildNamed(ihdr, QStringLiteral("dword width"))->value, QStringLiteral("1"));
    QCOMPARE(findChildNamed(ihdr, QStringLiteral("dword height"))->value, QStringLiteral("2"));
    QCOMPARE(findChildNamed(ihdr, QStringLiteral("byte colorType"))->value, QStringLiteral("PNG_COLOR_TRUECOLOR"));
    QCOMPARE(findChildNamed(ihdr, QStringLiteral("byte interlaceMethod"))->value, QStringLiteral("PNG_INTERLACE_ADAM7"));

    StructureRow *idat = findChildNamed(chunks->children[1].get(), QStringLiteral("PNG_RAW_CHUNK_DATA raw"));
    QVERIFY2(idat, qPrintable(childNames(chunks->children[1].get())));
    QCOMPARE(findChildNamed(idat, QStringLiteral("byte data[]"))->value, QStringLiteral("{ 120, 156 }"));
}

void StructViewTests::builderRendersBmpHeaderAndPixelPayload()
{
    // Scenario: BMP is a little-endian file header followed by a DIB header
    // whose first field selects the concrete header variant.
    // Expected: the standard BMP definition expands the selected DIB header and
    // exposes the pixel payload at bfOffBits as dynamic data.
    StrataLibrary library;
    QVERIFY2(parseStandardDefinition(&library, QStringLiteral("bmp.strata")), "bmp.strata failed to parse");
    TypeDecl *bmpRoot = exportedNamed(&library, QStringLiteral("BMP"));
    QVERIFY(bmpRoot);

    QByteArray bmp(58, '\0');
    bmp[0] = 'B';
    bmp[1] = 'M';
    writeLe32(&bmp, 2, quint32(bmp.size()));
    writeLe32(&bmp, 10, 54);
    writeLe32(&bmp, 14, 40); // BITMAPINFOHEADER
    writeLe32(&bmp, 18, 1);  // width
    writeLe32(&bmp, 22, 1);  // height
    writeLe16(&bmp, 26, 1);  // planes
    writeLe16(&bmp, 28, 32); // bit count
    writeLe32(&bmp, 30, 0);  // BI_RGB
    bmp[54] = char(0x11);
    bmp[55] = char(0x22);
    bmp[56] = char(0x33);
    bmp[57] = char(0x44);

    auto rows = buildRows(&library, bmpRoot, bmp);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->name, QStringLiteral("BMP"));

    StructureRow *fileHeader = findChildNamed(rows[0].get(), QStringLiteral("BMP_FILE_HEADER fileHeader"));
    QVERIFY2(fileHeader, qPrintable(childNames(rows[0].get())));
    QCOMPARE(findChildNamed(fileHeader, QStringLiteral("word type"))->value, QStringLiteral("BMP_FILE_BM"));
    QCOMPARE(findChildNamed(fileHeader, QStringLiteral("dword pixelArrayOffset"))->value, QStringLiteral("54"));

    StructureRow *dibHeaderSize = findChildNamed(rows[0].get(), QStringLiteral("dword dibHeaderSize"));
    QVERIFY2(dibHeaderSize, qPrintable(childNames(rows[0].get())));
    QCOMPARE(dibHeaderSize->value, QStringLiteral("BMP_DIB_INFO"));

    StructureRow *infoHeader = findChildNamed(rows[0].get(), QStringLiteral("BMP_INFO_HEADER infoHeader"));
    QVERIFY2(infoHeader, qPrintable(childNames(rows[0].get())));
    QCOMPARE(findChildNamed(infoHeader, QStringLiteral("long width"))->value, QStringLiteral("1"));
    QCOMPARE(findChildNamed(infoHeader, QStringLiteral("word bitCount"))->value, QStringLiteral("32"));
    QCOMPARE(findChildNamed(infoHeader, QStringLiteral("dword compression"))->value, QStringLiteral("BMP_COMPRESSION_RGB"));

    StructureRow *pixelOffset = findChildNamed(fileHeader, QStringLiteral("dword pixelArrayOffset"));
    QVERIFY2(pixelOffset, qPrintable(childNames(fileHeader)));
    StructureRow *pixelData = findChildNamed(pixelOffset, QStringLiteral("BYTE PixelData[]"));
    QVERIFY2(pixelData, qPrintable(childNames(pixelOffset)));
    QCOMPARE(pixelData->offset, QStringLiteral("00000036"));
    QCOMPARE(pixelData->children.size(), size_t(4));
    QCOMPARE(pixelData->children[0]->value, QStringLiteral("17"));
    QCOMPARE(pixelData->children[3]->value, QStringLiteral("68"));
}

void StructViewTests::builderRendersIcoDirectoryAndImagePayload()
{
    // Scenario: ICO/CUR files contain a directory of image records whose
    // payloads live elsewhere in the file.
    // Expected: the standard ICO definition renders directory entries and
    // emits each bounded image payload under ICO Data/Images.
    StrataLibrary library;
    QVERIFY2(parseStandardDefinition(&library, QStringLiteral("ico.strata")), "ico.strata failed to parse");
    TypeDecl *icoRoot = exportedNamed(&library, QStringLiteral("ICO"));
    QVERIFY(icoRoot);

    QByteArray ico(26, '\0');
    writeLe16(&ico, 0, 0);
    writeLe16(&ico, 2, 1);
    writeLe16(&ico, 4, 1);
    ico[6] = char(16); // width
    ico[7] = char(16); // height
    writeLe16(&ico, 10, 1);
    writeLe16(&ico, 12, 32);
    writeLe32(&ico, 14, 4);
    writeLe32(&ico, 18, 22);
    ico[22] = char(0x89);
    ico[23] = 'P';
    ico[24] = 'N';
    ico[25] = 'G';

    auto rows = buildRows(&library, icoRoot, ico);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->name, QStringLiteral("ICO"));
    QCOMPARE(findChildNamed(rows[0].get(), QStringLiteral("word type"))->value, QStringLiteral("ICO_TYPE_ICON"));

    StructureRow *entries = findChildNamed(rows[0].get(), QStringLiteral("ICO_DIRECTORY_ENTRY entries[]"));
    QVERIFY2(entries, qPrintable(childNames(rows[0].get())));
    QCOMPARE(entries->children.size(), size_t(1));

    StructureRow *entry = entries->children[0].get();
    QCOMPARE(findChildNamed(entry, QStringLiteral("byte width"))->value, QStringLiteral("16"));
    QCOMPARE(findChildNamed(entry, QStringLiteral("dword bytesInResource"))->value, QStringLiteral("4"));
    QCOMPARE(findChildNamed(entry, QStringLiteral("dword imageOffset"))->value, QStringLiteral("22"));

    StructureRow *imageOffset = findChildNamed(entry, QStringLiteral("dword imageOffset"));
    QVERIFY2(imageOffset, qPrintable(childNames(entry)));
    QVERIFY(imageOffset->children.empty());

    StructureRow *semantic = findChildNamed(rows[0].get(), QStringLiteral("ICO Data"));
    QVERIFY2(semantic, qPrintable(childNames(rows[0].get())));
    StructureRow *images = findChildNamed(semantic, QStringLiteral("Images"));
    QVERIFY2(images, qPrintable(childNames(semantic)));
    QCOMPARE(images->children.size(), size_t(1));
    StructureRow *imageData = images->children[0].get();
    QCOMPARE(imageData->name, QStringLiteral("22"));
    QCOMPARE(imageData->offset, QStringLiteral("00000016"));
    QCOMPARE(imageData->children.size(), size_t(4));
    QCOMPARE(imageData->children[0]->value, QStringLiteral("137"));
    QCOMPARE(imageData->children[3]->value, QStringLiteral("71"));
}

void StructViewTests::builderRendersGifBlocks()
{
    // Scenario: GIF is a little-endian header, logical screen descriptor,
    // optional color table, and a sentinel-tagged stream of extensions/images.
    // Expected: the standard GIF definition expands color tables, extension
    // blocks, image descriptors, data sub-blocks, and the trailer.
    StrataLibrary library;
    QVERIFY2(parseStandardDefinition(&library, QStringLiteral("gif.strata")), "gif.strata failed to parse");
    TypeDecl *gifRoot = exportedNamed(&library, QStringLiteral("GIF"));
    QVERIFY(gifRoot);

    QByteArray gif;
    gif.append("GIF89a", 6);
    gif.append(char(1));
    gif.append(char(0));
    gif.append(char(1));
    gif.append(char(0));
    gif.append(char(0x80)); // global color table present, 2 entries
    gif.append(char(0));
    gif.append(char(0));
    gif.append(QByteArray::fromHex("000000ffffff"));
    gif.append(char(0x21)); // extension
    gif.append(char(0xf9)); // graphic control
    gif.append(char(4));
    gif.append(char(1));
    gif.append(char(5));
    gif.append(char(0));
    gif.append(char(0));
    gif.append(char(0));
    gif.append(char(0x2c)); // image
    gif.append(char(0));
    gif.append(char(0));
    gif.append(char(0));
    gif.append(char(0));
    gif.append(char(1));
    gif.append(char(0));
    gif.append(char(1));
    gif.append(char(0));
    gif.append(char(0)); // no local color table
    gif.append(char(2)); // LZW minimum code size
    gif.append(char(2));
    gif.append(char(0x4c));
    gif.append(char(0x01));
    gif.append(char(0)); // sub-block terminator
    gif.append(char(0x3b)); // trailer

    auto rows = buildRows(&library, gifRoot, gif);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->name, QStringLiteral("GIF"));

    StructureRow *header = findChildNamed(rows[0].get(), QStringLiteral("GIF_HEADER header"));
    QVERIFY2(header, qPrintable(childNames(rows[0].get())));
    StructureRow *gifSignature = findChildNamed(header, QStringLiteral("char signature[]"));
    QVERIFY2(gifSignature, qPrintable(childNames(header)));
    QCOMPARE(gifSignature->value, QStringLiteral("\"GIF\""));
    StructureRow *gifVersion = findChildNamed(header, QStringLiteral("char version[]"));
    QVERIFY2(gifVersion, qPrintable(childNames(header)));
    QCOMPARE(gifVersion->value, QStringLiteral("\"89a\""));

    StructureRow *globalColorTable = findChildNamed(rows[0].get(), QStringLiteral("GIF_COLOR_TABLE_ENTRY globalColorTable[]"));
    QVERIFY2(globalColorTable, qPrintable(childNames(rows[0].get())));
    QCOMPARE(globalColorTable->children.size(), size_t(2));

    StructureRow *blocks = findChildNamed(rows[0].get(), QStringLiteral("GIF_BLOCK blocks[]"));
    QVERIFY2(blocks, qPrintable(childNames(rows[0].get())));
    QCOMPARE(blocks->children.size(), size_t(2));
    QCOMPARE(blocks->children[0]->name, QStringLiteral("[0]"));
    StructureRow *extensionBlockType = findChildNamed(blocks->children[0].get(), QStringLiteral("byte blockType"));
    QVERIFY2(extensionBlockType, qPrintable(childNames(blocks->children[0].get())));
    QCOMPARE(extensionBlockType->value, QStringLiteral("GIF_BLOCK_EXTENSION"));
    StructureRow *extensionLabel = findDescendantNamed(blocks->children[0].get(), QStringLiteral("byte label"));
    QVERIFY2(extensionLabel, qPrintable(childNames(blocks->children[0].get())));
    QCOMPARE(extensionLabel->value, QStringLiteral("GIF_EXTENSION_GRAPHIC_CONTROL"));

    StructureRow *image = findDescendantNamed(blocks->children[1].get(), QStringLiteral("GIF_IMAGE_DESCRIPTOR image"));
    QVERIFY2(image, qPrintable(childNames(blocks->children[1].get())));
    StructureRow *lzwMinimumCodeSize = findChildNamed(image, QStringLiteral("byte lzwMinimumCodeSize"));
    QVERIFY2(lzwMinimumCodeSize, qPrintable(childNames(image)));
    QCOMPARE(lzwMinimumCodeSize->value, QStringLiteral("2"));
    StructureRow *subBlocks = findDescendantNamed(image, QStringLiteral("GIF_DATA_SUB_BLOCK blocks[]"));
    QVERIFY2(subBlocks, qPrintable(childNames(image)));
    QCOMPARE(subBlocks->children.size(), size_t(2));
    StructureRow *imageData = findChildNamed(subBlocks->children[0].get(), QStringLiteral("byte data[]"));
    QVERIFY2(imageData, qPrintable(childNames(subBlocks->children[0].get())));
    QCOMPARE(imageData->value, QStringLiteral("{ 76, 1 }"));
    QCOMPARE(findChildNamed(subBlocks->children[1].get(), QStringLiteral("byte size"))->value,
             QStringLiteral("0"));
    StructureRow *trailer = findChildNamed(rows[0].get(), QStringLiteral("byte trailer"));
    QVERIFY2(trailer, qPrintable(childNames(rows[0].get())));
    QCOMPARE(trailer->value, QStringLiteral("GIF_BLOCK_TRAILER"));
}

void StructViewTests::builderRendersWoffDirectoryAndPayloads()
{
    // Scenario: WOFF 1.0 is a big-endian wrapper around sfnt table data with a
    // fixed header and table directory.
    // Expected: the standard WOFF definition renders the directory entries by
    // tag, emits compressed tables under WOFF Data/FontTables, and still exposes
    // metadata payloads as dynamic bytes.
    StrataLibrary library;
    QVERIFY2(parseStandardDefinition(&library, QStringLiteral("woff.strata")), "woff.strata failed to parse");
    TypeDecl *woffRoot = exportedNamed(&library, QStringLiteral("WOFF"));
    QVERIFY(woffRoot);

    QByteArray woff(68, '\0');
    woff.replace(0, 4, "wOFF");
    writeBe32(&woff, 4, 0x00010000); // TrueType flavor
    writeBe32(&woff, 8, quint32(woff.size()));
    writeBe16(&woff, 12, 1);
    writeBe16(&woff, 14, 0);
    writeBe32(&woff, 16, 32); // reconstructed sfnt size
    writeBe16(&woff, 20, 1);
    writeBe16(&woff, 22, 0);
    writeBe32(&woff, 24, 64);
    writeBe32(&woff, 28, 4);
    writeBe32(&woff, 32, 8);
    writeBe32(&woff, 36, 0);
    writeBe32(&woff, 40, 0);
    woff.replace(44, 4, "name");
    writeBe32(&woff, 48, 64);
    writeBe32(&woff, 52, 4);
    writeBe32(&woff, 56, 8);
    writeBe32(&woff, 60, 0x12345678);
    woff.replace(64, 4, QByteArray::fromHex("01020304"));

    auto rows = buildRows(&library, woffRoot, woff);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->name, QStringLiteral("WOFF"));

    StructureRow *header = findChildNamed(rows[0].get(), QStringLiteral("WOFF_HEADER header"));
    QVERIFY2(header, qPrintable(childNames(rows[0].get())));
    StructureRow *signature = findChildNamed(header, QStringLiteral("char signature[]"));
    QVERIFY2(signature, qPrintable(childNames(header)));
    QCOMPARE(signature->value, QStringLiteral("\"wOFF\""));
    StructureRow *flavor = findChildNamed(header, QStringLiteral("dword flavor"));
    QVERIFY2(flavor, qPrintable(childNames(header)));
    QCOMPARE(flavor->value, QStringLiteral("WOFF_SFNT_TRUETYPE_1_0"));
    StructureRow *numTables = findChildNamed(header, QStringLiteral("word numTables"));
    QVERIFY2(numTables, qPrintable(childNames(header)));
    QCOMPARE(numTables->value, QStringLiteral("1"));

    StructureRow *tables = findChildNamed(rows[0].get(), QStringLiteral("WOFF_TABLE_DIRECTORY_ENTRY tables[]"));
    QVERIFY2(tables, qPrintable(childNames(rows[0].get())));
    QCOMPARE(tables->children.size(), size_t(1));
    QCOMPARE(tables->children[0]->name, QStringLiteral("[0]name"));
    StructureRow *tag = findChildNamed(tables->children[0].get(), QStringLiteral("dword tag"));
    QVERIFY2(tag, qPrintable(childNames(tables->children[0].get())));
    QCOMPARE(tag->value, QStringLiteral("\"name\""));

    StructureRow *tableOffset = findChildNamed(tables->children[0].get(), QStringLiteral("dword offset"));
    QVERIFY2(tableOffset, qPrintable(childNames(tables->children[0].get())));
    QVERIFY(tableOffset->children.empty());

    StructureRow *semantic = findChildNamed(rows[0].get(), QStringLiteral("WOFF Data"));
    QVERIFY2(semantic, qPrintable(childNames(rows[0].get())));
    StructureRow *fontTables = findChildNamed(semantic, QStringLiteral("FontTables"));
    QVERIFY2(fontTables, qPrintable(childNames(semantic)));
    QCOMPARE(fontTables->children.size(), size_t(1));
    StructureRow *tableData = fontTables->children[0].get();
    QCOMPARE(tableData->name, QStringLiteral("name"));
    QCOMPARE(tableData->offset, QStringLiteral("00000040"));
    QCOMPARE(tableData->children.size(), size_t(4));

    StructureRow *metaOffset = findChildNamed(header, QStringLiteral("dword metaOffset"));
    QVERIFY2(metaOffset, qPrintable(childNames(header)));
    StructureRow *metadata = findChildNamed(metaOffset, QStringLiteral("BYTE Metadata[]"));
    QVERIFY2(metadata, qPrintable(childNames(metaOffset)));
    QCOMPARE(metadata->offset, QStringLiteral("00000040"));
    QCOMPARE(metadata->children[0]->value, QStringLiteral("1"));
}

void StructViewTests::builderRendersMp4Boxes()
{
    // Scenario: MP4/ISO BMFF files are big-endian FourCC box streams.
    // Expected: the standard MP4 definition labels boxes by type, decodes ftyp,
    // and keeps media/container payloads bounded.
    StrataLibrary library;
    QVERIFY2(parseStandardDefinition(&library, QStringLiteral("mp4.strata")), "mp4.strata failed to parse");
    TypeDecl *mp4Root = exportedNamed(&library, QStringLiteral("MP4"));
    QVERIFY(mp4Root);

    QByteArray mp4;
    const auto appendBe32 = [&mp4](quint32 value) {
        mp4.append(char((value >> 24) & 0xff));
        mp4.append(char((value >> 16) & 0xff));
        mp4.append(char((value >> 8) & 0xff));
        mp4.append(char(value & 0xff));
    };

    appendBe32(24);
    mp4.append("ftyp", 4);
    mp4.append("isom", 4);
    appendBe32(0x00000200);
    mp4.append("isom", 4);
    mp4.append("mp42", 4);

    appendBe32(116);
    mp4.append("moov", 4);
    appendBe32(108);
    mp4.append("mvhd", 4);
    mp4.append(char(0)); // version
    mp4.append(QByteArray(3, '\0'));
    appendBe32(1);       // creation_time
    appendBe32(2);       // modification_time
    appendBe32(1000);    // timescale
    appendBe32(5000);    // duration
    appendBe32(0x00010000);
    mp4.append(char(1));
    mp4.append(char(0));
    mp4.append(QByteArray(10, '\0'));
    appendBe32(0x00010000);
    appendBe32(0);
    appendBe32(0);
    appendBe32(0);
    appendBe32(0x00010000);
    appendBe32(0);
    appendBe32(0);
    appendBe32(0);
    appendBe32(0x40000000);
    mp4.append(QByteArray(24, '\0'));
    appendBe32(2);

    appendBe32(12);
    mp4.append("mdat", 4);
    mp4.append(QByteArray::fromHex("01020304"));

    auto rows = buildRows(&library, mp4Root, mp4);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->name, QStringLiteral("MP4"));

    StructureRow *boxes = findChildNamed(rows[0].get(), QStringLiteral("MP4_BOX boxes[]"));
    QVERIFY2(boxes, qPrintable(childNames(rows[0].get())));
    QCOMPARE(boxes->children.size(), size_t(3));
    QCOMPARE(boxes->children[0]->name, QStringLiteral("[0]ftyp"));
    QCOMPARE(boxes->children[1]->name, QStringLiteral("[1]moov"));
    QCOMPARE(boxes->children[2]->name, QStringLiteral("[2]mdat"));

    StructureRow *fileType = findDescendantNamed(boxes->children[0].get(), QStringLiteral("MP4_FILE_TYPE_BOX fileType"));
    QVERIFY2(fileType, qPrintable(childNames(boxes->children[0].get())));
    StructureRow *majorBrand = findChildNamed(fileType, QStringLiteral("char majorBrand[]"));
    QVERIFY2(majorBrand, qPrintable(childNames(fileType)));
    QCOMPARE(majorBrand->value, QStringLiteral("\"isom\""));
    StructureRow *compatibleBrands = findChildNamed(fileType, QStringLiteral("char compatibleBrands[][]"));
    QVERIFY2(compatibleBrands, qPrintable(childNames(fileType)));
    QCOMPARE(compatibleBrands->children.size(), size_t(2));

    StructureRow *moviePayload = findDescendantNamed(boxes->children[1].get(), QStringLiteral("MP4_RAW_BOX_PAYLOAD raw"));
    QVERIFY2(moviePayload, qPrintable(childNames(boxes->children[1].get())));
    StructureRow *movieData = findChildNamed(moviePayload, QStringLiteral("byte data[]"));
    QVERIFY2(movieData, qPrintable(childNames(moviePayload)));
    QCOMPARE(movieData->children.size(), size_t(100));

    StructureRow *mediaData = findDescendantNamed(boxes->children[2].get(), QStringLiteral("MP4_MEDIA_DATA_BOX mediaData"));
    QVERIFY2(mediaData, qPrintable(childNames(boxes->children[2].get())));
    StructureRow *data = findChildNamed(mediaData, QStringLiteral("byte data[]"));
    QVERIFY2(data, qPrintable(childNames(mediaData)));
    QCOMPARE(data->value, QStringLiteral("{ 1, 2, 3, 4 }"));
}

void StructViewTests::builderRendersRiffWaveAndWebpChunks()
{
    // Scenario: RIFF-family files share a chunk stream but use their form type
    // and chunk FourCCs to interpret payloads.
    // Expected: WAVE and WebP roots render named chunks, typed known payloads,
    // and RIFF's even-byte payload padding without a synthetic _align field.
    StrataLibrary library;
    QVERIFY2(parseStandardDefinition(&library, QStringLiteral("riff.strata")), "riff.strata failed to parse");

    TypeDecl *waveRoot = exportedNamed(&library, QStringLiteral("WAVE"));
    QVERIFY(waveRoot);
    QByteArray wave(48, '\0');
    wave.replace(0, 4, "RIFF");
    writeLe32(&wave, 4, 40);
    wave.replace(8, 4, "WAVE");
    wave.replace(12, 4, "fmt ");
    writeLe32(&wave, 16, 16);
    writeLe16(&wave, 20, 1);
    writeLe16(&wave, 22, 2);
    writeLe32(&wave, 24, 44100);
    writeLe32(&wave, 28, 176400);
    writeLe16(&wave, 32, 4);
    writeLe16(&wave, 34, 16);
    wave.replace(36, 4, "data");
    writeLe32(&wave, 40, 3);
    wave[44] = char(0x01);
    wave[45] = char(0x02);
    wave[46] = char(0x03);

    auto waveRows = buildRows(&library, waveRoot, wave);
    QCOMPARE(waveRows.size(), size_t(1));
    QCOMPARE(waveRows[0]->name, QStringLiteral("WAVE"));

    StructureRow *waveFormType = findChildNamed(waveRows[0].get(), QStringLiteral("dword formType"));
    QVERIFY2(waveFormType, qPrintable(childNames(waveRows[0].get())));
    QCOMPARE(waveFormType->value, QStringLiteral("\"WAVE\""));

    StructureRow *waveChunks = findChildNamed(waveRows[0].get(), QStringLiteral("RIFF_CHUNK chunks[]"));
    QVERIFY2(waveChunks, qPrintable(childNames(waveRows[0].get())));
    QCOMPARE(waveChunks->children.size(), size_t(2));
    QCOMPARE(waveChunks->children[0]->name, QStringLiteral("[0]fmt "));
    QCOMPARE(waveChunks->children[1]->name, QStringLiteral("[1]data"));

    StructureRow *waveFormat = findDescendantNamed(waveChunks->children[0].get(), QStringLiteral("RIFF_WAVE_FORMAT_CHUNK waveFormat"));
    QVERIFY2(waveFormat, qPrintable(childNames(waveChunks->children[0].get())));
    StructureRow *audioFormat = findChildNamed(waveFormat, QStringLiteral("word audioFormat"));
    QVERIFY2(audioFormat, qPrintable(childNames(waveFormat)));
    QCOMPARE(audioFormat->value, QStringLiteral("RIFF_WAVE_FORMAT_PCM"));
    StructureRow *samplesPerSecond = findChildNamed(waveFormat, QStringLiteral("dword samplesPerSecond"));
    QVERIFY2(samplesPerSecond, qPrintable(childNames(waveFormat)));
    QCOMPARE(samplesPerSecond->value, QStringLiteral("44100"));

    StructureRow *waveDataRaw = findDescendantNamed(waveChunks->children[1].get(), QStringLiteral("RIFF_RAW_CHUNK_PAYLOAD raw"));
    QVERIFY2(waveDataRaw, qPrintable(childNames(waveChunks->children[1].get())));
    StructureRow *waveData = findChildNamed(waveDataRaw, QStringLiteral("byte data[]"));
    QVERIFY2(waveData, qPrintable(childNames(waveDataRaw)));
    QCOMPARE(waveData->value, QStringLiteral("{ 1, 2, 3 }"));

    TypeDecl *webpRoot = exportedNamed(&library, QStringLiteral("WEBP"));
    QVERIFY(webpRoot);
    QByteArray webp(42, '\0');
    webp.replace(0, 4, "RIFF");
    writeLe32(&webp, 4, 34);
    webp.replace(8, 4, "WEBP");
    webp.replace(12, 4, "VP8X");
    writeLe32(&webp, 16, 10);
    webp[20] = char(0x10);
    webp[24] = char(0x3f);
    webp[27] = char(0x1f);
    webp.replace(30, 4, "VP8 ");
    writeLe32(&webp, 34, 4);
    webp.replace(38, 4, QByteArray::fromHex("9d012a00"));

    auto webpRows = buildRows(&library, webpRoot, webp);
    QCOMPARE(webpRows.size(), size_t(1));
    QCOMPARE(webpRows[0]->name, QStringLiteral("WEBP"));

    StructureRow *webpFormType = findChildNamed(webpRows[0].get(), QStringLiteral("dword formType"));
    QVERIFY2(webpFormType, qPrintable(childNames(webpRows[0].get())));
    QCOMPARE(webpFormType->value, QStringLiteral("\"WEBP\""));

    StructureRow *webpChunks = findChildNamed(webpRows[0].get(), QStringLiteral("RIFF_CHUNK chunks[]"));
    QVERIFY2(webpChunks, qPrintable(childNames(webpRows[0].get())));
    QCOMPARE(webpChunks->children.size(), size_t(2));
    QCOMPARE(webpChunks->children[0]->name, QStringLiteral("[0]VP8X"));
    QCOMPARE(webpChunks->children[1]->name, QStringLiteral("[1]VP8 "));

    StructureRow *vp8x = findDescendantNamed(webpChunks->children[0].get(), QStringLiteral("WEBP_VP8X_CHUNK vp8x"));
    QVERIFY2(vp8x, qPrintable(childNames(webpChunks->children[0].get())));
    StructureRow *flags = findChildNamed(vp8x, QStringLiteral("byte flags"));
    QVERIFY2(flags, qPrintable(childNames(vp8x)));
    QCOMPARE(flags->value, QStringLiteral("16"));

    StructureRow *vp8Raw = findDescendantNamed(webpChunks->children[1].get(), QStringLiteral("RIFF_RAW_CHUNK_PAYLOAD raw"));
    QVERIFY2(vp8Raw, qPrintable(childNames(webpChunks->children[1].get())));
    StructureRow *vp8Data = findChildNamed(vp8Raw, QStringLiteral("byte data[]"));
    QVERIFY2(vp8Data, qPrintable(childNames(vp8Raw)));
    QCOMPARE(vp8Data->value, QStringLiteral("{ 157, 1, 42, 0 }"));
}

void StructViewTests::builderRendersTarEntries()
{
    // Scenario: TAR stores records as 512-byte headers followed by payloads
    // whose size is encoded as ASCII octal.
    // Expected: the standard TAR definition names entries from the header,
    // converts the octal size field, and advances over the padded payload.
    StrataLibrary library;
    QVERIFY2(parseStandardDefinition(&library, QStringLiteral("tar.strata")), "tar.strata failed to parse");
    TypeDecl *tarRoot = exportedNamed(&library, QStringLiteral("TAR"));
    QVERIFY(tarRoot);

    QByteArray tar(2048, '\0');
    writeAscii(&tar, 0, "hello.txt");
    writeAscii(&tar, 100, "0000644");
    writeAscii(&tar, 108, "0000000");
    writeAscii(&tar, 116, "0000000");
    writeAscii(&tar, 124, "00000000005");
    writeAscii(&tar, 136, "00000000000");
    writeAscii(&tar, 148, "        ");
    tar[156] = char('0');
    writeAscii(&tar, 257, "ustar");
    writeAscii(&tar, 263, "00");
    tar.replace(512, 5, "hello");

    auto rows = buildRows(&library, tarRoot, tar);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->name, QStringLiteral("TAR"));

    StructureRow *entries = findChildNamed(rows[0].get(), QStringLiteral("TAR_ENTRY entries[]"));
    QVERIFY2(entries, qPrintable(childNames(rows[0].get())));
    QCOMPARE(entries->children.size(), size_t(2));
    QCOMPARE(entries->children[0]->name, QStringLiteral("[0]hello.txt"));
    QCOMPARE(entries->children[1]->name, QStringLiteral("[1]"));

    StructureRow *header = findChildNamed(entries->children[0].get(), QStringLiteral("TAR_HEADER header"));
    QVERIFY2(header, qPrintable(childNames(entries->children[0].get())));
    StructureRow *size = findChildNamed(header, QStringLiteral("char size[]"));
    QVERIFY2(size, qPrintable(childNames(header)));
    QCOMPARE(size->value, QStringLiteral("\"00000000005\""));

    StructureRow *data = findChildNamed(entries->children[0].get(), QStringLiteral("byte data[]"));
    QVERIFY2(data, qPrintable(childNames(entries->children[0].get())));
    QCOMPARE(data->offset, QStringLiteral("00000200"));
    QCOMPARE(data->value, QStringLiteral("{ 104, 101, 108, 108, 111 }"));
}

void StructViewTests::builderRendersGzipHeaderAndTrailer()
{
    // Scenario: GZip has a fixed header followed by optional NUL-terminated
    // filename/comment fields, compressed bytes, and a fixed trailer.
    // Expected: the standard GZip definition renders the optional strings and
    // bounds compressedData before the trailer.
    StrataLibrary library;
    QVERIFY2(parseStandardDefinition(&library, QStringLiteral("gzip.strata")), "gzip.strata failed to parse");
    TypeDecl *gzipRoot = exportedNamed(&library, QStringLiteral("GZIP"));
    QVERIFY(gzipRoot);

    QByteArray gzip;
    gzip.append(char(0x1f));
    gzip.append(char(0x8b));
    gzip.append(char(8));    // deflate
    gzip.append(char(0x18)); // FNAME | FCOMMENT
    gzip.append(QByteArray::fromHex("00000000"));
    gzip.append(char(0));
    gzip.append(char(3));    // Unix
    gzip.append("hello.txt", 9);
    gzip.append(char(0));
    gzip.append("comment", 7);
    gzip.append(char(0));
    gzip.append(QByteArray::fromHex("01020304"));
    gzip.append(QByteArray::fromHex("78563412"));
    gzip.append(QByteArray::fromHex("05000000"));

    auto rows = buildRows(&library, gzipRoot, gzip);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->name, QStringLiteral("GZIP"));

    StructureRow *header = findChildNamed(rows[0].get(), QStringLiteral("GZIP_HEADER header"));
    QVERIFY2(header, qPrintable(childNames(rows[0].get())));
    QCOMPARE(findChildNamed(header, QStringLiteral("byte compressionMethod"))->value,
             QStringLiteral("GZIP_COMPRESSION_DEFLATE"));
    QCOMPARE(findChildNamed(header, QStringLiteral("byte operatingSystem"))->value,
             QStringLiteral("GZIP_OS_UNIX"));
    StructureRow *originalName = findChildNamed(header, QStringLiteral("char originalName[]"));
    QVERIFY2(originalName, qPrintable(childNames(header)));
    QCOMPARE(originalName->value, QStringLiteral("\"hello.txt\""));
    StructureRow *comment = findChildNamed(header, QStringLiteral("char comment[]"));
    QVERIFY2(comment, qPrintable(childNames(header)));
    QCOMPARE(comment->value, QStringLiteral("\"comment\""));

    StructureRow *compressedData = findChildNamed(rows[0].get(), QStringLiteral("byte compressedData[]"));
    QVERIFY2(compressedData, qPrintable(childNames(rows[0].get())));
    QCOMPARE(compressedData->value, QStringLiteral("{ 1, 2, 3, 4 }"));

    StructureRow *trailer = findChildNamed(rows[0].get(), QStringLiteral("GZIP_TRAILER trailer"));
    QVERIFY2(trailer, qPrintable(childNames(rows[0].get())));
    QCOMPARE(findChildNamed(trailer, QStringLiteral("dword crc32"))->value, QStringLiteral("305419896"));
    QCOMPARE(findChildNamed(trailer, QStringLiteral("dword inputSize"))->value, QStringLiteral("5"));
}

void StructViewTests::builderRendersCabinetHeaderFilesAndData()
{
    // Scenario: CAB files contain a fixed header, folder table, file table, and
    // compressed data blocks referenced by each folder.
    // Expected: the standard CAB definition renders the folder's data blocks at
    // their target offset and names files from CFFILE.fileName.
    StrataLibrary library;
    QVERIFY2(parseStandardDefinition(&library, QStringLiteral("cab.strata")), "cab.strata failed to parse");
    TypeDecl *cabRoot = exportedNamed(&library, QStringLiteral("CAB"));
    QVERIFY(cabRoot);

    QByteArray cab(83, '\0');
    cab.replace(0, 4, "MSCF");
    writeLe32(&cab, 8, quint32(cab.size()));
    writeLe32(&cab, 16, 44); // filesOffset
    cab[24] = char(3);       // versionMinor
    cab[25] = char(1);       // versionMajor
    writeLe16(&cab, 26, 1);  // folderCount
    writeLe16(&cab, 28, 1);  // fileCount
    writeLe16(&cab, 32, 7);  // setId

    writeLe32(&cab, 36, 70); // firstDataBlockOffset
    writeLe16(&cab, 40, 1);  // dataBlockCount
    writeLe16(&cab, 42, 0);  // no compression

    writeLe32(&cab, 44, 5);  // uncompressedSize
    writeLe32(&cab, 48, 0);  // uncompressedFolderOffset
    writeLe16(&cab, 52, 0);  // folderIndex
    writeLe16(&cab, 58, 0x0020); // archive attribute
    writeAscii(&cab, 60, "hello.txt");

    writeLe32(&cab, 70, 0);  // checksum
    writeLe16(&cab, 74, 5);  // compressedSize
    writeLe16(&cab, 76, 5);  // uncompressedSize
    cab.replace(78, 5, "hello");

    auto rows = buildRows(&library, cabRoot, cab);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->name, QStringLiteral("CAB"));

    StructureRow *header = findChildNamed(rows[0].get(), QStringLiteral("CAB_CFHEADER header"));
    QVERIFY2(header, qPrintable(childNames(rows[0].get())));
    QCOMPARE(findChildNamed(header, QStringLiteral("dword signature"))->value,
             QStringLiteral("CAB_SIGNATURE_MSCF"));
    QCOMPARE(findChildNamed(header, QStringLiteral("word folderCount"))->value, QStringLiteral("1"));
    QCOMPARE(findChildNamed(header, QStringLiteral("word fileCount"))->value, QStringLiteral("1"));

    StructureRow *folders = findChildNamed(rows[0].get(), QStringLiteral("CAB_CFFOLDER folders[]"));
    QVERIFY2(folders, qPrintable(childNames(rows[0].get())));
    QCOMPARE(folders->children.size(), size_t(1));
    StructureRow *dataBlocks = findChildNamed(folders->children[0].get(), QStringLiteral("CAB_CFDATA dataBlocks[]"));
    QVERIFY2(dataBlocks, qPrintable(childNames(folders->children[0].get())));
    QCOMPARE(dataBlocks->children.size(), size_t(1));
    QCOMPARE(dataBlocks->children[0]->offset, QStringLiteral("00000046"));
    StructureRow *compressedData = findChildNamed(dataBlocks->children[0].get(), QStringLiteral("byte compressedData[]"));
    QVERIFY2(compressedData, qPrintable(childNames(dataBlocks->children[0].get())));
    QCOMPARE(compressedData->value, QStringLiteral("{ 104, 101, 108, 108, 111 }"));

    StructureRow *files = findChildNamed(rows[0].get(), QStringLiteral("CAB_CFFILE files[]"));
    QVERIFY2(files, qPrintable(childNames(rows[0].get())));
    QCOMPARE(files->children.size(), size_t(1));
    QCOMPARE(files->children[0]->name, QStringLiteral("[0]hello.txt"));
    StructureRow *fileName = findChildNamed(files->children[0].get(), QStringLiteral("char fileName[]"));
    QVERIFY2(fileName, qPrintable(childNames(files->children[0].get())));
    QCOMPARE(fileName->value, QStringLiteral("\"hello.txt\""));
}

void StructViewTests::builderRendersRawImgMbrPartitions()
{
    // Scenario: raw disk images commonly start with an MBR partition table.
    // Expected: the standard RAWIMG definition renders typed partition entries
    // and maps each non-empty partition payload to firstLba * 512.
    StrataLibrary library;
    QVERIFY2(parseStandardDefinition(&library, QStringLiteral("rawimg.strata")), "rawimg.strata failed to parse");
    TypeDecl *rawImgRoot = exportedNamed(&library, QStringLiteral("RAWIMG"));
    QVERIFY(rawImgRoot);

    QByteArray image(4096, '\0');
    writeLe32(&image, 440, 0x12345678);

    constexpr qsizetype firstPartition = 446;
    image[firstPartition + 0] = char(0x80);
    image[firstPartition + 1] = char(0x01);
    image[firstPartition + 2] = char(0x01);
    image[firstPartition + 4] = char(0x0c);
    image[firstPartition + 5] = char(0xfe);
    image[firstPartition + 6] = char(0xff);
    image[firstPartition + 7] = char(0xff);
    writeLe32(&image, firstPartition + 8, 1);
    writeLe32(&image, firstPartition + 12, 2);

    constexpr qsizetype secondPartition = firstPartition + 16;
    image[secondPartition + 4] = char(0x83);
    writeLe32(&image, secondPartition + 8, 3);
    writeLe32(&image, secondPartition + 12, 1);

    writeLe16(&image, 510, 0xaa55);
    image[512] = char(0xaa);
    image[513] = char(0xbb);
    image[1536] = char(0xcc);

    auto rows = buildRows(&library, rawImgRoot, image);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->name, QStringLiteral("RAWIMG"));

    StructureRow *mbr = findChildNamed(rows[0].get(), QStringLiteral("RAWIMG_MASTER_BOOT_RECORD mbr"));
    QVERIFY2(mbr, qPrintable(childNames(rows[0].get())));
    QCOMPARE(findChildNamed(mbr, QStringLiteral("dword diskSignature"))->value, QStringLiteral("305419896"));
    QCOMPARE(findChildNamed(mbr, QStringLiteral("word signature"))->value, QStringLiteral("RAWIMG_BOOT_SIGNATURE_MBR"));

    StructureRow *partitions = findChildNamed(mbr, QStringLiteral("RAWIMG_PARTITION_ENTRY partitions[]"));
    QVERIFY2(partitions, qPrintable(childNames(mbr)));
    QCOMPARE(partitions->children.size(), size_t(4));
    QCOMPARE(partitions->children[0]->name, QStringLiteral("[0]RAWIMG_PARTITION_FAT32_LBA"));
    QCOMPARE(partitions->children[1]->name, QStringLiteral("[1]RAWIMG_PARTITION_LINUX"));
    QCOMPARE(partitions->children[2]->name, QStringLiteral("[2]RAWIMG_PARTITION_EMPTY"));

    StructureRow *first = partitions->children[0].get();
    QCOMPARE(findChildNamed(first, QStringLiteral("byte bootIndicator"))->value,
             QStringLiteral("RAWIMG_PARTITION_ACTIVE"));
    QCOMPARE(findChildNamed(first, QStringLiteral("byte partitionType"))->value,
             QStringLiteral("RAWIMG_PARTITION_FAT32_LBA"));
    QCOMPARE(findChildNamed(first, QStringLiteral("dword firstLba"))->value, QStringLiteral("1"));
    QCOMPARE(findChildNamed(first, QStringLiteral("dword sectorCount"))->value, QStringLiteral("2"));

    StructureRow *firstSectorCount = findChildNamed(first, QStringLiteral("dword sectorCount"));
    QVERIFY2(firstSectorCount, qPrintable(childNames(first)));
    StructureRow *firstData = findChildNamed(firstSectorCount, QStringLiteral("BYTE PartitionData[]"));
    QVERIFY2(firstData, qPrintable(childNames(firstSectorCount)));
    QCOMPARE(firstData->offset, QStringLiteral("00000200"));
    QCOMPARE(firstData->children[0]->value, QStringLiteral("170"));
    QCOMPARE(firstData->children[1]->value, QStringLiteral("187"));

    StructureRow *secondSectorCount = findChildNamed(partitions->children[1].get(), QStringLiteral("dword sectorCount"));
    QVERIFY2(secondSectorCount, qPrintable(childNames(partitions->children[1].get())));
    StructureRow *secondData = findChildNamed(secondSectorCount, QStringLiteral("BYTE PartitionData[]"));
    QVERIFY2(secondData, qPrintable(childNames(secondSectorCount)));
    QCOMPARE(secondData->offset, QStringLiteral("00000600"));

    StructureRow *emptySectorCount = findChildNamed(partitions->children[2].get(), QStringLiteral("dword sectorCount"));
    QVERIFY2(emptySectorCount, qPrintable(childNames(partitions->children[2].get())));
    QVERIFY(!findChildNamed(emptySectorCount, QStringLiteral("BYTE PartitionData[]")));
}

void StructViewTests::builderRendersIsoVolumeDescriptors()
{
    // Scenario: ISO 9660 images anchor volume descriptors at sector 16 and the
    // primary descriptor points at path-table and root-directory extents.
    // Expected: the standard ISO definition renders the PVD, stops before the
    // terminator descriptor, and exposes the pointed-to byte ranges as dynamic rows.
    StrataLibrary library;
    QVERIFY2(parseStandardDefinition(&library, QStringLiteral("iso.strata")), "iso.strata failed to parse");
    TypeDecl *isoRoot = exportedNamed(&library, QStringLiteral("ISO_IMAGE"));
    QVERIFY(isoRoot);

    constexpr qsizetype sectorSize = 2048;
    constexpr qsizetype pvd = 16 * sectorSize;
    constexpr qsizetype terminator = 17 * sectorSize;
    constexpr qsizetype rootDirectorySector = 18;
    constexpr qsizetype pathTableSector = 19;
    QByteArray iso(20 * sectorSize, '\0');

    iso[pvd + 0] = char(1);
    writeAscii(&iso, pvd + 1, "CD001");
    iso[pvd + 6] = char(1);
    writeAscii(&iso, pvd + 8, "LINUX");
    writeAscii(&iso, pvd + 40, "TEST_ISO");
    writeLe32(&iso, pvd + 80, 20);
    writeBe32(&iso, pvd + 84, 20);
    writeLe16(&iso, pvd + 120, 1);
    writeBe16(&iso, pvd + 122, 1);
    writeLe16(&iso, pvd + 124, 1);
    writeBe16(&iso, pvd + 126, 1);
    writeLe16(&iso, pvd + 128, sectorSize);
    writeBe16(&iso, pvd + 130, sectorSize);
    writeLe32(&iso, pvd + 132, 10);
    writeBe32(&iso, pvd + 136, 10);
    writeLe32(&iso, pvd + 140, pathTableSector);
    writeBe32(&iso, pvd + 148, pathTableSector);

    constexpr qsizetype rootRecord = pvd + 156;
    iso[rootRecord + 0] = char(34);
    writeLe32(&iso, rootRecord + 2, rootDirectorySector);
    writeBe32(&iso, rootRecord + 6, rootDirectorySector);
    writeLe32(&iso, rootRecord + 10, sectorSize);
    writeBe32(&iso, rootRecord + 14, sectorSize);
    iso[rootRecord + 25] = char(0x02);
    writeLe16(&iso, rootRecord + 28, 1);
    writeBe16(&iso, rootRecord + 30, 1);
    iso[rootRecord + 32] = char(1);
    iso[rootRecord + 33] = char(0);
    iso[rootDirectorySector * sectorSize] = char(34);
    iso[rootDirectorySector * sectorSize + 25] = char(0x02);

    writeAscii(&iso, pathTableSector * sectorSize, "\x01");
    iso[pathTableSector * sectorSize + 1] = char(0);
    writeLe32(&iso, pathTableSector * sectorSize + 2, rootDirectorySector);
    writeLe16(&iso, pathTableSector * sectorSize + 6, 1);
    iso[pathTableSector * sectorSize + 8] = char(0);

    iso[terminator + 0] = char(255);
    writeAscii(&iso, terminator + 1, "CD001");
    iso[terminator + 6] = char(1);

    auto rows = buildRows(&library, isoRoot, iso);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->name, QStringLiteral("ISO_IMAGE"));

    StructureRow *descriptors = findChildNamed(rows[0].get(), QStringLiteral("ISO_VOLUME_DESCRIPTOR volumeDescriptors[]"));
    QVERIFY2(descriptors, qPrintable(childNames(rows[0].get())));
    QCOMPARE(descriptors->children.size(), size_t(2));
    QCOMPARE(descriptors->children[0]->name, QStringLiteral("[0]ISO_VOLUME_PRIMARY"));
    QCOMPARE(descriptors->children[1]->name, QStringLiteral("[1]ISO_VOLUME_TERMINATOR"));

    StructureRow *primary = findChildNamed(descriptors->children[0].get(),
                                           QStringLiteral("ISO_PRIMARY_VOLUME_DESCRIPTOR_PAYLOAD primary"));
    QVERIFY2(primary, qPrintable(childNames(descriptors->children[0].get())));
    QCOMPARE(findChildNamed(primary, QStringLiteral("char systemIdentifier[]"))->value, QStringLiteral("\"LINUX\""));
    QCOMPARE(findChildNamed(primary, QStringLiteral("char volumeIdentifier[]"))->value, QStringLiteral("\"TEST_ISO\""));

    StructureRow *typeLPathTableLocation = findChildNamed(primary, QStringLiteral("dword typeLPathTableLocation"));
    QVERIFY2(typeLPathTableLocation, qPrintable(childNames(primary)));
    StructureRow *pathTable = findChildNamed(typeLPathTableLocation, QStringLiteral("BYTE TypeLPathTable[]"));
    QVERIFY2(pathTable, qPrintable(childNames(typeLPathTableLocation)));
    QCOMPARE(pathTable->offset, QStringLiteral("00009800"));

    StructureRow *rootDirectoryRecord = findChildNamed(primary, QStringLiteral("ISO_DIRECTORY_RECORD rootDirectoryRecord"));
    QVERIFY2(rootDirectoryRecord, qPrintable(childNames(primary)));
    StructureRow *fileFlags = findChildNamed(rootDirectoryRecord, QStringLiteral("byte fileFlags"));
    QVERIFY2(fileFlags, qPrintable(childNames(rootDirectoryRecord)));
    QCOMPARE(fileFlags->value, QStringLiteral("ISO_FILE_FLAG_DIRECTORY"));
    StructureRow *directoryData = findChildNamed(fileFlags, QStringLiteral("BYTE DirectoryData[]"));
    QVERIFY2(directoryData, qPrintable(childNames(fileFlags)));
    QCOMPARE(directoryData->offset, QStringLiteral("00009000"));
}

void StructViewTests::builderRendersZipCentralDirectoryFromEocd()
{
    // Scenario: ZIP local headers may defer CRC/sizes to data descriptors, so
    // walking local headers can stop after the first payload.
    // Expected: the standard ZIP definition finds EOCD from the trailer and
    // renders all central-directory entries instead.
    StrataLibrary library;
    QVERIFY2(parseStandardDefinition(&library, QStringLiteral("zip.strata")), "zip.strata failed to parse");
    TypeDecl *zipRoot = exportedNamed(&library, QStringLiteral("ZIP"));
    QVERIFY(zipRoot);

    QByteArray zip;
    auto appendLe16 = [&zip](quint16 value) {
        zip.append(char(value & 0xff));
        zip.append(char((value >> 8) & 0xff));
    };
    auto appendLe32 = [&zip](quint32 value) {
        zip.append(char(value & 0xff));
        zip.append(char((value >> 8) & 0xff));
        zip.append(char((value >> 16) & 0xff));
        zip.append(char((value >> 24) & 0xff));
    };
    auto appendLocal = [&](const QByteArray &name, const QByteArray &data) -> quint32 {
        const quint32 offset = static_cast<quint32>(zip.size());
        appendLe32(0x04034b50);
        appendLe16(20);
        appendLe16(0x0008); // data descriptor follows payload
        appendLe16(8);
        appendLe16(0);
        appendLe16(0);
        appendLe32(0);
        appendLe32(0);
        appendLe32(0);
        appendLe16(static_cast<quint16>(name.size()));
        appendLe16(0);
        zip.append(name);
        zip.append(data);
        appendLe32(0x08074b50);
        appendLe32(0);
        appendLe32(static_cast<quint32>(data.size()));
        appendLe32(static_cast<quint32>(data.size()));
        return offset;
    };
    auto appendCentral = [&](const QByteArray &name, quint32 localOffset, quint32 size) {
        appendLe32(0x02014b50);
        appendLe16(20);
        appendLe16(20);
        appendLe16(0x0008);
        appendLe16(8);
        appendLe16(0);
        appendLe16(0);
        appendLe32(0);
        appendLe32(size);
        appendLe32(size);
        appendLe16(static_cast<quint16>(name.size()));
        appendLe16(0);
        appendLe16(0);
        appendLe16(0);
        appendLe16(0);
        appendLe32(0);
        appendLe32(localOffset);
        zip.append(name);
    };

    const quint32 firstOffset = appendLocal("a.txt", "alpha");
    const quint32 secondOffset = appendLocal("b.txt", "bravo");
    const quint32 centralOffset = static_cast<quint32>(zip.size());
    appendCentral("a.txt", firstOffset, 5);
    appendCentral("b.txt", secondOffset, 5);
    const quint32 centralSize = static_cast<quint32>(zip.size()) - centralOffset;
    appendLe32(0x06054b50);
    appendLe16(0);
    appendLe16(0);
    appendLe16(2);
    appendLe16(2);
    appendLe32(centralSize);
    appendLe32(centralOffset);
    appendLe16(0);

    auto rows = buildRows(&library, zipRoot, zip);
    QCOMPARE(rows.size(), size_t(1));

    StructureRow *locals = nullptr;
    StructureRow *central = nullptr;
    for (const auto &child : rows[0]->children)
    {
        if (child->name.contains(QStringLiteral("localFileHeaders")))
        {
            locals = child.get();
            continue;
        }

        if (child->name.contains(QStringLiteral("centralDirectory")))
        {
            central = child.get();
            break;
        }
    }

    QVERIFY(locals);
    QCOMPARE(locals->children.size(), size_t(1));
    QVERIFY(locals->children[0]->name.contains(QStringLiteral("a.txt")));

    QVERIFY(central);
    QCOMPARE(central->children.size(), size_t(2));
    QVERIFY(central->children[0]->name.contains(QStringLiteral("a.txt")));
    QVERIFY(central->children[1]->name.contains(QStringLiteral("b.txt")));

    StructureDisplayOptions sortedOptions;
    sortedOptions.sortTopLevelRowsByOffset = true;
    auto sortedRows = buildRows(&library, zipRoot, zip, 0, sortedOptions);
    QCOMPARE(sortedRows.size(), size_t(1));
    QCOMPARE(sortedRows[0]->children.size(), size_t(3));
    QVERIFY(sortedRows[0]->children[0]->name.contains(QStringLiteral("localFileHeaders")));
    QVERIFY(sortedRows[0]->children[1]->name.contains(QStringLiteral("centralDirectory")));
    QCOMPARE(sortedRows[0]->children[2]->name, QStringLiteral("ZIP_END_OF_CENTRAL_DIRECTORY_RECORD eocd"));

    QByteArray storedZip;
    auto appendStoredLe16 = [&storedZip](quint16 value) {
        storedZip.append(char(value & 0xff));
        storedZip.append(char((value >> 8) & 0xff));
    };
    auto appendStoredLe32 = [&storedZip](quint32 value) {
        storedZip.append(char(value & 0xff));
        storedZip.append(char((value >> 8) & 0xff));
        storedZip.append(char((value >> 16) & 0xff));
        storedZip.append(char((value >> 24) & 0xff));
    };
    auto appendStoredLocal = [&](const QByteArray &name, qsizetype size) -> quint32 {
        const quint32 offset = static_cast<quint32>(storedZip.size());
        appendStoredLe32(0x04034b50);
        appendStoredLe16(20);
        appendStoredLe16(0);
        appendStoredLe16(0);
        appendStoredLe16(0);
        appendStoredLe16(0);
        appendStoredLe32(0);
        appendStoredLe32(static_cast<quint32>(size));
        appendStoredLe32(static_cast<quint32>(size));
        appendStoredLe16(static_cast<quint16>(name.size()));
        appendStoredLe16(0);
        storedZip.append(name);
        storedZip.append(QByteArray(size, '\0'));
        return offset;
    };
    auto appendStoredCentral = [&](const QByteArray &name, quint32 localOffset, qsizetype size) {
        appendStoredLe32(0x02014b50);
        appendStoredLe16(20);
        appendStoredLe16(20);
        appendStoredLe16(0);
        appendStoredLe16(0);
        appendStoredLe16(0);
        appendStoredLe16(0);
        appendStoredLe32(0);
        appendStoredLe32(static_cast<quint32>(size));
        appendStoredLe32(static_cast<quint32>(size));
        appendStoredLe16(static_cast<quint16>(name.size()));
        appendStoredLe16(0);
        appendStoredLe16(0);
        appendStoredLe16(0);
        appendStoredLe16(0);
        appendStoredLe32(0);
        appendStoredLe32(localOffset);
        storedZip.append(name);
    };

    const quint32 largeOffset = appendStoredLocal("large.bin", 5000);
    const quint32 nextOffset = appendStoredLocal("next.bin", 1);
    const quint32 storedCentralOffset = static_cast<quint32>(storedZip.size());
    appendStoredCentral("large.bin", largeOffset, 5000);
    appendStoredCentral("next.bin", nextOffset, 1);
    const quint32 storedCentralSize = static_cast<quint32>(storedZip.size()) - storedCentralOffset;
    appendStoredLe32(0x06054b50);
    appendStoredLe16(0);
    appendStoredLe16(0);
    appendStoredLe16(2);
    appendStoredLe16(2);
    appendStoredLe32(storedCentralSize);
    appendStoredLe32(storedCentralOffset);
    appendStoredLe16(0);

    auto storedRows = buildRows(&library, zipRoot, storedZip);
    QCOMPARE(storedRows.size(), size_t(1));
    StructureRow *storedLocals = findChildNamed(storedRows[0].get(), QStringLiteral("ZIP_LOCAL_FILE_HEADER localFileHeaders[]"));
    QVERIFY(storedLocals);
    QCOMPARE(storedLocals->children.size(), size_t(2));
    QVERIFY(storedLocals->children[0]->name.contains(QStringLiteral("large.bin")));
    QVERIFY(storedLocals->children[1]->name.contains(QStringLiteral("next.bin")));
}

void StructViewTests::builderRendersElf32AndElf64Tables()
{
    // Scenario: ELF stores the 32/64-bit layout choice in e_ident[EI_CLASS].
    // Expected: Strata switch_is selects the matching header branch, and the
    // program/section table arrays use offsets and counts from that branch.
    // Regression guard: ELF support must not assume PE-like fixed offsets or a
    // single word size.
    StrataLibrary library32;
    QVERIFY2(parseStandardElfDefinition(&library32), "elf.strata failed to parse");
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

    TypeDecl *root32 = exportedNamed(&library32, QStringLiteral("ELF"));
    QVERIFY(root32);
    auto rows32 = buildRows(&library32, root32, elf32);
    QCOMPARE(rows32.size(), size_t(1));
    QStringList childNames32;
    for (const auto &child : rows32[0]->children)
        childNames32.push_back(child->name);
    const QByteArray childNames32Message = childNames32.join(QStringLiteral(", ")).toLocal8Bit();
    StructureRow *header32 = findChildNamed(rows32[0].get(), QStringLiteral("Elf32_Ehdr header32"));
    QVERIFY2(header32, childNames32Message.constData());
    // children[0] is now e_ident (Elf32_Ehdr matches the real upstream
    // layout, e_ident included), shifting every later field by one versus
    // the old e_ident-hoisted-out Elf32_EhdrTail: children[10] is e_phnum.
    QCOMPARE(header32->children[10]->value, QStringLiteral("1"));
    QVERIFY2(findChildNamed(rows32[0].get(), QStringLiteral("Elf32_Phdr programHeaders32[]")),
             childNames32Message.constData());
    StructureRow *sections32 = findChildNamed(rows32[0].get(), QStringLiteral("Elf32_Shdr sectionHeaders32[]"));
    QVERIFY2(sections32, childNames32Message.constData());
    QCOMPARE(sections32->children.size(), size_t(2));
    QCOMPARE(header32->children[4]->value, QStringLiteral("305419896"));

    StrataLibrary library32be;
    QVERIFY2(parseStandardElfDefinition(&library32be), "elf.strata failed to parse");
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

    TypeDecl *root32be = exportedNamed(&library32be, QStringLiteral("ELF"));
    QVERIFY(root32be);
    auto rows32be = buildRows(&library32be, root32be, elf32be);
    QCOMPARE(rows32be.size(), size_t(1));
    StructureRow *header32be = findChildNamed(rows32be[0].get(), QStringLiteral("Elf32_Ehdr header32"));
    QVERIFY(header32be);
    QCOMPARE(header32be->children[4]->value, QStringLiteral("16909060"));

    StrataLibrary library64;
    QVERIFY2(parseStandardElfDefinition(&library64), "elf.strata failed to parse");
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

    TypeDecl *root64 = exportedNamed(&library64, QStringLiteral("ELF"));
    QVERIFY(root64);
    auto rows64 = buildRows(&library64, root64, elf64);
    QCOMPARE(rows64.size(), size_t(1));
    QVERIFY(findChildNamed(rows64[0].get(), QStringLiteral("Elf64_Ehdr header64")));
    StructureRow *programs64 = findChildNamed(rows64[0].get(), QStringLiteral("Elf64_Phdr programHeaders64[]"));
    QVERIFY(programs64);
    QCOMPARE(programs64->children.size(), size_t(1));
    StructureRow *sections64 = findChildNamed(rows64[0].get(), QStringLiteral("Elf64_Shdr sectionHeaders64[]"));
    QVERIFY(sections64);
    QCOMPARE(sections64->children.size(), size_t(1));

    QByteArray phdr32(32, '\0');
    writeLe32(&phdr32, 24, 0x5);
    auto phdr32Rows = buildRows(&library32, typeNamed(&library32, QStringLiteral("Elf32_Phdr")), phdr32);
    QCOMPARE(phdr32Rows.size(), size_t(1));
    StructureRow *programFlags32 = findChildNamed(phdr32Rows[0].get(), QStringLiteral("e32 p_flags"));
    QVERIFY(programFlags32);
    QCOMPARE(programFlags32->value, QStringLiteral("PF_X | PF_R"));
    QVERIFY(findChildNamed(programFlags32, QStringLiteral("PF_X")));
    QVERIFY(findChildNamed(programFlags32, QStringLiteral("PF_R")));

    QByteArray phdr64(56, '\0');
    writeLe32(&phdr64, 4, 0x6);
    auto phdr64Rows = buildRows(&library64, typeNamed(&library64, QStringLiteral("Elf64_Phdr")), phdr64);
    QCOMPARE(phdr64Rows.size(), size_t(1));
    StructureRow *programFlags64 = findChildNamed(phdr64Rows[0].get(), QStringLiteral("e32 p_flags"));
    QVERIFY(programFlags64);
    QCOMPARE(programFlags64->value, QStringLiteral("PF_W | PF_R"));

    QByteArray shdr32(40, '\0');
    writeLe32(&shdr32, 8, 0x6);
    auto shdr32Rows = buildRows(&library32, typeNamed(&library32, QStringLiteral("Elf32_Shdr")), shdr32);
    QCOMPARE(shdr32Rows.size(), size_t(1));
    StructureRow *sectionFlags32 = findChildNamed(shdr32Rows[0].get(), QStringLiteral("e32 sh_flags"));
    QVERIFY(sectionFlags32);
    QCOMPARE(sectionFlags32->value, QStringLiteral("SHF_ALLOC | SHF_EXECINSTR"));
    QVERIFY(findChildNamed(sectionFlags32, QStringLiteral("SHF_ALLOC")));
    QVERIFY(findChildNamed(sectionFlags32, QStringLiteral("SHF_EXECINSTR")));

    QByteArray shdr64(64, '\0');
    writeLe64(&shdr64, 8, 0x3);
    auto shdr64Rows = buildRows(&library64, typeNamed(&library64, QStringLiteral("Elf64_Shdr")), shdr64);
    QCOMPARE(shdr64Rows.size(), size_t(1));
    StructureRow *sectionFlags64 = findChildNamed(shdr64Rows[0].get(), QStringLiteral("e64 sh_flags"));
    QVERIFY(sectionFlags64);
    QCOMPARE(sectionFlags64->value, QStringLiteral("SHF_WRITE | SHF_ALLOC"));
}

void StructViewTests::builderPlacesDynamicStructsUnderNamedDynamicContainers()
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
                        "  [dynamic_struct(case(Export), type(ExportDir), offset(VirtualAddress), mapper(offset_map), optional(Size != 0)), dynamic_struct(case(Import), type(ImportDesc), offset(VirtualAddress), mapper(offset_map), optional(Size != 0))] DataDir dirs[2];\n"
                        "  [name(Name)] Section sections[2];\n"
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
    verifyBranchIconsPresent(rows[0]->children[2].get());
    QCOMPARE(rows[0]->children[2]->children.size(), size_t(0));
    QCOMPARE(rows[0]->children[3]->name, QStringLiteral("SECTION .idata"));
    QVERIFY(!rows[0]->children[3]->name.startsWith(QStringLiteral("SECTION - ")));
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

void StructViewTests::builderPlacesDirectDynamicStructsUnderOwningRows()
{
    // Scenario: a non-PE format stores an absolute file offset in an ordinary
    // row and wants the pointed-to structure displayed as related data.
    // Expected: the default direct mapper uses that file offset and attaches
    // the dynamic row under the declaration carrying dynamic_struct(...).
    // Regression guard: dynamic_struct must not require array selectors or
    // PE-style offset_map containers.
    StrataLibrary library;
    Parser parser(&library);
    QVERIFY(parseBuffer(parser,
                        "typedef struct _Payload { byte value; } Payload;\n"
                        "[export]\n"
                        "struct Root {\n"
                        "  [dynamic_struct(name(RelatedPayload), type(Payload), offset(payloadOffset))] dword payloadOffset;\n"
                        "  dword padding;\n"
                        "} root;\n"));

    QByteArray bytes(16, '\0');
    writeLe32(&bytes, 0, 12);
    bytes[12] = char(0x5a);

    auto rows = buildRows(&library, firstExported(&library), bytes);
    QCOMPARE(rows.size(), size_t(1));

    StructureRow *offsetRow = findChildNamed(rows[0].get(), QStringLiteral("dword payloadOffset"));
    QVERIFY(offsetRow);
    QCOMPARE(offsetRow->children.size(), size_t(1));

    StructureRow *payload = offsetRow->children[0].get();
    QCOMPARE(payload->name, QStringLiteral("RelatedPayload"));
    QCOMPARE(payload->offset, QStringLiteral("0000000C"));
    QCOMPARE(static_cast<int>(payload->kind), static_cast<int>(StructureRowKind::Dynamic));
    QCOMPARE(payload->children.size(), size_t(1));
    QCOMPARE(payload->children[0]->name, QStringLiteral("byte value"));
    QCOMPARE(payload->children[0]->value, QStringLiteral("90"));
}

void StructViewTests::builderRendersDynamicArraysAtReferencedOffsets()
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
                        "  [dynamic_container(type(SECTION)), offset_map(VirtualAddress, SizeOfRawData, PointerToRawData)] Section section[1];\n"
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

void StructViewTests::builderStopsDynamicAndInlineArraysAtTerminators()
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
                        "  [dynamic_container(type(SECTION)), offset_map(VirtualAddress, SizeOfRawData, PointerToRawData)] Section section[1];\n"
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

void StructViewTests::builderEmitsSemanticRowsUnderAttachedSchema()
{
    // Scenario: a raw directory row points at byte payloads elsewhere in the
    // file and declares a semantic destination schema.
    // Expected: the raw tree is unchanged, while a Semantic/Payloads branch
    // contains one labeled byte-backed row per emitting raw row.
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
    QCOMPARE(rows.size(), size_t(1));

    StructureRow *entries = findChildNamed(rows[0].get(), QStringLiteral("Entry entries[]"));
    QVERIFY(entries);
    QCOMPARE(entries->children.size(), size_t(2));
    QVERIFY(!findChildNamed(entries->children[0].get(), QStringLiteral("17")));

    StructureRow *semantic = findChildNamed(rows[0].get(), QStringLiteral("Semantic"));
    QVERIFY(semantic);
    StructureRow *payloads = findChildNamed(semantic, QStringLiteral("Payloads"));
    QVERIFY(payloads);
    QCOMPARE(payloads->children.size(), size_t(1));

    StructureRow *payload = payloads->children[0].get();
    QCOMPARE(payload->name, QStringLiteral("17"));
    QCOMPARE(payload->offset, QStringLiteral("00000030"));
    QCOMPARE(payload->byteLength, uint64_t(3));
    QCOMPARE(static_cast<int>(payload->kind), static_cast<int>(StructureRowKind::Semantic));
    QCOMPARE(payload->children.size(), size_t(3));
    QCOMPARE(payload->children[0]->value, QStringLiteral("170"));
    QCOMPARE(payload->children[2]->value, QStringLiteral("204"));
}

void StructViewTests::builderEmitsSemanticRowsThroughNamedOffsetSpaces()
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
    QCOMPARE(rows.size(), size_t(1));

    StructureRow *semantic = findChildNamed(rows[0].get(), QStringLiteral("Semantic"));
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

void StructViewTests::builderEmitsAndMergesSemanticNodes()
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
                        "  [name(concat(\"item \", Key))]\n"
                        "  struct { byte Key; byte Size; char Label[]; } Items[];\n"
                        "} RootView;\n"
                        "[export, semantic(RootView)] typedef struct _Root { Entry entry; } Root;\n"));

    QByteArray bytes;
    bytes.append(char(7));
    bytes.append(char(12));
    bytes.append(char(0xaa));
    bytes.append(char(0xbb));

    auto rows = buildRows(&library, firstExported(&library), bytes);
    QCOMPARE(rows.size(), size_t(1));

    StructureRow *semantic = findChildNamed(rows[0].get(), QStringLiteral("Semantic"));
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

void StructViewTests::builderRejectsUnterminatedCstrSemanticNames()
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
    QCOMPARE(rows.size(), size_t(1));

    StructureRow *semantic = findChildNamed(rows[0].get(), QStringLiteral("Semantic"));
    QVERIFY(semantic);
    StructureRow *strings = findChildNamed(semantic, QStringLiteral("Strings"));
    QVERIFY(strings);
    QCOMPARE(strings->children.size(), size_t(1));
    QCOMPARE(strings->children[0]->name, QStringLiteral("OK"));
}

void StructViewTests::builderEmitsIntoMappedSemanticContainers()
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
                        "[semantic(\"Image\")] typedef struct _RootView { [tree(\"flatten\")] SectionView Sections[]; } RootView;\n"
                        "typedef struct _Dir { dword va; dword size; } Dir;\n"
                        "[offset_map(va, size, raw),\n"
                        " emit_row(dest(Sections, key(Name), name(Name)), offset(raw), map(\"rva\", va, size, raw)),\n"
                        " emit(dest(Sections.Bytes), type(PayloadByte), offset(\"rva\", va), count(size))]\n"
                        "typedef struct _Section { char Name[8]; dword va; dword size; dword raw; } Section;\n"
                        "[export, semantic(RootView)]\n"
                        "struct Root {\n"
                        "  [name(DirKind), emit(case(ImportDir), dest(Sections.Imports), label(\"Import Descriptors\"), type(ImportDesc), offset(\"rva\", va), max_count(size / sizeof(ImportDesc)), terminated_by(thunk == 0), terminator(\"hidden\"))] Dir dirs[2];\n"
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
    QCOMPARE(rows.size(), size_t(1));

    StructureRow *image = findChildNamed(rows[0].get(), QStringLiteral("Image"));
    QVERIFY2(image, qPrintable(childNames(rows[0].get())));
    QVERIFY(!findChildNamed(image, QStringLiteral("Sections")));
    QCOMPARE(image->children.size(), size_t(2));
    QCOMPARE(image->children[0]->name, QStringLiteral(".text"));
    QCOMPARE(image->children[0]->offset, QStringLiteral("00000040"));
    QCOMPARE(image->children[1]->name, QStringLiteral(".idata"));
    QCOMPARE(image->children[1]->offset, QStringLiteral("00000080"));

    QCOMPARE(image->children[1]->children.size(), size_t(2));
    QCOMPARE(image->children[1]->children[0]->name, QStringLiteral("Imports"));
    QCOMPARE(image->children[1]->children[1]->name, QStringLiteral("Bytes"));
    StructureRow *bytesGroup = findChildNamed(image->children[1].get(), QStringLiteral("Bytes"));
    QVERIFY2(bytesGroup, qPrintable(childNames(image->children[1].get())));
    QCOMPARE(bytesGroup->children.size(), size_t(1));
    QCOMPARE(bytesGroup->children[0]->children.size(), size_t(32));

    StructureRow *importsGroup = findChildNamed(image->children[1].get(), QStringLiteral("Imports"));
    QVERIFY2(importsGroup, qPrintable(childNames(image->children[1].get())));
    StructureRow *imports = findChildNamed(importsGroup, QStringLiteral("Import Descriptors"));
    QVERIFY2(imports, qPrintable(childNames(importsGroup)));
    QCOMPARE(imports->offset, QStringLiteral("00000080"));
    QCOMPARE(imports->children.size(), size_t(1));
    StructureRow *thunk = findChildNamed(imports->children[0].get(), QStringLiteral("dword thunk"));
    QVERIFY(thunk);
    QCOMPARE(thunk->value, QStringLiteral("305419896"));
}

void StructViewTests::definitionManagerFlagsUnknownSemanticDestinations()
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

void StructViewTests::definitionManagerFlagsUnknownSemanticNodeFields()
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

void StructViewTests::builderRunsSemanticViewsOnceForDynamicArrayTables()
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
                        "[view(\"test.dynamic_array.once\")]\n"
                        "typedef struct _Viewed { dword Value; } Viewed;\n"
                        "typedef struct _Section { char Name[8]; dword VirtualAddress; dword SizeOfRawData; dword PointerToRawData; } Section;\n"
                        "typedef struct _SectionBucket { } SECTION;\n"
                        "[export]\n"
                        "struct Root {\n"
                        "  [dynamic_array(name(Entries), type(Viewed), offset(tableRva), count(4), mapper(offset_map), terminated_by(Value == 0), terminator(\"hidden\"))] dword tableRva;\n"
                        "  [dynamic_container(type(SECTION)), offset_map(VirtualAddress, SizeOfRawData, PointerToRawData)] Section section[1];\n"
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

void StructViewTests::builderNamesPeDynamicSectionsFromStandardDefinition()
{
    // Scenario: the shipped PE definition renders dynamic SECTION rows from the
    // real IMAGE_SECTION_HEADER table rather than a simplified test-only type.
    // Expected: the generated SECTION rows inherit [name(Name)] from each source
    // section header, producing user-visible names such as SECTION .text.
    // Regression guard: fixed-width PE section names must survive the generic
    // dynamic_container handoff instead of falling back to a bare SECTION node.
    StrataLibrary library;
    QVERIFY2(parseStandardDefinition(&library, QStringLiteral("pe.strata")), "pe.strata failed to parse");

    QByteArray bytes(0x300, '\0');
    writeLe32(&bytes, 0x3c, 0x80);
    writeLe16(&bytes, 0x86, 2);
    writeLe16(&bytes, 0x96, 0x2002);
    writeLe16(&bytes, 0x94, 256);
    writeLe16(&bytes, 0x98, 0x10b);
    writeLe16(&bytes, 0x98 + 70, 0x0140);
    writeLe32(&bytes, 0x98 + 92, 16);

    const qsizetype sectionTable = 0x80 + 4 + 20 + 256;
    writeAscii(&bytes, sectionTable, ".text");
    writeLe32(&bytes, sectionTable + 12, 0x1000);
    writeLe32(&bytes, sectionTable + 16, 0x100);
    writeLe32(&bytes, sectionTable + 20, 0x200);
    writeLe32(&bytes, sectionTable + 36, 0x60000020);

    const qsizetype secondSection = sectionTable + 40;
    writeAscii(&bytes, secondSection, ".idata");
    writeLe32(&bytes, secondSection + 12, 0x2000);
    writeLe32(&bytes, secondSection + 16, 0x80);
    writeLe32(&bytes, secondSection + 20, 0x280);
    writeLe32(&bytes, secondSection + 36, 0xC0000040);

    TypeDecl *root = exportedNamed(&library, QStringLiteral("PE"));
    QVERIFY(root);
    auto rows = buildRows(&library, root, bytes);
    QCOMPARE(rows.size(), size_t(1));

    StructureRow *text = findChildNamed(rows[0].get(), QStringLiteral("SECTION .text"));
    QVERIFY(text);
    QCOMPARE(text->offset, QStringLiteral("00000200"));
    QVERIFY(!text->name.startsWith(QStringLiteral("SECTION - ")));

    StructureRow *idata = findChildNamed(rows[0].get(), QStringLiteral("SECTION .idata"));
    QVERIFY(idata);
    QCOMPARE(idata->offset, QStringLiteral("00000280"));
    QVERIFY(!idata->name.startsWith(QStringLiteral("SECTION - ")));

    StructureRow *peImage = findChildNamed(rows[0].get(), QStringLiteral("PE Image"));
    QVERIFY2(peImage, qPrintable(childNames(rows[0].get())));
    QVERIFY(!findChildNamed(peImage, QStringLiteral("Sections")));
    QCOMPARE(peImage->children.size(), size_t(2));
    QCOMPARE(peImage->children[0]->name, QStringLiteral(".text"));
    QCOMPARE(peImage->children[0]->offset, QStringLiteral("00000200"));
    QCOMPARE(peImage->children[1]->name, QStringLiteral(".idata"));
    QCOMPARE(peImage->children[1]->offset, QStringLiteral("00000280"));

    StructureRow *fileCharacteristics = findDescendantNamed(rows[0].get(), QStringLiteral("word Characteristics"));
    QVERIFY(fileCharacteristics);
    QCOMPARE(fileCharacteristics->value, QStringLiteral("IMAGE_FILE_EXECUTABLE_IMAGE | IMAGE_FILE_DLL"));
    QVERIFY(findChildNamed(fileCharacteristics, QStringLiteral("IMAGE_FILE_EXECUTABLE_IMAGE")));
    QVERIFY(findChildNamed(fileCharacteristics, QStringLiteral("IMAGE_FILE_DLL")));

    StructureRow *dllCharacteristics = findDescendantNamed(rows[0].get(), QStringLiteral("word DllCharacteristics"));
    QVERIFY(dllCharacteristics);
    QCOMPARE(dllCharacteristics->value, QStringLiteral("IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE | IMAGE_DLLCHARACTERISTICS_NX_COMPAT"));
    QVERIFY(findChildNamed(dllCharacteristics, QStringLiteral("IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE")));
    QVERIFY(findChildNamed(dllCharacteristics, QStringLiteral("IMAGE_DLLCHARACTERISTICS_NX_COMPAT")));

    StructureRow *textHeader = findDescendantNamed(rows[0].get(), QStringLiteral("[0].text"));
    QVERIFY(textHeader);
    StructureRow *textCharacteristics = findChildNamed(textHeader, QStringLiteral("dword Characteristics"));
    QVERIFY(textCharacteristics);
    QCOMPARE(textCharacteristics->value, QStringLiteral("IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ"));

    StructureRow *idataHeader = findDescendantNamed(rows[0].get(), QStringLiteral("[1].idata"));
    QVERIFY(idataHeader);
    StructureRow *idataCharacteristics = findChildNamed(idataHeader, QStringLiteral("dword Characteristics"));
    QVERIFY(idataCharacteristics);
    QCOMPARE(idataCharacteristics->value, QStringLiteral("IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE"));

    StructureTreeModel model;
    model.setRowsForTests(std::move(rows));
    StructureDisplayOptions options;
    options.typeNameMode = StructureTypeNameMode::Storage;
    model.applyDisplayOptions(options);

    const QModelIndex rootIndex = model.index(0, StructureTreeModel::NameColumn);
    QVERIFY(rootIndex.isValid());
    QStringList visibleNames;
    for (int i = 0; i < model.rowCount(rootIndex); ++i)
        visibleNames.push_back(model.data(model.index(i, StructureTreeModel::NameColumn, rootIndex)).toString());
    QVERIFY2(visibleNames.contains(QStringLiteral("SECTION .text")),
             qPrintable(QStringLiteral("SECTION .text missing after display option pass: %1").arg(visibleNames.join(QStringLiteral(", ")))));
    QVERIFY2(visibleNames.contains(QStringLiteral("SECTION .idata")),
             qPrintable(QStringLiteral("SECTION .idata missing after display option pass: %1").arg(visibleNames.join(QStringLiteral(", ")))));
}

void StructViewTests::builderPeSemanticModeCppSkipsDeclarativeRows()
{
    // Scenario: PE semantic mode is forced to the legacy C++ path.
    // Expected: the shipped PE definition still renders raw/dynamic SECTION
    // rows, but the declarative PE Image branch is suppressed for isolated
    // performance and shape comparisons.
    ScopedEnvironmentVariable mode("Q22_PE_SEMANTIC_VIEW", "cpp");

    StrataLibrary library;
    QVERIFY2(parseStandardDefinition(&library, QStringLiteral("pe.strata")), "pe.strata failed to parse");

    QByteArray bytes(0x300, '\0');
    writeLe32(&bytes, 0x3c, 0x80);
    writeLe16(&bytes, 0x86, 1);
    writeLe16(&bytes, 0x96, 0x2002);
    writeLe16(&bytes, 0x94, 256);
    writeLe16(&bytes, 0x98, 0x10b);
    writeLe32(&bytes, 0x98 + 92, 16);

    const qsizetype sectionTable = 0x80 + 4 + 20 + 256;
    writeAscii(&bytes, sectionTable, ".text");
    writeLe32(&bytes, sectionTable + 12, 0x1000);
    writeLe32(&bytes, sectionTable + 16, 0x100);
    writeLe32(&bytes, sectionTable + 20, 0x200);
    writeLe32(&bytes, sectionTable + 36, 0x60000020);

    TypeDecl *root = exportedNamed(&library, QStringLiteral("PE"));
    QVERIFY(root);
    auto rows = buildRows(&library, root, bytes);
    QCOMPARE(rows.size(), size_t(1));

    QVERIFY(findChildNamed(rows[0].get(), QStringLiteral("SECTION .text")));
    QVERIFY(!findChildNamed(rows[0].get(), QStringLiteral("PE Image")));
}

void StructViewTests::builderNamesPeImportDescriptorsFromStandardDefinition()
{
    // Scenario: the shipped PE definition's _IMAGE_IMPORT_DESCRIPTOR carries
    // dynamic_array(name(DllName), type(CHAR), offset(Name), count(4096), ...)
    // that this RVA-redirected sub-array is the per-element name source for
    // whichever array contains IMAGE_IMPORT_DESCRIPTOR elements.
    // Expected: each descriptor's "[i]" tree label gets the resolved DLL name
    // appended, resolved eagerly without requiring the lazily-built DllName
    // child row to be expanded first.
    // Regression guard: name()'s wrapped-argument form inside dynamic_array(...)
    // must resolve end-to-end through the real PE definition, not just in a
    // synthetic test-only struct.
    StrataLibrary library;
    QVERIFY2(parseStandardDefinition(&library, QStringLiteral("pe.strata")), "pe.strata failed to parse");

    QByteArray bytes(0x300, '\0');
    writeLe32(&bytes, 0x3c, 0x80);
    writeLe16(&bytes, 0x86, 2);
    writeLe16(&bytes, 0x94, 256);
    writeLe16(&bytes, 0x98, 0x10b);
    writeLe32(&bytes, 0x98 + 92, 16);

    // DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT] -> VA 0x2000, one descriptor.
    const qsizetype importDirEntry = 0x98 + 96 + 1 * 8;
    writeLe32(&bytes, importDirEntry, 0x2000);
    writeLe32(&bytes, importDirEntry + 4, 20);

    const qsizetype sectionTable = 0x80 + 4 + 20 + 256;
    writeAscii(&bytes, sectionTable, ".text");
    writeLe32(&bytes, sectionTable + 12, 0x1000);
    writeLe32(&bytes, sectionTable + 16, 0x100);
    writeLe32(&bytes, sectionTable + 20, 0x200);

    const qsizetype secondSection = sectionTable + 40;
    writeAscii(&bytes, secondSection, ".idata");
    writeLe32(&bytes, secondSection + 12, 0x2000);
    writeLe32(&bytes, secondSection + 16, 0x80);
    writeLe32(&bytes, secondSection + 20, 0x280);

    // One IMAGE_IMPORT_DESCRIPTOR at VA 0x2000 (file 0x280); only Name is set,
    // pointing at "KERNEL32.dll" placed right after it at VA 0x2014 (file 0x294).
    writeLe32(&bytes, 0x28c, 0x2014);
    writeAscii(&bytes, 0x294, "KERNEL32.dll");

    TypeDecl *root = exportedNamed(&library, QStringLiteral("PE"));
    QVERIFY(root);
    auto rows = buildRows(&library, root, bytes);
    QCOMPARE(rows.size(), size_t(1));

    StructureRow *descriptors = findDescendantNamed(rows[0].get(), QStringLiteral("IMAGE_IMPORT_DESCRIPTOR[]"));
    QVERIFY2(descriptors, "IMAGE_IMPORT_DESCRIPTOR[] container not found in rendered tree");
    QCOMPARE(descriptors->children.size(), size_t(1));
    QVERIFY2(descriptors->children[0]->name.contains(QStringLiteral("KERNEL32.dll")),
             qPrintable(QStringLiteral("descriptor[0] name missing DLL hint: %1").arg(descriptors->children[0]->name)));
}

void StructViewTests::builderEmitsPeImportDllsFromStandardDefinition()
{
    // Scenario: the shipped PE definition uses declarative semantic rows to
    // present each import DLL under PE Image/<section>/Imports.
    // Expected: pure Strata resolves the DLL name through the RVA offset map
    // and creates a named semantic row without relying on the legacy C++ view.
    ScopedEnvironmentVariable mode("Q22_PE_SEMANTIC_VIEW", "declarative");

    StrataLibrary library;
    QVERIFY2(parseStandardDefinition(&library, QStringLiteral("pe.strata")), "pe.strata failed to parse");

    QByteArray bytes(0x360, '\0');
    writeLe32(&bytes, 0x3c, 0x80);
    writeLe16(&bytes, 0x86, 2);
    writeLe16(&bytes, 0x94, 256);
    writeLe16(&bytes, 0x98, 0x10b);
    writeLe32(&bytes, 0x98 + 92, 16);

    const qsizetype exportDirEntry = 0x98 + 96;
    writeLe32(&bytes, exportDirEntry, 0x2050);
    writeLe32(&bytes, exportDirEntry + 4, 40);

    const qsizetype importDirEntry = 0x98 + 96 + 1 * 8;
    writeLe32(&bytes, importDirEntry, 0x2000);
    writeLe32(&bytes, importDirEntry + 4, 40);

    const qsizetype sectionTable = 0x80 + 4 + 20 + 256;
    writeAscii(&bytes, sectionTable, ".text");
    writeLe32(&bytes, sectionTable + 12, 0x1000);
    writeLe32(&bytes, sectionTable + 16, 0x100);
    writeLe32(&bytes, sectionTable + 20, 0x200);

    const qsizetype secondSection = sectionTable + 40;
    writeAscii(&bytes, secondSection, ".idata");
    writeLe32(&bytes, secondSection + 12, 0x2000);
    writeLe32(&bytes, secondSection + 16, 0xc0);
    writeLe32(&bytes, secondSection + 20, 0x280);

    writeLe32(&bytes, 0x280, 0x2030);
    writeLe32(&bytes, 0x28c, 0x2014);
    writeLe32(&bytes, 0x290, 0x2030);
    writeAscii(&bytes, 0x294, "KERNEL32.dll");
    writeLe32(&bytes, 0x2b0, 0x2040);
    writeLe32(&bytes, 0x2b4, 0);
    writeLe16(&bytes, 0x2c0, 4660);
    writeAscii(&bytes, 0x2c2, "CreateFileW");
    writeLe32(&bytes, 0x2d0 + 12, 0x2080);
    writeLe32(&bytes, 0x2d0 + 16, 1);
    writeLe32(&bytes, 0x2d0 + 20, 1);
    writeLe32(&bytes, 0x2d0 + 24, 1);
    writeLe32(&bytes, 0x2d0 + 28, 0x20b8);
    writeLe32(&bytes, 0x2d0 + 32, 0x20b0);
    writeLe32(&bytes, 0x2d0 + 36, 0x20b4);
    writeAscii(&bytes, 0x300, "sclib-csharp.dll");
    writeAscii(&bytes, 0x310, "DllGetClassObject");
    writeLe32(&bytes, 0x330, 0x2090);
    writeLe16(&bytes, 0x334, 0);
    writeLe32(&bytes, 0x338, 0x1000);

    TypeDecl *root = exportedNamed(&library, QStringLiteral("PE"));
    QVERIFY(root);
    auto rows = buildRows(&library, root, bytes);
    QCOMPARE(rows.size(), size_t(1));

    StructureRow *peImage = findChildNamed(rows[0].get(), QStringLiteral("PE Image"));
    QVERIFY2(peImage, qPrintable(childNames(rows[0].get())));
    StructureRow *idata = findChildNamed(peImage, QStringLiteral(".idata"));
    QVERIFY2(idata, qPrintable(childNames(peImage)));
    StructureRow *imports = findChildNamed(idata, QStringLiteral("Imports"));
    QVERIFY2(imports, qPrintable(childNames(idata)));

    StructureRow *kernel32 = findChildNamed(imports, QStringLiteral("KERNEL32.dll"));
    QVERIFY2(kernel32, qPrintable(childNames(imports)));
    QCOMPARE(kernel32->offset, QStringLiteral("00000294"));
    QCOMPARE(static_cast<int>(kernel32->kind), static_cast<int>(StructureRowKind::Semantic));
    StructureRow *function = findChildNamed(kernel32, QStringLiteral("CreateFileW"));
    QVERIFY2(function, qPrintable(childNames(kernel32)));
    QCOMPARE(function->offset, QStringLiteral("000002C0"));
    QCOMPARE(static_cast<int>(function->kind), static_cast<int>(StructureRowKind::Semantic));
    QVERIFY(!findChildNamed(imports, QStringLiteral("sclib-csharp.dll")));
    QVERIFY(!findChildNamed(imports, QStringLiteral("Imports")));
    QVERIFY(!findChildNamed(imports, QStringLiteral("IMAGE_IMPORT_DESCRIPTOR[]")));

    StructureRow *exports = findChildNamed(idata, QStringLiteral("Exports"));
    QVERIFY2(exports, qPrintable(childNames(idata)));
    StructureRow *exportedFunction = findChildNamed(exports, QStringLiteral("DllGetClassObject"));
    QVERIFY2(exportedFunction, qPrintable(childNames(exports)));
    QCOMPARE(exportedFunction->offset, QStringLiteral("00000310"));
    QCOMPARE(static_cast<int>(exportedFunction->kind), static_cast<int>(StructureRowKind::Semantic));
    QVERIFY(!findChildNamed(exports, QStringLiteral("sclib-csharp.dll")));
}

void StructViewTests::builderResolvesEntryPointRvaThroughSectionOffsetMap()
{
    // Scenario: AddressOfEntryPoint is a relative virtual address (RVA), not a
    // file offset -- it must be translated through the section table's
    // offset_map before it can be used to seek the hex view / disassembler.
    // Expected: codeTargetOffset lands at the mapped file offset inside the
    // owning section, not at baseOffset + the raw RVA.
    // Regression guard: resolveEntryPointRows must run after the section
    // dynamic_container/offset_map has been collected, using the real PE
    // definition (a synthetic struct with no sections can't exercise this).
    StrataLibrary library;
    QVERIFY2(parseStandardDefinition(&library, QStringLiteral("pe.strata")), "pe.strata failed to parse");

    QByteArray bytes(0x300, '\0');
    writeLe32(&bytes, 0x3c, 0x80);
    writeLe16(&bytes, 0x86, 2);
    writeLe16(&bytes, 0x94, 256);
    writeLe16(&bytes, 0x98, 0x10b);
    writeLe32(&bytes, 0x98 + 92, 16);

    // AddressOfEntryPoint = 0x1050, an RVA inside the .text section below.
    writeLe32(&bytes, 0x98 + 16, 0x1050);

    const qsizetype sectionTable = 0x80 + 4 + 20 + 256;
    writeAscii(&bytes, sectionTable, ".text");
    writeLe32(&bytes, sectionTable + 12, 0x1000);
    writeLe32(&bytes, sectionTable + 16, 0x100);
    writeLe32(&bytes, sectionTable + 20, 0x200);

    const qsizetype secondSection = sectionTable + 40;
    writeAscii(&bytes, secondSection, ".idata");
    writeLe32(&bytes, secondSection + 12, 0x2000);
    writeLe32(&bytes, secondSection + 16, 0x80);
    writeLe32(&bytes, secondSection + 20, 0x280);

    TypeDecl *root = exportedNamed(&library, QStringLiteral("PE"));
    QVERIFY(root);
    auto rows = buildRows(&library, root, bytes);
    QCOMPARE(rows.size(), size_t(1));

    StructureRow *entry = findDescendantNamed(rows[0].get(), QStringLiteral("dword AddressOfEntryPoint"));
    QVERIFY2(entry, "AddressOfEntryPoint row not found in rendered tree");
    QVERIFY(entry->hasCodeTarget);
    QCOMPARE(entry->codeLogicalOffset, uint64_t(0x1050));
    QCOMPARE(entry->codeTargetOffset, uint64_t(0x250));
}

void StructViewTests::builderResolvesUnionDiscriminatorFromCandidateOnlyField()
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

void StructViewTests::definitionManagerFlagsNonStaticFieldReferences()
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

void StructViewTests::definitionManagerFlagsRuntimeExpressionsInRootOffsets()
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

void StructViewTests::semanticRegistryRunsKnownViewsAndIgnoresUnknownViews()
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

void StructViewTests::builderRunsSemanticViewsAfterDynamicPlacement()
{
    // Scenario: a PE import directory is declared as a dynamic structure and
    // marked with view("pe.imports").
    // Expected: raw IMAGE_IMPORT_DESCRIPTOR fields stay visible, then semantic
    // DLL/function rows are appended beneath the dynamically placed import row.
    // Regression guard: PE knowledge must augment dynamic rows after RVA mapping
    // has found the containing section, not replace the raw Strata rendering.
    ScopedEnvironmentVariable mode("Q22_PE_SEMANTIC_VIEW", "both");

    StrataLibrary library;
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
                        "  [dynamic_struct(case(Import), type(ImportDesc), offset(VirtualAddress), mapper(offset_map), optional(Size != 0))] DataDir dirs[1];\n"
                        "  [name(Name), dynamic_container(type(SECTION)), offset_map(VirtualAddress, SizeOfRawData, PointerToRawData)] Section sections[1];\n"
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
    QCOMPARE(rows[0]->children[2]->children.size(), size_t(2));

    StructureRow *dynamicImport = rows[0]->children[2]->children[0].get();
    QCOMPARE(dynamicImport->name, QStringLiteral("ImportDesc"));
    QCOMPARE(dynamicImport->children.size(), size_t(5));
    QCOMPARE(dynamicImport->children[0]->name, QStringLiteral("dword OriginalFirstThunk"));
    QCOMPARE(static_cast<int>(dynamicImport->children[0]->kind), static_cast<int>(StructureRowKind::Raw));

    // "Imports" is appended as a sibling of the raw descriptor row, under the
    // same SECTION container, rather than nested inside it -- see
    // interpretPeImports()'s containerRow handling in pesemanticview.cpp.
    StructureRow *importsRow = rows[0]->children[2]->children[1].get();
    QCOMPARE(static_cast<int>(importsRow->kind), static_cast<int>(StructureRowKind::Semantic));
    QCOMPARE(importsRow->name, QStringLiteral("Imports"));
    QCOMPARE(importsRow->children.size(), size_t(1));

    StructureRow *dllRow = importsRow->children[0].get();
    QCOMPARE(static_cast<int>(dllRow->kind), static_cast<int>(StructureRowKind::Semantic));
    QCOMPARE(dllRow->name, QStringLiteral("KERNEL32.dll"));
    verifyBranchIconsPresent(dllRow);
    QCOMPARE(dllRow->children.size(), size_t(0));
    QVERIFY(dllRow->lazyChildLoader);

    std::vector<std::unique_ptr<StructureRow>> modelRows;
    modelRows.push_back(std::move(rows[0]));
    StructureTreeModel model;
    model.setRowsForTests(std::move(modelRows));
    const QModelIndex rootIndex = model.index(0, StructureTreeModel::NameColumn);
    const QModelIndex sectionIndex = model.index(2, StructureTreeModel::NameColumn, rootIndex);
    const QModelIndex importsIndex = model.index(1, StructureTreeModel::NameColumn, sectionIndex);
    const QModelIndex dllIndex = model.index(0, StructureTreeModel::NameColumn, importsIndex);
    QVERIFY(dllIndex.isValid());
    QVERIFY(!(model.flags(dllIndex) & Qt::ItemIsEditable));
    QVERIFY(model.canFetchMore(dllIndex));
    model.fetchMore(dllIndex);
    QCOMPARE(model.rowCount(dllIndex), 1);
    const QModelIndex importNameIndex = model.index(0, StructureTreeModel::NameColumn, dllIndex);
    const QModelIndex importValueIndex = model.index(0, StructureTreeModel::ValueColumn, dllIndex);
    QCOMPARE(model.data(importNameIndex).toString(), QStringLiteral("Import CreateFileW"));
    QCOMPARE(model.data(importValueIndex).toString(), QStringLiteral("hint 4660"));
    QVERIFY(!model.canFetchMore(dllIndex));
}

void StructViewTests::builderPeSemanticModeDeclarativeSkipsCppViews()
{
    // Scenario: PE semantic mode is forced to declarative-only while a raw row
    // still carries view("pe.imports").
    // Expected: the C++ PE interpreter is skipped, leaving only the raw dynamic
    // import descriptor under the SECTION container.
    ScopedEnvironmentVariable mode("Q22_PE_SEMANTIC_VIEW", "declarative");

    StrataLibrary library;
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
                        "  [dynamic_struct(case(Import), type(ImportDesc), offset(VirtualAddress), mapper(offset_map), optional(Size != 0))] DataDir dirs[1];\n"
                        "  [name(Name), dynamic_container(type(SECTION)), offset_map(VirtualAddress, SizeOfRawData, PointerToRawData)] Section sections[1];\n"
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
    writeAscii(&bytes, 0xe0, "KERNEL32.dll");

    auto rows = buildRows(&library, firstExported(&library), bytes);
    QCOMPARE(rows.size(), size_t(1));
    StructureRow *section = findChildNamed(rows[0].get(), QStringLiteral("SECTION .idata"));
    QVERIFY(section);
    QCOMPARE(section->children.size(), size_t(1));
    QVERIFY(findChildNamed(section, QStringLiteral("ImportDesc")));
    QVERIFY(!findChildNamed(section, QStringLiteral("Imports")));
}

void StructViewTests::builderKeepsRawDynamicRowsWhenSemanticImportDataIsTruncated()
{
    // Scenario: a PE import directory row is present, but the imported DLL/name
    // tables are incomplete or outside the mapped bytes.
    // Expected: semantic interpretation stops quietly while the raw dynamic
    // IMAGE_IMPORT_DESCRIPTOR row and its fields remain available.
    // Regression guard: educational views must never make the base structure
    // renderer brittle when a file is malformed or partially loaded.
    ScopedEnvironmentVariable mode("Q22_PE_SEMANTIC_VIEW", "both");

    StrataLibrary library;
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
                        "  [dynamic_struct(case(Import), type(ImportDesc), offset(VirtualAddress), mapper(offset_map), optional(Size != 0))] DataDir dirs[1];\n"
                        "  [name(Name), dynamic_container(type(SECTION)), offset_map(VirtualAddress, SizeOfRawData, PointerToRawData)] Section sections[1];\n"
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

void StructViewTests::semanticPeImportsWalksPe32PlusThunkTables()
{
    // Scenario: PE32+ import lookup/address tables use 8-byte thunk entries.
    // Expected: the semantic import view advances by 8 bytes and resolves every
    // function name before the zero thunk terminator.
    // Regression guard: the first semantic importer treated all thunks as
    // 32-bit entries, which made 64-bit import tables unreliable.
    registerBuiltInStructureSemanticViews();

    QByteArray bytes(0x240, '\0');
    writeLe32(&bytes, 0x3c, 0x80);
    writeLe16(&bytes, 0x80 + 24, 0x20b);

    writeLe32(&bytes, 0x90, 0x1240);
    writeLe32(&bytes, 0x9c, 0x1260);
    writeLe32(&bytes, 0xa0, 0x1240);

    writeLe64(&bytes, 0xd0, 0x1270);
    writeLe64(&bytes, 0xd8, 0x1280);
    writeLe64(&bytes, 0xe0, 0);
    writeAscii(&bytes, 0xf0, "KERNEL64.dll");
    writeLe16(&bytes, 0x100, 1);
    writeAscii(&bytes, 0x102, "CreateFileW");
    writeLe16(&bytes, 0x110, 2);
    writeAscii(&bytes, 0x112, "CloseHandle");

    StructureRow root;
    StructureRow importRow(&root);
    importRow.absoluteOffset = 0x90;
    importRow.name = QStringLiteral("ImportDesc");
    root.children.push_back(std::make_unique<StructureRow>(&root));

    std::vector<StructureOffsetMap> maps = { StructureOffsetMap{ 0x1200, 0x200, 0x90 } };
    StructureSemanticContext context(nullptr,
                                     &root,
                                     &importRow,
                                     0,
                                     [&bytes](uint64_t offset, uint8_t *buffer, size_t length) -> size_t {
                                         if (offset >= static_cast<uint64_t>(bytes.size()))
                                             return 0;
                                         const size_t available = static_cast<size_t>(bytes.size() - static_cast<qsizetype>(offset));
                                         const size_t got = std::min(length, available);
                                         std::memcpy(buffer, bytes.constData() + offset, got);
                                         return got;
                                     },
                                     maps);

    QVERIFY(StructureSemanticViewRegistry::instance().run(QStringLiteral("pe.imports"), context));

    // "Imports" is appended as a sibling of the raw descriptor row (a child of
    // importRow's parent), not nested inside importRow itself -- see
    // interpretPeImports()'s containerRow handling in pesemanticview.cpp.
    StructureRow *importsRow = findChildNamed(&root, QStringLiteral("Imports"));
    QVERIFY(importsRow);
    QCOMPARE(importsRow->children.size(), size_t(1));
    QCOMPARE(importsRow->children[0]->name, QStringLiteral("KERNEL64.dll"));
    QCOMPARE(importsRow->children[0]->children.size(), size_t(0));
    QVERIFY(importsRow->children[0]->lazyChildLoader);
    auto imports = importsRow->children[0]->lazyChildLoader();
    QCOMPARE(imports.size(), size_t(2));
    QCOMPARE(imports[0]->name, QStringLiteral("Import CreateFileW"));
    QCOMPARE(imports[1]->name, QStringLiteral("Import CloseHandle"));
}

void StructViewTests::semanticPeImportsRespectDynamicArrayDescriptorCount()
{
    // Scenario: a dynamic_array import table has already rendered two descriptor
    // elements, but the following bytes happen to look like another descriptor.
    // Expected: the semantic overlay stops at the rendered table boundary
    // instead of reading into thunk/name data and flooding the tree.
    // Regression guard: real PE files were producing tens of thousands of
    // semantic rows because pe.imports ignored the dynamic-array extent.
    registerBuiltInStructureSemanticViews();

    QByteArray bytes(0x260, '\0');
    writeLe32(&bytes, 0x80 + 12, 0x1300);
    writeLe32(&bytes, 0x94 + 12, 0x1320);
    writeLe32(&bytes, 0xa8 + 12, 0x1340);
    writeAscii(&bytes, 0x180, "FIRST.dll");
    writeAscii(&bytes, 0x1a0, "SECOND.dll");
    writeAscii(&bytes, 0x1c0, "THIRD.dll");

    StructureRow root;
    StructureRow importTable(&root);
    importTable.absoluteOffset = 0x80;
    importTable.name = QStringLiteral("IMAGE_IMPORT_DESCRIPTOR Imports[]");
    importTable.nameTypePrefix = QStringLiteral("IMAGE_IMPORT_DESCRIPTOR");
    importTable.kind = StructureRowKind::Dynamic;
    importTable.children.push_back(std::make_unique<StructureRow>(&importTable));
    importTable.children.push_back(std::make_unique<StructureRow>(&importTable));

    std::vector<StructureOffsetMap> maps = { StructureOffsetMap{ 0x1200, 0x300, 0x80 } };
    StructureSemanticContext context(nullptr,
                                     &root,
                                     &importTable,
                                     0,
                                     [&bytes](uint64_t offset, uint8_t *buffer, size_t length) -> size_t {
                                         if (offset >= static_cast<uint64_t>(bytes.size()))
                                             return 0;
                                         const size_t available = static_cast<size_t>(bytes.size() - static_cast<qsizetype>(offset));
                                         const size_t got = std::min(length, available);
                                         std::memcpy(buffer, bytes.constData() + offset, got);
                                         return got;
                                     },
                                     maps);

    QVERIFY(StructureSemanticViewRegistry::instance().run(QStringLiteral("pe.imports"), context));

    // "Imports" is appended as a sibling of the raw descriptor table (a child
    // of importTable's parent), not nested inside importTable itself -- see
    // interpretPeImports()'s containerRow handling in pesemanticview.cpp.
    QCOMPARE(importTable.children.size(), size_t(2));
    StructureRow *importsRow = findChildNamed(&root, QStringLiteral("Imports"));
    QVERIFY(importsRow);
    QCOMPARE(importsRow->children.size(), size_t(2));
    QCOMPARE(importsRow->children[0]->name, QStringLiteral("FIRST.dll"));
    QCOMPARE(importsRow->children[1]->name, QStringLiteral("SECOND.dll"));
    QVERIFY(!findChildNamed(importsRow, QStringLiteral("THIRD.dll")));
}

void StructViewTests::builderAddsElfSectionAndSymbolSemanticRows()
{
    // Scenario: an ELF file has a section-header string table plus a symbol
    // table linked to its own string table.
    // Expected: declarative Strata emits named section rows and symbol children
    // under ELF Image, while the legacy C++ semantic view remains available in
    // cpp mode for comparison.
    // Regression guard: ELF's declarative view and C++ fallback must be
    // independently selectable while the declarative version reaches parity.
    StrataLibrary library;
    QVERIFY2(parseStandardElfDefinition(&library), "elf.strata failed to parse");

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

    TypeDecl *root = exportedNamed(&library, QStringLiteral("ELF"));
    QVERIFY(root);
    auto rows = buildRows(&library, root, bytes);
    QCOMPARE(rows.size(), size_t(1));
    QVERIFY(findChildNamed(rows[0].get(), QStringLiteral("Elf32_Shdr sectionHeaders32[]")));

    StructureRow *elfImage = findChildNamed(rows[0].get(), QStringLiteral("ELF Image"));
    QVERIFY2(elfImage, qPrintable(childNames(rows[0].get())));
    StructureRow *text = findChildNamed(elfImage, QStringLiteral("SECTION .text"));
    QVERIFY2(text, qPrintable(childNames(elfImage)));
    QCOMPARE(text->offset, QStringLiteral("00000200"));
    QCOMPARE(text->byteLength, uint64_t(4));
    QCOMPARE(findChildNamed(text, QStringLiteral("Name"))->value, QStringLiteral(".text"));
    QCOMPARE(findChildNamed(text, QStringLiteral("Type"))->value, QStringLiteral("SHT_PROGBITS"));
    QCOMPARE(findChildNamed(text, QStringLiteral("Size"))->value, QStringLiteral("4"));
    QVERIFY(!findChildNamed(text, QStringLiteral("SYMBOL main")));

    StructureRow *symtab = findChildNamed(elfImage, QStringLiteral("SECTION .symtab"));
    QVERIFY(symtab);
    StructureRow *symbol = findChildNamed(symtab, QStringLiteral("SYMBOL main"));
    QVERIFY2(symbol, qPrintable(childNames(symtab)));
    QCOMPARE(findChildNamed(symbol, QStringLiteral("Name"))->value, QStringLiteral("main"));
    QCOMPARE(findChildNamed(symbol, QStringLiteral("Value"))->value, QStringLiteral("4096"));
    QCOMPARE(findChildNamed(symbol, QStringLiteral("Size"))->value, QStringLiteral("4"));

    {
        ScopedEnvironmentVariable mode("Q22_ELF_SEMANTIC_VIEW", "cpp");
        auto cppRows = buildRows(&library, root, bytes);
        QCOMPARE(cppRows.size(), size_t(1));
        QVERIFY(!findChildNamed(cppRows[0].get(), QStringLiteral("ELF Image")));

        StructureRow *cppText = findChildNamed(cppRows[0].get(), QStringLiteral("SECTION .text"));
        QVERIFY(cppText);
        QCOMPARE(cppText->offset, QStringLiteral("00000200"));
        QCOMPARE(cppText->byteLength, uint64_t(4));

        StructureRow *cppSymtab = findChildNamed(cppRows[0].get(), QStringLiteral("SECTION .symtab"));
        QVERIFY(cppSymtab);
        StructureRow *cppSymbol = findChildNamed(cppSymtab, QStringLiteral("SYMBOL main"));
        QVERIFY(cppSymbol);
        QCOMPARE(cppSymbol->value, QStringLiteral("value 0x1000, size 4"));
    }
}

void StructViewTests::builderKeepsRawElfRowsWhenSemanticDataIsTruncated()
{
    // Scenario: an ELF header points at a section table, but the string table
    // data needed by the educational semantic pass is missing.
    // Expected: semantic rows are skipped quietly while raw header/table rows
    // from Strata remain available.
    // Regression guard: malformed ELF files must not make Structure View blank
    // or fail just because name resolution cannot complete.
    StrataLibrary library;
    QVERIFY2(parseStandardElfDefinition(&library), "elf.strata failed to parse");

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

    TypeDecl *root = exportedNamed(&library, QStringLiteral("ELF"));
    QVERIFY(root);
    auto rows = buildRows(&library, root, bytes);
    QCOMPARE(rows.size(), size_t(1));
    QVERIFY(findChildNamed(rows[0].get(), QStringLiteral("Elf32_Ehdr header32")));
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

void StructViewTests::modelFetchesLazyChildrenOnceAndFormatsOffsets()
{
    // Scenario: semantic rows can defer expensive child creation until the user
    // expands that row in the tree.
    // Expected: the model advertises fetchable children, inserts them once with
    // correct parent links, and applies the current offset display options.
    // Regression guard: lazy semantic import functions must look like normal
    // model rows without forcing the whole PE import tree to materialise.
    auto root = std::make_unique<StructureRow>();
    root->name = QStringLiteral("root");
    root->value = QStringLiteral("{...}");
    root->kind = StructureRowKind::Semantic;
    root->absoluteOffset = 0x1000;
    root->relativeOffset = 0;
    root->byteLength = 0x100;
    int loadCount = 0;
    root->lazyChildLoader = [&loadCount]() {
        ++loadCount;
        std::vector<std::unique_ptr<StructureRow>> rows;
        auto child = std::make_unique<StructureRow>();
        child->name = QStringLiteral("lazy child");
        child->kind = StructureRowKind::Semantic;
        child->absoluteOffset = 0x1010;
        child->relativeOffset = 0x10;
        child->byteLength = 4;
        child->offset = QStringLiteral("00001010");
        child->generatedOffset = true;
        rows.push_back(std::move(child));
        return rows;
    };

    std::vector<std::unique_ptr<StructureRow>> rows;
    rows.push_back(std::move(root));

    StructureTreeModel model;
    model.setRowsForTests(std::move(rows));
    StructureDisplayOptions options;
    options.hexadecimalOffsets = true;
    options.relativeOffsets = true;
    model.applyDisplayOptions(options);

    const QModelIndex rootIndex = model.index(0, StructureTreeModel::NameColumn);
    QVERIFY(rootIndex.isValid());
    QVERIFY(model.hasChildren(rootIndex));
    QVERIFY(model.canFetchMore(rootIndex));
    QCOMPARE(model.rowCount(rootIndex), 0);

    model.fetchMore(rootIndex);
    QCOMPARE(loadCount, 1);
    QVERIFY(!model.canFetchMore(rootIndex));
    QCOMPARE(model.rowCount(rootIndex), 1);

    const QModelIndex childName = model.index(0, StructureTreeModel::NameColumn, rootIndex);
    const QModelIndex childOffset = model.index(0, StructureTreeModel::OffsetColumn, rootIndex);
    QVERIFY(childName.isValid());
    QCOMPARE(model.data(childName).toString(), QStringLiteral("lazy child"));
    QCOMPARE(model.data(childOffset).toString(), QStringLiteral("+0010"));
    QCOMPARE(model.rowForIndex(childName)->parent, model.rowForIndex(rootIndex));

    model.fetchMore(rootIndex);
    QCOMPARE(loadCount, 1);
    QCOMPARE(model.rowCount(rootIndex), 1);
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
    child->valueChoices = { QStringLiteral("IMAGE_FILE_MACHINE_I386"),
                            QStringLiteral("IMAGE_FILE_MACHINE_AMD64") };
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
    QVERIFY(model.data(childValue, StructureTreeModel::HasValueChoicesRole).toBool());
    QVERIFY(model.setData(childValue, QStringLiteral("IMAGE_FILE_MACHINE_I386")));
    QCOMPARE(model.data(childValue).toString(), QStringLiteral("IMAGE_FILE_MACHINE_I386"));
    QCOMPARE(model.data(childValue, StructureTreeModel::ValueChoicesRole).toStringList(),
             QStringList({ QStringLiteral("IMAGE_FILE_MACHINE_I386"),
                           QStringLiteral("IMAGE_FILE_MACHINE_AMD64") }));

    const QModelIndex childOffset = model.index(0, StructureTreeModel::OffsetColumn, parentIndex);
    QVERIFY(childOffset.isValid());
    QVERIFY(!(model.flags(childOffset) & Qt::ItemIsEditable));
}

void StructViewTests::modelAppliesTypeDisplayOptionsWithoutResettingRows()
{
    // Scenario: the user right-clicks Structure View and changes only the type
    // display lens, for example from Strata aliases to primitive storage types.
    // Expected: visible name text updates in place, preserving the existing row
    // objects so expansion, scroll position, and selection do not jump.
    // Regression guard: the first implementation rebuilt the whole tree for a
    // cosmetic setting, causing expanded structures to collapse unnecessarily.
    Parser parser;
    QVERIFY(parseBuffer(parser,
                        "typedef dword e32_addr;\n"
                        "[export]\n"
                        "struct Root { byte pad[4]; e32_addr entry; } root;\n"));
    TypeDecl *root = firstExported(parser.GetStrataLibrary());
    QVERIFY(root != nullptr);

    auto rows = buildRows(parser.GetStrataLibrary(), root, QByteArray(32, '\0'), 0x10);
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

    const QString userDir = temp.filePath(QStringLiteral("user-strata"));
    QVERIFY(QDir().mkpath(userDir));
    writeTextFile(QDir(userDir).filePath(QStringLiteral("types.txt")),
                  "[export]\n"
                  "struct Root { dword magic; word flags; } root;\n");

    StructureDefinitionManager manager;
    manager.setBuiltinStructDirsForTests({});
    manager.setUserStrataDirForTests(userDir);
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
