#pragma once

#include <QRect>

class HexView;

// Shows the bookmark settings popup (colour picker + copy/delete row) anchored
// below the gear button at btnGlobal.  Self-contained: only needs the HexView
// pointer so it can read/write bookmarks and keep the popup-open visual state.
void showBookmarkContextPopup(HexView *hv, int idx, QRect btnGlobal);
