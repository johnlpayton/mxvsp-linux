/*******************************************************************************
*   FILE: muxsock.c
*
*   Under GTK, we  implement only the client
*   The process is in the main thread and implemented with callbacks.
*   The callbacks drive two state machines.
*
* Receive Frame:
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
#include <winsock2.h>
#endif

#include "memlock.h"
//#include "MTTTY.H"
//#include "muxvttyW.h"
//#include "mttty.h"
#include "muxctl.h"
#include "framer.h"
//#include "muxevb.h"
#include "muxsock.h"

#include "muxsock_P.h"

#define BMEVENTTOUT     0x00
#define BMEVENTQUIT     0x01
#define BMEVENTNEWPKT   0x02
#define BMEVENTDONEWR   0x04
#define BMEVENTDONERD   0x08


extern void SimRouter(t_ctlreqpkt* pkIn);
extern void FatalError(char*);
extern void ErrorInComm(char * szMessage);
static void TCPKillThread(t_muxsockparam* TheComm, int code);

static int jsetparam(struct S_jdevice* pMe, APARAM wparam, APARAM lparam);
static int jgetparam(struct S_jdevice* pMe, APARAM wparam, APARAM lparam);
static int jdocmd(struct S_jdevice* pMe, APARAM wparam, APARAM lparam);
static int jdowctlpkt(struct S_jdevice* pMe, APARAM wparam, APARAM lparam);
static int jdowctlpktloop(struct S_jdevice* pMe, APARAM wparam, APARAM lparam);
static int jdosigquit(struct S_jdevice* pMe, APARAM wparam, APARAM lparam);
static void docallback(t_muxsockparam *TheComm, APARAM wparam, APARAM lparam);

static BOOL TCPDoStartWrite(t_muxsockparam* TheComm);
static BOOL TCPDoStartRead(t_muxsockparam* TheComm);
static void TCPDoDelayedRead(GObject *source_object,GAsyncResult *res,gpointer user_data);
static void TCPDoDelayedWrite(GObject *source_object,GAsyncResult *res,gpointer user_data);

static int jdonull(struct S_jdevice* pMe, APARAM wparam, APARAM lparam)
{
    return(0);
}


/*---------------------------------------------------------------------------
*  FUNCTION: int new_tcpdev(t_jdevice* pDev, pJfunction cb)
*
*  PURPOSE:
*   Initialize a devvice structure for tcpip
*
*  Parameters:
*     t_jdevice*    pDev  (empty) structure to fill
*     pJfunction     cb  callback function (can be NULL)
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
int new_tcpdev(t_jdevice* pDev, pJfunction cb)
{
    int retval = 1;
    t_muxsockparam* pP;
    int     nPriv;

    //======== Set indirect calls
    pDev->state = 0;
    pDev->cmd = jdocmd;
    pDev->setparam = jsetparam;
    pDev->getparam = jgetparam;
    pDev->callback = cb;
    pDev->sigquit = jdosigquit;
    if(inicfg.muxmode == MUXMODELOOP)
        pDev->wctlpkt = jdowctlpktloop;
    else
        pDev->wctlpkt = jdowctlpkt;

    //======== Allocate working memory
    nPriv = sizeof(t_muxsockparam);
    pP = (t_muxsockparam*)malloc(nPriv);
    pDev->pPriv = pP;
    memset(pP,0,nPriv);
    pP->pParent = pDev;                          // reverse index

    //======== default parameters
    pP->sockParamFamily = G_SOCKET_FAMILY_IPV4;
    pP->sockParamStream = G_SOCKET_TYPE_STREAM;
    pP->sockParamProto = G_SOCKET_PROTOCOL_TCP;
    pP->hSocket = INVALID_SOCKET;


    //======== clear input queue
    initDqueue(&(pP->InQHeader.link),NULL);      // remember to add locking
    BMEventInit(&(pP->BMEv));
    pP->pRecvQ = NULL;
    pP->pXmitQ = NULL;
    pP->readPending = 0;

    //======== clear framer
    memset(&(pP->framer),0,sizeof(t_framer));

    return retval;
}
/*---------------------------------------------------------------------------
*  int InitConnectSock(t_muxsockparam* TheComm)
*
*  Description:
*   Initialize the client side
*
*  Parameters:
*     none
*
*  Comments
*   Initialization
*   A socket is creates with overlapped access
*
*   Create a socket
*   Connect by name to a server
*       name
*       TCPIP
*       TELNET
*   Prints lots of stuff
*
*   On exit TheComm->hSocket should be set up.
*
*  History:   Date       Author      Comment
*  24-Feb-2012 jlp
*   initial pass
*
*---------------------------------------------------------------------------*/

static int InitConnectSock(t_muxsockparam* TheComm, char* name)
{
    int iResult = 0;
    int k;
    DWORD arg;
    char tmpstr[64];
    int iFlag;

    //======== In loop, skip opening a TCP
    if(inicfg.muxmode == MUXMODELOOP)
    {
#if EASYWINDEBUG && 1
if(inicfg.prflags & PRFLG_TCP)
{
//    printf("g_socket_connection_connect returns 0x%0x\n",(unsigned int)TheComm->hTcpConnection);
        printf("InitConnectSock: (Loopback) Name %s port %d\n",
            TheComm->sockParamRName,
            TheComm->sockParamRport);
}
#endif
        TheComm->threadState = THREADREADY;
        return(1);
    }

#if 1
    //======== Allocate a socket connection (must g_object_unref when done)
    TheComm->hSocket = g_socket_client_new();

    //======== connect to dopey:3283 (must g_io_stream_close when done)
    TheComm->hTcpConnection = g_socket_client_connect_to_host(
        TheComm->hSocket,
        TheComm->sockParamRName,
        TheComm->sockParamRport,
        NULL,NULL);
#endif

#if EASYWINDEBUG && 1
if(inicfg.prflags & PRFLG_TCP)
{
//    printf("g_socket_connection_connect returns 0x%0x\n",(unsigned int)TheComm->hTcpConnection);
    printf("InitConnectSock: Name %s port %d\n",
        TheComm->sockParamRName,
        TheComm->sockParamRport);

}
#endif
    if(!TheComm->hTcpConnection)
    {
        int k;
        k = MessageBox(
            NULL,
            TheComm->sockParamRName,
            "Cannot Connect to",
            MB_OK);
        return 0;
    }

    //======== clear abortive disconnect
    g_tcp_connection_set_graceful_disconnect(G_TCP_CONNECTION(TheComm->hTcpConnection),0);


//        AddLogMessage("Connected",1);
    docallback(TheComm,sockCBTextMNL,(BPARAM)"Connected");
//        CheckDlgButton(hStartDialog,IDC_STUPSocket,BST_CHECKED);

    TheComm->threadState = THREADREADY;
    TheComm->readPending = 0;
//    TCPDoStartRead(TheComm);
    TheComm->hTCP = SpawnThread(muxSocThread,0,TheComm);

    return(1);
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
    t_muxsockparam* pS;


    if(!pMe)
        return 0;

    pS = (t_muxsockparam*)(pMe->pPriv);
    if(!pS)
        return 0;

    qpkIn =(t_ctlreqpkt*)wparam;

#if 1
    LockSection(&gRouterCS);
    enqueue(&(pS->InQHeader.link),&(qpkIn->link),NULL); // link it in
    BMEventSet(&(pS->BMEv),BMEVENTNEWPKT);
    UnLockSection(&gRouterCS);
#endif
//    PostMail(&(pS->InMail),qpkIn);
    return retval;
}
/*
**************************************************************************
*
*  FUNCTION: static int jdowctlpktloop(struct S_jdevice* pMe, APARAM wparam, APARAM lparam)
*
*  PURPOSE: callback to loop back a packet
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
*  P R O B L E M   G T K
*
*   In Gtk, we use callbacks instead of threads.  Loopback during
*   download puts us into a repeated callback loop until all the
*   lines in the file are sent.  The problem will remain until we
*   learn how to put the socket into it's own thread and use semaphores
*   or other signaling to break the callback loop.  For the present, we'll'\
*   only do small download tests in loopback
*
**************************************************************************
*/
static int jdowctlpktloop(struct S_jdevice* pMe, APARAM wparam, APARAM lparam)
{
    int retval = 1;
    t_ctlreqpkt*    qpkIn;
    t_ctldatapkt*   pAck;
    char*           pFrame;
    char buf[8];
    t_muxsockparam* pS;
    int             raddr,rseq;


    if(!pMe)
        return 0;

    pS = (t_muxsockparam*)(pMe->pPriv);
    if(!pS)
        return 0;

    qpkIn =(t_ctlreqpkt*)wparam;
    pFrame = qpkIn->pData->pData;

    // get the address-seq
    raddr = pFrame[2];
    rseq  = pFrame[3];

    // change the from and the ACK
    qpkIn->from = ROUTESRCEVB;
    pFrame[2] &= ~WWANTACK;

#if 0
    LockSection(&gRouterCS);
    {
        enqueue(&(RouteInQHeader.link),&(qpkIn->link),NULL); // link it in
        SignalSemaphore(&RouteInputSem);
    }
    UnLockSection(&gRouterCS);
#endif
    ForwardCtlMail(&RouteInMail,qpkIn);

#if EASYWINDEBUG & 0
printf("CheckACK A: 0x%02x 0x%02x\n",raddr&0xff, rseq&0xff);
#endif
    if(raddr & WWANTACK)
    {
        buf[0] = raddr;
        buf[1] = rseq;
        strcpy(&(buf[2]),"ACK");
        pAck = ReframeData(buf, 6, raddr, rseq);
        if(!pAck)
        {
            FatalError("DoDelayedWrite:AllocDataPacket fail 1");
        }
        else
        {
            SendFrameToRouter(pAck, ROUTESRCEVB+0x400);
        }
#if EASYWINDEBUG & 0
printf("CheckACK B: %02x %02x\n",raddr&0xff, rseq&0xff);
#endif
    }


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
    t_muxsockparam* pP;
    int     nPriv;

    //======== Set indirect calls
    pDev->state = 0;

    if(!pDev->pPriv)
        return 0;

     pP = pDev->pPriv;


    if(pP->hSocket)
    {
        g_io_stream_close(G_IO_STREAM(pP->hTcpConnection),NULL,NULL);
        g_object_unref(pP->hSocket);
        pP->hSocket = NULL;
    }

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
    t_muxsockparam* pS;

    if(!pMe)
        return 0;

    pS = (t_muxsockparam*)(pMe->pPriv);

    if(!pS)
        return 0;

    switch(m)
    {
        //========
        case sockCmdInitConnectSock:
        //========
        {
            //static int InitConnectSock(t_muxsockparam* TheComm, char* name)
            retval = InitConnectSock(pS,(char*) lparam);

        }
        break;

        //========
        case sockCmdInitListenerSock:
        //========
        {
            //InitListenerSock(pS);
        }
        break;

        //========
        case sockCmdmuxSockListen:
        //========
        {
            //muxSockListen(pS);
        }
        break;

        //========
        case sockCmdmuxSockSpawn:
        //========
        {
            pS->hTCP=SpawnThread(muxSocThread,0,pS);
            pMe->hDevice = pS->hTCP;
        }
        break;

        //========
        case sockCmdGetThreadState:
        //========
        {
            retval = pS->threadState;
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
    t_muxsockparam* pS;

    if(!pMe)
        return 0;

    pS = (t_muxsockparam*)(pMe->pPriv);

    if(!pS)
        return 0;

    switch(m)
    {
        //========
        case sockParamMode:
        //========
        {
            pS->sockParamMode = 0x31&((U32)lparam);
        }
        break;

        //========
        case sockParamSOCK:
        //========
        {
        }
        break;

        //========
        case sockParamRName:
        //========
        {
            pS->sockParamRName = (char*)lparam;
        }
        break;

        //========
        case sockParamRaddr:
        //========
        {
            pS->sockParamRaddr = (U32)lparam;
        }
        break;

        //========
        case sockParamRport:
        //========
        {
            pS->sockParamRport = (U16)lparam;
        }
        break;

        //========
        case sockParamLName:
        //========
        {
        }
        break;

        //========
        case sockParamLaddr:
        //========
        {
            pS->sockParamLaddr = (U32)lparam;
        }
        break;

        //========
        case sockParamLport:
        //========
        {
            pS->sockParamLport = (U16)lparam;
        }
        break;

        //========
        case sockParamFamily:
        //========
        {
            pS->sockParamLport = (U8)lparam;
        }
        break;
        //========
        case sockParamStream:
        //========
        {
            pS->sockParamLport = (U8)lparam;
        }
        break;

        //========
        case sockParamProto:
        //========
        {
            pS->sockParamLport = (U8)lparam;
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
    t_muxsockparam* pS;

    if(!pMe)
        return 0;

    pS = (t_muxsockparam*)(pMe->pPriv);

    if(!pS)
        return 0;

    switch(m)
    {
        //========
        case sockParamMode:
        //========
        {
        }
        break;

        //========
        case sockParamSOCK:
        //========
        {
//            pS->hSocket = (SOCKET)lparam;
        }
        break;

        //========
        case sockParamRName:
        //========
        {
        }
        break;

        //========
        case sockParamRaddr:
        //========
        {
        }
        break;

        //========
        case sockParamRport:
        //========
        {
        }
        break;

        //========
        case sockParamLName:
        //========
        {
        }
        break;

        //========
        case sockParamLaddr:
        //========
        {
        }
        break;

        //========
        case sockParamLport:
        //========
        {
        }
        break;

        //========
        case sockParamFamily:
        //========
        {
        }
        break;
        //========
        case sockParamStream:
        //========
        {
        }
        break;

        //========
        case sockParamProto:
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
*  Function: static BOOL TCPDoStartRead(t_muxsockparam* TheComm)
*
*  Purpose: Reads the current buffer and start a new one
*
*  Arguments:
*   t_muxsockparam* pAsyn   Structure pointer
*
*  Comments:
*   This handles the calls to start a delayed read
*   A return value
*       TRUE:   delayed read is pending
*       FALSE:  no delayed read is pending
*
*
**************************************************************************
*/
#if 1
static BOOL TCPDoStartRead(t_muxsockparam* TheComm)
{
    BOOL    retval;
    GInputStream*   pgin;
    char*   bufin = TheComm->rxbuf;


    if(TheComm->threadState != THREADREADY)     // Are we shutting down
    {
        return TRUE;                            // pretend we have a pending read
    }

    if(TheComm->readPending)                    // Pending read
    {
        return TRUE;
    }

/*
    g_input_stream_read_async(GInputStream *stream,
                            void *buffer,
                            gsize count,
                            int io_priority,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data);
    readflag=ReadFile((HANDLE)(TheComm->hSocket), bufin, MAXDATASIZE, &dwRead, &(TheComm->osRead));
*/
    pgin = g_io_stream_get_input_stream(G_IO_STREAM(TheComm->hTcpConnection));
    g_input_stream_read_async( pgin,
        bufin,
        MAXDATASIZE,
        0,
        NULL,
        TCPDoDelayedRead,
        TheComm);

    TheComm->readPending = 1;
#if EASYWINDEBUG & 1
if(inicfg.prflags & PRFLG_TCP)
{
printf("TCPDoStartRead: bufin 0x%08x\n",(U32)bufin);
}
#endif
    retval = TRUE;                      //  pending read
    return retval;
}
#endif

/*
**************************************************************************
*
*  Function: static void TCPDoDelayedRead(GObject *source_object,GAsyncResult *res,gpointer user_data)
*
*  Purpose: Reads the current buffer After
*
*  Arguments:
*   t_muxsockparam* pAsyn   Structure pointer
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
#if 1
static void TCPDoDelayedRead(GObject *source_object, GAsyncResult *res, gpointer user_data)
{
    BOOL    retval;
    int     nRead;
    GInputStream*   pgin;
    t_muxsockparam* TheComm;
    char*   bufin;

    TheComm = (t_muxsockparam*)user_data;
    bufin = TheComm->rxbuf;

    //======== Are we dead?
    if(TheComm->threadState != THREADREADY)     // Are we shutting down
    {
        return ;
    }

    //======== Is a read pending (an error?)
    if( ! (TheComm->readPending))               // Pending read
    {
        return ;                                // nope
    }

    //======== Finish the read (add some error stuff later)
    pgin = g_io_stream_get_input_stream(G_IO_STREAM(TheComm->hTcpConnection));
    nRead = g_input_stream_read_finish(pgin,res,NULL);
    TheComm->nRead = nRead;

    if(nRead < 0)
    {
#if EASYWINDEBUG && 1
{
        int k;
        k = MessageBox(
            NULL,
            TheComm->sockParamRName,
            "Fatal Error reading from",
            MB_OK);
}
#endif
        FatalError("TCP Read error Exiting...");
    }

#if EASYWINDEBUG & 1
if(inicfg.prflags & PRFLG_TCP)
{
jlpDumpFrame("TCPDoDelayedRead:",bufin,nRead);
}
#endif

    BMEventSet(&(TheComm->BMEv),BMEVENTDONERD);

}
#endif

/*
**************************************************************************
*
*  Function: static BOOL TCPDoStartWrite(t_muxsockparam* TheComm)
*
*  Purpose: Gets a packet from the queue and starts a write
*
*  Arguments:
*   t_muxsockparam* pAsyn   Structure pointer
*
*  Return
*       TRUE:   delayed write is pending
*       FALSE:  no delayed write is pending
*
*  Comments:
*   This checks the input queue for packets to send.
*
*
*   Errors use FatalError as an exception
*
**************************************************************************
*/
#if 1
static BOOL TCPDoStartWrite(t_muxsockparam* TheComm)
{
    BOOL    retval;
    t_dqueue* qe;
    t_ctlreqpkt* txctlpkt;
    t_ctldatapkt* txdatapkt;
    t_ctldatapkt* newdpkt;
    GOutputStream* pgout;
    char*   pTxd;
    int     nTxd;


    //======== check for killed
    if(TheComm->threadState != THREADREADY)
        return FALSE;

    //======== Are we currently sending one
    if(TheComm->pXmitQ != NULL)
        return FALSE;

    qe = dequeue(&(TheComm->InQHeader.link),&gRouterCS);      // get the queue entry
    if(!qe)
    {                                  // none
#if EASYWINDEBUG & 0
printf("TCPDoStartWrite: Empty\n");
#endif
        TheComm->pXmitQ = NULL;
        return(FALSE);
    }

    txctlpkt = structof(t_ctlreqpkt,link,qe);   // get the control packet
#if EASYWINDEBUG & 0
jlpDumpCtl("TCPDoStartWrite:",txctlpkt);
#endif
    txdatapkt = txctlpkt->pData;                // finally the data packet
    ReleaseCtlPacket(txctlpkt,&gRouterCS);     // release the control packet

    // Check the data
    pTxd = txdatapkt->pData;
    nTxd = txdatapkt->nData;

    if(nTxd <= 0)                               // Empty?
    {
#if EASYWINDEBUG & 0
printf("TCPDoStartWrite: Length=0\n");
#endif
        ReleaseDataPacket(txdatapkt,&gRouterCS); // Release it
        TheComm->pXmitQ = NULL;
        retval = FALSE;
    }
    else                                        // data is framed
    {
#if EASYWINDEBUG & 0
printf("TCPDoStartWrite: nTxd = %d\n",nTxd);
#endif
        TheComm->pXmitQ = txdatapkt;            // remember what we send
        pTxd = TheComm->pXmitQ->pData;
        nTxd = TheComm->pXmitQ->nData;
        retval = TRUE;
    }

    // Ok, can we finally send?
    if(!retval)
        return retval;

    pgout = g_io_stream_get_output_stream(G_IO_STREAM(TheComm->hTcpConnection));
    g_output_stream_write_async(
        G_OUTPUT_STREAM(pgout),
        pTxd,
        nTxd,
        G_PRIORITY_DEFAULT,
        NULL,
        TCPDoDelayedWrite,
        TheComm);

#if EASYWINDEBUG & 1
if(inicfg.prflags & PRFLG_TCP)
{
printf("TCPDoStartWrite: Started %d bytes\n",nTxd);
}
#endif

    return TRUE;
}
#endif

/*
**************************************************************************
*
*  Function: static void TCPDoDelayedWrite(GObject *source_object,GAsyncResult *res,gpointer user_data)
*
*  Purpose: Finish a write
*
*  Arguments:
*   t_muxsockparam* pAsyn   Structure pointer
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
#if 1
static void TCPDoDelayedWrite(GObject *source_object,GAsyncResult *res,gpointer user_data)
{
    DWORD   dwWrite;
    BOOL    retval;
    BOOL    writeflag;
    t_muxsockparam* TheComm;

    TheComm = (t_muxsockparam*)user_data;

    BMEventSet(&(TheComm->BMEv),BMEVENTDONEWR);

}
#endif

/*
**************************************************************************
*
*  FUNCTION: static int docallback(struct S_jdevice* pMe, APARAM wparam, APARAM lparam)
*
*  PURPOSE: callback from the thread
*
*  ARGUMENTS:
*   pMe:        Pointer to device structure for me
*   wparam:     qpkin
*   lparan:     NA
*
*  Comments:
*
*
**************************************************************************
*/

static void docallback(t_muxsockparam *TheComm, APARAM wparam, APARAM lparam)
{
    t_jdevice*  pParent;

    pParent = TheComm->pParent;
    if(! (pParent->callback) )                  // Check if there is a callback
        return;
    (pParent->callback)(pParent,wparam,lparam); // Ok, call it
}

/*
**************************************************************************
*
*  Function: DWORD WINAPI muxSocThread(LPVOID lpV)
*
*  Purpose: Comm thread function
*
*  Comments:
*
*  This is the TCP thread function. It handles TCP calls
*  for a connected stream. Routing is between an open
*  socket and muxctl (via DoMonStream)
*
*   Opening of a socket must be done before the thread is started
*   This is because acceptor sockets are opened by the
*   "accept" call and connectors are a more complex sequence.
*   Thus this thread assumes communications are initialized
*   and functional before it starts running to maintain them
*
*   Some of this is done in new_tcpdev when a structure is
*   created.  The socket handling is either in init_connect
*   or in the acceptor code.  Its a little messy now but
*   we aim to clean it up
*
**************************************************************************
*/
#if 1

void muxSocThread(LPVOID lpV)
{

    t_muxsockparam *TheComm;
    int        k;
    U32         eIn;
    t_dqueue*   qe;
    t_ctlreqpkt* pIn;
    BOOL       fThreadDone;
    BOOL       fWaitingOnRead;
    BOOL       fWaitingOnStat;
    BOOL       fWaitingOnWrite;

    TheComm = (t_muxsockparam *) lpV;
    fThreadDone = FALSE;

#if 0
    {
        unsigned char *puChar;
    k = sizeof(struct sockaddr);
    getpeername(TheComm->hSocket,&(TheComm->peer),&k);
        puChar = (unsigned char *)&(TheComm->peer.sa_data);
        sprintf(TheComm->rxbuf,"Connect to %d.%d.%d.%d",puChar[2],puChar[3],puChar[4],puChar[5]);
    AddLogMessage(TheComm->rxbuf,1);
    }
#endif
    fWaitingOnRead = FALSE;                     // no pending read
    fWaitingOnWrite = FALSE;                    // no pending write
    TheComm->pRecvQ = NULL;                     // receive packet is null
    TheComm->pXmitQ = NULL;                     // transmit packet is null
    TheComm->threadState = THREADREADY;

#if EASYWINDEBUG & 1
printf("muxSocThread Thread Started\n",1);
#endif
    while ( !fThreadDone )
    {

        //========if no read is outstanding, then issue another one
        if (!fWaitingOnRead)
        {
            fWaitingOnRead = TCPDoStartRead(TheComm);
        }

        //======== ignore input if waiting for a write
        if(fWaitingOnWrite)
            k=BMEVENTQUIT+BMEVENTDONEWR+BMEVENTDONERD;
        else
            k=BMEVENTQUIT+BMEVENTDONEWR+BMEVENTDONERD+BMEVENTNEWPKT;

        //======== Wait for something to happen
        eIn = BMEventWait(&(TheComm->BMEv),(U32)k,0);

#if EASYWINDEBUG & 0
printf("muxSocThread: eIn = 0x%08x\n",eIn);
#endif

        //========
        switch(eIn)
        {
            //========
            case BMEVENTNEWPKT:                 // Input data
            //========
            {
                fWaitingOnWrite = TCPDoStartWrite(TheComm);
            }
            break;

            //========
            case BMEVENTDONEWR:                 // Done writing
            //========
            {
                if(TheComm->pXmitQ)
                    ReleaseDataPacket(TheComm->pXmitQ,&gRouterCS); // Release this one
                TheComm->pXmitQ = NULL;
                fWaitingOnWrite = FALSE;
            }
            break;

            //========
            case BMEVENTDONERD:                 // Done reading
            //========
            {

                DoMonStream(TheComm->rxbuf,                  // Parse the stream
                            TheComm->nRead,
                            ROUTESRCTCP,
                            &(TheComm->pRecvQ),
                            &(TheComm->framer));

                TheComm->readPending = 0;
                fWaitingOnRead = 0;
#if EASYWINDEBUG & 0
printf("muxSocThread: BMEVENTDONERD = %d\n",TheComm->nRead);
#endif
            }
            break;

            //========
            case BMEVENTTOUT:                   // timeout
            //========
            {
            }
            break;

            //========
            case BMEVENTQUIT:                   // quit
            //========
            {
                fThreadDone = 1;
            }
            break;

            //========
            default:
            //========
            {
            }
            break;
        }

    }

    //======== Shutting down a thread
    TheComm->threadState = THREADINKILL;
    while(1)
    {
        t_dqueue*       pq;
        t_ctlreqpkt*    txctlpkt;
        t_ctldatapkt*   txdatapkt;

        pq = dequeue(&(TheComm->InQHeader.link),&gRouterCS);      // get the queue entry
        if(!pq)
            break;
        txctlpkt = structof(t_ctlreqpkt,link,pq); // get the control packet
        if(txctlpkt->tag == CTLREQMSGTAG_PDATAREQ)
        {
            txdatapkt = txctlpkt->pData;
            ReleaseDataPacket(txdatapkt,&gRouterCS); // Release old
        }
        ReleaseCtlPacket(txctlpkt,&gRouterCS);      // release the control packet
    }
    if(TheComm->pRecvQ)
        ReleaseDataPacket(TheComm->pRecvQ,&gRouterCS); // Release old
    if(TheComm->pXmitQ)
        ReleaseDataPacket(TheComm->pXmitQ,&gRouterCS); // Release old
    g_mutex_clear(&(TheComm->BMEv.e_mx));
    g_cond_clear(&(TheComm->BMEv.e_cond));

    //======== All done, bye
    TheComm->threadState = THREADIDLE;
    ThreadReturn(0);
}
#endif

