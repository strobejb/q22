//
//  stmt.h
//
//  www.catch22.net
//
//  Copyright (C) 2012 James Brown
//  Please refer to the file LICENCE.TXT for copying permission
//

#ifndef STMT_INCLUDED
#define STMT_INCLUDED

enum STATEMENT
{
	stmtINCLUDE,
	stmtTAGSET,
	stmtTYPEDECL,
};

struct TagSet;

struct Statement
{
    //Statement()					: str(0),		stmtType(stmtINVALID),  expr(0) {}
	Statement(char *s)			: str(s),		stmtType(stmtINCLUDE),  expr(0) {}
    Statement(TagSet *t)		: tagSet(t),	stmtType(stmtTAGSET),   expr(0) {}
	Statement(TypeDecl *t)		: typeDecl(t),	stmtType(stmtTYPEDECL), expr(0) {}

	STATEMENT	stmtType;
	FILEREF		fileRef;

	union
	{
		char		*str;			// 'include' statements
		TagSet		*tagSet;		// reusable tag aliases
		TypeDecl	*typeDecl;		// any type decl
	};

	ExprNode *	expr;				// used for "typedecl =" statements
};

typedef vector<Statement *> StatementList;


#endif
