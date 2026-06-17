#ifndef STRUCTVIEW_STRUCTURETREEMODEL_H
#define STRUCTVIEW_STRUCTURETREEMODEL_H

#include "TypeLib/parser.h"

#include <QAbstractItemModel>
#include <QList>
#include <QString>

#include <memory>
#include <vector>

enum class StructureRowKind
{
    Raw,
    Dynamic,
    Semantic
};

struct StructureRow
{
    explicit StructureRow(StructureRow *parentRow = nullptr);

    QString name;
    QString nameTypePrefix;
    QString nameIdentifier;
    QString nameSuffix;
    bool emphasizeName = false;
    QString branchIconPath;
    QString value;
    QString offset;
    QString comment;
    Type *type = nullptr;
    TypeDecl *typeDecl = nullptr;
    StructureRowKind kind = StructureRowKind::Raw;
    uint64_t absoluteOffset = 0;
    uint64_t relativeOffset = 0;
    uint64_t byteLength = 0;
    StructureRow *parent = nullptr;
    std::vector<std::unique_ptr<StructureRow>> children;
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
        RowKindRole
    };

    explicit StructureTreeModel(QObject *parent = nullptr);

    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;

    void clear();
    void setTypeLibrary(TypeLibrary *library);
    void setTypeDecls(const QList<TypeDecl *> &typeDecls);
    void setRows(std::vector<std::unique_ptr<StructureRow>> rows);
    void setRowsForTests(std::vector<std::unique_ptr<StructureRow>> rows);
    StructureRow *rowForIndex(const QModelIndex &index) const;

private:
    StructureRow *parentRowForIndex(const QModelIndex &index) const;
    QString cellText(const StructureRow *row, int column) const;
    void setCellText(StructureRow *row, int column, const QString &text);
    std::unique_ptr<StructureRow> makeRowForTypeDecl(TypeDecl *decl) const;
    void addChildRowsForTypeDecl(StructureRow *parentRow, TypeDecl *decl, int depth) const;
    Type *compoundTypeForDecl(TypeDecl *decl) const;
    Type *compoundTypeInChain(Type *type) const;
    QString typeName(Type *type) const;

    std::unique_ptr<StructureRow> m_root;
};

#endif // STRUCTVIEW_STRUCTURETREEMODEL_H
