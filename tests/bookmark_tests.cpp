#define private public
#define protected public
#include "hexview.h"
#undef protected
#undef private

#include "hexviewbookmarklogic.h"

#include <QtTest/QtTest>

class QMenu;
void themeMenu(QMenu *) {}

namespace {

Bookmark bm(size_w offset, size_w length, int colour = 0)
{
    Bookmark b;
    b.offset = offset;
    b.length = length;
    b.colourIndex = colour;
    return b;
}

QByteArray bytes(int count)
{
    QByteArray data(count, '\0');
    for (int i = 0; i < count; ++i)
        data[i] = char(i & 0xff);
    return data;
}

void initHexView(HexView &hv, int byteCount = 4096)
{
    const QByteArray data = bytes(byteCount);
    QVERIFY(hv.initBuf(reinterpret_cast<const uint8_t *>(data.constData()),
                       static_cast<size_t>(data.size()),
                       true,
                       false));
    hv.resize(1000, 600);
    hv.setLineLen(16);
    hv.scrollHStart();
    hv.scrollTop(0);
}

} // namespace

class BookmarkTests : public QObject
{
    Q_OBJECT

private slots:
    void rangeDragDeltaStartsAtMouseDownAndAmplifiesHorizontally();
    void rangeDragDeltaDoesNotClampHorizontallyToRow();
    void rangeDragDeltaVerticalMovementUsesWholeLines();
    void nonNestedOffsetDragClampsBeforePreviousBookmark();
    void nonNestedLengthDragClampsBeforeNextBookmark();
    void nestedModeAllowsOverlapDuringRangeEdit();
    void rangeEditingHighlightWinsOverShorterBookmark();
    void normalHighlightStillUsesShortestWins();
    void selectionAndModifiedFlagsSurviveHighlightChoice();
    void collapsedDrawDoesNotClearLiveRangeEdit();
    void visualPriorityKeepsRangeEditAbovePinnedAndActive();
    void widgetLayoutKeepsRangeEditActiveInConflictGroup();
    void widgetHighlightUsesRangeEditingBookmarkColour();
    void widgetHitTestUsesRenderedBookmarkGeometry();
};

void BookmarkTests::rangeDragDeltaStartsAtMouseDownAndAmplifiesHorizontally()
{
    // Scenario: the user presses inside the offset/length value and drags right
    // without leaving the highlighted value rectangle first.
    // Expected: horizontal movement starts from the mouse-down point and advances
    // quickly enough to feel responsive.
    // Regression guard: dragging used to feel inert until the cursor escaped the
    // value rect, because distance was measured from the rect edge.
    const QRect valueRect(100, 20, 80, 18);
    QCOMPARE(BookmarkLogic::rangeDragDelta(valueRect, QPoint(120, 28), QPoint(121, 28), 16), 0);
    QCOMPARE(BookmarkLogic::rangeDragDelta(valueRect, QPoint(120, 28), QPoint(122, 28), 16), 1);
    QCOMPARE(BookmarkLogic::rangeDragDelta(valueRect, QPoint(120, 28), QPoint(136, 28), 16), 4);
}

void BookmarkTests::rangeDragDeltaDoesNotClampHorizontallyToRow()
{
    // Scenario: the user keeps dragging offset/length horizontally past the
    // current hex row boundary.
    // Expected: horizontal delta keeps accumulating naturally instead of stopping
    // at column 0 or column bytesPerLine-1.
    // Regression guard: an earlier implementation clamped horizontal movement to
    // the anchor column, making long horizontal drags stall at row edges.
    const QRect valueRect(100, 20, 80, 18);
    QCOMPARE(BookmarkLogic::rangeDragDelta(valueRect, QPoint(120, 28), QPoint(220, 28), 16), 25);
    QCOMPARE(BookmarkLogic::rangeDragDelta(valueRect, QPoint(120, 28), QPoint(20, 28), 16), -25);
}

void BookmarkTests::rangeDragDeltaVerticalMovementUsesWholeLines()
{
    // Scenario: the cursor moves vertically outside the value rect while editing
    // a bookmark range in a 16-byte row view.
    // Expected: vertical movement changes by whole rows, while small vertical
    // jitter inside the value rect does not move the range.
    // Regression guard: horizontal amplification must not make vertical movement
    // twitchy or byte-by-byte.
    const QRect valueRect(100, 20, 80, 18);
    QCOMPARE(BookmarkLogic::rangeDragDelta(valueRect, QPoint(120, 28), QPoint(120, 18), 16), 0);
    QCOMPARE(BookmarkLogic::rangeDragDelta(valueRect, QPoint(120, 28), QPoint(120, 8), 16), -16);
    QCOMPARE(BookmarkLogic::rangeDragDelta(valueRect, QPoint(120, 28), QPoint(120, 42), 16), 16);
}

void BookmarkTests::nonNestedOffsetDragClampsBeforePreviousBookmark()
{
    // Scenario: nested bookmarks are disabled and the user drags bookmark 3's
    // offset left toward an existing bookmark 2.
    // Expected: the edited bookmark stops at bookmark 2's end and keeps at least
    // one byte of length.
    // Regression guard: range editing must obey the same non-overlap rule as
    // creating a new bookmark.
    QList<Bookmark> bookmarks{bm(64, 160), bm(224, 80)};
    Bookmark updated = bookmarks[1];
    updated.offset = 180;
    updated.length = 124; // right edge remains anchored at 304

    BookmarkLogic::clampToNonNestedGap(bookmarks, 1, BookmarkLogic::RangeField::Offset,
                                       updated, 4096, false);

    QCOMPARE(updated.offset, size_w(224));
    QCOMPARE(updated.length, size_w(80));
}

void BookmarkTests::nonNestedLengthDragClampsBeforeNextBookmark()
{
    // Scenario: nested bookmarks are disabled and the user length-drags a range
    // toward the next bookmark.
    // Expected: the edited bookmark's end stops exactly at the next bookmark's
    // start, with no byte overlap.
    // Regression guard: length editing used to be able to create overlaps even
    // when the preference said nested bookmarks were off.
    QList<Bookmark> bookmarks{bm(100, 20), bm(150, 40)};
    Bookmark updated = bookmarks[0];
    updated.length = 80;

    BookmarkLogic::clampToNonNestedGap(bookmarks, 0, BookmarkLogic::RangeField::Length,
                                       updated, 4096, false);

    QCOMPARE(updated.offset, size_w(100));
    QCOMPARE(updated.length, size_w(50));
}

void BookmarkTests::nestedModeAllowsOverlapDuringRangeEdit()
{
    // Scenario: nested bookmarks are enabled and the user deliberately drags a
    // bookmark range into another bookmark.
    // Expected: the proposed overlapping range is left untouched.
    // Regression guard: the non-nested clamp must not apply when the user has
    // explicitly enabled nested bookmarks.
    QList<Bookmark> bookmarks{bm(100, 20), bm(150, 40)};
    Bookmark updated = bookmarks[0];
    updated.length = 80;

    BookmarkLogic::clampToNonNestedGap(bookmarks, 0, BookmarkLogic::RangeField::Length,
                                       updated, 4096, true);

    QCOMPARE(updated.length, size_w(80));
}

void BookmarkTests::rangeEditingHighlightWinsOverShorterBookmark()
{
    // Scenario: bookmark 3 is being range-dragged into a shorter blue bookmark's
    // byte range in the hex view.
    // Expected: the edited bookmark remains the highlight owner even though
    // shortest-wins would normally choose the neighbour.
    // Regression guard: the blue neighbour used to repaint over the range being
    // adjusted once the note collapsed.
    QList<Bookmark> highlights{bm(64, 160, 1), bm(181, 114, 0)};
    highlights[1]._rangeEditing = true;

    const auto choice = BookmarkLogic::chooseHighlight(highlights, 190, false);

    QCOMPARE(choice.bookmarkIndex, 1);
    QVERIFY(!choice.hasSelection);
    QVERIFY(!choice.hasModified);
}

void BookmarkTests::normalHighlightStillUsesShortestWins()
{
    // Scenario: two normal nested bookmarks cover the same byte and no range edit
    // is active.
    // Expected: the shorter bookmark supplies the highlight colour.
    // Regression guard: the range-edit priority must not disturb normal nested
    // bookmark layering.
    QList<Bookmark> highlights{bm(100, 200, 0), bm(150, 20, 1)};

    const auto choice = BookmarkLogic::chooseHighlight(highlights, 155, false);

    QCOMPARE(choice.bookmarkIndex, 1);
}

void BookmarkTests::selectionAndModifiedFlagsSurviveHighlightChoice()
{
    // Scenario: a byte is covered by a bookmark, a selection pseudo-bookmark, and
    // a modified-byte pseudo-bookmark.
    // Expected: highlight selection still reports both selection and modified
    // state so HexView can apply the existing colour precedence.
    // Regression guard: extracting bookmark choice logic must not drop selection
    // or modified-byte information.
    QList<Bookmark> highlights{bm(10, 20, 0)};
    Bookmark selection = bm(12, 4);
    selection.colourIndex = -2;
    highlights.append(selection);
    Bookmark modified = bm(13, 2);
    modified.colourIndex = -1;
    modified.bgColour = 0;
    modified.fgColour = 0xff00ff;
    highlights.append(modified);

    const auto choice = BookmarkLogic::chooseHighlight(highlights, 13, false);

    QCOMPARE(choice.bookmarkIndex, 0);
    QVERIFY(choice.hasSelection);
    QVERIFY(choice.hasModified);
}

void BookmarkTests::collapsedDrawDoesNotClearLiveRangeEdit()
{
    // Scenario: the note for the range being edited collapses while the mouse is
    // still held down.
    // Expected: collapsed-note drawing must not clear the range-edit marker.
    // Regression guard: clearing here was the root cause of the blue neighbour's
    // byte highlight taking over during drag.
    QVERIFY(!BookmarkLogic::shouldClearRangeEditFromCollapsedDraw(2, 2, true));
    QVERIFY(BookmarkLogic::shouldClearRangeEditFromCollapsedDraw(2, 2, false));
    QVERIFY(!BookmarkLogic::shouldClearRangeEditFromCollapsedDraw(1, 2, false));
}

void BookmarkTests::visualPriorityKeepsRangeEditAbovePinnedAndActive()
{
    // Scenario: one bookmark is being range-edited while another bookmark is
    // already pinned/active in the conflict group.
    // Expected: range edit has the highest visual priority and is drawn on top.
    // Regression guard: pinned state used to let a neighbour bury the edited note
    // or its byte range.
    QCOMPARE(BookmarkLogic::visualPriority(2, 2, -1, false, 1, -1, false), 5);
    QCOMPARE(BookmarkLogic::visualPriority(1, 2, -1, false, 1, -1, false), 3);
}

void BookmarkTests::widgetLayoutKeepsRangeEditActiveInConflictGroup()
{
    // Scenario: the Zorin-style second and third bookmarks overlap visually while
    // the third bookmark is being adjusted.
    // Expected: HexView's layout marks the range-edit owner active in its conflict
    // group, even if another bookmark was previously expanded.
    // Regression guard: helper rules must actually be consumed by the widget
    // layout path, not only pass isolated unit tests.
    HexView hv;
    initHexView(hv);
    QList<Bookmark> bookmarks{bm(16, 1), bm(64, 160, 1), bm(181, 114, 0)};
    bookmarks[2]._rangeEditing = true;
    hv.setBookmarks(bookmarks);
    hv.m_inlineRangeBookmarkIdx = 2;
    hv.expandBookmark(1);

    const QVector<HexView::BmLayout> layout = hv.computeBookmarkLayout();

    QVERIFY(layout.value(2).isActive);
    QVERIFY(!layout.value(2).hidden);
}

void BookmarkTests::widgetHighlightUsesRangeEditingBookmarkColour()
{
    // Scenario: the edited bookmark overlaps a blue neighbour in the actual
    // HexView highlight resolver.
    // Expected: the resolved byte background is the edited bookmark colour.
    // Regression guard: getHighlightCol must apply the same range-edit priority
    // as the pure highlight chooser.
    HexView hv;
    initHexView(hv);
    hv.setHexColour(HVC_BOOKMARK1, QColor(255, 255, 0));
    hv.setHexColour(HVC_BOOKMARK2, QColor(80, 160, 255));

    QList<Bookmark> highlights{bm(64, 160, 1), bm(181, 114, 0)};
    highlights[1]._rangeEditing = true;

    HEXCOL col1, col2;
    QVERIFY(hv.getHighlightCol(190, 0, highlights, &col1, &col2));

    QCOMPARE(QColor(col1.colBG), QColor(255, 255, 0));
}

void BookmarkTests::widgetHitTestUsesRenderedBookmarkGeometry()
{
    // Scenario: a bookmark note is rendered as a full strip and the user clicks
    // inside that rendered rectangle rather than on the bookmark's start line.
    // Expected: hit-testing recognizes the visible note geometry.
    // Regression guard: hit-testing previously filtered by start-line visibility,
    // which let neighbouring strips steal clicks from visible/clamped notes.
    HexView hv;
    initHexView(hv);
    hv.setBookmarks({bm(64, 160, 0)});
    hv.expandBookmark(0);
    hv.scrollHEnd();
    const HexView::NoteStripGeom geom = hv.noteStripGeom(hv.bookmarks().first());
    QVERIFY(geom.valid);

    int idx = -1;
    const QPoint hitPoint(geom.rect.left() + 5, geom.rect.center().y());
    const HitTestRegion hit = hv.hitTest(hitPoint.x(), hitPoint.y(), &idx);

    QCOMPARE(idx, 0);
    QVERIFY(hit == HVHT_BOOKMARK_OFFSET || hit == HVHT_BOOKMARK || hit == HVHT_BOOKMARK_LENGTH);
}

QTEST_MAIN(BookmarkTests)

#include "bookmark_tests.moc"
