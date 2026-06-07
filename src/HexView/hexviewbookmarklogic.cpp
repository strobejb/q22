#include "hexviewbookmarklogic.h"

#include <QtGlobal>
#include <algorithm>
#include <climits>

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

BookmarkLayoutResult computeLayout(const QList<Bookmark> &bookmarks,
                                   const QVector<BookmarkLayoutGeometry> &geometry,
                                   const BookmarkLayoutRequest &request)
{
    const int n = bookmarks.size();
    BookmarkLayoutResult result;
    result.layout.resize(n);
    result.activeFlags.resize(n);
    result.expandedBookmarkIdx = request.expandedBookmarkIdx;
    result.surfacedBookmarkIdx = request.surfacedBookmarkIdx;
    result.inlineRangeBookmarkIdx = request.inlineRangeBookmarkIdx;
    for (int i = 0; i < n; ++i)
        result.activeFlags[i] = bookmarks[i]._active;

    if (n == 0 || geometry.size() != n)
        return result;

    QVector<int> visualOrder(n);
    for (int i = 0; i < n; ++i)
        visualOrder[i] = i;
    std::stable_sort(visualOrder.begin(), visualOrder.end(),
                     [&](int a, int b) {
                         if (geometry[a].fullTop != geometry[b].fullTop)
                             return geometry[a].fullTop < geometry[b].fullTop;
                         return a < b;
                     });

    QVector<QVector<int>> groups;
    for (int p = 0; p < n;) {
        QVector<int> group;
        group.append(visualOrder[p]);
        int groupBot = geometry[visualOrder[p]].fullBot;
        ++p;
        while (p < n && geometry[visualOrder[p]].fullTop < groupBot) {
            const int idx = visualOrder[p];
            group.append(idx);
            groupBot = std::max(groupBot, geometry[idx].fullBot);
            ++p;
        }
        std::sort(group.begin(), group.end());
        groups.append(group);
    }

    auto groupContains = [](const QVector<int> &group, int idx) {
        return std::binary_search(group.begin(), group.end(), idx);
    };
    auto isInConflictGroup = [&](int idx) {
        if (idx < 0 || idx >= n)
            return false;
        for (const QVector<int> &group : groups) {
            if (group.size() > 1 && groupContains(group, idx))
                return true;
        }
        return false;
    };
    auto clearActiveInGroup = [&](const QVector<int> &group) {
        for (int j : group)
            result.activeFlags[j] = false;
    };
    auto activeInGroup = [&](const QVector<int> &group) {
        for (int j : group) {
            if (result.activeFlags[j])
                return j;
        }
        return -1;
    };
    auto setActiveInGroup = [&](const QVector<int> &group, int idx) {
        clearActiveInGroup(group);
        if (groupContains(group, idx))
            result.activeFlags[idx] = true;
    };
    auto bookmarkContainsCursor = [&](int idx) {
        if (idx < 0 || idx >= n)
            return false;
        const Bookmark &bm = bookmarks[idx];
        return request.cursorOffset >= bm.offset &&
               request.cursorOffset < endExclusive(bm);
    };

    const int activeRangeIdx = rangeEditingIndex(bookmarks, request.inlineRangeBookmarkIdx);
    if (request.allowStateUpdates && activeRangeIdx >= 0)
        result.inlineRangeBookmarkIdx = activeRangeIdx;

    // Keep sticky bookmark state in sync before the group sweep.  The helper
    // reports the updated values explicitly so hit-testing can request a frozen,
    // side-effect-free answer while normal painting can apply the returned state.
    if (request.allowStateUpdates && !request.mouseHeld) {
        int cursorIdx = -1;
        size_w cursorLen = (size_w)-1;
        for (int j = 0; j < n; ++j) {
            const Bookmark &bm = bookmarks[j];
            if (request.cursorOffset >= bm.offset &&
                request.cursorOffset < endExclusive(bm) &&
                bm.length < cursorLen) {
                cursorIdx = j;
                cursorLen = bm.length;
            }
        }

        const bool cursorInPinned = bookmarkContainsCursor(result.expandedBookmarkIdx);
        if (cursorIdx >= 0) {
            result.surfacedBookmarkIdx = cursorIdx;
            if (request.expandCursor) {
                result.expandedBookmarkIdx = cursorIdx;
            } else if (result.expandedBookmarkIdx >= 0 &&
                       result.expandedBookmarkIdx != cursorIdx) {
                result.expandedBookmarkIdx = -1;
            }
        } else if (result.expandedBookmarkIdx >= 0 && !cursorInPinned) {
            const bool keepGroupedExpansion = request.expandAlways &&
                                              isInConflictGroup(result.expandedBookmarkIdx);
            if (!keepGroupedExpansion)
                result.expandedBookmarkIdx = -1;
        }
    }

    for (const QVector<int> &group : groups) {
        if (group.size() == 1) {
            const int i = group.first();
            const Bookmark &bm = bookmarks[i];
            const bool rangeActive = (activeRangeIdx == i);
            const bool pinned = (result.expandedBookmarkIdx == i);
            const bool inRange = bookmarkContainsCursor(i);
            const bool cursorExpand = request.expandCursor && !request.mouseHeld && inRange;
            if (cursorExpand && request.allowStateUpdates)
                result.expandedBookmarkIdx = i;
            const bool active = rangeActive || pinned || request.expandLone || cursorExpand;
            Q_UNUSED(bm);
            result.layout[i] = {!active, active, false};
            continue;
        }

        int winnerIdx = -1;
        size_w winnerLen = (size_w)-1;

        if (groupContains(group, request.noteEditorIdx) && request.noteEditorVisible) {
            winnerIdx = request.noteEditorIdx;
            if (request.allowStateUpdates) {
                result.expandedBookmarkIdx = request.noteEditorIdx;
                setActiveInGroup(group, winnerIdx);
            }
        }

        // The range-edit owner must stay expanded before ordinary pin/cursor
        // logic.  This protects the live drag target from being buried when the
        // edit creates a temporary visual conflict with a neighbour.
        if (winnerIdx < 0 && groupContains(group, activeRangeIdx)) {
            winnerIdx = activeRangeIdx;
            if (request.allowStateUpdates)
                setActiveInGroup(group, winnerIdx);
        }

        if (winnerIdx < 0 && request.mouseHeld &&
            groupContains(group, result.expandedBookmarkIdx)) {
            winnerIdx = result.expandedBookmarkIdx;
        }

        if (winnerIdx < 0 && groupContains(group, result.expandedBookmarkIdx)) {
            winnerIdx = result.expandedBookmarkIdx;
            if (request.allowStateUpdates)
                setActiveInGroup(group, winnerIdx);
        }

        if (winnerIdx < 0 && request.expandCursor && !request.mouseHeld) {
            for (int j : group) {
                const Bookmark &bm = bookmarks[j];
                if (request.cursorOffset >= bm.offset &&
                    request.cursorOffset < endExclusive(bm) &&
                    bm.length < winnerLen) {
                    winnerIdx = j;
                    winnerLen = bm.length;
                }
            }
            if (winnerIdx >= 0 && request.allowStateUpdates) {
                result.expandedBookmarkIdx = winnerIdx;
                setActiveInGroup(group, winnerIdx);
            }
        }

        int surfacedIdx = -1;
        size_w surfacedLen = (size_w)-1;
        if (winnerIdx < 0 && !request.mouseHeld) {
            for (int j : group) {
                const Bookmark &bm = bookmarks[j];
                if (request.cursorOffset >= bm.offset &&
                    request.cursorOffset < endExclusive(bm) &&
                    bm.length < surfacedLen) {
                    surfacedIdx = j;
                    surfacedLen = bm.length;
                }
            }
        }
        if (surfacedIdx < 0 && groupContains(group, result.surfacedBookmarkIdx))
            surfacedIdx = result.surfacedBookmarkIdx;

        if (winnerIdx < 0 && request.expandAlways) {
            const int activeIdx = activeInGroup(group);
            winnerIdx = activeIdx >= 0 ? activeIdx : (surfacedIdx >= 0 ? surfacedIdx : group.first());
            if (request.allowStateUpdates)
                setActiveInGroup(group, winnerIdx);
        }

        const int activeTop = (winnerIdx >= 0) ? geometry[winnerIdx].fullTop : INT_MIN;
        const int activeBot = (winnerIdx >= 0) ? geometry[winnerIdx].fullBot : INT_MIN;
        for (int j : group) {
            const bool active = (winnerIdx != -1 && j == winnerIdx);
            bool hidden = false;
            if (!active && winnerIdx >= 0) {
                hidden = (geometry[j].tabTop < activeBot) &&
                         (geometry[j].tabBot > activeTop);
            }
            if (!active && !hidden && surfacedIdx >= 0 && j != surfacedIdx) {
                if (geometry[j].sy == geometry[surfacedIdx].sy)
                    hidden = true;
            }
            result.layout[j] = {true, active, hidden};
        }
    }

    struct ActiveStrip {
        int idx;
        QRect rect;
        int priority;
    };

    QVector<ActiveStrip> activeStrips;
    for (int k = 0; k < n; ++k) {
        if (!result.layout[k].isActive || !geometry[k].paintedFullRectValid)
            continue;
        activeStrips.append({k,
                             geometry[k].paintedFullRect,
                             visualPriority(k,
                                            activeRangeIdx,
                                            request.noteEditorIdx,
                                            request.noteEditorVisible,
                                            result.expandedBookmarkIdx,
                                            result.surfacedBookmarkIdx,
                                            bookmarkContainsCursor(k))});
    }
    std::stable_sort(activeStrips.begin(), activeStrips.end(),
                     [](const ActiveStrip &a, const ActiveStrip &b) {
                         if (a.priority != b.priority) return a.priority > b.priority;
                         if (a.rect.top() != b.rect.top()) return a.rect.top() < b.rect.top();
                         return a.idx < b.idx;
                     });

    QVector<ActiveStrip> keptActive;
    auto overlapsKeptActive = [&](const QRect &rect) {
        for (const ActiveStrip &kept : keptActive) {
            if (rect.top() < kept.rect.bottom() && rect.bottom() > kept.rect.top())
                return true;
        }
        return false;
    };
    auto collapsedTabOverlapsKept = [&](int idx) {
        for (const ActiveStrip &kept : keptActive) {
            if (geometry[idx].tabTop < kept.rect.bottom() &&
                geometry[idx].tabBot > kept.rect.top())
                return true;
        }
        return false;
    };

    for (const ActiveStrip &strip : activeStrips) {
        if (!overlapsKeptActive(strip.rect)) {
            keptActive.append(strip);
            continue;
        }

        result.layout[strip.idx] = {true, false, collapsedTabOverlapsKept(strip.idx)};
    }

    for (const ActiveStrip &kept : keptActive) {
        for (int j = 0; j < n; ++j) {
            if (result.layout[j].isActive || result.layout[j].hidden)
                continue;
            if (geometry[j].tabTop < kept.rect.bottom() &&
                geometry[j].tabBot > kept.rect.top())
                result.layout[j].hidden = true;
        }
    }

    return result;
}

} // namespace BookmarkLogic
