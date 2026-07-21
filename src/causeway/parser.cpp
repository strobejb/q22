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

TypeDecl * ParseTypeDecl(Tag *tagList, SymbolTable &table, bool nested = false, bool allowMultiDecl = true, bool allowUnsizedArray = false);
const char *inenglish(TYPE ty);

namespace
{
void removeGlobalTagSymbol(StrataLibrary *library, Symbol *symbol)
{
	if(!library || !symbol)
		return;

	for(auto it = library->globalTagSymbolList.begin(); it != library->globalTagSymbolList.end(); ++it)
	{
		if(*it == symbol)
		{
			library->globalTagSymbolList.erase(it);
			return;
		}
	}
}

void discardUnpublishedTypeDecl(StrataLibrary *library, TypeDecl *typeDecl)
{
	if(!typeDecl)
		return;

	Type *base = BaseNode(typeDecl->baseType);
	if(base)
	{
		if((base->ty == typeSTRUCT || base->ty == typeUNION) && base->sptr)
		{
			Symbol *symbol = base->sptr->symbol;
			removeGlobalTagSymbol(library, symbol);
			if(symbol && symbol->type == base)
				symbol->type = 0;
			delete base->sptr;
			base->sptr = 0;
		}
		else if(base->ty == typeENUM && base->eptr)
		{
			Symbol *symbol = base->eptr->symbol;
			removeGlobalTagSymbol(library, symbol);
			if(symbol && symbol->type == base)
				symbol->type = 0;
			delete base->eptr;
			base->eptr = 0;
		}
	}

	delete typeDecl;
}

template <typename T>
bool vectorContainsPointer(const vector<T *> &items, T *needle)
{
	for(T *item : items)
		if(item == needle)
			return true;
	return false;
}

}

StrataLibrary::StrataLibrary()
{
	aliasesInstalled = false;
}

StrataLibrary::~StrataLibrary()
{
	Cleanup();
}

void StrataLibrary::Cleanup()
{
	size_t i;

	// Symbol objects in both lists are referenced by sym pointers inside Type
	// nodes owned by globalTypeDeclList. They must outlive the TypeDecl pass
	// below and are freed last. (Known leak: Symbol wrappers themselves are not
	// freed because no safe deletion order exists without a two-pass approach.)
	for(i = 0; i < globalIdentifierList.size(); i++)
	{
		// Symbol wrapper not deleted — referenced from Type::sym nodes in TypeDecls
		(void)globalIdentifierList[i];
	}

	// sptr/eptr are not freed by Type::~Type(), so this is the only place they
	// are released. Must run before globalTypeDeclList is deleted because the
	// Type nodes (and their sym pointers back to these Symbols) are still live.
	//
	// Named compound symbols can be referenced by copied Type chains. Nested
	// compound declarations add another trap: deleting a parent Structure deletes
	// nested TypeDecls, and those TypeDecls may own Type objects still referenced
	// by globalTagSymbolList. Therefore this must be two-phase:
	//  1. Read every currently-live symbol Type and collect unique payloads.
	//  2. Detach all tag symbols from those Type objects.
	//  3. Delete the collected payloads without reading globalTagSymbolList again.
	vector<Structure *> structuresToDelete;
	vector<Enum *> enumsToDelete;
	for(i = globalTagSymbolList.size(); i > 0; i--)
	{
		Symbol *s = globalTagSymbolList[i - 1];
		if(!s || !s->type)
			continue;

		switch(s->type->ty)
		{
		case typeENUM:
			if(s->type->eptr && !vectorContainsPointer(enumsToDelete, s->type->eptr))
				enumsToDelete.push_back(s->type->eptr);
			s->type->eptr = 0;
			break;

		case typeSTRUCT:
		case typeUNION:
			if(s->type->sptr && !vectorContainsPointer(structuresToDelete, s->type->sptr))
				structuresToDelete.push_back(s->type->sptr);
			s->type->sptr = 0;
			break;

		default:
			break;
		}

		s->type = 0;

		// Symbol wrapper not deleted — referenced from Type::sym nodes in TypeDecls
	}

	for(Structure *structure : structuresToDelete)
		delete structure;   // Structure* owned here
	for(Enum *enumPtr : enumsToDelete)
		delete enumPtr;     // Enum* owned here

	// TypeDecl destructor frees the Type chain (but not sptr/eptr — done above).
	for(i = 0; i < globalTypeDeclList.size(); i++)
	{
		delete globalTypeDeclList[i];
	}

	for(i = 0; i < globalTagSetList.size(); i++)
	{
		delete globalTagSetList[i];
	}

	for(i = 0; i < globalBitfieldList.size(); i++)
	{
		delete globalBitfieldList[i];
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
	globalBitfieldList.clear();
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

	Tag *copy = new Tag(tag->tok,
	                    CloneTagList(tag->link, tail),
	                    CopyExpr(tag->expr),
	                    CloneTagList(tag->elementTags));
	copy->byteSequence = tag->byteSequence;
	return copy;
}

bool Parser::IsContextualKeyword(TOKEN tok)
{
	// DEFINE_KEYWORD = usable as field names; DEFINE_RESERVED_KEYWORD = always reserved.
	switch(tok)
	{
#undef DEFINE_KEYWORD
#define DEFINE_KEYWORD(t, s) case t:
#define DEFINE_RESERVED_KEYWORD(t, s)
#include "keywords.h"
#undef DEFINE_KEYWORD
		return true;
	default:
		return false;
	}
}

static bool MagicByteValue(ExprNode *expr, uint8_t *byte)
{
	if(!expr || !byte)
		return false;

	switch(expr->type)
	{
	case EXPR_NUMBER:
	case EXPR_UNARY:
		break;
	default:
		return false;
	}

	const INUMTYPE value = Evaluate(expr);
	if(value > 0xff)
		return false;

	*byte = static_cast<uint8_t>(value);
	return true;
}

bool Parser::ParseByteSequence(vector<uint8_t> *bytes)
{
	if(!bytes)
		return false;

	if(!Expected('{'))
		return false;

	if(t == '}')
	{
		Error(ERROR_SYNTAX_ERROR, inenglish(t));
		return false;
	}

	for(;;)
	{
		ExprNode *item = ConditionalExpression();
		uint8_t byte = 0;
		if(!MagicByteValue(item, &byte))
		{
			delete item;
			Error(ERROR_OVERFLOW);
			return false;
		}

		delete item;
		bytes->push_back(byte);

		if(t == ',')
		{
			Advance();
			if(t == '}')
				break;
			continue;
		}

		if(t == '}')
			break;

		Expected(',');
		return false;
	}

	return Expected('}');
}

//
//	Parse any IDL-style tags, return into the Tag* linked-list
//
// Tag keywords allowed to wrap an individual argument of dynamic_array's,
// dynamic_struct's, or dynamic_container's parameter list, to flag that
// argument's role explicitly instead of leaving it implied by position:
//   name(x)          -- per-element name source / display label
//   case(x)          -- selector matched against the owning array index
//   terminated_by(x) -- per-element stop condition
//   optional(x)      -- whole-tag gating condition
//   nested(...)      -- open-as metadata for the generated dynamic row
// Add more here -- TOK_NULL-terminated -- as more wrapped roles are needed.
static TOKEN kDynamicTagValueWrappers[] = {
	TOK_NAME, TOK_CASE, TOK_TYPE, TOK_OFFSET, TOK_COUNT,
	TOK_MAXCOUNT, TOK_MAPPER, TOK_TERMINATEDBY, TOK_TERMINATOR, TOK_OPTIONAL, TOK_CONTAINER, TOK_OPENAS, TOK_NULL
};

static TOKEN kEmitTagValueWrappers[] = {
	TOK_DEST, TOK_LABEL, TOK_CASE, TOK_TYPE, TOK_OFFSET, TOK_COUNT,
	TOK_MAXCOUNT, TOK_TERMINATEDBY, TOK_TERMINATOR, TOK_OPTIONAL, TOK_MAP, TOK_NULL
};

static TOKEN kEmitRowTagValueWrappers[] = {
	TOK_DEST, TOK_CASE, TOK_OFFSET, TOK_OPTIONAL, TOK_MAP, TOK_NULL
};

static TOKEN kEmitNodeTagValueWrappers[] = {
	TOK_DEST, TOK_CASE, TOK_NAME, TOK_OFFSET, TOK_EXTENT, TOK_OPTIONAL, TOK_ATTR, TOK_FIELD, TOK_NULL
};

static TOKEN kCodeTagValueWrappers[] = {
	TOK_ARCHITECTURE, TOK_OFFSET, TOK_EXTENT, TOK_NULL
};

static TOKEN kOpenAsTagValueWrappers[] = {
	TOK_TYPE, TOK_OFFSET, TOK_EXTENT, TOK_NAME, TOK_TRANSFORM, TOK_OPTIONAL, TOK_NULL
};

static TOKEN kFormatTagValueWrappers[] = {
	TOK_WIDTH, TOK_NULL
};

static TOKEN kBitfieldFieldValueWrappers[] = {
	TOK_ENUM, TOK_NULL
};

static TOKEN kElementTags[] = {
	TOK_NAME, TOK_ENUM, TOK_BITFLAG, TOK_BITFIELD, TOK_FORMAT, TOK_TREE,
	TOK_CODE, TOK_OFFSETMAP, TOK_OPENAS, TOK_VIEW, TOK_WARN, TOK_ASSERT,
	TOK_DYNAMICARRAY, TOK_DYNAMICCONTAINER, TOK_DYNAMICSTRUCT,
	TOK_EMIT, TOK_EMITNODE, TOK_EMITROW,
	TOK_ENDIAN,
	TOK_TAGS,
	TOK_NULL
};

static void collectExpressionArgs(ExprNode *expr, vector<ExprNode *> *args)
{
	if(!expr || !args)
		return;

	if(expr->type == EXPR_COMMA)
	{
		collectExpressionArgs(expr->left, args);
		collectExpressionArgs(expr->right, args);
		return;
	}

	args->push_back(expr);
}

static ExprNode *findTagWrapExpr(ExprNode *expr, TOKEN tok)
{
	if(!expr)
		return 0;
	if(expr->type == EXPR_TAGWRAP && expr->tok == tok)
		return expr;
	if(ExprNode *found = findTagWrapExpr(expr->left, tok))
		return found;
	if(ExprNode *found = findTagWrapExpr(expr->right, tok))
		return found;
	return findTagWrapExpr(expr->cond, tok);
}

static char *bitfieldDisplayNameFromExpr(ExprNode *expr)
{
	if(!expr || !expr->str)
		return 0;
	if(expr->type == EXPR_STRINGBUF || expr->type == EXPR_IDENTIFIER)
		return strdup(expr->str);
	return 0;
}

bool Parser::ParseTags(Tag **tagList,
                       TOKEN allowed[],
                       bool allowTagSetUse,
                       TOKEN openToken,
                       TOKEN closeToken,
                       bool optional)
{
	Tag *	tag = 0;
	bool	foundtag = false;
	int		i;
	TOKEN	tmp;
	ExprNode *expr;

	if(openToken != TOK_NULL && t != openToken)
	{
		*tagList = 0;
		if(optional)
			return true;
		Expected(openToken);
		return false;
	}

	if(openToken != TOK_NULL)
		Advance();

	if(t == closeToken)
	{
		*tagList = 0;
		return Expected(closeToken);
	}

	while(t != closeToken)
	{
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
		case TOK_IGNORE:	case TOK_STRING:
		case TOK_ENTRYPOINT:
		case TOK_DEFAULT:
			tag = new Tag(t, tag);
			Advance();
			break;

		case TOK_SEMANTIC:
			tmp = t;
			Advance();
			if(t == TOKEN('('))
			{
				Advance();
				if((expr = Expression(TOK_NULL)) == 0)
					return false;
				tag = new Tag(tmp, tag, expr);
				if(!Expected(')'))
					return false;
			}
			else
			{
				tag = new Tag(tmp, tag);
			}
			break;

		// export optionally takes a description string: export or export("name")
		case TOK_EXPORT:
			tmp = t;
			Advance();
			if(t == TOKEN('('))
			{
				Advance();
				if((expr = Expression(TOK_NULL)) == 0)
					return false;
				tag = new Tag(tmp, tag, expr);
				if(!Expected(')'))
					return false;
			}
			else
			{
				tag = new Tag(tmp, tag);
			}
			break;

		case TOK_ELEMENT:
		{
			tmp = t;
			Advance();
			if(!Expected('('))
				return false;

			Tag *elementTags = 0;
			if(!ParseTags(&elementTags, kElementTags, true, TOK_NULL, TOKEN(')'), false))
				return false;

			tag = new Tag(tmp, tag, 0, elementTags);
			break;
		}

		case TOK_TAGS:
		{
			char tagSetName[MAX_STRING_LEN];
			Advance();

			if(!Expected('('))
				return false;

            safe_strcpy(tagSetName, MAX_STRING_LEN, t.str);
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

		case TOK_MAGIC:
		{
			tmp = t;
			Advance();

			if(!Expected('('))
				return false;

			std::vector<uint8_t> bytes;
			if(t != '{')
			{
				Error(ERROR_MAGIC_SYNTAX);
				return false;
			}
			if(!ParseByteSequence(&bytes))
				return false;

			expr = new ExprNode(EXPR_NUMBER, TOK_INUMBER);
			expr->val = 0;
			expr->base = DEC;
			tag = new Tag(tmp, tag, expr);
			tag->byteSequence = bytes;

			if(t == ',')
			{
				Advance();
				if(t != TOK_INUMBER)
				{
					Error(ERROR_MAGIC_SYNTAX);
					return false;
				}

				delete tag->expr;
				tag->expr = new ExprNode(EXPR_NUMBER, TOK_INUMBER);
				tag->expr->val = t.num;
				tag->expr->base = t.base;
				Advance();

				if(!Expected(')'))
					return false;
			}
			else if(!Expected(')'))
			{
				return false;
			}

			break;
		}

		// TAGS which take expression-parameters
		case TOK_OFFSET:	case TOK_ALIGN:
		case TOK_ALGORITHM:
		case TOK_BITFLAG:	case TOK_ENDIAN:
		case TOK_BITFIELD:
		case TOK_COUNTAS:
		case TOK_COUNT:
		case TOK_MAXCOUNT:
		case TOK_PADTO:
		case TOK_STYLE:		case TOK_SELECT:
		case TOK_CASE:
		case TOK_DISPLAY:
		case TOK_FORMAT:
		case TOK_NAME:		case TOK_ENUM:
		case TOK_EXTENT:
		case TOK_OPTIONAL:
		case TOK_TERMINATEDBY:
		case TOK_TERMINATOR:
		case TOK_ASSOC:
		case TOK_CATEGORY:
		case TOK_ARCHITECTURE:
		case TOK_CODE:
		case TOK_OFFSETMAP:
		case TOK_OPENAS:
		case TOK_DYNAMICARRAY:
		case TOK_DYNAMICCONTAINER:
		case TOK_DYNAMICSTRUCT:
		case TOK_EMIT:
		case TOK_EMITNODE:
		case TOK_EMITROW:
		case TOK_VERSION:
		case TOK_VIEW:
		case TOK_TREE:
		case TOK_WARN:
		case TOK_ASSERT:

			tmp = t;
			Advance();

			// count(expression)
			if(!Expected('('))
				return false;

			if(tmp == TOK_DYNAMICARRAY || tmp == TOK_DYNAMICCONTAINER || tmp == TOK_DYNAMICSTRUCT)
			{
				// comma-separated, each argument optionally wrapped as e.g.
				// name(DllName) to mark its role explicitly -- see TagArgList().
				if((expr = TagArgList(kDynamicTagValueWrappers)) == 0)
				{
					Error(ERROR_SYNTAX_ERROR, inenglish(t));
					return false;
				}
			}
			else if(tmp == TOK_EMIT)
			{
				if((expr = TagArgList(kEmitTagValueWrappers)) == 0)
				{
					Error(ERROR_SYNTAX_ERROR, inenglish(t));
					return false;
				}
			}
			else if(tmp == TOK_EMITROW)
			{
				if((expr = TagArgList(kEmitRowTagValueWrappers)) == 0)
				{
					Error(ERROR_SYNTAX_ERROR, inenglish(t));
					return false;
				}
			}
			else if(tmp == TOK_EMITNODE)
			{
				if((expr = TagArgList(kEmitNodeTagValueWrappers)) == 0)
				{
					Error(ERROR_SYNTAX_ERROR, inenglish(t));
					return false;
				}
			}
			else if(tmp == TOK_CODE)
			{
				if((expr = TagArgList(kCodeTagValueWrappers)) == 0)
				{
					Error(ERROR_SYNTAX_ERROR, inenglish(t));
					return false;
				}
			}
			else if(tmp == TOK_OPENAS)
			{
				if((expr = TagArgList(kOpenAsTagValueWrappers)) == 0)
				{
					Error(ERROR_SYNTAX_ERROR, inenglish(t));
					return false;
				}
			}
			else if(tmp == TOK_FORMAT)
			{
				if((expr = TagArgList(kFormatTagValueWrappers)) == 0)
				{
					Error(ERROR_SYNTAX_ERROR, inenglish(t));
					return false;
				}
			}
			else if(tmp == TOK_OFFSET || tmp == TOK_COUNT || tmp == TOK_MAXCOUNT || tmp == TOK_TERMINATEDBY || tmp == TOK_ASSOC || tmp == TOK_OFFSETMAP || tmp == TOK_WARN || tmp == TOK_ASSERT)
			{
				// full comma-separated expression 
				if((expr = CommaExpression(TOK_NULL)) == 0)
				{
					Error(ERROR_SYNTAX_ERROR, inenglish(t));
					return false;
				}
				if(tmp == TOK_OFFSET && expr->type == EXPR_COMMA && expr->right == 0)
				{
					ExprNode *singleExpr = expr->left;
					expr->left = 0;
					delete expr;
					expr = singleExpr;
				}
			}
			else
			{
				// simple expression (number/string)
				if((expr = Expression(TOK_NULL)) == 0)
					return false;
			}

			if(tmp == TOK_BITFIELD)
			{
				if(!expr || !expr->str || !LookupBitfield(expr->str))
				{
					Error(ERROR_UNKNOWN_BITFIELD, expr && expr->str ? expr->str : "<invalid>");
					delete expr;
					return false;
				}
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
		else if(t != closeToken)
		{
			Expected(',');
			return false;
		}
	}

	if(!Expected(closeToken))
		return false;

	*tagList = tag;

	return true;
}

TagSet * Parser::LookupTagSet(const char *name)
{
	for(size_t i = 0; i < typeLibrary->globalTagSetList.size(); i++)
	{
		TagSet *tagSet = typeLibrary->globalTagSetList[i];
		if(tagSet && strcmp(tagSet->name, name) == 0)
			return tagSet;
	}

	return 0;
}

Bitfield * Parser::LookupBitfield(const char *name)
{
	for(size_t i = 0; i < typeLibrary->globalBitfieldList.size(); i++)
	{
		Bitfield *bitfield = typeLibrary->globalBitfieldList[i];
		if(bitfield && strcmp(bitfield->name, name) == 0)
			return bitfield;
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
    safe_strcpy(fileName, MAX_STRING_LEN, t.str);
	if(!Expected(TOK_STRINGBUF))
		return 0;

	// terminating semi-colon
	if(t == ';')
	{
		FILE_DESC *includedFile = 0;
		// initialize the lexical-analyser with this new file
		if(lexer.FileIncluded(fileName, &includedFile))
		{
			(void)includedFile;
		}
		else
		{
			Parser p(this);
			p.SetErrorStream(fperr);
		
			if(!p.Ooof(fileName))
			{
				errcount += p.errcount;
				lasterr   =  p.lasterr;
                safe_strcpy(errstr, MAX_STRING_LEN, p.errstr);
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
    return new Statement(strdup(fileName));
}

TagSet * Parser::ParseTagSet(FILEREF fileRef)
{
	char tagSetName[MAX_STRING_LEN];
	Tag *tagList = 0;

	if(!Expected(TOK_TAGSET))
		return 0;

    safe_strcpy(tagSetName, MAX_STRING_LEN, t.str);
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
		TOK_COUNT, TOK_MAXCOUNT, TOK_IGNORE, TOK_STRING,
		TOK_OFFSET, TOK_ALIGN, TOK_BITFLAG, TOK_BITFIELD, TOK_COUNTAS, TOK_STYLE,
		TOK_DISPLAY,
		TOK_ENDIAN, TOK_SELECT, TOK_CASE, TOK_NAME, TOK_PADTO, TOK_DEFAULT,
		TOK_ENUM, TOK_ENTRYPOINT, TOK_EXTENT, TOK_OPTIONAL, TOK_EXPORT, TOK_ASSOC, TOK_CATEGORY, TOK_CODE, TOK_MAGIC, TOK_OFFSETMAP, TOK_OPENAS, TOK_VERSION, TOK_WARN, TOK_ASSERT,
		TOK_DYNAMICARRAY, TOK_DYNAMICCONTAINER, TOK_DYNAMICSTRUCT, TOK_EMIT, TOK_EMITNODE, TOK_EMITROW, TOK_ELEMENT, TOK_TERMINATEDBY, TOK_TERMINATOR, TOK_VIEW, TOK_FORMAT, TOK_TREE,
		TOK_SEMANTIC,
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

Bitfield * Parser::ParseBitfield(FILEREF fileRef)
{
	char bitfieldName[MAX_STRING_LEN];
	auto resolveEnumValue = [&](ExprNode *expr, INUMTYPE *value, char **name) -> bool
	{
		if(!expr || expr->type != EXPR_IDENTIFIER || !expr->str)
			return false;

		Symbol *sym = LookupSymbol(typeLibrary->globalIdentifierList, expr->str);
		if(!sym || !sym->type || sym->type->ty != typeENUMVALUE || !sym->type->evptr)
			return false;

		if(value)
			*value = sym->type->evptr->val;
		if(name)
			*name = strdup(sym->name);
		return true;
	};
	auto resolveMaskValue = [&](ExprNode *expr, INUMTYPE *value, char **name) -> bool
	{
		if(resolveEnumValue(expr, value, name))
			return true;
		if(!expr || expr->type == EXPR_STRINGBUF || expr->type == EXPR_TAGWRAP)
			return false;
		if(value)
			*value = Evaluate(expr);
		return true;
	};
	auto parseEntryArgs = [&](TOKEN entryToken, ExprNode **expr, vector<ExprNode *> *args) -> bool
	{
		if(!Expected(entryToken))
			return false;
		if(!Expected('('))
			return false;

		*expr = TagArgList(kBitfieldFieldValueWrappers);
		if(!*expr)
		{
			Error(ERROR_SYNTAX_ERROR, inenglish(t));
			return false;
		}

		collectExpressionArgs(*expr, args);
		if(!Expected(')'))
		{
			delete *expr;
			*expr = 0;
			return false;
		}

		return true;
	};

	if(!Expected(TOK_BITFIELD))
		return 0;

	safe_strcpy(bitfieldName, MAX_STRING_LEN, t.str);
	if(!Expected(TOK_IDENTIFIER))
		return 0;

	if(LookupBitfield(bitfieldName))
	{
		Error(ERROR_BITFIELD_REDEFINITION, bitfieldName);
		return 0;
	}

	Bitfield *bitfield = new Bitfield(bitfieldName);
	bitfield->fileRef = fileRef;

	if(!Expected('{'))
	{
		delete bitfield;
		return 0;
	}

	while(t != '}')
	{
		FILEREF entryRef(lexer.CurrentFile());
		TOKEN entryToken = t;

		if(entryToken != TOK_MATCH && entryToken != TOK_FIELD)
		{
			Error(ERROR_BITFIELD_ENTRY_SYNTAX);
			delete bitfield;
			return 0;
		}

		ExprNode *expr = 0;
		vector<ExprNode *> args;
		if(!parseEntryArgs(entryToken, &expr, &args))
		{
			delete bitfield;
			return 0;
		}

		BitfieldEntry *entry = new BitfieldEntry(entryToken == TOK_MATCH ? bitfieldMATCH : bitfieldFIELD, expr);
		entry->fileRef = entryRef;

		if(entryToken == TOK_MATCH)
		{
			if(args.empty() || args.size() > 2)
			{
				Error(ERROR_BITFIELD_ENTRY_SYNTAX);
				delete entry;
				delete bitfield;
				return 0;
			}

			size_t maskIndex = 0;
			if(args.size() == 2)
			{
				entry->displayName = bitfieldDisplayNameFromExpr(args[0]);
				if(!entry->displayName)
				{
					Error(ERROR_BITFIELD_ENTRY_SYNTAX);
					delete entry;
					delete bitfield;
					return 0;
				}
				maskIndex = 1;
			}

			if(!resolveMaskValue(args[maskIndex], &entry->maskValue, entry->displayName ? 0 : &entry->inferredName))
			{
				Error(ERROR_BITFIELD_ENTRY_SYNTAX);
				delete entry;
				delete bitfield;
				return 0;
			}

			entry->matchValue = entry->maskValue;

			if(t == '=')
			{
				Advance();
				entry->valueExpr = AssignmentExpression(TOK_NULL);
				if(!entry->valueExpr)
				{
					Error(ERROR_BITFIELD_ENTRY_SYNTAX);
					delete entry;
					delete bitfield;
					return 0;
				}

				char *rhsName = 0;
				if(resolveEnumValue(entry->valueExpr, &entry->matchValue, &rhsName))
				{
					if(!entry->displayName && !entry->inferredName)
						entry->inferredName = rhsName;
					else
						free(rhsName);
				}
				else if(!resolveMaskValue(entry->valueExpr, &entry->matchValue, 0))
				{
					Error(ERROR_BITFIELD_ENTRY_SYNTAX);
					delete entry;
					delete bitfield;
					return 0;
				}
			}

			if(!entry->displayName && !entry->inferredName)
			{
				Error(ERROR_BITFIELD_ENTRY_SYNTAX);
				delete entry;
				delete bitfield;
				return 0;
			}
		}
		else
		{
			if(args.size() < 2 || args.size() > 3)
			{
				Error(ERROR_BITFIELD_ENTRY_SYNTAX);
				delete entry;
				delete bitfield;
				return 0;
			}

			entry->displayName = bitfieldDisplayNameFromExpr(args[0]);
			if(!entry->displayName || !resolveMaskValue(args[1], &entry->maskValue, 0))
			{
				Error(ERROR_BITFIELD_ENTRY_SYNTAX);
				delete entry;
				delete bitfield;
				return 0;
			}

			if(args.size() == 3)
			{
				ExprNode *inner = args[2]->type == EXPR_TAGWRAP ? args[2]->left : 0;
				if(args[2]->type != EXPR_TAGWRAP || args[2]->tok != TOK_ENUM || !inner || !inner->str ||
					(inner->type != EXPR_IDENTIFIER && inner->type != EXPR_STRINGBUF))
				{
					Error(ERROR_BITFIELD_ENTRY_SYNTAX);
					delete entry;
					delete bitfield;
					return 0;
				}
				entry->valueEnumName = strdup(inner->str);
			}
		}

		if(!Expected(';'))
		{
			delete entry;
			delete bitfield;
			return 0;
		}

		lexer.FileRef(&entry->postRef);
		bitfield->entries.push_back(entry);
	}

	if(!Expected('}'))
	{
		delete bitfield;
		return 0;
	}
	if(!Expected(';'))
	{
		delete bitfield;
		return 0;
	}

	lexer.FileRef(&bitfield->postRef);
	return bitfield;
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

void Parser::AddIncludePath(const char *path)
{
	lexer.AddIncludePath(path);
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
			TOK_LENGTHIS, TOK_COUNT, TOK_MAXCOUNT, TOK_IGNORE, TOK_STRING,
			TOK_OFFSET, TOK_ALIGN, TOK_BITFLAG, TOK_BITFIELD, TOK_COUNTAS, TOK_STYLE, TOK_DESCRIPTION,
			TOK_DISPLAY,
			TOK_ENDIAN,	TOK_SELECT, TOK_CASE, TOK_NAME, TOK_PADTO, TOK_DEFAULT,
			TOK_ENUM, TOK_ENTRYPOINT, TOK_EXTENT, TOK_OPTIONAL, TOK_EXPORT, TOK_ASSOC, TOK_CATEGORY, TOK_CODE, TOK_MAGIC, TOK_OFFSETMAP, TOK_OPENAS, TOK_VERSION, TOK_WARN, TOK_ASSERT,
			TOK_DYNAMICARRAY, TOK_DYNAMICCONTAINER, TOK_DYNAMICSTRUCT, TOK_EMIT, TOK_EMITNODE, TOK_EMITROW, TOK_ELEMENT, TOK_TERMINATEDBY, TOK_TERMINATOR, TOK_VIEW, TOK_TAGS, TOK_FORMAT, TOK_TREE,
			TOK_SEMANTIC,
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

		case TOK_BITFIELD:
		{
			if(tagList)
			{
				Error(ERROR_ILLEGAL_TAG, inenglish(tagList->tok));
				delete tagList;
				return 0;
			}

			Bitfield *bitfield = ParseBitfield(fileRef);
			if(!bitfield)
				return 0;

			typeLibrary->globalBitfieldList.push_back(bitfield);
			lexer.CurrentFile()->stmtList.push_back(new Statement(bitfield));
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

			ExprNode *semanticExpr = 0;
			if(FindTag(typeDecl->tagList, TOK_SEMANTIC, &semanticExpr)
				&& semanticExpr
				&& semanticExpr->type == EXPR_IDENTIFIER)
			{
				TypeDecl *schemaDecl = semanticExpr->str ? LookupTypeDecl(semanticExpr->str) : 0;
				ExprNode *schemaMarker = 0;
				if(!schemaDecl
					|| !FindTag(schemaDecl->tagList, TOK_SEMANTIC, &schemaMarker)
					|| (schemaMarker && schemaMarker->type != EXPR_STRINGBUF))
				{
					Error(ERROR_UNKNOWN_SEMANTIC_SCHEMA, semanticExpr->str ? semanticExpr->str : "<invalid>");
					discardUnpublishedTypeDecl(typeLibrary, typeDecl);
					return 0;
				}
			}

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

	if(parent || (lexer.CurrentFile() && lexer.CurrentFile()->included))
	{
		// Included files contribute declarations, tagsets, enums and metadata to the
		// shared library, but they are not independent root formats for this parse.
		// Keep any [export] tags intact for source fidelity; suppress only the
		// effective export flag used by root discovery and legacy IsExportedStruct.
		SuppressExportsForFile(lexer.CurrentFile());
		return;
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

void Parser::SuppressExportsForFile(FILE_DESC *fileDesc)
{
	if(!fileDesc)
		return;

	for(size_t i = 0; i < typeLibrary->globalTypeDeclList.size(); i++)
	{
		TypeDecl *typeDecl = typeLibrary->globalTypeDeclList[i];

		if(typeDecl->baseType->ty == typeSTRUCT && typeDecl->fileRef.fileDesc == fileDesc)
		{
			typeDecl->exported = false;
			typeDecl->baseType->sptr->exported = false;
		}
	}
}

void Parser::Cleanup()
{
	if(ownsStrataLibrary)
		typeLibrary->Cleanup();
}

Parser::Parser()
{
	fperr		= stderr;
	parent		= 0;
	typeLibrary = new StrataLibrary();
	ownsStrataLibrary = true;
	lexer.SetStrataLibrary(typeLibrary);
	lexer.SetErrorCallback(LexerError, this);
	errcount = 0;
	errstr[0] = '\0';
	lasterr = ERROR_NONE;
	errcallback = 0;
	errparam = 0;
	Initialize();
}

Parser::Parser(StrataLibrary *lib)
{
	fperr		= stderr;
	parent		= 0;
	typeLibrary = lib ? lib : new StrataLibrary();
	ownsStrataLibrary = lib ? false : true;
	lexer.SetStrataLibrary(typeLibrary);
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
	ownsStrataLibrary = false;
	lexer.SetStrataLibrary(typeLibrary);
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
	if(ownsStrataLibrary)
		delete typeLibrary;
}

StrataLibrary * Parser::GetStrataLibrary()
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
