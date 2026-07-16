#ifndef STRUCTVIEW_STRUCTURERENDERENGINE_H
#define STRUCTVIEW_STRUCTURERENDERENGINE_H

#include "structview/structurevaluebuilder.h"

#include <QStringList>

#include <memory>
#include <vector>

class StructureRenderEngine : public std::enable_shared_from_this<StructureRenderEngine>
{
public:
    StructureRenderEngine(StrataLibrary *library,
                          TypeDecl *rootType,
                          uint64_t baseOffset,
                          const StructureValueBuilder::ByteReader &reader,
                          const StructureDisplayOptions &options);

    std::vector<std::unique_ptr<StructureRow>> build();

    // Strata-language static analysis, not rendering: every select/
    // switch_is/endian/offset/size_is/optional/extent tag expression in
    // `library` is checked for constructs that cannot work in its static
    // context. This catches field references that resolveDirectField
    // cannot resolve without a live file open, and exported root
    // offset(...) expressions that are not constant-foldable. One message
    // per problem found, empty if none. Used by StructureDefinitionManager
    // to catch definition mistakes when a .struct/.strata file is loaded,
    // instead of them silently failing the first time a real file is opened.
    static QStringList validateStaticFieldReferences(StrataLibrary *library);

private:
    using RowPtr = std::unique_ptr<StructureRow>;

    RowPtr makeRow(StructureRow *parent, Type *type, TypeDecl *typeDecl, uint64_t offset) const;
    uint64_t appendTypeDecl(StructureRow *parent, TypeDecl *typeDecl, uint64_t offset);
    uint64_t appendIdentifierRow(StructureRow *parent, Type *type, TypeDecl *typeDecl, uint64_t offset);
    uint64_t recurseType(StructureRow *parent, Type *type, TypeDecl *typeDecl, uint64_t offset);
    uint64_t formatType(StructureRow *row, Type *type, TypeDecl *typeDecl, uint64_t offset);
    uint64_t formatScalar(StructureRow *row, Type *type, TypeDecl *typeDecl, uint64_t offset);
    uint64_t sizeOf(Type *type, uint64_t offset);
    uint64_t staticSizeOfName(const char *name);
    uint64_t staticSizeOfType(Type *type);

    struct EvalContext;
    struct EndianScope;
    struct AlignmentScope;
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

    enum class DynamicMapper
    {
        Direct,
        OffsetMap
    };

    struct DynamicRequest
    {
        StructureRow *owner = nullptr;
        TypeDecl *typeDecl = nullptr;
        Type *renderType = nullptr;
        QString label;
        QString containerLabel;
        uint64_t logicalOffset = 0;
        DynamicMapper mapper = DynamicMapper::Direct;
    };

    // Referenced PE/ELF-style tables are not inline C fields: a row contains an
    // RVA/file offset and count, and the actual array lives elsewhere. Dynamic
    // arrays preserve the honest raw structure while rendering that related
    // table beneath the row that owns the relationship.
    struct DynamicArrayRequest
    {
        StructureRow *owner = nullptr;
        TypeDecl *typeDecl = nullptr;
        Type *renderType = nullptr;
        QString label;
        QString containerLabel;
        uint64_t logicalOffset = 0;
        uint64_t maxCount = 0;
        ExprNode *stopExpr = nullptr;
        ExprNode *terminatorModeExpr = nullptr;
        ExprNode *conditionExpr = nullptr;
        DynamicMapper mapper = DynamicMapper::Direct;
        bool attachToMappedContainer = false;
    };
    bool evaluate(const EvalContext &context, ExprNode *expr, INUMTYPE *result);
    bool evaluate(StructureRow *scope, ExprNode *expr, INUMTYPE *result, uint64_t scopeOffset);
    bool evaluate(Type *scopeType, ExprNode *expr, INUMTYPE *result, uint64_t scopeOffset);
    bool evaluateFunction(const EvalContext &context, ExprNode *expr, INUMTYPE *result);
    bool evaluateFindFunction(const EvalContext &context, ExprNode *expr, INUMTYPE *result);
    bool evaluateString(const EvalContext &context, ExprNode *expr, QString *result);
    bool evaluateStringFunction(const EvalContext &context, ExprNode *expr, QString *result);
    QString fieldStringValue(StructureRow *row);
    uint64_t readableEnd(uint64_t startOffset) const;
    bool findPattern(uint64_t startOffset,
                     uint64_t endOffset,
                     const std::vector<uint8_t> &pattern,
                     bool reverse,
                     uint64_t *absoluteMatch) const;
    bool evaluateArrayCount(StructureRow *scope, TypeDecl *typeDecl, Type *arrayType, INUMTYPE *result, uint64_t offset);
    uint64_t evaluatedCountAs(StructureRow *scope, Type *scopeType, TypeDecl *typeDecl, uint64_t offset);
    StructureRow *findFieldRow(StructureRow *scope, ExprNode *expr);
    StructureRow *findDirectField(StructureRow *scope, const char *name) const;
    bool readInteger(uint64_t offset, uint64_t length, INUMTYPE *result, bool bigEndian) const;
    bool decodeScalarValue(Type *type, uint64_t offset, uint64_t *value, uint64_t *byteLength) const;
    struct ResolvedField;
    bool resolveField(Type *scopeType, ExprNode *expr, uint64_t scopeOffset, ResolvedField *field);
    bool resolveDirectField(Type *scopeType, const char *name, uint64_t scopeOffset, ResolvedField *field);
    bool isKnownEnumConstant(const char *name) const;
    void validateFieldTagExpressions(Type *enclosingScope, ExprNode *expr,
                                     TypeDecl *owner, TOKEN tagTok, QStringList *errors);
    void validateStructTags(Type *structType, QStringList *errors);
    void collectDynamicRows();
    void collectDynamicRows(StructureRow *row);
    void collectDynamicContainer(StructureRow *row);
    void collectDynamicRequests(StructureRow *row);
    void collectDynamicArrayRequests(StructureRow *row);
    void appendDynamicRows(StructureRow *parent);
    void appendDynamicArrayRows(StructureRow *row);
    std::vector<RowPtr> buildSubArraysForElement(StructureRow *elementRow,
                                                 std::vector<DynamicArrayRequest> subRequests);
    bool dynamicTagArgs(ExprNode *expr,
                        ExprNode **selector,
                        ExprNode **label,
                        ExprNode **container,
                        ExprNode **typeName,
                        ExprNode **logicalOffset,
                        ExprNode **condition,
                        DynamicMapper *mapper) const;
    bool dynamicArrayArgs(ExprNode *expr,
                          ExprNode **selectorOrLabel,
                          ExprNode **container,
                          ExprNode **typeName,
                          ExprNode **logicalOffset,
                          ExprNode **count,
                          ExprNode **stop,
                          ExprNode **terminatorMode,
                          ExprNode **condition,
                          DynamicMapper *mapper,
                          bool *isNameSource) const;
    bool dynamicContainerArgs(ExprNode *expr, ExprNode **typeName) const;
    bool offsetMapArgs(ExprNode *expr, ExprNode **logicalStart, ExprNode **logicalSize, ExprNode **fileOffset) const;
    TypeDecl *findTypeDecl(const char *name) const;
    Type *typeInDecl(TypeDecl *decl, const char *name) const;
    DynamicContainer *mapLogicalOffset(uint64_t logicalOffset, uint64_t *fileOffset);
    StructureRow *dynamicRootGroup(const QString &label);
    void resolveEntryPointRows(StructureRow *row);

    QString typeName(Type *type) const;
    QString formatOffset(uint64_t offset) const;
    uint64_t alignedOffset(uint64_t offset, uint64_t alignment) const;
    uint64_t declarationAlignment(TypeDecl *typeDecl,
                                  StructureRow *scope,
                                  Type *scopeType,
                                  uint64_t scopeOffset,
                                  uint64_t fallback) const;
    uint64_t declarationExtent(TypeDecl *typeDecl,
                               StructureRow *scope,
                               Type *scopeType,
                               uint64_t scopeOffset,
                               uint64_t fallback);
    bool declarationIsOptionalAndAbsent(TypeDecl *typeDecl,
                                        StructureRow *scope,
                                        Type *scopeType,
                                        uint64_t scopeOffset);
    bool declarationBigEndian(TypeDecl *typeDecl, StructureRow *scope, Type *scopeType, uint64_t scopeOffset);
    Enum *tagValueEnum(TypeDecl *typeDecl, TOKEN tagTok) const;
    Enum *tagEnum(TypeDecl *typeDecl) const;
    QString enumNameForValue(Enum *eptr, INUMTYPE value) const;
    QStringList enumChoiceLabels(Enum *eptr) const;
    void applyBitflagTag(StructureRow *row, Type *type, TypeDecl *typeDecl, uint64_t rawValue, uint64_t byteLength);
    bool applyFormatTag(StructureRow *row, TypeDecl *typeDecl, uint64_t byteLength);
    void applyTreeTag(StructureRow *row, TypeDecl *typeDecl) const;
    void appendPresentedRow(StructureRow *parent, RowPtr row) const;
    void applyEntryPointTag(StructureRow *row, TypeDecl *typeDecl);
    void applyDeclarationName(StructureRow *row, Type *type) const;
    QString stringArrayValue(StructureRow *scope, Type *type, TypeDecl *typeDecl, uint64_t offset);
    QString scalarArrayValue(StructureRow *scope, Type *type) const;
    uint64_t terminatorMatchLength(StructureRow *row,
                                   Type *elementType,
                                   ExprNode *stopExpr,
                                   uint64_t offset,
                                   uint64_t elementLength);
    bool terminatorShouldBeHidden(TypeDecl *typeDecl,
                                  Type *elementType,
                                  ExprNode *stopExpr,
                                  ExprNode *modeExpr) const;
    QString fieldNameValue(StructureRow *scope, Type *scopeType, ExprNode *expr, uint64_t scopeOffset);
    QString dynamicContainerAlias(StructureRow *row);
    QString dynamicArrayNameString(StructureRow *elementRow, ExprNode *dynamicArrayTagExpr);
    QString decodeNulTerminatedText(const QByteArray &bytes, bool wide) const;
    QString quoteString(const QString &text) const;

    StrataLibrary *m_library = nullptr;
    TypeDecl *m_rootType = nullptr;
    uint64_t m_baseOffset = 0;
    bool m_bigEndian = false;
    uint64_t m_structAlignment = 1;
    bool m_evaluatingEndian = false;
    StructureDisplayOptions m_options;
    StructureValueBuilder::ByteReader m_reader;
    StructureRow *m_rootRow = nullptr;
    std::vector<DynamicContainer> m_dynamicContainers;
    std::vector<DynamicRequest> m_dynamicRequests;
    std::vector<DynamicArrayRequest> m_dynamicArrayRequests;
};

#endif // STRUCTVIEW_STRUCTURERENDERENGINE_H
