#include "../structview_testsupport.h"

class StructViewElfTests : public QObject
{
    Q_OBJECT

private slots:
    void builderRendersElfImportsAndExportsSummary();
    void builderRendersElfDependenciesAndRelocationsSummary();
    void builderRendersElf32AndElf64Tables();
    void builderAddsElfSectionAndSymbolSemanticRows();
    void builderKeepsRawElfRowsWhenSemanticDataIsTruncated();
};

void StructViewElfTests::builderRendersElfImportsAndExportsSummary()
{
    // Scenario: a shared-object style ELF file has a dynamic symbol table with
    // both undefined symbols (imports) and defined global symbols (exports).
    // Expected: the semantic summary groups those symbols under Imports and
    // Exports at the ELF root, while the raw section rows remain intact. The
    // section's symbol table should be nested under a Symbols group.
    StrataLibrary library;
    QVERIFY2(parseStandardElfDefinition(&library), "elf.strata failed to parse");

    QByteArray bytes(0x300, '\0');
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
    writeLe16(&bytes, 50, 3);

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

    const QByteArray shstr("\0.dynsym\0.dynstr\0.shstrtab\0.text\0", 32);
    memcpy(bytes.data() + 0x260, shstr.constData(), size_t(shstr.size()));
    const QByteArray dynstr("\0printf\0main\0", 13);
    memcpy(bytes.data() + 0x220, dynstr.constData(), size_t(dynstr.size()));

    writeSection(1, 1, 11, 0x200, 32, 2, 16);
    writeSection(2, 9, 3, 0x220, 13, 0, 0);
    writeSection(3, 17, 3, 0x260, quint32(shstr.size()), 0, 0);
    writeSection(4, 27, 1, 0x240, 4, 0, 0);

    writeLe32(&bytes, 0x200 + 0, 1);
    writeLe32(&bytes, 0x200 + 4, 0);
    writeLe32(&bytes, 0x200 + 8, 0);
    bytes[0x200 + 12] = char(0x12);
    writeLe16(&bytes, 0x200 + 14, 0);

    writeLe32(&bytes, 0x210 + 0, 8);
    writeLe32(&bytes, 0x210 + 4, 0x1234);
    writeLe32(&bytes, 0x210 + 8, 4);
    bytes[0x210 + 12] = char(0x12);
    writeLe16(&bytes, 0x210 + 14, 4);

    TypeDecl *root = exportedNamed(&library, QStringLiteral("ELF"));
    QVERIFY(root);
    auto rows = buildRows(&library, root, bytes);
    QVERIFY(findTopLevelNamed(rows, QStringLiteral("ELF")));

    StructureRow *elfImage = findTopLevelNamed(rows, QStringLiteral("ELF Image"));
    QVERIFY2(elfImage, "ELF Image top-level row not found");

    StructureRow *dynsym = findChildNamed(elfImage, QStringLiteral("SECTION .dynsym"));
    QVERIFY2(dynsym, qPrintable(childNames(elfImage)));
    StructureRow *imports = findChildNamed(dynsym, QStringLiteral("Imports"));
    QVERIFY2(imports, qPrintable(childNames(dynsym)));
    StructureRow *importSymbol = findChildNamed(imports, QStringLiteral("printf"));
    QVERIFY2(importSymbol, qPrintable(childNames(imports)));
    QCOMPARE(importSymbol->offset, QStringLiteral("00000200"));

    StructureRow *exports = findChildNamed(dynsym, QStringLiteral("Exports"));
    QVERIFY2(exports, qPrintable(childNames(dynsym)));
    StructureRow *exportSymbol = findChildNamed(exports, QStringLiteral("main"));
    QVERIFY2(exportSymbol, qPrintable(childNames(exports)));
    QCOMPARE(exportSymbol->offset, QStringLiteral("00000210"));
    StructureRow *symbols = findChildNamed(dynsym, QStringLiteral("Symbols"));
    QVERIFY2(symbols, qPrintable(childNames(dynsym)));
    QVERIFY(findChildNamed(symbols, QStringLiteral("SYMBOL printf")));
    QVERIFY(findChildNamed(symbols, QStringLiteral("SYMBOL main")));
}

void StructViewElfTests::builderRendersElfDependenciesAndRelocationsSummary()
{
    // Scenario: an ELF shared object has a DT_NEEDED entry in .dynamic and a
    // relocation table in a dedicated relocation section.
    // Expected: the declarative summary shows a Dependencies branch under the
    // dynamic section and a Relocations branch under the relocation section,
    // while the raw section rows remain intact.
    StrataLibrary library;
    QVERIFY2(parseStandardElfDefinition(&library), "elf.strata failed to parse");

    QByteArray bytes(0x340, '\0');
    bytes[0] = char(0x7f);
    bytes[1] = 'E';
    bytes[2] = 'L';
    bytes[3] = 'F';
    bytes[4] = char(1);
    bytes[5] = char(1);
    bytes[6] = char(1);
    writeLe16(&bytes, 16, 3);
    writeLe16(&bytes, 18, 3);
    writeLe32(&bytes, 20, 1);
    writeLe32(&bytes, 32, 0x100);
    writeLe16(&bytes, 46, 40);
    writeLe16(&bytes, 48, 8);
    writeLe16(&bytes, 50, 4);

    auto writeSection = [&bytes](qsizetype index,
                                 quint32 name,
                                 quint32 type,
                                 quint32 offset,
                                 quint32 size,
                                 quint32 link,
                                 quint32 info,
                                 quint32 entrySize) {
        const qsizetype base = 0x100 + index * 40;
        writeLe32(&bytes, base + 0, name);
        writeLe32(&bytes, base + 4, type);
        writeLe32(&bytes, base + 16, offset);
        writeLe32(&bytes, base + 20, size);
        writeLe32(&bytes, base + 24, link);
        writeLe32(&bytes, base + 28, info);
        writeLe32(&bytes, base + 36, entrySize);
    };

    const QByteArray shstr("\0.text\0.symtab\0.strtab\0.shstrtab\0.dynstr\0.dynamic\0.rel.text\0", 60);
    memcpy(bytes.data() + 0x280, shstr.constData(), size_t(shstr.size()));
    const QByteArray strtab("\0main\0", 6);
    memcpy(bytes.data() + 0x260, strtab.constData(), size_t(strtab.size()));
    const QByteArray dynstr("\0libelf.so\0", 11);
    memcpy(bytes.data() + 0x2c0, dynstr.constData(), size_t(dynstr.size()));

    writeSection(1, 1, 1, 0x200, 4, 0, 0, 0);
    writeSection(2, 7, 2, 0x220, 32, 3, 0, 16);
    writeSection(3, 15, 3, 0x260, 6, 0, 0, 0);
    writeSection(4, 23, 3, 0x280, quint32(shstr.size()), 0, 0, 0);
    writeSection(5, 33, 3, 0x2c0, quint32(dynstr.size()), 0, 0, 0);
    writeSection(6, 41, 6, 0x2d0, 16, 5, 0, 8);
    writeSection(7, 50, 9, 0x2e0, 8, 2, 1, 8);

    writeLe32(&bytes, 0x2d0 + 0, 1);
    writeLe32(&bytes, 0x2d0 + 4, 1);
    writeLe32(&bytes, 0x2d8 + 0, 0);
    writeLe32(&bytes, 0x2d8 + 4, 0);

    writeLe32(&bytes, 0x2e0 + 0, 0x1234);
    writeLe32(&bytes, 0x2e0 + 4, (5u << 8) | 3u);

    TypeDecl *root = exportedNamed(&library, QStringLiteral("ELF"));
    QVERIFY(root);
    auto rows = buildRows(&library, root, bytes);
    QVERIFY(findTopLevelNamed(rows, QStringLiteral("ELF")));

    StructureRow *elfImage = findTopLevelNamed(rows, QStringLiteral("ELF Image"));
    QVERIFY2(elfImage, "ELF Image top-level row not found");

    StructureRow *dynamicSection = findChildNamed(elfImage, QStringLiteral("SECTION .dynamic"));
    QVERIFY(dynamicSection);
    QVERIFY2(dynamicSection->branchIconPath.contains(QStringLiteral("structure")), qPrintable(dynamicSection->branchIconPath));
    QVERIFY(dynamicSection->emphasizeName);
    StructureRow *dependencies = findChildNamed(dynamicSection, QStringLiteral("Dependencies"));
    QVERIFY2(dependencies, qPrintable(childNames(dynamicSection)));
    StructureRow *dependency = findChildNamed(dependencies, QStringLiteral("libelf.so"));
    QVERIFY2(dependency, qPrintable(childNames(dependencies)));
    QCOMPARE(findChildNamed(dependency, QStringLiteral("Name"))->value, QStringLiteral("libelf.so"));

    StructureRow *relSection = findChildNamed(elfImage, QStringLiteral("SECTION .rel.text"));
    QVERIFY(relSection);
    QVERIFY2(relSection->branchIconPath.contains(QStringLiteral("structure")), qPrintable(relSection->branchIconPath));
    QVERIFY(relSection->emphasizeName);
    StructureRow *relocations = findChildNamed(relSection, QStringLiteral("Relocations"));
    QVERIFY2(relocations, qPrintable(childNames(relSection)));
    StructureRow *relocation = findChildNamed(relocations, QStringLiteral("4660"));
    QVERIFY2(relocation, qPrintable(childNames(relocations)));
    QCOMPARE(findChildNamed(relocation, QStringLiteral("Offset"))->value, QStringLiteral("4660"));
    QVERIFY(findChildNamed(relocation, QStringLiteral("Type")));
    QVERIFY(findChildNamed(relocation, QStringLiteral("SymbolIndex")));
}

void StructViewElfTests::builderRendersElf32AndElf64Tables()
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

void StructViewElfTests::builderAddsElfSectionAndSymbolSemanticRows()
{
    // Scenario: an ELF file has a section-header string table plus a symbol
    // table linked to its own string table.
    // Expected: declarative Strata emits named section rows and a Symbols
    // branch under ELF Image, while the legacy C++ semantic view remains
    // available in cpp mode for comparison.
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
    StructureRow *elfRow = findTopLevelNamed(rows, QStringLiteral("ELF"));
    QVERIFY(elfRow);
    QVERIFY(findChildNamed(elfRow, QStringLiteral("Elf32_Shdr sectionHeaders32[]")));

    StructureRow *elfImage = findTopLevelNamed(rows, QStringLiteral("ELF Image"));
    QVERIFY2(elfImage, "ELF Image top-level row not found");
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
    StructureRow *symbols = findChildNamed(symtab, QStringLiteral("Symbols"));
    QVERIFY2(symbols, qPrintable(childNames(symtab)));
    StructureRow *symbol = findChildNamed(symbols, QStringLiteral("SYMBOL main"));
    QVERIFY2(symbol, qPrintable(childNames(symbols)));
    QCOMPARE(findChildNamed(symbol, QStringLiteral("Name"))->value, QStringLiteral("main"));
    QCOMPARE(findChildNamed(symbol, QStringLiteral("Value"))->value, QStringLiteral("4096"));
    QCOMPARE(findChildNamed(symbol, QStringLiteral("Size"))->value, QStringLiteral("4"));

    {
        ScopedEnvironmentVariable mode("Q22_ELF_SEMANTIC_VIEW", "cpp");
        auto cppRows = buildRows(&library, root, bytes);
        QCOMPARE(cppRows.size(), size_t(1));
        QVERIFY(!findTopLevelNamed(cppRows, QStringLiteral("ELF Image")));

        StructureRow *cppText = findChildNamed(cppRows[0].get(), QStringLiteral("SECTION .text"));
        QVERIFY(cppText);
        QCOMPARE(cppText->offset, QStringLiteral("00000200"));
        QCOMPARE(cppText->byteLength, uint64_t(4));

        StructureRow *cppSymtab = findChildNamed(cppRows[0].get(), QStringLiteral("SECTION .symtab"));
        QVERIFY(cppSymtab);
        StructureRow *cppSymbols = findChildNamed(cppSymtab, QStringLiteral("Symbols"));
        QVERIFY(cppSymbols);
        StructureRow *cppSymbol = findChildNamed(cppSymbols, QStringLiteral("SYMBOL main"));
        QVERIFY(cppSymbol);
        QCOMPARE(cppSymbol->value, QStringLiteral("value 0x1000, size 4"));
    }
}

void StructViewElfTests::builderKeepsRawElfRowsWhenSemanticDataIsTruncated()
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

REGISTER_STRUCTVIEW_TEST(StructViewElfTests)
#include "elf_tests.moc"
