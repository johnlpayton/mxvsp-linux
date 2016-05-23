
/*-----------------------------------------------------------------------------
    This is a part of the Microsoft Source Code Samples. 
    Copyright (C) 1995 Microsoft Corporation.
    All rights reserved. 
    This source code is only intended as a supplement to 
    Microsoft Development Tools and/or WinHelp documentation.
    See these sources for detailed information regarding the 
    Microsoft samples programs.

    MODULE: TTYINFO.h

    PURPOSE: Contains global definitions for the TTYINFO structure

-----------------------------------------------------------------------------*/

#include <stdio.h>
#include <fcntl.h>
//
// constant definitions
//

//
// hard coded maximum number of ports
//
#define MAXPORTS        4

//
// terminal size
//
#define MAXROWS         (384)
#define MAXCOLS         80

//
// Margins We add margins to the TTY screen
// and to each character. we probably should put these in a
// dialog selection
//
#define MARGIN_PAGELEFT     4
#define MARGIN_PAGERIGHT    0
#define MARGIN_PAGETOP      0
#define MARGIN_PAGEBOT      4
#define MARGIN_CHARTALL     0       /* effectivly line spacing */
#define MARGIN_CHARWIDE     0       /* I don't know if windows can do this */


//
// cursor states
//
#define CS_HIDE         0x00
#define CS_SHOW         0x01

//
// ascii definitions
//
#define ASCII_BEL       0x07
#define ASCII_BS        0x08
#define ASCII_LF        0x0A
#define ASCII_CR        0x0D
#define ASCII_XON       0x11
#define ASCII_XOFF      0x13

//
// data structures
//
typedef struct TTYInfoStruct
{
    HWND    TTYWindow;
    HINSTANCE Inst;
    HANDLE  hCommPort, hReaderStatus, hWriter ;
    DWORD   dwEventFlags;
    CHAR    chFlag, chXON, chXOFF;
    WORD    wXONLimit, wXOFFLimit;
    DWORD   fRtsControl;
    DWORD   fDtrControl;
    BOOL    fConnected, fTransferring, fRepeating,
            fLocalEcho, fNewLine,
            fDisplayErrors, fAutowrap,
            fCTSOutFlow, fDSROutFlow, fDSRInFlow, 
            fXonXoffOutFlow, fXonXoffInFlow,
            fTXafterXoffSent,
            fNoReading, fNoWriting, fNoEvents, fNoStatus,
            fDisplayTimeouts;
    BYTE    bPort, bByteSize, bParity, bStopBits ;
    DWORD   dwBaudRate ;
    WORD    wCursorState ;
    HFONT   hTTYFont ;
    LOGFONT lfTTYFont ;
    DWORD   rgbFGColor ;
    COMMTIMEOUTS timeoutsorig;
    COMMTIMEOUTS timeoutsnew;
    
    // his stuff
    int     xSize, ySize;                   /* size of screen in pixels */
                                            /*** lets make this the writable size */
    int     xScroll, yScroll;               /* pixels for a jump scroll */
                                            /*** keep these */
    int     xOffset, yOffset;               /* pixel offsets */
                                            /*** change these to get away from frames (small numbers)) */
    int     nColumn, nRow;                  /* not consistant */
                                            /*** change this to Base offsets of the screen */
    int     nCharPos;                       /* Not sure */
    int     toprow;                         /* Not used */
            
    int     xChar, yChar;                   /* size of the font */
                                            /*** keep */
    HGLOBAL hCpy2Clip;                      /* handle to what we last put on the clip */
    int     nhCpy2Clip;                                            
    HGLOBAL hPasteFmClip;                     /* handle to what we last read from the clip */
    int     nPasteFmClip;
                                                
    int     iLineEntry;                     /* buffer line to enter (wrap point)*/
    int     nTotalLines;                    /* total lines entered */
    int     MaxChar;                        /* number of allocated characters */
    POINT   BufNChar;                       /* number of allocated characters (x,y) */
    RECT    BufRect;                        /* same but in matrix char^2 form */
    POINT   ScrOffset;                      /* Offset of the screen */
    POINT   ScrPixels;                      /* a raw screen size in pixels*/
    POINT   ScrNChar;                       /* screen size in characters */
    RECT    ScrRect;                        /* same but in matrix char^2 form */
    POINT   Margins;                        /* display margins */
    POINT   BufCursor;                      /* cursor for buffet entry from the host*/
    POINT   MarkCursor;                     /* cursor where the mark is */
    POINT   PointCursor;                    /* cursor where our point is */

    CHAR    Screen[MAXCOLS * MAXROWS];      /*** change this to a dynamic pointer */

}T_TTYInfoStruct;

extern struct TTYInfoStruct TTYInfo;

//
// macros ( for easier readability )
//
#define COMDEV( x )         (x.hCommPort)       /* ? not used ? */
#define PORT( x )           (x.bPort)           /* com port */
#define BYTESIZE( x )       (x.bByteSize)       /* uart character size */
#define PARITY( x )         (x.bParity)         /* uart parity */
#define STOPBITS( x )       (x.bStopBits)       /* stop bits */
#define BAUDRATE( x )       (x.dwBaudRate)      /* bit rate */

#define DTRCONTROL( x )     (x.fDtrControl)
#define RTSCONTROL( x )     (x.fRtsControl)
#define XONCHAR( x )        (x.chXON)
#define XOFFCHAR( x )       (x.chXOFF)
#define XONLIMIT( x )       (x.wXONLimit)
#define XOFFLIMIT( x )      (x.wXOFFLimit)
#define CTSOUTFLOW( x )     (x.fCTSOutFlow)
#define DSROUTFLOW( x )     (x.fDSROutFlow)
#define DSRINFLOW( x )      (x.fDSRInFlow)
#define XONXOFFOUTFLOW( x ) (x.fXonXoffOutFlow)
#define XONXOFFINFLOW( x )  (x.fXonXoffInFlow)
#define TXAFTERXOFFSENT(x)  (x.fTXafterXoffSent)
#define NOREADING( x )      (x.fNoReading)
#define NOWRITING( x )      (x.fNoWriting)
#define NOEVENTS( x )       (x.fNoEvents)
#define NOSTATUS( x )       (x.fNoStatus)
#define SHOWTIMEOUTS( x )   (x.fDisplayTimeouts)

#define CURSORSTATE( x )    (x.wCursorState)
#define SCREEN( x )         (x.Screen)
#define HTTYFONT( x )       (x.hTTYFont)
#define LFTTYFONT( x )      (x.lfTTYFont)
#define FGCOLOR( x )        (x.rgbFGColor)

#define CONNECTED( x )      (x.fConnected)      /* connected flag */
#define TRANSFERRING( x )   (x.fTransferring)   /* Transferring flag */
#define REPEATING( x )      (x.fRepeating)      /* Repeating a Transfer flag */
#define LOCALECHO( x )      (x.fLocalEcho)      /* Local echo flag */
#define NEWLINE( x )        (x.fNewLine)        /* insert a <LF> after a <CR> flag */
#define AUTOWRAP( x )       (x.fAutowrap)       /* wrap at EOL */
#define XSIZE( x )          (x.xSize)           /* window hoizontal pixels */
#define YSIZE( x )          (x.ySize)           /* window vertical pixels */
#define XCHAR( x )          (x.xChar)           /* character hoizontal pixels */
#define YCHAR( x )          (x.yChar)           /* character vertical pixels */
#define XSCROLL( x )        (x.xScroll)         /* computed in WM_SIZE scroll amounts*/
#define YSCROLL( x )        (x.yScroll)
#define XOFFSET( x )        (x.xOffset)         /* scroll bar offsets */
#define YOFFSET( x )        (x.yOffset)
#define IENTRY ( x )        (x.ientry)
#define COLUMN( x )         (x.nColumn)         /* appears to be the cursor column */
#define ROW( x )            (x.nRow)            /* not quite sure about this guy yet */

#define DISPLAYERRORS( x )  (x.fDisplayErrors)
#define TIMEOUTSORIG( x )   (x.timeoutsorig)
#define TIMEOUTSNEW( x )    (x.timeoutsnew)
#define WRITERTHREAD( x )   (x.hWriter)
#define READSTATTHREAD( x ) (x.hReaderStatus)
#define EVENTFLAGS( x )     (x.dwEventFlags)
#define FLAGCHAR( x )       (x.chFlag)
#define TOPROW (x)          (x.toprow)


//---------------------------------------------------------------------------
//  End of File: ttyinfo.h
//---------------------------------------------------------------------------
