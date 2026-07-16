//
//  expr.h
//
//  www.catch22.net
//
//  Copyright (C) 2012 James Brown
//  Please refer to the file LICENCE.TXT for copying permission
//

#ifndef EXPR_INCLUDED
#define EXPR_INCLUDED

#include "tchar_compat.h"
#include "lexer.h"

#include <stdint.h>
#include <vector>

enum EXPR
{ 
	EXPR_NUMBER,	EXPR_STRINGBUF, EXPR_IDENTIFIER, 
	EXPR_POINTER,	EXPR_DEREF,		EXPR_ADDRESS,
	EXPR_FIELD,		EXPR_ARRAY,		EXPR_FUNCTION,
	EXPR_UNARY,		EXPR_BINARY,	EXPR_TERTIARY, 
	EXPR_ASSIGN,	EXPR_COMMA,		EXPR_SIZEOF,
	// Constant byte sequence literal, currently used by byte-search built-ins:
	// find_first({ 'P', 'K' }) / find_last({ ... }). This is intentionally an
	// expression-level counterpart to magic(...)'s existing byte sequence.
	EXPR_BYTESEQ,
	// One argument of a tag's parameter list written as tagKeyword(value) instead
	// of a plain value -- e.g. name(DllName) as an argument inside
	// dynamic_array(name(DllName), type(CHAR), offset(Name), count(4096)).
	// 'tok' on the ExprNode
	// records which tag keyword wrapped it (e.g. TOK_NAME); 'left' is the
	// wrapped value. Parsed directly by the tag-argument parser, not by the
	// general expression grammar -- see Parser::TagWrappedArg().
	EXPR_TAGWRAP,
	// select_offset(byteOffset): reads one raw byte at byteOffset relative to
	// the current struct/union's own base file offset, bypassing named-field
	// lookup entirely. 'left' holds the byteOffset sub-expression.
	//
	// Exists for discriminators/byte-order markers that live inside a union
	// member that hasn't been selected yet (e.g. ELF's e_ident[EI_CLASS] is
	// part of each per-bitness header struct, not a sibling field, so
	// select(...)/endian(...) can't reach it by name before picking a member).
	//
	// This is the narrow fix (Option 2): a raw-byte escape hatch. The general
	// fix (Option 3, not yet implemented) would let the evaluator fall back to
	// checking whether every union candidate declares the *named* field at the
	// same offset/type and read it that way, so plain field syntax works
	// without this expression at all. If that lands, EXPR_RAWOFFSET and its
	// callers can be removed in favour of it.
	EXPR_RAWOFFSET,
	// value_at(offsetExpr, TypeName) / value_at("space", offsetExpr, TypeName):
	// reads a fixed-size scalar integer at a computed offset. 'left' optionally
	// holds the named offset space string, 'right' holds the offset expression,
	// and 'cond' holds the scalar type identifier.
	EXPR_VALUEAT,
	EXPR_NULL
};

struct ExprNode
{
	ExprNode() : type(EXPR_NULL), left(0), right(0), cond(0)
	{
	}

	// constructor: initialize with default values
	ExprNode(EXPR ty, TOKEN t, ExprNode *l=0, ExprNode *r=0, ExprNode *c=0) :
		left(l), right(r), cond(c), tok(t), type(ty), brackets(false), base(DEC), val(0)
	{
	}

	// destructor: recursively delete all child expressions
	~ExprNode()
	{
		delete left;
		delete right;
		delete cond;
	}

	ExprNode		*	left;		// used for all expressions
	ExprNode		*	right;		// only used for binary
	ExprNode		*	cond;		// only used for tertiary (conditionals)

	TOKEN				tok;		// the operator token (e.g. '+', '&' etc)
	EXPR				type;		// type of expression
	bool				brackets;	// was this node enclosed in brackets?
	NUMBASE				base;		// dec/hex/oct etc (for EXPR_NUMBER only)
	std::vector<uint8_t>	byteSequence;

	union 
	{
		INUMTYPE			val;
		double				fval;
		char		*		str;
	};
};

// Parse a 'simple' expression, not including any 'comma' operators
ExprNode *Expression(TOKEN term);

// Parse a 'full' expression, includes the 'comma' operator
ExprNode *FullExpression(TOKEN term);

// Parse a 'primary' expression only (i.e. just a single number/identifier)
ExprNode *PrimaryExpression();

// Duplicate the specified expression-tree
ExprNode * CopyExpr(ExprNode *expr);

// Display (flatten) expression to specified stream
size_t Flatten(FILE *fp, ExprNode *expr);
size_t Flatten(TCHAR *buf, size_t len, ExprNode *expr);

// Evaluate (flatten) to a constant integer. Returns 0 if not contant.
INUMTYPE Evaluate(ExprNode *expr);

#endif
