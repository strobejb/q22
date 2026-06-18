#ifndef STRUCTVIEW_STRUCTURECOMMENTFORMATTER_H
#define STRUCTVIEW_STRUCTURECOMMENTFORMATTER_H

#include "TypeLib/parser.h"

#include <QString>

inline QString structureDisplayComment(const FILEREF &fileRef)
{
    FILEREF ref = fileRef;
    char *s = nullptr;
    char *cs = nullptr;
    char *ce = nullptr;
    char *e = nullptr;
    if (!LocateComment(&ref, &s, &cs, &ce, &e))
        return {};

    return QString::fromLocal8Bit(cs, qsizetype(ce - cs)).trimmed();
}

inline QString structureDisplayComment(TypeDecl *typeDecl)
{
    if (!typeDecl)
        return {};

    if (typeDecl->comment)
        return QString::fromLocal8Bit(typeDecl->comment).trimmed();

    return structureDisplayComment(typeDecl->postRef);
}

#endif // STRUCTVIEW_STRUCTURECOMMENTFORMATTER_H
