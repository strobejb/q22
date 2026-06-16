#ifndef STRUCTVIEW_STRUCTUREVALUEBUILDER_H
#define STRUCTVIEW_STRUCTUREVALUEBUILDER_H

#include "structview/structuretreemodel.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

class StructureValueBuilder
{
public:
    using ByteReader = std::function<size_t(uint64_t offset, uint8_t *buffer, size_t length)>;

    std::vector<std::unique_ptr<StructureRow>> build(TypeLibrary *library,
                                                     TypeDecl *rootType,
                                                     uint64_t baseOffset,
                                                     const ByteReader &reader) const;
};

#endif // STRUCTVIEW_STRUCTUREVALUEBUILDER_H
