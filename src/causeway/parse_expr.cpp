//
//  parse_expr.cpp
//
//  www.catch22.net
//
//  Copyright (C) 2012 James Brown
//  Please refer to the file LICENCE.TXT for copying permission
//
//  ---------------------------------------------------------
//	'Top Down' Recursive Descent Parser for C-style expressions
//
//	Derived from the Fraser&Hanson LCC compiler described in 
//	the book "A Retargetable C Compiler"
//

#define _CRT_SECURE_NO_DEPRECATE
#include <string.h>
#include <stdio.h>

#include <string>

#include "parser.h"
#include "stringprint.h"

#if (_MSC_VER == 1300)
#define _strdup strdup
#endif

#define ASSIGNMENT_EXPRESSIONS
//#define FUNCTION_EXPRESSIONS
//#define INCDEC_EXPRESSIONS

//
//	Binary operator precedence lookup. Defines the grouping of binary sub-expressions
//
static int Precedence(int t)
{
	switch((int)t)
	{
	// unary operators at 14+
	// '++', '--', '*', '&', '+', '-', '~', '!'

	// binary operators at 4 to 13 inclusive
	case '*': case '/': case '%':	return 13;
	case '+': case '-':				return 12;
	case TOK_SHL: case TOK_SHR:		return 11;
	case '<': case '>':				
	case TOK_LE : case TOK_GE:		return 10;
	case TOK_EQU: case TOK_NEQ:		return 9;
	case '&':						return 8;
	case '^':						return 7;
	case '|':						return 6;
	case TOK_ANDAND:				return 5;
	case TOK_OROR:					return 4;

	// not referenced
	case '?':						return 3;	
	case '=':						return 2;	
	case ',':						return 1;	
	default:						return 0;
	}
}

//
//	primary:	<number>
//				<identifier>
//				<string-literal>
//
ExprNode * Parser::PrimaryExpression()
{
	ExprNode *p = 0;

	if(IsContextualKeyword(t.kind))
	{
		p = new ExprNode(EXPR_IDENTIFIER, t);
        p->str = strdup(t.str);
		Advance();
		return p;
	}

	switch(t.kind)
	{
	// Integer number
	case TOK_INUMBER:
		
		p = new ExprNode(EXPR_NUMBER, t);
		p->val		= t.num;
		p->base		= t.base;
		Advance();
		break;

	// Floating-point number
	case TOK_FNUMBER:

		p = new ExprNode(EXPR_NUMBER, t);
		p->fval		= t.fnum;
		p->base		= DEC;
		Advance();
		break;

	// Identifier name (variable)
	case TOK_IDENTIFIER: 

		p = new ExprNode(EXPR_IDENTIFIER, t);
        p->str = strdup(t.str);
		Advance();
		break;

	// Quoted string-literal
	case TOK_STRINGBUF: 

		p = new ExprNode(EXPR_STRINGBUF, t);
        p->str = strdup(t.str);
		Advance();
		break;

	case '{':
		p = new ExprNode(EXPR_BYTESEQ, t);
		if(!ParseByteSequence(&p->byteSequence))
		{
			delete p;
			p = new ExprNode(EXPR_NULL, TOK_NULL);
		}
		break;

	default:
		Error(ERROR_SYNTAX_ERROR, inenglish(t));
		p = new ExprNode(EXPR_NULL, TOK_NULL);
		break;
	}

	return p;
}

//
//	postfix: primary [postfix-op]
//
ExprNode * Parser::PostfixExpression(ExprNode *p)
{
	ExprNode *q;

	for(;;)
	{
		TOKEN op = t;

        switch((int)op)
		{
		// array bounds
		case '[':

			Advance();
			q = Expression( TOKEN(']') );
			p = new ExprNode(EXPR_ARRAY, op, p, q);
			break;
			
		// field access
		case '.':
			
			Advance();
			q = PrimaryExpression();
			q = PostfixExpression(q);
			p = new ExprNode(EXPR_FIELD, op, p, q);
			break;

		case TOK_SCOPE:
		{
			Advance();
			q = UnaryExpression();
			p = new ExprNode(EXPR_SCOPE, op, p, q);
			break;
		}

		// pointer dereference '->'
		case TOK_DEREF:

			Advance();
			q = PrimaryExpression();
			q = PostfixExpression(q);
			p = new ExprNode(EXPR_DEREF, op, p, q);
			break;

		// post-increment and post-decrement operators
		case TOK_INC: case TOK_DEC:
#ifdef INCDEC_EXPRESSIONS
			return p;
#else
			Error(ERROR_OPERATOR_NOTSUPPORTED, inenglish(t));
			return p;
#endif
			
		// function calls (not supported)
		case '(':
#ifdef FUNCTION_EXPRESSIONS
			return p;			
#else
			Error(ERROR_FUNC_NOTSUPPORTED);
			return p;
#endif
			
		// pass as-is
		default:
			return p;
		}
	}
}

//
//	unary: [unary-op] postfix
//
//	unary-op: one of '-', '+', '*' etc
//
ExprNode * Parser::UnaryExpression(void)
{
	ExprNode *p = 0;
	TOKEN op = t;

    switch((int)op)
	{
	// pointer dereference / address-of
	case '*':	
	case '&':

		Advance(); 
		p = UnaryExpression();
		p = new ExprNode(op == '*' ? EXPR_POINTER : EXPR_ADDRESS, op, p);
		break;

	// regular unary operators
	case '+':	
	case '-':	
	case '~':	
	case '!':	
		
		Advance(); 
		p = UnaryExpression();
		p = new ExprNode(op == '*' ? EXPR_POINTER : EXPR_UNARY, op, p);
		break;
				
	// expressions within parenthesis
	case '(':
		
		Advance(); 
		p = FullExpression( TOKEN(')') );
		if(p) p->brackets = true;
		p = PostfixExpression(p);	
		break;

	case TOK_SIZEOF:
	{
		Advance();
		if(!Expected('('))
			return new ExprNode(EXPR_NULL, TOK_NULL);

		switch(t.kind)
		{
		case TOK_CHAR:  case TOK_WCHAR:
		case TOK_FLOAT: case TOK_DOUBLE:
		case TOK_BYTE:  case TOK_WORD:
		case TOK_DWORD: case TOK_QWORD:
		case TOK_ULEB128: case TOK_SLEB128:
		{
			TOKEN nameTok = t;
			p = new ExprNode(EXPR_IDENTIFIER, nameTok);
            p->str = strdup(inenglish(nameTok));
			Advance();
			break;
		}

		case TOK_IDENTIFIER:
			p = new ExprNode(EXPR_IDENTIFIER, t);
            p->str = strdup(t.str);
			Advance();
			break;

		case TOK_STRUCT:
		case TOK_UNION:
		{
			TOKEN tagKind = t;
			Advance();
			if(t.kind != TOK_IDENTIFIER)
			{
				Error(ERROR_SIZEOF_SCALAR_ONLY);
				p = new ExprNode(EXPR_NULL, TOK_NULL);
				break;
			}

			const std::string qualified = std::string(inenglish(tagKind)) + " " + t.str;
			p = new ExprNode(EXPR_IDENTIFIER, tagKind);
			p->str = strdup(qualified.c_str());
			Advance();
			break;
		}

		default:
			Error(ERROR_SIZEOF_SCALAR_ONLY);
			p = new ExprNode(EXPR_NULL, TOK_NULL);
			break;
		}

		if(p && p->type != EXPR_IDENTIFIER)
		{
			Error(ERROR_SIZEOF_SCALAR_ONLY);
			delete p;
			p = new ExprNode(EXPR_NULL, TOK_NULL);
		}

		if(p && p->type == EXPR_IDENTIFIER && t.kind != ')')
		{
			Error(ERROR_SIZEOF_SCALAR_ONLY);
			delete p;
			while(t.kind != ')' && t.kind != TOK_NULL)
				Advance();
			if(t.kind == ')')
				Advance();
			return new ExprNode(EXPR_NULL, TOK_NULL);
		}

		if(!Expected(')'))
		{
			delete p;
			return new ExprNode(EXPR_NULL, TOK_NULL);
		}

		p = new ExprNode(EXPR_SIZEOF, TOKEN(TOK_SIZEOF), p);
		break;
	}

	// select_offset(byteOffset) -- see EXPR_RAWOFFSET in expr.h for why this
	// exists. Only meaningful as a sub-expression inside another tag's
	// argument (e.g. select(select_offset(EI_CLASS)),
	// endian(select_offset(EI_DATA) == ELFDATA2MSB)); evaluates to 0 wherever
	// there's no file-backed render context (see Evaluate()'s
	// EXPR_RAWOFFSET case). Reserved-word only (see keywords.h) -- it's not
	// a tag in its own right, so [select_offset(...)] alone isn't valid.
	case TOK_SELECTOFFSET:
	{
		Advance();
		if(!Expected('('))
			return new ExprNode(EXPR_NULL, TOK_NULL);

		// FullExpression consumes the closing ')' itself (via Test()), so
		// no separate Expected(')') here -- unlike sizeof's case above,
		// which parses its inner content without FullExpression.
		p = FullExpression(TOKEN(')'));
		if(!p)
			return new ExprNode(EXPR_NULL, TOK_NULL);

		p = new ExprNode(EXPR_RAWOFFSET, TOKEN(TOK_SELECTOFFSET), p);
		break;
	}

	case TOK_VALUEAT:
	case TOK_ROOTVALUEAT:
	{
		TOKEN funcTok = t;
		Advance();
		if(!Expected('('))
			return new ExprNode(EXPR_NULL, TOK_NULL);

		ExprNode *spaceExpr = nullptr;
		ExprNode *offsetExpr = AssignmentExpression(TOK_NULL);
		if(!offsetExpr)
			return new ExprNode(EXPR_NULL, TOK_NULL);

		if(offsetExpr->type == EXPR_STRINGBUF)
		{
			if(!Expected(','))
			{
				delete offsetExpr;
				return new ExprNode(EXPR_NULL, TOK_NULL);
			}
			spaceExpr = offsetExpr;
			offsetExpr = AssignmentExpression(TOK_NULL);
			if(!offsetExpr)
			{
				delete spaceExpr;
				return new ExprNode(EXPR_NULL, TOK_NULL);
			}
		}

		if(!Expected(','))
		{
			delete spaceExpr;
			delete offsetExpr;
			return new ExprNode(EXPR_NULL, TOK_NULL);
		}

		ExprNode *typeExpr = nullptr;
		switch(t.kind)
		{
		case TOK_CHAR:  case TOK_WCHAR:
		case TOK_BYTE:  case TOK_WORD:
		case TOK_DWORD: case TOK_QWORD:
		{
			TOKEN typeTok = t;
			typeExpr = new ExprNode(EXPR_IDENTIFIER, typeTok);
			typeExpr->str = strdup(inenglish(typeTok));
			Advance();
			break;
		}

		case TOK_IDENTIFIER:
			typeExpr = PrimaryExpression();
			break;

		default:
			Error(ERROR_SIZEOF_SCALAR_ONLY);
			delete spaceExpr;
			delete offsetExpr;
			return new ExprNode(EXPR_NULL, TOK_NULL);
		}

		if(!typeExpr || typeExpr->type != EXPR_IDENTIFIER)
		{
			Error(ERROR_SIZEOF_SCALAR_ONLY);
			delete spaceExpr;
			delete offsetExpr;
			delete typeExpr;
			return new ExprNode(EXPR_NULL, TOK_NULL);
		}

		if(!Expected(')'))
		{
			delete spaceExpr;
			delete offsetExpr;
			delete typeExpr;
			return new ExprNode(EXPR_NULL, TOK_NULL);
		}

		p = new ExprNode(EXPR_VALUEAT, funcTok, spaceExpr, offsetExpr, typeExpr);
		break;
	}

	case TOK_CSTR:
	case TOK_CSTRAT:
	case TOK_CSTRFROM:
	case TOK_CONCAT:
	case TOK_EXTENTOF:
	case TOK_FIELDAT:
	case TOK_INDEXOF:
	case TOK_FINDFIRST:
	case TOK_FINDLAST:
	case TOK_FOURCC:
	case TOK_FMT:
	case TOK_OCTAL:
	case TOK_STR:
	{
		TOKEN funcTok = t;
		Advance();
		if(!Expected('('))
			return new ExprNode(EXPR_NULL, TOK_NULL);

		p = FullExpression(TOKEN(')'));
		if(!p)
			return new ExprNode(EXPR_NULL, TOK_NULL);

		p = new ExprNode(EXPR_FUNCTION, funcTok, p);
		break;
	}

	case TOK_FILESIZE:
	case TOK_INDEX:
	case TOK_SELF:
	case TOK_CURRENTOFFSET:
	{
		TOKEN funcTok = t;
		Advance();
		if(!Expected('('))
			return new ExprNode(EXPR_NULL, TOK_NULL);
		if(!Expected(')'))
			return new ExprNode(EXPR_NULL, TOK_NULL);

		p = new ExprNode(EXPR_FUNCTION, funcTok, 0);
		break;
	}

	// pre-increment & pre-decrement operators
	case TOK_INC: case TOK_DEC:		
#ifdef INCDEC_SUPPORTED
		Error(ERROR_OPERATOR_NOTSUPPORTED, inenglish(t));
		break;
#else
		Error(ERROR_OPERATOR_NOTSUPPORTED, inenglish(t));
		break;
#endif

	default:	
		
		p = PrimaryExpression();
		p = PostfixExpression(p);
		break;
	}

	return p;
}

//
//	binary:	unary [binary-op binary]
//
//	binary-op:  one of '+', '-', '*' etc
//
ExprNode * Parser::BinaryExpression(int k)
{
	ExprNode *p = UnaryExpression();
	ExprNode *r;

	// Modified Fraser-Hanson expression parser
	for(int i = Precedence(t); i >= k && i > 0; i--)
		while(Precedence(t) == i && t != '=' && lexer.CurrentChar() != '=')
		{
			TOKEN op = t;
			Advance();

			if(op == TOK_ANDAND || op == TOK_OROR)		// Right-associative
			{
				r = BinaryExpression(i);
			}
			else										// Left-associative
			{
				r = BinaryExpression(i+1);
			}
			
			p = new ExprNode(EXPR_BINARY, op, p, r);
		}

	return p;
}

//
//	cond: binary ['?' expr ':' cond]
//
ExprNode * Parser::ConditionalExpression(void)
{
	ExprNode *p = BinaryExpression(4);
	
	if(t == '?')
	{
		ExprNode *l, *r;
		
		Advance();
		l = Expression(TOKEN(':'));
		r = ConditionalExpression();

		p = new ExprNode(EXPR_TERTIARY, TOKEN('?'), l, r, p);
	}

	return p;
}

//
//	assign: cond
//			unary assign-op expr
//
ExprNode * Parser::AssignmentExpression(TOKEN term)
{
//	static int stop[] = { IF, IDENTIFIER, 0 };
	
	ExprNode *p = ConditionalExpression();		

#ifdef ASSIGNMENT_EXPRESSIONS
	if(t == '='
		|| (Precedence(t) >= 6  && Precedence(t) <= 8)		// & ^ |
		|| (Precedence(t) >= 11 && Precedence(t) <= 13))	// << >> + - * / %
	{
		ExprNode *q;
		TOKEN op = t;
		
		Advance();

		// normal assignment ( '=' )
		if(op == '=')	
		{
			q = AssignmentExpression(TOK_NULL);
			p = new ExprNode(EXPR_ASSIGN, op, p, q);
		}
		// augmented assignment ( &=, *=, +=, etc )
		else			
		{
			Expected('=');
			q = AssignmentExpression(TOK_NULL);
			p = new ExprNode(EXPR_ASSIGN, op, p, q);
		}
	}
#else
	if(t == '=')
	{
		Error(ERROR_ASSIGN_NOTSUPPORTED);
	}
#endif

	//if(tok) 
	//	Test(tok, stop);

	return p;
}

//
//	comma:	[','] expr
//
//
//	Comma-separated expressions are binary trees.
//	On return, the 'left' nodes hold the values, the 'right' nodes
//  are used to recursively link to the next expression
//
ExprNode *Parser::CommaExpression(TOKEN tok)
{
	ExprNode *left = AssignmentExpression(tok);
	ExprNode *right = 0;
	if(t == ',')
	{
		Advance();
		right = CommaExpression(tok);
	}

	return new ExprNode(EXPR_COMMA, TOKEN(','), left, right);
}

//
//	tag-wrapped argument: wrapperKeyword '(' expr ')'  |  expr
//
//	Lets one argument of a tag's parameter list be written as e.g. name(DllName)
//	instead of a plain value, to explicitly mark what role it plays -- without
//	teaching the general expression grammar a call syntax. 'wrappers' is a
//	TOK_NULL-terminated list of the tag keywords allowed to wrap an argument
//	in this position; anything else falls through to a normal expression.
//
ExprNode *Parser::TagWrappedArg(TOKEN wrappers[])
{
	static TOKEN kDestTagValueWrappers[] = {
		TOK_APPEND, TOK_ITEM, TOK_KEY, TOK_NAME, TOK_NULL
	};

	for(int i = 0; wrappers[i] != TOK_NULL; i++)
	{
		if(t != wrappers[i])
			continue;

		TOKEN wrapTok = t;
		Advance();

		if(!Expected('('))
			return 0;

		ExprNode *inner = 0;
		if(wrapTok == TOK_DEST)
			inner = TagArgList(kDestTagValueWrappers);
		else if(wrapTok == TOK_OFFSET || wrapTok == TOK_MAP || wrapTok == TOK_KEY || wrapTok == TOK_ITEM || wrapTok == TOK_ATTR || wrapTok == TOK_FIELD)
			inner = CommaExpression(TOK_NULL);
		else
			inner = AssignmentExpression(TOK_NULL);
		if(!inner)
			return 0;

		if(!Expected(')'))
		{
			delete inner;
			return 0;
		}

		return new ExprNode(EXPR_TAGWRAP, wrapTok, inner);
	}

	return AssignmentExpression(TOK_NULL);
}

//
//	tag-argument list: tagWrappedArg [',' tagArgList]
//
//	Same shape as CommaExpression, but every argument position accepts the
//	wrapped form via TagWrappedArg() instead of a plain AssignmentExpression.
//
ExprNode *Parser::TagArgList(TOKEN wrappers[])
{
	ExprNode *left = TagWrappedArg(wrappers);
	ExprNode *right = 0;
	if(t == ',')
	{
		Advance();
		right = TagArgList(wrappers);
	}

	return new ExprNode(EXPR_COMMA, TOKEN(','), left, right);
}

ExprNode * Parser::Expression(TOKEN term)
{
	ExprNode *p;
	
	//p = AssignmentExpression(TOK_NULL);
	p = ConditionalExpression();

	Test(term);
	return p;
}

//
//	expr: cond
//
ExprNode * Parser::FullExpression(TOKEN term)
{
	ExprNode *p = AssignmentExpression(TOK_NULL);
	
	while(p && t == ',')
	{
		ExprNode *q;

		Advance();
		
		if((q = AssignmentExpression(TOK_NULL)) != 0)
		{
			p = new ExprNode(EXPR_COMMA, TOKEN(','), p, q);
		}
		else
		{
			delete p;
			return 0;
		}
	}

	if(term && !Test(term))
    {
        delete p;
		return 0;
    }
	else
    {
		return p;
    }
}


int PrintCppString(stringprint &sbuf, const char *buf)
{
	int ch = *buf;

	sbuf._stprintf(TEXT("\""));

	while(ch)
	{
		if(ch >= 32 && ch < 127)
		{
			sbuf._stprintf(TEXT("%c"), ch);
		}
		else
		{
			TCHAR hex[10]; const TCHAR *str = 0;

			switch(ch)
			{
			case '\a' : str = TEXT("\\a");	break; // bell (alert)
			case '\b' : str = TEXT("\\b");	break; // backspace
			case '\f' : str = TEXT("\\f");	break; // formfeed
			case '\n' : str = TEXT("\\n");	break; // newline
			case '\r' : str = TEXT("\\r");	break; // carriage return
			case '\t' : str = TEXT("\\t");	break; // horizontal tab
			case '\v' : str = TEXT("\\v");	break; // vertial tab
			case '\'' : str = TEXT("\\'");	break; // single quotation
			case '\"' : str = TEXT("\\\"");	break; // double quotation
			case '\\' : str = TEXT("\\\\");	break; // backslash
			case '\?' : str = TEXT("\\?");	break; 

			default:
				_stprintf_s(hex, 10, TEXT("\\x%02x"), ch);
				str = hex;
				break;
			}

			sbuf._stprintf(str);
		}

		ch = *(++buf);
	}

	sbuf._stprintf(TEXT("\""));
	return 0;
}



//
//	Display (flatten) the expression to the specified stream
//
int RecurseFlatten(stringprint &sbuf, ExprNode *expr)
{
	int len = 0;
	static const TCHAR *numfmt[] = { TEXT("0x%x"), TEXT("%d"), TEXT("%o"), TEXT("%g") };

	if(expr == 0)
		return 0;

	if(expr->brackets)
		sbuf._stprintf(TEXT("("));

	switch(expr->type)
	{
	case EXPR_UNARY:
		sbuf._stprintf(TEXT("%hs"), Parser::inenglish(expr->tok));
		RecurseFlatten(sbuf, expr->left);
		break;

	case EXPR_POINTER: case EXPR_ADDRESS:
		sbuf._stprintf(TEXT("%hs"), Parser::inenglish(expr->tok));
		RecurseFlatten(sbuf, expr->left);
		break;

	case EXPR_NUMBER:
		if(expr->tok == TOK_INUMBER)
			sbuf._stprintf(numfmt[expr->base], expr->val);
		else
			sbuf._stprintf(TEXT("%g"), expr->fval);

		break;

	case EXPR_IDENTIFIER:
		sbuf._stprintf(TEXT("%hs"), expr->str);
		break;

	case EXPR_STRINGBUF:
		//len += printf("\"%s\"", expr->str);
		PrintCppString(sbuf, expr->str);
		break;

	case EXPR_BINARY:
		RecurseFlatten(sbuf, expr->left);
		sbuf._stprintf(TEXT(" %hs "), Parser::inenglish(expr->tok));
		RecurseFlatten(sbuf, expr->right);
		break;

	case EXPR_ARRAY:
		RecurseFlatten(sbuf, expr->left);
		sbuf._stprintf(TEXT("["));
		RecurseFlatten(sbuf, expr->right);
		sbuf._stprintf(TEXT("]"));
		break;
	
	case EXPR_DEREF:
		RecurseFlatten(sbuf, expr->left);
		sbuf._stprintf(TEXT("->"));
		RecurseFlatten(sbuf, expr->right);
		break;

	case EXPR_FIELD:
		RecurseFlatten(sbuf, expr->left);
		sbuf._stprintf(TEXT("."));
		RecurseFlatten(sbuf, expr->right);
		break;

	case EXPR_SCOPE:
		RecurseFlatten(sbuf, expr->left);
		sbuf._stprintf(TEXT("::"));
		RecurseFlatten(sbuf, expr->right);
		break;

	case EXPR_TERTIARY:
		RecurseFlatten(sbuf, expr->cond);
		sbuf._stprintf(TEXT(" ? "));
		RecurseFlatten(sbuf, expr->left);
		sbuf._stprintf(TEXT(" : "));
		RecurseFlatten(sbuf, expr->right);
		break;

	case EXPR_COMMA:
		RecurseFlatten(sbuf, expr->left);
		sbuf._stprintf(TEXT(" %hs "), Parser::inenglish(expr->tok));
		RecurseFlatten(sbuf, expr->right);
		sbuf._stprintf(TEXT(" : "));
		break;

	case EXPR_SIZEOF:
		sbuf._stprintf(TEXT("sizeof("));
		RecurseFlatten(sbuf, expr->left);
		sbuf._stprintf(TEXT(")"));
		break;

	case EXPR_BYTESEQ:
		sbuf._stprintf(TEXT("{ "));
		for(size_t i = 0; i < expr->byteSequence.size(); i++)
		{
			if(i)
				sbuf._stprintf(TEXT(", "));
			sbuf._stprintf(TEXT("0x%x"), expr->byteSequence[i]);
		}
		sbuf._stprintf(TEXT(" }"));
		break;

	case EXPR_FUNCTION:
		sbuf._stprintf(TEXT("%hs("), Parser::inenglish(expr->tok));
		RecurseFlatten(sbuf, expr->left);
		sbuf._stprintf(TEXT(")"));
		break;

	case EXPR_TAGWRAP:
	case EXPR_RAWOFFSET:
		sbuf._stprintf(TEXT("%hs("), Parser::inenglish(expr->tok));
		RecurseFlatten(sbuf, expr->left);
		sbuf._stprintf(TEXT(")"));
		break;

	case EXPR_VALUEAT:
		sbuf._stprintf(TEXT("%hs("), Parser::inenglish(expr->tok));
		if(expr->left)
		{
			RecurseFlatten(sbuf, expr->left);
			sbuf._stprintf(TEXT(", "));
		}
		RecurseFlatten(sbuf, expr->right);
		sbuf._stprintf(TEXT(", "));
		RecurseFlatten(sbuf, expr->cond);
		sbuf._stprintf(TEXT(")"));
		break;

    // unimplemented; ignore
    case EXPR_ASSIGN: case EXPR_NULL:
        break;
    }


	if(expr->brackets)
		sbuf._stprintf(TEXT(")"));

	return len;
}

size_t Flatten(FILE *fp, ExprNode *expr)
{
	stringprint sbuf(fp);
	RecurseFlatten(sbuf, expr);
	return sbuf.length();
}

size_t Flatten(TCHAR *buf, size_t len, ExprNode *expr)
{
	stringprint sbuf(buf, len);
	RecurseFlatten(sbuf, expr);
	return sbuf.length();
}

#if defined(_WIN32) && defined(UNICODE)
size_t Flatten(char *buf, size_t len, ExprNode *expr)
{
	stringprint sbuf(buf, len);
	RecurseFlatten(sbuf, expr);
	return sbuf.length();
}
#endif

//
//	Evaluate (flatten) the specified expression and
//	return it's numeric value
//
INUMTYPE Evaluate(ExprNode *expr)
{
	INUMTYPE left, right;

	if(expr == 0)
		return false;

	switch(expr->type)
	{
	case EXPR_IDENTIFIER:
		return 0;

	case EXPR_NUMBER:
		return expr->tok == TOK_INUMBER ? expr->val : (int)expr->fval;

	case EXPR_UNARY:

		left = Evaluate(expr->left);

        switch((int)expr->tok)
		{
		case '+':			return left;//+left;
		case '-':			return -left;
		case '!':			return !left;
		case '~':			return ~left;
		default:			return 0;
		}
		
	case EXPR_BINARY:

		if(expr->tok == TOK_ANDAND)
		{
			return Evaluate(expr->left) && Evaluate(expr->right);
		}
		else if(expr->tok == TOK_OROR)
		{
			return Evaluate(expr->left) || Evaluate(expr->right);
		}
		else
		{
			
			left  = Evaluate(expr->left);
			right = Evaluate(expr->right);

            switch((int)expr->tok)
			{
			case '+':			return left +  right;
			case '-':			return left -  right;
			case '*':			return left *  right;
            case '%':			return right != 0 ? left %  right : 0;
            case '/':			return right != 0 ? left /  right : 0;
			case '|':			return left |  right;
			case '&':			return left &  right;
			case '^':			return left ^  right;
			case TOK_ANDAND:	return left && right;
			case TOK_OROR:		return left || right;
			case TOK_SHR:		return left >> right;
			case TOK_SHL:		return left << right;
			case TOK_GE:		return left >= right;
			case TOK_LE:		return left <= right;
			default:			return 0;
			}
		}

	case EXPR_TERTIARY:

		if(Evaluate(expr->cond))
			return Evaluate(expr->left);
		else
			return Evaluate(expr->right);

	case EXPR_SIZEOF:
		return 0;

	// Needs a file-backed render context (see structurerenderengine.cpp's
	// evaluate()) to know which byte to read; not constant-foldable here.
	case EXPR_RAWOFFSET:
		return 0;

	case EXPR_VALUEAT:
		return 0;

	case EXPR_FUNCTION:
	case EXPR_BYTESEQ:
		return 0;

	default:
		// don't understand anything else
		return 0;
	}
}


//
//	Recursively duplicate the specified expression tree
//
ExprNode * CopyExpr(ExprNode *expr)
{
	ExprNode *expr2;

	// create the new node
	if(expr == 0 || (expr2 = new ExprNode(expr->type, expr->tok)) == 0)
		return 0;

	// copy the contents
	expr2->brackets = expr->brackets;
	expr2->base		= expr->base;
	expr2->byteSequence = expr->byteSequence;
	switch(expr->type)
	{
	case EXPR_IDENTIFIER:
	case EXPR_STRINGBUF:
        expr2->str = expr->str ? strdup(expr->str) : 0;
		break;

	case EXPR_NUMBER:
		if(expr->tok == TOK_FNUMBER)
			expr2->fval = expr->fval;
		else
			expr2->val = expr->val;
		break;

	default:
		expr2->val = expr->val;
		break;
	}

	// copy the children
	expr2->left		= CopyExpr(expr->left);
	expr2->right	= CopyExpr(expr->right);
	expr2->cond		= CopyExpr(expr->cond);

	// make sure it all copied ok
	if(!expr2->left && expr->left || !expr2->right && expr->right || !expr2->cond && expr->cond)
	{
		delete expr2;
		return 0;
	}

	return expr2;
}

/*//
//	Recursively free the specified expression tree
//
void FreeExpr(ExprNode *expr)
{
	if(expr != 0)
	{
		FreeExpr(expr->left);
		FreeExpr(expr->right);
		FreeExpr(expr->cond);

		delete expr;
	}
}
*/


ExprNode * Parser::ParseExpression()
{
	return Expression(TOK_NULL);
}
