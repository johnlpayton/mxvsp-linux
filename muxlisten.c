/*******************************************************************************
*   FILE: muxlisten.c
*
*   Implement a listen thread
*       1) Listen for a connection on port
*       2) Accept and spawn connections
*       3) Reap dead connections
*
*   This is a plain C thread and listener
*
*   the only hooks right now are the locking calls to the router mutex
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


extern void SimRouter(t_ctlreqpkt* pkIn);
extern void FatalError(char*);
extern void ErrorInComm(char * szMessage);
static void TCPKillThread(t_muxsockparam* TheComm, int code);
void muxSocKListen(LPVOID lpV);
extern void MsgToMain(void* dev, int A, int B);

static int jsetparam(struct S_jdevice* pMe, APARAM wparam, APARAM lparam);
static int jgetparam(struct S_jdevice* pMe, APARAM wparam, APARAM lparam);
static int jdocmd(struct S_jdevice* pMe, APARAM wparam, APARAM lparam);
static int jdowctlpkt(struct S_jdevice* pMe, APARAM wparam, APARAM lparam);
static int jdowctlpktloop(struct S_jdevice* pMe, APARAM wparam, APARAM lparam);
static int jdosigquit(struct S_jdevice* pMe, APARAM wparam, APARAM lparam);
//static void docallback(t_muxsockparam *TheComm, APARAM wparam, APARAM lparam);

static int doRing(t_muxsockparam* TheComm);
static int doHomeless(t_muxsockparam* TheComm);

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
int new_listener(t_jdevice* pDev, pJfunction cb)
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

    //======== Allocate working memory
    nPriv = sizeof(t_muxsockparam);
    pP = (t_muxsockparam*)malloc(nPriv);
    pDev->pPriv = pP;
    memset(pP,0,nPriv);
    pP->pParent = pDev;                          // reverse index

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
*  int InitListenSock(t_muxsockparam* TheComm)
*
*  Description:
*   Initialize the listener side
*
*  Parameters:
*     none
*
*  Comments
*   Initialization
*   Create a socket
*   bind
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

static int InitListenSock(t_muxsockparam* TheComm, char* name)
{
    int iResult = 0;
    int k,e;
    DWORD arg;
    char tmpstr[64];
    int iFlag;
    struct sockaddr_in serv_addr;


    //======== Allocate a socket connection (must g_object_unref when done)
//    TheComm->hSocket = g_socket_client_new();
    TheComm->sockID = socket(
        AF_INET,
        SOCK_STREAM | SOCK_NONBLOCK,
        0);
    if(TheComm->sockID < 0)
    {
        perror("InitListenSock.sOcket");
        exit(-2);
    }
#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_TCP)
{
printf("InitListenSock.socket: %d\n", TheComm->sockID);

}
#endif

    //======== Bind at the listenng address
     serv_addr.sin_family = AF_INET;
     serv_addr.sin_addr.s_addr = INADDR_ANY;
     serv_addr.sin_port = htons(TheComm->sockParamLport);

     e = bind(TheComm->sockID, (struct sockaddr *) &serv_addr,
              sizeof(serv_addr));
    if(e < 0)
    {
        perror("InitListenSock.bind");
        exit(-2);

    }
#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_TCP)
{
printf("InitListenSock.bind: %d\n", e);

}
#endif

    //======== Start Listen
    e = listen(TheComm->sockID,5);
    if(e < 0)
    {
        perror("InitListenSock.bind");
        exit(-2);

    }
#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_TCP)
{
printf("InitListenSock.listen: %d\n", e);

}
#endif

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

    return 0;
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
            //retval = InitConnectSock(pS,(char*) lparam);

        }
        break;

        //========
        case sockCmdInitListenerSock:
        //========
        {
            retval = InitListenSock(pS,"MXVSP");
        }
        break;

        //========
        case sockCmdmuxSockListen:
        //========
        {
            pMe->hDevice = SpawnThread(muxSocKListen,0,pS);
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
            pS->sockParamLport = (U32)lparam;
        }
        break;
        //========
        case sockParamStream:
        //========
        {
            pS->sockParamLport = (U32)lparam;
        }
        break;

        //========
        case sockParamProto:
        //========
        {
            pS->sockParamLport = (U32)lparam;
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
*  Function: static BOOL doRing(t_muxsockparam* TheComm)
*
*  Purpose: Called to accept a socket
*
*  Arguments:
*   t_muxsockparam* pAsyn   Structure pointer
*
*  Comments:
*
*
**************************************************************************
*/
#if 1
static int doRing(t_muxsockparam* TheComm)
{
    int    retval;
    int     k;
    int     news;
    t_jdevice *pD;
    struct sockaddr_in clientname;
    GSocket* pGS;


#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_TCP)
{
printf("muxSocKListen: ring\n",1);
}
#endif

    for(k=0;k<10; k++)
    {
        //======== check if a data structure exists
        if(!RemClients[k].pPriv)
            break;

    }
    if(k==10)
        return(0);

    //======== create a device entry
    pD = &RemClients[k];
    new_tcpdev(pD,(pJfunction) MsgToMain);

    k = sizeof (clientname);

    //========
    news = accept(TheComm->sockID,
            (struct sockaddr *) &clientname,
             &k);
    if(news < 0)
    {
        perror ("accept");
        exit (EXIT_FAILURE);
    }

    //======== Spawn (uses call from new_tcpdev)
    pD->cmd(pD, sockCmdmuxSockSpawn, news);


    retval = TRUE;
    return retval;
}
#endif


/*
**************************************************************************
*
*  Function: static int doHomeless(t_muxsockparam* TheComm)
*
*  Purpose: scak for homeless threads and reap them
*
*  Arguments:
*   t_muxsockparam* pAsyn   Structure pointer
*
*  Return
*       TRUE:   delayed write is pending
*       FALSE:  no delayed write is pending
*
*  Comments:
*
**************************************************************************
*/
#if 1
static int doHomeless(t_muxsockparam* TheComm)
{
    int    retval;
    return TRUE;
}
#endif



/*
**************************************************************************
*
*  Function: DWORD WINAPI muxSocKListen(LPVOID lpV)
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

void muxSocKListen(LPVOID lpV)
{

    t_muxsockparam *TheComm;

    int     k;
    int     e;
    fd_set  rfds;                   // receive file descriptors
    fd_set  tfds;                   // transmit file descriptors
    fd_set  exfds;                  // exception file descriptors
    struct timeval tv;              // timeout for wait
    int     maxfd;                  // temp to file the max

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

#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_TCP)
{
printf("muxSocKListen: Thread Started socket = %d\n",TheComm->sockID);
}
#endif
    while ( !fThreadDone )
    {
        FD_ZERO(&rfds);             // clear our descriptors
        FD_ZERO(&tfds);
        FD_ZERO(&exfds);

        FD_SET(TheComm->sockID, &rfds);  // Accept
        maxfd = TheComm->sockID;
        tv.tv_sec = 1;              // 1 second wait
        tv.tv_usec = 0;

        //======== Wait for an input
        e = select(maxfd+1, &rfds, NULL, NULL, &tv);
#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_TCP)
{
if(e != 0)
    printf("muxSocKListen:switch out, e= %d\n",e);
}
#endif

        //========
        if (e == -1) // Special for error trap
        //========
        {
            perror("C) select()");
        }
        //========
        else if(e > 0) // Ringing
        //========
        {
            doRing(TheComm);
        }
         //========
        else // timeout
        //========
        {
            //======== Scan for homeless

            doHomeless(TheComm);
           e = -2;
        }
    }

    //======== Shutting down a thread
    TheComm->threadState = THREADINKILL;
    if(TheComm->pRecvQ)
        ReleaseDataPacket(TheComm->pRecvQ,&gRouterCS); // Release old
    if(TheComm->pXmitQ)
        ReleaseDataPacket(TheComm->pXmitQ,&gRouterCS); // Release old

    //======== All done, bye
    TheComm->threadState = THREADIDLE;
    ThreadReturn(0);
//    pthread_exit(0);
}
#endif

