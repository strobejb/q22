//
//  tchar_compat.h
//
//  Minimal TCHAR compatibility for non-Windows builds. Causeway still uses the
//  old TCHAR spelling in rendering helpers, but the parser stores source text
//  as narrow strings.
//

#ifndef TCHAR_COMPAT_INCLUDED
#define TCHAR_COMPAT_INCLUDED

#ifdef _WIN32
#include <tchar.h>
#else
#include <stdio.h>
#include <wchar.h>

typedef char TCHAR;

#ifndef TEXT
#define TEXT(x) x
#endif

#ifndef _T
#define _T(x) x
#endif

#ifndef _vsnprintf
#define _vsnprintf vsnprintf
#endif

#ifndef _vsnwprintf
#define _vsnwprintf vswprintf
#endif

#ifndef _stprintf_s
#define _stprintf_s snprintf
#endif
#endif

#endif
