#ifndef STRUCTVIEW_STRUCTURETREEMODEL_H
#define STRUCTVIEW_STRUCTURETREEMODEL_H

#include "structview/structuredisplayoptions.h"
#include "causeway/parser.h"

#include <QAbstractItemModel>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <vector>

enum class StructureRowKind
{
    Raw,
    Dynamic,
    Semantic
};

enum class StructureRowValueKind
{
    Custom,
    ScalarInteger,
    ScalarArrayPreview
};

struct StructureRow;
using StructureLazyChildLoader = std::function<std::vector<std::unique_ptr<StructureRow>>()>;

struct StructureRow
{
    explicit StructureRow(StructureRow *parentRow = nullptr);

    void setNameParts(const QString &prefix,
                      const QString &identifier,
                      const QString &suffix = QString(),
                      bool emphasize = false);
    void setBranchIcons(const QString &closedIconPath,
                        const QString &openIconPath,
                        const QString &emptyIconPath);

    QString name;
    QString nameTypePrefix;
    QString nameIdentifier;
    QString nameSuffix;
    bool emphasizeName = false;
    bool generatedName = false;
    QString branchIconPath;
    QString branchOpenIconPath;
    QString branchEmptyIconPath;
    QString value;
    StructureRowValueKind valueKind = StructureRowValueKind::Custom;
    uint64_t scalarRawValue = 0;
    uint64_t scalarByteLength = 0;
    bool scalarSigned = false;
    QString scalarCharacterSuffix;
    QStringList valueChoices;
    QString offset;
    bool generatedOffset = false;
    QString comment;
    Type *type = nullptr;
    TypeDecl *typeDecl = nullptr;
    FILEREF sourceRef;
    QString sourcePath;
    int sourceLine = 0;
    StructureRowKind kind = StructureRowKind::Raw;
    bool suppressSemanticViews = false;
    bool bigEndian = false;
    bool hasCodeTarget = false;
    uint64_t codeLogicalOffset = 0;
    uint64_t codeTargetOffset = 0;
    uint64_t absoluteOffset = 0;
    uint64_t relativeOffset = 0;
    uint64_t byteLength = 0;
    uint64_t arrayCountContribution = 1;
    StructureRow *parent = nullptr;
    std::vector<std::unique_ptr<StructureRow>> children;
    StructureLazyChildLoader lazyChildLoader;
    bool lazyChildrenLoaded = false;
};

class StructureTreeModel : public QAbstractItemModel
{
    Q_OBJECT
public:
    enum Column
    {
        NameColumn,
        ValueColumn,
        OffsetColumn,
        CommentColumn,
        ColumnCount
    };

    enum Role
    {
        NameTypePrefixRole = Qt::UserRole + 1,
        NameIdentifierRole,
        NameSuffixRole,
        EmphasizeNameRole,
        BranchIconPathRole,
        BranchOpenIconPathRole,
        BranchEmptyIconPathRole,
        RowKindRole,
        ValueChoicesRole,
        HasValueChoicesRole
    };

    explicit StructureTreeModel(QObject *parent = nullptr);

    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    bool hasChildren(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    bool canFetchMore(const QModelIndex &parent) const override;
    void fetchMore(const QModelIndex &parent) override;

    void clear();
    void setStrataLibrary(StrataLibrary *library);
    void setTypeDecls(const QList<TypeDecl *> &typeDecls);
    void setRows(std::vector<std::unique_ptr<StructureRow>> rows);
    void setRowsForTests(std::vector<std::unique_ptr<StructureRow>> rows);
    void applyDisplayOptions(const StructureDisplayOptions &options);
    StructureRow *rowForIndex(const QModelIndex &index) const;

private:
    StructureRow *parentRowForIndex(const QModelIndex &index) const;
    QString cellText(const StructureRow *row, int column) const;
    void setCellText(StructureRow *row, int column, const QString &text);
    std::unique_ptr<StructureRow> makeRowForTypeDecl(TypeDecl *decl) const;
    void addChildRowsForTypeDecl(StructureRow *parentRow, TypeDecl *decl, int depth) const;
    void applyDisplayOptionsToRow(StructureRow *row,
                                  const StructureDisplayOptions &options,
                                  const QModelIndex &index);
    Type *compoundTypeForDecl(TypeDecl *decl) const;
    Type *compoundTypeInChain(Type *type) const;
    QString typeName(Type *type) const;

    std::unique_ptr<StructureRow> m_root;
    StructureDisplayOptions m_displayOptions;
};

#endif // STRUCTVIEW_STRUCTURETREEMODEL_H
