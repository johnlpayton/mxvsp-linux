#ifndef __JTYPES_H
#define __JTYPES_H

/*======================================================================
*
*   Basic machine types
*
======================================================================*/

typedef  void (*pFunction)();

typedef int                BOOL;

typedef char               I8;
typedef char               S8;
typedef unsigned char      U8;

typedef short              I16;
typedef short              S16;
typedef unsigned short     U16;

typedef int                I32;
typedef int                S32;
typedef unsigned int       U32;
typedef unsigned int       UINT;

typedef float               F32;
typedef double              F64;
typedef long double         F80;

#if USEWIN32
typedef __int64                I64;
typedef __int64                S64;
typedef unsigned __int64       U64;
#endif

#if USEGDK
typedef long long            I64;
typedef long long            S64;
typedef unsigned long long   U64;

//======== Windows porting
typedef void* HWND;
typedef void* HFONT;
typedef void* HDC;
typedef U32 HPEN;
typedef U32 COLORREF;
typedef U32 HINSTANCE;
typedef char* LPSTR;
typedef U32 LPARAM;
typedef U32 WPARAM;
typedef void* HANDLE;
typedef U32 DWORD;
typedef void* LPVOID;

#define stricmp(x,y) strcasecmp( (x), (y) )
#define strcmpi(x,y) strcasecmp( (x), (y) )
#endif

//======== Various useful
#define structof(_strname,_elname,_elptr) ((_strname*)((char*)_elptr-offsetof(_strname,_elname)))

#endif
