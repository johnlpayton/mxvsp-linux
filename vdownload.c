/*******************************************************************************
*   FILE: download.c
*
*   This is the modifications for Gtk operation. The fundemental
*   design of the GUI is different.  In here, the Download (Upload)
*   operation is focused upon the data structure as the basic
*   control.  The dialogs are merely GUI access to the data
*   structure.  For example, a download can be started then the GUI
*   dialog destroyed while it is still running.  In WIN32 we did
*   could hide the dialog and not destroy it.  Under Gtk, I cant
*   fide how to do this. So each GUI has to operate when opened
*   while a transfer (possibly the other direction) is active.
*
*   A thing that needs to be resolved is what happens when a status
*   (eg progress bar) widget is destroyed during transfer. How can
*   we detect that and adapt?
*
*
* Older comments:
*
*                   GUI-----------
*                    ^           |
*                    |           |
*                  Stat          |
*                    ^           |
*                    |           |
*                    |-<Engine<---
*                       |       ^
*                       V       |
*                    Framer   wCtlTTY
*
*   The engine (DlAction) gets packets by registering as VSP
*   eTid = TID_UNFRAMED+WANTACK (0x18+0x80).  When it sends,
*   the topbit (WANTACK) gets stripped out before entering
*   the EVB. The EVB recognizes it as UNFRAMED data and strips
*   both the framing and the WANTACK bit off and sends it as raw
*   data.  Thus we preserve compatability with MACSBUG.  When
*   kernel is running, UNFRAMED data is sent to it so we keep
*   compatability for the download between kernel and MACSBUG.
*   The WANTACK bit is used for flow control.  It never enters
*   the board.  The EVB VSP will check the bit after the Win32
*   comport signals transfer complete (OVERLAPPED I/O) and will
*   send a packet back that has the VSP channel (0x98) and the
*   sequence number.  The sending PC waits for an ACK before sending
*   a new packet.  This keeps the data flow restricted so to
*   not clog network or buffers.  The wait time is reasonably small
*   so throughput stays between 50% and 80% on wireless channels
*
*   The underlying issue here is that there are two controlling
*   operators.  The GUI is the user interaction that selects the file,
*   starts/aborts the process.  The TTYWindow is the engine that runs
*   from the secondary router and does the actual transfer.  The old
*   design had the engine running in an adhoc mixture where control
*   boundries were not clear. In this design, we will treat the GUI
*   as an I/O element.  It contains the following elements
*       File Button: Select a file using a canned GUI
*       Send/Abort Button  Start or Abort
*       Quit Button
*       CPS text (display)
*       Bytes text (display)
*       Time text (display)
*       Progress Bar (graphical)
*   The buttons are under control of the user (inputs), the three text
*   elements and the progress bar are set by the TTYWindow engine
*
*   The Win32 paradigm of a generalized call/command/arg has been useful
*   and we will continue to use it.The main difference when comparing
*   Win32 and Gtk is predominate in the dialog GUI and how the
*   engine comminicates.  Gtk is pretty sloppy.  Not surprising, it
*   was thrown together. We had to build a structure with handles to
*   graphic elements.  In Win32 we used SetDlgItem calls to do
*   the same thing.  So lts make a common control call.  This works
*   for all elements except the retry timeout of the upload.  Lets
*   leave that to the user right now and purely concentrate on commands
*   from the GUI and status messages from the engine.  In the control
*   structure follow Gtk and place a callback.  Standard Win32 Message
*   format
*
*   static int doDLStatusMsg(void* v, int idMsg, long arg1, long arg2)

*
*OLD COMMENTS
*   Contains the source for sending S-Record files to the EVB
*   Later, it will be modified to include XMODEM binary transfer
*   so we can upload data from the EVB.
*
*   The main problem with sending S-Record files is that it
*   is a straight ASCII dump.  There is no handshake to
*   slow down the source somputer. mxvsp will implement flow control
*   with an ACK scheme.  Routing in the mxvsp environment is
*   done on a packet basis. The packets have the format
*       <DLE><STX><ADDR><SEQ>....<DLE><ETX>
*   Between the <DLE><STX> and <DLE><ETX>  byte stuffing is done for
*   transparancy. To simplify some coding effort, we constrain the
*   two bytes <ADDR> and <SEQ> to the range [0x20..0x7e].  In our
*   implementation, we will use a subset of the range.
*
*   This constraint allows the VSP's to use the device numbers [46..54]ew other
*   as the address in the EVB. It also allows a sequence counter of
*   [0x20..0x2f] for download acks (actually only 1 bit is needed)
*
*   The flow control works like this.  In the address field (<ADDR>) if
*   bit 7 is set, the packet will be acked. The ack will have the same
*   <ADDR><SEQ> values as were set. In addition, the first two bytes
*   if the data field are a copy of <ADDR><SEQ>.  So, the downloader
*   Sends a packet to the EVB with the ack bit set.  When the EVB driver
*   sends the data to the EVB it will respond to that address.  The downloader
*   will not send a new line of S_Record data (packet) until it gets
*   the ack.
*
*   A few other issues are to be worked out. The involve the secondary
*   window based router. In the main router, there are 3 "ports"
*       1) EVB
*       2) TCP
*       3) Local (the secondary router)
*
*   Normally, when a packet arrives to the Local, it will look up to see if
*   there is a window open to service the packet. If not, it opens a new window
*   mvsp terminal.  We'll suppress that for packets with the ack bit set.
*   Instead, the downloader (in this file) must register a window with the secondary
*   router which forwards packets instead of calling the usual deframer.
*
*   Externals
*
*
*******************************************************************************/

#include "jtypes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
//#include "muxvttyW.h"
//#include "mttty.h"
#include "muxctl.h"
#include "framer.h"
//#include "muxevb.h"
//#include "muxsock.h"
//#include "MTTTY.H"
#include "vdownload.h"
#include "muxw2ctl.h"
#include "w32defs.h"

#if USEWIN32
#include "mttty.h"
#include "resource.h"
#endif

extern void* pMainWindow;
extern t_Dnload     sDNload;                     // Download structure

//static t_dlparam gDLparam;  // start with a static structure

static void doDLStatusMsg(HWND v, int idMsg, long arg1, long arg2);
static int DlAction(void* pMapEntry, int cmd, long arg1, long arg2);
static int vspsendnext(HWND hwndDlg, t_dlparam* pParam);
static int DlgDlSrecProc(t_Dnload* hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static void jlpprhwnd(char*title, HWND h)
{
    if(!h)
    {
        printf("\n<NULL>,%s\n");
    }
    else
    {
//        printf("\n%08X, %s\n",(U32)(*(U32*)h),title);
        printf("\n%08X, %08X, %s\n",(U32)(*(U32*)h),(U32)sDNload.tag,title);
    }
}
//=========================================================================
//
//====================== E N G I N E S
//
//=========================================================================

/*
**************************************************************************
*
*  Function: static int vspopendl(HWND hwndDlg, t_dlparam* pParam)
*
*  Purpose: Open elements for a download
*
*   Arguments:
*       HWND        hwndDlg     window for Message box
*       t_dlparam*  pParam      pointer to the download structure
*
*  Comments:
*   This will open the file and register the download
*   channel. If returns -1 on an error and also
*   tosses up message box
*
*
**************************************************************************
*/
#if 1
static int vspopendl(HWND hWnd, t_dlparam* pParam)
{

    int     k;
    HWND    hControl;

    if(pParam->fin)
    {
        fclose(pParam->fin);
        pParam->fin = NULL;
    }

    pParam->fin = fopen(pParam->fname,"rb");
    if(pParam->fin == 0)
    {
//        MessageBox(hWnd,"cant fopen","vspopendl",MB_OK+MB_ICONWARNING);
printf("can't fopen ");
printf("%s\n",pParam->fname);
        return(-3);
    }

    pParam->seqnum = 0;

    //======== GUI stuff

    // set the start time
    pParam->starttime = time(NULL);
    pParam->bytessent = 0;

    // get the file size
    fseek(pParam->fin,0,2);
    pParam->fSize = ftell(pParam->fin); // careful, it could be 0
    fseek(pParam->fin,0,0);
//printf("vspopendl: pParam->fin 0x%08x pParam->fSize 0x%08x\n",pParam->fin,pParam->fSize);
    clearerr(pParam->fin);
    return 1;
}
#endif

#if 1
static int vspclosedl(HWND hWnd, t_dlparam* pParam)
    {
    int     k;
    HWND    hControl;

//printf("vspclosedl: pParam->fin 0x%08x pParam->dladdr 0x%08x\n",pParam->fin,pParam->dladdr);
    if(pParam->wID > 0)
    {
        vmxUnRegisterWindow(pParam->dladdr);
        pParam->wID = 0;
    }
    if(pParam->fin)
    {
        fclose(pParam->fin);
        pParam->fin = NULL;
    }
    pParam->state = ST_IDLE;

    return 1;
}

#endif

/*
**************************************************************************
*
*  Function: static int vspsendnext(HWND hwndDlg, t_dlparam* pParam)
*
*  Purpose: Read a new buffer, build a frame and send it
*
*   Arguments:
*       HWND        hwndDlg     window to give to the message box (Dialog)
*       t_dlparam*  pParam      pointer to the download data structure
*
*  Comments:
*   This is tha main download engins.  It only has one state
*   It reads a file and sends the data read
*
*
**************************************************************************
*/
static int vspsendnext(HWND hWnd, t_dlparam* pParam)
{
    int     retval = 1;
    int     k;
    int     cps;
    time_t  dTime;
    char tmptxt[12];

    //======== Development check for null file
    if(!(pParam->fin))
    {
        FatalError("vspdlnewpacket: attempt to read NULL file");
        retval = -1;
        return retval;
    }

    //======== If done or errored, return
#if (EASYWINDEBUG>5) && 1
if( (inicfg.prflags & PRFLG_OTHER) || 0)
{
printf("vspsendnext pParam->state 0x%08x pParam->fin 0x%08x\n",
    pParam->state,(U32)(pParam->fin));
}
#endif
    if( (pParam->state & ST_STATUSBITS) != ST_ACTIVE)
        return(retval);

    //======== Read the hext chunk
#if 1// Read fixed chunks
    k = pParam->nbuf = fread(pParam->buf, 1, pParam->pktsize, pParam->fin);
#else // read lines (slower)
    k = pParam->nbuf = freadLn(pParam->buf,pParam->pktsize, pParam->fin);
    if(k > 0)
    {
        pParam->buf[k-1] = '\r';
        pParam->buf[k] = '\n';
        pParam->buf[k+1] = 0;
        pParam->nbuf = k+1;
    }
#endif
    //======== check for eof
    if(k <= 0)
    {
//printf("%d %d %d\n",k,feof(pParam->fin),ferror(pParam->fin));
        pParam->state = (pParam->state & ~ST_STATUSBITS) | ST_DONE;
        doDLStatusMsg(hWnd, UDL_MSGPBAR, 1, 100);
        doDLStatusMsg(hWnd, W2MSG_DONE, 0, 0);
        return retval;
    }

    //======== SeTd new oacket with updates sequence number
    pParam->seqnum = (pParam->seqnum + 1) & 0xf;    // mod 16
    SendFramed(pParam->buf,pParam->nbuf,ROUTESRCLOCAL+0x100,pParam->dladdr, pParam->seqnum + 0x20);
    pParam->bytessent += pParam->nbuf;
#if (EASYWINDEBUG>5) && 1
if( (inicfg.prflags & PRFLG_VSP) || 0)
{
jlpDumpFrame("vspsendnext",pParam->buf,pParam->nbuf);
}
#endif


    return retval;

}


/*
**************************************************************************
* ====================== M I D D L E   C O N T R O L
*
* These operate as the action function engines.  They are called
* From w2Ctl for
*
*   1) W2CMD_INIT
*   2) W2CMD_DESTROY
*   3) W2CMD_TEXT
*   4) W2CMD_T500
*
* From menus
*   5) W2CMD_CUT
*   6) W2CMD_COPY
*   7) W2CMD_PASTE
* =========================================================================
**************************************************************************
*/

/*
**************************************************************************
*
*  Function: static int DlAction(void* wHnd, void** pData, int cmd, long arg1, long arg2)
*
*  Purpose: W2 action engine
*
*   Arguments:
*
*  Comments:
*
*   Called windows style, this routine is the engine for a download.
*   The primary call is msg == VSP_NEWPACKET.  This message is sent
*   from the EVB as an ACK. The routine should increment the
*   sequence counter and send a new packet. For now, we'll do
*   binary reads of 256 characters. Later we might adjust the
*   sequence.
*
*
*
**************************************************************************
*/
static int DlAction(void* v, int cmd, long arg1, long arg2)
{
    int retval = 0;
    int k;
    struct S_windowmap* pMapEntry = (struct S_windowmap*)v;
    t_Dnload* pDlg = (t_Dnload*)(pMapEntry->pData);
    t_dlparam* pParam = &(pDlg->sp);

#if EASYWINDEBUG & 1
if(inicfg.prflags & PRFLG_VSP || 0)
{
if(cmd != W2CMD_T500)
printf("DlAction:: *pData 0x%08x cmd %d arg1 0x%08x arg2 0x%08x\n",
    (U32)v, cmd, arg1, arg2);
printf("DlAction:Dialog 0x%08x pParam 0x%08x\n",(U32)pDlg,(U32)pParam);
if(cmd != W2CMD_INIT)
    jlpprhwnd("DlAction",pDlg);
}
#endif


    switch(cmd)
    {
        //========
        case W2CMD_INIT:
        //========
        {
            // set the working data pointer
            pMapEntry->pData = (void*)(&sDNload);

            // reload pointers
            pDlg = (t_Dnload*)(pMapEntry->pData);
            pParam = (t_dlparam*)&(pDlg->sp);

            pParam->dladdr = pMapEntry->eTid;    // Use this!!
//            pParam->dladdr = TID_SRECORD;
            pParam->seqnum = 0;
            pParam->state = ST_DOWNLOAD;
            pParam->pktsize = 128;
            if(pParam->pktsize > MAXDATASIZE)
                pParam->pktsize = MAXDATASIZE-6;

            //======== GUI stuff

            // set the start time
            pParam->starttime = time(NULL);
            pParam->bytessent = 0;

            retval = 1;

        }
        break;

        //========
        case W2CMD_DESTROY:
        //========
        {
        }
        break;

        //========
        case W2CMD_TEXT:
        //========
        {
            char tmptxt[64];
            int ksec,kmin,cps;

#if (EASYWINDEBUG>5) && 1
if( (inicfg.prflags & PRFLG_OTHER) || 0)
{
t_dlparam* pP = (t_dlparam*)pMapEntry->pData;
char* pChar = (char*)arg1;
printf("DlAction:W2CMD_TEXT 0x%02x 0x%02x Sseq 0x%02x state 0x%x\n",
    0xff&pChar[0],0xff&pChar[1],0xff&pP->seqnum,pP->state );
}
#endif
            vspsendnext(pMapEntry->hWndCB,pParam);

            //======== bump the progress bar
            k = (100*pParam->bytessent) / pParam->fSize;
            doDLStatusMsg(pMapEntry->hWndCB, UDL_MSGPBAR, 1, k);

            //======== Calculate CPS
            k = time(NULL) - pParam->starttime;
            cps = 0;
            if(k > 0)
            {

                // Characters per second
                cps = pParam->bytessent / k;
                sprintf(tmptxt,"%d",cps);
                doDLStatusMsg(pMapEntry->hWndCB, UDL_LABEL2, 1, (long)tmptxt);
            }
            //======== elapsed seconds
            if(cps > 0)
            {
                int ksec,kmin;

                // remaining seconds
                k = (pParam->fSize - pParam->bytessent)/cps;
                kmin = k/60;
                ksec = k%60;
                sprintf(tmptxt,"%d:%02d",kmin,ksec);
                doDLStatusMsg(pMapEntry->hWndCB, UDL_LABEL4, 1, (long)tmptxt);
            }
        }
        break;

        //========
        case W2CMD_CUT:
        //========
        {
        }
        break;

        //========
        case W2CMD_COPY:
        //========
        {
        }
        break;

        //========
        case W2CMD_PASTE:
        //========
        {
        }
        break;

        //========
        case W2CMD_T500:
        //========
        {
        }
        break;

        //========
        default:
        //========
        {
        }
        break;
    }
    return retval;
}

//=========================================================================
//
//====================== D I A L O G S
//
//=========================================================================

/*
**************************************************************************
*
*  Function: int DlgDlSrecProc(t_Dnload* hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
*
*  Purpose: Gtk Dialog bog
*
*  Comments:
*   Basically we act just like the WIN32 dialog handler but turn
*   the messages into the GTK calls.  Our Paradigm for handling
*   a dialog is to store the widget handlers into the main structure
*   then translate the id's.  There may be a better method of doing
*   this but for now...
*
*   HARDWIRED TO VSP8 FOR NOW
*
*   The GUI is a dialog box. When asked to start, it opens the file
*   and registers the engine.
*   hWnd is a (void*) to the dialog structure. We cast it to
*       hwndDlg = (t_Dnload*)hWnd
*   for use.
*
**************************************************************************
*/
#if USEGDK
static char EndDLString[] = "S70500020000F8\r";

static int DlgDlSrecProc(t_Dnload* hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    BOOL retval=TRUE;
    int i,k;
    int ks,kd;
    BOOL result;
    t_Dnload* hwndDlg = (t_Dnload*)hWnd;
    t_dlparam* pParam = &(hwndDlg->sp);
    GtkButton* pButton;
    GtkLabel*  pLabel;
    GtkWidget *fOpendialog;

#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_VSP || 0)
{
jlpprhwnd("DlgDlSrecProc",(void*)hWnd);
printf("DlgDlSrecProc 0x%08x msg=0x%08x(0x%08x,0x%08x)\n",(U32)hWnd,(U32)msg,(U32)wParam,(U32)lParam);
}
#endif

    switch( msg & 0xffff )
    {
        //========
        case WM_INITDIALOG:     // Do nothing in Gtk
        //========
            retval=FALSE;
        break;

        //========
        case W2MSG_DONE: // close from the action function
        //========
        {
            vspclosedl(NULL,pParam);         // close files
            pButton = GTK_BUTTON(hwndDlg->SendButton);
            gtk_button_set_label(pButton,"Start");
            pParam->state = ST_DOWNLOAD;
        }
        break;

        //========
        case WM_COMMAND:                                       // from our controls
        //========
//printf("DlgDlSrecProc:WM_COMMAND 0x%08x 0x%08x\n",(U32)wParam,(U32)lParam);
            switch((wParam))
            {
                //==================
                case 105:       //  Start/abort
                //==================
                {
                    int     kerr = 0;
                    char*   filename;

                    if(pParam->state & ST_STATUSBITS)
                    {
                        pButton = GTK_BUTTON(hwndDlg->SendButton);
                        vspclosedl(NULL,pParam);         // close files
                        gtk_button_set_label(pButton,"Start");
                        pParam->state = ST_DOWNLOAD;
                    }
                    else
                    {
                        filename = (char*)gtk_entry_get_text(
                                            GTK_ENTRY(hwndDlg->pCombo));
                        strcpy(pParam->fname,filename);
                        if(vspopendl(NULL,pParam) < 0)
                            kerr |= 1;

                        if(!kerr)
                        {
                           pParam->wID = vmxRegisterWindow(
                                            0,               // not used`
                                            TID_SRECORD,     // vsp port
                                            DlAction,        // action function
                                            hwndDlg,         // window for actions
                                            hwndDlg);        // window for status messages
                            if(pParam->wID < 0)
                            {
#if 0
                                MessageBox(
                                    hwndDlg,
                                    "cant vmxRegisterWindow",
                                    "vspopendl",
                                    MB_OK+MB_ICONASTERISK);
#endif
                                kerr |= 2;
                            }
                            strcpy(inicfg.DLfilename,filename);
                        }
                        // need to check kerr and clean up do by hand now
#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_VSP || 0)
{
printf("kerr %x wID %d pParam->dladdr %d\n",kerr,pParam->wID,pParam->dladdr);
}
#endif
                        if(kerr)
                        {
                            vspclosedl(hwndDlg,pParam);
                        }
                        else
                        {
                            doDLStatusMsg(
                                hwndDlg,
                                UDL_MSGPBAR,
                                1,
                                0);
                            doDLStatusMsg(
                                hwndDlg,
                                UDL_LABEL1,
                                1,
                                (long)"CPS");
                            doDLStatusMsg(
                                hwndDlg,
                                UDL_LABEL2,
                                1,
                                (long)"0");
                            doDLStatusMsg(
                                hwndDlg,
                                UDL_LABEL3,
                                1,
                                (long)"t Left");
                            doDLStatusMsg(
                                hwndDlg,
                                UDL_LABEL4,
                                1,
                                (long)"----");
                            pButton = GTK_BUTTON(hwndDlg->SendButton);
                            gtk_button_set_label(pButton,"Abort");
                            pParam->state = ST_DOWNLOAD+ST_ACTIVE;

                            // kick off first transfer
                            vspsendnext(hwndDlg, pParam);
                        }
                    }

#if EASYWINDEBUG & 0
printf("DlgDlSrecProc:case 105 k=%d <%s>\n",k,pParam->fname);
#endif
                }
                break;

                //==================
                case 106:       // New
                //==================
                {

                    GtkFileFilter *fpat;
                    GtkFileFilter *fpat1;
                    char*   filename;


                    fpat = gtk_file_filter_new ();
            //        gtk_file_filter_add_pattern (fpattern, "*.txt");
                    gtk_file_filter_set_name(
                        fpat,
                        "S Records");
                    gtk_file_filter_add_pattern (
                        fpat,
                        "*.[sS][123][789]");

                    fpat1 = gtk_file_filter_new ();
                    gtk_file_filter_set_name(
                        fpat1,
                        "All");
                    gtk_file_filter_add_pattern (
                        fpat1,
                        "*");

                    fOpendialog = gtk_file_chooser_dialog_new (
                                          "Open File",
                                          GTK_WINDOW(pMainWindow),
                                          GTK_FILE_CHOOSER_ACTION_OPEN,
                                          GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                          GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
                                          NULL);
                    gtk_file_chooser_set_local_only(GTK_FILE_CHOOSER(fOpendialog),
                        FALSE);
                    gtk_file_chooser_set_action(GTK_FILE_CHOOSER(fOpendialog),
                        GTK_FILE_CHOOSER_ACTION_OPEN);
                    gtk_file_chooser_set_show_hidden(GTK_FILE_CHOOSER(fOpendialog),
                        FALSE);
                    gtk_file_chooser_select_filename(GTK_FILE_CHOOSER(fOpendialog),
                        inicfg.DLfilename);
                    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fOpendialog),
                        fpat);
                    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fOpendialog),
                        fpat1);
                    filename = (char*)gtk_entry_get_text(
                                    GTK_ENTRY(hwndDlg->pCombo));
                    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(fOpendialog),
                            filename);

                    if (gtk_dialog_run (GTK_DIALOG (fOpendialog)) == GTK_RESPONSE_ACCEPT)
                    {
                        filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (fOpendialog));
//printf("File = <%s>\n",filename);
                        gtk_entry_set_text(GTK_ENTRY(hwndDlg->pCombo),filename);
                        g_free (filename);
                     }
                    gtk_widget_destroy (fOpendialog);

                }
                break;

               //==================
                case 201:      // Combo box
               //==================
#if EASYWINDEBUG && 0
printf("DlgDlSrecProc:case 201\n");
#endif
                break;

               //==================
                case 202:      // Progress bar
               //==================
               {
#if 1
                    GtkWidget*  pProgressbar;
                    pProgressbar = (hwndDlg->pProgressbar);
                    if(!pProgressbar)
                        break;
                    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(pProgressbar),
                        (double)lParam/100.0);
#endif
               }
               break;
               //==================
               case 1001:      // Label 1
               //==================
               {
                    pLabel = GTK_LABEL(hwndDlg->pLabel1);
                    gtk_label_set_label(pLabel,(char*)lParam);
               }
               break;

               //==================
               case 1002:      // Label 2
               //==================
               {
                    pLabel = GTK_LABEL(hwndDlg->pLabel2);
                    gtk_label_set_label(pLabel,(char*)lParam);
               }
               break;

               //==================
               case 1003:      // Label 3
               //==================
               {
                    pLabel = GTK_LABEL(hwndDlg->pLabel3);
                    gtk_label_set_label(pLabel,(char*)lParam);
               }
               break;

               //==================
               case 1004:      // Label 4
               //==================
               {
                    pLabel = GTK_LABEL(hwndDlg->pLabel4);
                    gtk_label_set_label(pLabel,(char*)lParam);
               }
               break;

               //==================
                default:
               //==================
                    retval=FALSE;
                break;
            }
        break;

        default:
            retval=FALSE;
        break;
    }
    return (retval);                  /* Didn't process a message    */

}
#endif

/*
**************************************************************************
*
*  FUNCTION: void DoDlDialogButton(GtkDialog* pDialog, gint response_id, gpointer user_data)
*
*  PURPOSE: Respone to the dialog buttons
*
*  COMMENTS:
*   Windows sends these as WM_COMMAND messages.  Here we intercept
*   them and turn them into WM_COMMAND messages then call the
*   equivalent of a windows dialog
*
**************************************************************************
*/

void DoDlDialogButton(GtkDialog* pDialog, gint response_id, gpointer user_data)
{
    t_Dnload* dlgCtl = (t_Dnload*)user_data;
    GtkButton* pButton;
    GtkWidget *fOpendialog;
    char    tmptxt[32];
    char    *filename;
    int     k;


    if(!dlgCtl)
    {
        return;
    }

    //========
    if(response_id == 2)            // File Buton
    //========
    {
        DlgDlSrecProc((HWND)dlgCtl, WM_COMMAND, 106, 0);
    }
    //========
    else if(response_id == 1)       // Start/Abort button
    //========
    {
        DlgDlSrecProc((HWND)dlgCtl, WM_COMMAND, 105, 0);
    }
    //========
    else if(response_id == 3)       // Quit button
    //========
    {
        DlgDlSrecProc((HWND)dlgCtl, W2MSG_DONE, 0, 0);
        gtk_widget_destroy(GTK_WIDGET(pDialog));
//        gtk_widget_hide(GTK_WIDGET(pDialog));
    }
}

/*
**************************************************************************
*
*  FUNCTION: GtkWidget* makeDlDialog(GtkWindow* win, t_Upload* dlgCtl)
*
*  PURPOSE: Make the Download dialog
*
*  COMMENTS:
*   Right now, we do this the hard way.  There should be some sort
*   of graphical program but Glade seems to crash
*
**************************************************************************
*/
static gboolean DoDlDialogClose (GtkWidget *widget, GdkEventButton *event)
{
    printf("DoDlDialogClose\n");
    return FALSE;
}

GtkWidget* makeDlDialog(GtkWindow* win, t_Dnload* dlgCtl, const char* name)
{
    GtkWidget*      pDialog;
    GtkWidget*      pContent_area;
    GtkWidget*      pAction_area;
    GtkWidget*      pProgressbar;
    GtkWidget*      pCombo;
    GtkWidget*      pHbox;
    GtkWidget*      pLabel1;
    GtkWidget*      pLabel2;
    GtkWidget*      pLabel3;
    GtkWidget*      pLabel4;


    /* Create the widgets */
    pDialog = gtk_dialog_new_with_buttons (name,
                win,
                GTK_DIALOG_DESTROY_WITH_PARENT,
                "Start",1,
                "File",2,
                "Quit",3,
                NULL);
    pContent_area = gtk_dialog_get_content_area(GTK_DIALOG (pDialog));
    pAction_area = gtk_dialog_get_action_area(GTK_DIALOG (pDialog));

    pProgressbar = gtk_progress_bar_new();
    pCombo = (GtkWidget*)gtk_entry_new();
    if(strlen(inicfg.DLfilename) > 0)
        gtk_entry_set_text(GTK_ENTRY(pCombo),
            inicfg.DLfilename);
    pHbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_set_homogeneous(GTK_BOX(pHbox),TRUE);
    pLabel1 = gtk_label_new("CPS");
    pLabel2 = gtk_label_new("Sec");
    pLabel3 = gtk_label_new("Bytes");
    pLabel4 = gtk_label_new("Bytes");

    /* Add the label, and show everything we've added to the dialog. */
    gtk_container_add(GTK_CONTAINER (pHbox),pLabel1);
    gtk_container_add(GTK_CONTAINER (pHbox),pLabel2);
    gtk_container_add(GTK_CONTAINER (pHbox),pLabel3);
    gtk_container_add(GTK_CONTAINER (pHbox),pLabel4);
    gtk_container_add (GTK_CONTAINER (pContent_area), pCombo);
    gtk_container_add (GTK_CONTAINER (pContent_area), pHbox);
    gtk_container_add (GTK_CONTAINER (pContent_area), pProgressbar);

    dlgCtl->tag = 0xDEADFACE;
    dlgCtl->Self = pDialog;
    dlgCtl->SendButton = gtk_dialog_get_widget_for_response(GTK_DIALOG(pDialog),1);
    dlgCtl->pProgressbar = pProgressbar;
    dlgCtl->pCombo = pCombo;
    dlgCtl->pLabel1 = pLabel1;
    dlgCtl->pLabel2 = pLabel2;
    dlgCtl->pLabel3 = pLabel3;
    dlgCtl->pLabel4 = pLabel4;

//printf("dialog: &sDNload=0x%08x dlgCtl=0x%08x\n",(U32)(&sDNload),(U32)dlgCtl);

#if 1
    /* Ensure that the dialog box is destroyed when the user hits the close. */
    g_signal_connect_swapped (pDialog,
                "destroy-event",
                G_CALLBACK (DoDlDialogClose),
                pDialog);

    g_signal_connect_swapped (pDialog,
                "close",
                G_CALLBACK (gtk_widget_destroy),
                pDialog);
#else
    g_signal_connect_swapped (pDialog,
                "close",
                G_CALLBACK (DoDlDialogClose),
                pDialog);
#endif
    //======== connect to the button handler
    g_signal_connect(pDialog,
                "response",
                G_CALLBACK (DoDlDialogButton),
                dlgCtl);

    return pDialog;

}
/*
**************************************************************************
*
*  FUNCTION: static void doDLStatusMsg(t_Upload* dlgCtl, int idMsg, int ValMsg)
*
*  PURPOSE: Process status messages from the download
*
*  COMMENTS:
*
**************************************************************************
    SendDlgItemMessage(hwnd,IDC_TRANSFERPROGRESS,PBM_SETPOS,0,0);
    SendDlgItemMessage(hwnd,1002,WM_SETTEXT,0,(LPARAM)"---");
    SendDlgItemMessage(hwnd,105,WM_SETTEXT,0,(LPARAM)"Abort");
*/
static void doDLStatusMsg(HWND hwnd, int idMsg, long arg1, long arg2)
{
    t_dlparam*  pParam;
    t_Dnload*   hwndDlg;
    GtkButton*  pButton;
    GtkProgressBar*   pProgressbar;
    GtkLabel*   pLabel;
    char    tmptxt[64];

    if(!hwnd)
        return;
    hwndDlg = (t_Dnload*)hwnd;                      // NOT WORK WHY??? reverse index
    pParam = &(hwndDlg->sp);
//jlpprhwnd("doDLStatusMsg",(void*)hwnd);
//printf("doDLStatusMsg 0x%08x msg=0x%08x(0x%08x,0x%08x)\n",(U32)hwnd,(U32)idMsg,(U32)arg1,(U32)arg2);

//return;

    switch(idMsg)
    {
        //========
        case W2MSG_DONE:
        //========
        {
            DlgDlSrecProc(hwnd, W2MSG_DONE, (WPARAM)1, (LPARAM)arg2);
        }
        break;

        //========
        case UDL_MSGSTATE:
        //========
        {
            pButton = GTK_BUTTON(hwndDlg->SendButton);
            if(!pButton)
                break;
            if(pParam->state == ST_DOWNLOAD)
            {
                gtk_button_set_label(GTK_BUTTON(pButton),"ABORT");
            }
            else
            {
                gtk_button_set_label(GTK_BUTTON(pButton),"SEND");
            }
        }
        break;

        //========
        case UDL_MSGPBAR:
        //========
        {
            DlgDlSrecProc(hwnd, WM_COMMAND, 202, (LPARAM)arg2);
        }
        break;

        //========
        case UDL_LABEL1:
        //========
        {
            DlgDlSrecProc(hwnd, WM_COMMAND, 1001, (LPARAM)arg2);
        }
        break;

        case UDL_LABEL2:
        //========
        {
            DlgDlSrecProc(hwnd, WM_COMMAND, 1002, (LPARAM)arg2);
        }
        break;

        case UDL_LABEL3:
        //========
        {
            DlgDlSrecProc(hwnd, WM_COMMAND, 1003, (LPARAM)arg2);
        }
        break;

        case UDL_LABEL4:
        //========
        {
            DlgDlSrecProc(hwnd, WM_COMMAND, 1004, (LPARAM)arg2);
        }
        break;
    }
}
