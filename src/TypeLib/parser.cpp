//
//  parser.cpp
//
//  www.catch22.net
//
//  Copyright (C) 2012 James Brown
//  Please refer to the file LICENCE.TXT for copying permission
//

#define _CRT_SECURE_NO_DEPRECATE

#include "parser.h"

TypeDecl * ParseTypeDecl(Tag *tagList, SymbolTable &table, bool nested = false, bool allowMultiDecl = true);
const char *inenglish(TYPE ty);

TypeLibrary::TypeLibrary()
{
	aliasesInstalled = false;
}

TypeLibrary::~TypeLibrary()
{
	Cleanup();
}

void TypeLibrary::Cleanup()
{
	size_t i;

	for(i = 0; i < globalIdentifierList.size(); i++)
	{
		Symbol *s = globalIdentifierList[i];
//		delete s;
	}

	for(i = globalTagSymbolList.size(); i > 0; i--)
	{
		Symbol *s = globalTagSymbolList[i - 1];

		if(s->type)
		{
			if(s->type->ty == typeENUM)
				delete s->type->eptr;
			else
				delete s->type->sptr;
		}

//		delete s;
	}

	for(i = 0; i < globalTypeDeclList.size(); i++)
	{
		delete globalTypeDeclList[i];
	}

	for(i = 0; i < globalTagSetList.size(); i++)
	{
		delete globalTagSetList[i];
	}

	for(i = 0; i < globalFileHistory.size(); i++)
	{
		free(globalFileHistory[i]->buf);
		delete globalFileHistory[i];
	}

	globalIdentifierList.clear();
	globalTagSymbolList.clear();
	globalTypeDeclList.clear();
	globalTagSetList.clear();
	globalFileHistory.clear();
	aliasesInstalled = false;
}

Tag * FindTag(Tag *tag, TOKEN tok, ExprNode **expr /*= 0*/)
{
	for( ; tag; tag = tag->link)
	{
		if(tag->tok == tok)
		{
			if(expr)
				*expr = tag->expr;

			return tag;
		}
	}

	if(expr)
		*expr = 0;
	return 0;
}

static Tag *CloneTagList(Tag *tag, Tag *tail = 0)
{
	if(!tag)
		return tail;

	return new Tag(tag->tok, CloneTagList(tag->link, tail), CopyExpr(tag->expr));
}

//
//	Parse any IDL-style tags, return into the Tag* linked-list
//
bool Parser::ParseTags(Tag **tagList, TOKEN allowed[], bool allowTagSetUse)
{
	Tag *	tag = 0;
	bool	foundtag = false;
	int		i;
	TOKEN	tmp;
	ExprNode *expr;

	if(t != '[')
	{
		*tagList = 0;
		return true;
	}

	Advance();

	while(t != ']')
	{
		foundtag = true;

		if(t == TOK_LENGTHIS)
		{
			Error(ERROR_RESERVED_KEYWORD, inenglish(t));
			return false;
		}

		if(t == TOK_TAGS && !allowTagSetUse)
		{
			Error(ERROR_TAGS_NOT_ALLOWED_IN_TAGSET);
			return false;
		}

		//
		//	Make sure that the tag is allowed
		//
		for(i = 0; allowed[i]; i++)
		{
			if(allowed[i] == t)
				break;
		}

		if(allowed[i] == TOK_NULL)
		{
			if(t == TOK_IDENTIFIER)
				Error(ERROR_NOTA_TAG, t.str);
			else
				Error(ERROR_ILLEGAL_TAG, t.str);

			return false;
		}

		//
		//	Parse the tag and any parameters it might have
		//
		switch(t.kind)
		{
		// TAGS which don't take any parameters
		case TOK_IGNORE:	case TOK_STRING:	case TOK_EXPORT:
			tag = new Tag(t, tag);
			Advance();
			break;

		case TOK_TAGS:
		{
			char tagSetName[MAX_STRING_LEN];
			Advance();

			if(!Expected('('))
				return false;

			strcpy(tagSetName, t.str);
			if(!Expected(TOK_IDENTIFIER))
				return false;

			TagSet *tagSet = LookupTagSet(tagSetName);
			if(!tagSet)
			{
				Error(ERROR_UNKNOWN_TAGSET, tagSetName);
				return false;
			}

			tag = CloneTagList(tagSet->tagList, tag);

			if(!Expected(')'))
				return false;

			break;
		}

		// TAGS which take expression-parameters
		case TOK_OFFSET:	case TOK_ALIGN:	 
		case TOK_BITFLAG:	case TOK_ENDIAN:
		case TOK_SIZEIS:
		case TOK_STYLE:		case TOK_SWITCHIS:
		case TOK_CASE:		case TOK_DESCRIPTION:
		case TOK_DISPLAY:
		case TOK_NAME:		case TOK_ENUM:
		case TOK_ENTRYPOINT:
		case TOK_ASSOC:		case TOK_OFFSETMAP:
		case TOK_DYNAMICCONTAINER:
		case TOK_DYNAMICSTRUCT:
		case TOK_VIEW:

			tmp = t;
			Advance();

			// size_is(expression)
			if(!Expected('('))
				return false;

			if(tmp == TOK_SIZEIS || tmp == TOK_ASSOC
				|| tmp == TOK_OFFSETMAP || tmp == TOK_DYNAMICCONTAINER || tmp == TOK_DYNAMICSTRUCT)
			{
				// full comma-separated expression 
				if((expr = CommaExpression(TOK_NULL)) == 0)
				{
					Error(ERROR_SYNTAX_ERROR, inenglish(t));
					return false;
				}
			}
			else
			{
				// simple expression (number/string)
				if((expr = Expression(TOK_NULL)) == 0)
					return false;
			}

			tag = new Tag(tmp, tag, expr);

			if(!Expected(')'))
				return false;

			break;

		default:
			return false;
		}

		// comma's mean another parameter so loop again
		if(t == ',')
		{
			Advance();
			continue;
		}
		// anything else is an error
		else if(t != ']')
		{
			Expected(',');
			return false;
		}
	}

	if(!Expected(']'))
		return false;

	*tagList = tag;

	return true;
}

TagSet * Parser::LookupTagSet(char *name)
{
	for(size_t i = 0; i < typeLibrary->globalTagSetList.size(); i++)
	{
		TagSet *tagSet = typeLibrary->globalTagSetList[i];
		if(tagSet && strcmp(tagSet->name, name) == 0)
			return tagSet;
	}

	return 0;
}

//
//	Parse any 'include "filename"; ' statements
//	
Statement * Parser::ParseInclude()
{
	char fileName[MAX_STRING_LEN];

	// 'include'
	if(!Expected(TOK_INCLUDE))
		return 0;

	// "filename"
	strcpy(fileName, t.str);
	if(!Expected(TOK_STRINGBUF))
		return 0;

	// terminating semi-colon
	if(t == ';')
	{
		// initialize the lexical-analyser with this new file
		if(lexer.FileIncluded(fileName) == false)
		{
			Parser p(this);
			p.SetErrorStream(fperr);
		
			if(!p.Ooof(fileName))
			{
				errcount += p.errcount;
				lasterr   =  p.lasterr;
				strcpy(errstr, p.errstr);
				return 0;
			}
		}

		Expected(';');
	}
	else
	{
		Expected(';');
		return 0;
	}

	// start parsing!
	//Advance();
	return new Statement(_strdup(fileName));
}

TagSet * Parser::ParseTagSet(FILEREF fileRef)
{
	char tagSetName[MAX_STRING_LEN];
	Tag *tagList = 0;

	if(!Expected(TOK_TAGSET))
		return 0;

	strcpy(tagSetName, t.str);
	if(!Expected(TOK_IDENTIFIER))
		return 0;

	if(LookupTagSet(tagSetName))
	{
		Error(ERROR_TAGSET_REDEFINITION, tagSetName);
		return 0;
	}

	TagSet *tagSet = new TagSet(tagSetName);
	tagSet->fileRef = fileRef;

	lexer.FileRef(&tagSet->tagRef);
	if(t != '[')
	{
		delete tagSet;
		Expected('[');
		return 0;
	}

	TOKEN allowed[] =
	{
		TOK_SIZEIS, TOK_IGNORE, TOK_STRING,
		TOK_OFFSET, TOK_ALIGN, TOK_BITFLAG, TOK_STYLE, TOK_DESCRIPTION,
		TOK_DISPLAY,
		TOK_ENDIAN, TOK_SWITCHIS, TOK_CASE, TOK_NAME,
		TOK_ENUM, TOK_ENTRYPOINT, TOK_EXPORT, TOK_ASSOC, TOK_OFFSETMAP,
		TOK_DYNAMICCONTAINER, TOK_DYNAMICSTRUCT, TOK_VIEW,
		TOK_NULL
	};

	if(!ParseTags(&tagList, allowed, false))
	{
		delete tagSet;
		return 0;
	}

	tagSet->tagList = tagList;

	if(!Expected(';'))
	{
		delete tagSet;
		return 0;
	}

	lexer.FileRef(&tagSet->postRef);
	return tagSet;
}

void Parser::Initialize()
{
	// install DWORD, WORD, BYTE etc
	if(!typeLibrary->aliasesInstalled)
	{
		InstallTypeAliases();
		typeLibrary->aliasesInstalled = true;
	}
}

void Parser::Advance()
{
	t = lexer.Next();
}

bool Parser::Init(const char *file)
{
//	fileStack.clear();

	errcount = 0;
	lasterr = ERROR_NONE;
	errstr[0] = '\0';

	if(!lexer.InitFile(file))
	{
		return false;
	}

	Advance();

	return true;
}

bool Parser::Init(const char *buf, size_t len)
{	
//	fileStack.clear();
	errcount = 0;
	lasterr = ERROR_NONE;
	errstr[0] = '\0';

	if(!lexer.InitBuffer(buf, len))
		return false;

	Advance();

	return true;
}

bool Parser::Init(const wchar_t *buf, size_t len)
{
	char *b = new char[len+1];
	wcstombs(b, buf, len);
	b[len] = '\0';
	return Init(b, len);
}

bool Parser::Ooof(const char *file)
{
	if(!Init(file))
		return false;

	return Parse() ? true : false;
}

//
//	associate("*.txt", "*.exe");
//
/*
bool Parser::ParseAssociate()
{
	char str[MAX_STRING_LEN];

	if(!Expected(TOK_ASSOC))
		return false;

	if(!Expected('('))
		return false;

	for(;;)
	{
		strcpy(str, t.str);
		if(!Expected(TOK_STRINGBUF))
			return 0;

		if(t == ',')
		{
			Advance();
		}
		else if(t == ')')
		{
			break;
		}
		else
		{
			Unexpected(t);
			return false;
		}
	} 

	if(!Expected(')'))
		return false;

	if(!Expected(';'))
		return false;

	return true;
}
*/

int Parser::Parse()
{
	Tag *tagList;
	TypeDecl *typeDecl;
	Statement *stmt;


	// keep going until there are no more tokens!
	while(t)
	{
		TOKEN allowed[] = 
		{ 
			TOK_LENGTHIS, TOK_SIZEIS, TOK_IGNORE, TOK_STRING,
			TOK_OFFSET, TOK_ALIGN, TOK_BITFLAG, TOK_STYLE, TOK_DESCRIPTION,
			TOK_DISPLAY,
			TOK_ENDIAN,	TOK_SWITCHIS, TOK_CASE, TOK_NAME, 
			TOK_ENUM, TOK_ENTRYPOINT, TOK_EXPORT, TOK_ASSOC, TOK_OFFSETMAP,
			TOK_DYNAMICCONTAINER, TOK_DYNAMICSTRUCT, TOK_VIEW, TOK_TAGS,
			TOK_NULL 

		};

		// save any whitespace before the tags
		FILEREF fileRef;
		lexer.FileRef(&fileRef);

		if(!ParseTags(&tagList, allowed))
			return 0;
		
		//
		//	Decide what kind of statement/construct we need to parse 
		//
		switch(t.kind)
		{
		case TOK_TAGSET:
		{
			if(tagList)
			{
				Error(ERROR_ILLEGAL_TAG, inenglish(tagList->tok));
				delete tagList;
				return 0;
			}

			TagSet *tagSet = ParseTagSet(fileRef);
			if(!tagSet)
				return 0;

			typeLibrary->globalTagSetList.push_back(tagSet);
			lexer.CurrentFile()->stmtList.push_back(new Statement(tagSet));
			break;
		}

		case TOK_INCLUDE:
			
			// include-statement (not the same as #include which
			// is a C-preprocessor thing)
			if((stmt = ParseInclude()) == 0)
			{
				return 0;
			}

			lexer.CurrentFile()->stmtList.push_back(stmt);

			break;

		default:

			// anything else must be a type-declaration
			if((typeDecl = ParseTypeDecl(tagList, typeLibrary->globalIdentifierList, false, true)) == 0)
				return 0;

			// store in the global list
			typeDecl->fileRef = fileRef;
			typeLibrary->globalTypeDeclList.push_back(typeDecl);

			//lexer.CurrentFile()->typeDeclList.push_back(typeDecl);

			lexer.CurrentFile()->stmtList.push_back(new Statement(typeDecl));

			// every type-declaration must end with a ';'
			if(!Expected(';'))
				return 0;

			// record any whitespace that appears after the type-decl
			lexer.FileRef(&typeDecl->postRef);
			 
			break;
		}
	}

	ExportStructs();

	return (errcount == 0) ? 1 : 0;
}

void Parser::ExportStructs()
{
	bool foundExport = false;

	// go through every struct that we parsed in THIS file
	for(size_t i = 0; i < typeLibrary->globalTypeDeclList.size(); i++)
	{
		TypeDecl *typeDecl = typeLibrary->globalTypeDeclList[i];

		if(typeDecl->baseType->ty == typeSTRUCT && typeDecl->fileRef.fileDesc == lexer.CurrentFile())
		{
			if(FindTag(typeDecl->tagList, TOK_EXPORT, 0))
				foundExport = true;
		}
	}

	if(foundExport)
	{
		// clear the export flag on all structs
		for(size_t i = 0; i < typeLibrary->globalTypeDeclList.size(); i++)
		{
			TypeDecl *typeDecl = typeLibrary->globalTypeDeclList[i];

			if(typeDecl->baseType->ty == typeSTRUCT && typeDecl->fileRef.fileDesc == lexer.CurrentFile())
			{
				typeDecl->exported = false;
				typeDecl->baseType->sptr->exported = false;
			}
		}

		// set the export flag on explicity defined structs (with the "export" attribute)
		for(size_t i = 0; i < typeLibrary->globalTypeDeclList.size(); i++)
		{
			TypeDecl *typeDecl = typeLibrary->globalTypeDeclList[i];

			if(typeDecl->baseType->ty == typeSTRUCT && typeDecl->fileRef.fileDesc == lexer.CurrentFile())
			{
				if(FindTag(typeDecl->tagList, TOK_EXPORT, 0))
				{
					typeDecl->exported = true;
					typeDecl->baseType->sptr->exported = true;
				}
			}
		}
	}
}

void Parser::Cleanup()
{
	if(ownsTypeLibrary)
		typeLibrary->Cleanup();

	/*for(i = 0; i < 100; i++)
	{
		Type *type = smegHead[i];
		if(type)
		{
			printf("[%d] %s", i, ::inenglish(type->ty));

			if(type->ty == typeIDENTIFIER || type->ty == typeTYPEDEF)
			{
				printf("   %s\n", type->sym->name);
			}
			else
			{
				printf("\n");
			}
		}
	}*/

}

Parser::Parser()
{
	fperr		= stderr;
	parent		= 0;
	typeLibrary = new TypeLibrary();
	ownsTypeLibrary = true;
	lexer.SetTypeLibrary(typeLibrary);
	lexer.SetErrorCallback(LexerError, this);
	errcount = 0;
	errstr[0] = '\0';
	lasterr = ERROR_NONE;
	errcallback = 0;
	errparam = 0;
	Initialize();
}

Parser::Parser(TypeLibrary *lib)
{
	fperr		= stderr;
	parent		= 0;
	typeLibrary = lib ? lib : new TypeLibrary();
	ownsTypeLibrary = lib ? false : true;
	lexer.SetTypeLibrary(typeLibrary);
	lexer.SetErrorCallback(LexerError, this);
	errcount = 0;
	errstr[0] = '\0';
	lasterr = ERROR_NONE;
	errcallback = 0;
	errparam = 0;
	Initialize();
}

Parser::Parser(Parser *p)
{
	fperr		= stderr;
	parent		= p;
	typeLibrary = p->typeLibrary;
	ownsTypeLibrary = false;
	lexer.SetTypeLibrary(typeLibrary);
	lexer.SetParentLexer(&p->lexer);
	lexer.SetErrorCallback(LexerError, this);
	errcount = 0;
	errstr[0] = '\0';
	lasterr = ERROR_NONE;
	errcallback = 0;
	errparam = 0;
}

Parser::~Parser()
{
	if(ownsTypeLibrary)
		delete typeLibrary;
}

TypeLibrary * Parser::GetTypeLibrary()
{
	return typeLibrary;
}



extern "C" void * AllocParser(const char *buf, size_t len)
{
	Parser *p = new Parser();

	if(p && p->Init(buf, len))
	{
		return (void *)p;
	}
	else
	{
		delete p;
		return 0;
	}
}

extern "C" TOKEN nexttok(void *p)
{
	Parser *parser = (Parser *)p;

	return parser->nexttok();
}

extern "C" INUMTYPE INUM(void *p)
{
	Parser *parser = (Parser *)p;
	return parser->INUM();
}

extern "C" FNUMTYPE FNUM(void *p)
{
	Parser *parser = (Parser *)p;
	return parser->FNUM();
}
