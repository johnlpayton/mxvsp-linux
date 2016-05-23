/*
********************************************************************
*   gmxvsp.c
*
*GTK+-2.0 try to build a program

*   This version builds a tabbed window and can send a message
*   to dopet that appears in VSP0
LKFILES = conexpr.o \
 vspterm.o \                    terminal
 vspmenu.o \                    menus
 muxpacket.o \                  packet handlers
 muxctl.o \                     primary rounter
 muxwrte.o \                    secondary router
 framer.o \                     framer
 miscutil.o \                   misc
 muxsock.o \                    socket stuff
 evbcmds.o \                    basic evb commands
 init.o \                       command line and ini file
 ./Minini/minIni.o \            ini file source
 memlockGTK.o                   lock
*
*
*
* History
* 26-dec-2012   jlp  mxvspl09
*   -Minor tweaks to muxwrte fixed download problem
*   -tweaked VSP cursor to be smaller
*   -fussing with download GUI. Real pain when the dialog
*       is clossed but the download is still in progress.
*       How to tell if a widget or window is destroyed?
*   -When trying to update the progressbar from the callback,
*       There are crashes.  Havent found a trail but the
*       widget pointer is sometimes not correct. Sometimes it
*       is, sometimes not.
*   -Rewrote the argument passing. Seems to fixed the problem
*       possily using a pointer to the wrong thing. It was a mess.
*       still is, but less of a mess.
*   -Got copy from a VSP window to work.
*
* 26-dec-2012   jlp  mxvspl08
*   -Switched muxctl to be a thread.  Packets are sent to his
*   input queue and he runs in a loop processing all queue entries
*   then waits when the queue is empty.  It looks like it is
*   mostly working but need to verify.  He does callback processing
*   when sending packets so the data flow is a little contorted. Need to
*   check through this and see which thread is executing what.
*   -Download is still not really working.  Need to flush it out.
*   will save this and work in a new version
*
* 25-dec-2012   jlp  mxvspL08
*   In process of adding download.  This is a big change
*   Need to clean vmxRegisterWindow and associated.  The code is hacked
*   together and a bit fragile.  Will continue finishing download first.
*
* 25-dec-2012   jlp  mxvspL08
*   In process of adding download.  This is a big change
* *  P R O B L E M   G T K see muxsock.c::jdowctlpktloop
*
*   In Gtk, we use callbacks instead of threads.  Loopback during
*   download puts us into a repeated callback loop until all the
*   lines in the file are sent.  The problem will remain until we
*   learn how to put the socket into it's own thread and use semaphores
*   or other signaling to break the callback loop.  For the present, we'll'\
*   only do small download tests in loopback

* 21-dec-2012   jlp  mxvspL08
*   Fixed highlight of entry after enter
*
* 20-dec-2012   jlp  mxvspL07
*   Big step. Got a enter routine(s) to operate in a combo box style
*       like W32.  This is tough in gtk.  There is a lot of delving
*       into parent stuff (classes?) to access what should be simple
*       like reading elements of a combo box.  The main complexity is
*       this tree list crap which is overly too complicated.  But
*       it generally works now.  An oddity is that you have to hit the
*       up-arrow twice to get to the history.  Not too annoying.
*   Added error dialog to miscutil.c
*   Added a few error checks to socket stuff (simple ones)
*
* 20-dec-2012   jlp mxvspL07
*   Started doc
*   Basic functionality works with dopey
*   windows Tabbed paradigm for the dispkay
*   8 x VSP + UnFramed are live VT102 (xterm) windows
*   Edit is a terminal but not connected
*   Entry is a dialog at the bottom (ala W32) w/o memory
*   up/down load not implemented
*   minimal error checking, mostly printf
*   mucsock/muxctl stripped down to client only
*
*
**********************************************************************
*/

// Contains the necessary headers and definitions for this program
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <vte/vte.h>
#include <gdk/gdkkeysyms.h>
#include <glade/glade.h>

#include "memlock.h"
#include "muxctl.h"
#include "framer.h"
#include "muxsock.h"
#include "vspterm.h"
#include "vdownload.h"
#include "vspmenu.h"
#include "muxw2ctl.h"
#include "w32defs.h"

//#define GDK_KEY_Return 0xff0d

//======== Test of interface structure
#define JLPNEWENTRY 1

/* GTK globals */
GtkWidget*          pMainWindow;                 // Main window
GtkWidget*          vMainWinBox;
GtkWidget*          ScreenWidget;

GtkWidget*          pNotebook;                   // Notebook for the VSPs
GtkWidget*          pEntryBox;                   // text entry area
GtkEntry*           pEntryText;                  // H O W  T O  U S E  ?
GtkEntryBuffer*     UserTextEntry;               // entry text

GtkWidget*          menubar;                     // MenuBar for the window
t_hMenuBar          hMenubar;                    // GtkWidget pointers

t_Dnload            sDNload;                     // Download structure
GtkWidget*          pDlBox;                      // Download dialod
GtkWidget*          pUpBox;                      // Download dialod

GtkListStore*       UserTextList;                // For text entry
static GtkAccelGroup* AccelMap = NULL;          // Our accellerators

GdkScreen*  TheGScreen=NULL;

char    iniPath[256];

//******** My Terminal testing stuff
extern      GtkWidget*  newvspterm(void);
#define     VEDIT_TAB 0
#define     VUNFR_TAB 1

t_windowmap TTYWindows[MAXTOTALWINS];
int ActiveTab = 1;
int iTimer500;

//****** Test of TCP support

GSocketConnection   TheSock;
GSocketAddress      AddrDopey;
GSocketClient         jdopey_client;

GInetAddress        j_GInetAddress;
GSocketAddress      j_GSocketAddress;
GSocketConnection   j_GSocketConnection;

GInetAddress*        pj_GInetAddress;
GSocketAddress*      pj_GSocketAddress;
GSocketConnection*   pj_GSocketConnection;
GTcpConnection*     pj_GTcpConnection;

GSocketClient*      pjdopey_client;

static GtkWidget* makeTabbedWindow(void);                   // Build the main display
static GtkWidget* makeEntryBox(void);
static gboolean fTimer500(gpointer pData);
extern void TryDopey(void);
extern void FatalError(char* s);
extern int InitMuxCtl(pJfunction packetToLocal,pJfunction msgToLocalToMain);
extern void doTimerForAll(void);
gboolean EntryDoneEvent (GtkWidget *widget, GdkEventButton *event);


/*
**************************************************************************
*
*  Function: static void dumpptr(U32* pU32, int n)
*
*  Purpose:  Duno an array
*
*  Comments:
*   Debug - move to miscutil
*
**************************************************************************
*/
static void dumpptr(U32* pU32, int n)
{
    int k;
    printf("0x%08x\n",(U32)pU32);
    for(k=0;k<n;k++)
    printf("    0x%08x\n",*pU32++);
}
/*
**************************************************************************
*
*  Function: static gboolean configure_event (GtkWidget *widget, GdkEventConfigure *event)
*
*  Purpose:  Build a new pixmap and clear it
*
*  Comments:
*   This is called from the system when a screen size change happens.
*   We are going to assume the "widget" is the primary one (whatever that means)
*   and grab parameters for pater use.
*
*
**************************************************************************
*/
#if 1
static gboolean configure_event (GtkWidget *widget, GdkEventConfigure *event)
{
    int     nexpand;
    int     nfill;
    int     npadding;
    GtkPackType npack_type;

#if 0
//    printf("jlp configure_event %d\n",++jlpcnt);
    printf("configure_event: widget     0x%08x event   0x%08x\n",(U32)widget,(U32)event);
    //printf("               : TheGDevice 0x%08x pixmap  0x%08x\n",(U32)TheGDevice,(U32)pixmap);
#endif

    gtk_box_query_child_packing(GTK_BOX (vMainWinBox),pNotebook,
        &nexpand,&nfill,&npadding,&npack_type);
#if 0
    printf("gtk_box_query_child_packing: %d %d %d %d\n",nexpand,nfill,npadding,npack_type);
#endif
#if 0
    nexpand = 1;
    nfill = 1;
    gtk_box_set_child_packing(GTK_BOX (vMainWinBox),pNotebook,
        nexpand,nfill,npadding,npack_type);
#endif
    return FALSE;
}
#endif

#if 1
/*
**************************************************************************
*
*  Function: static gboolean expose_event (GtkWidget *widget, GdkEventExpose *event)
*
*  Purpose: This is the WM_PAINT event
*
*  Comments:
*
*
*
**************************************************************************
*/
static gboolean expose_event (GtkWidget *widget, GdkEventExpose *event)
{

#if 0
//    printf("jlp configure_event %d\n",++jlpcnt);
    printf("expose_event: widget     0x%08x event   0x%08x\n",(U32)widget,(U32)event);
    printf("            : TheGDevice 0x%08x pixmap  0x%08x\n",(U32)TheGDevice,(U32)pixmap);

    gdk_draw_drawable (widget->window,
                     widget->style->fg_gc[gtk_widget_get_state (widget)],
                     pixmap,
                     event->area.x, event->area.y,
                     event->area.x, event->area.y,
                     event->area.width, event->area.height);
#endif
    printf("expose pMainWindow == 0x%08x widget = 0x%08x\n",(U32)pMainWindow,(U32)widget);
//    gtk_widget_show_all(pMainWindow);
//    gtk_widget_show_all(pMyVsp);
//    gtk_widget_show_all(pMyVsp2);
//    gtk_notebook_set_show_tabs (GTK_NOTEBOOK (pNotebook),TRUE);

//    gtk_widget_show_all (widget);

  return FALSE;
}
#endif

#if 1
/*
 *
**************************************************************************
*
*  Function: static gboolean button_press_event (GtkWidget *widget, GdkEventButton *event)
*
*  Purpose: Capture the mouse posisition
*
*  Comments:
*   In GTK, we get a raw event. Some simple test check for the
*   left button press
*
*
**************************************************************************
*/
gboolean button_press_event (GtkWidget *widget, GdkEventButton *event)
{
    if( (event->type == GDK_BUTTON_PRESS) && (event->button == 1))
    {                                                       // is our button
//        mouseX = GetX((int)(event->x));                     // capture translated coordinates
//        mouseY = GetY((int)(event->y));
//        UserMain(_argc,_argv,CM_G_MOUSED);                  // call user main

//        vte_terminal_feed(VTE_TERMINAL(TabWidgets[0]),"Hello\n",6);
        return TRUE;
    }
    else
        return FALSE;                                       // Other button, pass it along
}
#endif

#if 1

#if 1
/*
 *
**************************************************************************
*
*  Function: gboolean NoteB_ChPg (GtkNotebook* pBook, gpointer pPage, gint nPage, gpointer pData)
*
*  Purpose: Catch a notbook event
*
*  Arguments:
*       pBook:      Pointer to the book widget
*       pPage:      Pointer to the page widget (VSP)
*       nPage:      Index of the page starting at 0
*       pData:      User data pointer
*
*  Comments:
*
**************************************************************************
*/
gboolean NoteB_ChPg (GtkNotebook* pBook, gpointer pPage, gint nPage, gpointer pData)
{
#if EASYWINDEBUG && 0
    printf("pBook 0x%08x pPage 0x%08x nPage %d pData 0x%08x\n",(U32)pBook,(U32)pPage,nPage,(U32)pData);
    printf("wHnd 0x%08x wrText 0x%08x wTab %d wType %d eTid %d\n",
        (U32)TTYWindows[nPage].wHnd,
        (U32)TTYWindows[nPage].wrText,
        TTYWindows[nPage].wTab,
        TTYWindows[nPage].wType,
        TTYWindows[nPage].eTid);

#endif
    if(nPage > 0)                                // Edit window not allowed
        ActiveTab = nPage;

//    printf("Active tab %d\n",ActiveTab);
    return TRUE;
}
#endif

/*
**************************************************************************
*
*  Function: void PacketToVSP(void* dev, t_ctlreqpkt* qpkIn, int B)
*
*  Purpose: Rerout packet to muxwrte
*
*  Arguments:
*
*  Comments:
*
**************************************************************************
*/
void PacketToVSP(void* dev, t_ctlreqpkt* qpkIn, int B)
{
#if EASYWINDEBUG && 0
printf("PacketToVSP: WmuxActive= %d\n",WmuxActive);
#endif
//    DoWCtlReq(NULL, 0, qpkIn);
    if(WmuxActive)
    {
        ForwardCtlMail(&WmuxInMail,qpkIn);
    }
    else
    {
        ReleaseCtlPacket(qpkIn,&gRouterCS);
    }

}
/*
**************************************************************************
*
*  Function: static gboolean fTime500(gpointer pData)
*
*  Purpose: 500 ms timer
*
*  Arguments:
*
*  Comments:
*
**************************************************************************
*/
static gboolean fTimer500(gpointer pData)
{
    doTimerForAll();
    return TRUE;
}

/*
**************************************************************************
*
*  Function: void MsgToMain(void* dev, int A, int B)
*
*  Purpose: Rerout packet to muxwrte
*
*  Arguments:
*
*  Comments:
*
**************************************************************************
*/
void MsgToMain(void* dev, int A, int B)
{
}
/*
**************************************************************************
*
*  Function: int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
*                              LPSTR lpCmdLine, int nCmdShow);
*
*  Purpose: Main Entry point from the launch
*
*  Comments:
* cproto.c 8-dec-2012
*   Here we are goint to try to add a page with a VSP.  The structure
*   will be a global array
*
* cproto05.c 8-dec-2012
*   Can now detect and capture when a page is selected.  The catcher
*   gets a pointer to something, a page number, and user data in addition to
*   the pNoteBook, se code below for more
*
* cproto04.c 8-dec-2012
*   Got the entry box to emit a signal and call a parser box.
*   It does nothing except print text on VSP0. But this is
*   a larger step. GTK enits the "activate" signal on most
*   widgets when <CR> is hit (I think)
*
* cproto03.c 8-dec-2012
*   Added an "GtkEntry" text box with a button.  The graphic
*   is not correct (but adequate) and there is no signal or
*   catcher.  Also not sure how to add <CR> to get a signal. That's
*   pretty important.. But lets move on in small steps on the GUI
*
* cproto02.c 8-dec-2012
*   Attempt to use a tab container built on cproto01
*       struct GtkNotebook;
*   This sets up a tabbed screen with two VSPs and a menu bar
*
* cproto01.c 8-dec-2012
* This is the starting version.
*   Single window
*   Menu bar
*
*
**************************************************************************
*/

int main(int argc, char** argv)
{
    char c;
    int i;
    int k;

    GladeXML *xml;


//======== Clear out memory

//======== Main GTK initialization
    gdk_threads_init();
    gtk_init (&argc, &argv);
    g_type_init();
    TheGScreen = gdk_screen_get_default();

//======== Parse our commands to get initialzation
    getcwd(iniPath,256-12);
    strcat(iniPath,"/mxvsp.ini");
//printf("ini file <%s>\n",iniPath);
    DoMSVPIni(iniPath,1);
    QueueCommandLine(argc, argv);

//    InitDownload(&sDNload);

//======== make a Primary parent window
    pMainWindow = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_name (pMainWindow, "gmxVSP");
    gtk_window_set_resizable (GTK_WINDOW (pMainWindow), TRUE);           // Can be resized

    //======== make the menubar (by hand, GUI later)
    menubar = makevspmenubar(pMainWindow, &hMenubar);

    //======== get a box container and add it
//    vMainWinBox = gtk_vbox_new (FALSE, 0);
    vMainWinBox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_set_border_width (GTK_CONTAINER (vMainWinBox), 1);
    gtk_container_add (GTK_CONTAINER (pMainWindow), vMainWinBox);

    //======== pack the menu at the top
    gtk_box_pack_start (GTK_BOX (vMainWinBox), menubar, FALSE, TRUE, 0);

    //======== Create a tabbed notebook
    pNotebook = makeTabbedWindow();

    //======== pack into the container after the menu
    gtk_box_pack_start (GTK_BOX (vMainWinBox), pNotebook, FALSE, TRUE, 0);

    //======== Let this grow when resizing
    gtk_box_set_child_packing(GTK_BOX (vMainWinBox),pNotebook,
        1,1,0,0);

    //======== Create a text area (later change to WIN32 combo box style)
    pEntryBox = makeEntryBox();

    //======== Pack this one at the end
    gtk_box_pack_end (GTK_BOX (vMainWinBox), pEntryBox, FALSE, TRUE, 0);


//======== Connect events
    // main gets a destroy
    g_signal_connect (pMainWindow, "destroy",
                    G_CALLBACK (gtk_main_quit), NULL);
    // Attempt to resize
    g_signal_connect (pMainWindow, "configure-event", G_CALLBACK (configure_event), NULL);


#if 0
    // TabWidgets[VUNFR_TAB] gets a mouse-down
    g_signal_connect (TabWidgets[VUNFR_TAB], "button-press-event",
                    G_CALLBACK (button_press_event), NULL);
#endif


//****** Attach our accellatators
//    gtk_window_add_accel_group((GtkWindow*)pMainWindow,AccelMap);

#if EASYWINDEBUG && 0
    // load the interface
    xml = glade_xml_new("Spin1.glade", NULL, NULL);

    // connect the signals in the interface
    glade_xml_signal_autoconnect(xml);

{
     GtkDialog * dialog1;
     int k;

    dialog1 = (GtkDialog *)glade_xml_get_widget(xml, "dialog1");
    printf("dialog1 =0x%x\n",(U32)dialog1);
    k=gtk_dialog_run (GTK_DIALOG (dialog1));
    printf("k =0x%x\n",(U32)k);
}
#endif

//======== Create the Download dialog box

//    pUpBox = makeDlDialog(GTK_WINDOW(pMainWindow), &sUPload, "Get Xmodem");
//    sUPload.who = XFRTYPEXMODEM;
//    gtk_widget_show_all(pUpBox);


//======== Test Windows message box
#if EASYWINDEBUG && 0
{
    int k;
    k = MessageBox(
        NULL,
        "error",
        "title",
        MB_ABORTRETRYIGNORE+MB_ICONASTERISK);
    printf("Message bow returns %d\n",k);
}
#endif

//******** Show our screen now
    InitWrte();
    InitMuxCtl((pJfunction)PacketToVSP,(pJfunction)MsgToMain);
//    new_tcpdev(&RemHost, (pJfunction) MsgToMain);


//    sleep(2);
    //======== This has to happen after all routing and sockets are set up
    // moved to muxctl.c
//    RemHost.cmd(&RemHost,(APARAM)sockCmdInitConnectSock,(BPARAM)0);

    gtk_widget_show_all(pMainWindow);

//======== Some special debug
#if (EASYWINDEBUG>5) && 1
{
}
#endif


//******** Greet the neighbors

{
    int klog;

    printf("Hello World\n");
    for(i=0;i<argc;i++)
    {
        printf("<%s>",argv[i]);
    }
    printf("\n");

}

//******

#if EASYWINDEBUG & 0    // Test message box
    i=(int)MessageBox(NULL,"Include Run at the end","Text of nessagebix",MB_ABORTRETRYIGNORE+MB_ICONEXCLAMATION);
    printf("i=%d\n",i);
#endif
    iTimer500 = g_timeout_add(500,fTimer500,NULL);

//****** Pass program control on to GTK!
    gtk_main ();
}

//======================================================================
// Supporting functions for testing that need to ve moved
//
// Menu handlers
//
//======================================================================
/*
**************************************************************************
*  FUNCTION: gboolean MenuF12(gpointer pData, GtkWidget* pWidget)
*
*  PURPOSE: Handler for the F12 menu
*
*  COMMENTS:
*
**************************************************************************
*/
gboolean MenuF12(gpointer pData, GtkWidget* pWidget)
{
    int k;

    printf("MenuF12 %s 0x%08x\n", (char*)pData, (U32)pWidget);


    return TRUE;
}

/*
**************************************************************************
*  FUNCTION: void FatalError(char* s)
*
*  PURPOSE: Handler for the F12 menu
*
*  COMMENTS:
*
**************************************************************************
*/
void FatalError(char* s)
{
    int k;

    printf("%s\n", s);
    exit(4);

}

/*
 *
**************************************************************************
*
*  Function: gboolean EVBCmdEventQuit (GtkWidget *widget, GdkEventButton *event)
*
*  Purpose: More graceful quit
*
*  Comments:
*
*
**************************************************************************
*/
gboolean EVBCmdEventQuit (GtkWidget *widget, GdkEventButton *event)
{
    //======== shut down TCP

    //======== save our ini file
    DoMSVPIni(iniPath,0);

    //========
    gtk_main_quit();
}


/*
**************************************************************************
*  FUNCTION: static GtkWidget* makeTabbedWindow(void)
*
*  PURPOSE: Build the main display window
*
*  COMMENTS:
*   We choose a tabbed window of nVSP+2 tabs as a style
*   The first tab is the edit window
*   The second tab is the UnFramed VSP
*   Tabs[2..9] are VSP0..VSP7
*
* For initial simplicity, we'll build all VSPs
*
**************************************************************************
*/
static GtkWidget* makeTabbedWindow(void)
{
    GtkWidget*  pBook;
    GtkWidget*  pW;
    int k,n,m;
    char tmptxt[16];

    //======== make a notebook
    pBook = gtk_notebook_new();

    //======== This will be the edit eindow later
    pW = newvspterm();
    vmxRegisterWindow(0, 1, NULL, pW, pW);
    n = LookUpWinByTID(1);

    k = gtk_notebook_append_page (GTK_NOTEBOOK (pBook),TTYWindows[n].wHnd,NULL);
    gtk_notebook_set_tab_label_text(GTK_NOTEBOOK (pBook),TTYWindows[n].wHnd,"Edit");

    //======== Unframed window is first
    pW = newvspterm();
    vmxRegisterWindow(0, TID_UNFRAMED, VspAction, pW, pW);
    n = LookUpWinByTID(TID_UNFRAMED);
    k = gtk_notebook_append_page (GTK_NOTEBOOK (pBook),TTYWindows[n].wHnd,NULL);
    gtk_notebook_set_tab_label_text(GTK_NOTEBOOK (pBook),TTYWindows[n].wHnd,"UnFr");

    //======== 8  VSPs follow
    for(k = DEVVSP0; k <= DEVVSP7; k += 1)
    {
        m = k-DEVVSP0;
        sprintf(tmptxt,"VSP%1d",m);
        pW = newvspterm();
        vmxRegisterWindow(0, k, VspAction, pW, pW);
        n = LookUpWinByTID(k);
        gtk_notebook_append_page (GTK_NOTEBOOK (pBook),TTYWindows[n].wHnd,NULL);
        gtk_notebook_set_tab_label_text(GTK_NOTEBOOK (pBook),TTYWindows[n].wHnd,tmptxt);
    }

    //======== show the tab bar
    gtk_notebook_set_show_tabs (GTK_NOTEBOOK (pBook),TRUE);

    //======== reported workaround to change pages
    gtk_widget_show_all(pBook);

    //======== start on unframed page VUNFR_TAB
    gtk_notebook_set_current_page(GTK_NOTEBOOK (pBook),VUNFR_TAB+1);

    //======== enable book signals NoteB_ChPg
    g_signal_connect ((pBook),
            "switch-page",
            G_CALLBACK(NoteB_ChPg),
            NULL);


    return pBook;
}

#if !JLPNEWENTRY
/*
**************************************************************************
*  FUNCTION: static GtkWidget* makeEntryBox(void)
*
*  PURPOSE: Build an elementary entry box
*
*  COMMENTS:
*   This is a sibgle line with no memory.  Later we want to expand
*   it with the LMRU popup style used in the W32 VSP
*
*   This builds sume globals right now. Later we need to clean it up
*
**************************************************************************
*/
static GtkWidget* makeEntryBox(void)
{
    GtkWidget* pE;

    UserTextEntry =  gtk_entry_buffer_new(NULL,256);
    pE = gtk_entry_new_with_buffer(UserTextEntry);
    gtk_entry_buffer_set_text (UserTextEntry,"Hello2",6);
    // Add an icon we can use for the <CR> key
    gtk_entry_set_icon_from_stock((GtkEntry*)pE,
        GTK_ENTRY_ICON_PRIMARY,GTK_STOCK_OK); // actually a check mark!!
    gtk_entry_set_icon_activatable((GtkEntry*)pE,
        GTK_ENTRY_ICON_PRIMARY,TRUE);

    gtk_entry_set_icon_from_stock((GtkEntry*)pE,
        GTK_ENTRY_ICON_SECONDARY,GTK_STOCK_GO_UP); // actually a check mark!!
    gtk_entry_set_icon_activatable((GtkEntry*)pE,
        GTK_ENTRY_ICON_SECONDARY,TRUE);

    // Enable signals to connect to the entry parser
#if 0
    gtk_signal_connect (GTK_OBJECT(pE),
            "activate",
            GTK_SIGNAL_FUNC(EntryDoneEvent),
            NULL);
    gtk_signal_connect (GTK_OBJECT(pE),
            "icon-release",
            GTK_SIGNAL_FUNC(EntryDoneEvent),
            NULL);
#else
    g_signal_connect ((pE),
            "activate",
            G_CALLBACK(EntryDoneEvent),
            NULL);
    g_signal_connect ((pE),
            "icon-release",
            G_CALLBACK(EntryDoneEvent),
            NULL);
#endif
    return pE;
}

/*
 *
**************************************************************************
*
*  Function: gboolean EntryDoneEvent (GtkWidget *widget, GdkEventButton *event)
*
*  Purpose: Try to get text from the buffer
*
*  Comments:
*   Step 1: Feed a string to vsp0 -- done
*   Step 2: Feed string from the entry buffer -- done
*   Step 3: Feed <CR><LF> -- done
*   Step 4: Find active vsp and feed a string
*       We actually will feed the mux thread but we need the tty
*       in the long run
*
**************************************************************************
*/
gboolean EntryDoneEvent (GtkWidget *widget, GdkEventButton *event)
{
    char    tmptxt[128];
    t_ctldatapkt* pPayload;
    char*   pChar;
    int     n,m;

    //======== Get the data and length
    pChar = (char*)gtk_entry_buffer_get_text(UserTextEntry);
    n = gtk_entry_buffer_get_length(UserTextEntry);

    //======== limit the size
    if (n<0) n=0;
    if(n>125) n=125;
    strncpy(tmptxt,pChar,n);

    //======== terminate with <CR> (later add escape codes)
    tmptxt[n++] = '\r';
    tmptxt[n] = 0;

    //======== packetize
    pPayload = ReframeData(tmptxt, n, TTYWindows[ActiveTab].eTid, SEQNUM_DEFAULT);
    if(!pPayload)
        FatalError("EntryDoneEvent:AllocDataPacket fail 1");

#if EASYWINDEBUG & 0
printf("EntryDoneEvent 0x%08x ",pPayload->pData);
#endif

    //======== send to the router
    SendFrameToRouter(pPayload,ROUTESRCLOCAL+0x500);

    //======== Highlight text for next time
    gtk_editable_select_region(GTK_EDITABLE(GTK_ENTRY(pEntryBox)),0,-1);

    return TRUE;
}
#endif

#if JLPNEWENTRY
/*
**************************************************************************
*  FUNCTION: static GtkWidget* makeEntryBox(void)
*
*  PURPOSE: Build a combo box with an entry
*
*  COMMENTS:
*   First just see if we can get a window
*   Ok, it looks the same but we cant enter text
*   By extensive experiment, we can chain down to an
*       entry box. No we need to see what we need to send in the
*       signal so we can get both the active text and chain thru
*       the history for memory ala our W32 dialog
*
*
**************************************************************************
*/
static GtkWidget* makeEntryBox(void)
{
    GtkWidget*      pE;
    GtkTreeIter     iter;
    GtkComboBox*    pComboBox;
    GtkComboBoxText* pComboBoxText;
    GtkEntry*       pEntryB;

    //======== Create a combo box with text entry
    pE = gtk_combo_box_text_new_with_entry();
    pComboBoxText = GTK_COMBO_BOX_TEXT(pE);

    //======== enter some lines for debug
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(pE),"Text0");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(pE),"Text1");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(pE),"Text2");

    //======== dereference to the parent
    pComboBox = &(pComboBoxText->parent_instance);
//    printf("makeEntryBox: pComboBox 0x%08x\n",(U32)pComboBox);

    //======== try to get the entry box
    pEntryB = (GtkEntry*)gtk_bin_get_child((GtkBin*)pComboBox);
//    printf("makeEntryBox: pEntryB 0x%08x\n",(U32)pEntryB);

    //======== Set an Icon
    gtk_entry_set_icon_from_stock(GTK_ENTRY(pEntryB),GTK_ENTRY_ICON_PRIMARY,"gtk-ok");
    gtk_entry_set_icon_activatable(GTK_ENTRY(pEntryB),GTK_ENTRY_ICON_PRIMARY,TRUE);


    //======== Enable signals to connect to the entry parser
#if 0
    gtk_signal_connect(GTK_OBJECT(pEntryB),        // NOTE the widget
            "activate",
            GTK_SIGNAL_FUNC(EntryDoneEvent),
            NULL);
    gtk_signal_connect(GTK_OBJECT(pEntryB),
            "icon-release",                         // this icon is actually a check mark!!
            GTK_SIGNAL_FUNC(EntryDoneEvent),
            NULL);
#else
    g_signal_connect_swapped((pEntryB),        // NOTE the widget
            "activate",
            G_CALLBACK(EntryDoneEvent),
            pComboBox);
    g_signal_connect_swapped((pEntryB),
            "icon-release",                         // this icon is actually a check mark!!
            G_CALLBACK(EntryDoneEvent),
            pComboBox);
#endif
    return pE;
}

/*
 *
**************************************************************************
*
*  Function: gboolean EntryDoneEvent (GtkWidget *widget, GdkEventButton *event)
*
*  Purpose: Try to get text from the buffer
*
*  Comments:
*   Step 1: Feed a string to vsp0 -- done
*   Step 2: Feed string from the entry buffer -- done
*   Step 3: Feed <CR><LF> -- done
*   Step 4: Find active vsp and feed a string
*       We actually will feed the mux thread but we need the tty
*       in the long run
*
**************************************************************************
*/
static gboolean PruneComboBox (GtkComboBox *pCo, char *pCh);
static int ParseEnterLine(char* dst, char* src, int maxchar);

gboolean EntryDoneEvent (GtkWidget *widget, GdkEventButton *event)
{
    GtkWidget*      pE;
    GtkTreeIter     iter;
    GtkComboBox*    pComboBox;
    GtkComboBoxText* pComboBoxText;
    GtkEntry*       pEntryB;
    gboolean        bDoInsert;
    char    tmptxt[128];
    t_ctldatapkt* pPayload;
    char*   pChar;
    int     k,n,m;

    //======== If this correct? Yea if we do connect_swapped and send combo box
#if 1
    pE = widget;
    pComboBox = GTK_COMBO_BOX(pE);
//    printf("EntryDoneEvent: pComboBox 0x%08x\n",(U32)pComboBox);

    //======== try to get the entry box
    pEntryB = (GtkEntry*)gtk_bin_get_child((GtkBin*)pComboBox);
//    printf("EntryDoneEvent: pEntryB 0x%08x\n",(U32)pEntryB);
#else
    //======== This used if signal sends the entry box instead
    pEntryB = (GtkEntry*)widget;
#endif

//======== down here, we want to remove any entries that are the same as
//  the edit area. And save new text in the history.  We migh also disable
//  recording an empty line


//gtk_entry_get_text (GTK_ENTRY (gtk_bin_get_child (GTK_BIN (combobox))).
    //======== Get the data and length
    pChar = (char*)gtk_entry_get_text(GTK_ENTRY(pEntryB));
    n = gtk_entry_get_text_length(GTK_ENTRY(pEntryB));
    ParseEnterLine(tmptxt,pChar,125);

#if EASYWINDEBUG && 0
printf("ParseEnterLine <%s>\n",pChar);
#endif

    //======== Remember the text
    if(n > 0)
    {
        bDoInsert = PruneComboBox(pComboBox,tmptxt);
        if(bDoInsert)
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(pComboBox),tmptxt);
    }

    //======== terminate with <CR>
    switch(inicfg.lineend)
    {
        case UART_LINEEND_CR:
        {
            tmptxt[n++] = '\r';
            tmptxt[n] = 0;
        }
        break;

        case UART_LINEEND_LF:
        {
            tmptxt[n++] = '\n';
            tmptxt[n] = 0;
        }
        break;

        case UART_LINEEND_CRLF:
        {
            tmptxt[n++] = '\r';
            tmptxt[n++] = '\n';
            tmptxt[n] = 0;
        }
        break;

        default:
            tmptxt[n] = 0;
        break;
    }



    //======== packetize
    pPayload = ReframeData(tmptxt, n, TTYWindows[ActiveTab].eTid, SEQNUM_DEFAULT);
    if(!pPayload)
        FatalError("EntryDoneEvent:AllocDataPacket fail 1");

#if EASYWINDEBUG & 0
printf("EntryDoneEvent 0x%08x ",pPayload->pData);
#endif

    //======== send to the router
    SendFrameToRouter(pPayload,ROUTESRCLOCAL+0x500);

    //======== Highlight text for next time
    gtk_editable_select_region(GTK_EDITABLE(GTK_ENTRY(pEntryB)),0,-1);

    return TRUE;
}
#endif


#if 0 // copied and using for cut/paste area
        enum{
          STRING_COLUMN,
          INT_COLUMN,
          N_COLUMNS
        };
        {
          GtkTreeModel *list_store;
          GtkTreeIter iter;
          gboolean valid;
          gint row_count = 0;
          /* make a new list_store */
          list_store = gtk_list_store_new (N_COLUMNS, G_TYPE_STRING, G_TYPE_INT);
          /* Fill the list store with data */
          populate_model (list_store);
          /* Get the first iter in the list */
          valid = gtk_tree_model_get_iter_first (list_store, &iter);
          while (valid)
            {
              /* Walk through the list, reading each row */
              gchar *str_data;
              gint   int_data;
              /* Make sure you terminate calls to gtk_tree_model_get()
               * with a '-1' value
               */
              gtk_tree_model_get (list_store, &iter,
                                  STRING_COLUMN, &str_data,
                                  INT_COLUMN, &int_data,
                                  -1);

        gboolean            gtk_tree_store_remove               (GtkTreeStore *tree_store,
                                                                 GtkTreeIter *iter);
        gboolean            gtk_list_store_remove               (GtkListStore *list_store,
                                                                 GtkTreeIter *iter);

              /* Do something with the data */
              g_print ("Row %d: (%s,%d)\n", row_count, str_data, int_data);
              g_free (str_data);
              row_count ++;
              valid = gtk_tree_model_iter_next (list_store, &iter);
            }
        }
#endif



static gboolean PruneComboBox (GtkComboBox *pCo, char *pCh)
{
    GtkWidget*      pE;
    GtkComboBox*    pComboBox;
    GtkComboBoxText* pComboBoxText;
    GtkEntry*       pEntryB;

    char    tmptxt[128];
    char*   pChar;
    int     k,n,m;

    GtkTreeModel *list_store;
    GtkTreeIter iter;
    gboolean valid;
    gint row_count = 0;

    list_store = gtk_combo_box_get_model(pCo);
    valid = gtk_tree_model_get_iter_first (list_store, &iter);
    k = 0;
    while (valid)
    {
        gtk_tree_model_get (list_store, &iter,
            0, &pChar,-1);

//        g_print ("Row %d: <%s>\n", k, pChar);
        if(strcmp(pChar,pCh) == 0)
        {
            gtk_list_store_remove(GTK_LIST_STORE(list_store), &iter);
            g_free (pChar);
            return TRUE;
        }
        k ++;
        valid = gtk_tree_model_iter_next (list_store, &iter);
    }
    return TRUE;
}

/*-----------------------------------------------------------------------------

FUNCTION: static int ParseEnterLine(char* dst, char* src, int maxchar)

PURPOSE: Parse the input from the user and send it

PARAMETERS:
    src     Source
    dst     destination
    maxchar limit

Returns
    Number in the line


Comments

    This works pretty well there are some "features" that should be added
    Oh, by the way \n works

    \n  <LF>
    \r  <CR>
    \e  <ESC>
    \\  <baskslash>
    \letter sends the control bits (0x1f&letter)


HISTORY:   Date:      Author:     Comment:
           12/12/12   jlp      Converted for gtk
           03/12/12   jlp      Big simplification to paste.. let W32 do the work
           01/17/10   jlp      Wrote it

-----------------------------------------------------------------------------*/
static int ParseEnterLine(char* dst, char* src, int maxchar)
{
    int     k,ksrc,kdst;
    char*   tmpSrc;
    char*   tmpDst;

    ksrc = 0;
    kdst = 0;
    while( src[ksrc] && (kdst < maxchar))
    {
        if( (0x7f&src[ksrc]) != 0x5c)         // check for escape
        {
            dst[kdst++] = 0x7f&src[ksrc++]; // no, just copy
        }
        else                                    // yep, do some common escape codes
        {
            ksrc +=1;
#if EASYWINDEBUG & 0
printf("ParseEnterLine: esc-%c\n",src[ksrc]);
#endif
            switch (src[ksrc])
            {
                case 0x5c: // backslash to baskslash>
                    dst[kdst++] = 10;
                    ksrc += 1;
                break;

                case 'n': // n is mapped to <LF>
                    dst[kdst++] = 10;
                    ksrc += 1;
                break;

                case 'r': // r is mapped to <CR>
                    dst[kdst++] = 13;
                    ksrc += 1;
                break;

                case 'b': // b is mapped to <BS>
                    dst[kdst++] = 8;
                    ksrc += 1;
                break;

                case 'e': // e is mapped to <ESC>
                    dst[kdst++] = 0x1b;
                    ksrc += 1;
                break;

                default:        // map to a control char
                    dst[kdst++] = src[ksrc++] & 0x1f;
                break;
            }
        }
    }

    dst[kdst] = 0;                       // terminate

    return(kdst);
}


#endif


// End of file cmdlg.c
