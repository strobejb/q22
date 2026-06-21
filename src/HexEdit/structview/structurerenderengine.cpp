#include "structview/structurerenderengine.h"

#include "structview/structurebranchicons.h"
#include "structview/structurecommentformatter.h"
#include "structview/structuresemanticview.h"
#include "structview/structuretypenameformatter.h"

#include <QByteArray>
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QLatin1String>
#include <QStringList>
#include <QTextStream>

#include <algorithm>
#include <cstring>

namespace
{
static constexpr uint64_t kMaxArrayElements = 100;
static constexpr qsizetype kMaxArrayPreviewElements = 8;
static constexpr bool kPrefixArrayAliasesWithDash = false;

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

QString cleanDynamicAlias(QString alias)
{
    alias = rowNameFragment(alias).trimmed();

    const QString prefix = arrayAliasPrefix();
    if (!prefix.isEmpty() && alias.startsWith(prefix))
        alias = alias.mid(prefix.size()).trimmed();

    return alias;
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

bool structureProfileEnabled()
{
    return qEnvironmentVariableIntValue("QEXED_STRUCTURE_PROFILE") != 0;
}

void structureProfileLog(const QString &message)
{
    qInfo().noquote() << message;

    const QString path = qEnvironmentVariable("QEXED_STRUCTURE_PROFILE_LOG",
                                              QStringLiteral("structure-profile.log"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;

    QTextStream stream(&file);
    stream << message << Qt::endl;
}

size_t structureRowCount(const StructureRow *row)
{
    if (!row)
        return 0;

    size_t count = 1;
    for (const auto &child : row->children)
        count += structureRowCount(child.get());
    return count;
}
}

struct StructureRenderEngine::ResolvedField
{
    Type *type = nullptr;
    TypeDecl *typeDecl = nullptr;
    uint64_t offset = 0;
    uint64_t length = 0;
    bool bigEndian = false;
};

struct StructureRenderEngine::EvalContext
{
    StructureRow *row = nullptr;
    Type *type = nullptr;
    uint64_t offset = 0;
};

struct StructureRenderEngine::EndianScope
{
    EndianScope(StructureRenderEngine *engine, bool bigEndian)
        : engine(engine)
        , previous(engine ? engine->m_bigEndian : false)
    {
        if (engine)
            engine->m_bigEndian = bigEndian;
    }

    ~EndianScope()
    {
        if (engine)
            engine->m_bigEndian = previous;
    }

    StructureRenderEngine *engine = nullptr;
    bool previous = false;
};

struct StructureRenderEngine::AlignmentScope
{
    AlignmentScope(StructureRenderEngine *engine, uint64_t alignment)
        : engine(engine)
        , previous(engine ? engine->m_structAlignment : 1)
    {
        if (engine)
            engine->m_structAlignment = alignment > 0 ? alignment : 1;
    }

    ~AlignmentScope()
    {
        if (engine)
            engine->m_structAlignment = previous;
    }

    StructureRenderEngine *engine = nullptr;
    uint64_t previous = 1;
};

StructureRenderEngine::StructureRenderEngine(TypeLibrary *library,
                                             TypeDecl *rootType,
                                             uint64_t baseOffset,
                                             const StructureValueBuilder::ByteReader &reader,
                                             const StructureDisplayOptions &options)
    : m_library(library)
    , m_rootType(rootType)
    , m_baseOffset(baseOffset)
    , m_options(options)
    , m_reader(reader)
{
}

std::vector<std::unique_ptr<StructureRow>> StructureRenderEngine::build()
{
    std::vector<RowPtr> rows;
    if (!m_rootType)
        return rows;

    const bool profile = structureProfileEnabled();
    QElapsedTimer totalTimer;
    QElapsedTimer phaseTimer;
    if (profile)
    {
        totalTimer.start();
        phaseTimer.start();
    }

    auto root = makeRow(nullptr, m_rootType->declList.empty() ? m_rootType->baseType : m_rootType->declList[0],
                        m_rootType, m_baseOffset);
    root->parent = nullptr;
    root->value = QStringLiteral("{...}");
    EndianScope rootEndian(this, declarationBigEndian(m_rootType, root.get(), root->type, m_baseOffset));
    AlignmentScope rootAlignment(this, declarationAlignment(m_rootType, root.get(), root->type, m_baseOffset, m_structAlignment));
    root->bigEndian = m_bigEndian;
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
    if (profile)
    {
        structureProfileLog(QStringLiteral("[StructureProfile] engine raw rows=%1 ms=%2")
                                .arg(structureRowCount(root.get()))
                                .arg(phaseTimer.restart()));
    }
    collectDynamicRows();
    collectDynamicRows(root.get());
    appendDynamicRows(root.get());
    if (profile)
    {
        structureProfileLog(QStringLiteral("[StructureProfile] engine dynamic_struct rows=%1 containers=%2 ms=%3")
                                .arg(structureRowCount(root.get()))
                                .arg(m_dynamicContainers.size())
                                .arg(phaseTimer.restart()));
    }
    collectDynamicArrayRequests(root.get());
    appendDynamicArrayRows(root.get());
    if (profile)
    {
        structureProfileLog(QStringLiteral("[StructureProfile] engine dynamic_array rows=%1 requests=%2 ms=%3")
                                .arg(structureRowCount(root.get()))
                                .arg(m_dynamicArrayRequests.size())
                                .arg(phaseTimer.restart()));
    }
    resolveEntryPointRows(root.get());
    if (profile)
    {
        structureProfileLog(QStringLiteral("[StructureProfile] engine entrypoints ms=%1")
                                .arg(phaseTimer.restart()));
    }

    std::vector<StructureOffsetMap> semanticOffsetMaps;
    for (const DynamicContainer &container : m_dynamicContainers)
    {
        for (const OffsetMap &map : container.maps)
            semanticOffsetMaps.push_back(StructureOffsetMap{ map.logicalStart, map.logicalSize, map.fileOffset });
    }
    runStructureSemanticViews(m_library, root.get(), m_baseOffset, m_reader, semanticOffsetMaps);
    if (profile)
    {
        structureProfileLog(QStringLiteral("[StructureProfile] engine semantic rows=%1 maps=%2 ms=%3 total=%4")
                                .arg(structureRowCount(root.get()))
                                .arg(semanticOffsetMaps.size())
                                .arg(phaseTimer.restart())
                                .arg(totalTimer.elapsed()));
    }

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
    row->bigEndian = m_bigEndian;
    row->sourceRef = typeDecl && typeDecl->tagRef.fileDesc ? typeDecl->tagRef
        : (typeDecl ? typeDecl->fileRef : FILEREF());
    if (row->sourceRef.fileDesc)
    {
        row->sourcePath = QString::fromLocal8Bit(row->sourceRef.fileDesc->filePath);
        row->sourceLine = static_cast<int>(row->sourceRef.lineNo);
    }
    row->absoluteOffset = offset;
    row->relativeOffset = offset >= m_baseOffset ? offset - m_baseOffset : 0;
    row->offset = formatOffset(offset);
    row->generatedOffset = true;
    row->comment = structureDisplayComment(typeDecl);
    return row;
}

uint64_t StructureRenderEngine::appendTypeDecl(StructureRow *parent, TypeDecl *typeDecl, uint64_t offset)
{
    if (!typeDecl || typeDecl->typeAlias)
        return 0;

    const uint64_t originalOffset = offset;
    if (declarationIsOptionalAndAbsent(typeDecl, parent, parent ? parent->type : nullptr, offset))
        return 0;

    ExprNode *offsetExpr = nullptr;
    if (FindTag(typeDecl->tagList, TOK_OFFSET, &offsetExpr))
    {
        INUMTYPE evaluated = offset;
        if (evaluate(parent, offsetExpr, &evaluated, offset))
            offset = m_baseOffset + evaluated;
    }
    else
    {
        const uint64_t alignment = typeDecl->compoundType
            ? m_structAlignment
            : declarationAlignment(typeDecl, parent, parent ? parent->type : nullptr, offset, m_structAlignment);
        offset = alignedOffset(offset, alignment);
    }

    const bool bigEndian = declarationBigEndian(typeDecl, parent, parent ? parent->type : nullptr, offset);
    EndianScope endian(this, bigEndian);
    const uint64_t childAlignment = declarationAlignment(typeDecl,
                                                         parent,
                                                         parent ? parent->type : nullptr,
                                                         offset,
                                                         m_structAlignment);

    uint64_t length = 0;
    if (typeDecl->declList.empty() && typeDecl->nested)
    {
        AlignmentScope alignment(this, childAlignment);
        length = recurseType(parent, typeDecl->baseType, typeDecl, offset);
        length = declarationExtent(typeDecl, parent, parent ? parent->type : nullptr, offset, length);
        return offset >= originalOffset ? (offset - originalOffset) + length : length;
    }

    AlignmentScope alignment(this, typeDecl->compoundType ? childAlignment : m_structAlignment);
    for (Type *type : typeDecl->declList)
        length += recurseType(parent, type, typeDecl, offset + length);

    length = declarationExtent(typeDecl, parent, parent ? parent->type : nullptr, offset, length);
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
    applyEntryPointTag(row.get(), typeDecl, type ? type->link : nullptr, offset);
    const QString stringValue = stringArrayValue(row.get(), type ? type->link : nullptr, typeDecl, offset);
    if (!stringValue.isNull())
        row->value = stringValue;
    else
    {
        const QString arrayValue = scalarArrayValue(row.get(), type ? type->link : nullptr);
        if (!arrayValue.isNull())
        {
            row->valueKind = StructureRowValueKind::ScalarArrayPreview;
            row->value = arrayValue;
        }
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
        ExprNode *terminatorExpr = nullptr;
        FindTag(typeDecl ? typeDecl->tagList : nullptr, TOK_TERMINATEDBY, &terminatorExpr);
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
            row->suppressSemanticViews = true;
            const QString enumLabel = enumNameForValue(nameEnum, i);
            if (!enumLabel.isEmpty())
            {
                row->nameIdentifier = arrayAliasPrefix() + enumLabel;
                row->name += row->nameIdentifier;
            }

            const uint64_t elementLength = formatType(row.get(), type->link, typeDecl, offset + length);
            row->byteLength = elementLength;
            const bool terminates = elementMatchesTerminator(row.get(), type->link, terminatorExpr, offset + length);
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
            if (terminates)
                break;

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
        const bool hasSwitchTag = FindTag(typeDecl ? typeDecl->tagList : nullptr, TOK_SWITCHIS, &switchExpr);
        const bool hasSwitch = hasSwitchTag && evaluate(EvalContext{ parent, type, offset }, switchExpr, &switchValue);
        if (hasSwitchTag && !hasSwitch)
            return 0;

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

    const uint64_t raw = unsignedValue(data, length, m_bigEndian);
    Enum *displayEnum = tagEnum(typeDecl);
    if (!displayEnum && base->ty == typeENUM)
        displayEnum = base->eptr;

    if (displayEnum)
    {
        const QString enumName = enumNameForValue(displayEnum, raw);
        row->value = enumName.isEmpty() ? QString::number(raw, 16).toUpper().rightJustified(int(length * 2), QLatin1Char('0'))
                                        : enumName;
        row->valueChoices = enumChoiceLabels(displayEnum);
        return length;
    }

    switch (base->ty)
    {
    case typeCHAR:
    {
        const char ch = data[0] >= ' ' ? char(data[0]) : '.';
        row->valueKind = StructureRowValueKind::ScalarInteger;
        row->scalarRawValue = raw;
        row->scalarByteLength = length;
        row->scalarSigned = false;
        row->scalarCharacterSuffix = QStringLiteral(" '%1'").arg(QLatin1Char(ch));
        row->value = formatStructureIntegerValue(raw, length, false, row->scalarCharacterSuffix, m_options);
        break;
    }
    case typeWCHAR:
        row->valueKind = StructureRowValueKind::ScalarInteger;
        row->scalarRawValue = raw;
        row->scalarByteLength = length;
        row->scalarSigned = false;
        row->scalarCharacterSuffix.clear();
        row->value = formatStructureIntegerValue(raw, length, false, QString(), m_options);
        break;
    case typeBYTE:
    case typeWORD:
    case typeDWORD:
    case typeQWORD:
    {
        row->valueKind = StructureRowValueKind::ScalarInteger;
        row->scalarRawValue = raw;
        row->scalarByteLength = length;
        row->scalarSigned = FindType(type, typeSIGNED);
        row->scalarCharacterSuffix.clear();
        row->value = formatStructureIntegerValue(raw, length, row->scalarSigned, QString(), m_options);
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
        row->valueKind = StructureRowValueKind::ScalarInteger;
        row->scalarRawValue = raw;
        row->scalarByteLength = length;
        row->scalarSigned = false;
        row->scalarCharacterSuffix.clear();
        row->value = formatStructureIntegerValue(raw, length, false, QString(), m_options);
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

    StructureRow *evalScope = scope && scope->parent ? scope->parent : scope;

    if (arrayType->elements)
        return evaluate(evalScope, arrayType->elements, result, offset);

    ExprNode *sizeExpr = nullptr;
    if (!FindTag(typeDecl ? typeDecl->tagList : nullptr, TOK_SIZEIS, &sizeExpr) || !sizeExpr)
        return false;

    std::vector<ExprNode *> args;
    appendCommaArgs(sizeExpr, &args);
    return !args.empty() && evaluate(evalScope, args[0], result, offset);
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
            return readInteger(row->absoluteOffset, row->byteLength, result, row->bigEndian);

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

        ResolvedField field;
        if (resolveField(context.type, expr, context.offset, &field))
            return readInteger(field.offset, field.length, result, field.bigEndian);
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
    case EXPR_SIZEOF:
    {
        if (!expr->left || expr->left->type != EXPR_IDENTIFIER || !expr->left->str)
            return false;

        const uint64_t size = staticSizeOfName(expr->left->str);
        if (size == 0)
            return false;

        *result = static_cast<INUMTYPE>(size);
        return true;
    }
    default:
        return false;
    }
}

uint64_t StructureRenderEngine::staticSizeOfName(const char *name)
{
    if (!name)
        return 0;

    struct BuiltIn
    {
        const char *name;
        TYPE type;
    };

    static constexpr BuiltIn builtIns[] =
    {
        { "char", typeCHAR },
        { "wchar_t", typeWCHAR },
        { "byte", typeBYTE },
        { "word", typeWORD },
        { "dword", typeDWORD },
        { "qword", typeQWORD },
        { "float", typeFLOAT },
        { "double", typeDOUBLE },
    };

    for (const BuiltIn &builtIn : builtIns)
        if (std::strcmp(name, builtIn.name) == 0)
            return scalarSize(builtIn.type);

    if (!m_library)
        return 0;

    if (Symbol *sym = LookupSymbol(m_library->globalIdentifierList, const_cast<char *>(name)))
        if (const uint64_t size = staticSizeOfType(sym->type))
            return size;

    for (TypeDecl *decl : m_library->globalTypeDeclList)
    {
        if (!decl)
            continue;

        for (Type *type : decl->declList)
        {
            if ((type->ty == typeTYPEDEF || type->ty == typeIDENTIFIER) && type->sym
                && std::strcmp(type->sym->name, name) == 0)
            {
                return staticSizeOfType(type);
            }
        }

        Type *base = BaseNode(decl->baseType);
        if (base && (base->ty == typeSTRUCT || base->ty == typeUNION) && base->sptr
            && base->sptr->symbol && std::strcmp(base->sptr->symbol->name, name) == 0)
        {
            return sizeOf(base, m_baseOffset);
        }
    }

    return 0;
}

uint64_t StructureRenderEngine::staticSizeOfType(Type *type)
{
    if (!type)
        return 0;

    for (Type *cursor = type; cursor; cursor = cursor->link)
    {
        switch (cursor->ty)
        {
        case typeTYPEDEF:
        case typeCONST:
        case typeSIGNED:
        case typeUNSIGNED:
            continue;

        case typeSTRUCT:
        case typeUNION:
        case typeARRAY:
        case typeIDENTIFIER:
            return sizeOf(cursor, m_baseOffset);

        default:
            return scalarSize(cursor->ty);
        }
    }

    return 0;
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

        const bool bigEndian = m_evaluatingEndian
            ? m_bigEndian
            : declarationBigEndian(decl, nullptr, scopeType, scopeOffset);
        EndianScope endian(this, bigEndian);

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
                field->bigEndian = m_bigEndian;
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

bool StructureRenderEngine::readInteger(uint64_t offset, uint64_t length, INUMTYPE *result, bool bigEndian) const
{
    uint8_t data[sizeof(INUMTYPE)] = {};
    const size_t requested = static_cast<size_t>(std::max<uint64_t>(1, std::min<uint64_t>(length, sizeof(data))));
    const size_t got = m_reader ? m_reader(offset, data, requested) : 0;
    if (got == 0)
        return false;

    *result = unsignedValue(data, got, bigEndian);
    return true;
}

void StructureRenderEngine::collectDynamicRows()
{
    m_dynamicContainers.clear();
    m_dynamicRequests.clear();
    m_dynamicArrayRequests.clear();
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
    container.alias = dynamicContainerAlias(row);

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

void StructureRenderEngine::collectDynamicArrayRequests(StructureRow *row)
{
    if (!row || !row->typeDecl)
        return;

    INUMTYPE probeArrayIndex = 0;
    const bool rowIsArrayElement = arrayIndexFromRow(row, &probeArrayIndex);

    for (Tag *tag = row->typeDecl->tagList; tag; tag = tag->link)
    {
        if (tag->tok != TOK_DYNAMICARRAY)
            continue;

        ExprNode *selectorOrLabelExpr = nullptr;
        ExprNode *typeNameExpr = nullptr;
        ExprNode *logicalOffsetExpr = nullptr;
        ExprNode *countExpr = nullptr;
        ExprNode *stopExpr = nullptr;
        ExprNode *conditionExpr = nullptr;
        if (!dynamicArrayArgs(tag->expr,
                              &selectorOrLabelExpr,
                              &typeNameExpr,
                              &logicalOffsetExpr,
                              &countExpr,
                              &stopExpr,
                              &conditionExpr))
            continue;

        // dynamic_array mirrors dynamic_struct for directory arrays: when the
        // owner row is itself an array element, the first argument can select
        // which element emits the referenced table.  On ordinary rows the same
        // argument is only a display label for the generated array.
        bool attachToMappedContainer = false;
        if (rowIsArrayElement)
        {
            INUMTYPE arrayIndex = 0;
            INUMTYPE selector = 0;
            if (arrayIndexFromRow(row, &arrayIndex)
                && evaluate(row, selectorOrLabelExpr, &selector, row->absoluteOffset))
            {
                if (selector != arrayIndex)
                    continue;
                attachToMappedContainer = true;
            }
        }

        if (!typeNameExpr || typeNameExpr->type != EXPR_IDENTIFIER || !typeNameExpr->str)
            continue;

        TypeDecl *targetType = findTypeDecl(typeNameExpr->str);
        Type *renderType = typeInDecl(targetType, typeNameExpr->str);
        if (!targetType || !renderType)
            continue;

        INUMTYPE logicalOffset = 0;
        INUMTYPE count = 0;
        INUMTYPE condition = 1;
        if (!evaluate(row, logicalOffsetExpr, &logicalOffset, row->absoluteOffset)
            || !evaluate(row, countExpr, &count, row->absoluteOffset)
            || (conditionExpr && (!evaluate(row, conditionExpr, &condition, row->absoluteOffset) || condition == 0))
            || count <= 0)
        {
            continue;
        }

        QString label;
        if (selectorOrLabelExpr && selectorOrLabelExpr->str
            && (selectorOrLabelExpr->type == EXPR_IDENTIFIER || selectorOrLabelExpr->type == EXPR_STRINGBUF))
        {
            label = QString::fromLocal8Bit(selectorOrLabelExpr->str);
        }

        m_dynamicArrayRequests.push_back(DynamicArrayRequest{
            row,
            targetType,
            renderType,
            label,
            uint64_t(logicalOffset),
            uint64_t(count),
            stopExpr,
            conditionExpr,
            attachToMappedContainer
        });
    }

    for (const auto &child : row->children)
        collectDynamicArrayRequests(child.get());
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
            row->setNameParts(row->nameTypePrefix, container.alias, row->nameSuffix, row->emphasizeName);
        }
        row->value = QStringLiteral("{...}");
        row->byteLength = container.byteLength;
        row->kind = StructureRowKind::Dynamic;
        row->setBranchIcons(QString::fromLatin1(StructureBranchIcons::kBlueDoubleClosed),
                            QString::fromLatin1(StructureBranchIcons::kBlueDoubleOpen),
                            QString::fromLatin1(StructureBranchIcons::kGrayDoubleClosed));
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
        row->setBranchIcons(QString::fromLatin1(StructureBranchIcons::kBlueDoubleClosed),
                            QString::fromLatin1(StructureBranchIcons::kBlueDoubleOpen),
                            QString::fromLatin1(StructureBranchIcons::kGrayDoubleClosed));
        const bool bigEndian = declarationBigEndian(request.typeDecl, row.get(), renderType, m_baseOffset + fileOffset);
        EndianScope endian(this, bigEndian);
        row->bigEndian = m_bigEndian;
        row->byteLength = formatType(row.get(), renderType, request.typeDecl, m_baseOffset + fileOffset);
        if (row->value.isEmpty() && !row->children.empty())
            row->value = QStringLiteral("{...}");
        container->row->children.push_back(std::move(row));
    }
}

void StructureRenderEngine::appendDynamicArrayRows(StructureRow *row)
{
    if (!row)
        return;

    // Index-based iteration: collectDynamicArrayRequests called inside this loop
    // may push new entries (sub-array requests for element rows), which is safe
    // because we re-evaluate .size() each iteration and copy requests by value.
    for (size_t ri = 0; ri < m_dynamicArrayRequests.size(); ++ri)
    {
        const DynamicArrayRequest request = m_dynamicArrayRequests[ri];
        if (request.owner != row || !request.renderType)
            continue;

        uint64_t fileOffset = 0;
        DynamicContainer *container = mapLogicalOffset(request.logicalOffset, &fileOffset);
        if (!container)
            continue;

        StructureRow *parentRow = request.attachToMappedContainer && container->row ? container->row : row;

        auto arrayRow = makeRow(parentRow, request.renderType, request.typeDecl, m_baseOffset + fileOffset);
        const QString elementTypeName = typeName(request.renderType);
        const QString label = request.label.isEmpty() ? elementTypeName : request.label;
        arrayRow->setNameParts(elementTypeName, label, QStringLiteral("[]"));
        arrayRow->value = QStringLiteral("{...}");
        arrayRow->kind = StructureRowKind::Dynamic;
        arrayRow->generatedName = true;
        arrayRow->setBranchIcons(QString::fromLatin1(StructureBranchIcons::kBlueDoubleClosed),
                                 QString::fromLatin1(StructureBranchIcons::kBlueDoubleOpen),
                                 QString::fromLatin1(StructureBranchIcons::kGrayDoubleClosed));

        // Check once whether the element type declares sub-arrays.  Primitive
        // element types (DWORD, WORD, CHAR, thunk unions, …) never do, so we
        // avoid calling collectDynamicArrayRequests for every element of the
        // large export/import raw-data arrays.
        bool elementTypeHasSubArrays = false;
        for (Tag *tag = request.typeDecl ? request.typeDecl->tagList : nullptr; tag; tag = tag->link)
        {
            if (tag->tok == TOK_DYNAMICARRAY) { elementTypeHasSubArrays = true; break; }
        }

        uint64_t length = 0;
        const uint64_t boundedCount = std::min<uint64_t>(request.maxCount, kMaxArrayElements);
        for (uint64_t i = 0; i < boundedCount; ++i)
        {
            auto elementRow = makeRow(arrayRow.get(), request.renderType, request.typeDecl, m_baseOffset + fileOffset + length);
            const QString indexLabel = QStringLiteral("[%1]").arg(i);
            elementRow->name = indexLabel;
            elementRow->nameTypePrefix = indexLabel;
            elementRow->kind = StructureRowKind::Dynamic;
            elementRow->suppressSemanticViews = true;

            const uint64_t elementLength = formatType(elementRow.get(), request.renderType, request.typeDecl, m_baseOffset + fileOffset + length);
            elementRow->byteLength = elementLength;

            // For compound element types, collect the sub-array requests that
            // reference this element's fields (e.g. DllName RVA, thunk RVAs) while
            // the element's children are live, then defer the actual array building
            // to a lazy loader so the initial tree open stays fast.
            if (elementTypeHasSubArrays)
            {
                const size_t requestsBefore = m_dynamicArrayRequests.size();
                collectDynamicArrayRequests(elementRow.get());

                if (m_dynamicArrayRequests.size() > requestsBefore)
                {
                    std::vector<DynamicArrayRequest> subRequests(
                        m_dynamicArrayRequests.begin() + static_cast<ptrdiff_t>(requestsBefore),
                        m_dynamicArrayRequests.end());
                    m_dynamicArrayRequests.erase(
                        m_dynamicArrayRequests.begin() + static_cast<ptrdiff_t>(requestsBefore),
                        m_dynamicArrayRequests.end());

                    StructureRow *elemPtr = elementRow.get();
                    auto self = shared_from_this();
                    elementRow->lazyChildLoader = [self, elemPtr, reqs = std::move(subRequests)]() mutable {
                        return self->buildSubArraysForElement(elemPtr, std::move(reqs));
                    };
                }
            }

            const bool terminates = elementMatchesTerminator(elementRow.get(),
                                                             request.renderType,
                                                             request.stopExpr,
                                                             m_baseOffset + fileOffset + length);
            length += elementLength;
            if (terminates)
                break;

            arrayRow->children.push_back(std::move(elementRow));
        }

        arrayRow->byteLength = length;
        if (!arrayRow->children.empty())
            parentRow->children.push_back(std::move(arrayRow));
    }

    for (const auto &child : row->children)
        appendDynamicArrayRows(child.get());
}

std::vector<StructureRenderEngine::RowPtr> StructureRenderEngine::buildSubArraysForElement(
    StructureRow *elementRow,
    std::vector<DynamicArrayRequest> subRequests)
{
    const size_t childrenBefore = elementRow->children.size();

    auto savedRequests = std::move(m_dynamicArrayRequests);
    m_dynamicArrayRequests = std::move(subRequests);

    appendDynamicArrayRows(elementRow);

    m_dynamicArrayRequests = std::move(savedRequests);

    const size_t childrenAfter = elementRow->children.size();
    std::vector<RowPtr> result;
    result.reserve(childrenAfter - childrenBefore);
    for (size_t i = childrenBefore; i < childrenAfter; ++i)
        result.push_back(std::move(elementRow->children[i]));
    elementRow->children.erase(
        elementRow->children.begin() + static_cast<ptrdiff_t>(childrenBefore),
        elementRow->children.end());
    return result;
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

bool StructureRenderEngine::dynamicArrayArgs(ExprNode *expr,
                                             ExprNode **selectorOrLabel,
                                             ExprNode **typeName,
                                             ExprNode **logicalOffset,
                                             ExprNode **count,
                                             ExprNode **stop,
                                             ExprNode **condition) const
{
    std::vector<ExprNode *> args;
    appendCommaArgs(expr, &args);
    if (args.size() != 4 && args.size() != 5 && args.size() != 6)
        return false;

    if (selectorOrLabel)
        *selectorOrLabel = args[0];
    if (typeName)
        *typeName = args[1];
    if (logicalOffset)
        *logicalOffset = args[2];
    if (count)
        *count = args[3];
    if (stop)
        *stop = args.size() >= 5 ? args[4] : nullptr;
    if (condition)
        *condition = args.size() == 6 ? args[5] : nullptr;
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

Type *StructureRenderEngine::typeInDecl(TypeDecl *decl, const char *name) const
{
    if (!decl)
        return nullptr;

    if (name)
    {
        for (Type *type : decl->declList)
        {
            if ((type->ty == typeTYPEDEF || type->ty == typeIDENTIFIER) && type->sym
                && std::strcmp(type->sym->name, name) == 0)
            {
                return type;
            }
        }
    }

    return decl->declList.empty() ? decl->baseType : decl->declList[0];
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

void StructureRenderEngine::resolveEntryPointRows(StructureRow *row)
{
    if (!row)
        return;

    if (row->hasCodeTarget)
    {
        uint64_t mappedOffset = 0;
        if (mapLogicalOffset(row->codeLogicalOffset, &mappedOffset))
            row->codeTargetOffset = m_baseOffset + mappedOffset;
        else
            row->codeTargetOffset = m_baseOffset + row->codeLogicalOffset;
    }

    for (auto &child : row->children)
        resolveEntryPointRows(child.get());
}

QString StructureRenderEngine::typeName(Type *type) const
{
    return StructureTypeNameFormatter(m_options).typeName(type);
}

QString StructureRenderEngine::formatOffset(uint64_t offset) const
{
    const uint64_t relativeOffset = offset >= m_baseOffset ? offset - m_baseOffset : 0;
    return formatStructureOffset(offset, relativeOffset, m_options);
}

uint64_t StructureRenderEngine::alignedOffset(uint64_t offset, uint64_t alignment) const
{
    if (alignment <= 1)
        return offset;

    const uint64_t remainder = offset % alignment;
    if (remainder == 0)
        return offset;

    const uint64_t increment = alignment - remainder;
    return offset > UINT64_MAX - increment ? offset : offset + increment;
}

uint64_t StructureRenderEngine::declarationAlignment(TypeDecl *typeDecl,
                                                     StructureRow *scope,
                                                     Type *scopeType,
                                                     uint64_t scopeOffset,
                                                     uint64_t fallback) const
{
    ExprNode *expr = nullptr;
    if (!FindTag(typeDecl ? typeDecl->tagList : nullptr, TOK_ALIGN, &expr) || !expr)
        return fallback > 0 ? fallback : 1;

    INUMTYPE value = 0;
    if (!const_cast<StructureRenderEngine *>(this)->evaluate(EvalContext{ scope, scopeType, scopeOffset }, expr, &value)
        || value <= 0)
        return fallback > 0 ? fallback : 1;

    return static_cast<uint64_t>(value);
}

uint64_t StructureRenderEngine::declarationExtent(TypeDecl *typeDecl,
                                                  StructureRow *scope,
                                                  Type *scopeType,
                                                  uint64_t scopeOffset,
                                                  uint64_t fallback)
{
    ExprNode *expr = nullptr;
    if (!FindTag(typeDecl ? typeDecl->tagList : nullptr, TOK_EXTENT, &expr) || !expr)
        return fallback;

    INUMTYPE value = 0;
    if (!evaluate(EvalContext{ scope, scopeType, scopeOffset }, expr, &value) || value <= 0)
        return fallback;

    return static_cast<uint64_t>(value);
}

bool StructureRenderEngine::declarationIsOptionalAndAbsent(TypeDecl *typeDecl,
                                                           StructureRow *scope,
                                                           Type *scopeType,
                                                           uint64_t scopeOffset)
{
    ExprNode *expr = nullptr;
    if (!FindTag(typeDecl ? typeDecl->tagList : nullptr, TOK_OPTIONAL, &expr) || !expr)
        return false;

    INUMTYPE value = 0;
    return !evaluate(EvalContext{ scope, scopeType, scopeOffset }, expr, &value) || value == 0;
}

bool StructureRenderEngine::declarationBigEndian(TypeDecl *typeDecl,
                                                 StructureRow *scope,
                                                 Type *scopeType,
                                                 uint64_t scopeOffset)
{
    ExprNode *expr = nullptr;
    if (!FindTag(typeDecl ? typeDecl->tagList : nullptr, TOK_ENDIAN, &expr) || !expr)
        return m_bigEndian;

    if (expr->type == EXPR_STRINGBUF && expr->str)
    {
        if (std::strcmp(expr->str, "big") == 0)
            return true;
        if (std::strcmp(expr->str, "little") == 0)
            return false;
        return m_bigEndian;
    }

    INUMTYPE value = 0;
    const bool wasEvaluatingEndian = m_evaluatingEndian;
    m_evaluatingEndian = true;
    const bool ok = evaluate(EvalContext{ scope, scope ? scope->type : scopeType, scopeOffset }, expr, &value);
    m_evaluatingEndian = wasEvaluatingEndian;
    return ok ? value != 0 : m_bigEndian;
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

QStringList StructureRenderEngine::enumChoiceLabels(Enum *eptr) const
{
    QStringList labels;
    if (!eptr)
        return labels;

    for (EnumField *field : eptr->fieldList)
    {
        if (field && field->name)
            labels.push_back(QString::fromLocal8Bit(field->name->name));
    }
    return labels;
}

void StructureRenderEngine::applyEntryPointTag(StructureRow *row, TypeDecl *typeDecl, Type *scopeType, uint64_t scopeOffset)
{
    ExprNode *expr = nullptr;
    if (!row || !FindTag(typeDecl ? typeDecl->tagList : nullptr, TOK_ENTRYPOINT, &expr) || !expr)
        return;

    INUMTYPE value = 0;
    const bool selfField = expr->type == EXPR_IDENTIFIER
        && expr->str
        && row->type
        && row->type->sym
        && std::strcmp(expr->str, row->type->sym->name) == 0
        && row->valueKind == StructureRowValueKind::ScalarInteger;
    if (selfField)
    {
        value = static_cast<INUMTYPE>(row->scalarRawValue);
    }
    else if (!evaluate(row, expr, &value, scopeOffset) && !evaluate(row->parent, expr, &value, scopeOffset))
    {
        return;
    }

    row->hasCodeTarget = true;
    row->codeLogicalOffset = static_cast<uint64_t>(value);
    row->codeTargetOffset = m_baseOffset + row->codeLogicalOffset;
    Q_UNUSED(scopeType);
}

void StructureRenderEngine::applyDeclarationName(StructureRow *row, Type *type) const
{
    if (!row)
        return;

    const StructureTypeNameFormatter formatter(m_options);
    const StructureDeclarationParts parts = formatter.declarationParts(type);
    row->setNameParts(parts.prefix, parts.name, parts.suffix, formatter.isCompoundDeclaration(type));
    row->name = formatter.declarationName(type);
    row->generatedName = true;
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
        for (int i = 0; i + 1 < bytes.size(); i += 2)
        {
            const ushort ch = m_bigEndian
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

bool StructureRenderEngine::elementMatchesTerminator(StructureRow *row,
                                                     Type *elementType,
                                                     ExprNode *stopExpr,
                                                     uint64_t offset)
{
    if (!row || !elementType || !stopExpr)
        return false;

    // terminated_by(0) is the common C-string/table-sentinel form. Treat a
    // constant stop expression as an element-value comparison; richer
    // expressions are evaluated as booleans against the formatted element row.
    if (stopExpr->type == EXPR_NUMBER)
    {
        Type *base = BaseNode(elementType);
        if (!base || !isScalar(base->ty))
            return false;

        INUMTYPE elementValue = 0;
        INUMTYPE terminatorValue = 0;
        return readInteger(offset, row->byteLength, &elementValue, row->bigEndian)
            && evaluate(row, stopExpr, &terminatorValue, offset)
            && elementValue == terminatorValue;
    }

    INUMTYPE value = 0;
    return evaluate(row, stopExpr, &value, offset) && value != 0;
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
    if (!readInteger(field.offset, field.length, &value, field.bigEndian))
        return {};

    return QString::number(value);
}

QString StructureRenderEngine::dynamicContainerAlias(StructureRow *row)
{
    if (!row)
        return {};

    QString alias = cleanDynamicAlias(row->nameIdentifier);
    if (!alias.isEmpty())
        return alias;

    ExprNode *nameExpr = nullptr;
    if (FindTag(row->typeDecl ? row->typeDecl->tagList : nullptr, TOK_NAME, &nameExpr))
    {
        if (StructureRow *fieldRow = findFieldRow(row, nameExpr))
        {
            alias = cleanDynamicAlias(fieldRow->value);
            if (!alias.isEmpty())
                return alias;

            const QString stringValue = stringArrayValue(fieldRow,
                                                         fieldRow->type,
                                                         fieldRow->typeDecl,
                                                         fieldRow->absoluteOffset);
            alias = cleanDynamicAlias(stringValue);
            if (!alias.isEmpty())
                return alias;
        }

        alias = cleanDynamicAlias(fieldNameValue(row, row->type, nameExpr, row->absoluteOffset));
    }

    return alias;
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
