
/*-----------------------------------------------------------------------------

    This is a part of the Microsoft Source Code Samples. 
    Copyright (C) 1995 Microsoft Corporation.
    All rights reserved. 
    This source code is only intended as a supplement to 
    Microsoft Development Tools and/or WinHelp documentation.
    See these sources for detailed information regarding the 
    Microsoft samples programs.

    MODULE: Error.c

    PURPOSE: Implement error handling functions 
             called to report errors.

    FUNCTIONS:
        ErrorExtender - Calls FormatMessage to translate error code to
                        error text
        ErrorReporter - Reports errors to user
        ErrorHandler  - Reports errors, then exits the process
        ErrorInComm   - Reports errors, closes comm connection, then exits

-----------------------------------------------------------------------------*/

#include "jtypes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
//#include "mttty.h"
#include "muxctl.h"
#include "framer.h"
#include "muxevb.h"
#include "muxsock.h"
#include "MTTTY.H"

extern int MessageLastError(char *title);
/*
    Prototypes of functions called only in this module
*/

/*-----------------------------------------------------------------------------

FUNCTION: ErrorReporter(char *)

PURPOSE: Report error to user

PARAMETERS:
    szMessage - Error message from app

COMMENTS: Reports error string in console and in debugger

HISTORY:   Date:      Author:     Comment:
           10/27/95   AllenD      Wrote it

-----------------------------------------------------------------------------*/
void ErrorReporter(char * szMessage)
{
    MessageLastError(szMessage);
}


/*-----------------------------------------------------------------------------

FUNCTION: ErrorHandler( char * )

PURPOSE: Handle a fatal error (before comm port is opened)

PARAMETERS:
    szMessage - Error message from app

HISTORY:   Date:      Author:     Comment:
           10/27/95   AllenD      Wrote it

-----------------------------------------------------------------------------*/
void ErrorHandler(char * szMessage)
{	
    ErrorReporter(szMessage);
    ExitProcess(0);
}


/*-----------------------------------------------------------------------------

FUNCTION: ErrorInComm( char * )

PURPOSE: Handle a fatal error after comm port is opened

PARAMETERS:
    szMessage - Error message from app

HISTORY:   Date:      Author:     Comment:
           10/27/95   AllenD      Wrote it

-----------------------------------------------------------------------------*/
void ErrorInComm(char * szMessage)
{
    ErrorReporter(szMessage);
    BreakDownCommPort();
    ExitProcess(0);
}
