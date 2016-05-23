/*
**************************************************************************
*
*  FILE: vspterm.c
*
*  PURPOSE: terminal functions
*
*  Comments:
*
*   GtkWidget*  newvspterm(void)
*
**************************************************************************
*/
#include "jtypes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if USEGDK
#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
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
//#include "muxvttyW.h"
//#include "mttty.h"
#include "muxctl.h"
#include "framer.h"
//#include "muxevb.h"
#include "muxsock.h"
//#include "MTTTY.H"
#include "muxw2ctl.h"
#include "vte/vte.h"


static const GdkColor terminal_palette[] = {
    { 0, 0x0000, 0x0000, 0x0000 },
    { 0, 0xcdcb, 0x0000, 0x0000 },
    { 0, 0x0000, 0xcdcb, 0x0000 },
    { 0, 0xcdcb, 0xcdcb, 0x0000 },
    { 0, 0x1e1a, 0x908f, 0xffff },
    { 0, 0xcdcb, 0x0000, 0xcdcb },
    { 0, 0x0000, 0xcdcb, 0xcdcb },
    { 0, 0xe5e2, 0xe5e2, 0xe5e2 },
    { 0, 0x4ccc, 0x4ccc, 0x4ccc },
    { 0, 0xffff, 0x0000, 0x0000 },
    { 0, 0x0000, 0xffff, 0x0000 },
    { 0, 0xffff, 0xffff, 0x0000 },
    { 0, 0x4645, 0x8281, 0xb4ae },
    { 0, 0xffff, 0x0000, 0xffff },
    { 0, 0x0000, 0xffff, 0xffff },
    { 0, 0xffff, 0xffff, 0xffff }
};
static GdkColor    colorfg={ 0, 0x0000, 0x0000, 0x0000 };
static GdkColor    colorbg={ 0, 0xffff, 0xffff, 0xffff };

/*
**************************************************************************
*
*  FUNCTION: GtkWidget*  newvspterm(void)
*
*  PURPOSE: Create a terminal window and set up some defaults
*
*  COMMENTS:
*
*
**************************************************************************
*/
GtkWidget*  newvspterm(void)
{
//******** Build a VSP
    GtkWidget*  pMyVsp;

    pMyVsp=vte_terminal_new();                  // new VSP (returns a GtkWidget, need a macro to dereferenc)
//    printf("pMyVsp = 0x%x\n",(U32)pMyVsp);

    vte_terminal_set_size(                      // set initial (col,line) can change with mouse
        VTE_TERMINAL(pMyVsp),80,25);
    vte_terminal_reset(                         // reset the terminal
        VTE_TERMINAL(pMyVsp),1,1);
    vte_terminal_set_scrollback_lines(          // reduce scroll history
        VTE_TERMINAL(pMyVsp),192);
    vte_terminal_set_colors(                    // set to black on white
        VTE_TERMINAL(pMyVsp),&colorfg,&colorbg,terminal_palette,16);
    vte_terminal_set_cursor_blink_mode(
        VTE_TERMINAL(pMyVsp),VTE_CURSOR_BLINK_OFF);
    vte_terminal_set_cursor_shape(
        VTE_TERMINAL(pMyVsp),VTE_CURSOR_SHAPE_UNDERLINE);

//    printf("%s\n",vte_terminal_get_default_emulation (VTE_TERMINAL(pMyVsp)));
    return(pMyVsp);
}

/*
 * Actually, gdk_flush() is more expensive than is necessary here,
 * since it waits for the X server to finish outstanding commands as well;
 *  if performance is an issue, you may want to call XFlush() directly:


#include <gdk/gdkx.h>

void my_flush_commands (void)
{
  GdkDisplay *display = gdk_display_get_default ();
  XFlush (GDK_DISPLAY_XDISPLAY (display);
}
* === gdk_flush crashes

*/
static int TextToVsp(void* v, void* txt, int ntxt)
{
    GtkWidget* pV;
    char* pChar;

    if(!v)
        return 0;
    pV = (GtkWidget*)v;
    pChar = (char*)txt;

    vte_terminal_feed(VTE_TERMINAL(v),pChar,ntxt);
//    gdk_flush(); // intermittent crashes
/* does not compile
    {
      GdkDisplay *display = gdk_display_get_default ();
      XFlush (GDK_DISPLAY_XDISPLAY (display);
    }
*/
    return 1;

}

static int VspToCopyBuf(void* v, void* txt, int ntxt)
{
    t_windowmap*    pWin;
    GtkWidget*      pWid;

    pWin = (t_windowmap*)v;
    if(!pWin)
        return(-1);
    pWid = (GtkWidget*)(pWin->wHnd);
    if(!pWid)
        return(-1);

    vte_terminal_copy_clipboard(VTE_TERMINAL(pWid));
    return 1;
}

int VspAction(struct S_windowmap* pMapEntry, int cmd, long arg1, long arg2)
{
    int retval = 0;
    int k;

#if EASYWINDEBUG & 0
if(inicfg.prflags & PRFLG_VSP || 0)
{
if( (cmd != W2CMD_T500) )
printf("VspAction:: cmd %d arg1 0x%08x arg2 0x%08x\n",
    cmd, arg1, arg2);
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
//            SendMessage((HWND)(pMapEntry->wHnd),VSP_SETTEXT,(WPARAM)(arg2),(LPARAM)arg1);
            vte_terminal_feed(VTE_TERMINAL(pMapEntry->wHnd),
                (char*)arg1,
                (int)arg2);
        }
        break;

        case W2CMD_CUT:
        {
        }
        break;

        case W2CMD_COPY:
        {
            if (vte_terminal_get_has_selection(pMapEntry->wHnd))
                vte_terminal_copy_clipboard(pMapEntry->wHnd);
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
