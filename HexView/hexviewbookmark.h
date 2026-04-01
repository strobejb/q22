#ifndef HEXVIEWBOOKMARK_H
#define HEXVIEWBOOKMARK_H

#include "seqbase.h"

#include <QRgb>
#include <QString>

struct Bookmark {
    size_w  offset   = 0;
    size_w  length   = 0;
    QString name;
    QRgb    fgColour = 0;
    QRgb    bgColour = 0;
    // bgColour == 0 (transparent) is a sentinel meaning "FG-only" —
    // the entry overrides the foreground but lets the BG from a lower-
    // priority entry show through.  Used for modified-byte highlights.
};

#endif // HEXVIEWBOOKMARK_H
