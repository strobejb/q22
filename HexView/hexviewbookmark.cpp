#include "hexview.h"

#include <QPainter>

// ── Bookmark management ───────────────────────────────────────────────────────

void HexView::addBookmark(const Bookmark &bm)
{
    m_bookmarks.append(bm);
    viewport()->update();
}

int HexView::findBookmark(size_w startoff, size_w endoff) const
{
    for (int i = 0; i < m_bookmarks.size(); ++i) {
        const Bookmark &bm = m_bookmarks[i];
        if (bm.offset + bm.length > startoff && bm.offset < endoff)
            return i;
    }
    return -1;
}

void HexView::drawNoteStrip(QPainter &/*painter*/, int /*nx*/, int /*ny*/, const Bookmark &/*bm*/)
{
    // stub — not yet implemented
}
