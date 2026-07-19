#include "../structview_testsupport.h"

class StructViewPeTests : public QObject
{
    Q_OBJECT

private slots:
    void builderNamesPeDynamicSectionsFromStandardDefinition();
    void builderPeSemanticModeCppSkipsDeclarativeRows();
    void builderNamesPeImportDescriptorsFromStandardDefinition();
    void builderEmitsPeImportDllsFromStandardDefinition();
    void builderEmitsPeBaseRelocationsFromStandardDefinition();
    void builderEmitsPeDebugDirectoriesFromStandardDefinition();
    void builderEmitsPeTlsDirectoryFromStandardDefinition();
    void builderEmitsPeLoadConfigFromStandardDefinition();
    void builderEmitsPeExceptionDirectoryFromStandardDefinition();
    void builderResolvesEntryPointRvaThroughSectionOffsetMap();
    void builderPeSemanticModeDeclarativeSkipsCppViews();
    void builderKeepsRawDynamicRowsWhenSemanticImportDataIsTruncated();
    void semanticPeImportsWalksPe32PlusThunkTables();
    void semanticPeImportsRespectDynamicArrayDescriptorCount();
    void builderRunsSemanticViewsAfterDynamicPlacement();
};

void StructViewPeTests::builderNamesPeDynamicSectionsFromStandardDefinition()
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
    writeLe32(&bytes, sectionTable + 36, 0x60500020);

    const qsizetype secondSection = sectionTable + 40;
    writeAscii(&bytes, secondSection, ".idata");
    writeLe32(&bytes, secondSection + 12, 0x2000);
    writeLe32(&bytes, secondSection + 16, 0x80);
    writeLe32(&bytes, secondSection + 20, 0x280);
    writeLe32(&bytes, secondSection + 36, 0xC0000040);

    TypeDecl *root = exportedNamed(&library, QStringLiteral("PE"));
    QVERIFY(root);
    auto rows = buildRows(&library, root, bytes);
    StructureRow *peRow = findTopLevelNamed(rows, QStringLiteral("PE"));
    QVERIFY(peRow);

    StructureRow *text = findChildNamed(peRow, QStringLiteral("SECTION .text"));
    QVERIFY(text);
    QCOMPARE(text->offset, QStringLiteral("00000200"));
    QVERIFY(text->branchIconPath.isEmpty());
    QVERIFY(!text->name.startsWith(QStringLiteral("SECTION - ")));

    StructureRow *idata = findChildNamed(peRow, QStringLiteral("SECTION .idata"));
    QVERIFY(idata);
    QCOMPARE(idata->offset, QStringLiteral("00000280"));
    QVERIFY(idata->branchIconPath.isEmpty());
    QVERIFY(!idata->name.startsWith(QStringLiteral("SECTION - ")));

    StructureRow *peImage = findSemanticRootChildNamed(rows, QStringLiteral("PE Image"));
    QVERIFY2(peImage, "PE Image semantic child row not found");
    QVERIFY(!findChildNamed(peImage, QStringLiteral("Sections")));
    QCOMPARE(peImage->children.size(), size_t(2));
    QCOMPARE(peImage->children[0]->name, QStringLiteral("SECTION .text"));
    QCOMPARE(peImage->children[0]->offset, QStringLiteral("00000200"));
    QCOMPARE(peImage->children[0]->byteLength, uint64_t(0x100));
    QCOMPARE(peImage->children[1]->name, QStringLiteral("SECTION .idata"));
    QCOMPARE(peImage->children[1]->offset, QStringLiteral("00000280"));
    QCOMPARE(peImage->children[1]->byteLength, uint64_t(0x80));

    StructureRow *textBytes = findChildNamed(peImage->children[0].get(), QStringLiteral("Bytes"));
    QVERIFY(textBytes);
    QCOMPARE(textBytes->byteLength, uint64_t(0x100));

    StructureRow *fileCharacteristics = findDescendantNamed(peRow, QStringLiteral("word Characteristics"));
    QVERIFY(fileCharacteristics);
    QCOMPARE(fileCharacteristics->value, QStringLiteral("IMAGE_FILE_EXECUTABLE_IMAGE | IMAGE_FILE_DLL"));
    QVERIFY(findChildNamed(fileCharacteristics, QStringLiteral("IMAGE_FILE_EXECUTABLE_IMAGE")));
    QVERIFY(findChildNamed(fileCharacteristics, QStringLiteral("IMAGE_FILE_DLL")));

    StructureRow *dllCharacteristics = findDescendantNamed(peRow, QStringLiteral("word DllCharacteristics"));
    QVERIFY(dllCharacteristics);
    QCOMPARE(dllCharacteristics->value, QStringLiteral("IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE | IMAGE_DLLCHARACTERISTICS_NX_COMPAT"));
    QVERIFY(findChildNamed(dllCharacteristics, QStringLiteral("IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE")));
    QVERIFY(findChildNamed(dllCharacteristics, QStringLiteral("IMAGE_DLLCHARACTERISTICS_NX_COMPAT")));

    StructureRow *textHeader = findDescendantNamed(peRow, QStringLiteral("[0].text"));
    QVERIFY(textHeader);
    StructureRow *textCharacteristics = findChildNamed(textHeader, QStringLiteral("dword Characteristics"));
    QVERIFY(textCharacteristics);
    QCOMPARE(textCharacteristics->value, QStringLiteral("IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ"));
    QVERIFY(findChildNamed(textCharacteristics, QStringLiteral("IMAGE_SCN_CNT_CODE")));
    StructureRow *textAlignment = findChildNamed(textCharacteristics, QStringLiteral("Alignment"));
    QVERIFY(textAlignment);
    QCOMPARE(textAlignment->value, QStringLiteral("IMAGE_SCN_ALIGN_16BYTES"));
    QCOMPARE(findChildNamed(textCharacteristics, QStringLiteral("IMAGE_SCN_MEM_EXECUTE"))->value, QStringLiteral("1"));
    QCOMPARE(findChildNamed(textCharacteristics, QStringLiteral("IMAGE_SCN_MEM_READ"))->value, QStringLiteral("1"));

    StructureRow *idataHeader = findDescendantNamed(peRow, QStringLiteral("[1].idata"));
    QVERIFY(idataHeader);
    StructureRow *idataCharacteristics = findChildNamed(idataHeader, QStringLiteral("dword Characteristics"));
    QVERIFY(idataCharacteristics);
    QCOMPARE(idataCharacteristics->value, QStringLiteral("IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE"));
    QCOMPARE(findChildNamed(idataCharacteristics, QStringLiteral("IMAGE_SCN_CNT_INITIALIZED_DATA"))->value, QStringLiteral("1"));
    QCOMPARE(findChildNamed(idataCharacteristics, QStringLiteral("IMAGE_SCN_MEM_READ"))->value, QStringLiteral("1"));
    QCOMPARE(findChildNamed(idataCharacteristics, QStringLiteral("IMAGE_SCN_MEM_WRITE"))->value, QStringLiteral("1"));

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

void StructViewPeTests::builderPeSemanticModeCppSkipsDeclarativeRows()
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
    StructureRow *peRow = findTopLevelNamed(rows, QStringLiteral("PE"));
    QVERIFY(peRow);

    QVERIFY(findChildNamed(peRow, QStringLiteral("SECTION .text")));
    QVERIFY(!findSemanticRootChildNamed(rows, QStringLiteral("PE Image")));
}

void StructViewPeTests::builderNamesPeImportDescriptorsFromStandardDefinition()
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
    StructureRow *peRow = findTopLevelNamed(rows, QStringLiteral("PE"));
    QVERIFY(peRow);

    StructureRow *descriptors = findDescendantNamed(peRow, QStringLiteral("IMAGE_IMPORT_DESCRIPTOR[]"));
    QVERIFY2(descriptors, "IMAGE_IMPORT_DESCRIPTOR[] container not found in rendered tree");
    QCOMPARE(descriptors->children.size(), size_t(1));
    QVERIFY2(descriptors->children[0]->name.contains(QStringLiteral("KERNEL32.dll")),
             qPrintable(QStringLiteral("descriptor[0] name missing DLL hint: %1").arg(descriptors->children[0]->name)));
}

void StructViewPeTests::builderEmitsPeImportDllsFromStandardDefinition()
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
    StructureRow *peRow = findTopLevelNamed(rows, QStringLiteral("PE"));
    QVERIFY(peRow);

    StructureRow *peImage = findSemanticRootChildNamed(rows, QStringLiteral("PE Image"));
    QVERIFY2(peImage, "PE Image semantic child row not found");
    StructureRow *idata = findChildNamed(peImage, QStringLiteral("SECTION .idata"));
    QVERIFY2(idata, qPrintable(childNames(peImage)));

    StructureRow *rawIdata = findChildNamed(peRow, QStringLiteral("SECTION .idata"));
    QVERIFY2(rawIdata, qPrintable(childNames(peRow)));
    StructureRow *exportDirectory = findDescendantNamed(rawIdata, QStringLiteral("IMAGE_EXPORT_DIRECTORY"));
    QVERIFY2(exportDirectory, qPrintable(childNames(rawIdata)));
    QVERIFY(exportDirectory->branchIconPath.isEmpty());
    StructureRow *exportFunctions = findChildNamed(exportDirectory, QStringLiteral("PE_EXPORT_FUNCTION_RVA ExportFunctions[]"));
    QVERIFY2(exportFunctions,
             qPrintable(childNames(exportDirectory)));
    QVERIFY(exportFunctions->branchIconPath.isEmpty());
    QVERIFY2(findChildNamed(exportDirectory, QStringLiteral("PE_EXPORT_NAME_RVA ExportNames[]")),
             qPrintable(childNames(exportDirectory)));
    QVERIFY2(findChildNamed(exportDirectory, QStringLiteral("PE_EXPORT_ORDINAL_INDEX ExportOrdinals[]")),
             qPrintable(childNames(exportDirectory)));

    StructureRow *imports = findChildNamed(idata, QStringLiteral("Imports"));
    QVERIFY2(imports, qPrintable(childNames(idata)));
    QCOMPARE(imports->branchIconPath, QString::fromLatin1(StructureBranchIcons::kBlueStructure));

    StructureRow *kernel32 = findChildNamed(imports, QStringLiteral("KERNEL32.dll"));
    QVERIFY2(kernel32, qPrintable(childNames(imports)));
    QCOMPARE(kernel32->branchIconPath, QString::fromLatin1(StructureBranchIcons::kBlueEntity));
    QCOMPARE(kernel32->offset, QStringLiteral("00000294"));
    QCOMPARE(static_cast<int>(kernel32->kind), static_cast<int>(StructureRowKind::Semantic));
    StructureRow *function = findChildNamed(kernel32, QStringLiteral("CreateFileW"));
    QVERIFY2(function, qPrintable(childNames(kernel32)));
    QCOMPARE(function->branchIconPath, QString::fromLatin1(StructureBranchIcons::kBlueElement));
    QCOMPARE(function->offset, QStringLiteral("000002C0"));
    QCOMPARE(static_cast<int>(function->kind), static_cast<int>(StructureRowKind::Semantic));
    QVERIFY(!findChildNamed(imports, QStringLiteral("sclib-csharp.dll")));
    QVERIFY(!findChildNamed(imports, QStringLiteral("Imports")));
    QVERIFY(!findChildNamed(imports, QStringLiteral("IMAGE_IMPORT_DESCRIPTOR[]")));

    StructureRow *exports = findChildNamed(idata, QStringLiteral("Exports"));
    QVERIFY2(exports, qPrintable(childNames(idata)));
    StructureRow *exportedFunction = findChildNamed(exports, QStringLiteral("DllGetClassObject"));
    QVERIFY2(exportedFunction, qPrintable(childNames(exports)));
    QCOMPARE(exportedFunction->branchIconPath, QString::fromLatin1(StructureBranchIcons::kBlueElement));
    QCOMPARE(exportedFunction->offset, QStringLiteral("00000310"));
    QCOMPARE(static_cast<int>(exportedFunction->kind), static_cast<int>(StructureRowKind::Semantic));
    QCOMPARE(findChildNamed(exportedFunction, QStringLiteral("Ordinal"))->value, QStringLiteral("1"));
    QCOMPARE(findChildNamed(exportedFunction, QStringLiteral("TargetRva"))->value, QStringLiteral("4096"));
    QVERIFY(!findChildNamed(exports, QStringLiteral("sclib-csharp.dll")));
}

void StructViewPeTests::builderEmitsPeBaseRelocationsFromStandardDefinition()
{
    ScopedEnvironmentVariable mode("Q22_PE_SEMANTIC_VIEW", "declarative");

    StrataLibrary library;
    QVERIFY2(parseStandardDefinition(&library, QStringLiteral("pe.strata")), "pe.strata failed to parse");

    QByteArray bytes(0x340, '\0');
    writeLe32(&bytes, 0x3c, 0x80);
    writeLe16(&bytes, 0x86, 1);
    writeLe16(&bytes, 0x94, 256);
    writeLe16(&bytes, 0x98, 0x10b);
    writeLe32(&bytes, 0x98 + 92, 16);

    // The base relocation directory is a single 12-byte block at RVA 0x2000:
    // one HIGHLOW entry for RVA 0x3123 and one ABSOLUTE padding entry.
    const qsizetype relocDirEntry = 0x98 + 96 + 5 * 8;
    writeLe32(&bytes, relocDirEntry, 0x2000);
    writeLe32(&bytes, relocDirEntry + 4, 12);

    const qsizetype sectionTable = 0x80 + 4 + 20 + 256;
    writeAscii(&bytes, sectionTable, ".reloc");
    writeLe32(&bytes, sectionTable + 12, 0x2000);
    writeLe32(&bytes, sectionTable + 16, 0x100);
    writeLe32(&bytes, sectionTable + 20, 0x200);

    writeLe32(&bytes, 0x200, 0x3000);
    writeLe32(&bytes, 0x204, 12);
    writeLe16(&bytes, 0x208, 0x3123);
    writeLe16(&bytes, 0x20a, 0);

    TypeDecl *root = exportedNamed(&library, QStringLiteral("PE"));
    QVERIFY(root);
    auto rows = buildRows(&library, root, bytes);

    StructureRow *peRow = findTopLevelNamed(rows, QStringLiteral("PE"));
    QVERIFY(peRow);
    StructureRow *rawRelocSection = findChildNamed(peRow, QStringLiteral("SECTION .reloc"));
    QVERIFY2(rawRelocSection, qPrintable(childNames(peRow)));
    StructureRow *blocks = findDescendantNamed(rawRelocSection, QStringLiteral("IMAGE_BASE_RELOCATION IMAGE_DIRECTORY_ENTRY_BASERELOC[]"));
    QVERIFY2(blocks, qPrintable(childNames(rawRelocSection)));
    QCOMPARE(blocks->children.size(), size_t(1));
    StructureRow *entries = findChildNamed(blocks->children[0].get(), QStringLiteral("IMAGE_BASE_RELOCATION_ENTRY Entries[]"));
    QVERIFY2(entries, qPrintable(childNames(blocks->children[0].get())));
    QCOMPARE(entries->children.size(), size_t(2));
    QVERIFY(entries->children[0]->typeDecl);
    QVERIFY(FindTag(entries->children[0]->typeDecl->tagList, TOK_EMITNODE, nullptr));

    StructureRow *peImage = findSemanticRootChildNamed(rows, QStringLiteral("PE Image"));
    QVERIFY(peImage);
    StructureRow *relocSection = findChildNamed(peImage, QStringLiteral("SECTION .reloc"));
    QVERIFY2(relocSection, qPrintable(childNames(peImage)));
    StructureRow *relocations = findChildNamed(relocSection, QStringLiteral("Relocations"));
    QVERIFY2(relocations, qPrintable(childNames(relocSection)));
    QCOMPARE(relocations->children.size(), size_t(2));
    StructureRow *relocation = relocations->children[0].get();
    QCOMPARE(findChildNamed(relocation, QStringLiteral("Type"))->value, QStringLiteral("3"));
    QCOMPARE(findChildNamed(relocation, QStringLiteral("Offset"))->value, QStringLiteral("291"));
    QCOMPARE(findChildNamed(relocation, QStringLiteral("TargetRva"))->value, QStringLiteral("12579"));

}

void StructViewPeTests::builderEmitsPeDebugDirectoriesFromStandardDefinition()
{
    ScopedEnvironmentVariable mode("Q22_PE_SEMANTIC_VIEW", "declarative");

    StrataLibrary library;
    QVERIFY2(parseStandardDefinition(&library, QStringLiteral("pe.strata")), "pe.strata failed to parse");

    QByteArray bytes(0x340, '\0');
    writeLe32(&bytes, 0x3c, 0x80);
    writeLe16(&bytes, 0x86, 1);
    writeLe16(&bytes, 0x94, 256);
    writeLe16(&bytes, 0x98, 0x10b);
    writeLe32(&bytes, 0x98 + 92, 16);

    const qsizetype debugDirEntry = 0x98 + 96 + 6 * 8;
    writeLe32(&bytes, debugDirEntry, 0x2000);
    writeLe32(&bytes, debugDirEntry + 4, 28);

    const qsizetype sectionTable = 0x80 + 4 + 20 + 256;
    writeAscii(&bytes, sectionTable, ".rdata");
    writeLe32(&bytes, sectionTable + 12, 0x2000);
    writeLe32(&bytes, sectionTable + 16, 0x100);
    writeLe32(&bytes, sectionTable + 20, 0x200);

    // IMAGE_DEBUG_DIRECTORY: CodeView data at RVA 0x2040, file offset 0x240.
    writeLe32(&bytes, 0x20c, 2);
    writeLe32(&bytes, 0x210, 36);
    writeLe32(&bytes, 0x214, 0x2040);
    writeLe32(&bytes, 0x218, 0x240);

    TypeDecl *root = exportedNamed(&library, QStringLiteral("PE"));
    QVERIFY(root);
    auto rows = buildRows(&library, root, bytes);

    StructureRow *peRow = findTopLevelNamed(rows, QStringLiteral("PE"));
    QVERIFY(peRow);
    StructureRow *rawSection = findChildNamed(peRow, QStringLiteral("SECTION .rdata"));
    QVERIFY2(rawSection, qPrintable(childNames(peRow)));
    StructureRow *debugDirectories = findDescendantNamed(rawSection,
        QStringLiteral("IMAGE_DEBUG_DIRECTORY IMAGE_DIRECTORY_ENTRY_DEBUG[]"));
    QVERIFY2(debugDirectories, qPrintable(childNames(rawSection)));
    QCOMPARE(debugDirectories->children.size(), size_t(1));

    StructureRow *peImage = findSemanticRootChildNamed(rows, QStringLiteral("PE Image"));
    QVERIFY(peImage);
    StructureRow *semanticSection = findChildNamed(peImage, QStringLiteral("SECTION .rdata"));
    QVERIFY2(semanticSection, qPrintable(childNames(peImage)));
    StructureRow *debug = findChildNamed(semanticSection, QStringLiteral("Debug"));
    QVERIFY2(debug, qPrintable(childNames(semanticSection)));
    QVERIFY2(debug->children.size() == size_t(1), qPrintable(childNames(debug)));
    StructureRow *entry = debug->children[0].get();
    QCOMPARE(findChildNamed(entry, QStringLiteral("Type"))->value, QStringLiteral("2"));
    QCOMPARE(findChildNamed(entry, QStringLiteral("SizeOfData"))->value, QStringLiteral("36"));
    QCOMPARE(findChildNamed(entry, QStringLiteral("AddressOfRawData"))->value, QStringLiteral("8256"));
    QCOMPARE(findChildNamed(entry, QStringLiteral("PointerToRawData"))->value, QStringLiteral("576"));
}

void StructViewPeTests::builderEmitsPeTlsDirectoryFromStandardDefinition()
{
    ScopedEnvironmentVariable mode("Q22_PE_SEMANTIC_VIEW", "declarative");

    StrataLibrary library;
    QVERIFY2(parseStandardDefinition(&library, QStringLiteral("pe.strata")), "pe.strata failed to parse");

    QByteArray bytes(0x340, '\0');
    writeLe32(&bytes, 0x3c, 0x80);
    writeLe16(&bytes, 0x86, 1);
    writeLe16(&bytes, 0x94, 256);
    writeLe16(&bytes, 0x98, 0x10b);
    writeLe32(&bytes, 0x98 + 92, 16);

    const qsizetype tlsDirEntry = 0x98 + 96 + 9 * 8;
    writeLe32(&bytes, tlsDirEntry, 0x2000);
    writeLe32(&bytes, tlsDirEntry + 4, 24);

    const qsizetype sectionTable = 0x80 + 4 + 20 + 256;
    writeAscii(&bytes, sectionTable, ".rdata");
    writeLe32(&bytes, sectionTable + 12, 0x2000);
    writeLe32(&bytes, sectionTable + 16, 0x100);
    writeLe32(&bytes, sectionTable + 20, 0x200);

    writeLe32(&bytes, 0x200, 0x401000);
    writeLe32(&bytes, 0x204, 0x401020);
    writeLe32(&bytes, 0x208, 0x402000);
    writeLe32(&bytes, 0x20c, 0x403000);
    writeLe32(&bytes, 0x210, 16);
    writeLe32(&bytes, 0x214, 0);

    TypeDecl *root = exportedNamed(&library, QStringLiteral("PE"));
    QVERIFY(root);
    auto rows = buildRows(&library, root, bytes);

    StructureRow *peRow = findTopLevelNamed(rows, QStringLiteral("PE"));
    QVERIFY(peRow);
    StructureRow *rawSection = findChildNamed(peRow, QStringLiteral("SECTION .rdata"));
    QVERIFY2(rawSection, qPrintable(childNames(peRow)));
    QVERIFY2(findDescendantNamed(rawSection, QStringLiteral("IMAGE_TLS_DIRECTORY32")),
             qPrintable(childNames(rawSection)));

    StructureRow *peImage = findSemanticRootChildNamed(rows, QStringLiteral("PE Image"));
    QVERIFY(peImage);
    StructureRow *semanticSection = findChildNamed(peImage, QStringLiteral("SECTION .rdata"));
    QVERIFY2(semanticSection, qPrintable(childNames(peImage)));
    StructureRow *tls = findChildNamed(semanticSection, QStringLiteral("TLS"));
    QVERIFY2(tls, qPrintable(childNames(semanticSection)));
    QCOMPARE(tls->children.size(), size_t(1));
    StructureRow *entry = tls->children[0].get();
    QCOMPARE(findChildNamed(entry, QStringLiteral("StartAddressOfRawData"))->value, QStringLiteral("4198400"));
    QCOMPARE(findChildNamed(entry, QStringLiteral("AddressOfCallbacks"))->value, QStringLiteral("4206592"));
    QCOMPARE(findChildNamed(entry, QStringLiteral("SizeOfZeroFill"))->value, QStringLiteral("16"));
}

void StructViewPeTests::builderEmitsPeLoadConfigFromStandardDefinition()
{
    ScopedEnvironmentVariable mode("Q22_PE_SEMANTIC_VIEW", "declarative");

    StrataLibrary library;
    QVERIFY2(parseStandardDefinition(&library, QStringLiteral("pe.strata")), "pe.strata failed to parse");

    QByteArray bytes(0x400, '\0');
    writeLe32(&bytes, 0x3c, 0x80);
    writeLe16(&bytes, 0x86, 1);
    writeLe16(&bytes, 0x94, 256);
    writeLe16(&bytes, 0x98, 0x20b);
    writeLe32(&bytes, 0x98 + 108, 16);

    const qsizetype loadConfigDirEntry = 0x98 + 112 + 10 * 8;
    writeLe32(&bytes, loadConfigDirEntry, 0x2000);
    writeLe32(&bytes, loadConfigDirEntry + 4, 0x9c);

    const qsizetype sectionTable = 0x80 + 4 + 20 + 256;
    writeAscii(&bytes, sectionTable, ".rdata");
    writeLe32(&bytes, sectionTable + 12, 0x2000);
    writeLe32(&bytes, sectionTable + 16, 0x200);
    writeLe32(&bytes, sectionTable + 20, 0x200);

    writeLe32(&bytes, 0x200, 0x9c);
    writeLe64(&bytes, 0x258, 0x140005000);
    writeLe64(&bytes, 0x270, 0x140006000);
    writeLe64(&bytes, 0x278, 0x140007000);
    writeLe64(&bytes, 0x280, 0x140008000);
    writeLe64(&bytes, 0x288, 42);
    writeLe32(&bytes, 0x290, 0x30000500);

    TypeDecl *root = exportedNamed(&library, QStringLiteral("PE"));
    QVERIFY(root);
    auto rows = buildRows(&library, root, bytes);

    StructureRow *peRow = findTopLevelNamed(rows, QStringLiteral("PE"));
    QVERIFY(peRow);
    StructureRow *rawSection = findChildNamed(peRow, QStringLiteral("SECTION .rdata"));
    QVERIFY2(rawSection, qPrintable(childNames(peRow)));
    QVERIFY2(findDescendantNamed(rawSection, QStringLiteral("IMAGE_LOAD_CONFIG_DIRECTORY64")),
             qPrintable(childNames(rawSection)));
    StructureRow *rawGuardFlags = findDescendantNamed(rawSection, QStringLiteral("dword GuardFlags"));
    QVERIFY2(rawGuardFlags, qPrintable(childNames(rawSection)));
    QCOMPARE(rawGuardFlags->value, QStringLiteral("IMAGE_GUARD_CF_INSTRUMENTED | IMAGE_GUARD_CF_FUNCTION_TABLE_PRESENT"));
    QCOMPARE(findChildNamed(rawGuardFlags, QStringLiteral("IMAGE_GUARD_CF_INSTRUMENTED"))->value, QStringLiteral("1"));
    QCOMPARE(findChildNamed(rawGuardFlags, QStringLiteral("IMAGE_GUARD_CF_FUNCTION_TABLE_PRESENT"))->value, QStringLiteral("1"));
    QCOMPARE(findChildNamed(rawGuardFlags, QStringLiteral("Function table size"))->value, QStringLiteral("3"));

    StructureRow *peImage = findSemanticRootChildNamed(rows, QStringLiteral("PE Image"));
    QVERIFY(peImage);
    StructureRow *semanticSection = findChildNamed(peImage, QStringLiteral("SECTION .rdata"));
    QVERIFY2(semanticSection, qPrintable(childNames(peImage)));
    StructureRow *loadConfig = findChildNamed(semanticSection, QStringLiteral("LoadConfig"));
    QVERIFY2(loadConfig, qPrintable(childNames(semanticSection)));
    QCOMPARE(loadConfig->children.size(), size_t(1));
    StructureRow *entry = loadConfig->children[0].get();
    QCOMPARE(findChildNamed(entry, QStringLiteral("SecurityCookie"))->value, QStringLiteral("5368729600"));
    QCOMPARE(findChildNamed(entry, QStringLiteral("GuardCFFunctionTable"))->value, QStringLiteral("5368741888"));
    QCOMPARE(findChildNamed(entry, QStringLiteral("GuardCFFunctionCount"))->value, QStringLiteral("42"));
    QCOMPARE(findChildNamed(entry, QStringLiteral("GuardFlags"))->value,
             QStringLiteral("IMAGE_GUARD_CF_INSTRUMENTED | IMAGE_GUARD_CF_FUNCTION_TABLE_PRESENT"));
}

void StructViewPeTests::builderEmitsPeExceptionDirectoryFromStandardDefinition()
{
    ScopedEnvironmentVariable mode("Q22_PE_SEMANTIC_VIEW", "declarative");

    StrataLibrary library;
    QVERIFY2(parseStandardDefinition(&library, QStringLiteral("pe.strata")), "pe.strata failed to parse");

    QByteArray bytes(0x400, '\0');
    writeLe32(&bytes, 0x3c, 0x80);
    writeLe16(&bytes, 0x86, 1);
    writeLe16(&bytes, 0x94, 256);
    writeLe16(&bytes, 0x98, 0x20b);
    writeLe32(&bytes, 0x98 + 108, 16);

    const qsizetype exceptionDirEntry = 0x98 + 112 + 3 * 8;
    writeLe32(&bytes, exceptionDirEntry, 0x2000);
    writeLe32(&bytes, exceptionDirEntry + 4, 24);

    const qsizetype sectionTable = 0x80 + 4 + 20 + 256;
    writeAscii(&bytes, sectionTable, ".pdata");
    writeLe32(&bytes, sectionTable + 12, 0x2000);
    writeLe32(&bytes, sectionTable + 16, 0x100);
    writeLe32(&bytes, sectionTable + 20, 0x200);

    writeLe32(&bytes, 0x200, 0x1010);
    writeLe32(&bytes, 0x204, 0x1040);
    writeLe32(&bytes, 0x208, 0x3000);
    writeLe32(&bytes, 0x20c, 0x1040);
    writeLe32(&bytes, 0x210, 0x1080);
    writeLe32(&bytes, 0x214, 0x3010);

    TypeDecl *root = exportedNamed(&library, QStringLiteral("PE"));
    QVERIFY(root);
    auto rows = buildRows(&library, root, bytes);

    StructureRow *peRow = findTopLevelNamed(rows, QStringLiteral("PE"));
    QVERIFY(peRow);
    StructureRow *rawSection = findChildNamed(peRow, QStringLiteral("SECTION .pdata"));
    QVERIFY2(rawSection, qPrintable(childNames(peRow)));
    StructureRow *runtimeFunctions = findDescendantNamed(rawSection,
        QStringLiteral("IMAGE_RUNTIME_FUNCTION_ENTRY IMAGE_DIRECTORY_ENTRY_EXCEPTION[]"));
    QVERIFY2(runtimeFunctions, qPrintable(childNames(rawSection)));
    QCOMPARE(runtimeFunctions->children.size(), size_t(2));

    StructureRow *peImage = findSemanticRootChildNamed(rows, QStringLiteral("PE Image"));
    QVERIFY(peImage);
    StructureRow *semanticSection = findChildNamed(peImage, QStringLiteral("SECTION .pdata"));
    QVERIFY2(semanticSection, qPrintable(childNames(peImage)));
    StructureRow *exceptions = findChildNamed(semanticSection, QStringLiteral("Exceptions"));
    QVERIFY2(exceptions, qPrintable(childNames(semanticSection)));
    QCOMPARE(exceptions->children.size(), size_t(2));
    StructureRow *entry = exceptions->children[0].get();
    QCOMPARE(findChildNamed(entry, QStringLiteral("BeginRva"))->value, QStringLiteral("4112"));
    QCOMPARE(findChildNamed(entry, QStringLiteral("EndRva"))->value, QStringLiteral("4160"));
    QCOMPARE(findChildNamed(entry, QStringLiteral("UnwindInfoRva"))->value, QStringLiteral("12288"));
}

void StructViewPeTests::builderResolvesEntryPointRvaThroughSectionOffsetMap()
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
    StructureRow *peRow = findTopLevelNamed(rows, QStringLiteral("PE"));
    QVERIFY(peRow);

    StructureRow *entry = findDescendantNamed(peRow, QStringLiteral("dword AddressOfEntryPoint"));
    QVERIFY2(entry, "AddressOfEntryPoint row not found in rendered tree");
    QVERIFY(entry->hasCodeTarget);
    QCOMPARE(entry->codeLogicalOffset, uint64_t(0x1050));
    QCOMPARE(entry->codeTargetOffset, uint64_t(0x250));
}

void StructViewPeTests::builderPeSemanticModeDeclarativeSkipsCppViews()
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

void StructViewPeTests::builderKeepsRawDynamicRowsWhenSemanticImportDataIsTruncated()
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

void StructViewPeTests::semanticPeImportsWalksPe32PlusThunkTables()
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

void StructViewPeTests::semanticPeImportsRespectDynamicArrayDescriptorCount()
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

void StructViewPeTests::builderRunsSemanticViewsAfterDynamicPlacement()
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

REGISTER_STRUCTVIEW_TEST(StructViewPeTests)
#include "pe_tests.moc"
