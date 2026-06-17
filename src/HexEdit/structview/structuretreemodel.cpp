#include "structview/structuretreemodel.h"

#include <QLatin1String>

namespace
{
static constexpr int kMaxDefinitionDepth = 8;
}

StructureRow::StructureRow(StructureRow *parentRow)
    : parent(parentRow)
{
}

StructureTreeModel::StructureTreeModel(QObject *parent)
    : QAbstractItemModel(parent)
    , m_root(std::make_unique<StructureRow>())
{
}

QModelIndex StructureTreeModel::index(int row, int column, const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent))
        return {};

    StructureRow *parentRow = rowForIndex(parent);
    if (!parentRow || row < 0 || row >= parentRow->children.size())
        return {};

    return createIndex(row, column, parentRow->children[row].get());
}

QModelIndex StructureTreeModel::parent(const QModelIndex &child) const
{
    if (!child.isValid())
        return {};

    StructureRow *row = rowForIndex(child);
    if (!row || !row->parent || row->parent == m_root.get())
        return {};

    StructureRow *parentRow = row->parent;
    StructureRow *grandParent = parentRow->parent ? parentRow->parent : m_root.get();
    const int rowInGrandParent = [&]() {
        for (int i = 0; i < grandParent->children.size(); ++i)
            if (grandParent->children[i].get() == parentRow)
                return i;
        return -1;
    }();

    return rowInGrandParent >= 0 ? createIndex(rowInGrandParent, 0, parentRow) : QModelIndex();
}

int StructureTreeModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() && parent.column() != 0)
        return 0;

    StructureRow *parentRow = rowForIndex(parent);
    return parentRow ? parentRow->children.size() : 0;
}

int StructureTreeModel::columnCount(const QModelIndex &) const
{
    return ColumnCount;
}

bool StructureTreeModel::hasChildren(const QModelIndex &parent) const
{
    if (parent.isValid() && parent.column() != 0)
        return false;

    const StructureRow *row = rowForIndex(parent);
    if (!row)
        return false;

    return !row->children.empty() || !row->branchIconPath.isEmpty();
}

QVariant StructureTreeModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};

    const StructureRow *row = rowForIndex(index);
    if (!row)
        return {};

    if (role == Qt::DisplayRole || role == Qt::EditRole)
        return cellText(row, index.column());

    if (role == RowKindRole)
        return static_cast<int>(row->kind);

    if (index.column() == NameColumn)
    {
        switch (role)
        {
        case NameTypePrefixRole:
            return row->nameTypePrefix;
        case NameIdentifierRole:
            return row->nameIdentifier;
        case NameSuffixRole:
            return row->nameSuffix;
        case EmphasizeNameRole:
            return row->emphasizeName;
        case BranchIconPathRole:
            return row->branchIconPath;
        case BranchOpenIconPathRole:
            return row->branchOpenIconPath;
        case BranchEmptyIconPathRole:
            return row->branchEmptyIconPath;
        default:
            break;
        }
    }

    return {};
}

QVariant StructureTreeModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};

    switch (section)
    {
    case NameColumn:
        return tr("Name");
    case ValueColumn:
        return tr("Value");
    case OffsetColumn:
        return tr("Offset");
    case CommentColumn:
        return tr("Comment");
    default:
        return {};
    }
}

Qt::ItemFlags StructureTreeModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;

    const Qt::ItemFlags flags = QAbstractItemModel::flags(index);
    const StructureRow *row = rowForIndex(index);
    if (row && row->kind != StructureRowKind::Raw)
        return flags;

    if (row && !row->children.empty()
        && (index.column() == ValueColumn || index.column() == OffsetColumn)
        && row->value.trimmed().startsWith(QLatin1Char('{')))
        return flags;

    return flags | Qt::ItemIsEditable;
}

bool StructureTreeModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (role != Qt::EditRole || !index.isValid())
        return false;

    StructureRow *row = rowForIndex(index);
    if (!row)
        return false;

    setCellText(row, index.column(), value.toString());
    emit dataChanged(index, index, { Qt::DisplayRole, Qt::EditRole });
    return true;
}

void StructureTreeModel::clear()
{
    beginResetModel();
    m_root = std::make_unique<StructureRow>();
    endResetModel();
}

void StructureTreeModel::setTypeLibrary(TypeLibrary *library)
{
    QList<TypeDecl *> typeDecls;
    if (library)
    {
        for (TypeDecl *decl : library->globalTypeDeclList)
            if (decl)
                typeDecls.push_back(decl);
    }
    setTypeDecls(typeDecls);
}

void StructureTreeModel::setTypeDecls(const QList<TypeDecl *> &typeDecls)
{
    beginResetModel();
    m_root = std::make_unique<StructureRow>();

    for (TypeDecl *decl : typeDecls)
    {
        if (decl)
        {
            auto row = makeRowForTypeDecl(decl);
            addChildRowsForTypeDecl(row.get(), decl, 0);
            m_root->children.push_back(std::move(row));
        }
    }

    endResetModel();
}

void StructureTreeModel::setRows(std::vector<std::unique_ptr<StructureRow>> rows)
{
    beginResetModel();
    m_root = std::make_unique<StructureRow>();
    for (auto &row : rows)
    {
        row->parent = m_root.get();
        m_root->children.push_back(std::move(row));
    }
    endResetModel();
}

void StructureTreeModel::setRowsForTests(std::vector<std::unique_ptr<StructureRow>> rows)
{
    setRows(std::move(rows));
}

StructureRow *StructureTreeModel::rowForIndex(const QModelIndex &index) const
{
    if (!index.isValid())
        return m_root.get();

    return static_cast<StructureRow *>(index.internalPointer());
}

QString StructureTreeModel::cellText(const StructureRow *row, int column) const
{
    switch (column)
    {
    case NameColumn:
        return row->name;
    case ValueColumn:
        return row->value;
    case OffsetColumn:
        return row->offset;
    case CommentColumn:
        return row->comment;
    default:
        return {};
    }
}

void StructureTreeModel::setCellText(StructureRow *row, int column, const QString &text)
{
    switch (column)
    {
    case NameColumn:
        if (row->name == text)
            break;
        row->name = text;
        row->nameTypePrefix.clear();
        row->nameIdentifier.clear();
        row->nameSuffix.clear();
        row->emphasizeName = false;
        row->branchIconPath.clear();
        row->branchOpenIconPath.clear();
        row->branchEmptyIconPath.clear();
        break;
    case ValueColumn:
        row->value = text;
        break;
    case OffsetColumn:
        row->offset = text;
        break;
    case CommentColumn:
        row->comment = text;
        break;
    default:
        break;
    }
}

std::unique_ptr<StructureRow> StructureTreeModel::makeRowForTypeDecl(TypeDecl *decl) const
{
    auto row = std::make_unique<StructureRow>(m_root.get());
    row->value = QStringLiteral("{...}");
    row->offset = QStringLiteral("00000000");
    row->comment = decl->comment ? QString::fromLocal8Bit(decl->comment) : QString();

    QStringList names;
    for (Type *type : decl->declList)
    {
        const QString name = typeName(type);
        if (!name.isEmpty())
            names.push_back(name);
    }

    const QString base = typeName(decl->baseType);
    if (!names.isEmpty())
        row->name = names.join(QStringLiteral(", "));
    else if (!base.isEmpty())
        row->name = base;
    else
        row->name = tr("(anonymous type)");

    if (!base.isEmpty() && !row->name.startsWith(base))
        row->name = base + QLatin1Char(' ') + row->name;

    return row;
}

void StructureTreeModel::addChildRowsForTypeDecl(StructureRow *parentRow, TypeDecl *decl, int depth) const
{
    if (!parentRow || !decl || depth >= kMaxDefinitionDepth)
        return;

    Type *compoundType = compoundTypeForDecl(decl);
    if (!compoundType || !compoundType->sptr)
        return;

    for (TypeDecl *childDecl : compoundType->sptr->typeDeclList)
    {
        if (!childDecl)
            continue;

        auto child = makeRowForTypeDecl(childDecl);
        child->parent = parentRow;
        addChildRowsForTypeDecl(child.get(), childDecl, depth + 1);
        parentRow->children.push_back(std::move(child));
    }
}

Type *StructureTreeModel::compoundTypeForDecl(TypeDecl *decl) const
{
    if (!decl)
        return nullptr;

    if (Type *type = compoundTypeInChain(decl->baseType))
        return type;

    for (Type *type : decl->declList)
        if (Type *compound = compoundTypeInChain(type))
            return compound;

    return nullptr;
}

Type *StructureTreeModel::compoundTypeInChain(Type *type) const
{
    for (Type *cursor = type; cursor; cursor = cursor->link)
    {
        if (cursor->ty == typeSTRUCT || cursor->ty == typeUNION)
            return cursor;
    }
    return nullptr;
}

QString StructureTreeModel::typeName(Type *type) const
{
    if (!type)
        return {};

    switch (type->ty)
    {
    case typeIDENTIFIER:
    case typeTYPEDEF:
        return type->sym ? QString::fromLocal8Bit(type->sym->name) : QString();
    case typeSTRUCT:
        if (type->sptr && type->sptr->symbol && !type->sptr->symbol->anonymous)
            return tr("struct %1").arg(QString::fromLocal8Bit(type->sptr->symbol->name));
        return tr("struct");
    case typeUNION:
        if (type->sptr && type->sptr->symbol && !type->sptr->symbol->anonymous)
            return tr("union %1").arg(QString::fromLocal8Bit(type->sptr->symbol->name));
        return tr("union");
    case typeENUM:
        if (type->eptr && type->eptr->symbol && !type->eptr->symbol->anonymous)
            return tr("enum %1").arg(QString::fromLocal8Bit(type->eptr->symbol->name));
        return tr("enum");
    case typeARRAY:
        return typeName(type->link) + QStringLiteral("[]");
    case typePOINTER:
        return typeName(type->link) + QStringLiteral(" *");
    case typeSIGNED:
        return tr("signed %1").arg(typeName(type->link));
    case typeUNSIGNED:
        return tr("unsigned %1").arg(typeName(type->link));
    case typeCHAR:
        return QStringLiteral("char");
    case typeWCHAR:
        return QStringLiteral("wchar_t");
    case typeBYTE:
        return QStringLiteral("byte");
    case typeWORD:
        return QStringLiteral("word");
    case typeDWORD:
        return QStringLiteral("dword");
    case typeQWORD:
        return QStringLiteral("qword");
    case typeFLOAT:
        return QStringLiteral("float");
    case typeDOUBLE:
        return QStringLiteral("double");
    default:
        return {};
    }
}
