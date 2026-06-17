#include "structview/structurerenderengine.h"

#include "structview/structuresemanticview.h"

#include <QByteArray>
#include <QLatin1String>
#include <QStringList>

#include <algorithm>
#include <cstring>

namespace
{
static constexpr uint64_t kMaxArrayElements = 100;
static constexpr qsizetype kMaxArrayPreviewElements = 8;
static constexpr bool kPrefixArrayAliasesWithDash = false;
static const char kDynamicBranchClosedIconPath[] = ":/icons/rendered/blue/double-closed.svg";
static const char kDynamicBranchOpenIconPath[] = ":/icons/rendered/blue/double-open.svg";
static const char kDynamicBranchEmptyIconPath[] = ":/icons/rendered/gray/double-closed.svg";

uint64_t scalarSize(TYPE type)
{
    switch (type)
    {
    case typeCHAR:
    case typeBYTE:
        return 1;
    case typeWCHAR:
    case typeWORD:
        return 2;
    case typeDWORD:
    case typeFLOAT:
    case typeENUM:
        return 4;
    case typeQWORD:
    case typeDOUBLE:
    case typeTIMET:
    case typeFILETIME:
        return 8;
    case typeDOSTIME:
    case typeDOSDATE:
        return 2;
    default:
        return 0;
    }
}

uint64_t unsignedValue(const uint8_t *data, uint64_t length, bool bigEndian)
{
    uint64_t value = 0;
    if (bigEndian)
    {
        for (uint64_t i = 0; i < length; ++i)
            value = (value << 8) | data[i];
    }
    else
    {
        for (uint64_t i = 0; i < length; ++i)
            value |= uint64_t(data[i]) << (i * 8);
    }
    return value;
}

int64_t signedValue(uint64_t value, uint64_t length)
{
    if (length == 0 || length >= 8)
        return static_cast<int64_t>(value);

    const uint64_t signBit = uint64_t(1) << (length * 8 - 1);
    const uint64_t mask = (uint64_t(1) << (length * 8)) - 1;
    if ((value & signBit) == 0)
        return static_cast<int64_t>(value);

    return -static_cast<int64_t>((~value + 1) & mask);
}

bool isScalar(TYPE type)
{
    return scalarSize(type) != 0;
}

QString rowNameFragment(QString value)
{
    if (value.size() >= 2 && value.front() == QLatin1Char('"') && value.back() == QLatin1Char('"'))
        value = value.mid(1, value.size() - 2);
    return value;
}

QString arrayAliasPrefix()
{
    return kPrefixArrayAliasesWithDash ? QStringLiteral(" - ") : QString();
}

void appendCommaArgs(ExprNode *expr, std::vector<ExprNode *> *args)
{
    if (!expr || !args)
        return;

    if (expr->type == EXPR_COMMA)
    {
        appendCommaArgs(expr->left, args);
        appendCommaArgs(expr->right, args);
        return;
    }

    args->push_back(expr);
}

bool arrayIndexFromRow(const StructureRow *row, INUMTYPE *index)
{
    if (!row || !index || !row->nameTypePrefix.startsWith(QLatin1Char('[')))
        return false;

    const qsizetype close = row->nameTypePrefix.indexOf(QLatin1Char(']'));
    if (close <= 1)
        return false;

    bool ok = false;
    const QString text = row->nameTypePrefix.mid(1, close - 1);
    const qulonglong value = text.toULongLong(&ok);
    if (!ok)
        return false;

    *index = static_cast<INUMTYPE>(value);
    return true;
}
}

struct StructureRenderEngine::ResolvedField
{
    Type *type = nullptr;
    TypeDecl *typeDecl = nullptr;
    uint64_t offset = 0;
    uint64_t length = 0;
};

struct StructureRenderEngine::EvalContext
{
    StructureRow *row = nullptr;
    Type *type = nullptr;
    uint64_t offset = 0;
};

StructureRenderEngine::StructureRenderEngine(TypeLibrary *library,
                                             TypeDecl *rootType,
                                             uint64_t baseOffset,
                                             const StructureValueBuilder::ByteReader &reader)
    : m_library(library)
    , m_rootType(rootType)
    , m_baseOffset(baseOffset)
    , m_reader(reader)
{
}

std::vector<std::unique_ptr<StructureRow>> StructureRenderEngine::build()
{
    std::vector<RowPtr> rows;
    if (!m_rootType)
        return rows;

    auto root = makeRow(nullptr, m_rootType->declList.empty() ? m_rootType->baseType : m_rootType->declList[0],
                        m_rootType, m_baseOffset);
    root->parent = nullptr;
    root->value = QStringLiteral("{...}");
    if (m_rootType->declList.size() == 1)
    {
        Type *rootType = m_rootType->declList[0];
        root->type = rootType;
        applyDeclarationName(root.get(), rootType);
        root->byteLength = recurseType(root.get(), rootType->link, m_rootType, m_baseOffset);
    }
    else
    {
        root->name = typeName(m_rootType->baseType);
        appendTypeDecl(root.get(), m_rootType, m_baseOffset);
        root->byteLength = sizeOf(root->type, m_baseOffset);
    }
    collectDynamicRows();
    collectDynamicRows(root.get());
    appendDynamicRows(root.get());

    std::vector<StructureOffsetMap> semanticOffsetMaps;
    for (const DynamicContainer &container : m_dynamicContainers)
    {
        for (const OffsetMap &map : container.maps)
            semanticOffsetMaps.push_back(StructureOffsetMap{ map.logicalStart, map.logicalSize, map.fileOffset });
    }
    runStructureSemanticViews(m_library, root.get(), m_baseOffset, m_reader, semanticOffsetMaps);

    rows.push_back(std::move(root));
    return rows;
}

StructureRenderEngine::RowPtr StructureRenderEngine::makeRow(StructureRow *parent,
                                                             Type *type,
                                                             TypeDecl *typeDecl,
                                                             uint64_t offset) const
{
    auto row = std::make_unique<StructureRow>(parent);
    row->type = type;
    row->typeDecl = typeDecl;
    row->absoluteOffset = offset;
    row->relativeOffset = offset >= m_baseOffset ? offset - m_baseOffset : 0;
    row->offset = formatOffset(offset);
    row->comment = typeDecl && typeDecl->comment ? QString::fromLocal8Bit(typeDecl->comment) : QString();
    return row;
}

uint64_t StructureRenderEngine::appendTypeDecl(StructureRow *parent, TypeDecl *typeDecl, uint64_t offset)
{
    if (!typeDecl || typeDecl->typeAlias)
        return 0;

    const uint64_t originalOffset = offset;
    ExprNode *offsetExpr = nullptr;
    if (FindTag(typeDecl->tagList, TOK_OFFSET, &offsetExpr))
    {
        INUMTYPE evaluated = offset;
        if (evaluate(parent, offsetExpr, &evaluated, offset))
            offset = m_baseOffset + evaluated;
    }

    uint64_t length = 0;
    if (typeDecl->declList.empty() && typeDecl->nested)
    {
        length = recurseType(parent, typeDecl->baseType, typeDecl, offset);
        return offset >= originalOffset ? (offset - originalOffset) + length : length;
    }

    for (Type *type : typeDecl->declList)
        length += recurseType(parent, type, typeDecl, offset + length);

    return offset >= originalOffset ? (offset - originalOffset) + length : length;
}

uint64_t StructureRenderEngine::appendIdentifierRow(StructureRow *parent,
                                                    Type *type,
                                                    TypeDecl *typeDecl,
                                                    uint64_t offset)
{
    auto row = makeRow(parent, type, typeDecl, offset);
    applyDeclarationName(row.get(), type);
    if (row->name.isEmpty() && type && type->sym)
        row->name = QString::fromLocal8Bit(type->sym->name);

    const uint64_t length = recurseType(row.get(), type ? type->link : nullptr, typeDecl, offset);
    row->byteLength = length;
    const QString stringValue = stringArrayValue(row.get(), type ? type->link : nullptr, typeDecl, offset);
    if (!stringValue.isNull())
        row->value = stringValue;
    else
    {
        const QString arrayValue = scalarArrayValue(row.get(), type ? type->link : nullptr);
        if (!arrayValue.isNull())
            row->value = arrayValue;
    }
    if (row->value.isEmpty() && !row->children.empty())
        row->value = QStringLiteral("{...}");
    if (!row->value.isEmpty() || !row->children.empty())
    {
        if (!parent)
            return length;
        parent->children.push_back(std::move(row));
    }
    return length;
}

uint64_t StructureRenderEngine::recurseType(StructureRow *parent,
                                            Type *type,
                                            TypeDecl *typeDecl,
                                            uint64_t offset)
{
    if (!type)
        return 0;

    switch (type->ty)
    {
    case typeIDENTIFIER:
        return appendIdentifierRow(parent, type, typeDecl, offset);

    case typeTYPEDEF:
    case typeSIGNED:
    case typeUNSIGNED:
    case typeCONST:
    case typePOINTER:
        return recurseType(parent, type->link, typeDecl, offset);

    case typeARRAY:
    {
        INUMTYPE count = 0;
        if (!evaluateArrayCount(parent, typeDecl, type, &count, offset))
            return 0;

        const uint64_t boundedCount = std::min<uint64_t>(count, kMaxArrayElements);
        uint64_t length = 0;
        Enum *nameEnum = nullptr;
        ExprNode *nameExpr = nullptr;
        if (FindTag(typeDecl ? typeDecl->tagList : nullptr, TOK_NAME, &nameExpr) && m_library && nameExpr && nameExpr->str)
        {
            if (Symbol *sym = LookupSymbol(m_library->globalTagSymbolList, nameExpr->str))
                if (sym->type && sym->type->ty == typeENUM)
                    nameEnum = sym->type->eptr;
        }

        for (uint64_t i = 0; i < boundedCount; ++i)
        {
            auto row = makeRow(parent, type->link, typeDecl, offset + length);
            const QString indexLabel = QStringLiteral("[%1]").arg(i);
            row->name = indexLabel;
            row->nameTypePrefix = indexLabel;
            const QString enumLabel = enumNameForValue(nameEnum, i);
            if (!enumLabel.isEmpty())
            {
                row->nameIdentifier = arrayAliasPrefix() + enumLabel;
                row->name += row->nameIdentifier;
            }

            const uint64_t elementLength = formatType(row.get(), type->link, typeDecl, offset + length);
            row->byteLength = elementLength;
            if (!nameEnum && nameExpr)
            {
                const QString fieldName = rowNameFragment(fieldNameValue(row.get(), type->link, nameExpr, offset + length));
                if (!fieldName.isEmpty())
                {
                    row->nameIdentifier = arrayAliasPrefix() + fieldName;
                    row->name += row->nameIdentifier;
                }
            }
            length += elementLength;
            parent->children.push_back(std::move(row));
        }
        return length;
    }

    case typeSTRUCT:
    {
        if (!type->sptr)
            return 0;

        uint64_t length = 0;
        for (TypeDecl *childDecl : type->sptr->typeDeclList)
            length += appendTypeDecl(parent, childDecl, offset + length);
        return length;
    }

    case typeUNION:
    {
        if (!type->sptr)
            return 0;

        INUMTYPE switchValue = 0;
        ExprNode *switchExpr = nullptr;
        const bool hasSwitch = FindTag(typeDecl ? typeDecl->tagList : nullptr, TOK_SWITCHIS, &switchExpr)
                               && evaluate(EvalContext{ parent, type, offset }, switchExpr, &switchValue);

        uint64_t length = 0;
        for (TypeDecl *childDecl : type->sptr->typeDeclList)
        {
            ExprNode *caseExpr = nullptr;
            if (hasSwitch && FindTag(childDecl ? childDecl->tagList : nullptr, TOK_CASE, &caseExpr))
            {
                INUMTYPE caseValue = 0;
                if (!evaluate(parent, caseExpr, &caseValue, offset) || caseValue != switchValue)
                    continue;
            }
            length = std::max(length, appendTypeDecl(parent, childDecl, offset));
        }
        return length;
    }

    default:
        return formatType(parent, type, typeDecl, offset);
    }
}

uint64_t StructureRenderEngine::formatType(StructureRow *row, Type *type, TypeDecl *typeDecl, uint64_t offset)
{
    if (!row || !type)
        return 0;

    Type *base = BaseNode(type);
    if (!base)
        return 0;

    if (base->ty == typeSTRUCT || base->ty == typeUNION)
    {
        row->value = QStringLiteral("{...}");
        return recurseType(row, base, typeDecl, offset);
    }

    if (type->ty == typeARRAY)
        return recurseType(row, type, typeDecl, offset);

    return formatScalar(row, type, typeDecl, offset);
}

uint64_t StructureRenderEngine::formatScalar(StructureRow *row, Type *type, TypeDecl *typeDecl, uint64_t offset)
{
    Type *base = BaseNode(type);
    if (!row || !base)
        return 0;

    const uint64_t length = scalarSize(base->ty);
    if (length == 0)
        return 0;

    uint8_t data[8] = {};
    const size_t got = m_reader ? m_reader(offset, data, static_cast<size_t>(length)) : 0;
    if (got < length)
    {
        row->value.clear();
        return length;
    }

    const bool bigEndian = isBigEndian(typeDecl);
    const uint64_t raw = unsignedValue(data, length, bigEndian);
    Enum *displayEnum = tagEnum(typeDecl);
    if (!displayEnum && base->ty == typeENUM)
        displayEnum = base->eptr;

    if (displayEnum)
    {
        const QString enumName = enumNameForValue(displayEnum, raw);
        row->value = enumName.isEmpty() ? QString::number(raw, 16).toUpper().rightJustified(int(length * 2), QLatin1Char('0'))
                                        : enumName;
        return length;
    }

    switch (base->ty)
    {
    case typeCHAR:
    {
        const char ch = data[0] >= ' ' ? char(data[0]) : '.';
        row->value = QStringLiteral("%1 '%2'").arg(raw).arg(QLatin1Char(ch));
        break;
    }
    case typeWCHAR:
        row->value = QString::number(raw);
        break;
    case typeBYTE:
    case typeWORD:
    case typeDWORD:
    case typeQWORD:
    {
        if (FindType(type, typeSIGNED))
            row->value = QString::number(signedValue(raw, length));
        else
            row->value = QString::number(raw);
        break;
    }
    case typeFLOAT:
    {
        uint32_t bits = static_cast<uint32_t>(raw);
        float value = 0.0f;
        std::memcpy(&value, &bits, sizeof(value));
        row->value = QString::number(value, 'g', 8);
        break;
    }
    case typeDOUBLE:
    {
        double value = 0.0;
        std::memcpy(&value, &raw, sizeof(value));
        row->value = QString::number(value, 'g', 12);
        break;
    }
    default:
        row->value = QString::number(raw);
        break;
    }

    return length;
}

uint64_t StructureRenderEngine::sizeOf(Type *type, uint64_t offset)
{
    if (!type)
        return 0;

    switch (type->ty)
    {
    case typeARRAY:
    {
        INUMTYPE count = 0;
        if (!evaluate(type, type->elements, &count, offset))
            return 0;
        return sizeOf(type->link, offset) * std::min<uint64_t>(count, kMaxArrayElements);
    }
    case typeSTRUCT:
    {
        uint64_t size = 0;
        if (type->sptr)
            for (TypeDecl *decl : type->sptr->typeDeclList)
                for (Type *child : decl->declList)
                    size += sizeOf(child, offset + size);
        return size;
    }
    case typeUNION:
    {
        uint64_t size = 0;
        if (type->sptr)
            for (TypeDecl *decl : type->sptr->typeDeclList)
                for (Type *child : decl->declList)
                    size = std::max(size, sizeOf(child, offset));
        return size;
    }
    default:
        if (const uint64_t scalar = scalarSize(type->ty))
            return scalar;
        return sizeOf(type->link, offset);
    }
}

bool StructureRenderEngine::evaluate(StructureRow *scope, ExprNode *expr, INUMTYPE *result, uint64_t scopeOffset)
{
    Type *scopeType = scope ? scope->type : nullptr;
    return evaluate(EvalContext{ scope, scopeType, scopeOffset }, expr, result);
}

bool StructureRenderEngine::evaluate(Type *scopeType, ExprNode *expr, INUMTYPE *result, uint64_t scopeOffset)
{
    return evaluate(EvalContext{ nullptr, scopeType, scopeOffset }, expr, result);
}

bool StructureRenderEngine::evaluateArrayCount(StructureRow *scope,
                                               TypeDecl *typeDecl,
                                               Type *arrayType,
                                               INUMTYPE *result,
                                               uint64_t offset)
{
    if (!arrayType || arrayType->ty != typeARRAY || !result)
        return false;

    if (arrayType->elements)
        return evaluate(scope, arrayType->elements, result, offset);

    ExprNode *sizeExpr = nullptr;
    if (!FindTag(typeDecl ? typeDecl->tagList : nullptr, TOK_SIZEIS, &sizeExpr) || !sizeExpr)
        return false;

    std::vector<ExprNode *> args;
    appendCommaArgs(sizeExpr, &args);
    return !args.empty() && evaluate(scope, args[0], result, offset);
}

bool StructureRenderEngine::evaluate(const EvalContext &context, ExprNode *expr, INUMTYPE *result)
{
    if (!expr || !result)
        return false;

    INUMTYPE left = 0;
    INUMTYPE right = 0;
    INUMTYPE cond = 0;

    switch (expr->type)
    {
    case EXPR_IDENTIFIER:
    case EXPR_FIELD:
    case EXPR_ARRAY:
    {
        if (StructureRow *row = findFieldRow(context.row, expr))
            return readInteger(row->absoluteOffset, row->byteLength, result);

        ResolvedField field;
        if (resolveField(context.type, expr, context.offset, &field))
            return readInteger(field.offset, field.length, result);

        if (expr->type == EXPR_IDENTIFIER && m_library)
        {
            if (Symbol *sym = LookupSymbol(m_library->globalIdentifierList, expr->str))
            {
                if (sym->type && sym->type->ty == typeENUMVALUE)
                {
                    *result = sym->type->evptr->val;
                    return true;
                }
            }

            for (Symbol *sym : m_library->globalTagSymbolList)
            {
                if (!sym || !sym->type || sym->type->ty != typeENUM || !sym->type->eptr)
                    continue;

                for (EnumField *field : sym->type->eptr->fieldList)
                {
                    if (field && field->name && std::strcmp(field->name->name, expr->str) == 0)
                    {
                        *result = field->val;
                        return true;
                    }
                }
            }
        }
        return false;
    }
    case EXPR_NUMBER:
        *result = expr->tok == TOK_INUMBER ? expr->val : static_cast<INUMTYPE>(expr->fval);
        return true;
    case EXPR_UNARY:
        if (!evaluate(context, expr->left, &left))
            return false;
        switch (static_cast<int>(expr->tok))
        {
        case '+': *result = left; break;
        case '-': *result = static_cast<INUMTYPE>(-static_cast<int64_t>(left)); break;
        case '!': *result = !left; break;
        case '~': *result = ~left; break;
        default: return false;
        }
        return true;
    case EXPR_BINARY:
        if (!evaluate(context, expr->left, &left) || !evaluate(context, expr->right, &right))
            return false;
        switch (static_cast<int>(expr->tok))
        {
        case '+': *result = left + right; break;
        case '-': *result = left - right; break;
        case '*': *result = left * right; break;
        case '%': if (right == 0) return false; *result = left % right; break;
        case '/': if (right == 0) return false; *result = left / right; break;
        case '|': *result = left | right; break;
        case '&': *result = left & right; break;
        case '^': *result = left ^ right; break;
        case TOK_ANDAND: *result = left && right; break;
        case TOK_OROR: *result = left || right; break;
        case TOK_SHL: *result = left << right; break;
        case TOK_SHR: *result = left >> right; break;
        case TOK_EQU: *result = left == right; break;
        case TOK_NEQ: *result = left != right; break;
        case TOK_GE: *result = left >= right; break;
        case TOK_LE: *result = left <= right; break;
        case '>': *result = left > right; break;
        case '<': *result = left < right; break;
        default: return false;
        }
        return true;
    case EXPR_TERTIARY:
        if (!evaluate(context, expr->cond, &cond))
            return false;
        return evaluate(context, cond ? expr->left : expr->right, result);
    default:
        return false;
    }
}

StructureRow *StructureRenderEngine::findFieldRow(StructureRow *scope, ExprNode *expr)
{
    if (!scope || !expr)
        return nullptr;

    if (expr->type == EXPR_IDENTIFIER)
        return findDirectField(scope, expr->str);

    if (expr->type == EXPR_ARRAY)
    {
        StructureRow *arrayRow = findFieldRow(scope, expr->left);
        INUMTYPE index = 0;
        if (!arrayRow || !evaluate(scope, expr->right, &index, scope->absoluteOffset))
            return nullptr;

        if (index >= arrayRow->children.size())
            return nullptr;

        return arrayRow->children[static_cast<size_t>(index)].get();
    }

    if (expr->type != EXPR_FIELD)
        return nullptr;

    StructureRow *row = findFieldRow(scope, expr->left);
    return row ? findFieldRow(row, expr->right) : nullptr;
}

StructureRow *StructureRenderEngine::findDirectField(StructureRow *scope, const char *name) const
{
    if (!scope || !name)
        return nullptr;

    for (StructureRow *cursor = scope; cursor; cursor = cursor->parent)
    {
        for (const auto &child : cursor->children)
        {
            Type *type = child->type;
            if (type && type->ty == typeIDENTIFIER && type->sym && std::strcmp(type->sym->name, name) == 0)
                return child.get();
        }
    }
    return nullptr;
}

bool StructureRenderEngine::resolveField(Type *scopeType, ExprNode *expr, uint64_t scopeOffset, ResolvedField *field)
{
    if (!scopeType || !expr || !field)
        return false;

    if (expr->type == EXPR_IDENTIFIER)
        return resolveDirectField(scopeType, expr->str, scopeOffset, field);

    if (expr->type == EXPR_ARRAY)
    {
        ResolvedField arrayField;
        INUMTYPE index = 0;
        if (!resolveField(scopeType, expr->left, scopeOffset, &arrayField)
            || !evaluate(EvalContext{ nullptr, scopeType, scopeOffset }, expr->right, &index))
        {
            return false;
        }

        Type *arrayType = nullptr;
        for (Type *cursor = arrayField.type; cursor; cursor = cursor->link)
        {
            if (cursor->ty == typeARRAY)
            {
                arrayType = cursor;
                break;
            }
        }

        if (!arrayType)
            return false;

        const uint64_t elementLength = sizeOf(arrayType->link, arrayField.offset);
        field->type = arrayType->link;
        field->typeDecl = arrayField.typeDecl;
        field->offset = arrayField.offset + uint64_t(index) * elementLength;
        field->length = elementLength;
        return true;
    }

    if (expr->type != EXPR_FIELD)
        return false;

    ResolvedField left;
    if (!resolveField(scopeType, expr->left, scopeOffset, &left))
        return false;

    return resolveField(left.type, expr->right, left.offset, field);
}

bool StructureRenderEngine::resolveDirectField(Type *scopeType, const char *name, uint64_t scopeOffset, ResolvedField *field)
{
    if (!scopeType || !name || !field)
        return false;

    Type *base = BaseNode(scopeType);
    if (!base || (base->ty != typeSTRUCT && base->ty != typeUNION) || !base->sptr)
        return false;

    uint64_t cursor = scopeOffset;
    uint64_t unionSize = 0;
    for (TypeDecl *decl : base->sptr->typeDeclList)
    {
        if (!decl || decl->typeAlias)
            continue;

        uint64_t declOffset = base->ty == typeUNION ? scopeOffset : cursor;
        ExprNode *offsetExpr = nullptr;
        if (FindTag(decl->tagList, TOK_OFFSET, &offsetExpr))
        {
            INUMTYPE evaluated = declOffset;
            if (evaluate(base, offsetExpr, &evaluated, scopeOffset))
                declOffset = m_baseOffset + evaluated;
        }

        uint64_t declLength = 0;
        for (Type *type : decl->declList)
        {
            const uint64_t fieldLength = sizeOf(type, declOffset);
            if (type && type->ty == typeIDENTIFIER && type->sym && std::strcmp(type->sym->name, name) == 0)
            {
                field->type = type;
                field->typeDecl = decl;
                field->offset = declOffset;
                field->length = fieldLength;
                return true;
            }

            if (base->ty == typeSTRUCT)
                declOffset += fieldLength;
            declLength = base->ty == typeUNION ? std::max(declLength, fieldLength) : declLength + fieldLength;
        }

        if (decl->declList.empty() && decl->nested && decl->baseType)
        {
            ResolvedField nested;
            if (resolveDirectField(decl->baseType, name, declOffset, &nested))
            {
                *field = nested;
                return true;
            }
            declLength = sizeOf(decl->baseType, declOffset);
        }

        if (base->ty == typeSTRUCT)
            cursor = declOffset;
        else
            unionSize = std::max(unionSize, declLength);
    }

    Q_UNUSED(unionSize);
    return false;
}

bool StructureRenderEngine::readInteger(uint64_t offset, uint64_t length, INUMTYPE *result) const
{
    uint8_t data[sizeof(INUMTYPE)] = {};
    const size_t requested = static_cast<size_t>(std::max<uint64_t>(1, std::min<uint64_t>(length, sizeof(data))));
    const size_t got = m_reader ? m_reader(offset, data, requested) : 0;
    if (got == 0)
        return false;

    *result = unsignedValue(data, got, false);
    return true;
}

void StructureRenderEngine::collectDynamicRows()
{
    m_dynamicContainers.clear();
    m_dynamicRequests.clear();
}

void StructureRenderEngine::collectDynamicRows(StructureRow *row)
{
    if (!row)
        return;

    collectDynamicContainer(row);
    collectDynamicRequests(row);

    for (const auto &child : row->children)
        collectDynamicRows(child.get());
}

void StructureRenderEngine::collectDynamicContainer(StructureRow *row)
{
    if (!row || !row->typeDecl || !FindTag(row->typeDecl->tagList, TOK_DYNAMICCONTAINER, nullptr))
        return;

    INUMTYPE arrayIndex = 0;
    if (!arrayIndexFromRow(row, &arrayIndex))
        return;

    ExprNode *containerExpr = nullptr;
    FindTag(row->typeDecl->tagList, TOK_DYNAMICCONTAINER, &containerExpr);
    ExprNode *typeNameExpr = nullptr;
    if (!dynamicContainerArgs(containerExpr, &typeNameExpr)
        || !typeNameExpr || typeNameExpr->type != EXPR_IDENTIFIER || !typeNameExpr->str)
    {
        return;
    }

    TypeDecl *containerType = findTypeDecl(typeNameExpr->str);
    if (!containerType)
        return;

    DynamicContainer container;
    container.typeDecl = containerType;
    container.alias = row->nameIdentifier;

    for (Tag *tag = row->typeDecl->tagList; tag; tag = tag->link)
    {
        if (tag->tok != TOK_OFFSETMAP)
            continue;

        ExprNode *logicalStartExpr = nullptr;
        ExprNode *logicalSizeExpr = nullptr;
        ExprNode *fileOffsetExpr = nullptr;
        if (!offsetMapArgs(tag->expr, &logicalStartExpr, &logicalSizeExpr, &fileOffsetExpr))
            continue;

        INUMTYPE logicalStart = 0;
        INUMTYPE logicalSize = 0;
        INUMTYPE fileOffset = 0;
        if (!evaluate(row, logicalStartExpr, &logicalStart, row->absoluteOffset)
            || !evaluate(row, logicalSizeExpr, &logicalSize, row->absoluteOffset)
            || !evaluate(row, fileOffsetExpr, &fileOffset, row->absoluteOffset))
        {
            continue;
        }

        if (logicalSize == 0)
            continue;

        container.fileOffset = uint64_t(fileOffset);
        container.byteLength = uint64_t(logicalSize);
        container.maps.push_back(OffsetMap{ uint64_t(logicalStart), uint64_t(logicalSize), uint64_t(fileOffset) });
    }

    if (!container.maps.empty())
        m_dynamicContainers.push_back(container);
}

void StructureRenderEngine::collectDynamicRequests(StructureRow *row)
{
    if (!row || !row->typeDecl)
        return;

    INUMTYPE arrayIndex = 0;
    if (!arrayIndexFromRow(row, &arrayIndex))
        return;

    for (Tag *tag = row->typeDecl->tagList; tag; tag = tag->link)
    {
        if (tag->tok != TOK_DYNAMICSTRUCT)
            continue;

        ExprNode *selectorExpr = nullptr;
        ExprNode *typeNameExpr = nullptr;
        ExprNode *logicalOffsetExpr = nullptr;
        ExprNode *conditionExpr = nullptr;
        if (!dynamicTagArgs(tag->expr, &selectorExpr, &typeNameExpr, &logicalOffsetExpr, &conditionExpr))
            continue;

        INUMTYPE selector = 0;
        INUMTYPE condition = 0;
        INUMTYPE logicalOffset = 0;
        if (!evaluate(row, selectorExpr, &selector, row->absoluteOffset)
            || selector != arrayIndex
            || !evaluate(row, conditionExpr, &condition, row->absoluteOffset)
            || condition == 0
            || !evaluate(row, logicalOffsetExpr, &logicalOffset, row->absoluteOffset))
        {
            continue;
        }

        if (!typeNameExpr || typeNameExpr->type != EXPR_IDENTIFIER || !typeNameExpr->str)
            continue;

        TypeDecl *targetType = findTypeDecl(typeNameExpr->str);
        if (!targetType)
            continue;

        m_dynamicRequests.push_back(DynamicRequest{ targetType, uint64_t(logicalOffset) });
    }
}

void StructureRenderEngine::appendDynamicRows(StructureRow *parent)
{
    if (!parent)
        return;

    for (DynamicContainer &container : m_dynamicContainers)
    {
        if (!container.typeDecl)
            continue;

        Type *renderType = container.typeDecl->declList.empty() ? container.typeDecl->baseType : container.typeDecl->declList[0];
        auto row = makeRow(parent, renderType, container.typeDecl, m_baseOffset + container.fileOffset);
        applyDeclarationName(row.get(), renderType);
        if (!container.alias.isEmpty())
        {
            row->name = row->name + QLatin1Char(' ') + container.alias;
            row->nameIdentifier = container.alias;
        }
        row->value = QStringLiteral("{...}");
        row->byteLength = container.byteLength;
        row->kind = StructureRowKind::Dynamic;
        row->branchIconPath = QString::fromLatin1(kDynamicBranchClosedIconPath);
        row->branchOpenIconPath = QString::fromLatin1(kDynamicBranchOpenIconPath);
        row->branchEmptyIconPath = QString::fromLatin1(kDynamicBranchEmptyIconPath);
        container.row = row.get();
        parent->children.push_back(std::move(row));
    }

    for (const DynamicRequest &request : m_dynamicRequests)
    {
        DynamicContainer *container = nullptr;
        uint64_t fileOffset = 0;
        if (!(container = mapLogicalOffset(request.logicalOffset, &fileOffset)) || !container->row || !request.typeDecl)
            continue;

        Type *renderType = request.typeDecl->declList.empty() ? request.typeDecl->baseType : request.typeDecl->declList[0];
        auto row = makeRow(container->row, renderType, request.typeDecl, m_baseOffset + fileOffset);
        applyDeclarationName(row.get(), renderType);
        row->kind = StructureRowKind::Dynamic;
        row->branchIconPath = QString::fromLatin1(kDynamicBranchClosedIconPath);
        row->branchOpenIconPath = QString::fromLatin1(kDynamicBranchOpenIconPath);
        row->branchEmptyIconPath = QString::fromLatin1(kDynamicBranchEmptyIconPath);
        row->byteLength = formatType(row.get(), renderType, request.typeDecl, m_baseOffset + fileOffset);
        if (row->value.isEmpty() && !row->children.empty())
            row->value = QStringLiteral("{...}");
        container->row->children.push_back(std::move(row));
    }
}

bool StructureRenderEngine::dynamicTagArgs(ExprNode *expr,
                                           ExprNode **selector,
                                           ExprNode **typeName,
                                           ExprNode **logicalOffset,
                                           ExprNode **condition) const
{
    std::vector<ExprNode *> args;
    appendCommaArgs(expr, &args);
    if (args.size() != 4)
        return false;

    if (selector)
        *selector = args[0];
    if (typeName)
        *typeName = args[1];
    if (logicalOffset)
        *logicalOffset = args[2];
    if (condition)
        *condition = args[3];
    return true;
}

bool StructureRenderEngine::dynamicContainerArgs(ExprNode *expr, ExprNode **typeName) const
{
    std::vector<ExprNode *> args;
    appendCommaArgs(expr, &args);
    if (args.empty())
        return false;

    if (typeName)
        *typeName = args[0];
    return true;
}

bool StructureRenderEngine::offsetMapArgs(ExprNode *expr,
                                          ExprNode **logicalStart,
                                          ExprNode **logicalSize,
                                          ExprNode **fileOffset) const
{
    std::vector<ExprNode *> args;
    appendCommaArgs(expr, &args);
    if (args.size() != 3)
        return false;

    if (logicalStart)
        *logicalStart = args[0];
    if (logicalSize)
        *logicalSize = args[1];
    if (fileOffset)
        *fileOffset = args[2];
    return true;
}

TypeDecl *StructureRenderEngine::findTypeDecl(const char *name) const
{
    if (!m_library || !name)
        return nullptr;

    for (TypeDecl *decl : m_library->globalTypeDeclList)
    {
        if (!decl)
            continue;

        for (Type *type : decl->declList)
        {
            if ((type->ty == typeTYPEDEF || type->ty == typeIDENTIFIER) && type->sym
                && std::strcmp(type->sym->name, name) == 0)
            {
                return decl;
            }
        }

        Type *base = BaseNode(decl->baseType);
        if (base && (base->ty == typeSTRUCT || base->ty == typeUNION) && base->sptr
            && base->sptr->symbol && std::strcmp(base->sptr->symbol->name, name) == 0)
        {
            return decl;
        }
    }

    return nullptr;
}

StructureRenderEngine::DynamicContainer *StructureRenderEngine::mapLogicalOffset(uint64_t logicalOffset,
                                                                                 uint64_t *fileOffset)
{
    if (!fileOffset)
        return nullptr;

    for (DynamicContainer &container : m_dynamicContainers)
    {
        for (const OffsetMap &map : container.maps)
        {
            if (logicalOffset < map.logicalStart || logicalOffset >= map.logicalStart + map.logicalSize)
                continue;

            *fileOffset = map.fileOffset + (logicalOffset - map.logicalStart);
            return &container;
        }
    }

    return nullptr;
}

QString StructureRenderEngine::typeName(Type *type) const
{
    if (!type)
        return {};

    switch (type->ty)
    {
    case typeIDENTIFIER:
        return type->sym ? QString::fromLocal8Bit(type->sym->name) : QString();
    case typeTYPEDEF:
        return type->sym ? QString::fromLocal8Bit(type->sym->name) : typeName(type->link);
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
        return typeName(type->link) + QStringLiteral("[]");
    case typePOINTER:
        return typeName(type->link) + QStringLiteral(" *");
    case typeSIGNED:
        return QStringLiteral("signed %1").arg(typeName(type->link));
    case typeUNSIGNED:
        return QStringLiteral("unsigned %1").arg(typeName(type->link));
    case typeCHAR: return QStringLiteral("char");
    case typeWCHAR: return QStringLiteral("wchar_t");
    case typeBYTE: return QStringLiteral("byte");
    case typeWORD: return QStringLiteral("word");
    case typeDWORD: return QStringLiteral("dword");
    case typeQWORD: return QStringLiteral("qword");
    case typeFLOAT: return QStringLiteral("float");
    case typeDOUBLE: return QStringLiteral("double");
    default:
        return typeName(type->link);
    }
}

QString StructureRenderEngine::formatOffset(uint64_t offset) const
{
    return QString::number(offset, 16).toUpper().rightJustified(8, QLatin1Char('0'));
}

bool StructureRenderEngine::isBigEndian(TypeDecl *typeDecl) const
{
    ExprNode *expr = nullptr;
    return FindTag(typeDecl ? typeDecl->tagList : nullptr, TOK_ENDIAN, &expr)
           && expr && expr->type == EXPR_STRINGBUF && expr->str
           && std::strcmp(expr->str, "big") == 0;
}

Enum *StructureRenderEngine::tagEnum(TypeDecl *typeDecl) const
{
    ExprNode *expr = nullptr;
    if (!m_library || !FindTag(typeDecl ? typeDecl->tagList : nullptr, TOK_ENUM, &expr) || !expr || !expr->str)
        return nullptr;

    if (Symbol *sym = LookupSymbol(m_library->globalTagSymbolList, expr->str))
        if (sym->type && sym->type->ty == typeENUM)
            return sym->type->eptr;

    if (Symbol *sym = LookupSymbol(m_library->globalIdentifierList, expr->str))
    {
        Type *base = BaseNode(sym->type);
        if (base && base->ty == typeENUM)
            return base->eptr;
    }

    return nullptr;
}

QString StructureRenderEngine::enumNameForValue(Enum *eptr, INUMTYPE value) const
{
    if (!eptr)
        return {};

    for (EnumField *field : eptr->fieldList)
    {
        if (field && field->val == value && field->name)
            return QString::fromLocal8Bit(field->name->name);
    }
    return {};
}

StructureRenderEngine::DeclarationParts StructureRenderEngine::declarationParts(Type *type) const
{
    DeclarationParts parts;
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

QString StructureRenderEngine::declarationName(Type *type) const
{
    const DeclarationParts parts = declarationParts(type);

    if (parts.prefix.isEmpty())
        return parts.name + parts.suffix;
    if (parts.name.isEmpty())
        return parts.prefix + parts.suffix;
    return parts.prefix + QLatin1Char(' ') + parts.name + parts.suffix;
}

void StructureRenderEngine::applyDeclarationName(StructureRow *row, Type *type) const
{
    if (!row)
        return;

    const DeclarationParts parts = declarationParts(type);
    row->name = declarationName(type);
    row->nameTypePrefix = parts.prefix;
    row->nameIdentifier = parts.name;
    row->nameSuffix = parts.suffix;
    row->emphasizeName = isCompoundDeclaration(type);
}

bool StructureRenderEngine::isCompoundDeclaration(Type *type) const
{
    for (Type *cursor = type; cursor; cursor = cursor->link)
    {
        Type *base = BaseNode(cursor);
        if (base && (base->ty == typeSTRUCT || base->ty == typeUNION))
            return true;
    }
    return false;
}

QString StructureRenderEngine::declaratorSuffix(Type *type) const
{
    QString suffix;
    for (Type *cursor = type; cursor; cursor = cursor->link)
    {
        if (cursor->ty == typeARRAY)
            suffix += QStringLiteral("[]");
    }
    return suffix;
}

QString StructureRenderEngine::stringArrayValue(StructureRow *scope, Type *type, TypeDecl *typeDecl, uint64_t offset)
{
    Type *arrayType = nullptr;
    for (Type *cursor = type; cursor; cursor = cursor->link)
    {
        if (cursor->ty == typeARRAY)
        {
            arrayType = cursor;
            break;
        }
    }

    if (!arrayType)
        return {};

    Type *elementType = BaseNode(arrayType->link);
    if (!elementType || (elementType->ty != typeCHAR && elementType->ty != typeWCHAR))
        return {};

    INUMTYPE count = 0;
    if (!evaluateArrayCount(scope, typeDecl, arrayType, &count, offset))
        return QString();

    const uint64_t boundedCount = std::min<uint64_t>(count, kMaxArrayElements);
    const uint64_t unitSize = elementType->ty == typeWCHAR ? 2 : 1;
    QByteArray bytes(static_cast<int>(boundedCount * unitSize), Qt::Uninitialized);
    const size_t got = m_reader ? m_reader(offset, reinterpret_cast<uint8_t *>(bytes.data()),
                                           static_cast<size_t>(bytes.size()))
                                : 0;
    bytes.truncate(static_cast<int>(got));

    QString text;
    if (elementType->ty == typeCHAR)
    {
        const int nul = bytes.indexOf('\0');
        if (nul >= 0)
            bytes.truncate(nul);
        text = QString::fromLatin1(bytes);
    }
    else
    {
        const bool bigEndian = isBigEndian(typeDecl);
        for (int i = 0; i + 1 < bytes.size(); i += 2)
        {
            const ushort ch = bigEndian
                ? ushort((uchar(bytes[i]) << 8) | uchar(bytes[i + 1]))
                : ushort(uchar(bytes[i]) | (uchar(bytes[i + 1]) << 8));
            if (ch == 0)
                break;
            text.append(QChar(ch));
        }
    }

    return quoteString(text);
}

QString StructureRenderEngine::scalarArrayValue(StructureRow *scope, Type *type) const
{
    Type *arrayType = nullptr;
    for (Type *cursor = type; cursor; cursor = cursor->link)
    {
        if (cursor->ty == typeARRAY)
        {
            arrayType = cursor;
            break;
        }
    }

    if (!scope || !arrayType)
        return {};

    Type *elementType = BaseNode(arrayType->link);
    if (!elementType || !isScalar(elementType->ty) || elementType->ty == typeCHAR || elementType->ty == typeWCHAR)
        return {};

    QStringList values;
    const qsizetype previewCount = std::min<qsizetype>(scope->children.size(), kMaxArrayPreviewElements);
    for (qsizetype i = 0; i < previewCount; ++i)
        values.push_back(scope->children[static_cast<size_t>(i)]->value);

    if (scope->children.size() > kMaxArrayPreviewElements)
        values.push_back(QStringLiteral("..."));

    return QStringLiteral("{ %1 }").arg(values.join(QStringLiteral(", ")));
}

QString StructureRenderEngine::fieldNameValue(StructureRow *scope, Type *scopeType, ExprNode *expr, uint64_t scopeOffset)
{
    if (!expr)
        return {};

    if (StructureRow *row = findFieldRow(scope, expr))
    {
        if (!row->value.isEmpty() && row->value != QStringLiteral("{...}"))
            return row->value;
    }

    ResolvedField field;
    if (!resolveField(scopeType, expr, scopeOffset, &field))
        return {};

    const QString stringValue = stringArrayValue(nullptr, field.type, field.typeDecl, field.offset);
    if (!stringValue.isNull())
        return stringValue;

    Type *base = BaseNode(field.type);
    if (!base || !isScalar(base->ty))
        return {};

    INUMTYPE value = 0;
    if (!readInteger(field.offset, field.length, &value))
        return {};

    return QString::number(value);
}

QString StructureRenderEngine::quoteString(const QString &text) const
{
    QString escaped;
    escaped.reserve(text.size() + 2);
    escaped += QLatin1Char('"');
    for (QChar ch : text)
    {
        if (ch == QLatin1Char('\\') || ch == QLatin1Char('"'))
            escaped += QLatin1Char('\\');
        if (ch.unicode() < 0x20)
            escaped += QLatin1Char('.');
        else
            escaped += ch;
    }
    escaped += QLatin1Char('"');
    return escaped;
}
