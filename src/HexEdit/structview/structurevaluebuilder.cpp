#include "structview/structurevaluebuilder.h"
#include "structview/structurerenderengine.h"

std::vector<std::unique_ptr<StructureRow>> StructureValueBuilder::build(StrataLibrary *library,
                                                                        TypeDecl *rootType,
                                                                        uint64_t baseOffset,
                                                                        const ByteReader &reader) const
{
    return build(library, rootType, baseOffset, reader, StructureDisplayOptions());
}

std::vector<std::unique_ptr<StructureRow>> StructureValueBuilder::build(StrataLibrary *library,
                                                                        TypeDecl *rootType,
                                                                        uint64_t baseOffset,
                                                                        const ByteReader &reader,
                                                                        const StructureDisplayOptions &options) const
{
    auto engine = std::make_shared<StructureRenderEngine>(library, rootType, baseOffset, reader, options);
    return engine->build();
}
