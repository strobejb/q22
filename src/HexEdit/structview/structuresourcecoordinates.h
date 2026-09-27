#ifndef STRUCTVIEW_STRUCTURESOURCECOORDINATES_H
#define STRUCTVIEW_STRUCTURESOURCECOORDINATES_H

#include <cstdint>

namespace StructureSourceCoordinates
{

inline bool toViewOffset(uint64_t sourceOffset,
                         bool sliceRoot,
                         bool isolatedSource,
                         uint64_t baseOffset,
                         uint64_t *viewOffset)
{
    if (!viewOffset)
        return false;

    if (sliceRoot && !isolatedSource)
    {
        if (sourceOffset < baseOffset)
            return false;
        *viewOffset = sourceOffset - baseOffset;
        return true;
    }

    *viewOffset = sourceOffset;
    return true;
}

} // namespace StructureSourceCoordinates

#endif // STRUCTVIEW_STRUCTURESOURCECOORDINATES_H
