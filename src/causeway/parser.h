//
//  parser.h
//
//  www.catch22.net
//
//  Copyright (C) 2012 James Brown
//  Please refer to the file LICENCE.TXT for copying permission
//

#ifndef PARSER_INCLUDED
#define PARSER_INCLUDED

#define _CRT_SECURE_NO_DEPRECATE

#include <stdio.h>

// Safe strcpy: truncates to fit, always null-terminates. dst must be an array.
#define safe_strcpy_a(dst, src)    snprintf((dst), sizeof(dst), "%s", (src))
#define safe_strcpy(dst, dstsize, src) snprintf((dst), dstsize, "%s", (src))

#include "lexer.h"

#ifdef __cplusplus

#include "error.h"
#include "expr.h"
#include "types.h"
#include "stmt.h"

bool LocateComment(FILEREF *fileRef, char **s, char **cs, char **ce, char **e);

//#include "lexer.h"
//#include "error.h"
//#include "expr.h"
//#include "types.h"

struct StrataLibrary
{
	StrataLibrary();
	~StrataLibrary();

	void Cleanup();

	// These tables used to be process-wide globals. Keeping the old names here
	// makes the refactor mechanical while giving each parser/library its own
	// type universe and source-file lifetime.
	SymbolTable	globalIdentifierList;
	SymbolTable globalTagSymbolList;
	TypeDeclList globalTypeDeclList;
	TagSetList globalTagSetList;
	vector <FILE_DESC *> globalFileHistory;

	bool aliasesInstalled;
};

class Parser
{
public:
	Parser();
	Parser(StrataLibrary *typeLibrary);
	~Parser();

	bool Ooof(const char *file);
	void SetErrorStream(FILE *fperr);
	void SetErrorCallback(ERROR_CALLBACK callback, void *param);
	void AddIncludePath(const char *path);

	void Initialize();
	bool Init(const char *file);
	bool Init(const char *buf, size_t len);
	bool Init(const wchar_t *buf, size_t len);
	//int  Parse(const char *file);
	//int  Parse(const char *buf, size_t len);
	int Parse();

	TypeDecl * ParseTypeDecl(Tag *tagList, SymbolTable &symTable, bool nested /*=false*/, bool allowMultiDecl /*=true*/);
	ExprNode * ParseExpression();

	
	static const char *inenglish(TOKEN t);
	static bool IsContextualKeyword(TOKEN t);

	ERROR LastErr();
	const char *LastErrStr();
	StrataLibrary *GetStrataLibrary();
	void Dump(FILE *fp);
	void Dump2(FILE *fp);

	TOKEN nexttok()
	{
		TOKEN tmp = t.kind;
		Advance();
		return tmp;
	}

	INUMTYPE INUM() { return t.num; }
	FNUMTYPE FNUM() { return t.fnum; }
	
private:

	Parser(Parser *p);

	// parser
	bool		ParseTags(Tag **tagList, TOKEN allowed[], bool allowTagSetUse = true);
	bool		ParseByteSequence(vector<uint8_t> *bytes);
	Statement * ParseInclude();
	TagSet   * ParseTagSet(FILEREF fileRef);
	void		ExportStructs();
	void		Cleanup();
	void		InstallTypeAliases();
    TypeDecl *	LookupTypeDecl(const char *name);
    TagSet   *  LookupTagSet(const char *name);
    Type	 *	MakeTypeDef(TYPE base, const char *name, TYPE base2 = typeNULL);
    Enum	 *	FindEnum(const char *enumName);

	// typedecls
	Type	 * ParseBaseType(TypeDecl *typeDecl, bool nested);
	Type	 * ParseStructBody(Symbol *sym, TYPE ty);
	Type	 * ParseEnumBody(Symbol *sym);
    EnumField* AddEnumField(Enum *enumPtr, const char *name, ExprNode *expr, unsigned val);
	Type	 * Decl(TOKEN term, SymbolTable &symTable);
	Type	 * PrefixDecl(SymbolTable &symTable);
	Type	 * PostfixDecl(Type *tptr);
	Function * ParseFuncDecl(Symbol *sym);

	// expressions
	ExprNode * PrimaryExpression();
	ExprNode * PostfixExpression(ExprNode *p);
	ExprNode * UnaryExpression(void);
	ExprNode * BinaryExpression(int k);
	ExprNode * ConditionalExpression(void);
	ExprNode * AssignmentExpression(TOKEN term);
	ExprNode * Expression(TOKEN term);
	ExprNode * FullExpression(TOKEN term);
	ExprNode * CommaExpression(TOKEN tok);

	// Tag-parameter argument lists, e.g. dynamic_array(...)'s comma-separated
	// arguments. Like CommaExpression, but each argument may optionally be
	// written as wrapperKeyword(value) -- one of 'wrappers' (TOK_NULL-terminated)
	// -- producing an EXPR_TAGWRAP node instead of a plain expression. Parsed
	// directly here, not via the expression grammar, so this stays scoped to
	// tag-parameter position only.
	ExprNode * TagWrappedArg(TOKEN wrappers[]);
	ExprNode * TagArgList(TOKEN wrappers[]);


	// error handling
	const char *inenglish(TOKEN t, bool use_t_state);
	void Error(ERROR err, ...);
	void ErrorV(ERROR err, va_list vargs);
	static void LexerError(void *context, ERROR err, va_list vargs);
	bool Test(TOKEN tok);
	bool Expected(TOKEN tok, const char *terminal_desc = 0);
	bool Expected(int tok, const char *terminal_desc = 0);
	void Unexpected(TOKEN tok);

	void Advance();

	Lexer			lexer;
	Token			t;

	// parser
	Parser		*	parent;
	StrataLibrary *	typeLibrary;
	bool			ownsStrataLibrary;

	// errors
	int				errcount;
	char			errstr[200];
	ERROR			lasterr;
	FILE		  * fperr;
	ERROR_CALLBACK  errcallback;
	void          * errparam;

};

Tag * FindTag(Tag *tag, TOKEN tok, ExprNode **expr /*= 0*/);

#else

void * AllocParser(const char * buf, size_t len);
TOKEN    nexttok(void * p);
INUMTYPE INUM(void *p);
FNUMTYPE FNUM(void *p);

#endif


#endif
