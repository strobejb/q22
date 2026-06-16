#include "structview/structurevaluebuilder.h"

std::vector<std::unique_ptr<StructureRow>> StructureValueBuilder::build(TypeLibrary *,
                                                                        TypeDecl *,
                                                                        uint64_t,
                                                                        const ByteReader &) const
{
    return {};
}
