#ifndef MEMLOCK_H
#define MEMLOCK_H
/*
*==========================================================================
*
*   memlockW32.h
*   (c) 2012 jlp
*
*   defines for Linux memory and locking
*
*==========================================================================
*/
#include "jtypes.h"

//======== debug interface
#define UART_LINEEND_CR 1
#define UART_LINEEND_LF 2
#define UART_LINEEND_CRLF 3
struct S_inicfg
{
//  [MUX]
    int     muxmode;                            // muxmode (DIRECT|CLIENT|SERVER)
    int     SrvrWinMode;                        // autowindows in server
    int     EvbMode;                            // 1 EVB | 0 LOOP
    char    NameSrvr[96];                       // server name
    int     PortTCP;                            // server port
// [UART]
    int     port;                               // port (COM 1)
    int     brate;                              // bitrate (19200)
    int     flow;                               // flow (none|soft|hard)
    int     lineend;                            // line end from the TTY (<CR>)
    int     autolapj;                           // auto connect on spawn
// [VTTYWIN]
    int     TTYHeight;                          // default tty window height pixels
    int     TTYWidth;                           // default tty window width pixels
    int     maxwin;                             // maxwin: ignored, hard set to 8
//  [EVBSTR]
    char    GF80000[72];                        // start evbnf kernel
    char    GFLASH[72];                         // start evbnf kernel from flash
//  USERSTR
    char    userstr1[72];
    char    userstr2[72];
    char    userstr3[72];
    char    userstr4[72];
// [MISC]
    char    editfname[256];                     // file name to save edit
    char    DLfilename[256];                    // last downloaded filename
    char    ULfilename[256];                    // last uploaded filename
// [DEBUG]
    int     prflags;                            // prflags ?
};
extern struct S_inicfg inicfg;
#define PRFLG_TCP   0x0001
#define PRFLG_EVB   0x0002
#define PRFLG_ROUTER 0x0004
#define PRFLG_WINMUX 0x0008
#define PRFLG_VSP   0x0010
#define PRFLG_REGISTER 0x0020
#define PRFLG_OTHER 0x0080
#define PRFLG_LAPJ  0x0100




//======== Portability interface
typedef void* pMXTask;                           // General pointer (HANDLE)
typedef intptr_t APARAM;
typedef intptr_t BPARAM;
typedef struct S_jdevice* t_pJdevice;
typedef int (*pJfunction)( t_pJdevice,APARAM,BPARAM);     // format of most functions
//typedef int (*pJfunction)( void*,APARAM,BPARAM);     // format of most functions

typedef struct S_jdevice{                                 // Interface structure
    pMXTask     deviceId;                       // Device reference
    HWND        hDevice;                        // Handle to call with
    int         state;                          // For state oriented devices
    pJfunction   cmd;                            // issue an OOB cmmand
    pJfunction   setparam;                       // set a parameter
    pJfunction   getparam;                       // set a parameter
    pJfunction   callback;                       // Callback to parent
    pJfunction   wctlpkt;                        // write a t_ctlreqpkt
                                                // (reads are done by the framer)
    pJfunction   sigquit;                        // request a quit
    void*       pPriv;                          // per instance data
}t_jdevice;

typedef void (*pMsgArgs2)(void*, int, int);

#if USEWIN32
typedef CRITICAL_SECTION t_lock;
typedef HANDLE t_semaphore;
#endif

//======== Major elements/threads
extern t_jdevice MainThread;                    // Main
extern t_jdevice LocalThread;                   // Local Device (usually same as main)
extern t_jdevice EditThread;                    // is a window child
extern t_jdevice EvbThread;                      // Evb
extern t_jdevice TcpThread[];                    // TCP destinations

/* move these to the ini file */
#define MAXPACKETSIZE (768)
#define MAXDATASIZE (MAXPACKETSIZE-6-48)
#define MAXDATAOVER 2
#define MAXDATAPACKETS 128
#define MAXCTLPACKETS (MAXDATAPACKETS*8)
#define MAXROUTETAB 8
#define MAXTTL 15

// Misc special address/constants
#define SEQNUM_DEFAULT 0x20
#define WWANTACK 0x80

typedef struct S_ctldatapkt{                    // Internal packet data structure
    void*   pData;                              // pointer to the payload
    int     nData;                              // length of the data block
    char    iUsage;                             // usage counter >0 means still alive
    char    SrcId;                              // copy of the frame address
    char    seqNum;                             // copy of the sequnce nunmber
    char    ttl;                                // time to live
}t_ctldatapkt;

typedef struct S_dqueue
{
    struct S_dqueue* next;                      // next
    struct S_dqueue* prev;                      // previous
    void*   payload;                            // payload pointer
}t_dqueue;

typedef struct S_ctlreqpkt{
    t_dqueue    link;                           // dqueue for link
//    void*       pData;                          // data packet pointer
    t_ctldatapkt* pData;                        // data packet pointer
    U8          from;                           // ID of source for sort of flow control needed
    U8          tag;
    U16         tmsg;
}t_ctlreqpkt;

// tag definitions
enum {
    CTLREQMSGTAG_PDATAREQ  = 0,        // data packet
    CTLREQTAG_QUIT,                    // quit message
    CTLREQTAG_CBERR,                   // callback reports error
    CTLREQTAG_TOUT,                    // timeout
    CTLREQTAG_DONEWR,                  // callback write done
    CTLREQTAG_DONERD                   // callback read done
};

//======== typedefs for portability
#if USEGDKLOCKS
typedef GRecMutex t_lock;

typedef struct S_semaphore{
    int     count;
    GMutex  mx;
    GCond   cnd;
}t_semaphore;

typedef struct S_mailbox{
    t_dqueue link;
    GMutex  mx;
    GCond   cnd;
}t_mailbox;

typedef struct S_BMevent{
    GCond   e_cond;
    U32     e_pend;
    GMutex  e_mx;
}t_BMevent;
#define SPURIOUS_INTERRUPT (1<<31)
#endif

//======== Pthread locks
#if USEPLOCKS
typedef pthread_mutex t_lock

typedef struct S_semaphore{
    int     count;
    t_lock  mx;
    pthread_cond_t   cnd;
}t_semaphore;

typedef struct S_mailbox{
    t_dqueue link;
    t_lock  mx;
    pthread_cond_t   cnd;
}t_mailbox;

typedef struct S_BMevent{
    t_lock  e_mx;
    pthread_cond_t   e_cond;
    U32     e_pend;
}t_BMevent;

#endif

//======== WIN32 locks
#if USEWIN32
typedef struct S_mailbox{
    t_ctlreqpkt Hdr;                            // queue 0f t_ctlreqpkt
    HANDLE  Sem;                                // semaphore
    t_lock  lck;                                // lock
}t_mailbox;

typedef struct S_BMevent{
    HANDLE  e_cond;
    U32     e_pend;
}t_BMevent;
#endif

extern U32 BMEventWait(t_BMevent* pEv, U32 enab, U32 tsec);
extern void BMEventSet(t_BMevent* pEv, U32 ev);
extern void BMEventInit(t_BMevent* pEv);

//======== externs (probably should go in a different file)

extern t_lock gRouterCS;
extern t_lock* InitLockSection(t_lock* lock);
extern t_lock* FreeLockSection(t_lock* lock);
extern void LockSection(t_lock* lock);
extern void UnLockSection(t_lock* lock);
extern HANDLE SpawnThread(void (*Proc)(void*),int StkSize, void* Arg);
extern void ThreadReturn(int iresult);
extern void InitSemaphore(t_semaphore* pSem);
extern int SignalSemaphore(t_semaphore* pSem);
extern int WaitOnSemaphore(t_semaphore* pSem);
extern void InitCtlMailBox(t_mailbox* pMail);
extern t_ctlreqpkt* AcceptCtlMail(t_mailbox* pMail);
extern void ForwardCtlMail(t_mailbox* pMail, t_ctlreqpkt* pCtl);
extern void FlushCtlMail(t_mailbox* pMail);
#endif
