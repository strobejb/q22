#ifndef HEXVIEWBOOKMARK_H
#define HEXVIEWBOOKMARK_H

#include "seqbase.h"

#include <QRgb>
#include <QString>

struct Bookmark {
    size_w  offset      = 0;
    size_w  length      = 0;
    QString text;
    QRgb    fgColour    = 0;
    QRgb    bgColour    = 0;
    // bgColour == 0 is a sentinel meaning "FG-only" — used for modified-byte
    // pseudo-bookmarks (internal use only).
    //
    // colourIndex >= 0 means this is a palette-driven user bookmark: the
    // background colour is looked up from HVC_BOOKMARK1+colourIndex at render
    // time, so palette changes automatically update existing bookmarks.
    // colourIndex == -1 (default) falls back to the raw bgColour field.
    // colourIndex == -2 marks the synthetic selection range; colours are
    // resolved at paint time so focus/palette changes cannot leave stale RGBs.
    int     colourIndex = -1;

    // Transient HexView display state.  This is deliberately not persisted by
    // BookmarkStore.
    bool    _active = false;
    // True only while the offset/length value is being mouse-dragged.  This is
    // separate from _active because layout recomputes and clears _active often;
    // the drag owner must survive replaceBookmark() re-sorting every mouse move.
    bool    _rangeEditing = false;

    // Transient draw priority for synthetic highlight records.  Real bookmarks
    // and search matches use the default priority; lower-priority overlays can
    // reuse the same renderer without ever winning over user-authored ranges.
    int     _highlightPriority = 0;

    // Synthetic overlays can opt out of the normal highlight+selection blend:
    // when true, the real HexView selection colour wins inside this range.
    bool    _selectionWins = false;
};

#endif // HEXVIEWBOOKMARK_H
