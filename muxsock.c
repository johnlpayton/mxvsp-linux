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
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <errno.h>

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
static void TCPDoDelayedWrite(t_muxsockparam* TheComm);

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
    int     k,e;

    //======== Set indirect calls
    pDev->state = 0;
    pDev->cmd = jdocmd;
    pDev->setparam = jsetparam;
    pDev->getparam = jgetparam;
    pDev->callback = cb;
    pDev->sigquit = jdosigquit;
    pDev->wctlpkt = jdowctlpkt;

    //======== Allocate working memory
    nPriv = sizeof(t_muxsockparam);
    pP = (t_muxsockparam*)malloc(nPriv);
    pDev->pPriv = pP;
    memset(pP,0,nPriv);
    pP->pParent = pDev;                          // reverse index

    //======== Signalling
    k=socketpair(AF_LOCAL, SOCK_DGRAM+SOCK_NONBLOCK, 0, pP->cmdsock);
    if(k < 0)
    {
        perror("new_tcpdev.socketpair");
        return(k);
    }

   //======== default parameters
    pP->sockParamFamily = AF_INET;
    pP->sockParamStream = SOCK_STREAM | SOCK_NONBLOCK;
    pP->sockParamProto = 0;                     // default
    pP->sockParamLport = inicfg.PortTCP;
    pP->hSocket = INVALID_SOCKET;


    //======== clear input queue
    initDqueue(&(pP->InQHeader.link),NULL);      // remember to add locking
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
static int
init_sockaddr (struct sockaddr_in *name,
               const char *hostname,
               U16 port)
{
    struct hostent *hostinfo;

    name->sin_family = AF_INET;
    name->sin_port = htons (port);
    hostinfo = gethostbyname (hostname);
    if (hostinfo == NULL)
    {
        fprintf (stderr, "Unknown host %s.\n", hostname);
        return(-1);
    }
    name->sin_addr = *(struct in_addr *) hostinfo->h_addr;
    return(1);
}


static int InitConnectSock(t_muxsockparam* TheComm, char* name)
{
    int iResult = 0;
    int k,e;
    DWORD arg;
    char tmpstr[64];
    int iFlag;
    struct sockaddr_in client_addr;

    //======== Resolve the server
    e = init_sockaddr(
            &client_addr,
            TheComm->sockParamRName,
            TheComm->sockParamRport);
    if(e < 0)
    {
        perror("InitConnectSock.init_sockaddr");
        exit(-2);

    }

    //======== Get a socket, use blocking mode
    TheComm->sockID = socket(
        AF_INET,
        SOCK_STREAM,
        0);
    if(TheComm->sockID < 0)
    {
        perror("InitConnectSock.sOcket");
        exit(-2);
    }
#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_TCP)
{
printf("InitConnectSock.socket: %d\n", TheComm->sockID);

}
#endif

    //======= Connect
    e = connect(TheComm->sockID, (struct sockaddr *)&client_addr, sizeof(client_addr));
    if(e < 0)
    {
        perror("InitConnectSock.connect");
        exit(-2);
    }
#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_TCP)
{
printf("InitConnectSock.connected: %d\n", TheComm->sockID);

}
#endif

#if 0
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
#endif

    TheComm->readPending = 0;
    TheComm->hTCP = SpawnThread(muxSocThread,0,TheComm);

    return(1);
}

/*---------------------------------------------------------------------------
*  int InitAcceptSock(t_muxsockparam* TheComm)
*
*  Description:
*   Initialize the acceptor side
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

static int InitAcceptSock(t_muxsockparam* TheComm, int sock)
{
    int iResult = 0;
    int k;
    DWORD arg;
    char tmpstr[64];
    int iFlag;

    TheComm->sockID = sock;
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
    char    msg[4];


    if(!pMe)
        return 0;

    pS = (t_muxsockparam*)(pMe->pPriv);
    if(!pS)
        return 0;

    qpkIn =(t_ctlreqpkt*)wparam;

#if 1
    LockSection(&gRouterCS);
    enqueue(&(pS->InQHeader.link),&(qpkIn->link),NULL); // link it in
    msg[0] = EVBCMD_FROMROUTER;
    write(pS->cmdsock[0],msg,1);
    UnLockSection(&gRouterCS);
#endif
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

    //========
    pDev->state = 0;

    if(!pDev->pPriv)
        return 0;

     pP = pDev->pPriv;


    if(pP->sockID > 0)
    {
        close(pP->sockID);
        pP->sockID = 0;
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
            pS->sockID = (int)lparam;
//            printf("sockCmdmuxSockSpawn: lparam=%d\n",lparam);
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
    int     dwRead;
    int     e;
    char*   bufin = TheComm->rxbuf;


    if(TheComm->threadState != THREADREADY)     // Are we shutting down
    {
        return TRUE;                            // pretend we have a pending read
    }
#if 0
    if(TheComm->readPending)                    // Pending read
    {
        return TRUE;
    }
#endif

    dwRead=read(TheComm->sockID, bufin, MAXDATASIZE);
#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_TCP)
{
printf("TCPDoStartRead: bufin 0x%08x dwRead= %d\n",(U32)bufin, dwRead);
}
#endif

    if (dwRead > 0)
    {
       DoMonStream(bufin,                  // Parse the upstream
                    dwRead,
                    ROUTESRCTCP,
                    &(TheComm->pRecvQ),
                    &(TheComm->framer));
    }
    else
    {
        e = errno;
        if( (dwRead == 0) &&
            ( (e == EAGAIN) || (e == EWOULDBLOCK)))
        {
            retval = FALSE;                      //  pending read
        }
        else
        {
            TheComm->threadState = THREADINKILL;
        }
    }

    retval = FALSE;                      //  pending read
    return retval;
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
#if (EASYWINDEBUG>5) && 1
printf("TCPDoStartWrite: Empty\n");
#endif
        TheComm->pXmitQ = NULL;
        return(FALSE);
    }

    txctlpkt = structof(t_ctlreqpkt,link,qe);   // get the control packet
#if (EASYWINDEBUG>5) && 1
jlpDumpCtl("TCPDoStartWrite:",txctlpkt);
#endif
    txdatapkt = txctlpkt->pData;                // finally the data packet
    ReleaseCtlPacket(txctlpkt,&gRouterCS);     // release the control packet

    // Check the data
    pTxd = txdatapkt->pData;
    nTxd = txdatapkt->nData;

    if(nTxd <= 0)                               // Empty?
    {
#if (EASYWINDEBUG>5) && 1
printf("TCPDoStartWrite: Length=0\n");
#endif
        ReleaseDataPacket(txdatapkt,&gRouterCS); // Release it
        TheComm->pXmitQ = NULL;
        retval = FALSE;
    }
    else                                        // data is framed
    {
#if (EASYWINDEBUG>5) && 1
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

    //======== Need to do delayes stuff here but for now, lets just releas the packet
    write(TheComm->sockID,pTxd,nTxd);

    //======== Assume it is written and release
    TCPDoDelayedWrite(TheComm);

#if (EASYWINDEBUG>5) && 1
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
static void TCPDoDelayedWrite(t_muxsockparam* TheComm)
{
    DWORD   dwWrite;
    BOOL    retval;
    BOOL    writeflag;


#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_TCP)
{
printf("TCPDoDelayedWrite:\n");
}
#endif
    if(TheComm->pXmitQ)
    {
        ReleaseDataPacket(TheComm->pXmitQ,&gRouterCS); // Release old
        TheComm->pXmitQ = NULL;
    }

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

static BOOL DoSockCommand(t_muxsockparam* TheComm)
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
printf("DoSockCommand: Sock e=%d cmubuf[0]=%d\n", e, 0xff&cmdbuf[0]);
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
printf("DoSockCommand:fetch\n", e, 0xff&cmdbuf[0]);
}
#endif
                //======== forward it to the net
                TCPDoStartWrite(TheComm);
            }
            break;

            //======== Write buffer to the uart
            case EVBCMD_WRITETOUART: // never det here
            //========
            {
                TCPDoStartWrite(TheComm);
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
                printf("DoSockCommand: bad command B\n");
                ErrorInComm("DoSockCommand: bad command B");
            }
            break;
        }
    }
    return (retval);
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
    fd_set  rfds;                   // receive file descriptors
    fd_set  tfds;                   // transmit file descriptors
    fd_set  exfds;                  // exception file descriptors
    struct timeval tv;              // timeout for wait
    int     maxfd;                  // temp to file the max
    int     retval;
    int     e;                      // another temp
    char    cmdbuf[4];

    //======== type convert our params
    TheComm = (t_muxsockparam *) lpV;

    //======== set to non blocking
    fcntl(TheComm->sockID, F_SETFL,O_NONBLOCK);


    //======== junk left over
    fThreadDone = FALSE;
    fWaitingOnRead = FALSE;                     // no pending read
    fWaitingOnWrite = FALSE;                    // no pending write
    TheComm->pRecvQ = NULL;                     // receive packet is null
    TheComm->pXmitQ = NULL;                     // transmit packet is null
    TheComm->threadState = THREADREADY;

#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_EVB)
{
printf("muxSocThread Thread Started sockID=%d\n",TheComm->sockID);
}
#endif
    while ( (!fThreadDone ) && (TheComm->threadState == THREADREADY))
    {

        FD_ZERO(&rfds);             // clear our descriptors
        FD_ZERO(&tfds);
        FD_ZERO(&exfds);

        FD_SET(TheComm->cmdsock[1], &rfds);  // socket in
        FD_SET(TheComm->sockID, &rfds);      // net in
        FD_SET(TheComm->sockID, &tfds);      // net out

        maxfd = TheComm->cmdsock[1];
        if(maxfd < TheComm->sockID)                // find the max fild descriptor
            maxfd = TheComm->sockID;
        tv.tv_sec = 1;              // 1 second wait
        tv.tv_usec = 0;


#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_EVB)
{
if(e != -2)
    printf("muxSocThread:switch in, maxfd= %d\n",maxfd);
}
#endif
//        maxfd = 4;
        if(!(fWaitingOnWrite))
            e = select(maxfd+1, &rfds, NULL, NULL, &tv);
        else
            e = select(maxfd+1, &rfds, &tfds, NULL, &tv);
#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_TCP)
{
if(e != 0)
    printf("muxSocThread:switch out, e= %d\n",e);
}
#endif

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
            if(FD_ISSET(TheComm->sockID,&rfds)) // Net in
            //========
            {
#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_TCP)
{
printf("muxSocThread: fWaitingOnRead\n");
}
#endif
                TCPDoStartRead(TheComm);
            }
            //======== This is shared in the select, so apply fWaitingOnWrite logic
            else if((fWaitingOnWrite) &&(FD_ISSET(TheComm->sockID,&tfds))) // Uart empty
            //========
            {
                TCPDoDelayedWrite(TheComm); // finish the write
#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_TCP)
{
printf("muxSocThread: TCPDoDelayedWrite fWaitingOnWrite %d\n",fWaitingOnWrite);
}
#endif
            }
            //========
            else if(FD_ISSET(TheComm->cmdsock[1],&rfds)) // command socket ?
            //========
            {
                //======== get a command
                DoSockCommand(TheComm);
            }
            //========
            else    // place holder
            {
                printf("muxSocThread.select Bad\n");
                ErrorInComm("muxSocThread: WaitForMultipleObjects");
            }
        }
        //========
        else // timeout
        //========
        {
            //======== dec counter
            e = -2;
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
    {
        ReleaseDataPacket(TheComm->pRecvQ,&gRouterCS); // Release old
        TheComm->pRecvQ = NULL;
    }
    if(TheComm->pXmitQ)
    {
        ReleaseDataPacket(TheComm->pXmitQ,&gRouterCS); // Release old
        TheComm->pXmitQ = NULL;
    }

    //======== All done, bye
    TheComm->threadState = THREADIDLE;
    ThreadReturn(0);
}
#endif

