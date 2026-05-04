//
//  seqbase.h
//
//  www.catch22.net
//
//  Copyright (C) 2012 James Brown
//  Please refer to the file LICENCE.TXT for copying permission
//

#ifndef SEQBASE_INCLUDED
#define SEQBASE_INCLUDED

#include <cstdint>

//
//	Define the underlying string/character type of the sequence.
//
//	'seqchar' can be redefined to BYTE, WCHAR, ULONG etc
//	depending on what kind of string you want your sequence to hold
//
typedef unsigned char	  seqchar;

#ifdef SEQUENCE64
typedef uint64_t  size_w;
#else
typedef uint32_t  size_w;
#endif

#endif
