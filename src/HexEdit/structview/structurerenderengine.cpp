#include "structview/structurerenderengine.h"

#include "structview/structurebranchicons.h"
#include "structview/structurecommentformatter.h"
#include "structview/structuresemanticview.h"
#include "structview/structuretypenameformatter.h"

#include <QByteArray>
#include <QDate>
#include <QDateTime>
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QLatin1String>
#include <QStringList>
#include <QTime>
#include <QTimeZone>
#include <QTextStream>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace
{
static constexpr uint64_t kMaxArrayElements = 100;
static constexpr size_t kMaxRecursiveTypeDepth = 64;
static constexpr qsizetype kMaxArrayPreviewElements = 8;
static constexpr bool kPrefixArrayAliasesWithDash = false;
// Dynamic rows expose raw layout navigation, so leave their semantic-style
// branch icons disabled unless that presentation is explicitly wanted.
static constexpr bool kShowDynamicViewBranchIcons = false;
static constexpr uint64_t kSearchChunkSize = 64 * 1024;
static constexpr uint64_t kMaxStringLookupBytes = 4096;

enum class ScalarEncoding
{
    None,
    Fixed,
    Uleb128,
    Sleb128
};

struct ScalarTypeInfo
{
    ScalarEncoding encoding = ScalarEncoding::None;
    uint64_t fixedSize = 0;
    bool signedValue = false;
    uint8_t maxBytes = 0;
};

enum class StrataFormat
{
    None,
    String,
    Utf16,
    Utf16Le,
    Utf16Be,
    FourCc,
    Guid,
    Uuid,
    Hex,
    Dec,
    Binary,
    Timestamp
};

enum class TimestampFormat
{
    None,
    Unix,
    FileTime,
    DosDate,
    DosTime
};

struct FormatTagInfo
{
    StrataFormat format = StrataFormat::None;
    TimestampFormat timestamp = TimestampFormat::None;
    ExprNode *widthExpr = nullptr;
};

bool normalizeOpenAsTransformName(QString transformName, QString *normalized)
{
    transformName = transformName.trimmed().toLower();
    if (transformName.isEmpty())
    {
        if (normalized)
            normalized->clear();
        return true;
    }

    if (transformName == QStringLiteral("store") || transformName == QStringLiteral("none"))
    {
        if (normalized)
            normalized->clear();
        return true;
    }

    if (transformName == QStringLiteral("gzip")
        || transformName == QStringLiteral("zlib")
        || transformName == QStringLiteral("deflate"))
    {
        if (normalized)
            *normalized = transformName;
        return true;
    }

    return false;
}

void appendCommaArgs(ExprNode *expr, std::vector<ExprNode *> *args);
TOKEN unwrapTagArg(ExprNode *arg, ExprNode **inner);

enum class TerminatorVisibility
{
    Auto,
    Hidden,
    Shown
};

ScalarTypeInfo scalarTypeInfo(TYPE type)
{
    switch (type)
    {
    case typeCHAR:
    case typeBYTE:
        return { ScalarEncoding::Fixed, 1, false, 0 };
    case typeWCHAR:
    case typeWORD:
        return { ScalarEncoding::Fixed, 2, false, 0 };
    case typeDWORD:
    case typeFLOAT:
    case typeENUM:
        return { ScalarEncoding::Fixed, 4, false, 0 };
    case typeQWORD:
    case typeDOUBLE:
    case typeTIMET:
    case typeFILETIME:
        return { ScalarEncoding::Fixed, 8, false, 0 };
    case typeDOSTIME:
    case typeDOSDATE:
        return { ScalarEncoding::Fixed, 2, false, 0 };
    case typeULEB128:
        return { ScalarEncoding::Uleb128, 0, false, 10 };
    case typeSLEB128:
        return { ScalarEncoding::Sleb128, 0, true, 10 };
    default:
        return {};
    }
}

QString tagString(Tag *tags, TOKEN token)
{
    ExprNode *expr = nullptr;
    if (!FindTag(tags, token, &expr) || !expr || expr->type != EXPR_STRINGBUF || !expr->str)
        return {};
    return QString::fromLocal8Bit(expr->str).toLower();
}

Tag *elementTagList(Tag *tags)
{
    Tag *elementTag = FindTag(tags, TOK_ELEMENT, nullptr);
    return elementTag ? elementTag->elementTags : nullptr;
}

bool declarationHasArray(TypeDecl *decl)
{
    if (!decl)
        return false;

    Type *type = decl->declList.empty() ? decl->baseType : decl->declList[0];
    for (Type *cursor = type; cursor; cursor = cursor->link)
        if (cursor->ty == typeARRAY)
            return true;

    return false;
}

Tag *declarationPresentationTags(TypeDecl *decl)
{
    if (!decl)
        return nullptr;

    return declarationHasArray(decl) ? elementTagList(decl->tagList) : decl->tagList;
}

Tag *effectiveTag(StructureRow *row, TypeDecl *typeDecl, TOKEN token, ExprNode **expr = nullptr)
{
    if (row)
    {
        if (Tag *tag = FindTag(row->tagListOverride, token, expr))
            return tag;
    }
    return FindTag(typeDecl ? typeDecl->tagList : nullptr, token, expr);
}

template <typename Func>
void forEachEffectiveTag(StructureRow *row, TypeDecl *typeDecl, Func fn)
{
    for (Tag *tag = row ? row->tagListOverride : nullptr; tag; tag = tag->link)
        fn(tag);
    for (Tag *tag = typeDecl ? typeDecl->tagList : nullptr; tag; tag = tag->link)
        fn(tag);
}

std::vector<Tag *> effectiveTags(StructureRow *row, TypeDecl *typeDecl)
{
    std::vector<Tag *> result;
    forEachEffectiveTag(row, typeDecl, [&result](Tag *tag) {
        if (tag && tag->tok != TOK_ELEMENT)
            result.push_back(tag);
    });
    return result;
}

StrataFormat strataFormatFromName(const QString &value)
{
    if (value.isEmpty())
        return StrataFormat::None;
    if (value == QLatin1String("string") || value == QLatin1String("ascii") || value == QLatin1String("utf8"))
        return StrataFormat::String;
    if (value == QLatin1String("utf16"))
        return StrataFormat::Utf16;
    if (value == QLatin1String("utf16le"))
        return StrataFormat::Utf16Le;
    if (value == QLatin1String("utf16be"))
        return StrataFormat::Utf16Be;
    if (value == QLatin1String("fourcc"))
        return StrataFormat::FourCc;
    if (value == QLatin1String("guid"))
        return StrataFormat::Guid;
    if (value == QLatin1String("uuid"))
        return StrataFormat::Uuid;
    if (value == QLatin1String("hex"))
        return StrataFormat::Hex;
    if (value == QLatin1String("dec"))
        return StrataFormat::Dec;
    if (value == QLatin1String("bin") || value == QLatin1String("binary"))
        return StrataFormat::Binary;
    if (value == QLatin1String("timestamp")
        || value == QLatin1String("time_t")
        || value == QLatin1String("unix")
        || value == QLatin1String("filetime")
        || value == QLatin1String("dosdate")
        || value == QLatin1String("dostime"))
    {
        return StrataFormat::Timestamp;
    }
    return StrataFormat::None;
}

TimestampFormat timestampFormatFromName(const QString &value)
{
    if (value.isEmpty()
        || value == QLatin1String("timestamp")
        || value == QLatin1String("time_t")
        || value == QLatin1String("unix")
        || value == QLatin1String("unix_time"))
    {
        return TimestampFormat::Unix;
    }
    if (value == QLatin1String("filetime"))
        return TimestampFormat::FileTime;
    if (value == QLatin1String("dosdate") || value == QLatin1String("dos_date"))
        return TimestampFormat::DosDate;
    if (value == QLatin1String("dostime") || value == QLatin1String("dos_time"))
        return TimestampFormat::DosTime;
    return TimestampFormat::None;
}

FormatTagInfo formatTagInfoFromExpr(ExprNode *expr)
{
    FormatTagInfo info;

    if (!expr)
        return info;

    std::vector<ExprNode *> args;
    appendCommaArgs(expr, &args);
    if (args.empty())
        return info;

    ExprNode *formatExpr = nullptr;
    if (unwrapTagArg(args[0], &formatExpr) != TOK_NULL)
        return info;
    if (!formatExpr || formatExpr->type != EXPR_STRINGBUF || !formatExpr->str)
        return info;

    info.format = strataFormatFromName(QString::fromLocal8Bit(formatExpr->str).toLower());
    if (info.format == StrataFormat::Timestamp)
        info.timestamp = timestampFormatFromName(QString::fromLocal8Bit(formatExpr->str).toLower());

    for (size_t i = 1; i < args.size(); ++i)
    {
        ExprNode *inner = nullptr;
        switch (unwrapTagArg(args[i], &inner))
        {
        case TOK_WIDTH:
            info.widthExpr = inner;
            break;
        default:
            if (inner && inner->type == EXPR_STRINGBUF && inner->str && info.format == StrataFormat::Timestamp)
                info.timestamp = timestampFormatFromName(QString::fromLocal8Bit(inner->str).toLower());
            break;
        }
    }

    if (info.format == StrataFormat::Timestamp && info.timestamp == TimestampFormat::None)
        info.timestamp = TimestampFormat::Unix;

    return info;
}

FormatTagInfo formatTagInfo(Tag *tags)
{
    ExprNode *expr = nullptr;
    FindTag(tags, TOK_FORMAT, &expr);
    return formatTagInfoFromExpr(expr);
}

StrataFormat formatTag(TypeDecl *typeDecl)
{
    return formatTagInfo(typeDecl ? typeDecl->tagList : nullptr).format;
}

TerminatorVisibility terminatorVisibilityExpr(ExprNode *expr)
{
    if (!expr || expr->type != EXPR_STRINGBUF || !expr->str)
        return TerminatorVisibility::Auto;

    const QString value = QString::fromLocal8Bit(expr->str).toLower();
    if (value == QLatin1String("hidden") || value == QLatin1String("hide"))
        return TerminatorVisibility::Hidden;
    if (value == QLatin1String("shown") || value == QLatin1String("show") || value == QLatin1String("visible"))
        return TerminatorVisibility::Shown;
    return TerminatorVisibility::Auto;
}

bool expressionIsLiteralZero(ExprNode *expr)
{
    return expr && expr->type == EXPR_NUMBER && expr->val == 0;
}

QString hexByte(uchar value)
{
    return QString::number(value, 16).toLower().rightJustified(2, QLatin1Char('0'));
}

QString formatUtcDateTime(const QDateTime &dateTime)
{
    if (!dateTime.isValid())
        return QStringLiteral("Invalid");

    return dateTime.toUTC().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss 'UTC'"));
}

QString formatUnixTime(uint64_t rawValue, uint64_t byteLength)
{
    return formatUtcDateTime(QDateTime::fromSecsSinceEpoch(signedStructureValue(rawValue, byteLength),
                                                           QTimeZone::UTC));
}

QString formatFileTime(uint64_t rawValue)
{
    const QDateTime epoch(QDate(1601, 1, 1), QTime(0, 0), QTimeZone::UTC);
    return formatUtcDateTime(epoch.addMSecs(qint64(rawValue / 10000)));
}

QString formatDosDate(uint64_t rawValue)
{
    const uint16_t raw = static_cast<uint16_t>(rawValue);
    const int year = 1980 + ((raw >> 9) & 0x7f);
    const int month = (raw >> 5) & 0x0f;
    const int day = raw & 0x1f;
    const QDate date(year, month, day);
    return date.isValid() ? date.toString(QStringLiteral("yyyy-MM-dd"))
                          : QStringLiteral("Invalid");
}

QString formatDosTime(uint64_t rawValue)
{
    const uint16_t raw = static_cast<uint16_t>(rawValue);
    const int hour = (raw >> 11) & 0x1f;
    const int minute = (raw >> 5) & 0x3f;
    const int second = (raw & 0x1f) * 2;
    const QTime time(hour, minute, second);
    return time.isValid() ? time.toString(QStringLiteral("HH:mm:ss"))
                          : QStringLiteral("Invalid");
}

QString formatTimestampValue(TimestampFormat format, uint64_t rawValue, uint64_t byteLength)
{
    switch (format)
    {
    case TimestampFormat::Unix:
        return formatUnixTime(rawValue, byteLength);
    case TimestampFormat::FileTime:
        return formatFileTime(rawValue);
    case TimestampFormat::DosDate:
        return formatDosDate(rawValue);
    case TimestampFormat::DosTime:
        return formatDosTime(rawValue);
    case TimestampFormat::None:
        break;
    }

    return {};
}

TimestampFormat timestampFormatForType(Type *type)
{
    if (FindType(type, typeTIMET))
        return TimestampFormat::Unix;
    if (FindType(type, typeFILETIME))
        return TimestampFormat::FileTime;
    if (FindType(type, typeDOSDATE))
        return TimestampFormat::DosDate;
    if (FindType(type, typeDOSTIME))
        return TimestampFormat::DosTime;
    return TimestampFormat::None;
}

QString formatUuidBytes(const QByteArray &bytes, bool guidByteOrder)
{
    if (bytes.size() < 16)
        return {};

    auto byteAt = [&bytes](int index) {
        return uchar(bytes[index]);
    };
    auto appendByte = [](QString *text, uchar value) {
        text->append(hexByte(value));
    };

    QString text;
    text.reserve(36);
    if (guidByteOrder)
    {
        appendByte(&text, byteAt(3)); appendByte(&text, byteAt(2));
        appendByte(&text, byteAt(1)); appendByte(&text, byteAt(0));
        text.append(QLatin1Char('-'));
        appendByte(&text, byteAt(5)); appendByte(&text, byteAt(4));
        text.append(QLatin1Char('-'));
        appendByte(&text, byteAt(7)); appendByte(&text, byteAt(6));
    }
    else
    {
        appendByte(&text, byteAt(0)); appendByte(&text, byteAt(1));
        appendByte(&text, byteAt(2)); appendByte(&text, byteAt(3));
        text.append(QLatin1Char('-'));
        appendByte(&text, byteAt(4)); appendByte(&text, byteAt(5));
        text.append(QLatin1Char('-'));
        appendByte(&text, byteAt(6)); appendByte(&text, byteAt(7));
    }
    text.append(QLatin1Char('-'));
    appendByte(&text, byteAt(8)); appendByte(&text, byteAt(9));
    text.append(QLatin1Char('-'));
    for (int i = 10; i < 16; ++i)
        appendByte(&text, byteAt(i));
    return text;
}

StructureRowTreeMode treeTag(Tag *tags)
{
    const QString value = tagString(tags, TOK_TREE);
    if (value == QLatin1String("hidden"))
        return StructureRowTreeMode::Hidden;
    if (value == QLatin1String("collapsed"))
        return StructureRowTreeMode::Collapsed;
    if (value == QLatin1String("expanded"))
        return StructureRowTreeMode::Expanded;
    if (value == QLatin1String("flatten"))
        return StructureRowTreeMode::Flatten;
    return StructureRowTreeMode::Default;
}

uint64_t scalarSize(TYPE type)
{
    const ScalarTypeInfo info = scalarTypeInfo(type);
    return info.encoding == ScalarEncoding::Fixed ? info.fixedSize : 0;
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
    return scalarTypeInfo(type).encoding != ScalarEncoding::None;
}

bool decodeLeb128(const StructureValueBuilder::ByteReader &reader,
                  uint64_t offset,
                  bool signedEncoding,
                  uint8_t maxBytes,
                  uint64_t *value,
                  uint64_t *byteLength)
{
    if (!reader || !value || !byteLength)
        return false;

    uint64_t result = 0;
    uint8_t shift = 0;
    uint8_t byte = 0;
    for (uint8_t i = 0; i < maxBytes; ++i)
    {
        if (reader(offset + i, &byte, 1) != 1)
            return false;

        result |= uint64_t(byte & 0x7f) << shift;
        *byteLength = uint64_t(i) + 1;
        shift += 7;

        if ((byte & 0x80) == 0)
        {
            if (signedEncoding && shift < 64 && (byte & 0x40))
                result |= (~uint64_t(0)) << shift;
            *value = result;
            return true;
        }
    }

    return false;
}

QString rowNameFragment(QString value)
{
    if (value.size() >= 2 && value.front() == QLatin1Char('"') && value.back() == QLatin1Char('"'))
        value = value.mid(1, value.size() - 2);
    return value;
}

bool tagListContains(Tag *tagList, TOKEN token)
{
    for (Tag *tag = tagList; tag; tag = tag->link)
        if (tag->tok == token)
            return true;
    return false;
}

bool typeContainsTag(Type *type, TOKEN token, std::vector<Structure *> *visited)
{
    if (!type || !visited)
        return false;

    if (type->parent && tagListContains(type->parent->tagList, token))
        return true;

    for (Type *cursor = type; cursor; cursor = cursor->link)
    {
        if (cursor->parent && tagListContains(cursor->parent->tagList, token))
            return true;

        Type *base = BaseNode(cursor);
        if (!base || (base->ty != typeSTRUCT && base->ty != typeUNION) || !base->sptr)
            continue;

        if (std::find(visited->begin(), visited->end(), base->sptr) != visited->end())
            continue;
        visited->push_back(base->sptr);

        if (tagListContains(base->sptr->tagList, token))
            return true;

        for (TypeDecl *decl : base->sptr->typeDeclList)
        {
            if (!decl)
                continue;
            if (tagListContains(decl->tagList, token))
                return true;
            if (typeContainsTag(decl->baseType, token, visited))
                return true;
            for (Type *declType : decl->declList)
                if (typeContainsTag(declType, token, visited))
                    return true;
        }
    }

    return false;
}

bool typeContainsTag(Type *type, TOKEN token)
{
    std::vector<Structure *> visited;
    return typeContainsTag(type, token, &visited);
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

bool semanticRowHasComplexChildren(const StructureRow *row)
{
    if (!row)
        return false;

    for (const auto &child : row->children)
    {
        if (!child || child->kind != StructureRowKind::Semantic)
            continue;
        if (child->branchIconPath == QString::fromLatin1(StructureBranchIcons::kBlueElementArray)
            || child->branchIconPath == QString::fromLatin1(StructureBranchIcons::kGrayElementArray))
        {
            continue;
        }
        if (!child->children.empty())
            return true;
        if (child->value == QStringLiteral("{...}"))
            return true;
    }

    return false;
}

void refreshSemanticBranchPresentation(StructureRow *row, const QString &rootLabel)
{
    if (!row)
        return;

    for (const auto &child : row->children)
        refreshSemanticBranchPresentation(child.get(), rootLabel);

    if (row->kind != StructureRowKind::Semantic)
        return;

    if (row->parent && row->parent->kind == StructureRowKind::Raw && row->name == rootLabel)
        return;

    if (row->branchIconPath == QString::fromLatin1(StructureBranchIcons::kBlueElementArray)
        || row->branchIconPath == QString::fromLatin1(StructureBranchIcons::kGrayElementArray))
    {
        return;
    }

    const bool hasComplexChildren = semanticRowHasComplexChildren(row);
    if (row->branchIconPath == QString::fromLatin1(StructureBranchIcons::kBlueEntityArray)
        || row->branchIconPath == QString::fromLatin1(StructureBranchIcons::kGrayEntityArray))
    {
        row->emphasizeName = hasComplexChildren;
        const QString icon = hasComplexChildren
            ? QString::fromLatin1(StructureBranchIcons::kBlueEntityArray)
            : QString::fromLatin1(StructureBranchIcons::kGrayEntityArray);
        row->setBranchIcons(icon, icon, icon);
        return;
    }

    if (row->branchIconPath == QString::fromLatin1(StructureBranchIcons::kBlueEntity)
        || row->branchIconPath == QString::fromLatin1(StructureBranchIcons::kBlueElement))
    {
        return;
    }

    if (!hasComplexChildren)
        return;

    row->emphasizeName = true;
    row->setBranchIcons(QString::fromLatin1(StructureBranchIcons::kBlueStructure),
                        QString::fromLatin1(StructureBranchIcons::kBlueStructureOpen),
                        QString::fromLatin1(StructureBranchIcons::kGrayStructure));
}

bool shouldPromoteSemanticRootSibling(const StructureRow *row)
{
    if (!row || row->kind != StructureRowKind::Semantic)
        return false;

    if (row->branchIconPath == QStringLiteral(":/icons/actions/circle-repeat.svg"))
        return false;

    if (row->branchIconPath == QString::fromLatin1(StructureBranchIcons::kBlueRoot))
        return true;

    return row->name.endsWith(QStringLiteral(" Summary"));
}

void applySemanticBranchIcons(StructureRow *row,
                              const QStringList &path,
                              bool isArray = false,
                              bool isScalarArray = false,
                              bool isScalarArrayElement = false,
                              bool isContainer = false,
                              bool isSemanticElement = false)
{
    if (!row)
        return;

    if (isScalarArrayElement)
    {
        row->branchIconPath.clear();
        row->branchOpenIconPath.clear();
        row->branchEmptyIconPath.clear();
        return;
    }

    if (path.isEmpty())
    {
        row->setBranchIcons(QString::fromLatin1(StructureBranchIcons::kBlueRoot),
                            QString::fromLatin1(StructureBranchIcons::kBlueRoot),
                            QString::fromLatin1(StructureBranchIcons::kGrayRoot));
        return;
    }

    if (isContainer)
    {
        row->setBranchIcons(QString::fromLatin1(StructureBranchIcons::kGrayStructure),
                            QString::fromLatin1(StructureBranchIcons::kGrayStructure),
                            QString::fromLatin1(StructureBranchIcons::kGrayStructure));
        return;
    }

    if (isSemanticElement)
    {
        row->setBranchIcons(QString::fromLatin1(StructureBranchIcons::kBlueElement),
                            QString::fromLatin1(StructureBranchIcons::kBlueElement),
                            QString::fromLatin1(StructureBranchIcons::kBlueElement));
        return;
    }

    if (isArray)
    {
        if (isScalarArray)
        {
            row->setBranchIcons(QString::fromLatin1(StructureBranchIcons::kBlueElementArray),
                                QString::fromLatin1(StructureBranchIcons::kBlueElementArray),
                                QString::fromLatin1(StructureBranchIcons::kGrayElementArray));
        }
        else
        {
            row->setBranchIcons(QString::fromLatin1(StructureBranchIcons::kBlueEntity),
                                QString::fromLatin1(StructureBranchIcons::kBlueEntityOpen),
                                QString::fromLatin1(StructureBranchIcons::kGrayEntity));
        }
        return;
    }

    if (isScalarArray)
    {
        row->setBranchIcons(QString::fromLatin1(StructureBranchIcons::kBlueElementArray),
                            QString::fromLatin1(StructureBranchIcons::kBlueElementArray),
                            QString::fromLatin1(StructureBranchIcons::kGrayElementArray));
        return;
    }

    if (semanticRowHasComplexChildren(row))
    {
        row->setBranchIcons(QString::fromLatin1(StructureBranchIcons::kBlueStructure),
                            QString::fromLatin1(StructureBranchIcons::kBlueStructureOpen),
                            QString::fromLatin1(StructureBranchIcons::kGrayStructure));
        return;
    }

    row->setBranchIcons(QString::fromLatin1(StructureBranchIcons::kBlueEntity),
                        QString::fromLatin1(StructureBranchIcons::kBlueEntityOpen),
                        QString::fromLatin1(StructureBranchIcons::kGrayEntity));
}

bool isDimensionPlaceholder(ExprNode *expr)
{
    return expr && expr->type == EXPR_IDENTIFIER && expr->str && std::strcmp(expr->str, "_") == 0;
}

qsizetype arrayDimensionIndex(TypeDecl *typeDecl, Type *arrayType)
{
    if (!typeDecl || !arrayType)
        return 0;

    for (Type *declType : typeDecl->declList)
    {
        qsizetype dimension = 0;
        for (Type *cursor = declType; cursor; cursor = cursor->link)
        {
            if (cursor == arrayType)
                return dimension;

            if (cursor->ty == typeARRAY)
                ++dimension;
        }
    }

    return 0;
}

ExprNode *dimensionTagArg(Tag *tagList, TOKEN tagTok, qsizetype dimension)
{
    ExprNode *expr = nullptr;
    if (!FindTag(tagList, tagTok, &expr) || !expr)
        return nullptr;

    std::vector<ExprNode *> args;
    appendCommaArgs(expr, &args);
    if (dimension < 0 || static_cast<size_t>(dimension) >= args.size())
        return nullptr;

    ExprNode *arg = args[static_cast<size_t>(dimension)];
    return isDimensionPlaceholder(arg) ? nullptr : arg;
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

bool semanticCppOnly(const char *environmentVariable)
{
    return qEnvironmentVariable(environmentVariable).trimmed().toLower() == QLatin1String("cpp");
}

bool rootUsesSemanticSchema(TypeDecl *rootType, const char *schemaName)
{
    ExprNode *semanticExpr = nullptr;
    return schemaName
        && FindTag(rootType ? rootType->tagList : nullptr, TOK_SEMANTIC, &semanticExpr)
        && semanticExpr
        && semanticExpr->type == EXPR_IDENTIFIER
        && semanticExpr->str
        && std::strcmp(semanticExpr->str, schemaName) == 0;
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

// If 'arg' was written as wrapperKeyword(value) inside a tag's parameter list
// (e.g. name(DllName) inside dynamic_array(name(DllName), type(CHAR), ...)),
// return the wrapping token and set *inner to the wrapped value. Otherwise
// return TOK_NULL and set *inner to 'arg' unchanged, so callers can use *inner
// exactly as before regardless of whether the argument was wrapped.
TOKEN unwrapTagArg(ExprNode *arg, ExprNode **inner)
{
    if (arg && arg->type == EXPR_TAGWRAP)
    {
        if (inner)
            *inner = arg->left;
        return arg->tok;
    }
    if (inner)
        *inner = arg;
    return TOK_NULL;
}

// offset(...) is parsed with CommaExpression(), including for its single
// argument.  Consumers of a tagged offset need the contained expression, not
// that one-element comma-list wrapper.
ExprNode *unwrapSingleCommaArg(ExprNode *expr)
{
    if (expr && expr->type == EXPR_COMMA && !expr->right)
        return expr->left;
    return expr;
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

StructureRenderEngine::StructureRenderEngine(StrataLibrary *library,
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

bool StructureRenderEngine::canReadByte(uint64_t offset) const
{
    uint8_t byte = 0;
    return m_reader && m_reader(offset, &byte, 1) == 1;
}

bool StructureRenderEngine::checkedAdd(uint64_t a, uint64_t b, uint64_t *result) const
{
    if (!result || a > std::numeric_limits<uint64_t>::max() - b)
        return false;

    *result = a + b;
    return true;
}

std::vector<std::unique_ptr<StructureRow>> StructureRenderEngine::build(
    SemanticRootPlacement semanticRootPlacement)
{
    std::vector<RowPtr> rows = buildRaw();
    if (rows.empty())
        return rows;

    std::vector<RowPtr> semanticRoots = buildSemanticOverlay(rows.front().get());
    for (RowPtr &semanticRoot : semanticRoots)
    {
        if (semanticRootPlacement == SemanticRootPlacement::ChildOfRawRoot)
        {
            semanticRoot->parent = rows.front().get();
            rows.front()->children.push_back(std::move(semanticRoot));
        }
        else
        {
            rows.push_back(std::move(semanticRoot));
        }
    }
    return rows;
}

std::vector<std::unique_ptr<StructureRow>> StructureRenderEngine::buildRaw()
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
    m_rootRow = root.get();
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

    linkWasmFunctionCodeTargets(root.get());
    resolveEntryPointRows(root.get());
    applyDiagnosticTags(root.get(), m_rootType, root.get());
    if (m_options.sortTopLevelRowsByOffset)
    {
        std::stable_sort(root->children.begin(), root->children.end(), [](const RowPtr &left, const RowPtr &right) {
            return left && right && left->absoluteOffset < right->absoluteOffset;
        });
    }
    if (profile)
    {
        structureProfileLog(QStringLiteral("[StructureProfile] engine raw complete rows=%1 ms=%2 total=%3")
                                .arg(structureRowCount(root.get()))
                                .arg(phaseTimer.restart())
                                .arg(totalTimer.elapsed()));
    }

    rows.push_back(std::move(root));
    return rows;
}

std::vector<std::unique_ptr<StructureRow>> StructureRenderEngine::buildSemanticOverlay(StructureRow *rawRoot)
{
    std::vector<RowPtr> semanticRoots;
    if (!rawRoot || !m_rootType)
        return semanticRoots;

    const bool profile = structureProfileEnabled();
    QElapsedTimer totalTimer;
    QElapsedTimer phaseTimer;
    if (profile)
    {
        totalTimer.start();
        phaseTimer.start();
    }

    m_rootRow = rawRoot;
    const bool skipDeclarativeSemantic =
        (semanticCppOnly("Q22_PE_SEMANTIC_VIEW") && rootUsesSemanticSchema(m_rootType, "PE_VIEW"))
        || (semanticCppOnly("Q22_ELF_SEMANTIC_VIEW") && rootUsesSemanticSchema(m_rootType, "ELF_VIEW"));
    if (!skipDeclarativeSemantic)
    {
        collectSemanticEmitRequests(rawRoot);
        for (const RowPtr &row : m_truncatedSemanticRows)
            collectSemanticEmitRequests(row.get());
        appendSemanticRowRequests();
        appendSemanticNodeRequests();
        appendSemanticEmitRows(rawRoot);
        linkWasmSemanticFunctionCodeTargets(rawRoot);
    }
    if (profile)
    {
        structureProfileLog(QStringLiteral("[StructureProfile] declarative semantic rows=%1 row_requests=%2 byte_requests=%3 skipped=%4 ms=%5")
                                .arg(structureRowCount(rawRoot))
                                .arg(m_semanticRowRequests.size())
                                .arg(m_semanticEmitRequests.size() + m_semanticNodeRequests.size())
                                .arg(skipDeclarativeSemantic ? 1 : 0)
                                .arg(phaseTimer.restart()));
    }
    std::vector<StructureOffsetMap> semanticOffsetMaps;
    for (const DynamicContainer &container : m_dynamicContainers)
    {
        for (const OffsetMap &map : container.maps)
            semanticOffsetMaps.push_back(StructureOffsetMap{ map.logicalStart, map.logicalSize, map.fileOffset });
    }
    runStructureSemanticViews(m_library, rawRoot, m_baseOffset, m_reader, semanticOffsetMaps);
    refreshSemanticBranchPresentation(rawRoot, semanticRootLabel());
    resolveEntryPointRows(rawRoot);
    if (profile)
    {
        structureProfileLog(QStringLiteral("[StructureProfile] engine semantic rows=%1 maps=%2 ms=%3 total=%4")
                                .arg(structureRowCount(rawRoot))
                                .arg(semanticOffsetMaps.size())
                                .arg(phaseTimer.restart())
                                .arg(totalTimer.elapsed()));
    }

    for (auto it = rawRoot->children.begin(); it != rawRoot->children.end();)
    {
        if (shouldPromoteSemanticRootSibling(it->get()))
        {
            (*it)->parent = nullptr;
            semanticRoots.push_back(std::move(*it));
            it = rawRoot->children.erase(it);
            continue;
        }

        ++it;
    }

    m_rootRow = nullptr;
    return semanticRoots;
}

bool StructureRenderEngine::hasSemanticOverlay() const
{
    return attachedSemanticSchema(m_rootType) != nullptr;
}

QString StructureRenderEngine::semanticRootLabelForDisplay() const
{
    return semanticRootLabel();
}

StructureRenderEngine::RowPtr StructureRenderEngine::makeRow(StructureRow *parent,
                                                             Type *type,
                                                             TypeDecl *typeDecl,
                                                             uint64_t offset,
                                                             Tag *tagListOverride) const
{
    auto row = std::make_unique<StructureRow>(parent);
    row->type = type;
    row->typeDecl = typeDecl;
    row->tagListOverride = tagListOverride;
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
    applyTreeTag(row.get(), typeDecl);
    return row;
}

uint64_t StructureRenderEngine::appendTypeDecl(StructureRow *parent, TypeDecl *typeDecl, uint64_t offset)
{
    if (!typeDecl || typeDecl->typeAlias)
        return 0;

    const uint64_t originalOffset = offset;
    collectNamedOffsetMaps(parent);
    if (declarationIsOptionalAndAbsent(typeDecl, parent, parent ? parent->type : nullptr, offset))
        return 0;

    ExprNode *offsetExpr = nullptr;
    if (FindTag(typeDecl->tagList, TOK_OFFSET, &offsetExpr))
    {
        INUMTYPE evaluated = offset;
        QString offsetSpace;
        ExprNode *positionExpr = nullptr;
        if (!offsetTagArgs(offsetExpr, &offsetSpace, &positionExpr)
            || !positionExpr
            || !evaluate(parent, positionExpr, &evaluated, offset))
        {
            return 0;
        }

        if (evaluated < 0)
        {
            return 0;
        }

        if (offsetSpace.isEmpty())
        {
            if (!checkedAdd(m_baseOffset, static_cast<uint64_t>(evaluated), &offset))
                return 0;
        }
        else if (!mapNamedOffset(offsetSpace, static_cast<uint64_t>(evaluated), &offset))
        {
            return 0;
        }
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
        if (offset >= originalOffset)
        {
            uint64_t adjusted = 0;
            return checkedAdd(offset - originalOffset, length, &adjusted) ? adjusted : 0;
        }
        return length;
    }

    AlignmentScope alignment(this, typeDecl->compoundType ? childAlignment : m_structAlignment);
    for (Type *type : typeDecl->declList)
    {
        uint64_t childOffset = 0;
        if (!checkedAdd(offset, length, &childOffset))
            return 0;

        uint64_t childLength = recurseType(parent, type, typeDecl, childOffset);
        if (!checkedAdd(length, childLength, &length))
            return 0;
    }

    length = declarationExtent(typeDecl, parent, parent ? parent->type : nullptr, offset, length);
    if (offset >= originalOffset)
    {
        uint64_t adjusted = 0;
        return checkedAdd(offset - originalOffset, length, &adjusted) ? adjusted : 0;
    }
    return length;
}

uint64_t StructureRenderEngine::appendIdentifierRow(StructureRow *parent,
                                                    Type *type,
                                                    TypeDecl *typeDecl,
                                                    uint64_t offset)
{
    TypeDecl *referencedDecl = referencedTypeDecl(type);
    Tag *referencedTags = referencedDecl && tagListContains(referencedDecl->tagList, TOK_DYNAMICSTRUCT)
        ? referencedDecl->tagList
        : nullptr;
    auto row = makeRow(parent, type, typeDecl, offset, referencedTags);
    applyDeclarationName(row.get(), type);
    if (row->name.isEmpty() && type && type->sym)
        row->name = QString::fromLocal8Bit(type->sym->name);

    const uint64_t length = recurseType(row.get(), type ? type->link : nullptr, typeDecl, offset);
    row->byteLength = length;
    applyEntryPointTag(row.get(), typeDecl);
    applyCodeTag(row.get(), typeDecl, row.get());
    applyOpenAsTag(row.get(), typeDecl, row.get());
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
    applyDiagnosticTags(row.get(), typeDecl, row.get());
    if (!row->value.isEmpty() || !row->children.empty())
    {
        if (!parent)
            return length;
        appendPresentedRow(parent, std::move(row));
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
        if (count <= 0)
            return 0;

        const qsizetype dimension = arrayDimensionIndex(typeDecl, type);
        Type *fixedScalarElement = type->link && type->link->ty == typeARRAY ? nullptr : BaseNode(type->link);
        const uint64_t fixedScalarElementSize = fixedScalarElement ? scalarSize(fixedScalarElement->ty) : 0;
        uint64_t extentLimit = 0;
        if (dimension == 0)
        {
            ExprNode *extentExpr = nullptr;
            if (FindTag(typeDecl ? typeDecl->tagList : nullptr, TOK_EXTENT, &extentExpr) && extentExpr)
            {
                INUMTYPE evaluatedExtent = 0;
                StructureRow *evalScope = parent && parent->parent ? parent->parent : parent;
                if (evaluate(evalScope, extentExpr, &evaluatedExtent, offset) && evaluatedExtent > 0)
                    extentLimit = static_cast<uint64_t>(evaluatedExtent);
            }
        }

        uint64_t length = 0;
        uint64_t logicalIndex = 0;
        uint64_t renderedElements = 0;
        ExprNode *terminatorExpr = dimensionTagArg(typeDecl ? typeDecl->tagList : nullptr,
                                                   TOK_TERMINATEDBY,
                                                   dimension);
        ExprNode *terminatorModeExpr = nullptr;
        FindTag(typeDecl ? typeDecl->tagList : nullptr, TOK_TERMINATOR, &terminatorModeExpr);
        Tag *explicitElementTags = elementTagList(typeDecl ? typeDecl->tagList : nullptr);
        TypeDecl *elementTypeDecl = nullptr;
        for (Type *cursor = type->link; cursor; cursor = cursor->link)
        {
            if ((cursor->ty == typeTYPEDEF || cursor->ty == typeIDENTIFIER) && cursor->sym)
            {
                elementTypeDecl = findTypeDecl(cursor->sym->name);
                break;
            }
        }
        TypeDecl *elementRowDecl = elementTypeDecl ? elementTypeDecl : typeDecl;

        const bool continuePastDisplayCap =
            count <= INUMTYPE(kMaxArrayElements + 16)
            || typeContainsTag(type->link, TOK_COUNTAS)
            || typeContainsTag(type->link, TOK_SELECT)
            || typeContainsTag(type->link, TOK_COUNT)
            || typeContainsTag(type->link, TOK_MAXCOUNT)
            // Semantic rows and code targets may be sourced from entries that
            // are deliberately omitted from the raw-tree preview. Keep
            // walking those declarations so their semantic tags still
            // populate the summary and its disassembler target.
            || typeContainsTag(type->link, TOK_EMIT)
            || typeContainsTag(type->link, TOK_EMITNODE)
            || typeContainsTag(type->link, TOK_EMITROW)
            || tagListContains(elementRowDecl ? elementRowDecl->tagList : nullptr, TOK_EMIT)
            || tagListContains(elementRowDecl ? elementRowDecl->tagList : nullptr, TOK_EMITNODE)
            || tagListContains(elementRowDecl ? elementRowDecl->tagList : nullptr, TOK_EMITROW)
            || tagListContains(explicitElementTags, TOK_EMIT)
            || tagListContains(explicitElementTags, TOK_EMITNODE)
            || tagListContains(explicitElementTags, TOK_EMITROW);

        Enum *nameEnum = nullptr;
        ExprNode *nameExpr = nullptr;

        while (logicalIndex < static_cast<uint64_t>(count)
               && (renderedElements < kMaxArrayElements || continuePastDisplayCap))
        {
            if (extentLimit > 0 && length >= extentLimit)
                break;
            uint64_t elementOffset = 0;
            if (!checkedAdd(offset, length, &elementOffset)
                || !canReadByte(elementOffset))
                break;

            const bool renderElement = renderedElements < kMaxArrayElements;
            auto row = makeRow(parent, type->link, elementRowDecl, elementOffset, explicitElementTags);
            const QString indexLabel = QStringLiteral("[%1]").arg(logicalIndex);
            row->nameTypePrefix = indexLabel;
            if (renderElement)
            {
                row->name = indexLabel;
            }
            row->suppressSemanticViews = true;
            nameExpr = nullptr;
            nameEnum = nullptr;
            if (effectiveTag(row.get(), row->typeDecl, TOK_NAME, &nameExpr) && m_library && nameExpr && nameExpr->str)
            {
                if (Symbol *sym = LookupSymbol(m_library->globalTagSymbolList, nameExpr->str))
                    if (sym->type && sym->type->ty == typeENUM)
                        nameEnum = sym->type->eptr;
            }
            if (renderElement)
            {
                const QString enumLabel = enumNameForValue(nameEnum, static_cast<INUMTYPE>(logicalIndex));
                if (!enumLabel.isEmpty())
                {
                    row->nameIdentifier = arrayAliasPrefix() + enumLabel;
                    row->name += row->nameIdentifier;
                }
            }

            const uint64_t elementLength = formatType(row.get(), type->link, row->typeDecl, elementOffset);
            row->byteLength = elementLength;
            // Array elements are formatted directly from their base type, so
            // appendIdentifierRow() does not get a chance to apply tags
            // attached to the element declaration itself.
            applyCodeTag(row.get(), row->typeDecl, row.get());
            applyOpenAsTag(row.get(), row->typeDecl, row.get());
            applyDiagnosticTags(row.get(), row->typeDecl, row.get());
            collectNamedOffsetMaps(row.get());
            if (renderElement && row->value.isEmpty())
            {
                const QString stringValue = stringArrayValue(row.get(), type->link, typeDecl, elementOffset);
                if (!stringValue.isNull())
                    row->value = stringValue;
            }
            const uint64_t terminatorLength = terminatorMatchLength(row.get(),
                                                                     type->link,
                                                                     terminatorExpr,
                                                                     elementOffset,
                                                                     elementLength);
            const bool hideTerminator = terminatorLength > 0
                && terminatorShouldBeHidden(typeDecl, type->link, terminatorExpr, terminatorModeExpr);
            if (renderElement && !nameEnum && nameExpr)
            {
                const QString fieldName = rowNameFragment(fieldNameValue(row.get(), type->link, nameExpr, elementOffset));
                if (!fieldName.isEmpty())
                {
                    row->nameIdentifier = arrayAliasPrefix() + fieldName;
                    row->name += row->nameIdentifier;
                }
            }

            const uint64_t consumedLength = terminatorLength > 0 ? terminatorLength : elementLength;
            // Empty recursive/container elements cannot advance toward the
            // extent boundary. Stop rather than manufacturing duplicate rows.
            if (consumedLength == 0)
                break;
            // extent(...) is the enclosing field boundary, not merely a hint
            // about where the following field should be placed.
            if (extentLimit > 0 && consumedLength > extentLimit - length)
                break;

            if (!checkedAdd(length, consumedLength, &length))
                break;
            logicalIndex += std::max<uint64_t>(row->arrayCountContribution, 1);
            ++renderedElements;
            if (terminatorLength > 0)
            {
                if (renderElement && !hideTerminator)
                    appendPresentedRow(parent, std::move(row));
                break;
            }

            if (renderElement)
                appendPresentedRow(parent, std::move(row));
            else if (row->typeDecl
                     && (effectiveTag(row.get(), row->typeDecl, TOK_EMIT, nullptr)
                         || effectiveTag(row.get(), row->typeDecl, TOK_EMITNODE, nullptr)
                         || effectiveTag(row.get(), row->typeDecl, TOK_EMITROW, nullptr)))
            {
                // Raw arrays are capped for responsiveness, but their
                // omitted tagged entries still contribute to semantic views.
                m_truncatedSemanticRows.push_back(std::move(row));
            }
        }
        if (!terminatorExpr && fixedScalarElementSize > 0)
        {
            const uint64_t totalBytes = static_cast<uint64_t>(count) <= std::numeric_limits<uint64_t>::max() / fixedScalarElementSize
                ? static_cast<uint64_t>(count) * fixedScalarElementSize
                : std::numeric_limits<uint64_t>::max();
            length = std::max(length, extentLimit > 0 ? std::min(totalBytes, extentLimit) : totalBytes);
        }
        return length;
    }

    case typeSTRUCT:
    {
        if (!type->sptr)
            return 0;

        const bool recursiveReentry = std::find(m_activeStructures.begin(),
                                                m_activeStructures.end(),
                                                type->sptr) != m_activeStructures.end();
        // A valid recursive container normally only reaches a handful of
        // levels. Cap hostile definitions/data before they exhaust the stack.
        if (recursiveReentry && m_activeStructures.size() >= kMaxRecursiveTypeDepth)
            return 0;
        m_activeStructures.push_back(type->sptr);

        collectNamedOffsetMaps(parent);
        uint64_t length = 0;
        for (TypeDecl *childDecl : type->sptr->typeDeclList)
        {
            uint64_t childOffset = 0;
            if (!checkedAdd(offset, length, &childOffset))
                break;
            uint64_t childLength = appendTypeDecl(parent, childDecl, childOffset);
            if (!checkedAdd(length, childLength, &length))
                break;
        }
        m_activeStructures.pop_back();
        return length;
    }

    case typeUNION:
    {
        if (!type->sptr)
            return 0;

        INUMTYPE switchValue = 0;
        QString switchString;
        ExprNode *switchExpr = nullptr;
        const bool hasSwitchTag = FindTag(typeDecl ? typeDecl->tagList : nullptr, TOK_SELECT, &switchExpr);
        const bool hasSwitch = hasSwitchTag && evaluate(EvalContext{ parent, type, offset }, switchExpr, &switchValue);
        const bool hasStringSwitch = hasSwitchTag
            && !hasSwitch
            && evaluateString(EvalContext{ parent, type, offset }, switchExpr, &switchString);
        if (hasSwitchTag && !hasSwitch && !hasStringSwitch)
            return 0;

        const bool recursiveReentry = std::find(m_activeStructures.begin(),
                                                m_activeStructures.end(),
                                                type->sptr) != m_activeStructures.end();
        if (recursiveReentry && m_activeStructures.size() >= kMaxRecursiveTypeDepth)
            return 0;
        m_activeStructures.push_back(type->sptr);

        uint64_t length = 0;
        bool matchedCase = false;
        std::vector<TypeDecl *> defaultDecls;
        for (TypeDecl *childDecl : type->sptr->typeDeclList)
        {
            if (FindTag(childDecl ? childDecl->tagList : nullptr, TOK_DEFAULT, nullptr))
            {
                defaultDecls.push_back(childDecl);
                continue;
            }

            ExprNode *caseExpr = nullptr;
            if ((hasSwitch || hasStringSwitch) && FindTag(childDecl ? childDecl->tagList : nullptr, TOK_CASE, &caseExpr))
            {
                bool caseMatches = false;
                if (hasSwitch)
                {
                    INUMTYPE caseValue = 0;
                    caseMatches = evaluate(parent, caseExpr, &caseValue, offset) && caseValue == switchValue;
                }
                else
                {
                    QString caseString;
                    caseMatches = evaluateString(EvalContext{ parent, type, offset }, caseExpr, &caseString)
                        && caseString == switchString;
                }

                if (!caseMatches)
                    continue;

                matchedCase = true;
            }
            length = std::max(length, appendTypeDecl(parent, childDecl, offset));
            if (parent)
                parent->arrayCountContribution = std::max(parent->arrayCountContribution,
                                                          evaluatedCountAs(parent, type, childDecl, offset));
        }

        if ((hasSwitch || hasStringSwitch) && !matchedCase)
        {
            for (TypeDecl *childDecl : defaultDecls)
            {
                length = std::max(length, appendTypeDecl(parent, childDecl, offset));
                if (parent)
                    parent->arrayCountContribution = std::max(parent->arrayCountContribution,
                                                              evaluatedCountAs(parent, type, childDecl, offset));
            }
        }
        else if (!hasSwitch && !hasStringSwitch)
        {
            for (TypeDecl *childDecl : defaultDecls)
            {
                length = std::max(length, appendTypeDecl(parent, childDecl, offset));
                if (parent)
                    parent->arrayCountContribution = std::max(parent->arrayCountContribution,
                                                              evaluatedCountAs(parent, type, childDecl, offset));
            }
        }

        m_activeStructures.pop_back();
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

    const ScalarTypeInfo info = scalarTypeInfo(base->ty);
    if (info.encoding == ScalarEncoding::None)
        return 0;

    uint64_t raw = 0;
    uint64_t length = 0;
    if (!decodeScalarValue(type, offset, &raw, &length))
    {
        row->value.clear();
        return info.fixedSize;
    }

    if (info.encoding != ScalarEncoding::Fixed)
    {
        row->valueKind = StructureRowValueKind::ScalarInteger;
        row->scalarRawValue = raw;
        row->scalarByteLength = info.signedValue ? 8 : length;
        row->scalarSigned = info.signedValue;
        row->scalarCharacterSuffix.clear();
        row->value = formatStructureIntegerValue(raw, row->scalarByteLength, row->scalarSigned, QString(), m_options);
        applyBitfieldTag(row, type, typeDecl, raw, row->scalarByteLength);
        return length;
    }

    uint8_t data[8] = {};
    const size_t got = m_reader ? m_reader(offset, data, static_cast<size_t>(length)) : 0;
    if (got < length)
    {
        row->value.clear();
        return length;
    }

    applyBitflagTag(row, type, typeDecl, raw, length);
    if (!row->children.empty())
        return length;

    row->valueKind = StructureRowValueKind::ScalarInteger;
    row->scalarRawValue = raw;
    row->scalarByteLength = length;
    row->scalarSigned = FindType(type, typeSIGNED);
    row->scalarCharacterSuffix.clear();
    row->value = formatStructureIntegerValue(raw, length, row->scalarSigned, QString(), m_options);
    if (applyFormatTag(row, typeDecl, length))
    {
        applyBitfieldTag(row, type, typeDecl, raw, length);
        return length;
    }

    const TimestampFormat builtinTimestamp = timestampFormatForType(type);
    if (builtinTimestamp != TimestampFormat::None)
    {
        row->valueKind = StructureRowValueKind::Custom;
        row->value = formatTimestampValue(builtinTimestamp, raw, length);
        return length;
    }

    Enum *displayEnum = tagEnum(row, typeDecl);
    if (!displayEnum && base->ty == typeENUM)
        displayEnum = base->eptr;

    if (displayEnum)
    {
        const QString enumName = enumNameForValue(displayEnum, raw);
        row->value = enumName.isEmpty() ? QString::number(raw, 16).toUpper().rightJustified(int(length * 2), QLatin1Char('0'))
                                        : enumName;
        row->valueChoices = enumChoiceLabels(displayEnum);
        applyBitfieldTag(row, type, typeDecl, raw, length);
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

    applyBitfieldTag(row, type, typeDecl, raw, length);
    return length;
}

uint64_t StructureRenderEngine::sizeOf(Type *type, uint64_t offset)
{
    return sizeOf(type, offset, TypeSizeMode::RenderCapped);
}

uint64_t StructureRenderEngine::sizeOf(Type *type, uint64_t offset, TypeSizeMode mode)
{
    if (!type)
        return 0;

    switch (type->ty)
    {
    case typeARRAY:
    {
        INUMTYPE count = 0;
        if (!evaluate(type, type->elements, &count, offset))
        {
            ExprNode *countExpr = dimensionTagArg(type->parent ? type->parent->tagList : nullptr,
                                                  TOK_COUNT,
                                                  arrayDimensionIndex(type->parent, type));
            if (!countExpr)
            {
                countExpr = dimensionTagArg(type->parent ? type->parent->tagList : nullptr,
                                            TOK_MAXCOUNT,
                                            arrayDimensionIndex(type->parent, type));
            }
            if (!countExpr || !evaluate(EvalContext{ nullptr, nullptr, offset }, countExpr, &count))
                return 0;
        }
        if (count < 0)
            return 0;

        const uint64_t elementSize = sizeOf(type->link, offset, mode);
        if (elementSize == 0)
            return 0;

        const uint64_t elementCount = mode == TypeSizeMode::RenderCapped
            ? std::min<uint64_t>(static_cast<uint64_t>(count), kMaxArrayElements)
            : static_cast<uint64_t>(count);
        if (elementCount != 0 && elementSize > std::numeric_limits<uint64_t>::max() / elementCount)
            return 0;
        return elementSize * elementCount;
    }
    case typeSTRUCT:
    {
        uint64_t size = 0;
        if (type->sptr)
        {
            for (TypeDecl *decl : type->sptr->typeDeclList)
            {
                ExprNode *optionalExpr = nullptr;
                if (mode == TypeSizeMode::StaticFixed
                    && FindTag(decl ? decl->tagList : nullptr, TOK_OPTIONAL, &optionalExpr)
                    && optionalExpr)
                {
                    INUMTYPE include = 0;
                    if (!evaluate(EvalContext{ nullptr, nullptr, offset + size }, optionalExpr, &include))
                        return 0;
                    if (!include)
                        continue;
                }

                for (Type *child : decl->declList)
                {
                    const uint64_t childSize = sizeOf(child, offset + size, mode);
                    if (childSize == 0 && mode == TypeSizeMode::StaticFixed)
                        return 0;
                    if (childSize > std::numeric_limits<uint64_t>::max() - size)
                        return 0;
                    size += childSize;
                }
            }
        }
        return size;
    }
    case typeUNION:
    {
        uint64_t size = 0;
        if (type->sptr)
            for (TypeDecl *decl : type->sptr->typeDeclList)
                for (Type *child : decl->declList)
                {
                    const uint64_t childSize = sizeOf(child, offset, mode);
                    if (childSize == 0 && mode == TypeSizeMode::StaticFixed)
                        return 0;
                    size = std::max(size, childSize);
                }
        return size;
    }
    default:
        if (const uint64_t scalar = scalarSize(type->ty))
            return scalar;
        if (mode == TypeSizeMode::StaticFixed && isScalar(type->ty))
            return 0;
        if (isScalar(type->ty))
        {
            uint64_t value = 0;
            uint64_t byteLength = 0;
            return decodeScalarValue(type, offset, &value, &byteLength) ? byteLength : 0;
        }
        return sizeOf(type->link, offset, mode);
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

    ExprNode *sizeExpr = dimensionTagArg(typeDecl ? typeDecl->tagList : nullptr,
                                         TOK_COUNT,
                                         arrayDimensionIndex(typeDecl, arrayType));
    if (!sizeExpr)
    {
        sizeExpr = dimensionTagArg(typeDecl ? typeDecl->tagList : nullptr,
                                   TOK_MAXCOUNT,
                                   arrayDimensionIndex(typeDecl, arrayType));
    }
    if (!sizeExpr)
        return false;

    return evaluate(evalScope, sizeExpr, result, offset);
}

uint64_t StructureRenderEngine::evaluatedCountAs(StructureRow *scope,
                                                 Type *scopeType,
                                                 TypeDecl *typeDecl,
                                                 uint64_t offset)
{
    ExprNode *expr = nullptr;
    if (!FindTag(typeDecl ? typeDecl->tagList : nullptr, TOK_COUNTAS, &expr) || !expr)
        return 1;

    INUMTYPE value = 0;
    if (!evaluate(EvalContext{ scope, scopeType, offset }, expr, &value) || value <= 0)
        return 1;

    return static_cast<uint64_t>(value);
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
    case EXPR_SCOPE:
    {
        EvalContext scoped;
        if (!resolveScopeContext(context, expr, &scoped))
            return false;
        if (expr->right && expr->right->type == EXPR_VALUEAT)
        {
            const uint64_t readBase = scoped.row ? scoped.row->absoluteOffset : scoped.offset;
            return evaluateValueAt(context, expr->right, result, &readBase);
        }
        return evaluate(scoped, expr->right, result);
    }
    case EXPR_IDENTIFIER:
    case EXPR_FIELD:
    case EXPR_ARRAY:
    {
        if (StructureRow *row = findFieldRow(context.row, expr))
        {
            if (row->valueKind == StructureRowValueKind::ScalarInteger)
            {
                *result = row->scalarRawValue;
                return true;
            }
            return readInteger(row->absoluteOffset, row->byteLength, result, row->bigEndian);
        }

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
        {
            uint64_t value = 0;
            uint64_t byteLength = 0;
            EndianScope endian(this, field.bigEndian);
            if (!decodeScalarValue(field.type, field.offset, &value, &byteLength))
                return false;
            *result = value;
            return true;
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
    case EXPR_SIZEOF:
    {
        if (!expr->left || expr->left->type != EXPR_IDENTIFIER || !expr->left->str)
            return false;

        const uint64_t size = sizeOfName(expr->left->str, context.offset, TypeSizeMode::RuntimeExact);
        if (size == 0)
            return false;

        *result = static_cast<INUMTYPE>(size);
        return true;
    }
    case EXPR_RAWOFFSET:
    {
        // select_offset(byteOffset): read 1 raw byte at
        // [enclosing struct/union's own base offset] + byteOffset, bypassing
        // field lookup entirely. Deliberately NOT context.offset -- that's
        // the running cursor for whichever specific field is currently being
        // positioned, which drifts as later sibling fields are processed
        // (e.g. by the time programHeaders32[]'s own offset(...) tag runs,
        // context.offset has already advanced past the union). What stays
        // constant regardless of which field's tag triggered this evaluate()
        // call is context.row itself: it's always the StructureRow of the
        // struct/union whose fields are being declared (_ELF, in ELF's case),
        // and StructureRow::absoluteOffset is that struct's own fixed base.
        //
        // This reaches a discriminator/marker byte that lives inside a
        // not-yet-selected union candidate (e.g. ELF's e_ident[EI_CLASS],
        // which is part of each per-bitness header struct, not a sibling
        // field -- see expr.h's EXPR_RAWOFFSET comment for the full story
        // and the Option 3 generalization this is a stand-in for).
        const uint64_t base = context.row ? context.row->absoluteOffset : context.offset;
        INUMTYPE relOffset = 0;
        if (!expr->left || !evaluate(context, expr->left, &relOffset))
            return false;

        if (relOffset < 0)
            return false;

        uint64_t absoluteOffset = 0;
        if (!checkedAdd(base, static_cast<uint64_t>(relOffset), &absoluteOffset))
            return false;

        return readInteger(absoluteOffset, 1, result, false);
    }
    case EXPR_VALUEAT:
        return evaluateValueAt(context, expr, result);
    case EXPR_FUNCTION:
        return evaluateFunction(context, expr, result);
    case EXPR_BYTESEQ:
        return false;
    default:
        return false;
    }
}

bool StructureRenderEngine::evaluateValueAt(const EvalContext &context, ExprNode *expr, INUMTYPE *result,
                                            const uint64_t *readBaseOverride)
{
    if (!expr || expr->type != EXPR_VALUEAT || !expr->right || !expr->cond || !expr->cond->str || !result)
        return false;

    INUMTYPE logicalOffset = 0;
    if (!evaluate(context, expr->right, &logicalOffset))
        return false;
    if (logicalOffset < 0)
        return false;

    uint64_t absoluteOffset = 0;
    if (expr->tok == TOK_ROOTVALUEAT)
    {
        if (expr->left)
            return false;
        if (!checkedAdd(m_baseOffset, static_cast<uint64_t>(logicalOffset), &absoluteOffset))
            return false;
    }
    else if (expr->left)
    {
        if (expr->left->type != EXPR_STRINGBUF || !expr->left->str)
            return false;
        if (!mapNamedOffset(QString::fromLocal8Bit(expr->left->str),
                            static_cast<uint64_t>(logicalOffset),
                            &absoluteOffset))
        {
            return false;
        }
    }
    else
    {
        const uint64_t base = readBaseOverride ? *readBaseOverride
            : context.row ? context.row->absoluteOffset
                          : context.offset;
        if (!checkedAdd(base, static_cast<uint64_t>(logicalOffset), &absoluteOffset))
            return false;
    }

    const TYPE scalarType = scalarTypeName(expr->cond->str);
    const uint64_t byteLength = scalarSize(scalarType);
    if (byteLength == 0)
        return false;

    return readInteger(absoluteOffset, byteLength, result, m_bigEndian);
}

namespace
{
bool packFourCcLiteral(const char *text, bool bigEndian, INUMTYPE *result)
{
    if (!text || !result)
        return false;

    const QByteArray bytes(text);
    if (bytes.size() != 4)
        return false;

    uint64_t value = 0;
    if (bigEndian)
    {
        for (int i = 0; i < 4; ++i)
            value = (value << 8) | uchar(bytes[i]);
    }
    else
    {
        for (int i = 0; i < 4; ++i)
            value |= uint64_t(uchar(bytes[i])) << (i * 8);
    }

    *result = static_cast<INUMTYPE>(value);
    return true;
}

void collectExpressionArgs(ExprNode *expr, std::vector<ExprNode *> *args)
{
    if (!expr || !args)
        return;

    if (expr->type == EXPR_COMMA)
    {
        collectExpressionArgs(expr->left, args);
        collectExpressionArgs(expr->right, args);
        return;
    }

    args->push_back(expr);
}

bool parseOctalText(const QString &text, INUMTYPE *result)
{
    if (!result)
        return false;

    uint64_t value = 0;
    bool sawDigit = false;
    const QString trimmed = text.trimmed();
    for (const QChar ch : trimmed)
    {
        if (ch == QLatin1Char('\0') || ch == QLatin1Char(' '))
            break;
        if (ch < QLatin1Char('0') || ch > QLatin1Char('7'))
            return false;

        sawDigit = true;
        value = (value << 3) | uint64_t(ch.unicode() - QLatin1Char('0').unicode());
        if (value > uint64_t(std::numeric_limits<INUMTYPE>::max()))
            return false;
    }

    if (!sawDigit)
        return false;

    *result = static_cast<INUMTYPE>(value);
    return true;
}
} // namespace

bool StructureRenderEngine::evaluateFunction(const EvalContext &context, ExprNode *expr, INUMTYPE *result)
{
    if (!expr || expr->type != EXPR_FUNCTION)
        return false;

    switch (expr->tok)
    {
    case TOK_BASEOF:
    {
        std::vector<ExprNode *> args;
        collectExpressionArgs(expr->left, &args);
        if (args.size() != 1 || !context.row)
            return false;

        StructureRow *row = resolveBaseOfScopeRow(context.row, args[0]);

        if (!row || row->absoluteOffset < m_baseOffset)
            return false;

        *result = static_cast<INUMTYPE>(row->absoluteOffset - m_baseOffset);
        return true;
    }
    case TOK_EXTENTOF:
    {
        std::vector<ExprNode *> args;
        collectExpressionArgs(expr->left, &args);
        if (args.size() != 1)
            return false;

        StructureRow *row = findFieldRow(context.row, args[0]);
        if (!row)
            return false;

        *result = static_cast<INUMTYPE>(row->byteLength);
        return true;
    }
    case TOK_FILESIZE:
    {
        std::vector<ExprNode *> args;
        collectExpressionArgs(expr->left, &args);
        if (!args.empty())
            return false;

        const uint64_t end = readableEnd(m_baseOffset);
        if (end < m_baseOffset)
            return false;

        *result = static_cast<INUMTYPE>(end - m_baseOffset);
        return true;
    }
    case TOK_INDEX:
    {
        std::vector<ExprNode *> args;
        collectExpressionArgs(expr->left, &args);
        if (!args.empty())
            return false;

        INUMTYPE index = 0;
        if (!arrayIndexFromRow(context.row, &index))
            return false;

        *result = index;
        return true;
    }
    case TOK_SELF:
    {
        std::vector<ExprNode *> args;
        collectExpressionArgs(expr->left, &args);
        if (!args.empty() || !context.row)
            return false;

        if (context.row->valueKind == StructureRowValueKind::ScalarInteger)
        {
            *result = context.row->scalarRawValue;
            return true;
        }

        return readInteger(context.row->absoluteOffset,
                           context.row->byteLength,
                           result,
                           context.row->bigEndian);
    }
    case TOK_CURRENTOFFSET:
    {
        std::vector<ExprNode *> args;
        collectExpressionArgs(expr->left, &args);
        if (!args.empty() || !context.row || context.row->absoluteOffset < m_baseOffset)
            return false;

        *result = static_cast<INUMTYPE>(context.row->absoluteOffset - m_baseOffset);
        return true;
    }
    case TOK_FIELDAT:
    {
        std::vector<ExprNode *> args;
        collectExpressionArgs(expr->left, &args);
        if (args.size() != 3 || !args[2] || args[2]->type != EXPR_IDENTIFIER || !args[2]->str)
            return false;

        StructureRow *arrayRow = findFieldRow(context.row, args[0]);
        INUMTYPE index = 0;
        if (!arrayRow || !evaluate(context, args[1], &index) || index < 0)
            return false;

        const size_t elementIndex = static_cast<size_t>(index);
        if (elementIndex >= arrayRow->children.size())
            return false;

        StructureRow *fieldRow = findFieldRow(arrayRow->children[elementIndex].get(), args[2]);
        if (!fieldRow)
            return false;

        if (fieldRow->valueKind == StructureRowValueKind::ScalarInteger)
        {
            *result = static_cast<INUMTYPE>(fieldRow->scalarRawValue);
            return true;
        }

        return readInteger(fieldRow->absoluteOffset, fieldRow->byteLength, result, fieldRow->bigEndian);
    }
    case TOK_INDEXOF:
    {
        std::vector<ExprNode *> args;
        collectExpressionArgs(expr->left, &args);
        if (args.size() != 3 || !args[1] || args[1]->type != EXPR_IDENTIFIER || !args[1]->str)
            return false;

        StructureRow *arrayRow = findFieldRow(context.row, args[0]);
        INUMTYPE keyValue = 0;
        if (!arrayRow || !evaluate(context, args[2], &keyValue))
            return false;

        for (size_t i = 0; i < arrayRow->children.size(); ++i)
        {
            StructureRow *fieldRow = findFieldRow(arrayRow->children[i].get(), args[1]);
            if (!fieldRow)
                continue;

            INUMTYPE fieldValue = 0;
            const bool read = fieldRow->valueKind == StructureRowValueKind::ScalarInteger
                ? (fieldValue = static_cast<INUMTYPE>(fieldRow->scalarRawValue), true)
                : readInteger(fieldRow->absoluteOffset, fieldRow->byteLength, &fieldValue, fieldRow->bigEndian);
            if (read && fieldValue == keyValue)
            {
                *result = static_cast<INUMTYPE>(i);
                return true;
            }
        }

        return false;
    }
    case TOK_FINDFIRST:
    case TOK_FINDLAST:
        return evaluateFindFunction(context, expr, result);
    case TOK_FOURCC:
    {
        std::vector<ExprNode *> args;
        collectExpressionArgs(expr->left, &args);
        if (args.size() != 1 || !args[0] || args[0]->type != EXPR_STRINGBUF)
            return false;

        return packFourCcLiteral(args[0]->str, m_bigEndian, result);
    }
    case TOK_OCTAL:
    {
        std::vector<ExprNode *> args;
        collectExpressionArgs(expr->left, &args);
        if (args.size() != 1)
            return false;

        QString text;
        if (!evaluateString(context, args[0], &text))
            return false;

        return parseOctalText(text, result);
    }
    default:
        return false;
    }
}

bool StructureRenderEngine::evaluateString(const EvalContext &context, ExprNode *expr, QString *result)
{
    if (!expr || !result)
        return false;

    switch (expr->type)
    {
    case EXPR_SCOPE:
    {
        EvalContext scoped;
        if (!resolveScopeContext(context, expr, &scoped))
            return false;
        return evaluateString(scoped, expr->right, result);
    }
    case EXPR_STRINGBUF:
        *result = QString::fromUtf8(expr->str ? expr->str : "");
        return true;
    case EXPR_FUNCTION:
        return evaluateStringFunction(context, expr, result);
    default:
        return false;
    }
}

bool StructureRenderEngine::evaluateStringFunction(const EvalContext &context, ExprNode *expr, QString *result)
{
    if (!expr || expr->type != EXPR_FUNCTION || !result)
        return false;

    switch (expr->tok)
    {
    case TOK_CONCAT:
    {
        std::vector<ExprNode *> args;
        collectExpressionArgs(expr->left, &args);
        if (args.empty())
            return false;

        QString text;
        for (ExprNode *arg : args)
        {
            const QString part = semanticExpressionText(context.row,
                                                        context.row ? context.row->type : context.type,
                                                        arg,
                                                        context.offset);
            if (part.isEmpty())
                return false;
            text += part;
        }

        *result = text;
        return true;
    }
    case TOK_FMT:
    {
        std::vector<ExprNode *> args;
        collectExpressionArgs(expr->left, &args);
        if (args.empty() || !args[0] || args[0]->type != EXPR_STRINGBUF)
            return false;

        QString text = QString::fromUtf8(args[0]->str ? args[0]->str : "");
        for (size_t i = 1; i < args.size(); ++i)
        {
            const QString part = semanticExpressionText(context.row,
                                                        context.row ? context.row->type : context.type,
                                                        args[i],
                                                        context.offset);
            text.replace(QStringLiteral("{%1}").arg(i - 1), part);
        }

        *result = text;
        return true;
    }
    case TOK_CSTR:
    {
        std::vector<ExprNode *> args;
        collectExpressionArgs(expr->left, &args);
        if (args.empty() || args.size() > 3)
            return false;

        QString space;
        ExprNode *offsetExpr = nullptr;
        ExprNode *maxExpr = nullptr;
        if (args[0] && args[0]->type == EXPR_STRINGBUF && args[0]->str)
        {
            if (args.size() < 2)
                return false;
            space = QString::fromLocal8Bit(args[0]->str);
            offsetExpr = args[1];
            maxExpr = args.size() == 3 ? args[2] : nullptr;
        }
        else
        {
            offsetExpr = args[0];
            maxExpr = args.size() == 2 ? args[1] : nullptr;
        }

        INUMTYPE logicalOffset = 0;
        if (!evaluate(context, offsetExpr, &logicalOffset) || logicalOffset < 0)
            return false;

        INUMTYPE maxLen = INUMTYPE(kMaxStringLookupBytes);
        if (maxExpr && (!evaluate(context, maxExpr, &maxLen) || maxLen <= 0))
            return false;

        uint64_t fileOffset = static_cast<uint64_t>(logicalOffset);
        if (!space.isEmpty() && !mapNamedOffset(space, fileOffset, &fileOffset))
            return false;

        const uint64_t cappedLength = std::min<uint64_t>(static_cast<uint64_t>(maxLen), kMaxStringLookupBytes);
        uint64_t absoluteOffset = 0;
        if (!checkedAdd(m_baseOffset, fileOffset, &absoluteOffset))
            return false;

        QByteArray bytes(static_cast<int>(cappedLength), Qt::Uninitialized);
        const size_t got = m_reader ? m_reader(absoluteOffset,
                                               reinterpret_cast<uint8_t *>(bytes.data()),
                                               static_cast<size_t>(bytes.size()))
                                    : 0;
        if (got == 0)
            return false;

        bytes.truncate(static_cast<int>(got));
        if (!bytes.contains('\0'))
            return false;

        *result = decodeNulTerminatedText(bytes, false);
        return true;
    }
    case TOK_CSTRAT:
    {
        std::vector<ExprNode *> args;
        collectExpressionArgs(expr->left, &args);
        if (args.size() != 2)
            return false;

        INUMTYPE logicalOffset = 0;
        INUMTYPE maxLen = 0;
        if (!evaluate(context, args[0], &logicalOffset)
            || !evaluate(context, args[1], &maxLen)
            || logicalOffset < 0
            || maxLen <= 0)
        {
            return false;
        }

        const uint64_t cappedLength = std::min<uint64_t>(static_cast<uint64_t>(maxLen), kMaxStringLookupBytes);
        uint64_t absoluteOffset = 0;
        if (!checkedAdd(m_baseOffset, static_cast<uint64_t>(logicalOffset), &absoluteOffset))
            return false;

        QByteArray bytes(static_cast<int>(cappedLength), Qt::Uninitialized);
        const size_t got = m_reader ? m_reader(absoluteOffset,
                                               reinterpret_cast<uint8_t *>(bytes.data()),
                                               static_cast<size_t>(bytes.size()))
                                    : 0;
        if (got == 0)
            return false;

        bytes.truncate(static_cast<int>(got));
        *result = decodeNulTerminatedText(bytes, false);
        return true;
    }
    case TOK_CSTRFROM:
    {
        std::vector<ExprNode *> args;
        collectExpressionArgs(expr->left, &args);
        if (args.size() < 2 || args.size() > 3)
            return false;

        INUMTYPE baseOffset = 0;
        INUMTYPE deltaOffset = 0;
        if (!evaluate(context, args[0], &baseOffset)
            || !evaluate(context, args[1], &deltaOffset)
            || baseOffset < 0
            || deltaOffset < 0)
        {
            return false;
        }
        INUMTYPE maxLen = INUMTYPE(kMaxStringLookupBytes);
        if (args.size() == 3 && (!evaluate(context, args[2], &maxLen) || maxLen <= 0))
            return false;

        uint64_t fileOffset = 0;
        if (!checkedAdd(static_cast<uint64_t>(baseOffset), static_cast<uint64_t>(deltaOffset), &fileOffset))
            return false;
        const uint64_t cappedLength = std::min<uint64_t>(static_cast<uint64_t>(maxLen), kMaxStringLookupBytes);
        uint64_t absoluteOffset = 0;
        if (!checkedAdd(m_baseOffset, fileOffset, &absoluteOffset))
            return false;

        QByteArray bytes(static_cast<int>(cappedLength), Qt::Uninitialized);
        const size_t got = m_reader ? m_reader(absoluteOffset,
                                               reinterpret_cast<uint8_t *>(bytes.data()),
                                               static_cast<size_t>(bytes.size()))
                                    : 0;
        if (got == 0)
            return false;

        bytes.truncate(static_cast<int>(got));
        if (!bytes.contains('\0'))
            return false;

        *result = decodeNulTerminatedText(bytes, false);
        return true;
    }
    case TOK_STR:
    {
        std::vector<ExprNode *> args;
        collectExpressionArgs(expr->left, &args);
        if (args.size() != 1)
            return false;

        StructureRow *row = findFieldRow(context.row, args[0]);
        if (!row)
            return false;

        const QString text = fieldStringValue(row);
        if (text.isNull())
            return false;

        *result = text;
        return true;
    }
    default:
        return false;
    }
}

bool StructureRenderEngine::resolveScopeContext(const EvalContext &context, ExprNode *expr, EvalContext *scoped) const
{
    if (!expr || expr->type != EXPR_SCOPE || !expr->left || !scoped)
        return false;

    StructureRow *row = resolveScopeRow(context.row, expr->left);
    if (!row)
        return false;

    scoped->row = row;
    scoped->type = row->type ? row->type : context.type;
    scoped->offset = row->absoluteOffset;
    return true;
}

StructureRow *StructureRenderEngine::resolveScopeRow(StructureRow *scope, ExprNode *expr) const
{
    if (!expr)
        return nullptr;

    if (expr->type != EXPR_IDENTIFIER || !expr->str)
        return nullptr;

    if (std::strcmp(expr->str, "root") == 0)
        return m_rootRow ? m_rootRow : scope;

    if (std::strcmp(expr->str, "parent") == 0)
        return scope ? scope->parent : nullptr;

    return nullptr;
}

StructureRow *StructureRenderEngine::resolveBaseOfScopeRow(StructureRow *scope, ExprNode *expr) const
{
    if (!expr)
        return nullptr;

    if (expr->type == EXPR_IDENTIFIER && expr->str)
    {
        if (std::strcmp(expr->str, "this") == 0)
            return scope;
        return resolveScopeRow(scope, expr);
    }

    if (expr->type == EXPR_SCOPE && expr->left && expr->right)
    {
        StructureRow *left = resolveBaseOfScopeRow(scope, expr->left);
        if (!left)
            return nullptr;
        return resolveBaseOfScopeRow(left, expr->right);
    }

    return nullptr;
}

bool StructureRenderEngine::evaluateFindFunction(const EvalContext &context, ExprNode *expr, INUMTYPE *result)
{
    if (!expr || !result)
        return false;

    std::vector<ExprNode *> args;
    collectExpressionArgs(expr->left, &args);
    if (args.empty() || args.size() > 2)
        return false;

    ExprNode *patternExpr = args[0];
    if (!patternExpr || patternExpr->type != EXPR_BYTESEQ || patternExpr->byteSequence.empty())
        return false;

    const uint64_t base = context.row ? context.row->absoluteOffset : context.offset;
    uint64_t end = readableEnd(base);
    if (context.row && context.row->byteLength > 0)
    {
        if (!checkedAdd(context.row->absoluteOffset, context.row->byteLength, &end))
            return false;
    }
    if (end < base)
        return false;

    const bool reverse = expr->tok == TOK_FINDLAST;
    uint64_t start = base;

    if (args.size() == 2)
    {
        INUMTYPE limitValue = 0;
        if (!evaluate(context, args[1], &limitValue))
            return false;

        const uint64_t limit = static_cast<uint64_t>(limitValue);
        const uint64_t available = end - base;
        if (limit < available)
        {
            if (reverse)
                start = end - limit;
            else if (!checkedAdd(base, limit, &end))
                return false;
        }
    }

    uint64_t absoluteMatch = 0;
    if (!findPattern(start, end, patternExpr->byteSequence, reverse, &absoluteMatch))
        return false;

    if (absoluteMatch < base)
        return false;

    *result = static_cast<INUMTYPE>(absoluteMatch - base);
    return true;
}

uint64_t StructureRenderEngine::readableEnd(uint64_t startOffset) const
{
    if (!m_reader)
        return startOffset;

    uint8_t byte = 0;
    auto canRead = [&](uint64_t offset) {
        return m_reader(offset, &byte, 1) == 1;
    };

    if (!canRead(startOffset))
        return startOffset;

    uint64_t step = 1;
    uint64_t lastGoodEnd = startOffset + 1;
    while (step < (std::numeric_limits<uint64_t>::max() / 2)
           && startOffset <= std::numeric_limits<uint64_t>::max() - (step * 2)
           && canRead(startOffset + (step * 2) - 1))
    {
        step *= 2;
        lastGoodEnd = startOffset + step;
    }

    uint64_t high = startOffset <= std::numeric_limits<uint64_t>::max() - (step * 2)
        ? startOffset + (step * 2) - 1
        : std::numeric_limits<uint64_t>::max();
    uint64_t low = lastGoodEnd;

    while (low < high)
    {
        const uint64_t mid = low + (high - low + 1) / 2;
        if (mid > 0 && canRead(mid - 1))
            low = mid;
        else
            high = mid - 1;
    }

    return low;
}

bool StructureRenderEngine::findPattern(uint64_t startOffset,
                                        uint64_t endOffset,
                                        const std::vector<uint8_t> &pattern,
                                        bool reverse,
                                        uint64_t *absoluteMatch) const
{
    if (!m_reader || !absoluteMatch || pattern.empty() || endOffset <= startOffset)
        return false;

    const uint64_t patternSize = static_cast<uint64_t>(pattern.size());
    if (endOffset - startOffset < patternSize)
        return false;

    const uint64_t overlap = patternSize > 0 ? patternSize - 1 : 0;
    QByteArray bytes;

    if (!reverse)
    {
        for (uint64_t pos = startOffset; pos < endOffset; )
        {
            const uint64_t coreEnd = std::min<uint64_t>(endOffset, pos + kSearchChunkSize);
            const uint64_t readEnd = std::min<uint64_t>(endOffset, coreEnd + overlap);
            const uint64_t readLength = readEnd - pos;
            bytes.resize(static_cast<qsizetype>(readLength));
            const size_t got = m_reader(pos, reinterpret_cast<uint8_t *>(bytes.data()), static_cast<size_t>(readLength));
            bytes.truncate(static_cast<qsizetype>(got));

            if (bytes.size() >= static_cast<qsizetype>(patternSize))
            {
                const uint64_t maxStart = std::min<uint64_t>(uint64_t(bytes.size()) - patternSize,
                                                             coreEnd - pos);
                for (uint64_t i = 0; i <= maxStart; ++i)
                {
                    if (std::memcmp(bytes.constData() + i, pattern.data(), pattern.size()) == 0)
                    {
                        *absoluteMatch = pos + i;
                        return true;
                    }
                }
            }

            if (coreEnd == pos)
                break;
            pos = coreEnd;
        }

        return false;
    }

    for (uint64_t pos = endOffset; pos > startOffset; )
    {
        const uint64_t coreStart = pos > kSearchChunkSize && pos - kSearchChunkSize > startOffset
            ? pos - kSearchChunkSize
            : startOffset;
        const uint64_t readStart = coreStart > startOffset + overlap
            ? coreStart - overlap
            : startOffset;
        const uint64_t readLength = pos - readStart;
        bytes.resize(static_cast<qsizetype>(readLength));
        const size_t got = m_reader(readStart, reinterpret_cast<uint8_t *>(bytes.data()), static_cast<size_t>(readLength));
        bytes.truncate(static_cast<qsizetype>(got));

        if (bytes.size() >= static_cast<qsizetype>(patternSize))
        {
            for (int64_t i = static_cast<int64_t>(bytes.size() - static_cast<qsizetype>(patternSize)); i >= 0; --i)
            {
                const uint64_t absolute = readStart + static_cast<uint64_t>(i);
                if (absolute + patternSize > pos)
                    continue;

                if (std::memcmp(bytes.constData() + i, pattern.data(), pattern.size()) == 0)
                {
                    *absoluteMatch = absolute;
                    return true;
                }
            }
        }

        if (coreStart == pos)
            break;
        pos = coreStart;
    }

    return false;
}

uint64_t StructureRenderEngine::sizeOfName(const char *name, uint64_t offset, TypeSizeMode mode)
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

    TYPE requiredTagType = typeNULL;
    const char *lookupName = name;
    if (std::strncmp(name, "struct ", 7) == 0)
    {
        requiredTagType = typeSTRUCT;
        lookupName = name + 7;
    }
    else if (std::strncmp(name, "union ", 6) == 0)
    {
        requiredTagType = typeUNION;
        lookupName = name + 6;
    }

    if (requiredTagType == typeNULL)
    {
        if (Symbol *sym = LookupSymbol(m_library->globalIdentifierList, const_cast<char *>(lookupName)))
            if (const uint64_t size = sizeOf(sym->type, offset, mode))
                return size;
    }

    for (Symbol *sym : m_library->globalTagSymbolList)
    {
        if (!sym || !sym->type || !sym->name[0])
            continue;
        Type *base = BaseNode(sym->type);
        if (!base || (base->ty != typeSTRUCT && base->ty != typeUNION))
            continue;
        if (requiredTagType != typeNULL && base->ty != requiredTagType)
            continue;
        if (std::strcmp(sym->name, lookupName) == 0)
        {
            if (const uint64_t size = sizeOf(base, offset, mode))
                return size;
        }
    }

    if (requiredTagType != typeNULL)
        return 0;

    for (TypeDecl *decl : m_library->globalTypeDeclList)
    {
        if (!decl)
            continue;

        for (Type *type : decl->declList)
        {
            if ((type->ty == typeTYPEDEF || type->ty == typeIDENTIFIER) && type->sym
                && std::strcmp(type->sym->name, lookupName) == 0)
            {
                return sizeOf(type, offset, mode);
            }
        }

        Type *base = BaseNode(decl->baseType);
        if (base && (base->ty == typeSTRUCT || base->ty == typeUNION) && base->sptr
            && base->sptr->symbol && std::strcmp(base->sptr->symbol->name, lookupName) == 0)
        {
            return sizeOf(base, offset, mode);
        }
    }

    return 0;
}

uint64_t StructureRenderEngine::staticSizeOfName(const char *name)
{
    return sizeOfName(name, m_baseOffset, TypeSizeMode::StaticFixed);
}

uint64_t StructureRenderEngine::staticSizeOfType(Type *type)
{
    return sizeOf(type, m_baseOffset, TypeSizeMode::StaticFixed);
}

StructureRow *StructureRenderEngine::findFieldRow(StructureRow *scope, ExprNode *expr)
{
    if (!scope || !expr)
        return nullptr;

    if (expr->type == EXPR_SCOPE)
    {
        StructureRow *scoped = resolveScopeRow(scope, expr->left);
        return scoped ? findFieldRow(scoped, expr->right) : nullptr;
    }

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

    const QString fieldName = QString::fromLocal8Bit(name);
    const QString suffix = QStringLiteral(" ") + fieldName;
    const QString arraySuffix = suffix + QStringLiteral("[]");

    for (StructureRow *cursor = scope; cursor; cursor = cursor->parent)
    {
        for (const auto &child : cursor->children)
        {
            Type *type = child->type;
            if (type && type->ty == typeIDENTIFIER && type->sym && std::strcmp(type->sym->name, name) == 0)
                return child.get();
            if (child->name == fieldName
                || child->name.endsWith(suffix)
                || child->name.endsWith(arraySuffix))
            {
                return child.get();
            }
            if (child->kind == StructureRowKind::Semantic && child->name == fieldName)
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

        uint64_t elementOffset = arrayField.offset;
        uint64_t elementLength = 0;
        for (INUMTYPE i = 0; i <= index; ++i)
        {
            elementLength = sizeOf(arrayType->link, elementOffset);
            if (elementLength == 0)
                return false;
            if (i < index)
                elementOffset += elementLength;
        }

        field->type = arrayType->link;
        field->typeDecl = arrayField.typeDecl;
        field->offset = elementOffset;
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
    // Named, [case(...)]-tagged union members that might declare `name`
    // themselves (e.g. ELF's e_ident is part of each per-bitness Elf32_Ehdr/
    // Elf64_Ehdr candidate, not a sibling field) -- collected rather than
    // tried eagerly, since a later candidate might also declare `name` and
    // disagree; resolved after the scan below once nothing else matches.
    struct UnionCandidate { Type *type; uint64_t offset; };
    std::vector<UnionCandidate> unionCandidates;

    for (TypeDecl *decl : base->sptr->typeDeclList)
    {
        if (!decl || decl->typeAlias)
            continue;

        uint64_t declOffset = base->ty == typeUNION ? scopeOffset : cursor;
        ExprNode *offsetExpr = nullptr;
        if (FindTag(decl->tagList, TOK_OFFSET, &offsetExpr))
        {
            INUMTYPE evaluated = declOffset;
            QString offsetSpace;
            ExprNode *positionExpr = nullptr;
            if (offsetTagArgs(offsetExpr, &offsetSpace, &positionExpr)
                && positionExpr
                && evaluate(base, positionExpr, &evaluated, scopeOffset))
            {
                if (evaluated < 0)
                {
                    continue;
                }

                if (offsetSpace.isEmpty())
                {
                    if (!checkedAdd(m_baseOffset, static_cast<uint64_t>(evaluated), &declOffset))
                        continue;
                }
                else
                {
                    mapNamedOffset(offsetSpace, static_cast<uint64_t>(evaluated), &declOffset);
                }
            }
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
        else if (base->ty == typeUNION && decl->baseType && FindTag(decl->tagList, TOK_CASE, nullptr))
        {
            unionCandidates.push_back(UnionCandidate{ decl->baseType, declOffset });
        }

        if (base->ty == typeSTRUCT)
            cursor = declOffset;
        else
            unionSize = std::max(unionSize, declLength);
    }

    Q_UNUSED(unionSize);

    // Try the collected candidates only once nothing in this scope matched
    // `name` directly. Require every candidate that does declare `name` to
    // agree on where and how big it is -- this only makes sense for fields
    // that are identical across every bitness/variant by construction (like
    // e_ident); divergence means the caller's expression is ambiguous before
    // a branch has actually been selected, so fail rather than guess.
    bool found = false;
    ResolvedField agreed;
    for (const UnionCandidate &candidate : unionCandidates)
    {
        ResolvedField nested;
        if (!resolveDirectField(candidate.type, name, candidate.offset, &nested))
            continue;

        if (!found)
        {
            agreed = nested;
            found = true;
        }
        else if (nested.offset != agreed.offset || nested.length != agreed.length)
        {
            return false;
        }
    }

    if (found)
    {
        *field = agreed;
        return true;
    }

    return false;
}

bool StructureRenderEngine::isKnownEnumConstant(const char *name) const
{
    if (!name || !m_library)
        return false;

    if (Symbol *sym = LookupSymbol(m_library->globalIdentifierList, name))
    {
        if (sym->type && sym->type->ty == typeENUMVALUE)
            return true;
    }

    for (Symbol *sym : m_library->globalTagSymbolList)
    {
        if (!sym || !sym->type || sym->type->ty != typeENUM || !sym->type->eptr)
            continue;

        for (EnumField *field : sym->type->eptr->fieldList)
            if (field && field->name && std::strcmp(field->name->name, name) == 0)
                return true;
    }

    return false;
}

namespace
{

// Field references that a select/endian/offset/count/optional/
// extent tag's expression might make: bare identifiers, possibly array-
// indexed, composed with the usual operators. Dotted field.access chains
// (EXPR_FIELD) are deliberately left alone -- they name their own scope
// explicitly, so there's nothing ambiguous to validate.
void collectFieldReferenceRoots(ExprNode *expr, std::vector<ExprNode *> *roots)
{
    if (!expr || !roots)
        return;

    switch (expr->type)
    {
    case EXPR_IDENTIFIER:
        roots->push_back(expr);
        return;
    case EXPR_ARRAY:
        collectFieldReferenceRoots(expr->left, roots);
        collectFieldReferenceRoots(expr->right, roots);
        return;
    case EXPR_SCOPE:
        collectFieldReferenceRoots(expr->right, roots);
        return;
    case EXPR_UNARY:
        collectFieldReferenceRoots(expr->left, roots);
        return;
    case EXPR_BINARY:
    case EXPR_COMMA:
        collectFieldReferenceRoots(expr->left, roots);
        collectFieldReferenceRoots(expr->right, roots);
        return;
    case EXPR_FUNCTION:
        if (expr->tok == TOK_FIELDAT || expr->tok == TOK_INDEXOF)
            return;
        collectFieldReferenceRoots(expr->left, roots);
        collectFieldReferenceRoots(expr->right, roots);
        return;
    case EXPR_VALUEAT:
        // left is an optional string address-space name; cond is the scalar
        // type name. Only the offset expression can contain field references.
        collectFieldReferenceRoots(expr->right, roots);
        return;
    case EXPR_TERTIARY:
        collectFieldReferenceRoots(expr->cond, roots);
        collectFieldReferenceRoots(expr->left, roots);
        collectFieldReferenceRoots(expr->right, roots);
        return;
    default:
        // EXPR_FIELD (see above), EXPR_NUMBER, EXPR_STRINGBUF, EXPR_SIZEOF,
        // EXPR_SCOPE, EXPR_RAWOFFSET, EXPR_BYTESEQ, EXPR_TAGWRAP, etc:
        // nothing to validate.
        return;
    }
}

} // namespace

namespace
{

QString tagLocationPrefix(TypeDecl *decl)
{
    if (!decl)
        return QString();

    const FILEREF &ref = decl->tagRef.fileDesc ? decl->tagRef : decl->fileRef;
    if (!ref.fileDesc)
        return QString();

    return QStringLiteral("%1(%2) : ")
        .arg(QString::fromLocal8Bit(ref.fileDesc->filePath))
        .arg(static_cast<qulonglong>(ref.lineNo));
}

} // namespace

void StructureRenderEngine::validateFieldTagExpressions(Type *enclosingScope, ExprNode *expr,
                                                         TypeDecl *owner, TOKEN tagTok, QStringList *errors)
{
    if (!errors)
        return;

    std::vector<ExprNode *> roots;
    collectFieldReferenceRoots(expr, &roots);

    for (ExprNode *root : roots)
    {
        if (!root || root->type != EXPR_IDENTIFIER || !root->str)
            continue;
        if (std::strcmp(root->str, "_") == 0)
            continue;
        if (isKnownEnumConstant(root->str))
            continue;

        ResolvedField dummy;
        if (enclosingScope && resolveDirectField(enclosingScope, root->str, 0, &dummy))
            continue;

        errors->push_back(tagLocationPrefix(owner)
                           + QStringLiteral("%1(...) references '%2', which cannot be resolved without "
                                            "a live file open (it isn't a sibling field, and isn't "
                                            "declared identically by every case(...) candidate of an "
                                            "enclosing union)")
                                 .arg(QString::fromLocal8Bit(Parser::inenglish(tagTok)))
                                 .arg(QString::fromLocal8Bit(root->str)));
    }
}

void StructureRenderEngine::validateStructTags(Type *structType, QStringList *errors)
{
    Type *base = BaseNode(structType);
    if (!base || (base->ty != typeSTRUCT && base->ty != typeUNION) || !base->sptr)
        return;

    static constexpr TOKEN kValidatedTags[] = {
        TOK_SELECT, TOK_ENDIAN, TOK_OFFSET, TOK_COUNT, TOK_MAXCOUNT, TOK_OPTIONAL, TOK_EXTENT, TOK_PADTO, TOK_COUNTAS, TOK_WARN, TOK_ASSERT
    };

    for (TypeDecl *decl : base->sptr->typeDeclList)
    {
        if (!decl || decl->typeAlias)
            continue;

        for (TOKEN tagTok : kValidatedTags)
        {
            ExprNode *tagExpr = nullptr;
            if (FindTag(decl->tagList, tagTok, &tagExpr) && tagExpr)
                validateFieldTagExpressions(structType, tagExpr, decl, tagTok, errors);
        }

        if (decl->baseType)
            validateStructTags(decl->baseType, errors);
    }
}

bool StructureRenderEngine::rootOffsetExpressionIsConstantFoldable(ExprNode *expr)
{
    if (!expr)
        return false;

    switch (expr->type)
    {
    case EXPR_NUMBER:
        return true;
    case EXPR_SIZEOF:
        return expr->left
            && expr->left->type == EXPR_IDENTIFIER
            && expr->left->str
            && staticSizeOfName(expr->left->str) != 0;
    case EXPR_UNARY:
        return rootOffsetExpressionIsConstantFoldable(expr->left);
    case EXPR_BINARY:
        return rootOffsetExpressionIsConstantFoldable(expr->left)
            && rootOffsetExpressionIsConstantFoldable(expr->right);
    case EXPR_TERTIARY:
        return rootOffsetExpressionIsConstantFoldable(expr->cond)
            && rootOffsetExpressionIsConstantFoldable(expr->left)
            && rootOffsetExpressionIsConstantFoldable(expr->right);
    default:
        return false;
    }
}

QString StructureRenderEngine::rootOffsetUnsupportedExpressionName(ExprNode *expr)
{
    if (!expr)
        return QStringLiteral("empty expression");

    switch (expr->type)
    {
    case EXPR_IDENTIFIER:
        return expr->str
            ? QStringLiteral("field reference '%1'").arg(QString::fromLocal8Bit(expr->str))
            : QStringLiteral("field reference");
    case EXPR_FIELD:
    case EXPR_ARRAY:
        return QStringLiteral("field reference");
    case EXPR_SIZEOF:
        return QStringLiteral("sizeof(...)");
    case EXPR_RAWOFFSET:
        return QStringLiteral("select_offset(...)");
    case EXPR_VALUEAT:
        return QStringLiteral("value_at(...)");
    case EXPR_FUNCTION:
        return QStringLiteral("%1(...)").arg(QString::fromLocal8Bit(Parser::inenglish(expr->tok)));
    case EXPR_STRINGBUF:
        return QStringLiteral("string literal");
    case EXPR_BYTESEQ:
        return QStringLiteral("byte sequence");
    case EXPR_TAGWRAP:
        return QStringLiteral("tag-wrapped expression");
    case EXPR_UNARY:
        return rootOffsetUnsupportedExpressionName(expr->left);
    case EXPR_BINARY:
    case EXPR_COMMA:
        if (!rootOffsetExpressionIsConstantFoldable(expr->left))
            return rootOffsetUnsupportedExpressionName(expr->left);
        return rootOffsetUnsupportedExpressionName(expr->right);
    case EXPR_TERTIARY:
        if (!rootOffsetExpressionIsConstantFoldable(expr->cond))
            return rootOffsetUnsupportedExpressionName(expr->cond);
        if (!rootOffsetExpressionIsConstantFoldable(expr->left))
            return rootOffsetUnsupportedExpressionName(expr->left);
        return rootOffsetUnsupportedExpressionName(expr->right);
    default:
        return QStringLiteral("runtime expression");
    }
}

QStringList StructureRenderEngine::validateStaticFieldReferences(StrataLibrary *library)
{
    QStringList errors;
    if (!library)
        return errors;

    static constexpr TOKEN kValidatedTags[] = {
        TOK_SELECT, TOK_ENDIAN, TOK_OFFSET, TOK_COUNT, TOK_MAXCOUNT, TOK_OPTIONAL, TOK_EXTENT, TOK_PADTO, TOK_COUNTAS, TOK_WARN, TOK_ASSERT
    };

    StructureRenderEngine scratch(library, nullptr, 0, StructureValueBuilder::ByteReader{}, StructureDisplayOptions{});

    for (TypeDecl *typeDecl : library->globalTypeDeclList)
    {
        if (!typeDecl || !typeDecl->baseType)
            continue;

        ExprNode *semanticExpr = nullptr;
        if (FindTag(typeDecl->tagList, TOK_SEMANTIC, &semanticExpr)
            && semanticExpr
            && semanticExpr->type == EXPR_IDENTIFIER)
        {
            TypeDecl *schemaDecl = semanticExpr->str ? scratch.findTypeDecl(semanticExpr->str) : nullptr;
            ExprNode *schemaMarker = nullptr;
            if (!schemaDecl
                || !FindTag(schemaDecl->tagList, TOK_SEMANTIC, &schemaMarker)
                || (schemaMarker && schemaMarker->type != EXPR_STRINGBUF))
            {
                errors.push_back(tagLocationPrefix(typeDecl)
                                 + QStringLiteral("semantic(...) references '%1', which is not a [semantic] schema")
                                       .arg(semanticExpr->str ? QString::fromLocal8Bit(semanticExpr->str) : QStringLiteral("<invalid>")));
            }
        }

        if (!typeDecl->typeAlias)
        {
            // The typedecl's own tags (e.g. _ELF's endian(...)) are scoped to its
            // own type, the same rule declarationBigEndian uses at the render-time
            // root (root->type, not root->parent).
            for (TOKEN tagTok : kValidatedTags)
            {
                ExprNode *tagExpr = nullptr;
                if (FindTag(typeDecl->tagList, tagTok, &tagExpr) && tagExpr)
                {
                    if (tagTok == TOK_OFFSET
                        && typeDecl->exported
                        && FindTag(typeDecl->tagList, TOK_EXPORT, nullptr)
                        && !scratch.rootOffsetExpressionIsConstantFoldable(tagExpr))
                    {
                        errors.push_back(tagLocationPrefix(typeDecl)
                                         + QStringLiteral("root offset(...) uses %1, but root offsets are "
                                                          "evaluated before a live render context exists; "
                                                          "use constant arithmetic only")
                                               .arg(scratch.rootOffsetUnsupportedExpressionName(tagExpr)));
                    }
                    scratch.validateFieldTagExpressions(typeDecl->baseType, tagExpr, typeDecl, tagTok, &errors);
                }
            }

            scratch.validateStructTags(typeDecl->baseType, &errors);
        }

        TypeDecl *schemaDecl = scratch.attachedSemanticSchema(typeDecl);
        if (schemaDecl)
        {
            struct DestinationIdentityUsage
            {
                QStringList path;
                bool positional = false;
                bool conventional = false;
                bool conflictReported = false;
            };
            std::vector<DestinationIdentityUsage> destinationIdentityUsages;

            auto validateEmitDestinations = [&](auto &&self, Type *scopeType) -> void {
                Type *base = BaseNode(scopeType);
                if (!base || (base->ty != typeSTRUCT && base->ty != typeUNION) || !base->sptr)
                    return;

                for (TypeDecl *decl : base->sptr->typeDeclList)
                {
                    if (!decl)
                        continue;

                    for (Tag *tag = decl->tagList; tag; tag = tag->link)
                    {
                        if (tag->tok != TOK_EMIT && tag->tok != TOK_EMITROW && tag->tok != TOK_EMITNODE)
                            continue;

                        ExprNode *destinationExpr = nullptr;
                        ExprNode *selectorExpr = nullptr;
                        ExprNode *labelExpr = nullptr;
                        ExprNode *typeNameExpr = nullptr;
                        ExprNode *offsetExpr = nullptr;
                        ExprNode *countExpr = nullptr;
                        ExprNode *stopExpr = nullptr;
                        ExprNode *terminatorModeExpr = nullptr;
                        ExprNode *conditionExpr = nullptr;
                        ExprNode *mapExpr = nullptr;
                        ExprNode *extentExpr = nullptr;
                        std::vector<SemanticNodeAttr> nodeAttrs;
                        bool parsed = false;
                        QString tagName;
                        ExprNode *rawDestinationExpr = nullptr;
                        std::vector<ExprNode *> rawTagArgs;
                        appendCommaArgs(tag->expr, &rawTagArgs);
                        for (ExprNode *rawArg : rawTagArgs)
                        {
                            ExprNode *inner = nullptr;
                            if (unwrapTagArg(rawArg, &inner) == TOK_DEST)
                            {
                                rawDestinationExpr = inner;
                                break;
                            }
                        }
                        if (tag->tok == TOK_EMIT)
                        {
                            tagName = QStringLiteral("emit");
                            parsed = scratch.emitArgs(tag->expr,
                                                      &destinationExpr,
                                                      &selectorExpr,
                                                      &labelExpr,
                                                      &typeNameExpr,
                                                      &offsetExpr,
                                                      &countExpr,
                                                      &stopExpr,
                                                      &terminatorModeExpr,
                                                      &conditionExpr,
                                                      &mapExpr);
                        }
                        else if (tag->tok == TOK_EMITROW)
                        {
                            tagName = QStringLiteral("emit_row");
                            parsed = scratch.emitRowArgs(tag->expr,
                                                         &destinationExpr,
                                                         &selectorExpr,
                                                         &offsetExpr,
                                                         &conditionExpr,
                                                         &mapExpr);
                        }
                        else
                        {
                            tagName = QStringLiteral("emit_node");
                            parsed = scratch.emitNodeArgs(tag->expr,
                                                          &destinationExpr,
                                                          &selectorExpr,
                                                          &labelExpr,
                                                          &offsetExpr,
                                                          &extentExpr,
                                                          &conditionExpr,
                                                          &nodeAttrs);
                        }
                        if (!parsed)
                        {
                            ExprNode *rawPathExpr = nullptr;
                            ExprNode *rawKeyExpr = nullptr;
                            ExprNode *rawNameExpr = nullptr;
                            ExprNode *rawAppendExpr = nullptr;
                            ExprNode *rawItemExpr = nullptr;
                            const bool hasPositionalDestination = rawDestinationExpr
                                && scratch.emitDestinationArgs(rawDestinationExpr,
                                                               &rawPathExpr,
                                                               &rawKeyExpr,
                                                               &rawNameExpr,
                                                               &rawAppendExpr,
                                                               &rawItemExpr)
                                && (rawAppendExpr || rawItemExpr);
                            if (tag->tok != TOK_EMITNODE && hasPositionalDestination)
                            {
                                errors.push_back(tagLocationPrefix(decl)
                                                 + QStringLiteral("append(...) and item(...) destination addressing is only valid on emit_node(...)"));
                                continue;
                            }
                            errors.push_back(tagLocationPrefix(decl)
                                             + QStringLiteral("%1(...) is missing a required wrapped argument")
                                                   .arg(tagName));
                            continue;
                        }

                        ExprNode *destinationPathExpr = nullptr;
                        ExprNode *keyExpr = nullptr;
                        ExprNode *nameExpr = nullptr;
                        ExprNode *appendExpr = nullptr;
                        ExprNode *itemExpr = nullptr;
                        ExprNode *destinationAddressExpr = rawDestinationExpr ? rawDestinationExpr : destinationExpr;
                        if (!scratch.emitDestinationArgs(destinationAddressExpr,
                                                         &destinationPathExpr,
                                                         &keyExpr,
                                                         &nameExpr,
                                                         &appendExpr,
                                                         &itemExpr))
                        {
                            errors.push_back(tagLocationPrefix(decl)
                                             + QStringLiteral("%1(dest(...)) is malformed")
                                                   .arg(tagName));
                            continue;
                        }
                        if (tag->tok != TOK_EMITNODE && (appendExpr || itemExpr))
                        {
                            errors.push_back(tagLocationPrefix(decl)
                                             + QStringLiteral("append(...) and item(...) destination addressing is only valid on emit_node(...)"));
                            continue;
                        }
                        if (tag->tok == TOK_EMITNODE && !labelExpr && nameExpr)
                            labelExpr = nameExpr;

                        Type *emitScopeType = scopeType;
                        Type *declType = decl->declList.empty() ? decl->baseType : decl->declList[0];
                        for (Type *cursor = declType; cursor; cursor = cursor->link)
                        {
                            if (cursor->ty == typeARRAY)
                            {
                                emitScopeType = cursor->link;
                                break;
                            }
                        }

                        const QStringList path = scratch.semanticPath(destinationPathExpr);
                        if (path.isEmpty() || !scratch.semanticDestinationExists(schemaDecl, path))
                        {
                            errors.push_back(tagLocationPrefix(decl)
                                             + QStringLiteral("%3(dest(%1)) does not resolve in semantic schema '%2'")
                                                   .arg(path.isEmpty() ? QStringLiteral("<invalid>") : path.join(QLatin1Char('.')))
                                                   .arg(schemaDecl->declList.empty() || !schemaDecl->declList[0]->sym
                                                            ? QStringLiteral("<anonymous>")
                                                            : QString::fromLocal8Bit(schemaDecl->declList[0]->sym->name))
                                                   .arg(tagName));
                        }

                        const bool positionalAddressing = appendExpr || itemExpr;
                        DestinationIdentityUsage *identityUsage = nullptr;
                        for (DestinationIdentityUsage &usage : destinationIdentityUsages)
                        {
                            if (usage.path == path)
                            {
                                identityUsage = &usage;
                                break;
                            }
                        }
                        if (!identityUsage)
                        {
                            destinationIdentityUsages.push_back(DestinationIdentityUsage{ path });
                            identityUsage = &destinationIdentityUsages.back();
                        }
                        identityUsage->positional = identityUsage->positional || positionalAddressing;
                        identityUsage->conventional = identityUsage->conventional || !positionalAddressing;
                        if (identityUsage->positional
                            && identityUsage->conventional
                            && !identityUsage->conflictReported)
                        {
                            errors.push_back(tagLocationPrefix(decl)
                                             + QStringLiteral("semantic destination '%1' mixes positional append/item addressing with key(...) or unaddressed emits")
                                                   .arg(path.isEmpty() ? QStringLiteral("<invalid>") : path.join(QLatin1Char('.'))));
                            identityUsage->conflictReported = true;
                        }

                        if (positionalAddressing)
                        {
                            TypeDecl *destinationDecl = scratch.semanticDestinationDecl(schemaDecl, path);
                            if (destinationDecl && !scratch.semanticDestinationIsArray(destinationDecl))
                            {
                                errors.push_back(tagLocationPrefix(decl)
                                                 + QStringLiteral("positional semantic destination '%1' is not an array")
                                                       .arg(path.join(QLatin1Char('.'))));
                            }
                        }

                        TypeDecl *elementSchema = tag->tok == TOK_EMITNODE
                            ? scratch.semanticDestinationElementSchema(schemaDecl, path)
                            : nullptr;
                        for (const SemanticNodeAttr &attr : nodeAttrs)
                        {
                            if (attr.schemaField
                                && (!elementSchema || !scratch.semanticSchemaHasField(elementSchema, attr.name)))
                            {
                                errors.push_back(tagLocationPrefix(decl)
                                                 + QStringLiteral("emit_node(field(%1, ...)) does not resolve in semantic destination '%2'")
                                                       .arg(attr.name)
                                                       .arg(path.isEmpty() ? QStringLiteral("<invalid>") : path.join(QLatin1Char('.'))));
                            }
                        }

                        if (offsetExpr)
                            scratch.validateFieldTagExpressions(emitScopeType, offsetExpr, decl, TOK_OFFSET, &errors);
                        if (countExpr)
                            scratch.validateFieldTagExpressions(emitScopeType, countExpr, decl, TOK_COUNT, &errors);
                        if (extentExpr)
                            scratch.validateFieldTagExpressions(emitScopeType, extentExpr, decl, TOK_EXTENT, &errors);
                        if (conditionExpr)
                            scratch.validateFieldTagExpressions(emitScopeType, conditionExpr, decl, TOK_OPTIONAL, &errors);
                        if (labelExpr)
                            scratch.validateFieldTagExpressions(emitScopeType, labelExpr, decl, TOK_LABEL, &errors);
                        if (selectorExpr)
                            scratch.validateFieldTagExpressions(emitScopeType, selectorExpr, decl, TOK_CASE, &errors);
                        if (mapExpr)
                            scratch.validateFieldTagExpressions(emitScopeType, mapExpr, decl, TOK_MAP, &errors);
                        if (keyExpr)
                            scratch.validateFieldTagExpressions(emitScopeType, keyExpr, decl, TOK_KEY, &errors);
                        if (nameExpr)
                            scratch.validateFieldTagExpressions(emitScopeType, nameExpr, decl, TOK_NAME, &errors);
                        if (itemExpr)
                        {
                            QString itemSequence;
                            ExprNode *itemIndexExpr = nullptr;
                            if (scratch.semanticItemArgs(itemExpr, &itemSequence, &itemIndexExpr) && itemIndexExpr)
                                scratch.validateFieldTagExpressions(emitScopeType, itemIndexExpr, decl, TOK_ITEM, &errors);
                        }
                        for (const SemanticNodeAttr &attr : nodeAttrs)
                            if (attr.valueExpr)
                                scratch.validateFieldTagExpressions(emitScopeType,
                                                                    attr.valueExpr,
                                                                    decl,
                                                                    attr.schemaField ? TOK_FIELD : TOK_ATTR,
                                                                    &errors);
                    }

                    self(self, decl->baseType);
                }
            };
            validateEmitDestinations(validateEmitDestinations, typeDecl->baseType);
        }
    }

    return errors;
}

bool StructureRenderEngine::readInteger(uint64_t offset, uint64_t length, INUMTYPE *result, bool bigEndian) const
{
    uint8_t data[sizeof(INUMTYPE)] = {};
    const size_t requested = static_cast<size_t>(std::max<uint64_t>(1, std::min<uint64_t>(length, sizeof(data))));
    const size_t got = m_reader ? m_reader(offset, data, requested) : 0;
    if (got != requested)
        return false;

    *result = unsignedValue(data, got, bigEndian);
    return true;
}

bool StructureRenderEngine::decodeScalarValue(Type *type, uint64_t offset, uint64_t *value, uint64_t *byteLength) const
{
    Type *base = BaseNode(type);
    if (!base || !value || !byteLength)
        return false;

    const ScalarTypeInfo info = scalarTypeInfo(base->ty);
    switch (info.encoding)
    {
    case ScalarEncoding::Fixed:
    {
        INUMTYPE fixedValue = 0;
        if (!readInteger(offset, info.fixedSize, &fixedValue, m_bigEndian))
            return false;
        *value = fixedValue;
        *byteLength = info.fixedSize;
        return true;
    }
    case ScalarEncoding::Uleb128:
        return decodeLeb128(m_reader, offset, false, info.maxBytes, value, byteLength);
    case ScalarEncoding::Sleb128:
        return decodeLeb128(m_reader, offset, true, info.maxBytes, value, byteLength);
    case ScalarEncoding::None:
        return false;
    }

    return false;
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
    if (!row || !row->typeDecl || !effectiveTag(row, row->typeDecl, TOK_DYNAMICCONTAINER, nullptr))
        return;

    INUMTYPE arrayIndex = 0;
    if (!arrayIndexFromRow(row, &arrayIndex))
        return;

    ExprNode *containerExpr = nullptr;
    effectiveTag(row, row->typeDecl, TOK_DYNAMICCONTAINER, &containerExpr);
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

    for (Tag *tag : effectiveTags(row, row->typeDecl))
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
            || !evaluate(row, fileOffsetExpr, &fileOffset, row->absoluteOffset)
            || logicalStart < 0
            || fileOffset < 0)
        {
            continue;
        }

        if (logicalSize <= 0)
            continue;

        container.fileOffset = uint64_t(fileOffset);
        container.byteLength = uint64_t(logicalSize);
        container.maps.push_back(OffsetMap{ uint64_t(logicalStart), uint64_t(logicalSize), uint64_t(fileOffset) });
    }

    if (!container.maps.empty())
        m_dynamicContainers.push_back(container);
}

void StructureRenderEngine::collectNamedOffsetMaps(StructureRow *row)
{
    if (!row || !row->typeDecl)
        return;

    for (Tag *tag : effectiveTags(row, row->typeDecl))
    {
        if (tag->tok != TOK_OFFSETMAP)
            continue;

        QString name;
        ExprNode *baseExpr = nullptr;
        ExprNode *logicalStartExpr = nullptr;
        ExprNode *logicalSizeExpr = nullptr;
        ExprNode *fileOffsetExpr = nullptr;
        if (!namedOffsetMapArgs(tag->expr,
                                &name,
                                &baseExpr,
                                &logicalStartExpr,
                                &logicalSizeExpr,
                                &fileOffsetExpr)
            || name.isEmpty())
        {
            continue;
        }

        if (baseExpr)
        {
            INUMTYPE base = 0;
            uint64_t mappedBase = 0;
            if (!evaluate(row, baseExpr, &base, row->absoluteOffset)
                || base < 0
                || !checkedAdd(m_baseOffset, static_cast<uint64_t>(base), &mappedBase))
            {
                continue;
            }

            m_namedOffsetMaps.push_back(NamedOffsetMap{
                name,
                0,
                0,
                mappedBase,
                false
            });
            continue;
        }

        INUMTYPE logicalStart = 0;
        INUMTYPE logicalSize = 0;
        INUMTYPE fileOffset = 0;
        if (!evaluate(row, logicalStartExpr, &logicalStart, row->absoluteOffset)
            || !evaluate(row, logicalSizeExpr, &logicalSize, row->absoluteOffset)
            || !evaluate(row, fileOffsetExpr, &fileOffset, row->absoluteOffset)
            || logicalStart < 0
            || logicalSize <= 0)
        {
            continue;
        }

        uint64_t mappedFileOffset = 0;
        if (fileOffset < 0
            || !checkedAdd(m_baseOffset, static_cast<uint64_t>(fileOffset), &mappedFileOffset))
        {
            continue;
        }

        m_namedOffsetMaps.push_back(NamedOffsetMap{
            name,
            static_cast<uint64_t>(logicalStart),
            static_cast<uint64_t>(logicalSize),
            mappedFileOffset,
            true
        });
    }
}

void StructureRenderEngine::collectDynamicRequests(StructureRow *row)
{
    if (!row || !row->typeDecl)
        return;

    INUMTYPE arrayIndex = 0;
    const bool rowIsArrayElement = arrayIndexFromRow(row, &arrayIndex);

    for (Tag *tag : effectiveTags(row, row->typeDecl))
    {
        if (tag->tok != TOK_DYNAMICSTRUCT)
            continue;

        ExprNode *selectorExpr = nullptr;
        ExprNode *labelExpr = nullptr;
        ExprNode *containerExpr = nullptr;
        ExprNode *typeNameExpr = nullptr;
        ExprNode *logicalOffsetExpr = nullptr;
        ExprNode *conditionExpr = nullptr;
        DynamicMapper mapper = DynamicMapper::Direct;
        if (!dynamicTagArgs(tag->expr, &selectorExpr, &labelExpr, &containerExpr, &typeNameExpr, &logicalOffsetExpr, &conditionExpr, &mapper))
            continue;

        INUMTYPE selector = 0;
        INUMTYPE condition = 1;
        INUMTYPE logicalOffset = 0;
        QString offsetSpace;
        ExprNode *offsetValueExpr = logicalOffsetExpr;
        if (!offsetTagArgs(logicalOffsetExpr, &offsetSpace, &offsetValueExpr))
            continue;

        if ((selectorExpr && (!rowIsArrayElement || !evaluate(row, selectorExpr, &selector, row->absoluteOffset) || selector != arrayIndex))
            || (conditionExpr && (!evaluate(row, conditionExpr, &condition, row->absoluteOffset) || condition == 0))
            || !evaluate(row, offsetValueExpr, &logicalOffset, row->absoluteOffset))
        {
            continue;
        }

        if (!offsetSpace.isEmpty())
        {
            uint64_t fileOffset = 0;
            if (mapper != DynamicMapper::Direct
                || !mapNamedOffset(offsetSpace, static_cast<uint64_t>(logicalOffset), &fileOffset))
            {
                continue;
            }
            logicalOffset = static_cast<INUMTYPE>(fileOffset);
        }

        if (!typeNameExpr || typeNameExpr->type != EXPR_IDENTIFIER || !typeNameExpr->str)
            continue;

        TypeDecl *targetType = findTypeDecl(typeNameExpr->str);
        if (!targetType)
            continue;

        Type *renderType = targetType->declList.empty() ? targetType->baseType : targetType->declList[0];
        if (!renderType)
            continue;

        QString label;
        if (labelExpr && labelExpr->str)
            label = QString::fromLocal8Bit(labelExpr->str);
        QString containerLabel;
        if (containerExpr && containerExpr->str)
            containerLabel = QString::fromLocal8Bit(containerExpr->str);

        m_dynamicRequests.push_back(DynamicRequest{ row, targetType, renderType, label, containerLabel, uint64_t(logicalOffset), mapper });
    }
}

void StructureRenderEngine::collectDynamicArrayRequests(StructureRow *row)
{
    if (!row || !row->typeDecl)
        return;
    if (row->lazyChildLoader)
        return;

    INUMTYPE probeArrayIndex = 0;
    const bool rowIsArrayElement = arrayIndexFromRow(row, &probeArrayIndex);

    for (Tag *tag : effectiveTags(row, row->typeDecl))
    {
        if (tag->tok != TOK_DYNAMICARRAY)
            continue;

        ExprNode *selectorOrLabelExpr = nullptr;
        ExprNode *containerExpr = nullptr;
        ExprNode *typeNameExpr = nullptr;
        ExprNode *logicalOffsetExpr = nullptr;
        ExprNode *countExpr = nullptr;
        ExprNode *stopExpr = nullptr;
        ExprNode *terminatorModeExpr = nullptr;
        ExprNode *conditionExpr = nullptr;
        DynamicMapper mapper = DynamicMapper::Direct;
        bool isCaseSelector = false;
        if (!dynamicArrayArgs(tag->expr,
                              &selectorOrLabelExpr,
                              &containerExpr,
                              &typeNameExpr,
                              &logicalOffsetExpr,
                              &countExpr,
                              &stopExpr,
                              &terminatorModeExpr,
                              &conditionExpr,
                              &mapper,
                              nullptr,
                              &isCaseSelector))
            continue;

        // dynamic_array mirrors dynamic_struct for directory arrays: when the
        // owner row is itself an array element, the first argument can select
        // which element emits the referenced table.  On ordinary rows the same
        // argument is only a display label for the generated array.
        bool attachToMappedContainer = false;
        if (isCaseSelector)
        {
            INUMTYPE arrayIndex = 0;
            INUMTYPE selector = 0;
            if (!rowIsArrayElement
                || !arrayIndexFromRow(row, &arrayIndex)
                || !evaluate(row, selectorOrLabelExpr, &selector, row->absoluteOffset)
                || selector != arrayIndex)
            {
                continue;
            }
            attachToMappedContainer = mapper == DynamicMapper::OffsetMap;
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
        QString offsetSpace;
        ExprNode *offsetValueExpr = logicalOffsetExpr;
        if (!offsetTagArgs(logicalOffsetExpr, &offsetSpace, &offsetValueExpr))
            continue;

        if (!evaluate(row, offsetValueExpr, &logicalOffset, row->absoluteOffset)
            || !evaluate(row, countExpr, &count, row->absoluteOffset)
            || (conditionExpr && (!evaluate(row, conditionExpr, &condition, row->absoluteOffset) || condition == 0))
            || count <= 0)
        {
            continue;
        }

        if (!offsetSpace.isEmpty())
        {
            uint64_t fileOffset = 0;
            if (mapper != DynamicMapper::Direct
                || !mapNamedOffset(offsetSpace, static_cast<uint64_t>(logicalOffset), &fileOffset))
            {
                continue;
            }
            logicalOffset = static_cast<INUMTYPE>(fileOffset);
        }

        QString label;
        if (selectorOrLabelExpr && selectorOrLabelExpr->str
            && (selectorOrLabelExpr->type == EXPR_IDENTIFIER || selectorOrLabelExpr->type == EXPR_STRINGBUF))
        {
            label = QString::fromLocal8Bit(selectorOrLabelExpr->str);
        }
        QString containerLabel;
        if (containerExpr && containerExpr->str
            && (containerExpr->type == EXPR_IDENTIFIER || containerExpr->type == EXPR_STRINGBUF))
        {
            containerLabel = QString::fromLocal8Bit(containerExpr->str);
        }

        m_dynamicArrayRequests.push_back(DynamicArrayRequest{
            row,
            targetType,
            renderType,
            label,
            containerLabel,
            uint64_t(logicalOffset),
            uint64_t(count),
            stopExpr,
            terminatorModeExpr,
            conditionExpr,
            mapper,
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
        uint64_t absoluteOffset = 0;
        if (!checkedAdd(m_baseOffset, container.fileOffset, &absoluteOffset))
            continue;

        auto row = makeRow(parent, renderType, container.typeDecl, absoluteOffset);
        applyDeclarationName(row.get(), renderType);
        if (!container.alias.isEmpty())
        {
            row->setNameParts(row->nameTypePrefix, container.alias, row->nameSuffix, row->emphasizeName);
        }
        row->value = QStringLiteral("{...}");
        row->byteLength = container.byteLength;
        row->kind = StructureRowKind::Dynamic;
        if (kShowDynamicViewBranchIcons)
        {
            row->setBranchIcons(QString::fromLatin1(StructureBranchIcons::kBlueStructure),
                                QString::fromLatin1(StructureBranchIcons::kBlueStructureOpen),
                                QString::fromLatin1(StructureBranchIcons::kGrayStructure));
        }
        container.row = (row->treeMode == StructureRowTreeMode::Default
                         || row->treeMode == StructureRowTreeMode::Collapsed
                         || row->treeMode == StructureRowTreeMode::Expanded)
            ? row.get()
            : nullptr;
        appendPresentedRow(parent, std::move(row));
    }

    for (const DynamicRequest &request : m_dynamicRequests)
    {
        uint64_t fileOffset = 0;
        StructureRow *parentRow = request.owner;
        if (request.mapper == DynamicMapper::OffsetMap)
        {
            DynamicContainer *container = mapLogicalOffset(request.logicalOffset, &fileOffset);
            if (!container || !container->row)
                continue;
            parentRow = container->row;
        }
        else
        {
            fileOffset = request.logicalOffset;
        }
        if (!request.containerLabel.isEmpty())
            parentRow = dynamicRootGroup(request.containerLabel);

        if (!parentRow || !request.typeDecl || !request.renderType)
            continue;

        uint64_t absoluteOffset = 0;
        if (!checkedAdd(m_baseOffset, fileOffset, &absoluteOffset))
            continue;

        auto row = makeRow(parentRow, request.renderType, request.typeDecl, absoluteOffset);
        applyDeclarationName(row.get(), request.renderType);
        if (!request.label.isEmpty())
            row->setNameParts(QString(), request.label, QString());
        row->kind = StructureRowKind::Dynamic;
        if (kShowDynamicViewBranchIcons)
        {
            row->setBranchIcons(QString::fromLatin1(StructureBranchIcons::kBlueStructure),
                                QString::fromLatin1(StructureBranchIcons::kBlueStructureOpen),
                                QString::fromLatin1(StructureBranchIcons::kGrayStructure));
        }
        const bool bigEndian = declarationBigEndian(request.typeDecl, row.get(), request.renderType, absoluteOffset);
        EndianScope endian(this, bigEndian);
        row->bigEndian = m_bigEndian;
        row->byteLength = formatType(row.get(), request.renderType, request.typeDecl, absoluteOffset);
        if (row->value.isEmpty() && !row->children.empty())
            row->value = QStringLiteral("{...}");
        appendPresentedRow(parentRow, std::move(row));
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

        uint64_t fileOffset = request.logicalOffset;
        StructureRow *parentRow = row;
        if (request.mapper == DynamicMapper::OffsetMap)
        {
            DynamicContainer *container = mapLogicalOffset(request.logicalOffset, &fileOffset);
            if (!container)
                continue;
            parentRow = request.attachToMappedContainer && container->row ? container->row : row;
        }
        if (!request.containerLabel.isEmpty())
            parentRow = dynamicRootGroup(request.containerLabel);

        EndianScope requestEndian(this, request.owner ? request.owner->bigEndian : m_bigEndian);
        uint64_t arrayOffset = 0;
        if (!checkedAdd(m_baseOffset, fileOffset, &arrayOffset))
            continue;

        auto arrayRow = makeRow(parentRow, request.renderType, request.typeDecl, arrayOffset);
        const QString elementTypeName = typeName(request.renderType);
        // Name the array container row. generatedName is set only when a separate
        // label is present (e.g. "CHAR DllName[]") so applyDisplayOptions can
        // reformat the type-name prefix when the user switches type-name modes.
        // When there is no label — either the name() tag supplies the full string
        // or the type name is used alone — the row name is fixed and generatedName
        // stays false so applyDisplayOptions does not reintroduce a redundant prefix.
        ExprNode *nameTagExpr = nullptr;
        if (FindTag(request.typeDecl ? request.typeDecl->tagList : nullptr, TOK_NAME, &nameTagExpr)
            && nameTagExpr && nameTagExpr->type == EXPR_STRINGBUF && nameTagExpr->str)
        {
            arrayRow->setNameParts(QString(), QString::fromLocal8Bit(nameTagExpr->str), QString());
        }
        else if (request.label.isEmpty())
        {
            arrayRow->setNameParts(QString(), elementTypeName, QStringLiteral("[]"));
        }
        else
        {
            arrayRow->setNameParts(elementTypeName, request.label, QStringLiteral("[]"));
            arrayRow->generatedName = true;
        }
        arrayRow->value = QStringLiteral("{...}");
        arrayRow->kind = StructureRowKind::Dynamic;
        if (kShowDynamicViewBranchIcons)
        {
            arrayRow->setBranchIcons(QString::fromLatin1(StructureBranchIcons::kBlueEntityArray),
                                     QString::fromLatin1(StructureBranchIcons::kBlueEntityArray),
                                     QString::fromLatin1(StructureBranchIcons::kGrayEntityArray));
        }

        // Check once whether the element type declares sub-arrays.  Primitive
        // element types (DWORD, WORD, CHAR, thunk unions, …) never do, so we
        // avoid calling collectDynamicArrayRequests for every element of the
        // large export/import raw-data arrays. While we're scanning anyway,
        // also look for a dynamic_array tag whose label argument was written
        // as name(...) (e.g. dynamic_array(name(DllName), type(CHAR), ...)):
        // that explicitly marks itself as the per-element name source for
        // whichever array contains elements of this type.
        bool elementTypeHasSubArrays = false;
        ExprNode *nameSourceTagExpr = nullptr;
        for (Tag *tag = request.typeDecl ? request.typeDecl->tagList : nullptr; tag; tag = tag->link)
        {
            if (tag->tok != TOK_DYNAMICARRAY)
                continue;
            elementTypeHasSubArrays = true;

            if (!nameSourceTagExpr)
            {
                bool isNameSource = false;
                ExprNode *nameSourceTypeNameExpr = nullptr;
                if (dynamicArrayArgs(tag->expr, nullptr, nullptr, &nameSourceTypeNameExpr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &isNameSource)
                    && isNameSource)
                {
                    Type *nameSourceType = nameSourceTypeNameExpr && nameSourceTypeNameExpr->str
                        ? typeInDecl(findTypeDecl(nameSourceTypeNameExpr->str), nameSourceTypeNameExpr->str)
                        : nullptr;
                    Type *nameSourceBase = BaseNode(nameSourceType);
                    if (nameSourceBase && (nameSourceBase->ty == typeCHAR || nameSourceBase->ty == typeWCHAR))
                        nameSourceTagExpr = tag->expr;
                }
            }
        }

        uint64_t length = 0;
        uint64_t logicalIndex = 0;
        uint64_t renderedElements = 0;
        const bool continuePastDisplayCap = typeContainsTag(request.renderType, TOK_COUNTAS);
        while (logicalIndex < request.maxCount
               && (renderedElements < kMaxArrayElements || continuePastDisplayCap))
        {
            if (fileOffset > std::numeric_limits<uint64_t>::max() - length)
                break;
            const uint64_t relativeElementOffset = fileOffset + length;
            uint64_t elementOffset = 0;
            if (!checkedAdd(m_baseOffset, relativeElementOffset, &elementOffset))
                break;
            if (!canReadByte(elementOffset))
                break;

            const bool renderElement = renderedElements < kMaxArrayElements;
            auto elementRow = makeRow(arrayRow.get(), request.renderType, request.typeDecl, elementOffset);
            if (renderElement)
            {
                const QString indexLabel = QStringLiteral("[%1]").arg(logicalIndex);
                elementRow->name = indexLabel;
                elementRow->nameTypePrefix = indexLabel;
            }
            elementRow->kind = StructureRowKind::Dynamic;
            elementRow->suppressSemanticViews = true;

            const uint64_t elementLength = formatType(elementRow.get(), request.renderType, request.typeDecl, elementOffset);
            elementRow->byteLength = elementLength;
            collectNamedOffsetMaps(elementRow.get());

            if (renderElement && nameSourceTagExpr)
            {
                const QString fieldName = dynamicArrayNameString(elementRow.get(), nameSourceTagExpr);
                if (!fieldName.isEmpty())
                {
                    elementRow->nameIdentifier = arrayAliasPrefix() + fieldName;
                    elementRow->name += elementRow->nameIdentifier;
                }
            }

            // For compound element types, collect the sub-array requests that
            // reference this element's fields (e.g. DllName RVA, thunk RVAs) while
            // the element's children are live, then defer the actual array building
            // to a lazy loader so the initial tree open stays fast.
            if (renderElement && elementTypeHasSubArrays)
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
                    elementRow->lazyChildLoader = [self, elemPtr, reqs = std::move(subRequests)]() {
                        return self->buildSubArraysForElement(elemPtr, reqs);
                    };
                }
            }

            const uint64_t terminatorLength = terminatorMatchLength(elementRow.get(),
                                                                     request.renderType,
                                                                     request.stopExpr,
                                                                     elementOffset,
                                                                     elementLength);
            const bool hideTerminator = terminatorLength > 0
                && terminatorShouldBeHidden(request.typeDecl,
                                            request.renderType,
                                            request.stopExpr,
                                            request.terminatorModeExpr);
            const uint64_t consumedLength = terminatorLength > 0 ? terminatorLength : elementLength;
            // Dynamic arrays are often driven by offsets/counts read from the
            // file. If the user manually applies a mismatched format, those
            // values can point past EOF or describe zero-sized elements. Stop
            // on non-progress exactly like inline arrays do, instead of
            // iterating a bogus count without advancing.
            if (consumedLength == 0)
                break;

            if (!checkedAdd(length, consumedLength, &length))
                break;
            logicalIndex += std::max<uint64_t>(elementRow->arrayCountContribution, 1);
            ++renderedElements;
            if (terminatorLength > 0)
            {
                if (renderElement && !hideTerminator)
                    appendPresentedRow(arrayRow.get(), std::move(elementRow));
                break;
            }

            if (renderElement)
                appendPresentedRow(arrayRow.get(), std::move(elementRow));
        }

        arrayRow->byteLength = length;
        const QString stringValue = dynamicArrayStringValue(request.renderType,
                                                            request.typeDecl,
                                                            arrayOffset,
                                                            length,
                                                            arrayRow->bigEndian);
        if (!stringValue.isNull())
            arrayRow->value = stringValue;
        if (!arrayRow->children.empty())
            appendPresentedRow(parentRow, std::move(arrayRow));
    }

    const size_t childCount = row->children.size();
    for (size_t childIndex = 0; childIndex < childCount; ++childIndex)
        appendDynamicArrayRows(row->children[childIndex].get());
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

void StructureRenderEngine::collectSemanticEmitRequests(StructureRow *row)
{
    if (!row || !row->typeDecl)
        return;

    TypeDecl *schemaDecl = attachedSemanticSchema(m_rootType);
    if (!schemaDecl)
        return;

    for (Tag *tag : effectiveTags(row, row->typeDecl))
    {
        if (tag->tok == TOK_EMITROW)
        {
            ExprNode *destinationExpr = nullptr;
            ExprNode *selectorExpr = nullptr;
            ExprNode *logicalOffsetExpr = nullptr;
            ExprNode *conditionExpr = nullptr;
            ExprNode *mapExpr = nullptr;
            if (!emitRowArgs(tag->expr, &destinationExpr, &selectorExpr, &logicalOffsetExpr, &conditionExpr, &mapExpr))
                continue;

            ExprNode *destinationPathExpr = nullptr;
            ExprNode *keyExpr = nullptr;
            ExprNode *nameExpr = nullptr;
            if (!emitDestinationArgs(destinationExpr, &destinationPathExpr, &keyExpr, &nameExpr))
                continue;

            const QStringList destination = semanticPath(destinationPathExpr);
            if (destination.isEmpty() || !semanticDestinationExists(schemaDecl, destination))
                continue;

            INUMTYPE arrayIndex = 0;
            INUMTYPE selector = 0;
            if (selectorExpr
                && (!arrayIndexFromRow(row, &arrayIndex)
                    || !evaluate(row, selectorExpr, &selector, row->absoluteOffset)
                    || selector != arrayIndex))
            {
                continue;
            }

            INUMTYPE condition = 1;
            if (conditionExpr && (!evaluate(row, conditionExpr, &condition, row->absoluteOffset) || condition == 0))
                continue;

            QString offsetSpace;
            ExprNode *offsetValueExpr = logicalOffsetExpr;
            INUMTYPE logicalOffset = row->absoluteOffset >= m_baseOffset ? INUMTYPE(row->absoluteOffset - m_baseOffset) : 0;
            if (logicalOffsetExpr)
            {
                if (!offsetTagArgs(logicalOffsetExpr, &offsetSpace, &offsetValueExpr)
                    || !evaluate(row, offsetValueExpr, &logicalOffset, row->absoluteOffset)
                    || logicalOffset < 0)
                {
                    continue;
                }
            }

            QString mapSpace;
            ExprNode *mapLogicalStartExpr = nullptr;
            ExprNode *mapLogicalSizeExpr = nullptr;
            ExprNode *mapFileOffsetExpr = nullptr;
            INUMTYPE mapLogicalStart = 0;
            INUMTYPE mapLogicalSize = 0;
            INUMTYPE mapFileOffset = 0;
            const bool createsMappedContainer = mapExpr
                && emitMapArgs(mapExpr, &mapSpace, &mapLogicalStartExpr, &mapLogicalSizeExpr, &mapFileOffsetExpr)
                && evaluate(row, mapLogicalStartExpr, &mapLogicalStart, row->absoluteOffset)
                && evaluate(row, mapLogicalSizeExpr, &mapLogicalSize, row->absoluteOffset)
                && evaluate(row, mapFileOffsetExpr, &mapFileOffset, row->absoluteOffset)
                && !mapSpace.isEmpty()
                && mapLogicalSize > 0;

            uint64_t fileOffset = uint64_t(logicalOffset);
            if (!offsetSpace.isEmpty() && !createsMappedContainer && !mapNamedOffset(offsetSpace, fileOffset, &fileOffset))
                fileOffset = uint64_t(logicalOffset);

            m_semanticRowRequests.push_back(SemanticRowRequest{
                row,
                destination,
                selectorExpr,
                keyExpr,
                nameExpr,
                offsetSpace,
                uint64_t(logicalOffset),
                fileOffset,
                conditionExpr,
                mapSpace,
                uint64_t(mapLogicalStart),
                uint64_t(mapLogicalSize),
                uint64_t(mapFileOffset),
                createsMappedContainer
            });
            continue;
        }

        if (tag->tok == TOK_EMITNODE)
        {
            ExprNode *destinationExpr = nullptr;
            ExprNode *selectorExpr = nullptr;
            ExprNode *nameExpr = nullptr;
            ExprNode *logicalOffsetExpr = nullptr;
            ExprNode *extentExpr = nullptr;
            ExprNode *conditionExpr = nullptr;
            std::vector<SemanticNodeAttr> attrs;
            if (!emitNodeArgs(tag->expr,
                              &destinationExpr,
                              &selectorExpr,
                              &nameExpr,
                              &logicalOffsetExpr,
                              &extentExpr,
                              &conditionExpr,
                              &attrs))
            {
                continue;
            }

            ExprNode *destinationPathExpr = nullptr;
            ExprNode *keyExpr = nullptr;
            ExprNode *destinationNameExpr = nullptr;
            ExprNode *appendExpr = nullptr;
            ExprNode *itemExpr = nullptr;
            if (!emitDestinationArgs(destinationExpr,
                                     &destinationPathExpr,
                                     &keyExpr,
                                     &destinationNameExpr,
                                     &appendExpr,
                                     &itemExpr))
                continue;

            SemanticNodeAddress address = SemanticNodeAddress::Ordinary;
            QString sequenceName;
            ExprNode *itemIndexExpr = nullptr;
            if (appendExpr)
            {
                if (!semanticAppendArgs(appendExpr, &sequenceName))
                    continue;
                address = SemanticNodeAddress::Append;
            }
            else if (itemExpr)
            {
                if (!semanticItemArgs(itemExpr, &sequenceName, &itemIndexExpr))
                    continue;
                address = SemanticNodeAddress::Item;
            }

            const QStringList destination = semanticPath(destinationPathExpr);
            if (destination.isEmpty() || !semanticDestinationExists(schemaDecl, destination))
                continue;

            INUMTYPE arrayIndex = 0;
            INUMTYPE selector = 0;
            if (selectorExpr
                && (!arrayIndexFromRow(row, &arrayIndex)
                    || !evaluate(row, selectorExpr, &selector, row->absoluteOffset)
                    || selector != arrayIndex))
            {
                continue;
            }

            INUMTYPE condition = 1;
            if (conditionExpr && (!evaluate(row, conditionExpr, &condition, row->absoluteOffset) || condition == 0))
                continue;

            QString offsetSpace;
            ExprNode *offsetValueExpr = logicalOffsetExpr;
            INUMTYPE logicalOffset = row->absoluteOffset >= m_baseOffset ? INUMTYPE(row->absoluteOffset - m_baseOffset) : 0;
            if (logicalOffsetExpr)
            {
                if (!offsetTagArgs(logicalOffsetExpr, &offsetSpace, &offsetValueExpr)
                    || !evaluate(row, offsetValueExpr, &logicalOffset, row->absoluteOffset))
                {
                    continue;
                }
            }

            uint64_t fileOffset = uint64_t(logicalOffset);
            if (!offsetSpace.isEmpty() && !mapNamedOffset(offsetSpace, fileOffset, &fileOffset))
                fileOffset = uint64_t(logicalOffset);

            m_semanticNodeRequests.push_back(SemanticNodeRequest{
                row,
                destination,
                selectorExpr,
                keyExpr,
                nameExpr ? nameExpr : destinationNameExpr,
                offsetSpace,
                uint64_t(logicalOffset),
                fileOffset,
                extentExpr,
                conditionExpr,
                attrs,
                address,
                sequenceName,
                itemIndexExpr
            });
            continue;
        }

        if (tag->tok != TOK_EMIT)
            continue;

        ExprNode *destinationExpr = nullptr;
        ExprNode *selectorExpr = nullptr;
        ExprNode *labelExpr = nullptr;
        ExprNode *typeNameExpr = nullptr;
        ExprNode *logicalOffsetExpr = nullptr;
        ExprNode *countExpr = nullptr;
        ExprNode *stopExpr = nullptr;
        ExprNode *terminatorModeExpr = nullptr;
        ExprNode *conditionExpr = nullptr;
        ExprNode *mapExpr = nullptr;
        if (!emitArgs(tag->expr,
                      &destinationExpr,
                      &selectorExpr,
                      &labelExpr,
                      &typeNameExpr,
                      &logicalOffsetExpr,
                      &countExpr,
                      &stopExpr,
                      &terminatorModeExpr,
                      &conditionExpr,
                      &mapExpr))
        {
            continue;
        }

        const QStringList destination = semanticPath(destinationExpr);
        if (destination.isEmpty() || !semanticDestinationExists(schemaDecl, destination))
            continue;

        if (!typeNameExpr || typeNameExpr->type != EXPR_IDENTIFIER || !typeNameExpr->str)
            continue;

        TypeDecl *targetType = findTypeDecl(typeNameExpr->str);
        Type *renderType = typeInDecl(targetType, typeNameExpr->str);
        if (!targetType || !renderType)
            continue;

        INUMTYPE arrayIndex = 0;
        INUMTYPE selector = 0;
        if (selectorExpr
            && (!arrayIndexFromRow(row, &arrayIndex)
                || !evaluate(row, selectorExpr, &selector, row->absoluteOffset)
                || selector != arrayIndex))
        {
            continue;
        }

        INUMTYPE logicalOffset = 0;
        INUMTYPE count = 0;
        INUMTYPE condition = 1;
        QString offsetSpace;
        ExprNode *offsetValueExpr = logicalOffsetExpr;
        if (!offsetTagArgs(logicalOffsetExpr, &offsetSpace, &offsetValueExpr))
            continue;

        if (!evaluate(row, offsetValueExpr, &logicalOffset, row->absoluteOffset)
            || !evaluate(row, countExpr, &count, row->absoluteOffset)
            || (conditionExpr && (!evaluate(row, conditionExpr, &condition, row->absoluteOffset) || condition == 0))
            || logicalOffset < 0
            || count <= 0)
        {
            continue;
        }

        QString mapSpace;
        ExprNode *mapLogicalStartExpr = nullptr;
        ExprNode *mapLogicalSizeExpr = nullptr;
        ExprNode *mapFileOffsetExpr = nullptr;
        INUMTYPE mapLogicalStart = 0;
        INUMTYPE mapLogicalSize = 0;
        INUMTYPE mapFileOffset = 0;
        const bool createsMappedContainer = mapExpr
            && emitMapArgs(mapExpr, &mapSpace, &mapLogicalStartExpr, &mapLogicalSizeExpr, &mapFileOffsetExpr)
            && evaluate(row, mapLogicalStartExpr, &mapLogicalStart, row->absoluteOffset)
            && evaluate(row, mapLogicalSizeExpr, &mapLogicalSize, row->absoluteOffset)
            && evaluate(row, mapFileOffsetExpr, &mapFileOffset, row->absoluteOffset)
            && !mapSpace.isEmpty()
            && mapLogicalStart >= 0
            && mapFileOffset >= 0
            && mapLogicalSize > 0;

        uint64_t fileOffset = uint64_t(logicalOffset);
        if (!offsetSpace.isEmpty() && !createsMappedContainer && !mapNamedOffset(offsetSpace, fileOffset, &fileOffset))
            fileOffset = uint64_t(logicalOffset);

        m_semanticEmitRequests.push_back(SemanticEmitRequest{
            row,
            targetType,
            renderType,
            destination,
            selectorExpr,
            labelExpr,
            offsetSpace,
            uint64_t(logicalOffset),
            fileOffset,
            uint64_t(count),
            stopExpr,
            terminatorModeExpr,
            conditionExpr,
            mapSpace,
            uint64_t(mapLogicalStart),
            uint64_t(mapLogicalSize),
            uint64_t(mapFileOffset),
            createsMappedContainer
        });
    }

    for (const auto &child : row->children)
        collectSemanticEmitRequests(child.get());

    if (row->lazyChildLoader)
    {
        std::vector<RowPtr> lazyRows = row->lazyChildLoader();
        for (const RowPtr &lazyRow : lazyRows)
            collectSemanticEmitRequests(lazyRow.get());
        for (RowPtr &lazyRow : lazyRows)
            m_semanticSourceRows.push_back(std::move(lazyRow));
    }
}

void StructureRenderEngine::appendSemanticRowRequests()
{
    if (m_semanticRowRequests.empty())
        return;

    auto appendRequest = [this](const SemanticRowRequest &request) {
        uint64_t fileOffset = request.fileOffset;
        if (!request.owner)
            return;

        std::vector<ExprNode *> keyExprs;
        appendCommaArgs(request.keyExpr, &keyExprs);
        QStringList keys;
        for (ExprNode *keyExpr : keyExprs)
        {
            const QString key = semanticExpressionText(request.owner,
                                                       request.owner->type,
                                                       keyExpr,
                                                       request.owner->absoluteOffset);
            if (key.isEmpty())
                return;
            keys.push_back(key);
        }

        StructureRow *parentRow = nullptr;
        QStringList entityPath = request.destinationPath;
        QString key = keys.isEmpty() ? QString() : keys.last();

        if (keys.size() > 1)
        {
            if (request.destinationPath.size() < int(keys.size()))
                return;

            const int parentKeyCount = int(keys.size()) - 1;
            const int parentPathLength = request.destinationPath.size() - parentKeyCount;
            if (parentPathLength <= 0)
                return;

            QStringList parentPath = request.destinationPath.mid(0, parentPathLength);
            uint64_t parentFileOffset = request.fileOffset;
            StructureRow *parentGroup = semanticDestinationForPath(parentPath,
                                                                   request.offsetSpace,
                                                                   request.logicalOffset,
                                                                   &parentFileOffset);
            if (!parentGroup)
                return;

            StructureRow *entityRow = nullptr;
            for (int i = 0; i < parentKeyCount; ++i)
            {
                entityPath = request.destinationPath.mid(0, parentPathLength + i);
                entityRow = nullptr;
                for (const SemanticEntity &entity : m_semanticEntities)
                {
                    if (entity.parent == parentGroup
                        && entity.destinationPath == entityPath
                        && entity.key == keys[i]
                        && entity.row)
                    {
                        entityRow = entity.row;
                        break;
                    }
                }

                if (!entityRow)
                    return;

                parentGroup = entityRow;
                if (i + 1 < parentKeyCount)
                {
                    QStringList nextPath = request.destinationPath.mid(0, parentPathLength + i + 1);
                    parentGroup = semanticChildGroup(parentGroup, nextPath);
                    if (!parentGroup)
                        return;
                }
            }

            parentRow = entityRow;
            QStringList currentPath = entityPath;
            for (int i = entityPath.size(); i < request.destinationPath.size(); ++i)
            {
                currentPath.append(request.destinationPath[i]);
                parentRow = semanticChildGroup(parentRow, currentPath);
                if (!parentRow)
                    return;
            }
        }
        else
        {
            parentRow = semanticDestinationForPath(request.destinationPath,
                                                   request.offsetSpace,
                                                   request.logicalOffset,
                                                   &fileOffset);
        }

        if (!parentRow)
            return;

        if (!key.isEmpty())
        {
            for (const SemanticEntity &entity : m_semanticEntities)
            {
                if (entity.parent == parentRow
                    && entity.destinationPath == request.destinationPath
                    && entity.key == key
                    && entity.row)
                {
                    if (request.createsMappedContainer)
                    {
                        m_semanticContainers.push_back(SemanticContainer{
                            entity.row,
                            request.destinationPath,
                            request.mapSpace,
                            request.mapLogicalStart,
                            request.mapLogicalSize,
                            request.mapFileOffset
                        });
                    }
                    return;
                }
            }
        }

        QString name = semanticExpressionText(request.owner,
                                              request.owner->type,
                                              request.nameExpr,
                                              request.owner->absoluteOffset);
        if (name.isEmpty())
            name = key;
        if (name.isEmpty())
            name = request.destinationPath.isEmpty() ? QStringLiteral("Semantic Row") : request.destinationPath.last();

        auto row = std::make_unique<StructureRow>(parentRow);
        row->kind = StructureRowKind::Semantic;
        row->suppressSemanticViews = true;
        row->name = rowNameFragment(name);
        row->nameIdentifier = row->name;
        row->value = QStringLiteral("{...}");
        if (!checkedAdd(m_baseOffset, fileOffset, &row->absoluteOffset))
            return;
        row->relativeOffset = fileOffset;
        row->offset = formatOffset(row->absoluteOffset);
        row->generatedOffset = true;
        // A mapped semantic row represents the mapped file range, not merely
        // its starting offset. This also makes it selectable in HexView.
        if (request.createsMappedContainer)
            row->byteLength = request.mapLogicalSize;
        TypeDecl *destinationDecl = semanticDestinationDecl(attachedSemanticSchema(m_rootType), request.destinationPath);
        const bool semanticElement = !request.createsMappedContainer
            && semanticDestinationElementIsEmptyCompound(destinationDecl);
        applySemanticBranchIcons(row.get(),
                                 request.destinationPath,
                                 false,
                                 semanticDestinationIsScalarArray(destinationDecl),
                                 false,
                                 request.createsMappedContainer,
                                 semanticElement);

        StructureRow *rowPtr = row.get();
        appendPresentedRow(parentRow, std::move(row));
        if (!key.isEmpty())
        {
            m_semanticEntities.push_back(SemanticEntity{
                parentRow,
                rowPtr,
                request.destinationPath,
                key
            });
        }

        if (request.createsMappedContainer)
        {
            m_semanticContainers.push_back(SemanticContainer{
                rowPtr,
                request.destinationPath,
                request.mapSpace,
                request.mapLogicalStart,
                request.mapLogicalSize,
                request.mapFileOffset
            });
        }
    };

    for (const SemanticRowRequest &request : m_semanticRowRequests)
        if (request.createsMappedContainer)
            appendRequest(request);

    for (const SemanticRowRequest &request : m_semanticRowRequests)
        if (!request.createsMappedContainer)
            appendRequest(request);
}

void StructureRenderEngine::appendSemanticNodeRequests()
{
    if (m_semanticNodeRequests.empty())
        return;

    auto upsertAttr = [this](StructureRow *node, const QString &name, const QString &value) {
        if (!node || name.isEmpty())
            return;
        for (const auto &child : node->children)
        {
            if (child && child->kind == StructureRowKind::Semantic && child->name == name)
            {
                child->value = value;
                return;
            }
        }

        auto attrRow = std::make_unique<StructureRow>(node);
        attrRow->kind = StructureRowKind::Semantic;
        attrRow->suppressSemanticViews = true;
        attrRow->name = name;
        attrRow->value = value;
        attrRow->absoluteOffset = node->absoluteOffset;
        attrRow->relativeOffset = node->relativeOffset;
        attrRow->offset = node->offset;
        attrRow->generatedOffset = true;
        appendPresentedRow(node, std::move(attrRow));
    };

    auto schemaFieldNames = [](TypeDecl *schemaDecl) {
        QStringList names;
        Type *base = BaseNode(schemaDecl ? schemaDecl->baseType : nullptr);
        if (!base || base->ty != typeSTRUCT || !base->sptr)
            return names;

        for (TypeDecl *decl : base->sptr->typeDeclList)
        {
            if (!decl)
                continue;
            for (Type *type : decl->declList)
                if (type && type->sym)
                    names.push_back(QString::fromLocal8Bit(type->sym->name));
        }
        return names;
    };

    auto sortSchemaFields = [&schemaFieldNames](StructureRow *node, TypeDecl *elementSchema) {
        if (!node || !elementSchema)
            return;

        const QStringList order = schemaFieldNames(elementSchema);
        if (order.isEmpty())
            return;

        auto orderOf = [&order](const QString &name) {
            const int index = order.indexOf(name);
            return index < 0 ? std::numeric_limits<int>::max() : index;
        };

        std::stable_sort(node->children.begin(), node->children.end(), [&orderOf](const RowPtr &left, const RowPtr &right) {
            return orderOf(left ? left->name : QString()) < orderOf(right ? right->name : QString());
        });
    };

    auto appendRequest = [this, &upsertAttr, &sortSchemaFields](const SemanticNodeRequest &request) {
        if (!request.owner)
            return;

        if (request.address == SemanticNodeAddress::Item)
        {
            const bool hasAllocatedDestination = std::any_of(
                m_semanticPositionalCollections.cbegin(),
                m_semanticPositionalCollections.cend(),
                [&request](const SemanticPositionalCollection &collection) {
                    return collection.destinationPath == request.destinationPath;
                });
            if (!hasAllocatedDestination)
                return;
        }

        uint64_t fileOffset = request.fileOffset;
        std::vector<ExprNode *> keyExprs;
        appendCommaArgs(request.keyExpr, &keyExprs);
        QStringList keyParts;
        for (ExprNode *keyExpr : keyExprs)
        {
            const QString keyPart = semanticExpressionText(request.owner,
                                                           request.owner->type,
                                                           keyExpr,
                                                           request.owner->absoluteOffset);
            if (keyPart.isEmpty())
                return;
            keyParts.push_back(keyPart);
        }

        StructureRow *parentRow = nullptr;
        QStringList entityPath = request.destinationPath;
        const bool useParentKeys = keyParts.size() > 1
            && request.destinationPath.size() >= int(keyParts.size());
        QString key = keyParts.isEmpty() ? QString()
            : useParentKeys ? keyParts.last() : keyParts.join(QChar(0x1f));

        if (useParentKeys)
        {
            const int parentKeyCount = int(keyParts.size()) - 1;
            const int parentPathLength = request.destinationPath.size() - parentKeyCount;
            if (parentPathLength <= 0)
                return;

            QStringList parentPath = request.destinationPath.mid(0, parentPathLength);
            uint64_t parentFileOffset = request.fileOffset;
            StructureRow *parentGroup = semanticDestinationForPath(parentPath,
                                                                   request.offsetSpace,
                                                                   request.logicalOffset,
                                                                   &parentFileOffset);
            if (!parentGroup)
                return;

            StructureRow *entityRow = nullptr;
            for (int i = 0; i < parentKeyCount; ++i)
            {
                entityPath = request.destinationPath.mid(0, parentPathLength + i);
                entityRow = nullptr;
                for (const SemanticEntity &entity : m_semanticEntities)
                {
                    if (entity.parent == parentGroup
                        && entity.destinationPath == entityPath
                        && entity.key == keyParts[i]
                        && entity.row)
                    {
                        entityRow = entity.row;
                        break;
                    }
                }

                if (!entityRow)
                    return;

                parentGroup = entityRow;
                if (i + 1 < parentKeyCount)
                {
                    QStringList nextPath = request.destinationPath.mid(0, parentPathLength + i + 1);
                    parentGroup = semanticChildGroup(parentGroup, nextPath);
                    if (!parentGroup)
                        return;
                }
            }

            parentRow = entityRow;
            QStringList currentPath = entityPath;
            for (int i = entityPath.size(); i < request.destinationPath.size(); ++i)
            {
                currentPath.append(request.destinationPath[i]);
                parentRow = semanticChildGroup(parentRow, currentPath);
                if (!parentRow)
                    return;
            }
        }
        else
        {
            parentRow = semanticDestinationForPath(request.destinationPath,
                                                   request.offsetSpace,
                                                   request.logicalOffset,
                                                   &fileOffset);
        }

        if (!parentRow)
            return;

        StructureRow *node = nullptr;
        if (request.address == SemanticNodeAddress::Item)
        {
            SemanticPositionalCollection *collection = nullptr;
            for (SemanticPositionalCollection &candidate : m_semanticPositionalCollections)
            {
                if (candidate.parent == parentRow && candidate.destinationPath == request.destinationPath)
                {
                    collection = &candidate;
                    break;
                }
            }
            if (!collection || !request.itemIndexExpr)
                return;

            INUMTYPE index = 0;
            if (!evaluate(request.owner, request.itemIndexExpr, &index, request.owner->absoluteOffset))
                return;

            const std::vector<StructureRow *> *rows = &collection->rows;
            if (!request.sequenceName.isEmpty())
            {
                rows = nullptr;
                for (const SemanticPositionalSequence &sequence : collection->sequences)
                {
                    if (sequence.name == request.sequenceName)
                    {
                        rows = &sequence.rows;
                        break;
                    }
                }
                if (!rows)
                    return;
            }

            if (index >= rows->size())
                return;
            node = (*rows)[static_cast<size_t>(index)];
            if (!node)
                return;
        }
        else if (!key.isEmpty())
        {
            for (const SemanticEntity &entity : m_semanticEntities)
            {
                if (entity.parent == parentRow
                    && entity.destinationPath == request.destinationPath
                    && entity.key == key
                    && entity.row)
                {
                    node = entity.row;
                    break;
                }
            }
        }

        const bool hasExplicitName = request.nameExpr != nullptr;
        QString name = semanticExpressionText(request.owner,
                                              request.owner->type,
                                              request.nameExpr,
                                              request.owner->absoluteOffset);
        if (name.isEmpty() && !keyParts.isEmpty())
            name = keyParts.last();
        if (name.isEmpty())
            name = request.destinationPath.isEmpty() ? QStringLiteral("Semantic Node") : request.destinationPath.last();
        name = rowNameFragment(name);

        TypeDecl *destinationDecl = semanticDestinationDecl(attachedSemanticSchema(m_rootType), request.destinationPath);
        TypeDecl *elementSchema = semanticDestinationElementSchema(attachedSemanticSchema(m_rootType), request.destinationPath);
        Tag *schemaPresentationTags = declarationPresentationTags(destinationDecl);

        if (!node && request.address == SemanticNodeAddress::Item)
            return;

        if (!node)
        {
            auto row = std::make_unique<StructureRow>(parentRow);
            row->kind = StructureRowKind::Semantic;
            row->suppressSemanticViews = true;
            row->name = name;
            row->nameIdentifier = name;
            if (elementSchema)
            {
                row->type = elementSchema->baseType;
                row->typeDecl = elementSchema;
            }
            if (!checkedAdd(m_baseOffset, fileOffset, &row->absoluteOffset))
                return;
            row->relativeOffset = fileOffset;
            row->offset = formatOffset(row->absoluteOffset);
            row->generatedOffset = true;
            row->byteLength = request.owner->byteLength;
            if (request.extentExpr)
            {
                INUMTYPE extent = 0;
                if (evaluate(request.owner, request.extentExpr, &extent, request.owner->absoluteOffset) && extent > 0)
                    row->byteLength = static_cast<uint64_t>(extent);
            }
            applySemanticBranchIcons(row.get(),
                                     request.destinationPath,
                                     false,
                                     semanticDestinationIsScalarArray(destinationDecl),
                                     false,
                                     false,
                                     semanticDestinationElementIsEmptyCompound(destinationDecl));

            node = row.get();
            appendPresentedRow(parentRow, std::move(row));
            if (request.address == SemanticNodeAddress::Append)
            {
                SemanticPositionalCollection *collection = nullptr;
                for (SemanticPositionalCollection &candidate : m_semanticPositionalCollections)
                {
                    if (candidate.parent == parentRow && candidate.destinationPath == request.destinationPath)
                    {
                        collection = &candidate;
                        break;
                    }
                }
                if (!collection)
                {
                    m_semanticPositionalCollections.push_back(SemanticPositionalCollection{
                        parentRow,
                        request.destinationPath
                    });
                    collection = &m_semanticPositionalCollections.back();
                }

                collection->rows.push_back(node);
                SemanticPositionalSequence *sequence = nullptr;
                for (SemanticPositionalSequence &candidate : collection->sequences)
                {
                    if (candidate.name == request.sequenceName)
                    {
                        sequence = &candidate;
                        break;
                    }
                }
                if (!sequence)
                {
                    collection->sequences.push_back(SemanticPositionalSequence{ request.sequenceName });
                    sequence = &collection->sequences.back();
                }
                sequence->rows.push_back(node);
            }
            if (!key.isEmpty())
            {
                m_semanticEntities.push_back(SemanticEntity{
                    parentRow,
                    node,
                    request.destinationPath,
                    key
                });
            }
        }
        else if (hasExplicitName && !name.isEmpty())
        {
            node->name = name;
            node->nameIdentifier = name;
        }

        if (request.extentExpr && node->byteLength == 0)
        {
            INUMTYPE extent = 0;
            if (evaluate(request.owner, request.extentExpr, &extent, request.owner->absoluteOffset) && extent > 0)
                node->byteLength = static_cast<uint64_t>(extent);
            if (!checkedAdd(m_baseOffset, fileOffset, &node->absoluteOffset))
                return;
            node->relativeOffset = fileOffset;
            node->offset = formatOffset(node->absoluteOffset);
            node->generatedOffset = true;
        }

        applyCodeTag(node, request.owner->typeDecl, request.owner);

        for (const SemanticNodeAttr &attr : request.attrs)
        {
            const QString value = semanticExpressionText(request.owner,
                                                         request.owner->type,
                                                         attr.valueExpr,
                                                         request.owner->absoluteOffset);
            if (value.isEmpty())
                continue;
            upsertAttr(node, attr.name, value);
        }

        sortSchemaFields(node, elementSchema);

        if (elementSchema && !hasExplicitName)
        {
            ExprNode *schemaNameExpr = nullptr;
            if (FindTag(schemaPresentationTags ? schemaPresentationTags : elementSchema->tagList,
                        TOK_NAME,
                        &schemaNameExpr) && schemaNameExpr)
            {
                const QString schemaName = semanticExpressionText(node,
                                                                  elementSchema->baseType,
                                                                  schemaNameExpr,
                                                                  node->absoluteOffset);
                if (!schemaName.isEmpty())
                {
                    node->name = rowNameFragment(schemaName);
                    node->nameIdentifier = node->name;
                }
            }
        }

        if (!node->children.empty())
            node->value = QStringLiteral("{...}");
    };

    // Positional collections are deliberately two-phase. All successful
    // append(...) requests allocate rows in source traversal order before any
    // item(...) contribution is applied, so an early parallel table can safely
    // contribute to rows declared later in the file.
    for (const SemanticNodeRequest &request : m_semanticNodeRequests)
        if (request.address == SemanticNodeAddress::Append)
            appendRequest(request);

    for (const SemanticNodeRequest &request : m_semanticNodeRequests)
        if (request.address == SemanticNodeAddress::Item)
            appendRequest(request);

    for (const SemanticNodeRequest &request : m_semanticNodeRequests)
        if (request.address == SemanticNodeAddress::Ordinary)
            appendRequest(request);
}

void StructureRenderEngine::appendSemanticEmitRows(StructureRow *root)
{
    if (!root || m_semanticEmitRequests.empty())
        return;

    auto appendRequest = [this](const SemanticEmitRequest &request) {
        uint64_t fileOffset = request.fileOffset;
        StructureRow *parentRow = semanticDestinationForRequest(request, &fileOffset);
        if (!parentRow || !request.owner || !request.renderType || !request.typeDecl)
            return;

        uint64_t arrayOffset = 0;
        if (!checkedAdd(m_baseOffset, fileOffset, &arrayOffset))
            return;

        auto arrayRow = makeRow(parentRow, request.renderType, request.typeDecl, arrayOffset);
        arrayRow->kind = StructureRowKind::Semantic;
        arrayRow->suppressSemanticViews = true;
        arrayRow->value = QStringLiteral("{...}");
        TypeDecl *destinationDecl = semanticDestinationDecl(attachedSemanticSchema(m_rootType), request.destinationPath);
        const bool scalarArrayDestination = semanticDestinationIsScalarArray(destinationDecl);

        QString label = semanticExpressionText(request.owner,
                                               request.owner->type,
                                               request.labelExpr,
                                               request.owner->absoluteOffset);
        const bool hasExplicitLabel = request.labelExpr != nullptr;
        if (label.isEmpty())
            label = typeName(request.renderType);

        // The destination array already names an unlabeled scalar payload.  A
        // second row named after its scalar type adds no semantic information.
        const bool elideImplicitScalarArrayRow = scalarArrayDestination
            && !hasExplicitLabel
            && !request.createsMappedContainer;
        if (!elideImplicitScalarArrayRow)
        {
            applySemanticBranchIcons(arrayRow.get(), request.destinationPath, true, scalarArrayDestination);
            arrayRow->setNameParts(QString(), label, QString());
        }

        StructureRow *elementParent = elideImplicitScalarArrayRow ? parentRow : arrayRow.get();
        if (request.createsMappedContainer)
        {
            QStringList payloadPath = request.destinationPath;
            payloadPath.append(QStringLiteral("Bytes"));
            if (semanticDestinationExists(attachedSemanticSchema(m_rootType), payloadPath))
                elementParent = semanticChildGroup(arrayRow.get(), payloadPath);
        }

        uint64_t length = 0;
        uint64_t logicalIndex = 0;
        uint64_t renderedElements = 0;
        while (logicalIndex < request.maxCount && renderedElements < kMaxArrayElements)
        {
            if (fileOffset > std::numeric_limits<uint64_t>::max() - length)
                break;
            const uint64_t relativeElementOffset = fileOffset + length;
            uint64_t elementOffset = 0;
            if (!checkedAdd(m_baseOffset, relativeElementOffset, &elementOffset))
                break;
            if (!canReadByte(elementOffset))
                break;

            auto row = makeRow(elementParent, request.renderType, request.typeDecl, elementOffset);
            row->kind = StructureRowKind::Semantic;
            row->suppressSemanticViews = true;
            applySemanticBranchIcons(row.get(),
                                     request.destinationPath,
                                     false,
                                     scalarArrayDestination,
                                     scalarArrayDestination);
            const QString indexLabel = QStringLiteral("[%1]").arg(logicalIndex);
            row->name = indexLabel;
            row->nameTypePrefix = indexLabel;

            const bool bigEndian = declarationBigEndian(request.typeDecl,
                                                        row.get(),
                                                        request.renderType,
                                                        elementOffset);
            EndianScope endian(this, bigEndian);
            row->bigEndian = m_bigEndian;
            const uint64_t elementLength = formatType(row.get(),
                                                      request.renderType,
                                                      request.typeDecl,
                                                      elementOffset);
            row->byteLength = elementLength;

            const uint64_t terminatorLength = terminatorMatchLength(row.get(),
                                                                     request.renderType,
                                                                     request.stopExpr,
                                                                     elementOffset,
                                                                     elementLength);
            const bool hideTerminator = terminatorLength > 0
                && terminatorShouldBeHidden(request.typeDecl,
                                            request.renderType,
                                            request.stopExpr,
                                            request.terminatorModeExpr);
            const uint64_t consumedLength = terminatorLength > 0 ? terminatorLength : elementLength;
            if (consumedLength == 0)
                break;

            if (!checkedAdd(length, consumedLength, &length))
                break;
            logicalIndex += std::max<uint64_t>(row->arrayCountContribution, 1);
            ++renderedElements;
            if (terminatorLength > 0)
            {
                if (!hideTerminator)
                    appendPresentedRow(elementParent, std::move(row));
                break;
            }

            appendPresentedRow(elementParent, std::move(row));
        }

        arrayRow->byteLength = length;
        if (!arrayRow->children.empty())
        {
            StructureRow *arrayRowPtr = arrayRow.get();
            appendPresentedRow(parentRow, std::move(arrayRow));
            if (request.createsMappedContainer)
            {
                m_semanticContainers.push_back(SemanticContainer{
                    arrayRowPtr,
                    request.destinationPath,
                    request.mapSpace,
                    request.mapLogicalStart,
                    request.mapLogicalSize,
                    request.mapFileOffset
                });
            }
        }
    };

    for (const SemanticEmitRequest &request : m_semanticEmitRequests)
    {
        if (request.createsMappedContainer)
            appendRequest(request);
    }

    for (const SemanticEmitRequest &request : m_semanticEmitRequests)
        if (!request.createsMappedContainer)
            appendRequest(request);
}

StructureRow *StructureRenderEngine::semanticRootGroup()
{
    if (!m_rootRow)
        return nullptr;

    const QString rootLabel = semanticRootLabel();
    for (const auto &child : m_rootRow->children)
    {
        if (child && child->kind == StructureRowKind::Semantic && child->name == rootLabel
            && child->branchIconPath != QStringLiteral(":/icons/actions/circle-repeat.svg"))
        {
            return child.get();
        }
    }

    auto group = std::make_unique<StructureRow>(m_rootRow);
    group->name = rootLabel;
    group->value = QStringLiteral("{...}");
    group->kind = StructureRowKind::Semantic;
    group->absoluteOffset = m_rootRow->absoluteOffset;
    group->relativeOffset = m_rootRow->relativeOffset;
    group->offset = formatOffset(group->absoluteOffset);
    group->generatedOffset = true;
    group->setBranchIcons(QString::fromLatin1(StructureBranchIcons::kBlueRoot),
                          QString::fromLatin1(StructureBranchIcons::kBlueRoot),
                          QString::fromLatin1(StructureBranchIcons::kGrayRoot));
    StructureRow *groupPtr = group.get();
    m_rootRow->children.push_back(std::move(group));
    return groupPtr;
}

StructureRow *StructureRenderEngine::semanticDestinationGroup(const QStringList &path)
{
    StructureRow *parent = semanticRootGroup();
    QStringList currentPath;
    for (const QString &part : path)
    {
        if (!parent || part.isEmpty())
            return nullptr;

        currentPath.append(part);
        parent = semanticChildGroup(parent, currentPath);
    }

    return parent;
}

StructureRow *StructureRenderEngine::semanticChildGroup(StructureRow *parent, const QStringList &path)
{
    const QString name = path.isEmpty() ? QString() : path.last();
    if (!parent || name.isEmpty())
        return nullptr;

    TypeDecl *schemaField = semanticDestinationDecl(attachedSemanticSchema(m_rootType), path);
    const StructureRowTreeMode treeMode = treeTag(declarationPresentationTags(schemaField));
    if (treeMode == StructureRowTreeMode::Flatten)
        return parent;
    if (treeMode == StructureRowTreeMode::Hidden)
        return nullptr;

    for (const auto &child : parent->children)
    {
        if (child && child->kind == StructureRowKind::Semantic && child->name == name)
            return child.get();
    }

    auto group = std::make_unique<StructureRow>(parent);
    group->name = name;
    group->value = QStringLiteral("{...}");
    group->kind = StructureRowKind::Semantic;
    group->treeMode = treeMode;
    group->absoluteOffset = parent->absoluteOffset;
    group->relativeOffset = parent->relativeOffset;
    group->offset = formatOffset(group->absoluteOffset);
    group->generatedOffset = true;
    // Groups are semantic organization only. Keep their owner's range so a
    // click on a group such as Bytes or Imports still selects meaningful data.
    group->byteLength = parent->byteLength;
    applySemanticBranchIcons(group.get(),
                             path,
                             false,
                             semanticDestinationIsScalarArray(schemaField),
                             false,
                             !semanticDestinationIsScalarArray(schemaField));
    StructureRow *groupPtr = group.get();

    const int newOrder = semanticDestinationOrder(path);
    auto insertAt = parent->children.end();
    if (newOrder >= 0)
    {
        for (auto it = parent->children.begin(); it != parent->children.end(); ++it)
        {
            if (!*it || (*it)->kind != StructureRowKind::Semantic)
                continue;

            QStringList siblingPath = path;
            siblingPath.last() = (*it)->name;
            const int siblingOrder = semanticDestinationOrder(siblingPath);
            if (siblingOrder >= 0 && siblingOrder > newOrder)
            {
                insertAt = it;
                break;
            }
        }
    }

    parent->children.insert(insertAt, std::move(group));
    return groupPtr;
}

QString StructureRenderEngine::semanticRootLabel() const
{
    TypeDecl *schemaDecl = attachedSemanticSchema(m_rootType);
    ExprNode *semanticExpr = nullptr;
    if (FindTag(schemaDecl ? schemaDecl->tagList : nullptr, TOK_SEMANTIC, &semanticExpr)
        && semanticExpr && semanticExpr->type == EXPR_STRINGBUF && semanticExpr->str)
    {
        const QString label = QString::fromLocal8Bit(semanticExpr->str).trimmed();
        if (!label.isEmpty())
            return label;
    }

    return QStringLiteral("Semantic");
}

bool StructureRenderEngine::dynamicTagArgs(ExprNode *expr,
                                           ExprNode **selector,
                                           ExprNode **label,
                                           ExprNode **container,
                                           ExprNode **typeName,
                                           ExprNode **logicalOffset,
                                           ExprNode **condition,
                                           DynamicMapper *mapper) const
{
    std::vector<ExprNode *> args;
    appendCommaArgs(expr, &args);

    ExprNode *selectorExpr = nullptr;
    ExprNode *labelExpr = nullptr;
    ExprNode *containerExpr = nullptr;
    ExprNode *typeExpr = nullptr;
    ExprNode *offsetExpr = nullptr;
    ExprNode *conditionExpr = nullptr;
    DynamicMapper mapperValue = DynamicMapper::Direct;

    for (ExprNode *arg : args)
    {
        ExprNode *inner = nullptr;
        const TOKEN wrapTok = unwrapTagArg(arg, &inner);
        switch (wrapTok)
        {
        case TOK_CASE:
            selectorExpr = inner;
            break;
        case TOK_NAME:
            labelExpr = inner;
            break;
        case TOK_CONTAINER:
            containerExpr = inner;
            break;
        case TOK_TYPE:
            typeExpr = inner;
            break;
        case TOK_OFFSET:
            offsetExpr = inner;
            break;
        case TOK_OPTIONAL:
            conditionExpr = inner;
            break;
        case TOK_MAPPER:
            if (!inner || !inner->str)
                return false;
            if (std::strcmp(inner->str, "direct") == 0)
                mapperValue = DynamicMapper::Direct;
            else if (std::strcmp(inner->str, "offset_map") == 0)
                mapperValue = DynamicMapper::OffsetMap;
            else
                return false;
            break;
        default:
            return false;
        }
    }

    if (!typeExpr || !offsetExpr)
        return false;

    if (selector)
        *selector = selectorExpr;
    if (label)
        *label = labelExpr;
    if (container)
        *container = containerExpr;
    if (typeName)
        *typeName = typeExpr;
    if (logicalOffset)
        *logicalOffset = offsetExpr;
    if (condition)
        *condition = conditionExpr;
    if (mapper)
        *mapper = mapperValue;
    return true;
}

bool StructureRenderEngine::dynamicArrayArgs(ExprNode *expr,
                                             ExprNode **selectorOrLabel,
                                             ExprNode **container,
                                             ExprNode **typeName,
                                             ExprNode **logicalOffset,
                                             ExprNode **count,
                                             ExprNode **stop,
                                             ExprNode **terminatorMode,
                                             ExprNode **condition,
                                             DynamicMapper *mapper,
                                             bool *isNameSource,
                                             bool *isCaseSelector) const
{
    std::vector<ExprNode *> args;
    appendCommaArgs(expr, &args);

    ExprNode *selector = nullptr;
    ExprNode *containerExpr = nullptr;
    ExprNode *typeExpr = nullptr;
    ExprNode *offsetExpr = nullptr;
    ExprNode *countExpr = nullptr;
    ExprNode *stopExpr = nullptr;
    ExprNode *terminatorModeExpr = nullptr;
    ExprNode *conditionExpr = nullptr;
    DynamicMapper mapperValue = DynamicMapper::Direct;
    bool nameSource = false;
    bool caseSelector = false;

    for (ExprNode *arg : args)
    {
        ExprNode *inner = nullptr;
        const TOKEN wrapTok = unwrapTagArg(arg, &inner);
        switch (wrapTok)
        {
        case TOK_NAME:
            selector = inner;
            nameSource = true;
            break;
        case TOK_CASE:
            selector = inner;
            caseSelector = true;
            break;
        case TOK_CONTAINER:
            containerExpr = inner;
            break;
        case TOK_TYPE:
            typeExpr = inner;
            break;
        case TOK_OFFSET:
            offsetExpr = inner;
            break;
        case TOK_COUNT:
        case TOK_MAXCOUNT:
            countExpr = inner;
            break;
        case TOK_TERMINATEDBY:
            stopExpr = inner;
            break;
        case TOK_TERMINATOR:
            terminatorModeExpr = inner;
            break;
        case TOK_OPTIONAL:
            conditionExpr = inner;
            break;
        case TOK_MAPPER:
            if (!inner || !inner->str)
                return false;
            if (std::strcmp(inner->str, "direct") == 0)
                mapperValue = DynamicMapper::Direct;
            else if (std::strcmp(inner->str, "offset_map") == 0)
                mapperValue = DynamicMapper::OffsetMap;
            else
                return false;
            break;
        default:
            return false;
        }
    }

    if (!typeExpr || !offsetExpr || !countExpr)
        return false;

    if (selectorOrLabel)
        *selectorOrLabel = selector;
    if (container)
        *container = containerExpr;
    if (isNameSource)
        *isNameSource = nameSource;
    if (isCaseSelector)
        *isCaseSelector = caseSelector;
    if (typeName)
        *typeName = typeExpr;
    if (logicalOffset)
        *logicalOffset = offsetExpr;
    if (count)
        *count = countExpr;
    if (stop)
        *stop = stopExpr;
    if (terminatorMode)
        *terminatorMode = terminatorModeExpr;
    if (condition)
        *condition = conditionExpr;
    if (mapper)
        *mapper = mapperValue;
    return true;
}

bool StructureRenderEngine::dynamicContainerArgs(ExprNode *expr, ExprNode **typeName) const
{
    std::vector<ExprNode *> args;
    appendCommaArgs(expr, &args);
    if (args.size() != 1)
        return false;

    ExprNode *inner = nullptr;
    if (unwrapTagArg(args[0], &inner) != TOK_TYPE)
        return false;

    if (typeName)
        *typeName = inner;
    return true;
}

bool StructureRenderEngine::emitArgs(ExprNode *expr,
                                     ExprNode **destination,
                                     ExprNode **selector,
                                     ExprNode **label,
                                     ExprNode **typeName,
                                     ExprNode **logicalOffset,
                                     ExprNode **count,
                                     ExprNode **stop,
                                     ExprNode **terminatorMode,
                                     ExprNode **condition,
                                     ExprNode **map) const
{
    std::vector<ExprNode *> args;
    appendCommaArgs(expr, &args);

    ExprNode *destinationExpr = nullptr;
    ExprNode *selectorExpr = nullptr;
    ExprNode *labelExpr = nullptr;
    ExprNode *typeExpr = nullptr;
    ExprNode *offsetExpr = nullptr;
    ExprNode *countExpr = nullptr;
    ExprNode *stopExpr = nullptr;
    ExprNode *terminatorModeExpr = nullptr;
    ExprNode *conditionExpr = nullptr;
    ExprNode *mapExpr = nullptr;

    for (ExprNode *arg : args)
    {
        ExprNode *inner = nullptr;
        const TOKEN wrapTok = unwrapTagArg(arg, &inner);
        switch (wrapTok)
        {
        case TOK_DEST:
            destinationExpr = inner;
            break;
        case TOK_CASE:
            selectorExpr = inner;
            break;
        case TOK_LABEL:
            labelExpr = inner;
            break;
        case TOK_TYPE:
            typeExpr = inner;
            break;
        case TOK_OFFSET:
            offsetExpr = inner;
            break;
        case TOK_COUNT:
        case TOK_MAXCOUNT:
            countExpr = inner;
            break;
        case TOK_TERMINATEDBY:
            stopExpr = inner;
            break;
        case TOK_TERMINATOR:
            terminatorModeExpr = inner;
            break;
        case TOK_OPTIONAL:
            conditionExpr = inner;
            break;
        case TOK_MAP:
            mapExpr = inner;
            break;
        default:
            return false;
        }
    }

    if (!destinationExpr || !typeExpr || !offsetExpr || !countExpr)
        return false;

    ExprNode *destinationPath = nullptr;
    if (!emitDestinationArgs(destinationExpr, &destinationPath, nullptr, nullptr))
        return false;

    if (destination)
        *destination = destinationPath;
    if (selector)
        *selector = selectorExpr;
    if (label)
        *label = labelExpr;
    if (typeName)
        *typeName = typeExpr;
    if (logicalOffset)
        *logicalOffset = offsetExpr;
    if (count)
        *count = countExpr;
    if (stop)
        *stop = stopExpr;
    if (terminatorMode)
        *terminatorMode = terminatorModeExpr;
    if (condition)
        *condition = conditionExpr;
    if (map)
        *map = mapExpr;
    return true;
}

bool StructureRenderEngine::emitRowArgs(ExprNode *expr,
                                        ExprNode **destination,
                                        ExprNode **selector,
                                        ExprNode **logicalOffset,
                                        ExprNode **condition,
                                        ExprNode **map) const
{
    std::vector<ExprNode *> args;
    appendCommaArgs(expr, &args);

    ExprNode *destinationExpr = nullptr;
    ExprNode *selectorExpr = nullptr;
    ExprNode *offsetExpr = nullptr;
    ExprNode *conditionExpr = nullptr;
    ExprNode *mapExpr = nullptr;

    for (ExprNode *arg : args)
    {
        ExprNode *inner = nullptr;
        const TOKEN wrapTok = unwrapTagArg(arg, &inner);
        switch (wrapTok)
        {
        case TOK_DEST:
            destinationExpr = inner;
            break;
        case TOK_CASE:
            selectorExpr = inner;
            break;
        case TOK_OFFSET:
            offsetExpr = inner;
            break;
        case TOK_OPTIONAL:
            conditionExpr = inner;
            break;
        case TOK_MAP:
            mapExpr = inner;
            break;
        default:
            return false;
        }
    }

    if (!destinationExpr)
        return false;

    if (destination)
        *destination = destinationExpr;
    if (selector)
        *selector = selectorExpr;
    if (logicalOffset)
        *logicalOffset = offsetExpr;
    if (condition)
        *condition = conditionExpr;
    if (map)
        *map = mapExpr;
    return true;
}

bool StructureRenderEngine::emitNodeArgs(ExprNode *expr,
                                         ExprNode **destination,
                                         ExprNode **selector,
                                         ExprNode **name,
                                         ExprNode **logicalOffset,
                                         ExprNode **extent,
                                         ExprNode **condition,
                                         std::vector<SemanticNodeAttr> *attrs) const
{
    std::vector<ExprNode *> args;
    appendCommaArgs(expr, &args);

    ExprNode *destinationExpr = nullptr;
    ExprNode *selectorExpr = nullptr;
    ExprNode *nameExpr = nullptr;
    ExprNode *offsetExpr = nullptr;
    ExprNode *extentExpr = nullptr;
    ExprNode *conditionExpr = nullptr;
    std::vector<SemanticNodeAttr> attrList;

    for (ExprNode *arg : args)
    {
        ExprNode *inner = nullptr;
        const TOKEN wrapTok = unwrapTagArg(arg, &inner);
        switch (wrapTok)
        {
        case TOK_DEST:
            destinationExpr = inner;
            break;
        case TOK_CASE:
            selectorExpr = inner;
            break;
        case TOK_NAME:
            nameExpr = inner;
            break;
        case TOK_OFFSET:
            offsetExpr = inner;
            break;
        case TOK_EXTENT:
            extentExpr = inner;
            break;
        case TOK_OPTIONAL:
            conditionExpr = inner;
            break;
        case TOK_ATTR:
        case TOK_FIELD:
        {
            QString attrName;
            ExprNode *attrValue = nullptr;
            if (!semanticAttrArgs(inner, &attrName, &attrValue))
                return false;
            attrList.push_back(SemanticNodeAttr{ attrName, attrValue, wrapTok == TOK_FIELD });
            break;
        }
        default:
            return false;
        }
    }

    if (!destinationExpr)
        return false;

    if (destination)
        *destination = destinationExpr;
    if (selector)
        *selector = selectorExpr;
    if (name)
        *name = nameExpr;
    if (logicalOffset)
        *logicalOffset = offsetExpr;
    if (extent)
        *extent = extentExpr;
    if (condition)
        *condition = conditionExpr;
    if (attrs)
        *attrs = std::move(attrList);
    return true;
}

bool StructureRenderEngine::emitDestinationArgs(ExprNode *expr,
                                                ExprNode **path,
                                                ExprNode **key,
                                                ExprNode **name,
                                                ExprNode **append,
                                                ExprNode **item) const
{
    std::vector<ExprNode *> args;
    appendCommaArgs(expr, &args);
    if (args.empty())
        return false;

    ExprNode *pathExpr = args[0];
    ExprNode *unusedInner = nullptr;
    if (unwrapTagArg(pathExpr, &unusedInner) != TOK_NULL)
        return false;

    ExprNode *keyExpr = nullptr;
    ExprNode *nameExpr = nullptr;
    ExprNode *appendExpr = nullptr;
    ExprNode *itemExpr = nullptr;
    for (size_t i = 1; i < args.size(); ++i)
    {
        ExprNode *inner = nullptr;
        const TOKEN wrapTok = unwrapTagArg(args[i], &inner);
        switch (wrapTok)
        {
        case TOK_KEY:
            keyExpr = inner;
            break;
        case TOK_NAME:
            nameExpr = inner;
            break;
        case TOK_APPEND:
            if (appendExpr)
                return false;
            appendExpr = inner;
            break;
        case TOK_ITEM:
            if (itemExpr)
                return false;
            itemExpr = inner;
            break;
        default:
            return false;
        }
    }

    QString sequence;
    ExprNode *itemIndexExpr = nullptr;
    if ((appendExpr && !semanticAppendArgs(appendExpr, &sequence))
        || (itemExpr && !semanticItemArgs(itemExpr, &sequence, &itemIndexExpr))
        || (appendExpr && itemExpr)
        || (keyExpr && (appendExpr || itemExpr)))
    {
        return false;
    }

    if ((keyExpr && !key) || (nameExpr && !name)
        || (appendExpr && !append) || (itemExpr && !item))
        return false;

    if (path)
        *path = pathExpr;
    if (key)
        *key = keyExpr;
    if (name)
        *name = nameExpr;
    if (append)
        *append = appendExpr;
    if (item)
        *item = itemExpr;
    return true;
}

bool StructureRenderEngine::semanticAppendArgs(ExprNode *expr, QString *sequence) const
{
    std::vector<ExprNode *> args;
    appendCommaArgs(expr, &args);
    if (args.size() != 1 || !sequence)
        return false;

    ExprNode *nameExpr = args[0];
    if (!nameExpr || nameExpr->type != EXPR_STRINGBUF || !nameExpr->str || nameExpr->str[0] == '\0')
        return false;

    *sequence = QString::fromUtf8(nameExpr->str);
    return !sequence->isEmpty();
}

bool StructureRenderEngine::semanticItemArgs(ExprNode *expr, QString *sequence, ExprNode **index) const
{
    std::vector<ExprNode *> args;
    appendCommaArgs(expr, &args);
    if ((args.size() != 1 && args.size() != 2) || !sequence || !index)
        return false;

    sequence->clear();
    ExprNode *indexExpr = args[0];
    if (args.size() == 2)
    {
        ExprNode *nameExpr = args[0];
        if (!nameExpr || nameExpr->type != EXPR_STRINGBUF || !nameExpr->str || nameExpr->str[0] == '\0')
            return false;
        *sequence = QString::fromUtf8(nameExpr->str);
        if (sequence->isEmpty())
            return false;
        indexExpr = args[1];
    }

    if (!indexExpr
        || indexExpr->type == EXPR_NULL
        || indexExpr->type == EXPR_STRINGBUF
        || indexExpr->type == EXPR_BYTESEQ
        || indexExpr->type == EXPR_TAGWRAP)
    {
        return false;
    }

    *index = indexExpr;
    return true;
}

bool StructureRenderEngine::semanticAttrArgs(ExprNode *expr, QString *name, ExprNode **value) const
{
    std::vector<ExprNode *> args;
    appendCommaArgs(expr, &args);
    if (args.size() != 2 || !name || !value)
        return false;

    ExprNode *nameExpr = args[0];
    QString attrName;
    if (nameExpr && nameExpr->type == EXPR_IDENTIFIER && nameExpr->str)
        attrName = QString::fromLocal8Bit(nameExpr->str);
    else if (nameExpr && nameExpr->type == EXPR_STRINGBUF && nameExpr->str)
        attrName = QString::fromUtf8(nameExpr->str);

    if (attrName.isEmpty())
        return false;

    *name = attrName;
    *value = args[1];
    return true;
}

bool StructureRenderEngine::emitMapArgs(ExprNode *expr,
                                        QString *name,
                                        ExprNode **logicalStart,
                                        ExprNode **logicalSize,
                                        ExprNode **fileOffset) const
{
    std::vector<ExprNode *> args;
    appendCommaArgs(expr, &args);
    if (args.size() != 4 || !args[0] || args[0]->type != EXPR_STRINGBUF || !args[0]->str)
        return false;

    if (name)
        *name = QString::fromLocal8Bit(args[0]->str);
    if (logicalStart)
        *logicalStart = args[1];
    if (logicalSize)
        *logicalSize = args[2];
    if (fileOffset)
        *fileOffset = args[3];
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
    if (args[0] && args[0]->type == EXPR_STRINGBUF)
        return false;

    if (logicalStart)
        *logicalStart = args[0];
    if (logicalSize)
        *logicalSize = args[1];
    if (fileOffset)
        *fileOffset = args[2];
    return true;
}

bool StructureRenderEngine::namedOffsetMapArgs(ExprNode *expr,
                                               QString *name,
                                               ExprNode **base,
                                               ExprNode **logicalStart,
                                               ExprNode **logicalSize,
                                               ExprNode **fileOffset) const
{
    std::vector<ExprNode *> args;
    appendCommaArgs(expr, &args);
    if ((args.size() != 2 && args.size() != 4)
        || !args[0]
        || args[0]->type != EXPR_STRINGBUF
        || !args[0]->str)
    {
        return false;
    }

    if (name)
        *name = QString::fromLocal8Bit(args[0]->str);

    if (args.size() == 2)
    {
        if (base)
            *base = args[1];
        if (logicalStart)
            *logicalStart = nullptr;
        if (logicalSize)
            *logicalSize = nullptr;
        if (fileOffset)
            *fileOffset = nullptr;
        return true;
    }

    if (base)
        *base = nullptr;
    if (logicalStart)
        *logicalStart = args[1];
    if (logicalSize)
        *logicalSize = args[2];
    if (fileOffset)
        *fileOffset = args[3];
    return true;
}

bool StructureRenderEngine::offsetTagArgs(ExprNode *expr, QString *space, ExprNode **offsetExpr) const
{
    std::vector<ExprNode *> args;
    appendCommaArgs(expr, &args);
    if (args.size() == 1)
    {
        if (space)
            space->clear();
        if (offsetExpr)
            *offsetExpr = args[0];
        return true;
    }

    if (args.size() == 2 && args[0] && args[0]->type == EXPR_STRINGBUF && args[0]->str)
    {
        if (space)
            *space = QString::fromLocal8Bit(args[0]->str);
        if (offsetExpr)
            *offsetExpr = args[1];
        return true;
    }

    return false;
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

TypeDecl *StructureRenderEngine::referencedTypeDecl(Type *type) const
{
    for (Type *cursor = type; cursor; cursor = cursor->link)
    {
        if ((cursor->ty == typeTYPEDEF || cursor->ty == typeIDENTIFIER) && cursor->sym)
            if (TypeDecl *decl = findTypeDecl(cursor->sym->name))
                return decl;
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

TypeDecl *StructureRenderEngine::attachedSemanticSchema(TypeDecl *rootType) const
{
    ExprNode *semanticExpr = nullptr;
    if (!FindTag(rootType ? rootType->tagList : nullptr, TOK_SEMANTIC, &semanticExpr) || !semanticExpr)
        return nullptr;

    if (semanticExpr->type != EXPR_IDENTIFIER || !semanticExpr->str)
        return nullptr;

    TypeDecl *schemaDecl = findTypeDecl(semanticExpr->str);
    ExprNode *schemaMarker = nullptr;
    if (!schemaDecl
        || !FindTag(schemaDecl->tagList, TOK_SEMANTIC, &schemaMarker)
        || (schemaMarker && schemaMarker->type != EXPR_STRINGBUF))
        return nullptr;

    return schemaDecl;
}

QStringList StructureRenderEngine::semanticPath(ExprNode *expr) const
{
    if (!expr)
        return {};

    if (expr->type == EXPR_IDENTIFIER && expr->str)
        return { QString::fromLocal8Bit(expr->str) };

    if (expr->type != EXPR_FIELD)
        return {};

    QStringList path = semanticPath(expr->left);
    QStringList tail = semanticPath(expr->right);
    if (path.isEmpty() || tail.isEmpty())
        return {};

    path.append(tail);
    return path;
}

TypeDecl *StructureRenderEngine::semanticDestinationDecl(TypeDecl *schemaDecl, const QStringList &path) const
{
    if (!schemaDecl || path.isEmpty())
        return nullptr;

    Type *scope = schemaDecl->baseType;
    for (int i = 0; i < path.size(); ++i)
    {
        Type *base = BaseNode(scope);
        if (!base || base->ty != typeSTRUCT || !base->sptr)
            return nullptr;

        TypeDecl *matched = nullptr;
        const QByteArray name = path[i].toLocal8Bit();
        for (TypeDecl *decl : base->sptr->typeDeclList)
        {
            if (!decl)
                continue;
            for (Type *type : decl->declList)
            {
                if (type && type->sym && std::strcmp(type->sym->name, name.constData()) == 0)
                {
                    matched = decl;
                    break;
                }
            }
            if (matched)
                break;
        }

        if (!matched)
            return nullptr;

        if (i == path.size() - 1)
            return matched;

        scope = matched->declList.empty() ? matched->baseType : matched->declList[0];
    }

    return nullptr;
}

TypeDecl *StructureRenderEngine::semanticDestinationElementSchema(TypeDecl *schemaDecl, const QStringList &path) const
{
    TypeDecl *destinationDecl = semanticDestinationDecl(schemaDecl, path);
    if (!destinationDecl)
        return nullptr;

    Type *type = destinationDecl->declList.empty() ? destinationDecl->baseType : destinationDecl->declList[0];
    for (Type *cursor = type; cursor; cursor = cursor->link)
    {
        if (cursor->ty == typeARRAY)
        {
            type = cursor->link;
            break;
        }
    }

    for (Type *cursor = type; cursor; cursor = cursor->link)
    {
        if ((cursor->ty == typeSTRUCT || cursor->ty == typeUNION)
            && cursor->sptr
            && cursor->sptr->semanticSchema)
        {
            return destinationDecl;
        }

        if ((cursor->ty != typeTYPEDEF && cursor->ty != typeIDENTIFIER) || !cursor->sym)
            continue;

        TypeDecl *candidate = findTypeDecl(cursor->sym->name);
        ExprNode *semanticExpr = nullptr;
        if (candidate && FindTag(candidate->tagList, TOK_SEMANTIC, &semanticExpr))
            return candidate;
    }

    return nullptr;
}

bool StructureRenderEngine::semanticTypeIsCompound(Type *type) const
{
    for (Type *cursor = type; cursor; cursor = cursor->link)
    {
        if (cursor->ty == typeSTRUCT || cursor->ty == typeUNION)
            return true;

        if ((cursor->ty != typeTYPEDEF && cursor->ty != typeIDENTIFIER) || !cursor->sym)
            continue;

        TypeDecl *candidate = findTypeDecl(cursor->sym->name);
        Type *base = BaseNode(candidate ? candidate->baseType : nullptr);
        if (base && (base->ty == typeSTRUCT || base->ty == typeUNION))
            return true;
    }

    return false;
}

bool StructureRenderEngine::semanticDestinationIsScalarArray(TypeDecl *destinationDecl) const
{
    if (!destinationDecl)
        return false;

    Type *type = destinationDecl->declList.empty() ? destinationDecl->baseType : destinationDecl->declList[0];
    for (Type *cursor = type; cursor; cursor = cursor->link)
    {
        if (cursor->ty != typeARRAY)
            continue;

        Type *elementType = cursor->link;
        return !semanticTypeIsCompound(elementType);
    }

    return false;
}

bool StructureRenderEngine::semanticDestinationIsArray(TypeDecl *destinationDecl) const
{
    if (!destinationDecl)
        return false;

    Type *type = destinationDecl->declList.empty() ? destinationDecl->baseType : destinationDecl->declList[0];
    for (Type *cursor = type; cursor; cursor = cursor->link)
        if (cursor->ty == typeARRAY)
            return true;

    return false;
}

bool StructureRenderEngine::semanticDestinationElementIsEmptyCompound(TypeDecl *destinationDecl) const
{
    if (!destinationDecl)
        return false;

    Type *type = destinationDecl->declList.empty() ? destinationDecl->baseType : destinationDecl->declList[0];
    for (Type *cursor = type; cursor; cursor = cursor->link)
    {
        if (cursor->ty == typeARRAY)
        {
            type = cursor->link;
            break;
        }
    }

    Type *base = BaseNode(type);
    return base && (base->ty == typeSTRUCT || base->ty == typeUNION)
        && base->sptr && base->sptr->typeDeclList.empty();
}

bool StructureRenderEngine::semanticSchemaHasField(TypeDecl *schemaDecl, const QString &name) const
{
    if (!schemaDecl || name.isEmpty())
        return false;

    Type *base = BaseNode(schemaDecl->baseType);
    if (!base || base->ty != typeSTRUCT || !base->sptr)
        return false;

    const QByteArray fieldName = name.toLocal8Bit();
    for (TypeDecl *decl : base->sptr->typeDeclList)
    {
        if (!decl)
            continue;
        for (Type *type : decl->declList)
            if (type && type->sym && std::strcmp(type->sym->name, fieldName.constData()) == 0)
                return true;
    }

    return false;
}

bool StructureRenderEngine::semanticDestinationExists(TypeDecl *schemaDecl, const QStringList &path) const
{
    return semanticDestinationDecl(schemaDecl, path) != nullptr;
}

int StructureRenderEngine::semanticDestinationOrder(const QStringList &path) const
{
    TypeDecl *schemaDecl = attachedSemanticSchema(m_rootType);
    if (!schemaDecl || path.isEmpty())
        return -1;

    Type *scope = schemaDecl->baseType;
    for (int depth = 0; depth < path.size(); ++depth)
    {
        Type *base = BaseNode(scope);
        if (!base || base->ty != typeSTRUCT || !base->sptr)
            return -1;

        TypeDecl *matched = nullptr;
        const QByteArray name = path[depth].toLocal8Bit();
        int order = 0;
        for (TypeDecl *decl : base->sptr->typeDeclList)
        {
            if (!decl)
            {
                ++order;
                continue;
            }

            bool matches = false;
            for (Type *type : decl->declList)
            {
                if (type && type->sym && std::strcmp(type->sym->name, name.constData()) == 0)
                {
                    matches = true;
                    break;
                }
            }

            if (matches)
            {
                matched = decl;
                break;
            }

            ++order;
        }

        if (!matched)
            return -1;

        if (depth == path.size() - 1)
            return order;

        scope = matched->declList.empty() ? matched->baseType : matched->declList[0];
    }

    return -1;
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
            uint64_t logicalEnd = 0;
            if (!checkedAdd(map.logicalStart, map.logicalSize, &logicalEnd))
                logicalEnd = std::numeric_limits<uint64_t>::max();

            if (logicalOffset < map.logicalStart || logicalOffset >= logicalEnd)
                continue;

            if (!checkedAdd(map.fileOffset, logicalOffset - map.logicalStart, fileOffset))
                return nullptr;
            return &container;
        }
    }

    return nullptr;
}

bool StructureRenderEngine::mapNamedOffset(const QString &name, uint64_t logicalOffset, uint64_t *fileOffset,
                                           uint64_t *mappedLength) const
{
    if (name.isEmpty() || !fileOffset)
        return false;

    for (auto it = m_namedOffsetMaps.rbegin(); it != m_namedOffsetMaps.rend(); ++it)
    {
        if (it->name != name)
            continue;

        if (!it->rangeMapped)
        {
            if (!checkedAdd(it->fileOffset, logicalOffset, fileOffset))
                return false;
            if (mappedLength)
                *mappedLength = std::numeric_limits<uint64_t>::max();
            return true;
        }

        uint64_t logicalEnd = 0;
        if (!checkedAdd(it->logicalStart, it->logicalSize, &logicalEnd))
            logicalEnd = std::numeric_limits<uint64_t>::max();

        if (logicalOffset < it->logicalStart || logicalOffset >= logicalEnd)
            continue;

        if (!checkedAdd(it->fileOffset, logicalOffset - it->logicalStart, fileOffset))
            return false;
        if (mappedLength)
            *mappedLength = it->logicalSize - (logicalOffset - it->logicalStart);
        return true;
    }

    return false;
}

StructureRow *StructureRenderEngine::semanticDestinationForPath(const QStringList &destinationPath,
                                                                const QString &offsetSpace,
                                                                uint64_t logicalOffset,
                                                                uint64_t *fileOffset)
{
    if (!fileOffset)
        return nullptr;

    // A source row with an explicit offset space is located by its logical
    // address.  Otherwise its existing file extent is sufficient to select a
    // mapped semantic container (for example, an inline table within a PE
    // section).  Prefer the most specific matching container in either case.
    if (destinationPath.size() > 1)
    {
        const SemanticContainer *bestContainer = nullptr;
        for (const SemanticContainer &container : m_semanticContainers)
        {
            if (!container.row || (!offsetSpace.isEmpty() && container.mapSpace != offsetSpace))
                continue;
            if (container.destinationPath.size() >= destinationPath.size())
                continue;

            bool prefixMatches = true;
            for (int i = 0; i < container.destinationPath.size(); ++i)
            {
                if (container.destinationPath[i] != destinationPath[i])
                {
                    prefixMatches = false;
                    break;
                }
            }
            if (!prefixMatches)
                continue;

            const bool containsSource = offsetSpace.isEmpty()
                ? ([this, &container, fileOffset]() {
                       uint64_t end = 0;
                       if (!checkedAdd(container.fileOffset, container.logicalSize, &end))
                           end = std::numeric_limits<uint64_t>::max();
                       return *fileOffset >= container.fileOffset && *fileOffset < end;
                   })()
                : (logicalOffset >= container.logicalStart
                   && [this, &container, logicalOffset]() {
                       uint64_t end = 0;
                       if (!checkedAdd(container.logicalStart, container.logicalSize, &end))
                           end = std::numeric_limits<uint64_t>::max();
                       return logicalOffset < end;
                   }());
            if (!containsSource)
                continue;

            if (!bestContainer || container.destinationPath.size() > bestContainer->destinationPath.size())
                bestContainer = &container;
        }

        if (bestContainer)
        {
            if (!offsetSpace.isEmpty())
                if (!checkedAdd(bestContainer->fileOffset,
                                logicalOffset - bestContainer->logicalStart,
                                fileOffset))
                    return nullptr;

            StructureRow *parent = bestContainer->row;
            QStringList currentPath = bestContainer->destinationPath;
            for (int i = bestContainer->destinationPath.size(); i < destinationPath.size(); ++i)
            {
                currentPath.append(destinationPath[i]);
                parent = semanticChildGroup(parent, currentPath);
            }
            return parent;
        }
    }

    if (!offsetSpace.isEmpty())
    {
        uint64_t mapped = 0;
        if (mapNamedOffset(offsetSpace, logicalOffset, &mapped))
            *fileOffset = mapped;
    }

    return semanticDestinationGroup(destinationPath);
}

StructureRow *StructureRenderEngine::semanticDestinationForRequest(const SemanticEmitRequest &request,
                                                                   uint64_t *fileOffset)
{
    return semanticDestinationForPath(request.destinationPath,
                                      request.offsetSpace,
                                      request.logicalOffset,
                                      fileOffset);
}

QString StructureRenderEngine::semanticExpressionText(StructureRow *scope,
                                                      Type *scopeType,
                                                      ExprNode *expr,
                                                      uint64_t scopeOffset)
{
    if (!expr)
        return {};

    QString stringValue;
    if (evaluateString(EvalContext{ scope, scopeType, scopeOffset }, expr, &stringValue))
        return rowNameFragment(stringValue);

    QString text = fieldNameValue(scope, scopeType, expr, scopeOffset);
    if (!text.isEmpty())
        return rowNameFragment(text);

    if (scope
        && scope->kind == StructureRowKind::Semantic
        && (expr->type == EXPR_IDENTIFIER || expr->type == EXPR_FIELD || expr->type == EXPR_ARRAY || expr->type == EXPR_SCOPE))
    {
        return {};
    }

    INUMTYPE value = 0;
    if (evaluate(scope, expr, &value, scopeOffset))
        return QString::number(value);

    return {};
}

TYPE StructureRenderEngine::scalarTypeName(const char *name) const
{
    if (!name)
        return typeNULL;

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
    };

    for (const BuiltIn &builtIn : builtIns)
        if (std::strcmp(name, builtIn.name) == 0)
            return builtIn.type;

    return typeNULL;
}

StructureRow *StructureRenderEngine::dynamicRootGroup(const QString &label)
{
    if (!m_rootRow || label.isEmpty())
        return nullptr;

    for (const auto &child : m_rootRow->children)
    {
        if (child && child->kind == StructureRowKind::Dynamic && child->name == label)
            return child.get();
    }

    auto group = std::make_unique<StructureRow>(m_rootRow);
    group->name = label;
    group->value = QStringLiteral("{...}");
    group->kind = StructureRowKind::Dynamic;
    group->absoluteOffset = m_rootRow->absoluteOffset;
    group->relativeOffset = m_rootRow->relativeOffset;
    group->offset = formatOffset(group->absoluteOffset);
    group->generatedOffset = true;
    group->setBranchIcons(QString::fromLatin1(StructureBranchIcons::kBlueStructure),
                          QString::fromLatin1(StructureBranchIcons::kBlueStructureOpen),
                          QString::fromLatin1(StructureBranchIcons::kGrayStructure));
    StructureRow *groupPtr = group.get();
    m_rootRow->children.push_back(std::move(group));
    return groupPtr;
}

void StructureRenderEngine::resolveEntryPointRows(StructureRow *row)
{
    if (!row)
        return;

    // Some code tags (notably ELF entry points) refer to named maps declared
    // later in the file. Re-evaluate after raw rendering has collected every
    // map; applyCodeTag is deliberately a no-op until all of its inputs exist.
    applyCodeTag(row, row->typeDecl, row);

    // [entrypoint] stores an unresolved logical address and is mapped after
    // dynamic containers have been collected. [code(...)] already resolved
    // its explicit offset (including named offset spaces), so keep it intact.
    if (row->hasCodeTarget && row->codeArchitecture.isEmpty())
    {
        uint64_t mappedOffset = 0;
        if (mapLogicalOffset(row->codeLogicalOffset, &mappedOffset))
        {
            if (!checkedAdd(m_baseOffset, mappedOffset, &row->codeTargetOffset))
                row->hasCodeTarget = false;
        }
        else if (!checkedAdd(m_baseOffset, row->codeLogicalOffset, &row->codeTargetOffset))
        {
            row->hasCodeTarget = false;
        }
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
    uint64_t length = fallback;

    INUMTYPE value = 0;
    if (FindTag(typeDecl ? typeDecl->tagList : nullptr, TOK_EXTENT, &expr) && expr
        && evaluate(EvalContext{ scope, scopeType, scopeOffset }, expr, &value)
        && value > 0)
    {
        length = static_cast<uint64_t>(value);
    }

    expr = nullptr;
    if (!FindTag(typeDecl ? typeDecl->tagList : nullptr, TOK_PADTO, &expr) || !expr)
        return length;

    value = 0;
    if (!evaluate(EvalContext{ scope, scopeType, scopeOffset }, expr, &value) || value <= 1)
        return length;

    if (scopeOffset > std::numeric_limits<uint64_t>::max() - length)
        return length;

    const uint64_t alignedEnd = alignedOffset(scopeOffset + length, static_cast<uint64_t>(value));
    return alignedEnd >= scopeOffset ? alignedEnd - scopeOffset : length;
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

Bitfield *StructureRenderEngine::tagValueBitfield(StructureRow *row, TypeDecl *typeDecl) const
{
    ExprNode *expr = nullptr;
    if (!m_library || !effectiveTag(row, typeDecl, TOK_BITFIELD, &expr) || !expr || !expr->str)
        return nullptr;

    for (Bitfield *bitfield : m_library->globalBitfieldList)
        if (bitfield && std::strcmp(bitfield->name, expr->str) == 0)
            return bitfield;

    return nullptr;
}

Enum *StructureRenderEngine::enumForName(const char *name) const
{
    if (!m_library || !name)
        return nullptr;

    if (Symbol *sym = LookupSymbol(m_library->globalTagSymbolList, name))
        if (sym->type && sym->type->ty == typeENUM)
            return sym->type->eptr;

    if (Symbol *sym = LookupSymbol(m_library->globalIdentifierList, name))
    {
        Type *base = BaseNode(sym->type);
        if (base && base->ty == typeENUM)
            return base->eptr;
    }

    return nullptr;
}

Enum *StructureRenderEngine::tagValueEnum(StructureRow *row, TypeDecl *typeDecl, TOKEN tagTok) const
{
    ExprNode *expr = nullptr;
    if (!m_library || !effectiveTag(row, typeDecl, tagTok, &expr) || !expr || !expr->str)
        return nullptr;

    return enumForName(expr->str);
}

Enum *StructureRenderEngine::tagEnum(StructureRow *row, TypeDecl *typeDecl) const
{
    return tagValueEnum(row, typeDecl, TOK_ENUM);
}

EnumField *StructureRenderEngine::enumFieldForRowValue(StructureRow *row, Enum *eptr) const
{
    if (!row)
        return nullptr;

    if (eptr)
    {
        for (EnumField *field : eptr->fieldList)
            if (field && field->val == row->scalarRawValue)
                return field;
    }

    // Scalar rows can be produced from a typedef-expanded declaration, where
    // the original [enum(...)] field declaration is not retained on the row.
    // The rendered enum label still resolves to the enum-value symbol, which
    // owns the same metadata.
    if (m_library && !row->value.isEmpty())
    {
        const QByteArray enumName = row->value.toLocal8Bit();
        if (Symbol *symbol = LookupSymbol(m_library->globalIdentifierList, enumName.constData()))
            if (symbol->type && symbol->type->ty == typeENUMVALUE)
                return symbol->type->evptr;
    }

    return nullptr;
}

QString StructureRenderEngine::enumValueStringMetadata(StructureRow *scope, ExprNode *fieldExpr, TOKEN tagTok)
{
    StructureRow *fieldRow = findFieldRow(scope, fieldExpr);
    if (!fieldRow && scope && scope->parent)
        fieldRow = findFieldRow(scope->parent, fieldExpr);
    if (!fieldRow)
        return {};

    EnumField *enumValue = enumFieldForRowValue(fieldRow, tagEnum(fieldRow, fieldRow->typeDecl));
    ExprNode *metadataExpr = nullptr;
    if (!enumValue || !FindTag(enumValue->tagList, tagTok, &metadataExpr)
        || !metadataExpr || metadataExpr->type != EXPR_STRINGBUF || !metadataExpr->str)
    {
        return {};
    }

    return QString::fromLocal8Bit(metadataExpr->str).trimmed().toLower();
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

void StructureRenderEngine::applyBitflagTag(StructureRow *row,
                                            Type *type,
                                            TypeDecl *typeDecl,
                                            uint64_t rawValue,
                                            uint64_t byteLength)
{
    if (!row)
        return;

    Enum *flags = tagValueEnum(row, typeDecl, TOK_BITFLAG);
    if (!flags)
        return;

    QStringList activeLabels;
    uint64_t coveredBits = 0;
    for (EnumField *field : flags->fieldList)
    {
        if (!field || !field->name || !field->name->name)
            continue;

        const uint64_t mask = static_cast<uint64_t>(field->val);
        const bool active = mask == 0 ? rawValue == 0 : (rawValue & mask) == mask;
        if (!active)
            continue;

        const QString label = QString::fromLocal8Bit(field->name->name);
        activeLabels.push_back(label);
        coveredBits |= mask;

        auto child = makeRow(row, type, typeDecl, row->absoluteOffset);
        child->name = label;
        child->value = formatStructureIntegerValue(mask, byteLength, false, QString(), m_options);
        child->byteLength = byteLength;
        row->children.push_back(std::move(child));
    }

    const uint64_t unknownBits = rawValue & ~coveredBits;
    if (unknownBits != 0)
    {
        activeLabels.push_back(QStringLiteral("Unknown bits"));

        auto child = makeRow(row, type, typeDecl, row->absoluteOffset);
        child->name = QStringLiteral("Unknown bits");
        child->value = formatStructureIntegerValue(unknownBits, byteLength, false, QString(), m_options);
        child->byteLength = byteLength;
        row->children.push_back(std::move(child));
    }

    row->valueKind = StructureRowValueKind::Custom;
    row->value = activeLabels.isEmpty()
        ? formatStructureIntegerValue(rawValue, byteLength, false, QString(), m_options)
        : activeLabels.join(QStringLiteral(" | "));
}

namespace
{
bool isContiguousMask(uint64_t mask)
{
    if (mask == 0)
        return false;

    while ((mask & 1) == 0)
        mask >>= 1;

    return (mask & (mask + 1)) == 0;
}

uint64_t maskShift(uint64_t mask)
{
    uint64_t shift = 0;
    while (mask && (mask & 1) == 0)
    {
        ++shift;
        mask >>= 1;
    }
    return shift;
}

QString bitfieldEntryName(BitfieldEntry *entry)
{
    if (entry && entry->displayName)
        return QString::fromLocal8Bit(entry->displayName);
    return entry && entry->inferredName ? QString::fromLocal8Bit(entry->inferredName) : QString();
}

} // namespace

void StructureRenderEngine::applyBitfieldTag(StructureRow *row,
                                             Type *type,
                                             TypeDecl *typeDecl,
                                             uint64_t rawValue,
                                             uint64_t byteLength)
{
    if (!row)
        return;

    Bitfield *bitfield = tagValueBitfield(row, typeDecl);
    if (!bitfield)
        return;

    QStringList activeLabels;
    uint64_t coveredBits = 0;
    bool hasMatchEntries = false;

    for (BitfieldEntry *entry : bitfield->entries)
    {
        if (!entry || entry->maskValue < 0)
            continue;

        const uint64_t mask = static_cast<uint64_t>(entry->maskValue);
        coveredBits |= mask;
        const uint64_t extractedField = isContiguousMask(mask)
            ? ((rawValue & mask) >> maskShift(mask))
            : (rawValue & mask);
        const bool matched = (rawValue & mask) == static_cast<uint64_t>(entry->matchValue);
        const uint64_t displayValue = entry->kind == bitfieldMATCH ? (matched ? 1 : 0) : extractedField;

        QString name = bitfieldEntryName(entry);
        if (name.isEmpty())
            continue;

        if (entry->kind == bitfieldMATCH)
        {
            hasMatchEntries = true;
            if (matched)
                activeLabels.push_back(name);
        }

        auto child = makeRow(row, type, typeDecl, row->absoluteOffset);
        child->name = name;
        child->comment.clear();
        child->byteLength = byteLength;
        child->valueKind = StructureRowValueKind::ScalarInteger;
        child->scalarRawValue = displayValue;
        child->scalarByteLength = byteLength;
        child->scalarSigned = false;
        child->scalarCharacterSuffix.clear();

        Enum *displayEnum = entry->kind == bitfieldFIELD ? enumForName(entry->valueEnumName) : nullptr;

        if (displayEnum)
        {
            const QString enumName = enumNameForValue(displayEnum, static_cast<INUMTYPE>(displayValue));
            child->value = enumName.isEmpty()
                ? formatStructureIntegerValue(displayValue, byteLength, false, QString(), m_options)
                : enumName;
            child->valueChoices = enumChoiceLabels(displayEnum);
        }
        else
        {
            child->value = formatStructureIntegerValue(displayValue, byteLength, false, QString(), m_options);
        }

        row->children.push_back(std::move(child));
    }

    if (hasMatchEntries)
    {
        const uint64_t unknownBits = rawValue & ~coveredBits;
        if (unknownBits != 0)
            activeLabels.push_front(QStringLiteral("Unknown bits"));

        row->valueKind = StructureRowValueKind::Custom;
        row->value = activeLabels.isEmpty()
            ? formatStructureIntegerValue(rawValue, byteLength, false, QString(), m_options)
            : activeLabels.join(QStringLiteral(" | "));
    }
}

bool StructureRenderEngine::applyFormatTag(StructureRow *row, TypeDecl *typeDecl, uint64_t byteLength)
{
    ExprNode *formatExpr = nullptr;
    effectiveTag(row, typeDecl, TOK_FORMAT, &formatExpr);
    const FormatTagInfo formatInfo = formatTagInfoFromExpr(formatExpr);
    const StrataFormat format = formatInfo.format;
    if (!row)
        return false;

    if (format == StrataFormat::Hex
        || format == StrataFormat::Dec
        || format == StrataFormat::Binary
        || format == StrataFormat::Timestamp)
    {
        if (row->valueKind != StructureRowValueKind::ScalarInteger)
            return false;

        row->valueKind = StructureRowValueKind::Custom;
        if (format == StrataFormat::Timestamp)
        {
            row->value = formatTimestampValue(formatInfo.timestamp,
                                              row->scalarRawValue,
                                              row->scalarByteLength);
            if (row->value.isEmpty())
                return false;
        }
        else if (format == StrataFormat::Binary)
        {
            int width = 8;
            if (formatInfo.widthExpr)
            {
                INUMTYPE evaluatedWidth = 0;
                if (evaluate(row, formatInfo.widthExpr, &evaluatedWidth, row->absoluteOffset) && evaluatedWidth > 0)
                    width = int(std::min<INUMTYPE>(evaluatedWidth, 64));
            }
            row->value = QString::number(row->scalarRawValue, 2).rightJustified(width, QLatin1Char('0'));
            if (!row->scalarCharacterSuffix.isEmpty())
                row->value += row->scalarCharacterSuffix;
        }
        else if (format == StrataFormat::Hex)
        {
            int width = qMax(1, int(row->scalarByteLength * 2));
            if (formatInfo.widthExpr)
            {
                INUMTYPE evaluatedWidth = 0;
                if (evaluate(row, formatInfo.widthExpr, &evaluatedWidth, row->absoluteOffset) && evaluatedWidth > 0)
                    width = int(std::min<INUMTYPE>(evaluatedWidth, 64));
            }
            row->value = QString::number(row->scalarRawValue, 16).toUpper().rightJustified(width, QLatin1Char('0'));
            if (!row->scalarCharacterSuffix.isEmpty())
                row->value += row->scalarCharacterSuffix;
        }
        else
        {
            StructureDisplayOptions options = m_options;
            options.hexadecimalValues = false;
            row->value = formatStructureIntegerValue(row->scalarRawValue,
                                                     row->scalarByteLength,
                                                     row->scalarSigned,
                                                     row->scalarCharacterSuffix,
                                                     options);
        }
        return true;
    }

    if (format != StrataFormat::FourCc || byteLength != 4)
        return false;

    QByteArray bytes(4, Qt::Uninitialized);
    const size_t got = m_reader ? m_reader(row->absoluteOffset,
                                           reinterpret_cast<uint8_t *>(bytes.data()),
                                           static_cast<size_t>(bytes.size()))
                                : 0;
    if (got != 4)
        return false;

    QString text;
    text.reserve(4);
    for (char byte : bytes)
    {
        const uchar ch = uchar(byte);
        text.append(ch >= 0x20 && ch <= 0x7e ? QChar(QLatin1Char(char(ch))) : QChar(QLatin1Char('.')));
    }

    row->valueKind = StructureRowValueKind::Custom;
    row->value = quoteString(text);
    return true;
}

void StructureRenderEngine::applyTreeTag(StructureRow *row, TypeDecl *typeDecl) const
{
    if (!row)
        return;
    row->treeMode = treeTag(row->tagListOverride ? row->tagListOverride : (typeDecl ? typeDecl->tagList : nullptr));
}

void StructureRenderEngine::appendPresentedRow(StructureRow *parent, RowPtr row) const
{
    if (!parent || !row)
        return;

    switch (row->treeMode)
    {
    case StructureRowTreeMode::Hidden:
        return;
    case StructureRowTreeMode::Flatten:
        for (auto &child : row->children)
        {
            child->parent = parent;
            parent->children.push_back(std::move(child));
        }
        row->children.clear();
        return;
    case StructureRowTreeMode::Default:
    case StructureRowTreeMode::Collapsed:
    case StructureRowTreeMode::Expanded:
        parent->children.push_back(std::move(row));
        return;
    }
}

void StructureRenderEngine::applyEntryPointTag(StructureRow *row, TypeDecl *typeDecl)
{
    if (!row || !effectiveTag(row, typeDecl, TOK_ENTRYPOINT, nullptr))
        return;

    if (row->valueKind != StructureRowValueKind::ScalarInteger)
        return;

    row->hasCodeTarget = true;
    row->codeLogicalOffset = static_cast<uint64_t>(row->scalarRawValue);
    if (!checkedAdd(m_baseOffset, row->codeLogicalOffset, &row->codeTargetOffset))
        row->hasCodeTarget = false;
}

bool StructureRenderEngine::codeTagArgs(ExprNode *expr, QString *architecture, ExprNode **architectureField,
                                         QString *offsetSpace, ExprNode **offset, ExprNode **extent) const
{
    std::vector<ExprNode *> args;
    appendCommaArgs(expr, &args);
    QString arch;
    ExprNode *architectureExpr = nullptr;
    QString space;
    ExprNode *offsetExpr = nullptr;
    ExprNode *extentExpr = nullptr;
    for (ExprNode *arg : args)
    {
        if (arg && arg->type == EXPR_STRINGBUF && arg->str)
        {
            arch = QString::fromLocal8Bit(arg->str).trimmed().toLower();
            continue;
        }
        ExprNode *inner = nullptr;
        switch (unwrapTagArg(arg, &inner))
        {
        case TOK_ARCHITECTURE: architectureExpr = inner; break;
        case TOK_OFFSET:
        {
            std::vector<ExprNode *> offsetArgs;
            appendCommaArgs(inner, &offsetArgs);
            if (offsetArgs.size() == 1)
                offsetExpr = offsetArgs[0];
            else if (offsetArgs.size() == 2 && offsetArgs[0]->type == EXPR_STRINGBUF && offsetArgs[0]->str)
            {
                space = QString::fromLocal8Bit(offsetArgs[0]->str);
                offsetExpr = offsetArgs[1];
            }
            else
                return false;
            break;
        }
        case TOK_EXTENT:
            extentExpr = inner;
            break;
        default: return false;
        }
    }

    if (arch.isEmpty() && !architectureExpr)
        return false;

    if (architecture)
        *architecture = arch;
    if (architectureField)
        *architectureField = architectureExpr;
    if (offsetSpace)
        *offsetSpace = space;
    if (offset)
        *offset = offsetExpr;
    if (extent)
        *extent = extentExpr;
    return true;
}

void StructureRenderEngine::applyCodeTag(StructureRow *target, TypeDecl *typeDecl, StructureRow *scope)
{
    ExprNode *expr = nullptr;
    if (!target || !scope)
        return;

    if (!effectiveTag(target, typeDecl, TOK_CODE, &expr))
    {
        for (Type *type = scope->type; type; type = type->link)
        {
            if (type->ty != typeIDENTIFIER || !type->sym)
                continue;
            TypeDecl *decl = findTypeDecl(type->sym->name);
            if (FindTag(decl ? decl->tagList : nullptr, TOK_CODE, &expr))
                break;
        }
        if (!expr)
            return;
    }

    QString architecture;
    ExprNode *architectureField = nullptr;
    QString offsetSpace;
    ExprNode *offsetExpr = nullptr;
    ExprNode *extentExpr = nullptr;
    if (!codeTagArgs(expr, &architecture, &architectureField, &offsetSpace, &offsetExpr, &extentExpr))
        return;

    if (architectureField)
    {
        StructureRow *architectureRow = findFieldRow(scope, architectureField);
        if (!architectureRow && scope->parent)
            architectureRow = findFieldRow(scope->parent, architectureField);
        Enum *architectureEnum = architectureRow ? tagEnum(architectureRow, architectureRow->typeDecl) : nullptr;
        EnumField *architectureValue = nullptr;
        if (architectureEnum && architectureRow)
            for (EnumField *field : architectureEnum->fieldList)
                if (field && field->val == architectureRow->scalarRawValue)
                    architectureValue = field;
        // Scalar rows can be produced from a typedef-expanded declaration,
        // where the original [enum(...)] field declaration is not retained on
        // the row. The rendered enum label still resolves to the enum-value
        // symbol, which owns the same metadata.
        if (!architectureValue && m_library && architectureRow && !architectureRow->value.isEmpty())
        {
            const QByteArray enumName = architectureRow->value.toLocal8Bit();
            if (Symbol *symbol = LookupSymbol(m_library->globalIdentifierList, enumName.constData()))
                if (symbol->type && symbol->type->ty == typeENUMVALUE)
                    architectureValue = symbol->type->evptr;
        }
        ExprNode *architectureExpr = nullptr;
        if (!architectureValue || !FindTag(architectureValue->tagList, TOK_ARCHITECTURE, &architectureExpr)
            || !architectureExpr || architectureExpr->type != EXPR_STRINGBUF || !architectureExpr->str)
            return;
        architecture = QString::fromLocal8Bit(architectureExpr->str).trimmed().toLower();
    }

    uint64_t absoluteOffset = scope->absoluteOffset;
    uint64_t mappedLength = std::numeric_limits<uint64_t>::max();
    uint64_t codeLogicalOffset = absoluteOffset >= m_baseOffset ? absoluteOffset - m_baseOffset : 0;
    StructureRow *codeRow = nullptr;
    if (offsetExpr)
    {
        if (!offsetSpace.isEmpty())
        {
            INUMTYPE logicalOffset = 0;
            if (!evaluate(scope, offsetExpr, &logicalOffset, scope->absoluteOffset)
                || logicalOffset < 0
                || !mapNamedOffset(offsetSpace, static_cast<uint64_t>(logicalOffset), &absoluteOffset,
                                   &mappedLength))
            {
                return;
            }
            codeLogicalOffset = static_cast<uint64_t>(logicalOffset);
        }
        else
        {
        codeRow = findFieldRow(scope, offsetExpr);
        if (codeRow)
            absoluteOffset = codeRow->absoluteOffset;
        else
        {
            ResolvedField field;
            if (resolveField(scope->type, offsetExpr, scope->absoluteOffset, &field))
                absoluteOffset = field.offset;
            else
            {
                INUMTYPE logicalOffset = 0;
                if (!evaluate(scope, offsetExpr, &logicalOffset, scope->absoluteOffset)
                    || logicalOffset < 0
                    || !checkedAdd(m_baseOffset, static_cast<uint64_t>(logicalOffset), &absoluteOffset))
                {
                    return;
                }
            }
        }
        }
    }

    uint64_t length = target->byteLength;
    if (extentExpr)
    {
        INUMTYPE extent = 0;
        if (evaluate(scope, extentExpr, &extent, scope->absoluteOffset) && extent > 0)
            length = static_cast<uint64_t>(extent);
        else if (codeRow)
            length = codeRow->byteLength;
        else
            return;
    }
    if (length == 0)
        return;

    // A named range map is also the authoritative physical extent of mapped
    // code. Keep a defensive explicit extent (such as 64 KiB) within the
    // remaining section/segment rather than allowing it to spill into the
    // next mapping.
    length = std::min(length, mappedLength);
    if (length == 0)
        return;

    target->hasCodeTarget = true;
    target->codeArchitecture = architecture;
    target->codeTargetOffset = absoluteOffset;
    target->codeLogicalOffset = codeLogicalOffset;
    target->codeByteLength = length;

    // A raw function record has several useful selectable rows (the body,
    // locals and instruction byte array). They all describe the same code
    // range, so make the context-menu action available from any of them.
    if (target->kind != StructureRowKind::Semantic)
    {
        const auto inheritCodeTarget = [&architecture, absoluteOffset, length](auto &&self, StructureRow *row) -> void {
            for (const auto &child : row->children)
            {
                if (!child)
                    continue;
                child->hasCodeTarget = true;
                child->codeArchitecture = architecture;
                child->codeTargetOffset = absoluteOffset;
                child->codeLogicalOffset = absoluteOffset >= row->absoluteOffset ? absoluteOffset - row->absoluteOffset : 0;
                child->codeByteLength = length;
                self(self, child.get());
            }
        };
        inheritCodeTarget(inheritCodeTarget, target);
    }
}

bool StructureRenderEngine::diagnosticTagArgs(ExprNode *expr, ExprNode **condition, QString *message) const
{
    if (condition)
        *condition = nullptr;
    if (message)
        message->clear();

    std::vector<ExprNode *> args;
    appendCommaArgs(expr, &args);
    if (args.empty() || args.size() > 2 || !args[0])
        return false;

    if (condition)
        *condition = args[0];

    if (args.size() == 2)
    {
        if (!args[1] || args[1]->type != EXPR_STRINGBUF || !args[1]->str)
            return false;
        if (message)
            *message = QString::fromUtf8(args[1]->str);
    }

    return true;
}

bool StructureRenderEngine::evaluateDiagnosticCondition(StructureRow *scope,
                                                        ExprNode *condition,
                                                        INUMTYPE *result)
{
    if (!condition || !result)
        return false;

    const uint64_t offset = scope ? scope->absoluteOffset : m_baseOffset;
    if (evaluate(scope, condition, result, offset))
        return true;

    if (scope && scope->parent && evaluate(scope->parent, condition, result, scope->parent->absoluteOffset))
        return true;

    return false;
}

QString StructureRenderEngine::diagnosticExpressionText(ExprNode *expr) const
{
    if (!expr)
        return QString();

    std::array<char, 1024> buffer = {};
    Flatten(buffer.data(), buffer.size(), expr);
    return QString::fromLocal8Bit(buffer.data()).trimmed();
}

void StructureRenderEngine::addDiagnostic(StructureRow *target,
                                          StructureRowDiagnosticSeverity severity,
                                          const QString &message)
{
    if (!target || severity == StructureRowDiagnosticSeverity::None || message.isEmpty())
        return;

    target->diagnostics.push_back(StructureRowDiagnostic{ severity, message });
    if (target->comment.isEmpty())
    {
        target->comment = message;
    }
    else
    {
        target->comment += QStringLiteral("; ");
        target->comment += message;
    }
}

void StructureRenderEngine::applyDiagnosticTags(StructureRow *target, TypeDecl *typeDecl, StructureRow *scope)
{
    if (!target || !scope)
        return;

    const auto apply = [&](TOKEN token) {
        ExprNode *expr = nullptr;
        if (!effectiveTag(target, typeDecl, token, &expr) || !expr)
            return;

        ExprNode *condition = nullptr;
        QString message;
        if (!diagnosticTagArgs(expr, &condition, &message) || !condition)
            return;

        INUMTYPE value = 0;
        if (!evaluateDiagnosticCondition(scope, condition, &value))
            return;

        const bool failed = token == TOK_ASSERT ? value == 0 : value != 0;
        if (!failed)
            return;

        if (message.isEmpty())
        {
            const QString expression = diagnosticExpressionText(condition);
            message = token == TOK_ASSERT
                ? QStringLiteral("assertion failed: %1").arg(expression)
                : QStringLiteral("warning: %1").arg(expression);
        }

        addDiagnostic(target,
                      token == TOK_ASSERT ? StructureRowDiagnosticSeverity::Error
                                          : StructureRowDiagnosticSeverity::Warning,
                      message);
    };

    apply(TOK_WARN);
    apply(TOK_ASSERT);
}

bool StructureRenderEngine::openAsTagArgs(ExprNode *expr,
                                          ExprNode **typeName,
                                          QString *offsetSpace,
                                          ExprNode **offset,
                                          ExprNode **extent,
                                          ExprNode **name,
                                          QString *transform,
                                          ExprNode **transformAlgorithmField,
                                          ExprNode **condition) const
{
    std::vector<ExprNode *> args;
    appendCommaArgs(expr, &args);

    ExprNode *typeExpr = nullptr;
    QString space;
    ExprNode *offsetExpr = nullptr;
    ExprNode *extentExpr = nullptr;
    ExprNode *nameExpr = nullptr;
    ExprNode *conditionExpr = nullptr;
    ExprNode *algorithmFieldExpr = nullptr;
    QString transformName;

    for (ExprNode *arg : args)
    {
        ExprNode *inner = nullptr;
        switch (unwrapTagArg(arg, &inner))
        {
        case TOK_TYPE:
            typeExpr = inner;
            break;
        case TOK_OFFSET:
            if (!offsetTagArgs(inner, &space, &offsetExpr))
                return false;
            break;
        case TOK_EXTENT:
            extentExpr = inner;
            break;
        case TOK_NAME:
            nameExpr = inner;
            break;
        case TOK_OPTIONAL:
            conditionExpr = inner;
            break;
        case TOK_TRANSFORM:
            {
                ExprNode *algorithmInner = nullptr;
                if (unwrapTagArg(inner, &algorithmInner) == TOK_ALGORITHM)
                {
                    algorithmFieldExpr = algorithmInner;
                    break;
                }
                if (!inner || inner->type != EXPR_STRINGBUF || !inner->str)
                    return false;
                if (!normalizeOpenAsTransformName(QString::fromLocal8Bit(inner->str), &transformName))
                    return false;
                break;
            }
        default:
            return false;
        }
    }

    if (!typeExpr || typeExpr->type != EXPR_IDENTIFIER || !typeExpr->str || !offsetExpr || !extentExpr)
        return false;

    if (typeName)
        *typeName = typeExpr;
    if (offsetSpace)
        *offsetSpace = space;
    if (offset)
        *offset = offsetExpr;
    if (extent)
        *extent = extentExpr;
    if (name)
        *name = nameExpr;
    if (transform)
        *transform = transformName;
    if (transformAlgorithmField)
        *transformAlgorithmField = algorithmFieldExpr;
    if (condition)
        *condition = conditionExpr;
    return true;
}

void StructureRenderEngine::applyOpenAsTag(StructureRow *target, TypeDecl *typeDecl, StructureRow *scope)
{
    ExprNode *expr = nullptr;
    if (!target || !scope)
        return;

    if (!effectiveTag(target, typeDecl, TOK_OPENAS, &expr))
    {
        for (Type *type = scope->type; type; type = type->link)
        {
            if (type->ty != typeIDENTIFIER || !type->sym)
                continue;
            TypeDecl *decl = findTypeDecl(type->sym->name);
            if (FindTag(decl ? decl->tagList : nullptr, TOK_OPENAS, &expr))
                break;
        }
        if (!expr)
            return;
    }

    ExprNode *typeExpr = nullptr;
    QString offsetSpace;
    ExprNode *offsetExpr = nullptr;
    ExprNode *extentExpr = nullptr;
    ExprNode *nameExpr = nullptr;
    ExprNode *transformAlgorithmField = nullptr;
    ExprNode *conditionExpr = nullptr;
    QString transform;
    if (!openAsTagArgs(expr, &typeExpr, &offsetSpace, &offsetExpr, &extentExpr, &nameExpr, &transform,
                       &transformAlgorithmField, &conditionExpr))
        return;

    if (conditionExpr)
    {
        INUMTYPE includeTarget = 0;
        if (!evaluate(scope, conditionExpr, &includeTarget, scope->absoluteOffset) || includeTarget == 0)
            return;
    }

    if (!typeExpr || !typeExpr->str)
        return;

    if (transformAlgorithmField)
    {
        const QString algorithmName = enumValueStringMetadata(scope, transformAlgorithmField, TOK_ALGORITHM);
        if (algorithmName.isEmpty() || !normalizeOpenAsTransformName(algorithmName, &transform))
            return;
    }

    const QString rootTypeName = QString::fromLocal8Bit(typeExpr->str);
    const bool autoDetectRoot = rootTypeName.compare(QStringLiteral("auto"), Qt::CaseInsensitive) == 0;
    TypeDecl *rootType = autoDetectRoot ? nullptr : findTypeDecl(typeExpr->str);
    if (!rootType && !autoDetectRoot)
        return;

    uint64_t absoluteOffset = 0;
    if (!offsetSpace.isEmpty())
    {
        INUMTYPE logicalOffset = 0;
        if (!evaluate(scope, offsetExpr, &logicalOffset, scope->absoluteOffset)
            || logicalOffset < 0
            || !mapNamedOffset(offsetSpace, static_cast<uint64_t>(logicalOffset), &absoluteOffset))
        {
            return;
        }
    }
    else
    {
        INUMTYPE logicalOffset = 0;
        if (!evaluate(scope, offsetExpr, &logicalOffset, scope->absoluteOffset) || logicalOffset < 0)
            return;
        if (!checkedAdd(m_baseOffset, static_cast<uint64_t>(logicalOffset), &absoluteOffset))
            return;
    }

    INUMTYPE extent = 0;
    if (!evaluate(scope, extentExpr, &extent, scope->absoluteOffset) || extent <= 0)
        return;
    uint64_t byteLength = static_cast<uint64_t>(extent);
    const uint64_t readable = readableEnd(absoluteOffset);
    if (readable <= absoluteOffset)
        return;
    byteLength = std::min(byteLength, readable - absoluteOffset);
    if (byteLength == 0)
        return;

    QString name;
    if (nameExpr)
        name = semanticExpressionText(scope, scope->type, nameExpr, scope->absoluteOffset);
    if (name.trimmed().isEmpty())
        name = rootTypeName;

    target->hasOpenAsTarget = true;
    target->openAsRootType = rootType;
    target->openAsRootTypeName = rootTypeName;
    target->openAsName = name.trimmed();
    target->openAsTransform = transform;
    target->openAsOffset = absoluteOffset;
    target->openAsByteLength = byteLength;
    if (!transform.isEmpty())
    {
        target->branchIconPath = QString::fromLatin1(StructureBranchIcons::kBlueNested);
        target->branchOpenIconPath = target->branchIconPath;
        target->branchEmptyIconPath = QString::fromLatin1(StructureBranchIcons::kGrayNested);
    }
}

void StructureRenderEngine::linkWasmFunctionCodeTargets(StructureRow *root)
{
    if (!root)
        return;

    TypeDecl *definedFunctionDecl = findTypeDecl("WASM_DEFINED_FUNCTION");
    TypeDecl *codeEntryDecl = findTypeDecl("WASM_CODE_ENTRY");
    if (!definedFunctionDecl || !codeEntryDecl)
        return;

    std::vector<StructureRow *> definedFunctions;
    std::vector<StructureRow *> codeEntries;
    const auto collect = [&](const auto &self, StructureRow *row) -> void {
        if (!row)
            return;

        if (row->typeDecl == definedFunctionDecl)
            definedFunctions.push_back(row);
        if (row->typeDecl == codeEntryDecl
            && row->hasCodeTarget
            && row->codeArchitecture == QStringLiteral("wasm"))
        {
            codeEntries.push_back(row);
        }

        for (const RowPtr &child : row->children)
            self(self, child.get());
    };
    collect(collect, root);

    const auto copyTarget = [](const auto &self, StructureRow *target, const StructureRow *source) -> void {
        target->hasCodeTarget = true;
        target->codeArchitecture = source->codeArchitecture;
        target->codeTargetOffset = source->codeTargetOffset;
        target->codeLogicalOffset = source->codeLogicalOffset;
        target->codeByteLength = source->codeByteLength;
        for (const RowPtr &child : target->children)
            self(self, child.get(), source);
    };

    const size_t count = std::min(definedFunctions.size(), codeEntries.size());
    for (size_t index = 0; index < count; ++index)
        copyTarget(copyTarget, definedFunctions[index], codeEntries[index]);
}

void StructureRenderEngine::linkWasmSemanticFunctionCodeTargets(StructureRow *root)
{
    if (!root)
        return;

    TypeDecl *codeEntryDecl = findTypeDecl("WASM_CODE_ENTRY");
    if (!codeEntryDecl)
        return;

    std::vector<StructureRow *> codeEntries;
    StructureRow *functions = nullptr;
    const auto collect = [&](const auto &self, StructureRow *row) -> void {
        if (!row)
            return;

        if (row->typeDecl == codeEntryDecl
            && row->hasCodeTarget
            && row->codeArchitecture == QStringLiteral("wasm"))
        {
            codeEntries.push_back(row);
        }
        if (row->kind == StructureRowKind::Semantic && row->name == QStringLiteral("Functions"))
            functions = row;

        for (const RowPtr &child : row->children)
            self(self, child.get());
    };
    collect(collect, root);
    if (!functions || codeEntries.empty())
        return;

    const auto hasCodeSize = [](const StructureRow *row) {
        return std::any_of(row->children.cbegin(), row->children.cend(), [](const RowPtr &child) {
            return child && child->kind == StructureRowKind::Semantic && child->name == QStringLiteral("CodeSize");
        });
    };
    const auto copyTarget = [](const auto &self, StructureRow *target, const StructureRow *source) -> void {
        target->hasCodeTarget = true;
        target->codeArchitecture = source->codeArchitecture;
        target->codeTargetOffset = source->codeTargetOffset;
        target->codeLogicalOffset = source->codeLogicalOffset;
        target->codeByteLength = source->codeByteLength;
        for (const RowPtr &child : target->children)
            self(self, child.get(), source);
    };

    size_t codeIndex = 0;
    for (const RowPtr &child : functions->children)
    {
        if (!child || !hasCodeSize(child.get()) || codeIndex >= codeEntries.size())
            continue;
        copyTarget(copyTarget, child.get(), codeEntries[codeIndex++]);
    }
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

    if (arrayType->link && arrayType->link->ty == typeARRAY)
        return {};

    Type *elementType = BaseNode(arrayType->link);
    const StrataFormat format = formatTag(typeDecl);
    const bool explicitString = FindTag(typeDecl ? typeDecl->tagList : nullptr, TOK_STRING, nullptr)
        || format == StrataFormat::String;
    const bool explicitUtf16 = format == StrataFormat::Utf16
        || format == StrataFormat::Utf16Le
        || format == StrataFormat::Utf16Be;
    const bool explicitGuid = format == StrataFormat::Guid || format == StrataFormat::Uuid;
    if (!elementType
        || (elementType->ty != typeCHAR
            && elementType->ty != typeWCHAR
            && !(explicitString && elementType->ty == typeBYTE)
            && !(explicitUtf16 && elementType->ty == typeBYTE)
            && !(explicitGuid && elementType->ty == typeBYTE)))
    {
        return {};
    }

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

    if (explicitGuid)
    {
        const QString uuid = formatUuidBytes(bytes, format == StrataFormat::Guid);
        return uuid.isEmpty() ? QString() : quoteString(uuid);
    }

    if (explicitUtf16)
    {
        const bool wideBigEndian = format == StrataFormat::Utf16Be
            || (format == StrataFormat::Utf16 && m_bigEndian);
        EndianScope endian(this, wideBigEndian);
        return quoteString(decodeNulTerminatedText(bytes, true));
    }

    return quoteString(decodeNulTerminatedText(bytes, elementType->ty == typeWCHAR));
}

QString StructureRenderEngine::dynamicArrayStringValue(Type *elementType,
                                                       TypeDecl *typeDecl,
                                                       uint64_t offset,
                                                       uint64_t byteLength,
                                                       bool bigEndian)
{
    Type *base = BaseNode(elementType);
    if (!base || byteLength == 0)
        return QString();

    const StrataFormat format = formatTag(typeDecl);
    const bool explicitString = FindTag(typeDecl ? typeDecl->tagList : nullptr, TOK_STRING, nullptr)
        || format == StrataFormat::String;
    const bool explicitUtf16 = format == StrataFormat::Utf16
        || format == StrataFormat::Utf16Le
        || format == StrataFormat::Utf16Be;

    if (base->ty != typeCHAR
        && base->ty != typeWCHAR
        && !(explicitString && base->ty == typeBYTE)
        && !(explicitUtf16 && base->ty == typeBYTE))
    {
        return QString();
    }

    const uint64_t cappedLength = std::min<uint64_t>(byteLength, kMaxStringLookupBytes);
    QByteArray bytes(static_cast<int>(cappedLength), Qt::Uninitialized);
    const size_t got = m_reader ? m_reader(offset,
                                           reinterpret_cast<uint8_t *>(bytes.data()),
                                           static_cast<size_t>(bytes.size()))
                                : 0;
    if (got == 0)
        return QString();
    bytes.truncate(static_cast<int>(got));

    if (explicitUtf16)
    {
        const bool wideBigEndian = format == StrataFormat::Utf16Be
            || (format == StrataFormat::Utf16 && bigEndian);
        EndianScope endian(this, wideBigEndian);
        return quoteString(decodeNulTerminatedText(bytes, true));
    }

    EndianScope endian(this, bigEndian);
    return quoteString(decodeNulTerminatedText(bytes, base->ty == typeWCHAR));
}

QString StructureRenderEngine::fieldStringValue(StructureRow *row)
{
    if (!row || row->byteLength == 0)
        return {};

    Type *arrayType = nullptr;
    for (Type *cursor = row->type; cursor; cursor = cursor->link)
    {
        if (cursor->ty == typeARRAY)
        {
            arrayType = cursor;
            break;
        }
    }

    if (!arrayType || (arrayType->link && arrayType->link->ty == typeARRAY))
        return {};

    Type *elementType = BaseNode(arrayType->link);
    ExprNode *formatExpr = nullptr;
    effectiveTag(row, row->typeDecl, TOK_FORMAT, &formatExpr);
    const StrataFormat format = formatTagInfoFromExpr(formatExpr).format;
    const bool explicitString = effectiveTag(row, row->typeDecl, TOK_STRING, nullptr)
        || format == StrataFormat::String;
    const bool explicitUtf16 = format == StrataFormat::Utf16
        || format == StrataFormat::Utf16Le
        || format == StrataFormat::Utf16Be;
    if (!elementType
        || (elementType->ty != typeCHAR
            && elementType->ty != typeWCHAR
            && !(explicitString && elementType->ty == typeBYTE)
            && !(explicitUtf16 && elementType->ty == typeBYTE)))
    {
        return {};
    }

    const uint64_t cappedLength = std::min<uint64_t>(row->byteLength, kMaxStringLookupBytes);
    QByteArray bytes(static_cast<int>(cappedLength), Qt::Uninitialized);
    const size_t got = m_reader ? m_reader(row->absoluteOffset,
                                           reinterpret_cast<uint8_t *>(bytes.data()),
                                           static_cast<size_t>(bytes.size()))
                                : 0;
    if (got == 0)
        return {};

    bytes.truncate(static_cast<int>(got));
    if (explicitUtf16)
    {
        const bool wideBigEndian = format == StrataFormat::Utf16Be
            || (format == StrataFormat::Utf16 && row->bigEndian);
        EndianScope endian(this, wideBigEndian);
        return decodeNulTerminatedText(bytes, true);
    }
    return decodeNulTerminatedText(bytes, elementType->ty == typeWCHAR);
}

QString decodeModifiedUtf8(const QByteArray &bytes)
{
    QString text;
    for (int i = 0; i < bytes.size();)
    {
        const uchar b0 = uchar(bytes[i]);
        if (b0 < 0x80)
        {
            text.append(QChar(ushort(b0)));
            ++i;
            continue;
        }

        if ((b0 & 0xe0) == 0xc0 && i + 1 < bytes.size())
        {
            const uchar b1 = uchar(bytes[i + 1]);
            if ((b1 & 0xc0) == 0x80)
            {
                const ushort ch = ushort(((b0 & 0x1f) << 6) | (b1 & 0x3f));
                text.append(QChar(ch));
                i += 2;
                continue;
            }
        }

        if ((b0 & 0xf0) == 0xe0 && i + 2 < bytes.size())
        {
            const uchar b1 = uchar(bytes[i + 1]);
            const uchar b2 = uchar(bytes[i + 2]);
            if ((b1 & 0xc0) == 0x80 && (b2 & 0xc0) == 0x80)
            {
                const ushort ch = ushort(((b0 & 0x0f) << 12) | ((b1 & 0x3f) << 6) | (b2 & 0x3f));
                text.append(QChar(ch));
                i += 3;
                continue;
            }
        }

        const char *start = bytes.constData() + i;
        const QString fallback = QString::fromUtf8(start, bytes.size() - i);
        text.append(fallback);
        break;
    }
    return text;
}

// Shared by stringArrayValue() and dynamicArrayNameString(): truncate raw bytes
// at the first NUL (8- or 16-bit, depending on element width) and decode the rest.
QString StructureRenderEngine::decodeNulTerminatedText(const QByteArray &bytes, bool wide) const
{
    QString text;
    if (!wide)
    {
        const int nul = bytes.indexOf('\0');
        text = decodeModifiedUtf8(nul >= 0 ? bytes.left(nul) : bytes);
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
    return text;
}

// Resolve the display text for a dynamic_array tag explicitly marked as a name
// source via name(...) on its label argument (e.g. dynamic_array(name(DllName),
// type(CHAR), ...) -- see the nameSourceTagExpr scan in
// appendDynamicArrayRows()). The referenced field is an RVA/offset, not inline
// data, so this re-derives the same offset + read that the dynamic_array would
// use for its own child row, without requiring that lazily-built child to
// exist yet.
QString StructureRenderEngine::dynamicArrayNameString(StructureRow *elementRow, ExprNode *dynamicArrayTagExpr)
{
    if (!elementRow || !dynamicArrayTagExpr)
        return {};

    ExprNode *typeNameExpr = nullptr;
    ExprNode *logicalOffsetExpr = nullptr;
    ExprNode *countExpr = nullptr;
    DynamicMapper mapper = DynamicMapper::Direct;
    if (!dynamicArrayArgs(dynamicArrayTagExpr, nullptr, nullptr, &typeNameExpr, &logicalOffsetExpr,
                          &countExpr, nullptr, nullptr, nullptr, &mapper, nullptr))
        return {};

    Type *renderType = typeNameExpr && typeNameExpr->str
        ? typeInDecl(findTypeDecl(typeNameExpr->str), typeNameExpr->str)
        : nullptr;
    Type *base = BaseNode(renderType);
    if (!base || (base->ty != typeCHAR && base->ty != typeWCHAR))
        return {};

    QString offsetSpace;
    ExprNode *offsetValueExpr = logicalOffsetExpr;
    if (!offsetTagArgs(logicalOffsetExpr, &offsetSpace, &offsetValueExpr))
        return {};

    INUMTYPE logicalOffset = 0;
    INUMTYPE count = 0;
    if (!evaluate(elementRow, offsetValueExpr, &logicalOffset, elementRow->absoluteOffset)
        || !evaluate(elementRow, countExpr, &count, elementRow->absoluteOffset)
        || logicalOffset < 0
        || count <= 0)
        return {};

    uint64_t fileOffset = uint64_t(logicalOffset);
    if (!offsetSpace.isEmpty())
    {
        if (mapper != DynamicMapper::Direct || !mapNamedOffset(offsetSpace, uint64_t(logicalOffset), &fileOffset))
            return {};
    }
    else if (mapper == DynamicMapper::OffsetMap && !mapLogicalOffset(uint64_t(logicalOffset), &fileOffset))
    {
        return {};
    }

    const uint64_t unitSize = base->ty == typeWCHAR ? 2 : 1;
    // Cap independently of kMaxArrayElements: that constant bounds how many
    // *rows* a real array build creates, not how many bytes a single name
    // lookup may read — names are short, so a generous fixed cap is enough.
    const uint64_t boundedCount = std::min<uint64_t>(uint64_t(count), 260);
    QByteArray bytes(static_cast<int>(boundedCount * unitSize), Qt::Uninitialized);
    uint64_t absoluteOffset = 0;
    if (!checkedAdd(m_baseOffset, fileOffset, &absoluteOffset))
        return {};

    const size_t got = m_reader ? m_reader(absoluteOffset,
                                           reinterpret_cast<uint8_t *>(bytes.data()),
                                           static_cast<size_t>(bytes.size()))
                                : 0;
    bytes.truncate(static_cast<int>(got));

    return decodeNulTerminatedText(bytes, base->ty == typeWCHAR);
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

uint64_t StructureRenderEngine::terminatorMatchLength(StructureRow *row,
                                                      Type *elementType,
                                                      ExprNode *stopExpr,
                                                      uint64_t offset,
                                                      uint64_t elementLength)
{
    if (!row || !elementType || !stopExpr)
        return 0;

    if (stopExpr->type == EXPR_BYTESEQ)
    {
        if (stopExpr->byteSequence.empty())
            return 0;

        QByteArray bytes(static_cast<int>(stopExpr->byteSequence.size()), Qt::Uninitialized);
        const size_t got = m_reader ? m_reader(offset,
                                               reinterpret_cast<uint8_t *>(bytes.data()),
                                               static_cast<size_t>(bytes.size()))
                                    : 0;
        if (got != static_cast<size_t>(bytes.size()))
            return 0;

        for (int i = 0; i < bytes.size(); ++i)
            if (uchar(bytes[i]) != stopExpr->byteSequence[static_cast<size_t>(i)])
                return 0;

        return static_cast<uint64_t>(stopExpr->byteSequence.size());
    }

    // terminated_by(0) is the common C-string/table-sentinel form. Treat a
    // constant stop expression as an element-value comparison; richer
    // expressions are evaluated as booleans against the formatted element row.
    if (stopExpr->type == EXPR_NUMBER)
    {
        Type *base = BaseNode(elementType);
        if (!base || !isScalar(base->ty))
            return false;

        uint64_t elementValue = 0;
        uint64_t scalarLength = 0;
        INUMTYPE terminatorValue = 0;
        const bool matches = decodeScalarValue(elementType, offset, &elementValue, &scalarLength)
            && evaluate(row, stopExpr, &terminatorValue, offset)
            && elementValue == static_cast<uint64_t>(terminatorValue);
        return matches ? scalarLength : 0;
    }

    INUMTYPE value = 0;
    return evaluate(row, stopExpr, &value, offset) && value != 0 ? elementLength : 0;
}

bool StructureRenderEngine::terminatorShouldBeHidden(TypeDecl *typeDecl,
                                                     Type *elementType,
                                                     ExprNode *stopExpr,
                                                     ExprNode *modeExpr) const
{
    if (!modeExpr)
        FindTag(typeDecl ? typeDecl->tagList : nullptr, TOK_TERMINATOR, &modeExpr);

    switch (terminatorVisibilityExpr(modeExpr))
    {
    case TerminatorVisibility::Hidden:
        return true;
    case TerminatorVisibility::Shown:
        return false;
    case TerminatorVisibility::Auto:
        break;
    }

    if (stopExpr && stopExpr->type == EXPR_BYTESEQ)
        return true;

    const StrataFormat format = formatTag(typeDecl);
    if (FindTag(typeDecl ? typeDecl->tagList : nullptr, TOK_STRING, nullptr)
        || format == StrataFormat::String
        || format == StrataFormat::Utf16
        || format == StrataFormat::Utf16Le
        || format == StrataFormat::Utf16Be)
    {
        return true;
    }

    Type *base = BaseNode(elementType);
    return expressionIsLiteralZero(stopExpr)
        && base
        && (base->ty == typeCHAR || base->ty == typeWCHAR);
}

QString StructureRenderEngine::fieldNameValue(StructureRow *scope, Type *scopeType, ExprNode *expr, uint64_t scopeOffset)
{
    if (!expr)
        return {};

    if (expr->type == EXPR_SCOPE)
    {
        EvalContext context{ scope, scopeType, scopeOffset };
        EvalContext scoped;
        if (!resolveScopeContext(context, expr, &scoped))
            return {};

        QString text = fieldNameValue(scoped.row, scoped.type, expr->right, scoped.offset);
        if (!text.isEmpty())
            return text;

        INUMTYPE value = 0;
        if (evaluate(scoped, expr->right, &value))
            return QString::number(value);

        return {};
    }

    if (StructureRow *row = findFieldRow(scope, expr))
    {
        if (!row->value.isEmpty() && row->value != QStringLiteral("{...}"))
            return row->value;
    }

    if (scope && scope->kind == StructureRowKind::Semantic)
        return {};

    ResolvedField field;
    if (!resolveField(scopeType, expr, scopeOffset, &field))
        return {};

    const QString stringValue = stringArrayValue(nullptr, field.type, field.typeDecl, field.offset);
    if (!stringValue.isNull())
        return stringValue;

    Type *base = BaseNode(field.type);
    if (!base || !isScalar(base->ty))
        return {};

    uint64_t value = 0;
    uint64_t byteLength = 0;
    EndianScope endian(this, field.bigEndian);
    if (!decodeScalarValue(field.type, field.offset, &value, &byteLength))
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
    if (effectiveTag(row, row->typeDecl, TOK_NAME, &nameExpr))
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
