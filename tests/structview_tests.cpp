#include "structview/structuredefinitionmanager.h"
#include "structview/structuretreemodel.h"

#include <QFile>
#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QtTest>

class StructViewTests : public QObject
{
    Q_OBJECT

private slots:
    void managerCreatesUserStructsDirectory();
    void managerDiscoversBuiltinAndUserDefinitionFiles();
    void reloadSwapsInParsedTypeLibrary();
    void failedReloadPreservesPreviousLibrary();
    void exportedTypesUseExplicitExportTagsOnly();
    void modelHeadersMatchStructureGridColumns();
    void modelSupportsHierarchyAndEditableCells();
    void modelBuildsExpandableRowsForStructFields();
};

static void writeTextFile(const QString &path, const QByteArray &text)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write(text), qint64(text.size()));
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
