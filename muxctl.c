/*******************************************************************************
*   FILE: muxctl.c
*
*   Main router for packets.
*   Memory and buffer management.
*
*   Basic design is built around Packet buffers.
*
*   Prior to entering a mux, all packets have been processed
*   and framed with DLE-STX/DLE-ETX pairs.  Source tags are
*   "OOB" and carried in the structure.  Destinations are inherent
*   from the source and opmode.
*   opmode:
*       MUXMODEDIRECT
*         TYPEMUXPORTEVB -> local window
*         TYPEMUXPORTWND -> EVB
*         TYPEMUXPORTTCP -> na.
*       MUXMODECLIENT
*         TYPEMUXPORTEVB -> na.
*         TYPEMUXPORTWND -> TCP
*         TYPEMUXPORTTCP -> local window
*       MUXMODESERVER
*         TYPEMUXPORTEVB -> local window
*                        -> All TCP
*         TYPEMUXPORTWND -> EVB
*         TYPEMUXPORTTCP -> EVB
*
*   So what does this guy do?
*   Bascally he accepts packets from the frasmer then sends them to
*   destinations. These are TCP,EVB,VSP and MAIN.  In the current
*   WIN32 implementation VSP is MAIN but thats not important
*   EVB and TCP have queued/semaphore interface. MAIN,VSP are handled
*   by a W32 PostMessage.  Also MAIN has dialogs that are called directly.
*   The upload/download dialogs are virtual VSPs.
*   muxctl opens TCP and EVB so they set up callinto write routines
*   in their structures (t_jdevice) We need VSP and dialogs to be
*   set up by main for use by muxctl.  Musctl is a child of main
*
*   Externals
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
#include "muxevb.h"
#include "muxsock.h"
//#include "MTTTY.H"
#include "muxw2ctl.h"


struct S_inicfg inicfg;

t_muxrouteinfo routetab[MAXROUTETAB];
int     iMuxMode;

HANDLE  hRouter;                                 // Handle to the thread
int     RouterActive;                           // flag for start sequencing
HANDLE  RouteExitEvent;                          // exit event
//t_semaphore  RouteInputSem;                           // input semaphore for the router
//t_ctlreqpkt RouteInQHeader;                     // header for the router input
t_mailbox   RouteInMail;



t_jdevice RemClients[10];                      // Remote host
t_jdevice jDevEvb;                               // Remote host
t_jdevice jDevServer;                               // Remote host
int     nRemClients;

//======== External calls
extern void MsgToMain(void* dev, int A, int B);
pJfunction  PostPktToLocal;
pJfunction  PostMsgToLocal;

static void SimRouter(t_ctlreqpkt* pkIn);
static void RouterCleanUp(void);
void ClientRouterGTK(void* lpV);
void DirectRouterGTK(void* lpV);
void muxSocKListen(LPVOID lpV);

//======== debug
extern void jlpChkDataPktHdr(char* title, t_ctldatapkt* pDpk);
extern void FatalError(char* s);


#if USEGDK
int InitMuxCtl(pJfunction packetToLocal,pJfunction msgToLocalToMain)
{
    int k;
    int retval = 1;

    PostPktToLocal = packetToLocal;



    // clear client memory
    memset(&RemHost,0,sizeof(RemHost));

    // set router up
    RouterActive = 0;

//    initDqueue(&(RouteInQHeader.link),NULL);
//    InitSemaphore(&RouteInputSem);
    InitCtlMailBox(&RouteInMail);

#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_ROUTER)
{
printf("RouterCtl started\n");
}
#endif

    //======== Spawn the router
    switch(inicfg.muxmode)
    {
        //========
        default:
        case MUXMODEDIRECT:
        //========
            hRouter = (HANDLE)SpawnThread(DirectRouterGTK, 0, NULL);
            newEVB(&jDevEvb, (pJfunction)MsgToMain);
            sleep(2);
            jDevEvb.cmd(&jDevEvb, EVBcmdOpen, 0);
        break;

        //========
        case MUXMODECLIENT:
        //========
            hRouter = (HANDLE)SpawnThread(ClientRouterGTK,0,NULL);
            new_tcpdev(&RemHost, (pJfunction) MsgToMain);
#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_ROUTER)
{
    printf("Host <%s> Pord %d\n",inicfg.NameSrvr,inicfg.PortTCP);
}
#endif

            RemHost.setparam(&RemHost,(APARAM)sockParamRName,(BPARAM)inicfg.NameSrvr);
            RemHost.setparam(&RemHost,(APARAM)sockParamRport,(BPARAM)inicfg.PortTCP);
            sleep(2);
            RemHost.cmd(&RemHost,(APARAM)sockCmdInitConnectSock,(BPARAM)0);
        break;

        //========
        case MUXMODESERVER:
        //========
            hRouter = (HANDLE)SpawnThread(ServerRouter, 0, NULL);
            new_listener(&jDevServer,(pJfunction) MsgToMain);
#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_ROUTER)
{
    printf("Starting server mode\n");
}
#endif
            sleep(2);
            jDevServer.cmd(&jDevServer,(APARAM)sockCmdInitListenerSock,(BPARAM)0);
            jDevServer.cmd(&jDevServer,(APARAM)sockCmdmuxSockListen,(BPARAM)0);

            newEVB(&jDevEvb, (pJfunction)MsgToMain);
            sleep(2);
            jDevEvb.cmd(&jDevEvb, EVBcmdOpen, 0);

        break;
    }
    RouterActive = 1;
    return(retval);
}
#endif
/*
**************************************************************************
*
*  Function: int SendFrameToRouter(t_ctldatapkt* p, int srcID)
*
*  Purpose: Sends a request to the router
*
*  Comments:
*
*   This is basically assumming some global variables that are unique
*   exist
*   A) Lock for router
*   B) Lock for control packets
*   C) Input queue for router
*   D) Semaphore for router
*
**************************************************************************
*/

int SendFrameToRouter(t_ctldatapkt* p, int srcID)
{
    t_ctlreqpkt* pSend;
    int k;

    pSend = AllocCtlPacket(&gRouterCS);        // get a control header
    if(!pSend)
        FatalError("SendFrameToRouter: AllocCtlPacket failed");

    pSend->from = (srcID&0xf);                  // from ID (kill upper bits)
    pSend->pData = p;                           // payload

#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_ROUTER)
{
jlpDumpCtl("SendFrameToRouterB",pSend);
}
#endif

#if 0
    LockSection(&gRouterCS);
    {
        enqueue(&(RouteInQHeader.link),&(pSend->link),NULL); // link it in
//        ReleaseSemaphore(RouteInputSem,1,&k);      // sigal arrival
        SignalSemaphore(&RouteInputSem);
//        ClientRouterGTK(NULL);
    }
    UnLockSection(&gRouterCS);
#endif
    ForwardCtlMail(&RouteInMail,pSend);

    return(1);
}


/*---------------------------------------------------------------------------
*  static void RouterCleanUp(void)
*
*  Description:
*   Clean up when the router thread exits
*
*  Parameters:
*
*   Returns
*
*  Comments
*   Common clean up when a thread exits
*
*   Open phase
*
*  History:   Date       Author      Comment
*  13-Dec-2012 jlp
*   initial pass
*
*---------------------------------------------------------------------------*/
static void RouterCleanUp(void)
{
    int k;
    t_ctlreqpkt*    pCpkt;              // reduce typing
    t_ctldatapkt*   pDpkt;
    t_dqueue*       qe;
    char*           pFrame;

    //======== remove data from the queues

#if 0
    LockSection(&gRouterCS);                    // Lock for the duration
    while(1)
    {
        qe = dequeue(&RouteInQHeader.link,NULL);
        if(!qe)
            break;
        pCpkt = structof(t_ctlreqpkt,link,qe);  // Back reference to the packet
        pDpkt = pCpkt->pData;                   // dereference to the data header
        ReleaseCtlPacket(pCpkt,NULL);
        ReleaseDataPacket(pDpkt,NULL);
    }
    UnLockSection(&gRouterCS);
#endif
    RouterActive = 0;
}
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
*   Actually, it IS a thread but does not really need to be... oh well
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
void ClientRouterGTK(void* lpV)
{
    HANDLE hArray[2];
    DWORD dwRes;
    DWORD dwSize;
    BOOL fDone = FALSE;
    t_ctldatapkt* qDpkt;
    t_ctlreqpkt* qpkIn;
    t_dqueue* qe;
    int     k;
    int     retval;


#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_ROUTER)
{
printf("ClientRouterGTK started\n");
}
#endif
    while(!fDone)
    {

        qpkIn = AcceptCtlMail(&RouteInMail);

#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_ROUTER)
{
jlpDumpCtl("ClientRouter Gets: ",qpkIn);
}
#endif
        if(qpkIn->tag != CTLREQMSGTAG_PDATAREQ)
        {
            ReleaseCtlPacket(qpkIn,&gRouterCS);
            continue;
        }

        switch(qpkIn->from)
        {
            //========
            default:
            //========
            {
                t_ctldatapkt*   pDpkt;

#if EASYWINDEBUG & 1
if(inicfg.prflags & PRFLG_ROUTER)
{
char tmpstr[64];
sprintf(tmpstr,"Bad routing source 0x%08x(0x%08x)",(U32)qpkIn,(U32)qpkIn->from);
}
                pDpkt = qpkIn->pData;               // dereference to the data header
                ReleaseDataPacket(pDpkt,&gRouterCS);     // Release old
                ReleaseCtlPacket(qpkIn,&gRouterCS);

#endif
//                FatalError(tmpstr);

            }
            break;

            //========
            case ROUTESRCLOCAL:         // Local -> TCP
            case ROUTESRCCMDS:          // CMDS->TCP
            //========
            {
#if EASYWINDEBUG & 1
if(inicfg.prflags & PRFLG_ROUTER)
{
jlpChkDataPktHdr("ClientRouter: ROUTESRCLOCAL -> TCP",qpkIn->pData);
jlpDumpCtl("ClientRouter: ROUTESRCLOCAL -> TCP",qpkIn);
jlpDumpFrame("Packet:", qpkIn->pData->pData, qpkIn->pData->nData);
printf("inicfg.muxmode %d\n",inicfg.muxmode);
}
#endif

            //======= Normal: post to TCP
                k = RemHost.cmd(&RemHost,sockCmdGetThreadState, 0);
                if(k != THREADREADY)
                    break;
                RemHost.wctlpkt(&RemHost, (APARAM)qpkIn, 0);
            }
            break;

            //========
            case ROUTESRCTCP:           // TCP   -> Local
            //========
            {
#if EASYWINDEBUG & 1
if(inicfg.prflags & PRFLG_ROUTER)
{
jlpChkDataPktHdr("ClientRouter: ROUTESRCTCP -> Local",qpkIn->pData);
jlpDumpCtl("ClientRouter: ROUTESRCTCP -> Local",qpkIn);
jlpCtlRoute("ClientRouter.1",qpkIn);
}
#endif
                (*PostPktToLocal)(NULL,(APARAM)qpkIn,0);
            }
            break;

            //========
            case ROUTESRCEVB:           // EVB na
            //========
            {
#if EASYWINDEBUG & 1
if(inicfg.prflags & PRFLG_ROUTER )
{
jlpChkDataPktHdr("ClientRouter: ROUTESRCEVB -> Local",qpkIn->pData);
jlpDumpCtl("ClientRouter: ROUTESRCEVB -> Local",qpkIn);
jlpCtlRoute("ClientRouter.2",qpkIn);
}
#endif
                // Write to the local port
                (*PostPktToLocal)(NULL,(APARAM)qpkIn,0);
            }
            break;
        }
    }

    // need to clean all outstanding resources threads etc.

    FlushCtlMail(&RouteInMail);

#if EASYWINDEBUG & 0
if(inicfg.prflags & PRFLG_ROUTER)
{
printf("ClientRouter:Exiting\n");
}
#endif

    ThreadReturn(1);
}

#endif

/*---------------------------------------------------------------------------
*  static int  DirectRouterGTK(void* lpV)
*
*  Description:
*   GTK Router for the direct mode
*
*  Parameters:
*     none
*
*   Returns
*       1
*
*  Comments
*
*  History:   Date       Author      Comment
*  2016-5-1 jlp
*   initial pass
*
*---------------------------------------------------------------------------*/
#if USEGDK
void DirectRouterGTK(void* lpV)
{
    HANDLE hArray[2];
    DWORD dwRes;
    DWORD dwSize;
    BOOL fDone = FALSE;
    t_ctldatapkt* qDpkt;
    t_ctlreqpkt* qpkIn;
    t_dqueue* qe;
    int     k;
    int     retval;


#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_ROUTER)
{
printf("DirectRouterGTK started\n");
}
#endif
    while(!fDone)
    {

        qpkIn = AcceptCtlMail(&RouteInMail);

#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_ROUTER)
{
//jlpDumpCtl("DirectRouterGTK Gets: ",qpkIn);
printf("DirectRouterGTK Gets: 0x%06x\n",qpkIn);
}
#endif
        if(qpkIn->tag != CTLREQMSGTAG_PDATAREQ)
        {
            ReleaseCtlPacket(qpkIn,&gRouterCS);
            continue;
        }

        switch(qpkIn->from)
        {
            //========
            default:
            //========
            {
                t_ctldatapkt*   pDpkt;

#if EASYWINDEBUG & 1
if(inicfg.prflags & PRFLG_ROUTER)
{
char tmpstr[64];
sprintf(tmpstr,"Bad routing source 0x%08x(0x%08x)",(U32)qpkIn,(U32)qpkIn->from);
}
                pDpkt = qpkIn->pData;               // dereference to the data header
                ReleaseDataPacket(pDpkt,&gRouterCS);     // Release old
                ReleaseCtlPacket(qpkIn,&gRouterCS);

#endif
//                FatalError(tmpstr);

            }
            break;

            //========
            case ROUTESRCLOCAL:         // Local -> EVB
            //========
            {
#if EASYWINDEBUG & 1
if(inicfg.prflags & PRFLG_ROUTER)
{
jlpChkDataPktHdr("DirectRouterGTK: ROUTESRCLOCAL -> EVB",qpkIn->pData);
jlpDumpCtl("DirectRouterGTK: ROUTESRCLOCAL -> EVB",qpkIn);
jlpDumpFrame("Packet:", qpkIn->pData->pData, qpkIn->pData->nData);
}
#endif
                jDevEvb.wctlpkt(&jDevEvb, (APARAM)qpkIn, 0);
            }
            break;
            //========
            case ROUTESRCCMDS:          // CMDS->bb
            //========
            {
#if EASYWINDEBUG & 1
if(inicfg.prflags & PRFLG_ROUTER)
{
jlpChkDataPktHdr("DirectRouterGTK: ROUTESRCCMDS ->  ",qpkIn->pData);
jlpDumpCtl("DirectRouterGTK: ROUTESRCCMDS -> ",qpkIn);
jlpDumpFrame("Packet:", qpkIn->pData->pData, qpkIn->pData->nData);
}
#endif
#if 0
            //=======
                k = RemHost.cmd(&RemHost,sockCmdGetThreadState, 0);
                if(k != THREADREADY)
                    break;
                RemHost.wctlpkt(&RemHost, (APARAM)qpkIn, 0);
#endif
            }
            break;

            //========
            case ROUTESRCTCP:           // TCP   -> bb
            //========
            {
#if EASYWINDEBUG & 1
if(inicfg.prflags & PRFLG_ROUTER)
{
jlpChkDataPktHdr("DirectRouterGTK: ROUTESRCTCP -> Local",qpkIn->pData);
jlpDumpCtl("DirectRouterGTK: ROUTESRCTCP -> Local",qpkIn);
jlpCtlRoute("DirectRouterGTK.1",qpkIn);
}
#endif
//                (*PostPktToLocal)(NULL,(APARAM)qpkIn,0);
            }
            break;

            //========
            case ROUTESRCEVB:           // EVB -> local
            //========
            {
#if EASYWINDEBUG & 1
if(inicfg.prflags & PRFLG_ROUTER )
{
jlpChkDataPktHdr("DirectRouterGTK: ROUTESRCEVB -> Local",qpkIn->pData);
jlpDumpCtl("DirectRouterGTK: ROUTESRCEVB -> Local",qpkIn);
jlpCtlRoute("DirectRouterGTK.2",qpkIn);
}
#endif
                // Write to the local port
                (*PostPktToLocal)(NULL,(APARAM)qpkIn,0);
            }
            break;
        }
    }

    // need to clean all outstanding resources threads etc.

    FlushCtlMail(&RouteInMail);

#if EASYWINDEBUG & 0
if(inicfg.prflags & PRFLG_ROUTER)
{
printf("DirectRouterGTK:Exiting\n");
}
#endif

    ThreadReturn(1);
}

/*---------------------------------------------------------------------------
*  static int  DirectRouterGTK(void* lpV)
*
*  Description:
*   GTK Router for the direct mode
*
*  Parameters:
*     none
*
*   Returns
*       1
*
*  Comments
*
*  History:   Date       Author      Comment
*  2016-5-1 jlp
*   initial pass
*
*---------------------------------------------------------------------------*/

void ServerRouter(void* lpV)
{
    HANDLE hArray[2];
    DWORD dwRes;
    DWORD dwSize;
    BOOL fDone = FALSE;
    t_ctldatapkt* qDpkt;
    t_ctlreqpkt* qpkIn;
    t_dqueue* qe;
    int     k;
    int     retval;

/********
 ** Open a listening socket here
*********/

#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_ROUTER)
{
printf("ServerRouter started\n");
}
#endif

    while(!fDone)
    {
        //======== Get the next control packet in the queue
        qpkIn = AcceptCtlMail(&RouteInMail);

#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_ROUTER)
{
printf("ServerRouter Gets: 0x%06x tag %d\n",qpkIn,qpkIn->tag);
//jlpDumpCtl("ServerRouter Gets:",qpkIn);
//jlpDumpFrame("Frame is",qpkIn->pData,16);
}
#endif
        //======== Bug check the tag
        if(qpkIn->tag != CTLREQMSGTAG_PDATAREQ)
        {
            ReleaseCtlPacket(qpkIn,&gRouterCS);
            continue;
        }

        //======== Routing done by the source
        switch(qpkIn->from)
        {
            //========
            default:
            //========
            {
                t_ctldatapkt*   pDpkt;

#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_ROUTER)
{
char tmpstr[64];
sprintf(tmpstr,"Bad routing source 0x%08x(0x%08x)",(U32)qpkIn,(U32)qpkIn->from);
}
                pDpkt = qpkIn->pData;               // dereference to the data header
                ReleaseDataPacket(pDpkt,&gRouterCS);     // Release old
                ReleaseCtlPacket(qpkIn,&gRouterCS);

#endif
//                FatalError(tmpstr);

            }
            break;

            //========
            case ROUTESRCLOCAL:         // Local -> EVB
            //========
            {
#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_ROUTER)
{
jlpChkDataPktHdr("ServerRouter: ROUTESRCLOCAL -> EVB",qpkIn->pData);
jlpDumpCtl("ServerRouter: ROUTESRCLOCAL -> EVB",qpkIn);
jlpDumpFrame("Packet:", qpkIn->pData->pData, qpkIn->pData->nData);
}
#endif
                jDevEvb.wctlpkt(&jDevEvb, (APARAM)qpkIn, 0);
            }
            break;

            //========
            case ROUTESRCTCP:           // TCP   -> evb
            //========
            {
#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_ROUTER)
{
jlpChkDataPktHdr("ServerRouter: ROUTESRCTCP -> EVB",qpkIn->pData);
jlpDumpCtl("ServerRouter: ROUTESRCTCP -> EVB",qpkIn);
jlpCtlRoute("ServerRouter.1",qpkIn);
}
#endif
                jDevEvb.wctlpkt(&jDevEvb, (APARAM)qpkIn, 0);
            }
            break;

            //========
            case ROUTESRCCMDS:          // CMDS->tcp
            case ROUTESRCEVB:           // EVB -> tcp,local
            //========
            {
                int kflag;
#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_ROUTER )
{
jlpChkDataPktHdr("ServerRouter: ROUTESRCEVB -> TCP,Local",qpkIn->pData);
jlpDumpCtl("ServerRouter: ROUTESRCEVB -> TCP,Local",qpkIn);
jlpCtlRoute("ServerRouter.2",qpkIn);
}
#endif
                LockSection(&gRouterCS);
                qDpkt = qpkIn->pData;   // remember the data packet
                nRemClients = 0;        // recount clients, one might have vanished
                // send a copy to all remotes
                for(k = 0; k < NMAXCLIENT; k+= 1)
                {
                    // is the thread active?  clean up!!
                    //kflag = (RemClients[k].hTCP == NULL) || (RemClients[k].threadState != THREADREADY);
                    if(RemClients[k].cmd)
                    {
                        kflag = RemClients[k].cmd(&(RemClients[k]), (APARAM)sockCmdGetThreadState, 0);
                        if(kflag == THREADREADY)
                            kflag = 1;
                        else
                            kflag = 0;
                    }
                    else
                        continue;

                    //======== Yep
                    if(kflag)          // test
                    {
                        t_ctlreqpkt* pTmpCtl;

                        nRemClients += 1;                               // inc the client count
                        pTmpCtl = AllocCtlPacket(NULL);                 // get a control packet
                        *pTmpCtl = *qpkIn;                              // duplicate it
                        qDpkt->iUsage += 1;                             // inc the usage
                        RemClients[k].wctlpkt(&(RemClients[k]), (APARAM)pTmpCtl, 0);
                    }
                }

                if((255 & qpkIn->from) == ROUTESRCEVB)  // from EVB?
                {
                    (*PostPktToLocal)(NULL,(APARAM)qpkIn,0);
                }

                //======== What do we do here
            #if 0
                if(qDpkt->iUsage <= 0)
                    DiscardCtlData(qpkIn);      // discard if not used
                else
                    ReleaseCtlPacket(qpkIn,NULL);
            #endif
                UnLockSection(&gRouterCS);

                // Write to the local port
            }
            break;
        }
    }

    // need to clean all outstanding resources threads etc.

    FlushCtlMail(&RouteInMail);

#if EASYWINDEBUG & 0
if(inicfg.prflags & PRFLG_ROUTER)
{
printf("DirectRouterGTK:Exiting\n");
}
#endif

    ThreadReturn(1);
}

#endif
