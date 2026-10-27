// Developer tool: prints the inferred file map (or the raw Strata row tree with
// --rows) for each file argument. Not a test; used to judge how far untagged
// Strata inference gets on real files.
//
//   filemap_dump [--rows] [--depth N] file...

#include "filemap/filemapbuilder.h"
#include "structview/strataroot.h"
#include "structview/structuredefinitionmanager.h"
#include "structview/structuretreemodel.h"
#include "structview/structurevaluebuilder.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include <cstring>

class QMenu;
void themeMenu(QMenu *) {}

namespace
{

QTextStream &out()
{
    static QTextStream stream(stdout);
    return stream;
}

QString hex(uint64_t value)
{
    return QStringLiteral("%1").arg(value, 8, 16, QLatin1Char('0'));
}

QString humanSize(uint64_t bytes)
{
    if (bytes >= 1024 * 1024)
        return QStringLiteral("%1M").arg(double(bytes) / (1024 * 1024), 0, 'f', 1);
    if (bytes >= 1024)
        return QStringLiteral("%1K").arg(double(bytes) / 1024, 0, 'f', 1);
    return QString::number(bytes);
}

void dumpRows(const StructureRow *row, int depth, int maxDepth)
{
    if (!row || depth > maxDepth)
        return;
    const char *kind = row->kind == StructureRowKind::Raw ? "R" : row->kind == StructureRowKind::Dynamic ? "D" : "S";
    out() << QString(depth * 2, QLatin1Char(' ')) << kind << ' '
          << hex(row->absoluteOffset) << " +" << humanSize(row->byteLength) << "  "
          << row->nameTypePrefix << '|' << row->name
          << (row->type ? QStringLiteral(" ty=%1").arg(int(row->type->ty)) : QString())
          << (row->children.empty() ? QString() : QStringLiteral(" [%1 children]").arg(row->children.size()))
          << (row->lazyChildLoader ? " lazy" : "")
          << (row->hasCodeTarget ? QStringLiteral(" code@%1+%2").arg(hex(row->codeTargetOffset)).arg(row->codeByteLength) : QString())
          << (row->hasOpenAsTarget ? QStringLiteral(" nested@%1+%2 %3").arg(hex(row->openAsOffset)).arg(row->openAsByteLength).arg(row->openAsTransform) : QString())
          << '\n';
    for (const auto &child : row->children)
        dumpRows(child.get(), depth + 1, maxDepth);
}

void dumpMap(const filemap::FileMap &map)
{
    out() << "root: " << (map.rootName.isEmpty() ? QStringLiteral("(none)") : map.rootName)
          << "  size: " << map.fileSize << '\n';

    out() << "-- structure (" << map.structure.size() << ")\n";
    for (const filemap::Section &section : map.structure)
        out() << "  " << hex(section.start) << '-' << hex(section.end) << ' '
              << QStringLiteral("%1").arg(humanSize(section.end - section.start), 7) << "  "
              << (section.dynamic ? "~" : " ") << section.label << '\n';

    out() << "-- content (" << map.content.size() << ")\n";
    for (const filemap::Region &region : map.content)
        out() << "  " << hex(region.start) << '-' << hex(region.end) << ' '
              << QStringLiteral("%1").arg(humanSize(region.end - region.start), 7) << "  "
              << QStringLiteral("%1").arg(filemap::regionKindName(region.kind), -10) << ' '
              << QStringLiteral("%1").arg(filemap::regionSourceName(region.source), -9) << ' '
              << region.path << '\n';

    if (!map.markers.isEmpty())
    {
        out() << "-- markers\n";
        for (const filemap::Marker &marker : map.markers)
            out() << "  " << hex(marker.offset) << "  " << marker.label << '\n';
    }
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    bool rows = false;
    int maxDepth = 4;
    QStringList files;
    const QStringList args = app.arguments().mid(1);
    for (int i = 0; i < args.size(); ++i)
    {
        if (args[i] == QLatin1String("--rows"))
            rows = true;
        else if (args[i] == QLatin1String("--depth") && i + 1 < args.size())
            maxDepth = args[++i].toInt();
        else
            files << args[i];
    }

    StructureDefinitionManager definitions;
#ifdef CAUSEWAY_TEST_DATA_DIR
    definitions.setBuiltinStructDirsForTests({QStringLiteral(CAUSEWAY_TEST_DATA_DIR)});
    definitions.setUserStrataDirForTests(QDir::temp().filePath(QStringLiteral("q22-filemap-dump-strata")));
#endif
    definitions.ensureLoaded();

    for (const QString &path : std::as_const(files))
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
        {
            out() << "cannot open " << path << '\n';
            continue;
        }
        const QByteArray data = file.readAll();
        const filemap::ByteReader reader = [&data](uint64_t offset, uint8_t *buf, size_t len) -> size_t {
            if (offset >= uint64_t(data.size()))
                return 0;
            const size_t n = std::min<size_t>(len, size_t(uint64_t(data.size()) - offset));
            memcpy(buf, data.constData() + offset, n);
            return n;
        };

        out() << "==== " << QFileInfo(path).fileName() << '\n';
        if (rows)
        {
            TypeDecl *root = associatedStrataRoot(definitions.exportedTypes(), path, reader);
            if (!root)
            {
                out() << "(no strata root)\n";
                continue;
            }
            const auto built = StructureValueBuilder().build(definitions.library(), root, strataRootBaseOffset(root), reader);
            for (const auto &row : built)
                dumpRows(row.get(), 0, maxDepth);
        }
        else
        {
            dumpMap(filemap::buildFileMap(definitions, path, uint64_t(data.size()), reader));
        }
        out().flush();
    }
    return 0;
}
