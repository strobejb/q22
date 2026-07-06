#ifndef BOOKMARKACTIVATION_H
#define BOOKMARKACTIVATION_H

#include <QString>
#include <functional>
#include <cstdint>

class HexView;

namespace BookmarkActivation {

using FunctionCallback = std::function<void(uint64_t offset, uint64_t length, const QString &name)>;

void activate(HexView *hv, int idx, FunctionCallback functionCallback = {});

}

#endif // BOOKMARKACTIVATION_H
