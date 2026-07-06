#include "bookmarks/bookmarkactivation.h"

#include "HexView/hexview.h"

namespace BookmarkActivation {

void activate(HexView *hv, int idx, FunctionCallback functionCallback)
{
    if (!hv)
        return;

    const QList<Bookmark> &bookmarks = hv->bookmarks();
    if (idx < 0 || idx >= bookmarks.size())
        return;

    const Bookmark &bm = bookmarks.at(idx);
    hv->expandBookmark(idx);
    hv->scrollCenterIfOffScreen(bm.offset, bm.length);
    hv->setCurSel(bm.offset + bm.length, bm.offset);
    hv->scrollHEnd();
    hv->setFocus();

    if (bm.kind == BookmarkKind::Function && functionCallback)
        functionCallback(static_cast<uint64_t>(bm.offset),
                         static_cast<uint64_t>(bm.length),
                         bm.text);
}

}
