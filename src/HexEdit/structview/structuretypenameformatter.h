#ifndef STRUCTVIEW_STRUCTURETYPENAMEFORMATTER_H
#define STRUCTVIEW_STRUCTURETYPENAMEFORMATTER_H

#include "structview/structuredisplayoptions.h"
#include "Causeway/parser.h"

#include <QString>

struct StructureDeclarationParts
{
    QString prefix;
    QString name;
    QString suffix;
};

class StructureTypeNameFormatter
{
public:
    explicit StructureTypeNameFormatter(const StructureDisplayOptions &options = StructureDisplayOptions());

    QString typeName(Type *type) const;
    StructureDeclarationParts declarationParts(Type *type) const;
    QString declarationName(Type *type) const;
    bool isCompoundDeclaration(Type *type) const;

private:
    QString definedTypeName(Type *type) const;
    QString storageTypeName(Type *type) const;
    QString declaratorSuffix(Type *type) const;

    StructureDisplayOptions m_options;
};

#endif // STRUCTVIEW_STRUCTURETYPENAMEFORMATTER_H
