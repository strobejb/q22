#ifndef BOOKMARKCOMBO_H
#define BOOKMARKCOMBO_H

class DataTypeComboBox;
class HexView;

namespace BookmarkCombo {

enum class Mode {
    IncludeNewBookmark,
    BookmarksOnly,
};

void populate(DataTypeComboBox *combo, HexView *hv, Mode mode, bool swatches);

} // namespace BookmarkCombo

#endif // BOOKMARKCOMBO_H
