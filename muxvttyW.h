#ifndef MUXVTTY_H
#define MUXVTTY_H 1
/*
*==========================================================================
*
*   muxvttyW.h
*   (c) 2012 jlp
*
*   TTY include: windows version
*
*==========================================================================
*/
#define SCRCOLMAX 80
#define SCRROWMAX 120
#define SZSCRBUF (SCRCOLMAX*SCRROWMAX)

//  VSP window commands
#define VSP_SETTEXT (WM_USER + 100)
#define VSP_HOME (WM_USER + 101)
#define VSP_CLS (WM_USER + 102)
#define VSP_CLRALL (WM_USER + 103)
#define VSP_SETPARENT (WM_USER + 104)
#define VSP_TELLID (WM_USER + 105)
#define VSP_CLOSING (WM_USER + 106)
#define VSP_SETIMAGESIZE (WM_USER + 107)
#define VSP_SETHISTSIZE (WM_USER + 108)
#define VSP_GETSCRIMAGE (WM_USER + 109)
#define VSP_GETLINES (WM_USER + 110)
#define VSP_GETSIZE (WM_USER + 111)

//  VSPEdit window commands
#define VSPEDIT_RELOAD (WM_USER + 201)
#define VSPEDIT_CUT (WM_USER + 202)
#define VSPEDIT_COPY (WM_USER + 203)
#define VSPEDIT_PASTE (WM_USER + 204)
#define VSPEDIT_LOADTEXT (WM_USER + 205)

//  Misc commands that belong elsewhere
#define VSP_TCPCLOSE (WM_USER + 403)
#define VSP_THREADCLOSE (WM_USER + 404)
#define VSP_NEWPACKET (WM_USER + 405)
#define VSP_STARTDLOAD (WM_USER + 406)
#define VSP_STOPDLOAD (WM_USER + 407)
#define VSP_TIC500MS (WM_USER + 408)
#define VSP_NEWWINDOW (WM_USER + 409)

#define XLF 10
#define XCR 13
#define XBS 8
#define XHT 9
#define XESC 0x1b
#define XTABSPACE 8


// tty window
typedef struct S_MVSPTTY{
    //======== Character based data
    int     curow;                          // vt100 Cursor row VT style (1,1)=Upper-left
    int     offsetrow;                      // an offset to account for line leeds
    int     cucol;                          // vt100 Cursor collumn
    int     nRows;                          // number of screen rows (verical)
    int     nCols;                          // number of screen collums (horizontal)
    int     nRowImage;                      // number of rows in the image
    int     nColImage;                      // number of rows in the image
    //======== vt102 subset
    int     inEscSeq;                       // escape sequence processing
    int     nVTParams;                      // number of parameters
    int     VTParamValues[4];               // values
    //======== screen (pixel)
    int     charH;                          // character height
    int     charW;                          // character width
    int     margin;                         // boundry margin
    HFONT   hfont;                          // font handle
    DWORD   fgcolor;                        // font color
    DWORD   bkcolor;                        // background color
    RECT    screenRect;                     // rectangle for visable screen
    //======== internel use
    HWND    parent;
    int     myID;                           // my TTY ID
    //======== local buffer
    int     putrow;                         // put row index
    int     nscrsize;
//    char*   scrbuf;                         // buffer
    char   scrbuf[SZSCRBUF];                         // buffer

}t_mvsptty;

extern BOOL CALLBACK _export muxvtty(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
extern HWND NewWinmuxvtty(HWND parent, HANDLE hInstance, int tid, LPCTSTR wName);
extern int VspAction(struct S_windowmap* pMapEntry, int cmd, long arg1, long arg2);
#endif
