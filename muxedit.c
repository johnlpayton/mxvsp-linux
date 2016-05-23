/*******************************************************************************
*   FILE: muxedit.c
*
*   Contains edit edit window and a lot of MDI stuff
*
*   Externals
*
*
*******************************************************************************/

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
#include <richedit.h>
#endif

#include "memlock.h"
#include "muxvttyW.h"
#include "mttty.h"
#include "muxctl.h"
#include "framer.h"
#include "muxevb.h"
#include "muxsock.h"
#include "MTTTY.H"

extern HFONT ghFontVTTY;
extern int boldflag;
HANDLE hRichEdit;

/*---------------------------------------------------------------------------
*  BOOL muxedit(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
*
*  Description:
*   Editor window child process
*
*  Parameters:
*     HWND hWnd
*        handle to TTY window (as always)
*
*  Comments
*   This is the companion editor window. Common to all TTY's it provides the
*   cut/paste/highlight capability
*
*
*  History:   Date       Author      Comment
*  24-Feb-2012 jlp
*   initial pass
*
*---------------------------------------------------------------------------*/
BOOL CALLBACK _export muxedit(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{

    BOOL    retval=TRUE;
    DWORD   wStyle;
    RECT    ScrRect;
    HDC     hdc,oldDC;
    HFONT   oldfont;
    int     k,m,n;
    char*   pChar;
    char    tmpTxt[32];

#if EASYWINDEBUG & 0
TraceWinMessages("muxedit",msg,wParam,lParam);
#endif

    switch  ( msg & 0xffff )
    {
        //==============
        case WM_CREATE:         // Initialize: open our richedit window
        //==============
        {
            // create the edit control
            wStyle=(  WS_CHILD
                    | WS_VISIBLE
                    | WS_BORDER
                    | WS_VSCROLL
                    | WS_HSCROLL
                    | ES_WANTRETURN
                    | ES_AUTOHSCROLL
                    | ES_AUTOVSCROLL
                    | ES_MULTILINE
        //            | ES_READONLY               // for now... Later we might change
                );

        	hRichEdit = CreateWindowEx(
        	    0,
        		"RichEdit",
        		"",     	              // Text for window title bar.
        		wStyle,
                0,0,100,100,              // not really important WM_SIZE will change this
        		hWnd,                     // Parent
        		(HMENU)42,                // Id for Notify
        		ghInst,     			  // This instance owns this window.
        		NULL			   	      // Pointer not needed.
        	);

            if(!hRichEdit)
                return(1);

            SendMessage(hRichEdit,WM_SETFONT,(WPARAM)ghFontVTTY,(LPARAM)1); // Set the font
            SendMessage(hRichEdit,EM_EXLIMITTEXT,(WPARAM)0,(WPARAM)(128*1024)); // Set to 128kb
            CheckDlgButton(hStartDialog,IDC_STUPContent,BST_CHECKED);

            retval=0;
        }
        break;

        //==============
        case WM_CLOSE:  // REVISIT: Close a Vxtty window
        //==============
        break;

        //==============
        case WM_DESTROY:
        //==============
        break;

        //==============
        case WM_SIZE: // somebody change our size
        //==============
        {
            RECT ScrRect;
            int dx,dy;
            int sx,sy;

            GetClientRect(hWnd,&ScrRect);       // Our visible rectangle

            dx=ScrRect.right-ScrRect.left;      // place edit in whole content
            dy=ScrRect.bottom-ScrRect.top;
            sx=0;
            sy=0;
            MoveWindow(hRichEdit,
                sx,sy,
                dx,dy,
                TRUE);
#if JLPMIDI
            retval = DefMDIChildProc(hWnd, msg, wParam, lParam);
            return(retval);
#endif
        }
        break;

        //==============
        case WM_SETFONT: // new font compute character sizes and redraw
        //==============
        {
            SendMessage(hRichEdit,WM_SETFONT,wParam,lParam); // Set the font
        }
        break;


#if 1
        // Send VSP TID_UNFRAMED?
        //==============
        case WM_MDIACTIVATE:       // we're becomming active/inactive
        //==============
            if(lParam  == (LPARAM)hWnd)
            {
                retval = 0;
            }
            else
            {
                retval = 1;
            }
        break;
#endif
        //==============
        case WM_PAINT:          // Redraw the window
        //==============
        {
            PAINTSTRUCT  ps ;
            hdc = BeginPaint( hWnd, &ps ) ;
            EndPaint( hWnd, &ps ) ;
        }
        break;
        
        //==============
        case VSPEDIT_LOADTEXT:
        //==============
        {
            SendMessage(hRichEdit,WM_SETTEXT,wParam,lParam); // sent to the editor
        }
        break;


        //========
        case WM_COMMAND:
        //========
        switch(LOWORD(wParam))
        {

            //========
            case CM_TOGBOLD:
            //========
            {
                CHARFORMAT TheCFormat ={0};
                TheCFormat.cbSize=sizeof(CHARFORMAT);
                SendMessage(hRichEdit,EM_GETCHARFORMAT,(WPARAM)1,(LPARAM)&TheCFormat);

                TheCFormat.dwMask = CFM_BOLD+CFM_ITALIC+CFM_UNDERLINE ;
                k = boldflag & (~TheCFormat.dwEffects);         // Toggle bits
                TheCFormat.dwEffects = k;
                SendMessage(hRichEdit,EM_SETCHARFORMAT,(WPARAM)SCF_SELECTION,(LPARAM)&TheCFormat);
            }
            break;

            //========
            case CM_CUT:    // Cut from the menu
            //========
            {
                SendMessage(hRichEdit,WM_CUT,(WPARAM)0,(LPARAM)0);
            }
            break;

            //========
            case CM_COPY:    // Copy from the menu
            //========
            {
                SendMessage(hRichEdit,WM_COPY,(WPARAM)0,(LPARAM)0);
            }
            break;

            //========
            case CM_PASTE:    // Paste from the menu
            //========
            {
                SendMessage(hRichEdit,WM_PASTE,(WPARAM)0,(LPARAM)0);
            }
            break;

            //========
            case CM_SELLINE:    // select the current line
            //========
            {
                CHARRANGE RangeCh;
                k=SendMessage(hRichEdit,EM_LINEFROMCHAR,-1,0);  // get line number of the current line

                RangeCh.cpMin=SendMessage(hRichEdit,EM_LINEINDEX,k,0);
                RangeCh.cpMax=SendMessage(hRichEdit,EM_LINEINDEX,k+1,0)-1;
                SendMessage(hRichEdit,EM_EXSETSEL,0,(LPARAM)&RangeCh); // set the selection
            }
            break;

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
    return (retval);

}
