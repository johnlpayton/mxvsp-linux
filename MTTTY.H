#ifndef MTTTY_H
#define MTTTY_H 1

/*-----------------------------------------------------------------------------
    This is a part of the Microsoft Source Code Samples. 
    Copyright (C) 1995 Microsoft Corporation.
    All rights reserved. 
    This source code is only intended as a supplement to 
    Microsoft Development Tools and/or WinHelp documentation.
    See these sources for detailed information regarding the 
    Microsoft samples programs.

    MODULE: MTTTY.h

    PURPOSE: Contains global definitions and variables

-----------------------------------------------------------------------------*/

//
// File: MTTTY.h
//

#include "resource.h"
#include "ttyinfo.h"
#include "muxvttyW.h"
#include "mttty.h"
#include "muxctl.h"
#include "framer.h"
#include "MTTTY.H"


#define EASYWINDEBUG 1
#define JLPMIDI 1

//
// GLOBAL DEFINES
//
#define TTY_BUFFER_SIZE         MAXROWS * MAXCOLS
#define MAX_STATUS_BUFFER       20000
#define MAX_WRITE_BUFFER        1024
#define MAX_READ_BUFFER         2048
#define READ_TIMEOUT            500
//#define STATUS_CHECK_TIMEOUT    500
#define WRITE_CHECK_TIMEOUT     500
#define PURGE_FLAGS             PURGE_TXABORT | PURGE_TXCLEAR | PURGE_RXABORT | PURGE_RXCLEAR 
#define EVENTFLAGS_DEFAULT      EV_BREAK | EV_CTS | EV_DSR | EV_ERR | EV_RING | EV_RLSD
#define FLAGCHAR_DEFAULT        '\n'

#define SYSCMD_RESET  0X7F00 
#define SYSCMD_RESTART  0X7F01 
#define SYSCMD_CONTINUE  0X7F02 
#define SYSRET_ERR -1
#define SYSRET_CONTINUE 257
#define SYSRET_NOP 258


//
// Write request types
//
#define WRITE_CHAR          0x01
#define WRITE_FILE          0x02
#define WRITE_FILESTART     0x03
#define WRITE_FILEEND       0x04
#define WRITE_ABORT         0x05
#define WRITE_BLOCK         0x06

//
// Read states
//
#define RECEIVE_TTY         0x01
#define RECEIVE_CAPTURED    0x02

//
// window coords
//
#define MAXXWINDOW          750
#define MAXYWINDOW          530
#define STARTXWINDOW        8
#define STARTYWINDOW        24

#define SETTINGSFACTOR      5
#define STATUSFACTOR        5

/*
** for aour tty display
*/
#define LNUM_DIRECTION      3
#define LNUM_BOT2TOP        0    //! we are goint to stqrt witj this
#define LNUM_TOP2BOT        1
#define LNUM_LEFT2RIGHT     2
#define LNUM_RIGHT2LEFT     3

#define LNUM_GETPARAMS      (WM_USER + 100)
#define LNUM_SETPARAMS      (WM_USER + 101)
#define LNUM_SETSTART       (WM_USER + 102)

#define UART_LINEEND_CR 1
#define UART_LINEEND_LF 2
#define UART_LINEEND_CRLF 3
struct S_inicfg
{
//  [MUX]
    int     muxmode;                            // muxmode (DIRECT|CLIENT|SERVER)
    int     SrvrWinMode;                        // autowindows in server
    int     EvbMode;                            // 1 EVB | 0 LOOP
    char    NameSrvr[96];                       // server name
// [UART]
    int     port;                               // port (COM 1)
    int     brate;                              // bitrate (19200)
    int     flow;                               // flow (none|soft|hard)
    int     lineend;                            // line end from the TTY (<CR>)
// [VTTYWIN]
    int     TTYHeight;                          // default tty window height pixels
    int     TTYWidth;                           // default tty window width pixels
    int     maxwin;                             // maxwin: ignored, hard set to 8
// [MISC]
    char    editfname[96];                      // file name to save edit
// [DEBUG]
    int     prflags;                            // prflags ?
};
extern struct S_inicfg inicfg;
#define PRFLG_TCP 0x0001
#define PRFLG_EVB 0x0002
#define PRFLG_ROUTER 0x0004
#define PRFLG_WINMUX 0x0008
#define PRFLG_VSP 0x0010
#define PRFLG_OTHER 0x0020


struct S_LnumParam
{
    int      NumStart;                  // start nubering here
    int      NumStep;                   // step the number
    int      PixelStep;                 // stp the pixel
    short    Hmargin,Vmargin;           // use rhis pixel margin
    HFONT    hFont;                     // this font
    POINT    CharSize;                  // his Vsiz is
    RECT     PrRect;                    // area to print in
    int      flags;
};

/*
** This is an aweful big fucker sup(k*4,7*4 + 1) -> 32bytes
*/
typedef struct WRITEREQUEST
{
  DWORD      dwWriteType;        // char, file start, file abort, file packet
  DWORD      dwSize;             // size of buffer
  char       ch;                 // ch to send
  char *     lpBuf;              // address of buffer to send
  HANDLE     hHeap;              // heap containing buffer
  HWND       hWndProgress;       // status bar window handle
  struct WRITEREQUEST *pNext;    // next node in the list
  struct WRITEREQUEST *pPrev;    // prev node in the list
} WRITEREQUEST, *PWRITEREQUEST;

typedef struct STATUS_MESSAGE
{
    struct STATUS_MESSAGE * lpNext;     // pointer to next node
    char chMessageStart;                // variable length string start here
} STATUS_MESSAGE;

//
// autorun flags
//
#define AUTOCMD_SIM 1

//
// GLOBAL VARIABLES
//
extern OSVERSIONINFO gOSV;
extern HINSTANCE     ghInst;
extern HACCEL        ghAccel;
extern HWND          ghwndMain;
extern HWND          ghwndContent;
extern HWND          ghWndStatusDlg;
extern HWND          heditBox;
extern HWND          hdownLoadBox;
extern HWND          hupLoadBox;
extern HWND          hWinInput;
extern HWND          hStartDialog;
extern int           AutoCommands;
extern t_windowmap   UpLoadTTY;
extern HFONT         ghFontVTTY;
//
// COMMTIMEOUTS is init'd in Init.c
//
extern COMMTIMEOUTS gTimeoutsDefault;

//
//  Window placement variables
//
extern WORD gwBaseY;
extern LONG gcyMinimumWindowHeight;
extern int     gDlgHistory;                    // show settings or history
extern int     AllowNewWindows;                 // enable new windows

//
//  Flags controlling thread actions
//
extern HANDLE ghThreadExitEvent;
extern BOOL gfAbortTransfer;

//
//  File transfer variables
//
extern DWORD  gdwFileTransferLeft;
extern DWORD  gdwReceiveState;
extern HANDLE ghFileCapture;
extern char szFileName[MAX_PATH];

//
//  Status updating
//
extern CRITICAL_SECTION gStatusCritical;
extern CRITICAL_SECTION gcsWriterHeap;
extern CRITICAL_SECTION gcsDataHeap;

extern HFONT ghFontStatus;
extern int   gnStatusIndex;
extern HANDLE ghWriterHeap;
extern HANDLE ghWriterEvent;
extern HANDLE ghTransferCompleteEvent;

//
//  Port name
//
extern char gszPort[10];
//
//  Error functions
//
void ErrorReporter( char * szMessage );
void ErrorHandler( char * szMessage );
extern void ErrorInComm( char * szMessage );
extern void FatalError(char* strerr);
extern void ShowDialogError(char* strerr);
extern int MessageLastError(char *title);
extern void FatalOnNullPtr(char* title, void* pPtr);
//
//  Initialization/deinitialization/settings functions
//
void ChangeConnection( HWND, BOOL );
BOOL BreakDownCommPort( void );
BOOL UpdateConnection( void );
void GlobalInitialize( void );
void DestroyTTYInfo( void );
void GlobalCleanup( void );
void UpdateTTYInfo( void );
BOOL DisconnectOK( void );
BOOL InitTTYInfo(BOOL save_load);
void InitNewFont( LOGFONT*, COLORREF );
int QueueCommandLine(HWND hWnd, HINSTANCE hInstance);
int LoadSimFile( char *pChar );
extern int WINAPI TTYLoggerProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
extern void AddLogMessage(char* msg, int crlf);
//
//  TTY functions
//
void OpenStatusToolbar( HWND );
BOOL CmdAbout( HWND );
extern void SendIRQ7(void);
extern void GoF80000(void);
extern void GoFlash(void);
extern void GoVersion(void);
extern void SendS7Record(void);
extern void PSCOMMAND(void);
extern void SendRESET(void);
extern void SendDL(void);
//
//  TTY functions
//
BOOL MoveTTYCursor( HWND );
BOOL KillTTYFocus( HWND );
BOOL SetTTYFocus( HWND );
BOOL SizeTTY( HWND, WORD, WORD );
int ParseVt100(HWND hTTY, struct TTYInfoStruct *pTTy, int msg, int p1, int p2 );
int Scr2BufChar( HWND hWnd, T_TTYInfoStruct *pTTY, POINT *pSrc, POINT *pDst );
int Buf2ScrChar( HWND hWnd, T_TTYInfoStruct *pTTY, POINT *pSrc, POINT *pDst );
int XY2ScrPixels( HWND hWnd, T_TTYInfoStruct *pTTY, POINT *pSrc, POINT *pDst );
int Scr2XYPixels( HWND hWnd, T_TTYInfoStruct *pTTY, POINT *pSrc, POINT *pDst );
void DumpTTYStruct(char *, int, int);
int CheckTTYStruct(char *s, int wParam,int lParam);
int GetBufBlock(HWND hWnd, struct TTYInfoStruct *pTTY, POINT *pBuf, char *pDst,int n );
int SetBufBlock(HWND hWnd, struct TTYInfoStruct *pTTY, POINT *pBuf, char *pSrc,int n );
//
//  Thread procedures
//
DWORD WINAPI ReaderAndStatusProc( LPVOID );
DWORD WINAPI WriterProc( LPVOID );

//
//  File transfer functions
//
void CALLBACK TransferRepeatDo( UINT, UINT, DWORD, DWORD, DWORD );  
void TransferRepeatCreate( LPCSTR, DWORD );
void TransferRepeatDestroy( void );
void TransferFileTextStart( LPCSTR );
void TransferFileTextEnd( void );
// void TransferFileText( LPCTSTR );
void ReceiveFileText( LPCTSTR );
DWORD GetAFrequency( void );

//
//  Buffer manipulation functions
//
void OutputABufferToWindow( HWND, char *, DWORD );
void OutputABuffer( HWND, char *, DWORD );
BOOL ClearTTYContents( void );
extern int freadLn(char *s, int maxc, FILE* fin);
//
// misc
//
extern UINT UChar2U32(unsigned char* bs);
extern void U32toUChar(unsigned char* bs, UINT t);
//
//  Status functions
//
HFONT CreateStatusEditFont( void );
void ReportStatusEvent( DWORD ); 
void CheckModemStatus( BOOL );
void ReportCommError( void );
void ReportComStat( COMSTAT* );
void StatusMessage( void );
void UpdateStatus( char * );
void CheckComStat( BOOL );
void LastErrorBox(char *Title);

//
//  Writer heap functions
//
BOOL WriterAddNewNode( DWORD, DWORD, char, char *, HANDLE, HWND );
BOOL WriterAddExistingNode( PWRITEREQUEST, DWORD, DWORD, char, char *, HANDLE, HWND );
BOOL WriterAddNewNodeTimeout( DWORD, DWORD, char, char *, HANDLE, HWND, DWORD );
BOOL WriterAddFirstNodeTimeout( DWORD, DWORD, char, char *, HANDLE, HWND, DWORD );

extern int WINAPI TTYEnterProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
extern int WINAPI Dlgmvxeditproc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Debug functions
extern void TraceWinMessages(char *txt, int msg, int wParam, int lParam);
extern void jlpDumpFrame(char* title, char* dp, int len);
extern void jlpDumpCtl(char* title, t_ctlreqpkt* pp);
extern void jlpCtlRoute(char* title, t_ctlreqpkt* pp);
extern void jlpChkDataPktHdr(char* title, t_ctldatapkt* pDpk);
#endif
