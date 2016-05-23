/*
**************************************************************************
* FILE: muxevb.c
*
* 32 BIT WINDOWS COM DRIVER to drive the evb under muxvtty
*
* Description
*
*   Adapted from ASYNW32.c
*
*   Data to the EVB
*   This implements the API for the EVB (com port). It takes a form
*   that has become fairly regular in this application.  At the
*   input to the EVB is a queue of t_ctlreqpkt packets and a semaphore.
*   To transmit a packet, SendFrameToEVB is called.  It enqueues the
*   control packet (t_ctlreqpkt*) and releases the semaphore (MS terminology
*   to signal).  A thread is waiting on this (and other).  It will dequeue
*   the control packet.  It then examines the frame and does one of three
*   things
*   1) Get the frame data and use WriteFile to send it
*       1a) Release the control packet
*       1b) (after data is sent) release the data frame (discard)
*   2) If the frame data is intended for the UnFramed address destination,
*       2a) DeFrame the data into a buffer
*       2b) Send the DeFrames data with WriteFile
*       2c) Release control packet
*       2d) Release both data and temp packets
*   3) If the frame data is intended for the download channel ???
*
*   Data from the EVB
*   Data is treated as a stream.  It is run through the standard
*   monitor. If a frame is detected, it is forwarded to the router.
*   Any unframed data is reframed, marked with the address TID_UNFRAMED
*   and send off to the router.
*
*   Event from the system.
*   A shutdown event is recognized.  The port is closed and a flag is
*   cleared to mark the port as closed.
*
*   2014-dec-28
*   - Some pretty big changes are needed to include the Lapj.  The primary
*     issue is the event notifications..  Windows WaitForMultipleObjects
*     might be very useful.  We can convert the Lapj messages to events
*     and call then as needed.  For example, the WriteComplete maps directly
*     to a DoLapj call.  The input is a bit more tricky, We have to map the
*     entire MvspScan call to a conditional event.  It cannot be called when
*     the uart is full but I think there is logic sor a similiar condition built
*     into the old implementation. Lets see,
*     LAPJCMD_UARTEMPTY: Some testing of flags
*     LAPJCMD_TXNEWDATA: scanning input, some work needed
*     LAPJCMD_RXNEWDATA: call to enqueue control. unframed data?
*     LAPJCMD_TIMER: direct call
*     LAPJCMD_CONFIG: control data
*     LAPJCMD_STAT: control data
*     LAPJCMD_CONNECT: ?
*     LAPJCMD_DISC: ?
*     LAPJCMD_PAUSE: ?
*     LAPJCMD_RESUME: ?
*     LAPJCMD_CANACCEPTTX: service call
*
*   - might have to add special software to handle the IRQ7 The
*     monitor must be disabled, TX/RX set to clear and the state
*     changed.  We might be able to integrate this into Lapj with
*     a state change instead.  But there needs to be a mechanism to
*     resume.  In the EVB, we use a "G" command but sometimes it fails
*     when interrupts are live.  Have to think about this from the user
*     level first before making software changes.
*
*   - The broadcast channel is currently handles by muxwrte.c::doBroadcastPkt.
*     I'm not sure how it operates. Do all threads get the message? Where is
*     the test? I forget.
*
**************************************************************************
*/

#include "jtypes.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <fcntl.h>   /* File control definitions */
#include <stdlib.h>
#include <stdio.h>   /* Standard input/output definitions */
#include <string.h>  /* String function definitions */
#include <unistd.h>  /* UNIX standard function definitions */
#include <errno.h>   /* Error number definitions */
#include <termios.h> /* POSIX terminal control definitions */
#include <sys/ioctl.h>
#include <pthread.h>    /* threads */

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
#include "muxctl.h"
#include "framer.h"
#include "muxevb.h"
#include "muxevbW.h"
#include "bctype.h"
#include "jsk_buff.h"
#include "lapj.h"

#if USEWIN32
#include "mttty.h"
#endif

#define STATUS_CHECK_TIMEOUT 5000
#define INVALID_HANDLE_VALUE 0

HANDLE hEVB;
DWORD  dwEVBID;

struct AsynWinComm ComPortA;                    // Port structure
struct S_SIMECC LapjPortA;

int EVBThreadLoopB(LPVOID lpV);
int EVBThread(LPVOID lpV);

extern void FatalError(char*);
extern void ErrorInComm(char * szMessage);
extern void jlpByteStreamOut(char* title, char* pp, int len);
extern int EVBProbeForEcho(struct S_EvbThreadvars* pT);

int EVBActive = EVBNOTACTIVE;
t_jdevice devEVB;                               // Device structure

static HANDLE EVBopen(struct AsynWinComm *ThePort, char *WhichPort, int TheRate, int TheFlow);
static int EVBSetBF(t_AsynWinComm *ThePort, int TheRate, int TheFlow);

static int jsetparam(struct S_jdevice* pMe, APARAM wparam, APARAM lparam);
static int jgetparam(struct S_jdevice* pMe, APARAM wparam, APARAM lparam);
static int jdocmd(struct S_jdevice* pMe, APARAM wparam, APARAM lparam);
static int jdowctlpkt(struct S_jdevice* pMe, APARAM wparam, APARAM lparam);
static int jdosigquit(struct S_jdevice* pMe, APARAM wparam, APARAM lparam);
static void docallback(t_AsynWinComm *TheComm, APARAM wparam, APARAM lparam);

FILE*   StreamDbug = NULL;
char    cStreamfile[]="jlpstream.txt";

static int jdonull(struct S_jdevice* pMe, APARAM wparam, APARAM lparam)
{
    return(0);
}
/*
**************************************************************************
*
*  Function: AsyncInit(void);
*
*  Purpose: Initialize global objects
*
*  Comments:
*
*
**************************************************************************
*/

int newEVB(t_jdevice* pDev, pJfunction cb)
{
    int     retval = 1;
    int     k;
    int     nPriv;
    t_AsynWinComm* pP;
    
#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_ROUTER)
{
printf("newEVB\n");
}
#endif

    //======== Set indirect calls
    pDev->state = 0;
    pDev->cmd = jdocmd;
    pDev->setparam = jsetparam;
    pDev->getparam = jgetparam;
    pDev->callback = cb;
    pDev->wctlpkt = jdowctlpkt;
    pDev->sigquit = jdosigquit;

    //======== Allocate working memory
    nPriv = sizeof(t_AsynWinComm);
    pP = (t_AsynWinComm*)malloc(nPriv);
    pDev->pPriv = pP;
    memset(pP,0,nPriv);
    pP->pParent = pDev;                         // reverse index

    //======== default parameters
    pP->comrate = 19200;
    pP->comflow = NOFLOW;
    strcpy(pP->cCOMNAME,"/dev/ttyS0");
    pP->PortId = INVALID_HANDLE_VALUE;

    k=socketpair(AF_LOCAL, SOCK_DGRAM+SOCK_NONBLOCK, 0, pP->cmdsock);

    //======== clear input queue
    initDqueue(&(pP->InQHeader.link),NULL);           // remember to add locking
    pP->pRecvQ = NULL;
    pP->pXmitQ = NULL;


    EVBActive = EVBNOTACTIVE;

    return retval;
}

/************** Interface handlers **************************************/

/*
**************************************************************************
*
*  FUNCTION: static int jdowctlpkt(struct S_jdevice* pMe, APARAM wparam, APARAM lparam)
*
*  PURPOSE: callback to enqeue a packet
*
*  ARGUMENTS:
*   pMe:        Pointer to device structure for me
*   wparam:     qpkin
*   lparan:     NA
*
*  Comments:
*   This is called by the user and runs in his context
*
*   We enqueue the packet in our private storage and hit the semaphore
*
**************************************************************************
*/
static int jdowctlpkt(struct S_jdevice* pMe, APARAM wparam, APARAM lparam)
{
    int retval = 1;
    t_ctlreqpkt* qpkIn;
    t_AsynWinComm* pS;
    char    msg[4];


    if(!pMe)
        return 0;

    pS = (t_AsynWinComm*)(pMe->pPriv);
    if(!pS)
        return 0;

    qpkIn =(t_ctlreqpkt*)wparam;

    LockSection(&gRouterCS);
    if(ENABLELAPJ > 0){
       // what to do here?
        enqueue(&(pS->InQHeader.link),&(qpkIn->link),NULL); // link it in
        msg[0] = EVBCMD_FROMROUTER;
        write(pS->cmdsock[0],msg,1);
    }else{
        enqueue(&(pS->InQHeader.link),&(qpkIn->link),NULL); // link it in
        msg[0] = EVBCMD_FROMROUTER;
        write(pS->cmdsock[0],msg,1);
    }
    UnLockSection(&gRouterCS);

    return retval;
}
/*---------------------------------------------------------------------------
*  FUNCTION: static int jdosigquit(struct S_jdevice* pMe, APARAM wparam, APARAM lparam)
*
*  PURPOSE:
*   Free resources for quit
*
*  Parameters:
*
*   RETURN
*       NULL ON ERROR
*       1 otherwise
*
*  Comments
*   malloc is called to get working sructure for the device
*   all fields are defaulted
*
*  History:   Date       Author      Comment
*  09-DEC-2012 jlp
*   initial pass
*
*---------------------------------------------------------------------------*/
static int jdosigquit(struct S_jdevice* pDev, APARAM wparam, APARAM lparam)
{
    int retval = 1;
    t_AsynWinComm* pP;
    int     nPriv;

    //======== Set indirect calls
    pDev->state = 0;
    pDev->cmd = jdonull;
    pDev->setparam = jdonull;
    pDev->getparam = jdonull;
    pDev->callback = jdonull;
    pDev->wctlpkt = jdonull;
    pDev->sigquit = jdonull;

    if(!pDev->pPriv)
        return 0;

     pP = pDev->pPriv;

    //========
    pDev->state = 0;

    if(!pDev->pPriv)
        return 0;

    pP = (t_AsynWinComm*)(pDev->pPriv);


    if(pP->PortId > 0)
    {
        close(pP->PortId);
        pP->PortId = 0;
    }
    if(pP->cmdsock[0] > 0)
    {
        close(pP->cmdsock[0]);
        pP->cmdsock[0] = 0;
    }
    if(pP->cmdsock[1] > 0)
    {
        close(pP->cmdsock[1]);
        pP->cmdsock[1] = 0;
    }


    free(pP);

    return retval;
}

/*
**************************************************************************
*
*  FUNCTION: static int jdocmd(struct S_jdevice* pMe, APARAM wparam, APARAM lparam);
*
*
*  Purpose: do a user command
*
*  Comments:
*   This is called by the user and runs in his context
*
**************************************************************************
*/
static int jdocmd(struct S_jdevice* pMe, APARAM wparam, APARAM lparam)
{
    int retval = 0;
    U32 m = (U32) wparam;
    t_AsynWinComm* pS;

    if(!pMe)
        return 0;

    pS = (t_AsynWinComm*)(pMe->pPriv);

    if(!pS)
        return 0;

    switch(m)
    {
        //========
        case EVBcmdOpen:
        //========
        {
            EVBopen(pS, pS->cCOMNAME, pS->comrate, pS->comflow);
            retval = (int)pS->PortId;
            if(retval == (int)INVALID_HANDLE_VALUE)
                retval = 0;
#if (EASYWINDEBUG>5) && 1
if( (inicfg.prflags & PRFLG_OTHER) || 0)
{
printf("After retyrn idComDev %08x\n",pS->PortId);
}
#endif
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
/*
**************************************************************************
*
*  Function: static int jsetparam(pMXTask me, U32 wparam, U32 lparam)
*
*
*  Purpose: Sets parameters from the user
*
*  Comments:
*   This is called by the user and runs in his context
*
**************************************************************************
*/
static int jsetparam(struct S_jdevice* pMe, APARAM wparam, APARAM lparam)
{
    int retval = 1;
    U32 m = (U32) wparam;
    t_AsynWinComm* pS;

    if(!pMe)
        return 0;

    pS = (t_AsynWinComm*)(pMe->pPriv);

    if(!pS)
        return 0;

    switch(m)
    {
        //========
        case EVBParamFlowBrate: // Flow|Rate
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

/*
**************************************************************************
*
*  Function: static int jgetparam(pMXTask me, U32 wparam, U32 lparam)
*
*
*  Purpose: Gets parameters for the user
*
*  Comments:
*   This is called by the user and runs in his context
*
*
**************************************************************************
*/
static int jgetparam(struct S_jdevice* pMe, APARAM wparam, APARAM lparam)
{
    int retval = 1;
    U32 m = (U32) wparam;
    t_AsynWinComm* pS;

    if(!pMe)
        return 0;

    pS = (t_AsynWinComm*)(pMe->pPriv);

    if(!pS)
        return 0;

    switch(m)
    {
        //========
        case EVBParamFlowBrate:
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


/*
**************************************************************************
*
*  Function: int EVBopen(t_AsynWinComm *ThePort, char *WhichPort, int TheRate, int TheFlow);
*
*  Purpose: Open a COM port
*
*  Comments:
*
*   WhichPort = "COMx:"
*   TheRate = 4800,9600,14400 etc.
*   TheFlow can be none || hardware || software
*
*
* The below is now somewhat obsolete because the driver now uses
* a callback function.
*
*   The caller is expected to initialize the 5 fields for event information
*
*       TheWindow, HWND to send events to
*       RxEvent, RxEvent ID, the event and the wParam when a line is received
*       ErrorEvent, ErrorEventId the event to send when and error is received
*
*       WM_COMMAND,WM_COMMNOTIFY,8 are send as part of the processing
*
*   Sample call

    TheComPort.TheWindow=mymainwindow;
    TheComPort.RxEvent=EV_MYINTERNAL;
    TheComPort.RxEventId=EVENT_RXLINE;
    TheComPort.ErrorEvent=EV_MYINTERNAL;
    TheComPort.ErrorEventId=EVENT_RXOV;;

    tmp=copen(&TheComPort,"COM1:",4800,0);
    if(tmp<0)
    {
        MessageBox(hWnd,"Check if allready running","Port in Use",
            MB_TASKMODAL|MB_ICONSTOP|MB_OK);
        return(FALSE);
    }
	//EnableCommNotification(TheComPort.PortId,mymainwindow,1,-1);
*
*   This should be very close to the last initialization the user does
*   before ready to parse messages because interrupts go live at this time
*
*
* we need to rework this to make it more general. It is gradually getting
* there although.
*
* Things to do:
*
* 1)    get rid of older baggage **
* 2)    place current globals into the structure **
* 3)    Set up default uses
* 3a)     character Interactive: fast turn single character
* 3b)     Block CR/LF
* 3c)     Flow control auto
* 3d)     68302 style
* 3e)     autobauder
* 4)    Protocol
* 4a)     ENQ/ACK
* 4b)     SOH/ETX
*
*
**************************************************************************
*/
static struct termios termios_sav;         // used by evb to save port attributes

HANDLE EVBopen(t_AsynWinComm *ThePort, char *WhichPort, int TheRate, int TheFlow)
{

    int     fd; /* File descriptor for the port */
    struct termios termios_p;
    int     termios_f;
    int     modemstatus;

#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_EVB)
{
printf("Start EVBopen\n");
}
#endif

    fd = open(WhichPort, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd < 0){
        perror("open_port: Unable to open port - ");
    }else{
        // Manage port
        //fcntl(fd, F_SETFL, 0);

        // Get the attriutes
        tcgetattr(fd, &termios_sav);        // Save current settings
        termios_p = termios_sav;

//        displaytermios(&termios_p);
        cfmakeraw(&termios_p);              // Raw I/O
        cfsetspeed(&termios_p, B19200);     // speed
        termios_p.c_cflag &= ~CRTSCTS;      // HW flow off

//        displaytermios(&termios_p);
        tcsetattr(fd, TCSANOW, &termios_p); // update

        ioctl(fd, TIOCMGET, &modemstatus);  // get rs232 signals
        printf("ModemStatus 0x%02x\n",modemstatus);

    }

#if (LAPJSTREAM>0) && 1
    if( (cStreamfile==NULL)|| (strlen(cStreamfile) <=0) )
    {
        StreamDbug = stdout;
    }
    else
    {
        StreamDbug = fopen(cStreamfile,"w");
    }
#endif

    ThePort->PortId = fd;
    ThePort->pRecvQ = NULL;
    ThePort->pXmitQ = NULL;

    hEVB = (HANDLE)SpawnThread((void*)EVBThread, 0, (void*)ThePort);

//    if (hEVB == NULL)
//        ShowDialogError("EVBopen:CreateThread Fail");

#if EASYWINDEBUG & 0
printf("hEVB %08x %08x\n",hEVB,dwEVBID);
#endif

#if EASYWINDEBUG & 0
printf("Before return idComDev %08x\n",ThePort->PortId);
#endif

//    CheckDlgButton(hStartDialog,IDC_STUPEvb,BST_CHECKED);
#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_EVB)
{
printf("EVBopen Port = %d hEVB = 0x%06x\n", fd, hEVB);
}
#endif

	return(hEVB);

}

int restore_port(int fd)
{
    tcsetattr(fd, TCSANOW, &termios_sav);
}


/*
**************************************************************************
*
*  Function: int EVBSetBF(t_AsynWinComm *ThePort, int rate,int flow)
*
*
*  Purpose: Called to Change the rate and flow parameters
*
*  Comments:
*
*   Right now, only the rate and flow are changed. So the other parameters
*   are not changed.
*
*   If the bit rate is -1, then no change is made
*   If the flow is -1 then no change is made
*
**************************************************************************
*/
static int EVBSetBF(t_AsynWinComm *ThePort, int TheRate, int TheFlow)
{
    HANDLE idComDev;
    struct termios termios_p;
#if 0
    // Quiet return if we don't have a comm device
    idComDev = ThePort->PortId;
    if(idComDev == INVALID_HANDLE_VALUE)
        return(0);
        
    tcgetattr(fd, &termios_p);        // Save current settings

    if(TheRate > 300)
        dcb.BaudRate=TheRate;       //OpenComm defaults to 9600

    switch(TheFlow)
    {
        case NOFLOW:
            dcb.fRtsControl=RTS_CONTROL_ENABLE; //Turn RTS on
            dcb.fOutxCtsFlow=0;
            dcb.fOutX=0;         // no XON/XOFF
            dcb.fInX=0;          // no XON/XOFF
        break;

        case HARDWAREFLOW:
            dcb.fRtsControl=RTS_CONTROL_TOGGLE; //RTS is flow
            dcb.fOutxCtsFlow=1;  // CTS is flow
            dcb.fOutX=0;         // no XON/XOFF
            dcb.fInX=0;          // no XON/XOFF
            dcb.XoffLim=8192-TheRate/100;
            dcb.XonLim=TheRate/100;
        break;
        case SOFTFLOW:
            dcb.fRtsControl=RTS_CONTROL_ENABLE; //Turn RTS on
            dcb.fOutxCtsFlow=0;
            dcb.fOutX=1;         // enable XON/XOFF
            dcb.fInX=1;          //enable XON/XOFF
            dcb.XoffLim=8192-TheRate/100;
            dcb.XonLim=TheRate/100;
        break;
        default:
        break;
    }
    SetCommState(idComDev,&dcb);
#endif

    return(0);
}

/*
**************************************************************************
*
*  Function: int EVBSendCmd(struct S_evbcmds* TheCmd)
*
*
*  Purpose: Send an EVB command
*
*  Comments:
*   Called to frame and send an EVB command.  It is used to
*   change the rate and flow settings for the EVB.  A command
*   is needed because the EVB server could be remote from
*   a client but the bit rate needs to be set.  This is generalized
*   for the case where other communications with the EVB driver is
*   needed.
*
**************************************************************************
*/
static int EVBSendCmd(struct S_evbcmds* TheCmd)
{
    t_ctldatapkt*   pAck;
    char* pHdr;

    pAck = ReframeData((char*)TheCmd, sizeof(struct S_evbcmds), TID_EVB, SEQNUM_DEFAULT);
    if(!pAck)
    {
        ShowDialogError("EVBSendCmd:AllocDataPacket fail 1");
    }
    else
    {
        SendFrameToRouter(pAck, ROUTESRCLOCAL);
    }
    return(1);
}

/*
**************************************************************************
*
*  Function: int EVBExecCmd(struct S_evbcmds* TheCmd)
*
*
*  Purpose: Execute a received EVB command
*
*  Comments:
*   Dalled to execute an internal command received. Primarily
*   intended for a changing the bit rate from a remote client.
*   We support Bit rate and flow control
*
**************************************************************************
*/
static int EVBExecCmd(t_AsynWinComm* TheComm, t_ctldatapkt* txdatapkt)
{
    struct S_evbcmds* TheCmd;
    t_ctldatapkt* pPayload;
    int retval;

    pPayload = (t_ctldatapkt*)DeFramePacket(txdatapkt,NULL,NULL);
    if(!pPayload)
    {
        ShowDialogError("EVBExecCmd:DeFramePacket fail 1");
        ReleaseDataPacket(txdatapkt,&gRouterCS); // Release old
        return(FALSE);
    }

#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_EVB)
{
jlpDumpFrame("EVBExecCmd:Payload",pPayload->pData,pPayload->nData);
}
#endif

    if(pPayload->nData == sizeof(struct S_evbcmds))  // valid sized frame
    {
        TheCmd = (struct S_evbcmds*)(pPayload->pData);
        switch(TheCmd->msg)
        {
            //======== force an IRQ7
            case EVBCMD_IRQ7:
            //========
            {
                char tmpbuf[4];
                tmpbuf[0] = XIRQ7;
                write(TheComm->PortId,tmpbuf,1);
            }
            break;

            //========
            case EVBCMD_BRATE:
            //========
            {
            }
            break;

            //========
            case EVBCMD_FLOW:
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
        
        retval = TRUE;
    }
    else
    {
        retval = FALSE;
    }


    ReleaseDataPacket(txdatapkt,&gRouterCS);    // Release the input packet
    ReleaseDataPacket(pPayload,&gRouterCS);     // Release the deframed packet
    return retval;
}

/*
**************************************************************************
*
*  Function: static BOOL DoStartRead(t_AsynWinComm* pAsyn)
*
*  Purpose: Reads the current buffer and start a new one
*
*  Arguments:
*   t_AsynWinComm* pAsyn   Structure pointer
*
*  Comments:
*   In Linux, this is the read call, there are no delays
*   A return value
*       TRUE:   delayed read is pending
*       FALSE:  no delayed read is pending
*
*   Errors use FatalError as an exception
*
**************************************************************************
*/
static BOOL DoStartRead(struct AsynWinComm* TheComm, struct S_EvbThreadvars* pT)
{
    int     dwRead;
    int     k;
    BOOL    retval;
    BOOL    readflag;
    char*   bufin = TheComm->rxbuf;
    struct S_SIMECC* pEcc = pT->pEcc;

    
    // The comm driver has internel buffers (see SetupComm)
    // So this routine is more of an editing container

    dwRead=read(TheComm->PortId, bufin, MAXDATASIZE);
#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_EVB)
{
printf("DoStartRead:Port 0x%08x readflag %d dwRead %d\n",TheComm->PortId,readflag,dwRead);
jlpDumpFrame("SRP=",bufin,dwRead);
}
#endif
    if(dwRead < 0)
    {
        k=errno;
        if( (k == EAGAIN) || (k==EWOULDBLOCK) )	// select should not allow this
        {                                       // ignore
        }
        else
        {
            ShowDialogError("DoStartRead:");    // file system error
        }
            retval = TRUE;                      // yep, it is started and pending
    }
    else                                        // read completed immediately
    {
       if (dwRead>0)                            // Are there any characters?
       {
#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_EVB)
{
printf("DoStartRead:Port 0x%08x readflag %d dwRead %d\n",TheComm->PortId,readflag,dwRead);
jlpDumpFrame("SRP=",bufin,dwRead);
}
#endif
#if (LAPJSTREAM>0) && 1
if((inicfg.prflags & PRFLG_EVB) || (inicfg.prflags & PRFLG_LAPJ))
{
jlpByteStreamOut("fe",bufin, dwRead);
}
#endif

            if(ENABLELAPJ > 0){
                DoLapj(pEcc, LAPJCMD_RXNEWDATA, (intptr_t)bufin, (intptr_t)dwRead );
            }
            else
            {
                HandleRxOutbound((intptr_t)(pT),(intptr_t)bufin, (intptr_t)dwRead);
            }
            pT->fWaitingOnRead = FALSE;
       }
       retval = FALSE;                      // No pending read
    }
    
    return retval;
}

/*
**************************************************************************
*
*  Function: static BOOL CheckACK(t_AsynWinComm* pAsyn, int raddr, int rseq)
*
*  Purpose: Check id ACK needed ans send
*
*  Arguments:
*   t_AsynWinComm* pAsyn   Structure pointer
*   int                 raddr   remote address
*   int                 rseq    remote seq
*
*  Comments:
*   Errors use FatalError as an exception
*
**************************************************************************
*/
static void CheckACK(t_AsynWinComm* pAsyn, int raddr, int rseq)
{
    t_ctldatapkt*   pAck;
    char buf[8];

#if (EASYWINDEBUG>5) && 1
printf("CheckACK A: %02x %02x\n",raddr&0xff, rseq&0xff);
#endif
    if(raddr & WWANTACK)
    {
        buf[0] = raddr;
        buf[1] = rseq;
        strcpy(&(buf[2]),"ACK");
        pAck = ReframeData(buf, 2, raddr, rseq);
        if(!pAck)
        {
            ShowDialogError("CheckACK:AllocDataPacket fail 1");
        }
        else
        {
            SendFrameToRouter(pAck, ROUTESRCEVB+0x400);
#if (EASYWINDEBUG>5) && 1
printf("CheckACK: ACKsent %02x %02x\n",raddr&0xff, rseq&0xff);
#endif
        }
    }
}
/*
**************************************************************************
*
*  Function: static BOOL DoStartWrite(t_AsynWinComm* pAsyn)
*
*  Purpose: Gets a packet from the queue and starts a write
*
*  Arguments:
*   t_AsynWinComm* pAsyn   Structure pointer
*   OVERLAPPED* osWrite         For the MS overlapped write
*
*  Return
*       TRUE:   delayed write is pending
*       FALSE:  no delayed write is pending
*
*  Comments:
*   This reads the input queue for a new packet
*   Read the queue
*       If the queue is empty, it returns false
*   If the queue has stuff
*   A) Dereference to the data packet
*   B) release the control packet
*   C) Switch on contents of data
*       C.1 Empty: release data packet
*       C.2 Unframed
*           Get empty packet
*           Deframe to create New data
*           copy some information (flow control)
*           Release old data
*       C.3 Else (is framed)
*   D) Send the packet
*       D.1 Was sent
*           Release packet
*           return not waiting
*       D.2 Is pending
*           Return am waiting
*
*
*   Errors use FatalError as an exception
* 2014-dec-29 3:30pm
*   Broke up. Still working.
*   Did a download and run of rbot40.s37 Both work. rbot40.s37  download had
*   CPS of 1888, runs, sends periodic messages to mxvsp-pc, they work
*   enabled evb event reports on pc, everything looks good.  So
*   it looks like we can use the two functions independantly.
*   I note below, EvbFetchPacket checks the broadcast address
*   and executes a command.  See below
*
* 2014-dec-28 2:30pm
*   We need to break this up.  One section gets a packet from the
*   queue, checks for unframed and (if needed) creates a raw buffer.
*   The second section will take a raw buffer and write it to the Uart using
*   the Windows WriteFile.
*   Apparently, a t_ctldatapkt is stored in TheComm->pXmitQ in the first part
*   then this is sent in the second part. Conceptually we just need to break
*   this into two function calls and test the pair.  We have to be careful of
*   the ACK bit because it is flow control
*
**************************************************************************
*/

/*
**************************************************************************
*
*  Function: BOOL EvbFetchPacket((struct AsynWinComm* TheComm, OVERLAPPED* osWrite)
*
*  Purpose: Fetch the packet
*
*  Arguments:
*   struct AsynWinComm* pAsyn   Structure pointer
*   OVERLAPPED* osWrite         For the MS overlapped write
*
*  Output  TheComm->pXmitQ
*
*  Comments:
*   Keep the same arguments but move the functions
*
* 2016-4-30 jlp
*   - convert to Linux
*   -This one is long, it basically has to check for unframed , framed and internal
*
* 2014-dec-29 3:45pm
*   After moving, I see that this routine will fetch and execute a broadcast command.
*   when it does so, it will return FALSE so when we use this, it is possible
*   the queue is full but there is no data to send.  Somehow we have to work this
*   case into our interface routine that tries to enqueue packets.
*
**************************************************************************
*/
BOOL EvbFetchPacket(struct AsynWinComm* TheComm)
{
    DWORD   dwWritten;
    BOOL    retval;
    BOOL    bWrite;
    t_dqueue* qe;
    t_ctlreqpkt* txctlpkt;
    t_ctldatapkt* txdatapkt;
    t_ctldatapkt* newdpkt;
    char*   pTxd;
    int     nTxd;
    int     aDst;
    


    //======== dequeue a control packet
    qe = dequeue(&(TheComm->InQHeader.link),&gRouterCS);      // get the queue entry
    if(!qe)
    {                                  // none
#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_EVB)
{
printf("EvbFetchPacket: Empty\n");
}
#endif
        TheComm->pXmitQ = NULL;
        return(FALSE);
    }

    txctlpkt = structof(t_ctlreqpkt,link,qe);   // get the control packet
#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_EVB)
{
jlpDumpCtl("EvbFetchPacket:",txctlpkt);
}
#endif
    txdatapkt = txctlpkt->pData;                // and the data packet
    ReleaseCtlPacket(txctlpkt,&gRouterCS);      // release the control packet
    
    //======== Check the data
    nTxd = txdatapkt->nData;                    // count

    if(nTxd <= 0)                               // Empty?
    {
#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_EVB)
{
printf("EvbFetchPacket: Length=0\n");
}
#endif
        ReleaseDataPacket(txdatapkt,&gRouterCS); // Release it
        TheComm->pXmitQ = NULL;
        retval = FALSE;
        return retval;
    }
    else
    {
        pTxd = txdatapkt->pData;                    // data
        aDst = (pTxd[2]&0x7f);
    }
    
#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_EVB)
{
printf("EvbFetchPacket: aDst = 0x%x\n",aDst);
}
#endif
    
    //========
    if(aDst == TID_UNFRAMED)                    // Unframed data?
    //========
    {
        t_ctldatapkt*   pPayload;

#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_EVB)
{
printf("EvbFetchPacket: TID_UNFRAMED\n");
}
#endif
        //======== deframe the packet into a new payload
        pPayload = (t_ctldatapkt*)DeFramePacket(txdatapkt,NULL,NULL);
        if(!pPayload)
        {
            ShowDialogError("EvbFetchPacket:AllocDataPacket fail 1");
            ReleaseDataPacket(txdatapkt,&gRouterCS); // Release old
            TheComm->pXmitQ = NULL;
            return(FALSE);
        }

        ReleaseDataPacket(txdatapkt,&gRouterCS); // Release old

        //======== queue for xmit in pXmitQ
        TheComm->pXmitQ = pPayload;             // remember what we send
        pTxd = (char*)(TheComm->pXmitQ->pData);
        nTxd = TheComm->pXmitQ->nData;

        if(nTxd<=0)                             // Final check on 0 length
        {
            ReleaseDataPacket(TheComm->pXmitQ,&gRouterCS); // Release this one
            TheComm->pXmitQ = NULL;
            retval = FALSE;
        }
        else
        {
            retval = TRUE;
        }
    }
    //========
    else if(aDst == TID_EVB)                    // internel command
    //========
    {
        EVBExecCmd(TheComm, txdatapkt);
        retval = FALSE;
    }
    //========
    else if((aDst >= DEVVSP0) && (aDst < (DEVVSP0+TID_MAXDEVVSP))) // data is framed
    {
    //========
#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_EVB)
{
printf("EvbFetchPacket: framed\n");
}
#endif
        //======== queue for xmit in pXmitQ
        TheComm->pXmitQ = txdatapkt;            // remember what we send
        pTxd = TheComm->pXmitQ->pData;
        nTxd = TheComm->pXmitQ->nData;
/*****Note, SrcId and seqNum are NOT allowed to  be control chatacters *****/
        pTxd[2] &= 0x7f;                        // kill any Retransmit bit
        retval = TRUE;
    }
    //========
    else
    //========
    {
        ReleaseDataPacket(txdatapkt,&gRouterCS); // Release it
        TheComm->pXmitQ = NULL;
        retval = FALSE;
    }

    // Ok, can we finally send?
#if (EASYWINDEBUG>5) && 0
if(inicfg.prflags & PRFLG_EVB)
{
printf("Fetch: pXmitQ 0x%06x\n",TheComm->pXmitQ);
}
#endif
        return retval;
}


/*
**************************************************************************
*
*  Function: int EnQueueBufferToLapj(t_SIMECC* pEcc, struct AsynWinComm* TheComm)
*
*  Purpose: enqueue the packet the packet
*
*  Arguments:
*   struct AsynWinComm* pAsyn   Structure pointer
*   OVERLAPPED* osWrite         For the MS overlapped write
*
*  Output  TheComm->pXmitQ
*
*  Comments:
*   Keep the same arguments but move the functions
*
* 2016-4-30 jlp
*   - convert to Linux
*
**************************************************************************
*/
int EnQueueBufferToLapj(t_SIMECC* pEcc, struct AsynWinComm* TheComm)
{
    U8*     pTxd = (U8*)(TheComm->pXmitQ->pData);
    int     nTxd = TheComm->pXmitQ->nData;
    int     retval;

#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_EVB)
{
jlpDumpFrame("EnQueueBufferToLapj(Packet)", pTxd, nTxd);
}
#endif
#if (LAPJSTREAM>0) && 1
if((inicfg.prflags & PRFLG_EVB) || (inicfg.prflags & PRFLG_LAPJ))
{
jlpByteStreamOut("fm",pTxd, nTxd);
}
#endif
    //======== Check for buffer ode (debug)
    if(ENABLELAPJ > 0)
    {
        retval = DoLapj(pEcc, LAPJCMD_TXNEWDATA, (intptr_t)pTxd, nTxd);
    }
    else
    {
        retval = try_writeDev((intptr_t)(TheComm->pThreadVars), (intptr_t)pTxd, nTxd);
    }

    if(TheComm->pXmitQ)
    {
        char raddr,rseq;
        raddr = TheComm->pXmitQ->SrcId;
        rseq = TheComm->pXmitQ->seqNum;
        ReleaseDataPacket(TheComm->pXmitQ,&gRouterCS); // Release this one
        TheComm->pXmitQ = NULL;
        CheckACK(TheComm, raddr, rseq);
    }
    else
    {
        ReleaseDataPacket(TheComm->pXmitQ,&gRouterCS); // Release this one
        TheComm->pXmitQ = NULL;
    }
    return retval;
}
#if 0 // Possibly for transparent mode
BOOL DoStartWrite(struct AsynWinComm* TheComm, OVERLAPPED* osWrite)
{
    BOOL    retval;

    retval = EvbFetchPacket(TheComm);
    if(!retval)
        return retval;

    retval = EvbSendThePacket(TheComm, osWrite);

    return retval;
}
#endif
/*
**************************************************************************
*
*  Function: static BOOL DoDelayedWrite(struct AsynWinComm* pAsyn, OVERLAPPED* osWrite)
*
*  Purpose: Finish a write
*
*  Arguments:
*   struct AsynWinComm* pAsyn   Structure pointer
*
*  Comments:
*   Mostly exists to release packets back into the pool
*   there is a marker for where we can put ack flow control for
*   the file transfer TBD
*
* 2016-5-4 jlp
*   - Added signal for completinf a delayed (partial write)
*
* 2016-4-30 jlp
*   - convert to linux
*     This does the actual write to the uart.  It writes out
*     what ever is left in the buffer If all accepted, it will clear
*     pending write.  The start of the write is in the interface function
*     try_writeDev
*
* 2014-dec-29 3:30pm
*   This needs modification, maybe we just won'tt call it.
*   Instead use a different Routine,  It has to return the Uart
*   status as True
*
**************************************************************************
*/

static BOOL DoDelayedWrite(struct AsynWinComm* TheComm)
{
    int     dwWrite;
    int     nwritten;
    int     savestart;
    t_EvbThreadvars* pT;

    pT = TheComm->pThreadVars;

    //======== check for spurIous call
    if( (pT->nBufHold) == (pT->iBufHold))
    {
        pT->nBufHold = pT->iBufHold = 0;
        pT->fWaitingOnWrite = FALSE;
        return(FALSE);
    }

    //======== Logic for a delayed (partial) complete operation
    savestart = pT->iBufHold;

    //======== write
    dwWrite = (pT->nBufHold) - (pT->iBufHold);
    nwritten = write(TheComm->PortId,&(pT->pBufHold[pT->iBufHold]),dwWrite);

#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_EVB)
{
printf("DoDelayedWrite: write returns %d dwWrite %d\n",nwritten,dwWrite);
}
#endif

    pT->iBufHold += nwritten;

    //======== Check
    if( (pT->iBufHold) >= (pT->nBufHold))
    {
        pT->nBufHold = pT->iBufHold = 0;
        pT->fWaitingOnWrite = FALSE;

        //======== did we complete a parial write?
        if( (pT->iBufHold == 0) && (savestart >0))
        {
            char    cmdbuf[4];
            cmdbuf[0] = EVBCMD_DELAYWRITEDONE;
            write(TheComm->cmdsock[0],cmdbuf,1);
        }

        return(FALSE);
    }
    else
        return TRUE;
}

/*
**************************************************************************
*
*  Function: static BOOL DoDelayedWrite(struct AsynWinComm* pAsyn, OVERLAPPED* osWrite)
*
*  Purpose: Finish a write
*
*  Arguments:
*   struct AsynWinComm* pAsyn   Structure pointer
*
*  Comments:
*   Mostly exists to release packets back into the pool
*   there is a marker for where we can put ack flow control for
*   the file transfer TBD
*
* 2016-4-30 jlp
*   - convert to linux
*     This does the actual write to the uart.  It writes out
*     what ever is left in the buffer If all accepted, it will clear
*     pending write.  The start of the write is in the interface function
*     try_writeDev
*
* 2014-dec-29 3:30pm
*   This needs modification, maybe we just won'tt call it.
*   Instead use a different Routine,  It has to return the Uart
*   status as True
*
**************************************************************************
*/

static BOOL DoEVBCommand(struct AsynWinComm* TheComm, struct S_EvbThreadvars* pT)
{
    int     e;
    int     k;
    BOOL    retval = FALSE;
    char    cmdbuf[4];

//    pT = TheComm->pThreadVars;

    e=read(TheComm->cmdsock[1],cmdbuf,1);      // one byte command
#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_EVB)
{
printf("DoEVBCommand: Sock e=%d cmubuf[0]=%d\n", e, 0xff&cmdbuf[0]);
}
#endif
    if(e != 1)
    {
        printf("EVBThread: bad command A\n");
        ErrorInComm("EVBThread: bad command A");
    }
    else
    {
        switch(cmdbuf[0])
        {
            //======== Router is sending us a packet
            case EVBCMD_FROMROUTER:
            //========
            {
    #if (EASYWINDEBUG>5) && 1
    if(inicfg.prflags & PRFLG_EVB)
    {
    printf("EVBThread:fetch\n", e, 0xff&cmdbuf[0]);
    }
    #endif
                //======== special for disabled LAPJ
                if((ENABLELAPJ==0) && (isTxDevEmpty((intptr_t)(TheComm->pThreadVars))))
                {
                    k = EvbFetchPacket(TheComm);
                }
                else if(DoLapj(pT->pEcc,LAPJCMD_CANACCEPTTX,0,0) > 0)
                {
                    k = EvbFetchPacket(TheComm);
                }
                else
                {
                    k=0;
                }

                if(k)
                {
                    if(ENABLELAPJ > 0)
                    {
                        EnQueueBufferToLapj(pT->pEcc, TheComm);
                    }
                    else
                    {
                        EnQueueBufferToLapj(pT->pEcc, TheComm); // test moved inside subr
                    }
                }

            }
            break;

            //======== Write buffer to the uart
            case EVBCMD_WRITETOUART:
            //========
            {
                DoDelayedWrite(TheComm);
            }
            break;

            //======== Delayed completion of a write for now, same as write: lapj might differ
            case EVBCMD_DELAYWRITEDONE:
            //========
            {
                //======== Queue a phony packet request, Fetch will ignore id=f empty
                char    cmdbuf[4];
                cmdbuf[0] = EVBCMD_FROMROUTER;
                write(TheComm->cmdsock[0],cmdbuf,1);

            }
            break;

            //========
            case EVBCMD_KICK:
            //========
            {
                //k = DoLapj(pEcc, LAPJCMD_KICK, 0, 0);
            }
            break;
            //========
            default:
            //========
            {
                printf("EVBThread: bad command B\n");
                ErrorInComm("EVBThread: bad command B");
            }
            break;
        }
    }
    return (retval);
}



/*
**************************************************************************
*
*  Function: DWORD WINAPI EVBThread(LPVOID lpV)
*
*  Purpose: Comm thread function
*
*  Comments:
*
*  This is the communications thread function. It handles
*  I/O to the comm driver and generates a comm event
*  to the main loop when required. Only one port is
*  currently supported.
*
*   The reading/writing is adapted from MS example code. From the
*   example, one needs to start a read/write an check to see if it
*   completed. If not, one does a wait on an event.  There is a
*   great deal of code here and some confusion.  The timeouts set
*   play a role.  We'll just keep with some previously used ones
*
* 2016-5-08 jlp
*   -Basically working in non-lapj mode.  But there are some
*     pending questions.  If the select is waiting for an input only
*     and the re is a need to start a transmit, how to do this?  The
*     situation will arise when Lapj needs to retransmit.
*     I think the internal "cmdbuf" has to be used because evb allways
*     enables this guy.  But how to kick a transmit?  Well, in Lapj,
*     it will send a new buffer so thats fine.  Or ?is it recursive?
*     When lapj has a buffer, he calls try_writeDev which enqueues it and
*     returns.  So there is not a recursive execution.  Lets try to
*     call LapjEmptied each transmit buffer.
*     How can it arise otherwise?
*
* 2016-4-30 jlp
*   -Linux mode.  The "events" are
*     socketpair[1]] read is the input
*     uart write
*     uart read
*   - This is a bit of a mess, pointers everywhere.
*   we will use the socket as a queued instruction
*       data from lapj comes from try_writeDev call
*       kick from Do_Lapj
*
*
* 2014-dec-29 8:30pm
*   Ok, lets get go nuts in events.  We are allowed 64
*   UartRead Complete (Call Lapj LAPJCMD_RXNEWDATA)
*   UartWrite Complete (just call Lapj LAPJCMD_UARTEMPTY)
*   Timer100ms Call Lapj LAPJCMD_TIMER
*   LapJEmptied This is the kick mux
*   InputPacket This is the read of the interface
*   RunLapj (do we need this)
*   StatusEvent (probably can remove)
*   WaitTimeout
*   ExitEvent
*
* 2014-dec-29 5:50pm
*   External Events
*       Arrival of a packet or a control
*         we have to read the queue before we know if a packet can be sent.
*         So there has to be a storage location and a method to
*         determine if it is ready or not
*       Arrival of a timer
*         Pipe this to Lapj and we can do any other timed stuff
*       Uart signal empty
*         Pipe this to Lapj
*       Uart rx has data
*         Call the monitor
*       Lapj can accept
* second check is buried in mxvspscan
            if( ! DoLapj(pEcc, LAPJCMD_CANACCEPTTX, NULL, NULL))      // Are we able to send?
            //========
            {
                return(0);                      // nope
            }
         move to first check then try to get a packet with
         EvbFetchPacket
         on success, use
         retval = DoLapj(pEcc, LAPJCMD_TXNEWDATA, (intptr_t)(pF->Bufout), (intptr_t)pF->cBufout);

*
* 2014-dec-28
*   - review prior to including Lapj.
*     I think the wait for an event might be ok and alternative is to
*     have an input queue.  We really don't want the windows one ... Unless
*     there is a filter to prevent unwanted system stuff from being posted.
*
**************************************************************************
*/

    struct AsynWinComm *jTheComm;
    struct S_EvbThreadvars* jpT;

int EVBThread(LPVOID lpV)
{

    struct AsynWinComm *TheComm;
    struct S_EvbThreadvars sT;
    struct S_framer UpstreamFrame;
    struct S_EvbThreadvars* pT;

    DWORD      dwRes;           // result from WaitForSingleObject
    DWORD      dwTmp;
    int     k;
    fd_set  rfds;                   // receive file descriptors
    fd_set  tfds;                   // transmit file descriptors
    fd_set  exfds;                  // exception file descriptors
    struct timeval tv;              // timeout for wait
    int     maxfd;                  // temp to file the max
    int     retval;
    int     e;                      // another temp
    char    cmdbuf[4];

    pT = &sT;
    TheComm = (struct AsynWinComm *) lpV;
    memset(pT, 0, sizeof(struct S_EvbThreadvars));
    memset(&LapjPortA,0,sizeof(LapjPortA));
    pT->TheComm = TheComm;
    TheComm->pThreadVars = pT;
    pT->pEcc = &LapjPortA;

    //======== for gdb access
    jTheComm = TheComm;
    jpT = pT;


    pT->pBufHold = malloc(1500);     // Max ethernet size
    pT->nBufHold = pT->iBufHold = 0; // mark empty

#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_EVB)
{
printf("EVBThread Started\n");
}
#endif

    // Clear out UART when thread (me) starts
    pT->fWaitingOnRead = FALSE;
    pT->fWaitingOnStat = FALSE;
    pT->fWaitingOnWrite = FALSE;
    pT->fThreadDone = FALSE;
    EVBActive = EVBONLINE;

    //======== Initialze Lapj
#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_EVB)
{
printf("EVBThread: pT= 0x%06x \r\n",pT);
}
#endif
    InitializeLapj(pT->pEcc, pT);
    pT->pMuxFramer = &UpstreamFrame;
    memset(pT->pMuxFramer,0,sizeof(pT->pMuxFramer));
    lapjInitFramer(&(pT->pEcc->sFRX), 0, NULL, NULL); // initialize mux framer
    lapjInitFramer(&(pT->pEcc->sF), 0, NULL, NULL); // initialize mux framer

    if( (ENABLELAPJ > 0) && (inicfg.autolapj>0) && (EVBProbeForEcho(pT)==0))
    {
        DoLapj(pT->pEcc,LAPJCMD_CONNECT, 0, 0);
    }

#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_EVB)
{
printf("EVBThread:EVBActive = %d\n",EVBActive);
}
sleep(4);
#endif

    while ( !pT->fThreadDone )
    {

        FD_ZERO(&rfds);             // clear our descriptors
        FD_ZERO(&tfds);
        FD_ZERO(&exfds);

        FD_SET(TheComm->cmdsock[1], &rfds);  // socket in
        FD_SET(TheComm->PortId, &rfds);      // UART in
        FD_SET(TheComm->PortId, &tfds);      // UART in

        maxfd = TheComm->cmdsock[1];
        if(maxfd < TheComm->PortId)                // find the max fild descriptor
            maxfd = TheComm->PortId;
        tv.tv_sec = 0;              // 100ms timer
        tv.tv_usec = 100000;

        //======== Wait for an input
#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_EVB)
{
if(e != -2)
    printf("EVBThread:switch in, maxfd= %d\n",maxfd);
}
#endif
//        maxfd = 4;
        if(!(pT->fWaitingOnWrite))
            e = select(maxfd+1, &rfds, NULL, NULL, &tv);
        else
            e = select(maxfd+1, &rfds, &tfds, NULL, &tv);
#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_EVB)
{
if(e != 0)
    printf("EVBThread:switch out, e= %d\n",e);
}
#endif
//        printf("threadConsole:Select returns %d\n",retval);

        //========
        if (e == -1) // Special for error trap
        //========
        {
            perror("C) select()");
        }
        //========
        else if(e > 0) // at least one descriptor
        //========
        {
            //========
            if(FD_ISSET(TheComm->PortId,&rfds)) // Uart in
            //========
            {
#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_EVB)
{
printf("EVBThread: fWaitingOnRead\n");
}
#endif
                DoStartRead(TheComm, pT);
            }
            //======== This is shared in the select, so apply fWaitingOnWrite logic
            else if((pT->fWaitingOnWrite) &&(FD_ISSET(TheComm->PortId,&tfds))) // Uart empty
            //========
            {
                DoDelayedWrite(TheComm); // finish the write
#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_EVB)
{
printf("EVBThread: DoDelayedWrite pT->fWaitingOnWrite %d\n",pT->fWaitingOnWrite);
}
#endif
            }
            //========
            else if(FD_ISSET(TheComm->cmdsock[1],&rfds)) // command socket ?
            //========
            {
                //======== get a command
                DoEVBCommand(TheComm,pT);
            }
            //========
            else    // place holder
            {
                printf("EVBThread.select Bad\n");
                ErrorInComm("EVBThread: WaitForMultipleObjects");
            }
        }
        //========
        else // timeout
        //========
        {
            //======== dec counter
            DoLapj(pT->pEcc, LAPJCMD_TIMER, 0, 0);
            e = -2;
        }

    }

    //
    // close local event handles
    //
    DoLapj(pT->pEcc, LAPJCMD_KILL, (intptr_t)free, 0);
    free(pT->pBufHold);

    return 1;
}

/*
**************************************************************************
*
*  Function: int EVBProbeForEcho(struct S_EvbThreadvars* pT)
*
*  Purpose: Probe for and echo from the evb
*
*  Comments:
*   If the EVB is running in MACSBUG or Linux (or other terminal)
*   it will echo the SAMB back.  Lapj will think it is a race from
*   the other side and proceed to send a UA and think it is connected.
*   we need to probe the other side to see if there is an echo before sending
*   a SABM.
*   Ok, Linux echos all characters except special line edition ones
*   MACSBUG suppresses characters with the high bit set
*   I think LAPJ suppresses all characters, higher layers echo them
*   mxvsp recognizes characters but frames a response.
*
*   Our Lapj needs a a framed packet with bit8 and a UA response to SABM
*
*   Pass1 simple stupid: Send clear uppercase string return if it matches
*
**************************************************************************
*/
static const char Probetext1[]="EPROBE";

int EVBProbeForEcho(struct S_EvbThreadvars* pT)
{
    struct AsynWinComm* TheComm = (struct AsynWinComm*)(pT->TheComm);
    int     dwWrite, dwRead;
    int     nwritten;
    int     retval=1;           // default to true
    int     k;
    fd_set  rfds;                   // receive file descriptors
    struct timeval tv;              // timeout for wait
    int     maxfd;                  // temp to file the max
    int     e;                      // another temp
    char*   pChar = pT->pBufHold;
/*
* simple uppercase string
*/
    //======== flush any stray characters
    do{
        dwRead=read(TheComm->PortId, pT->pBufHold, MAXDATASIZE);
    }while(dwRead > 0);

    //======== write the probe
    dwWrite = strlen(Probetext1);
    nwritten = write(TheComm->PortId,Probetext1,dwWrite);
#if (LAPJSTREAM>0) && 1
if((inicfg.prflags & PRFLG_EVB) || (inicfg.prflags & PRFLG_LAPJ))
{
jlpByteStreamOut("po",(char*)Probetext1, nwritten);
}
#endif

    //======== wait
    FD_ZERO(&rfds);             // clear our descriptors
    FD_SET(TheComm->PortId, &rfds);      // UART in
    maxfd = TheComm->PortId;
    tv.tv_sec = 2;              // 1 sec timer
    tv.tv_usec = 000000;
    e = select(maxfd+1, &rfds, NULL, NULL, &tv);
    //========
    if (e == -1) // Special for error trap
    //========
    {
        perror("C) select()");
    }
    //========
    else if(e == 0) // timeout
    //========
    {
        perror("ProbeText timeout");
        return(0);
    }

    //========
    else if(FD_ISSET(TheComm->PortId,&rfds)) // Uart in
    //========
    {
        goto testecho;
    }
    //======== Should not occue
    else
    {
        perror("ProbeText error");
        return(0);
    }

    //======== Read a response
testecho:
    //========
    dwRead=read(TheComm->PortId, pChar, MAXDATASIZE);
    if( (dwRead<0)){
        printf("dwRead %d, errn0 %d TheComm->PortId %d\n",dwRead,errno,TheComm->PortId);
        perror("ProbeText no text");
        return(0);
    }
    pChar[dwRead] = 0;
#if (LAPJSTREAM>0) && 1
if((inicfg.prflags & PRFLG_EVB) || (inicfg.prflags & PRFLG_LAPJ))
{
jlpByteStreamOut("pi",pChar, dwRead);
}
#endif

    //======== test for all chars
    if((dwRead == dwWrite) && (strncmp(pT->pBufHold,Probetext1,dwWrite)==0))
    {
        retval = 1;
        goto echoerase;
    }
    retval = 0;
    goto echoerase;

echoerase:
    for(k=0; k<dwWrite;k++){
        pChar[k]=8;         // backspace
    }
    nwritten = write(TheComm->PortId,pChar,dwWrite);

    return retval;
}
/*
**************************************************************************
*
*  Function: DWORD WINAPI EVBThreadLoopB(LPVOID lpV)
*
*  Purpose: Comm thread function LoopBack
*
*  Comments:
*   This is the thread for loopback
*
*   It loops the output from the router back into the router.
*   Possibly we might add some message interpertation but for now:
*
*   A more complete simulation of the EVB is done here for debug
*   purposes.  We will remove the packet from the queue just like the
*   actual driver does It will be placed into the structure and the timer
*   used for a loopback.
*
**************************************************************************
*/

#define LOOPBMODETEST 1
static int firstpkt = 0;
static char tstpkt[] = "\020\002\056AHello\015\020\003\000";
static char tstpkt2[] = "0123456789ABCDEF0123456789abcdef\r\n";
#if 0
int EVBThreadLoopB(LPVOID lpV)
{
    struct AsynWinComm *TheComm;
    BOOL       fThreadDone;
    DWORD      dwRes;
    int         k;
    int         flag;
    HANDLE     hArray[2];
    t_dqueue* qe;
    t_ctlreqpkt* txctlpkt;
    t_ctldatapkt* txdatapkt;
    t_ctldatapkt* newdpkt;
    char*       pChar;
    char*       pTxd;
    int         nTxd;

    fThreadDone = FALSE;
    EVBActive = EVBSIM;
//    TheComm = (struct AsynWinComm *) lpV;

    while ( !fThreadDone )
    {
        hArray[0] = EVBInputSem;
        hArray[1] = EVBExitEvent;
        dwRes = WaitForMultipleObjects(2, hArray, FALSE, 4000);
        switch(dwRes)
        {
            //========
            case WAIT_OBJECT_0: // EVBInputSem
            //========
                qe = dequeue(&EVBInQHeader.link,&gRouterCS);          // get the queue entry
                if(qe)
                {
                    txctlpkt = structof(t_ctlreqpkt,link,qe);   // get the control packet
#if EASYWINDEBUG && 0
jlpDumpCtl("EVBThreadLoopB: WAIT_OBJECT_0",txctlpkt);
#endif
                    txdatapkt = txctlpkt->pData;                // and the data packet
                    if( (txdatapkt == NULL))
                        FatalError("EVBThreadLoopB:NULL data packet");
                    pTxd = (char*)(txdatapkt->pData);
#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_EVB)
{
jlpDumpFrame("EVBThreadLoopB",pTxd,txdatapkt->nData);
}
#endif

                    k = pTxd[2] & 0x7f;
                    flag =(((k >= DEVVSP0) && (k < (DEVVSP0+TID_MAXDEVVSP)))
                        ||  (k == TID_UNFRAMED));

                    if(!flag)
                    {
                        if(k == TID_EVB)
                        ReleaseDataPacket(txdatapkt,&gRouterCS); // Release it
                        ReleaseCtlPacket(txctlpkt,&gRouterCS);   // release the control packet
                        break;
                    }

#if LOOPBMODETEST == 1 // echo mode with first packet VSP0
                    // on the first packet, change the data to open a VSP windos
                     txctlpkt->from = (0x01<<8)|ROUTESRCEVB;               // Change the header
                    if(firstpkt == 0)
                    {
                        pChar = txctlpkt->pData->pData;
                        strcpy(pChar,tstpkt);
                        txctlpkt->pData->nData=strlen(tstpkt);
                        firstpkt = 1;
                    }
                    enqueue(&(RouteInQHeader.link),qe,NULL);    // send it back
                    ReleaseSemaphore(RouteInputSem,1,(LPLONG)&k);
#endif
#if LOOPBMODETEST == 2 // long unframed string
{
    int n,m;
    pTxd = txctlpkt->pData->pData;
    nTxd = txctlpkt->pData->nData;
    pChar = pTxd;
    m=MAXDATASIZE+4;
    n = strlen(tstpkt2);
    for(k=0; k < m/n; k++)
    {
        strcpy(pChar,tstpkt2);
        pChar += n;
    }
    n = m - k*n;
    strncpy(pChar,tstpkt2,n);
    txctlpkt->pData->nData = m;
    DoMonStream(pTxd,                  // Parse the stream
                m,
                ROUTESRCEVB,
                &(TheComm->pRecvQ),
                &TheComm->framer);
}
#endif
                }
            break;

            //========
            case WAIT_OBJECT_0+1: // EVBExitEvent
            //========
                fThreadDone = TRUE;
            break;

            //========
            case WAIT_TIMEOUT:  // Timer
            //========

            break;

            //========
            default:
            //========
                ErrorInComm("EVBThread: WaitForMultipleObjects");
            break;
        }
    }
    return(1);
}
#endif

extern void* pMainWindow;

int ShowDialogError(char* s)
{
    MessageBox(pMainWindow,s,"Error",0x0030);
}

void ErrorInComm(char* str)
{
    ShowDialogError(str);
    exit(1);
}