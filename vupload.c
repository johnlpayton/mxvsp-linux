/*******************************************************************************
*   FILE: vupload.c
*
*   Upload moved to its own file. Up and download are very sililiar
*   so we want to static most calls to let the compilier check
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

static void doULStatusMsg(HWND v, int idMsg, long arg1, long arg2);
static int UlAction(void* pMapEntry, int cmd, long arg1, long arg2);
static int vspXModemFSM(HWND hwndDlg, char* bufin, int nin, t_dlparam* pParam);
static int DlgUlXmodem(t_Dnload* hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

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


//========================Upload XModem ==================================

/*
**************************************************************************
*
*  Function: static int vspopenul(HWND hwndDlg, t_dlparam* pParam)
*
*  Purpose: Open elements for a upload
*
*   Arguments:
*       HWND        hwndDlg     window
*       t_dlparam*  pParam      pointer to the download structure
*
*  Comments:
*   This will open the file and register the download
*   channel. If returns -1 on an error and also
*   tosses up message box
*
*   HARDWIRED TO VSP8 FOR NOW
*
*
**************************************************************************
*/
#if 1
static int vspopenul(HWND hwnd, t_dlparam* pParam)
{

    int     k;
    HWND    hControl;

    pParam->fin = fopen(pParam->fname,"wb");
    if(pParam->fin == 0)
    {
//        MessageBox(hwnd,"cant fopen","vspopenul",MB_OK+MB_ICONWARNING);
        return(-3);
    }

    pParam->seqnum = 1;
    pParam->fsmstate = 0;

    //======== GUI stuff

    // set the start time
    pParam->starttime = time(NULL);
    pParam->bytessent = 0;
    pParam->retrycntr = 0;
    pParam->nakcntr = 0;

    return 1;
}
#endif

#if 1
static int vspcloseul(HWND hWnd, t_dlparam* pParam)
{

    int     k;
    HWND    hControl;

    if(pParam->wID > 0)
    {
        vmxUnRegisterWindow(pParam->uladdr);
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
*  Function: static int vspXModemFSM(HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam, t_dlparam* pParam)
*
*  Purpose: Upload Binary Xmodem state machine
*
*   RETURN
*       -CHAREOT:   Done with upload
*       -1:         File closed (NULL FILE*)
*       0:          continue
*  Comments:
*   Thie routine will parse a character buffer for an Xmodem
*   upload.  It maintains internal state variables and will
*   deframe the packet.  When the packet if deframed, the
*   bytes are written to the file.  Checksums are not validated.
*   The VSP link is error free and in order because of the
*   underlying TCP layer (and the hardware serial link).
*   Timeouts muust be programmed externally
*   Each time the FSM goes into the search for the first
*   <SOH>, it increments the retrycntr.
*
*   Timeouts are handled by setting the fsmstate to 4.  Not
*   very elegant but this is a subroutine tool.
*
**************************************************************************
*/

static int vspXModemFSM(HWND hwndDlg, char* bufin, int nin, t_dlparam* pParam)
        {
    int     nc;
    int     k;
    int     c;

pParam->uladdr = TID_XMODEMUP;

    nc = 0;
    while(nc<nin)
    {
        c = bufin[nc] & 0xff;
        nc += 1;
#if EASYWINDEBUG & 0
printf("state %2d char 0x%02x: ",pParam->fsmstate,c&0xff);
#endif
        switch(pParam->fsmstate)
        {
            //========
            case 0:     // Search for a SOH
            //========
            {
                pParam->retrycntr += 1;

                if(c == CHARSOH)                // SOH
                {
#if EASYWINDEBUG && 1
printf(" SOH\n");
#endif
                    pParam->nbuf = 0;           // set buffer start
                    pParam->buf[pParam->nbuf++] = c;
                    pParam->fsmstate = 1;       // next state
        }
                else if(c == CHAREOT)           // EOT
                {
#if EASYWINDEBUG && 1
printf(" EOT\n");
#endif
                    return(-CHAREOT);           // all done
    }
                else
                {
                    pParam->retrycntr -= 1;     // SOH to SOH
                }
            }
            break;

            //========
            case 1:     // Search for the expected sequence number
            //========
            {
#if EASYWINDEBUG && 1
printf("Seq Rx %d Exp %d\n",c,(0xff & pParam->seqnum));
#endif
                if(c == (0xff & pParam->seqnum))
                {
                    pParam->buf[pParam->nbuf++] = c;
                    pParam->fsmstate = 2;       // yep, next state
                }
                else
                {
#if EASYWINDEBUG && 0
printf(" Reset 1 Expected 0x%02x \n",(0xff & pParam->seqnum));
#endif
                    pParam->fsmstate = 0;       // nope go back to search
                }
            }
            break;

            //========
            case 2:     // Search for the expected complement sequence number
            //========
            {
                if(c == (0xff & ~pParam->seqnum))
                {
                    pParam->buf[pParam->nbuf++] = c;
                    pParam->fsmstate = 3;       // yep, next state
                }
                else
                {
#if EASYWINDEBUG && 0
printf(" Reset 2 Expected 0x%02x \n",(0xff & ~pParam->seqnum));
#endif
                    pParam->fsmstate = 0;       // nope go back to search
                }
            }
            break;

            //========
            case 3:     // gather 128 bytes
            //========
            {
                if(pParam->nbuf < (128+3))
                {
#if EASYWINDEBUG && 0
printf(" buf[%d]\n",pParam->nbuf);
#endif
                    pParam->buf[pParam->nbuf++] = c;
                }
                else                            // chesksum is here
                {
                    pParam->buf[pParam->nbuf++] = c;

                    // attempt to write the buffer
                    if(pParam->fin)
                    {
#if EASYWINDEBUG && 0
printf(" write file\n");
#endif
                        fwrite(&(pParam->buf[3]),1,128,pParam->fin);
                        pParam->bytessent += (128);   // frame not included
                    }
                    else
                    {
#if EASYWINDEBUG && 0
printf(" write error file closed\n");
#endif
                        return -1;
                    }

                    // send an ACK
                    k = (pParam->uladdr) & 0x7f;
                    pParam->pSend = ReframeData("\006", 1, k, 0x20);
                    SendFrameToRouter(pParam->pSend, ROUTESRCLOCAL);
                    pParam->seqnum += 1;        // inc seq number
                    pParam->fsmstate = 0;       // look for a new frame
                    pParam->tout = 0;
#if EASYWINDEBUG && 1
printf(" ACK to %d\n",k);
#endif
                }
            }
            break;

            //========
            case 4:     // send a NAK
            //========
            {
                    k = (pParam->uladdr) & 0x7f;
                    pParam->pSend = ReframeData("\025", 1, k, 0x20);
                    SendFrameToRouter(pParam->pSend, ROUTESRCLOCAL);
                    pParam->nakcntr += 1;
                    pParam->fsmstate = 0;       // look for a new frame
                    pParam->nbuf = 0;
                    pParam->tout = 0;
#if EASYWINDEBUG && 1
printf(" nak to %d\n",k);
#endif
            }
            break;
        }
    }
    return(0);
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
*  Function: static int UlAction(void* wHnd, void** pData, int cmd, long arg1, long arg2)
*
*  Purpose: W2 action engine
*
*   Arguments:
*       v           Pointer to the down/upload structure
*       txt         pointer to the data
*       ntxt        length of the text
*
*  Comments:
*   This is called from the secondary schedular when a packet is
*       received. It replaces the VSP_SETTEXT message in W32
*
*   Called windows style, this routine is the engine for a download.
*   The primary call is msg == VSP_NEWPACKET.  This message is sent
*   from the EVB as an ACK. The routine should increment the
*   sequence counter and send a new packet. For now, we'll do
*   binary reads of 256 characters. Later we might adjust the
*   sequence.
*
*   HARDWIRED TO VSP8 FOR NOW
*
*
**************************************************************************
*/
#if 1
static int UlAction(void* v, int cmd, long arg1, long arg2)
{
    int retval = 0;
    int k;
    struct S_windowmap* pMapEntry = (struct S_windowmap*)v;
    t_Dnload* pDlg = (t_Dnload*)(pMapEntry->pData);
    t_dlparam* pParam = &(pDlg->sp);

#if EASYWINDEBUG & 0
if(inicfg.prflags & PRFLG_VSP || 0)
{
if(cmd != W2CMD_T500)
printf("UlAction:: *pData 0x%08x cmd %d arg1 0x%08x arg2 0x%08x\n",
    (U32)v, cmd, arg1, arg2);
if(cmd != W2CMD_INIT)
    jlpprhwnd("UlAction",pDlg);
printf("UlAction:Dialog 0x%08x pParam 0x%08x\n",(U32)pDlg,(U32)pParam);
}
#endif

    switch(cmd)
    {
        //========
        default:
        //========
        {
        }
        break;

        //========
        case W2CMD_INIT:
        //========
        {
            // set the working data pointer
            pMapEntry->pData = (void*)(&sDNload);

            // reload pointers
            pDlg = (t_Dnload*)(pMapEntry->pData);
            pParam = (t_dlparam*)&(pDlg->sp);

            pParam->seqnum = 1;
            pParam->state = ST_UPLOAD;
//            pParam->uladdr = TID_XMODEMUP;
            pParam->uladdr = pMapEntry->eTid;    // Use this!!
            pParam->pktsize = 128;
            if(pParam->pktsize > MAXDATASIZE)
                pParam->pktsize = MAXDATASIZE-6;

            //======== GUI stuff

            // set the start time
            pParam->starttime = time(NULL);
            pParam->bytessent = 0;
            pParam->nakcntr = 0;
            pParam->retrycntr = 0;

            pParam->maxnak = 5;                 // mak nat (retry attempts)
            pParam->tout = 2 * 2;               // Start with an initial timeut
            pParam->toutlim = 3 * 2;            // timeout

            // set parameters for the GUI
//            doULStatusMsg(pMapEntry->hWndCB, UDL_MSGPBAR, 1, 0);
//            doULStatusMsg(pMapEntry->hWndCB, UDL_LABEL4, 1, (long)"----");

            retval = 1;

        }
        break;

        //========
        case W2CMD_DESTROY:
        //========
        {
            pParam->state = (ST_UPLOAD+ST_DONE);
        }
        break;

        //========
        case W2CMD_TEXT:
        //========
        {
            char tmptxt[64];

#if EASYWINDEBUG && 1
if( (inicfg.prflags & PRFLG_VSP) || 1)
{
t_dlparam* pP = (t_dlparam*)pMapEntry->pData;
char* pChar = (char*)arg1;
printf("UlAction:W2CMD_TEXT 0x%02x 0x%02x Sseq 0x%02x state 0x%x\n",
    0xff&pChar[0],0xff&pChar[1],0xff&pP->seqnum,pP->state );
}
#endif
            //======== Call the fsm
            k = vspXModemFSM(pMapEntry->hWndCB, (char*)arg1, (int)arg2, pParam);
            if(k == (-CHAREOT))
            {
                pParam->state = (ST_UPLOAD+ST_DONE);
                doULStatusMsg(pMapEntry->hWndCB, W2MSG_DONE, 1, 0);
            }
            else if(k == (-1))
            {
                pParam->state = (ST_UPLOAD+ST_ERROR);
                doULStatusMsg(pMapEntry->hWndCB, W2MSG_ERROR, 1, 0);
            }
            else
            {
                int ksec,kmin,cps;

#if 0
                //======== bump the progress bar
                k = (100*pParam->bytessent) / pParam->fSize;
                doULStatusMsg(pMapEntry->hWndCB, UDL_MSGPBAR, 1, k);
#endif
                //======== Calculate CPS
                k = time(NULL) - pParam->starttime;
                cps = 0;
                if(k > 0)
                {

                    // Characters per second
                    cps = pParam->bytessent / k;
                    sprintf(tmptxt,"%d",cps);
                    doULStatusMsg(pMapEntry->hWndCB, UDL_LABEL2, 1, (long)tmptxt);
                }

                //======== Elapsed seconds
                if(cps > 0)
                {
                    // bytes
                    k = pParam->bytessent;
                    sprintf(tmptxt,"%d",k);
                    doULStatusMsg(pMapEntry->hWndCB, UDL_LABEL4, 1, (long)tmptxt);
                }
            }
                break;


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
        case W2CMD_T500: // most of the work is done here
        //========
        {
#if EASYWINDEBUG && 0
if( (inicfg.prflags & PRFLG_VSP) || 0)
{
printf("UlAction:: W2CMD_T500 state:0x%x tout %d fsmstate %d\n",
    pParam->state, pParam->tout, pParam->fsmstate);
}
#endif

            //======== Check if we are active
            if(pParam->state != (ST_UPLOAD+ST_ACTIVE))
                break;                          // Only when active

            //======== Do timeout logic
            pParam->tout += 1;                  // inc the timeout
            if(pParam->tout <= pParam->toutlim)  // check for an expire
                break;

            //======== did we exceed out attempts?
            if(pParam->nakcntr > pParam->maxnak)
            {
                pParam->state = (ST_UPLOAD+ST_ERROR);
                doULStatusMsg(pMapEntry->hWndCB, W2MSG_TOUT, 1, 0);
                break;
            }

            //======== Force a NAK
            pParam->tout = 0;                   // reset for the next time
            pParam->nbuf = 0;                   // housekeeping
            pParam->fsmstate = 4;               // slam him into NACK
            vspXModemFSM(pMapEntry->hWndCB, " ", 1, pParam); //give him a call
        }
        break;

    }
    return retval;
}
#endif

//=========================================================================
//
//====================== D I A L O G S
//
//=========================================================================


//========================Upload XModem ==================================


/*
**************************************************************************
*
*  Function: int UlgDlSrecProc(t_Dnload* hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
*
*  Purpose: Gtk Dialog box UP load
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

static int DlgUlXmodem(t_Dnload* hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
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

#if EASYWINDEBUG && 1
if(inicfg.prflags & PRFLG_VSP || 1)
{
jlpprhwnd("DlgUlXmodem",(void*)hWnd);
printf("DlgUlXmodem 0x%08x msg=0x%08x(0x%08x,0x%08x)\n",(U32)hWnd,(U32)msg,(U32)wParam,(U32)lParam);
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
            vspcloseul(NULL,pParam);         // close files
//            pButton = GTK_BUTTON(hwndDlg->SendButton);
//            gtk_button_set_label(pButton,"Done");
            doULStatusMsg(
                hwndDlg,
                UDL_LABEL3,
                1,
                (long)"Total");
            doULStatusMsg(
                hwndDlg,
                UDL_LABELB,
                1,
                (long)"Done");
            pParam->state = ST_UPLOAD+ST_DONE;
        }
        break;

        //========
        case WM_COMMAND:                                       // from our controls
        //========
//printf("DlgUlXmodem:WM_COMMAND 0x%08x 0x%08x\n",(U32)wParam,(U32)lParam);
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
                        vspcloseul(NULL,pParam);         // close files
                        doULStatusMsg(
                            hwndDlg,
                            UDL_LABELB,
                            1,
                            (long)"Start");
                        pParam->state = ST_UPLOAD;
                    }
                    else
                    {
                        filename = (char*)gtk_entry_get_text(
                                            GTK_ENTRY(hwndDlg->pCombo));
                        strcpy(pParam->fname,filename);
                        if(vspopenul(NULL,pParam) < 0)
                            kerr |= 1;

                        if(!kerr)
                        {
                           pParam->wID = vmxRegisterWindow(
                                            0,               // not used`
                                            TID_XMODEMUP,    // vsp port
                                            UlAction,        // action function
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
                            strcpy(inicfg.ULfilename,filename);
                        }
                        // need to check kerr and clean up do by hand now
#if EASYWINDEBUG && 1
if(inicfg.prflags & PRFLG_VSP || 0)
{
printf("kerr %x wID %d pParam->uladdr %d\n",kerr,pParam->wID,pParam->uladdr);
}
#endif
                        if(kerr)
                        {
                            vspcloseul(hwndDlg,pParam);
                        }
                        else
                        {
                            doULStatusMsg(
                                hwndDlg,
                                UDL_MSGPBAR,
                                1,
                                0);
                            doULStatusMsg(
                                hwndDlg,
                                UDL_LABEL1,
                                1,
                                (long)"CPS");
                            doULStatusMsg(
                                hwndDlg,
                                UDL_LABEL2,
                                1,
                                (long)"0");
                            doULStatusMsg(
                                hwndDlg,
                                UDL_LABEL3,
                                1,
                                (long)"Bytes");
                            doULStatusMsg(
                                hwndDlg,
                                UDL_LABEL4,
                                1,
                                (long)"----");
                            pButton = GTK_BUTTON(hwndDlg->SendButton);
                            gtk_button_set_label(pButton,"Abort");
                            pParam->state = ST_UPLOAD+ST_ACTIVE;

                            // kick off first transfer
                            pParam->fsmstate = 4;
                        }
                    }

#if EASYWINDEBUG & 0
printf("DlgUlXmodem:case 105 k=%d <%s>\n",k,pParam->fname);
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
                        "Bnary");
                    gtk_file_filter_add_pattern (
                        fpat,
                        "*.bin");

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
                                          GTK_FILE_CHOOSER_ACTION_SAVE,
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
                        inicfg.ULfilename);
                    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fOpendialog),
                        fpat);
                    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fOpendialog),
                        fpat1);
#if 0
                    filename = (char*)gtk_entry_get_text(
                                    GTK_ENTRY(hwndDlg->pCombo));
                    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(fOpendialog),
                            filename);
#endif
                    if (gtk_dialog_run (GTK_DIALOG (fOpendialog)) == GTK_RESPONSE_ACCEPT)
                    {
                        filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (fOpendialog));
//printf("File = <%s>\n",filename);
                        gtk_entry_set_text(GTK_ENTRY(hwndDlg->pCombo),filename);
                        g_free (filename);
                        doULStatusMsg(
                            hwndDlg,
                            UDL_LABELB,
                            1,
                            (long)"Start");
                        pParam->state = ST_UPLOAD;
                     }
                    gtk_widget_destroy (fOpendialog);

                }
                break;

               //==================
                case 201:      // Combo box
               //==================
#if EASYWINDEBUG && 0
printf("DlgUlXmodem:case 201\n");
#endif
                break;

               //==================
                case 202:      // Progress bar
               //==================
               {
                    GtkWidget*  pProgressbar;
                    pProgressbar = (hwndDlg->pProgressbar);
                    if(!pProgressbar)
                        break;
                    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(pProgressbar),
                        (double)lParam/100.0);
               break;
               }
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
               case 1005:      // Label Button
               //==================
               {
                    pButton = GTK_BUTTON(hwndDlg->SendButton);
                    gtk_button_set_label(pButton,(char*)lParam);
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
*  FUNCTION: void DoUlDialogButton(GtkDialog* pDialog, gint response_id, gpointer user_data)
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

void DoUlDialogButton(GtkDialog* pDialog, gint response_id, gpointer user_data)
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
        DlgUlXmodem((HWND)dlgCtl, WM_COMMAND, 106, 0);
    }
    //========
    else if(response_id == 1)       // Start/Abort button
    //========
    {
        DlgUlXmodem((HWND)dlgCtl, WM_COMMAND, 105, 0);
    }
    //========
    else if(response_id == 3)       // Quit button
    //========
    {
        DlgUlXmodem((HWND)dlgCtl, W2MSG_DONE, 0, 0);
        gtk_widget_destroy(GTK_WIDGET(pDialog));
//        gtk_widget_hide(GTK_WIDGET(pDialog));
    }
}

/*
**************************************************************************
*
*  FUNCTION: GtkWidget* DoUlDialogClose(GtkWindow* win, t_Upload* dlgCtl)
*
*  PURPOSE: Make the Download dialog
*
*  COMMENTS:
*   Right now, we do this the hard way.  There should be some sort
*   of graphical program but Glade seems to crash
*
**************************************************************************
*/
static gboolean DoUlDialogClose (GtkWidget *widget, GdkEventButton *event)
{
    printf("DoDlDialogClose\n");
    return FALSE;
}

GtkWidget* makeUlDialog(GtkWindow* win, t_Dnload* dlgCtl, const char* name)
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
    if(strlen(inicfg.ULfilename) > 0)
        gtk_entry_set_text(GTK_ENTRY(pCombo),
            inicfg.ULfilename);
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

    dlgCtl->tag = 0xFACEDEAD;
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
                G_CALLBACK (DoUlDialogClose),
                pDialog);

    g_signal_connect_swapped (pDialog,
                "close",
                G_CALLBACK (gtk_widget_destroy),
                pDialog);
#else
    g_signal_connect_swapped (pDialog,
                "close",
                G_CALLBACK (DoUlDialogClose),
                pDialog);
#endif
    //======== connect to the button handler
    g_signal_connect(pDialog,
                "response",
                G_CALLBACK (DoUlDialogButton),
                dlgCtl);

    return pDialog;

}
/*
**************************************************************************
*
*  FUNCTION: static void doULStatusMsg(t_Upload* dlgCtl, int idMsg, int ValMsg)
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
static void doULStatusMsg(HWND hwnd, int idMsg, long arg1, long arg2)
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
//jlpprhwnd("doULStatusMsg",(void*)hwnd);
//printf("doULStatusMsg 0x%08x msg=0x%08x(0x%08x,0x%08x)\n",(U32)hwnd,(U32)idMsg,(U32)arg1,(U32)arg2);

//return;

    switch(idMsg)
    {
        //========
        case W2MSG_DONE:
        //========
        {
            DlgUlXmodem(hwnd, W2MSG_DONE, (WPARAM)1, (LPARAM)arg2);
        }
        break;

        //========
        case UDL_MSGSTATE:
        //========
        {
            pButton = GTK_BUTTON(hwndDlg->SendButton);
            if(!pButton)
                break;
            if(pParam->state == ST_UPLOAD)
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
            DlgUlXmodem(hwnd, WM_COMMAND, 202, (LPARAM)arg2);
        }
        break;

        //========
        case UDL_LABEL1:
        //========
        {
            DlgUlXmodem(hwnd, WM_COMMAND, 1001, (LPARAM)arg2);
        }
        break;

        case UDL_LABEL2:
        //========
        {
            DlgUlXmodem(hwnd, WM_COMMAND, 1002, (LPARAM)arg2);
        }
        break;

        case UDL_LABEL3:
        //========
        {
            DlgUlXmodem(hwnd, WM_COMMAND, 1003, (LPARAM)arg2);
        }
        break;

        case UDL_LABEL4:
        //========
        {
            DlgUlXmodem(hwnd, WM_COMMAND, 1004, (LPARAM)arg2);
        }
        break;

        case UDL_LABELB:
        //========
        {
            DlgUlXmodem(hwnd, WM_COMMAND, 1005, (LPARAM)arg2);
        }
        break;
    }
}
