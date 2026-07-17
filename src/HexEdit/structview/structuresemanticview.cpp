#include "structview/structuresemanticview.h"

#include "structview/dexsemanticview.h"
#include "structview/elfsemanticview.h"
#include "structview/pesemanticview.h"
#include "structview/structurebranchicons.h"

#include <QByteArray>
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QLatin1Char>
#include <QTextStream>

#include <algorithm>
#include <cstring>

StructureSemanticContext::StructureSemanticContext(StrataLibrary *library,
                                                   StructureRow *rootRow,
                                                   StructureRow *currentRow,
                                                   uint64_t baseOffset,
                                                   const StructureValueBuilder::ByteReader &reader,
                                                   const std::vector<StructureOffsetMap> &offsetMaps)
    : m_library(library)
    , m_rootRow(rootRow)
    , m_currentRow(currentRow)
    , m_baseOffset(baseOffset)
    , m_reader(reader)
    , m_offsetMaps(&offsetMaps)
{
}

StrataLibrary *StructureSemanticContext::library() const
{
    return m_library;
}

StructureRow *StructureSemanticContext::rootRow() const
{
    return m_rootRow;
}

StructureRow *StructureSemanticContext::currentRow() const
{
    return m_currentRow;
}

uint64_t StructureSemanticContext::baseOffset() const
{
    return m_baseOffset;
}

const StructureValueBuilder::ByteReader &StructureSemanticContext::byteReader() const
{
    return m_reader;
}

std::vector<StructureOffsetMap> StructureSemanticContext::offsetMaps() const
{
    return m_offsetMaps ? *m_offsetMaps : std::vector<StructureOffsetMap>();
}

bool StructureSemanticContext::mapLogicalOffset(uint64_t logicalOffset, uint64_t *fileOffset) const
{
    if (!fileOffset || !m_offsetMaps)
        return false;

    for (const StructureOffsetMap &map : *m_offsetMaps)
    {
        if (logicalOffset < map.logicalStart || logicalOffset >= map.logicalStart + map.logicalSize)
            continue;

        *fileOffset = map.fileOffset + (logicalOffset - map.logicalStart);
        return true;
    }

    return false;
}

bool StructureSemanticContext::readBytes(uint64_t absoluteOffset, uint8_t *buffer, size_t length) const
{
    if (!buffer || length == 0)
        return false;

    const size_t got = m_reader ? m_reader(absoluteOffset, buffer, length) : 0;
    return got == length;
}

bool StructureSemanticContext::readUInt16(uint64_t absoluteOffset, uint16_t *value) const
{
    uint8_t data[2] = {};
    if (!value || !readBytes(absoluteOffset, data, sizeof(data)))
        return false;

    *value = uint16_t(data[0]) | (uint16_t(data[1]) << 8);
    return true;
}

bool StructureSemanticContext::readUInt32(uint64_t absoluteOffset, uint32_t *value) const
{
    uint8_t data[4] = {};
    if (!value || !readBytes(absoluteOffset, data, sizeof(data)))
        return false;

    *value = uint32_t(data[0]) | (uint32_t(data[1]) << 8) | (uint32_t(data[2]) << 16) | (uint32_t(data[3]) << 24);
    return true;
}

bool StructureSemanticContext::readUInt64(uint64_t absoluteOffset, uint64_t *value) const
{
    uint8_t data[8] = {};
    if (!value || !readBytes(absoluteOffset, data, sizeof(data)))
        return false;

    *value = 0;
    for (int i = 0; i < 8; ++i)
        *value |= uint64_t(data[i]) << (i * 8);
    return true;
}

QString StructureSemanticContext::readAsciiString(uint64_t absoluteOffset, size_t maxLength) const
{
    QString text;
    static constexpr size_t kStringReadChunk = 256;

    for (size_t offset = 0; offset < maxLength; offset += kStringReadChunk)
    {
        const size_t wanted = std::min(kStringReadChunk, maxLength - offset);
        QByteArray bytes(static_cast<qsizetype>(wanted), Qt::Uninitialized);
        const size_t got = m_reader ? m_reader(absoluteOffset + offset,
                                               reinterpret_cast<uint8_t *>(bytes.data()),
                                               wanted)
                                    : 0;
        if (got == 0)
            break;
        bytes.truncate(static_cast<qsizetype>(got));

        for (char byte : bytes)
        {
            const uint8_t ch = static_cast<uint8_t>(byte);
            if (ch == 0)
                return text;
            if (ch < 0x20)
                text += QLatin1Char('.');
            else
                text += QLatin1Char(char(ch));
        }

        if (got < wanted)
            break;
    }

    return QString();
}

StructureRow *StructureSemanticContext::appendSemanticRow(StructureRow *parent,
                                                          const QString &name,
                                                          const QString &value,
                                                          uint64_t absoluteOffset,
                                                          uint64_t byteLength) const
{
    if (!parent)
        return nullptr;

    auto row = createSemanticRow(parent, name, value, absoluteOffset, byteLength);
    StructureRow *raw = row.get();
    parent->children.push_back(std::move(row));
    return raw;
}

std::unique_ptr<StructureRow> StructureSemanticContext::createSemanticRow(StructureRow *parent,
                                                                          const QString &name,
                                                                          const QString &value,
                                                                          uint64_t absoluteOffset,
                                                                          uint64_t byteLength) const
{
    auto row = std::make_unique<StructureRow>(parent);
    row->kind = StructureRowKind::Semantic;
    row->name = name;
    row->value = value;
    row->absoluteOffset = absoluteOffset;
    row->relativeOffset = absoluteOffset >= m_baseOffset ? absoluteOffset - m_baseOffset : 0;
    row->byteLength = byteLength;
    row->offset = byteLength > 0 ? formatOffset(absoluteOffset) : QString();
    row->generatedOffset = byteLength > 0;
    if (value.trimmed() == QStringLiteral("{...}"))
    {
        row->setBranchIcons(QString::fromLatin1(StructureBranchIcons::kBlueStructure),
                            QString::fromLatin1(StructureBranchIcons::kBlueStructureOpen),
                            QString::fromLatin1(StructureBranchIcons::kGrayStructure));
    }
    else
    {
        row->setBranchIcons(QString::fromLatin1(StructureBranchIcons::kBlueElement),
                            QString::fromLatin1(StructureBranchIcons::kBlueElement),
                            QString::fromLatin1(StructureBranchIcons::kBlueElement));
    }

    return row;
}

QString StructureSemanticContext::formatOffset(uint64_t offset) const
{
    return QString::number(offset, 16).toUpper().rightJustified(8, QLatin1Char('0'));
}

StructureSemanticViewRegistry &StructureSemanticViewRegistry::instance()
{
    static StructureSemanticViewRegistry registry;
    return registry;
}

void StructureSemanticViewRegistry::registerInterpreter(const QString &id,
                                                        const StructureSemanticInterpreter &interpreter)
{
    if (!id.isEmpty() && interpreter)
        m_interpreters.insert(id, interpreter);
}

bool StructureSemanticViewRegistry::run(const QString &id, StructureSemanticContext &context) const
{
    const auto it = m_interpreters.find(id);
    if (it == m_interpreters.end())
        return false;

    it.value()(context);
    return true;
}

void registerBuiltInStructureSemanticViews()
{
    static bool registered = false;
    if (registered)
        return;

    registered = true;
    registerDexSemanticViews(StructureSemanticViewRegistry::instance());
    registerElfSemanticViews(StructureSemanticViewRegistry::instance());
    registerPeSemanticViews(StructureSemanticViewRegistry::instance());
}

namespace
{
enum class PeSemanticMode
{
    Cpp,
    Declarative,
    Both
};

PeSemanticMode semanticMode(const char *environmentVariable)
{
    const QString mode = qEnvironmentVariable(environmentVariable,
                                              QStringLiteral("declarative")).trimmed().toLower();
    if (mode == QLatin1String("cpp"))
        return PeSemanticMode::Cpp;
    if (mode == QLatin1String("both"))
        return PeSemanticMode::Both;
    return PeSemanticMode::Declarative;
}

bool shouldRunCppSemanticView(const QString &id)
{
    if (id.startsWith(QStringLiteral("pe.")))
        return semanticMode("Q22_PE_SEMANTIC_VIEW") != PeSemanticMode::Declarative;
    if (id.startsWith(QStringLiteral("elf.")))
        return semanticMode("Q22_ELF_SEMANTIC_VIEW") != PeSemanticMode::Declarative;
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

size_t semanticRowCount(const StructureRow *row)
{
    if (!row)
        return 0;

    size_t count = 1;
    for (const auto &child : row->children)
        count += semanticRowCount(child.get());
    return count;
}

void runSemanticViewsForRow(StructureRow *rootRow,
                            StructureRow *row,
                            StrataLibrary *library,
                            uint64_t baseOffset,
                            const StructureValueBuilder::ByteReader &reader,
                            const std::vector<StructureOffsetMap> &offsetMaps)
{
    if (!row)
        return;

    if (row->kind != StructureRowKind::Semantic && !row->suppressSemanticViews && row->typeDecl)
    {
        for (Tag *tag = row->typeDecl->tagList; tag; tag = tag->link)
        {
            if (tag->tok != TOK_VIEW || !tag->expr || tag->expr->type != EXPR_STRINGBUF || !tag->expr->str)
                continue;

            const QString viewId = QString::fromLocal8Bit(tag->expr->str);
            if (!shouldRunCppSemanticView(viewId))
                continue;

            const bool profile = structureProfileEnabled();
            const size_t rowsBefore = profile ? semanticRowCount(rootRow) : 0;
            QElapsedTimer timer;
            if (profile)
                timer.start();

            StructureSemanticContext context(library, rootRow, row, baseOffset, reader, offsetMaps);
            const bool ran = StructureSemanticViewRegistry::instance().run(viewId, context);
            if (profile && ran)
            {
                const size_t rowsAfter = semanticRowCount(rootRow);
                structureProfileLog(QStringLiteral("[StructureProfile] cpp semantic view id=%1 row=%2 added=%3 rows=%4 ms=%5")
                                        .arg(viewId)
                                        .arg(row->name)
                                        .arg(rowsAfter >= rowsBefore ? rowsAfter - rowsBefore : 0)
                                        .arg(rowsAfter)
                                        .arg(timer.elapsed()));
            }
        }
    }

    for (size_t i = 0; i < row->children.size(); ++i)
        runSemanticViewsForRow(rootRow, row->children[i].get(), library, baseOffset, reader, offsetMaps);
}
}

void runStructureSemanticViews(StrataLibrary *library,
                               StructureRow *rootRow,
                               uint64_t baseOffset,
                               const StructureValueBuilder::ByteReader &reader,
                               const std::vector<StructureOffsetMap> &offsetMaps)
{
    registerBuiltInStructureSemanticViews();
    runSemanticViewsForRow(rootRow, rootRow, library, baseOffset, reader, offsetMaps);
}
