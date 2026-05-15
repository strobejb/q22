#pragma once

#include "HexView/hexviewbookmark.h"
#include <QList>
#include <QString>

// Persists bookmarks to a per-app bookmarks.ini in the app config directory.
// Each file gets a section keyed by the MD5 hash of its canonical path, with
// the human-readable path stored alongside for reference.
//
// Call save() whenever bookmarks change; call load() after opening a file.

namespace BookmarkStore {

void            save(const QString &filePath, const QList<Bookmark> &bookmarks);
QList<Bookmark> load(const QString &filePath);

} // namespace BookmarkStore
