//
//  lexer.cpp
//
//  www.catch22.net
//
//  Copyright (C) 2012 James Brown
//  Please refer to the file LICENCE.TXT for copying permission
//
//  -----
//	Lexical analysis
//
//	convert the a text input-stream into a series
//  of TOKENS which are used by the parser to build the
//  syntax-tree
//

#define _CRT_SECURE_NO_DEPRECATE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <vector>
#include <errno.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#else
#include <unistd.h>
#include <strings.h>
#define _access access
#define _chdir chdir
#define _getcwd getcwd
#define _strcmpi strcasecmp
#define _strdup strdup
#endif

#include "parser.h"
using std::vector;


TOKEN_LOOKUP toklook[] = 
{
	{	TOK_CHAR,		"char"			},
	{	TOK_WCHAR,		"wchar_t"		},
//	{	TOK_BYTE,		"int8"			},
//	{	TOK_WORD,		"int16"			},
//	{	TOK_DWORD,		"int32"			},
//	{	TOK_QWORD,		"int64"			},
	{	TOK_BYTE,		"byte"			},
	{	TOK_WORD,		"word"			},
	{	TOK_DWORD,		"dword"			},
	{	TOK_QWORD,		"qword"			},
	{	TOK_FLOAT,		"float"			},
	{	TOK_DOUBLE,		"double"		},
	{	TOK_ULEB128,	"uleb128"		},
	{	TOK_SLEB128,	"sleb128"		},

#undef DEFINE_KEYWORD
#define DEFINE_KEYWORD(tok,str) {tok, str},
#define DEFINE_KEYWORD_ALIAS(tok,str) {tok, str},
#include "keywords.h"

	{	TOK_NULL,		0				}
};

//
//	preprocessor stuff
//
vector <char *> cpp_options;
bool	 cpp_save = false;
bool	 cpp_none;

#ifdef _WIN32
char	cpp_cmdstr[_MAX_PATH] = "CL.EXE";
char	PATHSEP[] = ";";
#else
char	cpp_cmdstr[_MAX_PATH] = "gcc";
char	PATHSEP[] = ":";
#endif

Lexer::Lexer()
{
	ch			= 0;
	token		= Token();
	wspStart	= 0;
	wspEnd		= 0;
	curFile		= 0;
	parentLexer	= 0;
	typeLibrary = 0;
	errorCallback = 0;
	errorContext = 0;
}

void Lexer::SetStrataLibrary(StrataLibrary *lib)
{
	typeLibrary = lib;
}

void Lexer::SetParentLexer(Lexer *parent)
{
	parentLexer = parent;
	if(parent)
		includePaths = parent->includePaths;
}

void Lexer::SetErrorCallback(void (*callback)(void *, ERROR, va_list), void *context)
{
	errorCallback = callback;
	errorContext = context;
}

void Lexer::AddIncludePath(const char *path)
{
	if(path && path[0])
		includePaths.push_back(path);
}

void Lexer::Error(ERROR err, ...)
{
	if(errorCallback == 0)
		return;

	va_list vargs;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wvarargs"
	va_start(vargs, err);
#pragma clang diagnostic pop
	errorCallback(errorContext, err, vargs);
	va_end(vargs);
}

void Lexer::newline()
{
	curFile->curLine++;
}

void Lexer::FileRef(FILEREF *fileRef)
{
	fileRef->lineNo		= curFile->curLine;
	fileRef->fileDesc	= curFile;
	fileRef->wspStart	= curFile->wspStart;
	fileRef->wspEnd		= curFile->wspEnd;
}

static void dirname(char *dirname, const char *filepath)
{
	const char *ptr;

	for(ptr = filepath + strlen(filepath); ptr > filepath; ptr--)
	{
		if(*ptr == '\\' || *ptr == '/')
			break;
	}

	strncpy(dirname, filepath, ptr-filepath+1);
	dirname[ptr-filepath+1]='\0';
}

//
//	"is absolute path"
//
static bool isabspath(const char *name)
{
	return (name[0] == '/' || name[0] == '\\' || name[0] == '~' || name[1] == ':');
}

static bool exists(const char *name)
{
	return _access(name, 0) == 0 ? true : false;
}

static void fullpath(char *dest, const char *name, size_t destlen)
{
#ifdef _WIN32
	_fullpath(dest, name, destlen);
#else
	if(realpath(name, dest) == 0)
		strcpy_s(dest, destlen, name);
#endif
}

static void joinpath(char *dest, size_t destlen, const char *dir, const char *name)
{
	size_t len    = strlen(dir);
	bool   needSep = len > 0 && dir[len-1] != '/' && dir[len-1] != '\\';
	snprintf(dest, destlen, "%s%s%s", dir, needSep ? "/" : "", name);
}

//
//	Search the specified environment-string
//
static bool searchenv(const char *envname, const char *name, char *fullname, size_t fullnamelen)
{
	char *includePaths;

	if((includePaths = getenv(envname)) != 0)
	{
		char *pathDup = _strdup(includePaths);
		char *tok     = strtok(pathDup, PATHSEP);
		bool  found   = false;

		while(tok && !found)
		{
			joinpath(fullname, fullnamelen, tok, name);
			found = exists(fullname);
			tok   = strtok(NULL, PATHSEP);
		}

		free(pathDup);
		return found;
	}

	return false;
}

//
//	Search order:
//
//		1. current directory
//		2. directories specified by /I switch
//		3. directories specified by INCLUDE environment variable
//
static bool getfullname(const char *curpath, const char *name, char *fullname, size_t fullnamelen,
                        const vector<string> *includePaths = 0)
{
	size_t i;
	//FILE_DESC *curFile;

	// if this is the first time we've been called then
	// work off the full path of the file
	if(curpath == 0 || curpath[0] == '\0' || isabspath(name))
	{
		fullpath(fullname, name, _MAX_PATH);
	}
	// otherwise we need to do a search relative to the directory
	// of the current file we are parsing
	else
	{
		char cur[_MAX_PATH], dir[_MAX_PATH];

		// get the directory of the current file being parsed
		dirname(dir, curpath);//curFile->filePath);

		// make the full path of the new file we want to parse
		_getcwd(cur, _MAX_PATH);
		_chdir(dir);		
		fullpath(fullname, name, _MAX_PATH);
		_chdir(cur);
	}

	// 1. search local directory
	if(exists(fullname))
	{
		//strcpy(fullname, name);
		return true;
	}

	// 2. search parser-supplied library/include paths
	if(includePaths)
	{
		for(i = 0; i < includePaths->size(); i++)
		{
			joinpath(fullname, fullnamelen, (*includePaths)[i].c_str(), name);
			if(exists(fullname))
				return true;
		}
	}

	// 3. search directories specified by -I commandline option
	for(i = 0; i < cpp_options.size(); i++)
	{
		char *opt = cpp_options[i];
		
		if(opt[1] == 'I' && (opt[0] == '/' || opt[0] == '-' ))
		{
			opt += 2;	// skip the '-I'

			if(*opt)
			{
				for( ; *opt && *opt != ' '; opt++)
					;
			}
			else
			{
				opt = cpp_options[++i];
			}

			//printf("searching in %s\n", opt);
			joinpath(fullname, fullnamelen, opt, name);

			if(exists(fullname))
				return true;
		}
	}

	// 4. search directories specified by INCLUDE environment
	if(searchenv("INCLUDE", name, fullname, fullnamelen))
		return true;

	// couldn't find it...
	snprintf(fullname, fullnamelen, "%s", name);
	return false;
}

FILE_DESC *Lexer::CurrentFile() const
{
	return curFile;
}

int Lexer::CurrentChar() const
{
	return ch;
}

bool Lexer::InitBuffer(const char *buffer, size_t len)
{
	if(buffer == 0 || len == 0)
		return false;

	// The library owns every source buffer that FILEREF can point into,
	// including anonymous in-memory buffers.
	if((curFile = new FILE_DESC("", "")) == 0)
		return false;

	curFile->buf	= (char *)malloc(len + 1);
	curFile->len	= len;

	memcpy(curFile->buf, buffer, len);
	curFile->buf[len] = '\0';
	typeLibrary->globalFileHistory.push_back(curFile);
		
	token	 = Token();
	ch		 = ' ';

	return true;
}

bool Lexer::FileIncluded(const char *filename)
{
	char fullpath[_MAX_PATH];

	// get the full path to the specified file.
	if(getfullname(curFile->filePath, filename, fullpath, _MAX_PATH, &includePaths) == false)
	{
		Error(ERROR_NOSUCHFILE, filename);
		return false;
	}

	// has it been included already?
	for(size_t i = 0; i < typeLibrary->globalFileHistory.size(); i++)
	{
		FILE_DESC *fd = typeLibrary->globalFileHistory[i];

		if(_strcmpi(fd->filePath, fullpath) == 0)
		{
			// do nothing!
			return true;
		}
	}

	return false;
}

bool Lexer::InitFile(const char *filename)
{
	char fullpath[_MAX_PATH];
	FILE *fp;

	// get the full path to the specified file.
	if(getfullname(parentLexer ? parentLexer->curFile->filePath : NULL, filename, fullpath, _MAX_PATH, &includePaths) == false)
	{
		Error(ERROR_NOSUCHFILE, filename);
		return false;
	}

	if((fp = fopen(fullpath, "rb")) == NULL)
	{
		Error(ERROR_FILENOTFOUND, filename);
		return false;
	}

	fseek(fp, 0, SEEK_END);
	long filesize = ftell(fp);
	rewind(fp);

	if(filesize < 0)
	{
		int saved = errno;
		fclose(fp);
		errno = saved;
		return false;
	}

	if((curFile = new FILE_DESC(fullpath, filename)) == 0)
	{
		fclose(fp);
		return false;
	}

	curFile->buf = (char *)malloc((size_t)filesize + 1);
	if(curFile->buf == 0)
	{
		fclose(fp);
		return false;
	}

	curFile->len          = fread(curFile->buf, 1, (size_t)filesize, fp);
	curFile->buf[filesize] = '\0'; // NOLINT(clang-analyzer-security.ArrayBound) -- filesize is from ftell, bounded by malloc above
	fclose(fp);

	typeLibrary->globalFileHistory.push_back(curFile);
		
	token	 = Token();
	ch		 = ' ';

	return true;
}

int Lexer::peekch(int advance /*= 0*/)
{
	if(curFile->pos + advance < curFile->len)
		return curFile->buf[curFile->pos + advance];
	else
		return 0;
}

//
//	Return next character in the input-stream, keep track of
//  line-number information at the same time
//
int Lexer::nextch()
{
	if(curFile->pos < curFile->len)
	{
		int c = curFile->buf[curFile->pos++];
		
		if(c == '\n')
		{
			newline();
		}
		else if(c == '\\')	// line-continuation
		{
			int nc = peekch(0);
			
			if(nc == '\n')
			{
				curFile->pos++;
				c = curFile->buf[curFile->pos++];
				newline();
			}
			else if(nc == '\r' && peekch(1) == '\n')
			{
				curFile->pos+=2;
				c = curFile->buf[curFile->pos++];
				newline();
			}			
		}

		return c;
	}
	else
	{
		return 0;	
	}
}

int Lexer::skipeol()
{
	while(ch != '\n')
		ch = nextch();

	if(ch == '\n')
		ch = nextch();

	return ch;
}

int Lexer::skipspaces()
{
	while(ch == ' ')
		ch = nextch();

	return ch;
}

//
//	Really basic preprocessor to handle #line directives
//
//	#line <number> "filename"
//	# <number> "filename"
//
int Lexer::preprocess()
{
	if(ch == '#')
	{
		ch = nextch();
		ch = skipspaces();

		// optional #identifier 
		if(isalpha(ch))
		{
			ch = parse_identifier();

			// ignore pragmas entirely
			if(strcmp(token.str, "pragma") == 0)
			{
				return skipeol();
			}
			// we don't handle anything other that #line
			else if(strcmp(token.str, "line") != 0)
			{
				Error(ERROR_PREPROC);
				//exit(-1);
				return 0;
			}
		}

		ch = skipspaces();

		if(isdigit(ch))
		{
			// grab the line-number
			parse_number();

			ch = skipspaces();

			if(ch == '\"' || ch == '<')
			{
				// grab the filename
				ch = parse_string(ch);
				return skipeol();
			}

		}
		else
		{
			Error(ERROR_PREPROC);
			return 0;
		}
	}

	return ch;
}


//
//	Process any combination of whitespace, return the character-offsets
//  of where the whitespace starts/ends
//
int Lexer::skipwhitespace(size_t *startpos, size_t *endpos)
{
	*startpos = curFile->pos;

	while(ch && ch <= ' ' || ch == '/' || ch == '#')
	{
		if(ch == '#')				// #line directive?
		{
			ch = preprocess();
			continue;
		}
		else if(ch == '/')
		{
			ch = nextch();

			if(ch == '/')			// single-line comment
			{
				ch = nextch();

				while(ch && ch != '\n')
					ch = nextch();
			}
			else if(ch == '*')		// block comment
			{
				do
				{
					ch = nextch();

					while(ch && ch != '*')
						ch = nextch();

					ch = nextch();
				}
				while(ch && ch != '/');
			}
			else					// really was a '/' by itself
			{
				*endpos = curFile->pos - 1;
				return '/';
				//break;
			}
		}

		ch = nextch();
	}

	*endpos = curFile->pos - 1;
	return ch;
}

static
TOKEN match_keyword(const char *buf)
{
	// match the specified string against a predefined keyword
	for(int i = 0; toklook[i].tok != TOK_NULL; i++)
	{
		if(strcmp(buf, toklook[i].str) == 0)
			return toklook[i].tok;
	}

	// no match, treat as a normal identifier
	return TOK_IDENTIFIER;
}

static inline
unsigned long hexval(int ch)
{
	if(isdigit(ch))	return ch - '0';
	else			return (ch & ~0x20) - 'A' + 10;
}

//
//	Numbers are the most complicated to parse as we need
//	to handle hex, octal, decimal and also floating-point
//
TOKEN Lexer::parse_number()
{
	bool floatnum = false;
	char numstr[MAX_STRING_LEN+4], *ep;
	int  i = 0;

	// Hex or Octal
	if(ch == '0')	
	{
		ch = nextch();

		// Hex numbers
		if(ch == 'x' || ch == 'X')
		{
			token.base = HEX;
			ch = nextch();

			// collect all hex-digits together
			while(i < MAX_STRING_LEN && isxdigit(ch))
			{
				numstr[i++] = ch;
				ch = nextch();
			}

			if(i == 0)
				Error(ERROR_ILLEGAL_HEXNUM);

			numstr[i] = '\0';
		}
		// Octal numbers
		else
		{
			token.base = OCT;

			// collect all digits together. Invalid octal sequences
			// will be caught when we call strtoul
			while(i < MAX_STRING_LEN && isdigit(ch))
			{
				numstr[i++] = ch;
				ch = nextch();
			}

			numstr[i] = '\0';
		}

	}
	// Decimal 
	else			
	{
		token.base = DEC;

		// Base digits
		while(i < MAX_STRING_LEN && isdigit(ch))
		{
			numstr[i++] = ch;
			ch = nextch();
		}
	}

	// Floating point!
	if(ch == '.' && token.base != HEX)
	{
		floatnum = true;
		numstr[i++] = ch;
		ch = nextch();

		// collect any exponent digits
		while(i < MAX_STRING_LEN && isdigit(ch))
		{
			numstr[i++] = ch;
			ch = nextch();
		}

		// optional exponent
		if(ch == 'e' || ch == 'E')
		{
			numstr[i++] = ch;
			ch = nextch();

			if(ch == '-' || ch == '+')
			{
				numstr[i++] = ch;
				ch = nextch();
			}

			if(!isdigit(ch))
			{
			}

			// exponent digits
			while(i < MAX_STRING_LEN && isdigit(ch))
			{
				numstr[i++] = ch;
				ch = nextch();
			}
		}
	}
	else if(ch == '.' && token.base == HEX)
	{
		Error(ERROR_ILLEGAL_HEXNUM);
	}

	// nul-terminate
	numstr[i] = '\0';
	errno = 0;

	if(floatnum)
	{
		if(ch == 'f' || ch == 'F') ch = nextch();
		if(ch == 'l' || ch == 'L') ch = nextch();

		// token type = FLOAT
		token.fnum = strtod(numstr, &ep);
		return TOK_FNUMBER;
	}
	else
	{
		if(ch == 'u' || ch == 'U') ch = nextch();
		if(ch == 'l' || ch == 'L') ch = nextch();

		if(isalpha(ch))
			Error(ERROR_ILLEGAL_SUFFIX, ch);

		int base[] = { 16, 10, 8 };
		token.num = strtoul(numstr, &ep, base[token.base]);

		if(*ep)
		{
			Error(ERROR_ILLEGAL_DIGIT, *ep, base[token.base]);
		}

		if(errno)
		{
			Error(ERROR_OVERFLOW);
		}

		return TOK_INUMBER;
	}


}

int Lexer::backslash()
{
	int n = 0;

	ch = nextch();

	switch(ch)
	{
	case 'a'  : return '\a';	// bell (alert)
	case 'b'  : return '\b';	// backspace
	case 'f'  : return '\f';	// formfeed
	case 'n'  : return '\n';	// newline
	case 'r'  : return '\r';	// carriage return
	case 't'  : return '\t';	// horizontal tab
	case 'v'  : return '\v';	// vertial tab
	case '\'' : return '\'';	// single quotation
	case '\"' : return '\"';	// double quotation
	case '\\' : return '\\';	// backslash
	case '?'  : return '\?';	// literal question mark
	
	case 'x'  :					// hexadecimal
		
		ch = nextch();
		while(isxdigit(ch))
		{
			n = n * 0x10 + hexval(ch);
			ch = nextch();
		}
		
		return n;

	// octal
	case '0': case '1': case '2': case '3': 
	case '4': case '5': case '6': case '7': 

		while(ch >= '0' && ch <= '7')
		{
			n = n * 8 + (ch - '0');
			ch = nextch();
		}

		return n;
		
	// don't handle any other escape
	default:
		return 0;
	}
}

//
//	process string-literals
//
int Lexer::parse_string(int term)
{
	int i = 0;

	// multiple quote strings are coallesced together
	while(ch == '\"')
	{
		ch = nextch();
		
		while(ch && ch != term)
		{
			// escape sequence
			if(ch == '\\')
			{
				ch = backslash();
			}
			
			if(i < MAX_STRING_LEN)
				token.str[i++] = (char)ch;
			
			ch = nextch();
		}
		
		// skip the terminating quote
		ch = nextch();
		ch = skipwhitespace(&curFile->wspStart, &curFile->wspEnd);
	}
	
	token.str[i] = '\0';
	
	return ch;
}

int Lexer::parse_identifier()
{
	int i = 0;
	
	while(isalnum(ch) || ch == '_')
	{
		if(i < MAX_STRING_LEN)
			token.str[i++] = (char)ch;
		
		ch = nextch();
	}
	
	token.str[i] = '\0';
	
	return ch;
}

TOKEN Lexer::parse_char()
{
	ch = nextch();

	int value = 0;
	if(ch == '\\')
	{
		value = backslash();
		if(ch != '\'' && ch != 0)
			ch = nextch();
	}
	else
	{
		value = ch;
		ch = nextch();
	}

	if(ch == '\'')
		ch = nextch();
	else
		Error(ERROR_SYNTAX_ERROR, "'");

	token.num = static_cast<unsigned char>(value);
	token.base = HEX;
	return TOK_INUMBER;
}

//
//	Next() is the main entry-point into the lexer, it is called by
//  the parser to retrieve the sequence of tokens in each translation unit
//
Token Lexer::Next()
{
	TOKEN tmp;
	token = Token();

	if(ch == '/' && (peekch() != '/' && peekch() != '*'))
	{
		ch = nextch();
		return Token(TOKEN('/'));
	}

	// skip any whitespace, but remember where it was found
	ch = skipwhitespace(&curFile->wspStart, &curFile->wspEnd);

	// integer/floating-point numbers
	if(isdigit(ch) || (ch == '.' && isdigit(peekch())))
	{
		// will return TOK_INUMBER or TOK_FNUMBER, with the token-value
		// stored in the current token payload
		token.kind = parse_number();
		return token;
	}

	// wide character/string literals
	if(ch == 'L' && (peekch() == '\"' || peekch() == '\''))
	{

	}	

	// regular string literal
	if(ch == '\"')
	{
		ch = parse_string(ch);
		token.kind = TOK_STRINGBUF;
		return token;
	}

	if(ch == '\'')
	{
		token.kind = parse_char();
		return token;
	}
	
	// match operators
	switch(ch)
	{
	case '!':		
		ch  = nextch();
		if(ch == '=')			// !=
		{
			ch = nextch();
			return Token(TOK_NEQ);
		}
		return Token(TOKEN('!'));		// !

	case '=':		
		ch  = nextch();
		if(ch == '=')			// ==
		{
			ch = nextch();
			return Token(TOK_EQU);
		}
		return Token(TOKEN('='));		// =

	case '<':		
		ch  = nextch();
		if(ch == '=')			// <=
		{
			ch = nextch();
			return Token(TOK_LE);
		}
		else if(ch == '<')		// <<
		{
			ch = nextch();
			return Token(TOK_SHL);
		}
		return Token(TOKEN('<'));		// <

	case '>':		
		ch  = nextch();
		if(ch == '=')			// >=
		{
			ch = nextch();
			return Token(TOK_GE);
		}
		else if(ch == '>')		// >>
		{
			ch = nextch();
			return Token(TOK_SHR);
		}
		return Token(TOKEN('>'));		// >

	case '&':		
		ch  = nextch();
		if(ch == '&')			// &&
		{
			ch = nextch();
			return Token(TOK_ANDAND);
		}
		return Token(TOKEN('&'));		// &

	case '|':		
		ch  = nextch();
		if(ch == '|')			// ||
		{
			ch = nextch();
			return Token(TOK_OROR);
		}
		return Token(TOKEN('|'));		// |

	case '+':		
		ch  = nextch();
		if(ch == '+')			// ++
		{
			ch = nextch();
			return Token(TOK_INC);
		}
		return Token(TOKEN('+'));		// +

	case '-':		
		ch  = nextch();
		if(ch == '-')			// --
		{
			ch = nextch();
			return Token(TOK_DEC);
		}
		else if(ch == '>')		// ->
		{
			ch = nextch();
			return Token(TOK_DEREF);
		}
		return Token(TOKEN('-'));		// -

	case '(': case ')': 
	case '{': case '}':
	case '[': case ']':
	case '.': case ',': 
	case ';':
	case '~': case '^':
	case '*': case '%': 
	case '/': case '?':
		tmp = TOKEN(ch);
		ch  = nextch();
		return Token(tmp);

	case ':':
		ch = nextch();
		if(ch == ':')
		{
			ch = nextch();
			return Token(TOK_SCOPE);
		}
		return Token(TOKEN(':'));

	// end of input
	case '\0':
		return Token(TOK_NULL);

	default:

		if(isalpha(ch) || ch == '_')
		{
			// match idenfifer to a keyword
			ch = parse_identifier();
			token.kind = match_keyword(token.str);
			return token;
		}
		else
		{
			// error, invalid character encountered
			ch = nextch();
			return Token(TOK_ILLEGAL);
		}
	}
}
