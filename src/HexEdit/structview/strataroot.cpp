#include "structview/strataroot.h"

#include <QByteArray>

namespace {

QString normalizedAssociation(QString extension)
{
    extension = extension.trimmed().toLower();
    if (!extension.isEmpty() && !extension.startsWith(QLatin1Char('.')))
        extension.prepend(QLatin1Char('.'));
    return extension;
}

} // namespace

int strataRootIndexForFileName(const QList<ExportedStructureType> &exportedTypes,
                               const QString &fileName)
{
    const QString lowerFileName = fileName.toLower();
    if (lowerFileName.isEmpty())
        return -1;

    // Prefer an exact suffix, then allow compound extensions such as .tar.gz.
    for (int i = 0; i < exportedTypes.size(); ++i)
        for (QString extension : exportedTypes[i].assocExtensions)
        {
            extension = normalizedAssociation(extension);
            if (!extension.isEmpty() && lowerFileName.endsWith(extension))
                return i;
        }

    for (int i = 0; i < exportedTypes.size(); ++i)
        for (QString extension : exportedTypes[i].assocExtensions)
        {
            extension = normalizedAssociation(extension);
            if (!extension.isEmpty() && lowerFileName.contains(extension + QLatin1Char('.')))
                return i;
        }

    return -1;
}

int associatedStrataRootIndex(const QList<ExportedStructureType> &exportedTypes,
                              const QString &fileName,
                              const StrataRootByteReader &reader)
{
    const int byName = strataRootIndexForFileName(exportedTypes, fileName);
    if (byName >= 0)
        return byName;

    for (int i = 0; i < exportedTypes.size(); ++i)
    {
        for (const StructureMagicSignature &signature : exportedTypes[i].magicSignatures)
        {
            if (signature.bytes.isEmpty())
                continue;
            QByteArray actual(signature.bytes.size(), Qt::Uninitialized);
            const size_t got = reader(signature.offset,
                                      reinterpret_cast<uint8_t *>(actual.data()),
                                      static_cast<size_t>(actual.size()));
            if (got == static_cast<size_t>(actual.size()) && actual == signature.bytes)
                return i;
        }
    }

    return -1;
}

TypeDecl *associatedStrataRoot(const QList<ExportedStructureType> &exportedTypes,
                               const QString &fileName,
                               const StrataRootByteReader &reader)
{
    const int index = associatedStrataRootIndex(exportedTypes, fileName, reader);
    return index >= 0 ? exportedTypes[index].typeDecl : nullptr;
}

bool strataRootExplicitOffset(TypeDecl *rootType, uint64_t *offset)
{
    ExprNode *offsetExpr = nullptr;
    if (!rootType || !offset || !FindTag(rootType->tagList, TOK_OFFSET, &offsetExpr) || !offsetExpr)
        return false;

    switch (offsetExpr->type)
    {
    case EXPR_NUMBER:
    case EXPR_UNARY:
    case EXPR_BINARY:
    case EXPR_TERTIARY:
    {
        const INUMTYPE value = Evaluate(offsetExpr);
        *offset = value >= 0 ? static_cast<uint64_t>(value) : 0;
        return true;
    }
    default:
        return false;
    }
}

uint64_t strataRootBaseOffset(TypeDecl *rootType)
{
    uint64_t offset = 0;
    return strataRootExplicitOffset(rootType, &offset) ? offset : 0;
}
