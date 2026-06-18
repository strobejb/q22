#include "structview/structurevaluebuilder.h"
#include "structview/structurerenderengine.h"

std::vector<std::unique_ptr<StructureRow>> StructureValueBuilder::build(TypeLibrary *library,
                                                                        TypeDecl *rootType,
                                                                        uint64_t baseOffset,
                                                                        const ByteReader &reader) const
{
    return build(library, rootType, baseOffset, reader, StructureDisplayOptions());
}

std::vector<std::unique_ptr<StructureRow>> StructureValueBuilder::build(TypeLibrary *library,
                                                                        TypeDecl *rootType,
                                                                        uint64_t baseOffset,
                                                                        const ByteReader &reader,
                                                                        const StructureDisplayOptions &options) const
{
    StructureRenderEngine engine(library, rootType, baseOffset, reader, options);
    return engine.build();
}
