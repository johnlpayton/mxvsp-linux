/*******************************************************************************
*   FILE: muxvtty.c
*
*   Window muxtty. Terminal screen child window
*
*   Comments:
*   This is a window that can be a child or a floating one.  It provides a limited
*   subset of VT102 terminal emulation
*
*   2012-mar-30 jlp
*   Finally a reasonable working TTY.  The main problems seem to be
*   centering around W32 concept of WM_PAINT.  For speed, se scroll the
*   bits when needed but sometimes large screen areas get blanked out.
*   Fortunatly minimize/restore can be used to repaint. I'm going to try
*   a menu button "refresh" at some time later. For not, a little use to
*   alpha test the screens seems to be in order.
*
*   2012-mar-26 jlp
*   Added VT102
*       \e[n:mH CUP (poisition)
*       \e[K    Kill line
*       \e[J    clear screen
*   sub variants are not yet enable. Probably the most important next is
*       \e[0K and \e[K which does cursor to EOL
*   so somebody can write text then clear to the end. For now they can
*   clear then write.
*
*   Screens, History, and escape sequences.
*   An unexpeced complicated topic.
*   We need to review this again. Currently we defer scrolling until
*   the end of a block of data.  To do this we allow writing "offscreen"
*   to (the oldest part of) the history.  Then we block scroll.  The problem
*   is tracking the virtual screen top.  A <LF> scrolls the history so we
*   track <LF> with an offset counter (reverse <LF> is not allowed). when writing to
*   the history, (pTTY->curow+pTTY->offset) is used. At the end we scroll up
*   by offset lines and write the "hidden" lines to the screen. There is a
*   large complexity with the W32 concept of update regions which is confusing
*   the design.  It is not sure it this is the major problem, but it is not minor.
*   I'm a little confused about handling curow and offset so need to sit and think
*
*   Let's see, when I scroll after a block, sync the top of the biffer
*   with the history. So I can keep curow inc'ed and access the buffer
*   without the offset. The offset should only come into play for the
*   scroll after and for any VT102 sequences.
*
*   There are 3 memory blocks.
*   1) Visible screen is the area the user can see. The "visible"
*       screen refers to lines. Areas to the right that are outside
*       the window are still part of the "visible" screen.
*   2) Screen image is an extended screen that keeps areas of the
*       screen that are not visible. There are issues that d
*   3) History Past history that has scrolled outside of the screen image.
*       This is accessable only by the buildin editor function.  Normally
*       this must be accessed for cut/paste because the characters on the
*       screen or in the screen image cannot be selected by the mouse.
*
*   VT102 Escape sequences:
*   These effect only the visible screen.  The cursor position (1,1) is the
*   upper-left cell on the screen. CUP (or other) escape sequences to move the
*   cursor will be clamped to the visible screen.  The visible screen scrolls
*   only when a <LF> (or more) moves the cursor below the bottom line. When this
*   happens, both the screen image and the history are adjusted. A VT102 CUP
*   can move the cursor to the right outside the visible screen (but not
*   outside the buffer).
*
*   Resize (WM_SIZE and WM_PAINT). When the window size is changed, new cursor
*   parameters are computed. Thes are issues not present in hard VT102 terminals.
*   There, the screen is physical and cannot be changed. Hmmm. I think what we will
*   do is to pin the screen to the upper left and allow it to go down. A <LF> moves
*   the cursor down until it hits the bottom. Then the screen will scroll. Now what
*   do we do with a resize?  In most applications, the cursor is BOT. When we
*   resize, it's to see previous stuff.  Suppose we resize when curcor is
*   2 lines above BOT?
*
*   Some conventions:
*   We use FORTRAN array conventions
*
*   curow   Current screen row to put characters
*           characters will be copied into the screen history
*           at putrow (C putrow-1)
*   nRows   Last row visible on the screen. When curow > nRows
*           history outside the screen is used.
*   offsetrow When a linefeed comes in, this is incremented.
*           scrolling will reset this value
*
*   Externals
*
*
*******************************************************************************/

#include "jtypes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#if USEGDK
#include <gdk/gdk.h>
#include <gtk/gtk.h>
#endif

#if USEWIN32
#include <windows.h>
#include <commdlg.h>
#include <alloc.h>
#include <commctrl.h>
#include <shlobj.h>
#include <fcntl.h>
#include <process.h>
#endif

#include "memlock.h"
#include "muxvttyW.h"
#include "muxw2ctl.h"

#if USEWIN32
#include "mttty.h"
#endif

extern HFONT ghFontVTTY;
extern struct S_inicfg inicfg;

extern void JlpTraceMsg(int, int, int);

static int CountLF(char* buf, int lbuf);
static int TTYScrollUp(HWND hWnd, HDC hdc, t_mvsptty* pTTY, int nlines);
static int TTYPartBufOut(HWND hWnd, HDC hdc, t_mvsptty* pTTY, char* buf, int nout);
static int TTYSavedLineOut(HWND hWnd, HDC hdc, t_mvsptty* pTTY, char* buf, int nout);
static int TTYNonPrintBuf(HWND hWnd, HDC hdc, t_mvsptty* pTTY, char* buf, int nout);
static int TTYEscBuf(HWND hWnd, HDC hdc, t_mvsptty* pTTY, char* buf, int nout);
static int doParseVT102(HWND hWnd, HDC hdc, t_mvsptty* pTTY, char* buf, int nout);
static int doExeVT102(HWND hWnd, HDC hdc, t_mvsptty* pTTY, char cmd);
static int TTYRenderText(HWND hWnd, HDC hdc, t_mvsptty* pTTY, char* buf, int nout);
static BOOL Char2Pix(t_mvsptty* pTTY, int row, int col, int* x, int* y);
static int Char2Image(t_mvsptty* pTTY, int row, int col);
static int GetLineLen(t_mvsptty* pTTY, char* buf);

void FillALine(t_mvsptty* pTTY, char* pMem, int n)
{
    int k;
    char tmpstr[8];
    char* pChar;
    
    k = n*pTTY->nCols;
    pChar = pTTY->scrbuf;
    pChar = &(pChar[k]);
    memset(pChar,'-',pTTY->nCols);
    sprintf(tmpstr,"%04d",k);
    strcpy(pChar,tmpstr);
    pChar[strlen(tmpstr)]=' ';
    k=pTTY->nCols;
    pChar[k-1]=0x0a;
    pChar[k-2]=0x0d;
}


/*---------------------------------------------------------------------------
*  BOOL muxvtty(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
*
*  Description:
*   TTY window child process
*
*  Parameters:
*     HWND hWnd
*        handle to TTY window (as always)
*
*  Comments
*   This is the child TTY window for muxtty  It handles a single
*   terminal window.  The window is a output only (display).  Auto
*   scrolling occurs when a <LF> drops the cursor out of view.
*   The is no scrolling of the window itself.  By program design,
*   access to the history buffer is with a edit window.
*
*   The main access (outside of management functions) is
*   VSP_SETTEXT:    len,pointer     Enter text to the window
*   VSP_HOME:       0,0             Home the cursoe
*   VSP_CLS:        0,0             Clear the visible
*   VSP_CLRALL:     0,0             Clear screen and history
*
*  History:   Date       Author      Comment
*  24-Feb-2012 jlp
*   initial pass
*
*---------------------------------------------------------------------------*/
BOOL CALLBACK _export muxvtty(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{

    BOOL    retval=TRUE;
    struct S_LnumParam *pParam;
    t_mvsptty* pTTY;
    RECT    ScrRect;
    HDC     hdc,oldDC;
    HFONT   oldfont;
    int     k,m,n;
    char*   pChar;
    char    tmpTxt[32];
    
#if EASYWINDEBUG & 0
TraceWinMessages("muxvtty",msg,wParam,lParam);
#endif

    pTTY = (t_mvsptty*)GetWindowLong(hWnd,GWL_USERDATA);
    if( !pTTY )
    {
        pTTY = malloc(sizeof(t_mvsptty));
        if(!pTTY)
        {
            retval = -1;                    // oops no memory, get out
            return(retval);
        }
        SetWindowLong(hWnd,GWL_USERDATA,(long)pTTY); // stash for later use
        memset(pTTY,0,sizeof(t_mvsptty));   // clear it out
    }

    switch  ( msg & 0xffff )
    {
        //==============
        case WM_CREATE:         // Initialize
        //==============
        {

            //======== set some default params
            //  We don't need to be picky since WM_SETFONT and WM_SIZE are coming
            hdc                 = GetDC(hWnd);
            pTTY->hfont         = SelectObject( hdc, GetStockObject(SYSTEM_FONT));
            SelectObject(hdc,pTTY->hfont);      // put it back
            ReleaseDC(hWnd, hdc) ;

            pTTY->bkcolor       = GetSysColor( COLOR_WINDOW );
            pTTY->fgcolor       = RGB(0,0,0);   // black
            pTTY->margin        = 2;            // pixel margin
            GetClientRect(hWnd,&(pTTY->screenRect)); // graphics rescangle
            pTTY->charH         = 12;           // height
            pTTY->charW         = 8;            // width
            
            pTTY->curow         = 1;            // upper
            pTTY->offsetrow     = 0;            // line feed offset
            pTTY->cucol         = 1;            // left
            pTTY->nRowImage     = SCRROWMAX;
            pTTY->nColImage     = SCRCOLMAX;
            pTTY->nRows         = pTTY->nRowImage;
            pTTY->nCols         = pTTY->nColImage;
            pTTY->putrow        = 1;
            pTTY->inEscSeq      = 0;
            pTTY->nscrsize      = pTTY->nRows*(pTTY->nCols);
#if 0
            pChar               = malloc(pTTY->nscrsize);
            if(!pChar)
                return(-1);
            pTTY->scrbuf = pChar;
#endif
            pChar = pTTY->scrbuf;
            memset(pChar,' ',pTTY->nscrsize); // fill with <SP>
#if EASYWINDEBUG & 0
for(k=0;k<pTTY->nRowImage;k++)
{
FillALine(pTTY,pChar,k);
}
#endif

            retval=0;
        }
        break;

        //==============
        case WM_CLOSE:  // REVISIT: Close a Vxtty window
        //==============
// We migh want to make the parent close us instead of telling him we closed
            if(1 || (pTTY->myID))
            {
                SendMessage(pTTY->parent,VSP_CLOSING,pTTY->myID,(LPARAM)hWnd);
#if JLPMIDI
                retval = DefMDIChildProc(hWnd, msg, wParam, lParam);
#else
                retval = DefWindowProc(hWnd, msg, wParam, lParam);
#endif
            }
            return(retval);
//        break;

        //==============
        case WM_DESTROY:
        //==============
#if EASYWINDEBUG & 0
TraceWinMessages("muxvtty.WM_DESTROY",msg,wParam,lParam);
#endif
#if 0
            pChar = pTTY->scrbuf;
            if(pChar)
            {
                free(pChar);
                pTTY->scrbuf = NULL;
            }
#endif
            free(pTTY);                         // dump our screen structure
            pTTY = NULL;                        // mark as empty
            SetWindowLong(hWnd,GWL_USERDATA,(long)pTTY); // mark empty
            retval = 0;
        break;

        //==============
        case WM_SIZE: // somebody change our size
        //==============
        {
            GetClientRect(hWnd,&ScrRect);
            pTTY->screenRect = ScrRect;
            pTTY->nRows = ScrRect.bottom/pTTY->charH-1;
            if(pTTY->nRows >= pTTY->nRowImage)
                pTTY->nRows = pTTY->nRowImage;
            if(pTTY->nRows <= 4)
                pTTY->nRows = 4;
//            pTTY->nCols=80;
#if EASYWINDEBUG & 0
printf("muxvtty.WM_SIZE pTTY->nRows %d pTTY->nCols %d\n",pTTY->nRows,pTTY->nCols);
#endif
            InvalidateRect(hWnd, NULL, TRUE ) ;
#if JLPMIDI
            retval = DefMDIChildProc(hWnd, msg, wParam, lParam);
            return(retval);
#endif
        }
//        break;

        //==============
        case WM_SETFONT: // new font compute character sizes and redraw
        //==============
        {
            TEXTMETRIC tm;

            pTTY->hfont = (HFONT)wParam;        // save for later
            hdc = GetDC(hWnd);                  // get graphics context
            SelectObject( hdc, pTTY->hfont ) ;  // put font in
            GetTextMetrics( hdc, &tm ) ;        // compute height and width sizes
            pTTY->charW = tm.tmAveCharWidth  ;
            pTTY->charH = tm.tmHeight + tm.tmExternalLeading ;
            SendMessage(hWnd,WM_SIZE,0,0);
            ReleaseDC(hWnd, hdc) ;
            InvalidateRect(hWnd, NULL, TRUE);   // mark window for a full repaint
        }
        break;

        //==============
        case WM_ACTIVATE:       // we're becomming active/inactive
        //==============
#if EASYWINDEBUG & 0
// if activated by any means, tell the parent
// he will keep the most recently activated window
// not sure exactly how this will be used but we keep it
// see W32 document, we might want to change to mouse click only
printf("muxvtty.WM_ACTIVATE.0: hWnd 0x%08x wParam 0x%08x 0x%08x\n",hWnd,wParam,lParam);
#endif
            if((wParam & 0Xffff) == WA_CLICKACTIVE	)
                SendMessage(pTTY->parent,VSP_TELLID,pTTY->myID,(LPARAM)hWnd);
        break;

        //==============
        case WM_MDIACTIVATE :       // we're becomming active/inactive
        //==============
#if EASYWINDEBUG & 0
printf("muxvtty.WM_MDIACTIVATE.0: hWnd 0x%08x wParam 0x%08x 0x%08x\n",hWnd,wParam,lParam);
#endif
            if(lParam  == (LPARAM)hWnd)
            {
#if EASYWINDEBUG & 0
printf("muxvtty.WM_MDIACTIVATE.1: parent 0x%08x myID 0x%08x hWnd 0x%08x\n",pTTY->parent,pTTY->myID,hWnd);
#endif
                SendMessage(pTTY->parent,VSP_TELLID,pTTY->myID,(LPARAM)hWnd);
                retval = 0;
            }
            else
            {
                retval = 1;
            }
        break;

        //==============
        case WM_PAINT:          // Redraw the window
        //==============
        {
            PAINTSTRUCT  ps ;
            hdc = BeginPaint( hWnd, &ps ) ;
            oldfont = SelectObject( hdc, pTTY->hfont );

            m = 1;
            for(k=1; k <= pTTY->nRows; k+= 1)
            {
                pChar = pTTY->scrbuf;
                pChar = &pChar[Char2Image(pTTY, m, 1)];
#if EASYWINDEBUG & 0
{
char saveChar;
char *pChar;
saveChar = pChar[10];
pChar[10]=0;
printf("Paint <%s>\n",pChar);
pChar[10]=saveChar;
}
#endif
                TTYSavedLineOut(hWnd, hdc, pTTY, pChar, m); // fixme
                
                m += 1;
            }
            // repaint all
            SelectObject( hdc, oldfont);
            EndPaint( hWnd, &ps ) ;
        }
        break;

        //==============
        case VSP_SETTEXT:       // Enter text
        //==============
        {
#if EASYWINDEBUG & 0
            FILE* fdbg;
            fdbg=fopen("DBG.bin","wb");
            pChar = pTTY->scrbuf;
            fwrite(pChar,1,pTTY->nscrsize,fdbg);
            fclose(fdbg);
#endif
            if(wParam <= 0)                     // ignore 0 length packets
                break;
            hdc = GetDC(hWnd);                  // get graphics context
            oldfont = SelectObject( hdc, pTTY->hfont );
            TTYRenderText(hWnd, hdc, pTTY, (char*)lParam, wParam);
            SelectObject( hdc, oldfont);
            ReleaseDC(hWnd,hdc);

        }
        break;

        //==============
        case VSP_HOME:          // Home cursor
        //==============
        {
            pTTY->curow         = 1;            // far left
            pTTY->cucol         = 1;            // upper
        }
        break;

        //==============
        case VSP_CLS:           // Clear screen and home cursor
        //==============
        {
            pTTY->curow         = 1;            // far left
            pTTY->cucol         = 1;            // upper
            hdc = GetDC(hWnd);                  // get graphics context
            oldfont = SelectObject( hdc, pTTY->hfont );
            TTYRenderText(hWnd, hdc, pTTY, (char*)"\033[1:1H\033[2J", 10);
            SelectObject( hdc, oldfont);
            ReleaseDC(hWnd,hdc);
        }
        break;

        //==============
        case VSP_CLRALL:        // Clear history, screen, and home cursor
        //==============
        {
            char* pChar;
            pChar = pTTY->scrbuf;
            memset(pChar,' ',pTTY->nscrsize); // fill with <SP>
            hdc = GetDC(hWnd);                  // get graphics context
            oldfont = SelectObject( hdc, pTTY->hfont );
            TTYRenderText(hWnd, hdc, pTTY, (char*)"\033[1:1H\033[2J", 10);
            SelectObject( hdc, oldfont);
            ReleaseDC(hWnd,hdc);
        }
        break;

        //==============
        case VSP_SETPARENT:     // set the parent handle
        //==============
        {
#if EASYWINDEBUG & 0
printf("muxvtty.VSP_SETPARENT 0x%08x 0x%08x\n",wParam,lParam);
#endif
            pTTY->myID          = wParam;       // my ID
            pTTY->parent        = (HWND)lParam; // parent handle
        }
        break;

        //==============
        case VSP_GETSIZE:       // Get the char count (include <CR><LF>)
        //==============
        {
            n = 0;
            for(k=1; k <= pTTY->nRowImage; k += 1)
            {
                pChar = pTTY->scrbuf;
                pChar = &pChar[Char2Image(pTTY,k,1)];
                n += 2+GetLineLen(pTTY, pChar);
            }
            retval = n;
        }
        break;

        //==============
        case VSP_GETLINES:  // get history lines
        //==============
        {
            char* pCharout = (char*)lParam;
            n = 0;
            for(k=1; k <= pTTY->nRowImage; k += 1)
            {
                pChar = pTTY->scrbuf;
                pChar = &pChar[Char2Image(pTTY,pTTY->nRows+k,1)];
                m = GetLineLen(pTTY, pChar);
                memcpy(pCharout,pChar,m);
                pCharout += m;
                *pCharout++ = XCR;
                *pCharout++ = XLF;
                n += m+2;
            }
            *pCharout = 0;                      // null terminate
            retval = n;
        }
        break;

        //==============
        case VSP_SETIMAGESIZE:  // set the image size (stored screen)
        //==============
        {
        }
        break;

        //==============
        case VSP_SETHISTSIZE:  // set the history size
        //==============
        {
        }
        break;

        //==============
        case VSP_GETSCRIMAGE:  // get screen image (with <CR><LF>)
        //==============
        {
        }
        break;

        //==============
        default:
        //==============
#if JLPMIDI
            retval = DefMDIChildProc(hWnd, msg, wParam, lParam);
#else
            retval = DefWindowProc(hWnd, msg, wParam, lParam);
#endif
        break;
    }
#if EASYWINDEBUG & 0
printf("muxvtty.return: retval %d row %d col %d\n",retval,pTTY->curow,pTTY->nRows);
#endif
    return (retval);                  /* Didn't process a message    */

}

/*---------------------------------------------------------------------------
*  HWND NewWinmuxvtty(HWND parent, HANDLE hInstance, LPCTSTR wName)
*
*  Description:
*   Open a window
*
*  Parameters:
*     HWND parent        Parent
*     HANDLE hInstance   Parent Instance
*     LPCTSTR wName      Window name
*
*  Comments
*   Utility to count the number of line feeds for scrolling
*
*  History:   Date       Author      Comment
*  24-Feb-2012 jlp
*   initial pass
*
*---------------------------------------------------------------------------*/
HWND NewWinmuxvtty(HWND parent, HANDLE hInstance, int tid, LPCTSTR wName)
{

    HWND    hMe;
    DWORD   wStyle;
    MDICREATESTRUCT pc;

#if JLPMIDI
    pc.szClass = "MVSPChild";
    pc.szTitle = wName;
    pc.hOwner = ghInst;
    pc.x = pc.y = CW_USEDEFAULT;
    pc.cx = pc.cy = CW_USEDEFAULT;
    pc.style = 0;
    pc.lParam = tid;
    hMe = (HWND)SendMessage(ghwndContent,WM_MDICREATE,0,(LPARAM)&pc);
#else
    wStyle = 0
            |WS_OVERLAPPEDWINDOW
            |WS_CLIPCHILDREN
            |0;

    hMe = CreateWindow("MVSPChild", wName,
            wStyle,
            CW_USEDEFAULT, CW_USEDEFAULT,
            inicfg.TTYWidth, inicfg.TTYHeight,
            NULL, NULL, hInstance, NULL);

#endif

    if (hMe == NULL) {
        return NULL;
    }
    SendMessage(hMe,VSP_SETPARENT,tid,(LPARAM)parent);
    SendMessage(hMe, WM_SETFONT, (WPARAM)ghFontVTTY, 0);
    SendMessage(hMe, WM_SIZE, 0, 0);
    InvalidateRect(hMe,NULL,TRUE);
    ShowWindow( hMe, SW_SHOWNORMAL) ;
    UpdateWindow( hMe ) ;
    
    return(hMe);
}

/*---------------------------------------------------------------------------
*  static BOOL Char2Pix(t_mvsptty* pTTY, int row, int col, int* x, int* y)
*
*  Description:
*   Convert a VT102 character cell to a pixel location
*
*  Parameters:
*     t_mvsptty* pTTY   pTTY structure pointer
*     int               row  VT102 row number
*     int               col  VT102 col number
*     int*              x   destination
*     int*              y   destination
*
*   Return
*     TRUE  position is on the visable screen
*     FALSE position is not on the visable screec
*
*  Comments
*   Utility convert a character position for drawing
*       VT102 place the cell coordinate of (1,1) as the upper left
*       W32 draws a character Below ant to the right of the position.
*   this routine will adjust the VT102 coordinates to the W32 with the
*   margins. If the final point is not on the screen, it returns
*   FALSE otherwist it returns TRUE
*
*  History:   Date       Author      Comment
*  24-Feb-2012 jlp
*   initial pass
*
*---------------------------------------------------------------------------*/
static BOOL Char2Pix(t_mvsptty* pTTY, int row, int col, int* x, int* y)
{
    POINT pt;
    
    *x = 0;                                     // just keep in bounds
    *y = 0;

    pt.x = pTTY->margin+pTTY->charW*(col-1);
    pt.y = pTTY->margin+pTTY->charH*(row-1);
#if EASYWINDEBUG & 0
if(inicfg.prflags & PRFLG_VSP)
{
printf("Char2Pix: (%d,%d) -> (%d,%d)\n",row,col,pt.x,pt.y);
}
#endif

    if(PtInRect(&pTTY->screenRect,pt))          // W32 screen
    {
        *x = pt.x;
        *y = pt.y;
        return (1);
    }
    
    return(0);
}

/*---------------------------------------------------------------------------
*  static int Char2Image(t_mvsptty* pTTY, int row, int col)
*
*  Description:
*   Convert a VT102 character cell to a index in the image
*
*  Parameters:
*     t_mvsptty* pTTY   pTTY structure pointer
*     int               row  VT102 row number
*     int               col  VT102 col number
*
*   Return
*     n>=0  position is in the image
*     -1    position is not in the image
*
*  Comments
*   Utility convert a character position for storage
*
*  History:   Date       Author      Comment
*  24-Feb-2012 jlp
*   initial pass
*
*---------------------------------------------------------------------------*/
static int Char2Image(t_mvsptty* pTTY, int row, int col)
{
    int     n;
    
    n = (row-1)+ (pTTY->putrow-1);
    
    while(n >= pTTY->nRowImage)
        n -= pTTY->nRowImage;
    while(n < 0)
        n += pTTY->nRowImage;

    n = n*pTTY->nColImage +(col-1);
#if EASYWINDEBUG & 1
if( (n<0) || (n >= pTTY->nscrsize))
{
    printf("Char2Image: error (%d,%d) -> (%d)\n",row,col,n);
    n=0;
}
#endif
#if EASYWINDEBUG & 0
if(inicfg.prflags & PRFLG_VSP)
{
    printf("Char2Image:(%d,%d) -> %d (%d)\n",row,col,n,n/pTTY->nColImage);
}
#endif

    return(n);
}

/*---------------------------------------------------------------------------
*  static int CountLF(char* buf, int lbuf)
*
*  Description:
*   Count line feeds in the buffer
*
*  Parameters:
*     char* buf         buffer
*     int lbuf          number of characters
*
*  Comments
*   Utility to count the number of line feeds for scrolling
*
*  History:   Date       Author      Comment
*  24-Feb-2012 jlp
*   initial pass
*
*---------------------------------------------------------------------------*/
static int CountLF(char* buf, int lbuf)
{
    int k,klf;
    klf = 0;
    for(k=0;k<lbuf;k++)
    {
        if(buf[k] == XLF)
            klf += 1;
    }
    return(klf);
}


/*---------------------------------------------------------------------------
*  static int TTYPartBufOut(HWND hWnd, t_mvsptty* pTTY, char* buf, int nout)
*
*  Description:
*   Print part of a buffer
*
*  Parameters:
*     HWND hWnd         window
*     t_mvsptty* pTTY   tty structure
*     char* buf         character buffer
*     int nout          number to print
*
*  Comments
*   Helper to print buffers on a screen.
*   If the first character is non printing, the routine returns -1;
*   It then scans the input buffer for printable characters.
*
*  History:   Date       Author      Comment
*  24-Feb-2012 jlp
*   initial pass
*
*---------------------------------------------------------------------------*/
static int TTYPartBufOut(HWND hWnd, HDC hdc, t_mvsptty* pTTY, char* buf, int nout)
{
    int     kout;
    int     endflag;
    int     mout;
    char*   pChar;

    // do we have any characters to print?
    if(nout <= 0)
        return(0);

    if(!isprint(buf[0]))                        // first character is non printing
        return(-1);

    // Calculate the character position in the history buf
    pChar = pTTY->scrbuf;
    pChar = &pChar[Char2Image(pTTY,pTTY->curow,pTTY->cucol)];
    
    // scan the input for printable characters
    kout = 0;
    while(1)
    {
        //========
        if( kout > 200)                 // fault check
        //========
        {
            endflag = 1;
            break;
        }
        //========
        else if(kout >= nout)           // finished with the buffer
        //========
        {
            endflag = 2;
            break;
        }
        //========
        else if(!isprint((255&buf[kout]) ))    // non printing
        //========
        {
            endflag = 3;
            break;
        }
        else                            // continue
        {
            kout += 1;
        }
    }
    
    // analyize the scan
    
    switch(endflag)
    {
        //========
        case 1: // fault
        //========
        {
            return(nout);                       // dump them
        }
//        break;

        //========
        case 3: // ran into a non printing at kout < nout
        case 2: // all characters are printing
        //========
        {
            mout = kout;
            if(mout + pTTY->cucol >= pTTY->nCols)
                mout = pTTY->nCols - pTTY->cucol;
        }
        break;
    }
    
    if(mout > 0)                              // print those we found
    {
        int ix,iy;
#if EASYWINDEBUG & 0
if(inicfg.prflags & PRFLG_VSP)
{
printf("TTYPartBufOut: (%d,%d) %d\n",pTTY->curow,pTTY->cucol,kout);
}
#endif
        memcpy(pChar,buf,mout);
        
        if(Char2Pix(pTTY,pTTY->curow, pTTY->cucol, &ix, &iy))
        {
        TextOut(hdc,
            ix,
            iy,
            buf,
            mout);
        }
        pTTY->cucol += mout;                  // adjust the cursor
    }
    
    // return the number in the scan because we look at all of these
    return(kout);
}

/*---------------------------------------------------------------------------
*  static int GetLineLen(t_mvsptty* pTTY, char* buf)
*
*  Description:
*   Print a saved line WM_PAINT
*
*  Parameters:
*     t_mvsptty* pTTY   tty structure
*     char* buf         character buffer
*
*  Comments
*   Helper get line length
*
*  History:   Date       Author      Comment
*  24-Feb-2012 jlp
*   initial pass
*
*---------------------------------------------------------------------------*/
static int GetLineLen(t_mvsptty* pTTY, char* buf)
{
    int kout;
    
    for(kout = pTTY->nCols-1; kout > 0; kout -= 1) // find the last printing character
    {
        if(buf[kout] > ' ')
            break;
    }
    
    return kout+1;
}

/*---------------------------------------------------------------------------
*  static int TTYSavedLineOut(HWND hWnd, t_mvsptty* pTTY, char* buf, int rw)
*
*  Description:
*   Print a saved line WM_PAINT
*
*  Parameters:
*     HWND hWnd         window
*     t_mvsptty* pTTY   tty structure
*     char* buf         character buffer
*     int nout          number to print
*
*  Comments
*   Helper to pring buffers on a screen.
*   This is for use by WM_PAINT.
*   It renders a line starting at col 1 into row rw where rw is the screen
*   row (1 is top)
*
*   The caller must have done all the HDC stuff before calling this routine.
*   The main reason it exists is to render characters at the correct places
*
*  History:   Date       Author      Comment
*  24-Feb-2012 jlp
*   initial pass
*
*---------------------------------------------------------------------------*/
static int TTYSavedLineOut(HWND hWnd, HDC hdc, t_mvsptty* pTTY, char* buf, int rw)
{
    int kout;
    int px,py;

    if( (rw < 0)|| (rw > pTTY->nRowImage) )     // too many lines scrolled
        return 1;

    kout = GetLineLen(pTTY, buf);
#if 0
    for(kout = pTTY->nCols-1; kout > 0; kout -= 1) // find the last printing character
    {
        if(buf[kout] >= ' ')
            break;
    }
#endif
    if(kout < 0)
        return(kout);
#if EASYWINDEBUG & 0
{
char saveChar;
char *pChar;
pChar = buf;
saveChar = pChar[10];
pChar[10]=0;
printf("TTYSavedLineOut <%s>\n",pChar);
pChar[10]=saveChar;
}
#endif

    if(Char2Pix(pTTY,rw, 1, &px, &py))
    {
        TextOut(hdc,
            px,
            py,
            buf,
            kout);
    }

    return(kout);
}

/*---------------------------------------------------------------------------
*  static int TTYNonPrintBuf(HWND hWnd, t_mvsptty* pTTY, char* buf, int nout)
*
*  Description:
*   Process non printing characters
*
*  Parameters:
*     HWND hWnd         window
*     t_mvsptty* pTTY   tty structure
*     char* buf         character buffer
*     int nout          number to print
*
*  Comments
*   This is called when TTYPartBufOut detects a nonprinting character
*   as the first in the buffer.  It will have rendered all characters
*   before this call and changed the position so the first character
*   in here is a non printing one. This looks at the character and
*   does what is needed.  It must eat at least one character from the
*   input so as not to hang
*
*   We have to review the line end stuff.  A <CR> is ending a line but
*   really should not. It should just move the cursor.  The review needs
*   to be considered with the WM_PAINT usage. <BS> and othe controls as well.
*
*  History:   Date       Author      Comment
*  24-Feb-2012 jlp
*   initial pass
*
*---------------------------------------------------------------------------*/
static int TTYNonPrintBuf(HWND hWnd, HDC hdc, t_mvsptty* pTTY, char* buf, int nout)
{
    int     retval = 1;
    int     kout;
    int     k;
    char*   pChar;
    
    if(isprint(buf[0]))
    {
#if EASYWINDEBUG & 0
if(inicfg.prflags & PRFLG_VSP)
{
printf("TTYNonPrintBuf: Printing character detected <%c>\n",buf[0]);
}
#endif
        return(retval);                         // eat the character
    }

    switch(buf[0]&0x7f)
    {
        //========
        case XLF:
        //========
        {
            pTTY->curow += 1;
            if(pTTY->curow > pTTY->nRows)       // off screen?
                pTTY->offsetrow += 1;           // bump the offset count

            // When going into the hidden area clear the new lines
            
            if(pTTY->curow > pTTY->nRows+pTTY->offsetrow-1)
            {
                pChar = pTTY->scrbuf;
                pChar = &pChar[Char2Image(pTTY,pTTY->curow,1)];
                memset(pChar,' ',pTTY->nCols);
            }
        }
        break;

        //========
        case XCR:   // cr
        //========
        {
            pTTY->cucol = 1;                    // <CR> move to col 1
        }
        break;

        //========
        case XBS:   // backspace
        //========
        {
            pTTY->cucol -= 1;                   // <BS> backspace
            if(pTTY->cucol < 1)                 // clamp
                pTTY->cucol = 1;
        }
        break;


        //========
        case XHT:   // <TAB>
        //========
        {
            // comput how many spaces to output
            k = (pTTY->cucol - 1)/XTABSPACE;
            kout = (k+1) * XTABSPACE + 1 - pTTY->cucol;
            
            // in a loop (tabs are rare)
            for(k=0;k<kout;k++)
            {
                int ix,iy;
                
                if(pTTY->cucol >= pTTY->nCols)  // screen limit test
                    break;
                    
                pChar = pTTY->scrbuf;
                pChar = &pChar[Char2Image(pTTY,pTTY->curow,pTTY->cucol)];
                *pChar = ' ';

                if(Char2Pix(pTTY,pTTY->curow, pTTY->cucol, &ix, &iy))
                {
                TextOut(hdc,
                    ix,
                    iy,
                    " ",
                    1);
                }
                pTTY->cucol += 1;                  // adjust the cursor
            }
        }
        break;

        //========
        case XESC:
        //========
        {
            pTTY->inEscSeq = 1;                     // <ESC> start escape detect
        }
        break;

        //========
        default:                                    // probably binary, ignore
        //========
        {
#if EASYWINDEBUG & 0
printf("TTYNonPrintBuf: default <%02x>\n",buf[0]);
#endif
        }
        break;

    }
    return(retval);
}

/*---------------------------------------------------------------------------
*  static int TTYEscBuf(HWND hWnd, t_mvsptty* pTTY, char* buf, int nout)
*
*  Description:
*   Handles the VT102 subset of escape sequences
*
*  Parameters:
*     HWND hWnd         window
*     t_mvsptty* pTTY   tty structure
*     char* buf         character buffer
*     int nout          number to print
*
*  Comments
*  If an escape character was detected, this routine will be called.
*  pTTY->inEscSeq is a state variable. Current states
*   0) No escape
*   1) <ESC> was detected as the previous character
*   It must eat at least one character from the input so as not to hang
*   The VT102 sequence can hang. We'll later have a reset menu entry
*   to clear it out.
*
*  ESC-[ optional parames LETTER
*
*  History:   Date       Author      Comment
*  24-Feb-2012 jlp
*   initial pass
*
*---------------------------------------------------------------------------*/
static int TTYEscBuf(HWND hWnd, HDC hdc, t_mvsptty* pTTY, char* buf, int nout)
{
    int retval = 1;
    switch(pTTY->inEscSeq)
    {
        //========
        case 1: // got ESC, next must be '['
        //========
        {
            if(buf[0] == '[')
            {
                pTTY->nVTParams = 0;
                memset(pTTY->VTParamValues,0,4*sizeof(int));
                pTTY->inEscSeq = 2;
            }
        }
        break;

        //========
        case 2: // parse the sequence
        //========
        {
            retval = doParseVT102(hWnd, hdc, pTTY, buf, nout);
        }
        break;

        //========
        default:
        //========
        {
            pTTY->inEscSeq = 0;                 // reset escape    
        }
        break;
    }
    return(retval);                                  // eat one character
}

/*---------------------------------------------------------------------------
*  static int TTYScrollUp(HWND hWnd, HDC hdc, int nlines)
*
*  Description:
*   Scroll window up by pixels
*
*  Parameters:
*     HWND hWnd         window
*     int nlines        number of lines
*
*  Comments
*   This routine combines two operations.
*   1) It scrolls the graphics screen
*   2a) It "scrolls" the screen memory
*   2b) It fills scrolled screen memory with ' '
*   2c) It decrements the cursor to the scrolled position
*
*   the screen HDC must have need opened and valid
*
*  History:   Date       Author      Comment
*  24-Feb-2012 jlp
*   initial pass
*
*---------------------------------------------------------------------------*/
static int TTYScrollUp(HWND hWnd, HDC hdc, t_mvsptty* pTTY, int nlines)
{
    RECT ScrRect;
    RECT InvRect = {0};
    int ks,kbp;
    int k,m;
    char *pChar;
    

    if(nlines <= 0)                             // no scrolling needed
        return(1);

    // Graphics
    GetClientRect(hWnd,&ScrRect);
    ks = nlines*pTTY->charH;
#if EASYWINDEBUG & 0
if(inicfg.prflags & PRFLG_VSP)
{
printf("TTYScrollUp: nlines %d row %d col %d\n",nlines,pTTY->curow,pTTY->nRows);
}
#endif

//==== The windows paint does not work as expected, Experimental... seems ok

    if(ks < ScrRect.bottom)
    {
        ScrollWindowEx(hWnd,0,-ks,NULL,&ScrRect,NULL,&InvRect,SW_INVALIDATE+SW_ERASE);
#if EASYWINDEBUG & 0
printf("TTYScrollUp: ScrRect=(%d,%d,%d,%d)\n",ScrRect.left,ScrRect.top,ScrRect.right,ScrRect.bottom);
printf("TTYScrollUp: InvRect=(%d,%d,%d,%d)\n",InvRect.left,InvRect.top,InvRect.right,InvRect.bottom);
#endif
//        FillRect(hdc,&InvRect,GetStockObject(WHITE_BRUSH));
    }
    else
    {
//        FillRect(hdc,&ScrRect,GetStockObject(WHITE_BRUSH));
        m=1;
    }
    
    m = 1;
    for(k=1; k <= nlines; k+= 1)
    {
        pChar = pTTY->scrbuf;
        pChar = &pChar[Char2Image(pTTY, pTTY->nRows+k, 1)];
        TTYSavedLineOut(hWnd, hdc, pTTY, pChar, pTTY->nRows+k-nlines); // fixme
    }

//    InvalidateRect(hWnd,&InvRect,TRUE);
    return(1);
}

/*---------------------------------------------------------------------------
*  static int TTYRenderText(HWND hWnd, HDC hdc, t_mvsptty* pTTY, char* buf, int nout)
*
*  Description:
*   Render multiple text lines
*
*  Parameters:
*     HWND hWnd         window
*     t_mvsptty* pTTY   tty structure
*     char* buf         character buffer
*     int nout          number to print
*
*   Return number of lines rendered
*
*  Comments
*   Render a buffer with multiple lines
*
*   Ok, wht I was trying to do is to defer the scrolling to collect multiple lines.
*   There are problems here. When we write off the screen, the saved line might have junk in
*   it
*
*  History:   Date       Author      Comment
*  24-Feb-2012 jlp
*   initial pass
*
*---------------------------------------------------------------------------*/
static int TTYRenderText(HWND hWnd, HDC hdc, t_mvsptty* pTTY, char* buf, int nout)
{
    HFONT oldfont;
    int k,m,n;
    char* pChar;
    int retval;
    char tmpstr[16];
    

#if EASYWINDEBUG & 0  // debug
if(nout>80)
{
printf("TTYRenderText.1: Buf too big 0x%08x\n",nout);
return(0);
}
#endif

#if EASYWINDEBUG & 0
if(inicfg.prflags & PRFLG_VSP)
{
printf("TTYRenderText.2: %d \n",nout);
}
#endif
    if(nout <= 0)                               // nothing to do
        return(0);

    m = 0;
    do{
        if(pTTY->inEscSeq)                      // Escape sequence processing
        {
            k = TTYEscBuf(hWnd,hdc,pTTY,(char*)&(buf[m]),nout-m);
            m += k;
        }
        else                                    // expect normal characters
        {
            k = TTYPartBufOut(hWnd,hdc,pTTY,(char*)&(buf[m]),nout-m);
            if(k >= 0)                          // got some
            {
                m += k;
            }
            else                                // nope, check what they are
            {
                k = TTYNonPrintBuf(hWnd,hdc,pTTY,(char*)&(buf[m]),nout-m);
                m += k;
            }
        }
#if EASYWINDEBUG & 0
if(inicfg.prflags & PRFLG_VSP)
{
printf("TTYRenderText.3: pTTY->curow %d pTTY->cucol %d pTTY->offsetrow %d \n",pTTY->curow,pTTY->cucol,pTTY->offsetrow);
}
#endif
    }while((nout-m) > 0);

    // scroll if needed
    if(pTTY->offsetrow > 0)
    {

        // scroll the screen
        TTYScrollUp(hWnd, hdc, pTTY, pTTY->offsetrow);
        pTTY->putrow += pTTY->offsetrow;
        if(pTTY->putrow > pTTY->nRowImage)
            pTTY->putrow -= pTTY->nRowImage;
        pTTY->curow -= pTTY->offsetrow;

#if EASYWINDEBUG & 1
k = pTTY->putrow;
if((k<1) || (k>pTTY->nRowImage))
{ 
printf("TTYRenderText.4: Bad putrow wrap %d\n",k);
pTTY->putrow=1;
}
k = pTTY->curow;
if((k<1) || (k>pTTY->nRows))
{ 
printf("TTYRenderText.4: Bad curow wrap %d\n",k);
pTTY->curow=1;
}
#endif

    }
    pTTY->offsetrow = 0;

    return(retval);
}



/*---------------------------------------------------------------------------
*  static int doParseVT102(HWND hWnd, HDC hdc, t_mvsptty* pTTY, char* buf, int nout)
*
*  Description:Parse a VT102 sequence
*
*  Parameters:
*     HWND hWnd         window
*     t_mvsptty* pTTY   tty structure
*     char* buf         character buffer
*     int nout          number to print
*
*   Return number of characters used up
*
*  Comments
*   VT102 sequences are a numeric parameter list ended by an
*   alphabetic printing character. This parses the list and returns or
*   calls the execution.
*
*  History:   Date       Author      Comment
*  24-Feb-2012 jlp
*   initial pass
*
*---------------------------------------------------------------------------*/
static int doParseVT102(HWND hWnd, HDC hdc, t_mvsptty* pTTY, char* buf, int nout)
{
    int     retval;
    int     k,n;
    
    n = 0;
    while(1)
    {
        //========
        if(n >= nout) // new digit
        //========
        {
            retval = n;
            break;
        }
        //========
        else if(isdigit(buf[n])) // new digit
        //========
        {
            k = pTTY->VTParamValues[pTTY->nVTParams];   // shift the digit in
            k = k*10 +(0xf & (buf[n] - '0'));
            pTTY->VTParamValues[pTTY->nVTParams] = k;
            n += 1;

        }
        //========
        else if(buf[n] == ';') // seperator
        //========
        {
            if(pTTY->nVTParams < 3)             // inc if not maxed out
                pTTY->nVTParams += 1;
            else
                pTTY->VTParamValues[pTTY->nVTParams] = 0; // Oh, why not, the end one is never used
            n += 1;
        }
        //========
        else if(isprint(buf[n])) // printable
        //========
        {
            doExeVT102(hWnd, hdc, pTTY, buf[n]);
            pTTY->inEscSeq = 0;
            n += 1;
            retval = n;
            break;
        }
        //========
        else // bad toss collected characters
        //========
        {
            pTTY->inEscSeq = 0;
            n += 1;
            retval = n;
            break;
        }

    }
    return(retval);
}

/*---------------------------------------------------------------------------
*  static int doExeVT102(HWND hWnd, HDC hdc, t_mvsptty* pTTY, char cmd)
*
*  Description: Execute a VT102 sequence
*
*  Parameters:
*     HWND hWnd         window
*     t_mvsptty* pTTY   tty structure
*     char* buf         character buffer
*     int nout          number to print
*
*   execute our subset of VT102 commands
*
*  Comments
*
*  History:   Date       Author      Comment
*  24-Feb-2012 jlp
*   initial pass
*
*---------------------------------------------------------------------------*/
static int doExeVT102(HWND hWnd, HDC hdc, t_mvsptty* pTTY, char cmd)
{
    int k,m;
    char* pChar;
    
#if EASYWINDEBUG & 0
if(inicfg.prflags & PRFLG_VSP)
{
printf("Esc[%c %3d,%3d\n",cmd,pTTY->VTParamValues[0],pTTY->VTParamValues[1]);
}
#endif
    switch(cmd)
    {
        //========
        case '@': //@   ICH     Insert the indicated # of blank characters.
        //========
        {
        }
        break;

        //========
        case 'A': //A   CUU     Move cursor up the indicated # of rows.
        //========
        {
        }
        break;

        //========
        case 'B': //B   CUD     Move cursor down the indicated # of rows.
        //========
        {
        }
        break;

        //========
        case 'C': //C   CUF     Move cursor right the indicated # of columns.
        //========
        {
            k = pTTY->VTParamValues[0];         // first param
            if(k<=0)                            // default to 1
                k=1;
            k = pTTY->cucol+k;                  // new col
            if(k > pTTY->nCols)                 // clamp
                k = pTTY->nCols;
            pTTY->cucol = k;
        }
        break;

        //========
        case 'D': //D   CUB     Move cursor left the indicated # of columns.
        //========
        {
            k = pTTY->VTParamValues[0];         // first param
            if(k<=0)                            // default to 1
                k=1;
            k = pTTY->cucol-k;                  // new col
            if(k <= 0)                          // clamp
                k = 1;
            pTTY->cucol = k;
        }
        break;

        //========
        case 'H': //H   CUP     Move cursor to the indicated row, column (origin at 1,1).
        //========
        {
            k = pTTY->VTParamValues[0];         // first param
            if(k<1) k=1;
            if(k>pTTY->nRows) k=pTTY->nRows;
            pTTY->curow = k+pTTY->offsetrow;
            k = pTTY->VTParamValues[1];         // second param
            if(k<1) k=1;
            if(k>pTTY->nCols) k=pTTY->nCols;
            pTTY->cucol = k;
#if EASYWINDEBUG & 0
if(inicfg.prflags & PRFLG_VSP)
{
printf("CUP (%d,%d)->(%d,%d)\n",
    pTTY->VTParamValues[0],pTTY->VTParamValues[1],
    pTTY->curow,pTTY->cucol);
}
#endif
        }
        break;

        //========
        case 'J': //J   ED  Erase in display
        //========
        {
            RECT erect;
            int ix,iy;
            switch(pTTY->VTParamValues[0])
            {
#if 0
                //========
                case 0: // Cursor to end of display
                //========
                {
                }
                break;

                //========
                case 1: // erase from start to cursor
                //========
                {
                }
                break;
#endif
                //========
                //========
                case 0:
                case 1:
                case 2: // erase whole display **
                //========
                {
                    // erase the line on the screen
                    GetClientRect(hWnd,&erect); // graphics rescangle
                    FillRect(hdc,&erect,GetStockObject(WHITE_BRUSH));
//                        FillRect(hdc,&erect,GetStockObject(LTGRAY_BRUSH));
                    // fill line memory with spaces
                    for(k = 1; k <= pTTY->nRows; k++)
                    {
                        pChar = pTTY->scrbuf;
                        pChar = &pChar[Char2Image(pTTY, k, 1)];
                        memset(pChar,' ',pTTY->nColImage);
                    }
                }
                break;
            }
        }
        break;

        //========
        case 'K': //K   EL  Erase in line
        //========
        {
            RECT erect;
            int ix,iy;
            
            switch(pTTY->VTParamValues[0])
            {
                //========
                case 0: // Cursor (include) to EOL
                //========
                {
                    if(Char2Pix(pTTY,pTTY->curow, pTTY->cucol, &ix, &iy))
                    {
                        erect.left = ix;
                        erect.top = iy;
                        erect.right = pTTY->screenRect.right;
                        erect.bottom = erect.top + pTTY->charH;
                        FillRect(hdc,&erect,GetStockObject(WHITE_BRUSH));
                        pChar = pTTY->scrbuf;
                        pChar = &pChar[Char2Image(pTTY, pTTY->curow, pTTY->cucol)];
                        memset(pChar,' ',pTTY->nColImage-pTTY->cucol);
                    }

                }
                break;
                
#if 0
                //========
                case 1: // BOL to cursor
                //========
                {
                }
                break;
#endif
                //========
                case 1: // BOL to Cursor (include)
                case 2: // Entire line
                //========
                {
                    if(Char2Pix(pTTY,pTTY->curow, pTTY->cucol, &ix, &iy)||1)
                    {
                        // erase the line on the screen
                        erect.left = pTTY->screenRect.left;
                        erect.top = iy;
                        erect.right = pTTY->screenRect.right;
                        erect.bottom = erect.top + pTTY->charH;
                        FillRect(hdc,&erect,GetStockObject(WHITE_BRUSH));
//                        FillRect(hdc,&erect,GetStockObject(LTGRAY_BRUSH));
                        // fill line memory with spaces
                        pChar = pTTY->scrbuf;
                        pChar = &pChar[Char2Image(pTTY, pTTY->curow, 1)];
                        memset(pChar,' ',pTTY->nColImage);
                    }
                }
                break;
            }
        }
        break;

        //========
        default: //
        //========
        {
        }
        break;
    }
    
    return(1);
}

int VspAction(struct S_windowmap* pMapEntry, int cmd, long arg1, long arg2)
{
    int retval = 0;
    int k;
    
#if EASYWINDEBUG & 0
if(inicfg.prflags & PRFLG_VSP)
{
if(cmd != W2CMD_T500)
printf("VspAction:: *pData 0x%08x cmd %d arg1 0x%08x arg2 0x%08x\n",
    *((U32*)pData), cmd, arg1, arg2);
}
#endif

    switch(cmd)
    {
        case W2CMD_INIT:
        {
            pMapEntry->pData = (void*)arg1;               // save our eTid
            retval = 1;
        }
        break;
        
        case W2CMD_DESTROY:
        {
        }
        break;

        case W2CMD_TEXT:
        {
            SendMessage((HWND)(pMapEntry->wHnd),VSP_SETTEXT,(WPARAM)(arg2),(LPARAM)arg1);
        }
        break;

        case W2CMD_CUT:
        {
        }
        break;

        case W2CMD_COPY:
        {
        }
        break;

        case W2CMD_PASTE:
        {
        }
        break;

        case W2CMD_T500:
        {
        }
        break;
        
        default:
        {
        }
        break;
    }
    return retval;
}
