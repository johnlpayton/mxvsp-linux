#ifndef W32DEFS_H
#define W32DEFS_H 1

/*
*==========================================================================
*
*   w32defs.h
*   (c) 2012 jlp
*
*   Defines for windows compatability
*
*==========================================================================
*/

#define WM_INITDIALOG 0x0110
#define WM_COMMAND 0x0111

#define MF_BYCOMMAND 0x0000             // Win32
#define MF_BYPOSITION 0x0400            // Win32
#define MF_ENABLED 0x0000               // Win32
#define MF_GRAYED 0x0001                // Win32
#define MF_DISABLED 0x0002              // Win32
#define CW_USEDEFAULT -1

#define MB_OK               0x0000
#define MB_OKCANCEL         0x0001
#define MB_ABORTRETRYIGNORE 0x0002
#define MB_YESNOCANCEL      0x0003
#define MB_YESNO            0x0004
#define MB_RETRYCANCEL      0x0005
#define MB_TYPEMASK         0x000F

#define MB_ICONHAND         0x0010
#define MB_ICONQUESTION     0x0020
#define MB_ICONEXCLAMATION  0x0030
#define MB_ICONASTERISK     0x0040
#define MB_ICONMASK         0x00F0

#define MB_ICONINFORMATION  MB_ICONASTERISK
#define MB_ICONSTOP         MB_ICONHAND

#define MB_DEFBUTTON1       0x0000
#define MB_DEFBUTTON2       0x0100
#define MB_DEFBUTTON3       0x0200
#define MB_DEFMASK          0x0F00

#define MB_APPLMODAL        0x0000
#define MB_SYSTEMMODAL      0x1000
#define MB_TASKMODAL        0x2000

#define MB_NOFOCUS          0x8000

/* Standard dialog button IDs */
#define IDOK                1
#define IDCANCEL            2
#define IDABORT             3
#define IDRETRY             4
#define IDIGNORE            5
#define IDYES               6
#define IDNO                7

extern int MessageBox(
    void* hWnd,
    const char* Errstr,
    const char* title,
    U32 flags);

#endif
