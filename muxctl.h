#ifndef MUXCTL_H
#define MUXCTL_H 1

#include "memlockW32.h"
/*
*==========================================================================
*
*   muxctl.h
*   (c) 2012 jlp
*
*   Include for packet data and routing
*
*==========================================================================
*/
#define structof(_strname,_elname,_elptr) ((_strname*)((char*)_elptr-offsetof(_strname,_elname)))


#define VSP_DATAPACKET (WM_USER+64)

#define MUXMODEDIRECT 0     /* Direct no network */
#define MUXMODECLIENT 1     /* Remote client */
#define MUXMODESERVER 2     /* Server */
#define MUXMODELOOP   3
#define MUXMODELOOPLW 3     /* Loop testing Input -> Local window */
#define MUXMODELOOPEV 3     /* Loop testing simulate EVB API for server */



#define ROUTESRCLOCAL 1     // from the local window
#define ROUTESRCEVB 2       // from the EVB
#define ROUTESRCTCP 3       // from the TCP remotes
#define ROUTESRCCMDS 4      // from internal command processor




typedef struct S_inportal{
    t_dqueue    list;                           // list header for t_ctlreqpkt's
    HANDLE      sem;                            // counting semaphore
}t_inportal;

typedef struct S_muxrouteinfo
{
    int     type;                               // type of port
    HANDLE  hThread;                            // handle to the thread
    t_ctlreqpkt  MuxRequestQ;                   // Header requests to the mux
    HANDLE  MuxReqSem;
    t_ctlreqpkt  ThrRequestQ;                   // Header requests to Thread
    HANDLE  ThrReqSem;

}t_muxrouteinfo;

#define TYPEMUXPORTNUL 0    /* None */
#define TYPEMUXPORTEVB 1    /* EVB */
#define TYPEMUXPORTWND 2    /* WINDOW */
#define TYPEMUXPORTTCP 3    /* TCP socket*/

// TID stuff
#define MAXTID 8
#define MAXTOTALWINS (MAXTID*2+2)
#define TID_UNFRAMED 24
#define TID_SRECORD (TID_UNFRAMED+WWANTACK)
#define TID_INTERNAL 0x22
#define TID_EVB 0x23
#define DEVVSP0 46
#define DEVVSP7 (DEVVSP0+7)
#define TID_XMODEMUP (DEVVSP0+8)
#define TID_MAXDEVVSP 16

// Internal messages
#define IMSG_BR (16385)

// moved out of socket
#define NMAXCLIENT 4
#define SOCKIDOFFSET 555
t_jdevice RemClients[10];                      // Remote array host
#define RemHost (RemClients[0])
//extern t_jdevice RemHost;


extern t_lock gRouterCS;
extern int OutstandingDataPkts;
extern int OutstandingCtlPkts;
extern HANDLE hRouter;                           // Handle to the thread
extern U32  dwRouterID;                          // ID set by windows
extern int     RouterActive;                     // flag for start sequencing
extern HANDLE RouteExitEvent;                    // exit event
//extern t_semaphore RouteInputSem;              // input semaphore for the router
//extern t_ctlreqpkt RouteInQHeader;             // header for the router input
extern t_mailbox   RouteInMail;


extern int InitMuxCtl(pJfunction packetToLocal,pJfunction msgToLocalToMain);

extern void InitPacketCtl(void);
extern int ReleaseDataPacket(t_ctldatapkt* ThePacket, t_lock* packetsection);
extern t_ctldatapkt* AllocDataPacket(int len, t_lock* DataSection);
extern int ReleaseCtlPacket(t_ctlreqpkt* ThePacket, t_lock* packetsection);
extern t_ctlreqpkt* AllocCtlPacket(t_lock* packetsection);
extern void DiscardCtlData(t_ctlreqpkt* pCpkt);
extern int enqueue(t_dqueue* hdrQ, t_dqueue* newPkt, t_lock* lock);
extern int initDqueue(t_dqueue* hdrQ, t_lock* lock);
extern t_dqueue* dequeue(t_dqueue* Pkt, t_lock* lock);

extern void  LoopBRouter(void* lpV);
extern void  DirectRouter(void* lpV);
extern void  ServerRouter(void* lpV);
extern void  ClientRouter(void* lpV);
#endif
