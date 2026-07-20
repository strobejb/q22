//
//  errordef.h
//
//  www.catch22.net
//
//  Copyright (C) 2012 James Brown
//  Please refer to the file LICENCE.TXT for copying permission
//

// This is an X-macro fragment, not a standalone header: includers #define
// DEFINE_ERR(err, msg) (e.g. to generate ERROR enum entries or a message
// lookup table) before #including this file. Opened directly -- e.g. an IDE
// parsing it as its own translation unit -- that macro is undefined, which is
// what produces the "type specifier required"/"undefined identifier" noise.
// The fallback below is a harmless no-op purely to keep that quiet; real
// includers always define their own first and are unaffected.
#ifndef DEFINE_ERR
#define DEFINE_ERR(err, msg)
#define ERRORDEF_H_UNDEF_DEFINE_ERR
#endif

DEFINE_ERR(	ERROR_EXPECTED_TYPENAME,	"Expected a typename"	)
DEFINE_ERR(	ERROR_NOT_TYPENAME,			"'%s' is not a type name"	)
DEFINE_ERR(	ERROR_UNKNOWN_STRUCT,		"Unknown struct type '%s'"	)
DEFINE_ERR(	ERROR_UNDEFINED_STRUCT,		"Undefined struct '%s'"	)
DEFINE_ERR(	ERROR_UNKNOWN_ENUM,			"Unknown enum type '%s'"	)
DEFINE_ERR(	ERROR_UNDEFINED_ENUM,		"Undefined enum type '%s'"	)
DEFINE_ERR(	ERROR_TYPE_REDEFINITION,	"Redefinition of type '%s'"	)
DEFINE_ERR(	ERROR_EXPECTED_TOKEN,		"Expected '%s', found '%s'" )
DEFINE_ERR(	ERROR_SYNTAX_ERROR,			"Syntax error : '%s'" )
DEFINE_ERR(	ERROR_ILLEGAL_TAG,			"Tag '%s' cannot be used in this context" )
DEFINE_ERR(	ERROR_NOTA_TAG,             "Identifier '%s' is invalid in context" )
DEFINE_ERR(	ERROR_UNEXPECTED,			"Unexpected: '%s'" )
DEFINE_ERR(	ERROR_NOFUNCPTR,			"Function pointers not supported")
DEFINE_ERR( ERROR_OVERFLOW,				"Overflow in constant value")
DEFINE_ERR( ERROR_OPERATOR_NOTSUPPORTED,"'%s' operator not supported")
DEFINE_ERR( ERROR_ASSIGN_NOTSUPPORTED,	"Assignment operator not supported")
DEFINE_ERR( ERROR_FUNC_NOTSUPPORTED,	"Function call operator not supported")
DEFINE_ERR( ERROR_ILLEGAL_SUFFIX,		"Illegal suffix '%c' on number")
DEFINE_ERR( ERROR_ILLEGAL_DIGIT,		"Illegal digit '%c' in base-%d number")
DEFINE_ERR( ERROR_ILLEGAL_HEXNUM,		"Syntax error in hex constant")
DEFINE_ERR( ERROR_PREPROC,				"Error in preprocessor")
DEFINE_ERR( ERROR_BITFIELDUNION,		"Bitfield not allowed in Union")
DEFINE_ERR( ERROR_RESERVED_KEYWORD,		"Keyword '%s' is reserved but not supported")
DEFINE_ERR( ERROR_UNSIZED_ARRAY_REQUIRES_SIZEIS, "Unsized array declaration requires a count or max_count tag")
DEFINE_ERR( ERROR_UNKNOWN_TAGSET,		"Unknown tagset '%s'")
DEFINE_ERR( ERROR_TAGSET_REDEFINITION,	"Redefinition of tagset '%s'")
DEFINE_ERR( ERROR_TAGS_NOT_ALLOWED_IN_TAGSET, "tags(...) cannot be used inside a tagset")
DEFINE_ERR( ERROR_ELEMENT_TAG_REQUIRES_ARRAY, "element(...) can only be used on array declarations")
DEFINE_ERR( ERROR_ARRAY_ELEMENT_TAG_REQUIRES_ELEMENT, "Tag '%s' on array declaration must be wrapped in element(...)")
DEFINE_ERR( ERROR_UNKNOWN_BITFIELD,		"Unknown bitfield '%s'")
DEFINE_ERR( ERROR_BITFIELD_REDEFINITION, "Redefinition of bitfield '%s'")
DEFINE_ERR( ERROR_BITFIELD_ENTRY_SYNTAX, "bitfield entries expect match(mask)[ = value] or field(name, mask[, enum(...)])")
DEFINE_ERR( ERROR_SIZEOF_SCALAR_ONLY,   "sizeof(...) expects a type name")
DEFINE_ERR( ERROR_MAGIC_SYNTAX,        "magic(...) expects { bytes... } followed by an optional integer byte offset")
DEFINE_ERR( ERROR_UNKNOWN_SEMANTIC_SCHEMA, "Unknown semantic schema '%s'")
DEFINE_ERR( ERROR_FILENOTFOUND,			"Failed to open '%s'")
DEFINE_ERR( ERROR_NOSUCHFILE,			"Filename '%s' does not exist")
DEFINE_ERR(	ERROR_UNKNOWN,				"Unknown error" )

#ifdef ERRORDEF_H_UNDEF_DEFINE_ERR
#undef DEFINE_ERR
#undef ERRORDEF_H_UNDEF_DEFINE_ERR
#endif
