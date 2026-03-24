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
};

#endif // HEXVIEWBOOKMARK_H
