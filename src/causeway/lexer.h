//
//  lexer.h
//
//  www.catch22.net
//
//  Copyright (C) 2012 James Brown
//  Please refer to the file LICENCE.TXT for copying permission
//

#ifndef LEXER_INCLUDED
#define LEXER_INCLUDED

#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <limits.h>
#ifndef _MAX_PATH
#define _MAX_PATH PATH_MAX
#endif

static inline void strcpy_s(char *dest, size_t destsz, const char *src)
{
	if(destsz == 0)
		return;

	strncpy(dest, src, destsz - 1);
	dest[destsz - 1] = '\0';
}

static inline void strncpy_s(char *dest, size_t destsz, const char *src, size_t count)
{
	if(destsz == 0)
		return;

	size_t n = count < destsz - 1 ? count : destsz - 1;
	strncpy(dest, src, n);
	dest[n] = '\0';
}
#endif

#ifdef __cplusplus
#include <string>
#include <vector>
#include "error.h"
using std::string;
using std::vector;

struct Statement;
struct StrataLibrary;
typedef vector<Statement *> StatementList;

#endif

typedef enum TOKEN
{
	TOK_NULL	= 0,
	TOK_ILLEGAL = -1,

	// Single-character ASCII tokens — value equals the character code.
	// Defined here so casts like TOKEN('/') are in-range for static analysis.
	// Use character literals directly in code; these names are rarely needed.
	TOK_EXCL = '!', TOK_PERCENT = '%', TOK_AMP  = '&', TOK_LPAREN = '(',
	TOK_RPAREN=')', TOK_STAR   = '*', TOK_PLUS  = '+', TOK_COMMA  = ',',
	TOK_MINUS= '-', TOK_DOT    = '.', TOK_SLASH = '/', TOK_COLON  = ':',
	TOK_LT   = '<', TOK_EQ     = '=', TOK_GT    = '>', TOK_QUEST  = '?',
	TOK_LBRACK='[', TOK_RBRACK = ']', TOK_CARET = '^', TOK_PIPE   = '|',
	TOK_TILDE= '~', TOK_SEMI   = ';', TOK_LBRACE= '{', TOK_RBRACE = '}',

	// basic tokens
	TOK_IDENTIFIER = 1000,
	TOK_STRINGBUF,
	TOK_INUMBER,
	TOK_FNUMBER,


	// operators
	TOK_SHL			= 2000,
	TOK_SHR,
	TOK_ANDAND,
	TOK_OROR,
	TOK_EQU,
	TOK_NEQ,
	TOK_LE,
	TOK_GE,
	TOK_INC,
	TOK_DEC,
	TOK_DEREF,
	
	// keywords
#undef  DEFINE_KEYWORD
#define DEFINE_KEYWORD(tok, str) tok,
#include "keywords.h"

	TOK_STATIC,
	TOK_EXTERN,

/*	tokAlign= 3000,
	tokBitflag,
	tokCase,
	tokConst,
	tokDisplay,
	tokEndian,
	tokEnum,
	tokIgnore,
	tokInclude,
	tokLengthIs,
	tokOffset,
	tokSigned,
	tokSizeIs,
	tokString,
	tokStruct,
	tokStyle,
	tokSwitchIs,
	tokTypedef,
	tokUnion,
	tokUnsigned,

	alignTok		= 3000,
	bitflagTok,
	caseTok,
	constTok,
	displayTok,
	endianTok,
	enumTok,
	ignoreTok,
	includeTok,
	lengthisTok,
	offsetTok,
	signedTok,
	sizeisTok,
	stringTok,
	structTok,
	styleTok,
	switchisTok,
	typedefTok,
	unionTok,
	unsignedTok,
*/

	// basetypes
	TOK_CHAR		= 4000,
	TOK_BYTE,
	TOK_WORD,
	TOK_DWORD,
	TOK_QWORD,
	TOK_FLOAT,
	TOK_DOUBLE,
	TOK_WCHAR,
	TOK_ULEB128,
	TOK_SLEB128,
} TOKEN;

#ifdef __cplusplus

// token-name lookup
struct TOKEN_LOOKUP
{
	TOKEN tok;
    const char *str;
};

// file-descriptor
struct FILE_DESC
{
	FILE_DESC(const char *path, const char *name) : buf(0), len(0), pos(0), curLine(1), wspStart(0), wspEnd(0)
	{
		origPath[0] = '\0';
		filePath[0] = '\0';
		fileName[0] = '\0';

		strncpy_s(filePath, _MAX_PATH, path, _MAX_PATH);
		strncpy_s(fileName, _MAX_PATH, name, _MAX_PATH);
		strcpy_s(origPath, _MAX_PATH, path);
	}


	char		filePath[_MAX_PATH];	// actual path
	char		fileName[_MAX_PATH];	// display name (can change due to preprocessor)
	char		origPath[_MAX_PATH];	// actual path

	size_t		curLine;

	char	*	buf;
	size_t		len;
	size_t		pos;

	size_t		wspStart;
	size_t		wspEnd;
	
	StatementList stmtList;

};

enum NUMTYPE
{
	//UN
};

typedef enum NUMBASE
{
	HEX,// = 16,
	DEC,// = 10,
	OCT,// = 8,
	//FLOAT,// = 1,
} NUMBASE;

#define MAX_STRING_LEN 200

typedef uint64_t		  INUMTYPE;
typedef double			  FNUMTYPE;

#ifdef __cplusplus

struct Token
{
	Token(TOKEN tok = TOK_NULL) : kind(tok), num(0), fnum(0.0), base(DEC)
	{
		str[0] = '\0';
	}

	operator TOKEN() const
	{
		return kind;
	}

	bool operator==(TOKEN tok) const
	{
		return kind == tok;
	}

	bool operator!=(TOKEN tok) const
	{
		return kind != tok;
	}

	TOKEN		kind;
	char		str[MAX_STRING_LEN+1];
	INUMTYPE	num;
	FNUMTYPE	fnum;
	NUMBASE		base;
};

extern struct TOKEN_LOOKUP	toklook[];

// file-reference
struct FILEREF
{
	FILEREF(FILE_DESC *fd = 0) : lineNo(0), fileDesc(0), wspStart(0), wspEnd(0)
	{
		MakeRef(fd);

		//FileRef(
		/*if(curFile)
		{
			//stackIdx = fileStack.size() - 1;
			fileDesc = curFile;
			lineNo   = curFile->curLine;
		}*/
	}

	void MakeRef(FILE_DESC *fd)
	{
		if(fd)
		{
			lineNo		= fd->curLine;
			fileDesc	= fd;
			wspStart	= fd->wspStart;
			wspEnd		= fd->wspEnd;
		}
	}


	//int		stackIdx;
	size_t			lineNo;
	FILE_DESC *		fileDesc;

	// whitespace markers
	size_t			wspStart;
	size_t			wspEnd;
};

class Lexer
{
public:
	Lexer();

	void SetStrataLibrary(StrataLibrary *lib);
	void SetParentLexer(Lexer *parent);
	void SetErrorCallback(void (*callback)(void *, ERROR, va_list), void *context);
	void AddIncludePath(const char *path);

	void newline();
	bool InitBuffer(const char *buffer, size_t len);
	bool InitFile(const char *filename);
	bool FileIncluded(const char *filename);
	void FileRef(FILEREF *fileRef);
	FILE_DESC *CurrentFile() const;
	int CurrentChar() const;
	Token Next();

	int parse_identifier();
	TOKEN parse_char();
	int parse_string(int term);
	int backslash();
	TOKEN parse_number();
	int skipwhitespace(size_t *startpos, size_t *endpos);
	int preprocess();
	int skipspaces();
	int skipeol();
	int nextch();
	int peekch(int advance = 0);

private:
	void Error(ERROR err, ...);

	int				ch;
	Token			token;
	int				wspStart;
	int				wspEnd;

	FILE_DESC	*	curFile;
	Lexer		*	parentLexer;
	StrataLibrary *	typeLibrary;
	vector<string>	includePaths;
	void		(*errorCallback)(void *, ERROR, va_list);
	void		*	errorContext;
};

#endif
#endif

#endif
