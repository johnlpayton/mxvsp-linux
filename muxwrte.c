/*******************************************************************************
*   FILE: muxwrte.c
*
*   Contains the source code for secondary routing of local
*   messages to windows.  The main call is from
*   mxvspMainProc.VSP_NEWPACKET. There are also extern
*   utilities
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
#include <time.h>

#if USEGDK
#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <vte/vte.h>
#include <gdk/gdkkeysyms.h>
#include "w32defs.h"
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
#include "muxctl.h"
#include "framer.h"
//#include "muxevb.h"
//#include "muxsock.h"
//#include "MTTTY.H"
#include "vspterm.h"
#include "muxw2ctl.h"
#include "vdownload.h"
#include "w32defs.h"

t_mailbox   WmuxInMail;
int     WmuxActive=0;
int AllowNewWindows = 1;

extern GtkWidget*  newvspterm(void);

#define WWACTIONNEW     (0x01)
#define WWACTIONSEND    (0x02)
#define WWACTIONCOOK    (0x04)
#define WWACTIONFORWARD (0x08)
#define WWACTIONDISCARD (0x10)
#define WWACTIONACK     (0X20)
#define WWACTIONNONE    (0x80)


/*---------------------------------------------------------------------------
*
* FUNCTION: static int DoNothing2Args(void* p1, void* p2, int a3)
*
*  History:   Date       Author      Comment
*  24-Feb-2012 jlp
*   initial pass
*
*---------------------------------------------------------------------------*/
static int DoNothing2Args(void* a, void* b, int c, char* d, int e)
{
    return 0;
}

/*---------------------------------------------------------------------------
*  FUNCTION: static int WWAnalyizePkt(t_ctlreqpkt* pCpkt, int* wID, int* wTID)
*
*  Description:
*   Handle an input CtlReq packet
*
*  Parameters:
*     t_ctlreqpkt*   pCpkt      input control packet
*     int*           wID        index into packet table
*     int*           wTID       starting TID
*
*   Returns
*       WWACTIONNEW 1
*       WWACTIONRAW 2
*       WWACTIONCOOK 3
*       WWACTIONNONE 4
*
*  Comments
*   This routine examines the request from a window (hWnd) that
*   will route the data to child (vxtty) wimdows for display. It's
*   called from the main event loop in response to a VSP_NEWPACKET
*   message. It examines the packet and the routing control table
*   to select what to do with the packet.
*
*
*  History:   Date       Author      Comment
*  24-Feb-2012 jlp
*   initial pass
*
*---------------------------------------------------------------------------*/
static int WWAnalyizePkt(t_ctlreqpkt* pCpkt, int* wID, int* wTID)
{

    t_ctldatapkt*   pDpkt;
    char*           pFrame;
    int             widx;
    int             wAddr;
    int             wFlags;
    int             retval;
    int             k;

    retval       = WWACTIONDISCARD;

    pDpkt = pCpkt->pData;                       // dereference to the data header
    pFrame = pDpkt->pData;                      // dereference to the frame
    wAddr = pFrame[2]&0xff;
    *wID         = -1;
    *wTID        = wAddr;
    widx = LookUpWinByTID(wAddr);             // get the window


    //========
    if(widx >= 0)   // window exists
    //========
    {
        wFlags = (TTYWindows[widx].wFlags) & 0xffff;
        retval &= ~WWACTIONNEW;                 // New window not needed
        *wID = widx;                            // this is the existing window
//        *wTID = wFlags & 0xff; // should have this

        if( (wFlags & W2NOREFRAME))
            retval &= ~WWACTIONCOOK;            // don't reframe
        else
            retval |= WWACTIONCOOK;             // do reframe

        if( (wFlags & W2NODISCARD))
            retval &= ~WWACTIONDISCARD;         // don't discard
        else
            retval |= WWACTIONDISCARD;          // do discard

        retval |= WWACTIONSEND;                 // forward it
    }
    else                                        // Open a window
    {

        if(wAddr & WWANTACK)                    // this wants an ACK
        {
            retval |= WWACTIONNONE;             // it is for somebody else
        }
        else if( (wAddr < DEVVSP0) || (wAddr > DEVVSP7))
        {
            retval |= WWACTIONNONE;             // private or illegal address
        }
        else if((1)                             // all modes
             && (AllowNewWindows < 2)           // .AND. no create
             && (wAddr > DEVVSP0))             // .AND. (.NOT. JBUG)      (logic is not workig just yet)
        {
            retval |= WWACTIONNONE;             // Server does not do auto windows
        }
        else                                    // Ok, finally open a new window
        {
            for(widx=1;widx<MAXTOTALWINS;widx++)      // look up an empty slot
            {
                if(TTYWindows[widx].wHnd == NULL)
                    break;
            }

            if(widx < MAXTOTALWINS)                   // found one
            {
                retval |= WWACTIONNEW;          // we want to open a new window
                *wID = widx;                    // in here
                *wTID = (wAddr);                // with this information
                retval |= WWACTIONSEND|WWACTIONCOOK;
            }
            else                                // tagle is full
            {
                retval |= WWACTIONNONE;         // do nothing
            }
        }
    }
    return retval;
}

/*---------------------------------------------------------------------------
*  FUNCTION: static int NewPacketName(char* NameNew, int wAction, int wTID, char* pTxd, int nTxd)
*
*  Description:
*   Extract a name for a new window from a packet
*
*  Parameters:
*   char* NameNew   new name
*   int wAction     action verd
*   int wTID        extracted TIG
*   char* pTxd      data pointer
*   int nTxd        data length
*
*   Returns
*
*  Comments
*   Thie is one of those messy little routines.
*   We attempt to extract some printable characters at the start to
*   make a name.  Otherwise we jst call it "VP-n"
*
*  History:   Date       Author      Comment
*  24-Feb-2012 jlp
*   initial pass
*
*---------------------------------------------------------------------------*/
static void NewPacketName(char* NameNew, int wAction, int wTID, char* pTxd, int nTxd)
{
    int     n,m;
    int     usedef = 1;

#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_OTHER)
{
printf("NewPacketName: wAction=0x%x wTID=0x%x nTxd=%d\n",wAction,wTID,pTxd[0]);
}
#endif

    if( (wAction & WWACTIONCOOK) &&  (nTxd > 2)) // cooked packet with some data in it
    {
        n = 0;
        while( (n < nTxd) && isspace(pTxd[n]))  // skip white
            n += 1;
        m = 0;
        while( (n < nTxd) && isalnum(pTxd[n]) && (m <= 7))  // collect 7 alnums only
        {
            NameNew[m++] = pTxd[n++];
        }
        NameNew[m] = 0;

        if( m >0 )                              // foumd something
            usedef = 0;
    }

    if(usedef)
    {
        if(wTID == TID_UNFRAMED)
            sprintf(NameNew,"UnFr");
        else
            sprintf(NameNew,"VSP-%d",0x7f & (wTID-DEVVSP0));
    }
}

/*---------------------------------------------------------------------------
*  FUNCTION: int DoWCtlReq(HWND hWnd, long wParam, long lParam)
*
*  Description:
*   Handle an input CtlReq packet
*
*  Parameters:
*     HWND hWnd    Caller window
*     wParam
*     lParam        Pointer to the control packet
*
*   Returns
*
*  Comments
*   There is some dereferencing to get down to the actual
*   frame and data. It's a bit deep, but the design allows the
*   server to keep one data packet (t_ctldatapkt*) and have
*   multiple small request packets (t_ctlreqpkt*) placed into
*   queues for transmission.
*
*   This routine examines the request from a window (hWnd) that
*   will route the data to child (vxtty) wimdows for display. It's
*   called from the main event loop in response to a VSP_NEWPACKET
*   message.
*
*  History:   Date       Author      Comment
*  24-Feb-2012 jlp
*   initial pass
*
*---------------------------------------------------------------------------*/
int DoWCtlReq(HWND hWnd, long wParam, long lParam)
{
    int             k;
    HWND            hTTY;
    t_ctlreqpkt*    pCpkt;              // reduce typing
    t_ctldatapkt*   pDpkt;
    t_ctldatapkt*   pSend;              // will be the packet to send
    char            *pTxd;              // Send character Pointer
    int             nTxd;               // Number of characters
    int             wID;
    int             wTID;
    int             wAction;


    pCpkt = (t_ctlreqpkt*)lParam;       // dereference the control
    pDpkt = pCpkt->pData;               // dereference to the data header

#if (EASYWINDEBUG>5) && 1
{
jlpDumpCtl("DoWCtlReq_1:",pCpkt);
    FatalOnNullPtr("DoWCtlReq_1",pDpkt);
}
#endif


    wAction = WWAnalyizePkt(pCpkt, &wID, &wTID);

#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_WINMUX)
{
jlpDumpCtl("DoWCtlReq_2:",pCpkt);
printf("wAction=0x%x wID=0x%x wTID=0x%x\n",wAction,wID,wTID);
}
#endif

    // all done with the control packet, release it
    ReleaseCtlPacket(pCpkt,&gRouterCS);
#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_WINMUX)
{
printf("DoWCtlReq_2: Outstanding Ctl %d Data %d\n",OutstandingCtlPkts,OutstandingDataPkts);
}
#endif


    //========
    if(wAction & WWACTIONCOOK)                   // do we deframe?
    //========
    {
        pSend = (t_ctldatapkt*)DeFramePacket(pDpkt,NULL,NULL);
        if(!pSend)
            FatalError("DoWCtlReq:DeFramePacket fail 1");

        ReleaseDataPacket(pDpkt,&gRouterCS);     // Release old
        pTxd = (char*)(pSend->pData);            // skip <addr>|<seq> bytes
        nTxd = pSend->nData;
#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_WINMUX)
{
printf("DoWCtlReq_3 TTY %d hWnd=0x%08x len=%d\n",wID,(U32)(TTYWindows[wID].wHnd),nTxd);
}
#endif
    }
    else
    {
        pSend = pDpkt;
        pTxd = (char*)(pSend->pData);           // Forward entire packet
        nTxd = pSend->nData;
    }
#if (EASYWINDEBUG>5) && 0
{
    FatalOnNullPtr("DoWCtlReq_4",pSend);
}
#endif

    //========
    if(wAction & WWACTIONNEW)                  // New window ?
    //========
    {
        char tmpstr[16];

        TTYWindows[wID].wHnd = newvspterm();

        if(!TTYWindows[wID].wHnd)
            return(0);

        wID = vmxRegisterWindow(0,wTID,VspAction,hWnd,NULL);
#if USEWIN32
        SendMessage(hWnd,VSP_SETPARENT,wTID,(LPARAM)ghwndMain);
#endif
        strcpy(TTYWindows[wID].wName,tmpstr);
        ActiveTab = wID;
    }

    //========
    if(wAction & WWACTIONSEND)                  // Send the frame
    //========
    {
#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_WINMUX)
{
printf("DoWCtlReq_5: WWACTIONSEND wID %d nTxd 0x%08x pTxd 0x%08x\n",wID,(nTxd),(LPARAM)pTxd);
}
#endif
        if(nTxd > 0)                            // Final check on 0 length
        {
            hTTY = TTYWindows[wID].wHnd;
            if(hTTY)
            {
#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_WINMUX)
{
   jlpDumpFrame("DoWCtlReq_6:WWACTIONSEND",pTxd,nTxd);
//   printf("<%s>",pTxd);
}
#endif
//                SendMessage(hTTY,VSP_SETTEXT,(WPARAM)(nTxd),(LPARAM)pTxd);
                TTYWindows[wID].doAction(
                    &(TTYWindows[wID]),
                    W2CMD_TEXT,
                    (long)pTxd,
                    (long)nTxd);
            }
        }
    }

    //========
    if(wAction & WWACTIONFORWARD)               // Forward the packet
    //========
    {
//        if(TTYWindows[wID].wHnd)
//            SendMessage(TTYWindows[wID].wHnd,VSP_SETTEXT,(WPARAM)(0),(LPARAM)pSend);
    }

    //========
    if((wAction & WWACTIONDISCARD))            // discard the packet
    //========
    {
        ReleaseDataPacket(pSend,&gRouterCS);  // release data to the system
#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_WINMUX)
{
printf("DoWCtlReq_7: WWACTIONDISCARD wID %d pSend 0x%08x\n",wID,(LPARAM)pSend);
}
#endif
    }
    return(0);
}

/*---------------------------------------------------------------------------
*  FUNCTION: int LookUpWinByTID(int wAddr)
*
*  Description:
*   Look up the desdination for this packet and return the index
*
*  Parameters:
*     int wAddr    Address taken fro the packet
*
*   Returns
*       idx     index into the TTYWindows table
*       -1      A window is not found
*
*  Comments
*   Looks up a destination window in the TTYWindows table
*
*  History:   Date       Author      Comment
*  24-Feb-2012 jlp
*   initial pass
*
*---------------------------------------------------------------------------*/
int LookUpWinByTID(int wAddr)
{
    int k;

    wAddr = wAddr & 0xff;       // ensure is a byte
#if EASYWINDEBUG && 0
if(inicfg.prflags & PRFLG_REGISTER)
{
printf("LookUpTTYWindowA: wAddr %d \n",wAddr);
}
#endif
    for(k=0; k < MAXTOTALWINS; k++)
    {
        if(wAddr == ((TTYWindows[k].eTid)&0xff) )
        {
#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_REGISTER)
{
printf("LookUpWinByTID: found %d %x %08x\n",k,TTYWindows[k].eTid,(U32)TTYWindows[k].wHnd);
}
#endif
            return(k);
        }
    }
    return(-1);

}

/*---------------------------------------------------------------------------
*  FUNCTION: int LookUpWinByTAB(int wAddr)
*
*  Description:
*   Look up the window by the tab number
*
*  Parameters:
*     int wAddr    Tab number fron the GUI
*
*   Returns
*       idx     index into the TTYWindows table
*       -1      A window is not found
*
*  Comments
*   Looks up a destination window in the TTYWindows table
*
*  History:   Date       Author      Comment
*  24-Feb-2012 jlp
*   initial pass
*
*---------------------------------------------------------------------------*/
int LookUpWinByTAB(int wAddr)
{
    int k;

    wAddr = wAddr & 0xff;       // ensure is a byte
#if EASYWINDEBUG && 0
if(inicfg.prflags & PRFLG_REGISTER)
{
printf("LookUpWinByTAB: wAddr %d \n",wAddr);
}
#endif
    for(k=0; k < MAXTOTALWINS; k++)
    {
        if(wAddr == ((TTYWindows[k].wTab)&0xff) )
        {
#if EASYWINDEBUG && 0
if(inicfg.prflags & PRFLG_REGISTER)
{
printf("LookUpWinByTAB: found %d %08x %08x\n",k,TTYWindows[k].eTid,(U32)TTYWindows[k].wHnd);
}
#endif
            return(k);
        }
    }
    return(-1);

}
/*---------------------------------------------------------------------------
*  FUNCTION: int LookUpWinByHWND(HWND hwnd)
*
*  Description:
*   Look up the window by the tab number
*
*  Parameters:
*     int wAddr    Tab number fron the GUI
*
*   Returns
*       idx     index into the TTYWindows table
*       -1      A window is not found
*
*  Comments
*   Looks up a destination window in the TTYWindows table
*
*  History:   Date       Author      Comment
*  24-Feb-2012 jlp
*   initial pass
*
*---------------------------------------------------------------------------*/
int LookUpWinByHWND(HWND hwnd)
{
    int k;

#if EASYWINDEBUG && 0
if(inicfg.prflags & PRFLG_REGISTER)
{
printf("LookUpWinByHWND: wAddr %d \n",hwnd);
}
#endif
    for(k=0; k < MAXTOTALWINS; k++)
    {
        if(hwnd == ((TTYWindows[k].wHnd)) )
        {
#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_REGISTER)
{
printf("LookUpWinByHWND: found %d %08x %08x\n",k,TTYWindows[k].eTid,(U32)TTYWindows[k].wHnd);
}
#endif
            return(k);
        }
    }
    return(-1);

}
/*---------------------------------------------------------------------------
*  FUNCTION: int vmxRegisterWindow(int wID, int tid, int kind, HWND hWnd)
*
*  Description:
*   Register a window in the dispatch table
*
*  Parameters:
*     int wID       Registry ID
*     int tid       tid of entry (packet address + flags)
*     HWND hWnd     window
*
*   Returns
*       wID     index into the TTYWindows table
*       -1,-2      errors
*
*
*  Comments
*   Registers a new window in the dispatch table.
*   the first argument is no longer used
*
*  History:   Date       Author      Comment
*  07-Jan-2013 jlp
*   Removed unused arg1 and streamlined
*
*  24-Feb-2012 jlp
*   initial pass
*
*---------------------------------------------------------------------------*/
int vmxRegisterWindow(int wArg1, int tid, pWinFunction efunc, HWND hWnd, HWND hMsg)
{
    int k;
    unsigned int wAddr;
    int     wID;

#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_REGISTER)
{
printf("vmxRegisterWindow: tid = 0x%x\n",tid);
}
#endif

    if(efunc == NULL)
        efunc = VspAction;

    //======== tid must be unique
    k = LookUpWinByTID(tid);
    if(k>=0)
        return -1;

    //======== find first free slot
    for(wID=0; wID < MAXTOTALWINS; wID += 1)
    {
        if(!TTYWindows[wID].wHnd)
            {
                break;
            }
        }

    //========== none emoty, error
        if(k >= MAXTOTALWINS)
        {
            wID = -3;
            return wID;
        }

    //======== fill structure entries
    TTYWindows[wID].wTab = wID;
    TTYWindows[wID].wHnd = hWnd;
    TTYWindows[wID].hWndCB = hMsg;
    TTYWindows[wID].eTid = tid;
    TTYWindows[wID].wFlags = 0;
    TTYWindows[wID].pData = (void*)tid;     // Features
    TTYWindows[wID].doAction = efunc;

#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_REGISTER)
{
printf("vmxRegisterWindow: wId = %d eTid = 0x%x wHnd = 0x%08x\n",
    wID,
    TTYWindows[wID].eTid,
    (U32)TTYWindows[wID].wHnd
    );
}
#endif

    //======== call the W2CMD_INIT
    k =TTYWindows[wID].doAction(
            &(TTYWindows[wID]),
            W2CMD_INIT,
            (long)TTYWindows[wID].eTid,
            (long)0);

     //======== negative return deregisters
     if(k < 0)
     {
        TTYWindows[wID].wHnd    = NULL;
        TTYWindows[wID].hWndCB  = NULL;
        TTYWindows[wID].eTid    = 0;
        TTYWindows[wID].wTab    = 0;
        wID = -4;
     }

    return(wID);
}

/*---------------------------------------------------------------------------
*  FUNCTION: int vmxUnRegisterWindow(int tid)
*
*  Description:
*   UnRegister a window in the dispatch table
*
*  Parameters:
*     int tid       tid of entry (packet address + flags)
*
*   Returns
*       0
*
*  Comments
*   UnRegisters a window in the dispatch table
*
*  History:   Date       Author      Comment
*  24-Feb-2012 jlp
*   initial pass
*
*---------------------------------------------------------------------------*/
int vmxUnRegisterWindow(int wTid)
{
    int k;
    int     wID;


#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_REGISTER)
{
printf("vmxUnRegisterWindow:wAddr=0x%02x\n",wTid);
}
#endif
    wID = LookUpWinByTID(wTid);
    if(wID > 0)
    {
        //======== attempt to unregister the active window
        if(ActiveTab == TTYWindows[wID].wTab)
            {
//                SendMessage(ghwndMain,VSP_TELLID,(WPARAM)TTYWindows[0].tid,(LPARAM)TTYWindows[0].wHnd);
                ActiveTab= 1;
//======== Need to activae this window
            }

        //======== call the W2CMD_DESTROY
        TTYWindows[wID].doAction(
            &(TTYWindows[wID]),
            W2CMD_DESTROY,
            (long)TTYWindows[wID].eTid,
            (long)0);

        TTYWindows[wID].wHnd    = NULL;
        TTYWindows[wID].eTid    = 0;
        TTYWindows[wID].wTab    = 0;
//            if(hStartDialog)
//                SendMessage(hStartDialog,VSP_NEWWINDOW,0,0);
        }
    return(k);

    }

/*---------------------------------------------------------------------------
*  FUNCTION: void doTimerForAll()
*
*  Description:
*   Loop through all TTYWindows sending a timer
*
*  Parameters:
*
*   Returns
*
*  Comments
*
*  History:   Date       Author      Comment
*  13-Jan-2013 jlp
*   initial pass
*
*---------------------------------------------------------------------------*/
void doTimerForAll()
{
    int     wID;
    time_t  ts;

    time(&ts);

    for(wID=1; wID < MAXTOTALWINS; wID += 1)
    {
        if((TTYWindows[wID].wHnd) && (TTYWindows[wID].doAction))
        {
            TTYWindows[wID].doAction(
                &(TTYWindows[wID]),
                W2CMD_T500,
                (long)TTYWindows[wID].eTid,
                (long)ts);
        }
    }
}

/*---------------------------------------------------------------------------
*  FUNCTION: int doBroadcastPkt(int wId, char* pTxd, int nTxd)
*
*  Description:
*   Process an internal broadcast packet
*
*  Parameters:
*   int     wId     not sure what this is
*   char*   pTxd    Pointer to the data
*   int     nTxd    Length of the data
*
*   Returns
*
*  Comments
*   This processes an internal broadcast packet
*   Broadcast packets are used for internal control
*   Packets are integer commands with optional arguments. They are
*   sent by a client or a server and received by all.  Only those that
*   are effected by the command take any action
*
*   EVB rate change
*       BCST_NEWRATE    (U32)rate,(U32)flow
*           The new rate for EVB communications is changed.  The server will
*           send (JBUG) a rate change command then it will change the local
*           com port to match.  Clients ignore the message.  The user should
*           see JBUG process the message.
*
*  History:   Date       Author      Comment
*  24-Feb-2012 jlp
*   initial pass
*
*---------------------------------------------------------------------------*/
unsigned int b2U32(char* bs)
{
    unsigned int utmp;

    // little endian
    utmp = ( (bs[0]<<0)  & 255)
         + ( (bs[1]<<8)  & 255)
         + ( (bs[2]<<16) & 255)
         + ( (bs[3]<<24) & 255);
    return(utmp);
}

int doBroadcastPkt(int wId, char* pTxd, int nTxd)
{
    int k;
    UINT msg;
    WPARAM wParam;
    LPARAM lParam;

    msg = b2U32(&(pTxd[0]));
    wParam = b2U32(&(pTxd[4]));
    lParam = b2U32(&(pTxd[8]));

    switch(msg)
    {
        //========
        case IMSG_BR:
        //========
//            EVBSetBF(&ComPortA,wParam,lParam);
        break;
    }
    return 1;
}
/*---------------------------------------------------------------------------
*  void GoF80000(void)
*
*  Description:
*   Send G 0xf80000
*
*  Parameters:
*
*   Returns
*
*  Comments
*   G 0xf80000 is a command sent to MACSBUG to start the kernel (and JBUG)
*
*  History:   Date       Author      Comment
*  24-Feb-2012 jlp
*   initial pass
*
*---------------------------------------------------------------------------*/
void GoF80000(void)
{
    SendUnFramed("G 0xf80000\r",11,ROUTESRCLOCAL);
}
/*---------------------------------------------------------------------------
*  void GoFlash(void)
*
*  Description:
*   Send G 0xfff0011c
*
*  Parameters:
*
*   Returns
*
*  Comments
*   G 0xfff0011c is a command sent to MACSBUG to start from flash
*   Unfortunatly the flash location can change when the build changes
*
*md.l 0xfff00000
*
*fff00000:  00000000 fff0011c 000002e4 00000000              ................
*fff00010:  00000000 00000000 424f4f54 00000000              ........BOOT....
*
*
*  History:   Date       Author      Comment
*  24-Feb-2012 jlp
*   initial pass
*
*---------------------------------------------------------------------------*/
void GoFlash(void)
{
    SendUnFramed("G 0xfff0011c\r",13,ROUTESRCLOCAL);
}

/*---------------------------------------------------------------------------
*  void GoFlash(void)
*
*  Description:
*   Send G 0xfff0011c
*
*  Parameters:
*
*   Returns
*
*  Comments
*   VERSION is recognized by MACSBUD and JBUG
*
*  History:   Date       Author      Comment
*  2-Jun-2012 jlp
*   initial pass
*
*---------------------------------------------------------------------------*/
void GoVersion(void)
{
    SendUnFramed("VERSION\r",8,ROUTESRCLOCAL);
}

/*---------------------------------------------------------------------------
*  void SendS7Record(void)
*
*  Description:
*   Sends a S7 record (end of download) to end a transfer
*
*  Parameters:
*
*   Returns
*
*  Comments
*   This is a semi useful command which sents an S7 record (end of transfer) to
*   attempt to kick MACBUG or JBUG out of an interrupted download.  Either should
*   report an error ans return to input command mode
*
*  History:   Date       Author      Comment
*  24-Feb-2012 jlp
*   initial pass
*
*---------------------------------------------------------------------------*/
void SendS7Record(void)
{
    SendUnFramed("S7050002800078\r",15,ROUTESRCLOCAL);
}

/*---------------------------------------------------------------------------
*  void PSCOMMAND(void)
*
*  Description:
*   Sends the common PS command to UnFramed
*
*  Parameters:
*
*   Returns
*
*  Comments
*   PS is a JBUG only command to display process status. Very useful to see
*   if JBUG is running/hung or there is some communications problem. MACSBUG
*   will respond with an uncrecognized command message.  When nothing comes
*   back, further investigation is needed.  Sometimes Dopey is hung in a windows
*   error dialog because mxtty is not yet hardened.  Its pretty good but
*   not fully there.
*
*  History:   Date       Author      Comment
*  24-Feb-2012 jlp
*   initial pass
*
*---------------------------------------------------------------------------*/
void PSCOMMAND(void)
{
    SendUnFramed("ps\r",3,ROUTESRCLOCAL);
}

/*-----------------------------------------------------------------------------

FUNCTION: int ParseEnterLine(HWND hWnd, char* src, char *dst, int flagCR)

PURPOSE: Parse the input from the user and send it

PARAMETERS:
    hWnd    - window handle
    flagCR  - flag (Bit 0 -> add CR)

Returns
    0 no characters to send
    >0 characters to send

Comments
    This does more than fimply parsing a line
        1) Release and deallocate any line
        2) Allocate a new line
        3) Get the source from the combo bos
        4) copy the source into the new line
        5) Send it off

    This works pretty well there are some "features" that should be added
    Oh, by the way \n works

    \n  <LF>
    \r  <CR>
    \e  <ESC>
    \\  <baskslash>
    \letter sends the control bits (0x1f&letter)

    The entered text is terminated by a selectable parameter. Right now
    it is <CR> so the program looks like a TTY. This is an area I want to
    exoand on later

HISTORY:   Date:      Author:     Comment:
           03/12/12   jlp      Big simplification to paste.. let W32 do the work
           01/17/10   jlp      Wrote it

-----------------------------------------------------------------------------*/
#if USEWIN32

int ParseEnterLine(HWND hWnd, int flagCR)
{
    HWND    hControl;                           // control of the list box
    int     selitem;                            // selected item
    int     itemlen;
    char    *pStr;
    int     k,ksrc,kdst;
    char*   tmpSrc;
    char*   tmpDst;

    /*
    ** Get the combo box text
    */
    hControl=GetDlgItem(hWnd,503);          // get the handle to the control
    itemlen = SendMessage( hControl, WM_GETTEXTLENGTH, 0, 0); // get selected item
    if(itemlen <= 256)
        k = 256;
    else
        k = ( ((itemlen/256) + 1) *256);    // sup to nearest 256
    tmpSrc = malloc(k);
    tmpDst = malloc(k);

    SendMessage( hControl, WM_GETTEXT, k, (LPARAM)tmpSrc);
    tmpSrc[itemlen]=0;                      // for safety

    ksrc = 0;
    kdst = 0;
    while( tmpSrc[ksrc])
    {
        if( (0x7f&tmpSrc[ksrc]) != 0x5c)    // '\'
        {
            tmpDst[kdst++] = 0x7f&tmpSrc[ksrc++];
        }
        else                                // yep, do some common escape codes
        {
            ksrc +=1;
#if EASYWINDEBUG & 0
printf("ParseEnterLine: esc-%c\n",tmpSrc[ksrc]);
#endif
            switch (tmpSrc[ksrc])
            {
                case 0x5c: // backslash to baskslash>
                    tmpDst[kdst++] = 10;
                    ksrc += 1;
                break;

                case 'n': // n is mapped to <LF>
                    tmpDst[kdst++] = 10;
                    ksrc += 1;
                break;

                case 'r': // r is mapped to <CR>
                    tmpDst[kdst++] = 13;
                    ksrc += 1;
                break;

                case 'b': // b is mapped to <BS>
                    tmpDst[kdst++] = 8;
                    ksrc += 1;
                break;

                case 'e': // e is mapped to <ESC>
                    tmpDst[kdst++] = 8;
                    ksrc += 1;
                break;

                default:        // map to a control char
                    tmpDst[kdst++] = tmpSrc[ksrc++] & 0x1f;
                break;
            }
        }
    }

    tmpDst[kdst] = 0;                       // terminate
    free(tmpSrc);                           // free the working source string

     // Sorting.
    k=strlen(tmpDst);
    if(k>0)
    {
        k=SendMessage( hControl, CB_FINDSTRINGEXACT, -1, (LPARAM)tmpDst);
        if(k != CB_ERR)
        {
           SendMessage( hControl, CB_DELETESTRING, k, NULL);
        }
        k = SendMessage( hControl, CB_INSERTSTRING, -1, (LPARAM)tmpDst);
        SendMessage( hControl, CB_SETCURSEL, k, NULL);
    }

    k=strlen(tmpDst);
    switch(inicfg.lineend)
    {
        //========
        case UART_LINEEND_CR:
        //========
            tmpDst[k+0]=13;
            tmpDst[k+1]=0;
            k += 1;
        break;

        //========
        case UART_LINEEND_LF:
        //========
            tmpDst[k+0]=10;
            tmpDst[k+1]=0;
            k += 1;
        break;

        //========
        case UART_LINEEND_CRLF:
        //========
            tmpDst[k]=13;
            tmpDst[k+1]=10;
            tmpDst[k+2]=0;
            k += 2;
        break;
    }
    //k=strlen(tmpDst);
    //SendMessage(ActiveTTY.wHnd,VSP_SETTEXT,k,(long)tmpDst);

    {
        t_ctldatapkt* pPayload;

        pPayload = ReframeData(tmpDst, k, ActiveTTY.eTid, SEQNUM_DEFAULT);
        if(!pPayload)
            FatalError("ParseEnterLine:AllocDataPacket fail 1");

#if EASYWINDEBUG & 1
if(inicfg.prflags & PRFLG_OTHER)
{
printf("ParseEnterLine: to %d <%s>\n",ActiveTTY.eTid,ActiveTTY.wName);
}
#endif
        SendFrameToRouter(pPayload,ROUTESRCLOCAL+0x500);
    }
    free(tmpDst);
    SetFocus(hControl);
    return(kdst);
}
/*-----------------------------------------------------------------------------

FUNCTION: TTYEnterProc(HWND, UINT, WPARAM, LPARAM)

PURPOSE: Window Procedure to process message for the TTY Enter Dialog

PARAMETERS:
    hWnd    - window handle
    message - window message
    wParam  - window message parameter (depends on message)
    lParam  - window message parameter (depends on message)
Items:
    503: Combo Box
    504: Enter

HISTORY:   Date:      Author:     Comment:
           10/27/95   AllenD      Wrote it

-----------------------------------------------------------------------------*/
int WINAPI TTYEnterProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    HWND    hControl;
    char    *pStr;
    int     k,retval,nBytes;

#if EASYWINDEBUG & 0
TraceWinMessages("Enter",msg,wParam,lParam);
#endif

    switch(LOWORD(msg))
    {

        //========
        case WM_INITDIALOG:                                     // Initialize
        //========
            SendMessage(GetDlgItem(hWnd, 503), WM_SETFONT, (WPARAM)ghFontStatus, 0);
            retval=FALSE;
        break;

        //========
        case WM_CLOSE:          // Hide ourselves
        //========
        break;

        //========
        case WM_SIZE:
        //========
        {
            RECT ScrRect;
            HWND Item50x;
            int dx,dy;
            int sx,sy;

            GetClientRect(hWnd,&ScrRect);       // our client area

            // adjust enter button
            Item50x = GetDlgItem(hWnd, 504);
            dy = ScrRect.bottom-ScrRect.top-4;  // 2 pixels from the frame
            if(dy <= 8) dy = 8;
            sx = ScrRect.left+2;                // About here for a left
            dx = dy;
            sy = ScrRect.top+2;
            MoveWindow(Item50x,
                sx,sy,
                dx,dy,
                TRUE);

            // adjust combo box
            Item50x = GetDlgItem(hWnd, 503);
            dy = ScrRect.bottom-ScrRect.top-4;  // 2 pixels from the frame
            if(dy <= 8) dy = 8;
            sx = ScrRect.left+28;               // About here for a left
            dx = ScrRect.right-sx-2;
            sy = ScrRect.top+2;
            MoveWindow(Item50x,
                sx,sy,
                dx,dy,
                TRUE);

#if EASYWINDEBUG & 0
GetWindowRect(hWnd,&ScrRect); // graphics rescangle
printf("dialog (%d,%d) (%d,%d)\n",ScrRect.left,ScrRect.top,ScrRect.right,ScrRect.bottom);
GetWindowRect(hWinInput,&ScrRect); // graphics rescangle
printf("combo (%d,%d) (%d,%d)\n",ScrRect.left,ScrRect.top,ScrRect.right,ScrRect.bottom);
#endif
        }
        break;

        //========
        case WM_SETFONT:
        //========
//            SendDlgItemMessage(hWnd,201,WM_SETFONT,(WPARAM)muFont,1);
        break;

        //========
        case WM_ACTIVATE:       // we're becomming active/inactive
        //========
            if(wParam==1)
            {
                hControl=GetDlgItem(hWnd,503);
                SetFocus(hControl);
            }
        break;

        //========
        case WM_COMMAND:                                       // from our controls
        //========
            switch(LOWORD(wParam))
            {
               //==================
               case IDCANCEL:
               //==================
               break;

               //==================
               case IDOK:
               case 504:       //send the line  Button does not add <CD>
               //==================
                    ParseEnterLine(hWnd,0);  // no <CR>
//                    ParseEnterLine(hWnd,1);  // yes <CR>
#if EASYWINDEBUG & 0
TraceWinMessages("Enter 504",msg,wParam,lParam);
#endif

                break;

               //==================
               case 503:       //From our combo box a <CR> was hit
               //==================
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

/*---------------------------------------------------------------------------
*  static int  ClientRouter(void* lpV)
*
*  Description:
*   GTK Router for the client
*
*  Parameters:
*     none
*
*   Returns
*       1
*
*  Comments
*   This is different from the W32 because it is a subroutine called in the
*   main context and is NOT a thread.
*
*   It polls the input queue for packets.  If there is a packet, it calls into
*   the routines to handle them.  Importantly, most of the calling is done when
*   GIO does a callback from an asynchronous read or write routine
*
*  History:   Date       Author      Comment
*  24-Feb-2012 jlp
*   initial pass
*
*---------------------------------------------------------------------------*/
#if USEGDK
void WmuxRouterGTK(void* lpV)
{
    BOOL fDone = FALSE;
    t_ctldatapkt* qDpkt;
    t_ctlreqpkt* qpkIn;
    t_dqueue* qe;
    int     k;
    int     retval;


#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_WINMUX)
{
printf("WmuxRouterGTK started\n");
}
#endif

    WmuxActive=1;

    while(!fDone)
    {

        qpkIn = AcceptCtlMail(&WmuxInMail);

#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_WINMUX)
{
jlpDumpCtl("WmuxRouterGTK: qpkIn ",qpkIn);
}
#endif

        if(qpkIn->tag == CTLREQMSGTAG_PDATAREQ)
        {
            DoWCtlReq(NULL, 0, (long)qpkIn);
        }
        else
        {
            ReleaseCtlPacket(qpkIn,&gRouterCS);
        }
    }

    //======== Shutting down a thread
    FlushCtlMail(&WmuxInMail);

    ThreadReturn(0);
}

/*---------------------------------------------------------------------------
*  int InitWrte(pJfunction packetToLocal,pJfunction msgToLocalToMain)
*
*  Description:
*   Set up the WinMux
*
*  Parameters:
*     t_ctlreqpkt*   pCpkt      input control packet
*     int*           wID        index into packet table
*     int*           wTID       starting TID
*
*   Returns
*
*  Comments
*   Initialize the secondary mux and spawn the thread
*
*
*  History:   Date       Author      Comment
*  24-Feb-2012 jlp
*   initial pass
*
*---------------------------------------------------------------------------*/

int InitWrte()
{
    int k;
    int retval = 1;

    // clear client memory

    // set router up
    WmuxActive=0;

//    initDqueue(&(RouteInQHeader.link),NULL);
//    InitSemaphore(&RouteInputSem);
    InitCtlMailBox(&WmuxInMail);

    SpawnThread(WmuxRouterGTK,0,NULL);
    return(retval);
}
#endif

