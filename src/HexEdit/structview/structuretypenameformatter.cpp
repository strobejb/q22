#include "structview/structuretypenameformatter.h"

StructureTypeNameFormatter::StructureTypeNameFormatter(const StructureDisplayOptions &options)
    : m_options(options)
{
}

QString StructureTypeNameFormatter::typeName(Type *type) const
{
    return m_options.typeNameMode == StructureTypeNameMode::Storage
        ? storageTypeName(type)
        : definedTypeName(type);
}

QString StructureTypeNameFormatter::definedTypeName(Type *type) const
{
    if (!type)
        return {};

    switch (type->ty)
    {
    case typeIDENTIFIER:
        return type->sym ? QString::fromLocal8Bit(type->sym->name) : QString();
    case typeTYPEDEF:
        return type->sym ? QString::fromLocal8Bit(type->sym->name) : definedTypeName(type->link);
    case typeSTRUCT:
        return type->sptr && type->sptr->symbol && !type->sptr->symbol->anonymous
            ? QStringLiteral("struct %1").arg(QString::fromLocal8Bit(type->sptr->symbol->name))
            : QStringLiteral("struct");
    case typeUNION:
        return type->sptr && type->sptr->symbol && !type->sptr->symbol->anonymous
            ? QStringLiteral("union %1").arg(QString::fromLocal8Bit(type->sptr->symbol->name))
            : QStringLiteral("union");
    case typeENUM:
        return type->eptr && type->eptr->symbol && !type->eptr->symbol->anonymous
            ? QStringLiteral("enum %1").arg(QString::fromLocal8Bit(type->eptr->symbol->name))
            : QStringLiteral("enum");
    case typeARRAY:
        return definedTypeName(type->link) + QStringLiteral("[]");
    case typePOINTER:
        return definedTypeName(type->link) + QStringLiteral(" *");
    case typeSIGNED:
    case typeUNSIGNED:
        return definedTypeName(type->link);
    case typeCHAR: return QStringLiteral("char");
    case typeWCHAR: return QStringLiteral("wchar_t");
    case typeBYTE: return QStringLiteral("byte");
    case typeWORD: return QStringLiteral("word");
    case typeDWORD: return QStringLiteral("dword");
    case typeQWORD: return QStringLiteral("qword");
    case typeFLOAT: return QStringLiteral("float");
    case typeDOUBLE: return QStringLiteral("double");
    default:
        return definedTypeName(type->link);
    }
}

QString StructureTypeNameFormatter::storageTypeName(Type *type) const
{
    if (!type)
        return {};

    switch (type->ty)
    {
    case typeIDENTIFIER:
        return type->sym ? QString::fromLocal8Bit(type->sym->name) : QString();
    case typeTYPEDEF:
    {
        Type *base = BaseNode(type);
        if (base && (base->ty == typeSTRUCT || base->ty == typeUNION || base->ty == typeENUM))
            return definedTypeName(type);
        return storageTypeName(type->link);
    }
    case typeSTRUCT:
    case typeUNION:
    case typeENUM:
        return definedTypeName(type);
    case typeARRAY:
        return storageTypeName(type->link) + QStringLiteral("[]");
    case typePOINTER:
        return storageTypeName(type->link) + QStringLiteral(" *");
    case typeSIGNED:
    case typeUNSIGNED:
        return storageTypeName(type->link);
    case typeCHAR: return QStringLiteral("char");
    case typeWCHAR: return QStringLiteral("wchar_t");
    case typeBYTE: return QStringLiteral("byte");
    case typeWORD: return QStringLiteral("word");
    case typeDWORD: return QStringLiteral("dword");
    case typeQWORD: return QStringLiteral("qword");
    case typeFLOAT: return QStringLiteral("float");
    case typeDOUBLE: return QStringLiteral("double");
    default:
        return storageTypeName(type->link);
    }
}

StructureDeclarationParts StructureTypeNameFormatter::declarationParts(Type *type) const
{
    StructureDeclarationParts parts;
    if (!type)
        return parts;

    if (type->ty != typeIDENTIFIER)
    {
        parts.prefix = typeName(type);
        return parts;
    }

    parts.prefix = typeName(type->link);
    parts.name = type->sym ? QString::fromLocal8Bit(type->sym->name) : QString();
    parts.suffix = declaratorSuffix(type->link);
    for (int i = 0; i < parts.suffix.size() / 2 && parts.prefix.endsWith(QStringLiteral("[]")); ++i)
        parts.prefix.chop(2);
    return parts;
}

QString StructureTypeNameFormatter::declarationName(Type *type) const
{
    const StructureDeclarationParts parts = declarationParts(type);

    if (parts.prefix.isEmpty())
        return parts.name + parts.suffix;
    if (parts.name.isEmpty())
        return parts.prefix + parts.suffix;
    return parts.prefix + QLatin1Char(' ') + parts.name + parts.suffix;
}

bool StructureTypeNameFormatter::isCompoundDeclaration(Type *type) const
{
    for (Type *cursor = type; cursor; cursor = cursor->link)
    {
        Type *base = BaseNode(cursor);
        if (base && (base->ty == typeSTRUCT || base->ty == typeUNION))
            return true;
    }
    return false;
}

QString StructureTypeNameFormatter::declaratorSuffix(Type *type) const
{
    QString suffix;
    for (Type *cursor = type; cursor; cursor = cursor->link)
    {
        if (cursor->ty == typeARRAY)
            suffix += QStringLiteral("[]");
    }
    return suffix;
}
