#ifndef HEXVIEWBOOKMARK_H
#define HEXVIEWBOOKMARK_H

#include "seqbase.h"

#include <QRgb>
#include <QString>

struct Bookmark {
    size_w  offset      = 0;
    size_w  length      = 0;
    QString name;
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
};

#endif // HEXVIEWBOOKMARK_H
