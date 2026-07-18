#include "../structview_testsupport.h"

class StructViewTreeModelTests : public QObject
{
    Q_OBJECT

private slots:
    void modelHeadersMatchStructureGridColumns();
    void modelFetchesLazyChildrenOnceAndFormatsOffsets();
    void modelSupportsHierarchyAndEditableCells();
    void modelAppliesTypeDisplayOptionsWithoutResettingRows();
    void modelBuildsExpandableRowsForStructFields();
};

void StructViewTreeModelTests::modelHeadersMatchStructureGridColumns()
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

void StructViewTreeModelTests::modelFetchesLazyChildrenOnceAndFormatsOffsets()
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

void StructViewTreeModelTests::modelSupportsHierarchyAndEditableCells()
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

void StructViewTreeModelTests::modelAppliesTypeDisplayOptionsWithoutResettingRows()
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

void StructViewTreeModelTests::modelBuildsExpandableRowsForStructFields()
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

REGISTER_STRUCTVIEW_TEST(StructViewTreeModelTests)
#include "tree_model_tests.moc"
