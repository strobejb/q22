#include "bookmarkstore.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSaveFile>
#include <QSettings>

namespace BookmarkStore {

// ── Helpers ───────────────────────────────────────────────────────────────────

static QString storePath()
{
    // Derive the directory from a QSettings probe using the same org/app names
    // as the rest of the app — this puts bookmarks.json alongside hexedit.ini
    // rather than in a deeper AppConfigLocation subdirectory.
    const QSettings probe(QSettings::IniFormat, QSettings::UserScope,
                          QCoreApplication::organizationName(),
                          QCoreApplication::applicationName());
    return QFileInfo(probe.fileName()).absolutePath() + QStringLiteral("/bookmarks.json");
}

// Use an MD5 hash of the canonical path as a stable file id.  The human-readable
// path is stored alongside it for readability.
static QString groupKey(const QString &filePath)
{
    const QString canonical = QFileInfo(filePath).canonicalFilePath();
    const QString key = canonical.isEmpty() ? filePath : canonical;
    return QString::fromLatin1(
        QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Md5).toHex());
}

static QString formatStore(const QJsonObject &root);

static QJsonObject emptyStore()
{
    return {
        { QStringLiteral("version"), 1 },
        { QStringLiteral("files"), QJsonArray() },
    };
}

static QJsonObject readStore()
{
    QFile f(storePath());
    if (!f.open(QIODevice::ReadOnly))
        return emptyStore();

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return emptyStore();

    QJsonObject root = doc.object();
    if (!root.value(QStringLiteral("files")).isArray())
        root.insert(QStringLiteral("files"), QJsonArray());
    if (!root.contains(QStringLiteral("version")))
        root.insert(QStringLiteral("version"), 1);
    return root;
}

static void writeStore(const QJsonObject &root)
{
    const QString path = storePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;

    f.write(formatStore(root).toUtf8());
    f.commit();
}

static QString jsonString(const QString &text)
{
    const QJsonDocument doc(QJsonArray { text });
    QString json = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
    json.remove(0, 1);
    json.chop(1);
    return json;
}

static QString indent(int level)
{
    return QString(level * 2, QLatin1Char(' '));
}

static void appendBookmark(QString &out, const QJsonObject &bookmark, int level)
{
    out += indent(level) + QStringLiteral("{\n");
    out += indent(level + 1) + QStringLiteral("\"offset\": ") + jsonString(bookmark.value(QStringLiteral("offset")).toString()) + QStringLiteral(",\n");
    out += indent(level + 1) + QStringLiteral("\"length\": ") + jsonString(bookmark.value(QStringLiteral("length")).toString()) + QStringLiteral(",\n");
    out += indent(level + 1) + QStringLiteral("\"text\": ") + jsonString(bookmark.value(QStringLiteral("text")).toString()) + QStringLiteral(",\n");
    out += indent(level + 1) + QStringLiteral("\"colour\": ") + QString::number(bookmark.value(QStringLiteral("colour")).toInt());
    out += QStringLiteral("\n") + indent(level) + QStringLiteral("}");
}

static void appendFile(QString &out, const QJsonObject &file, int level)
{
    out += indent(level) + QStringLiteral("{\n");
    out += indent(level + 1) + QStringLiteral("\"id\": ") + jsonString(file.value(QStringLiteral("id")).toString()) + QStringLiteral(",\n");
    out += indent(level + 1) + QStringLiteral("\"path\": ") + jsonString(file.value(QStringLiteral("path")).toString()) + QStringLiteral(",\n");
    out += indent(level + 1) + QStringLiteral("\"bookmarks\": [");

    const QJsonArray bookmarks = file.value(QStringLiteral("bookmarks")).toArray();
    if (!bookmarks.isEmpty()) {
        out += QStringLiteral("\n");
        for (int i = 0; i < bookmarks.size(); ++i) {
            appendBookmark(out, bookmarks.at(i).toObject(), level + 2);
            if (i + 1 < bookmarks.size())
                out += QStringLiteral(",");
            out += QStringLiteral("\n");
        }
        out += indent(level + 1);
    }
    out += QStringLiteral("]\n");
    out += indent(level) + QStringLiteral("}");
}

static QString formatStore(const QJsonObject &root)
{
    QString out;
    out += QStringLiteral("{\n");
    out += indent(1) + QStringLiteral("\"version\": ") + QString::number(root.value(QStringLiteral("version")).toInt(1)) + QStringLiteral(",\n");
    out += indent(1) + QStringLiteral("\"files\": [");

    const QJsonArray files = root.value(QStringLiteral("files")).toArray();
    if (!files.isEmpty()) {
        out += QStringLiteral("\n");
        for (int i = 0; i < files.size(); ++i) {
            appendFile(out, files.at(i).toObject(), 2);
            if (i + 1 < files.size())
                out += QStringLiteral(",");
            out += QStringLiteral("\n");
        }
        out += indent(1);
    }
    out += QStringLiteral("]\n");
    out += QStringLiteral("}\n");
    return out;
}

static QJsonObject bookmarkToJson(const Bookmark &bm)
{
    return {
        { QStringLiteral("offset"), QString::number(bm.offset) },
        { QStringLiteral("length"), QString::number(bm.length) },
        { QStringLiteral("text"),   bm.text },
        { QStringLiteral("colour"), bm.colourIndex },
    };
}

static Bookmark bookmarkFromJson(const QJsonObject &obj)
{
    Bookmark bm;
    bm.offset = obj.value(QStringLiteral("offset")).toString(QStringLiteral("0")).toULongLong();
    bm.length = obj.value(QStringLiteral("length")).toString(QStringLiteral("1")).toULongLong();
    bm.text = obj.value(QStringLiteral("text")).toString();
    bm.colourIndex = obj.value(QStringLiteral("colour")).toInt(0);
    return bm;
}

// ── Public API ────────────────────────────────────────────────────────────────

void save(const QString &filePath, const QList<Bookmark> &bookmarks)
{
    if (filePath.isEmpty()) return;

    QJsonObject root = readStore();
    QJsonArray files = root.value(QStringLiteral("files")).toArray();
    const QString group = groupKey(filePath);

    QJsonArray jsonBookmarks;
    for (const Bookmark &bm : bookmarks)
        jsonBookmarks.append(bookmarkToJson(bm));

    QJsonObject fileObj {
        { QStringLiteral("id"),        group },
        { QStringLiteral("path"),      filePath },
        { QStringLiteral("bookmarks"), jsonBookmarks },
    };

    bool replaced = false;
    for (int i = 0; i < files.size(); ++i) {
        const QJsonObject existing = files.at(i).toObject();
        if (existing.value(QStringLiteral("id")).toString() == group) {
            files.replace(i, fileObj);
            replaced = true;
            break;
        }
    }
    if (!replaced)
        files.append(fileObj);

    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("files"), files);
    writeStore(root);
}

QList<Bookmark> load(const QString &filePath)
{
    if (filePath.isEmpty()) return {};

    const QJsonObject root = readStore();
    const QJsonArray files = root.value(QStringLiteral("files")).toArray();
    const QString group = groupKey(filePath);

    for (const QJsonValue &fileValue : files) {
        const QJsonObject fileObj = fileValue.toObject();
        if (fileObj.value(QStringLiteral("id")).toString() != group)
            continue;

        const QJsonArray jsonBookmarks = fileObj.value(QStringLiteral("bookmarks")).toArray();
        QList<Bookmark> bookmarks;
        bookmarks.reserve(jsonBookmarks.size());

        for (const QJsonValue &bookmarkValue : jsonBookmarks)
            bookmarks.append(bookmarkFromJson(bookmarkValue.toObject()));

        return bookmarks;
    }

    return {};
}

#if 0
static QString settingsPath()
{
    const QSettings probe(QSettings::IniFormat, QSettings::UserScope,
                          QCoreApplication::organizationName(),
                          QCoreApplication::applicationName());
    return QFileInfo(probe.fileName()).absolutePath() + QStringLiteral("/bookmarks.ini");
}

void save(const QString &filePath, const QList<Bookmark> &bookmarks)
{
    if (filePath.isEmpty()) return;

    QSettings s(settingsPath(), QSettings::IniFormat);
    const QString group = groupKey(filePath);

    // Remove old data for this file entirely before rewriting.
    s.remove(group);

    s.beginGroup(group);
    s.setValue(QStringLiteral("path"),  filePath);
    s.setValue(QStringLiteral("count"), bookmarks.size());

    for (int i = 0; i < bookmarks.size(); ++i) {
        const Bookmark &bm = bookmarks[i];
        s.beginGroup(QString::number(i));
        // Store offset/length as decimal strings to preserve full 64-bit range.
        s.setValue(QStringLiteral("offset"),      QString::number(bm.offset));
        s.setValue(QStringLiteral("length"),      QString::number(bm.length));
        s.setValue(QStringLiteral("name"),        bm.text);
        s.setValue(QStringLiteral("colourIndex"), bm.colourIndex);
        s.endGroup();
    }
    s.endGroup();
}

QList<Bookmark> load(const QString &filePath)
{
    if (filePath.isEmpty()) return {};

    QSettings s(settingsPath(), QSettings::IniFormat);
    const QString group = groupKey(filePath);

    s.beginGroup(group);
    const int count = s.value(QStringLiteral("count"), 0).toInt();
    QList<Bookmark> bookmarks;
    bookmarks.reserve(count);

    for (int i = 0; i < count; ++i) {
        s.beginGroup(QString::number(i));
        Bookmark bm;
        bm.offset      = s.value(QStringLiteral("offset"),      QStringLiteral("0")).toString().toULongLong();
        bm.length      = s.value(QStringLiteral("length"),      QStringLiteral("1")).toString().toULongLong();
        bm.text        = s.value(QStringLiteral("name"),        QString()).toString();
        bm.colourIndex = s.value(QStringLiteral("colourIndex"), 0).toInt();
        bookmarks.append(bm);
        s.endGroup();
    }
    s.endGroup();
    return bookmarks;
}
#endif

} // namespace BookmarkStore
