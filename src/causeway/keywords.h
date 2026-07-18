//
//  keywords.h
//
//  www.catch22.net
//
//  Copyright (C) 2012 James Brown
//  Please refer to the file LICENCE.TXT for copying permission
//

//
// keywords
//

// This is an X-macro fragment, not a standalone header: includers #define
// DEFINE_KEYWORD(tok, str) (and optionally DEFINE_RESERVED_KEYWORD) before
// #including this file to generate enum entries, table rows or switch cases.
// Opened directly -- e.g. an IDE parsing it as its own translation unit --
// neither macro is defined, which is what produces the "type specifier
// required"/"undefined identifier" noise. The fallback below is a harmless
// no-op purely to keep that quiet; real includers always define their own
// first and are unaffected.
#ifndef DEFINE_KEYWORD
#define DEFINE_KEYWORD(tok, str)
#define KEYWORDS_H_UNDEF_DEFINE_KEYWORD
#endif

#ifndef DEFINE_RESERVED_KEYWORD
#define DEFINE_RESERVED_KEYWORD DEFINE_KEYWORD
#endif

// An alternative spelling of an existing keyword (e.g. "size_is" for count,
// "switch_is" for select) that maps to the SAME token. Consumers that build
// an enum or a switch over distinct tokens (lexer.h's TOKEN enum,
// parser.cpp's IsContextualKeyword) must leave this undefined/a no-op --
// defining it as DEFINE_KEYWORD would redeclare the enumerator or duplicate
// the switch case. Consumers that just want every valid spelling as a flat
// list (the lexer's keyword table, syntax highlighters) define this the
// same as DEFINE_KEYWORD before including this file.
#ifndef DEFINE_KEYWORD_ALIAS
#define DEFINE_KEYWORD_ALIAS(tok, str)
#define KEYWORDS_H_UNDEF_DEFINE_KEYWORD_ALIAS
#endif

DEFINE_KEYWORD          ( TOK_ALIGN,            "align" )
DEFINE_KEYWORD          ( TOK_APPEND,           "append" )
DEFINE_KEYWORD          ( TOK_ASSOC,            "assoc" )
DEFINE_KEYWORD          ( TOK_ATTR,             "attr" )
DEFINE_KEYWORD          ( TOK_BITFLAG,          "bitflag" )
DEFINE_RESERVED_KEYWORD ( TOK_CASE,             "case" )
DEFINE_KEYWORD          ( TOK_CATEGORY,         "category" )
DEFINE_RESERVED_KEYWORD ( TOK_CONCAT,           "concat" )
DEFINE_KEYWORD          ( TOK_CONTAINER,        "container" )
DEFINE_KEYWORD          ( TOK_COUNTAS,          "count_as" )
DEFINE_RESERVED_KEYWORD ( TOK_CONST,            "const" )
DEFINE_RESERVED_KEYWORD ( TOK_CSTR,             "cstr" )
DEFINE_RESERVED_KEYWORD ( TOK_CSTRAT,           "cstr_at" )
DEFINE_RESERVED_KEYWORD ( TOK_CSTRFROM,         "cstr_from" )
DEFINE_RESERVED_KEYWORD ( TOK_CURRENTOFFSET,    "current_offset" )
DEFINE_RESERVED_KEYWORD ( TOK_DEFAULT,          "default" )
DEFINE_RESERVED_KEYWORD ( TOK_DESCRIPTION,      "description" )
DEFINE_KEYWORD          ( TOK_DEST,             "dest" )
DEFINE_KEYWORD          ( TOK_DISPLAY,          "display" )
DEFINE_KEYWORD          ( TOK_DYNAMICARRAY,     "dynamic_array" )
DEFINE_KEYWORD          ( TOK_DYNAMICCONTAINER, "dynamic_container" )
DEFINE_KEYWORD          ( TOK_DYNAMICSTRUCT,	"dynamic_struct" )
DEFINE_KEYWORD          ( TOK_EMIT,             "emit" )
DEFINE_KEYWORD          ( TOK_EMITNODE,         "emit_node" )
DEFINE_KEYWORD          ( TOK_EMITROW,          "emit_row" )
DEFINE_KEYWORD          ( TOK_ENDIAN,           "endian" )
DEFINE_RESERVED_KEYWORD ( TOK_ENUM,             "enum" )
DEFINE_KEYWORD          ( TOK_ENTRYPOINT,       "entrypoint" )
DEFINE_KEYWORD          ( TOK_EXPORT,           "export" )
DEFINE_KEYWORD          ( TOK_EXTENT,           "extent" )
DEFINE_KEYWORD          ( TOK_EXTENTOF,         "extent_of" )
DEFINE_KEYWORD          ( TOK_FIELD,            "field" )
DEFINE_RESERVED_KEYWORD ( TOK_FIELDAT,          "field_at" )
DEFINE_KEYWORD          ( TOK_FILESIZE,         "file_size" )
DEFINE_KEYWORD          ( TOK_FORMAT,           "format" )
DEFINE_RESERVED_KEYWORD ( TOK_FMT,              "fmt" )
DEFINE_KEYWORD          ( TOK_FOURCC,           "fourcc" )
DEFINE_RESERVED_KEYWORD ( TOK_FINDFIRST,        "find_first" )
DEFINE_RESERVED_KEYWORD ( TOK_FINDLAST,         "find_last" )
DEFINE_KEYWORD          ( TOK_IGNORE,           "ignore" )
DEFINE_RESERVED_KEYWORD ( TOK_INDEX,            "array_index" )
DEFINE_KEYWORD          ( TOK_ITEM,             "item" )
DEFINE_KEYWORD          ( TOK_KEY,              "key" )
DEFINE_KEYWORD          ( TOK_LABEL,            "label" )
//DEFINE_KEYWORD ( TOK_IMPORT,		"import" )
DEFINE_RESERVED_KEYWORD ( TOK_INCLUDE,          "include" )
DEFINE_KEYWORD          ( TOK_LENGTHIS,         "length_is" )
DEFINE_KEYWORD          ( TOK_MAGIC,            "magic" )
DEFINE_KEYWORD          ( TOK_MAP,              "map" )
DEFINE_KEYWORD          ( TOK_MAXCOUNT,         "max_count" )
DEFINE_KEYWORD          ( TOK_NAME,             "name" )
DEFINE_KEYWORD          ( TOK_OFFSET,           "offset" )
DEFINE_KEYWORD          ( TOK_OFFSETMAP,        "offset_map" )
DEFINE_RESERVED_KEYWORD ( TOK_OCTAL,            "octal" )
DEFINE_KEYWORD          ( TOK_OPTIONAL,         "optional" )
DEFINE_KEYWORD          ( TOK_PADTO,            "pad_to" )
DEFINE_RESERVED_KEYWORD ( TOK_ROOTVALUEAT,      "root_value_at" )
DEFINE_RESERVED_KEYWORD ( TOK_SELECTOFFSET,     "select_offset" )
DEFINE_RESERVED_KEYWORD ( TOK_SELF,             "element_value" )
DEFINE_KEYWORD          ( TOK_SEMANTIC,         "semantic" )
DEFINE_RESERVED_KEYWORD ( TOK_SIGNED,           "signed" )
DEFINE_KEYWORD          ( TOK_COUNT,            "count" )
DEFINE_RESERVED_KEYWORD ( TOK_SIZEOF,           "sizeof" )
DEFINE_KEYWORD          ( TOK_STR,              "str" )
DEFINE_KEYWORD          ( TOK_STRING,           "string" )
DEFINE_RESERVED_KEYWORD ( TOK_STRUCT,           "struct" )
DEFINE_KEYWORD          ( TOK_STYLE,            "style" )
DEFINE_KEYWORD          ( TOK_SELECT,           "select" )
DEFINE_KEYWORD          ( TOK_TAGS,             "tags" )
DEFINE_KEYWORD          ( TOK_TAGSET,           "tagset" )
DEFINE_KEYWORD          ( TOK_TERMINATEDBY,     "terminated_by" )
DEFINE_KEYWORD          ( TOK_TERMINATOR,       "terminator" )
DEFINE_KEYWORD          ( TOK_TREE,             "tree" )
DEFINE_KEYWORD          ( TOK_TYPE,             "type" )
DEFINE_RESERVED_KEYWORD ( TOK_TYPEDEF,          "typedef" )
DEFINE_RESERVED_KEYWORD ( TOK_UNION,			"union" )
DEFINE_RESERVED_KEYWORD ( TOK_UNSIGNED,         "unsigned" )
DEFINE_RESERVED_KEYWORD ( TOK_VALUEAT,          "value_at" )
DEFINE_KEYWORD          ( TOK_VERSION,          "version" )
DEFINE_KEYWORD          ( TOK_VIEW,             "view" )
DEFINE_KEYWORD          ( TOK_MAPPER,           "mapper" )

// IDL-derived legacy spellings -- see DEFINE_KEYWORD_ALIAS above.
DEFINE_KEYWORD_ALIAS    ( TOK_COUNT,            "size_is" )
DEFINE_KEYWORD_ALIAS    ( TOK_SELECT,           "switch_is" )

#undef DEFINE_RESERVED_KEYWORD
#ifdef KEYWORDS_H_UNDEF_DEFINE_KEYWORD_ALIAS
#undef DEFINE_KEYWORD_ALIAS
#undef KEYWORDS_H_UNDEF_DEFINE_KEYWORD_ALIAS
#endif
#ifdef KEYWORDS_H_UNDEF_DEFINE_KEYWORD
#undef DEFINE_KEYWORD
#undef KEYWORDS_H_UNDEF_DEFINE_KEYWORD
#endif
