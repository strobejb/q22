#include "bookmarkstore.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QSettings>

namespace BookmarkStore {

// ── Helpers ───────────────────────────────────────────────────────────────────

static QString settingsPath()
{
    // Derive the directory from a QSettings probe using the same org/app names
    // as the rest of the app — this puts bookmarks.ini alongside hexedit.ini
    // rather than in a deeper AppConfigLocation subdirectory.
    const QSettings probe(QSettings::IniFormat, QSettings::UserScope,
                          QCoreApplication::organizationName(),
                          QCoreApplication::applicationName());
    return QFileInfo(probe.fileName()).absolutePath() + QStringLiteral("/bookmarks.ini");
}

// Use an MD5 hash of the canonical path as the INI group key.
// This avoids QSettings treating path separators as group nesting,
// and is safe for any filename on any platform.
// The human-readable path is stored as a value inside the group.
static QString groupKey(const QString &filePath)
{
    const QString canonical = QFileInfo(filePath).canonicalFilePath();
    const QString key = canonical.isEmpty() ? filePath : canonical;
    return QString::fromLatin1(
        QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Md5).toHex());
}

// ── Public API ────────────────────────────────────────────────────────────────

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
        s.setValue(QStringLiteral("name"),        bm.name);
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
        bm.name        = s.value(QStringLiteral("name"),        QString()).toString();
        bm.colourIndex = s.value(QStringLiteral("colourIndex"), 0).toInt();
        bookmarks.append(bm);
        s.endGroup();
    }
    s.endGroup();
    return bookmarks;
}

} // namespace BookmarkStore
