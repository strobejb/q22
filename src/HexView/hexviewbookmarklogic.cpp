#include "hexviewbookmarklogic.h"

#include <QtGlobal>

namespace BookmarkLogic {

size_w endExclusive(const Bookmark &bm)
{
    const size_w length = qMax<size_w>(1, bm.length);
    return bm.offset > (size_w)-1 - length ? (size_w)-1 : bm.offset + length;
}

int rangeDragDelta(const QRect &valueRect,
                   const QPoint &dragStart,
                   const QPoint &pos,
                   int bytesPerLine)
{
    if (!valueRect.isValid())
        return 0;

    const auto horizontalMagnitudeForDistance = [](int distance) {
        if (distance < 2)
            return 0;
        return (distance + 2) / 4;
    };

    const auto verticalMagnitudeForDistance = [](int distance) {
        if (distance < 4)
            return 0;
        return (distance + 11) / 12;
    };

    const int lineLength = qMax(1, bytesPerLine);

    int horizontalDelta = 0;
    const int horizontalDistance = pos.x() - dragStart.x();
    if (horizontalDistance < 0) {
        horizontalDelta = -horizontalMagnitudeForDistance(-horizontalDistance);
    } else if (horizontalDistance > 0) {
        horizontalDelta = horizontalMagnitudeForDistance(horizontalDistance);
    }

    int verticalDelta = 0;
    if (pos.y() < valueRect.top()) {
        verticalDelta = -verticalMagnitudeForDistance(valueRect.top() - pos.y()) * lineLength;
    } else if (pos.y() > valueRect.bottom()) {
        verticalDelta = verticalMagnitudeForDistance(pos.y() - valueRect.bottom()) * lineLength;
    }

    return horizontalDelta + verticalDelta;
}

void clampToNonNestedGap(const QList<Bookmark> &bookmarks,
                         int editedIndex,
                         RangeField field,
                         Bookmark &updated,
                         size_w fileSize,
                         bool nestedAllowed)
{
    if (nestedAllowed)
        return;

    const size_w updatedEnd = endExclusive(updated);
    size_w minOffset = 0;
    size_w maxEnd = fileSize > 0 ? fileSize : (size_w)-1;

    for (int i = 0; i < bookmarks.size(); ++i) {
        if (i == editedIndex)
            continue;

        const Bookmark &other = bookmarks[i];
        const size_w otherEnd = endExclusive(other);

        if (field == RangeField::Offset) {
            if (other.offset < updatedEnd)
                minOffset = qMax(minOffset, otherEnd);
        } else {
            if (otherEnd > updated.offset && other.offset >= updated.offset)
                maxEnd = qMin(maxEnd, other.offset);
        }
    }

    if (field == RangeField::Offset) {
        const size_w maxOffset = updatedEnd > 0 ? updatedEnd - 1 : 0;
        updated.offset = minOffset <= maxOffset
            ? qBound<size_w>(minOffset, updated.offset, maxOffset)
            : maxOffset;
        updated.length = qMax<size_w>(1, updatedEnd > updated.offset ? updatedEnd - updated.offset : 1);
    } else {
        const size_w desiredEnd = endExclusive(updated);
        const size_w minEnd = updated.offset == (size_w)-1 ? (size_w)-1 : updated.offset + 1;
        const size_w clampedEnd = minEnd <= maxEnd
            ? qBound<size_w>(minEnd, desiredEnd, maxEnd)
            : minEnd;
        updated.length = qMax<size_w>(1, clampedEnd - updated.offset);
    }

    if (fileSize > 0 && updated.offset < fileSize)
        updated.length = qMax<size_w>(1, qMin(updated.length, fileSize - updated.offset));
}

int rangeEditingIndex(const QList<Bookmark> &bookmarks,
                      int fallbackIndex)
{
    int activeRangeIdx = -1;
    for (int i = 0; i < bookmarks.size(); ++i) {
        if (bookmarks[i]._rangeEditing) {
            activeRangeIdx = i;
            break;
        }
    }
    if (activeRangeIdx < 0 && fallbackIndex >= 0 && fallbackIndex < bookmarks.size())
        activeRangeIdx = fallbackIndex;

    return activeRangeIdx;
}

HighlightChoice chooseHighlight(const QList<Bookmark> &highlights,
                                size_w pos,
                                bool gap)
{
    HighlightChoice choice;
    for (int i = 0; i < highlights.size(); ++i) {
        const Bookmark &bm = highlights[i];
        if (gap ? (bm.offset >= pos) : (pos < bm.offset)) continue;
        if (pos >= endExclusive(bm))                       continue;
        if (bm.colourIndex == -2) {
            choice.hasSelection = true;
        } else if (bm.colourIndex < 0 && bm.bgColour == 0) {
            choice.hasModified = true;
        } else if (choice.bookmarkIndex < 0 ||
                   (bm._rangeEditing && !highlights[choice.bookmarkIndex]._rangeEditing) ||
                   (bm._rangeEditing == highlights[choice.bookmarkIndex]._rangeEditing &&
                    bm.length < highlights[choice.bookmarkIndex].length)) {
            choice.bookmarkIndex = i;
        }
    }
    return choice;
}

bool shouldClearRangeEditFromCollapsedDraw(int bookmarkIndex,
                                           int inlineRangeIndex,
                                           bool mouseHeld)
{
    return bookmarkIndex == inlineRangeIndex && !mouseHeld;
}

int visualPriority(int bookmarkIndex,
                   int rangeEditingIndex,
                   int noteEditorIndex,
                   bool noteEditorVisible,
                   int expandedIndex,
                   int surfacedIndex,
                   bool containsCursor)
{
    if (bookmarkIndex == rangeEditingIndex)
        return 5;
    if (bookmarkIndex == noteEditorIndex && noteEditorVisible)
        return 4;
    if (bookmarkIndex == expandedIndex)
        return 3;
    if (bookmarkIndex == surfacedIndex)
        return 2;
    if (containsCursor)
        return 1;
    return 0;
}

} // namespace BookmarkLogic
