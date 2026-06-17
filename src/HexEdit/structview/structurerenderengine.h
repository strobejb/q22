#ifndef STRUCTVIEW_STRUCTURERENDERENGINE_H
#define STRUCTVIEW_STRUCTURERENDERENGINE_H

#include "structview/structurevaluebuilder.h"

#include <memory>
#include <vector>

class StructureRenderEngine
{
public:
    StructureRenderEngine(TypeLibrary *library,
                          TypeDecl *rootType,
                          uint64_t baseOffset,
                          const StructureValueBuilder::ByteReader &reader);

    std::vector<std::unique_ptr<StructureRow>> build();

private:
    using RowPtr = std::unique_ptr<StructureRow>;

    struct DeclarationParts
    {
        QString prefix;
        QString name;
        QString suffix;
    };

    RowPtr makeRow(StructureRow *parent, Type *type, TypeDecl *typeDecl, uint64_t offset) const;
    uint64_t appendTypeDecl(StructureRow *parent, TypeDecl *typeDecl, uint64_t offset);
    uint64_t appendIdentifierRow(StructureRow *parent, Type *type, TypeDecl *typeDecl, uint64_t offset);
    uint64_t recurseType(StructureRow *parent, Type *type, TypeDecl *typeDecl, uint64_t offset);
    uint64_t formatType(StructureRow *row, Type *type, TypeDecl *typeDecl, uint64_t offset);
    uint64_t formatScalar(StructureRow *row, Type *type, TypeDecl *typeDecl, uint64_t offset);
    uint64_t sizeOf(Type *type, uint64_t offset);

    struct EvalContext;
    struct EndianScope;
    struct OffsetMap
    {
        uint64_t logicalStart = 0;
        uint64_t logicalSize = 0;
        uint64_t fileOffset = 0;
    };

    struct DynamicContainer
    {
        TypeDecl *typeDecl = nullptr;
        StructureRow *row = nullptr;
        QString alias;
        uint64_t fileOffset = 0;
        uint64_t byteLength = 0;
        std::vector<OffsetMap> maps;
    };

    struct DynamicRequest
    {
        TypeDecl *typeDecl = nullptr;
        uint64_t logicalOffset = 0;
    };
    bool evaluate(const EvalContext &context, ExprNode *expr, INUMTYPE *result);
    bool evaluate(StructureRow *scope, ExprNode *expr, INUMTYPE *result, uint64_t scopeOffset);
    bool evaluate(Type *scopeType, ExprNode *expr, INUMTYPE *result, uint64_t scopeOffset);
    bool evaluateArrayCount(StructureRow *scope, TypeDecl *typeDecl, Type *arrayType, INUMTYPE *result, uint64_t offset);
    StructureRow *findFieldRow(StructureRow *scope, ExprNode *expr);
    StructureRow *findDirectField(StructureRow *scope, const char *name) const;
    bool readInteger(uint64_t offset, uint64_t length, INUMTYPE *result, bool bigEndian) const;
    struct ResolvedField;
    bool resolveField(Type *scopeType, ExprNode *expr, uint64_t scopeOffset, ResolvedField *field);
    bool resolveDirectField(Type *scopeType, const char *name, uint64_t scopeOffset, ResolvedField *field);
    void collectDynamicRows();
    void collectDynamicRows(StructureRow *row);
    void collectDynamicContainer(StructureRow *row);
    void collectDynamicRequests(StructureRow *row);
    void appendDynamicRows(StructureRow *parent);
    bool dynamicTagArgs(ExprNode *expr, ExprNode **selector, ExprNode **typeName, ExprNode **logicalOffset, ExprNode **condition) const;
    bool dynamicContainerArgs(ExprNode *expr, ExprNode **typeName) const;
    bool offsetMapArgs(ExprNode *expr, ExprNode **logicalStart, ExprNode **logicalSize, ExprNode **fileOffset) const;
    TypeDecl *findTypeDecl(const char *name) const;
    DynamicContainer *mapLogicalOffset(uint64_t logicalOffset, uint64_t *fileOffset);

    QString typeName(Type *type) const;
    QString formatOffset(uint64_t offset) const;
    bool declarationBigEndian(TypeDecl *typeDecl, StructureRow *scope, Type *scopeType, uint64_t scopeOffset);
    Enum *tagEnum(TypeDecl *typeDecl) const;
    QString enumNameForValue(Enum *eptr, INUMTYPE value) const;
    DeclarationParts declarationParts(Type *type) const;
    QString declarationName(Type *type) const;
    void applyDeclarationName(StructureRow *row, Type *type) const;
    bool isCompoundDeclaration(Type *type) const;
    QString declaratorSuffix(Type *type) const;
    QString stringArrayValue(StructureRow *scope, Type *type, TypeDecl *typeDecl, uint64_t offset);
    QString scalarArrayValue(StructureRow *scope, Type *type) const;
    QString fieldNameValue(StructureRow *scope, Type *scopeType, ExprNode *expr, uint64_t scopeOffset);
    QString quoteString(const QString &text) const;

    TypeLibrary *m_library = nullptr;
    TypeDecl *m_rootType = nullptr;
    uint64_t m_baseOffset = 0;
    bool m_bigEndian = false;
    bool m_evaluatingEndian = false;
    StructureValueBuilder::ByteReader m_reader;
    std::vector<DynamicContainer> m_dynamicContainers;
    std::vector<DynamicRequest> m_dynamicRequests;
};

#endif // STRUCTVIEW_STRUCTURERENDERENGINE_H
