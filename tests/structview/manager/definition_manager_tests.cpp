#include "../structview_testsupport.h"

class StructViewDefinitionManagerTests : public QObject
{
    Q_OBJECT

private slots:
    void managerCreatesUserStrataDirectory();
    void managerDiscoversBuiltinAndUserDefinitionFiles();
    void shippedRuntimeDefinitionsLoadWithoutFailures();
    void reloadSwapsInParsedStrataLibrary();
    void managerReportsChangedDefinitionsWithoutAutoReload();
    void brokenDefinitionFileIsReportedWithoutDroppingValidOnes();
    void fixingABrokenDefinitionFileRestoresItsTypes();
    void partiallyParsedFileDoesNotExposeStaleExportedType();
    void exportedTypesUseExplicitExportTagsOnly();
    void exportedTypesIgnoreExportsFromIncludedFiles();
    void exportedTypesExposeAssocExtensions();
    void exportedTypesExposeMagicSignatures();
    void exportedTypesExposeDescriptions();
    void exportedTypesExposeCategories();
    void exportedTypesResolveDuplicateVersionsAndLogDecision();
    void userDefinitionOverridesBuiltinWithSameBaseName();
    void brokenUserDefinitionOverrideBlocksBuiltinFallback();
};

void StructViewDefinitionManagerTests::managerCreatesUserStrataDirectory()
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

void StructViewDefinitionManagerTests::managerDiscoversBuiltinAndUserDefinitionFiles()
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

void StructViewDefinitionManagerTests::shippedRuntimeDefinitionsLoadWithoutFailures()
{
    // Scenario: the app loads the generated runtime Strata directory.
    // Expected: every discovered shipped definition parses cleanly when there
    // are no user overrides.
    // Regression guard: removed/renamed .strata files must not linger in the
    // build output and be discovered only by the real Structure View manager.
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString userDir = temp.filePath(QStringLiteral("user-strata"));

    StructureDefinitionManager manager;
    manager.setBuiltinStructDirsForTests({ QStringLiteral(CAUSEWAY_TEST_DATA_DIR) });
    manager.setUserStrataDirForTests(userDir);

    QVERIFY2(manager.reload(), qPrintable(manager.lastError()));
    QVERIFY2(manager.failedFiles().isEmpty(), qPrintable(manager.loadLog()));
}

void StructViewDefinitionManagerTests::reloadSwapsInParsedStrataLibrary()
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

void StructViewDefinitionManagerTests::managerReportsChangedDefinitionsWithoutAutoReload()
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

void StructViewDefinitionManagerTests::brokenDefinitionFileIsReportedWithoutDroppingValidOnes()
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

void StructViewDefinitionManagerTests::fixingABrokenDefinitionFileRestoresItsTypes()
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

void StructViewDefinitionManagerTests::partiallyParsedFileDoesNotExposeStaleExportedType()
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

void StructViewDefinitionManagerTests::exportedTypesUseExplicitExportTagsOnly()
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

void StructViewDefinitionManagerTests::exportedTypesIgnoreExportsFromIncludedFiles()
{
    // Scenario: a reusable definition file is root-capable when opened directly,
    // but another root includes it only for its shared structures.
    // Expected: the Structure View root selector exposes the including file's
    // export only; the included file's [export] tag is suppressed for this parse.
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString userDir = temp.filePath(QStringLiteral("user-strata"));
    QVERIFY(QDir().mkpath(userDir));
    writeTextFile(QDir(userDir).filePath(QStringLiteral("z_child.strata")),
                  "[export(\"Child Root\")]\n"
                  "typedef struct _ChildRoot { byte child; } ChildRoot;\n");
    writeTextFile(QDir(userDir).filePath(QStringLiteral("a_parent.strata")),
                  "include \"z_child.strata\";\n"
                  "[export(\"Parent Root\")]\n"
                  "struct ParentRoot { ChildRoot child; } parent;\n");

    StructureDefinitionManager manager;
    manager.setBuiltinStructDirsForTests({});
    manager.setUserStrataDirForTests(userDir);

    QVERIFY2(manager.reload(), qPrintable(manager.lastError()));
    const QList<ExportedStructureType> exported = manager.exportedTypes();
    QCOMPARE(exported.size(), 1);
    QCOMPARE(exported[0].description, QStringLiteral("Parent Root"));
}

void StructViewDefinitionManagerTests::exportedTypesExposeAssocExtensions()
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

void StructViewDefinitionManagerTests::exportedTypesExposeMagicSignatures()
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

void StructViewDefinitionManagerTests::exportedTypesExposeDescriptions()
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

void StructViewDefinitionManagerTests::exportedTypesExposeCategories()
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

void StructViewDefinitionManagerTests::exportedTypesResolveDuplicateVersionsAndLogDecision()
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

void StructViewDefinitionManagerTests::userDefinitionOverridesBuiltinWithSameBaseName()
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
    QCOMPARE(QDir::fromNativeSeparators(exported[0].filePath), QDir::fromNativeSeparators(userPath));
}

void StructViewDefinitionManagerTests::brokenUserDefinitionOverrideBlocksBuiltinFallback()
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

REGISTER_STRUCTVIEW_TEST(StructViewDefinitionManagerTests)
#include "definition_manager_tests.moc"
