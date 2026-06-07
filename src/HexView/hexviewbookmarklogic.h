#ifndef HEXVIEWBOOKMARKLOGIC_H
#define HEXVIEWBOOKMARKLOGIC_H

#include "hexviewbookmark.h"

#include <QList>
#include <QPoint>
#include <QRect>
#include <QVector>

namespace BookmarkLogic
{

enum class RangeField
{
    Offset,
    Length,
};

struct HighlightChoice
{
    int bookmarkIndex = -1;
    bool hasSelection = false;
    bool hasModified = false;
};

struct BookmarkLayoutEntry
{
    bool inConflict = false;
    bool isActive = true;
    bool hidden = false;
};

struct BookmarkLayoutGeometry
{
    int sy = 0;
    int fullTop = 0;
    int fullBot = 0;
    int tabTop = 0;
    int tabBot = 0;
    QRect paintedFullRect;
    bool paintedFullRectValid = false;
};

struct BookmarkLayoutRequest
{
    size_w cursorOffset = 0;
    int expandedBookmarkIdx = -1;
    int surfacedBookmarkIdx = -1;
    int inlineRangeBookmarkIdx = -1;
    int noteEditorIdx = -1;
    bool noteEditorVisible = false;
    bool mouseHeld = false;
    bool allowStateUpdates = true;
    bool expandLone = false;
    bool expandCursor = false;
    bool expandAlways = false;
};

struct BookmarkLayoutResult
{
    QVector<BookmarkLayoutEntry> layout;
    QVector<bool> activeFlags;
    int expandedBookmarkIdx = -1;
    int surfacedBookmarkIdx = -1;
    int inlineRangeBookmarkIdx = -1;
};

size_w endExclusive(const Bookmark &bm);

int rangeDragDelta(const QRect &valueRect, const QPoint &dragStart, const QPoint &pos,
                   int bytesPerLine);

void clampToNonNestedGap(const QList<Bookmark> &bookmarks, int editedIndex, RangeField field,
                         Bookmark &updated, size_w fileSize, bool nestedAllowed);

int rangeEditingIndex(const QList<Bookmark> &bookmarks, int fallbackIndex);

HighlightChoice chooseHighlight(const QList<Bookmark> &highlights, size_w pos, bool gap);

bool shouldClearRangeEditFromCollapsedDraw(int bookmarkIndex, int inlineRangeIndex, bool mouseHeld);

int visualPriority(int bookmarkIndex, int rangeEditingIndex, int noteEditorIndex,
                   bool noteEditorVisible, int expandedIndex, int surfacedIndex,
                   bool containsCursor);

BookmarkLayoutResult computeLayout(const QList<Bookmark> &bookmarks,
                                   const QVector<BookmarkLayoutGeometry> &geometry,
                                   const BookmarkLayoutRequest &request);

} // namespace BookmarkLogic

#endif // HEXVIEWBOOKMARKLOGIC_H
