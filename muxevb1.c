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

#if USEWIN32
#include "mttty.h"
#endif

#define STATUS_CHECK_TIMEOUT 5000


HANDLE hEVB;
DWORD  dwEVBID;

static void EVBThreadLoopB(LPVOID lpV);
static void EVBThread(LPVOID lpV);

extern void FatalError(char*);
extern void ErrorInComm(char * szMessage);

int EVBActive = EVBNOTACTIVE;
t_jdevice devEVB;                               // Device structure

static HANDLE EVBopen(struct AsynWinComm *ThePort, char *WhichPort, int TheRate, int TheFlow);

static int jsetparam(struct S_jdevice* pMe, APARAM wparam, APARAM lparam);
static int jgetparam(struct S_jdevice* pMe, APARAM wparam, APARAM lparam);
static int jdocmd(struct S_jdevice* pMe, APARAM wparam, APARAM lparam);
static int jdowctlpkt(struct S_jdevice* pMe, APARAM wparam, APARAM lparam);
static int jdosigquit(struct S_jdevice* pMe, APARAM wparam, APARAM lparam);
static void docallback(t_AsynWinComm *TheComm, APARAM wparam, APARAM lparam);


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
    int     nPriv;
    t_AsynWinComm* pP;
    

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
    strcpy(pP->cCOMNAME,"COM1:");
    pP->PortId = INVALID_HANDLE_VALUE;

    //======== make an exit event
    pP->ExitEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (pP->ExitEvent == NULL)
    {
        ShowDialogError("newEVB: CreateEvent.ExitEvent Fail");
        return 0;
    }

    //======== Create an input semaphore
    pP->InputSem = CreateSemaphore(NULL,0,MAXCTLPACKETS+100,NULL);
    if (pP->InputSem == NULL)
    {
        ShowDialogError("newEVB: CreateSemaphore Fail");
        return 0;
    }

    //======== clear input queue
    initDqueue(&(pP->InQHeader.link),NULL);           // remember to add locking
    pP->pRecvQ = NULL;
    pP->pXmitQ = NULL;

    //======== clear framer
    memset(&(pP->framer),0,sizeof(t_framer));

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


    if(!pMe)
        return 0;

    pS = (t_AsynWinComm*)(pMe->pPriv);
    if(!pS)
        return 0;

    qpkIn =(t_ctlreqpkt*)wparam;

    LockSection(&gRouterCS);
    enqueue(&(pS->InQHeader.link),&(qpkIn->link),NULL); // link it in
    ReleaseSemaphore(pS->InputSem,1,NULL);      // sigal arrival
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

    if(pP->PortId != INVALID_HANDLE_VALUE)
    {
        CloseHandle(pP->PortId);
        pP->PortId = INVALID_HANDLE_VALUE;
    }

    if(pP->ExitEvent)
    {
        CloseHandle(pP->ExitEvent);
        pP->ExitEvent = NULL;
    }
    if(pP->InputSem)
    {
        CloseHandle(pP->InputSem);
        pP->InputSem = NULL;
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
            //static int InitConnectSock(t_AsynWinComm* TheComm, char* name)
            retval = (int)pS->PortId;
            if(retval == (int)INVALID_HANDLE_VALUE)
                retval = 0;
#if EASYWINDEBUG && 1
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
            pS->comrate = LOWORD(lparam);
            pS->comflow = HIWORD(lparam);
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

    fd = open(pPort, O_RDWR | O_NOCTTY | O_NDELAY);
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

        displaytermios(&termios_p);
        tcsetattr(fd, TCSANOW, &termios_p); // update

        ioctl(fd, TIOCMGET, &modemstatus);  // get rs232 signals
        printf("ModemStatus 0x%02x\n",modemstatus);

    }

    ThePort->pRecvQ = NULL;
    ThePort->pXmitQ = NULL;

    ResetEvent(ThePort->ExitEvent);
    dwEVBID = -1;
    hEVB = (HANDLE)SpawnThread(EVBThread, NULL, ThePort);

    if (hEVB == NULL)
        ShowDialogError("EVBopen:CreateThread Fail");

#if EASYWINDEBUG & 0
printf("hEVB %08x %08x\n",hEVB,dwEVBID);
#endif

#if EASYWINDEBUG & 1
printf("Before retyrn idComDev %08x\n",ThePort->PortId);
#endif
    CheckDlgButton(hStartDialog,IDC_STUPEvb,BST_CHECKED);
	return(hEVB);
#endif
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
    DCB dcb;

    // Quiet return if we don't have a comm device
    idComDev = ThePort->PortId;
    if(idComDev == INVALID_HANDLE_VALUE)
        return(0);
        
    GetCommState(idComDev,&dcb);

    if(TheRate > 0)
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

#if EASYWINDEBUG & 1
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
            //========
            case EVBCMD_BRATE:
            //========
            {
                DCB commDCB;
                GetCommState(TheComm->PortId, &commDCB);
                commDCB.BaudRate = TheCmd->p1;
                SetCommState(TheComm->PortId, &commDCB);
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
*  Function: static BOOL DoStartRead(t_AsynWinComm* pAsyn, OVERLAPPED* osReader)
*
*  Purpose: Reads the current buffer and start a new one
*
*  Arguments:
*   t_AsynWinComm* pAsyn   Structure pointer
*
*  Comments:
*   This handles the calls to start a delayed read (OverLapped)
*   A return value
*       TRUE:   delayed read is pending
*       FALSE:  no delayed read is pending
*
*   Errors use FatalError as an exception
*
**************************************************************************
*/
static BOOL DoStartRead(t_AsynWinComm* TheComm, OVERLAPPED* osReader)
{
    DWORD   dwRead;
    BOOL    retval;
    BOOL    readflag;
    char*   bufin = TheComm->rxbuf;
    
    // The comm driver has internel buffers (see SetupComm)
    // So this routine is more of an editing container

    ResetEvent(osReader->hEvent);   // Examples don't do this..placeholder
    readflag=ReadFile(TheComm->PortId, bufin, MAXDATASIZE, &dwRead, osReader);
#if EASYWINDEBUG & 1
if(inicfg.prflags & PRFLG_EVB)
{
printf("DoStartRead:Port 0x%08x readflag %d dwRead %d\n",TheComm->PortId,readflag,dwRead);
}
#endif
    if(!readflag)
    {
        if (GetLastError() != ERROR_IO_PENDING)	// read not delayed?
        {
            ShowDialogError("DoStartRead:");
        }
        else
        {
            retval = TRUE;                          // yep, it is started and pending
        }
    }
    else                                        // read completed immediately
    {
       if (dwRead>0)                            // Are there any characters?
       {
            DoMonStream(bufin,                  // Parse the stream
                        dwRead,
                        ROUTESRCEVB,
                        &(TheComm->pRecvQ),
                        &TheComm->framer);
       }
       retval = FALSE;                      // No pending read
    }
    
    return retval;
}

/*
**************************************************************************
*
*  Function: static BOOL DoDelayedRead(t_AsynWinComm* pAsyn, OVERLAPPED* osReader)
*
*  Purpose: Reads the current buffer After
*
*  Arguments:
*   t_AsynWinComm* pAsyn   Structure pointer
*
*  Comments:
*   This handles the calls to start a delayed read (OverLapped)
*   A return value
*       TRUE:   delayed read is pending
*       FALSE:  no delayed read is pending
*
*   Errors use FatalError as an exception
*
**************************************************************************
*/
static BOOL DoDelayedRead(t_AsynWinComm* TheComm, OVERLAPPED* osReader)
{
    DWORD   dwRead;
    BOOL    retval;
    BOOL    readflag;
    char*   bufin = TheComm->rxbuf;

    readflag = GetOverlappedResult(TheComm->PortId, osReader, &dwRead, FALSE);
#if EASYWINDEBUG & 1
if(inicfg.prflags & PRFLG_EVB)
{
printf("DoDelayedRead: GetOverlappedResult returns %d dwRead %d\n",readflag,dwRead);
}
#endif
    if(!readflag)
    {
        retval = FALSE;
        
        if (GetLastError() == ERROR_OPERATION_ABORTED)
            ShowDialogError("DoDelayedRead:Read aborted");
        else
            ShowDialogError("DoDelayedRead:GetOverlappedResult");
    }
    else
    {      // read completed successfully
        if (dwRead > 0)
        {
            DoMonStream(bufin,                  // Parse the stream
                        dwRead,
                        ROUTESRCEVB,
                        &(TheComm->pRecvQ),
                        &TheComm->framer);
        }
        retval = FALSE;
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

#if EASYWINDEBUG & 0
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
            ShowDialogError("DoDelayedWrite:AllocDataPacket fail 1");
        }
        else
            SendFrameToRouter(pAck, ROUTESRCEVB+0x400);
#if EASYWINDEBUG & 0
printf("CheckACK B: %02x %02x\n",raddr&0xff, rseq&0xff);
#endif
    }
}
/*
**************************************************************************
*
*  Function: static BOOL DoStartWrite(t_AsynWinComm* pAsyn, OVERLAPPED* osWrite)
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
*
**************************************************************************
*/
static BOOL DoStartWrite(t_AsynWinComm* TheComm, OVERLAPPED* osWrite)
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
    


    // dequeue a control packet
    qe = dequeue(&(TheComm->InQHeader.link),&gRouterCS);      // get the queue entry
    if(!qe)
    {                                  // none
#if EASYWINDEBUG & 0
printf("DoStartWrite: Empty\n");
#endif
        TheComm->pXmitQ = NULL;
        return(FALSE);
    }

    txctlpkt = structof(t_ctlreqpkt,link,qe);   // get the control packet
#if EASYWINDEBUG & 0
jlpDumpCtl("DoStartWrite:",txctlpkt);
#endif
    txdatapkt = txctlpkt->pData;                // and the data packet
    ReleaseCtlPacket(txctlpkt,&gRouterCS);      // release the control packet
    
    // Check the data
    nTxd = txdatapkt->nData;                    // count

    if(nTxd <= 0)                               // Empty?
    {
#if EASYWINDEBUG & 0
printf("DoStartWrite: Length=0\n");
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
    
#if EASYWINDEBUG & 1
if(inicfg.prflags & PRFLG_EVB)
{
printf("DoStartWrite: aDst = 0x%x\n",aDst);
}
#endif
    
    //========
    if(aDst == TID_UNFRAMED)                    // Unframed data?
    //========
    {
        t_ctldatapkt*   pPayload;

#if EASYWINDEBUG & 0
printf("DoStartWrite: TID_UNFRAMED\n");
#endif
        pPayload = (t_ctldatapkt*)DeFramePacket(txdatapkt,NULL,NULL);
        if(!pPayload)
        {
            ShowDialogError("DoStartWrite:AllocDataPacket fail 1");
            ReleaseDataPacket(txdatapkt,&gRouterCS); // Release old
            TheComm->pXmitQ = NULL;
            return(FALSE);
        }

        ReleaseDataPacket(txdatapkt,&gRouterCS); // Release old

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
#if EASYWINDEBUG & 0
printf("DoStartWrite: framed\n");
#endif
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
    if(!retval)
        return retval;
        
    ResetEvent(osWrite->hEvent);   // Examples don't do this..placeholder
    bWrite = WriteFile(TheComm->PortId, pTxd, nTxd, &dwWritten, osWrite);
#if EASYWINDEBUG & 1
if(inicfg.prflags & PRFLG_EVB)
{
printf("DoStartWrite: WriteFile returns %d requested %d wrote %d\n",bWrite,nTxd,dwWritten);
}
#endif
    if (!bWrite)
    {
        if (GetLastError() == ERROR_IO_PENDING)
        {
            retval = TRUE;                      // Ok, waiting
        }
        else
        {
            ShowDialogError("DoStartWrite:WriteFile fail 1");
        }
    }
    else                                        // write completed immediatly
    {
        char raddr,rseq;
        if(TheComm->pXmitQ)
        {
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
        retval = FALSE;                         // Nope, not waiting anymore
    }
    
    return retval;
}

/*
**************************************************************************
*
*  Function: static BOOL DoDelayedWrite(t_AsynWinComm* pAsyn, OVERLAPPED* osWrite)
*
*  Purpose: Finish a write
*
*  Arguments:
*   t_AsynWinComm* pAsyn   Structure pointer
*
*  Comments:
*   Mostly exists to release packets back into the pool
*   there is a marker for where we can put ack flow control for
*   the file transfer TBD
*
*   Errors use FatalError as an exception
*
**************************************************************************
*/
static BOOL DoDelayedWrite(t_AsynWinComm* TheComm, OVERLAPPED* osWrite)
{
    DWORD   dwWrite;
    BOOL    retval;
    BOOL    writeflag;


    writeflag=GetOverlappedResult(TheComm->PortId, osWrite, &dwWrite, FALSE);
#if EASYWINDEBUG & 1
if(inicfg.prflags & PRFLG_EVB)
{
printf("DoDelayedWrite: GetOverlappedResult returns %d dwWrite %d\n",writeflag,dwWrite);
}
#endif
    if (!writeflag)                             //write failed
    {
        ShowDialogError("DoDelayedWrite:Fail 1");
        retval = FALSE;
    }
    else                                        // write completed successfully
    {
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
        retval=FALSE;  /* We are not waiting */
    }
    return retval;
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
**************************************************************************
*/


#define NUM_READSTAT_HANDLES 5

static void EVBThread(LPVOID lpV)
{

   t_AsynWinComm *TheComm;

    OVERLAPPED osReader = {0};  // overlapped structure for read operations
    OVERLAPPED osStatus = {0};  // overlapped structure for status operations
    OVERLAPPED osWrite = {0};
    HANDLE  NullEvent;
    HANDLE     hArray[NUM_READSTAT_HANDLES];
    DWORD      dwCommEvent;     // result from WaitCommEvent
    DWORD      dwOvRes;         // result from GetOverlappedResult
    DWORD 	   dwRead;          // bytes actually read
    DWORD      dwWritten;
    DWORD      dwToWrite;
    DWORD      dwRes;           // result from WaitForSingleObject
    DWORD      dwTmp;
    BOOL       fThreadDone;
    BOOL       fWaitingOnRead;
    BOOL       fWaitingOnStat;
    BOOL       fWaitingOnWrite;

    TheComm = (t_AsynWinComm *) lpV;

#if EASYWINDEBUG & 1
printf("Arrived in EVBThread Port 0x%08x\n",TheComm->PortId);
#endif
    //
    // create three overlapped structures
    //   Read pending
    //   status pending
    //   write pending
    // and a Null event just for implementation reasons
    //
    // we might change these to semaphores to allow people to queue
    // multiple requests from open sockets. Ie, a socket will
    // set an event and signal a semaphore.  It then waits on
    // the event.  The common reader, when finished with the packet
    // will set the event.  Gotta think about this some more.
    // the semaphore is a request and the event is the ack.
    //
    osReader.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (osReader.hEvent == NULL)
    {
        SetEvent(TheComm->ExitEvent);
        ShowDialogError("CreateEvent (Reader Event)");
    }

    osStatus.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (osStatus.hEvent == NULL)
    {
        SetEvent(TheComm->ExitEvent);
        ShowDialogError("CreateEvent (Status Event)");
    }

    osWrite.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (osWrite.hEvent == NULL)
    {
        SetEvent(TheComm->ExitEvent);
        ShowDialogError("CreateEvent (Write Event)");
    }

    NullEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (NullEvent == NULL)
    {
        SetEvent(TheComm->ExitEvent);
        ShowDialogError("CreateEvent (NullEvent)");
    }

    // Clear out UART when thread (me) starts
    PurgeComm(TheComm->PortId,PURGE_TXABORT|PURGE_RXABORT|PURGE_TXCLEAR|PURGE_RXCLEAR);
    fWaitingOnRead = FALSE;
    fWaitingOnStat = FALSE;
    fWaitingOnWrite = FALSE;
    fThreadDone = FALSE;
    EVBActive = EVBONLINE;

#if EASYWINDEBUG & 0
printf("EVBThread:EVBActive = %d\n",EVBActive);
#endif

    while ( !fThreadDone )
    {

        //========
        // if no read is outstanding, then issue another one
        //========
        if (!fWaitingOnRead)
        {
            fWaitingOnRead = DoStartRead(TheComm,&osReader);
        }

#if 1
        //
        // if no status check is outstanding, then issue another one
        //
        if (!fWaitingOnStat)
        {
            if (!WaitCommEvent(TheComm->PortId, &dwCommEvent, &osStatus))
            {
                if (GetLastError() != ERROR_IO_PENDING)	  // Wait not delayed?
                    ErrorInComm("WaitCommEvent");
                else
                    fWaitingOnStat = TRUE;
            }
            else
            {

#if EASYWINDEBUG & 0
printf("EVBThread:dwCommEventA %08x\n",dwCommEvent);
#endif
                fWaitingOnStat = FALSE;
                // WaitCommEvent returned immediately
                //ReportStatusEvent(dwCommEvent);
            }
        }
#endif
        //
        // wait for pending operations to complete
        // Always wait for read,status, and thread
        // if there is a pending write, wait for it
        //  else wait for the user progran to signal us
        // This forces a flow control of sort where we don't
        // queue up multiple writes.  There must be a backwards
        // flow chain of the same sort. Ie, each "writer" in
        // a chain must wait for completion
        //

        hArray[0] = osReader.hEvent;
        hArray[1] = osStatus.hEvent;
        if(fWaitingOnWrite)
        {
            hArray[2] = osWrite.hEvent;
            hArray[3] = NullEvent;
        }
        else
        {
            hArray[2] = NullEvent;
            hArray[3] = TheComm->InputSem;
        }
        hArray[4] = TheComm->ExitEvent;


        {
#if EASYWINDEBUG & 0
printf("EVBThread:Start wait\n",dwRes);
#endif
            dwRes = WaitForMultipleObjects(NUM_READSTAT_HANDLES, hArray, FALSE, 8000);

#if EASYWINDEBUG & 0
if(dwRes != (258))
    printf("EVBThread:dwRes %d\n",dwRes);
#endif

            switch(dwRes)
            {
                //
                // osReader.hEvent: read completed
                //========
                case WAIT_OBJECT_0:
                //========
                    fWaitingOnRead = DoDelayedRead(TheComm,&osReader);
                break;

                //
                // osStatus.hEvent: status completed
                //========
                case WAIT_OBJECT_0 + 1:
                //========
                    if (!GetOverlappedResult(TheComm->PortId, &osStatus, &dwOvRes, FALSE))
                    {
                        if (GetLastError() == ERROR_OPERATION_ABORTED)
                            ErrorInComm("WaitCommEvent aborted\r\n");
                        else
                            ErrorInComm("GetOverlappedResult (in Reader)");
                    }
                    else
                    {
#if EASYWINDEBUG & 0
printf("EVBThread:dwOvRes %08x\n",dwOvRes);
#endif
                        // status check completed successfully
                        //ReportStatusEvent(dwCommEvent);
                    }

                    fWaitingOnStat = FALSE;
                 break;

                //
                // osWrite.hEvent: write completed
                //========
                case WAIT_OBJECT_0+2:
                //========
                    fWaitingOnWrite = DoDelayedWrite(TheComm,&osWrite);
                break;

                //
                // EVBInputSem: New write requested
                //========
                case WAIT_OBJECT_0+3:
                //========
                    fWaitingOnWrite = DoStartWrite(TheComm,&osWrite);
                break;

                //
                // EVBExitEvent: thread exit event
                //========
                case WAIT_OBJECT_0 + 4:
                //========
                    fThreadDone = TRUE;
                break;

                //========
                case WAIT_TIMEOUT:
                //========
                {
                    if( (TheComm->framer.inframe))  // hung up in a frame?
                    {
#if EASYWINDEBUG & 0
printf("EVBThread:Timeoutreset\n");
#endif
                        if((TheComm->framer.cBufout > 0))
                        {
                            SendUnFramed(TheComm->framer.Bufout,TheComm->framer.cBufout,ROUTESRCEVB); // Yep ship of unframed
                        }
                        TheComm->framer.inframe = FRM_OUTSIDE;
                        TheComm->framer.state = 0;
                        TheComm->framer.cBufout = 0;
                    }

                    if(TheComm->pRecvQ)         // keep ttl active
                    {
                        TheComm->pRecvQ->ttl = MAXTTL;
                    }
                    if(TheComm->pXmitQ)         // keep ttl active
                    {
                        TheComm->pXmitQ->ttl = MAXTTL;
                    }
                }

                break;                       

                //========
                case -1:
                //========
                {
                    char tmpstr[64];
                    sprintf(tmpstr,"Wait Error %d\n",GetLastError());
                    FatalError(tmpstr);
                }
                break;

                //========
                default:
                //========
                    ErrorInComm("EVBThread: WaitForMultipleObjects");
                break;
            }
        }
    }

    //
    // close local event handles
    //
    CloseHandle(osReader.hEvent);
    CloseHandle(osStatus.hEvent);
    CloseHandle(osWrite.hEvent);
    CloseHandle(NullEvent);

    ThreadReturn(1);
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

#define LOOPBMODETEST 0
static int firstpkt = 0;
static char tstpkt[] = "\020\002\056AHello\015\020\003\000";
static char tstpkt2[] = "0123456789ABCDEF0123456789abcdef\r\n";
static void EVBThreadLoopB(LPVOID lpV)
{
    t_AsynWinComm *TheComm;
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
    TheComm = (t_AsynWinComm *) lpV;
#if EASYWINDEBUG && 1
if( (inicfg.prflags & PRFLG_OTHER) || 0)
{
printf("Arrived EVBThreadLoopB\n");
}
#endif

    while ( !fThreadDone )
    {
        hArray[0] = TheComm->InputSem;
        hArray[1] = TheComm->ExitEvent;
        dwRes = WaitForMultipleObjects(2, hArray, FALSE, 4000);
        switch(dwRes)
        {
            //========
            case WAIT_OBJECT_0: // EVBInputSem
            //========
                qe = dequeue(&(TheComm->InQHeader.link),&gRouterCS);      // get the queue entry
                if(qe)
                {
                    txctlpkt = structof(t_ctlreqpkt,link,qe);   // get the control packet
#if EASYWINDEBUG & 0
jlpDumpCtl("EVBThreadLoopB: WAIT_OBJECT_0",txctlpkt);
#endif
                    txdatapkt = txctlpkt->pData;                // and the data packet
                    if( (txdatapkt == NULL))
                        FatalError("EVBThreadLoopB:NULL data packet");
                    pTxd = (char*)(txdatapkt->pData);
#if EASYWINDEBUG & 1
if(inicfg.prflags & PRFLG_EVB)
{
jlpDumpFrame("EVBThreadLoopB",pTxd,txdatapkt->nData);
}
#endif

                    k = pTxd[2] & 0x7f;
                    flag =(((k >= DEVVSP0) && (k < (DEVVSP0+TID_MAXDEVVSP)))
                        ||  (k == TID_UNFRAMED));

                    if(!flag)                   // invalid address?
                    {
//                        if(k == TID_EVB)
                        ReleaseDataPacket(txdatapkt,&gRouterCS); // Release it
                        ReleaseCtlPacket(txctlpkt,&gRouterCS);   // release the control packet
                        break;                  // break out of loop
                    }

                    // on the first packet, change the data a canned string
                    if(firstpkt == 0)
                    {
                        pChar = txdatapkt->pData;
                        strcpy(pChar,tstpkt);
                        txdatapkt->nData=strlen(tstpkt);
                        firstpkt = 1;
                    }
                    ReleaseCtlPacket(txctlpkt,&gRouterCS);     // release the control packet
                    SendFrameToRouter(txdatapkt,ROUTESRCEVB);  // send the data packet vack
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
    ThreadReturn(1);
}
