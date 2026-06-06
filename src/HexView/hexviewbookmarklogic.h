#ifndef HEXVIEWBOOKMARKLOGIC_H
#define HEXVIEWBOOKMARKLOGIC_H

#include "hexviewbookmark.h"

#include <QList>
#include <QPoint>
#include <QRect>

namespace BookmarkLogic {

enum class RangeField {
    Offset,
    Length,
};

struct HighlightChoice {
    int  bookmarkIndex = -1;
    bool hasSelection  = false;
    bool hasModified   = false;
};

size_w endExclusive(const Bookmark &bm);

int rangeDragDelta(const QRect &valueRect,
                   const QPoint &dragStart,
                   const QPoint &pos,
                   int bytesPerLine);

void clampToNonNestedGap(const QList<Bookmark> &bookmarks,
                         int editedIndex,
                         RangeField field,
                         Bookmark &updated,
                         size_w fileSize,
                         bool nestedAllowed);

int rangeEditingIndex(const QList<Bookmark> &bookmarks,
                      int fallbackIndex);

HighlightChoice chooseHighlight(const QList<Bookmark> &highlights,
                                size_w pos,
                                bool gap);

bool shouldClearRangeEditFromCollapsedDraw(int bookmarkIndex,
                                           int inlineRangeIndex,
                                           bool mouseHeld);

int visualPriority(int bookmarkIndex,
                   int rangeEditingIndex,
                   int noteEditorIndex,
                   bool noteEditorVisible,
                   int expandedIndex,
                   int surfacedIndex,
                   bool containsCursor);

} // namespace BookmarkLogic

#endif // HEXVIEWBOOKMARKLOGIC_H
